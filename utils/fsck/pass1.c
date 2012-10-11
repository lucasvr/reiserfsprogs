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
#include "util/device.h"
#include "util/misc.h"

#include <time.h>

reiserfs_bitmap_t * bad_unfm_in_tree_once_bitmap;
#define is_bad_unfm_in_tree_once(block)		\
	reiserfs_bitmap_test_bit(bad_unfm_in_tree_once_bitmap, block)
#define mark_bad_unfm_in_tree_once(block) \
	reiserfs_bitmap_set_bit(bad_unfm_in_tree_once_bitmap, block)

static void stat_data_in_tree (reiserfs_bh_t *bh,
			       reiserfs_ih_t * ih)
{
#if 0
    __u32 objectid;
    
    objectid = reiserfs_key_get_oid (&ih->ih_key);
    
    if (mark_objectid_really_used (proper_id_map (fs), objectid)) {
	stat_shared_objectid_found (fs);
	mark_objectid_really_used (shared_id_map (fs), objectid);
    }
#endif
    
    zero_nlink (ih, reiserfs_item_by_ih (bh, ih));
}

static char *still_bad_unfm_ptr_to_string (int val) {
    switch (val) {
	case 1:
	    return "a leaf";
	case 2:
	    return "shared between a few files";
	case 3:
	    return "not a data block";
	case 4:
	    return "not used in on-disk bitmap";
	case 5:
	    return "out of partition boundary";
    }
    return "";
}

static int still_bad_unfm_ptr (unsigned long block) {
    if (!block)
	return 0;
    
    if (pass0_block_isleaf (block))
	return 1;
    
    if (pass0_block_isbad_unfm (block) && 
	!is_bad_unfm_in_tree_once (block))
    {
	return 2;
    }
    
    if (reiserfs_fs_block(fs, block) != BT_UNKNOWN)
	return 3;
    
/*  if (!was_block_used (block))
	return 4; */
    
    if (block >= reiserfs_sb_get_blocks (fs->fs_ondisk_sb))
	return 5;
    
    return 0;
}


/* this just marks blocks pointed by an extent item as used in the
   new bitmap */
static void extent_in_tree (reiserfs_bh_t * bh,
			    reiserfs_ih_t * ih)
{
    unsigned int i;
    __u32 * unp;
    __u32 unfm_ptr;
    int ret;

    unp = (__u32 *)reiserfs_item_by_ih (bh, ih);
    
    for (i = 0; i < reiserfs_ext_count (ih); i ++) {
	unfm_ptr = d32_get (unp, i);
	if (unfm_ptr == 0)
	    continue;
	if ((ret = still_bad_unfm_ptr (unfm_ptr)))
	    reiserfs_panic ("%s: block %lu: The file %k points to the "
			    "block (%u) which is %s", __FUNCTION__, 
			    bh->b_blocknr, &ih->ih_key, unfm_ptr, 
			    still_bad_unfm_ptr_to_string(ret));

	mark_block_used (unfm_ptr, 1);
    }
}

static void cb_mkunreach(reiserfs_ih_t *ih) {
	fsck_item_mkunreach(ih);
}

static void leaf_is_in_tree_now (reiserfs_bh_t * bh) {
    item_func_t actions[] = {stat_data_in_tree, extent_in_tree, 0, 0};

    mark_block_used ((bh)->b_blocknr, 1);

    reiserfs_leaf_traverse (bh, cb_mkunreach, actions);

    pass_1_stat (fs)->inserted_leaves ++;

    reiserfs_buffer_mkdirty (bh);
}


static void insert_pointer (reiserfs_bh_t * bh, reiserfs_path_t * path) {
    reiserfs_ih_t * ih;
    char * body;
    int retval;
    reiserfs_tb_t tb;
    
    reiserfs_tb_init (&tb, fs, path, 0x7fff);

    /* reiserfs_fix_nodes & reiserfs_tb_balance must work 
       for internal nodes only */
    ih = 0;

    retval = reiserfs_fix_nodes (M_INTERNAL, &tb, ih);
    if (retval != CARRY_ON)
	misc_die ("insert_pointer: reiserfs_fix_nodes failed with retval == %d",
		  retval);
    
    /* child_pos: we insert after position child_pos: this feature of 
       the insert_child; there is special case: we insert pointer after
       (-1)-st key (before 0-th key) in the parent */
    if (REISERFS_PATH_LEAF_POS (path) == 0 && path->pos_in_item == 0)
	REISERFS_PATH_UPPARENT_POS (path, 0) = -1;
    else if (REISERFS_PATH_UPPARENT (path, 0) == 0) {
	REISERFS_PATH_UPPARENT_POS (path, 0) = 0;
    }
    
    ih = 0;
    body = (char *)bh;

    reiserfs_tb_balance (&tb, ih, body, M_INTERNAL, 0);
    leaf_is_in_tree_now (bh);
}


/* return 1 if new can be joined with last node on the path or with
   its right neighbor, 0 otherwise */
static int balance_condition_2_fails (reiserfs_bh_t * new, 
				      reiserfs_path_t * path)
{
    const reiserfs_key_t *right_dkey;
    int pos, used_space;
    reiserfs_bh_t *bh;
    reiserfs_ih_t *ih;
    
    bh = REISERFS_PATH_LEAF (path);
    
    if (reiserfs_tree_node_mergeable (bh, new))
	/* new node can be joined with last buffer on the path */
	return 1;
    
    /* new node can not be joined with its left neighbor */
    
    right_dkey = reiserfs_tree_rkey (path, fs);
    if (!reiserfs_key_comp (right_dkey, &MAX_KEY))
	/* there is no right neighbor */
	return 0;
    
    pos = REISERFS_PATH_UPPOS (path, 1);
    if (pos == reiserfs_node_items (bh = REISERFS_PATH_UPBUFFER (path, 1))) {
	/* we have to read parent of right neighbor. For simplicity we
	   call search_item, which will read right neighbor as well */
	REISERFS_PATH_INIT(path_to_right_neighbor);
	
	if (reiserfs_tree_search_item (fs, right_dkey, &path_to_right_neighbor) 
	    != ITEM_FOUND)
	{
	    reiserfs_panic("%s: block %lu, pointer %d: The left delimiting "
			   "key %k of the block (%lu) is wrong, the item "
			   "cannot be found", __FUNCTION__,
			    REISERFS_PATH_UPBUFFER(path, 1)->b_blocknr, 
			    pos, right_dkey,
			    reiserfs_dc_get_nr(reiserfs_int_at (bh, pos + 1)));
	}
	
	used_space = reiserfs_node_used
		(REISERFS_PATH_LEAF(&path_to_right_neighbor));
	
	reiserfs_tree_pathrelse (&path_to_right_neighbor);
    } else {
	used_space = reiserfs_dc_get_size (reiserfs_int_at (bh, pos + 1));
    }
    
    ih = reiserfs_ih_at (new, reiserfs_node_items (new) - 1);
    if (reiserfs_node_free (new) >= used_space -
	(reiserfs_leaf_mergeable (ih, (reiserfs_ih_t *)right_dkey, new->b_size)
	 ? REISERFS_IH_SIZE : 0))
    {
	return 1;
    }

    return 0;
}


static void get_max_buffer_key (reiserfs_bh_t * bh, 
				reiserfs_key_t * key)
{
    reiserfs_ih_t * ih;

    ih = reiserfs_ih_at (bh, reiserfs_node_items (bh) - 1);
    reiserfs_key_copy (key, &(ih->ih_key));

    if (reiserfs_key_dir (key)) {
	/* copy deh_offset 3-rd and 4-th key components of the last entry */
	reiserfs_key_set_off (KEY_FORMAT_1, key, 
		reiserfs_deh_get_off (reiserfs_deh (bh, ih) + 
				      reiserfs_ih_get_entries (ih) - 1));

    } else if (!reiserfs_key_stat (key))
	/* get key of the last byte, which is contained in the item */
	reiserfs_key_set_off (reiserfs_key_format (key), key, 
			      reiserfs_key_get_off (key) + 
			      reiserfs_leaf_ibytes (ih, bh->b_size) - 1);
}

static int tree_is_empty (void)
{
    return (reiserfs_sb_get_root (fs->fs_ondisk_sb) == ~(__u32)0 || 
	    reiserfs_sb_get_root (fs->fs_ondisk_sb) == 0) ? 1 : 0;
}


static void make_single_leaf_tree (reiserfs_bh_t * bh) {
    /* tree is empty, make tree root */
    reiserfs_sb_set_root (fs->fs_ondisk_sb, bh->b_blocknr);
    reiserfs_sb_set_height (fs->fs_ondisk_sb, 2);
    reiserfs_buffer_mkdirty (fs->fs_super_bh);
    leaf_is_in_tree_now (bh);
}


/* inserts pointer to leaf into tree if possible. If not, marks node as
   uninsertable in special bitmap */
static int try_to_insert_pointer_to_leaf (reiserfs_bh_t * new_bh) {
    /* first and last keys of new buffer */
    reiserfs_key_t * first_bh_key, last_bh_key;	
    reiserfs_key_t last_path_buffer_last_key;
    const reiserfs_key_t *dkey;
    REISERFS_PATH_INIT (path);
    reiserfs_bh_t * bh;			/* last path buffer */
    int ret_value;

    if (tree_is_empty () == 1) {
	make_single_leaf_tree (new_bh);
	return 0;
    }

    first_bh_key = reiserfs_ih_key_at (new_bh, 0);
    
    /* try to find place in the tree for the first key of the coming node */
    ret_value = reiserfs_tree_search_item (fs, first_bh_key, &path);
    if (ret_value == ITEM_FOUND)
	goto cannot_insert;

    /* get max key in the new node */
    get_max_buffer_key (new_bh, &last_bh_key);

    bh = REISERFS_PATH_LEAF (&path);
    if (reiserfs_key_comp (reiserfs_ih_key_at (bh, 0), 
			   &last_bh_key) == 1/* first is greater*/) 
    {
	/* new buffer falls before the leftmost leaf */
	if (reiserfs_tree_node_mergeable (new_bh, bh))
	    goto cannot_insert;
	
	dkey = reiserfs_tree_lkey (&path, fs);
	if ((dkey && reiserfs_key_comp (dkey, &MIN_KEY) != 0) || 
	    REISERFS_PATH_LEAF_POS (&path) != 0)
	{
	    misc_die ("try_to_insert_pointer_to_leaf: bad search result");
	}
	
	path.pos_in_item = 0;
	goto insert;
    }
    
    /* get max key of buffer, that is in tree */
    get_max_buffer_key (bh, &last_path_buffer_last_key);
    if (reiserfs_key_comp (&last_path_buffer_last_key, 
			   first_bh_key) != -1/* second is greater */)
    {
	/* first key of new buffer falls in the middle of node that is in tree */
	goto cannot_insert;
    }
    
    dkey = reiserfs_tree_rkey (&path, fs);
    if (dkey && reiserfs_key_comp (dkey, &last_bh_key) != 1 
	/* first is greater */)
    {
	goto cannot_insert;
    }
    
    if (balance_condition_2_fails (new_bh, &path))
	goto cannot_insert;


 insert:
    insert_pointer (new_bh, &path);
    reiserfs_tree_pathrelse (&path);
    return 0;
    
 cannot_insert:
    reiserfs_tree_pathrelse (&path);
    return 1;
}



/* everything should be correct already in the leaf but contents of extent
   items. So we only
   1. zero slots pointing to a leaf
   2. zero pointers to blocks which are pointed already
   3. what we should do with directory entries hashed by another hash?
   they are deleted for now
*/
static void pass1_correct_leaf (reiserfs_filsys_t * fs,
				reiserfs_bh_t * bh)
{
    unsigned int i, j;
    reiserfs_ih_t * ih;
    __u32 * ind_item;
    __u32 unfm_ptr;
    int dirty = 0;


    ih = reiserfs_ih_at (bh, 0);
    for (i = 0; i < reiserfs_node_items (bh); i ++, ih ++) {
	if (reiserfs_ih_dir (ih)) {
	    reiserfs_deh_t * deh;
	    __u32 offset;
	    char * name;
	    int name_len;
	    unsigned int hash_code;

	    deh = reiserfs_deh (bh, ih);
	    offset = 0;
	    for (j = 0; j < reiserfs_ih_get_entries (ih); j ++) {
		name = reiserfs_deh_name (deh + j, j);
		name_len = reiserfs_direntry_name_len (ih, deh + j, j);

		if ((j == 0 && is_dot (name, name_len)) ||
		    (j == 1 && is_dot_dot (name, name_len))) {
		    continue;
		}

		hash_code = reiserfs_hash_find (
			name, name_len, reiserfs_deh_get_off (deh + j),
			reiserfs_sb_get_hash (fs->fs_ondisk_sb));
		
		if (hash_code != reiserfs_sb_get_hash (fs->fs_ondisk_sb)) {
		    fsck_log ("pass1: block %lu, item %d, entry %d: The "
			      "entry \"%.*s\" of the %k is hashed with %s "
			      "whereas proper hash is %s", bh->b_blocknr, 
			      i, j, name_len, name, &ih->ih_key,
			      reiserfs_hash_name (hash_code), 
			      reiserfs_hash_name (
				reiserfs_sb_get_hash (fs->fs_ondisk_sb)));
		    
		    if (reiserfs_ih_get_entries (ih) == 1) {
			reiserfs_leaf_delete_item (fs, bh, i);
			fsck_log(" - the only entry - item was deleted\n");
			i --;
			ih --;
			break;
		    } else {
			reiserfs_leaf_delete_entry (fs, bh, i, j, 1);
			fsck_log(" - deleted\n");
			j --;
			deh = reiserfs_deh (bh, ih);
			continue;
		    }
		}

		if (j && offset >= reiserfs_deh_get_off (deh + j)) {
		    fsck_log ("pass1: block %lu, item %d, entry %d: The entry "
			      "\"%.*s\" of the %k has hash offset %lu not "
			      "larger smaller than the previous one %lu. The "
			      "entry is deleted.\n", bh->b_blocknr, 
			      i, j, name_len, name, &ih->ih_key, 
			      reiserfs_deh_get_off(deh + j), offset);
		    reiserfs_leaf_delete_entry (fs, bh, i, j, 1);
		    j --;
		    deh = reiserfs_deh (bh, ih);
		    continue;
		}

		offset = reiserfs_deh_get_off (deh + j);
	    }
	    continue;
	}


	if (!reiserfs_ih_ext (ih))
	    continue;

	/* correct extent items */
	ind_item = (__u32 *)reiserfs_item_by_ih (bh, ih);

	for (j = 0; j < reiserfs_ext_count (ih); j ++) {
	    unfm_ptr = d32_get (ind_item, j);

	    if (!unfm_ptr)
		continue;

	    /* this corruption of extent item had to be fixed in pass0 */
	    if (reiserfs_fs_block(fs, unfm_ptr) != BT_UNKNOWN)
		/*!was_block_used (unfm_ptr))*/
		reiserfs_panic ("%s: block %lu, item %d, pointer %d: The "
				"wrong pointer (%u) in the file %K. Must "
				"be fixed on pass0.",  __FUNCTION__, 
				bh->b_blocknr, i, j, unfm_ptr, &ih->ih_key);

	    /* 1. zero slots pointing to a leaf */
	    if (pass0_block_isleaf (unfm_ptr)) {
		dirty ++;
		d32_put (ind_item, j, 0);
		pass_1_stat (fs)->pointed_leaves ++;
		continue;
	    }

	    /* 2. zero pointers to blocks which are pointed already */
	    if (pass0_block_isbad_unfm (unfm_ptr)) {
		/* this unformatted pointed more than once. Did we see 
		   it already? */
		if (!is_bad_unfm_in_tree_once (unfm_ptr)) {
		    /* keep first reference to it and mark about that in
                       special bitmap */
		    mark_bad_unfm_in_tree_once (unfm_ptr);
		} else {
		    /* Yes, we have seen this pointer already, zero other 
		       pointers to it. */
		    dirty ++;
		    d32_put (ind_item, j, 0);
		    pass_1_stat (fs)->non_unique_pointers ++;
		    continue;
		}
	    } else {
		pass_1_stat (fs)->correct_pointers ++;
	    }
	}
    }

    if (dirty)
	reiserfs_buffer_mkdirty (bh);
}

/* fsck starts creating of this bitmap on pass 1. It will then become
   on-disk bitmap */
static void init_new_bitmap (reiserfs_filsys_t * fs)
{
    unsigned int i;
    unsigned long block;
    unsigned long reserved;
    unsigned long count, bmap_nr;
    
    count = reiserfs_sb_get_blocks (fs->fs_ondisk_sb);
    
    fsck_new_bitmap (fs) = reiserfs_bitmap_create(count);

    /* mark_block_used skips 0, set the bit explicitly */
    reiserfs_bitmap_set_bit (fsck_new_bitmap (fs), 0);

    /* mark other skipped blocks and super block used */
    for (i = 1; i <= fs->fs_super_bh->b_blocknr; i ++)
	mark_block_used (i, 1);

    /* mark bitmap blocks as used */
    block = fs->fs_super_bh->b_blocknr + 1;
    
    bmap_nr = reiserfs_bmap_nr(count, fs->fs_blocksize);

    for (i = 0; i < bmap_nr; i ++) {
	mark_block_used (block, 1);
	if (reiserfs_bitmap_spread (fs)) {
	    block = (block / (fs->fs_blocksize * 8) + 1) * 
		    (fs->fs_blocksize * 8);
	} else {
	    block ++;
	}
    }

    reserved = reiserfs_journal_hostsize (fs->fs_ondisk_sb);
    /* where does journal area (or reserved journal area) start from */

    if (!reiserfs_new_location(fs->fs_super_bh->b_blocknr, fs->fs_blocksize) &&
    	!reiserfs_old_location(fs->fs_super_bh->b_blocknr, fs->fs_blocksize))
    {
	misc_die ("init_new_bitmap: Wrong super block location, "
		  "you must run --rebuild-sb.");
    }

    block = reiserfs_journal_start_must (fs);

    for (i = block; i < reserved + block; i ++)
	mark_block_used (i, 1);
	

    if (fs->fs_badblocks_bm)
    	for (i = 0; i < count; i ++) {
	    if (reiserfs_bitmap_test_bit (fs->fs_badblocks_bm, i)) {
	    	if (reiserfs_bitmap_test_bit (fsck_new_bitmap (fs), i))
	    	    reiserfs_panic ("%s: The block pointer to not data area, "
				    "must be fixed on the pass0.\n",
				    __FUNCTION__);
		
		reiserfs_bitmap_set_bit (fsck_new_bitmap (fs), i);
	    }
    	}
}


/* this makes a map of blocks which can be allocated when fsck will
   continue */
static void find_allocable_blocks (reiserfs_filsys_t * fs) {
    unsigned long i;

    fsck_progress ("Looking for allocable blocks .. ");

    fsck_allocable_bitmap (fs) = reiserfs_bitmap_create 
	    (reiserfs_sb_get_blocks (fs->fs_ondisk_sb));
    reiserfs_bitmap_fill (fsck_allocable_bitmap (fs));

    /* find how many leaves are not pointed by any extent items */
    for (i = 0; i < reiserfs_sb_get_blocks (fs->fs_ondisk_sb); i ++) {
	if (reiserfs_fs_block(fs, i) != BT_UNKNOWN) {
	    /* journal (or reserved for it area), bitmaps, super block and
	       blocks before it */
	    continue;
	}

	if (pass0_block_isgood_unfm (i) && pass0_block_isbad_unfm (i)) {
	    misc_die ("%s: The block (%lu) is marked as good and "
		      "as bad at once.", __FUNCTION__, i);
	}

	if (pass0_block_isgood_unfm (i) || pass0_block_isbad_unfm (i)) {
	    /* blocks which were pointed once or more then once 
	       from extent items - they will not be allocated */
	    continue;
	}

	/* make allocable not leaves, not bad blocks */
	if (!pass0_block_isleaf (i))
	    if (!fs->fs_badblocks_bm || 
		!reiserfs_bitmap_test_bit (fs->fs_badblocks_bm, i))
	    {
		/* this is not leaf and it is not pointed by found extent 
		   items, so it does not contains anything valuable */
		make_allocable (i);
		pass_1_stat (fs)->allocable_blocks ++;
	    }
    }
    
    fsck_progress ("finished\n");

    fs->block_allocator = reiserfsck_new_blocknrs;
    fs->block_deallocator = reiserfsck_free_block;
}

static void fsck_pass1_prep (reiserfs_filsys_t * fs) {
    /* this will become an on-disk bitmap */
    init_new_bitmap (fs);

    /* bitmap of leaves which could not be inserted on pass 1. 
       FIXME: no need to have 1 bit per block */
    fsck_uninsertables (fs) = reiserfs_bitmap_create
	    (reiserfs_sb_get_blocks (fs->fs_ondisk_sb));
    reiserfs_bitmap_fill (fsck_uninsertables (fs));
    
    /* find blocks which can be allocated */
    find_allocable_blocks (fs);

    /* bitmap of bad unformatted nodes which are in the tree already */
    bad_unfm_in_tree_once_bitmap = reiserfs_bitmap_create 
	    (reiserfs_sb_get_blocks (fs->fs_ondisk_sb));

    /* pass 1 does not deal with objectid */
}

static void fsck_pass1_save_result (reiserfs_filsys_t * fs) {
    FILE * file;
    int retval;

    if (!(file = util_file_open("temp_fsck_file.deleteme", "w+")))
	return;

        
    /* to be able to restart with pass 2 we need bitmap of
       uninsertable blocks and bitmap of alocable blocks */
    fsck_stage_start_put (file, PASS_1_DONE);
    reiserfs_bitmap_save (file,  fsck_uninsertables (fs));
    reiserfs_bitmap_save (file,  fsck_allocable_bitmap(fs));
    reiserfs_bitmap_save (file,  fsck_new_bitmap(fs));
    fsck_stage_end_put (file);
    fclose (file);
    retval = rename ("temp_fsck_file.deleteme", state_dump_file (fs));
    if (retval != 0)
	fsck_progress ("pass 1: Could not rename the temporary file "
		       "temp_fsck_file.deleteme to %s",
		       state_dump_file (fs));

    /* to be able to pack/unpack metadata after pass2 in stage-by-stage mode, 
       save into on-disk bitmap all blocks that are involved. 
       it should not be flushed on disk not in stage-by-stage mode, as if fsck 
       fails on pass1 we get wrong bitmap on the next fsck start */
    reiserfs_bitmap_flush (fsck_allocable_bitmap(fs), fs);
}


void fsck_pass1_load_result (FILE * fp, reiserfs_filsys_t * fs) {
    fsck_uninsertables (fs) = reiserfs_bitmap_load (fp);
    fsck_allocable_bitmap (fs) = reiserfs_bitmap_load (fp);
    fsck_new_bitmap (fs) = reiserfs_bitmap_load(fp);
    
    fs->block_allocator = reiserfsck_new_blocknrs;
    fs->block_deallocator = reiserfsck_free_block;

    if (!fsck_new_bitmap (fs) || !fsck_allocable_bitmap (fs) ||
	!fsck_allocable_bitmap (fs))
    {
	fsck_exit ("State dump file seems corrupted. Run without -d");
    }

    /* we need objectid map on pass 2 to be able to relocate files */
    proper_id_map (fs) = id_map_init();
    fetch_objectid_map (proper_id_map (fs), fs);

    fsck_progress ("Pass 1 result loaded. %d blocks used, %d allocable, "
		   "still to be inserted %d\n",
		   reiserfs_bitmap_ones (fsck_new_bitmap (fs)),
		   reiserfs_bitmap_zeros (fsck_allocable_bitmap (fs)),
		   reiserfs_bitmap_zeros (fsck_uninsertables (fs)));
}


extern reiserfs_bitmap_t * leaves_bitmap;

/* reads blocks marked in leaves_bitmap and tries to insert them into
   tree */
static void do_pass_1 (reiserfs_filsys_t * fs)
{
    unsigned long done = 0, total;
    reiserfs_bh_t * bh;
    unsigned long i, n; 
    int what_node;


    /* on pass0 we have found that amount of leaves */
    total = reiserfs_bitmap_ones (leaves_bitmap);

    /* read all leaves found on the pass 0 */
    for (n = 0; n < 2; n++) {
	for (i = 0; i < reiserfs_sb_get_blocks (fs->fs_ondisk_sb); i ++) {
	    if (!pass0_block_isleaf (i))
		continue;

	    if (n && !fsck_bitmap_isuninsert(i))
		continue;
	    
	    if (!fsck_quiet(fs)) {
		util_misc_progress (fsck_progress_file (fs), 
				    &done, total, 1, 0);
	    }

	    /* at least one of nr_to_read blocks is to be checked */
	    bh = reiserfs_buffer_read (fs->fs_dev, i, fs->fs_blocksize);
	    if (!bh) {
		/* we were reading one block at time, and failed, so mark
		    block bad */
		fsck_progress ("pass1: Reading of the block %lu failed\n", i);
		continue;
	    }

	    what_node = reiserfs_node_type (bh);
	    if ( what_node != NT_LEAF ) {
		fsck_info_checkmem();
		misc_die ("build_the_tree: Nothing but leaves "
			  "are expected. Block %lu - %s\n", 
			  i, reiserfs_node_type_name(what_node));
	    }

	    if (is_block_used (i))
		if (!(reiserfs_journal_block (fs, i) &&
		      fsck_data(fs)->rebuild.use_journal_area))
		{
		    /* block is in new tree already */
		    misc_die ("build_the_tree: The leaf (%lu) "
			      "is in the tree already\n", i);
		}

	    /* fprintf (block_list, "leaf %d\n", i + j);*/
	    if (n == 0) {
		pass_1_stat (fs)->leaves ++;

		/* the leaf may still contain extent items with wrong
		   slots. Fix that */
		pass1_correct_leaf (fs, bh);

		if (reiserfs_nh_get_items (NODE_HEAD (bh)) == 0) {
		    /* all items were deleted on pass 0 or pass 1 */
		    reiserfs_buffer_mkclean (bh);
		    reiserfs_buffer_close (bh);
		    make_allocable (i);
		    pass_1_stat (fs)->allocable_blocks ++;
		    continue;
		}
	    
		if (fsck_leaf_check(bh)) {
		    /* FIXME: will die */
		    fsck_log ("%s: WARNING: The leaf (%lu) is formatted "
			      "badly. Will be handled on the the pass2.\n",
			      __FUNCTION__, bh->b_blocknr);

		    fsck_bitmap_mkuninsert (bh->b_blocknr);
		    reiserfs_buffer_close (bh);
		    continue;
		}
	    }

	    if (reiserfs_journal_block (fs, i) && 
		fsck_data(fs)->rebuild.use_journal_area) 
	    {
		/* FIXME: temporary thing */
		if (tree_is_empty ()) {
		    /* we insert inot tree only first leaf of journal */
		    unsigned long block;
		    reiserfs_bh_t * new_bh;

		    block = alloc_block ();
		    if (!block)
			misc_die ("could not allocate block");
			
		    new_bh = reiserfs_buffer_open (bh->b_dev, block, 
						   bh->b_size);
		    
		    memcpy (new_bh->b_data, bh->b_data, bh->b_size);
		    reiserfs_buffer_mkuptodate (new_bh, 1);
		    reiserfs_buffer_mkdirty (new_bh);
		    make_single_leaf_tree (new_bh);
		    reiserfs_buffer_close (new_bh);
		    reiserfs_buffer_close (bh);
		    continue;
		}

		/* other blocks of journal will be inserted in pass 2 */
		fsck_bitmap_mkuninsert (bh->b_blocknr);
		reiserfs_buffer_close (bh);
		continue;
	    }

	    if (!try_to_insert_pointer_to_leaf(bh)) {
		/* Inserted successfully. */
		if (n) {
		    fsck_bitmap_cluninsert (i);
		}
	    } else {
		if (!n) {
		    /* Not inserted on the 1st pass. */
		    fsck_bitmap_mkuninsert (i);
		    done--;
		}
	    }
	    
	    reiserfs_buffer_close (bh);
	}
    }

    if (!fsck_quiet(fs))
	fsck_progress ("\n");
}


static void fsck_pass1_fini (reiserfs_filsys_t * fs) {
    time_t t;

    /* update fsck_state */
    
    /* we  should not flush bitmaps on disk after pass1, because
       new_bitmap contains only those blocks which are good leaves or 
       just allocated internal blocks. */
       
    reiserfs_sb_set_state (fs->fs_ondisk_sb, PASS_1_DONE);
    reiserfs_buffer_mkdirty (fs->fs_super_bh);

    /* write all dirty blocks */
    fsck_progress ("Flushing..");
    fs->fs_dirt = 1;
    reiserfs_fs_flush (fs);
    fsck_progress ("finished\n");

    fsck_stage_report (FS_PASS1, fs);

    /* we do not need this anymore */
    fsck_pass0_aux_fini();
    reiserfs_bitmap_delete (bad_unfm_in_tree_once_bitmap);

    if (!fsck_run_one_step (fs)) {
	if (fsck_info_ask (fs, "Continue? (Yes):", "Yes\n", 1))
	    /* reiserfsck continues */
	    return;
    } else
	fsck_pass1_save_result (fs);

    if (proper_id_map (fs)) {
	/* when we run pass 1 only - we do not have proper_id_map */
	id_map_free(proper_id_map (fs));
	proper_id_map (fs) = 0;
    }
    
    time (&t);
    fsck_progress ("###########\n"
		   "reiserfsck finished pass 1 at %s"
		   "###########\n", ctime (&t));
    fs->fs_dirt = 1;
    reiserfs_fs_close (fs);
    exit(0);
}


void fsck_pass1 (reiserfs_filsys_t * fs) {
    fsck_progress ("Pass 1 (will try to insert %lu leaves):\n",
		   reiserfs_bitmap_ones (fsck_source_bitmap (fs)));
    if (fsck_log_file (fs) != stderr)
	fsck_log ("####### Pass 1 #######\n");


    fsck_pass1_prep (fs);

    /* try to insert leaves found during pass 0 */
    do_pass_1 (fs);

    fsck_pass1_fini (fs);
}

