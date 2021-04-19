## Phantom device


### spinlock
```spin_lock_irqsave``` is basically used to save the interrupt state before taking the spin lock, this is because spin lock disables the interrupt, when the lock is taken in interrupt context, and re-enables it when while unlocking. The interrupt state is saved so that it should reinstate the interrupts again.

Example:

1. Lets say interrupt x was disabled before spin lock was acquired
2. ```spin_lock_irq``` will disable the interrupt x and take the the lock
3. ```spin_unlock_irq``` will enable the interrupt x.

So in the 3rd step above after releasing the lock we will have interrupt x enabled which was earlier disabled before the lock was acquired.

So **only when you are sure that interrupts are not disabled only then you should spin_lock_irq otherwise you should always use ```spin_lock_irqsave```**.

[src](https://stackoverflow.com/questions/2559602/spin-lock-irqsave-vs-spin-lock-irq#14963815)


### nonseekable_open
https://lwn.net/Articles/97154/

