/**
 * mapper.c is a simple tool that can be used to quickly test the mmap syscall.
 * It maps read-only parts of a file specified by command-line options and dumps
 * the mapped region to standard output.

/*
 * mapper.c -- simple file that mmap()s a file region and prints it
 *
 * Copyright (C) 1998,2000,2001 Alessandro Rubini
 * 
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA.
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <errno.h>
#include <limits.h>

int main(int argc, char **argv)
{
    char *fname; // filename
    FILE *f; 
    unsigned long offset, len;
    void *address;

    // if (scan argv[2] and store to offset, the number of input items assigned is not 1)
    if (argc != 4
            || sscanf(argv[2], "%li", &offset) != 1 // %li for signed long integers
            || sscanf(argv[3], "%li", &len) != 1) {
        fprintf(stderr, "%s: Usage \"%s <file> <offset> <len>\"\n", argv[0],
                argv[0]);
        exit(1);
    }

    // if the offset is larger than max (e.g. PCI devices), conversion trims it
    if(offset == INT_MAX){
        if(argv[2][1] == 'x'){
            sscanf(argv[2]+2, "%lx", &offset);//////////////////////////confused
        }
        else{
            sscanf(argv[2], "%lu", &offset);
        }
    }

    fname = argv[1];

    // if open failed
    if(!(f=fopen(fname, "r"))){
        fprintf(stderr, "%s: %s: %s\n", argv[0], fname, strerror(errno));
        exit(1);
    }

    // otherwise, open succeeded, do the mmap
    /**
     * Basically, it creates a new mapping in the virtual addr space of the 
     * calling process. 
     */ 
    address = mmap(0, len, PROT_READ, MAP_FILE | MAP_PRIVATE, fileno(f), offset);

    // if the return value of mmap is -1
    // need to convert it to void *
    if(address == (void *)-1) {
        fprintf(stderr,"%s: mmap(): %s\n",argv[0],strerror(errno));
        exit(1);
    }

    fclose(f);
    fprintf(stderr, "mapped \"%s\" from %lu (0x%08lx) to %lu (0x%08lx)\n",
            fname, offset, offset, offset+len, offset+len);

    fwrite(address, 1, len, stdout);

    return 0;
}