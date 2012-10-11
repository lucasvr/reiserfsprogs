/*
 *  Copyright 2000-2004 by Hans Reiser, licensing governed by 
 *  reiserfsprogs/README
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "reiserfs/libreiserfs.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>

/* access to fields of stat data (both v1 and v2) */

static void reiserfs_stat_field (int field, 
				 reiserfs_ih_t * ih, 
				 void * sd,
				 void * value, 
				 int set)
{
    if (reiserfs_ih_get_format (ih) == KEY_FORMAT_1) {
	reiserfs_sd_v1_t * sd_v1 = sd;

	switch (field) {
	case STAT_MODE:
	    if (set)
		sd_v1->sd_mode = cpu_to_le16 (*(__u16 *)value);
	    else
		*(__u16 *)value = le16_to_cpu (sd_v1->sd_mode);
	    break;

	case STAT_SIZE:
	    /* value must point to 64 bit int */
	    if (set)
		sd_v1->sd_size = cpu_to_le32 (*(__u64 *)value);
	    else
		*(__u64 *)value = le32_to_cpu (sd_v1->sd_size);
	    break;

	case STAT_BLOCKS:
	    if (set)
		sd_v1->u.sd_blocks = cpu_to_le32 (*(__u32 *)value);
	    else
		*(__u32 *)value = le32_to_cpu (sd_v1->u.sd_blocks);
	    break;

	case STAT_NLINK:
	    /* value must point to 32 bit int */
	    if (set)
		sd_v1->sd_nlink = cpu_to_le16 (*(__u32 *)value);
	    else
		*(__u32 *)value = le16_to_cpu (sd_v1->sd_nlink);
	    break;

	case STAT_FDB:
	    if (set)
		sd_v1->sd_fdb = cpu_to_le32 (*(__u32 *)value);
	    else
		*(__u32 *)value = le32_to_cpu (sd_v1->sd_fdb);
	    break;
	    
	default:
	    reiserfs_panic ("reiserfs_stat_set: unknown field of old stat data");
	}
    } else {
	reiserfs_sd_t * sd_v2 = sd;

	switch (field) {
	case STAT_MODE:
	    if (set)
		sd_v2->sd_mode = cpu_to_le16 (*(__u16 *)value);
	    else
		*(__u16 *)value = le16_to_cpu (sd_v2->sd_mode);
	    break;

	case STAT_SIZE:
	    if (set)
		sd_v2->sd_size = cpu_to_le64 (*(__u64 *)value);
	    else
		*(__u64 *)value = le64_to_cpu (sd_v2->sd_size);
	    break;

	case STAT_BLOCKS:
	    if (set)
		sd_v2->sd_blocks = cpu_to_le32 (*(__u32 *)value);
	    else
		*(__u32 *)value = le32_to_cpu (sd_v2->sd_blocks);
	    break;

	case STAT_NLINK:
	    if (set)
		sd_v2->sd_nlink = cpu_to_le32 (*(__u32 *)value);
	    else
		*(__u32 *)value = le32_to_cpu (sd_v2->sd_nlink);
	    break;

	case STAT_FDB:
	default:
	    reiserfs_panic ("reiserfs_stat_set: unknown field of new stat data");
	}
    }
}

void reiserfs_stat_set (int field, reiserfs_ih_t * ih, 
			void * sd, void * value)
{
	reiserfs_stat_field(field, ih, sd, value, 1/*set*/);
}

void reiserfs_stat_get (int field, reiserfs_ih_t * ih, 
			void * sd, void * value)
{
	reiserfs_stat_field(field, ih, sd, value, 0/*get*/);
}


/* prepare new or old stat data for the new directory */
void reiserfs_stat_init (int blocksize, int key_format, 
			 __u32 dirid, __u32 objectid, 
			 reiserfs_ih_t * ih, void * sd)
{
    memset (ih, 0, REISERFS_IH_SIZE);
    reiserfs_key_set_did (&ih->ih_key, dirid);
    reiserfs_key_set_oid (&ih->ih_key, objectid);
    reiserfs_key_set_off1 (&ih->ih_key, OFFSET_SD);
    reiserfs_key_set_uni (&ih->ih_key, 0);

    reiserfs_ih_set_format (ih, key_format);
    reiserfs_ih_set_free (ih, MAX_US_INT);
    
    if (key_format == KEY_FORMAT_2) {
        reiserfs_sd_t *sd_v2 = (reiserfs_sd_t *)sd;

	reiserfs_ih_set_len (ih, REISERFS_SD_SIZE);
        reiserfs_set_sd_v2_mode (sd_v2, S_IFDIR + 0755);
        reiserfs_set_sd_v2_nlink (sd_v2, 2);
        reiserfs_set_sd_v2_uid (sd_v2, 0);
        reiserfs_set_sd_v2_gid (sd_v2, 0);
        reiserfs_set_sd_v2_size (sd_v2, REISERFS_DIR_MIN);
        reiserfs_set_sd_v2_atime (sd_v2, time(NULL));
        sd_v2->sd_ctime = sd_v2->sd_mtime = sd_v2->sd_atime; /* all le */
        reiserfs_set_sd_v2_rdev (sd_v2, 0);
        reiserfs_set_sd_v2_blocks (sd_v2, REISERFS_DIR_BLOCKS (REISERFS_DIR_MIN));
    }else{
        reiserfs_sd_v1_t *sd_v1 = (reiserfs_sd_v1_t *)sd;

	reiserfs_ih_set_len (ih, REISERFS_SD_SIZE_V1);
        reiserfs_set_sd_v1_mode(sd_v1, S_IFDIR + 0755);
        reiserfs_set_sd_v1_nlink(sd_v1, 2);
        reiserfs_set_sd_v1_uid (sd_v1, 0);
        reiserfs_set_sd_v1_gid (sd_v1, 0);
        reiserfs_set_sd_v1_size(sd_v1, REISERFS_DIR_MIN_V1);
        reiserfs_set_sd_v1_atime(sd_v1, time(NULL));
        sd_v1->sd_ctime = sd_v1->sd_mtime = sd_v1->sd_atime; /* all le */
        reiserfs_set_sd_v1_blocks(sd_v1, REISERFS_DIR_BLOCKS(REISERFS_DIR_MIN_V1));
        reiserfs_set_sd_v1_fdb(sd_v1, REISERFS_SD_NODIRECT);
    }
}

char timebuf[256];

static char * timestamp (time_t t)
{
    strftime (timebuf, 256, "%d/%Y %T", localtime (&t));
    return timebuf;
}

int reiserfs_print_stat_data (FILE * fp, reiserfs_bh_t * bh, 
			      reiserfs_ih_t * ih, int alltimes)
{
    int retval;
    

    /* we cannot figure out if it is new stat data or old by key_format
       macro. Stat data's key looks identical in both formats */
    if (reiserfs_ih_get_format (ih) == KEY_FORMAT_1) {
        reiserfs_sd_v1_t * sd_v1 = 
		(reiserfs_sd_v1_t *)reiserfs_item_by_ih (bh, ih);
	
	reiserfs_warning (fp, "(OLD SD), mode %M, size %u, nlink %u, uid %u, "
			  "FDB %u, mtime %s blocks %u", 
			  reiserfs_sd_v1_mode(sd_v1), 
			  reiserfs_sd_v1_size(sd_v1), 
			  reiserfs_sd_v1_nlink(sd_v1),
			  reiserfs_sd_v1_uid(sd_v1), 
			  reiserfs_sd_v1_fdb(sd_v1), 
			  timestamp(reiserfs_sd_v1_mtime(sd_v1)), 
			  reiserfs_sd_v1_blocks(sd_v1));
	
	retval = (S_ISLNK (reiserfs_sd_v1_mode(sd_v1))) ? 1 : 0;
	
        if (alltimes)
            reiserfs_warning (fp, "%s %s\n", 
			      timestamp (reiserfs_sd_v1_ctime(sd_v1)),
			      timestamp (reiserfs_sd_v1_atime(sd_v1)));
    } else {
        reiserfs_sd_t * sd = (reiserfs_sd_t *)reiserfs_item_by_ih (bh, ih);
	reiserfs_warning (fp, "(NEW SD), mode %M, size %Lu, nlink %u, mtime "
			  "%s blocks %u, uid %u", reiserfs_sd_v2_mode(sd), 
			  reiserfs_sd_v2_size(sd), reiserfs_sd_v2_nlink(sd), 
			  timestamp (reiserfs_sd_v2_mtime(sd)), 
			  reiserfs_sd_v2_blocks(sd), reiserfs_sd_v2_uid(sd));
	
	retval = (S_ISLNK (reiserfs_sd_v2_mode(sd))) ? 1 : 0;
        if (alltimes)
            reiserfs_warning (fp, "%s %s\n", 
			      timestamp (reiserfs_sd_v2_ctime(sd)),
			      timestamp (reiserfs_sd_v2_atime(sd)));
    }

    reiserfs_warning (fp, "\n");
    return retval;
}


