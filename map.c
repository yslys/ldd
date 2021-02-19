#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#define SEG_ADDR 0x43c00000
#define SEG_SIZE 0x10000

int main(int argc, char **argv)
{
  // use the dev/mem dev driver that's build into linux
  int fd = open("/dev/mem", O_RDWR);
  if(fd < 0){
    perror("Could not open /dev/mem: \n");
    exit(0);
  }

  /* 
    void *mmap(void *addr, size_t length, int prot, int flags,
               int fd, off_t offset);
    
    usage:
      mmap creates a new mapping in the virtual addr space of the calling proc
      the contents of the file mapping are initialized using length bytes starting
      at offset in the file referred to by fd.
      
    params:
      void *addr - starting addr for the new mapping
             if addr == NULL:
                kernel chooses page-aligned addr at which to create the mapping
                the most portable method of creating a new mapping
             if addr != NULL:
                kernel takes addr as a hint about where to place the mapping;
                kernel will pick a nearby page boundary (always above or equal 
                to the value specified by /proc/sys/vm/mmap_min_addr) and attempt
                to create a mapping there

      size_t length - the length of the mapping (must be > 0)
      
      int prot - the desired memory protection of the mapping
             must NOT conflict with the open mode of the file
             PROT_READ: pages may be read
             PROT_WRITE: pages may be written
      
      int flags - determines whether "updates to the mapping" are visible to other
                  processes mapping the same region, and whether updates are 
                  carried through to the underlying file.
             MAP_SHARED: updates to the mapping are visible to other proceses
                         mapping the same region.
      
      fd - 

      offset - 
      offset must be a multiple
       of the page size as returned by sysconf(_SC_PAGE_SIZE)
    return:
      the address of the new mapping
  
  */
  uint32_t *base = mmap(0, getpagesize(), PROT_READ | PROT_WRITE, 
                        MAP_SHARED, fd, SEG_ADDR);
  if(base == MAP_FAILED){

  }
}