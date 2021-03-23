/**
 * a Blocking I/O example
 * a pipe-like device
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
    wait_queue_head_t inq, outq;       /* read and write queues */
    char *buffer, *end;                /* begin of buf, end of buf */
    int buffersize;                    /* used in pointer arithmetic */
    char *rp, *wp;                     /* where to read, where to write 
                                          read position and write position */
    int nreaders, nwriters;            /* number of openings for r/w */
    struct fasync_struct *async_queue; /* asynchronous readers */
    struct mutex lock;                 /* mutual exclusion mutex */
    struct cdev cdev;                  /* Char device structure */
}

// parameters (macros are defined in scull.h)
static int scull_p_nr_devs = SCULL_P_NR_DEVS; /* number of pipe devices */
int scull_p_buffer = SCULL_P_BUFFER;          /* buffer size */
dev_t scull_p_devno;                          /* the first device number */

module_param(scull_p_nr_devs, int, 0);
module_param(scull_p_buffer, int, 0);

static struct scull_pipe *scull_p_devices;


/* functions */
static int scull_p_open(struct inode *inode, struct file *filp);
static int scull_p_release(struct inode *inode, struct file *filp);
static ssize_t scull_p_read(struct file *filp, char __user *buf, size_t count,
                            loff_t *f_pos);
static int scull_getwritespace(struct scull_pipe *dev, struct file *filp);
static int spacefree(struct scull_pipe *dev);
static ssize_t scull_p_write(struct file *filp, const char __user *buf, 
                             size_t count, loff_t *f_pos);
static unsigned int scull_p_poll(struct file *filp, poll_table *wait);
static int scull_p_fasync(int fd, struct file *filp, int mode);
static int spacefree(struct scull_pipe *dev);

/*
 * The file operations for the pipe device
 * (some are overlayed with bare scull)
 */
struct file_operations scull_pipe_fops = {
	.owner =	THIS_MODULE,
	.llseek =	no_llseek,
	.read =		scull_p_read,
	.write =	scull_p_write,
	.poll =		scull_p_poll,
	.unlocked_ioctl = scull_ioctl,
	.open =		scull_p_open,
	.release =	scull_p_release,
	.fasync =	scull_p_fasync,
};

static void scull_p_setup_cdev(struct scull_pipe *dev, int index);
int scull_p_init(dev_t firstdev);
void scull_p_cleanup(void);




/**
 * open a scull_pipe device
 * @inode: used to retrieve the corresponding device (in this case, scull_pipe)
 * @filp: used to store the device (filp->private_data is used to store dev info)
 */
static int scull_p_open(struct inode *inode, struct file *filp)
{
    struct scull_pipe *dev;

    // get the scull_pipe device according to inode
    dev = container_of(inode->i_cdev, struct scull_pipe, cdev);
    // then store such device to filp's private_data field
    filp->private_data = dev;

    // acquire the lock, check the retval - 0 on success, negative error code on failure
    if(mutex_lock_interruptible(&dev->lock)){
        return -ERESTARTSYS;
    }

    // if the buffer of this scull_pipe device is empty, then need to allocate it
    if(!dev->buffer){
        // allocate the buffer
        dev->buffer = kmalloc(scull_p_buffer, GFP_KERNEL);
        if(!dev->buffer){
            mutex_unlock(&dev->lock);
            return -ENOMEM;
        }
    }

    // now, the buffer has been allocated, need to update the corresponding fields
    dev->buffersize = scull_p_buffer;
    dev->end = dev->buffer + dev->buffersize;
    dev->rp = dev->wp = dev->buffer; // read ptr and write ptr from the beginning

    /* use f_mode, not f_flags: it's cleaner (fs/open.c tells why) */
    if(filp->f_mode & FMODE_READ){ // if file pointer wants to read
        dev->nreaders++; // then # of readers ++
    }
    if(filp->f_mode & FMODE_WRITE){
        dev->nwriters++;
    }
    mutex_unlock(&dev->lock);

    /**
     * This is used by subsystems that don't want seekable file descriptors.
     * According to the source code of nonseekable_open(), it changes the f_mode
     * of filp to be NOT FMODE_LSEEK and NOT FMODE_PREAD and NOT FMODE_PWRITE.
     */ 
    return nonseekable_open(inode, filp); // return 0 on success (nonseekable open)
}

static int scull_p_release(struct *inode, struct file *filp)
{
    // get the device info from filp->private data
    struct scull_pipe *dev = filp->private_data;

    /**
     * remove this filp from the asynchronously notified filp's 
     * since the 3rd param is 0, it is calling 
     *      return fasync_remove_entry(filp, fapp);
     */
    scull_p_fasync(-1, filp, 0);

    // acquire the lock to make changes to the following
    mutex_lock(&dev->lock);
    if(filp->f_mode & FMODE_READ){
        dev->nreaders--;
    }
    if(filp->f_mode & FMODE_WRITE){
        dev->nwriters--;
    }
    if(dev->nreaders + dev->nwriters == 0){
        // no readers or writers
        kfree(dev->buffer);
        dev->buffer = NULL; /* the other fields are not checked on open */
    }

    mutex_unlock(&dev->lock);
    return 0;
}

/**
 * set up the fasync queue
 * @fd: used in fasync_helper, but not used in this case, as fd is set to -1
 * @filp: the file pointer that holds the dev info
 * @mode: used by fasync_helper()
 * @return: the retval of fasync_helper()
 */ 
static int scull_p_fasync(int fd, struct file *filp, int mode)
{
    struct scull_pipe *dev = filp->private_data;
    
    // set up the fasync queue (file asynchronous)
    return fasync_helper(fd, filp, mode, &dev->async_queue);
}


/**
 * manages both blocking and nonblocking input
 * read @count bytes from the device by @filp and store in @buf
 * @filp: fp that holds the device info
 * @buf: buffer in user space to store what has been read from the device
 * @count: # of bytes to read
 * @f_pos: not used
 * @return: # of bytes read on success, wake writer processes that are waiting
 *          for buffer space to become available.
 */ 
static ssize_t scull_p_read(struct file *filp, char __user *buf, size_t count,
                            loff_t *f_pos)
{
    struct scull_pipe *dev = filp->private_data;

    // acquire the lock, if failed, then return -ERESTARTSYS
    if(mutex_lock_interruptible(&dev->lock)){
        return -ERESTARTSYS;
    }

    /** 
     * the while-loop tests the buffer with device semaphore held.
     * while read position == write position, buffer is empty, nothing to read,
     * Hence, the reading process needs to sleep (sleep in the wait queue).
     * 
     * When we exit from this while loop, we know that the semaphore is held
     * and the buffer contains data that we can use.
     */ 
    while(dev->rp == dev->wp){
        /** 
         * before putting current process to sleep (wait in dev->inq), must 
         * release the lock.
         * if were to sleep holding the semaphore, no writer could have waken
         * the readers waiting in the dev->inq 
         */ 
        mutex_unlock(&dev->lock);

        // if the user has requested non-blocking I/O, then return try again 
        if(filp->f_flags & O_NONBLOCK){
            return -EAGAIN;
        }

        // current->comm stores the command name of the current process
        PDEBUG("\"%s\" reading: going to sleep\n", current->comm);

        /** 
         * sleep current reader proces at the read queue on condition -- 
         *          read position == write position.
         * as long as the condition (dev->rp != dev->wp) evaluates to false,  
         * wait_event_interruptible() will return 0
         */ 
        // the reader will keep sleeping, with device semaphore NOT-held.
        // the code after this will not be executed until 
        //   i) someone calls wake_up functions, OR
        //  ii) this process receives a signal
        if(wait_event_interruptible(dev->inq, (dev->rp != dev->wp))){
            /** 
             * if we enter here, it means something has woken us up.
             * Need to be aware that, the device's semaphore is not held. Hence,
             * it is possible that a writer process calls wake_up_interruptible()
             * to wake this up. However, if dev->rp == dev->wp (nothing to read), 
             * then the condition will still be evaluated to false; thus, this 
             * reader should still sleep (wait_event_interruptible() returns 0),
             * meaning that we cannot enter here.
             *  
             * Since we have entered here, one possibility is that the process 
             * has received a signal.
             * If a signal has arrived and it has not been blocked by the process, 
             * the proper behavior is to let upper layers of the kernel to handle
             * the event -- by returning -ERESTARTSYS. Such value is used
             * internally by the virtual file system (VFS) layer, which either
             * restarts the system call or returns -EINTR to user space.
             */ 
            return -ERESTARTSYS; /* signal: tell the fs layer to handle it */
        }

        /**
         * if there's no signal, we do not yet know for sure that there is data
         * to be read. Somebody else could have been waiting for the data as well,
         * and they might win the race and get the data first. So we must 
         *              **acquire the device semaphore again;** 
         * only then can we test the read buffer again in the while loop, and 
         * truly know that we can return the data in the buffer to the user.
         */ 
        if(mutex_lock_interruptible(&dev->lock)){
            return -ERESTARTSYS;
        }
    }

    /** 
     * now, we have passed the while loop, meaning that 
     *   i) there's data to be manipulated because rp != wp. AND
     *   ii) the device semaphore is HELD.
     * Need to figure out the value of count (# of bytes to read)
     */ 
    if(dev->wp > dev->rp){
        // can read at most (dev->wp - dev->rp) bytes
        count = min(count, (size_t)(dev->wp - dev->rp));
    }
    else{ // read position has exceeded the write position
        // can only read up to dev->end
        count = min(count, (size_t)(dev->end - dev->rp));
    }

    /**
     * After figuring out the value of count, can copy_to_user()
     * Copy the data starting from @dev->rp of @count bytes to @buf in user space
     * copy_to_user() @return: number of bytes that could not be copied. 
     *                         (0 on success)
     * 
     * Note that copy_to_user() may also causes current reader process to sleep.
     * But the device semaphore is held! How could it be?
     * If scull sleeps while copying data between kernel and user space, it does
     * NOT deadlock the system because the kernel will perform the copy to user 
     * space and wakes us up without trying to lock the same semaphore in the 
     * process. 
     * It is important that the device memory array NOT change while device sleeps.
     */
    if(copy_to_user(buf, dev->rp, count)){
        // Read failed -- there are bytes that could not be copied
        // unlock the device and return error code
        mutex_unlock(&dev->lock);
        return -EFAULT;
    } 

    // Here, it means read success, need to update the read position
    dev->rp += count;
    /**
     * the above calculation is pointer arithmetic.
     * rp is a pointer that points to the read position, plus count, will get the 
     * final read position -- two possibilities:
     *   i) not yet reached dev->end (dev->rp != dev->end)
     *   ii) reached dev->end (dev->rp == dev->end)
     * When reaching the end, need to set rp to the beginning of buffer of device
     */ 

    // check if rp reaches the end
    if(dev->rp == dev->end){
        dev->rp = dev->buffer; // set rp to be the beginning of device's buffer
    }

    // then, we can safely unlock the device
    mutex_unlock(&dev->lock);

    /**
     * note that this is a blocked read, meaning that if one user is trying to 
     * read this device, then others trying to write should be blocked.
     * Hence, we need to awake any writers and return
     */
    wake_up_interruptible(&dev->outq); // awake those waiting in out(write) queue
    PDEBUG("\"%s\" did read %li bytes\n",current->comm, (long)count);
    return count;
}

/**
 * how much space is free in device @dev's buffer?
 * Note that this is like a circular buffer
 * @dev: the device involved
 * @caller: scull_getwritespace()
 * @return: free space available
 */
static int spacefree(struct scull_pipe *dev)
{
    if(dev->rp == dev->wp){ // buffer is empty, no data has been written
        return dev->buffersize - 1;
    }

    /**
     * if rp > wp, return (rp - wp) space available
     * if rp < wp, return (buffersize - (wp - rp)) space available
     */ 
    return ((dev->rp - dev->wp + dev->buffersize ) % dev->buffersize) - 1;
}

/**
 * Wait for space for writing; caller must hold device semaphore.  
 * On error the semaphore will be released before returning.
 * @caller: scull_p_write()
 */
static int scull_getwritespace(struct scull_pipe *dev, struct file *filp)
{
    // while there's no space for writing
    while(spacefree(dev) == 0){
        DEFINE_WAIT(wait);

        mutex_unlock(&dev->lock);


    }
}