/*
 * Copyright 1996-2004 by Hans Reiser, licensing governed by 
 * reiserfsprogs/README
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "fsck.h"
#include "misc/malloc.h"

/* relocation rules: we have an item (it is taken from "non-insertable"
   leaf). It has original key yet. We check to see if object with this
   key is remapped. Object can be only remapped if it is not a piece
   of directory */

/* in list of this structures we store what has been relocated. */
struct relocated {
    unsigned long old_dir_id;
    unsigned long old_objectid;
    
    unsigned long new_objectid;
    
    struct relocated * next;
};

/* all relocated files will be linked into lost+found directory at the
   beginning of semantic pass */
static struct relocated * relocated_list = NULL;

__u32 fsck_relocate_get_oid (reiserfs_key_t * key) {
    struct relocated *cur = relocated_list;

    while (cur) {
	if (cur->old_dir_id == reiserfs_key_get_did (key) &&
	    cur->old_objectid == reiserfs_key_get_oid (key))
	    /* object is relocated already */
	    return cur->new_objectid;
	cur = cur->next;
    }
    return 0;
}

/* return objectid the object has to be remapped with */
__u32 fsck_relocate_oid(reiserfs_key_t * key) {
    struct relocated * cur;
    __u32 cur_id;

    if ((cur_id = fsck_relocate_get_oid (key)) != 0)
    	return cur_id;

    cur = misc_getmem (sizeof (struct relocated));
    cur->old_dir_id = reiserfs_key_get_did (key);
    cur->old_objectid = reiserfs_key_get_oid (key);
    cur->new_objectid = id_map_alloc(proper_id_map(fs));
    cur->next = relocated_list;
    relocated_list = cur;
    pass_2_stat (fs)->relocated ++;
    /*
    fsck_log ("relocation: %K is relocated to [%lu, %lu]\n",
	      key, reiserfs_key_get_did (key), cur->new_objectid);
    */
    return cur->new_objectid;
}

/* relocated files get added into lost+found with slightly different names */
static __u64 link_one (struct relocated * file) {
    char name[REISERFS_NAME_MAX];
    reiserfs_key_t obj_key;
    __u64 len = 0;
    int entry_len;

    name[0] = '\0';
    sprintf(name, "%lu,%lu", file->old_dir_id, file->new_objectid);
    reiserfs_key_set_did (&obj_key, file->old_dir_id);
    reiserfs_key_set_oid (&obj_key, file->new_objectid);

    /* 0 for fsck_need does not mean too much - it would make effect if there 
     * were no this directory yet. But /lost_found is there already */
    entry_len = reiserfs_direntry_entry_estimate(name, fs->lost_format);
    len = reiserfs_tree_insert_entry (fs, &lost_found_dir_key, name, entry_len,
				      &obj_key, 0/*fsck_need*/);
    return len;
}

void fsck_relocate_mklinked(reiserfs_key_t *new_key) {
    struct relocated *cur = relocated_list;
    struct relocated *prev = NULL;

    while (cur) {
	if (cur->old_dir_id == reiserfs_key_get_did(new_key) && 
	    cur->new_objectid == reiserfs_key_get_oid(new_key))
	    break;

	prev = cur;
	cur = cur->next;
    }

    if (cur) {	
	/* len = link_func(cur); */

	if (prev) 
	    prev->next = cur->next;
	else
	    relocated_list = cur->next;

	misc_freemem (cur);
    }
}

void fsck_relocate_link_all (void) {
    struct relocated * tmp;
    
    while (relocated_list) {
	link_one (relocated_list);
	tmp = relocated_list;
	relocated_list = relocated_list->next;
	misc_freemem (tmp);
    }
}

/* check whether there are any directory items with this key 
   Returns:
   0 is relocation is not needed;
   1 is the whole file relocation is needed;
   2 if a new item only relocation is needed. 
 */
__u32 fsck_relocate_check (reiserfs_ih_t * ih, int isdir) {
    const reiserfs_key_t * rkey;
    reiserfs_ih_t * path_ih;
    reiserfs_path_t path;
    reiserfs_key_t key;
    int found_dir;

    /* starting with the leftmost item with this key */
    key = ih->ih_key;
    reiserfs_key_set_sec (KEY_FORMAT_1, &key, OFFSET_SD, TYPE_STAT_DATA);

    while (1) {
	reiserfs_tree_search_item (fs, &key, &path);
	
	if (REISERFS_PATH_LEAF_POS (&path) == 
	    reiserfs_node_items (REISERFS_PATH_LEAF (&path))) 
	{
	    rkey = reiserfs_tree_rkey (&path, fs);
	    if (rkey && !reiserfs_key_comp2 (&key, rkey)) {
		/* file continues in the right neighbor */
		key = *rkey;
		reiserfs_tree_pathrelse (&path);
		continue;
	    }
	    
	    /* there is no more items with this key */
	    reiserfs_tree_pathrelse (&path);
	    break;
	}

	path_ih = REISERFS_PATH_IH (&path);
	if (reiserfs_key_comp2 (&key, &(path_ih->ih_key))) {
	    /* there are no more item with this key */
	    reiserfs_tree_pathrelse (&path);
	    break;
	}

	found_dir = reiserfs_dir(path_ih, REISERFS_PATH_ITEM (&path));
	
	if (reiserfs_ih_stat (path_ih)) {
	    if (reiserfs_ih_ischeck (path_ih)) {
		/* we have checked it already */
		reiserfs_tree_pathrelse (&path);

		if ((found_dir && isdir) || (!found_dir && !isdir)) {
		    /* If the item being inserted and the found one are 
		       both either directory ones of not directory ones, 
		       no relocation is needed. */
		    return 0;
		}
		
		/* If new one is a dirrectory one, relocate file in the tree. */
		if (isdir) return 1;
		
		/* If new one is file and there is a directory in the tree, return
		   id for the relocation, allocate it if needed. */
		return fsck_relocate_oid (&path_ih->ih_key);
	    } else {
		reiserfs_ih_mkcheck (path_ih);
		reiserfs_buffer_mkdirty (REISERFS_PATH_LEAF(&path));
	    }
	}
	
	reiserfs_tree_pathrelse (&path);
	
	if (isdir) {
	    /* If directory, force relocate as it costs nothing for 
	       the directory, and do not spend time on this check. */
	    return 1;
	}
	
	/* ok, item found, but make sure that it is not a directory one */
	if (found_dir) {
	    /* item of directory found. so, we have to relocate the file */
	    return 1;
	}
	
	key = path_ih->ih_key;
	reiserfs_key_set_off (KEY_FORMAT_1, &key, 
			      reiserfs_key_get_off (&key) + 1);
    }

    return 0;
}
