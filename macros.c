/**
 * This file consists of macros that are used in device drivers
 */ 

/**
 * gets the magic number of the device that cmd targets
 * @cmd: command
 */
_IOC_TYPE(cmd)

/**
 * gets the sequential number of the command within your device
 */ 
_IOC_NR(cmd)

/**
 * gets the size of the data structure
 */ 
_IOC_SIZE(cmd)

/**
 * gets the direction of data transfer, can be one of the following:
 * _IOC_NONE, _IOC_READ, _IOC_WRITE, _IOC_READ, _IOC_WRITE
 */ 
_IOC_DIR(cmd)

/**
 * direction of data transfer.
 * viewed from the user application
 */ 
_IOC_NONE // no data transfer
_IOC_READ // reading from the device (driver writing to user space)
_IOC_WRITE // writing to the device
_IOC_READ | _IOC_WRITE // data is transferred in both ways

/**
 * everyone can read (RUGO - R:read, UGO:user, group, others)
 */ 
S_IRUGO

/**
 * user can write (WUSR - W:write, USR:user)
 */ 
S_IWUSR

/**
 * __get_user: - Get a simple variable from user space, with less checking.
 * @x:   Variable to store result.
 * @ptr: Source address, in user space.
 *
 * Context: User context only.  This function may sleep.
 *
 * This macro copies a single simple variable from user space to kernel
 * space.  It supports simple types like char and int, but not larger
 * data types like structures or arrays.
 *
 * @ptr must have pointer-to-simple-variable type, and the result of
 * dereferencing @ptr must be assignable to @x without a cast.
 *
 * Caller must check the pointer with access_ok() before calling this
 * function.
 *
 * Returns 0 on success, or -EFAULT on error.
 * On error, the variable @x is set to zero.
 */
#define __get_user(x, ptr)

/**
 * __put_user: - Write a simple value into user space, with less checking.
 * @x:   Value to copy to user space.
 * @ptr: Destination address, in user space.
 *
 * Context: User context only.  This function may sleep.
 *
 * This macro copies a single simple value from kernel space to user
 * space.  It supports simple types like char and int, but not larger
 * data types like structures or arrays.
 *
 * @ptr must have pointer-to-simple-variable type, and @x must be assignable
 * to the result of dereferencing @ptr.
 *
 * Caller must check the pointer with access_ok() before calling this
 * function.
 *
 * Returns 0 on success, or -EFAULT on error.
 */
#define __put_user(x, ptr)


/**
 * Set the atomic variable v to the integer value i. You can also initialize 
 * atomic values at compile time with the ATOMIC_INIT macro.
 */ 
void atomic_set(atomic_t *v, int i);
atomic_t v = ATOMIC_INIT(0); // initialize atomic value at compile time


/**
 * Bypass file read, write, and execute permission checks.
 *             (DAC is an abbreviation of "discretionary access
 *             control".)
 * @link: https://man7.org/linux/man-pages/man7/capabilities.7.html
 */ 
CAP_DAC_OVERRIDE


/**
 * Explicitly nonblocking I/O is indicated by the O_NONBLOCK flag in filp->f_flags. 
 * The flag is defined in <linux/fcntl.h>, which is automatically included by 
 * <linux/fs.h>. The flag gets its name from “open-nonblock,” because it can be 
 * specified at open time (and originally could be specified only there). 
 * If you browse the source code, you find some references to an O_NDELAY flag; 
 * this is an alternate name for O_NONBLOCK, accepted for compatibility with 
 * System V code. 
 * The flag is cleared by default, because the normal behavior of a process 
 * waiting for data is just to sleep. 
 * 
 * Neither the open() nor any subsequent I/O operations on the file descriptor 
 * which is returned will cause the calling process to wait.
 */ 
O_NONBLOCK

/**
 * builds a dev_t data item from the major and minor numbers.
 */ 
dev_t MKDEV(unsigned int major, unsigned int minor);

/**
 * everyone can read (RUGO - R:read, UGO:user, group, others)
 */ 
S_IRUGO

/**
 * user can write (WUSR - W:write, USR:user)
 */ 
S_IWUSR

/**
 * create a struct wait_queue_entry, with its name = name.
 * #include <linux/wait.h>
 * 
 * This is equivalent to the following:
 * 		wait_queue_t my_wait;
 * 		init_wait(&my_wait);
 */ 
DEFINE_WAIT(name);

#define DEFINE_WAIT(name) DEFINE_WAIT_FUNC(name, autoremove_wake_function)

int autoremove_wake_function(struct wait_queue_entry *wq_entry, unsigned mode, int sync, void *key)
{
	int ret = default_wake_function(wq_entry, mode, sync, key);

	if (ret)
		list_del_init_careful(&wq_entry->entry);

	return ret;
}

/**
 * @brief flags used to indicate the possible operations
 * #include <linux/poll.h>
 * used in poll_wait()
 * 
 * more info, see ldd3 p.164
 */
POLLIN     // this bit must be set if the device can be read without blocking
POLLRDNORM // This bit must be set if “normal” data is available for reading. 
           // A readable device returns (POLLIN | POLLRDNORM).
POLLOUT    // This bit is set in the return value if the device can be written 
		   // to without blocking.
POLLWRNORM // This bit has the same meaning as POLLOUT, and sometimes it actually 
           // is the same number. A writable device returns (POLLOUT | POLLWRNORM).
POLLPRI    // High-priority data (out-of-band) can be read without blocking. 
           // This bit causes select() to report that an exception condition 
		   // occurred on the file, because select reports out-of-band data as 
		   // an exception condition.
POLLHUP    // When a process reading this device sees end-of-file, the driver 
		   // must set POLLHUP (hang-up). 
POLLERR    // An error condition has occurred on the device. 

/*
 * add the name of the func to the kernel symbol table so that other kernel modules could
 * use the func.
 */
EXPORT_MODULE(func_name);
