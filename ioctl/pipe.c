/* A driver that implements blocking I/O */

/*
 * pipe.c -- fifo driver for scull
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
 */
 
#include <linux/module.h>
#include <linux/moduleparam.h>

#include <linux/kernel.h>	/* printk(), min() */
#include <linux/slab.h>		/* kmalloc() */
#include <linux/fs.h>		/* everything... */
#include <linux/proc_fs.h>
#include <linux/errno.h>	/* error codes */
#include <linux/types.h>	/* size_t */
#include <linux/fcntl.h>
#include <linux/poll.h>
#include <linux/cdev.h>
#include <asm/uaccess.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/seq_file.h>

#include "proc_ops_version.h"

#include "scull.h"		/* local definitions */

struct scull_pipe {
    wait_queue_head_t inq, outq;        // read queue, write queue
    char *buffer, *end;                 // begin of buffer, end of buffer
    int buffersize;                     // used in pointer arithmetic
    char *rp, *wp;                      // where to read, where to write
    int nreaders, nwriters              // # of openings for read/write
    struct fasync_struct *async_queue;  // asynchronous readers
    struct mutex lock;                  // mutual exclusion mutex            
    struct cdev cdev;                   // Char device structure
}

/* parameters */
static int scull_p_nr_devs = SCULL_P_NR_DEVS; // # of pipe devices
                                              // SCULL_P_NR_DEVS defined in scull.h
int scull_p_buffer = SCULL_P_BUFFER;          // buffer size
                                              // SCULL_P_BUFFER defined in scull.h
dev_t scull_p_devno;                          // our first device number

/**
 * module_param is a macro that defines module parameters, defined in moduleparam.h
 * module_param(name, type, perm)
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
module_param(scull_p_nr_devs, int, 0); // perm set to 0, means no sysfs entry
module_param(scull_p_buffer, int, 0);

static struct scull_pipe *scull_p_devices;

static int scull_p_fasync(int fd, struct file *filp, int mode);
static int spacefree(struct scull_pipe *dev);

/* open */
static int scull_p_open(struct inode *inode, struct file *filp)
{
    struct scull_pipe *dev;

    dev = container_of(inode->i_cdev, struct scull_pipe, cdev);
    
}