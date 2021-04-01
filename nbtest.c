/**
 * test for scull_pipe driver 
 * All it does is copy its input to its output, using nonblocking I/O and 
 * delaying between retries. The delay time is passed on the command line (1 sec 
 * by default)
 * nbtest.c: read and write in non-blocking mode
 * This should run with any Unix
 *
 * Copyright (C) 2001 Alessandro Rubini and Jonathan Corbet
 * Copyright (C) 2001 O'Reilly & Associates
 *
 * The source code in this file can be freely used, adapted,
 * and redistributed in source or binary form, so long as an
 * acknowledgment appears in derived source files.  The citation
 * should list that the code comes from the book "Linux Device
 * Drivers" by Alessandro Rubini and Jonathan Corbet, published
 * by O'Reilly & Associates.   No warranty is attached;
 * we cannot take responsibility for errors or fitness for use.
 */

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <errno.h>

char buffer[4096];

int main(int argc, char **argv)
{
    int delay = 1, n, m = 0;

    // The delay time is passed on the command line
    if(argc > 1){
        delay = atoi(argv[1]);
    }

    /** 
     * F_SETFL(int) - Set the file status flags to the value specified by arg.
     * F_GETFL(void) - Return the file access mode and the file status flags;
     */
    fcntl(0, F_SETFL, fcntl(0, F_GETFL) | O_NONBLOCK); // set stdin to Non-block
    fcntl(1, F_SETFL, fcntl(1, F_GETFL) | O_NONBLOCK); // Set stdout to Non-block

    while(1){
        // read what's stored in device to buffer
        n = read(0, buffer, 4096);

        // write what has been read to buffer -> the device
        if(n >= 0){
            m = write(1, buffer, n);
        }

        // if read fails or write fails, and errno is not try-again
        if((n < 0 || m < 0) && (errno != EAGAIN)){
            break;
        }

        // sleep for delay seconds, in this case, is 1 sec.
        sleep(delay);
    }

    perror(n < 0 ? "stdin" : "stdout");
    exit(1);
}