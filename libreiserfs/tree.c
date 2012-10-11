/*
 *  Copyright 2000-2004 by Hans Reiser, licensing governed by 
 *  reiserfsprogs/README
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "reiserfs/libreiserfs.h"
#include "misc/unaligned.h"
#include "misc/malloc.h"

static int reiserfs_tree_search (reiserfs_filsys_t * fs, 
				 const reiserfs_key_t * key,
				 reiserfs_path_t * path, 
				 misc_comp_func_t comp_func)
{
    reiserfs_bh_t * bh;   
    unsigned long block;
    reiserfs_path_element_t * curr;
    int retval;
    

    block = reiserfs_sb_get_root (fs->fs_ondisk_sb);
    if (reiserfs_fs_block(fs, block) != BT_UNKNOWN)
	return IO_ERROR;
    
    path->path_length = REISERFS_PATH_OFFILL;
    while (1) {
	curr = REISERFS_PATH_ELEM (path, ++ path->path_length);
	bh = curr->pe_buffer = reiserfs_buffer_read (fs->fs_dev, block, 
						     fs->fs_blocksize);
        if (bh == 0) {
	    path->path_length --;
	    reiserfs_tree_pathrelse (path);
	    return ITEM_NOT_FOUND;
	}
	
	retval = misc_bin_search (key, reiserfs_ih_key_at (bh, 0), 
				  reiserfs_node_items (bh),
				  reiserfs_leaf_head (bh) ? 
				  REISERFS_IH_SIZE : REISERFS_KEY_SIZE,
				  &(curr->pe_position), comp_func);
	
	if (retval == 1) {
	    /* key found, return if this is leaf level */
	    if (reiserfs_leaf_head (bh)) {
		path->pos_in_item = 0;
		return ITEM_FOUND;
	    }
	    curr->pe_position ++;
	} else {
	    /* key not found in the node */
	    if (reiserfs_leaf_head (bh))
		return ITEM_NOT_FOUND;
	}
	
	block = reiserfs_dc_get_nr (reiserfs_int_at (bh, curr->pe_position));
	
	if (reiserfs_fs_block(fs, block) != BT_UNKNOWN)
		return IO_ERROR;
    }
    
    printf ("reiserfs_tree_search: you can not get here\n");
    return ITEM_NOT_FOUND;
}

int reiserfs_tree_search_body (reiserfs_filsys_t * fs, 
			       const reiserfs_key_t * key,
			       reiserfs_path_t * path)
{
    return reiserfs_tree_search (fs, key, path, reiserfs_key_comp3);
}


int reiserfs_tree_search_item (reiserfs_filsys_t * fs, 
			       const reiserfs_key_t * key,
			       reiserfs_path_t * path)
{
    return reiserfs_tree_search (fs, key, path, reiserfs_key_comp);
}


/* key is key of byte in the regular file. This searches in tree
   through items and in the found item as well */
int reiserfs_tree_search_position (reiserfs_filsys_t * fs, 
				   const reiserfs_key_t * key,
				   reiserfs_path_t * path)
{
    const reiserfs_key_t * next_key;
    reiserfs_bh_t * bh;
    reiserfs_ih_t * ih;

    if (reiserfs_tree_search_body (fs, key, path) == ITEM_FOUND) {
    	ih = REISERFS_PATH_IH (path);

	if (!reiserfs_ih_direct (ih) && !reiserfs_ih_ext (ih))
	    return DIRECTORY_FOUND;

	path->pos_in_item = 0;
	return POSITION_FOUND;
    }

    bh = REISERFS_PATH_LEAF (path);
    ih = REISERFS_PATH_IH (path);

    if (REISERFS_PATH_LEAF_POS (path) == 0) {
	/* previous item does not exist, that means we are in leftmost leaf of
	 * the tree */
	if (!reiserfs_key_comp2 (&ih->ih_key, key)) {
	    if (!reiserfs_ih_direct (ih) && !reiserfs_ih_ext (ih))
		return DIRECTORY_FOUND;
	    return POSITION_NOT_FOUND;
	}
	return FILE_NOT_FOUND;
    }

    /* take previous item */
    REISERFS_PATH_LEAF_POS (path) --;
    ih --;

    if (reiserfs_key_comp2 (&ih->ih_key, key) || reiserfs_ih_stat(ih)) {
	/* previous item belongs to another object or is a stat data, check
	 * next item */
	REISERFS_PATH_LEAF_POS (path) ++;
	if (REISERFS_PATH_LEAF_POS (path) < reiserfs_node_items (bh))
	    /* next key is in the same node */
	    next_key = reiserfs_ih_key_at (bh, REISERFS_PATH_LEAF_POS (path));
	else
	    next_key = reiserfs_tree_rkey (path, fs);
	if (next_key == 0 || reiserfs_key_comp2 (next_key, key)) {
	    /* there is no any part of such file in the tree */
	    path->pos_in_item = 0;
	    return FILE_NOT_FOUND;
	}

	if (reiserfs_key_dir (next_key)) {
	    reiserfs_warning (stderr, "reiserfs_tree_search_position: looking "
			      "for %k found a directory with the same key\n",
			      next_key);
	    
	    return DIRECTORY_FOUND;
	}

	/* next item is the part of this file */
	path->pos_in_item = 0;
	return POSITION_NOT_FOUND;
    }

    if (reiserfs_ih_dir (ih)) {
	return DIRECTORY_FOUND;
    }

    if (reiserfs_ih_stat(ih)) {
	REISERFS_PATH_LEAF_POS (path) ++;
	return FILE_NOT_FOUND;
    }

    /* previous item is part of desired file */
    if (reiserfs_item_has_key (ih, key, bh->b_size)) {
	path->pos_in_item = reiserfs_key_get_off (key) - 
		reiserfs_key_get_off (&ih->ih_key);
	
	if (reiserfs_ih_ext (ih) )
	    path->pos_in_item /= bh->b_size;
	return POSITION_FOUND;
    }

    path->pos_in_item = reiserfs_ih_ext (ih) ? reiserfs_ext_count (ih) : 
	    reiserfs_ih_get_len (ih);
    
    return POSITION_NOT_FOUND;
}

const reiserfs_key_t * reiserfs_tree_lkey (reiserfs_path_t * path, 
					   reiserfs_filsys_t * fs)
{
    int pos, offset = path->path_length;
    reiserfs_bh_t * bh;
    reiserfs_sb_t * sb;

    sb = fs->fs_ondisk_sb;
    
    if (offset < REISERFS_PATH_OFFINIT)
	misc_die ("reiserfs_tree_lkey: illegal offset in the path (%d)", offset);

    /* While not higher in path than first element. */
    while (offset-- > REISERFS_PATH_OFFINIT) {
	if (! reiserfs_buffer_uptodate (REISERFS_PATH_BUFFER (path, offset)) )
	    misc_die ("reiserfs_tree_lkey: parent is not uptodate");
	
	/* Parent at the path is not in the tree now. */
	if (! REISERFS_NODE_INTREE (bh = REISERFS_PATH_BUFFER (path, offset)))
	    misc_die ("reiserfs_tree_lkey: buffer on the path is not in tree");

	/* Check whether position in the parent is correct. */
	if ((pos = REISERFS_PATH_POS (path, offset)) > reiserfs_node_items (bh))
	    misc_die ("reiserfs_tree_lkey: invalid position (%d) in the path", pos);

	/* Check whether parent at the path really points to the child. */
	if (reiserfs_dc_get_nr (reiserfs_int_at (bh, pos)) != 
	    REISERFS_PATH_BUFFER (path, offset + 1)->b_blocknr)
	{
	    misc_die ("reiserfs_tree_lkey: invalid block number (%d). Must be %ld",
		 reiserfs_dc_get_nr (reiserfs_int_at (bh, pos)), 
		 REISERFS_PATH_BUFFER (path, offset + 1)->b_blocknr);
	}
	
	/* Return delimiting key if position in the parent is not equal to zero. */
	if (pos) return reiserfs_int_key_at(bh, pos - 1);
    }

    /* Return MIN_KEY if we are in the root of the buffer tree. */
    if ( REISERFS_PATH_BUFFER(path, REISERFS_PATH_OFFINIT)->b_blocknr
	 != reiserfs_sb_get_root (sb) )
    {
	misc_die("reiserfs_tree_lkey: invalid root block number (%lu). Must be (%u).",
		 REISERFS_PATH_BUFFER(path, REISERFS_PATH_OFFINIT)->b_blocknr,
		 reiserfs_sb_get_root (sb));
    }
    
    /* there is no left delimiting key */
    return &MIN_KEY;
}

const reiserfs_key_t * reiserfs_tree_rkey (reiserfs_path_t * path,
					   reiserfs_filsys_t * fs)
{
    int pos, offset = path->path_length;
    reiserfs_bh_t * bh;
    reiserfs_sb_t * sb;

    sb = fs->fs_ondisk_sb;
    
    if (offset < REISERFS_PATH_OFFINIT)
	misc_die ("reiserfs_tree_rkey: illegal offset in the path (%d)", offset);

    while (offset-- > REISERFS_PATH_OFFINIT) {
	if (! reiserfs_buffer_uptodate (REISERFS_PATH_BUFFER (path, offset)))
	    misc_die ("reiserfs_tree_rkey: parent is not uptodate");

	/* Parent at the path is not in the tree now. */
	if (! REISERFS_NODE_INTREE (bh = REISERFS_PATH_BUFFER (path, offset)))
	    misc_die ("reiserfs_tree_rkey: buffer on the path is not in tree");

	/* Check whether position in the parrent is correct. */
	if ((pos = REISERFS_PATH_POS (path, offset)) > reiserfs_node_items (bh))
	    misc_die ("reiserfs_tree_rkey: invalid position (%d) in the path", pos);

	/* Check whether parent at the path really points to the child. */
	if (reiserfs_dc_get_nr (reiserfs_int_at (bh, pos)) != 
	    REISERFS_PATH_BUFFER (path, offset + 1)->b_blocknr)
	{
	    misc_die ("reiserfs_tree_rkey: invalid block number (%d). Must be %ld",
		      reiserfs_dc_get_nr (reiserfs_int_at (bh, pos)), 
		      REISERFS_PATH_BUFFER (path, offset + 1)->b_blocknr);
	}
	
	/* Return delimiting key if position in the parent is not the 
	   last one. */
	if (pos != reiserfs_node_items (bh))
	    return reiserfs_int_key_at (bh, pos);
    }
    
    /* Return MAX_KEY if we are in the root of the buffer tree. */
    if ( REISERFS_PATH_BUFFER(path, REISERFS_PATH_OFFINIT)->b_blocknr !=
	 reiserfs_sb_get_root (sb) )
    {
	misc_die ("reiserfs_tree_rkey: invalid root block number (%lu). Must be (%u).",
		  REISERFS_PATH_BUFFER(path, REISERFS_PATH_OFFINIT)->b_blocknr,
		  reiserfs_sb_get_root (sb));
    }
    
    /* there is no right delimiting key */
    return &MAX_KEY;
}

const reiserfs_key_t *reiserfs_tree_next_key(reiserfs_path_t *path, 
					     reiserfs_filsys_t *fs) 
{
    if (REISERFS_PATH_LEAF_POS (path) < 
	reiserfs_node_items (REISERFS_PATH_LEAF (path)) - 1)
    {
	return reiserfs_ih_key_at(REISERFS_PATH_LEAF (path), 
				  REISERFS_PATH_LEAF_POS (path) + 1);
    }

    return reiserfs_tree_rkey (path, fs);
}

static int comp_dir_entries (const void * p1, const void * p2) {
    __u32 off1, off2;

    off1 = d32_get((__u32 *)p1, 0);
    off2 = *(__u32 *)p2;
    
    if (off1 < off2)
	return -1;
    if (off1 > off2)
	return 1;
    return 0;
}


/* NOTE: this only should be used to look for keys who exists */
int reiserfs_tree_search_entry (reiserfs_filsys_t * fs, 
				const reiserfs_key_t * key, 
				reiserfs_path_t * path)
{
    reiserfs_bh_t * bh;
    int item_pos;
    reiserfs_ih_t * ih;
    reiserfs_key_t tmpkey;
    __u32 offset;

    if (reiserfs_tree_search_item (fs, key, path) == ITEM_FOUND) {
        path->pos_in_item = 0;
        return POSITION_FOUND;
    }

    bh = REISERFS_PATH_LEAF (path);
    item_pos = REISERFS_PATH_LEAF_POS (path);
    ih = REISERFS_PATH_IH (path);

    if (item_pos == 0) {
	/* key is less than the smallest key in the tree */
	if (reiserfs_key_comp2 (&(ih->ih_key), key))
	    /* there are no items of that directory */
	    return DIRECTORY_NOT_FOUND;

	if (!reiserfs_ih_dir (ih)) {
	    reiserfs_panic ("reiserfs_tree_search_entry: found item "
			    "is not of directory type %H", ih);
	}

	/* key we looked for should be here */
        path->pos_in_item = 0;
	return POSITION_NOT_FOUND;
    }

    /* take previous item */
    item_pos --;
    ih --;
    REISERFS_PATH_LEAF_POS (path) --;

    if (reiserfs_key_comp2 (&(ih->ih_key), key) || !reiserfs_ih_dir (ih)) {
        /* previous item belongs to another object or is stat data, check next
           item */

	item_pos ++;
	REISERFS_PATH_LEAF_POS (path) ++;

        if (item_pos < reiserfs_node_items (bh)) {
	    /* next item is in the same node */
	    ih ++;
            if (reiserfs_key_comp2 (&(ih->ih_key), key)) {
		/* there are no items of that directory */
                path->pos_in_item = 0;
                return DIRECTORY_NOT_FOUND;
            }

            if (!reiserfs_ih_dir (ih))
		reiserfs_panic ("reiserfs_tree_search_entry: %k is not a directory",
				key);
        } else {
	    /* next item is in right neighboring node */
            const reiserfs_key_t * next_key = reiserfs_tree_rkey (path, fs);

            if (next_key == 0 || reiserfs_key_comp2 (next_key, key)) {
                /* there are no items of that directory */
                path->pos_in_item = 0;
                return DIRECTORY_NOT_FOUND;
            }

            if (!reiserfs_key_dir (next_key))
		reiserfs_panic ("reiserfs_tree_search_entry: %k is not a directory",
				key);

            /* we got right delimiting key - search for it - the entry will be
	       pasted in position 0 */
            reiserfs_key_copy (&tmpkey, next_key);
            reiserfs_tree_pathrelse (path);
            if (reiserfs_tree_search_item (fs, &tmpkey, path) != ITEM_FOUND || 
		REISERFS_PATH_LEAF_POS (path) != 0)
	    {
                reiserfs_panic ("reiserfs_tree_search_entry: item "
				"corresponding to delimiting key %k "
				"not found", &tmpkey);
	    }
        }

        /* next item is the part of this directory */
        path->pos_in_item = 0;
        return POSITION_NOT_FOUND;
    }

    /* previous item is part of desired directory */
    offset = reiserfs_key_get_off1 (key);
    if (misc_bin_search (&offset, reiserfs_deh (bh, ih), 
			     reiserfs_ih_get_entries (ih),
			     REISERFS_DEH_SIZE, &(path->pos_in_item), 
			     comp_dir_entries) == 1)
    {
	return POSITION_FOUND;
    }

    return POSITION_NOT_FOUND;
}

int reiserfs_tree_delete_entry (reiserfs_filsys_t * fs, reiserfs_key_t * key)
{
    reiserfs_path_t path;
    reiserfs_tb_t tb;
    reiserfs_ih_t * ih;
    reiserfs_deh_t * deh;
    int entry_len;

    if (reiserfs_tree_search_entry (fs, key, &path) != POSITION_FOUND) {
	reiserfs_tree_pathrelse (&path);
	return 1;
    }

    ih = REISERFS_PATH_IH (&path);
    if (reiserfs_ih_get_entries (ih) == 1) {
	
	reiserfs_tb_init (&tb, fs, &path, 
			  -(REISERFS_IH_SIZE + reiserfs_ih_get_len (ih)));
	
	if (reiserfs_fix_nodes (M_DELETE, &tb, 0) != CARRY_ON) {
	    reiserfs_unfix_nodes (&tb);
	    return 1;
	}
	reiserfs_tb_balance (&tb, 0, 0, M_DELETE, 0);
	return 0;
    }

    deh = reiserfs_deh (REISERFS_PATH_LEAF (&path), ih) + path.pos_in_item;
    entry_len = reiserfs_direntry_entry_len (ih, deh, path.pos_in_item);
    reiserfs_tb_init (&tb, fs, &path, -(REISERFS_DEH_SIZE + entry_len));
    
    if (reiserfs_fix_nodes (M_CUT, &tb, 0) != CARRY_ON) {
	reiserfs_unfix_nodes (&tb);
	return 1;
    }
    reiserfs_tb_balance (&tb, 0, 0, M_CUT, 0);
    return 0;
}

static void free_unformatted_nodes (reiserfs_filsys_t * fs, 
				    reiserfs_path_t * path, 
				    int start, int end)
{
    __u32 * item = REISERFS_PATH_ITEM(path); 
    unsigned int i;

    for (i = start; i < end; i ++) {
        __u32 unfm = d32_get (item, i);
	
	if (unfm != 0)
	    reiserfs_node_forget(fs, unfm);
    }
}

void reiserfs_tree_delete (reiserfs_filsys_t * fs, 
			   reiserfs_path_t * path, 
			   int temporary) 
{
    reiserfs_tb_t tb;
    reiserfs_ih_t * ih = REISERFS_PATH_IH (path);
    
    if (reiserfs_ih_ext (ih) && !temporary)
	free_unformatted_nodes (fs, path, 0, reiserfs_ext_count (ih));

    reiserfs_tb_init (&tb, fs, path, 
		      -(REISERFS_IH_SIZE + reiserfs_ih_get_len(ih)));

    if (reiserfs_fix_nodes (M_DELETE, &tb, 0/*ih*/) != CARRY_ON)
	misc_die ("reiserfs_tree_delete: reiserfs_fix_nodes failed");
    
    reiserfs_tb_balance (&tb, 0, 0, M_DELETE, 0/*zero num*/);
}

void reiserfs_tree_delete_unit (reiserfs_filsys_t *fs, 
				reiserfs_path_t * path, 
				int cut_size)
{
    reiserfs_ih_t * ih = REISERFS_PATH_IH (path);
    reiserfs_tb_t tb;

    if (cut_size >= 0) {
	misc_die ("reiserfs_tree_delete_unit: cut size == %d", cut_size);
    }

    if (reiserfs_ih_ext (ih)) 
	free_unformatted_nodes(fs, path, (reiserfs_ih_get_len(ih) + cut_size) / 
			       REISERFS_EXT_SIZE, reiserfs_ext_count (ih));

    reiserfs_tb_init (&tb, fs, path, cut_size);

    if (reiserfs_fix_nodes (M_CUT, &tb, 0) != CARRY_ON)
	misc_die ("reiserfs_tree_delete_unit: reiserfs_fix_nodes failed");

    reiserfs_tb_balance (&tb, 0, 0, M_CUT, 0/*zero num*/);
}

void reiserfs_tree_insert_unit (reiserfs_filsys_t * fs, 
				reiserfs_path_t * path,
				const void * body, int size)
{
    reiserfs_tb_t tb;
  
    reiserfs_tb_init (&tb, fs, path, size);

    if (reiserfs_fix_nodes (M_PASTE, &tb, 0/*ih*/) != CARRY_ON)
	reiserfs_panic ("reiserfs_tree_insert_unit: "
			"reiserfs_fix_nodes failed");

    reiserfs_tb_balance (&tb, 0, body, M_PASTE, 0/*zero num*/);
}

void reiserfs_tree_insert (reiserfs_filsys_t * fs, 
			   reiserfs_path_t * path,
			   reiserfs_ih_t * ih, 
			   const void * body)
{
    reiserfs_tb_t tb;
    
    reiserfs_tb_init (&tb, fs, path, REISERFS_IH_SIZE + 
		      reiserfs_ih_get_len(ih));
    
    if (reiserfs_fix_nodes (M_INSERT, &tb, ih) != CARRY_ON)
	misc_die ("reiserfs_tree_insert: reiserfs_fix_nodes failed");

    reiserfs_tb_balance (&tb, ih, body, M_INSERT, 0/*zero num*/);
}

/* if name is found in a directory - return 1 and set path to the name,
   otherwise return 0 and reiserfs_tree_pathrelse path */
int reiserfs_tree_scan_name (reiserfs_filsys_t * fs, 
			     reiserfs_key_t * dir, 
			     char * name,
			     reiserfs_path_t * path)
{
    const reiserfs_key_t * rdkey;
    reiserfs_key_t entry_key;
    reiserfs_deh_t * deh;
    reiserfs_ih_t * ih;
    int i, retval;
    
    reiserfs_key_set_did (&entry_key, reiserfs_key_get_did (dir));
    reiserfs_key_set_oid (&entry_key, reiserfs_key_get_oid (dir));
    reiserfs_key_set_off1 (&entry_key, 0);
    reiserfs_key_set_uni (&entry_key, UNI_DE);

 
    if (reiserfs_tree_search_entry (fs, &entry_key, path) == 
	DIRECTORY_NOT_FOUND) 
    {
	reiserfs_tree_pathrelse (path);
	return 0;
    }

    do {
	ih = REISERFS_PATH_IH (path);
	deh = reiserfs_deh (REISERFS_PATH_LEAF (path), ih) + path->pos_in_item;
	for (i = path->pos_in_item; i < reiserfs_ih_get_entries (ih); i ++, deh ++)
	{
	    /* the name in directory has the same hash as the given name */
	    if ((reiserfs_direntry_name_len (ih, deh, i) == 
		 (int)strlen (name)) &&
		!memcmp (reiserfs_deh_name (deh, i), name, strlen (name))) 
	    {
		path->pos_in_item = i;
		return 1;
	    }
	}

	rdkey = reiserfs_tree_rkey (path, fs);
	if (!rdkey || reiserfs_key_comp2 (rdkey, dir)) {
	    reiserfs_tree_pathrelse (path);
	    return 0;
	}
	
	if (!reiserfs_key_dir (rdkey))
	    reiserfs_panic ("reiserfs_tree_scan_name: can not find name in "
			    "broken directory yet");

	/* first name of that item may be a name we are looking for */
	entry_key = *rdkey;
	reiserfs_tree_pathrelse (path);
	retval = reiserfs_tree_search_entry (fs, &entry_key, path);
	
	if (retval != POSITION_FOUND)
	    reiserfs_panic ("reiserfs_tree_scan_name: wrong delimiting "
			    "key in the tree");
    } while (1);

    return 0;
}


/* returns 0 if name is not found in a directory and 1 if name is
   found. Stores key found in the entry in 'key'. Returns minimal not used
   generation counter in 'min_gen_counter'. dies if found object is not a
   directory. */
int reiserfs_tree_search_name (reiserfs_filsys_t * fs, 
			       const reiserfs_key_t * dir, 
			       char * name, 
			       unsigned int * min_gen_counter, 
			       reiserfs_key_t * key)
{
    const reiserfs_key_t * rdkey;
    reiserfs_key_t entry_key;
    REISERFS_PATH_INIT (path);
    reiserfs_deh_t * deh;
    reiserfs_ih_t * ih;
    __u32 hash;
    int retval;
    int i;


    reiserfs_key_set_did (&entry_key, reiserfs_key_get_did (dir));
    reiserfs_key_set_oid (&entry_key, reiserfs_key_get_oid (dir));
    if (!strcmp (name, "."))
	hash = OFFSET_DOT;
    else if (!strcmp (name, ".."))
	hash = OFFSET_DOT_DOT;
    else
	hash = reiserfs_hash_value (fs->hash, name, strlen (name));
    reiserfs_key_set_off1 (&entry_key, hash);
    reiserfs_key_set_uni (&entry_key, UNI_DE);

    *min_gen_counter = 0;

    if (reiserfs_tree_search_entry (fs, &entry_key, &path) == 
	DIRECTORY_NOT_FOUND) 
    {
	reiserfs_tree_pathrelse (&path);
	return 0;
    }
	
    do {
	ih = REISERFS_PATH_IH (&path);
	deh = reiserfs_deh (REISERFS_PATH_LEAF (&path), ih) + path.pos_in_item;
	for (i = path.pos_in_item; i < reiserfs_ih_get_entries (ih); i ++, deh ++) {
	    if (OFFSET_HASH (reiserfs_deh_get_off (deh)) != OFFSET_HASH (hash))
	    {
		/* all entries having the same hash were scanned */
		reiserfs_tree_pathrelse (&path);
		return 0;
	    }
			
	    if (OFFSET_GEN (reiserfs_deh_get_off (deh)) == 
		*min_gen_counter)
	    {
		(*min_gen_counter) ++;
	    }
			
	    if ((reiserfs_direntry_name_len (ih, deh, i) == 
		 (int)strlen (name)) &&
	        (!memcmp (reiserfs_deh_name (deh, i), name, strlen (name)))) 
	    {
		/* entry found in the directory */
		if (key) {
		    memset (key, 0, sizeof (reiserfs_key_t));
		    reiserfs_key_set_did (key, reiserfs_deh_get_did (deh));
		    reiserfs_key_set_oid (key, reiserfs_deh_get_obid (deh));
		}
		reiserfs_tree_pathrelse (&path);
		return 1;//reiserfs_deh_get_obid (deh) ? reiserfs_deh_get_obid (deh) : 1;
	    }
	}
		
	rdkey = reiserfs_tree_rkey (&path, fs);
	if (!rdkey || reiserfs_key_comp2 (rdkey, dir)) {
	    reiserfs_tree_pathrelse (&path);
	    return 0;
	}
		
	if (!reiserfs_key_dir (rdkey))
	    reiserfs_panic ("reiserfs_tree_search_name: can not find name "
			    "in broken directory yet");
		
	/* next item is the item of the directory we are looking name in */
	if (OFFSET_HASH (reiserfs_key_get_off (rdkey)) != hash) {
	    /* but there is no names with given hash */
	    reiserfs_tree_pathrelse (&path);
	    return 0;
	}
		
	/* first name of that item may be a name we are looking for */
	entry_key = *rdkey;
	reiserfs_tree_pathrelse (&path);
	retval = reiserfs_tree_search_entry (fs, &entry_key, &path);
	if (retval != POSITION_FOUND)
	    reiserfs_panic ("reiserfs_tree_search_name: wrong delimiting "
			    "key in the tree");
		
    } while (1);
    
    return 0;
}


/* compose directory entry: dir entry head and name itself */
static char * make_entry (char * entry, char * name, 
			  const reiserfs_key_t * key, 
			  __u32 offset)
{
    reiserfs_deh_t * deh;
    __u16 state;
	
    if (!entry)
	entry = misc_getmem (REISERFS_DEH_SIZE + MISC_ROUND_UP (strlen (name)));

    memset (entry, 0, REISERFS_DEH_SIZE + MISC_ROUND_UP (strlen (name)));
    deh = (reiserfs_deh_t *)entry;
	
    reiserfs_deh_set_loc (deh, 0);
    reiserfs_deh_set_off (deh, offset);
    state = (1 << DEH_Visible2);
    reiserfs_deh_set_state (deh, state);
	
    /* key of object entry will point to */
    reiserfs_deh_set_did (deh, reiserfs_key_get_did (key));
    reiserfs_deh_set_obid (deh, reiserfs_key_get_oid (key));
	
    memcpy ((char *)(deh + 1), name, strlen (name));
    return entry;
}

/* add new name into a directory. If it exists in a directory - do
   nothing */
int reiserfs_tree_insert_entry (reiserfs_filsys_t * fs, 
				const reiserfs_key_t * dir, 
				char * name, int name_len,
				const reiserfs_key_t * key, 
				__u16 fsck_need)
{
    reiserfs_ih_t entry_ih = {{0,}, };
    char * entry;
    int retval;
    REISERFS_PATH_INIT(path);
    unsigned int gen_counter;
    int item_len;
    __u32 hash;

    if (reiserfs_tree_search_name (fs, dir, name, &gen_counter, 0))
	/* entry is in the directory already or directory was not found */
	return 0;

    /* compose entry key to look for its place in the tree */
    reiserfs_key_set_did (&(entry_ih.ih_key), reiserfs_key_get_did (dir));
    reiserfs_key_set_oid (&(entry_ih.ih_key), reiserfs_key_get_oid (dir));
    if (!strcmp (name, "."))
	hash = OFFSET_DOT;
    else if (!strcmp (name, ".."))
	hash = OFFSET_DOT_DOT;
    else
	hash = reiserfs_hash_value (fs->hash, name, strlen (name)) + gen_counter;
    reiserfs_key_set_off1 (&(entry_ih.ih_key), hash);
    reiserfs_key_set_uni (&(entry_ih.ih_key), UNI_DE);

    reiserfs_ih_set_format (&entry_ih, KEY_FORMAT_1);
    reiserfs_ih_set_entries (&entry_ih, 1);

    item_len = REISERFS_DEH_SIZE + name_len;
/*
    if (reiserfs_super_format (fs->fs_ondisk_sb) == REISERFS_FORMAT_3_5)
	item_len = REISERFS_DEH_SIZE + strlen (name);
    else if (reiserfs_super_format (fs->fs_ondisk_sb) == REISERFS_FORMAT_3_6)
	item_len = REISERFS_DEH_SIZE + MISC_ROUND_UP (strlen (name));
    else
	reiserfs_panic ("unknown fs format");
*/

    reiserfs_ih_set_len (&entry_ih, item_len);

    /* fsck may need to insert item which was not reached yet */
    reiserfs_ih_set_flags (&entry_ih, fsck_need);

    entry = make_entry (0, name, key, 
			reiserfs_key_get_off (&(entry_ih.ih_key)));

    retval = reiserfs_tree_search_entry (fs, &(entry_ih.ih_key), &path);
    switch (retval) {
    case POSITION_NOT_FOUND:
	reiserfs_tree_insert_unit (fs, &path, entry, item_len);
	break;

    case DIRECTORY_NOT_FOUND:
	reiserfs_deh_set_loc ((reiserfs_deh_t *)entry, REISERFS_DEH_SIZE);
	reiserfs_tree_insert (fs, &path, &entry_ih, entry);
	break;

    default:
	reiserfs_panic ("reiserfs_tree_insert_entry: looking for %k (inserting "
			"name \"%s\") reiserfs_tree_search_entry returned %d",
			&(entry_ih.ih_key), name, retval);
    }

    misc_freemem (entry);
    return item_len;
}


/* inserts new or old stat data of a __DIRECTORY__ (unreachable, nlinks == 0) */
int reiserfs_tree_create_stat (reiserfs_filsys_t * fs,
			       reiserfs_path_t * path, 
			       const reiserfs_key_t * key,
			       item_modify_t modify)
{
    reiserfs_ih_t ih;
    reiserfs_sd_t sd;
    int key_format;

    if (fs->fs_format == REISERFS_FORMAT_3_5)
	key_format = KEY_FORMAT_1;
    else
	key_format = KEY_FORMAT_2;

    memset(&sd, 0, sizeof(sd));
    reiserfs_stat_init (fs->fs_blocksize, key_format, reiserfs_key_get_did (key),
			reiserfs_key_get_oid (key), &ih, &sd);

    if (modify)
        modify (&ih, &sd);
    
    reiserfs_tree_insert (fs, path, &ih, &sd);
    return key_format;
}

__u16 reiserfs_tree_root (reiserfs_filsys_t * fs,
			  item_modify_t modify,
			  __u16 ih_flags)
{
    __u16 format;
    REISERFS_PATH_INIT (path);


    /* is there root's stat data */
    if (reiserfs_tree_search_item (fs, &root_dir_key, &path) == ITEM_NOT_FOUND) {	
	format = reiserfs_tree_create_stat (fs, &path, &root_dir_key, modify);
    } else {
    	reiserfs_ih_t * ih = REISERFS_PATH_IH (&path);
    	
    	if (!reiserfs_ih_stat (ih))
	    reiserfs_panic ("It must be root's stat data %k\n", &ih->ih_key);
	
        format = (reiserfs_ih_get_len (REISERFS_PATH_IH (&path)) == REISERFS_SD_SIZE) ? 
		KEY_FORMAT_2 : KEY_FORMAT_1;
	
	reiserfs_tree_pathrelse (&path);
    }

    /* add "." and ".." if any of them do not exist. Last two
       parameters say: 0 - entry is not added on lost_found pass and 1
       - mark item unreachable */

    reiserfs_tree_insert_entry (fs, &root_dir_key, ".", 
				reiserfs_direntry_entry_estimate (".", format),
				&root_dir_key, ih_flags);
    reiserfs_tree_insert_entry (fs, &root_dir_key, "..", 
				reiserfs_direntry_entry_estimate ("..", format),
				&parent_root_dir_key, ih_flags);

    return format;
}

/* Release all buffers in the path. */
void  reiserfs_tree_pathrelse (reiserfs_path_t * p_s_search_path) 
{
    int n_path_offset = p_s_search_path->path_length;
    
    while ( n_path_offset > REISERFS_PATH_OFFILL )
	reiserfs_buffer_close(REISERFS_PATH_BUFFER(p_s_search_path, n_path_offset--));

    p_s_search_path->path_length = REISERFS_PATH_OFFILL;
}

int reiserfs_tree_left_mergeable (reiserfs_filsys_t * s, reiserfs_path_t * path)
{
    reiserfs_ih_t * right;
    reiserfs_bh_t * bh;
    int retval;
    
    right = reiserfs_ih_at (REISERFS_PATH_LEAF (path), 0);
    
    bh = reiserfs_tree_left_neighbor (s, path);
    if (bh == 0) {
	return 0;
    }
    retval = reiserfs_leaf_mergeable (reiserfs_ih_at (bh, reiserfs_node_items (bh) - 1), 
				      right, bh->b_size);
    reiserfs_buffer_close (bh);
    return retval;
}

int reiserfs_tree_right_mergeable (reiserfs_filsys_t * s, reiserfs_path_t * path)
{
    reiserfs_ih_t * left;
    reiserfs_bh_t * bh;
    int retval;
    
    left = reiserfs_ih_at (REISERFS_PATH_LEAF (path), 
			   reiserfs_node_items (REISERFS_PATH_LEAF (path)) - 1);
    
    bh = reiserfs_tree_right_neighbor (s, path);
    if (bh == 0) {
	return 0;
    }
    retval = reiserfs_leaf_mergeable (left, reiserfs_ih_at (bh, 0), bh->b_size);
    reiserfs_buffer_close (bh);
    return retval;
}

/* return 1 if left and right nodes can be packed into 1 node. 0 otherwise */
int reiserfs_tree_node_mergeable(reiserfs_bh_t * left, 
				 reiserfs_bh_t * right)
{
    reiserfs_ih_t *ih = reiserfs_ih_at (left, reiserfs_node_items (left) - 1);
    
    if (reiserfs_node_free (left) >= reiserfs_node_used (right) -
	(reiserfs_leaf_mergeable (ih, reiserfs_ih_at(right, 0), left->b_size)
	 ? REISERFS_IH_SIZE : 0))
    {
	return 1;
    }

    return 0;
}

/* A simple 2 nodes merge operation. */
int reiserfs_tree_merge(reiserfs_filsys_t *fs, 
			reiserfs_path_t *dst_path, 
			reiserfs_path_t *src_path)
{
	reiserfs_bufinfo_t dest_bi;
	reiserfs_bufinfo_t src_bi;
	int num, res;
	
	src_bi.bi_bh = REISERFS_PATH_LEAF (src_path);
	src_bi.bi_parent = REISERFS_PATH_UPPARENT (src_path, 0);
	src_bi.bi_position = REISERFS_PATH_UPPARENT_POS (src_path, 0);	
	
	dest_bi.bi_bh = REISERFS_PATH_LEAF(dst_path);
	dest_bi.bi_parent = REISERFS_PATH_UPPARENT (dst_path, 0);
	dest_bi.bi_position = REISERFS_PATH_UPPARENT_POS (dst_path, 0);
	
	num = reiserfs_node_items(src_bi.bi_bh);
	res = reiserfs_lb_copy (fs, &dest_bi, src_bi.bi_bh, 
				FIRST_TO_LAST, num, -1);
	
	/* Delete all but the very first one. */
	reiserfs_lb_delete (fs, &src_bi, FIRST_TO_LAST, 1, num - 1, -1);
	
	/* Delete the very first one and rebalance the tree. */
	REISERFS_PATH_LEAF_POS (src_path) = 0;
	reiserfs_tree_delete (fs, src_path, 0);
	return res;
}

