/* ---------------------------------------------------- *
 * hrandom.c -- Linux kernel module for HAVEGE          *
 * ---------------------------------------------------- *
 * Copyright (c) 2006 - O. Rochecouste, A. Seznec       *
 *                                                      *
 * based on HAVEGE 2.0 source code by: A. Seznec        *
 *                                                      *
 * This library is free software; you can redistribute  *
 * it and/or  * modify it under the terms of the GNU    *
 * Lesser General Public License as published by the    *
 * Free Software Foundation; either version 2.1 of the  *
 * License, or (at your option) any later version.      *
 *                                                      *
 * This library is distributed in the hope that it will *
 * be useful, but WITHOUT ANY WARRANTY; without even the*
 * implied warranty of MERCHANTABILITY or FITNESS FOR A *
 * PARTICULAR PURPOSE. See the GNU Lesser General Public*
 * License for more details.                            *
 *                                                      *
 * You should have received a copy of the GNU Lesser    *
 * General Public License along with this library; if   *
 * not, write to the Free Software Foundation, Inc., 51 *
 * Franklin Street, Fifth Floor, Boston, MA  02110-1301 *
 * USA                                                  *
 *                                                      *
 * contact information: orocheco [at] irisa [dot] fr    *
 * ==================================================== */

#include <linux/version.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/sched.h>

#include <linux/kobject.h>
//#include <linux/devfs_fs_kernel.h>
#include <linux/device.h>

#include <asm/uaccess.h>
#include <linux/semaphore.h>

//#include "config.h"

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,13))
#  include <linux/moduleparam.h>
#  include <linux/cdev.h>
#else
#  error "this module only works with kernels 2.6.13 and above"
#endif

/* ----------- *
 * Misc. infos *
 * ----------- */

#define DRIVER_AUTH "Olivier Rochecouste <orocheco@irisa.fr>"
#define DRIVER_DESC "Loadable kernel module for HAVEGE"
#define DRIVER_NAME "hrandom"
#define DRIVER_VERS "1.00"

/* the following interfaces are provided:           *
 *  -/dev/hrandom0 that uses havege_ndrand()        *
 *  -/dev/hrandom1 that uses havege_crypto_ndrand() */

#define DRIVER_NBDEV 2
#define DRIVER_MINOR 0

/* ----------------- *
 * driver parameters *
 * ----------------- */

/* 08 steps are initiated for havege_ndrand()                *
 * 32 steps are initiated for havege_crypto_ndrand()         *
 * 02 steps are sufficient in practice, from our experiments */

static unsigned int hrandom_min_initrand = 32;
module_param_named(min_initrand, hrandom_min_initrand, uint, 0);
MODULE_PARM_DESC(hrandom_min_initrand, "no description available");

static unsigned int hrandom_nb_stephiding = 32;
module_param_named(nb_stephiding, hrandom_nb_stephiding, uint, 0);
MODULE_PARM_DESC(hrandom_nb_stephiding, "no description available");

/* ------------ *
 * driver stuff *
 * ------------ */

static dev_t         hrandom_dev;   /* device number */
static struct class *hrandom_class; /* device class */

static ssize_t hrandom_open    (struct inode *inode, struct file *filp);
static ssize_t hrandom_release (struct inode *inode, struct file *filp);
static ssize_t hrandom_read_nd (struct file *filp, char __user *buf,
				size_t length, loff_t *f_pos);
static ssize_t hrandom_read_cr (struct file *filp, char __user *buf,
				size_t length, loff_t *f_pos);

static struct file_operations hrandom_fops = {
  .read    = hrandom_read_nd,
  .open    = hrandom_open,
  .release = hrandom_release
};

static struct file_operations crandom_fops = {
  .read    = hrandom_read_cr,
  .open    = hrandom_open,
  .release = hrandom_release
};

/* ------------ *
 * havege stuff *
 * ------------ */

#define HRANDOM_USEKMA 0 /* use kmalloc for the walk table */
#define HRANDOM_USEVMA 1 /* use vmalloc for the walk table */
#define HRANDOM_NDRAND 0 /* RNG = havege_ndrand()          */
#define HRANDOM_CRRAND 1 /* RNG = havege_ crypto_ndrand()  */

#define HRANDOM_BUFSIZ 256 /* transfer chunk size */

#define HRANDOM_NDSIZE 0x100000 /* 1M   (4MB int) */
#define HRANDOM_CRSIZE 0x040000 /* 256k (1MB int) */
#define HRANDOM_ANDPT  0x000fff // P4 only!

#define HRANDOM_INLINE // inline

#ifndef ADDR64
#  define HRANDOM_ALIGN(addr) \
     (((int) addr) & 0xfffff000)
#else
#  define HRANDOM_ALIGN(addr) \
     (((long long) addr) & 0xfffffffffffff000)
#endif

/* ------------------------------ *
 * access to the hardware counter *
 * ------------------------------ */

#ifdef HAVE_ISA_X86
#define HARDCLOCK(low) \
  __asm__ __volatile__ ("rdtsc":"=a"(low) : : "edx")
#endif

#ifdef HAVE_ISA_SPARC
#define HARDCLOCK(low) \
  __asm__ __volatile__ ("rd %%tick, %0":"=r"(low):"r"(low))
#endif

#ifdef HAVE_ISA_PPC
#define HARDCLOCK(low) \
  __asm__ __volatile__ ("mftb %0":"=r"(low)) /* eq. to mftb %0, 268 */
#endif

#ifdef HAVE_ISA_IA64
#define HARDCLOCK(low) \
  __asm__ __volatile__ ("mov %0=ar.itc" : "=r"(low))
#endif

#define HARDCLOCK(low) \
 __asm__ __volatile__ ("MCR p15, 0, %0, c9, c12, 0\t\n" :: "r"(low));

/* ----------------- */

typedef struct hrandom_state
{
  int ndpt;
  int *pool;            /* entropy pool */
  int poolsize;
  int *walk;            /* walk table */
  int *walkp;
  int PT, pt, PT2, pt2;
  int rdtsc;            /* hardtick value */

  struct semaphore sem;
  struct cdev cdev;
  int preempt_freq;     /* preemption frequency */
} hrandom_state_t;

static hrandom_state_t *hrandom_nstate; /* havege_ndrand() */
static hrandom_state_t *hrandom_cstate; /* havege_crypto_ndrand() */
static unsigned int hrandom_alloc_walk;

static void havege_collect_ndrand (hrandom_state_t *state);
static void havege_init           (hrandom_state_t *state);
static int  havege_ndrand         (void);
static int  havege_crypto_ndrand  (void);

/* --------------- *
 * havege 2.0 core *
 * --------------- */

static void
havege_collect_ndrand (hrandom_state_t *state)
{
  int i, inter, PTtest;
  int *Pt0, *Pt1, *Pt2, *Pt3;

  int *result = state->pool;
  int size    = state->poolsize;

#ifdef HRANDOM_FORCE_PREEMPT
  int iprev = 0;
#endif
  i = 0;

  while (i < size) {

#ifdef HRANDOM_FORCE_PREEMPT
    /* force preemption at given intervals */
    if((i - iprev) >= state->preempt_freq) {
      iprev = i;
      schedule();
    }
#endif /* HRANDOM_FORCE_PREEMPT */

    if(need_resched()) {
      schedule();
    }
#      include "loopbody.h"
  }
}

static void
havege_init (hrandom_state_t *state)
{
  int i, max;
  int size = state->poolsize;

  state->walkp = (int*)
    HRANDOM_ALIGN(&state->walk[4096]);

  max = (hrandom_min_initrand*HRANDOM_CRSIZE)/size;
  for (i = 0; i < max; i++) {
    havege_collect_ndrand (state);
  }
}

static HRANDOM_INLINE int
havege_ndrand (void)
{
  hrandom_state_t *state = hrandom_nstate;
  int poolsize = state->poolsize;

  if (state->ndpt >= poolsize) {
    if (state->ndpt >= 2 * poolsize) {
      havege_init (state);
    } else {
      havege_collect_ndrand (state);
    }
    state->ndpt = 0;
  }

  return state->pool[state->ndpt++];
}

static HRANDOM_INLINE int
havege_crypto_ndrand (void)
{
  int i;

  hrandom_state_t *state = hrandom_cstate;
  int poolsize = state->poolsize;

  if (state->ndpt >= poolsize) {
    if (state->ndpt >= 2 * poolsize) {
      havege_init (state);
    } else {
      for (i = 0; i < hrandom_nb_stephiding; i++) {
	havege_collect_ndrand (state);
      }
    }
    state->ndpt = 0;
  }

  return state->pool[state->ndpt++];
}

/* ---------------------------------------- *
 * allocate and initialize device resources *
 * ---------------------------------------- */

static hrandom_state_t*
hrandom_setup (int poolsize)
{
  hrandom_state_t *state;
  int walksize;

  state = kmalloc(sizeof(*state), GFP_KERNEL);
  if (!state) {
    printk(KERN_ERR "%s: failed allocating %s state.\n",
	   __FUNCTION__, DRIVER_NAME);
    return NULL;
  }

  state->ndpt = HRANDOM_NDSIZE * 2;
  state->PT   = 0;
  state->pt   = 0;
  state->PT2  = 0;
  state->pt2  = 0;

  /* vmalloc is used as we need to allocate up to 4MB for the    *
   * entropy pool. An alternative method would be to use the     *
   * "scatter & gather" approach, as discussed in: Linux Device  *
   * Drivers - A. Rubini, but this is left for the next release. */

  state->pool = vmalloc((poolsize+16384)*sizeof(int));
  if (!(state->pool)) {
    printk(KERN_ERR "%s: failed allocating entropy pool.\n",
	   __FUNCTION__);
    goto __no_memory;
  }

  state->poolsize = poolsize;

  /* kmalloc might fail if the walk table is larger than 128KB. *
   * In that case, we should fallback on using vmalloc or on    *
   * the "scatter & gather" approach, mentionned above.         *
   * HRANDOM_ANDPT is computed as follows:                      *
   * HRANDOM_ANDPT + 1 = 2 * dcache_size.                       */

  walksize = (HRANDOM_ANDPT + 4097) * sizeof(int);

#define CACHE(x)                                             \
  if (walksize <=x) { hrandom_alloc_walk = HRANDOM_USEKMA; } \
  else              { hrandom_alloc_walk = HRANDOM_USEVMA; }
#include <linux/kmalloc_sizes.h>
#undef CACHE

  if (hrandom_alloc_walk == HRANDOM_USEKMA) {
    state->walk = kmalloc(walksize, GFP_KERNEL);
  } else {
    state->walk = vmalloc(walksize);
  }

  if (!(state->walk)) {
    printk(KERN_ERR "%s: failed allocating walk table.\n",
	   __FUNCTION__);
    /* sometimes, kmalloc failed when memory is fragmented, should
     * we fallback on using vmalloc then? */
    goto __no_memory;
  }

  state->walkp = state->walk;

  /* initialize the mutex */
  init_MUTEX(&state->sem);
  if (&state->sem == NULL) {
    printk(KERN_ERR "%s: failed initializing mutex.\n",
	   __FUNCTION__);
    goto __no_memory;
  }

  /* preemption frequency */
  state->preempt_freq = poolsize >> 4;

  /* hrandom_struct successfully created */
  return state;

  /* if we failed allocating one of the above resources,    *
   * free the allocated resources and return a NULL pointer */
 __no_memory:
  if (hrandom_alloc_walk == HRANDOM_USEKMA) {
    kfree(state->walk);
  } else {
    vfree(state->walk);
  }
  vfree(state->pool);
  kfree(state);
  printk(KERN_ERR "%s: failed allocation %s basic resources.\n",
	 __FUNCTION__, DRIVER_NAME);
  return NULL;
}

/* --------------------------- *
 * cleanup allocated resources *
 * --------------------------- */

static void
hrandom_state_cleanup (hrandom_state_t *state)
{
  if (hrandom_alloc_walk == HRANDOM_USEKMA) {
    kfree(state->walk);
  } else {
    vfree(state->walk);
  }

  vfree(state->pool);
  kfree(state);
}

static void
hrandom_cleanup (void)
{
  /* unregister devices */
  cdev_del(&hrandom_nstate->cdev);
  cdev_del(&hrandom_cstate->cdev);

  hrandom_state_cleanup (hrandom_nstate);
  hrandom_state_cleanup (hrandom_cstate);

  printk(KERN_NOTICE "%s: successfully cleaned up.\n",
	 DRIVER_NAME);
}

/* ------------------------------------ *
 * register device (2.6.x kernels only) *
 * ------------------------------------ */

static int
hrandom_register (hrandom_state_t *state, int index)
{
  int error;

  cdev_init(&state->cdev, &hrandom_fops);
  state->cdev.owner = THIS_MODULE;
  state->cdev.ops   = &hrandom_fops;

  error = cdev_add(&state->cdev, hrandom_dev+index, 1);
  if (error) {
    printk (KERN_ERR "%s: failed to register %s%d\n",
	    __FUNCTION__, DRIVER_NAME, index);
    return error;
  }

  /* create class device */
  device_create (hrandom_class, NULL, hrandom_dev+index,
		       NULL, "%s%d", DRIVER_NAME, index);
  kobject_set_name(&state->cdev.kobj, "hrandom%d", index);

  return 0;
}

static ssize_t
hrandom_open (struct inode *inode, struct file *filp)
{
  /* enforce read-only access */
  if(!(filp->f_mode & FMODE_READ))
    return -EINVAL;

  if(filp->f_mode & FMODE_WRITE)
    return -EINVAL;

  switch (iminor(inode)) {
  case HRANDOM_NDRAND:
    filp->f_op = &hrandom_fops;
    break;
  case HRANDOM_CRRAND:
    filp->f_op = &crandom_fops;
    break;
  default:
    return -ENODEV;
  }

  /* try_module_get(THIS_MODULE); */

  return 0;
}


/* DATA transfer is performed in chunks of: *
 * HRANDOM_BUFSIZ x sizeof(int) bytes.      */

static ssize_t
hrandom_read_nd (struct file *filp, char __user *buf,
		 size_t count, loff_t *f_pos)
{
  int i, min;
  int localbuf[HRANDOM_BUFSIZ];
  int ret;

  ret = count;

  if (down_interruptible(&hrandom_nstate->sem))
    return -ERESTARTSYS;

  while (count > 0) {
    min = min_t (int, HRANDOM_BUFSIZ, count/sizeof(int));

    for (i = 0; i < min; i++) {
      localbuf[i] = havege_ndrand();
    }

    if(copy_to_user((char*)buf, localbuf, min*sizeof(int))) {
      ret = -EFAULT; /* either all or nothing */
      goto __out;
    }

    buf   += min*sizeof(int);
    count -= min*sizeof(int);
  }

  f_pos += ret;

 __out:
  up(&hrandom_nstate->sem);

  return ret;
}

static ssize_t
hrandom_read_cr (struct file *filp, char __user *buf,
		 size_t count, loff_t *f_pos)
{
  int i, min;
  int localbuf[HRANDOM_BUFSIZ];
  int ret;

  ret = count;

  if (down_interruptible(&hrandom_cstate->sem))
    return -ERESTARTSYS;

  while (count > 0) {
    min = min_t (int, HRANDOM_BUFSIZ, count/sizeof(int));

    for (i = 0; i < min; i++) {
      localbuf[i] = havege_crypto_ndrand();
    }

    if(copy_to_user((char*)buf, localbuf, min*sizeof(int))) {
      ret = -EFAULT; /* either all or nothing */
      goto __out;
    }

    count -= min*sizeof(int);
    buf   += min*sizeof(int);
  }

  f_pos += ret;

 __out:
  up(&hrandom_cstate->sem);

  return ret;
}


static ssize_t
hrandom_release (struct inode *inode, struct file *file)
{
  /* module_put(THIS_MODULE); */
  return 0;
}

/* --------------------- *
 * device initialization *
 * --------------------- */

static int __init
hrandom_init (void)
{
  int res;

  /* dynamic allocation of major number: kernel 2.6.x only */
  res = alloc_chrdev_region (&hrandom_dev, DRIVER_MINOR,
			     DRIVER_NBDEV, DRIVER_NAME);
  if (res < 0) {
    printk(KERN_ERR "%s: failed to register %s\n",
	   __FUNCTION__,DRIVER_NAME);
    return res;
  }


  /* hrandom class */
  hrandom_class = class_create(THIS_MODULE, DRIVER_NAME);
  if (IS_ERR (hrandom_class)) {
    printk (KERN_ERR "%s: cannot create hrandom_class\n",
	    __FUNCTION__);
    return PTR_ERR(hrandom_class);
  }

  /* allocate resources */
  hrandom_nstate = hrandom_setup (HRANDOM_NDSIZE);
  if (!hrandom_nstate) {
    printk(KERN_ERR "%s: cannot allocate hrandom_nstate\n",
	   __FUNCTION__);
    return -ENOMEM;
  }

  hrandom_cstate = hrandom_setup (HRANDOM_CRSIZE);
  if (!hrandom_cstate) {
    /* free allocated resources */
    hrandom_state_cleanup (hrandom_nstate);
    printk(KERN_ERR "%s: cannot allocate hrandom_cstate\n",
	   __FUNCTION__);
    return -ENOMEM;
  }

  /* register devices upon success */
  hrandom_register(hrandom_nstate, 0); /* hrandom0 */
  hrandom_register(hrandom_cstate, 1); /* hrandom1 */

#ifdef HRANDOM_FORCE_PREEMPT
  printk(KERN_NOTICE "%s: force preemption\n", 
	 DRIVER_NAME);
#endif

  return 0;
}

/* ----------- *
 * exit module *
 * ----------- */

static void __exit
hrandom_exit(void)
{
  /* cleanup allocated resources */
  hrandom_cleanup();

  /* free device number */
  unregister_chrdev_region(hrandom_dev, DRIVER_NBDEV);

  /* remove class entries */
  device_destroy(hrandom_class, hrandom_dev);

  /* remove class entries */
  device_destroy(hrandom_class, hrandom_dev+1);

  /* destroy hrandom class */
  class_destroy(hrandom_class);

  printk(KERN_NOTICE "%s: %s has been removed from kernel.\n",
	 __FUNCTION__, DRIVER_NAME);
}

module_init(hrandom_init);
module_exit(hrandom_exit);

/* ---------------------------------- *
 * licensing and module documentation *
 * ---------------------------------- */

MODULE_LICENSE("GPL");
MODULE_AUTHOR(DRIVER_AUTH);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_VERSION(DRIVER_VERS);
