/*
 * Copyright 1996-2002 Hans Reiser
 */

#include "fsck.h"


/* on pass2 we take leaves which could not be inserted into tree
   during pass1 and insert each item separately. It is possible that
   items of different objects with the same key can be found. We treat
   that in the following way: we put it into tree with new key and
   link it into /lost+found directory with name made of dir,oid. When
   coming item is a directory - we delete object from the tree, put it
   back with different key, link it to /lost+found directory and
   insert directory as it is */

/* relocation rules: we have an item (it is taken from "non-insertable"
   leaf). It has original key yet. We check to see if object with this
   key is remapped. Object can be only remapped if it is not a piece
   of directory */


/* in list of this structures we store what has been
   relocated. */
struct relocated {
    unsigned long old_dir_id;
    unsigned long old_objectid;
    unsigned long new_objectid;
    /*mode_t mode;*/
    struct relocated * next;
};


/* all relocated files will be linked into lost+found directory at the
   beginning of semantic pass */
struct relocated * relocated_list;


/* return objectid the object has to be remapped with */
__u32 objectid_for_relocation (struct key * key)
{
    struct relocated * cur;

    cur = relocated_list;

    while (cur) {
	if (cur->old_dir_id == get_key_dirid (key) &&
	    cur->old_objectid == get_key_objectid (key))
	    /* object is relocated already */
	    return cur->new_objectid;
	cur = cur->next;
    }

    cur = getmem (sizeof (struct relocated));
    cur->old_dir_id = get_key_dirid (key);
    cur->old_objectid = get_key_objectid (key);
    cur->new_objectid = get_unused_objectid (fs);
    cur->next = relocated_list;
    relocated_list = cur;
    fsck_log ("relocation: (%K) is relocated to (%lu, %lu)\n",
	      key, get_key_dirid (key), cur->new_objectid);
    return cur->new_objectid;
}


/* this item is in tree. All unformatted pointer are correct. Do not
   check them */
static void save_item_2 (struct si ** head, struct item_head * ih, 
			 char * item, __u32 blocknr)
{
    struct si * si, * cur;

    si = getmem (sizeof (*si));
    si->si_dnm_data = getmem (get_ih_item_len(ih));
    /*si->si_blocknr = blocknr;*/
    memcpy (&(si->si_ih), ih, IH_SIZE);
    memcpy (si->si_dnm_data, item, get_ih_item_len(ih));

    if (*head == 0)
	*head = si;
    else {
	cur = *head;
	while (cur->si_next)
	    cur = cur->si_next;
	cur->si_next = si;
    }
    return;
}


struct si * save_and_delete_file_item (struct si * si, struct path * path)
{
    struct buffer_head * bh = PATH_PLAST_BUFFER (path);
    struct item_head * ih = PATH_PITEM_HEAD (path);

    save_item_2 (&si, ih, B_I_PITEM (bh, ih), bh->b_blocknr);

    /* delete item temporary - do not free unformatted nodes */
    reiserfsck_delete_item (path, 1/*temporary*/);
    return si;
}


/* check whether there are any directory items with this key */
int should_relocate (struct item_head * ih)
{
    struct key key;
    struct key * rkey;
    struct path path;
    struct item_head * path_ih;


    /* starting with the leftmost item with this key */
    key = ih->ih_key;
    set_type_and_offset (KEY_FORMAT_1, &key, SD_OFFSET, TYPE_STAT_DATA);

    while (1) {
	reiserfs_search_by_key_4 (fs, &key, &path);
	
	if (get_item_pos (&path) == B_NR_ITEMS (get_bh (&path))) {
	    rkey = uget_rkey (&path);
	    if (rkey && !not_of_one_file (&key, rkey)) {
		/* file continues in the right neighbor */
		key = *rkey;
		pathrelse (&path);
		continue;
	    }
	    /* there is no more items with this key */
	    pathrelse (&path);
	    break;
	}

	if (is_stat_data_ih (get_ih(&path)))
	    fix_obviously_wrong_sd_mode (&path);
	
	path_ih = get_ih (&path);
	if (not_of_one_file (&key, &(path_ih->ih_key))) {
	    /* there are no more item with this key */
	    pathrelse (&path);
	    break;
	}

	/* ok, item found, but make sure that it is not a directory one */
	if ((is_stat_data_ih (path_ih) && !not_a_directory (get_item (&path))) ||
	    (is_direntry_ih (path_ih))) {
	    /* item of directory found. so, we have to relocate the file */
	    pathrelse (&path);
	    return 1;
	}
	key = path_ih->ih_key;
	set_offset (KEY_FORMAT_1, &key, get_offset (&key) + 1);
	pathrelse (&path);
    }
    return 0;
}


/* delete all items (but directory ones) with the same key 'ih' has
   (including stat data of not a directory) and put them back at the
   other place */
void relocate_file (struct item_head * ih, int change_ih)
{
    struct key key;
    struct key * rkey;
    struct path path;
    struct item_head * path_ih;
    struct si * si;
    __u32 new_objectid;


    /* starting with the leftmost one - look for all items of file,
       store them and delete */
    key = ih->ih_key;
    set_type_and_offset (KEY_FORMAT_1, &key, SD_OFFSET, TYPE_STAT_DATA);

    si = 0;
    while (1) {
	reiserfs_search_by_key_4 (fs, &key, &path);
	
	if (get_item_pos (&path) == B_NR_ITEMS (get_bh (&path))) {
	    rkey = uget_rkey (&path);
	    if (rkey && !not_of_one_file (&key, rkey)) {
		/* file continues in the right neighbor */
		key = *rkey;
		pathrelse (&path);
		continue;
	    }
	    /* there is no more items with this key */
	    pathrelse (&path);
	    break;
	}

	if (is_stat_data_ih (get_ih(&path)))
	    fix_obviously_wrong_sd_mode (&path);	
	
	path_ih = get_ih (&path);
	if (not_of_one_file (&key, &(path_ih->ih_key))) {
	    /* there are no more item with this key */
	    pathrelse (&path);
	    break;
	}

	/* ok, item found, but make sure that it is not a directory one */
	if ((is_stat_data_ih (path_ih) && !not_a_directory (get_item (&path))) ||
	    (is_direntry_ih (path_ih))) {
	    /* item of directory found. Leave it in the tree */
	    key = path_ih->ih_key;
	    set_offset (KEY_FORMAT_1, &key, get_offset (&key) + 1);
	    pathrelse (&path);
	    continue;
	}

	si = save_and_delete_file_item (si, &path);
    }


    if (si || change_ih) {
	int moved_items;
	struct key old, new;

	/* get new objectid for relocation or get objectid with which file
	   was relocated already */
	new_objectid = objectid_for_relocation (&ih->ih_key);
	if (change_ih)
	    set_key_objectid (&ih->ih_key, new_objectid);

	moved_items = 0;
		
	/* put all items removed back into tree */
	while (si) {
	    /*fsck_log ("relocate_file: move %H to ", &si->si_ih);*/
	    old = si->si_ih.ih_key;
	    set_key_objectid (&(si->si_ih.ih_key), new_objectid);
	    new = si->si_ih.ih_key;
	    /*fsck_log ("%H\n", &si->si_ih);*/
	    insert_item_separately (&(si->si_ih), si->si_dnm_data, 1/*was in tree*/);
	    si = remove_saved_item (si);
	    moved_items ++;
	}
	if (moved_items)
	    fsck_log ("relocate_file: %d items of file %K moved to %K\n",
		      moved_items, &old, &new);
    }
}


/* this works for both new and old stat data */
#define st_mode(sd) le16_to_cpu(((struct stat_data *)(sd))->sd_mode)

#define st_mtime_v1(sd) le32_to_cpu(((struct stat_data_v1 *)(sd))->sd_mtime)
#define st_mtime_v2(sd) le32_to_cpu(((struct stat_data *)(sd))->sd_mtime)


/* either both sd-s are new of both are old */
static void overwrite_stat_data (struct item_head * new_ih,
				 void * new_item, struct path * path)
{
    __u16 new_mode, old_mode;

    get_sd_mode (new_ih, new_item, &new_mode);
    get_sd_mode (get_ih (path), get_item (path), &old_mode);


    if (S_ISREG (new_mode) && !S_ISREG (old_mode)) {
	/* in tree we have not regular file - overwrite its stat data
           with stat data of regular file */
	memcpy (get_item (path), new_item, get_ih_item_len (get_ih (path)));
	mark_buffer_dirty (get_bh (path));
	return;
    }

    if (S_ISREG (old_mode) && !S_ISREG (new_mode)) {
	/* new stat data is not a stat data of regular file, keep
           regular file stat data in tree */
	return;
    }
    
    /* if coming stat data has newer mtime - use that */
    if (stat_data_v1 (new_ih)) {	
	if (st_mtime_v1 (new_item) > st_mtime_v1 (get_item (path))) {
	    memcpy (get_item (path), new_item, SD_V1_SIZE);
	    mark_buffer_dirty (get_bh (path));
	}
    } else {
	if (st_mtime_v2 (new_item) > st_mtime_v2 (get_item (path))) {
	    memcpy (get_item (path), new_item, SD_SIZE);
	    mark_buffer_dirty (get_bh (path));
	}
    }
    return;
}


/* insert sd item if it does not exist, overwrite it otherwise */
static void put_sd_into_tree (struct item_head * new_ih, char * new_item)
{
    struct path path;

    if (!not_a_directory (new_item)) {
	/* new item is a stat data of a directory. So we have to
           relocate all items which have the same short key and are of
           not a directory */
	relocate_file (new_ih, 0/*do not change new_ih*/);
    } else {
	/* new item is a stat data of something else but directory. If
           there are items of directory - we have to relocate the file */
	if (should_relocate (new_ih))
	    relocate_file (new_ih, 1/*change new_ih*/);
    }
    
    /* if we will have to insert item into tree - it is ready */
    zero_nlink (new_ih, new_item);
    mark_item_unreachable (new_ih);
    
    /* we are sure now that if we are inserting stat data of a
       directory - there are no items with the same key which are not
       items of a directory, and that if we are inserting stat data is
       of not a directory - it either has new key already or there are
       no items with this key which are items of a directory */
    if (reiserfs_search_by_key_4 (fs, &(new_ih->ih_key), &path) == ITEM_FOUND) {
	/* this stat data is found */
        if (get_ih_key_format (get_ih(&path)) != get_ih_key_format (new_ih)) {
	    /* in tree stat data and a new one are of different
               formats */
	    fsck_log ("put_sd_into_tree: inserting stat data %K (%M)..",
		      &(new_ih->ih_key), st_mode (new_item));
	    if (stat_data_v1 (new_ih)) {
		/* sd to be inserted is of V1, where as sd in the tree
                   is of V2 */
		fsck_log ("found newer in the tree (%M), skip inserting\n",
			  st_mode (get_item (&path)));
	    	pathrelse (&path);
	    } else {
		/* the stat data in the tree is sd_v1 */
		fsck_log ("older sd (%M) is replaced with it\n",
			  st_mode (get_item (&path)));
		reiserfsck_delete_item (&path, 0/*not temporary*/);
		
		reiserfs_search_by_key_4 (fs, &new_ih->ih_key, &path);
		reiserfsck_insert_item (&path, new_ih, new_item);
	    }
	} else {
	    /* both stat data are of the same version */
	    overwrite_stat_data (new_ih, new_item, &path);
	    pathrelse (&path);
	}
	return;
    }
    
    /* item not found, insert a new one */
    reiserfsck_insert_item (&path, new_ih, new_item);
}


/* this tries to put each item entry to the tree, if there is no items
   of the directory, insert item containing 1 entry */
static void put_directory_item_into_tree (struct item_head * comingih, char * item)
{
    struct reiserfs_de_head * deh;
    int i;
    char * buf;
    char * name;
    int namelen;

    /* if there are anything with this key but a directory - move it
       somewhere else */
    relocate_file (comingih, 0/* do not change ih */);

    deh = (struct reiserfs_de_head *)item;

    for (i = 0; i < get_ih_entry_count (comingih); i ++, deh ++) {
	name = name_in_entry (deh, i);
	namelen = name_in_entry_length (comingih, deh, i);

	if (!is_properly_hashed (fs, name, namelen, get_deh_offset (deh)))
	    reiserfs_panic ("put_directory_item_into_tree: should be hashed properly (%k)", &comingih->ih_key);

	asprintf (&buf, "%.*s", namelen, name);
	/* 1 for fsck is important: if there is no any items of this
           directory in the tree yet - new item will be inserted
           marked not reached */
	reiserfs_add_entry (fs, &(comingih->ih_key), buf, entry_length (comingih, deh, i),
			(struct key *)&(deh->deh2_dir_id), 1 << IH_Unreachable);
	free (buf);
    }
}


/* relocated files get added into lost+found with slightly different names */
static void link_one (struct relocated * file)
{
    char * name;
    struct key obj_key;

    asprintf (&name, "%lu,%lu", file->old_dir_id, file->new_objectid);
    set_key_dirid (&obj_key, file->old_dir_id);
    set_key_objectid (&obj_key, file->new_objectid);


    /* 0 for fsck_need does not mean too much - it would make effect
       if there were no this directory yet. But /lost_found is there
       already */
    reiserfs_add_entry (fs, &lost_found_dir_key, name, name_length (name, lost_found_dir_format),
    		&obj_key, 0/*fsck_need*/);
    pass_2_stat (fs)->relocated ++;
    free (name);
}


void link_relocated_files (void)
{
    struct relocated * tmp;
    int count;
    
    count = 0;
    while (relocated_list) {
	link_one (relocated_list);
	tmp = relocated_list;
	relocated_list = relocated_list->next;
	freemem (tmp);
	count ++;
    }
}


void insert_item_separately (struct item_head * ih,
			     char * item, int was_in_tree)
{
    if (get_key_dirid (&ih->ih_key) == get_key_objectid (&ih->ih_key))
	reiserfs_panic ("insert_item_separately: can not insert bad item %H", ih);
    
    if (is_stat_data_ih (ih)) {
	put_sd_into_tree (ih, item);
    } else if (is_direntry_ih (ih)) {
	put_directory_item_into_tree (ih, item);
    } else {
	if (!was_in_tree && should_relocate (ih))
	    relocate_file (ih, 1/*change new_ih*/);
	
	reiserfsck_file_write (ih, item, was_in_tree);
    }
}


static void put_stat_data_items (struct buffer_head * bh) 
{
    int i;
    struct item_head * ih;


    ih = B_N_PITEM_HEAD (bh, 0);
    for (i = 0; i < B_NR_ITEMS (bh); i ++, ih ++) {

        /* this check instead of saved_items */
        if (!is_stat_data_ih (ih) || is_bad_item (bh, ih, B_I_PITEM (bh, ih))) {
	    continue;
        }
	insert_item_separately (ih, B_I_PITEM (bh, ih), 0/*was in tree*/);
    }
}

static void put_not_stat_data_items (struct buffer_head * bh)
{
    int i;
    struct item_head * ih;

    ih = B_N_PITEM_HEAD (bh, 0);
    for (i = 0; i < B_NR_ITEMS (bh); i ++, ih ++) {

        if (is_stat_data_ih (ih) || is_bad_item (bh, ih, B_I_PITEM (bh, ih))) {
	    continue;
	}
	insert_item_separately (ih, B_I_PITEM (bh, ih), 0/*was in tree*/);
    }
}


static void before_pass_2 (reiserfs_filsys_t * fs)
{
    /* anything specific for pass 2 ? */
}


static void save_pass_2_result (reiserfs_filsys_t * fs)
{
    FILE * file;
    int retval;

    file = open_file("temp_fsck_file.deleteme", "w+");
    if (!file)
	return;
    
    /* to be able to restart from semantic we do not need to save
       anything here, but two magic values */
    reiserfs_begin_stage_info_save(file, TREE_IS_BUILT);
    reiserfs_end_stage_info_save (file);
    close_file (file);
    retval = unlink (state_dump_file (fs));
    retval = rename ("temp_fsck_file.deleteme", state_dump_file (fs));
    if (retval != 0)
	fsck_progress ("pass 2: could not rename temp file temp_fsck_file.deleteme to %s",
		       state_dump_file (fs));
}


/* we have nothing to load from a state file, but we have to fetch
   on-disk bitmap, copy it to allocable bitmap, and fetch objectid
   map */
void load_pass_2_result (reiserfs_filsys_t * fs)
{
    fsck_new_bitmap (fs) = reiserfs_create_bitmap (get_sb_block_count (fs->fs_ondisk_sb));
    reiserfs_bitmap_copy (fsck_new_bitmap (fs), fs->fs_bitmap2);
    
    fsck_allocable_bitmap (fs) = reiserfs_create_bitmap (get_sb_block_count (fs->fs_ondisk_sb));
    reiserfs_bitmap_copy (fsck_allocable_bitmap (fs), fs->fs_bitmap2);

    fs->block_allocator = reiserfsck_reiserfs_new_blocknrs;
    fs->block_deallocator = reiserfsck_reiserfs_free_block;

    /* we need objectid map on semantic pass to be able to relocate files */
    proper_id_map (fs) = init_id_map ();
    fetch_objectid_map (proper_id_map (fs), fs);    
}

    
/* uninsertable blocks are marked by 0s in uninsertable_leaf_bitmap
   during the pass 1. They must be not in the tree */
static void do_pass_2 (reiserfs_filsys_t * fs) {

    struct buffer_head * bh;
    unsigned long j;
    int i, what_node;
    unsigned long done = 0, total;


    total = reiserfs_bitmap_zeros (fsck_uninsertables (fs)) * 2;
    if (!total)
	return;

    fsck_progress ("\nPass2:\n");

    for (i = 0; i < 2; i++) {
        j = 0;
        while (reiserfs_bitmap_find_zero_bit (fsck_uninsertables (fs), &j) == 0) {
	    bh = bread (fs->fs_dev, j, fs->fs_blocksize);
	    if (bh == 0) {
	        fsck_log ("pass_2_take_bad_blocks_put_into_tree: "
		      "unable to read %lu block on device 0x%x\n",
		      j, fs->fs_dev);
                goto next;
            }
	
            if (is_block_used (bh->b_blocknr) && !(block_of_journal (fs, bh->b_blocknr) &&
					       fsck_data(fs)->rebuild.use_journal_area)) {
	        fsck_log ("pass_2_take_bad_blocks_put_into_tree: "
		     "block %d can not be in tree\n", bh->b_blocknr);
	        goto next;
            }
            /* this must be leaf */
            what_node = who_is_this (bh->b_data, bh->b_size);
	    if (what_node != THE_LEAF) { // || B_IS_KEYS_LEVEL(bh)) {
	        fsck_log ("take_bad_blocks_put_into_tree: buffer (%b %z) must contain leaf\n", bh, bh);
	        goto next;
	    }

	    	
//	    fsck_log ("block %lu is being inserted\n", bh->b_blocknr);
//	    fflush(fsck_log_file (fs));
	
	    if (i) {
                /* insert all not SD items */
                put_not_stat_data_items (bh);
                pass_2_stat (fs)->leaves ++;
                make_allocable (j);
            } else
                /* insert SD items only */
                put_stat_data_items (bh);

            print_how_far (fsck_progress_file (fs), &done, total, 1, fsck_quiet (fs));
        next:
	    brelse (bh);
	    j ++;
        }
    }

    fsck_progress ("\n");
}


static void after_pass_2 (reiserfs_filsys_t * fs)
{
    time_t t;

    /* we can now flush new_bitmap on disk as tree is built and 
       contains all data, which were found on dik at start in 
       used bitmaps */
    reiserfs_bitmap_copy (fs->fs_bitmap2, fsck_new_bitmap (fs));
    
    /* we should copy new_bitmap to allocable bitmap, becuase evth what is used 
       for now (marked as used in new_bitmap) should not be allocablel;
       and what is not in tree for now should be allocable.
       these bitmaps differ because on pass2 we skip those blocks, whose SD's 
       are not in the tree, and therefore indirect items of such bad leaves points 
       to not used and not allocable blocks.       
     */


    /* DEBUG only */
    if (reiserfs_bitmap_compare (fsck_allocable_bitmap (fs), fsck_new_bitmap(fs))) {
        reiserfs_warning (fsck_log_file (fs), "allocable bitmap differs from new bitmap after pass2\n");
	reiserfs_bitmap_copy (fsck_allocable_bitmap(fs), fsck_new_bitmap (fs));
    }

    /* update super block: objectid map, fsck state */
    set_sb_fs_state (fs->fs_ondisk_sb, TREE_IS_BUILT);
    mark_buffer_dirty (fs->fs_super_bh);
  
    /* write all dirty blocks */
    fsck_progress ("Flushing..");
    flush_objectid_map (proper_id_map (fs), fs);
    fs->fs_dirt = 1;
    reiserfs_flush_to_ondisk_bitmap (fs->fs_bitmap2, fs);
    reiserfs_flush (fs);
    fsck_progress ("done\n");
    
    /* fixme: should be optional */
/*    fsck_progress ("Tree is built. Checking it - ");
    reiserfsck_check_pass1 ();
    fsck_progress ("done\n");*/

    stage_report (2, fs);

    /* free what we do not need anymore */
    reiserfs_delete_bitmap (fsck_uninsertables (fs));

    if (!fsck_run_one_step (fs)) {
	if (fsck_user_confirmed (fs, "Continue? (Yes):", "Yes\n", 1))
	    /* reiserfsck continues */
	    return;
    } else
	save_pass_2_result (fs);

    
    free_id_map (proper_id_map (fs));
    proper_id_map (fs) = 0;
    
    reiserfs_delete_bitmap (fsck_new_bitmap (fs));
    reiserfs_delete_bitmap (fsck_allocable_bitmap (fs));
    
    time (&t);
    fsck_progress ("###########\n"
		   "reiserfsck finished pass 2 at %s"
		   "###########\n", ctime (&t));
    fs->fs_dirt = 1;
    reiserfs_close (fs);
    exit (4);
}



void pass_2 (reiserfs_filsys_t * fs)
{
    if (fsck_log_file (fs) != stderr)
	fsck_log ("####### Pass 2 #######\n");
    
    before_pass_2 (fs);
    
    /* take blocks which were not inserted into tree yet and put each
	item separately */
    do_pass_2 (fs);
    
    after_pass_2 (fs);
    
    if (get_sb_root_block (fs->fs_ondisk_sb) == -1)
	die ("\n\nNo reiserfs metadata found");
}


