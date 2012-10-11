/*
 *  Copyright 2000-2004 by Hans Reiser, licensing governed by 
 *  reiserfsprogs/README
 */

#ifndef REISERFS_FS_H
#define REISERFS_FS_H

#include <reiserfs/types.h>

/* ReiserFS leaves the first 64k unused, so that partition labels have enough
   space.  If someone wants to write a fancy bootloader that needs more than
   64k, let us know, and this will be increased in size.  This number must be
   larger than than the largest block size on any platform, or code will
   break.  -Hans */
#define REISERFS_DISK_OFFSET_IN_BYTES (64 * 1024)

/*#define MD_RAID_SUPERBLOCKS_IN_BYTES (128 * 1024)*/

/* the spot for the super in versions 3.5 - 3.5.11 (inclusive) */
#define REISERFS_OLD_DISK_OFFSET_IN_BYTES (8 * 1024)

#define reiserfs_ondisk_offset(block_of_super_block, block_size)	\
	(block_of_super_block * block_size)

#define reiserfs_new_location(block_of_super_block, block_size)		\
    ((reiserfs_ondisk_offset(block_of_super_block, block_size) ==	\
      REISERFS_DISK_OFFSET_IN_BYTES) ? 1 : 0)
	
/*only 4k blocks for old location*/
#define reiserfs_old_location(block_of_super_block, block_size)		\
    ((reiserfs_ondisk_offset(block_of_super_block, 4096) ==		\
      REISERFS_OLD_DISK_OFFSET_IN_BYTES) ? 1 : 0)

enum reiserfs_blktype {
    BT_INVAL	= 0x1,
    BT_SUPER	= 0x2,
    BT_JOURNAL	= 0x3,
    BT_BITMAP	= 0x4,
    BT_UNKNOWN
};

typedef enum reiserfs_blktype reiserfs_blktype_t;

extern reiserfs_filsys_t * reiserfs_fs_open (char * filename, 
					     int flags, 
					     int * error, 
					     void * vp, 
					     int skip_check);

extern reiserfs_filsys_t * reiserfs_fs_create (char * filename, 
					       int version, 
					       unsigned long block_count,
					       int block_size, 
					       int default_journal, 
					       int new_format);

extern void reiserfs_fs_flush (reiserfs_filsys_t *);

extern void reiserfs_fs_free (reiserfs_filsys_t *);

extern void reiserfs_fs_close (reiserfs_filsys_t *);

extern void reiserfs_fs_reopen (reiserfs_filsys_t *, int flags);

extern int reiserfs_fs_rw (reiserfs_filsys_t * fs);

extern int reiserfs_fs_blksize_check (unsigned int blocksize);

extern reiserfs_blktype_t reiserfs_fs_block(reiserfs_filsys_t *fs, 
					    unsigned long block);

#endif
