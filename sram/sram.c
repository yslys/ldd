// this code works for Linux version v5.11.14

// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Generic on-chip SRAM allocation driver
 * This code does a memory copy, working with an actual device without interrupts
 * Copyright (C) 2012 Philipp Zabel, Pengutronix
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/genalloc.h>
#include <linux/io.h>
#include <linux/list_sort.h>
#include <linux/mfd/syscon.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <soc/at91/atmel-secumod.h>

#include "sram.h"

#define SRAM_GRANULARITY 32



/**
 * The prototype of read() is defined in struct bin_attribute in <linux/sysfs.h>
 * <linux/sysfs.h> - definitions for the device driver filesystem
 * 
 * @param filp not used here
 * @param kobj not used here
 * @param attr used to find its container (struct sram_partition)
 * @param buf 
 * @param pos 
 * @param count 
 * @return ssize_t 
 */
static ssize_t sram_read(struct file *filp, struct kobject *kobj,
                         struct bin_attribute *attr,
                         char *buf, loff_t pos, size_t count) {
  struct sram_partition *part; // the memory partition to read from

  // FIRSTLY, GET the ptr to sram_partition based on attr (bin_attribute *)
  // we know that struct sram_partition has a field: struct bin_attribute battr;
  // given attr (a ptr to bin_attribute), it could be in the struct sram_partition
  // so we call container_of(), to get the pointer to struct sram_partition that
  // contains attr as its battr.

  // attr corresponds to sram_partition.battr
  // given attr, we want to find its corresponding ptr to sram_partition
  part = container_of(attr, struct sram_partition, battr);

  mutex_lock(&part->lock);

  /**
     * @brief copy data from I/O memory
     *   static inline void
     *   memcpy_fromio(void *dst, volatile void __iomem *src, int count)
     *   {
     *       memcpy(dst, (void __force *) src, count);
     *   }
     * 
     * copy @count bytes from @part->base (memory partition) to @buf (user space) 
     * 
     */
  memcpy_fromio(buf, part->base + pos, count);
  mutex_unlock(&part->lock);

  return count;
}

/**
 * @brief write @count bytes of @buf to the memory partition that contains @attr
 * 
 * @param filp not used
 * @param kobj not used
 * @param attr used to find its container (struct sram_partition)
 * @param buf src of write
 * @param pos offset to the base position of memory partition
 * @param count #of bytes to write from @buf to memory partition
 * @return ssize_t 
 */
static ssize_t sram_write(struct file *filp, struct kobject *kobj,
                          struct bin_attribute *attr,
                          char *buf, loff_t pos, size_t count) {
  struct sram_partition *part;
  part = container_of(attr, struct sram_partition, battr);

  mutex_lock(&part->lock);

  /**
     * @brief copy data to I/O memory
     * static inline void
     * memcpy_toio(volatile void __iomem *dst, const void *src, int count)
     * {
     *        memcpy((void __force *) dst, src, count);
     * }
     */
  memcpy_toio(part->base + pos, buf, count);
  mutex_unlock(&part->lock);

  return count;
}

/**
 * devm_gen_pool_create() - create a special memory pool
 * IS_ERR() - check if creation succeeded
 * gen_pool_add_virt() - add a new chunk of special memory to the specified pool
 * check retval again
 * finished
 * 
 * @param sram 
 * @param block use its fields name and size
 * @param start 
 * @param part 
 * @return int 
 */
static int sram_add_pool(struct sram_dev *sram, struct sram_reserve *block,
                         phys_addr_t start, struct sram_partition *part) {
  int ret; // return value for generating subpool

  /**
     * devm_gen_pool_create - managed gen_pool_create
     * extern struct gen_pool *devm_gen_pool_create(struct device *dev,
		                        int min_alloc_order, int nid, const char *name);
     * @dev: device that provides the gen_pool
     * @min_alloc_order: (log base 2 of) number of bytes each bitmap bit represents
     * @nid: node selector for allocated gen_pool, %NUMA_NO_NODE for all nodes
     * @name: name of a gen_pool or NULL, identifies a particular gen_pool on device
     *
     * Create a new special memory pool that can be used to manage special purpose
     * memory not managed by the regular kmalloc/kfree interface. The pool will be
     * automatically destroyed by the device management code.
     */
  part->pool = devm_gen_pool_create(sram->dev, ilog2(SRAM_GRANULARITY),
                                    NUMA_NO_NODE, block->label);

  /*
     * Kernel pointers have redundant information, so we can use a
     * scheme where we can return either an error code or a normal
     * pointer with the same return value.
     *
     * This should be a per-architecture thing, to allow different
     * error and pointer decisions.
     */
  if (IS_ERR(part->pool)) {
    return PTR_ERR(part->pool);
  }

  /**
     * gen_pool_add_virt() calls gen_pool_add_owner() with its last param = NULL
     *
     * gen_pool_add_owner- add a new chunk of special memory to the pool
     * @pool: pool to add new memory chunk to
     * @virt: virtual starting address of memory chunk to add to pool
     * @phys: physical starting address of memory chunk to add to pool
     * @size: size in bytes of the memory chunk to add to pool
     * @nid: node id of the node the chunk structure and bitmap should be
     *       allocated on, or -1
     * @owner: private data the publisher would like to recall at alloc time
     *
     * Add a new chunk of special memory to the specified pool.
     *
     * Returns 0 on success or a -ve errno on failure.
     */
  ret = gen_pool_add_virt(part->pool, (unsigned long)part->base, start,
                          block->size, NUMA_NO_NODE);

  if (ret < 0) {
    dev_err(sram->dev, "failed to register subpool: %d\n", ret);
    return ret;
  }

  return 0;
}

/**
 * sram_add_export() - creates sysfs binary attribute file for device and returns
 *                     0 on success
 * 
 * Basically, it does the following:
 * sysfs_bin_attr_init - initialize a dynamically allocated bin_attribute.
 * devm_kasprintf - allocate resource managed space and format a string into that,
 *                  to store in name
 * initialize sram_partition's bin_attribute's fields
 * device_create_bin_file - create sysfs binary attribute file for device.
 * 
 * @sram: used to hold dev
 * @block: used to get the size
 * @start: used to set the name of attr of battr of sram_partition
 * @part: ptr to sram_partition
 * @return: 0 on success
 */
static int sram_add_export(struct sram_dev *sram, struct sram_reserve *block,
                           phys_addr_t start, struct sram_partition *part) {
  /**
     *	sysfs_bin_attr_init - initialize a dynamically allocated bin_attribute
     *	@attr: struct bin_attribute to initialize
     *
     *	Initialize a dynamically allocated struct bin_attribute so we
     *	can make lockdep happy. (lockdep - runtime locking correctness validator)
     *  This is a new requirement for
     *	attributes and initially this is only needed when lockdep is
     *	enabled.  Lockdep gives a nice error when your attribute is
     *	added to sysfs if you don't have this.
     */
  sysfs_bin_attr_init(&part->battr);

  /** 
     * part(sram_partition) has field attr(bin_attribute)
     * attr(bin_attribute) has field name(const *char)
     * 
     * devm_kasprintf - Allocate resource managed space and format a string
     *		    into that.
     * @dev: Device to allocate memory for
     * @gfp: the GFP mask used in the devm_kmalloc() call when
     *       allocating memory
     * @fmt: The printf()-style format string
     * @...: Arguments for the format string
     * @return: Pointer to allocated string on success, NULL on failure.
     */
  part->battr.attr.name = devm_kasprintf(sram->dev, GFP_KERNEL, "%llx.sram",
                                         (unsigned long long)start);

  if (!part->battr.attr.name) {
    return -ENOMEM;
  }

  // after allocation, initialize its fields
  part->battr.attr.mode = S_IRUSR | S_IWUSR; // set mode bits for access permission
  part->battr.read = sram_read;              // set read()
  part->battr.write = sram_write;            // set write()
  part->battr.size = block->size;            // set size

  /**
     * device_create_bin_file - create sysfs binary attribute file for device.
     * @dev: device.
     * @attr: device binary attribute descriptor.
     * @return: 0 on success; o.w. error code
     * 
     * this function calls sysfs_create_bin_file(), returns 0 on success
     */
  return device_create_bin_file(sram->dev, &part->battr);
}

/**
 * add a partition to sram
 * 
 * @sram: 
 * @block: 
 * @start: 
 * @return: 
 */
static int sram_add_partition(struct sram_dev *sram, struct sram_reserve *block,
                              phys_addr_t start) {
  int ret;
  // get the pointer to the partition
  struct sram_partition *part = &sram->partition[sram->partitions];

  mutex_init(&part->lock);

  part->base = sram->virt_base + block->start;

  // the following several if-conditions evaluate if they are NULL
  if (block->pool) { // if block->pool is not NULL
    ret = sram_add_pool(sram, block, start, part);
    if (ret) { // yeah, only when ret != 0 can enter this
      return ret;
    }
  }

  if (block->export) {
    ret = sram_add_export(sram, block, start, part);
    if (ret) {
      return ret;
    }
  }

  if (block->protect_exec) {
    // this is defined in <drivers/misc/sram-exec.c>
    ret = sram_check_protect_exec(sram, block, part);
    if (ret) {
      return ret;
    }

    ret = sram_add_pool(sram, block, start, part);
    if (ret) {
      return ret;
    }

    // this is defined in <drivers/misc/sram-exec.c>
    sram_add_protect_exec(part);
  }

  sram->partitions++;

  return 0;
}

static void sram_free_partitions(struct sram_dev *sram) {
  struct sram_partition *part;

  // if sram device's partitions is 0
  if (!sram->partitions) {
    return;
  }

  // get the last partition
  part = &sram->partition[sram->partitions - 1];

  // note here, part-- is pointer arithmetic
  for (; sram->partitions; sram->partitions--, part--) {
    if (part->battr.size) {
      device_remove_bin_file(sram->dev, &part->battr);
    }

    if (part->pool &&
        gen_pool_avail(part->pool) < gen_pool_size(part->pool)) {
      dev_err(sram->dev, "removed pool while SRAM allocated\n");
    }
  }
}

static void sram_free_partitions(struct sram_dev *sram) {
  struct sram_partition *part;

  if (!sram->partitions)
    return;

  part = &sram->partition[sram->partitions - 1];
  for (; sram->partitions; sram->partitions--, part--) {
    if (part->battr.size)
      device_remove_bin_file(sram->dev, &part->battr);

    if (part->pool &&
        gen_pool_avail(part->pool) < gen_pool_size(part->pool))
      // if available free space of the specified pool (part->pool) is
      // less than the size in bytes of memory managed by the pool
      dev_err(sram->dev, "removed pool while SRAM allocated\n");
  }
}

/**
 * compare the two start address of two struct sram_reserve
 * 
 * @param priv not used here
 * @param a 
 * @param b 
 * @return int 
 */
static int sram_reserve_cmp(void *priv, struct list_head *a, struct list_head *b) {
  /**
     * list_entry(ptr, type, member) - get the struct for this entry
     * 
     * it is actually container_of(ptr, type, member)
     * 
     * @ptr:	the &struct list_head pointer.
     * @type:	the type of the struct this is embedded in.
     * @member:	the name of the list_head within the struct.
     */
  struct sram_reserve *ra = list_entry(a, struct sram_reserve, list);
  struct sram_reserve *rb = list_entry(b, struct sram_reserve, list);

  return ra->start - rb->start;
}

static int sram_reserve_regions(struct sram_dev *sram, struct resource *res) {
  // https://www.cnblogs.com/xiaojiang1025/p/6368260.html great explanation in Chinese
  struct device_node *np = sram->dev->of_node;
  struct device_node *child;
  unsigned long size, cur_start, cur_size;
  struct sram_reserve *rblocks, *block; // block is the working ptr
  struct list_head reserve_list;
  unsigned int nblocks, exports = 0;
  const char *label;
  int ret = 0;

  INIT_LIST_HEAD(&reserve_list); // initialize the list reserve_list

  size = resource_size(res); // size of the resource

  /**
     * We need an additional block to mark the end of the memory region after
     * the reserved blocks from the dt are processed
     */

  // nblocks = # of blocks
  // here we add one device_node
  nblocks = (np) ? of_get_available_child_count(np) + 1 : 1;

  // rblocks = reserved block
  // allocate space for only the pointers to the new reserved blocks
  rblocks = kcalloc(nblocks, sizeof(*rblocks), GFP_KERNEL);
  if (!rblocks) {
    return -ENOMEM;
  }

  block = &rblocks[0]; // @block is the pointer to the first block of the newly allocated reserved blocks

  // for each ptr to the device_node in the list(starting with np)
  for_each_available_child_of_node(np, child) {
    struct resource child_res; // child resource

    /**
     * of_address_to_resource - Translate device tree address and return as resource
     * int of_address_to_resource(struct device_node *dev, int index,
     *	                           struct resource *r)
     * 
     * Note that if your address is a PIO address, the conversion will fail if
     * the physical address can't be internally converted to an IO token with
     * pci_address_to_pio(), that is because it's either called too early or it
     * can't be matched to any host bridge IO space
     */
    ret = of_address_to_resource(child, 0, &child_res);
    if (ret < 0) {
      dev_err(sram->dev,
              "could not get address for node %pOF\n",
              child);
      goto err_chunks;
    }

    // if the child resource starts before original resource's start
    //    or the child resource ends after original resource's end, then error
    if (child_res.start < res->start || child_res.end > res->end) {
      dev_err(sram->dev,
              "reserved block %pOF outside the sram area\n",
              child);
      ret = -EINVAL;
      goto err_chunks;
    }

    // after setting up the resource, set corresponding fields
    block->start = child_res.start - res->start;
    block->size = resource_size(&child_res);

    // then insert to the first of the reserve_list
    list_add_tail(&block->list, &reserve_list);

    // each device_node has a field - struct property *properties
    // of_find_property() deals with @properties
    if (of_find_property(child, "export", NULL))
      block->export = true;

    if (of_find_property(child, "pool", NULL))
      block->pool = true;

    if (of_find_property(child, "protect-exec", NULL))
      block->protect_exec = true;

    // then, if any of the properties is true && block size != 0
    if ((block->export || block->pool || block->protect_exec) &&
        block->size) {

      exports++;

      label = NULL;
      /**
       * of_property_read_string - Find and read a string from a property
       * 
       * int of_property_read_string(struct device_node *np, 
       *                             const char *propname,
       *                             const char **out_string)
       * 
       * @np:		device node from which the property value is to be read.
       * @propname:	name of the property to be searched.
       * @out_string:	pointer to null terminated return string, modified only if
       *		return value is 0.
      *
      * Search for a property in a device tree node and retrieve a null
      * terminated string value (pointer to data, not a copy). Returns 0 on
      * success, -EINVAL if the property does not exist, -ENODATA if property
      * does not have a value, and -EILSEQ if the string is not null-terminated
      * within the length of the property data.
      *
      * The out_string pointer is modified only if a valid string can be decoded.
      */
      ret = of_property_read_string(child, "label", &label);
      if (ret && ret != -EINVAL) { // if retval is not 0, and not -EINVAL
        dev_err(sram->dev,
                "%pOF has invalid label name\n",
                child);
        goto err_chunks;
      }

      // if label is NULL, set it to be the name of child block
      if (!label)
        label = child->name;

      /**
       * devm_kstrdup - Allocate resource managed space and
       *                copy an existing string into that.
       * char *devm_kstrdup(struct device *dev, const char *s, gfp_t gfp)
       * 
       * @dev: Device to allocate memory for
       * @s: the string to duplicate
       * @gfp: the GFP mask used in the devm_kmalloc() call when
       *       allocating memory
       * RETURNS:
       * Pointer to allocated string on success, NULL on failure.
       */
      block->label = devm_kstrdup(sram->dev,
                                  label, GFP_KERNEL);
      if (!block->label) {
        ret = -ENOMEM;
        goto err_chunks;
      }

      // print debug messages
      dev_dbg(sram->dev, "found %sblock '%s' 0x%x-0x%x\n",
              block->export ? "exported " : "", block->label,
              block->start, block->start + block->size);
    } else {
      dev_dbg(sram->dev, "found reserved block 0x%x-0x%x\n",
              block->start, block->start + block->size);
    }

    block++; // pointer arithmetic, go to next block
  }

  child = NULL;

  /* the last chunk marks the end of the region */
  // mark the last block's start = size, size = 0;
  /**
   * Question: rblock is a pointer to struct sram_reserve, can also be considered
   * as an address, pointing to the start of an array of struct sram_reserve
   * the use of rblocks[nblocks - 1] means getting the last struct sram_reserve.
   * Not sure if my understanding is correct
   */
  rblocks[nblocks - 1].start = size;
  rblocks[nblocks - 1].size = 0;

  // insert the list field of the last struct sram_reserve to reserve_list
  list_add_tail(&rblocks[nblocks - 1].list, &reserve_list);

  // sort the reserve_list according to function sram_reserve_cmp
  list_sort(NULL, &reserve_list, sram_reserve_cmp);

  if (exports) {
    /**
     * devm_kcalloc() is a wrapper function of devm_kmalloc()
     *
     * void *devm_kmalloc(struct device *dev, size_t size, gfp_t gfp);
     * 
     * devm_kmalloc - Resource-managed kmalloc
     * @dev: Device to allocate memory for
     * @size: Allocation size
     * @gfp: Allocation gfp flags
     *
     * Managed kmalloc.  Memory allocated with this function is
     * automatically freed on driver detach.  Like all other devres
     * resources, guaranteed alignment is unsigned long long.
     *
     * RETURNS:
     * Pointer to allocated memory on success, NULL on failure.
     */
    sram->partition = devm_kcalloc(sram->dev, exports,
                                   sizeof(*sram->partition),
                                   GFP_KERNEL);
    if (!sram->partition) {
      ret = -ENOMEM;
      goto err_chunks;
    }
  }

  cur_start = 0;

  // use block as the working ptr, traversing the reserve_list
  list_for_each_entry(block, &reserve_list, list) {
    /* can only happen if sections overlap */
    if (block->start < cur_start) {
      dev_err(sram->dev,
              "block at 0x%x starts after current offset 0x%lx\n",
              block->start, cur_start);
      ret = -EINVAL;
      // free all partitions of sram
      sram_free_partitions(sram);
      goto err_chunks;
    }

    if ((block->export || block->pool || block->protect_exec) &&
        block->size) {
      ret = sram_add_partition(sram, block,
                               res->start + block->start);
      if (ret) {
        sram_free_partitions(sram);
        goto err_chunks;
      }
    }

    /* current start is in a reserved block, so continue after it */
    if (block->start == cur_start) {
      cur_start = block->start + block->size;
      continue;
    }

    /*
		 * allocate the space between the current starting
		 * address and the following reserved block, or the
		 * end of the region.
		 */
    cur_size = block->start - cur_start;

    dev_dbg(sram->dev, "adding chunk 0x%lx-0x%lx\n",
            cur_start, cur_start + cur_size);

    // Add a new chunk of special memory to the specified pool.
    ret = gen_pool_add_virt(sram->pool,
                            (unsigned long)sram->virt_base + cur_start,
                            res->start + cur_start, cur_size, -1);
    if (ret < 0) {
      sram_free_partitions(sram); //undo allocation if failed
      goto err_chunks;
    }

    /* next allocation after this reserved block */
    cur_start = block->start + block->size;
  }

err_chunks:
  /**
   * of_node_put() - Decrement refcount of a node
   * @node:	Node to dec refcount, NULL is supported to simplify writing of
   *		    callers
   */
  of_node_put(child);
  kfree(rblocks);

  return ret;
}

/**
 * struct regmap is defined in <drivers/base/regmap/internal.h>
 * used for register map
 * 
 * NOT yet understood what this means
 */
static int atmel_securam_wait(void) {
  struct regmap *regmap;
  u32 val;

  regmap = syscon_regmap_lookup_by_compatible("atmel,sama5d2-secumod");
  if (IS_ERR(regmap))
    return -ENODEV;

  return regmap_read_poll_timeout(regmap, AT91_SECUMOD_RAMRDY, val,
                                  val & AT91_SECUMOD_RAMRDY_READY,
                                  10000, 500000);
}
/**
 * what's the meaning of this struct definition
 * 
 */
static const struct of_device_id sram_dt_ids[] = {
    {.compatible = "mmio-sram"},
    {.compatible = "atmel,sama5d2-securam", .data = atmel_securam_wait},
    {}
};


static int sram_probe(struct platform_device *pdev) {
  struct sram_dev *sram;
  int ret;
  int (*init_func)(void);

  sram = devm_kzalloc(&pdev->dev, sizeof(*sram), GFP_KERNEL);
  if (!sram)
    return -ENOMEM;

  sram->dev = &pdev->dev;

  if (of_property_read_bool(pdev->dev.of_node, "no-memory-wc"))
    sram->virt_base = devm_platform_ioremap_resource(pdev, 0);
  else
    sram->virt_base = devm_platform_ioremap_resource_wc(pdev, 0);
  if (IS_ERR(sram->virt_base)) {
    dev_err(&pdev->dev, "could not map SRAM registers\n");
    return PTR_ERR(sram->virt_base);
  }

  sram->pool = devm_gen_pool_create(sram->dev, ilog2(SRAM_GRANULARITY),
                                    NUMA_NO_NODE, NULL);
  if (IS_ERR(sram->pool))
    return PTR_ERR(sram->pool);

  sram->clk = devm_clk_get(sram->dev, NULL);
  if (IS_ERR(sram->clk))
    sram->clk = NULL;
  else
    clk_prepare_enable(sram->clk);

  ret = sram_reserve_regions(sram,
                             platform_get_resource(pdev, IORESOURCE_MEM, 0));
  if (ret)
    goto err_disable_clk;

  platform_set_drvdata(pdev, sram);

  init_func = of_device_get_match_data(&pdev->dev);
  if (init_func) {
    ret = init_func();
    if (ret)
      goto err_free_partitions;
  }

  dev_dbg(sram->dev, "SRAM pool: %zu KiB @ 0x%p\n",
          gen_pool_size(sram->pool) / 1024, sram->virt_base);

  return 0;

err_free_partitions:
  sram_free_partitions(sram);
err_disable_clk:
  if (sram->clk)
    clk_disable_unprepare(sram->clk);

  return ret;
}

static int sram_remove(struct platform_device *pdev) {
  /**
   * get the device's driver_data (i.e. sram)
   */
  struct sram_dev *sram = platform_get_drvdata(pdev);

  sram_free_partitions(sram);


  if (gen_pool_avail(sram->pool) < gen_pool_size(sram->pool))
    dev_err(sram->dev, "removed while SRAM allocated\n");

  if (sram->clk)
    clk_disable_unprepare(sram->clk);

  return 0;
}

/**
 * struct platform_driver is defined in <linux/platform_device.h>
 * three of the fields (used here):
 *      struct device_driver driver;
 *      int (*probe)(struct platform_device *);
 *      int (*remove)(struct platform_device *);
 */
static struct platform_driver sram_driver = {
    .driver = {
        .name = "sram",
        .of_match_table = sram_dt_ids, // struct of_device_id; used for matching a device
    },
    .probe = sram_probe,
    .remove = sram_remove,
};

static int __init sram_init(void) {
  return platform_driver_register(&sram_driver);
}

postcore_initcall(sram_init);