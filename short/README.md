## "short" device (Chapter 9 - Communicating with Hardware)
Implementing a real device requires hardware. In scull, we have examined the internals of software concepts; this device will show you how a driver can access I/O ports and I/O memory while being portable across Linux platforms.

We use simple digital I/O ports (such as the standard PC parallel port) to show how the I/O instructions work and normal frame-buffer video memory to show memory-mapped I/O.

### I/O Ports and I/O Memory
Every peripheral device is controlled by writing and reading its registers. Most of the time a device has several registers, and they are accessed at consecutive addresses, either in the memory address space or in the I/O address space.

At the hardware level, both memory regions and I/O regions are accessed by asserting electrical signals on **address bus** and **control bus**, and by reading from or writing to the **data bus**.

Some processors (most notably the x86 family) have separate read and write electrical lines for I/O ports and special CPU instructions to access ports.

### I/O Registers and Conventional Memory
Since memory access speed is critical to CPU performance, there are two optimizations made:
1. compiler can cache the data values into CPU registers without writing them into memory, even if it stores them, both read and write operations can operate on cache memory without reaching physical RAM.
2. read/write instructions are reordered, s.t. could be executed more quickly.

The above optimizations are benign when applied to conventional memory (uniprocessor systems), but they can be fatal to correct I/O operations. Why?

Because the processor cannot anticipate a situation in which **some other process** (running on a separate processor, or something happening inside an I/O controller) **depends on the order of memory access**. If the operations you request were reordered by compiler or CPU, then the result will be strange errors, hard to debug.

Hence, a driver must ensure that 
- no caching is performed when accessing registers.
- no read or write reordering takes place when accessing registers.

How?

Solution1 - to make sure no caching is performed: by manually configuring hardware.

Solution2 - to make sure no reordering: by placing a **memory barrier** between operations that must be visible to the hardware (or to another processor) in a particular order.

Below are several useful macros to prevent reordering:
+ ```#include <linux/kernel.h>```
    + ```void barrier(void)```: **compiler level prevention** - tell only the compiler to insert a memory barrier, but not hardware. Compiled code stores all values that are currently modified and resident in CPU registers to memory - rereads them later when needed. Prevents compiler optimizations across the barrier.

+ ```#include <asm\system.h>```
    + ```void rmb(void)```: read memory barrier - guarantees that any reads appearing before the barrier are completed before any subsequent read.
    + ```void read_barrier_depends(void)```: special, weaker form of read barrier. Blocks only the reordering of reads that depend on data from other reads.
    + ```void wmb(void)```: write memory barrier - guarantees write.
    + ```void mb(void)```: memory barrier - guanrantes both read and write.
    + The above functions insert hardware memory barriers in the compiled instruction flow; their actual instantiation is platform dependent.

Usage:
```
writel(dev->registers.addr, io_destination_address);
writel(dev->registers.size, io_size);
writel(dev->registers.operation, DEV_READ);
wmb( );
writel(dev->registers.control, DEV_GO);
```
Memory barriers should be used only where they're really needed.


### Using I/O Ports
I/O ports are the means by which drivers communicate with many devices, at least part of the time.

1. Driver must claim the ports it needs, making sure that it has exclusive access to those ports. ```request_region()``` in *<linux/ioport.h>*

2. Manipulate I/O ports (see next section).

3. When done with a set of I/O ports (at module unload time, perhaps), should be returned to the system. ```release_region()```

### Manipulating I/O Ports
This section discusses what to do after a driver has reuqested the range of I/O ports.

Most hardware differentiates between 8-bit, 16-bit and 32-bit ports. Must call different functions to access different size ports. Computer architectures that support only memory-mapped I/O registers fake port I/O by remapping port addresses to memory addresses, and the kernel hides the details from the driver in order to ease portability. The Linux kernel headers (specifically, the architecture-dependent header **<asm/io.h>**) define the following inline functions to access I/O ports:

+ ```#include <asm/io.h>```
    + ```unsigned inb(unsigned port)```: read ```port``` bytes (8-bit)
    +```void outb(unsigned char byte, unsigned port);```: write ```port``` bytes (8-bit)
        + Read or write byte ports (eight bits wide). The port argument is defined as unsigned long for some platforms and unsigned short for others. The return type of inb is also different across architectures.
    + ```unsigned inw(unsigned port);```: read ```port``` words (16-bit)
    + ```void outw(unsigned short word, unsigned port);```: write ```port``` words (16-bit)
        + These functions access 16-bit ports (one word wide); they are not available when compiling for the S390 platform, which supports only byte I/O.
    + ```unsigned inl(unsigned port);```: read ```port``` long words (32-bit)
    + ```void outl(unsigned longword, unsigned port);```: write ```port``` long words (32-bit)
        + These functions access 32-bit ports. longword is declared as either unsigned long or unsigned int, according to the platform. Like word I/O, “long” I/O is not available on S390.

Even on 64-bit architectures, the port address space uses a 32-bit data path.

### I/O Port Access from User Space
Functions we have described in previous section are used by device drivers, they can also be used in user space, defined in **<sys/io.h>**.

- The program must be compiled with -O option (to force expansion of inline functions).
- Must use ```ioperm``` and ```iopl``` syscalls to get permission to perform I/O operations on ports. ```ioperm``` - gets permission for individual ports; ```iopl``` - gets permission for the entire I/O space.
- The program must run as root to invoke ```ioperm``` and ```iopl```.

If the host platform has no ```ioperm``` and no ```iopl``` system calls, user space can still access I/O ports by using the **/dev/port** device file.

The sample sources misc-progs/inp.c and misc-progs/outp.c are a minimal tool for reading and writing ports from the command line, in user space. They expect to be installed under multiple names (e.g., inb, inw, and inl and manipulates byte, word, or long ports depending on which name was invoked by the user). They use ioperm or
iopl under x86, /dev/port on other platforms.

### String Operations
The single-shot in and out operations are slow. Some processors implement special instructions to trasfer a sequence of bytes, words, or longs to and from a single I/O port or the same size - called **string instructions**.

The following macros implement the concept of string I/O either by using a single machine instruction or by executing a tight loop if the target processor has no instruction that performs string I/O.

+ ```void insb(unsigned port, void *addr, unsigned long count);```: (in string bytes)
+ ```void outsb(unsigned port, void *addr, unsigned long count);```: (out string bytes)
    + Read or write count bytes starting at the memory address addr. Data is read from or written to the single port port.
+ ```void insw(unsigned port, void *addr, unsigned long count);```: (in string words)
+ ```void outsw(unsigned port, void *addr, unsigned long count);```: (out string words)
    + Read or write 16-bit values to a single 16-bit port.
+ ```void insl(unsigned port, void *addr, unsigned long count);```: (in string long words)
+ ```void outsl(unsigned port, void *addr, unsigned long count);```: (out string long words)
    + Read or write 32-bit values to a single 32-bit port.

### Pausing I/O
When processor transfers data too quickly to or from the bus, problem occurs. The problems can arise when the processor is overclocked with respect to the peripheral bus (think ISA here) and can show up when the device board is too slow. 

The solution is to insert a small delay after each I/O instruction if another such instruction follows. On the x86, the pause is achieved by performing an out b instruction to port 0x80 (normally but not always unused), or by busy waiting. See the io.h file under your platform’s asm subdirectory for details.

If the device misses some data, or if you fear it might miss some, you can use pausing functions in place of the normal ones. ```inb_p```, ```outb_p```, ...

### I/O Port Example
short.c