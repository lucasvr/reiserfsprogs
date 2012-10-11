/*
 * Copyright 1996-2004 by Hans Reiser, licensing governed by 
 * reiserfsprogs/README
 */

/* Lookup the @file in the @mntfile. @file is mntent.mnt_fsname if @fsname 
   is set; mntent.mnt_dir otherwise. Return the mnt entry from the @mntfile.
   
   Warning: if the root fs is mounted RO, the content of /etc/mtab may be 
   not correct. */


#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "misc/types.h"
#include "util/device.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/vfs.h>
#include <mntent.h>
#include <assert.h>
#include <stdio.h>

static struct mntent *util_mntent_lookup(char *mntfile, 
					 char *file, 
					 int path) 
{
	struct mntent *mnt;
	int name_match = 0;
	struct stat st;
	dev_t rdev = 0;
	dev_t dev = 0;
	ino_t ino = 0;
	char *name;
	FILE *fp;
	
	assert(mntfile != NULL);
	assert(file != NULL);

	if (stat(file, &st) == 0) {
		/* Devices is stated. */
		if (S_ISBLK(st.st_mode)) {
			rdev = st.st_rdev;
		} else {
			dev = st.st_dev;
			ino = st.st_ino;
		}
	}

	if ((fp = setmntent(mntfile, "r")) == NULL)
		return INVAL_PTR;

	while ((mnt = getmntent(fp)) != NULL) {
		/* Check if names match. */
		name = path ? mnt->mnt_dir : mnt->mnt_fsname;
		
		if (strcmp(file, name) == 0)
			name_match = 1;

		if (stat(name, &st))
			continue;
		
		/* If names do not match, check if stats match. */
		if (!name_match) {
			if (rdev && S_ISBLK(st.st_mode)) {
				if (rdev != st.st_rdev)
					continue;
			} else if (dev && !S_ISBLK(st.st_mode)) {
				if (dev != st.st_dev ||
				    ino != st.st_ino)
					continue;
			} else {
				continue;
			}
		}

		/* If not path and not block device do not check anything more. */
		if (!path && !rdev) 
			break;

		if (path) {
			/* Either names or stats match. Make sure the st_dev of 
			   the path is same as @mnt_fsname device rdev. */
			if (stat(mnt->mnt_fsname, &st) == 0 && 
			    dev == st.st_rdev)
				break;
		} else {
			/* Either names or stats match. Make sure the st_dev of 
			   the mount entry is same as the given device rdev. */
			if (stat(mnt->mnt_dir, &st) == 0 && 
			    rdev == st.st_dev)
				break;
		}
	}

	endmntent (fp);
        return mnt;
}

struct mntent *util_mntent(char *device) {
	int proc = 0, path = 0, root = 0;
	
	struct mntent *mnt;
	struct statfs stfs;

	assert(device != NULL);
	
	/* Check if the root. */
	if (util_root_mounted(device) == 1)
		root = 1;
	
#ifdef __linux__
	/* Check if /proc is procfs. */
	if (statfs("/proc", &stfs) == 0 && stfs.f_type == 0x9fa0) {
		proc = 1;
		
		if (root) {
			/* Lookup the "/" entry in /proc/mounts. Special 
			   case as root entry can present as:
				rootfs / rootfs rw 0 0
			   Look up the mount point in this case. */
			mnt = util_mntent_lookup("/proc/mounts", "/", 1);
		} else {
			/* Lookup the @device /proc/mounts */
			mnt = util_mntent_lookup("/proc/mounts", device, 0);
		}
		
		if (mnt == INVAL_PTR) 
			proc = 0;
		else if (mnt)
			return mnt;
	}
#endif /* __linux__ */

#if defined(MOUNTED) || defined(_PATH_MOUNTED)

#ifndef MOUNTED
    #define MOUNTED _PATH_MOUNTED
#endif
	/* Check in MOUNTED (/etc/mtab) if RW. */
	if (!util_file_ro(MOUNTED)) {
		path = 1;

		if (root) {
			mnt = util_mntent_lookup(MOUNTED, "/", 1);
		} else {
			mnt = util_mntent_lookup(MOUNTED, device, 0);
		}

		if (mnt == INVAL_PTR) 
			path = 0;
		else if (mnt)
			return mnt;
	}
#endif /* defined(MOUNTED) || defined(_PATH_MOUNTED) */
	
	/* If has not been checked in neither /proc/mounts nor /etc/mtab (or 
	   errors have occured), return INVAL_PTR, NULL otherwise. */
	return (!proc && !path) ? INVAL_PTR : NULL;
}

