/**
 * module_param is a macro that defines module parameters, defined in moduleparam.h
 * @name: name of the variable
 * @type: type of the variable
 * @perm: permissions mask to be used for an accompanying sysfs entry
 *        If perm is set to 0, there is no sysfs entry at all; otherwise, it 
 *        appears under /sys/module with the given set of permissions.
 * 
 * e.g. 
 * loading the hellop module (p. 36) using following command line:
 *      $ insmod hellop howmany=10 whom="Mom"
 * Upon being loaded that way, hellop would say “Hello, Mom” 10 times.
 * However, before insmod can change module parameters, the module must make
 * them available. The module_param macro should be placed outside of any 
 * function and is typically found near the head of the source file. So hellop 
 * would declare its parameters and make them available to insmod as follows:
 *      static char *whom = "world";
 *      static int howmany = 1;
 *      module_param(howmany, int, S_IRUGO);
 *      module_param(whom, charp, S_IRUGO);
 */ 
module_param(name, type, perm);


/**
 * container_of(pointer, container_type, container_field);
 * (linux/kernel.h)
 * container_of - cast a member of a structure out to the containing structure
 * @pointer: a pointer to a field of type container_field(3rd param)
 * @container_type: the type of the struct that contains the container_field
 * @container_field: type of the field pointed to by the pointer
 * @return: a pointer to the containing structure
 *
 * In this case, given the inode, take its field - inode->i_cdev (of type cdev)
 * and specify its container's type - struct scull_dev, then the return value
 * is the pointer to the container, i.e. pointer to the struct scull_dev.
 */
container_of(pointer, container_type, container_field);
