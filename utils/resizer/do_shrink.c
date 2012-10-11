/*
 * Copyright 2000-2004 by Hans Reiser, licensing governed by 
 * reiserfsprogs/README
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "resize.h"
#include "misc/unaligned.h"
#include "util/misc.h"

#include <time.h>
#include <fcntl.h>

static unsigned long int_node_cnt   = 0, int_moved_cnt   = 0;
static unsigned long leaf_node_cnt  = 0, leaf_moved_cnt  = 0;
static unsigned long unfm_node_cnt  = 0, unfm_moved_cnt  = 0;
static unsigned long total_node_cnt = 0;
static unsigned long total_moved_cnt = 0;

static unsigned long unused_block;
static unsigned long blocks_used;
static int block_count_mismatch = 0;

static reiserfs_bitmap_t * bmp;
static reiserfs_sb_t * ondisk_sb;

/* abnornal exit from block reallocation process */
static void quit_resizer(reiserfs_filsys_t * fs)
{
	/* save changes to bitmap blocks */
	reiserfs_fs_close (fs);
	/* leave fs in ERROR state */
	reiserfs_exit(1, "fs shrinking was not completed successfully, "
		      "run reiserfsck.");
}

/* block moving */
static unsigned long move_generic_block(reiserfs_filsys_t * fs, 
					unsigned long block, 
					unsigned long bnd, int h)
{
    reiserfs_bh_t * bh, * bh2;

	/* primitive fsck */
	if (block > reiserfs_sb_get_blocks(ondisk_sb)) {
		fprintf(stderr, "resize_reiserfs: invalid block number "
			"(%lu) found.\n", block);
		quit_resizer(fs);
	}
	
	/* progress bar, 3D style :) */
	if (opt_verbose)
	    util_misc_progress(stderr, &total_node_cnt, blocks_used, 1, 0);
	else
	    total_node_cnt ++;

	/* infinite loop check */
	if( total_node_cnt > blocks_used && !block_count_mismatch) {
		fputs("resize_reiserfs: warning: block count exeeded\n",stderr);
		block_count_mismatch = 1;
	}

	if (block < bnd) /* block will not be moved */
		return 0;
	
	/* move wrong block */ 
	bh = reiserfs_buffer_read(fs->fs_dev, block, fs->fs_blocksize);

	if (!bh)
	    reiserfs_exit (1, "move_generic_block: reiserfs_buffer_read failed.\n");

	reiserfs_bitmap_find_zero_bit(bmp, &unused_block);
	if (unused_block == 0 || unused_block >= bnd) {
		fputs ("resize_reiserfs: can\'t find free block\n", stderr);
		quit_resizer(fs);
	}

	/* blocknr changing */
	bh2 = reiserfs_buffer_open(fs->fs_dev, unused_block, fs->fs_blocksize);
	memcpy(bh2->b_data, bh->b_data, bh2->b_size);
	reiserfs_bitmap_clear_bit(bmp, block);
	reiserfs_bitmap_set_bit(bmp, unused_block);

	reiserfs_buffer_close(bh);
	reiserfs_buffer_mkuptodate(bh2,1);
	reiserfs_buffer_mkdirty(bh2);
	reiserfs_buffer_write(bh2);
	reiserfs_buffer_close(bh2);

	total_moved_cnt++;
	return unused_block;
}

static unsigned long move_unformatted_block(reiserfs_filsys_t * fs, unsigned long block, unsigned long bnd, int h)
{
	unsigned long b;
	unfm_node_cnt++;
	b = move_generic_block(fs, block, bnd, h);
	if (b)
		unfm_moved_cnt++;
	return b;		
}


/* recursive function processing all tree nodes */
static unsigned long move_formatted_block(reiserfs_filsys_t * fs, 
					  unsigned long block, 
					  unsigned long bnd, int h)
{
    reiserfs_bh_t * bh;
    reiserfs_ih_t *ih;
    unsigned long new_blocknr = 0;
    int node_is_internal = 0;
    unsigned int i, j;
	
    bh = reiserfs_buffer_read(fs->fs_dev, block, fs->fs_blocksize);
    
    if (!bh)
	reiserfs_exit (1, "move_formatted_block: reiserfs_buffer_read failed");

    if (reiserfs_leaf_head (bh)) {
	leaf_node_cnt++;

	for (i = 0; i < reiserfs_node_items(bh); i++) {
	    ih = reiserfs_ih_at(bh, i);

	    /* skip the bad blocks. */
	    if (reiserfs_key_get_oid (&ih->ih_key) == REISERFS_BAD_OID &&
		reiserfs_key_get_did (&ih->ih_key) == REISERFS_BAD_DID)
	    {
		continue;
	    }

	    if (reiserfs_ih_ext(ih)) {
		__u32 * extent;

		extent = (__u32 *)reiserfs_item_by_ih (bh, ih);
		for (j = 0; j < reiserfs_ext_count(ih); j++) {
		    unsigned long  unfm_block;

		    /* hole */
		    if (d32_get (extent, j) == 0) 
			continue;

		    unfm_block = move_unformatted_block(fs, d32_get (extent, j),
							bnd, h + 1);
		    if (unfm_block) {
			d32_put (extent, j, unfm_block);
			reiserfs_buffer_mkdirty(bh);
		    }
		}
	    }	
	}
    } else if (reiserfs_int_head (bh)) { /* internal node */
	int_node_cnt++;
	node_is_internal = 1;

	for (i=0; i <= reiserfs_node_items(bh); i++) {
	    unsigned long moved_block;
	    moved_block = 
		    move_formatted_block(fs, reiserfs_dc_get_nr(
				reiserfs_int_at (bh, i)), bnd, h+1);
	    if (moved_block) {
		reiserfs_dc_set_nr (reiserfs_int_at (bh, i), 
				      moved_block);
		reiserfs_buffer_mkdirty(bh);
	    }
	}
    } else {
	misc_die("Block (%lu) has invalid format\n", block);
    }

    if (reiserfs_buffer_isdirty(bh)) {
	reiserfs_buffer_mkuptodate(bh,1);
	reiserfs_buffer_write(bh);
    }

    reiserfs_buffer_close(bh);	

    new_blocknr = move_generic_block(fs, block, bnd, h);
    if (new_blocknr) {
	if (node_is_internal)
	    int_moved_cnt++;
	else
	    leaf_moved_cnt++;
    }

    return new_blocknr;
}

int shrink_fs(reiserfs_filsys_t * fs, long long int new_blocks)
{
	unsigned long n_root_block;
	unsigned int bmap_nr_new;
	unsigned int bmap_nr_old;
	unsigned long bad_count;
	unsigned long old_blocks;
	
	ondisk_sb = fs->fs_ondisk_sb;
	
	old_blocks = reiserfs_sb_get_blocks(ondisk_sb);
	bmap_nr_old = reiserfs_bmap_nr(old_blocks, fs->fs_blocksize);
	bmap_nr_new = reiserfs_bmap_nr(new_blocks, fs->fs_blocksize);

	/* is shrinking possible ? */
	if (old_blocks - new_blocks > 
	    reiserfs_sb_get_free(ondisk_sb) + bmap_nr_old - bmap_nr_new) 
	{
	    fprintf(stderr, "resize_reiserfs: can\'t shrink fs; too many "
		"blocks already allocated\n");
	    return -1;
	}

	/* warn about alpha version */
	{
		int c;

		printf(
		    "You are running BETA version of reiserfs shrinker.\n"
		    "This version is only for testing or VERY CAREFUL use.\n"
		    "Backup of you data is recommended.\n\n"
		    "Do you want to continue? [y/N]:"
		);
		fflush(stdout);
		c = getchar();
		if (c != 'y' && c != 'Y')
			exit(1);
	}

	reiserfs_fs_reopen(fs, O_RDWR);
	if (reiserfs_bitmap_open(fs))
	    reiserfs_exit(1, "cannot open ondisk bitmap");
	bmp = fs->fs_bitmap2;
	ondisk_sb = fs->fs_ondisk_sb;

	reiserfs_sb_set_state (fs->fs_ondisk_sb, FS_ERROR);
	reiserfs_buffer_mkuptodate(fs->fs_super_bh, 1);
	reiserfs_buffer_mkdirty(fs->fs_super_bh);
	reiserfs_buffer_write(fs->fs_super_bh);

	/* calculate number of data blocks */		
	blocks_used = 
	    old_blocks
	    - reiserfs_sb_get_free(fs->fs_ondisk_sb)
	    - bmap_nr_old
	    - reiserfs_jp_get_size(reiserfs_sb_jp (fs->fs_ondisk_sb))
	    - REISERFS_DISK_OFFSET_IN_BYTES / fs->fs_blocksize
	    - 2; /* superblock itself and 1 descriptor after the journal */

	unused_block = 1;

	if (opt_verbose) {
		printf("Processing the tree: ");
		fflush(stdout);
	}

	n_root_block = move_formatted_block(fs, reiserfs_sb_get_root(ondisk_sb), 
	    new_blocks, 0);
	
	if (n_root_block)
	    reiserfs_sb_set_root (ondisk_sb, n_root_block);

	if (opt_verbose)
	    printf ("\n\nnodes processed (moved):\n"
		    "int        %lu (%lu),\n"
		    "leaves     %lu (%lu),\n" 
		    "unfm       %lu (%lu),\n"
		    "total      %lu (%lu).\n\n",
		    int_node_cnt, int_moved_cnt,
		    leaf_node_cnt, leaf_moved_cnt, 
		    unfm_node_cnt, unfm_moved_cnt,
		    (unsigned long)total_node_cnt, total_moved_cnt);

	if (block_count_mismatch) {
	    fprintf(stderr, "resize_reiserfs: data block count %lu"
		    " doesn\'t match data block count %lu from super block\n",
		    (unsigned long)total_node_cnt, blocks_used);
	}

	{
	    unsigned long l;

	    /* make sure that none of truncated block are in use */
	    printf("check for used blocks in truncated region\n");
	    for (l = new_blocks; l < fs->fs_bitmap2->bm_bit_size; l ++) {
		if ((l % (fs->fs_blocksize * 8)) == 0)
		    continue;
		if (reiserfs_bitmap_test_bit (fs->fs_bitmap2, l))
		    printf ("<%lu>", l);
	    }
	    printf("\n");
	}

	reiserfs_badblock_traverse(fs, reiserfs_badblock_extract, NULL);
	
	if (fs->fs_badblocks_bm) {
		bad_count = reiserfs_bitmap_ones(fs->fs_badblocks_bm);
		reiserfs_bitmap_shrink (fs->fs_badblocks_bm, new_blocks);
		reiserfs_badblock_flush(fs, 1);
		bad_count -= reiserfs_bitmap_ones(fs->fs_badblocks_bm);
	} else
		bad_count = 0;
	
	reiserfs_bitmap_shrink (fs->fs_bitmap2, new_blocks);

	reiserfs_sb_set_free (ondisk_sb, reiserfs_sb_get_free(ondisk_sb)
			    - (old_blocks - new_blocks)
			    + (bmap_nr_old - bmap_nr_new)
			    + bad_count);
	reiserfs_sb_set_blocks (ondisk_sb, new_blocks);
	reiserfs_sb_set_bmaps (ondisk_sb, bmap_nr_new);

	return 0;
}
