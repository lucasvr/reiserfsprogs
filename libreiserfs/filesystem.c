/*
 *  Copyright 2000-2004 by Hans Reiser, licensing governed by 
 *  reiserfsprogs/README
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "reiserfs/libreiserfs.h"
#include "misc/malloc.h"
#include "misc/device.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

int reiserfs_fs_blksize_check (unsigned int blocksize)
{
    return ((((blocksize & -blocksize) == blocksize) 
	&& (blocksize >= 512) && (blocksize <= 8192)));
}

reiserfs_blktype_t reiserfs_fs_block(reiserfs_filsys_t *fs, 
				     unsigned long block) 
{
    if (block < fs->fs_super_bh->b_blocknr ||
	block >= reiserfs_sb_get_blocks (fs->fs_ondisk_sb))
    {
	return BT_INVAL;
    }

    if (block == fs->fs_super_bh->b_blocknr)
	return BT_SUPER;

    if (reiserfs_journal_block (fs, block))
	return BT_JOURNAL;

    if (reiserfs_bitmap_block(fs, block))
	return BT_BITMAP;

    return BT_UNKNOWN;
}


/* read super block. fixme: only 4k blocks, pre-journaled format
   is refused. Journal and bitmap are to be opened separately.
   skip_check is set to 1 if checks of openned SB should be omitted.*/
reiserfs_filsys_t * reiserfs_fs_open (char * filename, int flags, 
				      int *error, void * vp, int check)
{
    reiserfs_filsys_t * fs;
    reiserfs_bh_t * bh;
    reiserfs_sb_t * sb;
    int fd, i;

    if (error) 
	*error = 0;

    fd = open (filename, flags 
#if defined(O_LARGEFILE)
	       | O_LARGEFILE
#endif
	       );
    if (fd == -1) {
	if (error)
	    *error = errno;
	return 0;
    }

    fs = misc_getmem (sizeof (*fs));
    fs->fs_dev = fd;
    fs->fs_vp = vp;
    strncpy(fs->fs_file_name, filename, sizeof(fs->fs_file_name));
    
    /* reiserfs super block is either in 16-th or in 2-nd 4k block of the
       device */
    for (i = 2; i < 17; i += 14) {
	bh = reiserfs_buffer_read (fd, i, 4096);
	if (!bh) {
	    reiserfs_warning (stderr, "reiserfs_fs_open: reiserfs_buffer_read "
			      "failed reading block %d\n", i);
	} else {
	    sb = (reiserfs_sb_t *)bh->b_data;
	    
	    if (reiserfs_super_magic(sb))
		goto found;

	    /* reiserfs signature is not found at the i-th 4k block */
	    reiserfs_buffer_close (bh);
	}
    }

    reiserfs_warning(stderr, "\nreiserfs_fs_open: the reiserfs superblock "
		     "cannot be found on %s.\n", filename);
    
    misc_freemem (fs);
    close (fd);
    fs = NULL;
    return fs;

 found:

    if (!reiserfs_fs_blksize_check(reiserfs_sb_get_blksize(sb))) {
	reiserfs_warning(stderr, "reiserfs_fs_open: a superblock with "
			 "wrong parameters was found in the block (%d).\n", i);
	misc_freemem (fs);
	close (fd);
	reiserfs_buffer_close(bh);
	return NULL;
    }

    if (check) {
	/* A few checks of found super block. */
	reiserfs_bh_t *tmp_bh;
	
	tmp_bh = reiserfs_buffer_read (fd, reiserfs_sb_get_blocks(sb) - 1, 
				 reiserfs_sb_get_blksize(sb));
	
	if (!tmp_bh) {
	    reiserfs_warning (stderr, "\n%s: Your partition is not big enough "
			      "to contain the \nfilesystem of (%lu) blocks as "
			      "was specified in the found super block.\n", 
			      __FUNCTION__,  reiserfs_sb_get_blocks(sb) - 1);
	    
	    misc_freemem (fs);
	    close (fd);
	    reiserfs_buffer_close(bh);
	    return NULL;
	}
	
	reiserfs_buffer_close(tmp_bh);
    }
   
    fs->fs_blocksize = reiserfs_sb_get_blksize (sb);
    
    /* check block size on the filesystem */
    if (fs->fs_blocksize != 4096) {
	i = bh->b_blocknr * 4096 / fs->fs_blocksize;
	reiserfs_buffer_close (bh);
	bh = reiserfs_buffer_read (fd, i, fs->fs_blocksize);
	if (!bh) {
	    reiserfs_warning (stderr, "reiserfs_fs_open: reiserfs_buffer_read "
			      "failed reading block %d, size %d\n",
			      i, fs->fs_blocksize);
	    misc_freemem (fs);
	    return 0;
	}
	sb = (reiserfs_sb_t *)bh->b_data;
    }

    fs->hash = reiserfs_hash_func (reiserfs_sb_get_hash (sb));
    fs->fs_super_bh = bh;
    fs->fs_ondisk_sb = sb;
    fs->fs_flags = flags; /* O_RDONLY or O_RDWR */

    fs->fs_format = reiserfs_super_format (sb);
    
    /*reiserfs_read_bitmap_blocks(fs);*/
    if (flags & O_RDWR)
	fs->fs_dirt  = 1;
    else
	fs->fs_dirt = 0;

    return fs;
}


/* creates buffer for super block and fills it up with fields which are
   constant for given size and version of a filesystem */
reiserfs_filsys_t * reiserfs_fs_create (char * filename,
					int version,
					unsigned long block_count, 
					int block_size, 
					int default_journal, 
					int new_format)
{
    reiserfs_filsys_t * fs;
    unsigned long bmap_nr;

    if (misc_device_count_blocks (filename, block_size) < block_count) {
	reiserfs_warning (stderr, "reiserfs_fs_create: no enough "
			  "blocks on device\n");
	return 0;
    }

    if (!reiserfs_journal_fits (REISERFS_DISK_OFFSET_IN_BYTES / block_size, 
				block_size, block_count, 0))
    {
	reiserfs_warning (stderr, "reiserfs_fs_create: can not create that "
			  "small (%d blocks) filesystem\n", block_count);
	return 0;
    }

    fs = misc_getmem (sizeof (*fs));
    if (!fs) {
	reiserfs_warning (stderr, "reiserfs_fs_create: misc_getmem failed\n");
	return 0;
    }

    fs->fs_dev = open (filename, O_RDWR 
#if defined(O_LARGEFILE)
		       | O_LARGEFILE
#endif
		       );
    if (fs->fs_dev == -1) {
	reiserfs_warning (stderr, "reiserfs_fs_create: could not open %s: %s\n",
			  filename, strerror(errno));
	
	misc_freemem (fs);
	return 0;
    }

    fs->fs_blocksize = block_size;
    sprintf(fs->fs_file_name, filename);
    fs->fs_format = version;

    if (new_format)
        fs->fs_super_bh = reiserfs_buffer_open (fs->fs_dev, 
	    REISERFS_DISK_OFFSET_IN_BYTES / block_size, block_size);
    else 
        fs->fs_super_bh = reiserfs_buffer_open (fs->fs_dev, 
	    REISERFS_OLD_DISK_OFFSET_IN_BYTES / block_size, block_size);
    
    if (!fs->fs_super_bh) {
	reiserfs_warning (stderr, "reiserfs_fs_create: "
			  "reiserfs_buffer_open failed\n");
	return 0;
    }

    reiserfs_buffer_mkuptodate (fs->fs_super_bh, 1);
    
    fs->fs_ondisk_sb = (reiserfs_sb_t *)fs->fs_super_bh->b_data;
    memset (fs->fs_ondisk_sb, 0, block_size);
    
    /* fill super block fields which are constant for given version 
       and block count */
    reiserfs_sb_set_blocks (fs->fs_ondisk_sb, block_count);
    /* sb_free_blocks */
    /* sb_root_block */
    /* sb_journal_1st_block */
    /* sb_journal_dev */
    /* sb_orig_journal_size */
    /* sb_joural_magic */
    /* sb_journal magic_F */
    /* sb_mount_id */
    /* sb_not_used0 */
    /* sb_generation_number */    
    reiserfs_sb_set_blksize (fs->fs_ondisk_sb, block_size);
    switch (version) {
    case REISERFS_FORMAT_3_5:
	reiserfs_sb_set_mapmax (fs->fs_ondisk_sb, 
	    (block_size - REISERFS_SB_SIZE_V1) / sizeof(__u32) / 2 * 2);
	/* sb_oid_cursize */
	/* sb_state */
	memcpy (fs->fs_ondisk_sb->s_v1.s_magic, 
		REISERFS_3_5_SUPER_MAGIC_STRING,
		strlen (REISERFS_3_5_SUPER_MAGIC_STRING));
	break;

    case REISERFS_FORMAT_3_6:
	reiserfs_sb_set_mapmax (fs->fs_ondisk_sb, 
	    (block_size - REISERFS_SB_SIZE) / sizeof(__u32) / 2 * 2);
	/* sb_oid_cursize */
	/* sb_state */
        memcpy (fs->fs_ondisk_sb->s_v1.s_magic, 
		REISERFS_3_6_SUPER_MAGIC_STRING,
                strlen (REISERFS_3_6_SUPER_MAGIC_STRING));
	break;
    }
    if (!default_journal)
        memcpy (fs->fs_ondisk_sb->s_v1.s_magic, 
		REISERFS_JR_SUPER_MAGIC_STRING,
                strlen (REISERFS_JR_SUPER_MAGIC_STRING));

    /* sb_fsck_state */
    /* sb_hash_function_code */
    /* sb_tree_height */
    bmap_nr = reiserfs_bmap_nr(block_count, block_size);

    reiserfs_sb_set_version (fs->fs_ondisk_sb, version);
    reiserfs_sb_set_bmaps (fs->fs_ondisk_sb, 
			   reiserfs_bmap_over(bmap_nr) ? 0 : bmap_nr);
    
    /* sb_not_used1 */
    reiserfs_buffer_mkdirty (fs->fs_super_bh);
    fs->fs_dirt = 1;

    return fs;
}

/* flush bitmap, reiserfs_buffer_close super block, flush all dirty buffers, 
   close and open again the device, read super block */
static void reiserfs_only_reopen (reiserfs_filsys_t * fs, int flag)
{
    unsigned long super_block;


    /*  reiserfs_bitmap_flush (fs->fs_bitmap2, fs);*/
    super_block = fs->fs_super_bh->b_blocknr;                
    reiserfs_buffer_close (fs->fs_super_bh);
    reiserfs_buffer_flush_all(fs->fs_dev);
    
    reiserfs_buffer_invalidate_all (fs->fs_dev);
    if (close (fs->fs_dev))
	misc_die ("reiserfs_fs_reopen: closed failed: %s", strerror(errno));
    
    fs->fs_dev = open (fs->fs_file_name, flag 
#if defined(O_LARGEFILE)
		       | O_LARGEFILE
#endif
		       );
    if (fs->fs_dev == -1)
	misc_die ("reiserfs_fs_reopen: could not reopen device: %s", 
	     strerror(errno));

    fs->fs_super_bh = reiserfs_buffer_read (fs->fs_dev, super_block, 
				      fs->fs_blocksize);
    
    if (!fs->fs_super_bh)
	misc_die ("reiserfs_fs_reopen: reading super block failed");
    
    fs->fs_ondisk_sb = (reiserfs_sb_t *)fs->fs_super_bh->b_data;
    fs->fs_flags = flag; /* O_RDONLY or O_RDWR */
    
    if (flag & O_RDWR)
	fs->fs_dirt  = 1;
    else
	fs->fs_dirt = 0;
}

void reiserfs_fs_reopen (reiserfs_filsys_t * fs, int flag)
{
    reiserfs_only_reopen (fs, flag);
    reiserfs_journal_reopen (fs, flag);
}

int reiserfs_fs_rw (reiserfs_filsys_t * fs)
{
    if ((fs->fs_flags) & O_RDWR)
	return 1;
    return 0;
}


/* flush all changes made on a filesystem */
void reiserfs_fs_flush (reiserfs_filsys_t * fs)
{
    if (fs->fs_dirt) {
	reiserfs_journal_flush (fs);
	reiserfs_buffer_flush_all(fs->fs_dev);
    }
    fs->fs_dirt = 0;
}


/* free all memory involved into manipulating with filesystem */
void reiserfs_fs_free (reiserfs_filsys_t * fs)
{
    reiserfs_journal_free (fs);
    reiserfs_bitmap_free (fs);

    /* release super block and memory used by filesystem handler */
    reiserfs_buffer_close (fs->fs_super_bh);
    fs->fs_super_bh = 0;

    reiserfs_buffer_free_all ();
    misc_freemem (fs);
}


/* this closes everything: journal. bitmap and the fs itself */
void reiserfs_fs_close (reiserfs_filsys_t * fs)
{
    reiserfs_journal_close (fs);
    reiserfs_bitmap_close (fs);

    reiserfs_fs_flush (fs);
    fsync(fs->fs_dev);
    reiserfs_fs_free (fs);
}

void reiserfs_print_bmap_block (FILE * fp, int i, unsigned long block, 
				char * map, int blocks, int silent, 
				int blocksize)
{
    int j, k;
    int bits = blocksize * 8;
    int zeros = 0, ones = 0;
  

    reiserfs_warning (fp, "#%d: block %lu: ", i, block);

    blocks = blocksize * 8;

    if (misc_test_bit (0, map)) {
	/* first block addressed by this bitmap block is used */
	ones ++;
	if (!silent)
	    reiserfs_warning (fp, "Busy (%d-", i * bits);
	for (j = 1; j < blocks; j ++) {
	    while (misc_test_bit (j, map)) {
		ones ++;
		if (j == blocks - 1) {
		    if (!silent)
			reiserfs_warning (fp, "%d)\n", j + i * bits);
		    goto end;
		}
		j++;
	    }
	    if (!silent)
		reiserfs_warning (fp, "%d) Free(%d-", 
				  j - 1 + i * bits, j + i * bits);

	    while (!misc_test_bit (j, map)) {
		zeros ++;
		if (j == blocks - 1) {
		    if (!silent)
			reiserfs_warning (fp, "%d)\n", j + i * bits);
		    goto end;
		}
		j++;
	    }
	    if (!silent)
		reiserfs_warning (fp, "%d) Busy(%d-", 
				  j - 1 + i * bits, j + i * bits);

	    j --;
	end:
	    /* to make gcc 3.2 do not sware here */;
	}
    } else {
	/* first block addressed by this bitmap is free */
	zeros ++;
	if (!silent)
	    reiserfs_warning (fp, "Free (%d-", i * bits);
	for (j = 1; j < blocks; j ++) {
	    k = 0;
	    while (!misc_test_bit (j, map)) {
		k ++;
		if (j == blocks - 1) {
		    if (!silent)
			reiserfs_warning (fp, "%d)\n", j + i * bits);
		    zeros += k;
		    goto end2;
		}
		j++;
	    }
	    zeros += k;
	    if (!silent)
		reiserfs_warning (fp, "%d) Busy(%d-", 
				  j - 1 + i * bits, j + i * bits);
	    
	    k = 0;
	    while (misc_test_bit (j, map)) {
		ones ++;
		if (j == blocks - 1) {
		    if (!silent)
			reiserfs_warning (fp, "%d)\n", j + i * bits);
		    ones += k;
		    goto end2;
		}
		j++;
	    }
	    ones += k;
	    if (!silent)
		reiserfs_warning (fp, "%d) Free(%d-", 
				  j - 1 + i * bits, j + i * bits);
	
	    j --;
	end2:
	    /* to make gcc 3.2 do not sware here */;
	}
    }

    reiserfs_warning (fp, "used %d, free %d\n", ones, zeros);
}

