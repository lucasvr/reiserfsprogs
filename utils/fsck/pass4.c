/*
 * Copyright 1996-2004 by Hans Reiser, licensing governed by 
 * reiserfsprogs/README
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "fsck.h"
#include "util/misc.h"

static int fsck_squeeze(reiserfs_filsys_t *fs, reiserfs_path_t *path) {
	reiserfs_bh_t *bh_left, *bh;
	REISERFS_PATH_INIT(lpath);
	reiserfs_key_t key;
	__u64 off;
	__u32 oid;
	int res;
	
	bh = REISERFS_PATH_LEAF(path);
	
	key = *(reiserfs_ih_key_at(bh, 0));
	if ((off = reiserfs_key_get_off(&key)) == 0) {
		if ((oid = reiserfs_key_get_oid(&key)) == 0) 
			reiserfs_panic("Object id 0 found in the tree.");
		
		reiserfs_key_set_oid(&key, reiserfs_key_get_oid(&key) - 1);
		reiserfs_key_set_off(reiserfs_key_format(&key), &key, -1ull);
	} else {
		reiserfs_key_set_off(reiserfs_key_format(&key), &key, 
				     reiserfs_key_get_off(&key) - 1);
	}
	
	reiserfs_tree_search_item (fs, &key, &lpath);
	bh_left = REISERFS_PATH_LEAF (&lpath);
	
	if (bh->b_blocknr == bh_left->b_blocknr || 
	    !reiserfs_tree_node_mergeable(bh_left, bh))
	{
		reiserfs_tree_pathrelse(&lpath);
		return 0;
	}

	/* Mergeable leaves must be merged. */
	res = reiserfs_tree_merge(fs, &lpath, path);
	reiserfs_tree_pathrelse(&lpath);
	reiserfs_bitmap_clear_bit(fsck_new_bitmap (fs), bh->b_blocknr);
	return res;
}

void fsck_cleanup (void) {
    const reiserfs_key_t * rdkey;
    reiserfs_path_t path;
    reiserfs_key_t key;
    reiserfs_bh_t * bh;
    reiserfs_ih_t * ih;
    unsigned long items;
    int i, res;

    path.path_length = REISERFS_PATH_OFFILL;
    key = root_dir_key;
    
    fsck_progress ("Pass 4 - ");
    items = 0;

    while (1) {
	res = reiserfs_tree_search_item (fs, &key, &path);
	
	bh = REISERFS_PATH_LEAF (&path);
	ih = REISERFS_PATH_IH (&path);
	
	if (res != ITEM_FOUND) {
	    if (REISERFS_PATH_LEAF_POS (&path) == 0)
		    break;
	    
	    REISERFS_PATH_LEAF_POS (&path) --;
	    ih = REISERFS_PATH_IH (&path);

	    if (reiserfs_key_comp2(&key, &ih->ih_key))
		    break;
            
	    /* Position found. */
	    //fsck_log("Warning: Node %lu, item %u, %k: item start is expected, "
	    //	     "not a unit.\n", bh->b_blocknr, 
	    //	     REISERFS_PATH_LEAF_POS (&path), &ih->ih_key);
	}
	
	/* print ~ how many leaves were scanned and how fast it was */
	if (!fsck_quiet (fs))
	    util_misc_speed (fsck_progress_file(fs), items++, 0, 50, 0);

	for (i = REISERFS_PATH_LEAF_POS (&path); 
	     i < reiserfs_node_items (bh); i ++, ih ++)
	{
	    if (!fsck_item_reach(ih)) {
		REISERFS_PATH_LEAF_POS (&path) = i;
		rdkey = reiserfs_tree_next_key(&path, fs);
		if (rdkey)
		    key = *rdkey;
		else
		    memset (&key, 0xff, REISERFS_KEY_SIZE);
		
		pass_4_stat (fs)->deleted_items ++;
	
		/* fsck_log("Node %lu, item %u, %k: delete.\n", 
			 bh->b_blocknr, i, &ih->ih_key); */
		reiserfs_tree_delete (fs, &path, 0);
		goto cont;
	    }
	
	    if (reiserfs_ih_get_flags(ih) != 0) {
		reiserfs_ih_clflags(ih);
		reiserfs_buffer_mkdirty(bh);
	    }
	    
	    /* Merge mergeable items. */
	    if (i == 0)
		continue;
	    
	    if (reiserfs_ih_stat(ih))
		    continue;
	    
	    /* If not stat data and types match, try to merge items. */
	    if (reiserfs_key_get_type(&ih->ih_key) != 
		reiserfs_key_get_type(&(ih - 1)->ih_key))
	    {
		continue;
	    }
	    
	    if (reiserfs_ih_dir(ih) || 
		must_there_be_a_hole(ih - 1, &ih->ih_key) == 0)
	    {
		REISERFS_PATH_LEAF_POS (&path) = i;
		rdkey = reiserfs_tree_next_key(&path, fs);
		if (rdkey)
		    key = *rdkey;
		else
		    memset (&key, 0xff, REISERFS_KEY_SIZE);
		
		/* fsck_log ("%s: 2 items of the same type %k, %k are found "
			  "in the same node (%d), pos (%u). Merged.\n", 
			  __FUNCTION__, &ih->ih_key, &(ih + 1)->ih_key,
			  bh->b_blocknr, REISERFS_PATH_LEAF_POS(&path)); */
		fsck_tree_merge(&path);
		goto cont;
	    }
	}
	
	REISERFS_PATH_LEAF_POS(&path) = i - 1;
	rdkey = reiserfs_tree_next_key (&path, fs);
	if (rdkey)
	    key = *rdkey;
	else
	    memset (&key, 0xff, REISERFS_KEY_SIZE);	
	
	fsck_squeeze(fs, &path);
	
	reiserfs_tree_pathrelse (&path);
    cont:
	/* to make gcc 3.2 do not sware here */;
    }

    reiserfs_tree_pathrelse (&path);

    fsck_progress ("finished\n");
    fsck_stage_report (FS_CLEANUP, fs);

    /* after pass 4 */

    /* put bitmap on place */
    reiserfs_bitmap_copy (fs->fs_bitmap2, fsck_new_bitmap (fs));

    /* update super block */
    reiserfs_sb_set_free (fs->fs_ondisk_sb, 
			  reiserfs_bitmap_zeros (fsck_new_bitmap (fs)));
    
    reiserfs_buffer_mkdirty (fs->fs_super_bh);

    /* write all dirty blocks */
    fsck_progress ("Flushing..");
    fs->fs_dirt = 1;
    reiserfs_bitmap_flush (fs->fs_bitmap2, fs);
    reiserfs_fs_flush (fs);
    fsck_progress ("finished\n");

    
    return;
}
