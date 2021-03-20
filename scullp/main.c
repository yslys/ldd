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
#include <linux/uaccess.h> /* copy_to_user */
#include <linux/uio.h>	/* ivo_iter* */
#include "scullp.h"		/* local definitions */
#include "scull-shared/scull-async.h"
#include "access_ok_version.h"
#include "proc_ops_version.h"

// get such params from scullp.h
int scullp_major =    SCULLP_MAJOR;
int scullp_devs  =    SCULLP_DEVS; // # of bare scullp devices
int scullp_qset  =    SCULLP_QSET; // length of the quantum set array
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
 * @dev: ptr to the start of list of quantum sets
 * @n: the index of quantum set (in the qset list) we want to reach
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
 * @filp: file pointer (for the device)
 * @buf: starting address in user space to store what has been read from kernel space
 * @count: # of bytes to read from the device
 * @f_pos: starting address in kernel space to be read by user
 * @return: the number of bytes read from device on success
 */
ssize_t scullp_read(struct file *filp, char __user *buf, 
                    size_t count, loff_t *f_pos)
{
    struct scullp_dev *dev = filp->private_data; // the first item in quantum set list
    struct scullp_dev *dptr; // dynamic ptr
    
    /* refer to p. 61 for layout of scull */

    // quantum = size of each quantum = a couple of pages 
    //         = PAGE_SIZE * (2 ^ order)
    int quantum = PAGE_SIZE << dev->order; // order is the power n of 2^n
    
    // qset = # of quantums in a quantum set, actually a length
    int qset = dev->qset; 

    // itemsize = (sizeof quantum) * (# of quantums in qset) 
    //          = size of a quantum set
    int itemsize = quantum * qset; 
    
    int item, s_pos, q_pos, rest; 
    ssize_t retval = 0;


    if(mutex_lock_interruptible(&dev->mutex)){ // return 0 on success
        return -ERESTARTSYS;
    }
    if(*f_pos > dev->size){ // out of bound
        goto nothing;
    }
    if(*f_pos + count > dev->size){
        count = dev->size - *f_pos; // change the value of # of bytes to read
    }

    /* find listitem, qset index, and offset in the quantum */
    // item = # of quantum sets required to reach f_pos
    item = ((long) *f_pos) / itemsize;

    // offset in the last quantum set
    rest = ((long) *f_pos) % itemsize;

    // item * sizeof(quantum set) + rest = *f_pos 

    // after we have got the last quantum set, we need to figure out which quantum

    // s_pos (inside-qSet POSition) = the maximum quantum that NOT exceeds *f_pos
    s_pos = rest / quantum;

    // q_pos (Quantum POSition) = the offset in such quantum
    q_pos = rest % quantum;

    /* Again, if you are confused, go back to p. 61 of LDD3 */

    /* follow the list of quantum sets up to the right quantum set */
    dptr = scullp_follow(dev, item);

    // if the quantum set we have found is NULL, goto nothing
    if(!dptr->data){
        goto nothing;
    }

    /** 
     * quantum set we have found is not NULL, but the quantum inside such qset 
     * is NULL
     */ 
    if(!dptr->data[s_pos]){
        goto nothing;
    }

    /**
     * if the exact quantum is not NULL, but the amount of data we'd like to read
     * (count) exceeds this quantum's rest space, reduce count s.t. only reads 
     * up to the end of this quantum.
     */
    if(count > quantum - q_pos){
        count = quantum - q_pos;
    }

    // copy data from kernel space to user space
    if(copy_to_user(buf, dptr->data[s_pos]+q_pos, count)){
        retval = -EFAULT;
        goto nothing;
    }

    // now, the data has been successfully copied to user

    // release the device mutex acquired
    mutex_unlock(&dev->mutex);

    // update the file position
    *f_ops += count;
    return count;

    nothing:
        mutex_unlock(&dev->mutex);
        return retval;
}


/**
 * write - data management
 * @filp: file pointer (for the device)
 * @buf: starting address in user space for writing to kernel space
 * @count: # of bytes to write to the device
 * @f_pos: starting address in kernel space to be written by user
 * @return: the number of bytes written to device on success
 */
ssize_t scullp_write(struct file *filp, const char __user *buf, size_t count,
                loff_t *f_pos)
{
    /* reference to scullp_read */
    struct scullp_dev *dev = filp->private_data;
    struct scullp_dev *dptr;
    int quantum = PAGE_SIZE << dev->order;
    int qset = dev->qset;
    int itemsize = quantum *qset;
    int item, s_pos, q_pos, rest;
    ssize_t retval = -ENOMEM; /* our most likely error - no memory */

    if(mutex_lock_interruptible(&dev->mutex)){
        return -ERESTARTSYS;
    }

    /* calculate corresponding values to find the exact quantum */
    item = ((long) *f_pos) / itemsize;
    rest = ((long) *f_pos) % itemsize;
    s_pos = rest / quantum;
    q_pos = rest / quantum;

    /** 
     * folllow the qset list up to the right qset
     * meanwhile, allocate memory and fill with const byte 0 (kmalloc + memset)
     */ 
    dptr = scullp_follow(dev, item);
    
    // if such qset is NULL, then allocate quantum set
    if(!dptr->data){
        /**
         * allocate memory of size = sizeof(qset)
         * 
         * recall that a quantum set is an array of pointers to quantums 
         * of length qset
         * GFP_KERNEL means allocating normal kernel ram
         */
        dptr->data = kmalloc(qset * sizeof(void *), GFP_KERNEL);

        // if kmalloc fails, goto nomem
        if(!dptr->data){
            goto nomem;
        }

        /** 
         * otherwise, just like what we have done in scullp_follow():
         * After kmalloc, needs to fill such kmalloc'd memory with constant
         * byte 0.
         * You may have noticed here we filled such memory using char *.
         * I believe it is because what the user would write to the device is 
         * more likely to be string. Anyway, the size of pointer does not change
         * no matter what it's pointing to.
         */ 
        memset(dptr->data, 0, qset * sizeof(char *));
    }

    // if the quantum is NULL, then allocate a quantum
    if(!dptr->data[s_pos]){
        // allocate 2^(dptr->order) free pages
        dptr->data[s_pos] = 
            (void *)__get_free_pages(GFP_KERNEL, dptr->order);
        
        if(!dptr->data[s_pos]){
            goto nomem;
        }
        /** 
         * fill the allocated space with const byte 0.
         * Such space is of size PAGE_SIZE * 2^(dptr->order)
         */
        memset(dptr->data[s_pos], 0, PAGE_SIZE << dptr->order);
    }

    // write only up to the end of this quantum
    if(count > quantum - q_pos){
        count = quantum - q_pos;
    }

    if(copy_from_user(dptr->data[s_pos] + q_pos, buf, count)){
        retval = -EFAULT;
        goto nomem;
    }

    *f_pos += count;

    // since we have written to the device, need to update device's size
    if(dev->size < *f_pos){
        dev->size = *f_pos;
    }

    mutex_unlock(&dev->mutex);
    return count;

    nomem:
        mutex_unlock(&dev->mutex);
        return retval;
} 

/**
 * ioctl() implementation
 * @filp: file pointer (for the device)
 * @cmd:
 * @arg:
 * @return:
 */
long scullp_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    int err = 0, ret = 0, tmp;

    //
}

/**
 * llseek() is used to change the current read/write position in a file
 * @filp: file pointer (for the device)
 * @off: 64 bits wide offset
 * @whence: used for specifying different operations
 * @return: the new position
 */
loff_t scullp_llseek(struct file *filp, loff_t off, int whence)
{
    struct scullp_dev *dev = filp->private_data;
    long newpos; // new position

    switch(whence){
        case 0: /* SEEK_SET - set position to be off */
            newpos = off;
            break;
        
        case 1: /* SEEK_CUR */
            newpos = filp->f_pos + off;
            break;

        case 2: /* SEEK_END */
            newpos = dev->size + off;
            break;

        default: /* cannot happen */
            return -EINVAL;
    }

    // check if newpos is invalid
    if(newpos < 0){
        return -EINVAL;
    }

    // now the newpos is valid, then update f_pos in the file
    filp->f_pos = newpos;
    return newpos;
} 

/**
 * mmap is implemented in mmap.c
 * The extern keyword extends the function’s visibility to the whole program, 
 * the function can be used (called) anywhere in any of the files of the whole 
 * program, provided those files contain a declaration of the function.
 * 
 * @link: https://www.geeksforgeeks.org/understanding-extern-keyword-in-c/
 */
extern int scullp_mmap(struct file *filp, struct vm_area_struct *vma); 


/**
 * file operations
 */
struct file_operations scullp_fops = {
    .owner =          THIS_MODULE,
    .llseek =         scullp_llseek,
    .read =           scullp_read,
    .write =          scullp_write,
    .unlocked_ioctl = scullp_ioctl,
    .mmap =	          scullp_mmap,
    .open =	          scullp_open,
    .release =        scullp_release,
    .read_iter =      scull_read_iter,
    .write_iter =     scull_write_iter,
}; 


/**
 * scullp_trim() is to empty out the scullp device, must be called with the 
 * device mutex held. Invoked either i) by scull_open() when the file is opened 
 * for writing; or ii) by module cleanup function to return "memory used by 
 * scullp" to the system.  
 * 
 * @dev: ptr to the device
 * @return:
 * 
 * @link: http://ece-research.unm.edu/jimp/310/slides/linux_driver4.html
 */
int scullp_trim(struct scullp_dev *dev)
{
    struct scullp_dev *next, *dptr;
    int qset = dev->qset; /* dev is not-NULL */
    int i; // working index

    // if there are active mappings, do not trim
    if(dev->vmas){
        return -EBUSY;
    }

    /**
     * It is worth to note here that there are three steps of freeing
     *      1) free the quantums (consists of >= 1 page(s))
     *      2) free the data field of qset (ptr to the array of quantums)
     *      3) free the quantum set itself (what "next" points to)
     */ 

    // start to trim, traverse all quantum sets
    for(dptr = dev; dptr; dptr = next){
        // if the data field of quantum set is not NULL, free it
        if(dptr->data){
            for(i = 0; i < qset; i++){ // qset = # of quantums in a qset
                if(dptr->data[i]){
                    /** 
                     * use free_pages() to free pages
                     * because quantum consists of 1 or more pages
                     * and we used __get_free_pages() to allocate quantums
                     */  
                    free_pages((unsigned long)(dptr->data[i]), dptr->order);
                }
            }

            /** 
             * after freeing all quantums, need to free the data field of qset
             * use kfree() to free such because we used kmalloc() to allocate 
             * qset->data
             */ 
            kfree(dptr->data);
            dptr->data = NULL;
        }

        // store the ptr to the next qset
        next = dptr->next;

        // then, free current qset if it is not dev itself
        if(dptr != dev){
            kfree(dptr);
        }
    }

    // update the info of dev
    dev->size = 0;
    dev->qset = scullp_qset;
    dev->order = scullp_order;
    dev->next = NULL;
    return 0;
} 

/**
 * Register the device (with minor number = index)
 * Set up the char_dev (cdev) structure for this device
 * @dev: ptr to the device to be registered
 * @index: index of the device (minor number)
 * @caller: scullp_init()
 */ 
static void scullp_setup_cdev(struct scullp_dev *dev, int index)
{
    // err - used later for retval of cdev_add()
    // devno - register scullp using MKDEV with major number and minor number
    int err, devno = MKDEV(scullp_major, index); // only set the value of devno

    // allocate the initialized cdev structure
    cdev_init(&dev->cdev, &scullp_fops);

    // update fields in allocated cdev
    dev->cdev.owner = THIS_MODULE;
    dev->cdev.ops = &scullp_fops;

    /**
     * Once the cdev structure is set up, the final step is to tell the kernel 
     * about it.
     * More details, see scull/main.c
     */
    err = cdev_add(&dev->cdev, devno, 1); 

    if(err){
        printk(KERN_NOTICE "Error %d when adding scull%d", err, index);
    }
}

/**
 * Finally, the module stuff
 */ 
int scullp_init(void)
{
    int result, i;
    dev_t dev = MKDEV(scullp_major, 0);

    // register the major number and accept a dynamic number
    if(scullp_major){
        result = register_chrdev_region(dev, scullp_devs, "scullp");
    }
    else{
        result = alloc_chrdev_region(&dev, 0, scullp_devs, "scullp");
        scullp_major = MAJOR(dev);
    }

    if(result < 0){
        return result;
    }

    /**
     * allocate the devices - we cannot have them static because the number can 
     * be specified at load time.
     */
    scullp_devices = kmalloc(scullp_devs * sizeof(struct scullp_dev), GFP_KERNEL);
    if(!scullp_devices){
        result = -ENOMEM;
        goto fail_malloc;
    } 

    /**
     * after kmalloc(), again, use memset() to fill such space with constant 
     * bytes 0
     */
    memset(scullp_devices, 0, scullp_devs * sizeof(struct scullp_dev));

    /**
     * for each device allocated, set its corresponding info (order, qset)
     * then, initialize the mutex
     */
    for(i = 0; i < scullp_devs; i++){
        scullp_devices[i].order = scullp_order;
        scullp_devices[i].qset = scullp_qset;
        mutex_init(&scullp_devices[i].mutex);
        scullp_setup_cdev(scullp_devices + i, i);// increment pointer
    } 
    
#ifdef SCULLP_USE_PROC /* only when available */
    proc_create("scullpmem", 0, NULL, proc_ops_wrapper(&scullp_proc_ops, scullp_pops));
#endif

    return 0;

    fail_malloc:
        unregister_chrdev_region(dev, scullp_devs);
        return result;
}

/**
 * scullp cleanup function
 * 
 */
void scullp_cleanup(void)
{
    int i;

#ifdef SCULLP_USE_PROC
    remove_proc_entry("scullpmem", NULL);
#endif

    for(i = 0; i < scullp_devs; i++){
        // cleanup the cdev in each scullp device
        cdev_del(&scullp_devices[i].cdev);
        scullp_trim(scullp_devices + i); // TODO: why using i here???
        /** 
         * this is simply pointer arithmetic
         * when we increment a pointer, we go to the next pointer
         * equivalent to: ptr + i * sizeof(ptr)
         */ 
    }
    kfree(scullp_devices);
    unregister_chrdev_region(MKDEV(scullp_major, 0), scullp_devs);
} 


module_init(scullp_init);
module_exit(scullp_cleanup);