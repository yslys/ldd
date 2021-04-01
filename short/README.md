## "short" device (Chapter 9 - Communicating with Hardware)
Implementing a real device requires hardware. In scull, we have examined the internals of software concepts; this device will show you how a driver can access I/O ports and I/O memory while being portable across Linux platforms.

We use simple digital I/O ports (such as the standard PC parallel port) to show how the I/O instructions work and normal frame-buffer video memory to show memory-mapped I/O.

### I/O Ports and I/O Memory
Every peripheral device is controlled by writing and reading its registers. Most of the time a device has several registers, and they are accessed at consecutive addresses, either in the memory address space or in the I/O address space.

At the hardware level, both memory regions and I/O regions are accessed by asserting electrical signals on **address bus** and **control bus**, and by reading from or writing to the **data bus**.

Some processors (most notably the x86 family) have separate read and write electrical lines for I/O ports and special CPU instructions to access ports.

### Using I/O Ports
I/O ports are the means by which drivers communicate with many devices, at least part of the time.

1. Driver must claim the ports it needs, making sure that it has exclusive access to those ports. ```request_region()```
2. 