/*
 *  Copyright 2000-2004 by Hans Reiser, licensing governed by 
 *  reiserfsprogs/README
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "reiserfs/libreiserfs.h"

int reiserfs_super_35_magic (reiserfs_sb_t * rs)
{
    return (!strncmp (rs->s_v1.s_magic, REISERFS_3_5_SUPER_MAGIC_STRING, 
		      strlen ( REISERFS_3_5_SUPER_MAGIC_STRING)));
}


int reiserfs_super_36_magic (reiserfs_sb_t * rs)
{
    return (!strncmp (rs->s_v1.s_magic, REISERFS_3_6_SUPER_MAGIC_STRING, 
		      strlen ( REISERFS_3_6_SUPER_MAGIC_STRING)));
}


int reiserfs_super_jr_magic (reiserfs_sb_t * rs)
{
    return (!strncmp (rs->s_v1.s_magic, REISERFS_JR_SUPER_MAGIC_STRING, 
		      strlen ( REISERFS_JR_SUPER_MAGIC_STRING)));
}


int reiserfs_super_magic (reiserfs_sb_t * rs)
{
    if (reiserfs_super_35_magic (rs) ||
	reiserfs_super_36_magic (rs) ||
	reiserfs_super_jr_magic (rs))
	return 1;
    return 0;
}


int reiserfs_super_format (reiserfs_sb_t * sb)
{
    /* after conversion to 3.6 format we change magic correctly, 
       but do not change sb_format. When we create non-standard journal 
       field format in sb get adjusted correctly. Thereby, for standard 
       journal we should rely on magic and for non-standard - on format */
    if (reiserfs_super_35_magic (sb) ||
	(reiserfs_super_jr_magic (sb) && 
	 reiserfs_sb_get_version (sb) == REISERFS_FORMAT_3_5))
	return REISERFS_FORMAT_3_5;

    if (reiserfs_super_36_magic (sb) ||
	(reiserfs_super_jr_magic (sb) &&
	 reiserfs_sb_get_version (sb) == REISERFS_FORMAT_3_6))
	return REISERFS_FORMAT_3_6;

    return REISERFS_FORMAT_UNKNOWN;
}

int reiserfs_super_size (reiserfs_sb_t * sb)
{
    switch (reiserfs_super_format (sb)) {
    case REISERFS_FORMAT_3_5:
	return REISERFS_SB_SIZE_V1;
    case REISERFS_FORMAT_3_6:
	return REISERFS_SB_SIZE;
    }
    reiserfs_panic ("Unknown format found");
    return 0;
}

/* this one had signature in different place of the super_block
   structure */
int reiserfs_super_prejournaled (reiserfs_sb_t * rs)
{
    return (!strncmp((char*)rs + REISERFS_SUPER_MAGIC_STRING_OFFSET_NJ,
		     REISERFS_3_5_SUPER_MAGIC_STRING,
		     strlen(REISERFS_3_5_SUPER_MAGIC_STRING)));
}

int reiserfs_super_valid(reiserfs_bh_t *bh) {
    reiserfs_sb_t *sb;

    sb = (reiserfs_sb_t *)bh->b_data;
    
    if (!reiserfs_super_magic (sb))
    	return 0;
    
    if (!reiserfs_fs_blksize_check(reiserfs_sb_get_blksize (sb)))
	return 0;
	
    return 1;
}

/* return 1 if this is not super block */
int reiserfs_super_print (FILE * fp, 
			  reiserfs_filsys_t * fs, 
			  char * file_name,
			  reiserfs_bh_t * bh, 
			  int short_print)
{
    reiserfs_sb_t * sb = (reiserfs_sb_t *)(bh->b_data);
    int format = 0;
    __u16 state;

    if (!reiserfs_super_valid(bh))
	return 1;

    /* Print volume label if it is non-empty. */
    if (sb->s_label[0])
	    reiserfs_warning (fp, "%s: ", sb->s_label);
    else
	    reiserfs_warning (fp, "%s: ", fs->fs_file_name);
    
    reiserfs_warning (fp, "Reiserfs super block in block %lu on %s of ",
		      bh->b_blocknr, fs->fs_file_name);
    switch (reiserfs_super_format (sb)) {
    case REISERFS_FORMAT_3_5:
	reiserfs_warning (fp, "format 3.5 with ");
        format = 1;
	break;
    case REISERFS_FORMAT_3_6:
	reiserfs_warning (fp, "format 3.6 with ");
        format = 2;
	break;
    default:
	reiserfs_warning (fp, "unknown format with ");
	break;
    }
    if (reiserfs_super_jr_magic (sb))
	reiserfs_warning (fp, "non-");
    reiserfs_warning (fp, "standard journal\n");
    if (short_print) {
	reiserfs_warning (fp, "Blocks (total/free): %u/%u by %d bytes\n",
		reiserfs_sb_get_blocks (sb), reiserfs_sb_get_free (sb), reiserfs_sb_get_blksize (sb));
    } else {
	reiserfs_warning (fp, "Count of blocks on the device: %u\n", reiserfs_sb_get_blocks (sb));
	reiserfs_warning (fp, "Number of bitmaps: %u\n", reiserfs_sb_get_bmaps (sb));
	reiserfs_warning (fp, "Blocksize: %d\n", reiserfs_sb_get_blksize (sb));
	reiserfs_warning (fp, "Free blocks (count of blocks - used [journal, "
		      "bitmaps, data, reserved] blocks): %u\n", reiserfs_sb_get_free (sb));
	reiserfs_warning (fp, "Root block: %u\n", reiserfs_sb_get_root (sb));
    }
    reiserfs_warning (fp, "Filesystem is %sclean\n",
		      (reiserfs_sb_get_umount (sb) == FS_CLEANLY_UMOUNTED) ? "" : "NOT ");

    if (short_print)
    	return 0;
    reiserfs_warning (fp, "Tree height: %d\n", reiserfs_sb_get_height (sb));
    reiserfs_warning (fp, "Hash function used to sort names: %s\n",
		      reiserfs_hash_name (reiserfs_sb_get_hash (sb)));
    reiserfs_warning (fp, "Objectid map size %d, max %d\n", reiserfs_sb_get_mapcur (sb),
		      reiserfs_sb_get_mapmax (sb));
    reiserfs_warning (fp, "Journal parameters:\n");
    reiserfs_journal_print_params (fp, reiserfs_sb_jp (sb));
    reiserfs_warning (fp, "Blocks reserved by journal: %u\n",
		      reiserfs_sb_get_reserved (sb));
    state = reiserfs_sb_get_state (sb);
    reiserfs_warning (fp, "Fs state field: 0x%x:\n", state);
    if ((state & FS_FATAL) == FS_FATAL)
	reiserfs_warning (fp, "\tFATAL corruptions exist.\n");
    if ((state & FS_ERROR) == FS_ERROR)
	reiserfs_warning (fp, "\t some corruptions exist.\n");
    if ((state & IO_ERROR) == IO_ERROR)
	reiserfs_warning (fp, "\tI/O corruptions exist.\n");

    reiserfs_warning (fp, "sb_version: %u\n", reiserfs_sb_get_version (sb));
    if (format == 2) {
        reiserfs_warning (fp, "inode generation number: %u\n", reiserfs_sb_get_gen (sb));
        reiserfs_warning (fp, "UUID: %U\n", sb->s_uuid);
        reiserfs_warning (fp, "LABEL: %.16s\n", sb->s_label);
        reiserfs_warning (fp, "Set flags in SB:\n");
	if ((reiserfs_sb_isflag (sb, reiserfs_attrs_cleared)))
	    reiserfs_warning (fp, "\tATTRIBUTES CLEAN\n");
    }

    return 0;
}


void reiserfs_super_print_state (FILE * fp, reiserfs_filsys_t * fs)
{
    reiserfs_warning (fp, "\nFilesystem state: ");
    if (reiserfs_sb_state_ok (fs))
	reiserfs_warning (fp, "consistent\n\n");
    else
	reiserfs_warning (fp, "consistency is not checked after last mounting\n\n");
}


