/*
 * main.c -- the bare scull char module
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
 * Citation: https://github.com/martinezjavier/ldd3/tree/master/scull
 */


#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>

#include <linux/kernel.h>	   /* printk() */
#include <linux/slab.h>		   /* kmalloc() */
#include <linux/fs.h>		     /* file structure represents an open file */
#include <linux/errno.h>   	 /* error codes */
#include <linux/types.h>	   /* size_t */
#include <linux/proc_fs.h>
#include <linux/fcntl.h>	   /* O_ACCMODE */
#include <linux/seq_file.h>
#include <linux/cdev.h>      /* type struct cdev; kernel uses cdev to represent char devices internally */

#include <linux/uaccess.h>	 /* copy_*_user */

#include "scull.h"		       /* local definitions */
#include "access_ok_version.h"
#include "proc_ops_version.h"

/*
 * Our parameters which can be set at load time.
 */
int scull_major = SCULL_MAJOR;
int scull_minor = 0;
int scull_nr_devs = SCULL_NR_DEVS;  /* num of bare scull devices */
int scull_quantum = SCULL_QUANTUM; /* each memory area is a quantum 
                                      optimal size of quantum is SCULL_QUANTUM 
                                      usually 4000 */
int scull_qset = SCULL_QSET; /* quantum set, 4000 or 8000 for 32bits or 64bits*/

/*
 * module params
 * prototype: 
 *    module_param(name, type, perm);
 * params: 
 *    name - name of both the parameter exposed to the user and the variable 
 *           holding the parameter inside your module
 *    type - holds the parameter's data type; 
 *           byte, short, ushort, int, uint, long, ulong, charp, bool, or invbool. 
 *           ushort: unsigned short int
 *           ulong: unsigned long int
 *           charp: pointer to a char
 *           invbool: Boolean whose value is inverted from what the user specifies
 *           (The byte type is stored in a single char and 
 *           the Boolean types are stored in variables of type int.)
 *    perm - permissions of the corresponding file in sysfs
 *           0644: owner can read & write, group can read, everyone else can read
 *           S_Ifoo prototype: 
 *            S_IRUGO - everyone can read (RUGO - R:read, UGO:user, group, others)
 *            S_IWUSR - user can write    (WUSR - W:write, USR:user)
 * 
 * http://books.gigatux.nl/mirror/kerneldevelopment/0672327201/ch16lev1sec6.html
 */
module_param(scull_major, int, S_IRUGO);
module_param(scull_minor, int, S_IRUGO);
module_param(scull_nr_devs, int, S_IRUGO);
module_param(scull_quantum, int, S_IRUGO);
module_param(scull_qset, int, S_IRUGO);

MODULE_AUTHOR("Alessandro Rubini, Jonathan Corbet");
MODULE_LICENSE("Dual BSD/GPL");

struct scull_dev *scull_devices;	/* allocated in scull_init_module */

/*
  Empty out the scull device; must be called with the device semaphore held.
  http://ece-research.unm.edu/jimp/310/slides/linux_driver4.html
  Note:
    struct scull_dev {
      struct scull_qset *data; // Pointer to first quantum set 
      int quantum;             // the current quantum size 
      int qset;                // the current array size 
      unsigned long size;      // amount of data stored here 
      unsigned int access_key; // used by sculluid and scullpriv 
      struct semaphore sem;    // mutual exclusion semaphore 
      struct cdev cdev;        // Char device structure 
    };

    // representation of scull quantum sets
    struct scull_qset {
      void **data;
      struct scull_qset *next;
    };
*/
int scull_trim(struct scull_dev *dev)
{
  struct scull_qset *next, *dptr; // dptr - data pointer
  int qset = dev->qset; // current array size; ("dev" is not NULL)

  for(dptr = dev->data; dptr; dptr = next){
    if(dptr->data){
      for(int i = 0; i < qset; i++){
        kfree(dptr->data[i]);
      }
      kfree(dptr->data);
      dptr->data = NULL;
    }
  }

  dev->size = 0;
  dev->quantum = scull_quantum;
  dev->qset = scull_qset;
  dev->data = NULL;
  return 0;
}

#ifdef SCULL_DEBUG // use proc only if debugging

/*
 * The proc filesystem: function to read and entry
 */
int scull_read_procmem(struct seq_file *s, void *v)
{
  int limit = s->size - 80; 

  for(int i = 0; i < scull_nr_devs && s->count <= limit; i++){
    struct scull_dev *d = &scull_devices[i];
    struct scull_qset *qs = d->data;

    if(mutex_lock_interruptible(&d->lock)){
      return -ERESTARTSYS; // error restart sys
    }

    seq_printf(s, "\nDevice %i: qset %i, q %i, sz %li\n",
                i, d->qset, d->quantum, d->size);
  }
}










/*
 * Kernel uses struct cdev to represent char devices internally.
 * Before kernel invokes device's operations, must allocate and register one or
 * more of struct cdev.
 * Mostly, struct cdev is embedded within a device-specific structure - scull_dev
 * then, 1) allocate a struct scull_dev
 *       2) use cdev_init() to initialize the scull_dev->cdev
 * After initialization, need to tell kernel about that:
 *       call cdev_add()
 */ 

/*
 * Device registration in scull
 * Set up the char_dev structure for this device.
 * kernel uses cdev to represent char devices internally
 */
static void scull_setup_cdev(struct scull_dev *dev, int index)
{
	int err, devno = MKDEV(scull_major, scull_minor + index); // only devno is set
  
  /**
   * void cdev_init(struct cdev *dev, struct file_operations *fops)
   * 
   * allocate an initialized cdev structure
   * embed the cdev structure within a device-specific structure of your own
   * struct cdev has an owner filed that should be set to THIS_MODULE
   */
  cdev_init(&dev->cdev, &scull_fops);
  dev->cdev.owner = THIS_MODULE;

  /**
   * int cdev_add(struct cdev *dev, dev_t num, unsigned int count)
   * 
   * Once the cdev structure is set up, the final step is to tell the kernel about it
   * @dev: cdev structure
   * @num: the first device number to which this device responds
   * @count: the number of device numbers
   * @return: negative error code if the device has not been added to the system
   * 
   * @note: on succeed, your device is "live" and its operations can be called by the kernel
   *        Do NOT call cdev_add until your driver is completely ready to handle operations on device
   */ 
  err = cdev_add(&dev->dev, devno, 1);
  if(err){
    printk(KERNEL_NOTICE "Error %d adding scull%d", err, index);
  }
}


/**
 * The open method is provided for a driver to do any initialization in preparation 
 * for later operations. In most drivers, open should perform the following tasks:
 *    1) Check for device-specific errors (such as device-not-ready or similar 
 *       hardware problems)
 *    2) Initialize the device if it is being opened for the first time
 *    3) Update the f_op pointer, if necessary
 *    4) Allocate and fill any data structure to be put in filp->private_data
 * @inode: (inode->i_cdev) identify which device is being opened, which contains
 *         the cdev structure we set up before
 * @filep: (filep->private_data) points to the struct scull_dev for future easier 
 *         access
 */
int scull_open(struct inode *inode, struct file *filp)
{
  struct scull_dev *dev; /* device information */

  /**
   * container_of(pointer, container_type, container_field); 
   * (linux/kernel.h)
   * container_of - cast a member of a structure out to the containing structure
   * @pointer: a pointer to a field of type container_field(3rd param)
   * @container_type: the type of the struct that contains the container_field
   * @container_field: type of the field pointed to by the pointer
   * @return: a pointer to the containing structure
   * 
   * In this case, given the inode, take its field - inode->i_cdev (of type cdev)
   * and specify its container's type - struct scull_dev, then the return value
   * is the pointer to the container, i.e. pointer to the struct scull_dev.
   */
  dev = container_of(inode->i_cdev, struct scull_dev, cdev);
  filp->private_data = dev; /* for other methods */

  // truncate the device to a length of 0 when the device is opened for writing
  /* trim to 0 the length of the device if open was write-only */
  /**
   * Macro: int O_ACCMODE
   * This macro stands for a mask that can be bitwise-ANDed with the "file status 
   * flag" value to produce a value representing the file access mode. 
   * The mode will be O_RDONLY, O_WRONLY, or O_RDWR.
   */ 
  if((filp->f_flags & O_ACCMODE) == O_WRONLY){
    scull_trim(dev); /* ignore errors */
  }

  return 0; // success
}


/**
 * The release method
 *    1) Deallocate anything that open allocated in filp->private_data
 *    2) Shut down the device on last close
 * The basic form of scull has no hardware to shut down
 */ 
int scull_release(struct inode *inode, struct file *filep)
{
  return 0;
}


/**
 * Intro to memory mngt in scull (LDD 3rd Edition, p. 60)
 * 
 * The region of memory used by scull, also called a device, is variable in length.
 * The more you write, the more it grows; 
 *    trimming is performed by overwriting the device with a shorter file.
 * Two core (but not efficient) functions: (to manage memory)
 *    void *kmalloc(size_t size, int flags);
 *    void kfree(void *ptr);
 * 
 * kmalloc()
 * @flags: use GFP_KERNEL for now. (More details in Chap. 8)
 * @return: pointer to the size bytes of memory on success
 *          NULL on failure
 * kfree()
 * @ptr: the pointer to the memory obtained from kmalloc()
 * 
 * Use kmalloc() and kfree() without resorting to allocation of whole pages
 *    - for showing read and write
 *    - but less efficient
 * 
 * Interesting tips:
 *    use the command cp /dev/zero /dev/scull0 to eat all the real RAM with scull.
 *    use the dd utility to choose how much data is copied to the scull device.
 * 
 * Refer to Chap.8 for more details on memory management
 */ 

/**
 * The layout of a scull device (LDD 3rd Edition, p. 61)
 * 
 * In scull, each device is a linked list of pointers, with each pointer points
 *  to a struct scull_dev. Each scull_dev can refer to at most 4 million bytes,
 *  through an array of intermediate pointers.
 * The released source uses an array of 1000 pointers to areas of 4000 bytes.
 * 
 * We call:
 *  each memory area a "quantum", 
 *  the array (or its length) a "quantum set".
 * 
 * Question:
 *  How many bytes are consumed when writing a single byte in scull?
 * Solution:
 *  8000 bytes or 12000 bytes
 *  quantum: 4000 bytes
 *  + quantum set: 4000 bytes (a pointer is represented in 32 bits)
 *                 8000 bytes (a pointer is represented in 64 bits)
 * 
 * Three ways of determining the quantum and quantum set sizes:
 *  1) macros: SCULL_QUANTUM, SCULL_QSET in "scull.h"
 *  2) at module load time: set int values - scull_quantum, scull_qset
 *  3) at runtime: using ioctl
 */ 



































/**
 * void cdev_del(struct cdev *dev)
 * 
 * remove a char device from the system
 */ 