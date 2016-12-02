/*
 * kv_mod.c -- the bare kv_mod char module
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
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>

#include <linux/kernel.h>	/* printk() */
#include <linux/slab.h>		/* kmalloc() */
#include <linux/fs.h>		/* everything... */
#include <linux/errno.h>	/* error codes */
#include <linux/types.h>	/* size_t */
#include <linux/proc_fs.h>
#include <linux/fcntl.h>	/* O_ACCMODE */
#include <linux/seq_file.h>
#include <linux/cdev.h>
#include <linux/sched.h>
#include <linux/cred.h>

#include <asm/uaccess.h>	/* copy_*_user */

#include "kv_mod.h"		   /* local definitions */

/*
 * Our parameters which can be set at load time.
 */
int kv_mod_major   = KV_MOD_MAJOR;
int kv_mod_minor   = 0;
int kv_mod_nr_devs = KV_MOD_NR_DEVS;

module_param(kv_mod_major,   int, S_IRUGO);
module_param(kv_mod_minor,   int, S_IRUGO);
module_param(kv_mod_nr_devs, int, S_IRUGO);
void fix_uid(int *idnum);
void insert(struct kv_list **data, const char __user *buf);
int get_user_id(void);


MODULE_AUTHOR("Alessandro Rubini, Jonathan Corbet modified K. Shomper and further modified by Rich Lively and Tim Froberg");
MODULE_LICENSE("Dual BSD/GPL");

/* the set of devices allocated in kv_mod_init_module */
struct kv_mod_dev *kv_mod_devices = NULL;

/*
 * Release the memory held by the kv_mod device; must be called with the device
 * semaphore held.  Requires that dev not be NULL
 */
int remove_data(struct kv_mod_dev *dev) {
	//TODO: hold semaphore?
    close_vault(dev->data);
	dev->data = NULL;

	return 0;
}

/*
 * Open: to open the device is to initialize it for the remaining methods.
 */
int kv_mod_open(struct inode *inode, struct file *filp) {
    struct kv_mod_dev *dev;
    /* we need the scull_dev object (dev), but the required prototpye
      for the open method is that it receives a pointer to an inode.
      now an inode contains a struct cdev (the field is called
      i_cdev) and we can use this field with the container_of macro
      to obtain the scull_dev object (since scull_dev also contains
      a cdev object.
    */
    printk(KERN_WARNING "starting open\n");
    dev = container_of(inode->i_cdev, struct kv_mod_dev, cdev);
    /* so that we don't need to use the container_of() macro repeatedly,
		we save the handle to dev in the file's private_data for other methods.
	 */
    filp->private_data = dev;

    //TODO: finish open setup

    //here we need to decide if we set fp to null or the first
    //key needed
    int uid = get_user_id();
    if (num_keys(dev->data, uid) == 0 ) {
        dev->data->ukey_data[uid].fp = NULL;
    } else {
        //TODO: test if this is right
        //set the file pointer to the first key value pair in the user's key vault
        dev->data->ukey_data[uid].fp = dev->data->ukey_data[uid].data[0];
    }
    printk(KERN_WARNING "finished open\n");
	return 0;
}

/*
 * Release: release is the opposite of open, so it deallocates any
 *          memory allocated by kv_mod_open and shuts down the device.
 *          since open didn't allocate anything and our device exists
 *          only in memory, there are no actions to take here.
 */
int kv_mod_release(struct inode *inode, struct file *filp) {
	//Nothing to release; stub
    return 0;
}

/*
 * Read: implements the read action on the device by reading count
 *       bytes into buf beginning at file position f_pos from the file 
 *       referenced by filp.  The attribute "__user" is a gcc extension
 *       that indicates that buf originates from user space memory and
 *       should therefore not be trusted.
 */

ssize_t kv_mod_read(struct file *filp, char __user *buf, size_t count,
                    loff_t *f_pos) {
    //TODO: stub
    return 0;
}

ssize_t kv_mod_write(struct file *filp, const char __user *buf, size_t count,
                     loff_t *f_pos) {
    printk(KERN_WARNING "Debug:  starting write on buf: %s\n", buf);
    struct kv_mod_dev *dev = filp->private_data;
    ssize_t retval = -ENOMEM;
   
    
    struct key_vault *vault = dev->data;
    struct kv_list *curr = vault->ukey_data->fp;
	int idnum = get_user_id();
    char *key;
    char *val;

    //get the semaphore
    if (down_interruptible(&dev->sem)) return -ERESTARTSYS;

    if (strcmp(buf, "") == 0) {
        //delete
        printk(KERN_WARNING "Debug:  deleting key value pair\n");
        if (curr == NULL) {
            //nothing to delete
            printk(KERN_WARNING "Debug:  nothing to erase\n");
            goto out;
        }
        key = curr->kv.key;
        val = curr->kv.val;
        //update the filepointer
        vault->ukey_data->fp = next_key(vault, idnum, curr);
        //delete the pair
        delete_pair(vault, idnum, key, val);
        printk(KERN_WARNING "Debug:  finished delete; fp now points to: %p\n", vault->ukey_data->fp);
    } else {
        //insert
        printk(KERN_WARNING "Debug:  inserting %s\n", buf);
    	key = kmalloc(MAX_KEY_SIZE, GFP_KERNEL);
        val = kmalloc(MAX_VAL_SIZE, GFP_KERNEL);
        //simple parsing for loop (doesn't handle bad input)
        int space = -1;
        int i;
        for (i = 0; i < strlen(buf); i++) {
            if (buf[i] == ' ') {
                space = i;
            }
        }
        strncpy(key, buf, space);
        key[space] = '\0';
        for (i = space + 1; i < strlen(buf); i++) {
            val[i] = buf[i];
        }
        printk(KERN_WARNING "Debug:  key: %s\nval: %s\n", key, val);
        insert_pair(vault, idnum, key, val);
        vault->ukey_data->fp = find_key_val(vault, idnum, key, val);
        printk(KERN_WARNING "Debug:  finished inserting; fp now points to %p\n", vault->ukey_data->fp);
    }
	
	/* release the semaphore and return */
  out:
	up(&dev->sem);
	return retval;

}

//void insert(struct kv_list **data, const char __user *buf) {
    /*int i, space;
    for (i = 0; i < strlen(buf); i++) {
        if (buf[i] == ' ') {
            space = i;
            break;
        }
    }

    char* key = new char[space];
    for (i = 0; i < space; i++) key[i] = buf[i];
    for (i = (space + 1); i < strlen(buf); i++) val[i] = buf[i];
    insert_from_list(data, key, val);*/
    //stub
//}

void fix_uid(int *id) {
    //is this going to cause problems?
	if (*id == 0) *id = 1;
	else *id -= 998;
}

int get_user_id(void) {
    int id = get_current_user()->uid.val;
    fix_uid(&id);
    return id;
}


long kv_mod_ioctl(struct file *filp, unsigned int cmd, unsigned long arg) {
    //TODO: stub
    return 0;
}

/*
 * Seek:  the only one of the "extended" operations which kv_mod implements.
 */
loff_t kv_mod_llseek(struct file *filp, loff_t off, int whence) {
    //TODO: stub
    return 0;
}

/* this assignment is what "binds" the template file operations with those that
 * are implemented herein.
 */
struct file_operations kv_mod_fops = {
	.owner =    THIS_MODULE,
	.llseek =   kv_mod_llseek,
	.read =     kv_mod_read,
	.write =    kv_mod_write,
	.unlocked_ioctl = kv_mod_ioctl,
	.open =     kv_mod_open,
	.release =  kv_mod_release,
};

/*
 * The cleanup function is used to handle initialization failures as well.
 * Thefore, it must be careful to work correctly even if some of the items
 * have not been initialized
 */
void kv_mod_cleanup_module(void) {
    printk(KERN_WARNING "Debug:  removing module\n");

    dev_t devno = MKDEV(kv_mod_major, kv_mod_minor);

	/* if the devices were succesfully allocated, then the referencing pointer
    * will be non-NULL.
    */
	if (kv_mod_devices != NULL) {

	   /* Get rid of our char dev entries by first deallocating memory and then
       * deleting them from the kernel */
	   int i;
		for (i = 0; i < kv_mod_nr_devs; i++) {
			remove_data(kv_mod_devices + i);
			cdev_del(&kv_mod_devices[i].cdev);
		}

		/* free the referencing structures */
		kfree(kv_mod_devices);
	}

    unregister_chrdev_region(devno, kv_mod_nr_devs);

    printk(KERN_WARNING "Debug:  removal complete\n");
}


/*
* Set up the char_dev structure for this device.
*/
static void kv_mod_setup_cdev(struct kv_mod_dev *dev, int index) {
  int err, devno = MKDEV(kv_mod_major, kv_mod_minor + index);
	
  /* cdev_init() and cdev_add() are kernel-required initialization */
  cdev_init(&dev->cdev, &kv_mod_fops);
  dev->cdev.owner = THIS_MODULE;
  dev->cdev.ops   = &kv_mod_fops;
  err             = cdev_add (&dev->cdev, devno, 1);

  /* Fail gracefully if need be */
  if (err) printk(KERN_NOTICE "Error %d adding kv_mod%d", err, index);
}

int kv_mod_init_module(void) {
    int result, i;
    dev_t dev = 0;

    printk(KERN_WARNING "Debug:  initializing kv_mod module\n");
    /*
    * Compile-time default for major is zero (dynamically assigned) unless 
    * directed otherwise at load time.  Also get range of minors to work with.
    */

    if (kv_mod_major == 0) {
		result      = alloc_chrdev_region(&dev,kv_mod_minor,kv_mod_nr_devs,"kv_mod");
		kv_mod_major = MAJOR(dev);
        printk(KERN_WARNING "Debug:  kv_mod_major number is %d\n", kv_mod_major);
	} else {
		dev    = MKDEV(kv_mod_major, kv_mod_minor);
		result = register_chrdev_region(dev, kv_mod_nr_devs, "kv_mod");
	}

	/* report failue to aquire major number */
	if (result < 0) {
		printk(KERN_WARNING "kv_mod: can't get major %d\n", kv_mod_major);
		return result;
	}

    /* 
	 * allocate the devices -- we can't have them static, as the number
	 * can be specified at load time
	 */
	kv_mod_devices = kmalloc(kv_mod_nr_devs*sizeof(struct kv_mod_dev), GFP_KERNEL);

	/* exit if memory allocation fails */
	if (!kv_mod_devices) {
		result = -ENOMEM;
        kv_mod_cleanup_module();
        return result;
	}

	/* otherwise, zero the memory */
	memset(kv_mod_devices, 0, kv_mod_nr_devs * sizeof(struct kv_mod_dev));
   /* Initialize each device. */
	for (i = 0; i < kv_mod_nr_devs; i++) {
        // Need to alloc the data field so there is something for init_vault to init
        kv_mod_devices[i].data = kmalloc(sizeof(struct key_vault), GFP_KERNEL); 
        memset(kv_mod_devices[i].data, 0, sizeof(struct key_vault));

        init_vault(kv_mod_devices[i].data, MAX_KEY_USER);
		sema_init(&kv_mod_devices[i].sem, 1);
		kv_mod_setup_cdev(&kv_mod_devices[i], i);
	}

    printk(KERN_WARNING "Debug:  initializing complete\n");
      /* succeed */
	return 0;
}




/* identify to the kernel the entry points for initialization and release, these
 * functions are called on insmod and rmmod, respectively
 */
module_init(kv_mod_init_module);
module_exit(kv_mod_cleanup_module);
