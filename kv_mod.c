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

MODULE_AUTHOR("Alessandro Rubini, Jonathan Corbet modified K. Shomper and further modified by Rich Lively and Tim Froberg");
MODULE_LICENSE("Dual BSD/GPL");

/* the set of devices allocated in kv_mod_init_module */
struct kv_mod_dev *kv_mod_devices = NULL;

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

/*
 * Release the memory held by the kv_mod device; must be called with the device
 * semaphore held.  Requires that dev not be NULL
 */
/*
int kv_mod_trim(struct kv_mod_dev *dev) {
	close_vault(dev->data);
	dev->data = NULL;

	return 0;
}
*/

/*
 * Release: release is the opposite of open, so it deallocates any
 *          memory allocated by kv_mod_open and shuts down the device.
 *          since open didn't allocate anything and our device exists
 *          only in memory, there are no actions to take here.
 */
int kv_mod_release(struct inode *inode, struct file *filp) {
	return 0;
}



int kv_mod_init_module(void) {
	int result, i;
	dev_t dev = 0;

   /*
    * Compile-time default for major is zero (dynamically assigned) unless 
    * directed otherwise at load time.  Also get range of minors to work with.
    */
	if (kv_mod_major == 0) {
		result      = alloc_chrdev_region(&dev,kv_mod_minor,kv_mod_nr_devs,"kv");
		kv_mod_major = MAJOR(dev);
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
		goto fail;
	}

	/* otherwise, zero the memory */
	memset(kv_mod_devices, 0, kv_mod_nr_devs * sizeof(struct kv_mod_dev));

   /* Initialize each device. */
	for (i = 0; i < kv_mod_nr_devs; i++) {
		init_vault(kv_mod_devices[i].data, MAX_KEY_USER);

		sema_init(&kv_mod_devices[i].sem, 1);
		kv_mod_setup_cdev(&kv_mod_devices[i], i);
	}
 
   /* succeed */
	return 0;

	/* failure, so cleanup is necessary */
  fail:
	kv_mod_cleanup_module();
	return result;
}



/*
 * The cleanup function is used to handle initialization failures as well.
 * Thefore, it must be careful to work correctly even if some of the items
 * have not been initialized
 */
void kv_mod_cleanup_module(void) {
}

/* identify to the kernel the entry points for initialization and release, these
 * functions are called on insmod and rmmod, respectively
 */
module_init(kv_mod_init_module);
module_exit(kv_mod_cleanup_module);
