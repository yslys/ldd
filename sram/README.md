[line #322](https://github.com/yslys/ldd/blob/main/sram/sram.c#L322) (in sram_free_partitions())
```
if (part->pool &&
        gen_pool_avail(part->pool) < gen_pool_size(part->pool)) {
      dev_err(sram->dev, "removed pool while SRAM allocated\n");
}
``

[line #533](https://github.com/yslys/ldd/blob/main/sram/sram.c#L533) (in sram_reserve_regions())
```
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
```


[line #733](https://github.com/yslys/ldd/blob/main/sram/sram.c#L733) (in sram_remove())
```
/*
  Why does available space in pool less then pool size will cause an error?
  "removed while SRAM allocated"?
*/
if (gen_pool_avail(sram->pool) < gen_pool_size(sram->pool))
    dev_err(sram->dev, "removed while SRAM allocated\n");
```

