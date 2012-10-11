/*
 * Copyright 1996-2002 Hans Reiser
 */
#include "fsck.h"
#include <stdlib.h>

reiserfs_bitmap_t * bad_unfm_in_tree_once_bitmap;


//int step = 0; // 0 - find stat_data or any item ; 1 - find item ; 2 - already found


/* allocates buffer head and copy buffer content */
struct buffer_head * make_buffer (int dev, unsigned long blocknr, int size, char * data)
{
    struct buffer_head * bh;
    
    bh = getblk (dev, blocknr, size);
    if (buffer_uptodate (bh))
	return bh;
//    die ("make_buffer: uptodate buffer found");
    memcpy (bh->b_data, data, size);
    set_bit (BH_Uptodate, (char *)&bh->b_state);
    return bh;
}


int find_not_of_one_file(struct key * to_find, struct key * key)
{
    if ((get_key_objectid (to_find) != -1) &&
        (get_key_objectid (to_find) != get_key_objectid (key)))
        return 1;
    if ((get_key_dirid (to_find) != -1) &&
        (get_key_dirid (to_find) != get_key_dirid (key)))
        return 1;
    return 0;
}


int is_item_reachable (struct item_head * ih)
{
    return ih_reachable (ih) ? 1 : 0;
}


void mark_item_unreachable (struct item_head * ih)
{
    clean_ih_flags (ih);
    mark_ih_unreachable (ih);

    if (is_indirect_ih (ih))
	set_ih_free_space (ih, 0);
}


void mark_item_reachable (struct item_head * ih, struct buffer_head * bh)
{
    mark_ih_reachable (ih);
    mark_buffer_dirty (bh);
}

static void stat_data_in_tree (struct buffer_head *bh,
			       struct item_head * ih)
{
#if 0
    __u32 objectid;
    
    objectid = get_key_objectid (&ih->ih_key);
    
    if (mark_objectid_really_used (proper_id_map (fs), objectid)) {
	stat_shared_objectid_found (fs);
	mark_objectid_really_used (shared_id_map (fs), objectid);
    }
#endif
    
    zero_nlink (ih, B_I_PITEM (bh, ih));
}


/* this just marks blocks pointed by an indirect item as used in the
   new bitmap */
static void indirect_in_tree (struct buffer_head * bh,
			      struct item_head * ih)
{
    int i;
    __u32 * unp;
    __u32 unfm_ptr;

    unp = (__u32 *)B_I_PITEM (bh, ih);
    
    for (i = 0; i < I_UNFM_NUM (ih); i ++) {
	unfm_ptr = le32_to_cpu (unp[i]);
	if (unfm_ptr == 0)
	    continue;
	if (still_bad_unfm_ptr_1 (unfm_ptr))
	    reiserfs_panic ("mark_unformatted_used: (%lu: %k) "
			    "still has bad pointer %lu",
			    bh->b_blocknr, &ih->ih_key, unfm_ptr);
	
	mark_block_used (unfm_ptr, 1);
    }
}


static void leaf_is_in_tree_now (struct buffer_head * bh)
{
    item_action_t actions[] = {stat_data_in_tree, indirect_in_tree, 0, 0};

    mark_block_used ((bh)->b_blocknr, 1);

    for_every_item (bh, mark_item_unreachable, actions);

    pass_1_stat (fs)->inserted_leaves ++;

    mark_buffer_dirty (bh);
}


static void insert_pointer (struct buffer_head * bh, struct path * path)
{
    struct item_head * ih;
    char * body;
    int zeros_number;
    int retval;
    struct tree_balance tb;
    
    init_tb_struct (&tb, fs, path, 0x7fff);

    /* fix_nodes & do_balance must work for internal nodes only */
    ih = 0;

    retval = fix_nodes (/*tb.transaction_handle,*/ M_INTERNAL, &tb, ih);
    if (retval != CARRY_ON)
	die ("insert_pointer: fix_nodes failed with retval == %d", retval);
    
    /* child_pos: we insert after position child_pos: this feature of the insert_child */
    /* there is special case: we insert pointer after
       (-1)-st key (before 0-th key) in the parent */
    if (PATH_LAST_POSITION (path) == 0 && path->pos_in_item == 0)
	PATH_H_B_ITEM_ORDER (path, 0) = -1;
    else {
	if (PATH_H_PPARENT (path, 0) == 0)
	    PATH_H_B_ITEM_ORDER (path, 0) = 0;
/*    PATH_H_B_ITEM_ORDER (path, 0) = PATH_H_PPARENT (path, 0) ? PATH_H_B_ITEM_ORDER (path, 0) : 0;*/
    }
    
    ih = 0;
    body = (char *)bh;
    //memmode = 0;
    zeros_number = 0;
    
    do_balance (&tb, ih, body, M_INTERNAL, zeros_number);

    leaf_is_in_tree_now (bh);
}


/* return 1 if left and right can be joined. 0 otherwise */
int balance_condition_fails (struct buffer_head * left, struct buffer_head * right)
{
    if (B_FREE_SPACE (left) >= B_CHILD_SIZE (right) -
	(are_items_mergeable (B_N_PITEM_HEAD (left, B_NR_ITEMS (left) - 1), B_N_PITEM_HEAD (right, 0), left->b_size) ? IH_SIZE : 0))
	return 1;
    return 0;
}


/* return 1 if new can be joined with last node on the path or with
   its right neighbor, 0 otherwise */
int balance_condition_2_fails (struct buffer_head * new, struct path * path)
{
    struct buffer_head * bh;
    struct key * right_dkey;
    int pos, used_space;
    
    bh = PATH_PLAST_BUFFER (path);
    

    if (balance_condition_fails (bh, new))
	/* new node can be joined with last buffer on the path */
	return 1;
    
    /* new node can not be joined with its left neighbor */
    
    right_dkey = uget_rkey (path);
    if (right_dkey == 0)
	/* there is no right neighbor */
	return 0;
    
    pos = PATH_H_POSITION (path, 1);
    if (pos == B_NR_ITEMS (bh = PATH_H_PBUFFER (path, 1))) {
	/* we have to read parent of right neighbor. For simplicity we
	   call search_by_key, which will read right neighbor as well */
	INITIALIZE_PATH(path_to_right_neighbor);
	
	if (reiserfs_search_by_key_4 (fs, right_dkey, &path_to_right_neighbor) != ITEM_FOUND)
	    die ("get_right_neighbor_free_space: invalid right delimiting key");
	used_space =  B_CHILD_SIZE (PATH_PLAST_BUFFER (&path_to_right_neighbor));
	pathrelse (&path_to_right_neighbor);
    }
    else
	used_space = get_dc_child_size (B_N_CHILD (bh, pos + 1));
    
    if (B_FREE_SPACE (new) >= used_space -
	(are_items_mergeable (B_N_PITEM_HEAD (new, B_NR_ITEMS (new) - 1), (struct item_head *)right_dkey, new->b_size) ? IH_SIZE : 0))
	return 1;

    return 0;
}


static void get_max_buffer_key (struct buffer_head * bh, struct key * key)
{
    struct item_head * ih;

    ih = B_N_PITEM_HEAD (bh, B_NR_ITEMS (bh) - 1);
    copy_key (key, &(ih->ih_key));

    if (is_direntry_key (key)) {
	/* copy deh_offset 3-rd and 4-th key components of the last entry */
	set_offset (KEY_FORMAT_1, key, 
		    get_deh_offset (B_I_DEH (bh, ih) + get_ih_entry_count (ih) - 1));

    } else if (!is_stat_data_key (key))
	/* get key of the last byte, which is contained in the item */
	set_offset (key_format (key), key, get_offset (key) + get_bytes_number (ih, bh->b_size) - 1);
}


int tree_is_empty (void)
{
    return (get_sb_root_block (fs->fs_ondisk_sb) == ~0) ? 1 : 0;
}


void make_single_leaf_tree (struct buffer_head * bh)
{
    /* tree is empty, make tree root */
    set_sb_root_block (fs->fs_ondisk_sb, bh->b_blocknr);
    set_sb_tree_height (fs->fs_ondisk_sb, 2);
    mark_buffer_dirty (fs->fs_super_bh);
    leaf_is_in_tree_now (bh);
}


/* inserts pointer to leaf into tree if possible. If not, marks node as
   uninsertable in special bitmap */
static void try_to_insert_pointer_to_leaf (struct buffer_head * new_bh)
{
    INITIALIZE_PATH (path);
    struct buffer_head * bh;			/* last path buffer */
    struct key * first_bh_key, last_bh_key;	/* first and last keys of new buffer */
    struct key last_path_buffer_last_key, * right_dkey;
    int ret_value;

    if (tree_is_empty () == 1) {
	make_single_leaf_tree (new_bh);
	return;
    }

    first_bh_key = B_N_PKEY (new_bh, 0);
    
    /* try to find place in the tree for the first key of the coming node */
    ret_value = reiserfs_search_by_key_4 (fs, first_bh_key, &path);
    if (ret_value == ITEM_FOUND)
	goto cannot_insert;

    /* get max key in the new node */
    get_max_buffer_key (new_bh, &last_bh_key);

    bh = PATH_PLAST_BUFFER (&path);
    if (comp_keys (B_N_PKEY (bh, 0), &last_bh_key) == 1/* first is greater*/) {
	/* new buffer falls before the leftmost leaf */
	if (balance_condition_fails (new_bh, bh))
	    goto cannot_insert;
	
	if (uget_lkey (&path) != 0 || PATH_LAST_POSITION (&path) != 0)
	    die ("try_to_insert_pointer_to_leaf: bad search result");
	
	path.pos_in_item = 0;
	goto insert;
    }
    
    /* get max key of buffer, that is in tree */
    get_max_buffer_key (bh, &last_path_buffer_last_key);
    if (comp_keys (&last_path_buffer_last_key, first_bh_key) != -1/* second is greater */)
	/* first key of new buffer falls in the middle of node that is in tree */
	goto cannot_insert;
    
    right_dkey = uget_rkey (&path);
    if (right_dkey && comp_keys (right_dkey, &last_bh_key) != 1 /* first is greater */)
	goto cannot_insert;
    
    if (balance_condition_2_fails (new_bh, &path))
	goto cannot_insert;


 insert:
    insert_pointer (new_bh, &path);
    goto out;
    
 cannot_insert:
    /* statistic */

    mark_block_uninsertable (new_bh->b_blocknr);
    
 out:
    pathrelse (&path);
    return;
}



/* everything should be correct already in the leaf but contents of indirect
   items. So we only
   1. zero slots pointing to a leaf
   2. zero pointers to blocks which are pointed already
   3. what we should do with directory entries hashed by another hash?
   they are deleted for now
*/
static void pass1_correct_leaf (reiserfs_filsys_t * fs,
				struct buffer_head * bh)
{
    int i, j;
    struct item_head * ih;
    __u32 * ind_item;
    unsigned long unfm_ptr;
    int dirty = 0;


    ih = B_N_PITEM_HEAD (bh, 0);
    for (i = 0; i < B_NR_ITEMS (bh); i ++, ih ++) {
	if (is_direntry_ih (ih)) {
	    struct reiserfs_de_head * deh;
	    char * name;
	    int name_len;
	    int hash_code;

	    deh = B_I_DEH (bh, ih);
	    for (j = 0; j < get_ih_entry_count (ih); j ++) {
		name = name_in_entry (deh + j, j);
		name_len = name_in_entry_length (ih, deh + j, j);

		if ((j == 0 && is_dot (name, name_len)) ||
		    (j == 1 && is_dot_dot (name, name_len))) {
		    continue;
		}

		hash_code = find_hash_in_use (name, name_len,
					      GET_HASH_VALUE (get_deh_offset (deh + j)),
					      get_sb_hash_code (fs->fs_ondisk_sb));
		if (hash_code != get_sb_hash_code (fs->fs_ondisk_sb)) {
		    fsck_log ("pass1: block %lu, %H, entry \"%.*s\" "
			      "hashed with %s whereas proper hash is %s\n",
			      bh->b_blocknr, ih, name_len, name, 
			      code2name (hash_code), code2name (get_sb_hash_code (fs->fs_ondisk_sb)));
		    if (get_ih_entry_count (ih) == 1) {
			delete_item (fs, bh, i);
			i --;
			ih --;
			break;
		    } else {
			cut_entry (fs, bh, i, j, 1);
			j --;
			deh = B_I_DEH (bh, ih);
		    }
		}
	    }
	    continue;
	}


	if (!is_indirect_ih (ih))
	    continue;

	/* correct indirect items */
	ind_item = (__u32 *)B_I_PITEM (bh, ih);

	for (j = 0; j < I_UNFM_NUM (ih); j ++, ind_item ++) {
	    unfm_ptr = le32_to_cpu (*ind_item);

	    if (!unfm_ptr)
		continue;

	    /* this corruption of indirect item had to be fixed in pass0 */
	    if (not_data_block (fs, unfm_ptr) || unfm_ptr >= get_sb_block_count (fs->fs_ondisk_sb))
		/*!was_block_used (unfm_ptr))*/
		reiserfs_panic ("pass1_correct_leaf: (%lu: %k), %d-th slot is not fixed",
				bh->b_blocknr, &ih->ih_key, j);

	    /* 1. zero slots pointing to a leaf */
	    if (is_used_leaf (unfm_ptr)) {
		dirty ++;
		*ind_item = 0;
		pass_1_stat (fs)->pointed_leaves ++;
		continue;
	    }

	    /* 2. zero pointers to blocks which are pointed already */
	    if (is_bad_unformatted (unfm_ptr)) {
		/* this unformatted pointed more than once. Did we see it already? */
		if (!is_bad_unfm_in_tree_once (unfm_ptr))
		    /* keep first reference to it and mark about that in
                       special bitmap */
		    mark_bad_unfm_in_tree_once (unfm_ptr);
		else {
		    /* Yes, we have seen this pointer already, zero other pointers to it. */
		    dirty ++;
		    *ind_item = 0;
		    pass_1_stat (fs)->non_unique_pointers ++;
		    continue;
		}
	    } else
		pass_1_stat (fs)->correct_pointers ++;
	}
    }

    if (dirty)
	mark_buffer_dirty (bh);
}


/*######### has to die ##########*/
/* append item to end of list. Set head if it is 0. For indirect item
   set wrong unformatted node pointers to 0 */
void save_item (struct si ** head, struct buffer_head * bh, struct item_head * ih, char * item)
{
    struct si * si, * cur;

    if (is_bad_item (bh, ih, item/*, fs->s_blocksize, fs->s_dev*/)) {
	return;
    }

    if (is_indirect_ih (ih)) {
	fsck_progress ("save_item (block %lu: %H (should not happen)\n", ih);
    }
    
    pass_1_stat (fs)->saved_items ++;

    si = getmem (sizeof (*si));
    si->si_dnm_data = getmem (get_ih_item_len(ih));
    /*si->si_blocknr = blocknr;*/
    memcpy (&(si->si_ih), ih, IH_SIZE);
    memcpy (si->si_dnm_data, item, get_ih_item_len(ih));

    // changed by XB
    si->last_known = NULL;

    if (*head == 0)
	*head = si;
    else {
	cur = *head;
	// changed by XB
	//    while (cur->si_next)
	//      cur = cur->si_next;

	{
	  int count = 0;
	  int speedcount = 0;
	  
	  while (cur->si_next) {
	    if (cur->last_known!=NULL) {
	      cur = cur->last_known; // speed up to the end if the chain
	      speedcount++;
	    } else {
	      cur = cur->si_next;
	      count++;
	    }
	  }
	  
	  if ((*head)!=cur) // no self referencing loop please
	    (*head)->last_known = cur;
	}
	
	cur->si_next = si;
    }
    return;
}


struct si * remove_saved_item (struct si * si)
{
    struct si * tmp = si->si_next;
    
    freemem (si->si_dnm_data);
    freemem (si);
    return tmp;
}


#if 0
static void save_items (struct si ** head, struct buffer_head * bh)
{
    int i;
    struct item_head * ih;

    ih = B_N_PITEM_HEAD (bh, 0);
    for (i = 0; i < B_NR_ITEMS (bh); i ++, ih ++) {
	save_item (head, bh, ih, B_I_PITEM (bh, ih));
    }
}

/* insert_item_separately */
static void put_saved_items_into_tree_1 (struct si * si)
{
    while (si) {
	insert_item_separately (&(si->si_ih), si->si_dnm_data,
				0/*was not in tree*/);
	si = remove_saved_item (si);
    }
}
#endif


/* fsck starts creating of this bitmap on pass 1. It will then become
   on-disk bitmap */
static void init_new_bitmap (reiserfs_filsys_t * fs)
{
    int i;
    unsigned long block;
    unsigned long reserved;
    

    fsck_new_bitmap (fs) = reiserfs_create_bitmap (get_sb_block_count (fs->fs_ondisk_sb));

    /* mark_block_used skips 0, set the bit explicitly */
    reiserfs_bitmap_set_bit (fsck_new_bitmap (fs), 0);

    /* mark other skipped blocks and super block used */
    for (i = 1; i <= fs->fs_super_bh->b_blocknr; i ++)
	mark_block_used (i, 1);

    /* mark bitmap blocks as used */
    block = fs->fs_super_bh->b_blocknr + 1;
    for (i = 0; i < get_sb_bmap_nr (fs->fs_ondisk_sb); i ++) {
	mark_block_used (block, 1);
	if (spread_bitmaps (fs))
	    block = (block / (fs->fs_blocksize * 8) + 1) * (fs->fs_blocksize * 8);
	else
	    block ++;
    }

    reserved = get_size_of_journal_or_reserved_area (fs->fs_ondisk_sb);
    /* where does journal area (or reserved journal area) start from */

    if (!is_new_sb_location (fs->fs_super_bh->b_blocknr, fs->fs_blocksize) &&
    	!is_old_sb_location (fs->fs_super_bh->b_blocknr, fs->fs_blocksize))
	die ("init_new_bitmap: wrong super block");

    block = get_journal_start_must (fs);

    for (i = block; i < reserved + block; i ++)
	mark_block_used (i, 1);
	

    if (fs->fs_badblocks_bm)
    	for (i = 0; i < get_sb_block_count (fs->fs_ondisk_sb); i ++) {
	    if (reiserfs_bitmap_test_bit (fs->fs_badblocks_bm, i)) {
	    	if (reiserfs_bitmap_test_bit (fsck_new_bitmap (fs), i))
	    	    reiserfs_panic ("%s: bad block pointer to not data area, cannot happen\n",
	    	    	__FUNCTION__);	    	
		reiserfs_bitmap_set_bit (fsck_new_bitmap (fs), i);
	    }
    	}

#if 0
    /* mark journal area as used if journal is standard or it is non standard
       and initialy has been created on a main device */
    reserved = 0;
    if (!is_reiserfs_jr_magic_string (fs->fs_ondisk_sb))
	reserved = get_jp_journal_size (sb_jp (fs->fs_ondisk_sb));
    if (get_sb_reserved_for_journal (fs->fs_ondisk_sb))
	reserved = get_sb_reserved_for_journal (fs->fs_ondisk_sb);
    if (reserved) {
	for (i = 0; i <= reserved; i ++)
	    mark_block_used (i + get_jp_journal_1st_block (sb_jp
							   (fs->fs_ondisk_sb)));
    }
#endif
}


/* this makes a map of blocks which can be allocated when fsck will
   continue */
static void find_allocable_blocks (reiserfs_filsys_t * fs)
{
    int i;

    fsck_progress ("Looking for allocable blocks .. ");

    fsck_allocable_bitmap (fs) = reiserfs_create_bitmap (get_sb_block_count (fs->fs_ondisk_sb));
    reiserfs_bitmap_fill (fsck_allocable_bitmap (fs));


    /* find how many leaves are not pointed by any indirect items */
    for (i = 0; i < get_sb_block_count (fs->fs_ondisk_sb); i ++) {
	if (not_data_block (fs, i))
	    /* journal (or reserved for it area), bitmaps, super block and
	       blocks before it */
	    continue;

	if (is_good_unformatted (i) && is_bad_unformatted (i))
	    die ("find_allocable_blocks: bad and good unformatted");

	if (is_good_unformatted (i) || is_bad_unformatted (i)) {
	    /* blocks which were pointed once or more thn onec from indirect
	       items - they will not be allocated */
	    continue;
	}

	/* make allocable not leaves, not bad blocks */
	if (!is_used_leaf (i) &&
	    (!fs->fs_badblocks_bm || !reiserfs_bitmap_test_bit (fs->fs_badblocks_bm, i))) {
	    /* this is not leaf and it is not pointed by found indirect items,
               so it does not contains anything valuable */
	    make_allocable (i);
	    pass_1_stat (fs)->allocable_blocks ++;
	}
    }
    fsck_progress ("ok\n");

    fs->block_allocator = reiserfsck_reiserfs_new_blocknrs;
    fs->block_deallocator = reiserfsck_reiserfs_free_block;
}


static void before_pass_1 (reiserfs_filsys_t * fs)
{
    /* this will become an on-disk bitmap */
    init_new_bitmap (fs);

    /* bitmap of leaves which could not be inserted on pass 1. FIXME:
       no need to have 1 bit per block */
    fsck_uninsertables (fs) = reiserfs_create_bitmap (get_sb_block_count (fs->fs_ondisk_sb));
    reiserfs_bitmap_fill (fsck_uninsertables (fs));
    
    /* find blocks which can be allocated */
    find_allocable_blocks (fs);

    /* bitmap of bad unformatted nodes which are in the tree already */
    bad_unfm_in_tree_once_bitmap = reiserfs_create_bitmap (get_sb_block_count (fs->fs_ondisk_sb));

    /* pass 1 does not deal with objectid */
}


static void save_pass_1_result (reiserfs_filsys_t * fs)
{
    FILE * file;
    int retval;

    file = open_file("temp_fsck_file.deleteme", "w+");
    if (!file)
	return;

    /* to be able to get a new bitmap on pass2 we should flush it on disk
       new_bitmap should not be flushed on disk if run w/out -d option, as
       if fsck fails on pass1 we get wrong bitmap on the next fsck start */
    reiserfs_flush_to_ondisk_bitmap (fsck_new_bitmap (fs), fs);
    
    /* to be able to restart with pass 2 we need bitmap of
       uninsertable blocks and bitmap of alocable blocks */
    reiserfs_begin_stage_info_save(file, PASS_1_DONE);
    reiserfs_bitmap_save (file,  fsck_uninsertables (fs));
    reiserfs_bitmap_save (file,  fsck_allocable_bitmap(fs));
    reiserfs_end_stage_info_save (file);
    close_file (file);
    retval = unlink (state_dump_file (fs));
    retval = rename ("temp_fsck_file.deleteme", state_dump_file (fs));
    if (retval != 0)
	fsck_progress ("pass 1: could not rename temp file temp_fsck_file.deleteme to %s",
		       state_dump_file (fs));
}


void load_pass_1_result (FILE * fp, reiserfs_filsys_t * fs)
{
    fsck_new_bitmap (fs) = reiserfs_create_bitmap (get_sb_block_count (fs->fs_ondisk_sb));
    reiserfs_bitmap_copy (fsck_new_bitmap (fs), fs->fs_bitmap2);
    
    fsck_uninsertables (fs) = reiserfs_bitmap_load (fp);
    fsck_allocable_bitmap (fs) = reiserfs_bitmap_load (fp);
    
    fs->block_allocator = reiserfsck_reiserfs_new_blocknrs;
    fs->block_deallocator = reiserfsck_reiserfs_free_block;

    if (!fsck_new_bitmap (fs) || !fsck_allocable_bitmap (fs) ||
	!fsck_allocable_bitmap (fs))
	fsck_exit ("State dump file seems corrupted. Run without -d");


    /* we need objectid map on pass 2 to be able to relocate files */
    proper_id_map (fs) = init_id_map ();
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
    struct buffer_head * bh;
    int i; 
    int what_node;
    unsigned long done = 0, total;


    /* on pass0 we have found that amount of leaves */
    total = reiserfs_bitmap_ones (leaves_bitmap);

    /* read all leaves found on the pass 0 */
    for (i = 0; i < get_sb_block_count (fs->fs_ondisk_sb); i ++) {
	if (!is_used_leaf (i))
	    continue;

	print_how_far (fsck_progress_file (fs), &done, total, 1, fsck_quiet (fs));

	/* at least one of nr_to_read blocks is to be checked */
	bh = bread (fs->fs_dev, i, fs->fs_blocksize);
	if (!bh) {
	    /* we were reading one block at time, and failed, so mark
	       block bad */
	    fsck_progress ("pass1: reading block %lu failed\n", i);
	    continue;
	}

	what_node = who_is_this (bh->b_data, bh->b_size);
	if ( what_node != THE_LEAF ) {
	    fsck_progress ("build_the_tree: nothing but leaves are expected. "
			   "Block %lu - %s\n", i,
			   (what_node == THE_INTERNAL) ? "internal" : "??");
	    brelse (bh);
	    continue;
	}
	
	if (is_block_used (i) && !(block_of_journal (fs, i) &&
				   fsck_data(fs)->rebuild.use_journal_area))
	    /* block is in new tree already */
	    die ("build_the_tree: leaf (%u) is in tree already\n", i);
		    
	/* fprintf (block_list, "leaf %d\n", i + j);*/
	pass_1_stat (fs)->leaves ++;

	/* the leaf may still contain indirect items with wrong
	   slots. Fix that */
	pass1_correct_leaf (fs, bh);

	if (get_blkh_nr_items (B_BLK_HEAD (bh)) == 0) {
	    /* all items were deleted on pass 0 or pass 1 */
	    mark_buffer_clean (bh);
	    brelse (bh);
	    continue;
	}

	if (is_leaf_bad (bh)) {
	    /* FIXME: will die */
	    fsck_log ("pass1: (is_leaf_bad) bad leaf (%lu)\n", bh->b_blocknr);
	    
	    /* Save good items only to put them into tree at the
	       end of this pass */
	    //save_items (&saved_items, bh);
	    mark_block_uninsertable (bh->b_blocknr);
	    brelse (bh);
	    continue;
	}
	
	if (block_of_journal (fs, i) && fsck_data(fs)->rebuild.use_journal_area) {
	    /* FIXME: temporary thing */
	    if (tree_is_empty ()) {
		/* we insert inot tree only first leaf of journal */
		unsigned long block;
		struct buffer_head * new_bh;
		
		block = alloc_block ();
		if (!block)
		    die ("could not allocate block");
		new_bh = getblk (bh->b_dev, block, bh->b_size);
		memcpy (new_bh->b_data, bh->b_data, bh->b_size);
		mark_buffer_uptodate (new_bh, 1);
		mark_buffer_dirty (new_bh);
		make_single_leaf_tree (new_bh);
		brelse (new_bh);
		brelse (bh);
		continue;
	    }

	    /* other blocks of journal will be inserted in pass 2 */
	    mark_block_uninsertable (bh->b_blocknr);
	    brelse (bh);
	    continue;
	}
	
	try_to_insert_pointer_to_leaf (bh);
	brelse (bh);
    }

    fsck_progress ("\n");

}


static void after_pass_1 (reiserfs_filsys_t * fs)
{
    time_t t;

    /* update fsck_state */
    
    /* we  should not flush bitmaps on disk after pass1, because
       new_bitmap contains only those blocks which are good leaves or 
       just allocated internal blocks. */
       
    set_sb_fs_state (fs->fs_ondisk_sb, PASS_1_DONE);
    mark_buffer_dirty (fs->fs_super_bh);

    /* write all dirty blocks */
    fsck_progress ("Flushing..");
    fs->fs_dirt = 1;
    reiserfs_flush (fs);
    fsck_progress ("done\n");

    stage_report (1, fs);

    /* we do not need this anymore */
    delete_aux_bitmaps ();
    reiserfs_delete_bitmap (bad_unfm_in_tree_once_bitmap);

    if (!fsck_run_one_step (fs)) {
	if (fsck_user_confirmed (fs, "Continue? (Yes):", "Yes\n", 1))
	    /* reiserfsck continues */
	    return;
    } else
	save_pass_1_result (fs);

    if (proper_id_map (fs)) {
	/* when we run pass 1 only - we do not have proper_id_map */
	free_id_map (proper_id_map (fs));
	proper_id_map (fs) = 0;
    }
    
    time (&t);
    fsck_progress ("###########\n"
		   "reiserfsck finished pass 1 at %s"
		   "###########\n", ctime (&t));
    fs->fs_dirt = 1;
    reiserfs_close (fs);
    exit (4);
}


void pass_1 (reiserfs_filsys_t * fs)
{
    fsck_progress ("\nPass 1 (will try to insert %lu leaves):\n",
		   reiserfs_bitmap_ones (fsck_source_bitmap (fs)));
    if (fsck_log_file (fs) != stderr)
	fsck_log ("####### Pass 1 #######\n");


    before_pass_1 (fs);

    /* try to insert leaves found during pass 0 */
    do_pass_1 (fs);

    after_pass_1 (fs);
}


#if 0

/* pass the S+ tree of filesystem */
void recover_internal_tree (struct super_block * s)
{
    check_internal_structure(s);
    build_the_tree();
}
#endif
