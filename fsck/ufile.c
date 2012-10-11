/*
 * Copyright 1996-2002 Hans Reiser
 */
#include "fsck.h"

static int do_items_have_the_same_type (struct item_head * ih, struct key * key)
{
    return (get_type (&ih->ih_key) == get_type (key)) ? 1 : 0;
}

static int are_items_in_the_same_node (struct path * path)
{
  return (PATH_LAST_POSITION (path) < B_NR_ITEMS (PATH_PLAST_BUFFER (path)) - 1) ? 1 : 0;
}


int do_make_tails ()
{
    return 1;/*SB_MAKE_TAIL_FLAG (&g_sb) == MAKE_TAILS ? YES : NO;*/
}


static void cut_last_unfm_pointer (struct path * path, struct item_head * ih)
{
    set_ih_free_space(ih, 0);
    if (I_UNFM_NUM (ih) == 1)
	reiserfsck_delete_item (path, 0);
    else
	reiserfsck_cut_from_item (path, -((int)UNFM_P_SIZE));
}

/*
    if this is not a symlink - make it of_this_size;
    otherwise find a size and return it in symlink_size;
*/
static unsigned long indirect_to_direct (struct path * path, __u64 len, int symlink)
{
    struct buffer_head * bh = PATH_PLAST_BUFFER (path);
    struct item_head * ih = PATH_PITEM_HEAD (path);
    __u32 unfm_ptr;
    struct buffer_head * unfm_bh = 0;
    struct item_head ins_ih;
    char * buf;
    __u32 * indirect;
    char bad_drct[fs->fs_blocksize];

    /* direct item to insert */
    memset (&ins_ih, 0, sizeof (ins_ih));
    if (symlink) {
	set_ih_key_format (&ins_ih, KEY_FORMAT_1);
    } else {
	set_ih_key_format (&ins_ih, get_ih_key_format (ih));
    }
    set_key_dirid (&ins_ih.ih_key, get_key_dirid (&ih->ih_key));
    set_key_objectid (&ins_ih.ih_key, get_key_objectid (&ih->ih_key));
    set_type_and_offset (get_ih_key_format (ih), &ins_ih.ih_key,
			 get_offset (&ih->ih_key) + (I_UNFM_NUM (ih) - 1) * bh->b_size, TYPE_DIRECT);

    // we do not know what length this item should be
    indirect = get_item (path);
    unfm_ptr = le32_to_cpu (indirect [I_UNFM_NUM (ih) - 1]);
    if (unfm_ptr && (unfm_bh = bread (bh->b_dev, unfm_ptr, bh->b_size))) {
        /* we can read the block */
	buf = unfm_bh->b_data;
    } else {
	/* we cannot read the block */
 	if (unfm_ptr)
	    fsck_log ("indirect_to_direct: could not read block %lu (%K)\n", unfm_ptr, &ih->ih_key);
	memset (bad_drct, 0, fs->fs_blocksize);
	buf = bad_drct;
    }
/*
    if (len > MAX_DIRECT_ITEM_LEN (fs->fs_blocksize)) {
	fsck_log ("indirect_to_direct: caanot create such a long item %d (%K), "
	    "Cutting it down to %d byte\n", len,  &ih->ih_key, MAX_DIRECT_ITEM_LEN (fs->fs_blocksize) - 8);
	len = MAX_DIRECT_ITEM_LEN (fs->fs_blocksize) - 8;
    }
    
    if (!len) {
	buf = bad_link;
	len = strlen (bad_link);
    }
*/
    set_ih_item_len (&ins_ih, (get_ih_key_format (ih) == KEY_FORMAT_2) ? ROUND_UP(len) : len);
    set_ih_free_space (&ins_ih, MAX_US_INT);


    // last last unformatted node pointer
    path->pos_in_item = I_UNFM_NUM (ih) - 1;
    cut_last_unfm_pointer (path, ih);

    /* insert direct item */
    if (reiserfs_search_by_key_4 (fs, &(ins_ih.ih_key), path) == ITEM_FOUND)
	die ("indirect_to_direct: key must be not found");
    reiserfsck_insert_item (path, &ins_ih, (const char *)(buf));

    brelse (unfm_bh);

    /* put to stat data offset of first byte in direct item */
    return get_offset (&ins_ih.ih_key); //offset;
}


__u64 get_min_bytes_number (struct item_head * ih, int blocksize)
{
    switch (get_type (&ih->ih_key)) {
    case TYPE_DIRECT:
	if (fs->fs_format == REISERFS_FORMAT_3_6)
	    return ROUND_UP(get_ih_item_len (ih) - 8);
        else
	    return get_ih_item_len (ih);

    case TYPE_INDIRECT:
	return (I_UNFM_NUM(ih) - 1) * blocksize;
    }
    fsck_log ("get_min_bytes_number: called for wrong type of item %H\n", ih);
    return 0;
}

/*  start_key is the key after which all items need to be deleted
    save_here is a pointer where deleted items need to be saved if save is set.
 */
static void delete_file_items_after_key(struct key * start_key, struct si ** save_here) {
    struct path path;
    struct key key = *start_key;
    struct key * rkey;

    while (1) {
	reiserfs_search_by_key_4 (fs, &key, &path);
	
	if (get_item_pos (&path) == B_NR_ITEMS (get_bh (&path))) {
	    rkey = uget_rkey (&path);
	    if (rkey && !not_of_one_file (&key, rkey)) {
		/* file continues in the right neighbor */
		copy_key (&key, rkey);
		pathrelse (&path);
		continue;
	    }
	    /* there is no more items with this key */
	    pathrelse (&path);
	    break;
	}

	if (is_stat_data_ih (get_ih(&path)))
	    fix_obviously_wrong_sd_mode (&path);
	    	
	rkey = &(get_ih (&path))->ih_key;
	if (not_of_one_file (&key, rkey)) {
	    /* there are no more item with this key */
	    pathrelse (&path);
	    break;
	}

	/* ok, item found, but make sure that it is not a directory one */
	if ((is_stat_data_key (rkey) && !not_a_directory (get_item (&path))) ||
	    (is_direntry_key (rkey)))
	    reiserfs_panic ("rewrite_file: no directory items of %K are expected", &key);

	if (save_here != NULL)
	    *save_here = save_and_delete_file_item (*save_here, &path);
	else
	    reiserfsck_delete_item (&path, 0);
    }
}


/* returns 1 when file looks correct, -1 if directory items appeared
   there, 0 - only holes in the file found */
/* when it returns, key->k_offset is offset of the last item of file */
/* sd_size is zero if we do not need to convert any indirect to direct */
int are_file_items_correct (struct item_head * sd_ih, void * sd, __u64 * size, __u32 * blocks,
				int mark_passed_items, int * symlink)
{
    struct path path;
    struct item_head * ih;
    struct key * next_key, * key;
    __u32 sd_first_direct_byte = 0;
    __u64 sd_size;
    int retval, i, was_tail = 0;
    int had_direct = 0;
    int key_version = get_ih_key_format (sd_ih);
    int next_is_another_object = 0;
    __u64 last_unfm_offset = 0;
    int will_convert = 0;
    int should_convert;

    should_convert = (fsck_mode (fs) != FSCK_REBUILD) || mark_passed_items;
    key = &sd_ih->ih_key;
    get_sd_size (sd_ih, sd, &sd_size);

    if (key_version == KEY_FORMAT_1)
    	get_sd_first_direct_byte (sd_ih, sd, &sd_first_direct_byte);

    set_offset (key_version, key, 1);
    set_type (key_version, key, TYPE_DIRECT);

    /* correct size and st_blocks */
    *size = 0;
    *blocks = 0;

    path.path_length = ILLEGAL_PATH_ELEMENT_OFFSET;

    do {
	retval = usearch_by_position (fs, key, key_version, &path);
	if (retval == POSITION_FOUND && path.pos_in_item != 0)
	    die ("are_file_items_correct: all bytes we look for must be found at position 0");

	switch (retval) {
	case POSITION_FOUND:/**/

	    ih = PATH_PITEM_HEAD (&path);

	    if (ih_was_tail (ih)) {
	    	was_tail = 1;
	    }
	
	    set_type (key_version, key, get_type (&ih->ih_key));
	
	    if (mark_passed_items == 1) {
		mark_item_reachable (ih, PATH_PLAST_BUFFER (&path));
	    }
	
	    // does not change path
	    next_key = get_next_key_2 (&path);

	    if (next_key == 0 || not_of_one_file (key, next_key) || 
		(!is_indirect_key (next_key) && !is_direct_key(next_key))) 
	    {
		next_is_another_object = 1;
		will_convert = (is_indirect_ih (ih) && sd_size && I_UNFM_NUM (ih) > 0);
		if (will_convert) {
		    last_unfm_offset = get_offset (key) + fs->fs_blocksize * (I_UNFM_NUM (ih) - 1);
		    /* if symlink or
		       [ 1. sd_size points somewhere into last unfm block
		         2. one item of the file was direct before for 3_6 || FDB points to the tail correctly for 3_5
		         3. we can have a tail in the file of a such size ] */
		    will_convert = will_convert && (sd_size >= last_unfm_offset) && 
			(sd_size < last_unfm_offset + fs->fs_blocksize) && 
			!STORE_TAIL_IN_UNFM (sd_size, sd_size - last_unfm_offset + 1, fs->fs_blocksize);
		    
		    will_convert = will_convert && (*symlink || ((key_version == KEY_FORMAT_1) && 
			(sd_first_direct_byte == last_unfm_offset)) || ((key_version == KEY_FORMAT_2) && was_tail));
		}
	    }

	    if (should_convert) {
		*symlink = *symlink && (will_convert || is_direct_key(&ih->ih_key));
	    
		if (!(*symlink) && key_version != get_ih_key_format (ih)) {
		    if (fsck_mode(fs) == FSCK_CHECK) {
			fsck_log("are_file_items_correct: vpf-10250: block %lu, item (%d), item format (%H)"\
			    " is not equal to SD format (%d)\n",
			    get_bh(&path)->b_blocknr, PATH_LAST_POSITION(&path), ih, key_version);
			one_more_corruption (fs, fixable);
		    } else {
			fsck_log("are_file_items_correct: vpf-10280: block %lu, item (%d), item format (%H)"\
			    " is not equal to SD format (%d)  - delete\n",
			    get_bh(&path)->b_blocknr, PATH_LAST_POSITION(&path), ih, key_version);
		
			pathrelse (&path);
			delete_file_items_after_key (key, NULL);
			return 1;
		    }
		}

		if (will_convert)
		    *size = sd_size;
		else
		    *size = get_offset (&ih->ih_key) + get_bytes_number (ih, fs->fs_blocksize) - 1;
	    
		if (get_type (&ih->ih_key) == TYPE_INDIRECT) {
		    if (*symlink) /* symlinks must be calculated as dirs */
			*blocks = dir_size2st_blocks (*size);
		    else
			for (i = 0; i < I_UNFM_NUM (ih); i ++) {
			    __u32 * ind = (__u32 *)get_item(&path);

			    if (ind[i] != 0)
				*blocks += (fs->fs_blocksize >> 9);
			}
		} else if (get_type (&ih->ih_key) == TYPE_DIRECT) {
		    if (*symlink) /* symlinks must be calculated as dirs */
			*blocks = dir_size2st_blocks (*size);
		    else if (!had_direct)
			*blocks += (fs->fs_blocksize >> 9);

		    /* calculate only the first direct byte */
		    had_direct++;
		}
	    }

	    if (next_is_another_object) 
            {
		/* next item does not exists or is of another object,
                   therefore all items of file are correct */
		if (will_convert) {
		    if (fsck_mode (fs) == FSCK_CHECK) {
			/* here it can be symlink only */
			fsck_log ("are_file_items_correct: the indirect item should be converted back to direct %K\n", &ih->ih_key);
			one_more_corruption (fs, fixable);
			pathrelse (&path);
		    } else {
			__u32 * ind = (__u32 *)get_item(&path);
			fsck_log ("are_file_items_correct: convert the indirect item back to direct %K\n", &ih->ih_key);
			if (ind [I_UNFM_NUM (ih) - 1] == 0)
			    *blocks += (fs->fs_blocksize >> 9);
			sd_first_direct_byte = indirect_to_direct (&path, sd_size - last_unfm_offset + 1, *symlink);
			/* last item of the file is direct item */
			set_offset (key_version, key, sd_first_direct_byte);
			set_type (key_version, key, TYPE_DIRECT);
		    }
		} else 
		    pathrelse (&path);
		
		return 1;
	    }

	    /* next item is item of this file */
	    if ((is_indirect_ih (ih) &&
                 (get_offset (&ih->ih_key) + fs->fs_blocksize * I_UNFM_NUM (ih) != get_offset (next_key))) ||
		(is_direct_ih (ih) &&
		 (get_offset (&ih->ih_key) + get_ih_item_len (ih) != get_offset (next_key))))
	    {
		/* next item has incorrect offset (hole or overlapping) */
		pathrelse (&path);
		return 0;
	    }
	    if (do_items_have_the_same_type (ih, next_key) == 1 && are_items_in_the_same_node (&path) == 1) 
	    {
		/* two indirect items or two direct items in the same leaf. FIXME: Second will be deleted */
		pathrelse (&path);
		return 0;
	    }

	    /* items are of different types or are in different nodes */
	    if (get_offset (&ih->ih_key) + get_bytes_number (ih, fs->fs_blocksize) != get_offset (next_key))
            {
		/* indirect item free space is not set properly */
		if (!is_indirect_ih (ih) ) //|| get_ih_free_space(ih) == 0)
		    fsck_log ("are_file_items_correct: "
			      "item must be indirect and must have invalid free space (%H)", ih);
	
                if (fsck_mode (fs) != FSCK_CHECK && fsck_mode (fs) != FSCK_FIX_FIXABLE)
                {		
                    set_ih_free_space(ih, 0);
                    mark_buffer_dirty (PATH_PLAST_BUFFER (&path));
        	}
	    }

	    /* next item exists */
	    set_type_and_offset(key_version, key, get_offset (next_key), get_type(next_key));
	
	    if (comp_keys (key, next_key))
		reiserfs_panic ("are_file_items_correct: keys do not match %k and %k", key, next_key);
	    pathrelse (&path);
	    break;

	case POSITION_NOT_FOUND:
	    // we always must have next key found. Exception is first
	    // byte. It does not have to exist
	
	    if (get_offset (key) != 1)
		die ("are_file_items_correct: key not found byte can be not found only when it is first byte of file");
	    pathrelse (&path);
	    return 0;
      
	case FILE_NOT_FOUND:
	    if (get_offset (key) != 1)
		die ("are_file_items_correct: there is no items of this file, byte 0 found though");
	    pathrelse (&path);
	    return 1;

	case DIRECTORY_FOUND:
	    pathrelse (&path);
	    return -1;
	}
    } while (1);

    die ("are_file_items_correct: code can not reach here");
    return 0;
}


/* delete all items and put them back (after that file should have
   correct sequence of items.It is very similar to
   pass2.c:relocate_file () and should_relocate () */
static void rewrite_file (struct item_head * ih)
{
    struct key key;
    struct si * si;

    /* starting with the leftmost one - look for all items of file,
       store and delete and  */
    key = ih->ih_key;
    set_type_and_offset (KEY_FORMAT_1, &key, SD_OFFSET, TYPE_STAT_DATA);

    si = 0;
    delete_file_items_after_key (&key, &si);

    if (si && should_relocate (&(si->si_ih)))
	relocate_file (&(si->si_ih), 1);

    /* put all items back into tree */
    while (si) {
	insert_item_separately (&(si->si_ih), si->si_dnm_data, 1/*was in tree*/);
	si = remove_saved_item (si);
    }
}


/* file must have correct sequence of items and tail must be stored in
   unformatted pointer */
static int make_file_writeable (struct item_head * sd_ih, void * sd)
{
    struct item_head sd_ih_copy;
    struct stat_data sd_copy;
    __u64 size;
    __u32 blocks;
    int retval, symlink;
    __u16 mode;

    sd_ih_copy = *sd_ih;
    memcpy (&sd_copy, sd, get_ih_item_len (sd_ih));
    get_sd_mode (sd_ih, sd, &mode);
    symlink = S_ISLNK(mode);

    retval = are_file_items_correct (&sd_ih_copy, &sd_copy, &size, &blocks, 0/*do not mark accessed*/, &symlink);
    if (retval == 1)
	/* file looks correct */
	return 1;

    rewrite_file (sd_ih);
    /*fsck_data (fs)->rebuild.rewritten ++;*/

/*%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%*/

    size = 0;

    if (are_file_items_correct (&sd_ih_copy, &sd_copy, &size, &blocks, 0/*do not mark accessed*/, &symlink) == 0) {
	fsck_progress ("file still incorrect %K\n", &sd_ih->ih_key);
    }

/*%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%*/

    return 1;
}


/* this inserts __first__ indirect item (having k_offset == 1 and only
   one unfm pointer) into tree */
int create_first_item_of_file (struct item_head * ih, char * item, struct path * path,
				      int *pos_in_coming_item, int was_in_tree)
{
    __u32 unfm_ptr;
    struct buffer_head * unbh;
    struct item_head indih;
    int retval;
    __u32 free_sp = 0;

    mark_item_unreachable (&indih);
    copy_key (&(indih.ih_key), &(ih->ih_key));

    if (get_offset (&ih->ih_key) > fs->fs_blocksize) {
	/* insert indirect item containing 0 unfm pointer */
	unfm_ptr = 0;
	set_ih_free_space (&indih, 0);
	free_sp = 0;
	retval = 0;
    } else {
	if (is_direct_ih (ih)) {
	    /* copy direct item to new unformatted node. Save information about it */
	    //__u64 len = get_bytes_number(0, ih, item, CHECK_FREE_BYTES);
	    __u64 len = get_bytes_number (ih, fs->fs_blocksize);

	    unbh = reiserfsck_get_new_buffer (PATH_PLAST_BUFFER (path)->b_blocknr);
	    memset (unbh->b_data, 0, unbh->b_size);
	    unfm_ptr = cpu_to_le32 (unbh->b_blocknr);
/* this is for check only */
	    /*mark_block_unformatted (le32_to_cpu (unfm_ptr));*/
	    memcpy (unbh->b_data + get_offset (&ih->ih_key) - 1, item, len);

//	    save_unfm_overwriting (le32_to_cpu (unfm_ptr), ih);

	    set_ih_free_space (&indih, fs->fs_blocksize - len - (get_offset (&ih->ih_key) - 1));
	    mark_ih_was_tail (&indih);
	
	    free_sp = fs->fs_blocksize - len - (get_offset (&ih->ih_key) - 1);
	    mark_buffer_dirty (unbh);
//      mark_buffer_uptodate (unbh, 0);
	    mark_buffer_uptodate (unbh, 1);
	    brelse (unbh);

	    retval = len;
	} else {
	    /* take first unformatted pointer from an indirect item */
	    unfm_ptr = *(__u32 *)item;/*B_I_POS_UNFM_POINTER (bh, ih, 0);*/
	    if (!was_in_tree) {
		if (still_bad_unfm_ptr_2 (le32_to_cpu (unfm_ptr)))
		    die ("create_first_item_of_file: bad unfm pointer %d", le32_to_cpu (unfm_ptr));
		mark_block_used (le32_to_cpu (unfm_ptr), 0);
	    }

	    //free_sp = ih_get_free_space(0, ih, item);
/*	    free_sp = 0;//get_ih_free_space (ih);
	    set_ih_free_space (&indih, ((get_ih_item_len(ih) == UNFM_P_SIZE) ? free_sp : 0));
	    if (get_ih_item_len (ih) != UNFM_P_SIZE)
		free_sp = 0;
*/
	    retval = fs->fs_blocksize - free_sp;
	    (*pos_in_coming_item) ++;
	}
    }

    set_ih_key_format (&indih, get_ih_key_format (ih));
    //ih_version(&indih) = ih_version(ih);
    set_offset (key_format (&(ih->ih_key)), &indih.ih_key, 1);
    set_type (key_format (&(ih->ih_key)), &indih.ih_key, TYPE_INDIRECT);

    set_ih_item_len (&indih, UNFM_P_SIZE);

    reiserfsck_insert_item (path, &indih, (const char *)&unfm_ptr);

    /* update sd_first_direct_byte */
    /*
    if (get_ih_key_format (&indih) == KEY_FORMAT_1) {
	struct key sd_key;

	sd_key = indih.ih_key;
	set_type_and_offset (KEY_FORMAT_1, &sd_key, SD_OFFSET, TYPE_STAT_DATA);

	if (reiserfs_search_by_key_4 (fs, &sd_key, path) == ITEM_FOUND) {
	    struct buffer_head * bh;
	    struct stat_data_v1 * sd;
	    __u32 fdb;

	    bh = get_bh (path);
	    ih = get_ih (path);
	    if (get_ih_item_len (ih) != SD_V1_SIZE) {
		// symlink?
		if (get_ih_item_len (ih) != SD_SIZE)
		    die ("create_first_item_of_file: wrong stat data found");
	    } else {
		sd = get_item (path);
		get_sd_first_direct_byte (ih, sd, &fdb);
		if (fdb != NO_BYTES_IN_DIRECT_ITEM) {
		    fdb = NO_BYTES_IN_DIRECT_ITEM;
		    set_sd_first_direct_byte (ih, sd, &fdb);
		    mark_buffer_dirty (bh);
		}
	    }
	    pathrelse (path);
	}
    }

*/
    return retval;
}


/* path points to first part of tail. Function copies file tail into unformatted node and returns
   its block number. If we are going to overwrite direct item then keep free space (keep_free_space
   == YES). Else (we will append file) set free space to 0 */
/* we convert direct item that is on the path to indirect. we need a number of free block for
   unformatted node. reiserfs_new_blocknrs will start from block number returned by this function */
static unsigned long block_to_start (struct path * path)
{
  struct buffer_head * bh;
  struct item_head * ih;

  bh = PATH_PLAST_BUFFER (path);
  ih = PATH_PITEM_HEAD (path);
  if (get_offset(&ih->ih_key) == 1 || PATH_LAST_POSITION (path) == 0)
    return bh->b_blocknr;

  ih --;
  return (B_I_POS_UNFM_POINTER (bh, ih, I_UNFM_NUM (ih) - 1)) ?: bh->b_blocknr;
}


static void direct2indirect2 (unsigned long unfm, struct path * path, int keep_free_space)
{
    struct item_head * ih;
    struct key key;
    struct buffer_head * unbh;
    struct unfm_nodeinfo ni;
    int copied = 0;
    int file_format;

    ih = PATH_PITEM_HEAD (path);
    copy_key (&key, &(ih->ih_key));

    file_format = key_format (&key);

    if (get_offset (&key) % fs->fs_blocksize != 1) {
	/* look for first part of tail */
	pathrelse (path);
	set_offset (file_format, &key, (get_offset (&key) & ~(fs->fs_blocksize - 1)) + 1);	
	if (reiserfs_search_by_key_4 (fs, &key, path) != ITEM_FOUND)
	    die ("direct2indirect: can not find first part of tail");
    }

    unbh = reiserfsck_get_new_buffer (le32_to_cpu (unfm) ? le32_to_cpu (unfm) : block_to_start (path));
    memset (unbh->b_data, 0, unbh->b_size);

    /* delete parts of tail coping their contents to new buffer */
    do {
	__u64 len;

	ih = PATH_PITEM_HEAD (path);
	
	len = get_bytes_number(ih, fs->fs_blocksize);
	
	memcpy (unbh->b_data + copied, B_I_PITEM (PATH_PLAST_BUFFER (path), ih), len);

//	save_unfm_overwriting (unbh->b_blocknr, ih);
	copied += len;	
	set_offset (file_format, &key, get_offset (&key) +  len);

	reiserfsck_delete_item (path, 0);
	
    } while (reiserfs_search_by_key_4 (fs, &key, path) == ITEM_FOUND);

    pathrelse (path);


    /* paste or insert pointer to the unformatted node */
    set_offset (file_format, &key, get_offset (&key) - copied);
//    set_offset (ih_key_format (ih), &key, get_offset (&key) - copied);
//  key.k_offset -= copied;
    ni.unfm_nodenum = cpu_to_le32 (unbh->b_blocknr);
    ni.unfm_freespace = (keep_free_space == 1) ? cpu_to_le16 (fs->fs_blocksize - copied) : 0;


    if (usearch_by_position (fs, &key, file_format, path) == FILE_NOT_FOUND) {
	struct item_head insih;

	copy_key (&(insih.ih_key), &key);
	set_ih_key_format (&insih, file_format);
	set_type (get_ih_key_format (&insih), &insih.ih_key, TYPE_INDIRECT);
	set_ih_free_space (&insih, le16_to_cpu (ni.unfm_freespace));
//    insih.u.ih_free_space = ni.unfm_freespace;
	mark_item_unreachable (&insih);
	set_ih_item_len (&insih, UNFM_P_SIZE);
	mark_ih_was_tail (&insih);
	reiserfsck_insert_item (path, &insih, (const char *)&(ni.unfm_nodenum));
    } else {
	ih = PATH_PITEM_HEAD (path);

	if (!is_indirect_ih (ih) || get_offset (&key) != get_bytes_number (ih, fs->fs_blocksize) + get_offset (&ih->ih_key))
	    die ("direct2indirect: incorrect item found in %lu block", PATH_PLAST_BUFFER(path)->b_blocknr);

	mark_ih_was_tail (ih);
	mark_buffer_dirty (get_bh(path));
	reiserfsck_paste_into_item (path, (const char *)&ni, UNFM_P_SIZE);
    }

    mark_buffer_dirty (unbh);
    mark_buffer_uptodate (unbh, 1);
    brelse (unbh);

    /* update sd_first_direct_byte */
/*
    while (file_format == KEY_FORMAT_1) {
	struct key sd_key;

	sd_key = key;
	set_type_and_offset (KEY_FORMAT_1, &sd_key, SD_OFFSET, TYPE_STAT_DATA);

	if (reiserfs_search_by_key_4 (fs, &sd_key, path) == ITEM_FOUND) {
	    struct buffer_head * bh;
	    struct stat_data_v1 * sd;
	    __u32 fdb;
	    __u16 mode;

	    bh = get_bh (path);
	    ih = get_ih (path);
	    get_sd_mode (ih, get_item(path), &mode);
	
//	    if (get_ih_item_len (ih) != SD_V1_SIZE)
	    if (get_ih_item_len (ih) != SD_V1_SIZE) {
		if (S_ISLNK(mode)) {
		    pathrelse (path);
		    break;
		} else
		    reiserfs_panic ("direct2indirect: wrong stat data found %H", ih);
	    }

	    sd = get_item (path);
	    fdb = NO_BYTES_IN_DIRECT_ITEM;
	    set_sd_first_direct_byte (ih, sd, &fdb);
	    mark_buffer_dirty (bh);
	    pathrelse (path);
	}
	break;
    }
*/

    if (usearch_by_position (fs, &key, file_format, path) != POSITION_FOUND ||
	!is_indirect_ih (PATH_PITEM_HEAD (path)))
	die ("direct2indirect: position not found");
    return;
}




static int append_to_unformatted_node (struct item_head * comingih, struct item_head * ih, char * item,
                                        struct path * path, __u16 * free_sp, __u64 coming_len)
{
    struct buffer_head * bh, * unbh;
    __u64 end_of_data; //ih->u.ih_free_space;
    __u64 offset = get_offset (&comingih->ih_key) % fs->fs_blocksize - 1;
    int zero_number;
    __u32 unfm_ptr;
    
    /* append to free space of the last unformatted node of indirect item ih */
    if (*free_sp /*ih->u.ih_free_space*/ < coming_len)
    {

	*free_sp = get_offset (&ih->ih_key) + fs->fs_blocksize * I_UNFM_NUM (ih) - get_offset (&comingih->ih_key);
	if (*free_sp < coming_len)
	        die ("reiserfsck_append_file: there is no enough free space in unformatted node");
    }

    end_of_data = fs->fs_blocksize - *free_sp;
    zero_number = offset - end_of_data;

    bh = PATH_PLAST_BUFFER (path);
    
    unfm_ptr = le32_to_cpu (B_I_POS_UNFM_POINTER (bh, ih, I_UNFM_NUM (ih) - 1));

    /*if (unfm_ptr != 0 && unfm_ptr < SB_BLOCK_COUNT (fs))*/
    if (unfm_ptr && !not_data_block (fs, unfm_ptr))
    {
	unbh = bread (fs->fs_dev, unfm_ptr, fs->fs_blocksize);
	if (!is_block_used (unfm_ptr))
	    die ("append_to_unformatted_node:  unused block %d", unfm_ptr);
	if (unbh == 0)
	    unfm_ptr = 0;
    } else {
	/* indirect item points to block which can not be pointed or to 0, in
           any case we have to allocate new node */
	/*if (unfm_ptr == 0 || unfm_ptr >= SB_BLOCK_COUNT (fs)) {*/
	unbh = reiserfsck_get_new_buffer (bh->b_blocknr);
	memset (unbh->b_data, 0, unbh->b_size);
	B_I_POS_UNFM_POINTER (bh, ih, I_UNFM_NUM (ih) - 1) = unbh->b_blocknr;
	/*mark_block_unformatted (unbh->b_blocknr);*/
	mark_buffer_dirty (bh);
    }
    memset (unbh->b_data + end_of_data, 0, zero_number);
    memcpy (unbh->b_data + offset, item, coming_len);

//    save_unfm_overwriting (unbh->b_blocknr, comingih);

    *free_sp /*ih->u.ih_free_space*/ -= (zero_number + coming_len);
    set_ih_free_space(ih, get_ih_free_space(ih) - (zero_number + coming_len));
    memset (unbh->b_data + offset + coming_len, 0, *free_sp);
//  mark_buffer_uptodate (unbh, 0);
    mark_buffer_uptodate (unbh, 1);
    mark_buffer_dirty (unbh);
    brelse (unbh);
    pathrelse (path);
    return coming_len;
}


static void adjust_free_space (struct buffer_head * bh, struct item_head * ih, struct item_head * comingih, __u16 *free_sp)
{
  //    printf ("adjust_free_space does nothing\n");
    return;
    if (is_indirect_ih (comingih)) {
	set_ih_free_space(ih, 0);//??
	*free_sp = (__u16)0;
    } else {
	if (get_offset (&comingih->ih_key) < get_offset (&ih->ih_key) + fs->fs_blocksize * I_UNFM_NUM (ih))
	{
	    /* append to the last unformatted node */
	    set_ih_free_space (ih, fs->fs_blocksize - get_offset(&ih->ih_key) % fs->fs_blocksize + 1);//??
	    *free_sp = (__u16)fs->fs_blocksize - get_offset(&ih->ih_key) % fs->fs_blocksize + 1;
	}
	else
	{
	    set_ih_free_space(ih,0);//??
	    *free_sp =0;
	}
    }
    mark_buffer_dirty (bh);
}


/* this appends file with one unformatted node pointer (since balancing
   algorithm limitation). This pointer can be 0, or new allocated block or
   pointer from indirect item that is being inserted into tree */
int reiserfsck_append_file (struct item_head * comingih, char * item, int pos, struct path * path,
			    int was_in_tree)
{
    struct unfm_nodeinfo ni;
    struct buffer_head * unbh;
    int retval;
    struct item_head * ih = PATH_PITEM_HEAD (path);
    __u16 keep_free_space;
    __u32 bytes_number;

    if (!is_indirect_ih (ih))
	die ("reiserfsck_append_file: can not append to non-indirect item");

    //keep_free_space = ih_get_free_space(PATH_PLAST_BUFFER (path), PATH_PITEM_HEAD(path), 0);
    keep_free_space = 0;//get_ih_free_space (ih);

    if (get_offset (&ih->ih_key) + get_bytes_number (ih, fs->fs_blocksize)
	//get_bytes_number (PATH_PLAST_BUFFER (path), PATH_PITEM_HEAD(path), 0, CHECK_FREE_BYTES)
	!= get_offset (&comingih->ih_key)){
	adjust_free_space (PATH_PLAST_BUFFER (path), ih, comingih, &keep_free_space);
    }

    if (is_direct_ih (comingih)) {
	//__u64 coming_len = get_bytes_number (0,comingih, item, CHECK_FREE_BYTES);
	__u64 coming_len = get_bytes_number (comingih, fs->fs_blocksize);

	if (get_offset (&comingih->ih_key) < get_offset (&ih->ih_key) + fs->fs_blocksize * I_UNFM_NUM (ih)) {
	    /* direct item fits to free space of indirect item */
	    return append_to_unformatted_node (comingih, ih, item, path, &keep_free_space, coming_len);
	}

	unbh = reiserfsck_get_new_buffer (PATH_PLAST_BUFFER (path)->b_blocknr);
	memset (unbh->b_data, 0, unbh->b_size);
	/* this is for check only */
	/*mark_block_unformatted (unbh->b_blocknr);*/
	memcpy (unbh->b_data + get_offset (&comingih->ih_key) % unbh->b_size - 1, item, coming_len);

//	save_unfm_overwriting (unbh->b_blocknr, comingih);

	mark_buffer_dirty (unbh);
//    mark_buffer_uptodate (unbh, 0);
	mark_buffer_uptodate (unbh, 1);

	ni.unfm_nodenum = cpu_to_le32 (unbh->b_blocknr);
	ni.unfm_freespace = cpu_to_le16 (fs->fs_blocksize - coming_len - (get_offset (&comingih->ih_key) % unbh->b_size - 1));
	brelse (unbh);
	retval = coming_len;
    } else {
	/* coming item is indirect item */
	//bytes_number = get_bytes_number (PATH_PLAST_BUFFER (path), PATH_PITEM_HEAD(path), 0, CHECK_FREE_BYTES);
	bytes_number = get_bytes_number (ih, fs->fs_blocksize);
	if (get_offset (&comingih->ih_key) + pos * fs->fs_blocksize != get_offset (&ih->ih_key) + bytes_number)
	    fsck_log ("reiserfsck_append_file: can not append indirect item (%H) to the %H\n",
			   comingih, ih);

	/* take unformatted pointer from an indirect item */
	ni.unfm_nodenum = *(__u32 *)(item + pos * UNFM_P_SIZE);/*B_I_POS_UNFM_POINTER (bh, ih, pos);*/
	    
	if (!was_in_tree) {
	    if (still_bad_unfm_ptr_2 (le32_to_cpu (ni.unfm_nodenum)))
		die ("reiserfsck_append_file: bad unfm pointer %u", le32_to_cpu (ni.unfm_nodenum));
	    mark_block_used (le32_to_cpu (ni.unfm_nodenum), 0);
	}

	ni.unfm_freespace = cpu_to_le16 (((pos == (I_UNFM_NUM (comingih) - 1)) ?
			     //ih_get_free_space(0, comingih, item) /*comingih->u.ih_free_space*/ : 0);
			     get_ih_free_space (comingih) /*comingih->u.ih_free_space*/ : 0));
	ni.unfm_freespace = 0;
	retval = fs->fs_blocksize - le16_to_cpu (ni.unfm_freespace);
    }

    reiserfsck_paste_into_item (path, (const char *)&ni, UNFM_P_SIZE);
    return retval;
}


int must_there_be_a_hole (struct item_head * comingih, struct path * path)
{
    struct item_head * ih = PATH_PITEM_HEAD (path);
    int keep_free_space;

    if (is_direct_ih (ih)) {
	direct2indirect2 (0, path, keep_free_space = 1);
	ih = PATH_PITEM_HEAD (path);
    }

    path->pos_in_item = I_UNFM_NUM (ih);
    if (get_offset (&ih->ih_key) + (I_UNFM_NUM (ih) + 1) * fs->fs_blocksize <= get_offset (&comingih->ih_key))
	return 1;

    return 0;
}


int reiserfs_append_zero_unfm_ptr (struct path * path)
{
    struct unfm_nodeinfo ni;
    int keep_free_space;

    ni.unfm_nodenum = 0;
    ni.unfm_freespace = 0;

    if (is_direct_ih (PATH_PITEM_HEAD (path)))
	/* convert direct item to indirect */
	direct2indirect2 (0, path, keep_free_space = 0);
	
    reiserfsck_paste_into_item (path, (const char *)&ni, UNFM_P_SIZE);
    return 0;
}


/* write direct item to unformatted node */
/* coming item is direct */
static int overwrite_by_direct_item (struct item_head * comingih, char * item, struct path * path)
{
    __u32 unfm_ptr;
    struct buffer_head * unbh, * bh;
    struct item_head * ih;
    int offset;
    __u64 coming_len = get_bytes_number (comingih, fs->fs_blocksize);


    bh = PATH_PLAST_BUFFER (path);
    ih = PATH_PITEM_HEAD (path);

    unfm_ptr = le32_to_cpu (B_I_POS_UNFM_POINTER (bh, ih, path->pos_in_item));
    unbh = 0;

    if (unfm_ptr != 0 && unfm_ptr < get_sb_block_count (fs->fs_ondisk_sb)) {
	/**/
	unbh = bread (fs->fs_dev, unfm_ptr, bh->b_size);
	if (!is_block_used (unfm_ptr))
	    die ("overwrite_by_direct_item: unused block %d", unfm_ptr);
	if (unbh == 0)
	    unfm_ptr = 0;
    }
    if (unfm_ptr == 0 || unfm_ptr >= get_sb_block_count (fs->fs_ondisk_sb)) {
	unbh = reiserfsck_get_new_buffer (bh->b_blocknr);
	memset (unbh->b_data, 0, unbh->b_size);
	B_I_POS_UNFM_POINTER (bh, ih, path->pos_in_item) = cpu_to_le32 (unbh->b_blocknr);
	mark_buffer_dirty (bh);
    }

    if (!unbh) {
	die ("overwrite_by_direct_item: could not put direct item in");
    }
      
    offset = (get_offset (&comingih->ih_key) % bh->b_size) - 1;
    if (offset + coming_len > MAX_DIRECT_ITEM_LEN (bh->b_size))
    	die ("overwrite_by_direct_item: direct item too long (offset=%lu, length=%u)",
	         ( long unsigned ) get_offset (&comingih->ih_key), 
	     ( unsigned ) coming_len);

    memcpy (unbh->b_data + offset, item, coming_len);

//    save_unfm_overwriting (unbh->b_blocknr, comingih);

    if ((path->pos_in_item == (I_UNFM_NUM (ih) - 1)) && 
	(bh->b_size - 0/*ih_free_space (ih)*/) < (offset + coming_len)) {
	set_ih_free_space (ih, bh->b_size - (offset + coming_len)) ;
	mark_buffer_dirty (bh);
    }
    mark_buffer_dirty (unbh);
//  mark_buffer_uptodate (unbh, 0);
    mark_buffer_uptodate (unbh, 1);
    brelse (unbh);
    return coming_len;
}


#if 0

void overwrite_unfm_by_unfm (unsigned long unfm_in_tree, unsigned long coming_unfm, int bytes_in_unfm)
{
    struct overwritten_unfm_segment * unfm_os_list;/* list of overwritten segments of the unformatted node */
    struct overwritten_unfm_segment unoverwritten_segment;
    struct buffer_head * bh_in_tree, * coming_bh;


    if (!test_bit (coming_unfm % (fs->fs_blocksize * 8), SB_AP_BITMAP (fs)[coming_unfm / (fs->fs_blocksize * 8)]->b_data))
	/* block (pointed by indirect item) is free, we do not have to keep its contents */
	return;
    
    /* coming block is marked as used in disk bitmap. Put its contents to
       block in tree preserving everything, what has been overwritten there by
       direct items */
    unfm_os_list = find_overwritten_unfm (unfm_in_tree, bytes_in_unfm, &unoverwritten_segment);
    if (unfm_os_list) {
	/*    add_event (UNFM_OVERWRITING_UNFM);*/
	bh_in_tree = bread (fs->fs_dev, unfm_in_tree, fs->fs_blocksize);
	coming_bh = bread (fs->fs_dev, coming_unfm, fs->fs_blocksize);
	if (bh_in_tree == 0 || coming_bh == 0)
	    return;
	
	while (get_unoverwritten_segment (unfm_os_list, &unoverwritten_segment)) {
	    if (unoverwritten_segment.ous_begin < 0 || unoverwritten_segment.ous_end > bytes_in_unfm - 1 ||
		unoverwritten_segment.ous_begin > unoverwritten_segment.ous_end)
		die ("overwrite_unfm_by_unfm: invalid segment found (%d %d)", unoverwritten_segment.ous_begin, unoverwritten_segment.ous_end);
	    
	    memcpy (bh_in_tree->b_data + unoverwritten_segment.ous_begin, coming_bh->b_data + unoverwritten_segment.ous_begin,
		    unoverwritten_segment.ous_end - unoverwritten_segment.ous_begin + 1);
	    mark_buffer_dirty (bh_in_tree);
	}
	
	brelse (bh_in_tree);
	brelse (coming_bh);
    }
}
#endif


/* put unformatted node pointers from incoming item over the in-tree ones */
static int overwrite_by_indirect_item (struct item_head * comingih, __u32 * coming_item,
				       struct path * path, int * pos_in_coming_item)
{
    struct buffer_head * bh = PATH_PLAST_BUFFER (path);
    struct item_head * ih = PATH_PITEM_HEAD (path);
    int written;
    __u32 * item_in_tree;
    int src_unfm_ptrs, dest_unfm_ptrs, to_copy;
    int i;
    __u16 free_sp;
    int dirty = 0;

    item_in_tree = (__u32 *)B_I_PITEM (bh, ih) + path->pos_in_item;
    coming_item += *pos_in_coming_item;

    dest_unfm_ptrs = I_UNFM_NUM (ih) - path->pos_in_item;
    src_unfm_ptrs = I_UNFM_NUM (comingih) - *pos_in_coming_item;
  
    if (dest_unfm_ptrs >= src_unfm_ptrs) {
	/* whole coming item (comingih) fits into item in tree (ih) starting with path->pos_in_item */

	//free_sp = ih_get_free_space(0, comingih, (char *)coming_item);
	free_sp = 0;//ih_free_space (comingih);

	written = get_bytes_number (comingih, fs->fs_blocksize) -
	    free_sp - *pos_in_coming_item * fs->fs_blocksize;
	*pos_in_coming_item = I_UNFM_NUM (comingih);
	to_copy = src_unfm_ptrs;
	if (dest_unfm_ptrs == src_unfm_ptrs)
	    set_ih_free_space(ih, free_sp); //comingih->u.ih_free_space;
    } else {
	/* only part of coming item overlaps item in the tree */
	*pos_in_coming_item += dest_unfm_ptrs;
	written = dest_unfm_ptrs * fs->fs_blocksize;
	to_copy = dest_unfm_ptrs;
	set_ih_free_space(ih, 0);
    }
  
    for (i = 0; i < to_copy; i ++) {
	if (coming_item[i] != 0 && item_in_tree[i] == 0) {
	    /* overwrite holes only by correct a pointer in the coming item
               which must be correct */
	    item_in_tree[i] = coming_item[i];
	    mark_block_used (le32_to_cpu (coming_item[i]), 0);
	    dirty ++;
	}

#if 0

	if (!is_block_used (coming_item[i]) && !is_block_uninsertable (coming_item[i])) {
	    if (item_in_tree[i]) {
		/* do not overwrite unformatted pointer. We must save everything what is there already from
		   direct items */
		overwrite_unfm_by_unfm (item_in_tree[i], coming_item[i], fs->fs_blocksize);
	    } else {
		item_in_tree[i] = coming_item[i];
		mark_block_used (coming_item[i]);
	    }
	}
#endif
    }
    if (dirty)
	mark_buffer_dirty (bh);
    return written;
}


static int reiserfsck_overwrite_file (struct item_head * comingih, char * item,
				      struct path * path, int * pos_in_coming_item,
				      int was_in_tree)
{
    __u32 unfm_ptr;
    int written = 0;
    int keep_free_space;
    struct item_head * ih = PATH_PITEM_HEAD (path);


    if (not_of_one_file (ih, &(comingih->ih_key)))
	reiserfs_panic ("reiserfsck_overwrite_file: found [%K], new item [%K]",
	     &ih->ih_key, &comingih->ih_key);

    if (is_direct_ih (ih)) {
	unfm_ptr = 0;
	if (is_indirect_ih (comingih)) {
	    if (get_offset (&ih->ih_key) % fs->fs_blocksize != 1)
		die ("reiserfsck_overwrite_file: second part of tail can not be overwritten by indirect item");
	    /* use pointer from coming indirect item */
	    unfm_ptr = le32_to_cpu (*(__u32 *)(item + *pos_in_coming_item * UNFM_P_SIZE));
	    if (!was_in_tree) {
		if (still_bad_unfm_ptr_2 (unfm_ptr))
		    die ("reiserfsck_overwrite_file: still bad ");
	    }
	}
	/* */
	direct2indirect2 (unfm_ptr, path, keep_free_space = 1);
    }
    if (is_direct_ih (comingih))
    {
	written = overwrite_by_direct_item (comingih, item, path);
    } else {
	if (was_in_tree)
	    die ("reiserfsck_overwrite_file: item we are going to overwrite with could not be in the tree yet");
	written = overwrite_by_indirect_item (comingih, (__u32 *)item, path, pos_in_coming_item);
    }

    return written;
}


/*
 */
int reiserfsck_file_write (struct item_head * ih, char * item, int was_in_tree)
{
    struct path path;
    int count, pos_in_coming_item;
    int retval, written;
    struct key key;
    int file_format = KEY_FORMAT_UNDEFINED;
    int symlink = 0;
/*    __u64 size;
    __u32 blocks;*/

    if (!was_in_tree) {
	__u16 mode;
        
        // we already inserted all SD items. If we cannot find SD of this item - skip it
	memset (&key, 0, sizeof (key));
        copy_short_key (&key, &(ih->ih_key));
	
        if (reiserfs_search_by_key_4 (fs, &key, &path) != ITEM_FOUND) {
            fsck_log ("vpf-10260: no SD found, item skipped (%H)\n", ih);
            pathrelse (&path);
            return 0;
        }

        /*SD found*/
        file_format = get_ih_key_format (get_ih(&path));
        get_sd_mode (get_ih(&path), get_item(&path), &mode);
        symlink = S_ISLNK(mode);

        if (!symlink && file_format != get_ih_key_format (ih)) {
            fsck_log ("vpf-10270: item to be inserted is of different format then found SD,"\
            		" item skipped \n\t(%H)\n\t(%H)\n", ih, get_ih(&path));
            pathrelse (&path);
            return 0;
        }

	if (make_file_writeable (get_ih(&path), get_item (&path)) == -1) {
	    /* write was not completed. Skip that item. Maybe it should be
	       saved to lost_found */
	    fsck_progress ("reiserfsck_file_write: skip writing %H\n", ih);
	    return 0;
	}

        pathrelse (&path);
    }

    count = get_bytes_number (ih, fs->fs_blocksize);
    pos_in_coming_item = 0;

    copy_key (&key, &(ih->ih_key));

    while (count) {

	retval = usearch_by_position (fs, &key, key_format (&key), &path);
	
	if (retval == DIRECTORY_FOUND)
	    reiserfs_panic ("directory found %k", key);

	if (retval == POSITION_FOUND) {
	    written = reiserfsck_overwrite_file (ih, item, &path, &pos_in_coming_item, was_in_tree);
            count -= written;
	    set_offset (key_format (&key), &key, get_offset (&key) + written);
	}
	if (retval == FILE_NOT_FOUND) {
	    written = create_first_item_of_file (ih, item, &path, &pos_in_coming_item, was_in_tree);
	    count -= written;

	    set_offset (key_format (&key), &key, get_offset (&key) + written );
	}
	if (retval == POSITION_NOT_FOUND) {
	
	    if (is_direct_ih (ih)) {
	        mark_ih_was_tail (get_ih(&path));
	        mark_buffer_dirty (get_bh(&path));
	    }
	
	    if (must_there_be_a_hole (ih, &path) == 1)
	    {
		reiserfs_append_zero_unfm_ptr (&path);
	    }else {
		count -= reiserfsck_append_file (ih, item, pos_in_coming_item, &path, was_in_tree);
		set_offset (key_format (&key), &key, get_offset (&key) + fs->fs_blocksize);
		pos_in_coming_item ++;
	    }
	}
	if (count < 0)
	    reiserfs_panic ("reiserfsck_file_write: %K: count < 0 (%d)",
			    &key, count);
	pathrelse (&path);
    }


    /* This is a test for writing into the file. If not sure that file data are consistent after
       reiserfsck_file_write - uncomment this clause: */

/*    if (!was_in_tree && are_file_items_correct (&ih->ih_key,
    	(file_format == KEY_FORMAT_UNDEFINED) ?	get_ih_key_format (ih) : file_format,
    	&size, &blocks, 0, symlink, 0) == 0)
        reiserfs_panic ("reiserfsck_file_write: item was not inserted properly\n");*/

    return get_bytes_number (ih, fs->fs_blocksize);
}

