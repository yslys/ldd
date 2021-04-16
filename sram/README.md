```
/*
  Why does available space in pool less then pool size will cause an error?
*/
if (gen_pool_avail(sram->pool) < gen_pool_size(sram->pool))
    dev_err(sram->dev, "removed while SRAM allocated\n");
```

