/*
 * Copyright 1996-2004 by Hans Reiser, licensing governed by 
 * reiserfsprogs/README
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "fsck.h"
#include "misc/malloc.h"
#include "util/device.h"
#include "util/misc.h"

#include <time.h>

/* on pass2 we take leaves which could not be inserted into tree
   during pass1 and insert each item separately. It is possible that
   items of different objects with the same key can be found. We treat
   that in the following way: we put it into tree with new key and
   link it into /lost+found directory with name made of dir,oid. When
   coming item is a directory - we delete object from the tree, put it
   back with different key, link it to /lost+found directory and
   insert directory as it is */

static void fsck_pass2_save_result (reiserfs_filsys_t * fs) {
    FILE * file;
    int retval;

    file = util_file_open("temp_fsck_file.deleteme", "w+");
    if (!file)
	return;
    
    /* to be able to restart from semantic we do not need to save
       anything here, but two magic values */
    fsck_stage_start_put (file, TREE_IS_BUILT);
    fsck_stage_end_put (file);
    fclose (file);
    retval = rename ("temp_fsck_file.deleteme", state_dump_file (fs));
    if (retval != 0)
	fsck_progress ("%s: Could not rename the temporary file "
		       "temp_fsck_file.deleteme to %s",
		        __FUNCTION__, state_dump_file (fs));
}

/* we have nothing to load from a state file, but we have to fetch
   on-disk bitmap, copy it to allocable bitmap, and fetch objectid
   map */
void fsck_pass2_load_result (reiserfs_filsys_t * fs) {
    fsck_new_bitmap (fs) = 
	    reiserfs_bitmap_create (reiserfs_sb_get_blocks (fs->fs_ondisk_sb));
    reiserfs_bitmap_copy (fsck_new_bitmap (fs), fs->fs_bitmap2);
    
    fsck_allocable_bitmap (fs) = 
	    reiserfs_bitmap_create (reiserfs_sb_get_blocks (fs->fs_ondisk_sb));
    reiserfs_bitmap_copy (fsck_allocable_bitmap (fs), fs->fs_bitmap2);

    fs->block_allocator = reiserfsck_new_blocknrs;
    fs->block_deallocator = reiserfsck_free_block;

    /* we need objectid map on semantic pass to be able to relocate files */
    proper_id_map (fs) = id_map_init();
    fetch_objectid_map (proper_id_map (fs), fs);    
}

#include <unistd.h>

/* uninsertable blocks are marked by 0s in uninsertable_leaf_bitmap
   during the pass 1. They must be not in the tree */
static void do_pass_2 (reiserfs_filsys_t * fs) {

    reiserfs_bh_t * bh;
    reiserfs_ih_t * ih;
    unsigned long total;
    unsigned long done;
    unsigned long b;
    int i, n, ntype;

    done = 0;
    if (!(total = reiserfs_bitmap_zeros(fsck_uninsertables(fs)) * 2))
	return;

    fsck_progress ("Pass 2:\n");

    /* 2 loops for SD items and others. */
    for (n = 0; n < 2; n++) {
        b = 0;
        while ((b < fsck_uninsertables(fs)->bm_bit_size) && 
	       reiserfs_bitmap_find_zero_bit(fsck_uninsertables(fs), &b) == 0) 
	{
	    bh = reiserfs_buffer_read (fs->fs_dev, b, fs->fs_blocksize);
	    if (bh == 0) {
	        fsck_log ("pass_2: Reading of the block (%lu) failed on "
			  "the device 0x%x\n", b, fs->fs_dev);
                goto cont;
            }
	
            if (is_block_used (bh->b_blocknr) && 
		!(reiserfs_journal_block (fs, bh->b_blocknr) &&
		  fsck_data(fs)->rebuild.use_journal_area)) 
	    {
		fsck_log("%s: The block (%lu) is in the tree already. "
			 "Should not happen.\n", __FUNCTION__, bh->b_blocknr);
		goto cont;
            }
	    
            /* this must be leaf */
            ntype = reiserfs_node_type (bh);
	    if (ntype != NT_LEAF) { // || B_IS_KEYS_LEVEL(bh)) {
	        fsck_log ("%s: The block (%b), marked as a leaf on "
			  "the first two passes, is not a leaf! Will "
			  "be skipped.\n", __FUNCTION__, bh);
	        goto cont;
	    }

	    /* Item-by-item loop. */
	    for (i = 0, ih = reiserfs_ih_at (bh, 0); 
		 i < reiserfs_node_items (bh); i ++, ih ++)
	    {
		/* Only SD items are inserted initially. */
		if (n == 0 && !reiserfs_ih_stat (ih))
		    continue;
		
		/* All other items are inserted later. */
		if (n && reiserfs_ih_stat (ih))
		    continue;
		
		if (fsck_leaf_item_check (bh, ih, reiserfs_item_by_ih (bh, ih)))
		    continue;
		    
		fsck_tree_insert_item (ih, reiserfs_item_by_ih (bh, ih), 1);
	    }

	    if (n) {
		pass_2_stat (fs)->leaves ++;
		make_allocable (b);
	    }

	    if (!fsck_quiet(fs)) {
		util_misc_progress (fsck_progress_file (fs), 
				    &done, total, 1, 0);
	    }
        cont:
	    reiserfs_buffer_close (bh);
	    b++;
        }   
    }

    if (!fsck_quiet(fs))	
	fsck_progress ("\n");
}


static void fsck_pass2_fini (reiserfs_filsys_t * fs) {
    time_t t;

    /* we can now flush new_bitmap on disk as tree is built and 
       contains all data, which were found on dik at start in 
       used bitmaps */
    reiserfs_bitmap_copy (fs->fs_bitmap2, fsck_new_bitmap (fs));
    
    /* we should copy new_bitmap to allocable bitmap, becuase evth what is 
       used for now (marked as used in new_bitmap) should not be allocable;
       and what is not in tree for now should be allocable.
       these bitmaps differ because on pass2 we skip those blocks, whose 
       SD's are not in the tree, and therefore extent items of such bad 
       leaves points to not used and not allocable blocks. */

    /* DEBUG only
    if (reiserfs_bitmap_compare (fsck_allocable_bitmap (fs), 
				 fsck_new_bitmap(fs))) 
    {
        fsck_log ("Allocable bitmap differs from the new bitmap after pass2\n");
	reiserfs_bitmap_copy (fsck_allocable_bitmap(fs), fsck_new_bitmap (fs));
    } */

    /* update super block: objectid map, fsck state */
    reiserfs_sb_set_state (fs->fs_ondisk_sb, TREE_IS_BUILT);
    reiserfs_buffer_mkdirty (fs->fs_super_bh);
  
    /* write all dirty blocks */
    fsck_progress ("Flushing..");
    id_map_flush(proper_id_map (fs), fs);
    fs->fs_dirt = 1;
    reiserfs_bitmap_flush (fs->fs_bitmap2, fs);
    reiserfs_fs_flush (fs);
    fsck_progress ("finished\n");
    
    /* fixme: should be optional */
/*    fsck_progress ("Tree is built. Checking it - ");
    reiserfsck_check_pass1 ();
    fsck_progress ("finished\n");*/

    fsck_stage_report (FS_PASS2, fs);

    /* free what we do not need anymore */
    reiserfs_bitmap_delete (fsck_uninsertables (fs));

    if (!fsck_run_one_step (fs)) {
	if (fsck_info_ask (fs, "Continue? (Yes):", "Yes\n", 1))
	    /* reiserfsck continues */
	    return;
    } else
	fsck_pass2_save_result (fs);

    
    id_map_free(proper_id_map (fs));
    proper_id_map (fs) = 0;
    
    reiserfs_bitmap_delete (fsck_new_bitmap (fs));
    reiserfs_bitmap_delete (fsck_allocable_bitmap (fs));
    
    time (&t);
    fsck_progress ("###########\n"
		   "reiserfsck finished pass 2 at %s"
		   "###########\n", ctime (&t));
    fs->fs_dirt = 1;
    reiserfs_fs_close (fs);
    exit(0);
}



void fsck_pass2 (reiserfs_filsys_t * fs) {
    if (fsck_log_file (fs) != stderr)
	fsck_log ("####### Pass 2 #######\n");
    
    /* take blocks which were not inserted into tree yet and put each
	item separately */
    do_pass_2 (fs);
    
    fsck_pass2_fini (fs);

    if (reiserfs_sb_get_root (fs->fs_ondisk_sb) == ~(__u32)0 || 
	reiserfs_sb_get_root (fs->fs_ondisk_sb) == 0)
	misc_die ( "\nNo reiserfs metadata found.  If you are sure that you had the reiserfs\n"
		"on this partition,  then the start  of the partition  might be changed\n"
		"or all data were wiped out. The start of the partition may get changed\n"
		"by a partitioner  if you have used one.  Then you probably rebuilt the\n"
		"superblock as there was no one.  Zero the block at 64K offset from the\n"
		"start of the partition (a new super block you have just built) and try\n"
	        "to move the start of the partition a few cylinders aside  and check if\n" 
		"debugreiserfs /dev/xxx detects a reiserfs super block. If it does this\n"
	        "is likely to be the right super block version.                        \n"
		"If this makes you nervous, try  www.namesys.com/support.html,  and for\n"
		"$25 the author of fsck,  or a colleague  if he is out,  will  step you\n"
		"through it all.\n");
}
