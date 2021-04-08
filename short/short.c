/*
 * short.c -- Simple Hardware Operations and Raw Tests
 * Read and write a few 8-bit ports, starting from the one we select at load time.
 * short.c -- also a brief example of interrupt handling ("short int")
 *
 * Copyright (C) 2001 Alessandro Rubini and Jonathan Corbet
 * Copyright (C) 2001 O'Reilly & Associates
 *
 * The source code in this file can be freely used, adapted,
 * and redistributed in source or binary form, so long as an
 * acknowledgment appears in derived source files.  The citation
 * should list that the code comes from the book "Linux Device
 * Drivers" by Alessandro Rubini and Jonathan Corbet, published
 * by O'Reilly & Associates.   No warranty is attached;
 * we cannot take responsibility for errors or fitness for use.
 *
 * $Id: short.c,v 1.16 2004/10/29 16:45:40 corbet Exp $
 */

/*
 * FIXME: this driver is not safe with concurrent readers or
 * writers.
 */

#include <linux/version.h>      /* LINUX_VERSION_CODE  */
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>

#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/kernel.h>	/* printk() */
#include <linux/fs.h>		/* everything... */
#include <linux/errno.h>	/* error codes */
#include <linux/delay.h>	/* udelay */
#include <linux/kdev_t.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>
#include <linux/poll.h>
#include <linux/wait.h>

#include <asm/io.h>

#define SHORT_NR_PORTS	8	/* use 8 ports by default */

/*
 * all of the parameters have no "short_" prefix, to save typing when
 * specifying them at load time
 */
static int major = 0;	/* dynamic by default */
module_param(major, int, 0);

static int use_mem = 0;	/* default is I/O-mapped */
module_param(use_mem, int, 0);

/* default is the first printer port on PC's. "short_base" is there too
   because it's what we want to use in the code */
static unsigned long base = 0x378;
unsigned long short_base = 0;
module_param(base, long, 0);

/* The interrupt line is undefined by default. "short_irq" is as above */
static int irq = -1;
volatile int short_irq = -1;
module_param(irq, int, 0);

static int probe = 0;	/* select at load time how to probe irq line */
module_param(probe, int, 0);

static int wq = 0;	/* select at load time whether a workqueue is used */
module_param(wq, int, 0);

static int tasklet = 0;	/* select whether a tasklet is used */
module_param(tasklet, int, 0);

static int share = 0;	/* select at load time whether install a shared irq */
module_param(share, int, 0);

MODULE_AUTHOR ("Alessandro Rubini");
MODULE_LICENSE("Dual BSD/GPL");


// beginning of buffer
unsigned long short_buffer = 0;

unsigned long volatile short_head;
volatile unsigned long short_tail;
DECLARE_WAIT_QUEUE_HEAD(short_queue);


/* Set up our tasklet if we're doing that. */
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 9, 0))
void short_do_tasklet(unsigned long);
DECLARE_TASKLET(short_tasklet, short_do_tasklet, 0);
#else
void short_do_tasklet(struct tasklet_struct *);
DECLARE_TASKLET(short_tasklet, short_do_tasklet);
#endif

/*
 * The devices with low minor numbers write/read burst of data to/from
 * specific I/O ports (by default the parallel ones).
 * 
 * The device with 128 as minor number returns ascii strings telling
 * when interrupts have been received. Writing to the device toggles
 * 00/FF on the parallel data lines. If there is a loopback wire, this
 * generates interrupts.  
 * 
 * I was confused about inode and file structures here at the beginning.
 * inode contains the metadata of a file in Linux, a unique number assigned to 
 *      files and directories while it is created.
 * file represents an open file - not the metadata.
 * 
 * You can open a file for reading or writing using the open system call. 
 * This returns a file descriptor. Linux maintains a global file descriptor 
 * table and adds an entry to it representing the opened file. This entry is 
 * represented by the file structure which is local to the process. Internally, 
 * linux uses the inode struct to represent the file. The file struct has a 
 * pointer to this and linux ensures that multiple file descriptors that touch 
 * the same file point to the same inode so that their changes are visible to 
 * each other. The i_mapping field on the inode struct is what’s used to get 
 * the right set of pages from the page cache for an offset in the file.
 * 
 * https://medium.com/i0exception/memory-mapped-files-5e083e653b1
 */
int short_open(struct inode *inode, struct file *filp)
{
    extern struct file_operations short_i_fops;

    /**
     * note that major numbers and minor numbers are 8-bit quantities.
     * iminor(inode) gets the minor number of this device.
     * performing bitwise AND with 0x80 (0b10000000),
     * only when minor number is 128 will enter the if-statement
     */
    if(iminor (inode) & 0x80){ // 0x80 is 128 in decimal, 0b10000000 in binary
        filp->f_op = &short_i_fops;
    }

    return 0;
}


int short_release(struct inode *inode, struct file *filp)
{
    return 0;
}

/**
 * first, the port-oriented device
 */
// 0,1,2,3
enum short_modes {SHORT_DEFAULT=0, SHORT_PAUSE, SHORT_STRING, SHORT_MEMORY}; 

ssize_t do_short_read(struct inode *inode, struct file *filp, char __user *buf,
                      size_t count, loff_t *f_ops)
{
    // return # of bytes read
    int retval = count; 

    // get the minor number of the device
    // note that major and minor numbers are all 8-bit quantities
    int minor = iminor(inode); 

    // short_base is a relative offset, base is 0x378 (the first port, port0)
    // 0x0f is  0b00001111
    // minor is 0bxxxxxxxx
    // after bitwise AND, we get the last four bits of minor number
    // using I/O port
    unsigned long port = short_base + (minor & 0x0f);

    // using I/O memory, same address of port
    void *address = (void *) short_base + (minor & 0x0f);

    // 0x70 is ob01110000
    // get the [1-3] bits of minor number to determine the mode
    int mode = (minor & 0x70) >> 4;

    // allocate memory from RAM
    unsigned char *kbuf = kmalloc(count, GFP_KERNEL);
    unsigned char *ptr;

    if(!kbuf){
        return -ENOMEM;
    }
    else{
        ptr = kbuf;
    }

    // if accessing through I/O Memory
    if(use_mem){
        mode = SHORT_MEMORY;
    }

    switch(mode){
        case SHORT_STRING:
            insb(port, ptr, count); // string operations of read (a seq of count bytes)
            rmb(); // read memory barrier
            break;
        
        case SHORT_DEFAULT:
            while(count--){
                *(ptr++) = inb(port); // read through port
                rmb();
            }
            break;
        
        case SHORT_MEMORY:
            while(count--){
                *ptr++ = ioread8(address); // read through I/O memory
                rmb();
            }
            break;
        
        default:
            retval = -EINVAL;
            break;
    }

    // retval = count > 0; copy_to_user() returns # of bytes could not be copied (0 on success)
    if((retval > 0) && copy_to_user(buf, kbuf, retval)){
        retval = -EFAULT;
    }

    kfree(kbuf);
    return retval;
}


/*
 * Version-specific methods for the fops structure.  FIXME don't need anymore.
 */
ssize_t short_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
    return do_short_read(file_dentry(filp)->d_inode, filp, buf, count, f_pos);
}


ssize_t do_short_write(struct inode *inode, struct file *filp, 
                       const char __user *buf,
                       size_t count, loff_t *f_pos)
{
    int retval = count;

    // minor number is 8 bits (0-7), 1-3: mode, 4-7:port and address 
    int minor = iminor(inode);
    unsigned long port = short_base + (minor & 0x0f);
    void *address = (void *)short_base + (minor & 0x0f);

    int mode = (minor & 0x70) >> 4;

    // kbuf stores the starting address of allocated memory
    // *ptr has the same value as kbuf at the beginning, but it serves as a working ptr
    unsigned char *kbuf = kmalloc(count, GFP_KERNEL), *ptr;
    if(!kbuf){
        return -ENOMEM;
    }

    // copy data from user to kernel buffer (kbuf), then write what's in kbuf to dev 
    if(copy_from_user(kbuf, buf, count)){
        return -EFAULT;
    }

    // initialize the working ptr
    ptr = kbuf;

    // check the mode to see if it's using I/O memory
    if(use_mem){
        mode = SHORT_MEMORY;
    }

    switch (mode)
    {
    case SHORT_PAUSE:
        while(count--){
            outb_p(*(ptr++), port); // used in pausing I/O (p means pause)
            wmb();
        }
        break;
    
    case SHORT_STRING:
        outsb(port, ptr, count); // using I/O port and string operations
        wmb();
        break;
    
    case SHORT_DEFAULT:
        while(count--){
            outb(*(ptr++), port);
            wmb();
        }
        break;
    
    case SHORT_MEMORY:
        while(count--){
            iowrite8(*ptr++, address);
            wmb();
        }
        break;

    default:
        retval = -EINVAL;
        break;
    }

    kfree(kbuf);
    return retval;
}

ssize_t short_write(struct file *filp, const char __user *buf, size_t count,
		loff_t *f_pos)
{
	return do_short_write(file_dentry(filp)->d_inode, filp, buf, count, f_pos);
}


unsigned int short_poll(struct file *filp, poll_table *wait)
{
    return POLLIN | POLLRDNORM | POLLOUT | POLLWRNORM;
}

struct file_operations short_fops = {
	.owner	 = THIS_MODULE,
	.read	 = short_read,
	.write	 = short_write,
	.poll	 = short_poll,
	.open	 = short_open,
	.release = short_release,
};


////////////////////////////////////////////////////////////////////////////////
/**
 * the following is the interrupt-related device
 * note that registration of interrupt handler is in short_init
 */ 

/**
 * Atomicly increment an index into short_buffer
 * @caller: short_i_read()
 */
static inline void short_incr_bp(volatile unsigned long *index, int delta)
{
    unsigned long new = *index + delta;
    barrier(); /* don't optimize these two together */
    *index = (new >= (short_buffer + PAGE_SIZE)) ? short_buffer : new;
}


ssize_t short_i_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
    int count0;
    // create a struct wait_queue_entry, with its name = wait
    DEFINE_WAIT(wait);

    // while nothing to read, put the process to sleep
    while(short_head == short_tail){
        // wait current process to short_queue
        // set the state of current process to mark it as being asleep
        prepare_to_wait(&short_queue, &wait, TASK_INTERRUPTIBLE);

        // before calling the scheduler, we must check the sleep condition again
        if(short_head == short_tail){
            // invoke the scheduler and yield(give up) CPU
            schedule();
        }
        
        // i) reset the task state to TASK_RUNNING;
        // ii) remove the process from the wait queue, or it may
        //     be awakened more than once.
        finish_wait(&short_queue, &wait);
        
        // if we are awakened by a signal
        if(signal_pending(current)){
            return -RESTARTSYS;
        }
    }

    // if we enter here, it means there's something to read
    // count0 stores the number of readable bytes
    count0 = short_head - short_tail;
    
    if(count < 0){ // wrapped
        count0 = short_buffer + PAGE_SIZE - short_tail;
    }

    if(count0 < count){ // can only read up to count0 bytes
        count = count0;
    }

    if(copy_to_user(buf, (char *)short_tail, count)){
        return -EFAULT;
    }

    // atomically increment short_tail
    short_incr_bp(&short_tail, count);
    return count;
}


ssize_t short_i_write (struct file *filp, const char __user *buf, size_t count,
		loff_t *f_pos)
{
    int written = 0;
    
    /**
     * I was confused here at first: what's the point of doing bitwise AND with 1?
     * The answer is to turn the address (hexadecimal) to decimal - 1 is decimal
     * odd - the position we are at in the file
     */
    int odd = *f_pos & 1;
    unsigned long port = short_base; // output to the parallel data latch (port)
    void *address = (void *)short_base;

    if(use_mem){ // if we're using I/O memory
        while(written < count){
            /**
             * here, we'd like to write something (LHS) to the address (RHS).
             * Actually, LHS is also an address - that's why we use 0xff * ...
             * written is the offset - already written
             * odd stores the position we are at in the file
             * So, odd + written -> new position, (note that we are in the while loop)
             */
            iowrite8(0xff * ((++written + odd) & 1), address);
        }
    }
    else{ // using I/O port
        while(written < count){
            outb(0xff * ((++written + odd) & 1), port);
        }
    }

    // after writing to dev, update the file position
    *f_pos += count;
    return written; // on success, return number of bytes written
}


struct file_operations short_i_fops = {
	.owner	 = THIS_MODULE,
	.read	 = short_i_read,
	.write	 = short_i_write,
	.open	 = short_open,
	.release = short_release,
};

/**
 * this sample code responds to the interrupt by calling do_gettimeofday and 
 * printing the current time into a page-sized circular buffer. It then awakes
 * any reading process because there's data available to be read.
 * 
 * @irq: the interrupt number - useful as information you may print in your log msgs.
 * @dev_id: the client data - usually we pass a pointer to the device data structure
 *          in dev_id, so a driver that manages several instances of the same device 
 *          doesn't need any extra code in the interrupt handler to find out which 
 *          device is in charge of the current interrupt event.
 * @return irqreturn_t 
 */
irqreturn_t short_interrupt(int irq, void *dev_id)
{
    struct timespec64 tv;
    int written;

    ktime_get_real_ts64(&tv);

    // write a 16 byte record. Assume PAGE_SIZE is a multiple of 16
    written = sprintf((char *)short_head, "%08u.%06lu\n",
                (int)(tv.tv_sec % 100000000), (int)(tv.tv_nsec) /  NSEC_PER_USEC);
    
    // written should be 16, if not, then something went wrong
    BUG_ON(written != 16);

    // increment short_head to update the new position
    // short_head is always the written position, short_tail is where to read
    // here, we have written 16 bytes, hence increment short_head by 16
    short_incr_bp(&short_head, written);

    // wake up any reading process waiting in short_queue
    wake_up_interruptible(&short_queue);
    return IRQ_HANDLED;
}


/*
 * The following two functions are equivalent to the previous one,
 * but split in top and bottom half. First, a few needed variables
 */

#define NR_TIMEVAL 512 /* length of the array of time values */

struct timespec64 tv_data[NR_TIMEVAL]; /* too lazy to allocate it */

// at the initialization, tv_head and tv_tail all point to beginning of array tv_data
volatile struct timespec64 *tv_head=tv_data;
volatile struct timespec64 *tv_tail=tv_data;

// wait queue
static struct work_struct short_wq;

// # of elements in wait queue
int short_wq_count = 0;

/*
 * Increment a circular buffer pointer in a way that nobody sees
 * an intermediate value.
 * 
 * @tvp: circular buffer pointer (a pointer to an array of struct timespec64)
 */
static inline void short_incr_tv(volatile struct timespec64 **tvp)
{
    if(*tvp == (tv_data + NR_TIMEVAL - 1)){
        *tvp = tv_data;
    }
    else{
        (*tvp)++;
    }
}


/**
 * tasklet implementation
 * 
 */
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 9, 0))
void short_do_tasklet (unsigned long unused)
#else
void short_do_tasklet (struct tasklet_struct * unused)
#endif
{
    // int savecount = short_wq_count, written;
    int savecount = short_wq_count;
    int written;

    // we have already been removed from the queue
    short_wq_count = 0;

    /*
	 * The bottom half reads the tv array, filled by the top half,
	 * and prints it to the circular text buffer, which is then consumed
	 * by reading processes
	 */

    // first write the # of interrupts that occurred before this bottom half
    written = sprintf((char *)short_head, "bottom half after %6i\n", savecount);
    // then increment short_head (bp = base ptr)
    short_incr_bp(&short_head, written);

    /*
	 * Then, write the time values. Write exactly 16 bytes at a time,
	 * so it aligns with PAGE_SIZE
	 */
    do{
        written = sprintf((char *)short_head,"%08u.%06lu\n",
                (int)(tv_tail->tv_sec % 100000000),
                (int)(tv_tail->tv_nsec) /  NSEC_PER_USEC);
        
        // then, increment short_head
        short_incr_bp(&short_head, written);

        //???????????????????????????????
        short_incr_tv(&tv_tail);
    } while(tv_tail != tv_head);

    // finally, wake up any reading process in short_queue
    wake_up_interruptible(&short_queue);
}


irqreturn_t short_wq_interrupt(int irq, void *dev_id)
{
    // grab current time info
    ktime_get_real_ts64((struct timespec64 *) tv_head);

    // increment tv_head
    // inside the function, its pointer arithmetic
    // if we view it from address point of view, it is incrementing sizeof(timespec64 *)
	short_incr_tv(&tv_head);

    // then queue the bottom half to the waitqueue
    schedule_work(&short_wq);

    // record that an interrupt arrived
    short_wq_count++;
    return IRQ_HANDLED;
}


/**
 * Tasklet top half
 */
irqreturn_t short_tl_interrupt(int irq, void *dev_id)
{
    ktime_get_real_ts64((struct timespec64 *) tv_head); /* cast to stop 'volatile' warning */
    short_incr_tv(&tv_head);
    tasklet_schedule(&short_tasklet);
    short_wq_count++;
    return IRQ_HANDLED;
}

irqreturn_t short_sh_interrupt(int irq, void *dev_id)
{
    int value, written;
    struct timespec64 tv;

    // if it wasn't short, then return immediately
    value = inb(short_base);
    if(!(value & 0x80)){ // 0x80 is 0b10000000
        return IRQ_NONE;
    }

    /* clear the interrupting bit */
    outb(value & 0x7F, short_base);

    /* the rest is unchanged */

    ktime_get_real_ts64(&tv);
    written = sprintf((char *)short_head,"%08u.%06lu\n",
            (int)(tv.tv_sec % 100000000), (int)(tv.tv_nsec) / NSEC_PER_USEC);
    short_incr_bp(&short_head, written);
    wake_up_interruptible(&short_queue); /* awake any reading process */
    return IRQ_HANDLED;
}

void short_kernelprobe(void)
{

}

irqreturn_t short_probing(int irq, void *dev_id)
{

}

void short_selfprobe(void)
{

}

/**
 * init and cleanup
 */
int short_init(void)
{
    int result;

    // irq - interrupt requests

}

void short_cleanup(void)
{

}

module_init(short_init);
module_exit(short_cleanup);