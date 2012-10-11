/*
 * Copyright 1996-2002 Hans Reiser
 */
#include "fsck.h"

int wrong_mode (struct key * key, __u16 * mode, __u64 real_size, int symlink);
int wrong_st_blocks (struct key * key, __u32 * blocks, __u32 sd_blocks, __u16 mode, int new_format);
int wrong_st_size (struct key * key, loff_t max_file_size, int blocksize,
		   __u64 * size, __u64 sd_size, int is_dir);
int wrong_first_direct_byte (struct key * key, int blocksize, 
			     __u32 * first_direct_byte,
			     __u32 sd_first_direct_byte, __u32 size);
void get_object_key (struct reiserfs_de_head * deh, struct key * key, 
		     struct key * entry_key, struct item_head * ih);
void print_name (char * name, int len);
void erase_name (int len);


struct path_key
{
    struct short_key
    {
        __u32 k_dir_id;
        __u32 k_objectid;
    } key;
    struct path_key * next, * prev;
};

struct path_key * head_key = NULL;
struct path_key * tail_key = NULL;

static int check_path_key(struct key * key)
{
    struct path_key * cur = head_key;

    while(cur != NULL)
    {
        if (!comp_short_keys(&cur->key, key)) {
            fsck_log("\nsemantic check: directory %k has 2 names ", key);
            return LOOP_FOUND;
        }
        cur = cur->next;
    }
    return 0;
}

static int add_path_key(struct key * key, int check)
{
    if (check && check_path_key(key))
    	return LOOP_FOUND;

    if (tail_key == NULL)
    {
        tail_key = getmem(sizeof(struct path_key));
        head_key = tail_key;
        tail_key->prev = NULL;
    }else{
        tail_key->next = getmem(sizeof(struct path_key));
        tail_key->next->prev = tail_key;
        tail_key = tail_key->next;
    }
    copy_short_key (&tail_key->key, key);
    tail_key->next = NULL;

    return 0;
}

void del_path_key()
{
    if (tail_key == NULL)
        die("wrong path_key structure");

    if (tail_key->prev == NULL)
    {
        freemem(tail_key);
        tail_key = head_key = NULL;
    }else{
        tail_key = tail_key->prev;
        freemem(tail_key->next);
        tail_key->next = NULL;
    }
}

/* path is path to stat data. If file will be relocated - new_ih will contain
   a key file was relocated with */
static int check_check_regular_file (struct path * path, void * sd,
                                     struct item_head * new_ih)
{
    int is_new_file;
//    struct key key, sd_key;
    __u16 mode;
    __u32 nlink;
    __u64 real_size, saved_size;
    __u32 blocks, saved_blocks;	/* proper values and value in stat data */
    __u32 first_direct_byte, saved_first_direct_byte;

    struct buffer_head * bh;
    struct item_head * ih, sd_ih;
    int fix_sd;
    int symlnk = 0;
    int retval = OK;


    ih = get_ih (path);
    bh = get_bh (path);

    if (new_ih) {
	/* this objectid is used already */
	*new_ih = *ih;
	pathrelse (path);
	relocate_file (new_ih, 1);
	one_less_corruption (fs, fixable);
	sem_pass_stat (fs)->oid_sharing_files_relocated ++;
	retval = RELOCATED;
	if (reiserfs_search_by_key_4 (fs, &(new_ih->ih_key), path) == ITEM_NOT_FOUND)
	    reiserfs_panic ("check_check_regular_file: could not find stat data of relocated file");
	/* stat data is marked unreachable again due to relocation, fix that */
	ih = get_ih (path);
	bh = get_bh (path);
	sd = get_item (path);
    }
    

    if (get_ih_item_len (ih) == SD_SIZE)
	is_new_file = 1;
    else
	is_new_file = 0;


    get_sd_nlink (ih, sd, &nlink);
    get_sd_mode (ih, sd, &mode);
    get_sd_size (ih, sd, &saved_size);
    get_sd_blocks (ih, sd, &saved_blocks);
	
    if (fsck_mode (fs) == FSCK_FIX_FIXABLE) {
    	/* check and set nlink first */
    	nlink ++;
    	set_sd_nlink (ih, sd, &nlink);
    	mark_buffer_dirty (bh);

    	if (nlink > 1)
	    return OK;
    }
 
    if (!is_new_file)
	get_sd_first_direct_byte (ih, sd, &saved_first_direct_byte);

    if (S_ISLNK (mode)) 	
	symlnk = 1;
    
    sd_ih = *ih;
//    sd_key = sd_ih.ih_key;
    pathrelse (path);

    if (are_file_items_correct (&sd_ih, sd, &real_size, &blocks, 0/*do not mark items reachable*/, &symlnk) != 1) {
	one_more_corruption (fs, fatal);
	fsck_log ("check_regular_file: broken file found %K\n", &sd_ih.ih_key);
    } else {
	fix_sd = 0;
    
	fix_sd += wrong_mode (&sd_ih.ih_key, &mode, real_size, symlnk);
	if (!is_new_file)
	    fix_sd += wrong_first_direct_byte (&sd_ih.ih_key, fs->fs_blocksize,
					       &first_direct_byte, saved_first_direct_byte, real_size);
	fix_sd += wrong_st_size (&sd_ih.ih_key, is_new_file ? MAX_FILE_SIZE_V2 : MAX_FILE_SIZE_V1,
				 fs->fs_blocksize, &real_size, saved_size, 0/*not dir*/);
	
	fix_sd += wrong_st_blocks (&sd_ih.ih_key, &blocks, saved_blocks, mode, is_new_file);

	if (fix_sd) {
	    if (fsck_mode (fs) == FSCK_FIX_FIXABLE) {
  	        /* find stat data and correct it */
  	        set_type_and_offset (KEY_FORMAT_1, &sd_ih.ih_key, SD_OFFSET, TYPE_STAT_DATA);
	        if (reiserfs_search_by_key_4 (fs, &sd_ih.ih_key, path) != ITEM_FOUND) {
		    fsck_log ("check_regular_file: stat data not found");
                    one_more_corruption (fs, fatal);
                    return STAT_DATA_NOT_FOUND;
	        }
	    
	        bh = get_bh (path);
	        ih = get_ih (path);
	        sd = get_item (path);
	        set_sd_size (ih, sd, &real_size);
	        set_sd_blocks (ih, sd, &blocks);
	        set_sd_mode (ih, sd, &mode);
	        if (!is_new_file)
		    set_sd_first_direct_byte (ih, sd, &first_direct_byte);
		mark_buffer_dirty (bh);
	    } else {
		fsck_check_stat (fs)->fixable_corruptions += fix_sd;
	    }
	}
    }    
    return OK;
}

/* returns buffer, containing found directory item.*/
static char * get_next_directory_item (struct key * key, /* on return this will
						     contain key of next item
						     in the tree */
				struct key * parent,
				struct item_head * ih,/*not in tree*/
				__u32 * pos_in_item, int dir_format)
{
    INITIALIZE_PATH (path);
    char * dir_item;
    struct key * rdkey;
    struct buffer_head * bh;
    struct reiserfs_de_head * deh;
    int i;
    int retval;


start_again:

    retval = reiserfs_search_by_entry_key (fs, key, &path);

    if (retval != POSITION_FOUND && get_offset (key) != DOT_OFFSET)
	reiserfs_panic ("get_next_directory_item: %k is not found", key);

    /* leaf containing directory item */
    bh = PATH_PLAST_BUFFER (&path);
    *pos_in_item = path.pos_in_item;
    *ih = *get_ih (&path);
    deh = B_I_DEH (bh, ih);

    /* position was not found for '.' or there is no '..' */
    if (retval != POSITION_FOUND || ((get_offset (key) == DOT_OFFSET) &&
	(get_ih_entry_count (ih) < 2 || name_in_entry_length (ih, deh + 1, 1) != 2 ||
	 strncmp (name_in_entry (deh + 1, 1), "..", 2)))) {

	fsck_log ("get_next_directory_item: %k %s is not found", key,
		(retval == POSITION_NOT_FOUND) ? "entry" : "directory");
	
	if (fsck_mode (fs) == FSCK_FIX_FIXABLE) {
	    /* add "." and ".." exist */
	    pathrelse (&path);
	    reiserfs_add_entry (fs, key, ".",  name_length (".", dir_format), key, 0);
	    reiserfs_add_entry (fs, key, "..", name_length ("..", dir_format), parent, 0);
	    fsck_log (" - fixed\n");
	    goto start_again;
	} else {
	    one_more_corruption (fs, fixable);
	    fsck_log ("\n");
	    if (retval == DIRECTORY_NOT_FOUND)
	        return 0;
	}
    }

    /* make sure, that ".." exists as well */
/*
    if (get_offset (key) == DOT_OFFSET) {
	if (get_ih_entry_count (ih) < 2 ||
	    name_in_entry_length (ih, deh + 1, 1) != 2 ||
	    strncmp (name_in_entry (deh + 1, 1), "..", 2))
	{
	    fsck_log ("get_next_directory_item: \"..\" not found in %H\n", ih);
	    pathrelse (&path);
	    return 0;
	}
    }
*/
    /* mark hidden entries as visible, set "." and ".." correctly */
    deh += *pos_in_item;
    for (i = *pos_in_item; i < get_ih_entry_count (ih); i ++, deh ++) {
	int namelen;
	char * name;

	name = name_in_entry (deh, i);
	namelen = name_in_entry_length (ih, deh, i);
/*	if (de_hidden (deh)) // handled in check_tree
	    reiserfs_panic ("get_next_directory_item: item %k: hidden entry %d \'%.*s\'\n",
			    key, i, namelen, name);
*/

	if (get_deh_offset (deh) == DOT_OFFSET) {
	    if (not_of_one_file (&(deh->deh2_dir_id), key)) {
		/* "." must point to the directory it is in */
		
		//deh->deh_objectid != REISERFS_ROOT_PARENT_OBJECTID)/*????*/ {
		fsck_log ("get_next_directory_item: %k: \".\" pointes to [%K], "
			"should point to [%K]", key, (struct key *)(&(deh->deh2_dir_id)));
		if (fsck_mode (fs) == FSCK_FIX_FIXABLE) {
		    set_deh_dirid (deh, get_key_dirid (key));
		    set_deh_objectid (deh, get_key_objectid (key));
		    mark_buffer_dirty (bh);
		    fsck_log (" - fixed\n");
		} else
		    fsck_log ("\n");
	    }
	}

	if (get_deh_offset (deh) == DOT_DOT_OFFSET) {
	    /* set ".." so that it points to the correct parent directory */
	    if (comp_short_keys (&(deh->deh2_dir_id), parent)) {
		fsck_log ("get_next_directory_item: %k: \"..\" pointes to [%K], "
			"should point to [%K]", key, (struct key *)(&(deh->deh2_dir_id)), parent);
		if (fsck_mode (fs) == FSCK_FIX_FIXABLE) {
		    set_deh_dirid (deh, get_key_dirid (parent));
		    set_deh_objectid (deh, get_key_objectid (parent));
		    mark_buffer_dirty (bh);
		    fsck_log (" - fixed\n");
		} else
		    fsck_log ("\n");
	    }
	}
    }

    /* copy directory item to the temporary buffer */
    dir_item = getmem (get_ih_item_len (ih));
    memcpy (dir_item, B_I_PITEM (bh, ih), get_ih_item_len (ih));


    /* next item key */
    if (PATH_LAST_POSITION (&path) == (B_NR_ITEMS (bh) - 1) &&
	(rdkey = uget_rkey (&path)))
	copy_key (key, rdkey);
    else {
	set_key_dirid (key, 0);
	set_key_objectid (key, 0);
    }

    if (fsck_mode (fs) != FSCK_CHECK && fsck_mode (fs) != FSCK_FIX_FIXABLE)
        mark_item_reachable (get_ih (&path), bh);
    pathrelse (&path);

    return dir_item;
}

/* semantic pass of --check */
static int check_semantic_pass (struct key * key, struct key * parent, int dot_dot, struct item_head * new_ih)
{
    struct path path;
    void * sd;
    __u32 nlink;
    int is_new_dir;
    struct buffer_head * bh;
    struct item_head * ih;
    int retval;
    char * dir_item;
    __u32 pos_in_item;
    struct item_head tmp_ih;
    struct key next_item_key, entry_key, object_key;
    __u64 dir_size = 0;
    __u32 blocks;
    __u64 saved_size;
    __u32 saved_blocks;
    int fix_sd;
    int relocate;
    int dir_format = 0;
    __u16 mode;
    	
    retval = OK;

 start_again: /* when directory was relocated */

    if (!KEY_IS_STAT_DATA_KEY (key)) {
	fsck_log ("check_semantic_pass: key must be key of a stat data");
	one_more_corruption (fs, fatal);
        return STAT_DATA_NOT_FOUND;
    }

    /* look for stat data of an object */
    if (reiserfs_search_by_key_4 (fs, key, &path) == ITEM_NOT_FOUND) {
	pathrelse (&path);
	return STAT_DATA_NOT_FOUND;
    }

    /* stat data has been found */
    ih = get_ih (&path);
    sd = get_item(&path);

    get_sd_nlink (ih, sd, &nlink);
    relocate = should_be_relocated(&ih->ih_key);

    if (fix_obviously_wrong_sd_mode (&path)) {
        one_more_corruption (fs, fixable);
        return OK;
    }

    if (not_a_directory (sd)) {
	fsck_check_stat (fs)->files ++;
	retval = check_check_regular_file (&path, sd, relocate ? new_ih : 0);
	pathrelse (&path);
	return retval;
    }

    if (relocate) {
	if (!new_ih)
	    reiserfs_panic ("check_semantic_pass: can not relocate %K",
			    &ih->ih_key);
	*new_ih = *ih;
	pathrelse (&path);
	sem_pass_stat (fs)->oid_sharing_dirs_relocated ++;
	relocate_dir (new_ih, 1);
	one_less_corruption (fs, fixable);
	*key = new_ih->ih_key;
	retval = RELOCATED;
	goto start_again;
    }
    
    if (fsck_mode (fs) == FSCK_FIX_FIXABLE) {
    	/* it looks like stat data of a directory found */
    	if (nlink) {
	    /* we saw this directory already */
	    if (!dot_dot) {
	    	/* this name is not ".."  - and hard links are not allowed on
      		   directories */
	    	pathrelse (&path);
	    	return STAT_DATA_NOT_FOUND;
	    } else {
	    	/* ".." found */
	    	nlink ++;
	    	set_sd_nlink (ih, sd, &nlink);
	    	mark_buffer_dirty (get_bh (&path));
	    	pathrelse (&path);
	    	return OK;
	    }
    	} /*do not run it for dot_dot on check at all*/
    	
     	nlink = 2;
    	if (get_key_objectid (key) == REISERFS_ROOT_OBJECTID)
	    nlink ++;
    	set_sd_nlink (ih, sd, &nlink);
    	mark_buffer_dirty (get_bh (&path));    
    }
    
    /* directory stat data found */
    if (get_ih_item_len (ih) == SD_SIZE)
	is_new_dir = 1;
    else
	is_new_dir = 0;

    /* save stat data's size and st_blocks */
    get_sd_size (ih, sd, &saved_size);
    get_sd_blocks (ih, sd, &saved_blocks);
    get_sd_mode (ih, sd, &mode);

    dir_format = (get_ih_item_len (get_ih (&path)) == SD_SIZE) ? KEY_FORMAT_2 : KEY_FORMAT_1;

    /* release path pointing to stat data */
    pathrelse (&path);

    fsck_check_stat (fs)->dirs ++;

    set_key_dirid (&next_item_key, get_key_dirid (key));
    set_key_objectid (&next_item_key, get_key_objectid (key));
    set_key_offset_v1 (&next_item_key, DOT_OFFSET);
    set_key_uniqueness (&next_item_key, DIRENTRY_UNIQUENESS);

    dir_size = 0;
    while ((dir_item = get_next_directory_item (&next_item_key, parent, &tmp_ih, &pos_in_item, dir_format)) != 0) {
	/* dir_item is copy of the item in separately allocated memory,
	   item_key is a key of next item in the tree */
	int i;
	char * name = 0;
	int namelen, entry_len;
	struct reiserfs_de_head * deh = (struct reiserfs_de_head *)dir_item + pos_in_item;
	
	
	for (i = pos_in_item; i < get_ih_entry_count (&tmp_ih); i ++, deh ++) {
	    struct item_head relocated_ih;
	    
	    if (name) {
		free (name);
		name = 0;
	    }
	
	    namelen = name_in_entry_length (&tmp_ih, deh, i);
	    asprintf (&name, "%.*s", namelen, name_in_entry (deh, i));	
	    entry_len = entry_length (&tmp_ih, deh, i);
	
	    get_object_key (deh, &object_key, &entry_key, &tmp_ih);

	    if ((dir_format == KEY_FORMAT_2) && (entry_len % 8 != 0)) {
		/* not alighed directory of new format - delete it */
		if (fsck_mode (fs) == FSCK_FIX_FIXABLE) {
		    fsck_log ("name \"%.*s\" in directory %K of wrong format, %K - deleted\n",
			namelen, name, &tmp_ih.ih_key, (struct key *)&(deh->deh2_dir_id));
		    reiserfs_remove_entry (fs, &entry_key);
		    entry_len = name_length (name, dir_format);
		    reiserfs_add_entry (fs, key, name, entry_len, (struct key *)&(deh->deh2_dir_id), 0);
		} else {
		    fsck_log ("name \"%.*s\" in directory %K of wrong format, %K\n",
			namelen, name, &tmp_ih.ih_key, (struct key *)&(deh->deh2_dir_id));
		    one_more_corruption (fs, fixable);
		}
	    }
	
	
	    print_name (name, namelen);
	    
	    if (!is_properly_hashed (fs, name, namelen, get_deh_offset (deh))) {
		one_more_corruption (fs, fatal);
		fsck_log ("check_semantic_pass: hash mismatch detected (%.*s)\n", namelen, name);
	    }
	
	    if (is_dot (name, namelen) || (is_dot_dot (name, namelen) &&
           	(fsck_mode (fs) != FSCK_FIX_FIXABLE || get_key_objectid (&object_key) == REISERFS_ROOT_PARENT_OBJECTID))) {
		/* do not go through "." and ".." */
		retval = OK;
	    } else {
		if ((retval = add_path_key (&object_key, (fsck_mode (fs) != FSCK_FIX_FIXABLE)? 1 : 0)) == 0) {
		    retval = check_semantic_pass (&object_key, key, is_dot_dot(name, namelen), &relocated_ih);
		    del_path_key ();
		}
	    }
	    
	    erase_name (namelen);
	    
	    /* check what check_semantic_tree returned */
	    switch (retval) {
	    case OK:
		dir_size += DEH_SIZE + entry_len;
		break;
		
	    case STAT_DATA_NOT_FOUND:
		fsck_log ("check_semantic_pass: name \"%.*s\" in directory %K points to nowhere",
			  namelen, name, &tmp_ih.ih_key);
	    case LOOP_FOUND:
		if (fsck_mode (fs) == FSCK_FIX_FIXABLE) {
		    reiserfs_remove_entry (fs, &entry_key);
		    fsck_log (" - removed");
		} else {
		    one_more_corruption (fs, fixable);
		}
		fsck_log ("\n");
		break;
		    
	    case DIRECTORY_HAS_NO_ITEMS:
		fsck_log ("check_semantic_pass: name \"%.*s\" in directory %K points dir without body\n",
			  namelen, name, &tmp_ih.ih_key);
		
		/* fixme: stat data should be deleted as well */
		/*
		  if (fsck_fix_fixable (fs)) {
		  reiserfs_remove_entry (fs, &entry_key);
		  fsck_data(fs)->deleted_entries ++;
		  fsck_log (" - removed");
		  }
		  fsck_log ("\n");*/
		break;
		
	    case RELOCATED:
		/* file was relocated, update key in corresponding directory entry */
		if (reiserfs_search_by_entry_key (fs, &entry_key, &path) != POSITION_FOUND) {
		    fsck_progress ("could not find name of relocated file\n");
		} else {
		    /* update key dir entry points to */
		    struct reiserfs_de_head * tmp_deh;
		    
		    tmp_deh = B_I_DEH (get_bh (&path), get_ih (&path)) + path.pos_in_item;
		    fsck_log ("name \"%.*s\" of dir %K pointing to %K updated to point to ",
			      namelen, name, &tmp_ih.ih_key, &tmp_deh->deh2_dir_id);
		    set_deh_dirid (tmp_deh, get_key_dirid (&relocated_ih.ih_key));
		    set_deh_objectid (tmp_deh, get_key_objectid (&relocated_ih.ih_key));

		    fsck_log ("%K\n",  &tmp_deh->deh2_dir_id);
		    mark_buffer_dirty (get_bh (&path));
		}
		dir_size += DEH_SIZE + entry_len;
		pathrelse (&path);
		break;
	    }
	} /* for */
	
	freemem (dir_item);
	free (name);
	name = 0;

	if (not_of_one_file (&next_item_key, key))
	    /* next key is not of this directory */
	    break;
	
    } /* while (dir_item) */
    
    
    if (dir_size == 0)
	/* FIXME: is it possible? */
	return DIRECTORY_HAS_NO_ITEMS;
    
    /* calc correct value of sd_blocks field of stat data */
    blocks = dir_size2st_blocks (dir_size);
    
    fix_sd = 0;
    fix_sd += wrong_st_blocks (key, &blocks, saved_blocks, mode, is_new_dir);
    fix_sd += wrong_st_size (key, is_new_dir ? MAX_FILE_SIZE_V2 : MAX_FILE_SIZE_V1,
			     fs->fs_blocksize, &dir_size, saved_size, 1/*dir*/);

    if (fix_sd) {
	if (fsck_mode (fs) == FSCK_FIX_FIXABLE) {
	    /* we have to fix either sd_size or sd_blocks, so look for stat data again */
	    if (reiserfs_search_by_key_4 (fs, key, &path) != ITEM_FOUND) {
		fsck_log ("check_semantic_tree: stat data not found");
		one_more_corruption(fs, fatal);
                return STAT_DATA_NOT_FOUND;
	    }
	
	    bh = get_bh (&path);
	    ih = get_ih (&path);
	    sd = get_item (&path);
	
	    set_sd_size (ih, sd, &dir_size);
	    set_sd_blocks (ih, sd, &blocks);
	    mark_buffer_dirty (bh);
	    pathrelse (&path);
	} else {
	    fsck_check_stat (fs)->fixable_corruptions += fix_sd;
	}
    }
    
    return retval;
}

int check_safe_links ()
{
    struct path safe_link_path, path;
    struct key safe_link_key = {-1, 0, {{0, 0}}};
    struct key key = {0, 0, {{0, 0}}};
    struct key * rkey;
    struct item_head * tmp_ih;

    while (1) {
        if (get_key_dirid (&safe_link_key) == 0)
            break;

	reiserfs_search_by_key_4 (fs, &safe_link_key, &safe_link_path);
	
	if (get_blkh_nr_items ( B_BLK_HEAD (get_bh(&safe_link_path))) <= PATH_LAST_POSITION (&safe_link_path)) {
	    pathrelse (&safe_link_path);
            break;
	}	
	
	tmp_ih = get_ih(&safe_link_path);
	
        if (get_key_dirid(&tmp_ih->ih_key) != (__u32)-1 ||
	    get_key_objectid(&tmp_ih->ih_key) == (__u32)-1) {
	    pathrelse (&safe_link_path);
            break;
	}

        if (get_ih_item_len (tmp_ih) != 4)
	    reiserfs_panic ("safe link cannot be of size %d", get_ih_item_len (tmp_ih));
	
	set_key_dirid(&key, le32_to_cpu(*(__u32 *)get_item(&safe_link_path)));
	set_key_objectid(&key, get_key_objectid(&tmp_ih->ih_key));
	if ( (rkey = get_next_key_2 (&safe_link_path)) == NULL )
	    set_key_dirid (&safe_link_key, 0);
	else
	    safe_link_key = *rkey;
	
	if (reiserfs_search_by_key_4 (fs, &key, &path) == ITEM_NOT_FOUND) {
	    /*sware on check, delete on fix-fixable*/
	    if (fsck_mode(fs) == FSCK_CHECK) {
		fsck_log ("invalid safe link, cannot find a pointed object (%K)\n", &key);
		one_more_corruption (fs, fixable);
	    } else if (fsck_mode(fs) == FSCK_FIX_FIXABLE) {
		fsck_log ("invalid safe link, cannot find a pointed object (%K) - delete\n", &key);
		*(__u32 *)get_item(&safe_link_path) = (__u32)0;
		pathrelse (&path);
		reiserfsck_delete_item (&safe_link_path, 0);		
		continue;
	    }
	} else if (get_offset(&tmp_ih->ih_key) == 0x1) {
	    if (!not_a_directory (get_item(&path))) {
		/*truncate on directory should not happen*/
		/*sware on check, delete on fix-fixable*/
		if (fsck_mode(fs) == FSCK_CHECK) {
		    fsck_log ("invalid truncate safe link, cannot happen for directory (%K)\n", &key);
		    one_more_corruption (fs, fixable);
		} else if (fsck_mode(fs) == FSCK_FIX_FIXABLE) {
		    fsck_log ("invalid truncate safe link, cannot happen for directory (%K) - delete\n", &key);
		    *(__u32 *)get_item(&safe_link_path) = (__u32)0;
		    pathrelse (&path);
		    reiserfsck_delete_item (&safe_link_path, 0);		
		    continue;
		}
	    }
	}
	pathrelse (&path);
	pathrelse (&safe_link_path);
    }

    return OK;
}

/* called when --check is given */
void semantic_check (void)
{
    if (fsck_data (fs)->check.bad_nodes) {
        fsck_progress ("Bad nodes were found, Semantic pass skipped\n");
        return;
    }

    if (fsck_data (fs)->check.fatal_corruptions) {
        fsck_progress ("Fatal corruptions were found, Semantic pass skipped\n");
        return;
    }

    
    fsck_progress ("Checking Semantic tree...\n");

    if (fsck_mode(fs) == FSCK_FIX_FIXABLE) {
        /*create new_bitmap, and initialize new_bitmap & allocable bitmap*/
        fsck_new_bitmap (fs) = reiserfs_create_bitmap (get_sb_block_count (fs->fs_ondisk_sb));
        reiserfs_bitmap_copy (fsck_new_bitmap (fs), fs->fs_bitmap2);
        fsck_allocable_bitmap (fs) = reiserfs_create_bitmap (get_sb_block_count (fs->fs_ondisk_sb));
        reiserfs_bitmap_copy (fsck_allocable_bitmap (fs), fs->fs_bitmap2);
	fs->block_allocator = reiserfsck_reiserfs_new_blocknrs;
	fs->block_deallocator = reiserfsck_reiserfs_free_block;
    }
    
    if (check_semantic_pass (&root_dir_key, &parent_root_dir_key, 0, 0) != OK) {
        fsck_log ("check_semantic_tree: no root directory found");
        one_more_corruption (fs, fatal);
    }

    check_safe_links ();

    if (fsck_mode(fs) == FSCK_FIX_FIXABLE) {
        reiserfs_delete_bitmap (fs->fs_bitmap2);
        fs->fs_bitmap2 = fsck_new_bitmap (fs);
        reiserfs_delete_bitmap (fsck_allocable_bitmap (fs));
        fsck_allocable_bitmap (fs) = NULL;
        set_sb_free_blocks (fs->fs_ondisk_sb, reiserfs_bitmap_zeros (fs->fs_bitmap2));
        mark_buffer_dirty (fs->fs_super_bh);
	add_badblock_list(fs, 0);
    }
    
    fsck_progress ("ok\n");
}
