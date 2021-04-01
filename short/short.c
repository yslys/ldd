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

/**
 * Atomicly increment an index into short_buffer
 * 
 */
static inline void short_incr_bp(volatile unsigned long *index, int delta)
{
    unsigned long new = *index + delta;
    barrier(); /* don't optimize these two together */
    *index = (new >= (short_buffer + PAGE_SIZE)) ? short_buffer : new;
}

/*
 * The devices with low minor numbers write/read burst of data to/from
 * specific I/O ports (by default the parallel ones).
 * 
 * The device with 128 as minor number returns ascii strings telling
 * when interrupts have been received. Writing to the device toggles
 * 00/FF on the parallel data lines. If there is a loopback wire, this
 * generates interrupts.  
 */



int short_release(struct inode *inode, struct file *filp)
{
    return 0;
}