/*
 * Copyright 1996-2004 by Hans Reiser, licensing governed by 
 * reiserfsprogs/README
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "fsck.h"
#include "misc/unaligned.h"
#include "misc/malloc.h"

#include <sys/stat.h>

static void cut_last_unfm_pointer (reiserfs_path_t * path, 
				   reiserfs_ih_t * ih)
{
    reiserfs_ih_set_free(ih, 0);
    if (reiserfs_ext_count (ih) == 1)
	reiserfs_tree_delete (fs, path, 0);
    else
	reiserfs_tree_delete_unit (fs, path, -((int)REISERFS_EXT_SIZE));
}

/*
    if this is not a symlink - make it of_this_size;
    otherwise find a size and return it in symlink_size;
*/
static unsigned long extent2direct (reiserfs_path_t * path, 
				    __u64 len, int symlink)
{
    reiserfs_bh_t * bh = REISERFS_PATH_LEAF (path);
    reiserfs_ih_t * ih = REISERFS_PATH_IH (path);
    __u32 unfm_ptr;
    reiserfs_bh_t * unfm_bh = 0;
    reiserfs_ih_t ins_ih;
    char * buf;
    char bad_drct[fs->fs_blocksize];

    /* direct item to insert */
    memset (&ins_ih, 0, sizeof (ins_ih));
    if (symlink) {
	reiserfs_ih_set_format (&ins_ih, KEY_FORMAT_1);
    } else {
	reiserfs_ih_set_format (&ins_ih, reiserfs_ih_get_format (ih));
    }
    reiserfs_key_set_did (&ins_ih.ih_key, reiserfs_key_get_did (&ih->ih_key));
    reiserfs_key_set_oid (&ins_ih.ih_key, reiserfs_key_get_oid (&ih->ih_key));
    reiserfs_key_set_sec (reiserfs_ih_get_format (&ins_ih), &ins_ih.ih_key,
			  reiserfs_key_get_off (&ih->ih_key) + 
			  (reiserfs_ext_count (ih) - 1) * bh->b_size, 
			  TYPE_DIRECT);

    // we do not know what length this item should be
    unfm_ptr = d32_get ((__u32 *)REISERFS_PATH_ITEM (path), 
			reiserfs_ext_count (ih) - 1);
    
    if (unfm_ptr && (unfm_bh = reiserfs_buffer_read(bh->b_dev, unfm_ptr, 
						    bh->b_size)))
    {
	/* we can read the block */
	buf = unfm_bh->b_data;
    } else {
        /* we cannot read the block */
 	if (unfm_ptr) {
	    fsck_log ("%s: Reading of the block (%lu), pointed to by the "
		      "file %K, failed\n", __FUNCTION__, unfm_ptr, &ih->ih_key);
	}
	
	memset (bad_drct, 0, fs->fs_blocksize);
	buf = bad_drct;
    }
    
    reiserfs_ih_set_len (&ins_ih, (reiserfs_ih_get_format (ih) == KEY_FORMAT_2) 
			 ? MISC_ROUND_UP(len) : len);
    
    reiserfs_ih_set_free (&ins_ih, MAX_US_INT);
    reiserfs_ih_cltail(ih);
    reiserfs_buffer_mkdirty(bh);
    
    // last last unformatted node pointer
    path->pos_in_item = reiserfs_ext_count (ih) - 1;
    cut_last_unfm_pointer (path, ih);

    /* insert direct item */
    if (reiserfs_tree_search_item (fs, &(ins_ih.ih_key), path) == ITEM_FOUND) {
	reiserfs_panic ("%s: The direct item %k should not exist yet.", 
			__FUNCTION__, &(ins_ih.ih_key));
    }
    
    reiserfs_tree_insert (fs, path, &ins_ih, (const char *)(buf));

    reiserfs_buffer_close (unfm_bh);

    /* put to stat data offset of first byte in direct item */
    return reiserfs_key_get_off (&ins_ih.ih_key); //offset;
}

/* returns 1 when file looks correct, -1 if directory items appeared
   there, 0 - only holes in the file found */
/* when it returns, key->k_offset is offset of the last item of file */
/* sd_size is zero if we do not need to convert any extent to direct */
int are_file_items_correct (reiserfs_ih_t * sd_ih, 
			    void * sd, 
			    __u64 * size, 
			    __u32 * blocks,
			    int mark_passed_items, 
			    int * symlink)
{
    const reiserfs_key_t *next_key;
    reiserfs_key_t *key;
    reiserfs_path_t path;
    reiserfs_bh_t * bh;
    reiserfs_ih_t * ih;

    int next_is_another_object = 0;
    __u64 last_unfm_offset = 0;
    int retval, was_tail = 0;
    int will_convert = 0;
    int should_convert;
    int had_direct = 0;
    long long int gap;
    __u32 sd_fdb = 0;
    int key_version;
    unsigned int i;
    __u64 sd_size;

    key_version = reiserfs_ih_get_format (sd_ih);
    should_convert = (fsck_mode (fs) != FSCK_REBUILD) || mark_passed_items;
    key = &sd_ih->ih_key;
    reiserfs_stat_get_size (sd_ih, sd, &sd_size);

    if (key_version == KEY_FORMAT_1)
    	reiserfs_stat_get_fdb (sd_ih, sd, &sd_fdb);

    reiserfs_key_set_off (key_version, key, 1);
    reiserfs_key_set_type (key_version, key, TYPE_DIRECT);

    /* correct size and st_blocks */
    *size = 0;
    *blocks = 0;

    path.path_length = REISERFS_PATH_OFFILL;

    do {
	retval = reiserfs_tree_search_position (fs, key, &path);
	if (retval == POSITION_FOUND && path.pos_in_item != 0)
	    reiserfs_panic ("%s: We look for %k, found %k. Must be found "
		    "first item byte (position 0).", __FUNCTION__,
		    key, &(REISERFS_PATH_IH (&path)->ih_key));

	switch (retval) {
	case POSITION_FOUND:/**/

	    ih = REISERFS_PATH_IH (&path);
	    bh = REISERFS_PATH_LEAF (&path);

	    if (reiserfs_ih_wastail (ih)) {
	    	was_tail = 1;
	    }
	
	    reiserfs_key_set_type (key_version, key, 
				   reiserfs_key_get_type (&ih->ih_key));
	
	    if (mark_passed_items == 1) {
		fsck_item_mkreach (ih, bh);
	    }
	    
	    // does not change path
	    next_key = reiserfs_tree_next_key(&path, fs);

	    if (next_key == 0 || reiserfs_key_comp2 (key, next_key) || 
		(!reiserfs_key_ext (next_key) && 
		 !reiserfs_key_direct(next_key)))
	    {
		next_is_another_object = 1;
		will_convert = (reiserfs_ih_ext (ih) && sd_size && 
				(reiserfs_ext_count (ih) > 0));
		
		if (will_convert) {
		    last_unfm_offset = reiserfs_key_get_off (key) + 
			    fs->fs_blocksize * (reiserfs_ext_count (ih) - 1);
		    
		    /* if symlink or
		       [ 1. sd_size points somewhere into last unfm block
		         2. one item of the file was direct before for 3_6 ||
			    FDB points to the tail correctly for 3_5
		         3. we can have a tail in the file of a such size ] */
		    will_convert = will_convert && 
			    (sd_size >= last_unfm_offset) && 
			    (sd_size < last_unfm_offset + fs->fs_blocksize) &&
			    !reiserfs_policy_ext (sd_size, sd_size - 
						  last_unfm_offset + 1, 
						  fs->fs_blocksize);
		    
		    will_convert = will_convert &&
			    (*symlink || 
			     ((key_version == KEY_FORMAT_1) &&
			      (sd_fdb == last_unfm_offset)) ||
			     ((key_version == KEY_FORMAT_2) && was_tail));
		}
	    }

	    if (should_convert) {
		*symlink = *symlink && 
			(will_convert || reiserfs_key_direct(&ih->ih_key));
	    
		if (!(*symlink) && key_version != reiserfs_ih_get_format (ih)) {
		    if (fsck_mode(fs) == FSCK_CHECK) {
			fsck_log("%s: vpf-10250: block %lu, item (%d): The "
				 "item format (%H) is not equal to SD format "
				 "(%d)\n", __FUNCTION__, bh->b_blocknr, 
				 REISERFS_PATH_LEAF_POS(&path),ih, key_version);
			
			one_more_corruption (fs, FIXABLE);
		    } else {
			fsck_log("%s: vpf-10280: block %lu, item (%d): The "
				 "item format (%H) is not equal to SD format "
				 "(%d) - fixed.\n", __FUNCTION__, bh->b_blocknr,
				 REISERFS_PATH_LEAF_POS(&path),ih, key_version);

			reiserfs_key_set_sec (key_version, &ih->ih_key, 
					      reiserfs_key_get_off (&ih->ih_key), 
					      reiserfs_key_get_type (&ih->ih_key));
			
			reiserfs_ih_set_format(ih, key_version);
			reiserfs_buffer_mkdirty(bh);
		    }
		}

		if (*symlink && reiserfs_key_direct(&ih->ih_key)) {
		    /* symlink. Check that it is of KEY_FORMAT_1 */
		    if (fsck_mode(fs) == FSCK_CHECK) {
			if ((reiserfs_ih_get_format(ih) != KEY_FORMAT_1) || 
			    (reiserfs_key_format(&ih->ih_key) != KEY_FORMAT_1)) 
			{
			    fsck_log("%s: vpf-10732: block %lu, item (%d): "
				     "The symlink format (%H) is not equal "
				     "to 3.5 format (%d)\n", 
				     __FUNCTION__, bh->b_blocknr, 
				     REISERFS_PATH_LEAF_POS(&path),
				     ih, KEY_FORMAT_1);
			    
			    one_more_corruption (fs, FIXABLE);
			}
		    } else {
			if ((reiserfs_ih_get_format(ih) != KEY_FORMAT_1) || 
			    (reiserfs_key_format(&ih->ih_key) != KEY_FORMAT_1)) 
			{
			    fsck_log("%s: vpf-10732: block %lu, item (%d): "
				     "The symlink format (%H) is not equal "
				     "to 3.5 format (%d)\n", 
				     __FUNCTION__, bh->b_blocknr,
				     REISERFS_PATH_LEAF_POS(&path),
				     ih, KEY_FORMAT_1);
			    
			    reiserfs_key_set_sec(KEY_FORMAT_1, &ih->ih_key, 
						 reiserfs_key_get_off(&ih->ih_key), 
						 reiserfs_key_get_type(&ih->ih_key));
			    
			    reiserfs_ih_set_format(ih, KEY_FORMAT_1);
			    reiserfs_buffer_mkdirty(bh);
			}
		    }
		}
		
		if (will_convert)
		    *size = sd_size;
		else
		    *size = reiserfs_key_get_off (&ih->ih_key) + 
			    reiserfs_leaf_ibytes (ih, fs->fs_blocksize) - 1;
	    
		if (reiserfs_key_get_type (&ih->ih_key) == TYPE_EXTENT) {
		    if (*symlink) /* symlinks must be calculated as dirs */
			*blocks = REISERFS_DIR_BLOCKS (*size);
		    else
			for (i = 0; i < reiserfs_ext_count (ih); i ++) {
			    __u32 * ind = (__u32 *)REISERFS_PATH_ITEM(&path);

			    if (d32_get(ind, i) != 0)
				*blocks += (fs->fs_blocksize >> 9);
			}
		} else if (reiserfs_key_get_type (&ih->ih_key) == TYPE_DIRECT) {
		    if (*symlink) /* symlinks must be calculated as dirs */
			*blocks = REISERFS_DIR_BLOCKS (*size);
		    else if (!had_direct)
			*blocks += (fs->fs_blocksize >> 9);

		    /* calculate only the first direct byte */
		    had_direct++;
		}
	    }

	    if (next_is_another_object) {
		/* next item does not exists or is of another object,
                   therefore all items of file are correct */
		if (will_convert) {
		    if (fsck_mode (fs) == FSCK_CHECK) {
			/* here it can be symlink only */
			fsck_log ("%s: The extent item should be converted "
				  "back to direct %K\n", __FUNCTION__, 
				  &ih->ih_key);
			one_more_corruption (fs, FIXABLE);
			reiserfs_tree_pathrelse (&path);
		    } else {
			__u32 * ind = (__u32 *)REISERFS_PATH_ITEM(&path);
			
			if (d32_get(ind, reiserfs_ext_count (ih) - 1) == 0)
			    *blocks += (fs->fs_blocksize >> 9);

			/* path is released here. */
			sd_fdb = extent2direct (&path, sd_size - 
						last_unfm_offset + 1,
						*symlink);
			
			/* last item of the file is direct item */
			reiserfs_key_set_off (key_version, key, 
					      sd_fdb);
			
			reiserfs_key_set_type (key_version, key, TYPE_DIRECT);
		    }
		} else {
		    reiserfs_tree_pathrelse (&path);
		}
		
		return 1;
	    }

	    /* next item is item of this file */
	    if ((gap = must_there_be_a_hole(ih, next_key))) {
		/* next item has incorrect offset (hole or overlapping) */
		reiserfs_tree_pathrelse (&path);

		if (reiserfs_ih_ext(ih) &&
		    fsck_mode (fs) == FSCK_REBUILD && 
		    gap < FSCK_MAX_GAP(fs->fs_blocksize)) 
		{
			if (gap < 0) {
			    reiserfs_panic("%s: found items %k, %k are "
					   "overlapped.\n", __FUNCTION__, 
					   &ih->ih_key, next_key);
			}

			reiserfs_key_set_off(key_version, key, 
				reiserfs_key_get_off(&ih->ih_key) +
				reiserfs_leaf_ibytes(ih, fs->fs_blocksize));
			
			fsck_tree_rewrite(fs, key, 
				reiserfs_key_get_off(next_key), 
				1 << IH_Unreachable);
			
			break;
		}
		
		return 0;
	    }

	    /* next item exists */
	    reiserfs_key_set_sec(key_version, key, 
				 reiserfs_key_get_off (next_key), 
				 reiserfs_key_get_type(next_key));
	
	    if (reiserfs_key_comp (key, next_key))
		reiserfs_panic ("%s: Internal tree is in inconsistent state, "
				"the current item key %K and the next key %K "
				"must match\n", __FUNCTION__, key, next_key);
	    
	    reiserfs_tree_pathrelse (&path);
	    break;

	case POSITION_NOT_FOUND:
	    /* We always must have next key found. Exception is first byte. 
	       It does not have to exist */
	    if (reiserfs_key_get_off (key) != 1)
		reiserfs_panic ("%s: Position (offset == %llu) in the middle "
				"of the file %K was not found.", __FUNCTION__, 
				reiserfs_key_get_off(key), key);

	    bh = REISERFS_PATH_LEAF (&path);
	    
	    if (reiserfs_node_items(bh) <= REISERFS_PATH_LEAF_POS(&path))
		next_key = reiserfs_tree_next_key(&path, fs);
	    else
		next_key = &REISERFS_PATH_IH(&path)->ih_key;
	    
	    reiserfs_tree_pathrelse (&path);
	    
	    gap = reiserfs_key_get_off(next_key);
	    
	    if (fsck_mode(fs) == FSCK_REBUILD && 
		gap < FSCK_MAX_GAP(fs->fs_blocksize) + 1) 
	    {
		fsck_tree_rewrite(fs, key, gap, 1 << IH_Unreachable);
		break;
	    }
	    
	    return 0;
      
	case FILE_NOT_FOUND:
	    if (reiserfs_key_get_off (key) != 1)
		reiserfs_panic ("%s: File %K must be found as we found its "
				"StatData.", __FUNCTION__, key);
	    reiserfs_tree_pathrelse (&path);
	    return 1;

	case DIRECTORY_FOUND:
	    reiserfs_tree_pathrelse (&path);
	    return -1;
	}
    } while (1);

    misc_die ("%s: Cannot reach here", __FUNCTION__);
    return 0;
}

static int cb_relocate(reiserfs_path_t *path, void *data) {
    return 1;
}

/* delete all items and put them back (after that file should have
   correct sequence of items.
   if @relocate is specified then it relocates items to a new id.
   if @adjust_ih is specified then the key in ih is changed also. */
void fsck_file_relocate(reiserfs_key_t *key, int update_key) {
    reiserfs_key_t start;
    saveitem_t *si;
    __u32 oid;
    int count;
    int moved;
    
    /* Find all items of the file, remove, relocate, insert them. */
    oid = 0;
    moved = 0;
    si = NULL;
    start = *key;
    reiserfs_key_set_sec(KEY_FORMAT_1, &start, OFFSET_SD, TYPE_STAT_DATA);

    while ((count = fsck_tree_delete(&start, &si, 1, cb_relocate, NULL))) {
	if (oid == 0)
	    oid = fsck_relocate_oid(key);

	reiserfs_key_set_oid(&(si->si_ih.ih_key), oid);
	fsck_tree_insert_item(&(si->si_ih), si->si_dnm_data, 0);
	
	si = fsck_item_free(si);
	moved += count;
    }

    if (moved) {
	fsck_log ("%s: %d items of file %K moved to %u oid\n",
              __FUNCTION__, moved, key, oid);
    }
    
    if (update_key) {
	/* If nothing has been relocated but ih needs to be adjusted,
	  allocate a new oid for relocation. */

	if (oid == 0)
	    oid = fsck_relocate_oid(key);

	reiserfs_key_set_oid (key, oid);
    }
}
