/*
 * Copyright 2000-2004 by Hans Reiser, licensing governed by 
 * reiserfsprogs/README
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "resize.h"
#include "util/mntent.h"

#include <errno.h>
#include <sys/mount.h>
#include <mntent.h>

/* the front-end for kernel on-line resizer */
int resize_fs_online(char * devname, long long int blocks) {
	struct mntent *mnt;
	char buf[64];
	
	/* Find the mount entry. */
	if ((mnt = util_mntent(devname)) == NULL)
		misc_die ("resize_reiserfs: can't find mount entry\n");

	sprintf(buf,"resize=%lld", blocks);

	if (mount(mnt->mnt_fsname, mnt->mnt_dir, mnt->mnt_type,
		  (unsigned long)(MS_MGC_VAL << 16 | MS_REMOUNT), buf)) 
	{
		misc_die ("resize_reiserfs: remount failed: %s\n", 
		     strerror(errno));
	}

	return 0;
}

