/* 
 * Copyright 2000-2004 by Hans Reiser, licensing governed by 
 * reiserfsprogs/README
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "resize.h"
#include "misc/device.h"
#include "misc/malloc.h"
#include "util/device.h"
#include "util/print.h"

#include <getopt.h>
#include <errno.h>
#include <fcntl.h>


int opt_banner = 0;
int opt_force = 0;
int opt_verbose = 1;			/* now "verbose" option is default */
int opt_nowrite = 0;
int opt_safe = 0;
int opt_skipj = 0;

/* calculate the new fs size (in blocks) from old fs size and the string
   representation of new size */
static long long int calc_new_fs_size(unsigned long count, 
				      unsigned int bs, 
				      char *bytes_str)
{
    long long int bytes;
    long long int blocks;
    char *end;
    int rel;

    end = bytes_str + strlen(bytes_str) - 1;
    rel = bytes_str[0] == '+' || bytes_str[0] == '-';
    bytes = strtoll(bytes_str, &bytes_str, 10);
    
    /* Some error occured while convertion or the specified 
       string is not valid. */
    if (bytes == LONG_LONG_MIN || bytes == LONG_LONG_MAX || 
	(bytes_str != end && bytes_str != end + 1))
	return -EINVAL;

    switch (*end) {
    case 'G':
    case 'g':
	bytes *= 1024;
    case 'M':
    case 'm':
	bytes *= 1024;
    case 'K':
    case 'k':
	bytes *= 1024;
    }
	
    blocks = bytes / bs;

    return rel ? count + blocks : blocks;
}

/* print some fs parameters */
static void sb_report(reiserfs_sb_t * sb1,
		      reiserfs_sb_t * sb2)
{
    printf(
	"ReiserFS report:\n"
	"blocksize             %d\n"
	"block count           %d (%d)\n"
	"free blocks           %d (%d)\n"
	"bitmap block count    %d (%d)\n", 
	reiserfs_sb_get_blksize(sb1),
	reiserfs_sb_get_blocks(sb1), reiserfs_sb_get_blocks(sb2),
	reiserfs_sb_get_free(sb1), reiserfs_sb_get_free(sb2),
	reiserfs_sb_get_bmaps(sb1), reiserfs_sb_get_bmaps(sb2));
};

/* conditional bwrite */
static int bwrite_cond (reiserfs_bh_t * bh)
{
    if(!opt_nowrite) { 
	reiserfs_buffer_mkuptodate(bh,1);
	reiserfs_buffer_mkdirty(bh);
	reiserfs_buffer_write(bh);
    }
    return 0;
}


/* the first one of the most important functions */
static int expand_fs (reiserfs_filsys_t * fs, long long int block_count_new) {
    unsigned int bmap_nr_new, bmap_nr_old;
    unsigned int block_count_old;
    reiserfs_sb_t * sb;
    unsigned int i;


    reiserfs_fs_reopen(fs, O_RDWR);
    if (reiserfs_bitmap_open(fs))
	reiserfs_exit(1, "cannot open ondisk bitmap");

    sb = fs->fs_ondisk_sb;

    reiserfs_sb_set_state (fs->fs_ondisk_sb, FS_ERROR);

    bwrite_cond(fs->fs_super_bh);


    if (reiserfs_bitmap_expand (fs->fs_bitmap2, block_count_new))
	reiserfs_exit(1, "cannot expand bitmap\n");


    /* count bitmap blocks in new fs */
    block_count_old = reiserfs_sb_get_blocks(sb);
    bmap_nr_old = reiserfs_bmap_nr(block_count_old, fs->fs_blocksize);
    bmap_nr_new = reiserfs_bmap_nr(block_count_new, fs->fs_blocksize);

    /* update super block buffer*/
    reiserfs_sb_set_free (sb, reiserfs_sb_get_free(sb) + 
			(block_count_new - reiserfs_sb_get_blocks(sb)) - 
			(bmap_nr_new - bmap_nr_old));
    reiserfs_sb_set_blocks (sb, block_count_new);
    reiserfs_sb_set_bmaps (sb, reiserfs_bmap_over(bmap_nr_new) ? 
			   0 : bmap_nr_new);

    /* mark new bitmap blocks as used */
    for (i = bmap_nr_old; i < bmap_nr_new; i++)
	reiserfs_bitmap_set_bit (fs->fs_bitmap2, i * fs->fs_blocksize * 8);

    /* normally, this is done by reiserfs_bitmap_set_bit, but if we
    ** haven't actually added any bitmap blocks, the bitmap won't be dirtied.
    **
    ** In memory, reiserfsprogs puts zeros for the bits past the end of
    ** the old filesystem.  But, on disk that bitmap is full of ones.  
    ** we explicitly dirty the bitmap here to make sure the zeros get written
    ** to disk
    */
    fs->fs_bitmap2->bm_dirty = 1 ;
    
    return 0;
}

static int resizer_check_fs_size(reiserfs_filsys_t *fs, long long int new_size) {
    if (new_size < 0) {
	    reiserfs_warning(stderr, "\nresizer_reiserfs: the new size "
			     "value is wrong.\n\n");
	    return new_size;
    }
    
    if (new_size == reiserfs_sb_get_blocks(fs->fs_ondisk_sb)) {
	reiserfs_warning (stderr, "%s already is of the needed size. "
			  "Nothing to be done\n\n", fs->fs_file_name);
	return 1;
    }

    if (new_size < reiserfs_sb_get_blocks(fs->fs_ondisk_sb)) {
	if (util_device_mounted(fs->fs_file_name) > 0) {
	    reiserfs_warning (stderr, "Can't shrink filesystem on-line.\n\n");
	    return 1;
	}
    }

    if (new_size >= reiserfs_sb_get_blocks(fs->fs_ondisk_sb)) {
	unsigned long long offset = (unsigned long long)new_size * fs->fs_blocksize - 1;
	
	if(!misc_device_valid_offset(fs->fs_dev, offset)) {
	    reiserfs_warning (stderr, "%s is of %lu blocks size only with "
			      "reiserfs of %d blocks\nsize on it. You are "
			      "trying to expand reiserfs up to %lu blocks "
			      "size.\nYou probably forgot to expand your "
			      "partition size.\n\n", fs->fs_file_name,
			      misc_device_count_blocks(fs->fs_file_name, fs->fs_blocksize),
			      reiserfs_sb_get_blocks(fs->fs_ondisk_sb), new_size);
	    return 1;
	}
    }

    return 0;
}

int main(int argc, char *argv[]) {
    char * bytes_count_str = NULL;
    char * devname;
    char * jdevice_name = NULL;
    reiserfs_filsys_t * fs;
    reiserfs_sb_t * sb;

    int c;
    int error;

    reiserfs_sb_t *sb_old;

    long long int block_count_new;

    if (argc < 2)
	print_usage_and_exit();
	
    while ((c = getopt(argc, argv, "fvcqks:j:V")) != EOF) {
	switch (c) {
	case 's' :
	    if (!optarg)
		reiserfs_exit(1, "Missing argument to -s option");
	    bytes_count_str = optarg;
	    break;
	case 'j' :
	    if (!optarg) 
		reiserfs_exit(1, "Missing argument to -j option");
	    jdevice_name = optarg;
	case 'f':
	    opt_force = 1;
	    break;		 
	case 'v':
	    opt_verbose++; 
	    break;
	case 'n':
	    /* no nowrite option at this moment */
	    /* opt_nowrite = 1; */
	    break;
	case 'c':
	    opt_safe = 1;
	    break;
	case 'q':
	    opt_verbose = 0;
	    break;
	case 'k':
	    opt_skipj = 1;
	    break;
	case 'V':
	    opt_banner++;
	    break;
	default:
	    print_usage_and_exit ();
	}
    }

    util_print_banner (argv[0]);
    
    if (opt_banner)
	exit(0);
    

    devname = argv[optind];

    fs = reiserfs_fs_open(devname, O_RDONLY, &error, 0, 1);
    if (!fs) {
	if (error) {
		reiserfs_exit(1, "cannot open '%s': %s", 
			      devname, strerror(error));
        } else {
		exit(1);
	}
    }

    if (reiserfs_journal_open (fs, jdevice_name, O_RDWR 
#ifdef O_LARGEFILE
			       | O_LARGEFILE
#endif
			       )) 
    {
	reiserfs_exit(1, "Failed to open the journal device (%s).", 
		      jdevice_name);
    }
    
    if (reiserfs_journal_params_check(fs)) {
	if (!opt_skipj) {
	    reiserfs_exit(1, "Wrong journal parameters detected on (%s)", 
			  jdevice_name);
	} else {
	    reiserfs_journal_close(fs);
	}
    }

    /* forced to continue without journal available/specified */

    if (fs == NULL) {
	reiserfs_exit(1, "no reiserfs found on the device.");
    }
    
    if (!reiserfs_bitmap_spread (fs)) {
	reiserfs_exit(1, "cannot resize reiserfs in old (not spread "
		      "bitmap) format.");
    }

    sb = fs->fs_ondisk_sb;

    /* If size change was specified by user, calculate it, 
       otherwise take the whole device. */
    block_count_new = bytes_count_str ? 
	    calc_new_fs_size(reiserfs_sb_get_blocks(sb), 
			     fs->fs_blocksize, bytes_count_str) :
	    misc_device_count_blocks(devname, fs->fs_blocksize);

    if (resizer_check_fs_size(fs, block_count_new))
	return 1;

    if (util_device_mounted(devname) > 0) {
	reiserfs_fs_close(fs);
	fs = NULL;
	error = resize_fs_online(devname, block_count_new);
	reiserfs_warning(stderr, "\n\nresize_reiserfs: On-line resizing %s.\n\n",
			 error ? "failed" : "finished successfully");
	return error;
    }

    if (!reiserfs_sb_state_ok (fs)) {
	reiserfs_warning (stderr, "\n\nresize_reiserfs: run reiserfsck --check "
	    "first\n\n");
	reiserfs_fs_close (fs);
	fs = NULL;
	return 1;
    }

    if (reiserfs_sb_get_umount(sb) != FS_CLEANLY_UMOUNTED)
	/* fixme: shouldn't we check for something like: fsck guarantees: fs is ok */
	reiserfs_exit(1, "the file system isn't in valid state.");
		
    /* Needed to keep idiot compiler from issuing false warning */
    sb_old = 0;		
    
    /* save SB for reporting */
    if(opt_verbose) {
	sb_old = misc_getmem(REISERFS_SB_SIZE);
	memcpy(sb_old, fs->fs_ondisk_sb, REISERFS_SB_SIZE);
    }

    error = (block_count_new > reiserfs_sb_get_blocks(fs->fs_ondisk_sb)) ? 
	expand_fs(fs, block_count_new) : shrink_fs(fs, block_count_new);
    if (error) {
	reiserfs_warning(stderr, "\n\nresize_reiserfs: Resizing failed.\n\n ");
	return error;
    }

    if(opt_verbose) {
	sb_report(fs->fs_ondisk_sb, sb_old);
	misc_freemem(sb_old);
    }

    reiserfs_sb_set_state (fs->fs_ondisk_sb, FS_CONSISTENT);
    bwrite_cond(fs->fs_super_bh);
	
    if (opt_verbose) {
	printf("\nSyncing..");
	fflush(stdout);
    }
    reiserfs_fs_close (fs);
    if (opt_verbose)
	printf("done\n");
	
    reiserfs_warning(stderr, "\n\nresize_reiserfs: Resizing finished "
	"successfully.\n\n ");
    
    return 0;
}
