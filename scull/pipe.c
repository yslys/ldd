/**
 * a Blocking I/O example
 * a pipe-like device
 * read - using wait_event();
 * write - using prepare_to_wait() and finish_wait();
 * Normally, only one wait mechanism is chosen. 
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
                                       /* wait_queue_head_t contains info about 
                                        * sleeping process and exactly how it 
                                        * would like to be woken up
                                        */
    char *buffer, *end;                /* begin of buf, end of buf */
    int buffersize;                    /* used in pointer arithmetic */
    char *rp, *wp;                     /* where to read, where to write 
                                          read position and write position */
                                       /* wp = rp+1  => device buffer full
                                          wp = rp    => device buffer empty
                                          o.w.       => there's space to write
                                        */
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
 * @brief release the device
 * 
 * @param filp 
 * @return int 
 */
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

    /**
     * When you compile the driver, you can enable messaging to make it easier 
     * to follow the interaction of different processes. (PDEBUG)
     */ 
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
 * Advanced sleeping using the wait queue mechanism
 * Wait for space for writing; caller must hold device semaphore.  
 * On error the semaphore will be released before returning.
 * @caller: scull_p_write()
 */
static int scull_getwritespace(struct scull_pipe *dev, struct file *filp)
{
    /**
     * Note that this is advanced sleeping using the wait queue mechanism
     */
    // while there's no space for writing
    while(spacefree(dev) == 0){
        // create and initialize a struct wait_queue_entry, with its name = wait.
        DEFINE_WAIT(wait);

        // unlock the device (drop the semaphore) before wait()
        mutex_unlock(&dev->lock);

        // if the write is Non-block, then try again
        if(filp->f_flags & O_NONBLOCK){
            return -EAGAIN;
        }

        // else, the write is NOT non-block, then put it to sleep
        // print the command of current process, and go to sleep (if enabled debugging)
        PDEBUG("\"%s\" writing: going to sleep\n",current->comm);

        /** 
         * do several things while spinlock acquired;
         * wait this writing process to dev->outq;
         * set the state of this current process to mark it as being asleep;
         * by changing the STATE of the process, we have changed the way the 
         * scheduler treats a process, but have not yet yielded (give up) the processor
         */
        prepare_to_wait(&dev->outq, &wait, TASK_INTERRUPTIBLE);
        // prepare_to_wait_exclusive() to set the "exclusive" falg in the wait queue entry
        // and adds current process to the end of wait queue.

        /** 
         * hence, in this final step of putting current process to sleep, we are 
         * going to give up the processor.
         * BUT before that, we need to check the condition we are sleeping for.
         * Failure to do that will invite a race condition - 
         *      What if the condition came true while we were engaged in the process,
         *      and some other thread has just tried to wake us up? We would miss
         *      the wakeup altogether and sleep longer than intended.
         *      
         */ 
        if(spacefree(dev) == 0){
            /**
             * By checking the condition after setting the process state, we are
             * covered against all possible sequences of events. If the condition 
             * we are waiting for had come about before setting the process state, 
             * we notice in this check and not actually sleep. If the wakeup 
             * happens thereafter, the process is made runnable whether or not 
             * we have actually gone to sleep yet.
             */ 

            /**
             * schedule() - the way to invoke the scheduler and yield (give up)
             *              the CPU.
             * Whenever we call this function, we are telling the kernel to consider
             * which process should be running and to switch control to that process
             * if necessary. So we never know how long it will be before schedule()
             * returns to our code.
             * 
             * @return: schedule() will not return until the process is in a 
             *          runnable state.
             */ 
            schedule();

            /**
             * One thing worth looking at is - what happens if the wakeup happens 
             * between the test in the if statement and the call to schedule? 
             * In that case, all is well. The wakeup resets the process state to 
             * TASK_RUNNING and schedule returns—although not necessarily right 
             * away. As long as the test happens after the process has put itself 
             * on the wait queue and changed its state, things will work.
             */
        }

        /**
         * if the if-condition is true, then we do not worry about doing the cleanup
         * because schedule() will reset the task state to TASK_RUNNING.
         * However, if the if-condition was false, that means we will not call 
         * schedule(), and current process was not necessary to sleep. Hence, we 
         * need to manually i) reset the task state to TASK_RUNNING;
         *                  ii) remove the process from the wait queue, or it may
         *                      be awakened more than once.
         * The above is done by calling finish_wait() defined in <linux/wait.h>
         */ 
        finish_wait(&dev->outq, &wait);

        // signal_pending() tells us whether we were awakened by a signal
        // note that @current is a pointer to a struct task_struct
        if(signal_pending(current)){ // if we were awakened by a signal
            return -ERESTARTSYS; // return to the user and let them try again later
        }
        // otherwise, we re-acquire the semaphore, and test again for free space (in while-loop)
        if(mutex_lock_interruptible(&dev->lock)){
            return -ERESTARTSYS;
        }
    }

    return 0; // on success, meaning there's free space to write
}


/**
 * write method
 */
static ssize_t scull_p_write(struct file *filp, const char __user *buf, 
                             size_t count, loff_t *f_ops)
{
    struct scull_pipe *dev = filp->private_data;
    int result;

    // whenever we want to operate on the device, need to acquire the lock of it.
    if(mutex_lock_interruptible(&dev->lock)){
        return -ERESTARTSYS;
    }

    // make sure there's space to write
    result = scull_getwritespace(dev, filp);
    if(result){
        return result;
    }

    // now, there's space to write
    // first, find out how many bytes could be written
    //      count - from user, 
    //      spacefree(dev) - actual space available
    count = min(count, (size_t)spacefree(dev));

    // if write position is greater than or equal to read position
    // can only write to dev->end, i.e.
    // can write at most (dev->end - dev->wp) bytes
    if(dev->wp >= dev->rp){
        count = min(count, (size_t)(dev->end - dev->wp));
    }
    // else, if read position exceeds write position,
    // can write up to read position, but not exceeding it.
    // don't know why? Imagine the buffer of the device to be a circular buffer
    else{
        count = min(count, (size_t)(dev->rp - dev->wp - 1));
    }

    PDEBUG("Going to accept %li bytes to %p from %p\n", (long)count, dev->wp, buf);

    // start writing from user space to kernel space
    if(copy_from_user(dev->wp, buf, count)){
        // copy_from_user() fails, then unlock the device and return
        mutex_unlock(&dev->lock);
        return -EFAULT;
    }

    // now, it means writing succeeded
    // update the write position
    dev->wp += count;

    // not yet finished, need to check if wp reaches the end of device's buffer
    if(dev->wp == dev->end){
        dev->wp = dev->buffer; // update write position to be the start of buffer
    }

    // now, we can release the semaphore of the device safe
    mutex_unlock(&dev->lock);

    // can we return now? Not yet! there might be readers waiting, wake them up!
    wake_up_interruptible(&dev->inq);

    // remember that we still have a data structure called async_queue in scull_pipe
    // this is discused in Chapter 5.
    // TODO
    if(dev->async_queue){
        kill_fasync(&dev->async_queue, SIGIO, POLL_IN);
    }

    PDEBUG("\"%s\" did write %li bytes\n",current->comm, (long)count);
    return count;
}

/**
 * poll() syscall implementation - sychronous I/O multiplexing
 * 
 * 
 * @wait: pointer to a poll_table struct, declared in <linux/poll.h>. It is passed
 *        to the driver method s.t. the driver can load it with every wait queue
 *        that could wake up the process and change the status of the poll operation.
 * @wait: pointer to the poll_table structure
 * @return: a bit mask indicating the operation could be performed without blocking
 * 
 * This method is in charge of the following 2 steps:
 * 1. Call poll_wait() on one or more wait queues that could indicate a change 
 *      in the poll status. If no file descriptors are currently available for 
 *      I/O, the kernel causes the process to wait on the wait queues for all 
 *      file descriptors passed to the system call.
 * 2. Return a bit mask describing the operations (if any) that could be 
 *      immediately performed without blocking.
 */
static unsigned int scull_p_poll(struct file *filp, poll_table *wait)
{
    struct scull_pipe *dev = filp->private_data;
    unsigned int mask = 0;

    // acquire the lock first
    mutex_lock(&dev->lock);

    // add two wait queues to the poll_table structure
    poll_wait(filp, &dev->inq, wait);
    poll_wait(filp, &dev->outq, wait);

    // if read position != write position => device is not empty => readable
    if(dev->rp != dev->wp){
        mask = mask | POLLIN | POLLRDNORM; // readable (more info, see macros.c)
    }

    // if spacefree() returns non-negative number
    if(spacefree(dev)){
        mask |= POLLOUT | POLLWRNORM; // writable (more info, see macros.c)
    }

    /**
     * this poll() method does not implement end-of-file support because
     * scullpipe does not support an end-of-file condition. 
     * For most real devices, the poll method should return POLLHUP if no more data
     * is (or will become) available. If the caller used the select system call, 
     * the file is reported as readable. Regardless of whether poll or select is 
     * used, the application knows that it can call read without waiting forever,
     * and the read method returns, 0 to signal end-of-file.
     * 
     * Implementing end-of-file in the same way as FIFOs do would mean checking 
     * dev->nwriters, both in read and in poll, and reporting end-of-file (as 
     * just described) if no process has the device opened for writing.
     * 
     * For more details, refer to ldd3 p. 165
     */

    // after we may have made the mask to be readable and/or writable or neither,
    // unlock the device
    mutex_unlock(&dev->lock);

    return mask;
}


/******************************************************************************/

/* FIXME this should use seq_file */
#ifdef SCULL_DEBUG

static int scull_read_p_mem(struct seq_file *s, void *v)
{
	int i;
	struct scull_pipe *p;

#define LIMIT (PAGE_SIZE-200)        /* don't print any more after this size */
	seq_printf(s, "Default buffersize is %i\n", scull_p_buffer);
	for(i = 0; i<scull_p_nr_devs && s->count <= LIMIT; i++) {
		p = &scull_p_devices[i];
		if (mutex_lock_interruptible(&p->lock))
			return -ERESTARTSYS;
		seq_printf(s, "\nDevice %i: %p\n", i, p);
/*		seq_printf(s, "   Queues: %p %p\n", p->inq, p->outq);*/
		seq_printf(s, "   Buffer: %p to %p (%i bytes)\n", p->buffer, p->end, p->buffersize);
		seq_printf(s, "   rp %p   wp %p\n", p->rp, p->wp);
		seq_printf(s, "   readers %i   writers %i\n", p->nreaders, p->nwriters);
		mutex_unlock(&p->lock);
	}
	return 0;
}

static int scullpipe_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, scull_read_p_mem, NULL);
}

static struct file_operations scullpipe_proc_ops = {
	.owner   = THIS_MODULE,
	.open    = scullpipe_proc_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = single_release
};

#endif

/*
 * Set up a cdev entry.
 */
static void scull_p_setup_cdev(struct scull_pipe *dev, int index)
{
	int err, devno = scull_p_devno + index;
    
	cdev_init(&dev->cdev, &scull_pipe_fops);
	dev->cdev.owner = THIS_MODULE;
	err = cdev_add (&dev->cdev, devno, 1);
	/* Fail gracefully if need be */
	if (err)
		printk(KERN_NOTICE "Error %d adding scullpipe%d", err, index);
}


/*
 * Initialize the pipe devs; return how many we did.
 */
int scull_p_init(dev_t firstdev)
{
	int i, result;

	result = register_chrdev_region(firstdev, scull_p_nr_devs, "scullp");
	if (result < 0) {
		printk(KERN_NOTICE "Unable to get scullp region, error %d\n", result);
		return 0;
	}
	scull_p_devno = firstdev;
	scull_p_devices = kmalloc(scull_p_nr_devs * sizeof(struct scull_pipe), GFP_KERNEL);
	if (scull_p_devices == NULL) {
		unregister_chrdev_region(firstdev, scull_p_nr_devs);
		return 0;
	}
	memset(scull_p_devices, 0, scull_p_nr_devs * sizeof(struct scull_pipe));
	for (i = 0; i < scull_p_nr_devs; i++) {
		init_waitqueue_head(&(scull_p_devices[i].inq));
		init_waitqueue_head(&(scull_p_devices[i].outq));
		mutex_init(&scull_p_devices[i].lock);
		scull_p_setup_cdev(scull_p_devices + i, i);
	}
#ifdef SCULL_DEBUG
	proc_create("scullpipe", 0, NULL, proc_ops_wrapper(&scullpipe_proc_ops,scullpipe_pops));
#endif
	return scull_p_nr_devs;
}

/*
 * This is called by cleanup_module or on failure.
 * It is required to never fail, even if nothing was initialized first
 */
void scull_p_cleanup(void)
{
	int i;

#ifdef SCULL_DEBUG
	remove_proc_entry("scullpipe", NULL);
#endif

	if (!scull_p_devices)
		return; /* nothing else to release */

	for (i = 0; i < scull_p_nr_devs; i++) {
		cdev_del(&scull_p_devices[i].cdev);
		kfree(scull_p_devices[i].buffer);
	}
	kfree(scull_p_devices);
	unregister_chrdev_region(scull_p_devno, scull_p_nr_devs);
	scull_p_devices = NULL; /* pedantic */
}