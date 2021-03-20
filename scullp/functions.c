/**
 * module_param is a macro that defines module parameters, defined in moduleparam.h
 * @name: name of the variable
 * @type: type of the variable
 * @perm: permissions mask to be used for an accompanying sysfs entry
 *        If perm is set to 0, there is no sysfs entry at all; otherwise, it 
 *        appears under /sys/module with the given set of permissions.
 * 
 * e.g. 
 * loading the hellop module (p. 36) using following command line:
 *      $ insmod hellop howmany=10 whom="Mom"
 * Upon being loaded that way, hellop would say “Hello, Mom” 10 times.
 * However, before insmod can change module parameters, the module must make
 * them available. The module_param macro should be placed outside of any 
 * function and is typically found near the head of the source file. So hellop 
 * would declare its parameters and make them available to insmod as follows:
 *      static char *whom = "world";
 *      static int howmany = 1;
 *      module_param(howmany, int, S_IRUGO);
 *      module_param(whom, charp, S_IRUGO);
 */
module_param(name, type, perm);

/**
 * container_of(pointer, container_type, container_field);
 * (linux/kernel.h)
 * container_of - cast a member of a structure out to the containing structure
 * @pointer: a pointer to a field of type container_field(3rd param)
 * @container_type: the type of the struct that contains the container_field
 * @container_field: type of the field pointed to by the pointer
 * @return: a pointer to the containing structure
 *
 * In this case, given the inode, take its field - inode->i_cdev (of type cdev)
 * and specify its container's type - struct scull_dev, then the return value
 * is the pointer to the container, i.e. pointer to the struct scull_dev.
 */
container_of(pointer, container_type, container_field);

/**
 * #include <sys/mman.h>
 * mmap() creates a new  mapping in the virtual address space of the calling 
 * process. The contents of a file mapping are initialized using length bytes 
 * starting at offset in the file referred to by the file descriptor fd.  
 * offset must be a multiple of the page size as returned by sysconf(_SC_PAGE_SIZE).
 * 
 * @addr: starting address for the new mapping
 *          if addr == NULL, kernel chooses the address at which to create the 
 *              mapping;
 *          if addr != NULL, kernel takes it as a hint about where to place the 
 *              mapping;
 * @length: the length of the mapping
 * @prot: the desired memory protection of the mapping (must not conflict with 
 *        the open mode of the file). It is either PROT_NONE or the bitwise OR 
 *        of one or more of the following flags:
 *          - PROT_EXEC  Pages may be executed.
 *          - PROT_READ  Pages may be read.
 *          - PROT_WRITE Pages may be written.
 *          - PROT_NONE  Pages may not be accessed.
 * @flags: determines whether updates to the mapping are visible to other
 *         processes mapping the same region, and whether updates are carried 
 *         through to the underlying file.
 * @fd: file descriptor of the file to be mapped
 * @offset: offset in the file (fd)   
 * @return: address of the new mapping
 */
void *mmap(void *addr, size_t length, int prot, int flags,
           int fd, off_t offset);

/**
 * #include <stdio.h>
 * @return: the file descriptor associated with the stream pointed to by stream
 */
int fileno(FILE *stream);

/**
 * #include <linux/uaccess.h>
 * copy "n" bytes of data starting from "from" in kernel space, to "to" in user space 
 * @return: number of bytes that could not be copied. (0 on success)
 */
unsigned long copy_to_user(void __user *to, const void *from, unsigned long n);

/**
 * #include <linux/mm.h>
 * To allocate entire pages (or multiple pages) at once
 * @gfp_mask: normally set to GFP_KERNEL, GFP_ATOMIC or GFP_DMA
 * @order: the number of pages in the power of 2
 * @return: the base address of a page in kernel space
 * 
 * @link: http://www.cs.otago.ac.nz/cosc440/lab07.php
 */
unsigned long get_zeroed_page(gfp_t gfp_mask);
unsigned long __get_free_page(gfp_t gfp_mask);
unsigned long __get_free_pages(gfp_t gfp_mask, unsigned long order);

/**
 * #include <linux/mm.h>
 * To free entire pages (or multiple pages) at once
 * 
 * @link: http://www.cs.otago.ac.nz/cosc440/lab07.php
 */
void free_page(unsigned long addr);
void free_pages(unsigned long addr, unsigned long order);

/**
 * allocate an initialized cdev structure.
 * embed the cdev structure within a device-specific structure of your own.
 * struct cdev has an owner filed that should be set to THIS_MODULE
 */
void cdev_init(struct cdev *dev, struct file_operations *fops);

/**
 * Once the cdev structure is set up, the final step is to tell the kernel about it
 * @dev: cdev structure
 * @num: the first device number to which this device responds
 * @count: the number of device numbers
 * @return: negative error code if the device has not been added to the system
 *
 * @note: on succeed, your device is "live" and its operations can be called by
 *          the kernel
 *        do NOT call cdev_add until your driver is completely ready to handle
 *          operations on device
 */
int cdev_add(struct cdev *dev, dev_t num, unsigned int count);

/**
 * Acquire the mutex that can be interrupted by signals.
 * @lock: The mutex to be acquired.
 *
 * Lock the mutex like mutex_lock(). If a signal is delivered while the
 * process is sleeping, this function will return without acquiring the mutex.
 *
 * Context: Process context.
 * Return: 0 if the lock was successfully acquired or %-EINTR if a signal arrived.
 */
int __sched mutex_lock_interruptible(struct mutex *lock);


/**
 * register a range of device numbers (the major number must be provided)
 * @from: the first in the desired range of device numbers.
 * @count: the number of consecutive device numbers required.
 * @name: the name of the device or driver.
 * @return: 0 on success, negative error code on failure.
 */
int register_chrdev_region(dev_t from, unsigned count, const char *name);

/**
 * dynamically register a range of device numbers, major number chosen dynamically
 * @dev: output parameter for first assigned number
 * @baseminor: first of the requested range of minor numbers
 * @count: the number of minor numbers required
 * @name: the name of the associated device or driver
 * @return: 0 on success, negative error code on failure.
 */
int alloc_chrdev_region(dev_t *dev, unsigned baseminor,
 	                    unsigned count, const char *name);

/**
 * initialize the mutex to unlocked state.
 * it's not allowed to initialize an already locked mutex.
 * @mutex: the mutex to be initialized
 */
mutex_init(mutex); 

/**
 * remove a character device from the system, possibly freeing the structure itself
 * @p: ptr to the char device
 */ 
void cdev_del(struct cdev *p);

/**
 * #include <linux/fs.h>
 * unregister a range of device numbers
 * @from: the first in the range of numbers to unregister
 * @count: the number of device numbers to unregister
 *
 * This function will unregister a range of @count device numbers,
 * starting with @from.  The caller should normally be the one who
 * allocated those numbers in the first place...
 */
void unregister_chrdev_region(dev_t from, unsigned count);