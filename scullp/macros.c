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