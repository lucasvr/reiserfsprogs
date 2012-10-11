/*
 * Copyright 2000-2004 by Hans Reiser, licensing governed by 
 * reiserfsprogs/README
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "fsck.h"
#include "util/misc.h"
#include "util/device.h"
#include "util/misc.h"

#include <sys/stat.h>


extern void cb_item_modify (reiserfs_ih_t *ih, void *item);

static __u64 _look_for_lost (reiserfs_filsys_t * fs, int link_lost_dirs) {
    reiserfs_key_t key, prev_key;
    const reiserfs_key_t *rdkey;
    REISERFS_PATH_INIT (path);
    static int lost_files = 0; /* looking for lost dirs we calculate amount of
				  lost files, so that when we will look for
				  lost files we will be able to stop when
				  there are no lost files anymore */
    unsigned long leaves;
    reiserfs_bh_t * bh;
    reiserfs_ih_t * ih;
    int entry_len;
    int is_it_dir;
    int item_pos;
    int retval;
    __u64 size;

    key = root_dir_key;

    if (!link_lost_dirs && !lost_files) {
	/* we have to look for lost files but we know already that there are
           no any */
	return 0;
    }
	
    fsck_progress ("Looking for lost %s:\n", link_lost_dirs ? 
		   "directories" : "files");
    leaves = 0;

    /* total size of added entries */
    size = 0;
    while (1) {
	retval = reiserfs_tree_search_item (fs, &key, &path);
	/* fixme: we assume path ends up with a leaf */
	bh = REISERFS_PATH_LEAF (&path);
	item_pos = REISERFS_PATH_LEAF_POS (&path);
	if (retval != ITEM_FOUND) {
	    if (item_pos == reiserfs_nh_get_items (NODE_HEAD (bh))) {
		rdkey = reiserfs_tree_rkey (&path, fs);
		if (!reiserfs_key_comp (rdkey, &MAX_KEY)) {
		    reiserfs_tree_pathrelse (&path);
		    break;
		}
		key = *rdkey;
		reiserfs_tree_pathrelse (&path);
		continue;
	    }
	    /* we are on the item in the buffer */
	}

	/* print ~ how many leaves were scanned and how fast it was */
	if (!fsck_quiet (fs))
	    util_misc_speed (fsck_progress_file(fs), leaves++, 0, 50, 0);

	for (ih = REISERFS_PATH_IH (&path); 
	     item_pos < reiserfs_nh_get_items (NODE_HEAD (bh)); 
	     item_pos ++, ih ++, REISERFS_PATH_LEAF_POS(&path)++) 
	{
	    if (fsck_item_reach(ih))
		continue;

	    /* found item which can not be reached */
	    if (!reiserfs_ih_dir (ih) && !reiserfs_ih_stat (ih)) {
		continue;
	    }

	    if (reiserfs_ih_dir (ih)) {
		/* if this directory has no stat data - try to recover it */
		reiserfs_key_t sd;
		reiserfs_path_t tmp;

		sd = ih->ih_key;
		reiserfs_key_set_sec (KEY_FORMAT_1, &sd, 
				      OFFSET_SD, TYPE_STAT_DATA);
		if (reiserfs_tree_search_item (fs, &sd, &tmp) == ITEM_FOUND) {
		    /* should not happen - because if there were a stat data -
                       we would have done with the whole directory */
		    reiserfs_tree_pathrelse (&tmp);
		    continue;
		}
		sem_pass_stat(fs)->added_sd ++;
		reiserfs_tree_create_stat (fs, &tmp, &sd, cb_item_modify);
                id_map_mark(proper_id_map (fs), reiserfs_key_get_oid (&sd));
		key = sd;
		reiserfs_tree_pathrelse (&path);
		goto cont;
	    }


	    /* stat data marked "not having name" found */
	    if (reiserfs_ih_stat (REISERFS_PATH_IH(&path)))
		fix_obviously_wrong_sd_mode (&path);
		
    	    is_it_dir = (not_a_directory(reiserfs_item_by_ih(bh,ih))) ? 0 : 1;

	    if (is_it_dir) {
		reiserfs_key_t tmp_key;
		REISERFS_PATH_INIT (tmp_path);
		reiserfs_ih_t * tmp_ih;
		reiserfs_bh_t *tmp_bh;

		/* there is no need to link empty lost dirs into /lost+found */
		tmp_key = ih->ih_key;
		reiserfs_key_set_sec (KEY_FORMAT_1, &tmp_key, 
				      0xffffffff, TYPE_DIRENTRY);
		reiserfs_tree_search_item (fs, &tmp_key, &tmp_path);
		tmp_ih = REISERFS_PATH_IH (&tmp_path);
		tmp_bh = REISERFS_PATH_LEAF (&tmp_path);
		tmp_ih --;
		if (reiserfs_key_comp2 (&tmp_key, tmp_ih))
		    reiserfs_panic ("not directory found");
		if (!reiserfs_ih_dir (tmp_ih) ||
		    (reiserfs_deh_get_off (reiserfs_deh (tmp_bh, tmp_ih) + 
					   reiserfs_ih_get_entries (tmp_ih) - 1)
		     == OFFSET_DOT_DOT))
		{
		    /* last directory item is either stat data or empty
                       directory item - do not link this dir into lost+found */
		    sem_pass_stat(fs)->empty_lost_dirs ++;
		    reiserfs_tree_pathrelse (&tmp_path);
		    continue;
		}
		reiserfs_tree_pathrelse (&tmp_path);
	    }

	    if (link_lost_dirs && !is_it_dir) {
		/* we are looking for directories and it is not a dir */
		lost_files ++;
		continue;
	    }

	    sem_pass_stat(fs)->lost_found ++;

	    {
		reiserfs_key_t obj_key = {0, 0, {{0, 0},}};
		char lost_name[REISERFS_NAME_MAX];
		reiserfs_ih_t tmp_ih;

		/* key to continue */
		key = ih->ih_key;
		reiserfs_key_set_oid (&key, reiserfs_key_get_oid (&key) + 1);

		tmp_ih = *ih;
		if (id_map_test(semantic_id_map (fs), 
				reiserfs_key_get_oid (&ih->ih_key))) 
		{
		    /* objectid is used, relocate an object */
		    sem_pass_stat(fs)->oid_sharing ++;
		    
		    if (is_it_dir) {
			relocate_dir (&tmp_ih);
			sem_pass_stat(fs)->oid_sharing_dirs_relocated ++;
		    } else {
			fsck_file_relocate (&tmp_ih.ih_key, 1);
			sem_pass_stat(fs)->oid_sharing_files_relocated ++;
		    }		    
		    
		    fsck_relocate_mklinked(&tmp_ih.ih_key);
		} else {
		    if (!is_it_dir)
			id_map_mark(semantic_id_map (fs), 
				    reiserfs_key_get_oid (&ih->ih_key));
		}

		lost_name[0] = '\0';
		sprintf (lost_name, "%u_%u", reiserfs_key_get_did (&tmp_ih.ih_key),
			 reiserfs_key_get_oid (&tmp_ih.ih_key));

		/* entry in lost+found directory will point to this key */
		reiserfs_key_set_did (&obj_key, 
			reiserfs_key_get_did (&tmp_ih.ih_key));
		reiserfs_key_set_oid (&obj_key, 
			reiserfs_key_get_oid (&tmp_ih.ih_key));

		reiserfs_tree_pathrelse (&path);
		
		/* 0 does not mean anyting - item with "." and ".." already
		   exists and reached, so only name will be added */
		entry_len = reiserfs_direntry_entry_estimate (lost_name, 
							      fs->lost_format);
		size += reiserfs_tree_insert_entry (fs, &lost_found_dir_key, 
						    lost_name, entry_len,
						    &obj_key, 0/*fsck_need*/);
		
		if (is_it_dir) {
		    /* fixme: we hope that if we will try to pull all the
		       directory right now - then there will be less
		       lost_found things */
		    if (!fsck_quiet(fs)) {
			util_misc_print_name (fsck_progress_file(fs), 
					      lost_name, strlen (lost_name));
		    }
		    
		    /*fsck_progress ("\tChecking lost dir \"%s\":", lost_name);*/
		    rebuild_semantic_pass (&obj_key, &lost_found_dir_key, 
					   ET_NAME, /*reloc_ih*/0);
		    
		    if (!fsck_quiet(fs)) {
			util_misc_erase_name (fsck_progress_file(fs), 
					      strlen (lost_name));
		    
			util_misc_fini_name(fsck_progress_file(fs));
		    }
		    
		    /*fsck_progress ("finished\n");*/
		    
		    sem_pass_stat(fs)->lost_found_dirs ++;
		} else {
		    if (reiserfs_tree_search_item (fs, &obj_key, &path) != 
			ITEM_FOUND)
		    {
			reiserfs_panic ("look_for_lost: lost file stat data "
					"%K not found", &obj_key);
		    }

		    /* check_regular_file does not mark stat data reachable */
		    fsck_item_mkreach (REISERFS_PATH_IH (&path), 
				       REISERFS_PATH_LEAF (&path));

		    rebuild_check_regular_file (&path, REISERFS_PATH_ITEM(&path), 
						0/*reloc_ih*/);
		    
		    reiserfs_tree_pathrelse (&path);

		    sem_pass_stat(fs)->lost_found_files ++;
		    lost_files --;
		}

		goto cont;
	    }
	} /* for */

	prev_key = key;
	
	REISERFS_PATH_LEAF_POS(&path) = item_pos - 1;
	rdkey = reiserfs_tree_next_key (&path, fs);
	if (rdkey)
	    key = *rdkey;
	else
	    break;
	    	
	if (reiserfs_key_comp (&prev_key, &key) != -1)
	    reiserfs_panic ("pass_3a: key must grow 2: prev=%k next=%k",
			    &prev_key, &key);
	reiserfs_tree_pathrelse (&path);

    cont:
	if (!link_lost_dirs && !lost_files) {
	    break;
	}
    }

    reiserfs_tree_pathrelse (&path);
    util_misc_speed(fsck_progress_file(fs), leaves, 0, 50, 1);
    
#if 0
    /* check names added we just have added to/lost+found. Those names are
       marked DEH_Lost_found flag */
    fsck_progress ("Checking lost+found directory.."); fflush (stdout);
    check_semantic_tree (&lost_found_dir_key, &root_dir_key, 0, 1/* lost+found*/);
    fsck_progress ("finished\n");
#endif

    return size;

}

static void fsck_lost_save_result (reiserfs_filsys_t * fs)
{
    FILE * file;
    int retval;

    /* save bitmaps with which we will be able start reiserfs from
       pass 1 */
    file = util_file_open ("temp_fsck_file.deleteme", "w+");
    if (!file)
	return;

    fsck_stage_start_put (file, LOST_FOUND_DONE);
    fsck_stage_end_put (file);
    fclose (file);

    retval = rename ("temp_fsck_file.deleteme", state_dump_file (fs));
    if (retval != 0) {
	fsck_progress ("pass 0: Could not rename the temporary file "
		       "temp_fsck_file.deleteme to %s", state_dump_file (fs));
    }
}

/* we have nothing to load from a state file, but we have to fetch
   on-disk bitmap, copy it to allocable bitmap, and fetch objectid
   map */
void fsck_lost_load_result (reiserfs_filsys_t * fs) {
    fsck_new_bitmap (fs) = 
	    reiserfs_bitmap_create (reiserfs_sb_get_blocks (fs->fs_ondisk_sb));
    
    reiserfs_bitmap_copy (fsck_new_bitmap (fs), fs->fs_bitmap2);

    fsck_allocable_bitmap (fs) = 
	    reiserfs_bitmap_create (reiserfs_sb_get_blocks (fs->fs_ondisk_sb));
    
    reiserfs_bitmap_copy (fsck_allocable_bitmap (fs), fs->fs_bitmap2);

    fs->block_allocator = reiserfsck_new_blocknrs;
    fs->block_deallocator = reiserfsck_free_block;
}

static void fsck_lost_fini(reiserfs_filsys_t * fs) {
    /* update super block: objectid map, fsck state */
    reiserfs_sb_set_state (fs->fs_ondisk_sb, LOST_FOUND_DONE);
    reiserfs_buffer_mkdirty (fs->fs_super_bh);

    /* write all dirty blocks */
    fsck_progress ("Flushing..");
    id_map_flush(semantic_id_map (fs), fs);
    id_map_free(semantic_id_map(fs));
    semantic_id_map(fs) = NULL;
 
    fs->fs_dirt = 1;
    reiserfs_bitmap_flush(fsck_new_bitmap(fs), fs);
    reiserfs_fs_flush (fs);
    fsck_progress ("finished\n");

    fsck_stage_report (FS_LOST, fs);
    
    id_map_free(proper_id_map(fs));
    proper_id_map(fs) = NULL;

    if (!fsck_run_one_step (fs)) {
	if (fsck_info_ask (fs, "Continue? (Yes):", "Yes\n", 1))
	    /* reiserfsck continues */
	    return;
    }

    fsck_lost_save_result (fs);

    fs->fs_dirt = 1;
    reiserfs_fs_close (fs);
    exit(EXIT_OK);
}

void fsck_lost (reiserfs_filsys_t * fs) {
    REISERFS_PATH_INIT (path);
    reiserfs_ih_t * ih;
    void * sd;
    __u64 size, sd_size;
    __u32 blocks;
    __u16 mode;
    __u32 objectid;
    unsigned int gen_counter;
    fsck_progress ("Pass 3a (looking for lost dir/files):\n");

    /* when warnings go not to stderr - separate them in the log */
    if (fsck_log_file (fs) != stderr)
	fsck_log ("####### Pass 3a (lost+found pass) #########\n");


    /* look for lost dirs first */
    size = _look_for_lost (fs, 1);

    /* link files which are still lost */
    size += _look_for_lost (fs, 0);

    /* update /lost+found sd_size and sd_blocks (nlink is correct already) */

    objectid = reiserfs_tree_search_name (fs, &root_dir_key, "lost+found",
                                       &gen_counter, &lost_found_dir_key);

    if (!objectid) {
       reiserfs_panic ("look_for_lost: The entry 'lost+found' could "
		       "not be found in the root directory.");
    }

    if (reiserfs_tree_search_item (fs, &lost_found_dir_key, &path) != 
	ITEM_FOUND)
    {
	reiserfs_panic ("look_for_lost: The StatData of the 'lost+found' "
			"directory %K could not be found", &lost_found_dir_key);
    }
    
    ih = REISERFS_PATH_IH (&path);
    sd = REISERFS_PATH_ITEM (&path);
    reiserfs_stat_get_size (ih, sd, &sd_size);
    size += sd_size;
    blocks = REISERFS_DIR_BLOCKS (size);

    reiserfs_stat_set_size (ih, sd, &size);
    reiserfs_stat_set_blocks (ih, sd, &blocks);

    /* make lost+found to be drwx------ */
    mode = S_IFDIR | S_IRUSR | S_IWUSR | S_IXUSR;
    reiserfs_stat_set_mode (ih, sd, &mode);

    reiserfs_buffer_mkdirty (REISERFS_PATH_LEAF (&path));
    reiserfs_tree_pathrelse (&path);

    fsck_lost_fini(fs);
}

