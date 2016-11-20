/*
 * scull.c -- the bare scull char module
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

#include "scull.h"		   /* local definitions */

/*
 * Our parameters which can be set at load time.
 */
int scull_major   = SCULL_MAJOR;
int scull_minor   = 0;
int scull_nr_devs = SCULL_NR_DEVS;
int scull_quantum = SCULL_QUANTUM;
int scull_qset    = SCULL_QSET;

module_param(scull_major,   int, S_IRUGO);
module_param(scull_minor,   int, S_IRUGO);
module_param(scull_nr_devs, int, S_IRUGO);
module_param(scull_quantum, int, S_IRUGO);
module_param(scull_qset,    int, S_IRUGO);

MODULE_AUTHOR("Alessandro Rubini, Jonathan Corbet modified K. Shomper");
MODULE_LICENSE("Dual BSD/GPL");

/* the set of devices allocated in scull_init_module */
struct scull_dev *scull_devices = NULL;

/*
 * Release the memory held by the scull device; must be called with the device
 * semaphore held.  Requires that dev not be NULL
 */
int scull_trim(struct scull_dev *dev) {
	struct scull_qset *next, *dptr;
	int qset = dev->qset;
	int i;

   /* release all the list items */
	for (dptr = dev->data; dptr; dptr = next) {

		/* if list item has associated quantums, release memory for those also */
		if (dptr->data) {

			/* walk qset, releasing each */
			for (i = 0; i < qset; i++) {
				kfree(dptr->data[i]);
			}

			/* then release array of pointers to the quantums and NULL terminate */
			kfree(dptr->data);
			dptr->data = NULL;
		}

		/* advance to next item in list */
		next = dptr->next;
		kfree(dptr);
	}

	/* set the dev fields to initial values */
	dev->size    = 0;
	dev->quantum = scull_quantum;
	dev->qset    = scull_qset;
	dev->data    = NULL;

	return 0;
}

/*
 * The proc filesystem: function to read an entry
 */
#ifdef SCULL_DEBUG /* use proc only if debugging */

int scull_read_procmem(struct seq_file *s, void *v) {
   int i, j;
   int limit = s->size - 80; /* Don't print more than this */

   for (i = 0; i < scull_nr_devs && s->count <= limit; i++) {
      struct scull_dev   *d = &scull_devices[i];
      struct scull_qset *qs = d->data;

      if (down_interruptible(&d->sem)) return -ERESTARTSYS;

      seq_printf(s,"\nDevice %i: qset %i, q %i, sz %li\n",
                    i, d->qset, d->quantum, d->size);

      for (; qs && s->count<=limit; qs=qs->next) { /* scan the list */
         seq_printf(s, "  item at %p, qset at %p\n",
                        qs, qs->data);
         if (qs->data && !qs->next) { /* dump only the last item */
            for (j = 0; j < d->qset; j++) {
               if (qs->data[j]) {
                  seq_printf(s, "    % 4i: %8p\n", j, qs->data[j]);
					}
            }
			}
      }

      up(&scull_devices[i].sem);
   }

   return 0;
}

/*
 * Here are our sequence iteration methods.  Our "position" is
 * simply the device number.
 */
static void *scull_seq_start(struct seq_file *s, loff_t *pos) {
	if (*pos >= scull_nr_devs) return NULL;   /* No more to read */

	return scull_devices + *pos;
}

static void *scull_seq_next(struct seq_file *s, void *v, loff_t *pos) {
	(*pos)++;
	if (*pos >= scull_nr_devs) return NULL;
	return scull_devices + *pos;
}

static void scull_seq_stop(struct seq_file *s, void *v) {
	/* Actually, there's nothing to do here */
}

static int scull_seq_show(struct seq_file *s, void *v) {
	struct scull_dev *dev = (struct scull_dev *) v;
	struct scull_qset *d;
	int i;

	if (down_interruptible(&dev->sem)) return -ERESTARTSYS;

	seq_printf(s, "\nDevice %i: qset %i, q %i, sz %li\n",
			         (int) (dev - scull_devices), dev->qset,
			         dev->quantum, dev->size);
	for (d = dev->data; d; d = d->next) { /* scan the list */
		seq_printf(s, "  item at %p, qset at %p\n", d, d->data);
		if (d->data && !d->next) { /* dump only the last item */
			for (i = 0; i < dev->qset; i++) {
				if (d->data[i]) {
					seq_printf(s, "    % 4i: %8p\n", i, d->data[i]);
				}
			}
		}
	}

	up(&dev->sem);
	return 0;
}
	
/*
 * Tie the sequence operators up.
 */
static struct seq_operations scull_seq_ops = {
	.start = scull_seq_start,
	.next  = scull_seq_next,
	.stop  = scull_seq_stop,
	.show  = scull_seq_show
};

/*
 * Now to implement the /proc files we need only make an open
 * method which sets up the sequence operators.
 */
static int scullmem_proc_open(struct inode *inode, struct file *file) {
	return single_open(file, scull_read_procmem, NULL);
}

static int scullseq_proc_open(struct inode *inode, struct file *file) {
	return seq_open(file, &scull_seq_ops);
}

/*
 * Create a set of file operations for our proc files.
 */
static struct file_operations scullmem_proc_ops = {
	.owner   = THIS_MODULE,
	.open    = scullmem_proc_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = single_release
};

static struct file_operations scullseq_proc_ops = {
	.owner   = THIS_MODULE,
	.open    = scullseq_proc_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = seq_release
};
	

/*
 * Actually create (and remove) the /proc file(s).
 */

static void scull_create_proc(void) {
	proc_create_data("scullmem", 0 /* default mode */,
			NULL /* parent dir */, &scullmem_proc_ops,
			NULL /* client data */);
	proc_create("scullseq", 0, NULL, &scullseq_proc_ops);
}

static void scull_remove_proc(void) {
	/* no problem if it was not registered */
	remove_proc_entry("scullmem", NULL /* parent dir */);
	remove_proc_entry("scullseq", NULL);
}

#endif /* SCULL_DEBUG */

/*
 * Open: to open the device is to initialize it for the remaining methods.
 */
int scull_open(struct inode *inode, struct file *filp) {

   /* the device this function is handling (one of the scull_devices) */
	struct scull_dev *dev;

	/* we need the scull_dev object (dev), but the required prototpye
      for the open method is that it receives a pointer to an inode.
      now an inode contains a struct cdev (the field is called
      i_cdev) and we can use this field with the container_of macro
      to obtain the scull_dev object (since scull_dev also contains
      a cdev object.
    */
	dev = container_of(inode->i_cdev, struct scull_dev, cdev);

	/* so that we don't need to use the container_of() macro repeatedly,
		we save the handle to dev in the file's private_data for other methods.
	 */
	filp->private_data = dev;

	/* now trim to 0 the length of the device if open was write-only */
	if ( (filp->f_flags & O_ACCMODE) == O_WRONLY) {

		/* grab the semaphore, so the call to trim() is atomic */
		if (down_interruptible(&dev->sem)) return -ERESTARTSYS;

		scull_trim(dev);

		/* release the semaphore */
		up(&dev->sem);
	}

	return 0;
}

/*
 * Release: release is the opposite of open, so it deallocates any
 *          memory allocated by scull_open and shuts down the device.
 *          since open didn't allocate anything and our device exists
 *          only in memory, there are no actions to take here.
 */
int scull_release(struct inode *inode, struct file *filp) {
	return 0;
}

/*
 * Follow the list--used by scull_read() and scull_write() to find the
 *                  item in the list that corresponds to the file's
 *                  "file position" pointer.  If the file position is
 *                  beyond the end of the file, then items are added
 *                  to extend the file, this is typical file-oriented
 *                  behavior.
 */
struct scull_qset *scull_follow(struct scull_dev *dev, int n) {

	/* get the first item in the list */
	struct scull_qset *qs = dev->data;

   /* allocate first qset explicitly if need be */
	if (qs == NULL) {

		/* allocate and also set the data field to rference this item */
		qs = dev->data = kmalloc(sizeof(struct scull_qset), GFP_KERNEL);

		/* if the allocation fails, return NULL */
		if (qs == NULL) return NULL;

		/* initialize the qset to all zeros */
		memset(qs, 0, sizeof(struct scull_qset));
	}

	/* follow the list to item n */
	while (n--) {

		/* if there is no next item in the list, allocate one */
		if (qs->next == NULL) {

			/* allocate and return NULL on failure as before */
			qs->next = kmalloc(sizeof(struct scull_qset), GFP_KERNEL);
			if (qs->next == NULL) return NULL;

			/* or zero the memory */
			memset(qs->next, 0, sizeof(struct scull_qset));
		}

		/* advance to next item */
		qs = qs->next;

		/* curious -- continue; */
	}

	/* return the qset associated with the n-th item */
	return qs;
}

/*
 * Read: implements the read action on the device by reading count
 *       bytes into buf beginning at file position f_pos from the file 
 *       referenced by filp.  The attribute "__user" is a gcc extension
 *       that indicates that buf originates from user space memory and
 *       should therefore not be trusted.
 */
ssize_t scull_read(struct file *filp, char __user *buf, size_t count,
                    loff_t *f_pos) {

	struct scull_dev  *dev  = filp->private_data; 
	struct scull_qset *dptr;

	int     quantum  = dev->quantum;
	int     qset     = dev->qset;
	int     itemsize = quantum * qset; /* number of bytes in the list item    */
	int     item, s_pos, q_pos, rest;  /* other variables for calculating pos */
	ssize_t retval   = 0;

	/* acquire the semaphore */
	if (down_interruptible(&dev->sem)) return -ERESTARTSYS;

	/* if the read position is beyond the end of the file, then goto exit
    * note that we can't simply return, because we are holding the
    * semaphore, "goto out" provides a single exit point that allows for
    * releasing the semaphore.
    */
	if (*f_pos >= dev->size) goto out;

	if (*f_pos + count > dev->size) {
		count = dev->size - *f_pos;
	}

	/* find listitem, qset index, and offset in the quantum */
	item = (long)*f_pos / itemsize;
	rest = (long)*f_pos % itemsize;
	s_pos = rest / quantum; q_pos = rest % quantum;

	/* follow the list up to the right position (defined elsewhere) */
	dptr = scull_follow(dev, item);

	if (dptr == NULL || !dptr->data || ! dptr->data[s_pos])
		goto out; /* don't fill holes */

	/* read only up to the end of this quantum */
	if (count > quantum - q_pos) {
		count = quantum - q_pos;
	}

	/* this is where the actual "read" occurs, when we copy from the
    * in-memory data into the user-supplied buffer.  This copy is
    * handled by the copy_to_user() function, which handles the
    * transfer of data from kernel space data structures to user space
    * data structures.
    */
	if (copy_to_user(buf, dptr->data[s_pos] + q_pos, count)) {
		retval = -EFAULT;
		goto out;
	}

	/* on successful copy, update the file position and return the number
    * of bytes read.
    */
	*f_pos += count;
	retval = count;

	/* release the semaphore and return */
  out:
	up(&dev->sem);
	return retval;
}

ssize_t scull_write(struct file *filp, const char __user *buf, size_t count,
                     loff_t *f_pos) {

	struct scull_dev  *dev  = filp->private_data;
	struct scull_qset *dptr;

	int     quantum  = dev->quantum;
	int     qset     = dev->qset;
	int     itemsize = quantum * qset;
	int     item, s_pos, q_pos, rest;
	ssize_t retval   = -ENOMEM;         /* value used in "goto out" statements */

	/* acquire the semaphore */
	if (down_interruptible(&dev->sem)) return -ERESTARTSYS;

	/* find list item, qset index and offset in the quantum */
	item = (long)*f_pos / itemsize;
	rest = (long)*f_pos % itemsize;
	s_pos = rest / quantum; q_pos = rest % quantum;

	/* follow the list up to the right position */
	dptr = scull_follow(dev, item);

	/* if there is no item at this file position, then exit */
	if (dptr == NULL) goto out;

	/* if there is memory for writing data in this item, allocate some */
	if (!dptr->data) {

		/* exit if the allocation fails */
		dptr->data = kmalloc(qset * sizeof(char *), GFP_KERNEL);
		if (!dptr->data) goto out;

		/* otherwise zero the memory */
		memset(dptr->data, 0, qset * sizeof(char *));
	}

	/* if there is no memory for writing data in this quantum, allocate some */
	if (!dptr->data[s_pos]) {

		/* exit if the allocation fails */
		dptr->data[s_pos] = kmalloc(quantum, GFP_KERNEL);
		if (!dptr->data[s_pos]) goto out;
	}

	/* adjust count to write only up to the end of this quantum */
	if (count > quantum - q_pos) {
		count = quantum - q_pos;
	}

	/* this is where the actual "write" occurs, when we copy from the
    * the user-supplied buffer into the in-memory data area.  This copy is
    * handled by the copy_from_user() function, which handles the
    * transfer of data from user space data structures to kernel space
    * data structures.
    */
	if (copy_from_user(dptr->data[s_pos]+q_pos, buf, count)) {
		retval = -EFAULT;
		goto out;
	}

	/* on successful transfer, update the file position and record the
    * number of bytes written.
	 */
	*f_pos += count;
	retval = count;

   /* update the size of the file */
	if (dev->size < *f_pos) {
		dev->size = *f_pos;
	}

	/* release the semaphore and return */
  out:
	up(&dev->sem);
	return retval;
}

/*
 * Ioctl:  the ioctl() call is the "catchall" device function; its purpose
 *         is to provide device control through a single standard function
 *         call.  It accomplishes this via a command value and an arg
 *         parameter which indicates which action to take.
 */
long scull_ioctl(struct file *filp, unsigned int cmd, unsigned long arg) {

	int err    = 0, tmp;
	int retval = 0;
    
	/*
	 * extract the type and number bitfields, and don't decode
	 * wrong cmds: return ENOTTY (inappropriate ioctl) before access_ok()
	 */
	if (_IOC_TYPE(cmd) != SCULL_IOC_MAGIC) return -ENOTTY;
	if (_IOC_NR(cmd)   >  SCULL_IOC_MAXNR) return -ENOTTY;

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

 	  /* Reset: values are compile-time defines */
	  case SCULL_IOCRESET:
		scull_quantum = SCULL_QUANTUM;
		scull_qset    = SCULL_QSET;
		break;
        
 	  /* Set: arg points to the value */
	  case SCULL_IOCSQUANTUM:
		  if (! capable (CAP_SYS_ADMIN))
			  return -EPERM;
		  retval = __get_user(scull_quantum, (int __user *)arg);
		  break;

 	  /* Tell: arg is the value */
	  case SCULL_IOCTQUANTUM:
		  if (! capable (CAP_SYS_ADMIN))
			  return -EPERM;
		  scull_quantum = arg;
		  break;

 	  /* Get: arg is pointer to result */
	  case SCULL_IOCGQUANTUM:
		  retval = __put_user(scull_quantum, (int __user *)arg);
		  break;

     /* Query: return it (it's positive) */
	  case SCULL_IOCQQUANTUM:
		  return scull_quantum;

     /* eXchange: use arg as pointer; requires user to have root privilege */
	  case SCULL_IOCXQUANTUM:
		  if (! capable (CAP_SYS_ADMIN))
			  return -EPERM;
		  tmp = scull_quantum;
		  retval = __get_user(scull_quantum, (int __user *)arg);
		  if (retval == 0)
			  retval = __put_user(tmp, (int __user *)arg);
		  break;

     /* sHift: like Tell + Query; also requires root access */
	  case SCULL_IOCHQUANTUM:
		  if (! capable (CAP_SYS_ADMIN))
			  return -EPERM;
		  tmp = scull_quantum;
		  scull_quantum = arg;
		  return tmp;
        
	  case SCULL_IOCSQSET:
		  if (! capable (CAP_SYS_ADMIN))
			  return -EPERM;
		  retval = __get_user(scull_qset, (int __user *)arg);
		  break;

	  case SCULL_IOCTQSET:
		  if (! capable (CAP_SYS_ADMIN))
			  return -EPERM;
		  scull_qset = arg;
		  break;

	  case SCULL_IOCGQSET:
		  retval = __put_user(scull_qset, (int __user *)arg);
		  break;

	  case SCULL_IOCQQSET:
		  return scull_qset;

	  case SCULL_IOCXQSET:
		  if (! capable (CAP_SYS_ADMIN))
			  return -EPERM;
		  tmp = scull_qset;
		  retval = __get_user(scull_qset, (int __user *)arg);
		  if (retval == 0)
			  retval = put_user(tmp, (int __user *)arg);
		  break;

	  case SCULL_IOCHQSET:
		  if (! capable (CAP_SYS_ADMIN))
			  return -EPERM;
		  tmp = scull_qset;
		  scull_qset = arg;
		  return tmp;

     /* redundant, as cmd was checked against MAXNR */
	  default:
		  return -ENOTTY;
	}

	return retval;
}



/*
 * Seek:  the only one of the "extended" operations which scull implements.
 */
loff_t scull_llseek(struct file *filp, loff_t off, int whence) {

	struct scull_dev *dev    = filp->private_data;
	loff_t            newpos;

	/* reset the file position as is standard */
	switch(whence) {
	  case 0: /* SEEK_SET */
		newpos = off;
		break;

	  case 1: /* SEEK_CUR */
		newpos = filp->f_pos + off;
		break;

	  case 2: /* SEEK_END */
		newpos = dev->size + off;
		break;

	  default: /* can't happen */
		return -EINVAL;
	}

	/* file positions can't be negative */
	if (newpos < 0) return -EINVAL;

	/* set the postion and return */
	filp->f_pos = newpos;
	return newpos;
}

/* this assignment is what "binds" the template file operations with those that
 * are implemented herein.
 */
struct file_operations scull_fops = {
	.owner =    THIS_MODULE,
	.llseek =   scull_llseek,
	.read =     scull_read,
	.write =    scull_write,
	.unlocked_ioctl = scull_ioctl,
	.open =     scull_open,
	.release =  scull_release,
};

/*
 * Finally, the module stuff
 */

/*
 * The cleanup function is used to handle initialization failures as well.
 * Thefore, it must be careful to work correctly even if some of the items
 * have not been initialized
 */
void scull_cleanup_module(void) {

	dev_t devno = MKDEV(scull_major, scull_minor);

	/* if the devices were succesfully allocated, then the referencing pointer
    * will be non-NULL.
    */
	if (scull_devices != NULL) {

	   /* Get rid of our char dev entries by first deallocating memory and then
       * deleting them from the kernel */
	   int i;
		for (i = 0; i < scull_nr_devs; i++) {
			scull_trim(scull_devices + i);
			cdev_del(&scull_devices[i].cdev);
		}

		/* free the referencing structures */
		kfree(scull_devices);
	}

#ifdef SCULL_DEBUG /* use proc only if debugging */
	scull_remove_proc();
#endif

	/* cleanup_module is never called if registering failed */
	unregister_chrdev_region(devno, scull_nr_devs);
}


/*
 * Set up the char_dev structure for this device.
 */
static void scull_setup_cdev(struct scull_dev *dev, int index) {
	int err, devno = MKDEV(scull_major, scull_minor + index);
    
	/* cdev_init() and cdev_add() are kernel-required initialization */
	cdev_init(&dev->cdev, &scull_fops);
	dev->cdev.owner = THIS_MODULE;
	dev->cdev.ops   = &scull_fops;
	err             = cdev_add (&dev->cdev, devno, 1);

	/* Fail gracefully if need be */
	if (err) printk(KERN_NOTICE "Error %d adding scull%d", err, index);
}


int scull_init_module(void) {
	int result, i;
	dev_t dev = 0;

   /*
    * Compile-time default for major is zero (dynamically assigned) unless 
    * directed otherwise at load time.  Also get range of minors to work with.
    */
	if (scull_major == 0) {
		result      = alloc_chrdev_region(&dev,scull_minor,scull_nr_devs,"scull");
		scull_major = MAJOR(dev);
	} else {
		dev    = MKDEV(scull_major, scull_minor);
		result = register_chrdev_region(dev, scull_nr_devs, "scull");
	}

	/* report failue to aquire major number */
	if (result < 0) {
		printk(KERN_WARNING "scull: can't get major %d\n", scull_major);
		return result;
	}

   /* 
	 * allocate the devices -- we can't have them static, as the number
	 * can be specified at load time
	 */
	scull_devices = kmalloc(scull_nr_devs*sizeof(struct scull_dev), GFP_KERNEL);

	/* exit if memory allocation fails */
	if (!scull_devices) {
		result = -ENOMEM;
		goto fail;
	}

	/* otherwise, zero the memory */
	memset(scull_devices, 0, scull_nr_devs * sizeof(struct scull_dev));

   /* Initialize each device. */
	for (i = 0; i < scull_nr_devs; i++) {
		scull_devices[i].quantum = scull_quantum;
		scull_devices[i].qset    = scull_qset;
		sema_init(&scull_devices[i].sem, 1);
		scull_setup_cdev(&scull_devices[i], i);
	}

   /* only when debugging */
#ifdef SCULL_DEBUG
	scull_create_proc();
#endif

   /* succeed */
	return 0;

	/* failure, so cleanup is necessary */
  fail:
	scull_cleanup_module();
	return result;
}

/* identify to the kernel the entry points for initialization and release, these
 * functions are called on insmod and rmmod, respectively
 */
module_init(scull_init_module);
module_exit(scull_cleanup_module);
