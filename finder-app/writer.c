/* 
Bhumika Sood
Write a file a tthe path specified with a string.
*/

#include <fcntl.h>
#include <syslog.h>
#include <stdio.h> 
#include <string.h>
#include <errno.h>
#include <unistd.h>


int main(int argc, char *argv[])
{
    // Open log
    openlog("writer", LOG_PID, LOG_USER);

    // Check number of command line arguments
    if (argc != 3)
    {
        syslog(LOG_ERR, "Invalid number of arguments: %d. Total number of arguments should be 2.", argc);
        closelog();
        return 1;
    }

    // Create or open file at path specified
    int writeFile = creat(argv[1], 0644);
    if (writeFile == -1) 
    {
        syslog(LOG_ERR, "Could not write file: %s", argv[1]);
        closelog();
        return 1;
    }

    // Write string to file
    ssize_t bytesWritten = write(writeFile, argv[2], strlen(argv[2]));
    if (bytesWritten != strlen(argv[2]))
    {
        syslog(LOG_ERR, "Could not write string to file.");
        close(writeFile);
        closelog();
        return 1;
    }

    syslog(LOG_DEBUG, "Writing %s to %s", argv[2], argv[1]);
    closelog();
    return 0;
}