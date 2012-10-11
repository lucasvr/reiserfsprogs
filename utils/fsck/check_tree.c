/*
 * Copyright 1999-2004 by Hans Reiser, licensing governed by 
 * reiserfsprogs/README
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "fsck.h"
#include "misc/unaligned.h"

/* check_fs_tree stops and recommends to run fsck --rebuild-tree when:
   1. read fails
   2. node of wrong level found in the tree
   3. something in the tree points to wrong block number
      out of filesystem boundary is pointed by tree
      to block marked as free in bitmap
      the same block is pointed from more than one place
      not data blocks (journal area, super block, bitmaps)
   4. bad formatted node found
   5. delimiting keys are incorrect
*/

/* mark every block we see in the tree in control_bitmap, so, when to make
   sure, that no blocks are pointed to from more than one place we use
   additional bitmap (control_bitmap). If we see pointer to a block we set
   corresponding bit to 1. If it is set already - run with --rebuild-tree */
static reiserfs_bitmap_t * control_bitmap;

static reiserfs_bitmap_t * source_bitmap;

static void fsck_tree_check_bitmap_prep (reiserfs_filsys_t * fs) {
    unsigned int i;
    unsigned long nr;
    unsigned long block;
    unsigned long reserved;
    unsigned long bmap_nr;

    nr = reiserfs_sb_get_blocks(fs->fs_ondisk_sb);
    
    control_bitmap = reiserfs_bitmap_create(nr);
    
    if (!control_bitmap)
	misc_die ("%s: Failed to allocate a control bitmap.", __FUNCTION__);

    /* skipped and super block */
    for (i = 0; i <= fs->fs_super_bh->b_blocknr; i ++)
    	reiserfs_bitmap_set_bit (control_bitmap, i);

    /* bitmaps */
    bmap_nr = reiserfs_bmap_nr(nr, fs->fs_blocksize);
    block = fs->fs_super_bh->b_blocknr + 1;
    for (i = 0; i < bmap_nr; i ++) {
        reiserfs_bitmap_set_bit (control_bitmap, block);

	if (reiserfs_bitmap_spread (fs))
	    block = (block / (fs->fs_blocksize * 8) + 1) * 
		    (fs->fs_blocksize * 8);
	else
	    block ++;	
    }
    
    /* mark as used area of the main device either containing a journal or
       reserved to hold it */

    reserved = reiserfs_journal_hostsize(fs->fs_ondisk_sb);
    
    /* where does journal area (or reserved journal area) start from */

    if (!reiserfs_new_location (fs->fs_super_bh->b_blocknr, fs->fs_blocksize) &&
    	!reiserfs_old_location (fs->fs_super_bh->b_blocknr, fs->fs_blocksize))
    {
	misc_die ("%s: Wrong super block location. You must "
		  "run --rebuild-sb.", __FUNCTION__);
    }

    block = reiserfs_journal_start_must (fs);

    for (i = block; i < reserved + block; i ++)
	reiserfs_bitmap_set_bit (control_bitmap, i);

    if (fs->fs_badblocks_bm)
	for (i = 0; i < nr; i ++) {
	    if (reiserfs_bitmap_test_bit (fs->fs_badblocks_bm, i))
	    	reiserfs_bitmap_set_bit (control_bitmap, i);	
	}
}

/* if we managed to complete tree scanning and if control bitmap and/or proper
   amount of free blocks mismatch with bitmap on disk and super block's
   s_free_blocks - we can fix that */
static void fsck_tree_check_bitmap_fini (reiserfs_filsys_t * fs) {
    int problem = 0;

    /* check free block counter */
    if (reiserfs_sb_get_free (fs->fs_ondisk_sb) != 
	reiserfs_bitmap_zeros (control_bitmap)) 
    {
/*	fsck_log ("vpf-10630: The count of free blocks in the on-disk bitmap "
		  "(%lu) mismatches with the correct one (%lu).\n",  
		  reiserfs_sb_get_free (fs->fs_ondisk_sb),
		  reiserfs_bitmap_zeros (control_bitmap));
*/
	problem++;
    }

    if (reiserfs_bitmap_compare (source_bitmap, control_bitmap))
	problem++;    

    if (problem) {
        if (fsck_mode (fs) == FSCK_FIX_FIXABLE) {
	    fsck_log ("vpf-10630: The on-disk and the correct "
		      "bitmaps differs. Will be fixed later.\n");
	    
//            fsck_progress ("Trying to fix bitmap ..\n");
            /* mark blocks as used in source bitmap if they are used in 
	       control bitmap */
            reiserfs_bitmap_disjunction (source_bitmap, control_bitmap);
            /* change used blocks count accordinly source bitmap, copy 
	       bitmap changes to on_disk bitmap */
            reiserfs_sb_set_free (fs->fs_ondisk_sb, 
				reiserfs_bitmap_zeros(source_bitmap));
            reiserfs_bitmap_copy (fs->fs_bitmap2, source_bitmap);
            reiserfs_buffer_mkdirty (fs->fs_super_bh);
/*
            // check again 
            if ((diff = reiserfs_bitmap_compare(source_bitmap, 
						control_bitmap)) != 0)  
	    {
                // do not mark them as fatal or fixable because one can live 
		// with leaked space. So this is not a fatal corruption, and 
		// fix-fixable cannot fix it 
                fsck_progress (" bitmaps were not recovered.\n\tYou can either "
		"run rebuild-tree or live with %d leaked blocks\n", diff);
            } else {
                fsck_progress ("finished\n");
            }
*/
        } else if (problem) {
	    fsck_log ("vpf-10640: The on-disk and the "
		      "correct bitmaps differs.\n");
	    
            while (problem) {
                /* fixable corruptions because we can try to recover them 
		   without rebuilding the tree */
        	one_more_corruption (fs, FIXABLE); 
        	problem --;
            }
        }
    } else 
        //fsck_progress ("finished\n");
        
    return;
}

static int fsck_tree_check_bitmap_auto (reiserfs_filsys_t *fs) {
    unsigned long i;
    
    if (source_bitmap->bm_byte_size != control_bitmap->bm_byte_size)
	return -1;
    
    for (i = 0; i < source_bitmap->bm_byte_size; i ++) {
	if (control_bitmap->bm_map[i] & ~source_bitmap->bm_map[i]) {
	    return 1;
	}
    }
    
    return 0;
}

/* is this block legal to be pointed to by some place of the tree? */
static int fsck_tree_check_blknr (reiserfs_filsys_t * fs, unsigned long block) {
    if (reiserfs_fs_block(fs, block) != BT_UNKNOWN) {
	/* block has value which can not be used as a pointer in a tree */
	return 1;
    }

    return 0;
}

static int fsck_tree_check_mark_block (unsigned long block) {
    if (reiserfs_bitmap_test_bit (control_bitmap, block)) {
	/* block is in tree at least twice */
    	return 1;
    }
    
    reiserfs_bitmap_set_bit (control_bitmap, block);
    return 0;
}

/* 1 if it does not look like reasonable stat data */
static int fsck_tree_check_stat (reiserfs_filsys_t * fs, 
				 reiserfs_bh_t * bh, 
				 reiserfs_ih_t * ih)
{
    unsigned long objectid;

    objectid = reiserfs_key_get_oid (&ih->ih_key);
    if (!reiserfs_objmap_test (fs, objectid)) {
	/* FIXME: this could be cured right here */
	fsck_log ("%s: The objectid (%lu) is marked free, but used by an "
		  "object %k\n", __FUNCTION__, objectid, &ih->ih_key);

        /* if it is FIX_FIXABLE we flush objectid map at the end
           no way to call one_less_corruption later
        */
	if (fsck_mode (fs) != FSCK_FIX_FIXABLE)
	    one_more_corruption (fs, FIXABLE);
    }

    if (id_map_mark(proper_id_map (fs), objectid)) {
	fsck_log ("%s: The objectid (%lu) is shared by at least "
		  "two files. Can be fixed with --rebuild-tree "
		  "only.\n", __FUNCTION__, objectid);
    }

    return 0;
}

static inline void handle_one_pointer (reiserfs_filsys_t * fs, 
				       reiserfs_bh_t * bh,
				       __u32 * item, 
				       int offset)
{
    if (fsck_mode (fs) == FSCK_FIX_FIXABLE) {
	fsck_log (" - zeroed");
	d32_put (item, offset, 0);
	reiserfs_buffer_mkdirty (bh);
    } else {
	one_more_corruption (fs, FIXABLE);
    }
}

static int fsck_tree_check_badblock (reiserfs_filsys_t * fs, 
				     reiserfs_bh_t * bh,
				     reiserfs_ih_t * ih) 
{
    __u32 i;
    __u32 * ind = (__u32 *)reiserfs_item_by_ih (bh, ih);

    if (reiserfs_ih_get_len (ih) % 4) {
	fsck_log ("%s: block %lu: item (%H) has bad length\n", 
		  __FUNCTION__, bh->b_blocknr, ih);
	one_more_corruption (fs, FATAL);
	return 1;
    }
	
    /* All valid badblocks are given in badblock bitmap. 
       Nothing to check anymore. */
    if (fs->fs_badblocks_bm)
	    return 0;
    
    for (i = 0; i < reiserfs_ext_count (ih); i ++) {
	if (!d32_get (ind, i)) {
/*	    fsck_log ("%s: block %lu: badblocks item (%H) has "
		      "zero pointer.", __FUNCTION__, bh->b_blocknr, ih);
	    
	    if (fsck_mode(fs) != FSCK_FIX_FIXABLE) {
		    fsck_log("Not an error, but could be deleted "
			     "with --fix-fixable\n");
	    } else {
		    fsck_log("Will be deleted later.\n");
	    }	*/
	    
	    continue;
	}

	/* check list of badblocks pointers */
	if (d32_get (ind, i) >= reiserfs_sb_get_blocks (fs->fs_ondisk_sb)) {
	    fsck_log ("%s: badblock pointer (block %lu) points "
		      "out of disk spase (%lu)", __FUNCTION__, 
		      bh->b_blocknr, d32_get (ind, i));
	    
	    handle_one_pointer (fs, bh, ind, i);
	    fsck_log ("\n");
	}

	if (reiserfs_bitmap_test_bit (control_bitmap, d32_get (ind, i))) {
	    /* it can be
	       1. not_data_block
	       		delete pointer
	       2. ind [i] or internal/leaf
	          advice to run fix-fixable if there is no fatal errors
	          with list of badblocks, say that it could fix it. */
	
	    if (reiserfs_fs_block(fs, d32_get (ind, i)) != BT_UNKNOWN ) {
		fsck_log ("%s: badblock pointer (block %lu) points on fs "
			  "metadata (%lu)", __FUNCTION__, bh->b_blocknr, 
			  d32_get (ind, i));
		
		handle_one_pointer (fs, bh, ind, i);
		fsck_log ("\n");
	    } else {
		one_more_corruption(fs, FIXABLE);
	        fsck_log ("%s: badblock pointer (block %lu) points to "
			  "a block (%lu) which is in the tree already. "
			  "Use badblock option (-B) to fix the problem\n", 
			  __FUNCTION__, bh->b_blocknr, d32_get (ind, i));
	    }

	    continue;
	}
	
	reiserfs_bitmap_set_bit (control_bitmap, d32_get(ind, i));
    }

    return 0;
}

/* for each unformatted node pointer: make sure it points to data area and
   that it is not in the tree yet */
static int fsck_tree_check_ext (reiserfs_filsys_t * fs, 
				reiserfs_bh_t * bh,
				reiserfs_ih_t * ih)
{
    __u32 * ind = (__u32 *)reiserfs_item_by_ih (bh, ih);
    unsigned int i;

    if (reiserfs_ih_get_len (ih) % REISERFS_EXT_SIZE) {
	fsck_log ("%s: block %lu: The item (%H) has the bad length (%u)\n",
		  __FUNCTION__, bh->b_blocknr, ih, reiserfs_ih_get_len (ih));
	one_more_corruption (fs, FATAL);
	return 1;
    }

    for (i = 0; i < reiserfs_ext_count (ih); i ++) {

	fsck_check_stat (fs)->unfm_pointers ++;
	if (!d32_get (ind, i)) {
	    fsck_check_stat (fs)->zero_unfm_pointers ++;
	    continue;
	}

	/* check unformatted node pointer and mark it used in the
           control bitmap */
	if (fsck_tree_check_blknr(fs, d32_get (ind, i))) {
	    fsck_log ("%s: block %lu: The item %k has the bad pointer (%d) to "
		      "the block (%lu)", __FUNCTION__, bh->b_blocknr, 
		      &ih->ih_key, i, d32_get (ind, i));
	    
	    handle_one_pointer (fs, bh, ind, i);
	    fsck_log ("\n");
	    continue;
	}

        if (fsck_tree_check_mark_block (d32_get (ind, i))) {
	    fsck_log ("%s: block %lu: The item (%H) has the bad pointer (%d) "
		      "to the block (%lu), which is in tree already", 
		      __FUNCTION__, bh->b_blocknr, ih, i, d32_get (ind, i));
	    
	    handle_one_pointer (fs, bh, ind, i);
	    fsck_log ("\n");
            continue;
	}
    }

#if 0
    /* delete this check for 3.6 */
    if (reiserfs_ih_get_free (ih) > fs->fs_blocksize - 1) {
        if (fsck_mode (fs) == FSCK_FIX_FIXABLE) {
            /*FIXME: fix it if needed*/
        } else {
            fsck_log ("%s: %H has wrong ih_free_space\n", __FUNCTION__, ih);
            one_more_corruption (fs, fixable);
        }
    }
#endif

    return 0;
}

static int fsck_tree_check_dir (reiserfs_filsys_t * fs, 
			       reiserfs_bh_t * bh, 
			       reiserfs_ih_t * ih)
{
    char *name, *prev_name;
    __u32 off, prev_off;
    unsigned int count, i;
    reiserfs_deh_t * deh = reiserfs_deh (bh, ih);
    int min_entry_size = 1;/* We have no way to understand whether the
                              filesystem was created in 3.6 format or
                              converted to it. So, we assume that minimal name
                              length is 1 */
    __u16 state;
    int namelen;

    count = reiserfs_ih_get_entries (ih);

    if (count == 0) {
	one_more_corruption (fs, FATAL);	
	return 1;
    }
    
    /* make sure item looks like a directory */
    if (reiserfs_ih_get_len (ih) / 
	(REISERFS_DEH_SIZE + min_entry_size) < count) 
    {
	/* entry count can not be that big */
	fsck_log ("%s: block %lu: The directory item %k has the exsessively "
		  "big entry count (%u)\n",  __FUNCTION__, bh->b_blocknr, 
		  &ih->ih_key, count);
	
	one_more_corruption (fs, FATAL);	
	return 1;
    }

    if (reiserfs_deh_get_loc (&deh[count - 1]) != REISERFS_DEH_SIZE * count) {
	/* last entry should start right after array of dir entry headers */
	fsck_log ("%s: block %lu: The directory item %k has the corrupted "
		  "entry structure\n", __FUNCTION__, bh->b_blocknr, 
		  &ih->ih_key);
	
	one_more_corruption (fs, FATAL);
	return 1;
    }
    
    /* check name hashing */
    prev_name = reiserfs_item_by_ih(bh, ih) + reiserfs_ih_get_len(ih);
    prev_off = 0;

    for (i = 0; i < count; i ++, deh ++) {
	namelen = reiserfs_direntry_name_len (ih, deh, i);
	name = reiserfs_deh_name (deh, i);
	off = reiserfs_deh_get_off (deh);
	
	if (namelen > REISERFS_NAME_MAX || 
	    name >= prev_name || off <= prev_off) 
	{
	    fsck_log ("%s: block %lu: The directory item %k has a broken entry "
		      "(%d)\n", __FUNCTION__, bh->b_blocknr, &ih->ih_key, i);
	    one_more_corruption (fs, FATAL);
	    return 1;
	}

	if (!reiserfs_hash_correct (&fs->hash, name, namelen, off)) {
	    fsck_log ("%s: block %lu: The directory item %k has a not properly "
		      "hashed entry (%d)\n", __FUNCTION__, bh->b_blocknr, 
		      &ih->ih_key, i);
	    
	    one_more_corruption (fs, FATAL);
	    return 1;
	}

	prev_name = name;
	prev_off = off;
    }

    deh = reiserfs_deh (bh, ih);
    state = (1 << DEH_Visible2);
    /* ok, items looks like a directory */
    for (i = 0; i < count; i ++, deh ++) {
	if (reiserfs_deh_get_state (deh) != state) {
	    fsck_log ("%s: block %lu: The directory item %k has the entry (%d) "
		      "\"%.*s\" with a not legal state (%o), (%o) expected", 
		      __FUNCTION__, bh->b_blocknr, &ih->ih_key, i, 
		      reiserfs_direntry_name_len (ih, deh, i), 
		      reiserfs_deh_name (deh, i), 
		      reiserfs_deh_get_state (deh), state);
	    
	    if (fsck_mode (fs) == FSCK_FIX_FIXABLE) {
		reiserfs_deh_set_state (deh, 1 << DEH_Visible2);
		reiserfs_buffer_mkdirty (bh);
		fsck_log (" - corrected\n");
	    } else 
		one_more_corruption (fs, FIXABLE);
	    
	    fsck_log ("\n");
	}
    }

    return 0;
}

static int fsck_tree_check_item (reiserfs_filsys_t * fs, 
				 reiserfs_bh_t * bh, 
				 int num)
{
    reiserfs_ih_t * ih;
    int format;

    ih = reiserfs_ih_at (bh, num);

    if ((reiserfs_ih_get_flags(ih)) != 0) {
	if (fsck_mode(fs) != FSCK_FIX_FIXABLE) {
	    one_more_corruption (fs, FIXABLE);
	    fsck_log ("%s: vpf-10570: block %lu: The item header (%d) has not "
		      "cleaned flags.\n", __FUNCTION__, bh->b_blocknr, num);
	} else {
	    fsck_log ("%s: vpf-10580: block %lu: Flags in the item header "
		      "(%d) were cleaned\n", __FUNCTION__, bh->b_blocknr, num);
	    
	    reiserfs_ih_clflags(ih);
	    reiserfs_buffer_mkdirty(bh);
	}
    }

    
    if (reiserfs_ih_stat(ih) && 
	reiserfs_ih_get_len(ih) == REISERFS_SD_SIZE) 
    {
	format = KEY_FORMAT_2;
    } else if (reiserfs_ih_stat(ih) && 
	       reiserfs_ih_get_len(ih) == REISERFS_SD_SIZE_V1)
    {
	format = KEY_FORMAT_1;
    } else {
	format = reiserfs_key_format(&ih->ih_key);
    }
    
    if (format != reiserfs_ih_get_format(ih)) {
	if (fsck_mode(fs) != FSCK_FIX_FIXABLE) {
	    one_more_corruption (fs, FIXABLE);
	    fsck_log ("%s: vpf-10710: block %lu: The format (%d) specified "
		      "in the item header (%d) differs from the key format "
		      "(%d).\n", __FUNCTION__, bh->b_blocknr, 
		      reiserfs_ih_get_format(ih), num, format);
	} else {
	    fsck_log ("%s: vpf-10720: block %lu: The format (%d) specified "
		      "in the item header (%d) was fixed to the key format "
		      "(%d).\n", __FUNCTION__, bh->b_blocknr, 
		      reiserfs_ih_get_format(ih), num, format);
	    
	    reiserfs_ih_set_format(ih, format);
	    reiserfs_buffer_mkdirty(bh);
	}
    }

    if (reiserfs_key_get_oid (&ih->ih_key) == REISERFS_BAD_OID) {
	if (reiserfs_key_get_did (&ih->ih_key) == REISERFS_BAD_DID && 
	    reiserfs_ih_ext (ih)) 
	{
		/* Bad Block support. */
		return fsck_tree_check_badblock (fs, bh, ih);
	}

	goto error;
    } else {
	if (reiserfs_key_get_did (&ih->ih_key) == (__u32)-1) {
	    /*  Safe Link support. Allowable safe links are:
		-1 object_id           0x1	EXTENT (truncate) or
		-1 object_id   blocksize+1	DIRECT   (unlink) */
	    if (reiserfs_ih_direct(ih) && 
		reiserfs_key_get_off(&ih->ih_key) == fs->fs_blocksize + 1) 
	    {
		if (reiserfs_ih_get_len (ih) == 4) {
		/*  fsck_log("vpf-00010: safe link found %k\n", &ih->ih_key);*/
		    fsck_check_stat(fs)->safe ++;
		    return 0;
		}
	    }

	    if (reiserfs_ih_ext(ih) && 
		reiserfs_key_get_off(&ih->ih_key) == 0x1) 
	    {
		if (reiserfs_ih_get_len (ih) == 4) {
		/*  fsck_log("vpf-00020: safe link found %k\n", &ih->ih_key);*/
		    fsck_check_stat(fs)->safe ++;
		    return 0;
		}
	    }
	
	    /* it does not look like safe link */
	    goto error;
	}
    }
    
    if (reiserfs_ih_stat (ih))
	return fsck_tree_check_stat (fs, bh, ih);

    if (reiserfs_ih_direct (ih))
	return 0;

    if (reiserfs_ih_ext(ih))
	return fsck_tree_check_ext (fs, bh, ih);
    
    return fsck_tree_check_dir (fs, bh, ih);
 
 error:
    one_more_corruption (fs, FATAL);
    fsck_log ("%s: vpf-10310: block %lu, item %d: The item has a wrong "
	      "key %k\n", __FUNCTION__, num, bh->b_blocknr, &ih->ih_key);
    return 1;
}

/* 1 if block head or any of items is bad */
static int fsck_tree_leaf_check (reiserfs_filsys_t * fs, reiserfs_bh_t * bh) {
    int i;

    if (fsck_leaf_check_header(fs, bh))
	return 1;
    
    for (i = 0; i < reiserfs_node_items (bh); i ++) {
	if (fsck_tree_check_item (fs, bh, i)) {
	    fsck_log ("%s: block %lu, item %d: The corrupted item found (%H)\n",
		      __FUNCTION__, bh->b_blocknr, i, reiserfs_ih_at (bh, i));
	}

	if (i && fsck_leaf_check_neigh (fs, bh, i)) {
	    fsck_log ("%s: block %lu, items %d and %d: The wrong order of "
		      "items: %k, %k\n", __FUNCTION__, bh->b_blocknr, i - 1, i,
		      &reiserfs_ih_at (bh, i - 1)->ih_key,
		      &reiserfs_ih_at (bh, i)->ih_key);
	}
    }
    return 0;
}

/* 1 if bh does not look like internal node */
static int fsck_tree_check_internal (reiserfs_filsys_t * fs, 
				     reiserfs_bh_t * bh) 
{
    int i;

    for (i = 0; i <= reiserfs_node_items (bh); i ++) {
        if (i != reiserfs_node_items (bh) && i != 
	    reiserfs_node_items (bh) - 1)
	{
	    /* make sure that keys are in increasing order */
            if (reiserfs_key_comp (reiserfs_int_key_at (bh, i), 
				   reiserfs_int_key_at (bh, i + 1)) != -1) 
	    {
		fsck_log ("%s: vpf-10320: block %lu, items %d and %d: The "
			  "wrong order of items: %k, %k\n", __FUNCTION__, 
			  bh->b_blocknr, i, i + 1, reiserfs_int_key_at (bh, i),
			  reiserfs_int_key_at (bh, i + 1));
		
		one_more_corruption (fs, FATAL);
                return 1;
	    }
	}
	
	/* make sure that the child is correct */
        if (fsck_tree_check_blknr(fs, reiserfs_dc_get_nr (reiserfs_int_at (bh,i))))
	{
	    fsck_log ("%s: vpf-10330: block %lu, item %d: The internal "
		      "item points to the not legal block (%lu)\n", 
		      __FUNCTION__, bh->b_blocknr, i, 
		      reiserfs_dc_get_nr (reiserfs_int_at (bh,i)));
	    
            one_more_corruption (fs, FATAL);
            return 1;
	}
    }
    
    return 0;
}

/* h == 0 for root level. block head's level == 1 for leaf level  */
static inline int h_to_level (reiserfs_filsys_t * fs, int h) {
    return reiserfs_sb_get_height (fs->fs_ondisk_sb) - h + 1;
}

/* bh must be formatted node. blk_level must be tree_height - path.path_length */
static int fsck_tree_check_node (reiserfs_filsys_t * fs, 
				 reiserfs_path_t * path)
{
    reiserfs_bh_t *pbh = REISERFS_PATH_LEAF(path);

    if (reiserfs_node_level(pbh) != h_to_level (fs, path->path_length)) {
       	fsck_log ("block %lu: The level of the node (%d) is not "
		  "correct, (%d) expected\n", pbh->b_blocknr, 
		  reiserfs_node_level(pbh), h_to_level(fs, path->path_length));
	
	one_more_corruption (fs, FATAL);
        return 1;
    }

    if (fsck_tree_check_blknr(fs, pbh->b_blocknr)) {
	one_more_corruption (fs, FATAL);
	fsck_log ("%s: vpf-10340: The node in the wrong block number (%lu) "
		  "found in the tree\n",  __FUNCTION__, pbh->b_blocknr);
	return 1;
    }

    if (fsck_tree_check_mark_block (pbh->b_blocknr)) {
	fsck_log ("%s: vpf-10350: The block (%lu) is used more than once "
		  "in the tree.\n",  __FUNCTION__, pbh->b_blocknr);
	one_more_corruption (fs, FATAL);
        return 1;
    }
    
    if (reiserfs_leaf_head(pbh)) {
	fsck_check_stat (fs)->leaves ++;
	return fsck_tree_leaf_check (fs, pbh);
    }
    
    fsck_check_stat (fs)->internals ++;
    return fsck_tree_check_internal (fs, pbh);
}

/* are all delimiting keys correct */
static int fsck_tree_check_path (reiserfs_filsys_t * fs, 
				 reiserfs_path_t * path)
{
    const reiserfs_key_t *dk;
    reiserfs_bh_t *parent;
    reiserfs_bh_t *bh;
    int items;
    int pos;
    int h1;

    h1 = REISERFS_PATH_OFFILL;
    while (path->path_elements[h1 + 1].pe_buffer)
	h1 ++;

    // path[h] is leaf
    if (h1 != path->path_length)
	misc_die ("%s: The leaf is expected as the last "
		  "element in the path.", __FUNCTION__);

    bh = REISERFS_PATH_LEAF(path);
    if (h1 != REISERFS_PATH_OFFINIT) {
	parent = REISERFS_PATH_BUFFER(path, h1 - 1);
	pos = reiserfs_internal_get_pos (parent, bh->b_blocknr);
    } else {
	parent = NULL;
	pos = 0;
    }

    dk = reiserfs_tree_lkey (path, fs);
    if (dk != &MIN_KEY && 
	reiserfs_key_comp (dk, reiserfs_ih_key_at (bh, 0))) 
    {
	/* left delimiting key must be equal to the key of 0-th item in the
	   node */
	fsck_log ("%s: The left delimiting key %k of the node (%lu) must "
		  "be equal to the first element's key %k within the node.\n",
		  __FUNCTION__, dk, bh->b_blocknr, reiserfs_ih_key_at(bh, 0));
	
	one_more_corruption (fs, FATAL);
	return 1;
    }
    
    items = reiserfs_nh_get_items( NODE_HEAD(bh) ) - 1;
    dk = reiserfs_tree_rkey (path, fs);
    if (dk != &MAX_KEY && 
	reiserfs_key_comp (dk, reiserfs_ih_key_at (bh, items)) != 1) 
    {
	/* right delimiting key must be greather then the key of the last 
	   item in the node */
	fsck_log ("%s: The right delimiting key %k of the node (%lu) must "
		  "be greater than the last (%d) element's key %k within "
		  "the node.\n", __FUNCTION__, dk, bh->b_blocknr,
		  reiserfs_nh_get_items (NODE_HEAD (bh)) - 1, 
		  reiserfs_ih_key_at (bh, items));
	
	one_more_corruption (fs, FATAL);
	return 1;
    }

    if ((h1 != REISERFS_PATH_OFFINIT) && 
	(reiserfs_dc_get_size(reiserfs_int_at(parent, pos)) +
	 reiserfs_nh_get_free(NODE_HEAD(bh)) + 
	 REISERFS_NODEH_SIZE != bh->b_size))
    {
	/* wrong dc_size */
	fsck_log ("bad_path: block %lu, pointer %d: The used space (%d) "
		  "of the child block (%lu)", parent->b_blocknr, pos, 
		  reiserfs_dc_get_size(reiserfs_int_at(parent, pos)), 
		  bh->b_blocknr);
	
	fsck_log (" is not equal to the (blocksize (4096) - free space (%d) - "
		  "header size (%u))", reiserfs_nh_get_free(NODE_HEAD(bh)), 
		  REISERFS_NODEH_SIZE);
	
	if (fsck_mode (fs) == FSCK_FIX_FIXABLE) {
	    reiserfs_dc_set_size (
		    reiserfs_int_at(parent, pos), bh->b_size -
		    reiserfs_nh_get_free(NODE_HEAD(bh)) - REISERFS_NODEH_SIZE);
	    
	    fsck_log (" - corrected to (%lu)\n", 
		      reiserfs_dc_get_size (reiserfs_int_at(parent, pos)));
	    
	    reiserfs_buffer_mkdirty (parent);
	} else {
	    one_more_corruption (fs, FIXABLE);
	    fsck_log ("\n");
	    return 1;
	}
    }

    return 0;
}

static void fsck_tree_check_prep (reiserfs_filsys_t * fs) {
    fsck_tree_check_bitmap_prep (fs);

    source_bitmap = 
	    reiserfs_bitmap_create (reiserfs_sb_get_blocks (fs->fs_ondisk_sb));
    
    reiserfs_bitmap_copy (source_bitmap, fs->fs_bitmap2);

    proper_id_map (fs) = id_map_init();
}

static void fsck_tree_check_fini (reiserfs_filsys_t * fs) {
    if (fsck_mode(fs) == FSCK_FIX_FIXABLE) {
        reiserfs_bitmap_flush(fs->fs_bitmap2, fs);
        reiserfs_fs_flush (fs);
        fs->fs_dirt = 1;
	reiserfs_bitmap_delta (source_bitmap, control_bitmap);
	fsck_deallocate_bitmap(fs) = source_bitmap;
    } else 
	reiserfs_bitmap_delete (source_bitmap);
    
    reiserfs_bitmap_delete (control_bitmap);
    reiserfs_buffer_flush_all (fs->fs_dev);
}

/* pass internal tree of filesystem */
void check_fs_tree (reiserfs_filsys_t * fs)
{
    fsck_tree_check_prep (fs);
    
    fsck_progress ("Checking Internal tree...\n");

    fsck_tree_trav (fs, fsck_tree_check_node, 
		    fsck_tree_check_path, 
		    fsck_mode(fs) == FSCK_AUTO ? 2 : -1);
 
    if (!fsck_quiet(fs))
	fsck_progress("\n");
    
    /* compare created bitmap with the original */
    if (fsck_mode(fs) == FSCK_AUTO) {
	if (fsck_tree_check_bitmap_auto(fs)) {
	    fprintf(stderr, "The on-disk bitmap looks corrupted.");
	    one_more_corruption(fs, FIXABLE);
	}
	id_map_free(proper_id_map(fs));
    } else
	fsck_tree_check_bitmap_fini(fs);
    
    fsck_tree_check_fini (fs);
}

static int clean_attributes_handler (reiserfs_filsys_t * fs, 
				     reiserfs_path_t * path) 
{
    reiserfs_bh_t * bh = REISERFS_PATH_LEAF(path);
    int i;

    if (reiserfs_node_level (bh) != h_to_level (fs, path->path_length)) {
       	reiserfs_panic ("The node (%lu) with wrong level (%d) "
			"found in the tree, (%d) expected\n", 
			bh->b_blocknr, reiserfs_node_level (bh), 
			h_to_level (fs, path->path_length));
    }

    if (!reiserfs_leaf_head (bh))
        return 0;

    for (i = 0; i < reiserfs_node_items (bh); i ++) {
        if (reiserfs_ih_stat (reiserfs_ih_at (bh, i)) &&
            reiserfs_ih_get_len (reiserfs_ih_at (bh, i)) == 
	    REISERFS_SD_SIZE) 
	{
	    reiserfs_set_sd_v2_attrs
		    ((reiserfs_sd_t *)reiserfs_item_at(bh, i), 0);
	    reiserfs_buffer_mkdirty (bh);
	}
    }

    return 0;
}

void fsck_tree_clean_attr (reiserfs_filsys_t * fs) {
    fsck_tree_trav (fs, clean_attributes_handler, NULL, -1);
    reiserfs_sb_mkflag (fs->fs_ondisk_sb, reiserfs_attrs_cleared);
    reiserfs_buffer_mkdirty (fs->fs_super_bh);
}
