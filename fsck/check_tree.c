/*
 * Copyright 1999 Hans Reiser
 */

#include "fsck.h"

struct check_relocated {
    __u32 old_dir_id;
    __u32 old_objectid;
    /*mode_t mode;*/
    struct check_relocated * next;
};

struct check_relocated * relocated_list;

void to_be_relocated (struct key * key)
{
    struct check_relocated * cur, * prev, * new_relocated;

    cur = relocated_list;
    prev = 0;

    while (cur && comp_short_keys(key, (struct key *)cur) != 1) {
	if (comp_short_keys (key, (struct key *)cur) == 0)
	    return;
	prev = cur;
        cur = cur->next;
    }
        
    new_relocated = getmem (sizeof (struct check_relocated));
    copy_short_key ((struct key *)new_relocated, key);
             
    if (prev) {
        new_relocated->next = prev->next;
        prev->next = new_relocated;
    } else {
        new_relocated->next = 0;
        relocated_list = new_relocated;
    }
}

int should_be_relocated (struct key * key)
{
    struct check_relocated * cur, * next;
    int ret = 0;

    cur = relocated_list;

    while (cur && comp_short_keys(key, (struct key *)cur) != 1) {
	if (comp_short_keys (key, (struct key *)cur) == 0) {
	    ret = 1;
	    break;
	}
        cur = cur->next;
    }
        
    while (relocated_list != cur) {
        next = relocated_list->next;
        freemem (relocated_list);
        relocated_list = next;
    }
    return ret;    
}

//
//
//  check S+ tree of the file system 
//
// check_fs_tree stops and recommends to run fsck --rebuild-tree when:
// 1. read fails
// 2. node of wrong level found in the tree
// 3. something in the tree points to wrong block number
//      out of filesystem boundary is pointed by tree
//      to block marked as free in bitmap
//      the same block is pointed from more than one place
//      not data blocks (journal area, super block, bitmaps)
// 4. bad formatted node found
// 5. delimiting keys are incorrect
//      



/* mark every block we see in the tree in control bitmap, so, when to make
   sure, that no blocks are pointed to from more than one place we use
   additional bitmap (control_bitmap). If we see pointer to a block we set
   corresponding bit to 1. If it is set already - run fsck with --rebuild-tree */
static reiserfs_bitmap_t * control_bitmap;
static reiserfs_bitmap_t * source_bitmap;

static int tree_scanning_failed = 0;


/* 1 if block is not marked as used in the bitmap */
static int is_block_free (reiserfs_filsys_t * fs, unsigned long block)
{
    return !reiserfs_bitmap_test_bit (source_bitmap, block);
}


/*static int hits = 0;*/

/* we have seen this block in the tree, mark corresponding bit in the
   control bitmap */
static void we_met_it (unsigned long block)
{
    reiserfs_bitmap_set_bit (control_bitmap, block);
    /*hits ++;*/
}


/* have we seen this block somewhere in the tree before? */
static int did_we_meet_it (unsigned long block)
{
    return reiserfs_bitmap_test_bit (control_bitmap, block);
}


static void init_control_bitmap (reiserfs_filsys_t * fs)
{
    int i;
    unsigned long block;
    unsigned long reserved;
    

    control_bitmap = reiserfs_create_bitmap (get_sb_block_count (fs->fs_ondisk_sb));
    if (!control_bitmap)
	die ("init_control_bitmap: could not create control bitmap");

    /*printf ("Initially number of zeros in control bitmap %d\n", reiserfs_bitmap_zeros (control_bitmap));*/

    /* skipped and super block */
    for (i = 0; i <= fs->fs_super_bh->b_blocknr; i ++)
    	we_met_it (i);

    /*printf ("SKIPPED: %d blocks marked used (%d)\n", hits, reiserfs_bitmap_zeros (control_bitmap));
      hits = 0;*/

    /* bitmaps */
    block = fs->fs_super_bh->b_blocknr + 1;
    for (i = 0; i < get_sb_bmap_nr (fs->fs_ondisk_sb); i ++) {
        we_met_it (block);

	if (spread_bitmaps (fs))
	    block = (block / (fs->fs_blocksize * 8) + 1) * (fs->fs_blocksize * 8);
	else
	    block ++;	
    }
    /*printf ("BITMAPS: %d blocks marked used (%d)\n", hits, reiserfs_bitmap_zeros (control_bitmap));
      hits = 0;*/
    
    /* mark as used area of the main device either containing a journal or
       reserved to hold it */

    reserved = get_size_of_journal_or_reserved_area (fs->fs_ondisk_sb);
    
    /* where does journal area (or reserved journal area) start from */

    if (!is_new_sb_location (fs->fs_super_bh->b_blocknr, fs->fs_blocksize) &&
    	!is_old_sb_location (fs->fs_super_bh->b_blocknr, fs->fs_blocksize))
	die ("init_control_bitmap: wrong super block");

    block = get_journal_start_must (fs);

    for (i = block; i < reserved + block; i ++)
	we_met_it (i);

    if (fs->fs_badblocks_bm)
	for (i = 0; i < get_sb_block_count (fs->fs_ondisk_sb); i ++) {
	    if (reiserfs_bitmap_test_bit (fs->fs_badblocks_bm, i))
	    	we_met_it (i);	
	}
}


/* if we managed to complete tree scanning and if control bitmap and/or proper
   amount of free blocks mismatch with bitmap on disk and super block's
   s_free_blocks - we can fix that */
static void handle_bitmaps (reiserfs_filsys_t * fs)
{
    int diff, problem = 0;

    if (tree_scanning_failed) {
	fsck_progress ("Could not scan whole tree. "
		       "--rebuild-tree is required\n");
	return;
    }

    fsck_progress ("Comparing bitmaps..");

    /* check free block counter */
    if (get_sb_free_blocks (fs->fs_ondisk_sb) != reiserfs_bitmap_zeros (control_bitmap)) {
	fsck_log ("free block count %lu mismatches with a correct one %lu. \n",
		  get_sb_free_blocks (fs->fs_ondisk_sb),
		  reiserfs_bitmap_zeros (control_bitmap));
	problem++;
//	one_more_corruption (fs, fatal); /* for now */
    }

    diff = reiserfs_bitmap_compare (source_bitmap, control_bitmap);
    if (diff) {
	fsck_log ("on-disk bitmap does not match to the correct one. \n", diff);
	problem++;
    }

    if (problem) {
        if (fsck_mode (fs) == FSCK_FIX_FIXABLE) {
            fsck_progress ("Trying to fix bitmap ..\n", diff);
            /* mark blocks as used in source bitmap if they are used in control bitmap */
            reiserfs_bitmap_disjunction (source_bitmap, control_bitmap);
            /* change used blocks count accordinly source bitmap,
               copy bitmap changes to on_disk bitmap */
            set_sb_free_blocks (fs->fs_ondisk_sb, reiserfs_bitmap_zeros (source_bitmap));
            reiserfs_bitmap_copy (fs->fs_bitmap2, source_bitmap);
            mark_buffer_dirty (fs->fs_super_bh);
            /* check again */
            if ((diff = reiserfs_bitmap_compare (source_bitmap, control_bitmap)) != 0)  {
                /* do not mark them as fatal or fixable because one can live with leaked space.
                   So this is not a fatal corruption, and fix-fixable cannot fix it */
//                one_more_corruption (fs, fatal);
                fsck_progress (" bitmaps were not recovered. \n"
                "\tYou can either run rebuild-tree or live with %d leaked blocks\n", diff);
            } else {
                fsck_progress ("ok\n");
            }
        } else {
            while (problem) {
                /* fixable corruptions because we can try to recover them w/out rebuilding the tree */
        	one_more_corruption (fs, fixable); 
        	problem --;
            }
        }
    } else 
        fsck_progress ("ok\n");
        
    return;
}


/* is this block legal to be pointed to by some place of the tree? */
static int bad_block_number (reiserfs_filsys_t * fs, unsigned long block)
{
    if (block >= get_sb_block_count (fs->fs_ondisk_sb) ||
	not_data_block (fs, block)) {
	/* block has value which can not be used as a pointer in a tree */
	
	return 1;
/*
	if (is_unfm_pointer) {
	    // unformatted node pointer will be zeroed
	    one_more_corruption (fs, fixable);
	    return 1;
	}

	// internal nodes can not have wrong pointer
	one_more_corruption (fs, fatal);
	return 1;
*/

    }

    if (is_block_free (fs, block)) {
	/* block is marked free - bitmap problems will be handled later */
	//one_more_corruption (fs, fixable);
    }

    return 0;
}


static int got_already (reiserfs_filsys_t * fs, unsigned long block)
{
#if 0
    if (0/*opt_fsck_mode == FSCK_FAST_REBUILD*/){
        if (is_block_used(block)){
	    fsck_log ("block %lu is in tree already\n", block);
	    return 1;
	}
    } else {
#endif

    if (did_we_meet_it (block)) {
	/* block is in tree at least twice */
    	return 1;
    }
    we_met_it (block);
    return 0;
}


/* 1 if some of fields in the block head of bh look bad */
static int bad_block_head (reiserfs_filsys_t * fs, struct buffer_head * bh)
{
    struct block_head * blkh;
    int sum_length = 0;

    blkh = B_BLK_HEAD (bh);
    if (get_blkh_nr_items (blkh) > (bh->b_size - BLKH_SIZE) / IH_SIZE) {
	fsck_log ("block %lu has wrong blk_nr_items (%z)\n",
		  bh->b_blocknr, bh);
	one_more_corruption (fs, fatal);
	return 1;
    }

    if (who_is_this (bh->b_data, bh->b_size) != THE_LEAF) {
	fsck_log ("leaf %lu has wrong structure\n", bh->b_blocknr);
        one_more_corruption (fs, fatal);
	return 1;
    }

    sum_length = (get_blkh_nr_items (blkh) > 0) ?
	get_ih_location (B_N_PITEM_HEAD (bh, get_blkh_nr_items (blkh) - 1)) : bh->b_size;

    if (get_blkh_free_space (blkh) !=
	sum_length - BLKH_SIZE - IH_SIZE * get_blkh_nr_items (blkh))
    {
	fsck_log ("block (%lu) has wrong blk_free_space (%lu) should be (%lu)",
		bh->b_blocknr, get_blkh_free_space (blkh), sum_length - BLKH_SIZE - IH_SIZE * get_blkh_nr_items (blkh) );
	if (fsck_mode(fs) == FSCK_FIX_FIXABLE) {
	    set_blkh_free_space (blkh, sum_length - BLKH_SIZE - IH_SIZE * get_blkh_nr_items (blkh));
	    fsck_log (" - fixed on (%lu)\n", get_blkh_free_space (blkh));
	    mark_buffer_dirty (bh);
	} else {
	    fsck_log ("\n");
	    one_more_corruption (fs, fixable);
	    return 1;
	}
    }

    return 0;
}

/* 1 if it does not look like reasonable stat data */
static int bad_stat_data (reiserfs_filsys_t * fs, struct buffer_head * bh, struct item_head * ih)
{
    unsigned long objectid;
    __u32 pos;
    __u32 links;
    struct stat_data * sd;

/*
    if (opt_fsck_mode == FSCK_FAST_REBUILD)
	return 0;
*/
    objectid = get_key_objectid (&ih->ih_key);
    if (!is_objectid_used (fs, objectid)) {
	/* FIXME: this could be cured right here */
	fsck_log ("\nbad_stat_data: %lu is marked free, but used by an object %k\n",
		  objectid, &ih->ih_key);	
        
        /* if it is FIX_FIXABLE we flush objectid map at the end
           no way to call one_less_corruption later
        */
	if (fsck_mode (fs) != FSCK_FIX_FIXABLE)
	    one_more_corruption (fs, fixable);
    }

    if (is_objectid_really_used (proper_id_map (fs), objectid, &pos)) {
	fsck_log ("\nbad_stat_data: %lu is shared by at least two files\n",
		  objectid);
	if (fsck_mode (fs) == FSCK_FIX_FIXABLE) 
            to_be_relocated (&ih->ih_key);
	    
	one_more_corruption (fs, fixable);	
	
	return 0;
    }

    sd = (struct stat_data *)B_I_PITEM(bh,ih);
    if (fsck_mode (fs) == FSCK_CHECK) {
        get_sd_nlink (ih, sd, &links);
        if (S_ISDIR(sd->sd_mode)) {
            if (links < 2) {
                fsck_log ("%s: block %lu, [%k], directory SD has bad nlink number\n",
                        __FUNCTION__, bh->b_blocknr, &ih->ih_key);
                one_more_corruption (fs, fixable);
            }
        } else {
            if (links == 0) {
                fsck_log ("%s: block %lu, [%k], SD has bad nlink number\n",
                        __FUNCTION__, bh->b_blocknr, &ih->ih_key);
                one_more_corruption (fs, fixable);
            }
        }
    } else {
        links = 0;
	set_sd_nlink (ih, sd, &links);
	mark_buffer_dirty (bh);
    }

    __mark_objectid_really_used (proper_id_map (fs), objectid, pos);
    return 0;
}


/* it looks like we can check item length only */
static int bad_direct_item (reiserfs_filsys_t * fs, struct buffer_head * bh, struct item_head * ih)
{
    return 0;
}


inline void handle_one_pointer (reiserfs_filsys_t * fs, struct buffer_head * bh, __u32 * ptr) {
    if (fsck_mode (fs) == FSCK_FIX_FIXABLE) {
	fsck_log (" - fixed");
	*ptr = 0;
	mark_buffer_dirty (bh);
    } else {
	one_more_corruption (fs, fixable);
    }
}

/*
static int bad_badblocks_item (reiserfs_filsys_t * fs, struct buffer_head * bh,
			      struct item_head * ih) {
    int i;
    __u32 * ind = (__u32 *)B_I_PITEM (bh, ih);

    if (get_ih_item_len (ih) % 4) {
	fsck_log ("%s: block %lu: item (%H) has bad length\n", __FUNCTION__,
		  bh->b_blocknr, ih);
	one_more_corruption (fs, fatal);
	return 1;
    }

    for (i = 0; i < I_UNFM_NUM (ih); i ++) {
//	__u32 unfm_ptr;

//	unfm_ptr = le32_to_cpu (ind [i]);
	if (!le32_to_cpu (ind [i])) {
	    fsck_log ("%s: block %lu: badblocks item (%H) has zero pointer.");
	    fsck_log ("Not an error, but could be deleted with --fix-fixable\n",
	    		__FUNCTION__, bh->b_blocknr, ih);
	    continue;
	}

	// check list of badblocks pointers
	if (le32_to_cpu (ind [i]) >= get_sb_block_count (fs->fs_ondisk_sb)) {
	    fsck_log ("%s: badblock pointer (block %lu) points out of disk spase (%lu)",
			__FUNCTION__, bh->b_blocknr, le32_to_cpu (ind [i]));
	    handle_one_pointer (fs, bh, &ind[i]);
	    fsck_log ("\n");
	}

	if (did_we_meet_it (le32_to_cpu (ind [i]))) {
	    // it can be
	    // 1. not_data_block
	    // 		delete pointer
	    // 2. ind [i] or internal/leaf	       		
	    //    advice to run fix-fixable if there is no fatal errors
	    //    with list of badblocks, say that it could fix it.
	
	    if (not_data_block (fs, le32_to_cpu (ind [i]))) {
		fsck_log ("%s: badblock pointer (block %lu) points on fs metadata (%lu)",
			__FUNCTION__, bh->b_blocknr, le32_to_cpu (ind [i]));
		handle_one_pointer (fs, bh, &ind[i]);
		fsck_log ("\n");
	    } else {
	        one_more_corruption (fs, badblocks);
	        fsck_log ("%s: badblock item points to a block"
	        	  " which in the tree already. Use --badblock-file option"
	        	  " to fix the problem\n", __FUNCTION__);
	    }
	} else {
	    we_met_it (le32_to_cpu (ind [i]));
	}
    }

    return 0;
}
*/

/* for each unformatted node pointer: make sure it points to data area and
   that it is not in the tree yet */
static int bad_indirect_item (reiserfs_filsys_t * fs, struct buffer_head * bh,
			      struct item_head * ih)
{
    int i;
    __u32 * ind = (__u32 *)B_I_PITEM (bh, ih);

    if (get_ih_item_len (ih) % 4) {
	fsck_log ("%s: block %lu: item (%H) has bad length\n",
		  __FUNCTION__, bh->b_blocknr, ih);
	one_more_corruption (fs, fatal);
	return 1;
    }

    for (i = 0; i < I_UNFM_NUM (ih); i ++) {
//	__u32 unfm_ptr;

	fsck_check_stat (fs)->unfm_pointers ++;
//	unfm_ptr = le32_to_cpu (ind [i]);
	if (!le32_to_cpu (ind [i])) {
	    fsck_check_stat (fs)->zero_unfm_pointers ++;
	    continue;
	}

	/* check unformatted node pointer and mark it used in the
           control bitmap */
	if (bad_block_number (fs, le32_to_cpu (ind [i]))) {
	    fsck_log ("%s: block %lu: item %H has bad pointer %d: %lu",
		      __FUNCTION__, bh->b_blocknr, ih, i, le32_to_cpu (ind [i]));
	    handle_one_pointer (fs, bh, &ind[i]);
	    fsck_log ("\n");
	    continue;
	}

        if (got_already (fs, le32_to_cpu (ind [i]))) {
	    fsck_log ("%s: block %lu: item %H has a pointer %d "
		      "to the block %lu which is in tree already",
		      __FUNCTION__, bh->b_blocknr, ih, i, le32_to_cpu (ind [i]));
	    handle_one_pointer (fs, bh, &ind [i]);
	    fsck_log ("\n");
            continue;
	}
    }

#if 0
    /* delete this check for 3.6 */
    if (get_ih_free_space (ih) > fs->fs_blocksize - 1) {
        if (fsck_mode (fs) == FSCK_FIX_FIXABLE) {
            /*FIXME: fix it if needed*/
        } else {
            fsck_log ("bad_indirect_item: %H has wrong ih_free_space\n", ih);
            one_more_corruption (fs, fixable);
        }
    }
#endif

    return 0;
}


/* FIXME: this was is_bad_directory from pass0.c */
static int bad_directory_item (reiserfs_filsys_t * fs, struct buffer_head * bh, struct item_head * ih)
{
    int i;
    char * name;
    int namelen;
    struct reiserfs_de_head * deh = B_I_DEH (bh, ih);
    int min_entry_size = 1;/* we have no way to understand whether the
                              filesystem were created in 3.6 format or
                              converted to it. So, we assume that minimal name
                              length is 1 */
    __u16 state;


    /* make sure item looks like a directory */
    if (get_ih_item_len (ih) / (DEH_SIZE + min_entry_size) < get_ih_entry_count (ih)) {
	/* entry count can not be that big */
	fsck_log ("%s: block %lu: directory item %H has corrupted structure\n",
			__FUNCTION__, bh->b_blocknr, ih);
	one_more_corruption (fs, fatal);	
	return 1;
    }

    if (get_deh_location (&deh[get_ih_entry_count (ih) - 1]) != DEH_SIZE * get_ih_entry_count (ih)) {
	/* last entry should start right after array of dir entry headers */
	fsck_log ("%s: block %lu: directory item %H has corrupted structure\n",
			__FUNCTION__, bh->b_blocknr, ih);
	one_more_corruption (fs, fatal);	
	return 1;
    }

    /* check name hashing */
    for (i = 0; i < get_ih_entry_count (ih); i ++, deh ++) {
	namelen = name_in_entry_length (ih, deh, i);
	name = name_in_entry (deh, i);
	if (!is_properly_hashed (fs, name, namelen, get_deh_offset (deh))) {
	    fsck_log ("%s: block %lu: directory item %H has hashed bad entry\n",
			__FUNCTION__, bh->b_blocknr, ih);
	    one_more_corruption (fs, fatal);	
	    return 1;
	}
    }

    deh = B_I_DEH (bh, ih);
    state = (1 << DEH_Visible2);
    /* ok, items looks like a directory */
    for (i = 0; i < get_ih_entry_count (ih); i ++, deh ++) {
	if (get_deh_state (deh) != state) {
	    fsck_log ("bad_directory_item: block %lu: item %H has entry "
		      "\"%.*s\" with wrong deh_state %o - expected %o",
		      bh->b_blocknr, ih, name_in_entry_length (ih, deh, i),
		      name_in_entry (deh, i), get_deh_state (deh), state );
	    if (fsck_mode (fs) == FSCK_FIX_FIXABLE) {
		set_deh_state (deh, 1 << DEH_Visible2);
		mark_buffer_dirty (bh);
		fsck_log (" - fixed");
	    } else 
		one_more_corruption (fs, fixable);
	    
	    fsck_log ("\n");
	}
    }

    return 0;
}


static int bad_item (reiserfs_filsys_t * fs, struct buffer_head * bh, int num)
{
    struct item_head * ih;

    ih = B_N_PITEM_HEAD (bh, num);

    if (get_key_dirid (&ih->ih_key) == (__u32)-1) {
	if (get_key_objectid (&ih->ih_key) != (__u32)-1) {
	    /*  safe link can be
		-1 object_id 0x1    	-2 (truncate) or
		-1 object_id blocksize+1 	-1 (unlink)
	    */

	    if (is_direct_ih(ih))
		if (get_offset(&ih->ih_key) == fs->fs_blocksize + 1)
		    if (get_ih_item_len (ih) == 4) {
			/*fsck_log("vpf-00010: safe link found [%k]\n", &ih->ih_key);*/
			fsck_check_stat(fs)->safe ++;
			return 0;
		    }

	    if (is_indirect_ih(ih))
		if(get_offset(&ih->ih_key) == 0x1)
		    if (get_ih_item_len (ih) == 4) {
			/*fsck_log("vpf-00020: safe link found [%k]\n", &ih->ih_key);*/
			fsck_check_stat(fs)->safe ++;
			return 0;
		    }
	
	    /* it does not look like safe link */

	    /* dir_id == -1 can be used only by safe links */
	    one_more_corruption (fs, fatal);
	    fsck_log ("%s: vpf-10290: block %lu item with wrong key (%k) found\n", __FUNCTION__, bh->b_blocknr, &ih->ih_key);
	    return 1;
	} else {

/*              BAD BLOCK LIST SUPPORT
            if (is_indirect_ih (ih)) {
	    // it looks like badblocks item
	    if (fs->fs_badblocks_bm)
	    	return 0;
	    else
		return bad_badblocks_item (fs, bh, ih);
	} else  {*/

	    one_more_corruption (fs, fatal);
	    fsck_log ("%s: vpf-10300: block %lu item with wrong key (%k) found\n", __FUNCTION__, bh->b_blocknr, &ih->ih_key);
	    return 1;
	}
    } else if (get_key_objectid (&ih->ih_key) == (__u32)-1) {
	one_more_corruption (fs, fatal);
	fsck_log ("%s: vpf-10310: block %lu item with wrong key (%k) found\n", __FUNCTION__, bh->b_blocknr, &ih->ih_key);
	return 1;
    }

    if (is_stat_data_ih (ih))
	return bad_stat_data (fs, bh, ih);

    if (is_direct_ih (ih))
	return bad_direct_item (fs, bh, ih);

    if (is_indirect_ih(ih))
	return bad_indirect_item (fs, bh, ih);
    
    return bad_directory_item (fs, bh, ih);
}


/* 1 if i-th and (i-1)-th items can not be neighbors in a leaf */
int bad_pair (reiserfs_filsys_t * fs, struct buffer_head * bh, int pos)
{
    struct item_head * ih;

    ih = B_N_PITEM_HEAD (bh, pos);

    if (comp_keys (&((ih - 1)->ih_key), &ih->ih_key) != -1) {
	if (fsck_mode (fs) != FSCK_REBUILD)
	    one_more_corruption (fs, fatal);	
	return 1;
    }

    if (is_stat_data_ih (ih))
	/* left item must be of another object */
	if (comp_short_keys (&((ih - 1)->ih_key), &ih->ih_key) != -1) {
	    if (fsck_mode (fs) != FSCK_REBUILD)
		one_more_corruption (fs, fatal);	
	    return 1;
	}

    if (get_key_dirid (&ih->ih_key) == (__u32)-1) {
    	if (get_key_objectid (&ih->ih_key) == (__u32)-1) {
/*              BAD BLOCK LIST SUPPORT
    	    if (is_indirect_ih (ih) && comp_short_keys (&((ih - 1)->ih_key), &ih->ih_key) != 0)
		return 0; // badblocks item
*/
	    /* it does not look like badblocks item */
    	} else {
    	    /* safe link ? */
	    if (comp_short_keys (&((ih - 1)->ih_key), &ih->ih_key) == 0) {
		if (is_indirect_ih (ih - 1) && is_direct_ih(ih))
		    return 0; /* safe link */
		/* they do not look like safe links */
	    } else {
		if (is_indirect_ih (ih) || is_direct_ih(ih))
		    return 0; /* safe link */
		/* it does not look like safe link */
	    }
    	}
    }

    if (is_direct_ih(ih)) {
	/* left item must be indirect or stat data item of the same
	   file */
	if (not_of_one_file (&((ih - 1)->ih_key), &ih->ih_key)) {
	    if (fsck_mode (fs) != FSCK_REBUILD)
		one_more_corruption (fs, fatal);
	    return 1;
	}

	if (!((is_stat_data_ih (ih - 1) && get_offset (&ih->ih_key) == 1) ||
	      (is_indirect_ih (ih - 1) &&	
	       get_offset (&(ih - 1)->ih_key) + get_bytes_number (ih-1, bh->b_size) ==
	       get_offset (&ih->ih_key)))) {
	    if (fsck_mode (fs) != FSCK_REBUILD)
		one_more_corruption (fs, fatal);
	    return 1;
	}
    }

    if (is_indirect_ih (ih) || is_direntry_ih (ih)) {
	/* left item must be stat data of the same object */
	if (not_of_one_file (&((ih - 1)->ih_key), &ih->ih_key) ||
	    !is_stat_data_ih (ih - 1)) {
	    if (fsck_mode (fs) != FSCK_REBUILD)
		one_more_corruption (fs, fatal);
	    return 1;
	}
    }

    return 0;
}

/* 1 if block head or any of items is bad */
static int bad_leaf (reiserfs_filsys_t * fs, struct buffer_head * bh)
{
    int i;

    if (bad_block_head (fs, bh)) {
	return 1;
    }
    
    for (i = 0; i < B_NR_ITEMS (bh); i ++) {
	if (bad_item (fs, bh, i)) {
	    fsck_log ("bad_leaf: block %lu has invalid item %d: %H\n",
		      bh->b_blocknr, i, B_N_PITEM_HEAD (bh, i));
	}

	if (i && bad_pair (fs, bh, i)) {
	    fsck_log ("bad_leaf: block %lu has wrong order of items [%d, %d]: %k %k\n",
		      bh->b_blocknr, i-1, i, &B_N_PITEM_HEAD (bh, i-1)->ih_key, &B_N_PITEM_HEAD (bh, i)->ih_key);
	}
    }
    return 0;
}

/* 1 if bh does not look like internal node */
static int bad_internal (reiserfs_filsys_t * fs, struct buffer_head * bh)
{
    int i;

    for (i = 0; i <= B_NR_ITEMS (bh); i ++) {
        if (i != B_NR_ITEMS (bh) && i != B_NR_ITEMS (bh) - 1)
	    /* make sure that keys are in increasing order */
            if (comp_keys (B_N_PDELIM_KEY (bh, i), B_N_PDELIM_KEY (bh, i + 1)) != -1) {
		fsck_log ("%s: vpf-10320: block %lu has wrong order of keys %k, %k\n",
			__FUNCTION__, bh->b_blocknr, B_N_PDELIM_KEY (bh, i), B_N_PDELIM_KEY (bh, i + 1));
		one_more_corruption (fs, fatal);
                return 1;
	    }

	/* make sure that the child is correct */
        if (bad_block_number (fs, get_dc_child_blocknr (B_N_CHILD (bh,i)))) {
	    fsck_log ("%s: vpf-10330: block %lu: has bad pointer %d: %lu\n",
		      __FUNCTION__, bh->b_blocknr, i, get_dc_child_blocknr (B_N_CHILD (bh,i)));
            one_more_corruption (fs, fatal);
            return 1;
	}
    }
    return 0;
}


/* h == 0 for root level. block head's level == 1 for leaf level  */
static inline int h_to_level (reiserfs_filsys_t * fs, int h)
{
    return get_sb_tree_height (fs->fs_ondisk_sb) - h - 1;
}


/* bh must be formatted node. blk_level must be tree_height - h + 1 */
static int bad_node (reiserfs_filsys_t * fs, struct buffer_head ** path,
		     int h)
{
    struct buffer_head ** pbh = &path[h];

    if (B_LEVEL (*pbh) != h_to_level (fs, h)) {
       	fsck_log ("node (%lu) with wrong level (%d) found in the tree (should be %d)\n",
		  (*pbh)->b_blocknr, B_LEVEL (*pbh), h_to_level (fs, h));
	one_more_corruption (fs, fatal);
        return 1;
    }

    if (bad_block_number (fs, (*pbh)->b_blocknr)) {
	one_more_corruption (fs, fatal);
	fsck_log ("%s: vpf-10340: wrong block number %lu found in the tree\n",
	      __FUNCTION__, (*pbh)->b_blocknr);
	return 1;
    }

    if (got_already (fs, (*pbh)->b_blocknr)) {
	fsck_log ("%s: vpf-10350: block number %lu is already in the tree\n",
	      __FUNCTION__, (*pbh)->b_blocknr);
	one_more_corruption (fs, fatal);
        return 1;
    }
    
    if (is_leaf_node (*pbh)) {
	fsck_check_stat (fs)->leaves ++;
	return bad_leaf (fs, *pbh);
    }
    
    fsck_check_stat (fs)->internals ++;
    return bad_internal (fs, *pbh);
}


/* internal node bh must point to block */
static int get_pos (struct buffer_head * bh, unsigned long block)
{
    int i;

    for (i = 0; i <= B_NR_ITEMS (bh); i ++) {
	if (get_dc_child_blocknr (B_N_CHILD (bh, i)) == block)
	    return i;
    }
    die ("get_pos: position for block %lu not found", block);
    return 0;
}


/* path[h] - leaf node */
static struct key * lkey (struct buffer_head ** path, int h)
{
    int pos;

    while (h > 0) {
       pos = get_pos (path[h - 1], path[h]->b_blocknr);
       if (pos)
           return B_N_PDELIM_KEY(path[h - 1], pos - 1);
       h --;
    }
    return 0;
}


/* path[h] - leaf node */
static struct key * rkey (struct buffer_head ** path, int h)
{
    int pos;

    while (h > 0) {
       pos = get_pos (path[h - 1], path[h]->b_blocknr);
       if (pos != B_NR_ITEMS (path[h - 1]))
           return B_N_PDELIM_KEY (path[h - 1], pos);
       h --;
    }
    return 0;
}


/* are all delimiting keys correct */
static int bad_path (reiserfs_filsys_t * fs, struct buffer_head ** path, int h1)
{
    int h = 0;
    struct key * dk;
    int pos = 0;

    while (path[h])
	h ++;

    h--;
    
    // path[h] is leaf
    if (h != h1)
	die ("bad_path: wrong path");

    if (h)
	pos = get_pos (path[h - 1], path[h]->b_blocknr);

    dk = lkey (path, h);
    if (dk && comp_keys (dk, B_N_PKEY (path[h], 0))) {
	/* left delimiting key must be equal to the key of 0-th item in the
	   node */
	fsck_log ("bad_path: left delimiting key %k must be equal to %k (block %lu, pos %d)\n",
		dk, B_N_PKEY (path[h], 0), path[h]->b_blocknr, 0);
	one_more_corruption (fs, fatal);
	return 1;
    }
    
    dk = rkey (path, h);
    if (dk && comp_keys (dk, B_N_PKEY (path[h],
       get_blkh_nr_items (B_BLK_HEAD (path[h])) - 1)) != 1) {
	/* right delimiting key must be bigger than the key of the last item
	   in the node */
	fsck_log ("bad_path: right delimiting key %k must be equal to %k (block %lu, pos %d)\n",
		dk, B_N_PKEY (path[h], get_blkh_nr_items (B_BLK_HEAD (path[h])) - 1),
		path[h]->b_blocknr, get_blkh_nr_items (B_BLK_HEAD (path[h])) - 1);
	one_more_corruption (fs, fatal);
	return 1;
    }

    if (h && (get_dc_child_size (B_N_CHILD(path[h-1],pos)) +
    	get_blkh_free_space ((struct block_head *)path[h]->b_data) +
    	BLKH_SIZE != path[h]->b_size))
    {
	/* wrong dc_size */
	
	fsck_log ("bad_path: wrong sum of dc_size %d (block %lu, pos %d) ",
		get_dc_child_size (B_N_CHILD(path[h-1],pos)), path[h-1]->b_blocknr, pos);
	fsck_log ("free space %d (block %lu) and BLKH_SIZE %lu",
		get_blkh_free_space ((struct block_head *)path[h]->b_data),
		path[h]->b_blocknr, BLKH_SIZE);
	
	if (fsck_mode (fs) == FSCK_FIX_FIXABLE) {
	    set_dc_child_size (B_N_CHILD(path[h-1],pos), path[h]->b_size -
	    		get_blkh_free_space ((struct block_head *)path[h]->b_data) - BLKH_SIZE);
	    fsck_log (" - fixed (%lu)\n", get_dc_child_size (B_N_CHILD(path[h-1],pos)));
	    mark_buffer_dirty (path[h-1]);
	} else {
	    one_more_corruption (fs, fixable);
	    fsck_log ("\n");
	    return 1;
	}
    }

    return 0;
}

static void before_check_fs_tree (reiserfs_filsys_t * fs) {
    init_control_bitmap (fs);

    source_bitmap = reiserfs_create_bitmap (get_sb_block_count (fs->fs_ondisk_sb));
    reiserfs_bitmap_copy (source_bitmap, fs->fs_bitmap2);

    proper_id_map (fs) = init_id_map ();
}

static void after_check_fs_tree (reiserfs_filsys_t * fs) {
    if (fsck_mode(fs) == FSCK_FIX_FIXABLE) {
        fs->fs_dirt = 1;
        reiserfs_flush_to_ondisk_bitmap (fs->fs_bitmap2, fs);
        reiserfs_flush (fs);
    }
    reiserfs_delete_bitmap (source_bitmap);
    reiserfs_delete_bitmap (control_bitmap);
    flush_buffers (fs->fs_dev);
}

/* pass the S+ tree of filesystem */
void check_fs_tree (reiserfs_filsys_t * fs)
{
    before_check_fs_tree (fs);
    
    fsck_progress ("Checking S+tree..");
    pass_through_tree (fs, bad_node, bad_path);
    /* S+ tree is correct (including all objects have correct
       sequences of items) */
    fsck_progress ("ok\n");
    
    /* compare created bitmap with the original */
    handle_bitmaps (fs);
    
    after_check_fs_tree (fs);
}

static int clean_attributes_handler (reiserfs_filsys_t * fs, struct buffer_head ** path, int h) {
    struct buffer_head * bh = path[h];
    int i;

    if (B_LEVEL (bh) != h_to_level (fs, h)) {
       	reiserfs_panic ("node (%lu) with wrong level (%d) found in the tree (should be %d)\n",
		  bh->b_blocknr, B_LEVEL (bh), h_to_level (fs, h));
    }

    if (!is_leaf_node (bh))
        return 0;

    for (i = 0; i < B_NR_ITEMS (bh); i ++) {
        if (is_stat_data_ih (B_N_PITEM_HEAD (bh, i)) &&
            get_ih_item_len (B_N_PITEM_HEAD (bh, i)) == SD_SIZE) {
	    set_sd_v2_sd_attrs((struct stat_data *)B_N_PITEM(bh, i), 0);
	    mark_buffer_dirty (bh);
	}
    }

    return 0;
}

void do_clean_attributes (reiserfs_filsys_t * fs) {
    pass_through_tree (fs, clean_attributes_handler, NULL);
    set_sb_v2_flag (fs->fs_ondisk_sb, reiserfs_attrs_cleared);
    mark_buffer_dirty (fs->fs_super_bh);
}

#if 0

void remove_internal_pointer(struct super_block * s, struct buffer_head ** path)
{
    int h = 0;
    int pos, items;
    __u32 block;


    while (path[h])
        h ++;

    h--;
    block = path[h]->b_blocknr;
        printf("\nremove pointer to (%d) block", block);
    brelse(path[h]);
    path[h] = 0;
    h--;
    while (h>=0)
    {
        if (B_NR_ITEMS(path[h]) <= 1)
        {
            block = path[h]->b_blocknr;
            brelse(path[h]);
            path[h] = 0;
            mark_block_free(block);
            /*unmark_block_formatted(block);*/
            used_blocks++;
            h --;
            continue;
        }
        pos = get_pos (path[h], block);
        if (pos)
        {
            memmove (B_N_CHILD(path[h],pos), B_N_CHILD(path[h],pos+1),
                s->s_blocksize - BLKH_SIZE - B_NR_ITEMS(path[h])*KEY_SIZE - DC_SIZE*(pos+1));
            memmove(B_N_PDELIM_KEY(path[h],pos-1), B_N_PDELIM_KEY(path[h],pos),
                s->s_blocksize - BLKH_SIZE - (pos)*KEY_SIZE);
        }else{
            __u32 move_block = path[h]->b_blocknr;
            int move_to_pos;
            int height = h;

            while(--height >= 0)
            {
                move_to_pos = get_pos (path[height], move_block);
                if (move_to_pos == 0){
                    move_block = path[height]->b_blocknr;
                    continue;
                }
                *B_N_PDELIM_KEY(path[height], move_to_pos-1) = *B_N_PDELIM_KEY(path[h], 0);
                break;
            }

            memmove (B_N_CHILD(path[h], 0), B_N_CHILD(path[h], 1),
                s->s_blocksize - BLKH_SIZE - B_NR_ITEMS(path[h])*KEY_SIZE - DC_SIZE);
            memmove(B_N_PDELIM_KEY(path[h], 0), B_N_PDELIM_KEY(path[h], 1),
                s->s_blocksize - BLKH_SIZE - KEY_SIZE);
        }
        set_node_item_number(path[h], node_item_number(path[h]) - 1);
        mark_buffer_dirty(path[h], 1);
        break;
    }
    if (h == -1)
    {
        SB_DISK_SUPER_BLOCK(s)->s_root_block = ~0;
        SB_DISK_SUPER_BLOCK(s)->s_tree_height = ~0;
        mark_buffer_dirty(SB_BUFFER_WITH_SB(s), 1);
    }
}

void handle_buffer(struct super_block * s, struct buffer_head * bh)
{
    int i, j;
    struct item_head * ih;

    if (is_leaf_node (bh))
    {
        for (i = 0, ih = B_N_PITEM_HEAD (bh, 0); i < B_NR_ITEMS (bh); i ++, ih ++)
        {
            if (is_indirect_ih(ih))
                for (j = 0; j < I_UNFM_NUM (ih); j ++)
                    if (B_I_POS_UNFM_POINTER(bh,ih,j)){
                        /*mark_block_unformatted(le32_to_cpu(B_I_POS_UNFM_POINTER(bh,ih,j)));*/
                        mark_block_used(le32_to_cpu(B_I_POS_UNFM_POINTER(bh,ih,j)));
                        used_blocks++;
                    }
        	if (is_stat_data_ih (ih)) {
		  /*add_event (STAT_DATA_ITEMS);*/
		    if (ih_key_format(ih) == KEY_FORMAT_1)
		      ((struct stat_data_v1 *)B_I_PITEM(bh,ih))->sd_nlink = 0;
		    else
		      ((struct stat_data *)B_I_PITEM(bh,ih))->sd_nlink = 0;
		    mark_buffer_dirty(bh, 1);
        	}
        }
    }
    mark_block_used(bh->b_blocknr);
//    we_met_it(s, bh->b_blocknr);
    used_blocks++;
}
	
/* bh must be formatted node. blk_level must be tree_height - h + 1 */
static int handle_node (struct super_block * s, struct buffer_head ** path, int h)
{
    if (bad_node(s, path, h)){
       remove_internal_pointer(s, path);
       return 1;
    }
    handle_buffer(s, path[h]);
    return 0;
}

/* are all delimiting keys correct */
static int handle_path (struct super_block * s, struct buffer_head ** path, int h)
{
    if (bad_path(s, path, h)){
        remove_internal_pointer(s, path);
        return 1;
    }
    return 0;
}

//return 1 to run rebuild tree from scratch
void check_internal_structure(struct super_block * s)
{
    /* control bitmap is used to keep all blocks we should not put into tree again */
    /* used bitmap is used to keep all inserted blocks. The same as control bitmap plus unfm blocks */
//    init_control_bitmap(s);

    printf ("Checking S+tree..");

    pass_through_tree (s, handle_node, handle_path);

//    compare_bitmaps(s);
    printf ("ok\n");
}

#endif
/*
int check_sb (reiserfs_filsys_t * fs)
{


    reiserfs_panic ("Not ready");
    return 0;
#if 0
    int format_sb = 0;
    int problem = 0;
    struct reiserfs_super_block * sb;
    __u32 block_count;
    unsigned long j_1st_block;
    int sb_version;

    sb = fs->fs_ondisk_sb;
    j_1st_block = get_jp_journal_1st_block (sb_jp (sb));

    // in (REISERFS_DISK_OFFSET_IN_BYTES / 4096) block
    if ((is_reiser2fs_magic_string (sb) || is_reiser2fs_jr_magic_string (sb) )&&
        j_1st_block == get_journal_new_start_must (fs)) {
	// 3.6 or >=3.5.22
	printf("\t  3.6.x format SB found\n");
        format_sb = 1;
        goto good_format;
    }

    if ( (is_reiserfs_magic_string (sb) ||is_reiser2fs_jr_magic_string (sb) ) &&
        j_1st_block == get_journal_new_start_must (fs)) {
	// >3.5.9(10) and <=3.5.21
        printf("\t>=3.5.9 format SB found\n");
        format_sb = 2;
        goto good_format;
    }

    // in 2 block
    if ( (is_reiser2fs_magic_string (sb) || is_reiser2fs_jr_magic_string (sb) ) &&
        j_1st_block == get_journal_old_start_must (sb)) {
	// <3.5.9(10) converted to new format
        printf("\t< 3.5.9(10) SB converted to new format found \n");
        format_sb = 3;
        goto good_format;
    }
	
    if ( (is_reiserfs_magic_string (sb) ||is_reiser2fs_jr_magic_string (sb) ) &&
        j_1st_block == get_journal_old_start_must (sb)) {
	// <3.5.9(10)
        printf("\t< 3.5.9(10) format SB found\n");
        format_sb = 4;
        goto good_format;
    }
    else
	die("check SB: wrong SB format found\n");
	
good_format:	
	
    printf("\n\t%d-%d\n", get_sb_block_count (sb), get_sb_free_blocks (sb));
    if (fs->fs_blocksize != 4096) {
	//???
	fsck_log("check SB: specified block size (%d) is not correct must be 4096\n",
		 fs->fs_blocksize);
        problem++;
    }

    //for 4096 blocksize only
    if ((get_sb_tree_height (sb) < DISK_LEAF_NODE_LEVEL + 1) ||
	(get_sb_tree_height (sb) > MAX_HEIGHT)){
	fsck_log ("check SB: wrong tree height (%d)\n", get_sb_tree_height (sb));
        problem++;
    }

    block_count = count_blocks (fs->fs_file_name, fs->fs_blocksize);

    if (get_sb_block_count (sb) > block_count){
	fsck_log ("check SB: specified block number (%d) is too high\n",
		  get_sb_block_count (sb));
        problem++;
    }

    if ((get_sb_root_block (sb) >= block_count)) {
	fsck_log ("check SB: specified root block number (%d) is too high\n", 
		  get_sb_root_block (sb));
        problem++;
    }

    if (get_sb_free_blocks (sb) > get_sb_block_count (sb)) {
	fsck_log ("check SB: specified free block number (%d) is too high\n",
		  get_sb_free_blocks (sb));
        problem++;
    }		

    if (get_sb_state (sb) != REISERFS_VALID_FS && get_sb_state (sb) != REISERFS_ERROR_FS){
	fsck_log ("check SB: unknown (%d) state\n", get_sb_state (sb));
        problem++;
    }		
    
    if (get_sb_bmap_nr (sb) != (get_sb_block_count (sb) + (fs->fs_blocksize * 8 - 1)) / (fs->fs_blocksize * 8)) {
	fsck_log("check SB: wrong bitmap number (%d)\n", get_sb_bmap_nr (sb));
        problem++;
    }		
    
    sb_version = get_sb_version (sb);
    if (sb_version == REISERFS_VERSION_3 || sb_version == REISERFS_VERSION_2 || sb_version == REISERFS_VERSION_1) {
        if (!((sb_version == REISERFS_VERSION_3 ||sb_version == REISERFS_VERSION_2) && (format_sb == 1 || format_sb == 3)) &&
            !(sb_version == REISERFS_VERSION_1 && (format_sb == 2 || format_sb == 4))) {
	    fsck_log("check SB: wrong SB version == %d, format == %d\n",
		     sb_version, format_sb);
	    problem++;
        }		
    } else {
	fsck_log ("check SB: wrong SB version (%d)\n", sb_version);
        problem++;
    }		

    if ((sb_version == REISERFS_VERSION_3 ||sb_version == REISERFS_VERSION_2) &&
	// FIXME: 
        (get_sb_hash_code (sb) < 1 || get_sb_hash_code (sb) > 3)) {
	fsck_log("check SB: wrong hash (%d)\n", get_sb_hash_code (sb));
	problem++;
    }		


    if ((sb_version == REISERFS_VERSION_3 ||sb_version == REISERFS_VERSION_2) ?
	(get_sb_oid_maxsize (sb) != ((fs->fs_blocksize - SB_SIZE) / sizeof(__u32) / 2 * 2)) :
	(get_sb_oid_maxsize (sb) != ((fs->fs_blocksize - SB_SIZE_V1) / sizeof(__u32) / 2 * 2))) {
	fsck_log("check SB: objectid map corrupted max_size == %d\n",
		 get_sb_oid_maxsize (sb));
        problem++;
    }

    if (get_sb_oid_cursize (sb) < 2 ||
	get_sb_oid_cursize (sb) > get_sb_oid_maxsize (sb)) {
	fsck_log("check SB: objectid map corrupted cur_size == %d\n",
		 get_sb_oid_cursize (sb));
	problem++;
    }		

    if (get_jp_journal_size (sb_jp (sb)) < JOURNAL_DEFAULT_SIZE/32 ){
	fsck_log("check SB: specified journal size (%d) is not correct must be not less that %d\n",
		 get_jp_journal_size (sb_jp (sb)), JOURNAL_DEFAULT_SIZE/32);
        problem++;
    }

    if (!problem) {
        fsck_progress ("\t  No problem found\n");
    } else if (fsck_log_file (fs) != stderr)
	fsck_progress ("Look for super block corruptions in the log file\n");

    return format_sb;

#endif
}

*/
