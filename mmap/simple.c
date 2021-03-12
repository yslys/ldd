/*
 * Simple - REALLY simple memory mapping demonstration.
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
 * $Id: simple.c,v 1.12 2005/01/31 16:15:31 rubini Exp $
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>

#include <linux/kernel.h>   /* printk() */
#include <linux/slab.h>   /* kmalloc() */
#include <linux/fs.h>       /* everything... */
#include <linux/errno.h>    /* error codes */
#include <linux/types.h>    /* size_t */
#include <linux/mm.h>
#include <linux/kdev_t.h>
#include <asm/page.h>
#include <linux/cdev.h>
#include <linux/version.h>

#include <linux/device.h>

static int simple_major = 0;
module_param(simple_major, int, 0);
MODULE_AUTHOR("Jonathan Corbet");
MODULE_LICENSE("Dual BSD/GPL");


////////////////////////////////////////////////////////////////////////////////
// Common VMA operations

/*
 * The remap_pfn_range version of mmap.  This one is heavily borrowed
 * from drivers/char/mem.c.
 */

static struct vm_operations_struct simple_remap_vm_ops = {
    .open = simple_vma_open,
    .close = simple_vma_close,
};


/*
 * vma open and close operations, simply print a message
 * vm_area_struct contains a set of operations that may be applied to vma,
 * so we provide open and close operations for our vma.
 */
void simple_vma_open(struct vm_area_struct *vma) {
    printk(KERN_NOTICE "Simple VMA open, virtual addr %lx, physical addr %lx\n",
                    vma->vm_start, vma->vm_pgoff << PAGE_SHIFT);
    /*
     * vma->vm_pgoff contains the page frame number (physical address right-shifted
     * by PAGE_SHIFT bits)
     * So if we perform left-shift on vma->vm_pgoff by PAGE_SHIFT bits,
     * we will get the physical address.
     */
}

void simple_vma_close(struct vm_area_struct *vma) {
    printk(KERN_NOTICE "Simple VMA closed.\n");
}


static int simple_remap_mmap(struct file *filep, struct vm_area_struct *vma)
{
    /*
     * remap_pfn_range() builds page table all at once to map a range of physical
     * address (device memory) into a user address space 
     * 
     * returns 0 on success
     */
    if (remap_pfn_range(vma, vma->vm_start, vma->vm_pgoff, 
                        vma->vm_end - vma->vm_start, 
                        vma->vm_page_prot)){
        // on failure
        return -EAGAIN;
    }

    /*
     * To make these operations active for a specific mapping, it is necessary
     * to store "a pointer to simple_remap_vm_ops" in the "vma->vm_ops" field of 
     * the relevant VMA. This is usually done in the mmap method.
     */ 
    vma->vm_ops = &simple_remap_vm_ops;

    /*
     * Since the open method is not invoked on the initial mmap, we must call
     * open explicitly if we want to run.
     */  
    simple_vma_open(vma);
    
    return 0;
}

////////////////////////////////////////////////////////////////////////////////


/*
 * Although remap_pfn_range() works well for many driver mmap implementations,
 * sometimes it's necessary to be "flexible".
 *   => nopage VMA method
 * 
 * When to use nopage mapping?
 *     mremap() syscall - used by applications to change the bounding addresses 
 *                        of a mapped region (expand or reduce).
 *     If VMA is reduced in size, kernel can flush out the unwanted pages without
 *          telling the driver.
 *     If VMA is expended, the driver eventually finds out by way of calls to 
 *          nopage when mappings must be set up for the new pages, so there's no 
 *          need to perform a separate notification.
 * 
 * Therefore, the nopage method, must be implemented if you want to support the 
 *     mremap() syscall. 
 * 
 * What is mremap()? 
 * mremap() expands (or shrinks) an existing memory mapping,
 *      potentially moving it at the same time (controlled by the flags
 *      argument and the available virtual address space).
 *
 *      #include <sys/mman.h>
 *
 *       void *mremap(void *old_address, size_t old_size,
 *                   size_t new_size, int flags, ... / void *new_address /);
 * 
 * When a user process attempts to access a page in a VMA that is not present in 
 * memory, nopage() is called.
 */
////////////////////////////////////////////////////////////////////////////////
// Mapping memory with nopage (another version of mmap)

#if LINUX_VERSION_CODE < KERNEL_VERSION(4,17,0)
typedef int vm_fault_t
#endif

static struct vm_operations_struct simple_nopage_vm_ops = {
    .open = simple_vma_open,
    .close = simple_vma_close,
};

/**
 * The main thing mmap() has to do is to replace the default (NULL) vm_ops pointer 
 * with our own operations.
 */ 
static int simple_nopage_mmap(struct file *filep, struct vm_area_struct *vma)
{
    // get the physical address
    unsigned long offset = vma->vm_pgoff << PAGE_SHIFT;

    // if the offset is larger than the high memory OR
    //    flag of the file is O_SYNC
    //        write() system call will be block until all file data and metadata 
    //        have been written to the disk
    if (offset >= __pa(high_memory) || (filp->f_flags & O_SYNC)){
        
        // VM_IO marks a VMA as being a memorymapped I/O region
        vma->vm_flags |= VM_IO;
        // vma->vm_flags = vma->vm_flags | VM_IO; bit-wise OR operator
    }

    // VM_RESERVED tells the memory management system not to attempt to swap out 
    // this VMA; it should be set in most device mappings.
    vma->vm_flags |= VM_REVERSED;

    /*
     * To make these operations active for a specific mapping, it is necessary
     * to store "a pointer to simple_remap_vm_ops" in the "vma->vm_ops" field of 
     * the relevant VMA. This is usually done in the mmap method.
     */ 
    vma->vm_ops = &simple_nopage_vm_ops;

    /*
     * Since the open method is not invoked on the initial mmap, we must call
     * open explicitly if we want to run.
     */  
    simple_vma_open(vma);
    return 0;
}


/**
 * nopage method then takes care of "remapping" one page at a time
 * nopage method need only find the correct struct page for the faulting address 
 * and increment its reference count.
 * @return: the address of (pointer to) its struct page structure.
 * 
 * Note:
 *      this implementation works for ISA memory regions but not for those on
 * the PCI bus. PCI memory is mapped above the highest system memory, and there 
 * are no entries in the system memory map for those addresses. Because there is 
 * no struct page to return a pointer to, nopage cannot be used in these situations; 
 * you must use remap_pfn_range instead.
 */ 
struct page *simple_vma_nopage(struct vm_area_struct *vma, 
                                unsigned long address, int *type)
{
    struct page *pageptr;
    unsigned long offset = vma->vm_pgoff << PAGE_SHIFT;

    // calculate the desired physical address
    unsigned long physaddr = adress - vma->vm_start + offset;
    // turn it into a page frame number by right-shifting it PAGE_SHIFT bits
    unsigned long pageframe = physaddr >> PAGE_SHIFT;


    /**
     * Since user space can give us any address it likes, we must ensure that we 
     * have a valid page frame; the pfn_valid function does that for us. 
     */
    
    // If the address is out of range, we return NOPAGE_SIGBUS, which causes 
    // a bus signal to be delivered to the calling process.
    if (!pfn_valid(pageframe)){
        return NOPAGE_SIGBUS;
        /**
         * Note that, nopage can also return NOPAGE_OOM to indicate failures caused
         * by resource limitations.
         */
    }

    // otherwise, get the necessary struct page pointer
    pageptr = pfn_to_page(pageframe);

    // increment the page's reference count
    get_page(pageptr);

    if(type){
        *type = VM_FAULT_MINOR;
    }

    return pageptr;
}










