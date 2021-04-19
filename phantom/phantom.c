// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Copyright (C) 2005-2007 Jiri Slaby <jirislaby@gmail.com>
 *
 *  You need a userspace library to cooperate with this driver. It (and other
 *  info) may be obtained here:
 *  http://www.fi.muni.cz/~xslaby/phantom.html
 *  or alternatively, you might use OpenHaptics provided by Sensable.
 */

#include <linux/cdev.h>
#include <linux/compat.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/pci.h>
#include <linux/phantom.h>
#include <linux/poll.h>
#include <linux/sched.h>
#include <linux/slab.h>

#include <asm/io.h>
#include <linux/atomic.h>

#define PHANTOM_VERSION "n0.9.8"

#define PHANTOM_MAX_MINORS 8

#define PHN_IRQCTL 0x4c /* irq control in caddr space */

#define PHB_RUNNING 1
#define PHB_NOT_OH 2

static DEFINE_MUTEX(phantom_mutex);
static struct class *phantom_class;
static int phantom_major;

struct phantom_device {
  unsigned int opened;
  void __iomem *caddr; // PHN_IRQCTL 0x4c, irq control in caddr space
  u32 __iomem *iaddr; // PHN_CONTROL 0x6, control byte in iaddr space
  u32 __iomem *oaddr;
  unsigned long status;
  atomic_t counter;

  wait_queue_head_t wait; // wait queue for I/O
  struct cdev cdev;

  struct mutex open_lock;
  spinlock_t regs_lock;

  /* used in NOT_OH mode */
  struct phm_regs oregs;
  u32 ctl_reg;
};

/**
 * # of phantom_device = PHANTOM_MAX_MINORS (defined in this file)
 */
static unsigned char phantom_devices[PHANTOM_MAX_MINORS];

/**
 * set the status of the phantom_device according to @dev->status and @newstat
 * 
 * @dev: the phantom device
 * @newstat: a long type that used for status
 * @return: always return 0
 */
static int phantom_status(struct phantom_device *dev, unsigned long newstat) 
{
  // print the current status, and the new status
  pr_debug("phantom_status %lx %lx\n", dev->status, newstat);

  if (!(dev->status & PHB_RUNNING) && (newstat & PHB_RUNNING)) {
    /**
     * PHB_RUNNING = 0b00...001 (either 32bit or 64bit)
     * if the last bit of status is 0, and last bit of newstat is not 0
     */

    /**
     * void atomic_set(atomic_t *v, int i);
     * atomically sets the value of v to i
     */
    atomic_set(&dev->counter, 0); // set counter to be 0

    /* iowrite32(u32 value, void *addr) */

    // write PHN_CTL_IRQ to (iaddr + PHN_CONTROL(ctrl byte) )
    iowrite32(PHN_CTL_IRQ, dev->iaddr + PHN_CONTROL);

    // write 0x43 to (caddr + PHN_IRQCTL (irq ctrl) )
    iowrite32(0x43, dev->caddr + PHN_IRQCTL);

    // read from I/O memory to post the success of write
    ioread32(dev->caddr + PHN_IRQCTL); /* PCI posting */
  } else if ((dev->status & PHB_RUNNING) && !(newstat & PHB_RUNNING)) {
    // if the last bit of status is not 0, and the last bit of newstat is 0

    iowrite32(0, dev->caddr + PHN_IRQCTL);
    ioread32(dev->caddr + PHN_IRQCTL); /* PCI posting */
  }

  dev->status = newstat;

  return 0;
}

/**
 * File ops
 * 
 * @file: uses its private_data to get the device 
 * @cmd: ioctl cmd
 * @arg: arg from user space (corresponds to @cmd)
 * @return: 
 */
static long phantom_ioctl(struct file *file, unsigned int cmd,
                          unsigned long arg) 
{
  struct phantom_device *dev = file->private_data;
  struct phm_regs rs;
  struct phm_reg r;
  void __user *argp = (void __user *)arg;
  unsigned long flags;
  unsigned int i;

  switch (cmd) {
    case PHN_SETREG:
      /* not implemented here */

    /* phantom set register */  
    case PHN_SET_REG:
      // copy from argp to r, of len=sizeof(r)
      if (copy_from_user(&r, argp, sizeof(r)))
        // on failure, copy_from_user() returns # of bytes failed to copy
        return -EFAULT;

      // phm_reg's reg could not exceed 7
      if (r.reg > 7)
        return -EINVAL;

      // save the interrupt state before taking the spinlock
      // spin_lock_irqsave is much safer than spin_lock_irq
      spin_lock_irqsave(&dev->regs_lock, flags);

      if (r.reg == PHN_CONTROL && (r.value & PHN_CTL_IRQ) &&
          phantom_status(dev, dev->status | PHB_RUNNING)) {
        /**
         * if reg of phm_reg is equal to 0x6 (control byte in iaddr space)
         * && the 2nd lsb of value of phm_reg is not 0
         * && ???
         */
        
        spin_unlock_irqrestore(&dev->regs_lock, flags);
        return -ENODEV;
      }

      // write the phm_reg.value to phm_reg.reg
      pr_debug("phantom: writing %x to %u\n", r.value, r.reg);

      /* preserve amp bit (don't allow to change it when in NOT_OH) */
      if (r.reg == PHN_CONTROL && (dev->status & PHB_NOT_OH)) {
        // if phm_reg's reg is set to control byte in iaddr space, 
        // && status of the phamtom_device is not OH
        r.value &= ~PHN_CTL_AMP;
        r.value |= dev->ctl_reg & PHN_CTL_AMP;
        dev->ctl_reg = r.value;
      }

      iowrite32(r.value, dev->iaddr + r.reg);
      ioread32(dev->iaddr); /* PCI posting */

      if (r.reg == PHN_CONTROL && !(r.value & PHN_CTL_IRQ))
        phantom_status(dev, dev->status & ~PHB_RUNNING);
      spin_unlock_irqrestore(&dev->regs_lock, flags);
      break;


    case PHN_SETREGS:
    case PHN_SET_REGS:
      if (copy_from_user(&rs, argp, sizeof(rs)))
        return -EFAULT;

      pr_debug("phantom: SRS %u regs %x\n", rs.count, rs.mask);
      spin_lock_irqsave(&dev->regs_lock, flags);
      if (dev->status & PHB_NOT_OH)
        memcpy(&dev->oregs, &rs, sizeof(rs));
      else {
        u32 m = min(rs.count, 8U);
        for (i = 0; i < m; i++)
          if (rs.mask & BIT(i))
            iowrite32(rs.values[i], dev->oaddr + i);
        ioread32(dev->iaddr); /* PCI posting */
      }
      spin_unlock_irqrestore(&dev->regs_lock, flags);
      break;
    case PHN_GETREG:
    case PHN_GET_REG:
      if (copy_from_user(&r, argp, sizeof(r)))
        return -EFAULT;

      if (r.reg > 7)
        return -EINVAL;

      r.value = ioread32(dev->iaddr + r.reg);

      if (copy_to_user(argp, &r, sizeof(r)))
        return -EFAULT;
      break;
    case PHN_GETREGS:
    case PHN_GET_REGS: {
      u32 m;

      if (copy_from_user(&rs, argp, sizeof(rs)))
        return -EFAULT;

      m = min(rs.count, 8U);

      pr_debug("phantom: GRS %u regs %x\n", rs.count, rs.mask);
      spin_lock_irqsave(&dev->regs_lock, flags);
      for (i = 0; i < m; i++)
        if (rs.mask & BIT(i))
          rs.values[i] = ioread32(dev->iaddr + i);
      atomic_set(&dev->counter, 0);
      spin_unlock_irqrestore(&dev->regs_lock, flags);

      if (copy_to_user(argp, &rs, sizeof(rs)))
        return -EFAULT;
      break;
    }
    case PHN_NOT_OH:
      spin_lock_irqsave(&dev->regs_lock, flags);
      if (dev->status & PHB_RUNNING) {
        printk(KERN_ERR "phantom: you need to set NOT_OH "
                        "before you start the device!\n");
        spin_unlock_irqrestore(&dev->regs_lock, flags);
        return -EINVAL;
      }
      dev->status |= PHB_NOT_OH;
      spin_unlock_irqrestore(&dev->regs_lock, flags);
      break;
    default:
      return -ENOTTY;
    }

  return 0;
}

#ifdef CONFIG_COMPAT
static long phantom_compat_ioctl(struct file *filp, unsigned int cmd,
                                 unsigned long arg) {
  if (_IOC_NR(cmd) <= 3 && _IOC_SIZE(cmd) == sizeof(compat_uptr_t)) {
    cmd &= ~(_IOC_SIZEMASK << _IOC_SIZESHIFT);
    cmd |= sizeof(void *) << _IOC_SIZESHIFT;
  }
  return phantom_ioctl(filp, cmd, (unsigned long)compat_ptr(arg));
}
#else
#define phantom_compat_ioctl NULL
#endif

/**
 * open the phantom device
 * 
 * @inode: used for retrieving the device, and nonseekable open
 * @file: used for nonseekable open
 * @return 0 on success
 */
static int phantom_open(struct inode *inode, struct file *file) {
  
  // get the struct phantom_device according to inode->i_cdev
  struct phantom_device *dev = container_of(inode->i_cdev,
                                            struct phantom_device, cdev);

  mutex_lock(&phantom_mutex);

  // open the device that does NOT support llseek()
  // https://lwn.net/Articles/97154/
  nonseekable_open(inode, file);

  if (mutex_lock_interruptible(&dev->open_lock)) {
    mutex_unlock(&phantom_mutex);
    return -ERESTARTSYS;
  }

  // if the device already opened, unlock the mutex, report error
  if (dev->opened) {
    mutex_unlock(&dev->open_lock);
    mutex_unlock(&phantom_mutex);
    return -EINVAL;
  }

  // issue warning
  WARN_ON(dev->status & PHB_NOT_OH);

  // the last several things before open ends
  file->private_data = dev;
  atomic_set(&dev->counter, 0);
  dev->opened++; // increment # of times opened
  mutex_unlock(&dev->open_lock);
  mutex_unlock(&phantom_mutex);
  return 0;
}

/**
 * release the device
 * 
 * @inode: unused here
 * @file: its private_data field is used for retrieving the device
 * @return 0 on success 
 */
static int phantom_release(struct inode *inode, struct file *file) 
{
  struct phantom_device *dev = file->private_data;

  mutex_lock(&dev->open_lock);

  // the following should be done while mutex acquired
  dev->opened = 0;
  phantom_status(dev, dev->status & ~PHB_RUNNING); // set status to be not running
  dev->status &= ~PHB_NOT_OH; // set status to be not OH

  mutex_unlock(&dev->open_lock);

  return 0;
}

/**
 * examine a set of file descriptors to see if some of them are ready for I/O
 * 
 * @file: used in poll_wait
 * @wait: poll_table
 * @return the mask of type __poll_t 
 */
static __poll_t phantom_poll(struct file *file, poll_table *wait) {
  struct phantom_device *dev = file->private_data;
  __poll_t mask = 0;

  pr_debug("phantom_poll: %d\n", atomic_read(&dev->counter));

  /**
   * wait for selectable event to be ready
   * @file is a new param compared to older versions of poll_wait()
   * 
   * This function is used in support of the poll device driver entry point. 
   * The intent is for a device driver to put sync into the poll table immediately 
   * entering the device poll routine, 
   * then returning a bit mask of events that are currently ready. 
   * The kernel looks at the mask of events to see if something it needs is ready, 
   * and suspends the process if not.
   */
  poll_wait(file, &dev->wait, wait);

  if (!(dev->status & PHB_RUNNING)) // check the status
    mask = EPOLLERR;
  else if (atomic_read(&dev->counter)) // read the counter 
    mask = EPOLLIN | EPOLLRDNORM;

  pr_debug("phantom_poll end: %x/%d\n", mask, atomic_read(&dev->counter));

  return mask;
}


static const struct file_operations phantom_file_ops = {
    .open = phantom_open,
    .release = phantom_release,
    .unlocked_ioctl = phantom_ioctl,
    .compat_ioctl = phantom_compat_ioctl,
    .poll = phantom_poll,
    .llseek = no_llseek,
};

/**
 * phantom interrupt service routine - interrupt handler (top half)
 * 
 * @irq: irq number
 * @data: ptr to a phantom_device
 * @return irqreturn_t the return type of an interrupt handler
 */
static irqreturn_t phantom_isr(int irq, void *data) 
{
  struct phantom_device *dev = data;
  unsigned int i;
  u32 ctl;

  spin_lock(&dev->regs_lock);
  ctl = ioread32(dev->iaddr + PHN_CONTROL);
  // if value at iaddr+control byte != irq enabled, release the spinlock, report error
  if (!(ctl & PHN_CTL_IRQ)) {
    spin_unlock(&dev->regs_lock);
    return IRQ_NONE;
  }

  iowrite32(0, dev->iaddr);
  iowrite32(0xc0, dev->iaddr);

  if (dev->status & PHB_NOT_OH) {
    struct phm_regs *r = &dev->oregs;
    u32 m = min(r->count, 8U);

    for (i = 0; i < m; i++)
      if (r->mask & BIT(i))
        iowrite32(r->values[i], dev->oaddr + i);

    dev->ctl_reg ^= PHN_CTL_AMP;
    iowrite32(dev->ctl_reg, dev->iaddr + PHN_CONTROL);
  }
  spin_unlock(&dev->regs_lock);

  ioread32(dev->iaddr); /* PCI posting */

  atomic_inc(&dev->counter);
  wake_up_interruptible(&dev->wait);

  return IRQ_HANDLED;
}

/*
 * Init and deinit driver
 */

static unsigned int phantom_get_free(void) {
  unsigned int i;

  for (i = 0; i < PHANTOM_MAX_MINORS; i++)
    if (phantom_devices[i] == 0)
      break;

  return i;
}

static int phantom_probe(struct pci_dev *pdev,
                         const struct pci_device_id *pci_id) {
  struct phantom_device *pht;
  unsigned int minor;
  int retval;

  retval = pci_enable_device(pdev);
  if (retval) {
    dev_err(&pdev->dev, "pci_enable_device failed!\n");
    goto err;
  }

  minor = phantom_get_free();
  if (minor == PHANTOM_MAX_MINORS) {
    dev_err(&pdev->dev, "too many devices found!\n");
    retval = -EIO;
    goto err_dis;
  }

  phantom_devices[minor] = 1;

  retval = pci_request_regions(pdev, "phantom");
  if (retval) {
    dev_err(&pdev->dev, "pci_request_regions failed!\n");
    goto err_null;
  }

  retval = -ENOMEM;
  pht = kzalloc(sizeof(*pht), GFP_KERNEL);
  if (pht == NULL) {
    dev_err(&pdev->dev, "unable to allocate device\n");
    goto err_reg;
  }

  pht->caddr = pci_iomap(pdev, 0, 0);
  if (pht->caddr == NULL) {
    dev_err(&pdev->dev, "can't remap conf space\n");
    goto err_fr;
  }
  pht->iaddr = pci_iomap(pdev, 2, 0);
  if (pht->iaddr == NULL) {
    dev_err(&pdev->dev, "can't remap input space\n");
    goto err_unmc;
  }
  pht->oaddr = pci_iomap(pdev, 3, 0);
  if (pht->oaddr == NULL) {
    dev_err(&pdev->dev, "can't remap output space\n");
    goto err_unmi;
  }

  mutex_init(&pht->open_lock);
  spin_lock_init(&pht->regs_lock);
  init_waitqueue_head(&pht->wait);
  cdev_init(&pht->cdev, &phantom_file_ops);
  pht->cdev.owner = THIS_MODULE;

  iowrite32(0, pht->caddr + PHN_IRQCTL);
  ioread32(pht->caddr + PHN_IRQCTL); /* PCI posting */
  retval = request_irq(pdev->irq, phantom_isr,
                       IRQF_SHARED, "phantom", pht);
  if (retval) {
    dev_err(&pdev->dev, "can't establish ISR\n");
    goto err_unmo;
  }

  retval = cdev_add(&pht->cdev, MKDEV(phantom_major, minor), 1);
  if (retval) {
    dev_err(&pdev->dev, "chardev registration failed\n");
    goto err_irq;
  }

  if (IS_ERR(device_create(phantom_class, &pdev->dev,
                           MKDEV(phantom_major, minor), NULL,
                           "phantom%u", minor)))
    dev_err(&pdev->dev, "can't create device\n");

  pci_set_drvdata(pdev, pht);

  return 0;
err_irq:
  free_irq(pdev->irq, pht);
err_unmo:
  pci_iounmap(pdev, pht->oaddr);
err_unmi:
  pci_iounmap(pdev, pht->iaddr);
err_unmc:
  pci_iounmap(pdev, pht->caddr);
err_fr:
  kfree(pht);
err_reg:
  pci_release_regions(pdev);
err_null:
  phantom_devices[minor] = 0;
err_dis:
  pci_disable_device(pdev);
err:
  return retval;
}

static void phantom_remove(struct pci_dev *pdev) {
  struct phantom_device *pht = pci_get_drvdata(pdev);
  unsigned int minor = MINOR(pht->cdev.dev);

  device_destroy(phantom_class, MKDEV(phantom_major, minor));

  cdev_del(&pht->cdev);

  iowrite32(0, pht->caddr + PHN_IRQCTL);
  ioread32(pht->caddr + PHN_IRQCTL); /* PCI posting */
  free_irq(pdev->irq, pht);

  pci_iounmap(pdev, pht->oaddr);
  pci_iounmap(pdev, pht->iaddr);
  pci_iounmap(pdev, pht->caddr);

  kfree(pht);

  pci_release_regions(pdev);

  phantom_devices[minor] = 0;

  pci_disable_device(pdev);
}

static int __maybe_unused phantom_suspend(struct device *dev_d) {
  struct phantom_device *dev = dev_get_drvdata(dev_d);

  iowrite32(0, dev->caddr + PHN_IRQCTL);
  ioread32(dev->caddr + PHN_IRQCTL); /* PCI posting */

  synchronize_irq(to_pci_dev(dev_d)->irq);

  return 0;
}

static int __maybe_unused phantom_resume(struct device *dev_d) {
  struct phantom_device *dev = dev_get_drvdata(dev_d);

  iowrite32(0, dev->caddr + PHN_IRQCTL);

  return 0;
}

static struct pci_device_id phantom_pci_tbl[] = {
    {.vendor = PCI_VENDOR_ID_PLX, .device = PCI_DEVICE_ID_PLX_9050, .subvendor = PCI_VENDOR_ID_PLX, .subdevice = PCI_DEVICE_ID_PLX_9050, .class = PCI_CLASS_BRIDGE_OTHER << 8, .class_mask = 0xffff00},
    {
        0,
    }};
MODULE_DEVICE_TABLE(pci, phantom_pci_tbl);

static SIMPLE_DEV_PM_OPS(phantom_pm_ops, phantom_suspend, phantom_resume);

static struct pci_driver phantom_pci_driver = {
    .name = "phantom",
    .id_table = phantom_pci_tbl,
    .probe = phantom_probe,
    .remove = phantom_remove,
    .driver.pm = &phantom_pm_ops,
};

static CLASS_ATTR_STRING(version, 0444, PHANTOM_VERSION);

static int __init phantom_init(void) {
  int retval;
  dev_t dev;

  phantom_class = class_create(THIS_MODULE, "phantom");
  if (IS_ERR(phantom_class)) {
    retval = PTR_ERR(phantom_class);
    printk(KERN_ERR "phantom: can't register phantom class\n");
    goto err;
  }
  retval = class_create_file(phantom_class, &class_attr_version.attr);
  if (retval) {
    printk(KERN_ERR "phantom: can't create sysfs version file\n");
    goto err_class;
  }

  retval = alloc_chrdev_region(&dev, 0, PHANTOM_MAX_MINORS, "phantom");
  if (retval) {
    printk(KERN_ERR "phantom: can't register character device\n");
    goto err_attr;
  }
  phantom_major = MAJOR(dev);

  retval = pci_register_driver(&phantom_pci_driver);
  if (retval) {
    printk(KERN_ERR "phantom: can't register pci driver\n");
    goto err_unchr;
  }

  printk(KERN_INFO "Phantom Linux Driver, version " PHANTOM_VERSION ", "
                   "init OK\n");

  return 0;
err_unchr:
  unregister_chrdev_region(dev, PHANTOM_MAX_MINORS);
err_attr:
  class_remove_file(phantom_class, &class_attr_version.attr);
err_class:
  class_destroy(phantom_class);
err:
  return retval;
}

static void __exit phantom_exit(void) {
  pci_unregister_driver(&phantom_pci_driver);

  unregister_chrdev_region(MKDEV(phantom_major, 0), PHANTOM_MAX_MINORS);

  class_remove_file(phantom_class, &class_attr_version.attr);
  class_destroy(phantom_class);

  pr_debug("phantom: module successfully removed\n");
}

module_init(phantom_init);
module_exit(phantom_exit);

MODULE_AUTHOR("Jiri Slaby <jirislaby@gmail.com>");
MODULE_DESCRIPTION("Sensable Phantom driver (PCI devices)");
MODULE_LICENSE("GPL");
MODULE_VERSION(PHANTOM_VERSION);