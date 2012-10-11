/*
 * Copyright 1996-2004 by Hans Reiser, licensing governed by 
 * reiserfsprogs/README
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "fsck.h"
#include <errno.h>

#define FSCK_DUMP_START_MAGIC 374033
#define FSCK_DUMP_END_MAGIC 7786470

void fsck_stage_start_put (FILE * file, unsigned long stage) {
    __u32 v = FSCK_DUMP_START_MAGIC;
    fwrite (&v, 4, 1, file);
    fwrite (&stage, 4, 1, file);
}


void fsck_stage_end_put (FILE * file) {
    __u32 v = FSCK_DUMP_END_MAGIC;
    fwrite (&v, 4, 1, file);
}

/*return last passed stage*/
int fsck_stage_magic_check (FILE * fp)
{
    __u32 v;

    if (fseek (fp, -4, SEEK_END)) {
	reiserfs_warning (stderr, "%s: fseek failed: %s\n", 
			  __FUNCTION__, strerror(errno));
	return -1;
    }

    fread (&v, 4, 1, fp);
    if (v != FSCK_DUMP_END_MAGIC) {
	reiserfs_warning (stderr, "%s: no magic found\n", __FUNCTION__);
	return -1;
    }

    if (fseek (fp, 0, SEEK_SET)) {
	reiserfs_warning (stderr, "%s: fseek failed: %s\n", 
			  __FUNCTION__, strerror(errno));
	return -1;
    }

    fread (&v, 4, 1, fp);
    if (v != FSCK_DUMP_START_MAGIC) {
	reiserfs_warning (stderr, "%s: no magic found\n", __FUNCTION__);
	return -1;
    }

    fread (&v, 4, 1, fp);
    if (v != PASS_0_DONE && 
	v != PASS_1_DONE && 
	v != TREE_IS_BUILT && 
	v != SEMANTIC_DONE && 
	v != LOST_FOUND_DONE) 
    {
	reiserfs_warning (stderr, "%s: wrong pass found", __FUNCTION__);
	return -1;
    }

    return (__u16)v;
}

