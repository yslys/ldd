# Chap 6 - Advanced Char Driver Operations
### Goal:
1. implement functionality more than synchronous read and write
2. handle concurrency issues
3. implement ```ioctl``` syscall - a common interface used for device control
4. synchronizing with user space
5. know how to put processes to sleep & to wake them up
6. implement nonblocking I/O
7. inform user space when your devices are available for reading or writing
8. implement a few different device access policies within drivers

### ioctl background
Most drivers need i) ability to read/write device and ii) perform various types of hardware control via device driver. User space must be able to request:
    1. device lock its door
    2. eject its media
    3. report error info
    4. change a baud rate
    5. self destruct
    6. etc.
The above operations are usually supported via ```ioctl``` method. In user space, it has the prototype of:
```
int ioctl(int fd, unsigned long cmd, ...);

/*
 * The ioctl() system call manipulates the underlying device
       parameters of special files.  In particular, many operating
       characteristics of character special files (e.g., terminals) may
       be controlled with ioctl() requests.  The argument fd must be an
       open file descriptor.

       The second argument is a device-dependent request code.  The
       third argument is an untyped pointer to memory.  It's
       traditionally char *argp (from the days before void * was valid
       C), and will be so named for this discussion.

       An ioctl() request has encoded in it whether the argument is an
       in parameter or out parameter, and the size of the argument argp
       in bytes.  Macros and defines used in specifying an ioctl()
       request are located in the file <sys/ioctl.h>.
 */
```
Since a system call cannot have a variable number of arguments, the dots in the prototype represents a single **optional argument** - ```char *argp```. Such dots are used to prevent type checking during compilation.

The third argument depends on the **specific control command (2nd argument)** being issued. Such commands may take no arguments, or an integer value, or a pointer to other data. Using a pointer is the way to pass arbitrary data to the ioctl call; the device is then able to exchange any amount of data with user space.

As we can see, the ```ioctl```'s arguments are unstructured, which, makes it hard to make such arguments work identically on all systems. E.g. 64-bit systems with a userspace process running in 32-bit mode (haven't figured out why).

Hence, we need to implement miscellaneous control operations. There are two means:
    i) embedding commands into the data stream (in this chapter)
    ii) using virtual filesystems, either sysfs or driverspecific filesystems (chapter 14).


### ioctl driver method prototype
The ```ioctl``` driver method has a prototype of:
```
int (*ioctl) (struct inode *inode, struct file *filp,
                unsigned int cmd, unsigned long arg);

/*
 * @inode @filp: the values corresponding to the file descriptor fd passed on
 *               by the application and are the same parameters passed to the
 *               open method.
 * @cmd: passed from the user unchanged, same as ioctl() syscall
 *       type of which is int - a symbolic name, declared in header files
 * @arg: compiler will not warn user if this argument is not passed.
 */
```

### choosing the ioctl commands
Before writing the code for ```ioctl```, need to choose the numbers that correspond to commands.

The ioctl command numbers should be unique across the system in order to prevent errors caused by issuing the right command to the wrong device. Such a mismatch is not unlikely to happen, and a program might find itself trying to change the baud rate of a non-serial-port input stream, such as a FIFO or an audio device. If each ioctl number is unique, the application gets an EINVAL error rather than succeeding in doing something unintended. To help programmers create unique ioctl command codes, these codes have been split up into several bitfields.

Need to choose ```ioctl``` numbers for the driver according to the Linux kernel convention - first check *include/asm/ioctl.h* and *Documentation/ioctl-number.txt*. 

Such header defines the bitfields that will be using: 
    1) type - 
        the magic number assiciated with the device.

    2) ordinal number - 
        eight bits (_IOC_NRBITS) wide

    3) direction of transfer -
        possible values are _IOC_NONE (no data transfer), _IOC_READ, _IOC_WRITE,
        and _IOC_READ|_IOC_WRITE (data is transferred both ways)

    4) size of argument - 
         the size of user data involved (can be found in the macro _IOC_SIZEBITS)

The *ioctl-number.txt* file lists the magic numbers used throughout the kernel, so we’ll be able to choose our own magic number and avoid overlaps. The text file also lists the reasons why the convention should be used.

