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
#include <linux/fs.h>		     /* everything... */
#include <linux/errno.h>   	 /* error codes */
#include <linux/types.h>	   /* size_t */
#include <linux/proc_fs.h>
#include <linux/fcntl.h>	   /* O_ACCMODE */
#include <linux/seq_file.h>
#include <linux/cdev.h>

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