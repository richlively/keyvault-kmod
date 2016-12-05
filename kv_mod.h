/*
 * kv_mod.h -- definitions for the char module
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
 * $Id: kv_mod.h,v 1.15 2004/11/04 17:51:18 rubini Exp $
 * 
 * Modified by Rich Lively and Tim Froeberg 11/19/16
 */

#ifndef _KV_MOD_H_
#define _KV_MOD_H_

#include "key_vault.h" /* key vault data structure */
#include <linux/ioctl.h> /* needed for the _IOW etc stuff used later */

/*
 * Macros to help debugging
 */
#undef PDEBUG             /* undef it, just in case */
#ifdef KV_MOD_DEBUG
#  ifdef __KERNEL__
     /* This one if debugging is on, and kernel space */
#    define PDEBUG(fmt, args...) printk( KERN_DEBUG "kv_mod: " fmt, ## args)
#  else
     /* This one for user space */
#    define PDEBUG(fmt, args...) fprintf(stderr, fmt, ## args)
#  endif
#else
#  define PDEBUG(fmt, args...) /* not debugging: nothing */
#endif

#undef PDEBUGG
#define PDEBUGG(fmt, args...) /* nothing: it's a placeholder */

#ifndef KV_MOD_MAJOR
#define KV_MOD_MAJOR 0   /* dynamic major by default */
#endif

#ifndef KV_MOD_NR_DEVS
#define KV_MOD_NR_DEVS 1    /* kv_mod0 through kv_mod3 */
#endif

struct kv_mod_dev {
	struct key_vault   *data;      /* Pointer to first key vault     */
	struct semaphore    sem;       /* mutual exclusion semaphore       */
	struct cdev         cdev;	    /* Char device structure	   	    */
};

/*
 * Split minors in two parts
 */
#define TYPE(minor)	(((minor) >> 4) & 0xf)	/* high nibble */
#define NUM(minor)	((minor) & 0xf)		   /* low  nibble */

/*
 * The different configurable parameters
 */
extern int kv_mod_major;
extern int kv_mod_nr_devs;

/*
 * Prototypes for shared functions
 */
int     kv_mod_trim  (struct kv_mod_dev *dev);
ssize_t kv_mod_read  (struct file *filp, char __user *buf, size_t count,
                     loff_t *f_pos);
ssize_t kv_mod_write (struct file *filp, const char __user *buf, size_t count,
                     loff_t *f_pos);
loff_t  kv_mod_llseek(struct file *filp, loff_t off, int whence);
long    kv_mod_ioctl (struct file *filp, unsigned int cmd, unsigned long arg);


/*
 * Ioctl definitions
 */

/* Use 'r' as magic number */
#define KV_MOD_IOC_MAGIC  'r'
#define KV_MOD_IOCRESET    _IO(KV_MOD_IOC_MAGIC,     0)

/*
 * S means "Set"       through a ptr,
 * T means "Tell"      directly with the argument value
 * G means "Get":      reply by setting through a pointer
 * Q means "Query":    response is on the return value
 * X means "eXchange": switch G and S atomically
 * H means "sHift":    switch T and Q atomically
 */
#define KV_MOD_IOCSKEY _IOW (KV_MOD_IOC_MAGIC,   1, char)
#define KV_MOD_IOC_MAXNR 1

#endif /* _KV_MOD_H_ */
