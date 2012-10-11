/* 
 * Copyright 2000-2002 by Hans Reiser, licensing governed by reiserfs/README
 */
 
/*  
 * Written by Alexander Zarochentcev.
 * 
 * FS resize utility 
 *
 */

#include "resize.h"
#include <libgen.h>

int opt_force = 0;
int opt_verbose = 1;			/* now "verbose" option is default */
int opt_nowrite = 0;
int opt_safe = 0;
int opt_skipj = 0;

char * g_progname;

/* calculate the new fs size (in blocks) from old fs size and the string
   representation of new size */
static unsigned long calc_new_fs_size(unsigned long count, int bs,
				      char *bytes_str)
{
    long long int bytes;
    unsigned long blocks;
    int c;
	
    bytes = atoll(bytes_str);
    c = bytes_str[strlen(bytes_str) - 1];

    switch (c) {
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

    if (bytes_str[0] == '+' || bytes_str[0] == '-')
	return (count + blocks);

    return blocks;
}

/* print some fs parameters */
static void sb_report(struct reiserfs_super_block * sb1,
		      struct reiserfs_super_block * sb2)
{
    printf(
	"ReiserFS report:\n"
	"blocksize             %d\n"
	"block count           %d (%d)\n"
	"free blocks           %d (%d)\n"
	"bitmap block count    %d (%d)\n", 
	get_sb_block_size(sb1),
	get_sb_block_count(sb1), get_sb_block_count(sb2),
	get_sb_free_blocks(sb1), get_sb_free_blocks(sb2),
	get_sb_bmap_nr(sb1), get_sb_bmap_nr(sb2));
};

/* conditional bwrite */
static int bwrite_cond (struct buffer_head * bh)
{
    if(!opt_nowrite) { 
	mark_buffer_uptodate(bh,1);
	mark_buffer_dirty(bh);
	bwrite(bh);
    }
    return 0;
}


/* the first one of the most important functions */
static int expand_fs (reiserfs_filsys_t * fs, unsigned long block_count_new) {
    unsigned int bmap_nr_new, bmap_nr_old;
    int i;
    struct reiserfs_super_block * sb;


    reiserfs_reopen(fs, O_RDWR);
    if (!reiserfs_open_ondisk_bitmap (fs))
	DIE("cannot open ondisk bitmap");

    sb = fs->fs_ondisk_sb;

    set_sb_fs_state (fs->fs_ondisk_sb, REISERFS_CORRUPTED);

    bwrite_cond(fs->fs_super_bh);


    if (reiserfs_expand_bitmap(fs->fs_bitmap2, block_count_new))
	DIE("cannot expand bitmap\n");


    /* count bitmap blocks in new fs */
    bmap_nr_new = (block_count_new - 1) / (fs->fs_blocksize * 8) + 1;
    bmap_nr_old = get_sb_bmap_nr(sb);
	
    /* update super block buffer*/
    set_sb_free_blocks (sb, get_sb_free_blocks(sb) + 
			(block_count_new - get_sb_block_count(sb)) - 
			(bmap_nr_new - bmap_nr_old));
    set_sb_block_count (sb, block_count_new);
    set_sb_bmap_nr (sb, bmap_nr_new);

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


int main(int argc, char *argv[]) {
    char * bytes_count_str = NULL;
    char * devname;
    char * jdevice_name = NULL;
    reiserfs_filsys_t * fs;
    struct reiserfs_super_block * sb;

    int c;
    int error;

    struct reiserfs_super_block *sb_old;

    unsigned long block_count_new;

    g_progname = basename(argv[0]);
    print_banner (g_progname);
	
    while ((c = getopt(argc, argv, "fvcqks:j:")) != EOF) {
	switch (c) {
	case 's' :
	    if (!optarg)
		DIE("Missing argument to -s option");
	    bytes_count_str = optarg;
	    break;
	case 'j' :
	    if (!optarg) 
		DIE("Missing argument to -j option");
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
	default:
	    print_usage_and_exit ();
	}
    }

    if (optind == argc )
	print_usage_and_exit();
    devname = argv[optind];

    fs = reiserfs_open(devname, O_RDONLY, &error, 0);
    if (!fs)
	DIE ("cannot open '%s': %s", devname, strerror(error));
    if (!reiserfs_open_journal (fs, jdevice_name, O_RDONLY)) {
	if (!opt_skipj)
	    DIE ("can not open journal of '%s'", devname);
    }

    /* forced to continue without journal available/specified */

    if (no_reiserfs_found (fs)) {
	DIE ("no reiserfs found on the device.");
    }
    if (!spread_bitmaps (fs)) {
	DIE ("cannot resize reiserfs in old (not spread bitmap) format.");
    }

    sb = fs->fs_ondisk_sb;
	
    if(bytes_count_str) {	/* new fs size is specified by user */
	block_count_new = calc_new_fs_size(get_sb_block_count(sb), fs->fs_blocksize, bytes_count_str);
    } else {		/* use whole device */
	block_count_new = count_blocks(devname, fs->fs_blocksize);
    }

    if (is_mounted (devname)) {
	reiserfs_close(fs);
	return resize_fs_online(devname, block_count_new);
    }

    if (!reiserfs_is_fs_consistent (fs)) {
	reiserfs_warning (stderr, "\n\nresize_reiserfs: run reiserfsck --check first\n\n");
	reiserfs_close (fs);
	return 1;
    }

    if (get_sb_umount_state(sb) != REISERFS_CLEANLY_UMOUNTED)
	/* fixme: shouldn't we check for something like: fsck guarantees: fs is ok */
	DIE ("the file system isn't in valid state.");
		
    if (block_count_new >= get_sb_block_count(sb)) {
	if (block_count_new == get_sb_block_count(sb)) {
	    reiserfs_warning (stderr, "%s already is of the needed size. Nothing to be done\n\n", devname);
	    exit (1);
	}
	
	if(!valid_offset(fs->fs_dev, (loff_t) block_count_new * fs->fs_blocksize - 1)) {
	    reiserfs_warning (stderr, "%s is of %lu blocks size only with reiserfs of %d blocks\nsize on it. "\
		"You are trying to expant reiserfs up to %lu blocks size.\nYou probably forgot to expand your "\
		"partition size.\n\n", devname, count_blocks (devname, fs->fs_blocksize),
		get_sb_block_count(sb), block_count_new);
	    exit (1);
	}
    }


    sb_old = 0;		/* Needed to keep idiot compiler from issuing false warning */
    /* save SB for reporting */
    if(opt_verbose) {
	sb_old = getmem(SB_SIZE);
	memcpy(sb_old, fs->fs_ondisk_sb, SB_SIZE);
    }

    error = (block_count_new > get_sb_block_count(fs->fs_ondisk_sb)) ? expand_fs(fs, block_count_new) : shrink_fs(fs, block_count_new);
    if (error)
	return error;

    if(opt_verbose) {
	sb_report(fs->fs_ondisk_sb, sb_old);
	freemem(sb_old);
    }

    set_sb_fs_state (fs->fs_ondisk_sb, REISERFS_CONSISTENT);
    bwrite_cond(fs->fs_super_bh);
	
    if (opt_verbose) {
	printf("\nSyncing..");
	fflush(stdout);
    }
    reiserfs_close (fs);
    if (opt_verbose)
	printf("done\n");
	
    return 0;
}
