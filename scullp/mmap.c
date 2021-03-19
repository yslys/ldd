/*  -*- C -*-
 * mmap.c -- memory mapping for the scullp char module
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
 * $Id: _mmap.c.in,v 1.13 2004/10/18 18:07:36 corbet Exp $
 */

#include <linux/module.h>

#include <linux/mm.h>		/* everything */
#include <linux/errno.h>	/* error codes */
#include <asm/pgtable.h>
#include <linux/fs.h>
#include <linux/version.h>
#include "scullp.h"		/* local definitions */



/*
 * open and close: just keep track of how many times the device is
 * mapped, to avoid releasing it.
 */

void scullp_vma_open(struct vm_area_struct *vma)
{
    // store vm_private_data in scullp_dev struct
	struct scullp_dev *dev = vma->vm_private_data;

    // vmas stores # of active mappings
	dev->vmas++;
}

void scullp_vma_close(struct vm_area_struct *vma)
{
	struct scullp_dev *dev = vma->vm_private_data;

	dev->vmas--;
}



/**
 * a keyword called typedef, which you can use to give a type a new name
 * in this case, vm_fault_t is defined to be the name of int, which stands for
 * the return type for page fault handlers
 */ 
#if LINUX_VERSION_CODE < KERNEL_VERSION(4,17,0)
typedef int vm_fault_t
#endif

/*
 * The nopage method: the core of the file. It retrieves the
 * page required from the scullp device and returns it to the
 * user. The count for the page must be incremented, because
 * it is automatically decremented at page unmap.
 *
 * For this reason, "order" must be zero. Otherwise, only the first
 * page has its count incremented, and the allocating module must
 * release it as a whole block. Therefore, it isn't possible to map
 * pages from a multipage block: when they are unmapped, their count
 * is individually decreased, and would drop to 0.
 */
static vm_fault_t scullp_vma_nopage(struct vm_fault *vmf)
{
	// vm_fault is defined in /include/linux/mm.h
	
	unsigned long offset;
	struct vm_area_struct *vma = vmf->vma;

	// ptr is the working ptr; dev points to the scullp device
	struct scullp_dev *ptr, *dev = vma->vm_private_data;

	struct page *page = NULL;
	void *pageptr = NULL; // default to "missing"

	vm_fault_t retval = VM_FAULT_NOPAGE; // fault installed the pte, not return page

	mutex_lock(&dev->mutex);

	// vma->vm_pgoff stores the page frame number (vm_pgoff = physical addr >> PAGE_SHIFT)
	offset = (unsigned long)(vmf->address - vma->vm_start) + (vma->vm_pgoff << PAGE_SHIFT);
	
	// check if it's out of range
	if(offset >= dev->size){
		goto out;
	}

	/*
	 * Now retrieve the scullp device from the list,then the page.
	 * If the device has holes, the process receives a SIGBUS when
	 * accessing the hole.
	 */
	offset >>= PAGE_SHIFT; // now offset stores # of pages


	for(ptr = dev; ptr && offset >= dev->qset;){
		ptr = ptr->next;
		offset -= dev->qset;
	}

	if(ptr && ptr->data){
		pageptr = ptr->data[offset];
	}

	// hole or EOF
	if(!pageptr){
		go to out; 
	}

	page = virt_to_page(pageptr);

	// got it, now increment the count
	get_page(page);
	vmf->page = page;
	retval = 0;

	out:
		mutex_unlock(&dev->mutex);
		return retval;

}





struct vm_operations_struct scullp_vm_ops = {
	.open = scullp_vma_open,
	.close = scullp_vma_close,
	.fault = scullp_vma_nopage,
};


/**
 * map RAM to virtual memory
 */ 
int scullp_mmap(struct file *filep, struct vm_area_struct *vma)
{
	struct inode *inode = filp->f_path.entry->d_inode;

	// if order is 0, then refuse to map
	if(scullp_devices[iminor(inode)].order == 0){
		return -ENODEV;
	}

	// don't do anything here: "nopage" will set up page table entries

	// scullp's operations are stored in the vm_ops field
	vma->vm_ops = &scullp_vm_ops;

	// a pointer to the device structure is stashed in the vm_private_data field
	vma->vm_private_data = filep->private_data;
	scullp_vma_open(vma);
	return 0;
}