/*
 * sleepy.c -- the writers awake the readers
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
 * $Id: sleepy.c,v 1.7 2004/09/26 07:02:43 gregkh Exp $
 */

#include <linux/module.h>
#include <linux/init.h>

#include <linux/sched.h>  /* current and everything */
#include <linux/kernel.h> /* printk() */
#include <linux/fs.h>     /* everything... */
#include <linux/types.h>  /* size_t */
#include <linux/wait.h>

MODULE_LICENSE("Dual BSD/GPL");

static int sleepy_major = 0; // major number of this device


/*----------------------------------------------------------------------------*/
/* The code below is on p. 150 */

// Statically define and initialize a wait queue with name - wq
static DECLARE_WAIT_QUEUE_HEAD(wq);

// used as the condition of wait and wakeup
static int flag = 0;

// method for read (put current process to sleep)
static ssize_t sleepy_read(struct file *filp, char __user *buf, 
                           size_t count, loff_t *pos)
{
    /**
     * Kernel code can refer to the current process by accessing the global item 
     * current, defined in <asm/current.h>, which yields a pointer to struct 
     * task_struct, defined by <linux/sched.h>.
     */ 
    printk(KERN_DEBUG "process %i (%s) going to sleep\n", 
                    current->pid, current->comm); 
                    // comm - executable name of process, excluding path, 
                    // in <linux/sched.h>
    
    // wait current process to wait queue wq if flag != 0
    wait_event_interruptible(wq, flag != 0);
    flag = 0;
    printk(KERN_DEBUG "awoken %i (%s)\n", current->pid, current->comm);
}


static ssize_t sleepy_write(struct file *filp, const char __user *buf, 
                            size_t count, loff_t *pos)
{
    printk(KERN_DEBUG "process %i (%s) awakening the readers...\n", 
                    current->pid, current->comm);
    flag = 1;
    wake_up_interruptible(&wq);
    return count; /* succeed, to avoid retrial */
}

/*----------------------------------------------------------------------------*/

struct file_operations sleepy_fops = {
    .owner = THIS_MODULE,
    .read = sleepy_read,
    .write = sleepy_write,
};

static int sleepy_init(void)
{
    int result;

    // register the major number, and accept a dynamic number
    result = register_chrdev(sleepy_major, "sleepy", &sleepy_fops);

    // if register failed
    if(result < 0){
        return result;
    }

    // check if sleepy_major is still 0
    if(sleepy_major == 0){
        sleepy_major = result; // dynamic number
    }

    return 0;
}

static void sleepy_cleanup(void)
{
    unregister_chrdev(sleepy_major, "sleepy");
}

module_init(sleepy_init);
module_exit(sleepy_cleanup);