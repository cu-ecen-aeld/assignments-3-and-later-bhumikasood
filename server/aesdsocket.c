// Bhumika Sood
// Create simple server. Receive messages and write to /var/tmp/aesdsocketdata file.
// Send file contents back over connection.

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h> 
#include <string.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

#ifndef USE_AESD_CHAR_DEVICE
#define USE_AESD_CHAR_DEVICE 1
#endif

#if USE_AESD_CHAR_DEVICE
#define OUTFILE "/dev/aesdchar"
#else
#define OUTFILE "/var/tmp/aesdsocketdata"
#endif

#define PORT    "9000"
#define BACKLOG 10
#define BUFFER_SIZE 1024

// Node structure for connection thread
typedef struct ThreadNode {
    pthread_t thread;
    int clientfd;
    char clientIP[16];
    int complete;
    SLIST_ENTRY(ThreadNode) entries;
} ThreadNode;

// Define head of linked list
SLIST_HEAD(ThreadHead, ThreadNode) connectionList;

// Declare socket file descriptors
int sockfd = -1;

// Declare file pointer
FILE *outfile;

// Mutex for outfile access
pthread_mutex_t outfileMutex = PTHREAD_MUTEX_INITIALIZER;

// Signal flag
static volatile sig_atomic_t caughtSignal = 0;

// Handle SIGINT or SIGTERM signals
static void signalHandler(int signalNumber)
{   
    // Set signal flag high
    syslog(LOG_INFO, "Caught signal");
    caughtSignal = 1;

    // Close server socket
    if (sockfd != -1)
    {
        syslog(LOG_DEBUG, "Shutting down socket");
        shutdown(sockfd, SHUT_RDWR);
        close(sockfd);
        sockfd = -1;
    }
}

static void exitRoutine()
{
    syslog(LOG_DEBUG, "Exiting safely!");

    // Parse linked list
    ThreadNode *currentNode;
    while (!SLIST_EMPTY(&connectionList))
    {
        currentNode = SLIST_FIRST(&connectionList);
        SLIST_REMOVE_HEAD(&connectionList, entries);

        if (currentNode->clientfd != -1)
        {
            shutdown(currentNode->clientfd, SHUT_RDWR);
            close(currentNode->clientfd);
        }

        pthread_join(currentNode->thread, NULL);
        free(currentNode);
    }

    pthread_mutex_destroy(&outfileMutex);
    remove(OUTFILE); 
}

// Create daemon if command line argument is specified
int createDaemon()
{
    // Create new process
    pid_t pid = fork();
    if (pid == -1)
    {
        return -1;
    }
    else if (pid > 0)
    {
        exit(EXIT_SUCCESS);
    }

    // Create new session and process group
    if (setsid() == -1)
    {
        return -1;
    }

    // Change working directory to root
    if (chdir("/") == -1)
    {
        return -1;
    }

    // Close standard file descriptors 
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    // Redirect standard I/O streams to /dev/null
    open("/dev/null", O_RDWR);
    dup(0);
    dup(0);

    return 0;
}

// Write timestamp to outfile every 10 seconds
void *timerTask(void *arg)
{
    while (!caughtSignal)
    {
        // Sleep for 10 seconds
        for (int i = 0; i < 10 && !caughtSignal; i++)
        {
            sleep(1);
        }

        // Get system time
        time_t currentTime = time(NULL);
        struct tm *timeInfo = localtime(&currentTime);
        
        // Format system time
        char timeValue[128]; 
        strftime(timeValue, sizeof(timeValue), "%a, %d %b %Y %H:%M:%S %z", timeInfo);

        // Grab file mutex and write time to file
        pthread_mutex_lock(&outfileMutex);
        FILE *outfile = fopen(OUTFILE, "a");

        if (outfile != NULL)
        {
            fprintf(outfile, "timestamp:%s\n", timeValue);
            fclose(outfile);
        }
        
        // Release mutex
        pthread_mutex_unlock(&outfileMutex);
    }

    return NULL;
}

// Handle incoming client connections and write packets to file
void *clientTask(void *arg)
{
    // Read node from linked list
    ThreadNode *node = (ThreadNode*) arg;

    // Read incoming data from server and write to file
    size_t bufferSize = BUFFER_SIZE;
    size_t bytesRead, packetLength = 0;
    char *buffer = malloc(bufferSize);

    while (!caughtSignal && (bytesRead = recv(node->clientfd, buffer + packetLength, BUFFER_SIZE, 0)) > 0)
    {
        // Increment packet length by actual number of bytes read
        packetLength += bytesRead;

        // Process packet to file upon receipt of new line character
        if (packetLength > 0 && buffer[packetLength - 1] == '\n')
        {
            // Grab file mutex and open outfile
            pthread_mutex_lock(&outfileMutex);
            FILE *outfile = fopen(OUTFILE, "a+");

            if (outfile != NULL)
            {
                // Write packet to outfile
                fwrite(buffer, sizeof(char), packetLength, outfile);

                // Move file pointer to beginning of outfile
                fseek(outfile, 0, SEEK_SET);

                // Read from file to write buffer and second to client
                char writeBuffer[BUFFER_SIZE];
                size_t bytesToSend = 0;
                while ((bytesToSend = fread(writeBuffer, sizeof(char), BUFFER_SIZE, outfile)) > 0)
                {
                    send(node->clientfd, writeBuffer, bytesToSend, 0);
                }
                
                // Close outfile
                fclose(outfile);
            }
            // Release mutex
            pthread_mutex_unlock(&outfileMutex);

            // 3. Clear our tracking parameters completely to start fresh for the next line
            packetLength = 0;
            bufferSize = BUFFER_SIZE;
            char *resetBuffer = realloc(buffer, bufferSize);
            buffer = resetBuffer;
            memset(buffer, 0, bufferSize);
        }
        else
        {
            // Realloc buffer if end of packet not received
            bufferSize += BUFFER_SIZE;
            char *tempBuffer = realloc(buffer, bufferSize);
            buffer = tempBuffer;
        }

    }

    // Close connection with client
    close(node->clientfd);
    syslog(LOG_INFO, "Closed connection from %s", node->clientIP);
    node->complete = 1;

    // Free allocated memory
    free(buffer);

    return NULL;
}

int main(int argc, char* argv[])
{   
    // Set daemon flag
    int daemonSpecified = 0;
    if (argc > 1)
    {
        if(strcmp(argv[1], "-d") == 0)
        {
            daemonSpecified = 1;
        }
    }

    // Create signal action
    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_handler = signalHandler;
    sigaction(SIGINT, &action, NULL);
    sigaction(SIGTERM, &action, NULL);

    // Set up socket address info
    struct addrinfo hints, *server;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    int addrResult = getaddrinfo(NULL, PORT, &hints, &server);
    if (addrResult != 0)
    {
        perror("Failed to get socket address information");
        return -1;
    }

    // Create socket
    sockfd = socket(server->ai_family, server->ai_socktype, server->ai_protocol);
    if (sockfd == -1)
    {
        perror("Failed to create socket");
        freeaddrinfo(server);
        return -1;
    }

    // Bind socket to port
    int bindResult = bind(sockfd, server->ai_addr, server->ai_addrlen);
    if (bindResult == -1)
    {
        perror("Failed to bind to port");
        close(sockfd);
        return -1;
    }

    // Free address info
    freeaddrinfo(server);

    // Spawn daemon
    if (daemonSpecified)
    {
        createDaemon();
    }

    // Listen for connection on socket
    int listenResult = listen(sockfd, BACKLOG);
    if (listenResult == -1)
    {
        perror("Failed to listen for connections on socket");
        close(sockfd);
        return -1;
    }

    // Initialize the linked list
    SLIST_INIT(&connectionList);

#if !USE_AESD_CHAR_DEVICE
    // Start timer task
    pthread_t timerThread;
    if (pthread_create(&timerThread, NULL, timerTask, NULL) != 0)
    {
        perror("Failed to create timer task");
        close(sockfd);
        return -1;
    }
#endif
    // Continuously accept connections until signal is received
    while (!caughtSignal)
    {
        // Accept incoming connection
        struct sockaddr_storage client;
        socklen_t addrLength = sizeof(client);
        int incomingClient = accept(sockfd, (struct sockaddr*)&client, &addrLength);
        // Store IP address of incoming connection
        char incomingClientIP[16];
        struct sockaddr_in *clientAddr = (struct sockaddr_in *)&client;
        inet_ntop(AF_INET, &clientAddr->sin_addr, incomingClientIP, sizeof(incomingClientIP));
        if (incomingClient == -1)
        {
            if (caughtSignal)
            {
                break;
            }
            perror("Failed to accept connection");
            continue;
        }
        else
        {
            syslog(LOG_INFO, "Accepted connection from %s", incomingClientIP);
        }

        // Create node for new connection
        ThreadNode *newNode = malloc(sizeof(ThreadNode));
        newNode->clientfd = incomingClient;
        strcpy(newNode->clientIP, incomingClientIP);
        newNode->complete = 0;

        // Start thread for new node
        if (pthread_create(&newNode->thread, NULL, clientTask, newNode) != 0)
        {
            perror("Failed to create client task");
            close(incomingClient);
            free(newNode);
            continue;
        }

        // Add node to linked list
        SLIST_INSERT_HEAD(&connectionList, newNode, entries);

        // Manage linked list
        ThreadNode *currentNode;
        ThreadNode *completeNode = NULL;

        SLIST_FOREACH(currentNode, &connectionList, entries)
        {
            // Check if current node is complete
            if (currentNode->complete)
            {
                completeNode = currentNode;
                break;
            }
        }

        // Remove completed node from list and join thread
        if (completeNode != NULL)
        {
            SLIST_REMOVE(&connectionList, completeNode, ThreadNode, entries);
            pthread_join(completeNode->thread, NULL);
            free(completeNode);
        }
    }

#if !USE_AESD_CHAR_DEVICE
    pthread_join(timerThread, NULL);
    unlink(OUTFILE);
#endif

    // Run exit routine
    exitRoutine();
    return 0;
}