## Set of keywords in LDD3 codes

+ DEBFLAGS - debug flags
+ file - p. 53. represents an open file
	+ ```
    #include <linux/fs.h>
		struct file {
		 	mode_t f_mode; // file mode, identifies the file as readable or writable or both
		 				   // FMODE_READ, FMODE_WRITE, read/write permission check
		 	loff_t f_ops; // current reading or writing position
		 	unsigned int f_flags; // file flags (O_RDONLY, O_NONBLOCK, O_SYNC)
		 	struct file_operations *f_op;
		 	void *private_data; // open syscall sets this ptr to NULL before calling
                                // open for the driver
            struct dentry *f_dentry; // directory entry associated with the file
		};```
+ FILE - p. 53. only used in C
+ file_operations - p. 49
    + ```#include <linux/fs.h>
         struct file_operations {
            struct module *owner;
            loff_t (*llseek) (struct file *, loff_t, int);
            ssize_t (*read) (struct file *, char __user *, size_t, loff_t *);
            ssize_t (*aio_read)(struct kiocb *, char __user *, size_t, loff_t);
            ssize_t (*write) (struct file *, const char __user *, size_t, loff_t *);
            ssize_t (*aio_write)(struct kiocb *, const char __user *, size_t, loff_t *);
            int (*readdir) (struct file *, void *, filldir_t);
            unsigned int (*poll) (struct file *, struct poll_table_struct *);
            int (*ioctl) (struct inode *, struct file *, unsigned int, unsigned long);
            int (*mmap) (struct file *, struct vm_area_struct *);
            int (*open) (struct inode *, struct file *);
            int (*flush) (struct file *);
            int (*release) (struct inode *, struct file *);
            int (*fsync) (struct file *, struct dentry *, int);
            int (*aio_fsync)(struct kiocb *, int);
            int (*fasync) (int, struct file *, int);
            int (*lock) (struct file *, int, struct file_lock *);
            ssize_t (*readv) (struct file *, const struct iovec *, unsigned long, loff_t *);
            ssize_t (*writev) (struct file *, const struct iovec *, unsigned long, loff_t *);
            ssize_t (*sendfile)(struct file *, loff_t *, size_t, read_actor_t, void *);
            ssize_t (*sendpage) (struct file *, struct page *, int, size_t, loff_t *, int);
            unsigned long (*get_unmapped_area)(struct file *, unsigned long, 
                    unsigned long,unsigned long, unsigned long);
            int (*check_flags)(int);
            int (*dir_notify)(struct file *, unsigned long);
         };```
+ inode - p. 55. Used by the kernel to internally to represent files (there can be numerous file structures representing multiple open descriptors on a single file, but they all point to a single inode structure)
    + '''#include
         struct inode {
             dev_t i_rdev; // actual device number
             struct cdev *i_cdev; // kernel’s internal structure that represents char devices
         };'''
+ loff_t - long offsets
+ PAGE_SIZE - size of a page
+
+ -ERESTARTSYS - Error RESTART SYStem (return -ERESTARTSYS)
