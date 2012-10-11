/*
 * Copyright 1996-2004 by Hans Reiser, licensing governed by 
 * reiserfsprogs/README
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "util/device.h"
#include "util/print.h"
#include "util/mntent.h"
#include "misc/device.h"
#include "misc/types.h"
#include "misc/misc.h"
#include "reiserfs/print.h"

#include <linux/hdreg.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <mntent.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <utime.h>
#include <assert.h>

#define reiserfs_confirm(force) \
	if (force < 1) {\
	    /* avoid formatting it without being forced */\
	    reiserfs_warning (stderr, "Use -f to force over\n");\
	    return 0;\
	}\
	if (force < 2) {\
	    if (!util_user_confirmed (stderr, "Continue (y/n):", "y\n"))\
		return 0;\
	}\

/* 0 - dma is not supported, scsi or regular file */
/* 1 - xt drive                                   */
/* 2 - ide drive */
static void get_dma_support(util_device_dma_t *dma_info, struct stat *st){
    if (S_ISREG(st->st_mode))
	st->st_rdev = st->st_dev;

    if (IDE_DISK_MAJOR(major(st->st_rdev))) {
        dma_info->support_type = 2;
	return;
    }
    
#ifdef XT_DISK_MAJOR
    if (major(st->st_rdev) == XT_DISK_MAJOR) {
	dma_info->support_type = 1;
	return;
    }
#endif    
    dma_info->support_type = 0;
}

/* 
 * Return values: 
 * 0 - ok;
 * 1 - preparation cannot be done 
 * -1 - preparation failed
 */

int util_device_dma_prep(util_device_dma_t *dma_info) {
    struct stat st;
    char buf[4096];
    dev_t rdev;
    int rem;
    int res;

#ifndef HDIO_GET_DMA
        return -1;
#endif
	
    if (fstat(dma_info->fd, &st))
	misc_die("stat on device failed\n");
   
    get_dma_support(dma_info, &st);
   
    /* dma should be supported */
    if (dma_info->support_type == 0)
	return 1;
    
    if (dma_info->support_type == 1)
	return 0;

    rdev = dma_info->st_rdev = st.st_rdev;

    /* If it is the whole device? no preparation needed then. */
    if ((rem = (minor(rdev) % 64)) == 0)
	return 0;
    
    dma_info->st_rdev -= rem;

    strcpy(buf, "/dev");

    if ((res = misc_dir_walk(buf, misc_device_rdev_match, &dma_info->st_rdev)) <= 0) {
	    dma_info->st_rdev += rem;
	    dma_info->support_type = 1;
	    return res;
    } 
    
    /* Matched block device file found. Open it. */
    dma_info->fd = open(buf, O_RDONLY
#if defined(O_LARGEFILE)
			| O_LARGEFILE
#endif
		       );
    
    return 0;
}

static int is_dma_on (int fd) {
#ifdef HDIO_GET_DMA    
    static long parm;
    if (ioctl(fd, HDIO_GET_DMA, &parm))
	return -1;
    else 
	return parm;
#endif
    return 0;
}


static __u64 dma_speed(int fd, int support_type) {
    static struct hd_driveid id;
    __u64 speed = 0;
            
    if (support_type != 2) 
	return 0;

#ifdef HDIO_OBSOLETE_IDENTITY
    if (!ioctl(fd, HDIO_GET_IDENTITY, &id) || 
	!ioctl(fd, HDIO_OBSOLETE_IDENTITY)) {
#else
    if (!ioctl(fd, HDIO_GET_IDENTITY, &id)) {
#endif
	speed |= (__u64)id.dma_1word &  ~(__u64)0xff;
	speed |= ((__u64)id.dma_mword & ~(__u64)0xff) << 16;
	speed |= ((__u64)id.dma_ultra & ~(__u64)0xff) << 32;
    } else if (errno == -ENOMSG)
	return -1;
    else 
	return -1;
    
    return speed;
}

int util_device_get_dma(util_device_dma_t *dma_info) {
    if ((dma_info->dma = is_dma_on(dma_info->fd)) == -1)
	return -1;
    if ((dma_info->speed = dma_speed(dma_info->fd, dma_info->support_type)) == (__u64)-1) 
	return -1;
    return 0;
}

void util_device_dma_fini(int fd, util_device_dma_t *dma_info) {
    signal(SIGALRM, SIG_IGN);
    if (dma_info->fd && fd != dma_info->fd)
	close(dma_info->fd);
}

FILE * util_file_open (char * filename, char * option) 
{
    FILE * fp = fopen (filename, option);
    if (!fp) {
	reiserfs_warning (stderr, "util_file_open: could not "
			  "open file %s\n", filename);
	return 0;
    }
    reiserfs_warning (stderr, "Temp file opened by fsck: "
		      "\"%s\" .. \n", filename);
    return fp;
}

/* we only can use a file for filesystem or journal if it is either not
   mounted block device or regular file and we are forced to use it */
int util_device_formatable (char * device_name, int force)
{
    mode_t mode;
    dev_t rdev;

    if (util_device_mounted(device_name) > 0) {
	/* device looks mounted */
	reiserfs_warning (stderr, "'%s' looks mounted.", device_name);
	reiserfs_confirm (force);
    }

    mode = misc_device_mode(device_name);
    rdev = misc_device_rdev(device_name);

    if (!S_ISBLK (mode)) {
	/* file is not a block device */
	reiserfs_warning (stderr, "%s is not a block special device\n", device_name);
	reiserfs_confirm (force);
    } else {
	if ((IDE_DISK_MAJOR (major(rdev)) && minor(rdev) % 64 == 0) ||
	    (SCSI_BLK_MAJOR (major(rdev)) && minor(rdev) % 16 == 0)) {
	    /* /dev/hda or similar */
	    reiserfs_warning (stderr, "%s is entire device, not just one partition!\n",
		    device_name);
	    reiserfs_confirm (force);
	}
    }

    return 1;
}

int util_root_mounted(char *device) {
	struct stat rootst, devst;
	
	assert(device != NULL);

	if (stat("/", &rootst) != 0) 
		return -1;

	if (stat(device, &devst) != 0)
		return -1;

	if (!S_ISBLK(devst.st_mode) || 
	    devst.st_rdev != rootst.st_dev)
		return 0;

	return 1;
}

int util_device_mounted(char *device) {
	struct mntent *mnt;
	
	/* Check for the "/" first to avoid any possible problem with 
	   reflecting the root fs info in mtab files. */
	if (util_root_mounted(device) == 1) {
		return util_file_ro("/") ? MF_RO : MF_RW;
	}
	
	/* Lookup the mount entry. */
	if ((mnt = util_mntent(device)) == NULL) {
		return MF_NOT_MOUNTED;
	} else if (mnt == INVAL_PTR) {
		return 0;
	}

	return hasmntopt(mnt, MNTOPT_RO) ? MF_RO : MF_RW;
}

int util_file_ro(char *file) {
	if (utime(file, 0) == -1) {
		if (errno == EROFS)
			return 1;
	}

	return 0;
}

