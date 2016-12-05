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

/*
Modifiers: Rich Lively and Tim Froberg
Date: 12/5/2016
Version 1.2
Summary of Modifications
- Version 1.2: Cleaned up code and improved comments
- Version 1.1: Fixed ioctl and lseek
- Version 1: first working version
*/

//TODO: remove debugging printk
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

char seek_key[80];

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
    close_vault(dev->data);
	dev->data = NULL;

	return 0;
}

/*
 * Open: to open the device is to initialize it for the remaining methods.
 */
int kv_mod_open(struct inode *inode, struct file *filp) {
    struct kv_mod_dev *dev;
    /* we need the kv_mod_dev object (dev), but the required prototpye
      for the open method is that it receives a pointer to an inode.
      now an inode contains a struct cdev (the field is called
      i_cdev) and we can use this field with the container_of macro
      to obtain the kv_mod_dev object (since kv_mod_dev also contains
      a cdev object.
    */
    dev = container_of(inode->i_cdev, struct kv_mod_dev, cdev);

    /* so that we don't need to use the container_of() macro repeatedly,
		we save the handle to dev in the file's private_data for other methods.
	 */
    filp->private_data = dev;

    if (down_interruptible(&dev->sem)) return -ERESTARTSYS;

    int uid = get_user_id();
    /* no keys in the vault, so set the filepointer to null */
    if (num_pairs(dev->data, uid) == 0 ) {
        dev->data->ukey_data[uid-1].fp = NULL;
    }

    /* there are keys, so set the filepointer to the first key-value pair */
    else {
        dev->data->ukey_data[uid-1].fp = dev->data->ukey_data[uid-1].data[0];
    }

    /* release the semaphore and return */
    up(&dev->sem);
	return 0;
}

/*
 * Release: release is the opposite of open, so it deallocates any
 *          memory allocated by kv_mod_open and shuts down the device.
 *          since open didn't allocate anything and our device exists
 *          only in memory, there are no actions to take here.
 */
int kv_mod_release(struct inode *inode, struct file *filp) {
	/* Nothing to release; stub */
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
    ssize_t retval = -ENOMEM;
    struct kv_mod_dev *dev = filp->private_data;
    struct key_vault *vault = dev->data;
    /* acquire semephore */
    if (down_interruptible(&dev->sem)) return -ERESTARTSYS;
    /* get 1-indexed user id */
    int uid = get_user_id();
    /* get key-val pair at current fp for this user */
    struct kv_list *curr = vault->ukey_data[uid-1].fp;
    /* nothing to read for the user */
    if (curr == NULL) {
        retval = 0;
        goto out;
    }
    /* extract the key and value from the filepointer */
    char key[MAX_KEY_SIZE];
    char val[MAX_VAL_SIZE];
    strncpy(key, curr->kv.key, 80);
    strncpy(val, curr->kv.val, 80);

    /* assemble pair into local buffer */
    char kbuf[80];
    snprintf(kbuf, 80, "%s %s", key, val);

   /* the copy below originally had 80 where 79 appears and did not have
       the '+1' part.  As a result, length of kbuf characters were copied
       into buf, but this did not include the trailing NULL character, so
       buf was not properly NULL terminated.  KAS
     */
    /* copy local buff to user buffer */
    if (copy_to_user(buf, kbuf, strnlen(kbuf, 79)+1)) {
		retval = -EFAULT;
		goto out;
	}

    /* update the filepointer */
    vault->ukey_data[uid-1].fp = next_key(vault, uid, curr);
    /* succesfully wrote one key-value pair so return 1 */
    retval = 1;
  out:
    /* release semaphore and return */
    up(&dev->sem);
    return retval;
}

ssize_t kv_mod_write(struct file *filp, const char __user *buf, size_t count,
                     loff_t *f_pos) {
    struct kv_mod_dev *dev = filp->private_data;
    ssize_t retval = -ENOMEM;
    if (down_interruptible(&dev->sem)) return -ERESTARTSYS;

    /* this is where the actual "write" occurs, when we copy from the
    * the user-supplied buffer into the in-memory data area.  This copy is
    * handled by the copy_from_user() function, which handles the
    * transfer of data from user space data structures to kernel space
    * data structures.
    */
    char kbuf[80];
	if (copy_from_user(kbuf, buf, strnlen(buf, 80))) {
		retval = -EFAULT;
		goto out;
	}

    kbuf[strnlen(buf, 80)] = '\0'; 

    /* get the key vault, one-indexed user id, and the user's filepointer */
    struct key_vault *vault = dev->data;
	int uid = get_user_id();
    struct kv_list *curr = vault->ukey_data[uid-1].fp;

    /* if an empty buffer, delete; else insert */
    if (strcmp(kbuf, "") == 0) {
        /* nothing to delete */
        if (curr == NULL) goto out;

        /* update the filepointer */
        vault->ukey_data[uid-1].fp = next_key(vault, uid, curr);
        /* delete the pair */
        delete_pair(vault, uid, curr->kv.key, curr->kv.val);
        /* will return 1 because 1 pair was successfully deleted */
        retval = 1;
    }

    /* insert key-value pair */
    else {
        /* extract key and value from the buffer */
        char key[MAX_KEY_SIZE];
        char val[MAX_VAL_SIZE];
        sscanf(kbuf, "%s %s", key, val);
        /* insert the key-value pair */
        int rc = insert_pair(vault, uid, key, val);
        /* successful insert so set retval to 1 because one pair was successfully written */
        if (rc) retval = 1;
        /* failed to insert */
        else goto out;

        /* update the file pointer to the inserted item */
        vault->ukey_data[uid-1].fp = find_key_val(vault, uid, key, val);
    }
	
	/* release the semaphore and return */
  out:
	up(&dev->sem);
	return retval;
}

/* a crude method for adjusting the user id to close the gap between the id of root and users */
void fix_uid(int *id) {
	if (*id == 0) *id = 1;
	else *id -= 998;
}

/* get's the current user's id; automatically calls fix_uid */
int get_user_id(void) {
    int id = get_current_user()->uid.val;
    fix_uid(&id);
    return id;
}


long kv_mod_ioctl(struct file *filp, unsigned int cmd, unsigned long arg) {
   	int err    = 0;
	int retval = 0;    
	/*
	 * extract the type and number bitfields, and don't decode
	 * wrong cmds: return ENOTTY (inappropriate ioctl) before access_ok()
	 */
	if (_IOC_TYPE(cmd) != KV_MOD_IOC_MAGIC) return -ENOTTY;
	if (_IOC_NR(cmd)   >  KV_MOD_IOC_MAXNR) return -ENOTTY;

	/*
	 * the direction is a bitmask, and VERIFY_WRITE catches R/W
	 * transfers. `Type' is user-oriented, while
	 * access_ok is kernel-oriented, so the concept of "read" and
	 * "write" is reversed
	 */
	if (_IOC_DIR(cmd) & _IOC_READ) {
		err = !access_ok(VERIFY_WRITE, (void __user *)arg, _IOC_SIZE(cmd));
	} else if (_IOC_DIR(cmd) & _IOC_WRITE) {
		err = !access_ok(VERIFY_READ,  (void __user *)arg, _IOC_SIZE(cmd));
	}

	/* exit on error */
	if (err) return -EFAULT;
	
    /* parse the incoming command */
	switch(cmd) {
      case KV_MOD_IOCSKEY:
		  retval = copy_from_user(seek_key, (char*) arg, strnlen((char*) arg, 79)+1);
          break;
      default:
          return -ENOTTY;
    }
    
    return retval;
}

/*
 * Seek:  the only one of the "extended" operations which kv_mod implements.
 */
loff_t kv_mod_llseek(struct file *filp, loff_t off, int whence) {
    struct kv_mod_dev *dev    = filp->private_data; 
    int uid = get_user_id();
    /* get seek_key data using sscanf */
    char key[MAX_KEY_SIZE];
    char val[MAX_VAL_SIZE];
    sscanf(seek_key, "%s %s", key, val);

    /* find the key-value pair; return 0 on failure and 1 on success */
    dev->data->ukey_data[uid-1].fp = find_key_val(dev->data, uid, key, val);
    if (dev->data->ukey_data[uid-1].fp == NULL) {
        return 0;
    } else return 1;
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

    /*
    * Compile-time default for major is zero (dynamically assigned) unless 
    * directed otherwise at load time.  Also get range of minors to work with.
    */

    if (kv_mod_major == 0) {
		result      = alloc_chrdev_region(&dev,kv_mod_minor,kv_mod_nr_devs,"kv_mod");
		kv_mod_major = MAJOR(dev);
	} else {
		dev    = MKDEV(kv_mod_major, kv_mod_minor);
		result = register_chrdev_region(dev, kv_mod_nr_devs, "kv_mod");
	}

	/* report failue to aquire major number */
	if (result < 0) {
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
        /* Need to alloc the data field so there is something for init_vault to init */
        kv_mod_devices[i].data = kmalloc(sizeof(struct key_vault), GFP_KERNEL); 
        memset(kv_mod_devices[i].data, 0, sizeof(struct key_vault));
        init_vault(kv_mod_devices[i].data, MAX_KEY_USER);

		sema_init(&kv_mod_devices[i].sem, 1);
		kv_mod_setup_cdev(&kv_mod_devices[i], i);
	}

      /* succeed */
	return 0;
}




/* identify to the kernel the entry points for initialization and release, these
 * functions are called on insmod and rmmod, respectively
 */
module_init(kv_mod_init_module);
module_exit(kv_mod_cleanup_module);
