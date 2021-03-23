/*
 * access.c -- the files with access control on open
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
 * $Id: access.c,v 1.17 2004/09/26 07:29:56 gregkh Exp $
 */

/* FIXME: cloned devices as a use for kobjects? */
 
#include <linux/kernel.h> /* printk() */
#include <linux/module.h>
#include <linux/slab.h>   /* kmalloc() */
#include <linux/fs.h>     /* everything... */
#include <linux/errno.h>  /* error codes */
#include <linux/types.h>  /* size_t */
#include <linux/fcntl.h>
#include <linux/cdev.h>
#include <linux/tty.h>
#include <asm/atomic.h>
#include <linux/list.h>
#include <linux/cred.h> /* current_uid(), current_euid() */
#include <linux/sched.h>
#include <linux/sched/signal.h>

#include "scull.h"        /* local definitions */

static dev_t scull_a_firstdev; /* where our range begins */

/*
 * These devices fall back on the main scull operations. They only
 * differ in the implementation of open() and close()
 */


/*******************************************************************************
 *
 * The first device is the single-open one,
 * it has an hw structure(scull_s_device) and an open count(scull_s_available)
 */

// static variables have a property of preserving their value even out of their scope
static struct scull_dev scull_s_device;
static atomic_t scull_s_available = ATOMIC_INIT(1);

static int scull_s_open(struct inode *inode, struct file *filp)
{
    // get the device info (stored in scull_s_device)
    struct scull_dev *dev = &scull_s_device; // note that scull_s_device is static

    if(!atomic_dec_and_test(&scull_s_available)){
        atomic_inc(&scull_s_available);
        return -EBUSY; // already open
    }

    // if the device's access mode is write only, trim the device
    if((filp->f_flags & O_ACCMODE) == O_WRONLY){
        scull_trim(dev);
    }

    filp->private_data = dev;
    return 0;
}


static int scull_s_release(struct inode *inode, struct file *filp)
{
    atomic_inc(&scull_s_available); // release the device
    return 0;
}

/*
 * The other operations for the single-open device come from the bare device
 */
struct file_operations scull_sngl_fops = {
    .owner =          THIS_MODULE,
    .llseek =         scull_llseek,
    .read =           scull_read,
    .write =          scull_write,
    .unlocked_ioctl = scull_ioctl,
    .open =           scull_s_open,
    .release =        scull_s_release,
};


/*******************************************************************************
 *
 * Next, the "uid" device. It can be opened multiple times by the
 * same user, but access is denied to other users if the device is open
 */

static struct scull_dev scull_u_device;
static int scull_u_count;   /* # of times the device is opened by user. 
                               initialized to 0 by default */
static uid_t scull_u_owner; /* the owner's uid of this device.
                               initialized to 0 by default */
static DEFINE_SPINLOCK(scull_u_lock); // set scull_u_lock to be a spinlock_t struct


static int scull_u_open(struct inode *inode, struct file *filp)
{
    struct scull_dev *dev = &scull_u_device; // get the device info

    // acquire the lock (for scull_u_owner, scull_u_count)
    spin_lock(scull_u_lock);

    // if all of the conditions evaluates to true, then open failed
    if(scull_u_count // already open
            && (scull_u_owner != current_uid().val) // owner is not current uid
            && (scull_u_owner != current_euid().val) // owner is not effective uid (did not "su")
            && !capable(CAP_DAC_OVERRIDE)){ // not root
        
        spin_unlock(&scull_u_lock);
        return -EBUSY;
    }

    if(scull_u_count == 0){ // means this device is the first time open
        scull_u_owner = current_uid().val;
    }

    scull_u_count++;
    spin_unlock(&scull_u_lock);

    // then, everything else is copied from the bare scull device
    if((filp->f_flags & O_ACCMODE) == O_WRONLY){
        scull_trim(dev);
    }
    filp->private_data = dev;
    return 0; // success
}

static int scull_u_release(struct inode *inode, struct file *filp)
{
    spin_lock(&scull_u_lock);
    scull_u_count--;
    spin_unlock(&scull_u_lock);
    return 0;
}


/*
 * The other operations for the device come from the bare device
 */
struct file_operations scull_user_fops = {
    .owner =          THIS_MODULE,
    .llseek =         scull_llseek,
    .read =           scull_read,
    .write =          scull_write,
    .unlocked_ioctl = scull_ioctl,
    .open =           scull_u_open,
    .release =        scull_u_release,
};


/************************************************************************
 *
 * Next, the device with blocking-open based on uid
 * blocking-open requires a wait queue
 */
static struct scull_dev scull_w_device;
static int scull_w_count; /* # of times the device is opened by user. 
                             initialized to 0 by default */
static uid_t scull_w_owner; /* the owner's uid of this device.
                               initialized to 0 by default */
static DECLARE_WAIT_QUEUE_HEAD(scull_w_wait);
static DEFINE_SPINLOCK(scull_w_lock);

// method used to check for availability of device
static inline int scull_w_available(void)
{
    return scull_w_count == 0 ||
            scull_w_owner == current_uid().val ||
            scull_w_owner == current_euid().val ||
            capable(CAP_DAC_OVERRIDE);
}

static int scull_w_open(struct inode *inode, struct file *filp)
{
    struct scull_dev *dev = &scull_w_device; // get device info

    // acquire the lock
    spin_lock(&scull_w_lock);

    // while availability check failed
    while(! scull_w_available()){

        // unlock the device
        spin_unlock(&scull_w_lock);

        /** 
         * check if the device's file status flag is non-block
         * if yes, then need to try again
         */ 
        if(filp->f_flags & O_NONBLOCK){
            return -EAGAIN;
        }

        // if scull_w_available() evaluates to false, then wait queue keeps 
        // sleeping and return -ERESTARTSYS
        if(wait_event_interruptible(scull_w_wait, scull_w_available())){
            return -ERESTARTSYS; // tell the fs layer to handle it
        }

        spin_lock(&scull_w_lock);
    }

    /* then, everything else is copied from the bare scull device */
    if((filp->f_flags & O_ACCMODE) == O_WRONLY){
        scull_trim(dev);
    }
    filp->private_data = dev;
    return 0; // success
}

static int scull_w_release(struct inode *inode, struct file *filp)
{
    int temp; // used to store # of devices open after release

    // acquire the lock before performing operations
    spin_lock(&scull_w_lock);
    scull_w_count--;
    temp = scull_w_count;
    spin_unlock(&scull_w_lock);

    // check if # of this device open is 0
    if(temp == 0){
        // if it's 0, meaning that current uid has done its job, then awake other uid
        wake_up_interruptible_sync(&scull_w_wait);
    }

    return 0;
}

/*
 * The other operations for the device come from the bare device
 */
struct file_operations scull_wusr_fops = {
	.owner =      THIS_MODULE,
	.llseek =     scull_llseek,
	.read =       scull_read,
	.write =      scull_write,
	.unlocked_ioctl = scull_ioctl,
	.open =       scull_w_open,
	.release =    scull_w_release,
};


/************************************************************************
 *
 * Finally the `cloned' private device. This is trickier because it
 * involves list management, and dynamic allocation.
 */

/* The clone-specific data structure includes a key field */
struct scull_listitem{
    struct scull_dev device;
    dev_t key;
    struct list_head list;
};

/* The list of devices, and a lock to protect it */
static LIST_HEAD(scull_c_list);
static DEFINE_SPINLOCK(scull_c_lock);

/* A placeholder scull_dev which really just holds the cdev stuff. */
static struct scull_dev scull_c_device;

/** 
 * Look for a device in the list (scull_listitem) OR create one if missing 
 * @key: the key used to find the device
 * @return: ptr to scull_dev
 */
static struct scull_dev *scull_c_lookfor_device(dev_t key)
{
    // used for scull_listitem
    struct scull_listitem *lptr;

    /**
     * list_for_each_entry(pos, head, member) -	iterate over list of given type
     * @pos:	the type * to use as a loop cursor.
     * @head:	the head for your list.
     * @member:	the name of the list_head within the struct.
     */
    list_for_each_entry(lptr, &scull_c_list, list){
        if(lptr->key == key){
            return &(lptr->device);
        }
    }

    // not found, then allocate that
    lptr = kmalloc(sizeof(struct scull_listitem), GFP_KERNEL);
    if(!lptr){
        return NULL;
    }

    // after allocating scull_listitem, needs to initialize its fields
    
    // of course, after kmalloc, what we need to do is to fill with const bytes 0
    memset(lptr, 0, sizeof(struct scull_listitem));
    lptr->key = key; // update the key
    scull_trim(&(lptr->device)); // trim the allocated lptr->device
    mutex_init(&lptr->device.lock); // init the device's mutex field

    // then, add to the list (scull_listitem)
    list_add(&lptr->list, &scull_c_list);

    // return the address of the newly allocated and initialized device
    return &(lptr->device);
}

static int scull_c_open(struct inode *inode, struct file *filp)
{
    struct scull_dev *dev;
    dev_t key;

    /** 
     * task_struct   current
     * signal_struct signal
     * tty_struct    tty (NULL if no tty)
     * @link: https://elixir.bootlin.com/linux/latest/source/include/linux/sched/signal.h#L164
     */ 
    if(!current->signal->tty){ // if no tty
        PDEBUG("Process \"%s\" has no ctl tty\n", current->comm);
        return -EINVAL;
    }

    // get the device number according to tty and store it to key
    key = tty_devnum(current->signal->tty);

    // look for a scullc device in the list
    // before that, need to acquire the spinlock first
    spin_lock(&scull_c_lock);
    dev = scull_c_lookfor_device(key);
    spin_unlock(&scull_c_lock);

    // if we cannot find such device, then return -ENOMEM
    if(!dev){
        return -ENOMEM;
    }

    // if we can find such device, then everything else is copied from the bare
    // scull device
    if((filp->f_flags & O_ACCMODE) == O_WRONLY){
        scull_trim(dev);
    }
    filp->private_data = dev;
    return 0; // success
}


static int scull_c_release(struct inode *inode, struct file *filp)
{
    /*
	 * Nothing to do, because the device is persistent.
	 * A `real' cloned device should be freed on last close
	 */
    return 0;
}


struct file_operations scull_priv_fops = {
	.owner =    THIS_MODULE,
	.llseek =   scull_llseek,
	.read =     scull_read,
	.write =    scull_write,
	.unlocked_ioctl = scull_ioctl,
	.open =     scull_c_open,
	.release =  scull_c_release,
};



/************************************************************************
 *
 * And the init and cleanup functions come last
 */

static struct scull_adev_info{
    char *name;
    struct scull_dev *sculldev;
    struct file_operations *fops;
} scull_access_devs[] = { // define an array (len=4) of struct scull_adev_info
    {"scullsingle", &scull_s_device, &scull_sngl_fops},
    {"sculluid",    &scull_u_device, &scull_user_fops},
    {"scullwuid",   &scull_w_device, &scull_wusr_fops},
    {"scullpriv",   &scull_c_device, &scull_priv_fops}
};
#define SCULL_N_ADEVS 4 // # of devices

/**
 * set up a single device
 */
static void scull_access_setup(dev_t devno, struct scull_adev_info *devinfo)
{
    struct scull_dev *dev = devinfo->sculldev;
    int err;

    // initialize the device structure (quantum, qset, mutex lock)
    dev->quantum = scull_quantum;
    dev->qset = scull_qset;
    mutex_init(&dev->lock);

    // initialize the cdev
    cdev_init(&dev->cdev, devinfo->fops);

    // set the name(devinfo->name) of a kobject (cdev.kobject)
    kobject_set_name(&dev->cdev.kobj, devinfo->name);

    // set the owner of cdev
    dev->cdev.owner = THIS_MODULE;

    // then, cdev_add() to add cdev to the system
    err = cdev_add(&dev->cdev, devno, 1);

    if(err){
        printk(KERN_NOTICE "Error %d adding %s\n", err, devinfo->name);
        
        // decrement refcount for object.
        kobject_put(&dev->cdev.kobj);
    } else{
        printk(KERN_NOTICE "%s registered at %x\n", devinfo->name, devno);
    }
} 


int scull_access_init(dev_t firstdev)
{
    int result, i;

    // get our number space
    result = register_chrdev_region(firstdev, SCULL_N_ADEVS, "sculla");
    if(result < 0){
        printk(KERN_WARNING "sculla: device number registration failed\n");
		return 0;
    }

    scull_a_firstdev = firstdev;

    // set up each device
    for(i = 0; i < SCULL_N_ADEVS; i++){
        scull_access_setup(firstdev + i, scull_access_devs + i)
    }
}


/*
 * This is called by cleanup_module or on failure.
 * It is required to never fail, even if nothing was initialized first
 */
void scull_access_cleanup(void)
{
    struct scull_listitem *lptr, *next;
    int i;

    // clean up the static devs
    for(i = 0; i < SCULL_N_ADEVS; i++){
        struct scull_dev *dev = scull_access_devs[i].sculldev;
        cdev_del(&dev->cdev);
        scull_trim(scull_access_devs[i].sculldev);
    }

    // clean up the cloned devices (scull_c_list is the list head)
    /**
     * list_for_each_entry_safe(pos, n, head, member)
     *  - iterate over list of given type safe against removal of list entry
     * @pos:	the type * to use as a loop cursor.
     * @n:		another type * to use as temporary storage
     * @head:	the head for your list.
     * @member:	the name of the list_head within the struct.
     */
    list_for_each_entry_safe(lptr, next, &scull_c_list, list){
        // operate on lptr
        list_del(&lptr->list);
        scull_trim(&(lptr->device));
        kfree(lptr);
    }

    // free up the number space
    unregister_chrdev_region(scull_a_firstdev, SCULL_N_ADEVS);
    return;
}