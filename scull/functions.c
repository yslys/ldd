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
 * allocate memory of size=size, type of memory = flags
 * @size: how many bytes of memory are required
 * @flags: the type of memory to allocate
 * @return: address of the start of the allocated memory
 */ 
void * kmalloc(size_t size, gfp_t flags);


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
 * @return: 0 if the lock was successfully acquired or %-EINTR if a signal arrived.
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

/**
 * #include <asm/uaccess.h>
 * used for address verification in user address space (kernel version < 5.0.0)
 * @type: either VERIFY_READ (reading the user-space memory area). 
 *        or VERIFY_WRITE (writing to user-space memory area).
 *        If both read and write are needed, use VERIFY_WRITE (superset of 
 *        VERIFY_READ).
 * @addr: user space address
 * @size: byte count.
 *        If ioctl() needs to read an integer from user space, then 
 *        size = sizeof(int). 
 *        
 * @return: 1 on success, 0 on failure
 * @inline: https://www.geeksforgeeks.org/inline-function-in-c/
 */
static inline int access_ok(int type, const void *addr, unsigned long size);

/**
 * #include <asm/uaccess.h>
 * used for address verification in user address space (kernel version >= 5.0.0)
 * @addr: user space address
 * @size: byte count.
 *        If ioctl() needs to read an integer from user space, then 
 *        size = sizeof(int). 
 * @return: 1 on success, 0 on failure
 * 
 * @inline: https://www.geeksforgeeks.org/inline-function-in-c/
 */
 static inline int access_ok(const void *addr, unsigned long size);

/**
 * get the minor number of the device represented by inode
 * #include <linux/fs.h>
 * @return: minor number of such device
 */ 
static inline unsigned iminor(const struct inode *inode)
{
	return MINOR(inode->i_rdev); // i_rdev - device represented by this inode
}

/**
 * decrement and test: atomically decrements @v by 1 and returns true if result
 * is 0, or false o.w.
 * @v: address of the value to be tested
 * @return: true if result == 0 after decrement, false o.w.
 */ 
bool atomic_dec_and_test(atomic_t *v);

/**
 * atomic increment function.
 * Read the 32-bit value (referred to as old) stored at location pointed by p. 
 * Compute (old + 1) and store result at location pointed by p. 
 * @return: old.
 */ 
int atomic_inc(volatile global(3clc) int *p);

/**
 * acquire a spinlock
 * #include <linux/spinlock.h>
 */ 
static __always_inline void spin_lock(spinlock_t *lock)
{
	raw_spin_lock(&lock->rlock);
}

/**
 * capable - Determine if the current task has a superior capability in effect
 * #include <kernel/capability.c>
 * @cap: The capability to be tested for
 *
 * @return: true if the current task has the given superior capability currently
 * available for use, false if not.
 *
 * This sets PF_SUPERPRIV on the task if the capability is available on the
 * assumption that it's about to be used.
 */
bool capable(int cap)
{
	return ns_capable(&init_user_ns, cap);
}


/**
 * wait_event_interruptible - sleep until a condition gets true
 * #include <linux/wait.h>
 * @wq_head: the waitqueue to wait on
 * @condition: a C expression for the event to wait for
 * @return: -ERESTARTSYS if it was interrupted by a signal 
 *          and 0 if @condition evaluated to true.
 * 
 * The process is put to sleep (TASK_INTERRUPTIBLE) until the
 * @condition evaluates to true or a signal is received.
 * The @condition is checked each time the waitqueue @wq_head is woken up.
 *
 * wake_up() has to be called after changing any variable that could
 * change the result of the wait condition.
 *
 * Blocks the current task on a wait queue until a CONDITION becomes true.

 * This is actually a macro. It repeatedly evaluates the CONDITION, which is a 
 * fragment of C code such as foo == bar or function() > 3. Once the condition 
 * is true, wait_event_interruptible returns 0. If the condition is false, the 
 * current task is added to the wait_queue_head_t list with state 
 * TASK_INTERRUPTIBLE; the current process will block until wake_up_all(&q) is 
 * called, then it will re-check the CONDITION. If the current task receives a 
 * signal before CONDITION becomes true, the macro returns -ERESTARTSYS.
 
 */
#define wait_event_interruptible(wq_head, condition)				\
({																	\
	int __ret = 0;													\
	might_sleep();													\
	if (!(condition))												\
		__ret = __wait_event_interruptible(wq_head, condition);		\
	__ret;															\
})

/**
 * wake_up_all
 * Wake up all tasks in the wait queue by setting their states to TASK_RUNNABLE.
 */
wake_up_all(); 

/**
 * Normally, a process that is awakened may preempt the current process and be
 * scheduled into the processor before wake_up returns. In other words, a call 
 * to wake_up may not be atomic. If the process calling wake_up is running in an
 * atomic context (it holds a spinlock, for example, or is an interrupt handler), 
 * this rescheduling does not happen. Normally, that protection is adequate. 
 * If, however, you need to explicitly ask to not be scheduled out of the 
 * processor at this time, you can use the “sync” variant of wake_up_interruptible. 
 * This function is most often used when the caller is about to reschedule anyway, 
 * and it is more efficient to simply finish what little work remains first.
 */ 
wake_up_interruptible_sync(wait_queue_head_t *queue);

/**
 * list_for_each_entry	-	iterate over list of given type
 * @pos:	the type * to use as a loop cursor.
 * @head:	the head for your list.
 * @member:	the name of the list_head within the struct.
 */
#define list_for_each_entry(pos, head, member)					\
	for (pos = list_first_entry(head, typeof(*pos), member);	\
	     !list_entry_is_head(pos, head, member);				\
	     pos = list_next_entry(pos, member))


/**
 * list_for_each_entry_safe - iterate over list of given type safe against removal of list entry
 * @pos:	the type * to use as a loop cursor.
 * @n:		another type * to use as temporary storage
 * @head:	the head for your list.
 * @member:	the name of the list_head within the struct.
 */
#define list_for_each_entry_safe(pos, n, head, member)			\
	for (pos = list_first_entry(head, typeof(*pos), member),	\
		n = list_next_entry(pos, member);						\
	     !list_entry_is_head(pos, head, member); 				\
	     pos = n, n = list_next_entry(n, member))

/**
 * current - current process
 * #include <asm/current.h>
 * 
 * Kernel code can refer to the current process by accessing the global item 
 * current, defined in <asm/current.h>, which yields a pointer to struct 
 * task_struct, defined by <linux/sched.h>.
 * The current pointer refers to the process that is currently executing. 
 * During the execution of a system call, such as open or read, the current 
 * process is the one that invoked the call. Kernel code can use process-specific 
 * information by using current, if it needs to do so. 
 */ 
current


/**
 * get the device number according to a tty_struct tty
 */ 
dev_t tty_devnum(struct tty_struct *tty)
{
	return MKDEV(tty->driver->major, tty->driver->minor_start) + tty->index;
}

/**
 * a macro that builds a dev_t data item from the major and minor numbers.
 */ 
dev_t MKDEV(unsigned int major, unsigned int minor);


/**
 * cdev_init() - initialize a cdev structure
 * #include <linux/cdev.h>
 * @cdev: the structure to initialize
 * @fops: the file_operations for this device
 *
 * Initializes @cdev, remembering @fops, making it ready to add to the
 * system with cdev_add().
 */
void cdev_init(struct cdev *cdev, const struct file_operations *fops)
{
	memset(cdev, 0, sizeof *cdev);
	INIT_LIST_HEAD(&cdev->list);
	kobject_init(&cdev->kobj, &ktype_cdev_default);
	cdev->ops = fops;
}

/**
 * kobject_set_name() - Set the name of a kobject.
 * #include <linux/kobject.h>
 * @kobj: struct kobject to set the name of
 * @fmt: format string used to build the name
 *
 * This sets the name of the kobject.  If you have already added the
 * kobject to the system, you must call kobject_rename() in order to
 * change the name of the kobject.
 */
int kobject_set_name(struct kobject *kobj, const char *fmt, ...)
{
	va_list vargs;
	int retval;

	va_start(vargs, fmt);
	retval = kobject_set_name_vargs(kobj, fmt, vargs);
	va_end(vargs);

	return retval;
}

/**
 * kobject_put() - Decrement refcount for object.
 * @kobj: object.
 *
 * Decrement the refcount, and if 0, call kobject_cleanup().
 */
void kobject_put(struct kobject *kobj)
{
	if (kobj) {
		if (!kobj->state_initialized)
			WARN(1, KERN_WARNING
				"kobject: '%s' (%p): is not initialized, yet kobject_put() is being called.\n",
			     kobject_name(kobj), kobj);
		kref_put(&kobj->kref, kobject_release);
	}
}

/**
 * cdev_add() - add a char device to the system
 * @p: the cdev structure for the device
 * @dev: the first device number for which this device is responsible
 * @count: the number of consecutive minor numbers corresponding to this
 *         device
 *
 * cdev_add() adds the device represented by @p to the system, making it
 * live immediately.  A negative error code is returned on failure.
 */
int cdev_add(struct cdev *p, dev_t dev, unsigned count)
{
	int error;

	p->dev = dev;
	p->count = count;

	if (WARN_ON(dev == WHITEOUT_DEV))
		return -EBUSY;

	error = kobj_map(cdev_map, dev, count, NULL,
			 exact_match, exact_lock, p);
	if (error)
		return error;

	kobject_get(p->kobj.parent);

	return 0;
}

/*
 * This is used by subsystems that don't want seekable
 * file descriptors. The function is not supposed to ever fail, the only
 * reason it returns an 'int' and not 'void' is so that it can be plugged
 * directly into file_operations structure.
 */
int nonseekable_open(struct inode *inode, struct file *filp)
{
	filp->f_mode &= ~(FMODE_LSEEK | FMODE_PREAD | FMODE_PWRITE);
	return 0;
}

/*
 * fasync_helper() is used by almost all character device drivers to set up 
 * the fasync queue, and for regular files by the file lease code. 
 * @return: negative on error, 0 if it did no changes,
 * 			positive if it added/deleted the entry.
 */
int fasync_helper(int fd, struct file * filp, int on, struct fasync_struct **fapp)
{
	if (!on)
		return fasync_remove_entry(filp, fapp);
	return fasync_add_entry(fd, filp, fapp);
}

/**
 * 
 * #include <linux/wait.h>
 */ 
#define DEFINE_WAIT(name) DEFINE_WAIT_FUNC(name, autoremove_wake_function)

int autoremove_wake_function(struct wait_queue_entry *wq_entry, unsigned mode, int sync, void *key)
{
	int ret = default_wake_function(wq_entry, mode, sync, key);

	if (ret)
		list_del_init_careful(&wq_entry->entry);

	return ret;
}