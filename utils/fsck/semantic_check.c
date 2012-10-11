/*
 * Copyright 1996-2004 by Hans Reiser, licensing governed by reiserfsprogs/README
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "fsck.h"
#include "misc/malloc.h"
#include "misc/unaligned.h"
#include "misc/misc.h"
#include "util/misc.h"

#include <sys/stat.h>

static reiserfs_key_t *trunc_links = NULL;
static __u32 links_num = 0;

int wrong_mode (reiserfs_key_t * key, __u16 * mode, __u64 real_size, int symlink);
int wrong_fdb (reiserfs_key_t * key, int blocksize, __u32 * first_direct_byte,
			     __u32 sd_fdb, __u32 size);
void get_object_key (reiserfs_deh_t * deh, reiserfs_key_t * key, 
		     reiserfs_key_t * entry_key, reiserfs_ih_t * ih);

struct path_key {
    struct short_key {
        __u32 k_dir_id;
        __u32 k_objectid;
    } key;
    struct path_key * next, * prev;
};

struct path_key * head_key = NULL;
struct path_key * tail_key = NULL;

static int check_path_key(reiserfs_key_t * key) {
    struct path_key * cur = head_key;

    while(cur != NULL) {
        if (!reiserfs_key_comp2(&cur->key, key)) {
            fsck_log("\nsemantic check: The directory %k has 2 names.", key);
            return LOOP_FOUND;
        }
	
        cur = cur->next;
    }
    
    return 0;
}

static int add_path_key(reiserfs_key_t * key) {
    if (check_path_key(key))
    	return LOOP_FOUND;

    if (tail_key == NULL) {
        tail_key = misc_getmem(sizeof(struct path_key));
        head_key = tail_key;
        tail_key->prev = NULL;
    } else {
        tail_key->next = misc_getmem(sizeof(struct path_key));
        tail_key->next->prev = tail_key;
        tail_key = tail_key->next;
    }
    
    reiserfs_key_copy2 ((reiserfs_key_t *)&tail_key->key, key);
    tail_key->next = NULL;

    return 0;
}

static void del_path_key() {
    if (tail_key == NULL)
        misc_die("Wrong path_key structure");

    if (tail_key->prev == NULL) {
        misc_freemem(tail_key);
        tail_key = head_key = NULL;
    } else {
        tail_key = tail_key->prev;
        misc_freemem(tail_key->next);
        tail_key->next = NULL;
    }
}

/* path is path to stat data. If file will be relocated - new_ih will contain
   a key file was relocated with */
static int check_check_regular_file (reiserfs_path_t * path, void * sd,
                                     reiserfs_ih_t * new_ih)
{
    int is_new_file;
//    reiserfs_key_t key, sd_key;
    __u16 mode;
    __u32 nlink;
    __u64 real_size, sd_size;
    __u32 blocks, sd_blocks;	/* proper values and value in stat data */
    __u32 first_direct_byte, sd_fdb;

    reiserfs_ih_t * ih, sd_ih;
    int fix_sd;
    int symlnk = 0;
    int retval = OK;
    int tmp_position;


    ih = REISERFS_PATH_IH (path);

    if (new_ih) {
	/* this objectid is used already */
	*new_ih = *ih;
	reiserfs_tree_pathrelse (path);
	fsck_file_relocate (&new_ih->ih_key, 1);
	fsck_relocate_mklinked(&new_ih->ih_key);
	one_less_corruption (fs, FIXABLE);
	sem_pass_stat (fs)->oid_sharing_files_relocated ++;
	retval = RELOCATED;
	
	if (reiserfs_tree_search_item (fs, &(new_ih->ih_key), path) == 
	    ITEM_NOT_FOUND)
	{
	    reiserfs_panic ("%s: Could not find a StatData of the relocated "
			    "file %K", __FUNCTION__, &new_ih->ih_key);
	}
	
	/* stat data is marked unreachable again due to relocation, fix that */
	ih = REISERFS_PATH_IH (path);
	sd = REISERFS_PATH_ITEM (path);
    }
    

    if (reiserfs_ih_get_len (ih) == REISERFS_SD_SIZE)
	is_new_file = 1;
    else
	is_new_file = 0;


    reiserfs_stat_get_nlink (ih, sd, &nlink);
    reiserfs_stat_get_mode (ih, sd, &mode);
    reiserfs_stat_get_size (ih, sd, &sd_size);
    reiserfs_stat_get_blocks (ih, sd, &sd_blocks);

/*	
    if (fsck_mode (fs) == FSCK_FIX_FIXABLE) {
    	// check and set nlink first 
    	nlink ++;
    	reiserfs_stat_set_nlink (ih, sd, &nlink);
    	reiserfs_buffer_mkdirty (bh);

    	if (nlink > 1)
	    return OK;
    }
*/

    if (!is_new_file)
	reiserfs_stat_get_fdb (ih, sd, &sd_fdb);

    if (S_ISLNK (mode)) 	
	symlnk = 1;
    
    sd_ih = *ih;
//    sd_key = sd_ih.ih_key;
    reiserfs_tree_pathrelse (path);

    if (are_file_items_correct (&sd_ih, sd, &real_size, &blocks, 
				0/* do not mark reachable */, &symlnk) != 1)
    {
	one_more_corruption (fs, FATAL);
	fsck_log ("check_regular_file: The file %K with the corrupted "
		  "structure found\n", &sd_ih.ih_key);
    } else {
	fix_sd = 0;
    
	fix_sd += wrong_mode (&sd_ih.ih_key, &mode, real_size, symlnk);
	if (!is_new_file)
	    fix_sd += wrong_fdb (&sd_ih.ih_key, fs->fs_blocksize,
		&first_direct_byte, sd_fdb, real_size);
	
	if (misc_bin_search(&sd_ih.ih_key, trunc_links, links_num, 
				sizeof(sd_ih.ih_key), &tmp_position, 
				reiserfs_key_comp2) != 1) 
	{
	    fix_sd += wrong_st_size (&sd_ih.ih_key, is_new_file ? 
				     REISERFS_SD_SIZE_MAX_V2 : 
				     REISERFS_SD_SIZE_MAX_V1,
				     fs->fs_blocksize, &real_size, 
				     sd_size, symlnk ? TYPE_SYMLINK : 0);
	} else {
	    real_size = sd_size;
	}
	
	fix_sd += wrong_st_blocks (&sd_ih.ih_key, &blocks, 
				   sd_blocks, mode, is_new_file);

	if (fix_sd) {
	    if (fsck_mode (fs) == FSCK_FIX_FIXABLE) {
    		reiserfs_bh_t * bh;
  	        /* find stat data and correct it */
  	        reiserfs_key_set_sec (KEY_FORMAT_1, &sd_ih.ih_key, 
				      OFFSET_SD, TYPE_STAT_DATA);
		
	        if (reiserfs_tree_search_item (fs, &sd_ih.ih_key, path) != 
		    ITEM_FOUND) 
		{
		    fsck_log ("check_regular_file: A StatData of the file %K "
			      "cannot be found\n", &sd_ih.ih_key);
		    
                    one_more_corruption (fs, FATAL);
                    return STAT_DATA_NOT_FOUND;
	        }
	    
	        bh = REISERFS_PATH_LEAF (path);
	        ih = REISERFS_PATH_IH (path);
	        sd = REISERFS_PATH_ITEM (path);
	        reiserfs_stat_set_size (ih, sd, &real_size);
	        reiserfs_stat_set_blocks (ih, sd, &blocks);
	        reiserfs_stat_set_mode (ih, sd, &mode);
	        if (!is_new_file)
		    reiserfs_stat_set_fdb (ih, sd, &first_direct_byte);
		reiserfs_buffer_mkdirty (bh);
	    } else {
		fsck_check_stat (fs)->fixable_corruptions += fix_sd;
	    }
	}
    }
    
    return retval;
}

/* returns buffer, containing found directory item.*/
static char * get_next_directory_item (
	/* on return this will contain key of next item in the tree */
	reiserfs_key_t * key, 
	const reiserfs_key_t * parent, 
	reiserfs_ih_t * ih, 
	__u32 * pos_in_item, 
	int dir_format)
{
    const reiserfs_key_t * rdkey;
    REISERFS_PATH_INIT (path);
    reiserfs_deh_t * deh;
    reiserfs_bh_t * bh;
    char * dir_item;
    int entry_len;
    int retval;
    int i;


start_again:

    retval = reiserfs_tree_search_entry (fs, key, &path);

    if (retval != POSITION_FOUND && 
	reiserfs_key_get_off (key) != OFFSET_DOT)
    {
	reiserfs_panic ("get_next_directory_item: The current "
			"directory %k cannot be found", key);
    }

    /* leaf containing directory item */
    bh = REISERFS_PATH_LEAF (&path);
    *pos_in_item = path.pos_in_item;
    *ih = *REISERFS_PATH_IH (&path);
    deh = reiserfs_deh (bh, ih);

    /* position was not found for '.' or there is no '..' */
    if (retval != POSITION_FOUND || 
	((reiserfs_key_get_off (key) == OFFSET_DOT) &&
	 (reiserfs_ih_get_entries (ih) < 2 || 
	  reiserfs_direntry_name_len (ih, deh + 1, 1) != 2 ||
	  strncmp (reiserfs_deh_name (deh + 1, 1), "..", 2)))) 
    {
	fsck_log ("get_next_directory_item: The %s %k cannot be found in %k",
		  (retval == POSITION_NOT_FOUND) ? "entry" : "directory", 
		  key, &ih->ih_key);
	
	if (fsck_mode (fs) == FSCK_FIX_FIXABLE) {
	    /* add "." and ".." exist */
	    reiserfs_tree_pathrelse (&path);
	    
	    entry_len = reiserfs_direntry_entry_estimate (".", dir_format);
	    reiserfs_tree_insert_entry (fs, key, ".",  entry_len, key, 0);
	    entry_len = reiserfs_direntry_entry_estimate ("..", dir_format);
	    reiserfs_tree_insert_entry (fs, key, "..", entry_len, parent, 0);
	    
	    fsck_log (" - entry was added\n");
	    goto start_again;
	} else {
	    one_more_corruption (fs, FIXABLE);
	    fsck_log ("\n");
	    if (retval == DIRECTORY_NOT_FOUND)
	        return 0;
	}
    }

    /* mark hidden entries as visible, set "." and ".." correctly */
    deh += *pos_in_item;
    for (i = *pos_in_item; i < reiserfs_ih_get_entries (ih); i ++, deh ++) {
	if (reiserfs_deh_get_off (deh) == OFFSET_DOT) {
	    if (reiserfs_key_comp2 (&(deh->deh2_dir_id), key)) {
		/* "." must point to the directory it is in */
		
		//deh->deh_objectid != REISERFS_ROOT_PARENT_OBJECTID)/*????*/ {
		fsck_log ("get_next_directory_item: The entry \".\" of the "
			  "directory %K points to %K, instead of %K", key, 
			  (reiserfs_key_t *)(&(deh->deh2_dir_id)), key);
		if (fsck_mode (fs) == FSCK_FIX_FIXABLE) {
		    reiserfs_deh_set_did (deh, reiserfs_key_get_did (key));
		    reiserfs_deh_set_obid (deh, reiserfs_key_get_oid (key));
		    reiserfs_buffer_mkdirty (bh);
		    fsck_log (" - corrected\n");
		} else {
		    one_more_corruption (fs, FIXABLE);
		    fsck_log ("\n");
		}
	    }
	}

	if (reiserfs_deh_get_off (deh) == OFFSET_DOT_DOT) {
	    /* set ".." so that it points to the correct parent directory */
	    if (reiserfs_key_comp2 (&(deh->deh2_dir_id), parent)) {
		fsck_log ("get_next_directory_item: The entry \"..\" of the "
			  "directory %K points to %K, instead of %K", key, 
			  (reiserfs_key_t *)(&(deh->deh2_dir_id)), parent);
		
		if (fsck_mode (fs) == FSCK_FIX_FIXABLE) {
		    reiserfs_deh_set_did (deh, reiserfs_key_get_did (parent));
		    reiserfs_deh_set_obid (deh, reiserfs_key_get_oid (parent));
		    reiserfs_buffer_mkdirty (bh);
		    fsck_log (" - corrected\n");
		} else {
		    one_more_corruption (fs, FIXABLE);
		    fsck_log ("\n");
		}
	    }
	}
    }

    /* copy directory item to the temporary buffer */
    dir_item = misc_getmem (reiserfs_ih_get_len (ih));
    memcpy (dir_item, reiserfs_item_by_ih (bh, ih), reiserfs_ih_get_len (ih));


    /* next item key */
    if (REISERFS_PATH_LEAF_POS (&path) == (reiserfs_node_items (bh) - 1) &&
	(rdkey = reiserfs_tree_rkey (&path, fs)))
	reiserfs_key_copy (key, rdkey);
    else {
	reiserfs_key_set_did (key, 0);
	reiserfs_key_set_oid (key, 0);
    }

    if (fsck_mode (fs) == FSCK_REBUILD)
        fsck_item_mkreach (REISERFS_PATH_IH (&path), bh);
    reiserfs_tree_pathrelse (&path);

    return dir_item;
}

/* semantic pass of --check */
static int check_semantic_pass (const reiserfs_key_t * key, 
				const reiserfs_key_t * parent, 
				int dot_dot, 
				reiserfs_ih_t * new_ih)
{
    reiserfs_path_t path;
    void * sd;
    __u32 nlink;
    int is_new_dir;
    reiserfs_bh_t * bh;
    reiserfs_ih_t * ih;
    int retval;
    char * dir_item;
    __u32 pos_in_item;
    reiserfs_ih_t tmp_ih;
    reiserfs_key_t next_item_key, entry_key, object_key;
    __u64 dir_size = 0;
    __u32 blocks;
    __u64 sd_size;
    __u32 sd_blocks;
    int fix_sd;
    /*int relocate;*/
    int dir_format = 0;
    __u16 mode;
    	
    retval = OK;

 /* start_again: when directory was relocated */

    if (!reiserfs_key_stat (key)) {
	fsck_log ("%s: The key %k must be key of a StatData\n", 
		  __FUNCTION__, key);
	one_more_corruption (fs, FATAL);
        return STAT_DATA_NOT_FOUND;
    }

    /* look for stat data of an object */
    if (reiserfs_tree_search_item (fs, key, &path) == ITEM_NOT_FOUND) {
	reiserfs_tree_pathrelse (&path);
	return STAT_DATA_NOT_FOUND;
    }

    /* stat data has been found */
    ih = REISERFS_PATH_IH (&path);
    sd = REISERFS_PATH_ITEM(&path);

    reiserfs_stat_get_nlink (ih, sd, &nlink);

    /* It seems quite difficult to relocate objects on fix-fixable - 
     * rewrite_file calls reiserfs_file_write which can convert tails 
     * to unfm, plus unreachable, was_tail flags, etc. */
#if 0
    if ((/* relocate = */ should_be_relocated(&ih->ih_key))) {
	/*
	if (fsck_mode(fs) == FSCK_CHECK)
	    relocate = 0;
	*/
	one_more_corruption(fs, FATAL);
    }
#endif

    if (fix_obviously_wrong_sd_mode (&path)) {
        one_more_corruption (fs, FIXABLE);
	reiserfs_tree_pathrelse (&path);
        return OK;
    }
    
    if (nlink == 0) {
	fsck_log ("%s: block %lu: The StatData %k has "
		  "bad nlink number (%u)\n", __FUNCTION__,
		  REISERFS_PATH_LEAF(&path)->b_blocknr, 
		  &ih->ih_key, nlink);
	
	one_more_corruption (fs, FATAL); 
    }
    
    if (not_a_directory (sd)) {
	fsck_check_stat (fs)->files ++;
	
	retval = check_check_regular_file (&path, sd, 0);
	reiserfs_tree_pathrelse (&path);
	return retval;
    }
    
/*
    if (relocate) {
	if (!new_ih)
	    reiserfs_panic ("%s: Memory is not prepared for relocation of %K", 
			    __FUNCTION__, &ih->ih_key);
	*new_ih = *ih;
	reiserfs_tree_pathrelse (&path);
	sem_pass_stat (fs)->oid_sharing_dirs_relocated ++;
	relocate_dir (new_ih, 1);
	fsck_relocate_mklinked(&new_ih->ih_key);
	one_less_corruption (fs, FIXABLE);
	*key = new_ih->ih_key;
	retval = RELOCATED;
	goto start_again;
    }
*/

/* 
    if (fsck_mode (fs) == FSCK_FIX_FIXABLE) {
    	// it looks like stat data of a directory found 
    	if (nlink) {
	    // we saw this directory already 
	    if (!dot_dot) {
	    	// this name is not ".."  - and hard links are not 
		// allowed on directories 
	    	reiserfs_tree_pathrelse (&path);
	    	return STAT_DATA_NOT_FOUND;
	    } else {
	    	// ".." found 
	    	nlink ++;
	    	reiserfs_stat_set_nlink (ih, sd, &nlink);
	    	reiserfs_buffer_mkdirty (REISERFS_PATH_LEAF (&path));
	    	reiserfs_tree_pathrelse (&path);
	    	return OK;
	    }
    	} // do not run it for dot_dot on check at all
    	
     	nlink = 2;
    	if (reiserfs_key_get_oid (key) == REISERFS_ROOT_OBJECTID)
	    nlink ++;
    	reiserfs_stat_set_nlink (ih, sd, &nlink);
    	reiserfs_buffer_mkdirty (REISERFS_PATH_LEAF (&path));    
    }
*/

    /* directory stat data found */
    if (reiserfs_ih_get_len (ih) == REISERFS_SD_SIZE)
	is_new_dir = 1;
    else
	is_new_dir = 0;

    /* save stat data's size and st_blocks */
    reiserfs_stat_get_size (ih, sd, &sd_size);
    reiserfs_stat_get_blocks (ih, sd, &sd_blocks);
    reiserfs_stat_get_mode (ih, sd, &mode);

    dir_format = (reiserfs_ih_get_len (REISERFS_PATH_IH (&path)) == 
		  REISERFS_SD_SIZE) ? KEY_FORMAT_2 : KEY_FORMAT_1;

    /* release path pointing to stat data */
    reiserfs_tree_pathrelse (&path);

    fsck_check_stat (fs)->dirs ++;

    reiserfs_key_set_did (&next_item_key, reiserfs_key_get_did (key));
    reiserfs_key_set_oid (&next_item_key, reiserfs_key_get_oid (key));
    reiserfs_key_set_off1 (&next_item_key, OFFSET_DOT);
    reiserfs_key_set_uni (&next_item_key, UNI_DE);

    dir_size = 0;
    while ((dir_item = get_next_directory_item (&next_item_key, parent, &tmp_ih, 
						&pos_in_item, dir_format)) != 0)
    {
	/* dir_item is copy of the item in separately allocated memory,
	   item_key is a key of next item in the tree */
	int i;
	char name[REISERFS_NAME_MAX];
	int namelen, entry_len;
	reiserfs_deh_t * deh = (reiserfs_deh_t *)dir_item + pos_in_item;
	
	for (i = pos_in_item; 
	     i < reiserfs_ih_get_entries (&tmp_ih); 
	     i ++, deh ++) 
	{
	    reiserfs_ih_t relocated_ih;
	    int ret = OK;
	    
	    name[0] = '\0';
	
	    namelen = reiserfs_direntry_name_len (&tmp_ih, deh, i);
	    sprintf(name, "%.*s", namelen, reiserfs_deh_name (deh, i));
	    entry_len = reiserfs_direntry_entry_len (&tmp_ih, deh, i);
	
	    get_object_key (deh, &object_key, &entry_key, &tmp_ih);

	    if ((dir_format == KEY_FORMAT_2) && (entry_len % 8 != 0)) {
		/* not alighed directory of new format - delete it */
		if (fsck_mode (fs) == FSCK_FIX_FIXABLE) {
		    fsck_log ("Entry %K (\"%.*s\") in the directory %K is not "
			      "formated properly - deleted\n",
			      (reiserfs_key_t *)&(deh->deh2_dir_id), namelen, 
			      name, &tmp_ih.ih_key);
		    
		    reiserfs_tree_delete_entry (fs, &entry_key);
		    
		    entry_len = 
			reiserfs_direntry_entry_estimate (name, dir_format);

		    reiserfs_tree_insert_entry (fs, key, name, entry_len,
			(reiserfs_key_t *)&(deh->deh2_dir_id), 0);
		} else {
		    fsck_log ("Entry %K (\"%.*s\") in the directory %K is "
			      "not formated properly.\n", 
			      (reiserfs_key_t *)&(deh->deh2_dir_id), 
			      namelen, name, &tmp_ih.ih_key);
		    
		    one_more_corruption (fs, FIXABLE);
		}
	    }
	
	    if (!reiserfs_hash_correct (&fs->hash, name, 
					namelen, reiserfs_deh_get_off (deh))) 
	    {
		one_more_corruption (fs, FATAL);
		fsck_log ("%s: Hash mismatch detected for (%.*s) in directory "
			  "%K\n", __FUNCTION__, namelen, name, &tmp_ih.ih_key);
	    }
	
	    if (is_dot (name, namelen) || (is_dot_dot (name, namelen))) {
		/* do not go through "." and ".." */
		ret = OK;
	    } else {
		if (!fsck_quiet(fs)) {
			util_misc_print_name (fsck_progress_file(fs), 
					      name, namelen);
		}
	    
		if ((ret = add_path_key (&object_key)) == 0) {
		    ret = check_semantic_pass (&object_key, key, 
					       is_dot_dot(name, namelen), 
					       &relocated_ih);
		    del_path_key ();
		}

		if (!fsck_quiet(fs)) {
		    util_misc_erase_name (fsck_progress_file(fs), 
					  namelen);
		}
	    }
	    
	    /* check what check_semantic_tree returned */
	    switch (ret) {
	    case OK:
		dir_size += REISERFS_DEH_SIZE + entry_len;
		break;
		
	    case STAT_DATA_NOT_FOUND:
		fsck_log ("%s: Name \"%.*s\" in directory %K points to "
			  "nowhere\n", __FUNCTION__, namelen, name, 
			  &tmp_ih.ih_key);
	    case LOOP_FOUND:
		if (fsck_mode (fs) == FSCK_FIX_FIXABLE) {
		    reiserfs_tree_delete_entry (fs, &entry_key);
		    fsck_log (" - removed");
		} else {
		    one_more_corruption (fs, FIXABLE);
		}
		fsck_log ("\n");
		break;
		    
	    case DIRECTORY_HAS_NO_ITEMS:
		fsck_log ("%s: Name \"%.*s\" in directory %K points a "
			  "directory without body\n", __FUNCTION__, 
			  namelen, name, &tmp_ih.ih_key);
		
		/* fixme: stat data should be deleted as well */
		/*
		  if (fsck_fix_fixable (fs)) {
		  reiserfs_tree_delete_entry (fs, &entry_key);
		  fsck_data(fs)->deleted_entries ++;
		  fsck_log (" - removed");
		  }
		  fsck_log ("\n");*/
		break;
		
	    case RELOCATED:
		/* file was relocated, update key in corresponding direntry */
		if (reiserfs_tree_search_entry (fs, &entry_key, &path) != 
		    POSITION_FOUND) 
		{
		    fsck_log("Cannot find a name of the relocated file %K in "
			     "the directory %K\n", &entry_key, &tmp_ih.ih_key);
		} else {
		    /* update key dir entry points to */
		    reiserfs_deh_t * tmp_deh;
		    
		    tmp_deh = reiserfs_deh (REISERFS_PATH_LEAF (&path), 
					    REISERFS_PATH_IH (&path)) + 
			      path.pos_in_item;
		    
		    fsck_log ("The directory %K pointing to %K (\"%.*s\") "
			      "updated to point to ", &tmp_ih.ih_key, 
			      &tmp_deh->deh2_dir_id, namelen, name);
		    
		    reiserfs_deh_set_did (tmp_deh, 
			reiserfs_key_get_did (&relocated_ih.ih_key));
		    reiserfs_deh_set_obid (tmp_deh, 
			reiserfs_key_get_oid (&relocated_ih.ih_key));

		    fsck_log ("%K (\"%.*s\")\n",  &tmp_deh->deh2_dir_id, 
			      namelen, name);
		    
		    reiserfs_buffer_mkdirty (REISERFS_PATH_LEAF (&path));
		}
		
		dir_size += REISERFS_DEH_SIZE + entry_len;
		reiserfs_tree_pathrelse (&path);
		break;
	    }
	} /* for */
	
	misc_freemem (dir_item);

	if (reiserfs_key_comp2 (&next_item_key, key))
	    /* next key is not of this directory */
	    break;
    } /* while (dir_item) */
    
    
    if (dir_size == 0)
	/* FIXME: is it possible? */
	return DIRECTORY_HAS_NO_ITEMS;
    
    /* calc correct value of sd_blocks field of stat data */
    blocks = REISERFS_DIR_BLOCKS (dir_size);
    
    fix_sd = 0;
    fix_sd += wrong_st_blocks (key, &blocks, sd_blocks, mode, is_new_dir);
    fix_sd += wrong_st_size (key, is_new_dir ? REISERFS_SD_SIZE_MAX_V2 : 
			     REISERFS_SD_SIZE_MAX_V1, fs->fs_blocksize, 
			     &dir_size, sd_size, TYPE_DIRENTRY);

    if (fix_sd) {
	if (fsck_mode (fs) == FSCK_FIX_FIXABLE) {
	    /* we have to fix either sd_size or sd_blocks, look for SD again */
	    if (reiserfs_tree_search_item (fs, key, &path) != ITEM_FOUND) {
		fsck_log ("%s: The StatData of the file %K was not found\n",
			  __FUNCTION__, key);
		one_more_corruption(fs, FATAL);
                return STAT_DATA_NOT_FOUND;
	    }
	
	    bh = REISERFS_PATH_LEAF (&path);
	    ih = REISERFS_PATH_IH (&path);
	    sd = REISERFS_PATH_ITEM (&path);
	
	    reiserfs_stat_set_size (ih, sd, &dir_size);
	    reiserfs_stat_set_blocks (ih, sd, &blocks);
	    reiserfs_buffer_mkdirty (bh);
	    reiserfs_tree_pathrelse (&path);
	} else {
	    fsck_check_stat (fs)->fixable_corruptions += fix_sd;
	}
    }
    
    return retval;
}

int check_safe_links ()
{
    reiserfs_path_t safe_link_path, path;
    reiserfs_key_t safe_link_key = {-1, 0, {{0, 0}}};
    reiserfs_key_t key = {0, 0, {{0, 0}}};
    const reiserfs_key_t * rkey;
    reiserfs_ih_t * tmp_ih;

    while (1) {
        if (reiserfs_key_get_did (&safe_link_key) == 0)
            break;

	reiserfs_tree_search_item (fs, &safe_link_key, &safe_link_path);
	
	if (reiserfs_nh_get_items( NODE_HEAD(REISERFS_PATH_LEAF(&safe_link_path))) <= 
	    REISERFS_PATH_LEAF_POS (&safe_link_path)) 
	{
	    reiserfs_tree_pathrelse (&safe_link_path);
            break;
	}	
	
	tmp_ih = REISERFS_PATH_IH(&safe_link_path);
	
        if (reiserfs_key_get_did(&tmp_ih->ih_key) != (__u32)-1 ||
	    reiserfs_key_get_oid(&tmp_ih->ih_key) == (__u32)-1) {
	    reiserfs_tree_pathrelse (&safe_link_path);
            break;
	}

        if (reiserfs_ih_get_len (tmp_ih) != 4)
	    reiserfs_panic ("Safe Link %k cannot be of the size %d", 
		&tmp_ih->ih_key, reiserfs_ih_get_len (tmp_ih));
	
	reiserfs_key_set_did(&key, d32_get((__u32 *)REISERFS_PATH_ITEM(&safe_link_path), 0));
	reiserfs_key_set_oid(&key, reiserfs_key_get_oid(&tmp_ih->ih_key));
	if ((rkey = reiserfs_tree_next_key(&safe_link_path, fs)) == NULL)
	    reiserfs_key_set_did (&safe_link_key, 0);
	else
	    safe_link_key = *rkey;
	
	if (reiserfs_tree_search_item (fs, &key, &path) == ITEM_NOT_FOUND) {
	    /*sware on check, delete on fix-fixable*/
	    if (fsck_mode(fs) == FSCK_CHECK) {
		fsck_log ("Invalid safe link %k: cannot find the pointed "
			  "object (%K)\n", &tmp_ih->ih_key, &key);
		
		one_more_corruption (fs, FIXABLE);
	    } else if (fsck_mode(fs) == FSCK_FIX_FIXABLE) {
		fsck_log ("Invalid safe link %k: cannot find the pointed "
			  "object (%K) - safe link was deleted\n", 
			  &tmp_ih->ih_key, &key);
		
		d32_put((__u32 *)REISERFS_PATH_ITEM(&safe_link_path), 0, 0);
		reiserfs_tree_pathrelse (&path);
		reiserfs_tree_delete (fs, &safe_link_path, 0);		
		continue;
	    }
	} else if (reiserfs_key_get_off(&tmp_ih->ih_key) == 0x1) {
	    /* Truncate */
	    if (!not_a_directory (REISERFS_PATH_ITEM(&path))) {
		/*truncate on directory should not happen*/
		/*sware on check, delete on fix-fixable*/
		if (fsck_mode(fs) == FSCK_CHECK) {
		    fsck_log ("Invalid 'truncate' safe link %k, cannot happen "
			      "for directory (%K)\n", &tmp_ih->ih_key, &key);
		    one_more_corruption (fs, FIXABLE);
		} else if (fsck_mode(fs) == FSCK_FIX_FIXABLE) {
		    fsck_log ("Invalid 'truncate' safe link %k, cannot happen "
			      "for a directory (%K) - safe link was deleted\n", 
			      &tmp_ih->ih_key, &key);
		    
		    d32_put((__u32 *)REISERFS_PATH_ITEM(&safe_link_path), 0, 0);
		    reiserfs_tree_pathrelse (&path);
		    reiserfs_tree_delete (fs, &safe_link_path, 0);		
		    continue;
		}
	    } else {
		/* save 'safe truncate links' to avoid wrong sizes swaring. */
		int position;
		
		if (misc_bin_search (&key, trunc_links, links_num, 
					 sizeof(key), &position, 
					 reiserfs_key_comp2) != 1)
		{
		    blocklist__insert_in_position(&key, (void *)&trunc_links, 
						  &links_num, sizeof(key), 
						  &position);
		}
	    }
	}
	
	reiserfs_tree_pathrelse (&path);
	reiserfs_tree_pathrelse (&safe_link_path);
    }

    return OK;
}

void release_safe_links () {
    misc_freemem(trunc_links);
}

/* called when --check is given */
void fsck_semantic_check (void) {
    if (fsck_data (fs)->check.bad_nodes) {
        fsck_progress ("Bad nodes were found, Semantic pass skipped\n");
        goto clean;
    }

    if (fsck_data (fs)->check.fatal_corruptions) {
        fsck_progress ("Fatal corruptions were found, Semantic pass skipped\n");
        goto clean;
    }
 
    fsck_progress ("Checking Semantic tree...\n");
    
    if (fsck_mode(fs) == FSCK_FIX_FIXABLE) {
        /*create new_bitmap, and initialize new_bitmap & allocable bitmap*/
        fsck_new_bitmap (fs) = 
		reiserfs_bitmap_create(reiserfs_sb_get_blocks(fs->fs_ondisk_sb));
	
        reiserfs_bitmap_copy (fsck_new_bitmap (fs), fs->fs_bitmap2);
        fsck_allocable_bitmap (fs) = 
		reiserfs_bitmap_create(reiserfs_sb_get_blocks(fs->fs_ondisk_sb));
	
        reiserfs_bitmap_copy (fsck_allocable_bitmap (fs), fs->fs_bitmap2);
	fs->block_allocator = reiserfsck_new_blocknrs;
	fs->block_deallocator = reiserfsck_free_block;
    }
    
    check_safe_links ();
    
    if (check_semantic_pass (&root_dir_key, &parent_root_dir_key, 0, 0) != OK) {
        fsck_log ("check_semantic_tree: No root directory found");
        one_more_corruption (fs, FATAL);
    }

    release_safe_links ();

    if (fsck_mode(fs) == FSCK_FIX_FIXABLE) {
        reiserfs_bitmap_delete (fs->fs_bitmap2);
	reiserfs_bitmap_delta (fsck_new_bitmap (fs), fsck_deallocate_bitmap (fs));	
        fs->fs_bitmap2 = fsck_new_bitmap (fs);
        reiserfs_bitmap_delete (fsck_allocable_bitmap (fs));	
        fsck_allocable_bitmap (fs) = NULL;
        reiserfs_sb_set_free (fs->fs_ondisk_sb, 
			    reiserfs_bitmap_zeros (fs->fs_bitmap2));
	
        reiserfs_buffer_mkdirty (fs->fs_super_bh);
	reiserfs_badblock_flush(fs, 1);
    }
   
    if (!fsck_quiet(fs))
	util_misc_fini_name(fsck_progress_file(fs));
    
clean:
    if (fsck_deallocate_bitmap (fs))
	reiserfs_bitmap_delete (fsck_deallocate_bitmap (fs));
}
