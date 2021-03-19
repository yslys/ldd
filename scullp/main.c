/* -*- C -*-
 * main.c -- the bare scullp char module
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
 * $Id: _main.c.in,v 1.21 2004/10/14 20:11:39 corbet Exp $
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/kernel.h>	/* printk() */
#include <linux/slab.h>		/* kmalloc() */
#include <linux/fs.h>		/* everything... */
#include <linux/errno.h>	/* error codes */
#include <linux/types.h>	/* size_t */
#include <linux/proc_fs.h>  /* all modules work with /proc */
#include <linux/seq_file.h>
#include <linux/fcntl.h>	/* O_ACCMODE */
#include <linux/aio.h>
#include <linux/uaccess.h>
#include <linux/uio.h>	/* ivo_iter* */
#include "scullp.h"		/* local definitions */
#include "scull-shared/scull-async.h"
#include "access_ok_version.h"
#include "proc_ops_version.h"

// get such params from scullp.h
int scullp_major =    SCULLP_MAJOR;
int scullp_devs  =    SCULLP_DEVS; // # of bare scullp devices
int scullp_qset  =    SCULLP_QSET;
int scullp_order =    SCULLP_ORDER;

// define module parameters; 0 means there's no sysfs entry
module_param(scullp_major, int, 0);
module_param(scullp_devs, int, 0);
module_param(scullp_qset, int, 0);
module_param(scullp_order, int, 0);

MODULE_AUTHOR("Alessandro Rubini");
MODULE_LICENSE("Dual BSD/GPL");

// scullp_dev is defined in scullp.h
// declaration here, will be allocated in scullp_init() 
struct scullp_dev *scullp_devices; 

int scullp_trim(struct scullp_dev *dev);
void scullp_cleanup(void); // this function takes no parameters

#ifdef SCULLP_USE_PROC // maybe in Makefile
// the /proc filesystem (read-only files): function to read and entry
//--------------------------------------------------------------

#endif // SCULLP_USE_PROC



/* open */
int scullp_open(struct inode *inode, struct file *filp)
{
    struct scullp_dev *dev; // stores device information

    // find the device according to inode->i_cdev
    /**
     *  hi, given inode's i_cdev, with type cdev; help me find its
     * container, with type struct scullp_dev
     */
    dev = container_of(inode->i_cdev, struct scullp_dev, cdev);

    // now, trim to 0 the length of the device if open was write-only
    if((filp->f_flags & O_ACCMODE) == O_WRONLY){
        /**
         * mutex_lock_interruptible(lock) - Acquire the mutex that can be 
         *                                  interrupted by signals.
         * @lock: The mutex to be acquired.
         *
         * Lock the mutex like mutex_lock().  If a signal is delivered while the
         * process is sleeping, this function will return without acquiring the
         * mutex.
         *
         * Context: Process context.
         * Return: 0 if the lock was successfully acquired or %-EINTR if a
         * signal arrived.
         */
        if(mutex_lock_interruptible(&dev->mutex)){ // acquire the interruptable mutex
            return -ERESTARTSYS;
        }
        scullp_trim(dev); // ignore errors
        mutex_unlock(&dev->mutex); // unlock the acquired mutex
    }

    // use filp->private_data to store the device info
    filp->private_data = dev;

    return 0; // success
}

/* close */
int scullp_release(struct inode *inode, struct file *filp)
{
    return 0;
}

/**
 * follow the list up to the right position,
 * if memory not enough, kmalloc it, and fill it with 0's 
 * @return: ptr to scullp_dev 
 */
struct scullp_dev *scullp_follow(struct scullp_dev *dev, int n)
{
    // loop n times
    while(n--){
        // if dev has reached to end
        if(!dev->next){
            /** 
             * kmalloc sizeof(scullp_dev) of space, with type of memory allocated
             * to be GFP_KERNEL - Allocate normal kernel ram. May sleep.
             * https://elixir.bootlin.com/linux/latest/source/include/linux/slab.h#L538
             */
            dev->next = kmalloc(sizeof(struct scullp_dev), GFP_KERNEL); 
            
            /**
             * After kmalloc, needs to fill such kmalloc'd memory with constant
             * byte 0
             */ 
            memset(dev->next, 0, sizeof(struct scullp_dev));
        }
        dev = dev->next;
        continue;
    } /* end of while loop */

    return dev;
}

/**
 * read - data management
 * @filp: file pointer
 * @buf: 
 * @count:
 * @f_pos: 
 * 
 *  */
ssize_t scullp_read(struct file *filp, char __user *buf, 
                    size_t count, loff_t *f_pos)
{
    struct scullp_dev *dev = filp->private_data; // the first listitem
    struct scullp_dev *dptr; // dynamic ptr
    int quantum = PAGE_SIZE << dev->order;
    int qset = dev->qset;
    int itemsize = quantum * qset; // # of bytes in the listitem
    int item, s_pos, q_pos, rest;
    ssize_t retval = 0;

    if(mutex_lock_interruptible(&dev->mutex)){
        return -ERESTARTSYS;
    }
    if(*)
}