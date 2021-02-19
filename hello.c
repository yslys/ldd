/*
  This piece of code is borrowed from https://www.youtube.com/watch?v=Zi6ooCultI0

  Please refer to https://tldp.org/LDP/lkmpg/2.4/html/x579.html for the 
  file_operations structure
*/

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>

/* function prototypes:
  ssize_t (*read) (struct file *, char *, size_t, loff_t *);
  ssize_t (*write) (struct file *, const char *, size_t, loff_t *);
  int (*open) (struct inode *, struct file *);
  int (*release) (struct inode *, struct file *);
*/

// to make sure that a device is opened once, need to keep track of it
volatile static int is_open = 0;

static char message[1024];
int num_bytes = 0;

// below is the user-defined functions

ssize_t hello_read (struct file *fp, char *output_buffer, size_t num_bytes, loff_t *offset)
{
  // num of bytes already read
  int bytes_read = 0;

  if(offset == NULL) return -EINVAL;

  if(*offset >= num_bytes) return 0;

  while((bytes_read < num_bytes) && (*offset < num_bytes)){
    put_user(message[*offset], output_buffer[bytes_read]);
  }

}

// when the user is writing to our program, we're handling the write
// hence, there's a const char __user *
ssize_t hello_write (struct file *fp, const char __user *input_buffer, size_t num_bytes, loff_t *offset)
{

}

int hello_open (struct inode *inode_ptr, struct file *fp)
{
  if(is_open == 1){
    printk(KERN_INFO "Error - hello device already open\n");
    return -EBUSY;
  }
  is_open = 1;
}

int hello_release (struct inode *inode_ptr, struct file *fp)
{
  if(is_open == 0){
    printk(KERN_INFO "Error - device wasn't opened\n");
    return -EBUSY;
  }
  is_open = 0;

  // decrement the usage count of the module
  module_put(THIS_MODULE);
}

// structure file_operations init
struct file_operations fops = {
  // nameof the field of the struct: nameof the func we define
  read: hello_read,
  write: hello_write,
  open: hello_open,
  release: hello_release
};




static int __init hello_start(void)
{
  printk(KERN_INFO "Hello!\n");

  strncpy(message, "Hello world.", 1023);
  num_bytes = strlen(message);

  // doc of registering a device
  // https://tldp.org/LDP/lkmpg/2.4/html/x579.html#AEN631
  // int register_chrdev(unsigned int major, const char *name, 
  //                                         struct file_operations *fops); 
  // three params:
  // major = 0, the return val of register_chrdev() will be dynamically allocated 
  // major number
  // name = "hello", the name of the module
  int devnum = register_chrdev(0, "hello", &fops);
  printk(KERN_INFO "The hello device's major number is: %d\n", devnum);

  return 0;
}

static void __exit hello_end(void)
{
  printk(KERN_INFO "Goodbye!\n");
}

module_init(hello_start);
module_exit(hello_end);