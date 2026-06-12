// Bhumika Sood
// Create simple server. Receive messages and write to /var/tmp/aesdsocketdata file.
// Send file contents back over connection.

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h> 
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <syslog.h>
#include <unistd.h>

#define PORT    "9000"
#define BACKLOG 10
#define OUTFILE "/var/tmp/aesdsocketdata.txt"

// Declare socket and client file descriptors
int sockfd, clientfd = -1;

// Declare file pointer
FILE *outfile;

// Handle SIGINT or SIGTERM signals
static void signalHandler(int signalNumber)
{   
    syslog(LOG_DEBUG, "Caught signal, exiting");

    // Close client socket
    if (clientfd != -1)
    {
        shutdown(clientfd, SHUT_RDWR);
        close(clientfd);
    }

    // Close server socket
    if (sockfd != -1)
    {
        shutdown(sockfd, SHUT_RDWR);
        close(sockfd);
    }

    // Close outfile and delete from filesystem
    if (outfile != NULL)
    {
        fclose(outfile);
    }
    unlink(OUTFILE);

    exit(0);
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
        freeaddrinfo(server);
        return -1;
    }

    // Open output file 
    outfile = fopen(OUTFILE, "a+");
    if (outfile == NULL)
    {
        perror("Failed to open output file");
        close(sockfd);
        return -1;
    }

    // Continuously accept connections until signal is received
    while (1)
    {
        // Accept incoming connection
        struct sockaddr_storage client;
        socklen_t addrLength = sizeof(client);
        clientfd = accept(sockfd, (struct sockaddr*)&client, &addrLength);
        // Store IP address of incoming connection
        char clientIP[16];
        struct sockaddr_in *clientAddr = (struct sockaddr_in *)&client;
        inet_ntop(AF_INET, &clientAddr->sin_addr, clientIP, sizeof(clientIP));
        if (clientfd == -1)
        {
            perror("Failed to accept connection");
            continue;
        }
        else
        {
            syslog(LOG_INFO, "Accepted connection from %s", clientIP);
        }

        // Read incoming data from server and write to file
        size_t bufferSize = 1024;
        size_t bytesRead = 0;
        char buffer[bufferSize];
        memset(&buffer, 0, bufferSize);

        while ((bytesRead = recv(clientfd, buffer, bufferSize, 0)) > 0)
        {
            // Write 1024 bytes read from input buffer to file
            fwrite(buffer, sizeof(char), bytesRead, outfile);

            // Send outfile over connection when last packet is received
            if (bytesRead < bufferSize)
            {
                // Move file pointer to beginning of outfile
                fseek(outfile, 0, SEEK_SET);

                // Clear buffer
                memset(buffer, 0, bufferSize);

                // Send outfile to client
                while ((bytesRead = fread(buffer, sizeof(char), bufferSize, outfile)) != 0)
                {
                    send(clientfd, buffer, bytesRead, 0);
                }
            }
        }
        // Close connection with client
        close(clientfd);
        syslog(LOG_INFO, "Closed connection from %s", clientIP);
    }

    // Close sockets
    close(sockfd);

    // Close outfile
    fclose(outfile);
}