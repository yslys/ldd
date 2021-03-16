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

The above operations are usually supported via ```ioctl``` method. In user space, it has the prototype of:
```
int ioctl(int fd, unsigned long cmd, ...);

// fd - file descriptor

/*
  The ioctl() system call manipulates the underlying device
       parameters of special files.  In particular, many operating
       characteristics of character special files (e.g., terminals) may
       be controlled with ioctl() requests.  
       
       The argument fd must be an open file descriptor.
       The second argument is a device-dependent request code.  
       The third argument is an untyped pointer to memory.  
       It's traditionally char *argp (from the days before void * was valid
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
1. embedding commands into the data stream (in this chapter)
2. using virtual filesystems, either sysfs or driverspecific filesystems (chapter 14).

### A small demonstration of ioctl
*Retrieved from https://stackoverflow.com/questions/15807846/ioctl-linux-device-driver#15856623*

A printer that has configuration options to check and set the font family. ```ioctl``` could be used to get the current font + set the font to a new one. A user application uses ```ioctl``` to send a code to a printer telling it to return the current font or to set the font to a new one.

Here's how a user program will call ```ioctl``` to interact with the printer (device):
```
int ioctl(int fd, int request, ...)
```
1. ```fd``` - file descriptor, returned by ```open```
2. ```request``` - request code, identify what action to do on such device
3. 3rd argument ```void *``` - depends on the 2nd argument, e.g. if the second argument is ```SETFONT```, the third argument can be the font name such as ```"Arial"```

A user application has to generate a **request code** and the **device driver module** to determine which configuration on device must be played with. In other words: 
1. the user application sends the **request code** using ```ioctl```
2. uses the **request code** in the device driver module to determine which action to perform.

As we have noticed, such **request code** is important, so we need to talk about it in details. Firstly, it has 4 main parts:
1. A Magic number - 8 bits:
    usually defined at the beginning, e.g. ```#define PRIN_MAGIC 'P'```
2. A sequence number - 8 bits:
    also called ordinal number
3. Argument type (typically 14 bits), if any:
    the magic number assiciated with the device
4. Direction of data transfer - 2 bits:
    e.g. if request code is ```SETFONT```, then the direction will be user application -> device driver module; if ```GETFONT```, then the direction will be reversed

*Now we have a brief understanding of the **request code***, but actually, we still need to figure out how to **generate** such code: *using predefined function-like macros in Linux* as follows:
1. ```_IO(type,nr)``` - for a command that has no argument
2. ```_IOR(type,nr,datatype)``` - for reading data from the driver
3. ```_IOW(type,nr,datatype)``` - for writing data
4. ```_IOWR(type,nr,datatype)``` - for bidirectional transfers

#### Example 1
Say, we would like to pause printer. Since pausing the printer does not require any data transfer, we could use ```_IO(type,nr)```, see the code below:
```
#define PRIN_MAGIC 'P' // define the magic number at the beginning
#define SEQ_NUM 0 // define the sequence number
#define PAUSE_PRIN _IO(PRIN_MAGIC, SEQ_NUM) // define the request code PAUSE_PRIN
                                            // using _IO(PRIN_MAGIC, NUM)
```
After we have done such things, user could use ```ioctl``` as:
```
ret_val = ioctl(fd, PAUSE_PRIN) // note that this ioctl takes two arguments
                                // because no extra arguments is needed here
```
Then, the corresponding syscall in the driver module will receive the code and pause the printer.

#### Example 2
Say, we would like to set the font to be Arial. The direction of data transfer will be: user application -> printer (device), and we would like to write to the device; hence, we have to use the predefined function-like macro - ```_IOW(type,nr,datatype)```. The code is as follows:
```
#define PRIN_MAGIC 'S' // define the magic number at the beginning (S = SET)
#define SEQ_NO 1 // define the sequence number as 1
#define SETFONT _IOW(PRIN_MAGIC, SEQ_NO, unsigned long) // define the request code
                                                        // SETFONT
```
```datatype``` here gives us the type of the **third argument** in ```ioctl```. Someone may be wondering why the type of which is ```unsigned long```. Recall that the prototype of ioctl is ```int ioctl(int fd, int request, ...)```, and the type of the optional 3rd argument is ```void *```, we need to interpret it to be an address, which is of type ```unsigned long```.

Now, the user could execute the following to interact with the printer (device):
```
char *font = "Arial";
ret_val = ioctl(fd, SETFONT, font); // font is a pointer, i.e. an address
```
The address of ```font``` is passed to corresponding syscall implemented in device driver module as ```unsigned long``` and we need to cast it to proper type before using it. Kernel space can access user space and hence this works. 


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

1. ```type``` - 
    the magic number assiciated with the device

2. ```ordinal number``` - 
    eight bits (_IOC_NRBITS) wide

3. ```direction of transfer``` -
    possible values are _IOC_NONE (no data transfer), _IOC_READ, _IOC_WRITE, and _IOC_READ|_IOC_WRITE (data is transferred both ways)

4. ```size of argument``` - 
    the size of user data involved (can be found in the macro _IOC_SIZEBITS)

The *ioctl-number.txt* file lists the magic numbers used throughout the kernel, so we’ll be able to choose our own magic number and avoid overlaps. The text file also lists the reasons why the convention should be used.

The header file *<asm/ioctl.h>*, which is included by *<linux/ioctl.h>*, defines macros that help set up the **command numbers** as follows: 

1. ```_IO(type,nr)``` - for a command that has no argument
2. ```_IOR(type,nr,datatype)``` - for reading data from the driver
3. ```_IOW(type,nr,datatype)``` - for writing data
4. ```_IOWR(type,nr,datatype)``` - for bidirectional transfers

As we can see here, for the command that has no argument, there's no ```datatype``` field because we do not need to specify what type of data to read/write. 

The ```type``` and ```number (nr)``` fields are passed as arguments, and the size field is derived by applying sizeof to the datatype argument.

See *scull.h* in this directory for more detials of how *ioctl* commands are defined in scull. Such commands set and get the driver's configurable parameters.

### Using the ioctl argument
Remember that optional 3rd argument? If that is an integer, it would be easy to handle. However, if that is a pointer, be careful.

+ ```access_ok()``` - address verification
    + declared in *<asm/uaccess.h>*
    + ```int access_ok(int type, const void *addr, unsigned long size);```
    + ```type``` - either ```VERIFY_READ``` (reading the user-space memory area) or ```VERIFY_WRITE``` (writing to user-space memory area)
    + ```addr``` - user-space address
    + ```size``` - byte count. If ```ioctl``` needs to read an integer from user space, then ```size = sizeof(int)```. If both read and write are needed, use ```VERIFY_WRITE``` (superset of ```VERIFY_READ```).
    + return value: 1 on success, 0 on failure (then the driver should return ```-EFAULT```)
