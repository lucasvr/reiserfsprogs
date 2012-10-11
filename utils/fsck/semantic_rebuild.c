/*
 * Copyright 1996-2004 by Hans Reiser, licensing governed by 
 * reiserfsprogs/README
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "fsck.h"
#include "misc/malloc.h"
#include "misc/device.h"
#include "util/device.h"
#include "util/misc.h"

#include <sys/stat.h>

#define MIN(a, b) (((a)>(b))?(b):(a))

/* *size is "real" file size, sd_size - size from stat data */
int wrong_st_size (const reiserfs_key_t * key, 
		   unsigned long long max_file_size, 
		   int blocksize, 
		   __u64 * size, 
		   __u64 sd_size, int type)
{
    if (sd_size <= max_file_size) {
	if (sd_size == *size)
	    return 0;

	if (type == TYPE_DIRENTRY) {
	    /* directory size must match to the sum of length of its entries */
	    fsck_log ("vpf-10650: The directory %K has the wrong size in the StatData "
		"(%Lu)%s(%Lu)\n", key, sd_size, fsck_mode(fs) == FSCK_CHECK ? 
		", should be " : " - corrected to ", *size);
	    return 1;
	}
	
	if (sd_size > *size) {
	    /* size in stat data can be bigger than size calculated by items */
	    if (fsck_adjust_file_size (fs) || type == TYPE_SYMLINK) {
		/* but it -o is given - fix that */
		fsck_log ("vpf-10660: The file %K has too big size in the StatData "
		    "(%Lu)%s(%Lu)\n", key, sd_size, fsck_mode(fs) == FSCK_CHECK ? 
		    ", should be " : " - corrected to ", *size);
		sem_pass_stat (fs)->fixed_sizes ++;
		return 1;
	    }
	    *size = sd_size;
	    return 0;
	}
	
	if (!(*size % blocksize)) {
	    /* last item is extent */
	    if (((sd_size & ~(blocksize - 1)) == (*size - blocksize)) && 
		sd_size % blocksize) 
	    {
		/* size in stat data is correct */
		*size = sd_size;
		return 0;
	    }
	} else {
	    /* last item is a direct one */
	    if (!(*size % 8)) {
		if (((sd_size & ~7) == (*size - 8)) && sd_size % 8) {
		    /* size in stat data is correct */
		    *size = sd_size;
		    return 0;
		}
	    }
	}
    }

    fsck_log ("vpf-10670: The file %K has the wrong size in the StatData (%Lu)%s(%Lu)\n", key, 
	sd_size, fsck_mode(fs) == FSCK_CHECK ? ", should be " : " - corrected to ", 
	*size);
    sem_pass_stat (fs)->fixed_sizes ++;
    return 1;
}


/* sd_blocks is 32 bit only */
/* old stat data shares sd_block and sd_dev - do not wipe sd_rdev out */
/* we should fix it as following:
|------------------------------------------------------------------|
|                    |        3.6                   |       3.5    |
|---------------------------------------------------|              |
|                    |  blocks |  r_dev|generation  | blocks/r_dev |
|------------------------------------------------------------------|
|   fifo, sockets    |    0    |    generation      |      0       |
|   chr/blk_dev      |    0    |     maj:min        |   maj:min    |
|   file, dir, link  |  blocks |    generation      |   blocks     |
|------------------------------------------------------------------|
*/
int wrong_st_blocks (const reiserfs_key_t * key, 
		     __u32 * blocks, 
		     __u32 sd_blocks, 
		     __u16 mode, 
		     int new_format)
{
    int ret = 0;

    if (S_ISREG (mode) || S_ISLNK (mode) || S_ISDIR (mode)) {
	if ((!S_ISLNK(mode) && *blocks != sd_blocks) ||
	    (S_ISLNK(mode) && *blocks != sd_blocks && (MISC_ROUND_UP(*blocks) != sd_blocks))) {
	    fsck_log ("vpf-10680: The %s %K has the wrong block count in the StatData "
		"(%u)%s(%u)\n", S_ISDIR (mode) ? "directory" : S_ISREG (mode) ? "file" : "link",  key, sd_blocks, 
		fsck_mode(fs) == FSCK_CHECK ? ", should be " : " - corrected to ", *blocks);
	    ret = 1;
	}
    } else if (new_format || (S_ISFIFO (mode) || S_ISSOCK (mode))) {
	if (sd_blocks != 0) {
	    fsck_log ("vpf-10690: The object %K has the wrong block count in the StatData "
		"(%u)%s(%u)\n",	key, sd_blocks, fsck_mode(fs) == FSCK_CHECK ? 
		", should be " : " - corrected to ", 0);
	    *blocks = 0;	
	    ret = 1;
	}
    }

    return ret;
}
/*
int wrong_st_rdev (reiserfs_key_t * key, __u32 * sd_rdev, __u16 mode, int new_format)
{
    int ret = 0;

    if (!new_format)
	return 0;

    if (!S_ISCHR (mode) && !S_ISBLK (mode)) {
	if (*sd_rdev != 0) {
	    fsck_log ("%s %K has wrong sd_rdev %u, has to be 0\n",
		S_ISDIR (mode) ? "dir" : "file", key, *sd_rdev);
	    *sd_rdev = 0;
	    ret = 1;
	}
    }
    return ret;
}
*/
/* only regular files and symlinks may have items but stat
   data. Symlink should have body */
int wrong_mode (reiserfs_key_t * key, __u16 * mode, __u64 real_size, int symlink)
{
    int retval = 0;
    if (S_ISLNK (*mode) && !symlink) {
	fsck_log ("The file %K (%M) is too big to be the symlink%s regfile\n", 
	    key, *mode, fsck_mode(fs) == FSCK_CHECK ? ", should be the" : " - corrected "
	    "to the");
	*mode &= ~S_IFMT;
	*mode |= S_IFREG;
	retval = 1;
    }

    if (misc_device_typec (*mode) != '?') {
	/* mode looks reasonable */
	if (S_ISREG (*mode) || S_ISLNK (*mode))
	    return retval;
	
	/* device, pipe, socket have no items */
	if (!real_size)
	    return retval;
    }
    /* there are items, so change file mode to regular file. Otherwise
       - file bodies do not get deleted */
    if (fsck_mode(fs) == FSCK_CHECK) {
	fsck_log ("The object %K has wrong mode (%M)\n", key, *mode);
    } else {
	fsck_log("The object %K has wrong mode (%M) - corrected to %M\n", 
	    key, *mode, (S_IFREG | 0600));
    }
    *mode = (S_IFREG | 0600);
    return 1;
}


/* key is a key of last file item */
int wrong_fdb (reiserfs_key_t * key, int blocksize, 
	       __u32 * first_direct_byte,
	       __u32 sd_fdb, __u32 size)
{
    if (!size || reiserfs_key_ext (key)) {
	/* there is no direct item */
	*first_direct_byte = REISERFS_SD_NODIRECT;
	if (sd_fdb != REISERFS_SD_NODIRECT) {
	    if (fsck_mode(fs) == FSCK_CHECK) {
		fsck_log ("The file %K: The wrong info about the tail in the StatData, "
		    "first_direct_byte (%d) - should be (%d)\n", key, 
		    sd_fdb, *first_direct_byte);
	    } else {
		fsck_log ("The file %K: The wrong info about the tail in the StatData, "
		    "first_direct_byte (%d) - corrected to (%d)\n", key, 
		    sd_fdb, *first_direct_byte);
	    }
	    return 1;
	}
	return 0;
    }

    /* there is direct item */
    *first_direct_byte = (reiserfs_key_get_off (key) & ~(blocksize - 1)) + 1;
    if (*first_direct_byte != sd_fdb) {
	if (fsck_mode(fs) == FSCK_CHECK) {
	    fsck_log ("The file %K: The wrong info about the tail in the StatData, "
		"first_direct_byte (%d) - should be (%d)\n", key, sd_fdb, 
		*first_direct_byte);
	} else {
	    fsck_log ("The file %K: The wrong info about the tail in the StatData, "
		"first_direct_byte (%d) - corrected to (%d)\n", key, sd_fdb, 
		*first_direct_byte);
	}
	    
	return 1;
    }
    return 0;
}


/* delete all items (but directory ones) with the same key 'ih' has
   (including stat data of not a directory) and put them back at the
   other place */
void relocate_dir (reiserfs_ih_t * ih) {
    const reiserfs_key_t * rkey;
    reiserfs_ih_t * path_ih;
    reiserfs_path_t path;
    __u32 new_objectid;
    reiserfs_key_t key;
    saveitem_t * si;
    int moved;

    /* starting with the leftmost one - look for all items of file,
       store them and delete */
    key = ih->ih_key;
    reiserfs_key_set_sec (KEY_FORMAT_1, &key, OFFSET_SD, TYPE_STAT_DATA);

    si = 0;
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
	    /* there is no more items of a directory */
	    reiserfs_tree_pathrelse (&path);
	    break;
	}

	path_ih = REISERFS_PATH_IH (&path);
	if (reiserfs_key_comp2 (&key, &(path_ih->ih_key))) {
	    /* there are no more item with this key */
	    reiserfs_tree_pathrelse (&path);
	    break;
	}

	/* ok, item found, but make sure that it is not a directory one */
	if ((reiserfs_ih_stat (path_ih) && 
	     not_a_directory (REISERFS_PATH_ITEM (&path))) ||
	    reiserfs_ih_direct (path_ih) || reiserfs_ih_ext (path_ih)) 
	{
	    /* item of not a directory found. Leave it in the
               tree. FIXME: should not happen */
	    key = path_ih->ih_key;
	    reiserfs_key_set_off (KEY_FORMAT_1, &key, 
				  reiserfs_key_get_off (&key) + 1);
	    reiserfs_tree_pathrelse (&path);
	    continue;
	}

	/* directory stat data ro directory item */
	fsck_item_save(&path, &si);
	reiserfs_tree_delete(fs, &path, 1);
    }

    if (!si) {
	fsck_log ("%s: WARNING: No one item of the directory "
		  "%K found\n", __FUNCTION__, &key);
	return;
    }

    /* get new objectid for relocation or get objectid with which file
       was relocated already */
    new_objectid = fsck_relocate_oid (&ih->ih_key);
    moved = 0;

    /* put all items removed back into tree */
    while (si) {
	reiserfs_key_set_oid (&si->si_ih.ih_key, new_objectid);
	
	if (reiserfs_key_get_off (&(si->si_ih.ih_key)) == OFFSET_DOT) {
	    /* fix "." entry to point to a directtory properly */
	    reiserfs_deh_t * deh;

	    deh = (reiserfs_deh_t *)si->si_dnm_data;
	    reiserfs_deh_set_obid (deh, new_objectid);
	}
	
	fsck_tree_insert_item (&(si->si_ih), si->si_dnm_data, 0);
	si = fsck_item_free(si);
	moved++;
    }
    
    if (moved) {
	fsck_log("%s: %d items of dir %K are moved to %u oid\n",
		 __FUNCTION__, moved, &ih->ih_key, new_objectid);
    }
    
    reiserfs_key_set_oid (&ih->ih_key, new_objectid);
}



/* path is path to stat data. If file will be relocated - new_ih will contain
   a key file was relocated with */
int rebuild_check_regular_file (reiserfs_path_t * path, void * sd,
				reiserfs_ih_t * new_ih)
{
    int is_new_file;
//    reiserfs_key_t sd_key;
    __u16 mode;
    __u32 nlink;
    __u64 real_size, saved_size;
    __u32 blocks, saved_blocks;	/* proper values and value in stat data */
    __u32 first_direct_byte, saved_fdb;

    reiserfs_bh_t * bh;
    reiserfs_ih_t * ih, sd_ih;
    int fix_sd;
    int symlnk = 0;
    int retval;

    retval = OK;

    /* stat data of a file */
    ih = REISERFS_PATH_IH (path);
    bh = REISERFS_PATH_LEAF (path);

    if (new_ih) {
	/* this objectid is used already */
	*new_ih = *ih;
	reiserfs_tree_pathrelse (path);
	fsck_file_relocate (&new_ih->ih_key, 1);
	fsck_relocate_mklinked(&new_ih->ih_key);
	sem_pass_stat (fs)->oid_sharing_files_relocated ++;
	retval = RELOCATED;
	if (reiserfs_tree_search_item (fs, &(new_ih->ih_key), path) == ITEM_NOT_FOUND)
	    reiserfs_panic ("%s: Could not find the StatData of the relocated file %k", 
		__FUNCTION__, &(new_ih->ih_key));
	/* stat data is marked unreachable again due to relocation, fix that */
	ih = REISERFS_PATH_IH (path);
	bh = REISERFS_PATH_LEAF (path);
	fsck_item_mkreach (ih, bh);
	sd = REISERFS_PATH_ITEM (path);
	
    }
	
    id_map_mark(semantic_id_map(fs), reiserfs_key_get_oid (&ih->ih_key));

    /* check and set nlink first */
    reiserfs_stat_get_nlink (ih, sd, &nlink);
    nlink ++;
    reiserfs_stat_set_nlink (ih, sd, &nlink);
    reiserfs_buffer_mkdirty (bh);

    if (nlink > 1)
	return retval;

    /* firts name of a file found */
    if (reiserfs_ih_get_len (ih) == REISERFS_SD_SIZE)
	is_new_file = 1;
    else
	is_new_file = 0;

    reiserfs_stat_get_mode (ih, sd, &mode);
    reiserfs_stat_get_size (ih, sd, &saved_size);
    reiserfs_stat_get_blocks (ih, sd, &saved_blocks);
    if (!is_new_file)
	reiserfs_stat_get_fdb (ih, sd, &saved_fdb);
    
    /* we met this file first time */
    if (S_ISREG (mode)) {
	sem_pass_stat(fs)->regular_files ++;
    } else if (S_ISLNK (mode)) {
	symlnk = 1;
	sem_pass_stat (fs)->symlinks ++;
    } else {
	sem_pass_stat (fs)->others ++;
    }


    sd_ih = *ih;
//    sd_key = sd_ih.ih_key;
    reiserfs_tree_pathrelse (path);
    
    if (are_file_items_correct (&sd_ih, sd, &real_size, 
				&blocks, 1/*mark reachable*/, 
				&symlnk) != 1)
    {
	/* unpassed items will be deleted in pass 4 as they left unaccessed */
	fsck_log("%s: some items of %K are left unaccessed.\n", 
		 __FUNCTION__, &sd_ih.ih_key);
	sem_pass_stat (fs)->broken_files ++;
    }
    
    fix_sd = 0;
    
    fix_sd += wrong_mode (/*&sd_key, */ &sd_ih.ih_key, &mode, real_size, symlnk);

    if (!is_new_file)
	fix_sd += wrong_fdb (&sd_ih.ih_key, fs->fs_blocksize,
	    &first_direct_byte, saved_fdb, real_size);

    fix_sd += wrong_st_size (/*&sd_key,*/ &sd_ih.ih_key, 
	is_new_file ? REISERFS_SD_SIZE_MAX_V2 : REISERFS_SD_SIZE_MAX_V1,
	fs->fs_blocksize, &real_size, saved_size, symlnk ? TYPE_SYMLINK : 0);

    fix_sd += wrong_st_blocks (&sd_ih.ih_key, &blocks, saved_blocks, mode, is_new_file);

    if (fix_sd) {
	/* find stat data and correct it */
	reiserfs_key_set_sec (KEY_FORMAT_1, &sd_ih.ih_key, 
			      OFFSET_SD, TYPE_STAT_DATA);
	if (reiserfs_tree_search_item (fs, &sd_ih.ih_key, path) != ITEM_FOUND)
	    reiserfs_panic ("%s: The StatData of the file %k could not be found", 
		__FUNCTION__, &sd_ih.ih_key);
	
	bh = REISERFS_PATH_LEAF (path);
	ih = REISERFS_PATH_IH (path);
	sd = REISERFS_PATH_ITEM (path);
	reiserfs_stat_set_size (ih, sd, &real_size);
	reiserfs_stat_set_blocks (ih, sd, &blocks);
	reiserfs_stat_set_mode (ih, sd, &mode);
	if (!is_new_file)
	    reiserfs_stat_set_fdb (ih, sd, &first_direct_byte);
	reiserfs_buffer_mkdirty (bh);
    }

    return retval;
}


static int is_rootdir_key (const reiserfs_key_t * key)
{
    if (reiserfs_key_comp (key, &root_dir_key))
	return 0;
    return 1;
}


/* returns buffer, containing found directory item.*/
static char * get_next_directory_item (reiserfs_key_t * key, /* on return this will
								contain key of next item
								in the tree */
				       const reiserfs_key_t * parent,
				       reiserfs_ih_t * ih,/*not in tree*/
				       __u32 * pos_in_item)
{
    const reiserfs_key_t * rdkey;
    REISERFS_PATH_INIT (path);
    reiserfs_deh_t * deh;
    reiserfs_bh_t * bh;
    char * dir_item;
    int retval;
    int i;


    if ((retval = reiserfs_tree_search_entry (fs, key, &path)) != POSITION_FOUND)
	reiserfs_panic ("get_next_directory_item: The current directory %k cannot be "
	    "found", key);

    /* leaf containing directory item */
    bh = REISERFS_PATH_LEAF (&path);
    *pos_in_item = path.pos_in_item;
    *ih = *REISERFS_PATH_IH (&path);
    deh = reiserfs_deh (bh, ih);

    /* make sure, that ".." exists as well */
    if (reiserfs_key_get_off (key) == OFFSET_DOT) {
	if (reiserfs_ih_get_entries (ih) < 2 ||
	    reiserfs_direntry_name_len (ih, deh + 1, 1) != 2 ||
	    strncmp (reiserfs_deh_name (deh + 1, 1), "..", 2))
	{
	    fsck_log ("get_next_directory_item: The entry \"..\" cannot be "
		      "found in %k\n", &ih->ih_key);
	    
	    reiserfs_tree_pathrelse (&path);
	    return 0;
	}
    }

    /* mark hidden entries as visible, set "." and ".." correctly */
    deh += *pos_in_item; 
    for (i = *pos_in_item; i < reiserfs_ih_get_entries (ih); i ++, deh ++) {
	int namelen;
	char * name;

	name = reiserfs_deh_name (deh, i);
	namelen = reiserfs_direntry_name_len (ih, deh, i);

	if (reiserfs_deh_get_off (deh) == OFFSET_DOT) {
	    if (reiserfs_key_comp2 (&(deh->deh2_dir_id), key)) {
		/* "." must point to the directory it is in */
		
		//deh->deh_objectid != REISERFS_ROOT_PARENT_OBJECTID)/*????*/ {
		fsck_log ("get_next_directory_item: The entry \".\" of the "
			  "directory %K points to %K, instead of %K", 
			  key, (reiserfs_key_t *)(&(deh->deh2_dir_id)), key);
		
		reiserfs_deh_set_did (deh, reiserfs_key_get_did (key));
		reiserfs_deh_set_obid (deh, reiserfs_key_get_oid (key));
		reiserfs_buffer_mkdirty (bh);
		fsck_log (" - corrected\n");
	    }
	}

	if (reiserfs_deh_get_off (deh) == OFFSET_DOT_DOT) {
	    /* set ".." so that it points to the correct parent directory */
	    if (reiserfs_key_comp2 (&(deh->deh2_dir_id), parent)) {
		fsck_log ("get_next_directory_item: The entry \"..\" of the "
			  "directory %K points to %K, instead of %K", 
		    key, (reiserfs_key_t *)(&(deh->deh2_dir_id)), parent);
		reiserfs_deh_set_did (deh, reiserfs_key_get_did (parent));
		reiserfs_deh_set_obid (deh, reiserfs_key_get_oid (parent));
		reiserfs_buffer_mkdirty (bh);
		fsck_log (" - corrected\n");
	    }
	}
    }

    /* copy directory item to the temporary buffer */
    dir_item = misc_getmem (reiserfs_ih_get_len (ih)); 
    memcpy (dir_item, reiserfs_item_by_ih (bh, ih), reiserfs_ih_get_len (ih));


    /* next item key */
    if ((rdkey = reiserfs_tree_next_key (&path, fs)))
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


// get key of an object pointed by direntry and the key of the entry itself
void get_object_key (reiserfs_deh_t * deh, reiserfs_key_t * key, 
		     reiserfs_key_t * entry_key, reiserfs_ih_t * ih)
{
    /* entry points to this key */
    reiserfs_key_set_did (key, reiserfs_deh_get_did (deh));
    reiserfs_key_set_oid (key, reiserfs_deh_get_obid (deh));
    reiserfs_key_set_off1 (key, OFFSET_SD);
    reiserfs_key_set_uni (key, 0);

    /* key of entry */
    reiserfs_key_set_did (entry_key, reiserfs_key_get_did (&ih->ih_key));
    reiserfs_key_set_oid (entry_key, reiserfs_key_get_oid (&ih->ih_key));
    reiserfs_key_set_off1 (entry_key, reiserfs_deh_get_off (deh));
    reiserfs_key_set_uni (entry_key, UNI_DE);
}

int fix_obviously_wrong_sd_mode (reiserfs_path_t * path) {
    const reiserfs_key_t * next_key;
    __u16 mode;
    int retval = 0;

    next_key = reiserfs_tree_next_key (path, fs);

    if (!next_key)
    	return 0;
	    
    if (reiserfs_key_comp2 (next_key, &REISERFS_PATH_IH(path)->ih_key))
	return 0;
    	
    /* next item exists and of the same file. Fix the SD mode */

    if (not_a_directory (REISERFS_PATH_ITEM (path)) && 
	reiserfs_key_dir (next_key)) 
    {
        /* make SD mode SD of dir */
	reiserfs_stat_get_mode (REISERFS_PATH_IH (path), 
				REISERFS_PATH_ITEM (path), &mode);
	
	fsck_log ("The directory %K had wrong mode %M", 
		  &REISERFS_PATH_IH(path)->ih_key, mode);

	if (fsck_mode(fs) != FSCK_CHECK) {
	    mode &= ~S_IFMT;
	    mode |= S_IFDIR;
	    fsck_log (" - corrected to %M\n", mode);	
	    reiserfs_stat_set_mode (REISERFS_PATH_IH (path), 
				    REISERFS_PATH_ITEM (path), &mode);
	    
	    reiserfs_buffer_mkdirty (REISERFS_PATH_LEAF(path));
	} else {
	    fsck_log ("\n");
	    retval = 1;
	}
    } else if (!not_a_directory (REISERFS_PATH_ITEM (path)) && 
	       !reiserfs_key_dir (next_key)) 
    {
        /* make SD mode SD of regular file */
	reiserfs_stat_get_mode (REISERFS_PATH_IH (path), 
				REISERFS_PATH_ITEM (path), &mode);
	
	fsck_log ("The file %K had wrong mode %M", 
		  &REISERFS_PATH_IH(path)->ih_key, mode);
	
	if (fsck_mode(fs) != FSCK_CHECK) {
	    mode &= ~S_IFMT;
	    mode |= S_IFREG;
	    fsck_log (" - corrected to %M\n", mode);
	    reiserfs_stat_set_mode (REISERFS_PATH_IH (path), 
				    REISERFS_PATH_ITEM (path), &mode);
	    
	    reiserfs_buffer_mkdirty (REISERFS_PATH_LEAF(path));
	} else {
	    fsck_log ("\n");
	    retval = 1;
	}
	    
    }

    return retval;
}

static int is_lost_found (char * name, int namelen)
{
    return (namelen == 10 && !memcmp(name, "lost+found", 10)) ? 1 : 0;
}

void cb_item_modify (reiserfs_ih_t *ih, void *item) {
    zero_nlink (ih, item);
    reiserfs_ih_mkunreach (ih);
}

/* check recursively the semantic tree. Returns OK if entry points to good
   object, STAT_DATA_NOT_FOUND if stat data was not found or RELOCATED when
   file was relocated because its objectid was already marked as used by
   another file */
int rebuild_semantic_pass (reiserfs_key_t * key, 
			   const reiserfs_key_t * parent, 
			   int etype, reiserfs_ih_t * new_ih)
{
    reiserfs_path_t path;
    void * sd;
    int is_new_dir;
    __u32 nlink;
    reiserfs_bh_t * bh;
    reiserfs_ih_t * ih;
    int retval, retval1;
    char * dir_item;
    __u32 pos_in_item;
    reiserfs_ih_t tmp_ih;
    reiserfs_key_t item_key, entry_key, object_key, *found;
    __u64 dir_size;
    __u32 blocks;
    __u64 saved_size;
    __u32 saved_blocks;
    int fix_sd;
    int relocate;
    int dir_format = 0;
    __u16 mode;
    int entry_len;


    retval = OK;

 start_again: /* when directory was relocated */

    if (!reiserfs_key_stat (key))
	reiserfs_panic ("rebuild_semantic_pass: The key %k must "
			"be key of a StatData", key);

    /* look for stat data of an object */
    if (reiserfs_tree_search_item (fs, key, &path) == ITEM_NOT_FOUND) {
	if (is_rootdir_key (key))
	    /* root directory has to exist at this point */
	    reiserfs_panic ("rebuild_semantic_pass: The root directory "
			    "StatData was not found");
	/* If an item of the same object is found, create a stat item. */
	found = &REISERFS_PATH_IH(&path)->ih_key;
	if (!reiserfs_key_comp2(key, found)) {
	    if (reiserfs_key_dir (found) || reiserfs_key_get_off (key) == 1) {
		sem_pass_stat(fs)->added_sd ++;
		reiserfs_tree_create_stat (fs, &path, key, cb_item_modify);
		reiserfs_tree_pathrelse (&path);
		goto start_again;
	    }
	}
	
	reiserfs_tree_pathrelse (&path);
	return STAT_DATA_NOT_FOUND;
    }

    if ((etype == ET_NAME) && !reiserfs_key_comp2(key, &lost_found_dir_key)) {
	/* This is not "lost+found" entry that points to "lost+found" object. */
	reiserfs_tree_pathrelse (&path);
	return DIRECTORY_HAS_NO_ITEMS;
    }

    /* stat data has been found */
    bh = REISERFS_PATH_LEAF (&path);
    ih = REISERFS_PATH_IH (&path);
    sd = REISERFS_PATH_ITEM(&path);

    /* */
    reiserfs_stat_get_nlink (ih, sd, &nlink);

    relocate = 0;
    if (!nlink) {
	/* we reached the stat data for the first time */
	if (id_map_mark(semantic_id_map(fs), reiserfs_key_get_oid (&ih->ih_key))) {
	    /* calculate number of found files/dirs who are using objectid
	       which is used by another file */
	    sem_pass_stat (fs)->oid_sharing ++;
	    relocate = 1;
	}

	fsck_item_mkreach (ih, bh);
    }

    fix_obviously_wrong_sd_mode (&path);

    if (not_a_directory (sd)) {
	retval = rebuild_check_regular_file (&path, sd, relocate ? new_ih : 0);
	reiserfs_tree_pathrelse (&path);
	return retval;
    }

    if (relocate) {
	if (!new_ih)
	    reiserfs_panic ("rebuild_semantic_pass: Memory is not "
			    "prepared for relocation of %K", &ih->ih_key);
	*new_ih = *ih;
	reiserfs_tree_pathrelse (&path);
	sem_pass_stat (fs)->oid_sharing_dirs_relocated ++;
	relocate_dir (new_ih);
	fsck_relocate_mklinked(&new_ih->ih_key);
	*key = new_ih->ih_key;	
	retval = RELOCATED;
	
	goto start_again;
    }

    /* it looks like stat data of a directory found */
    if (nlink) {
	/* we saw this directory already */
	if (etype != ET_DOT_DOT) {
	    /* this name is not ".."  - and hard links are not allowed on
               directories */
	    reiserfs_tree_pathrelse (&path);
	    return STAT_DATA_NOT_FOUND;
	} else {
	    /* ".." found */
	    nlink ++;
	    reiserfs_stat_set_nlink (ih, sd, &nlink);
	    reiserfs_buffer_mkdirty (bh);
	    reiserfs_tree_pathrelse (&path);
	    return OK;
	}
    }


    /* we see the directory first time */
    sem_pass_stat (fs)->directories ++;
    nlink = 2;
    if (reiserfs_key_get_oid (key) == REISERFS_ROOT_OBJECTID)
	nlink ++;
    reiserfs_stat_set_nlink (ih, sd, &nlink);
    reiserfs_buffer_mkdirty (bh);
    
    if (reiserfs_ih_get_len (ih) == REISERFS_SD_SIZE)
	is_new_dir = 1;
    else
	is_new_dir = 0;


/*
    {
	reiserfs_key_t * tmp;

	// check next item: if it is not a directory item of the same file
	// therefore sd_mode is corrupted so we just reset it to regular file
	// mode
	tmp = reiserfs_tree_next_key (&path);
	if (tmp && !reiserfs_key_comp2 (tmp, key) && !reiserfs_key_dir (tmp)) {
	    __u16 mode;

	    reiserfs_stat_get_mode (ih, sd, &mode);
	    fsck_log ("file %K had broken mode %M, ", key, mode);
	    mode &= ~S_IFMT;
	    mode |= S_IFREG;
	    fsck_log ("fixed to %M\n", mode);
	    reiserfs_stat_set_mode (ih, sd, &mode);

	    nlink = 0;
	    reiserfs_stat_set_nlink (ih, sd, &nlink);
	    
	    retval = rebuild_check_regular_file (&path, sd, 0); //no relocate
	    reiserfs_tree_pathrelse (&path);
	    return retval;
	}
    }
*/


    dir_format = (reiserfs_ih_get_len (REISERFS_PATH_IH (&path)) == REISERFS_SD_SIZE) ? 
	    KEY_FORMAT_2 : KEY_FORMAT_1;
    
    /* save stat data's size and st_blocks */
    reiserfs_stat_get_size (ih, sd, &saved_size);
    reiserfs_stat_get_blocks (ih, sd, &saved_blocks);	
    reiserfs_stat_get_mode (ih, sd, &mode);

    /* release path pointing to stat data */
    reiserfs_tree_pathrelse (&path);

    /* make sure that "." and ".." exist */
    entry_len = reiserfs_direntry_entry_estimate (".", dir_format);
    reiserfs_tree_insert_entry (fs, key, ".",  entry_len, key, 
				1 << IH_Unreachable);
    
    entry_len = reiserfs_direntry_entry_estimate ("..", dir_format);
    reiserfs_tree_insert_entry (fs, key, "..", entry_len, 
				parent, 1 << IH_Unreachable);

    reiserfs_key_set_did (&item_key, reiserfs_key_get_did (key));
    reiserfs_key_set_oid (&item_key, reiserfs_key_get_oid (key));
    reiserfs_key_set_off1 (&item_key, OFFSET_DOT);
    reiserfs_key_set_uni (&item_key, UNI_DE);


    dir_size = 0;
    while ((dir_item = get_next_directory_item (&item_key, parent, 
						&tmp_ih, &pos_in_item)) != 0) 
    {
	/* dir_item is copy of the item in separately allocated memory,
	   item_key is a key of next item in the tree */
	int i;
	char name[REISERFS_NAME_MAX];
	int namelen, entry_len;
	reiserfs_deh_t * deh = (reiserfs_deh_t *)dir_item + pos_in_item;	
	
	for (i = pos_in_item; i < reiserfs_ih_get_entries (&tmp_ih); i ++, deh ++) {
	    reiserfs_ih_t relocated_ih;
	
	    name[0] = '\0';
	
	    namelen = reiserfs_direntry_name_len (&tmp_ih, deh, i);
	    sprintf(name, "%.*s", namelen, reiserfs_deh_name (deh, i));
	
	    entry_len = reiserfs_direntry_entry_len (&tmp_ih, deh, i);
	
	    get_object_key (deh, &object_key, &entry_key, &tmp_ih);
	
	    if ((dir_format == KEY_FORMAT_2) && (entry_len % 8 != 0)) {
	    	/* not alighed directory of new format - delete it */
		fsck_log ("Entry %K (\"%.*s\") in the directory %K is not "
			  "formated properly - fixed.\n", 
			  (reiserfs_key_t *)&(deh->deh2_dir_id), 
			  namelen, name, &tmp_ih.ih_key);
		
		reiserfs_tree_delete_entry (fs, &entry_key);
		entry_len = reiserfs_direntry_entry_estimate (name, dir_format);
		reiserfs_tree_insert_entry (fs, key, name, entry_len,
                                (reiserfs_key_t *)&(deh->deh2_dir_id), 0);
	    }
	    
	    /*	
	    if ((dir_format == KEY_FORMAT_1) && (namelen != entry_len)) {
	    	// aligned entry in directory of old format - remove and 
		// insert it back 
		fsck_log ("Entry %K (\"%.*s\") in the directory %K is not "
			  "formated properly - deleted\n",
			  (reiserfs_key_t *)&(deh->deh2_dir_id), 
			  namelen, name, &tmp_ih.ih_key);
			  
		reiserfs_tree_delete_entry (fs, &entry_key);
		entry_len = reiserfs_direntry_entry_estimate (name, dir_format);
		reiserfs_tree_insert_entry (fs, key, name, entry_len,
				(reiserfs_key_t *)&(deh->deh2_dir_id), 0);
	    }
	    */	
	    
	    if (is_dot (name, namelen)) {
		dir_size += REISERFS_DEH_SIZE + entry_len;
		continue;
	    }
	
	    if (!fsck_quiet(fs)) {
		util_misc_print_name (fsck_progress_file(fs), 
				      name, namelen);
	    }
	    
	    if (!reiserfs_hash_correct (&fs->hash, name, namelen, 
					reiserfs_deh_get_off (deh)))
	    {
		reiserfs_panic ("rebuild_semantic_pass: Hash mismatch "
				"detected for (\"%.*s\") in directory %K\n", 
				namelen, name, &tmp_ih.ih_key);
	    }
	
	    retval1 = rebuild_semantic_pass (&object_key, key, 
					     is_dot_dot (name, namelen) ? ET_DOT_DOT :
					     reiserfs_key_get_oid (key) == REISERFS_ROOT_OBJECTID &&
					     is_lost_found (name, namelen) ? ET_LOST_FOUND : 0, 
					     &relocated_ih);
	   
	    if (!fsck_quiet(fs)) {
		util_misc_erase_name (fsck_progress_file(fs), 
				      namelen);
	    }
	    
	    switch (retval1) {
	    case OK:
		dir_size += REISERFS_DEH_SIZE + entry_len;
		break;

	    case STAT_DATA_NOT_FOUND:
	    case DIRECTORY_HAS_NO_ITEMS:
		if (reiserfs_key_get_off (&entry_key) == OFFSET_DOT_DOT && 
		    reiserfs_key_get_oid (&object_key) == 
		    REISERFS_ROOT_PARENT_OBJECTID) 
		{
		    /* ".." of root directory can not be found */
		    dir_size += REISERFS_DEH_SIZE + entry_len;
		    continue;
		}
		
		fsck_log ("%s: The entry %K (\"%.*s\") in directory %K "
			  "points to nowhere - is removed\n", __FUNCTION__, 
			  &object_key, namelen, name, &tmp_ih.ih_key);
		
		reiserfs_tree_delete_entry (fs, &entry_key);
		sem_pass_stat (fs)->deleted_entries ++;
		break;
		
	    case RELOCATED:
		/* file was relocated, update key in directory entry */
		
		if (reiserfs_tree_search_entry (fs, &entry_key, &path) != 
		    POSITION_FOUND) 
		{
		    fsck_log ("WARNING: Cannot find the name of the relocated "
			      "file %K in the directory %K\n", &object_key, 
			      &tmp_ih.ih_key);
		} else {
		    /* update key dir entry points to */
		    reiserfs_deh_t * tmp_deh;
		    
		    tmp_deh = reiserfs_deh (REISERFS_PATH_LEAF (&path), 
				       REISERFS_PATH_IH (&path)) + path.pos_in_item;
		    
		    fsck_log ("The entry %K (\"%.*s\") in directory %K "
			      "updated to point to ", &object_key, namelen, 
			      name, &tmp_ih.ih_key);
		    
		    reiserfs_deh_set_did (tmp_deh, reiserfs_key_get_did (&relocated_ih.ih_key));
		    reiserfs_deh_set_obid (tmp_deh, 
				      reiserfs_key_get_oid (&relocated_ih.ih_key));

		    fsck_log ("%K\n",  &tmp_deh->deh2_dir_id);
		    reiserfs_buffer_mkdirty (REISERFS_PATH_LEAF (&path));
		}
		
		dir_size += REISERFS_DEH_SIZE + entry_len;
		reiserfs_tree_pathrelse (&path);
		break;
	    }
	} /* for */
	
	misc_freemem (dir_item);
	
	if (reiserfs_key_comp2 (&item_key, key)  || !reiserfs_key_dir(&item_key))
	    /* next key is not of this directory */
	    break;
	
    } /* while (dir_item) */
    
    
    if (dir_size == 0)
	/* FIXME: is it possible? */
	return DIRECTORY_HAS_NO_ITEMS;
    
    /* calc correct value of sd_blocks field of stat data */
    blocks = REISERFS_DIR_BLOCKS (dir_size);
    
    fix_sd = 0;
    fix_sd += wrong_st_blocks (key, &blocks, saved_blocks, mode, is_new_dir);
    fix_sd += wrong_st_size (key, is_new_dir ? REISERFS_SD_SIZE_MAX_V2 : 
			     REISERFS_SD_SIZE_MAX_V1, fs->fs_blocksize, 
			     &dir_size, saved_size, TYPE_DIRENTRY);

    if (fix_sd) {
	/* we have to fix either sd_size or sd_blocks, so look for SD again */
	if (reiserfs_tree_search_item (fs, key, &path) != ITEM_FOUND)
	    reiserfs_panic ("rebuild_semantic_pass: The StatData of the "
			    "file %K was not found", key);
	    
	bh = REISERFS_PATH_LEAF (&path);
	ih = REISERFS_PATH_IH (&path);
	sd = REISERFS_PATH_ITEM (&path);
	
	reiserfs_stat_set_size (ih, sd, &dir_size);
	reiserfs_stat_set_blocks (ih, sd, &blocks);
	reiserfs_buffer_mkdirty (bh);
	reiserfs_tree_pathrelse (&path);
    }
    
    return retval;
}


int is_dot (char * name, int namelen)
{
    return (namelen == 1 && name[0] == '.') ? 1 : 0;
}


int is_dot_dot (char * name, int namelen)
{
    return (namelen == 2 && name[0] == '.' && name[1] == '.') ? 1 : 0;
}

int not_a_directory (void * sd)
{
    /* mode is at the same place and of the same size in both stat
       datas (v1 and v2) */
    reiserfs_sd_v1_t * sd_v1 = sd;

    return !(S_ISDIR (le16_to_cpu (sd_v1->sd_mode)));
}

int not_a_regfile (void * sd)
{
    /* mode is at the same place and of the same size in both stat
       datas (v1 and v2) */
    reiserfs_sd_v1_t * sd_v1 = sd;

    return !(S_ISREG (le16_to_cpu (sd_v1->sd_mode)));
}



void zero_nlink (reiserfs_ih_t * ih, void * sd)
{
    int zero = 0;

    if (reiserfs_ih_get_len (ih) == REISERFS_SD_SIZE_V1 && reiserfs_ih_get_format (ih) != 
	KEY_FORMAT_1) 
    {
	fsck_log ("zero_nlink: The StatData %k of the wrong format version "
		  "(%d) - corrected to (%d)\n", ih, reiserfs_ih_get_format (ih), 
		  KEY_FORMAT_1);
	
	reiserfs_ih_set_format (ih, KEY_FORMAT_1);
    }
    
    if (reiserfs_ih_get_len (ih) == REISERFS_SD_SIZE && reiserfs_ih_get_format (ih) != 
	KEY_FORMAT_2) 
    {
	fsck_log ("zero_nlink: The StatData %k of the wrong format version "
		  "(%d) - corrected to (%d)\n", ih, reiserfs_ih_get_format (ih), 
		  KEY_FORMAT_2);
	
	reiserfs_ih_set_format (ih, KEY_FORMAT_2);
    }

    reiserfs_stat_set_nlink (ih, sd, &zero);
}

/* mkreiserfs should have created this */
static void make_sure_lost_found_exists (reiserfs_filsys_t * fs, 
					 __u16 root_format)
{
    int retval;
    REISERFS_PATH_INIT (path);
    unsigned int gen_counter;
    __u32 objectid;
    __u64 sd_size;
    __u32 sd_blocks;
    reiserfs_bh_t * bh;
    reiserfs_ih_t * ih;
    void * sd;
    int item_len;
    int entry_len;


    /* look for "lost+found" in the root directory */
    retval = reiserfs_tree_search_name (fs, &root_dir_key,
					"lost+found", &gen_counter,
					&lost_found_dir_key);
    if (!retval) {
	objectid = id_map_alloc(proper_id_map(fs));
	if (!objectid) {
	    fsck_progress ("Could not allocate an objectid for '/lost+found'",
			   "lost files will not be linked\n");
	    return;
	}
	reiserfs_key_set_did (&lost_found_dir_key, REISERFS_ROOT_OBJECTID);
	reiserfs_key_set_oid (&lost_found_dir_key, objectid);
    }


    /* look for stat data of "lost+found" */
    retval = reiserfs_tree_search_item (fs, &lost_found_dir_key, &path);
    if (retval == ITEM_NOT_FOUND) {
	fs->lost_format = reiserfs_tree_create_stat 
		(fs, &path, &lost_found_dir_key, cb_item_modify);
    } else {
    	reiserfs_ih_t * ih = REISERFS_PATH_IH (&path);
	
    	if (!reiserfs_ih_stat (ih))
	    reiserfs_panic ("It must be lost+found's stat data %k\n", 
			    &ih->ih_key);
	
	fix_obviously_wrong_sd_mode (&path);
	
	if (not_a_directory (REISERFS_PATH_ITEM (&path))) {
	    fsck_progress ("\"/lost+found\" exists, but it is not a "
			   "directory, lost files will not be linked\n");
	    reiserfs_key_set_oid (&lost_found_dir_key, 0);
	    reiserfs_tree_pathrelse (&path);
	    return;
	}
    	
        fs->lost_format = (reiserfs_ih_get_len (REISERFS_PATH_IH (&path)) == 
			   REISERFS_SD_SIZE) ? KEY_FORMAT_2 : KEY_FORMAT_1;
        	
	reiserfs_tree_pathrelse (&path);
    }

    /* add "." and ".." if any of them do not exist */
    entry_len = reiserfs_direntry_entry_estimate (".", fs->lost_format);
    reiserfs_tree_insert_entry (fs, &lost_found_dir_key, ".", entry_len,
				&lost_found_dir_key, 1 << IH_Unreachable);
    
    entry_len = reiserfs_direntry_entry_estimate ("..", fs->lost_format);
    reiserfs_tree_insert_entry (fs, &lost_found_dir_key, "..", entry_len,
				&root_dir_key, 1 << IH_Unreachable);

    entry_len = reiserfs_direntry_entry_estimate ("lost+found", root_format);
    item_len = reiserfs_tree_insert_entry (fs, &root_dir_key, "lost+found",
					   entry_len, &lost_found_dir_key, 
					   1 << IH_Unreachable);

    if (item_len) {
	if (reiserfs_tree_search_item (fs, &root_dir_key, &path) == 
	    ITEM_NOT_FOUND)
	{
	    reiserfs_panic ("%s: StatData of the root directory must exists", 
			    __FUNCTION__);
	}
	
	bh = REISERFS_PATH_LEAF (&path);
	ih = REISERFS_PATH_IH (&path);
	sd = REISERFS_PATH_ITEM(&path);
	
	reiserfs_stat_get_size (ih, sd, &sd_size);
	sd_size += item_len;
	reiserfs_stat_set_size (ih, sd, &sd_size);
	sd_blocks = REISERFS_DIR_BLOCKS (sd_size);
	reiserfs_stat_set_blocks (ih, sd, &sd_blocks);
	reiserfs_buffer_mkdirty (bh);
	reiserfs_tree_pathrelse (&path);
    }
			
    return;
}

/* Result of the rebuild pass will be saved in the state file 
   which is needed to start fsck again from the next pass. */
static void fsck_semantic_save_result (reiserfs_filsys_t * fs) {
    FILE * file;
    int retval;

    file = util_file_open ("temp_fsck_file.deleteme", "w+");
    if (!file)
	return;

    fsck_stage_start_put (file, SEMANTIC_DONE);
    reiserfs_objectid_map_save (file, semantic_id_map (fs));
    fsck_stage_end_put (file);
    fclose (file);
    retval = rename ("temp_fsck_file.deleteme", state_dump_file (fs));
    if (retval != 0) {
	fsck_progress ("%s: Could not rename the temporary file "
		       "temp_fsck_file.deleteme to %s",
		       __FUNCTION__, state_dump_file (fs));
    }
}

/* we have nothing to load from a state file, but we have to fetch
   on-disk bitmap, copy it to allocable bitmap, and fetch objectid
   map */
void fsck_semantic_load_result (FILE * file, reiserfs_filsys_t * fs) {
    unsigned int gen_counter;
    REISERFS_PATH_INIT(path);
    
    fsck_new_bitmap (fs) = reiserfs_bitmap_create 
	    (reiserfs_sb_get_blocks (fs->fs_ondisk_sb));
    
    reiserfs_bitmap_copy (fsck_new_bitmap (fs), fs->fs_bitmap2);

    fsck_allocable_bitmap (fs) = reiserfs_bitmap_create 
	    (reiserfs_sb_get_blocks (fs->fs_ondisk_sb));
    
    reiserfs_bitmap_copy (fsck_allocable_bitmap (fs), fs->fs_bitmap2);

    fs->block_allocator = reiserfsck_new_blocknrs;
    fs->block_deallocator = reiserfsck_free_block;

    /* we need objectid map on semantic pass to be able to relocate files */
    proper_id_map (fs) = id_map_init();
    
    fetch_objectid_map (proper_id_map (fs), fs);
    semantic_id_map (fs) = reiserfs_objectid_map_load (file);

    /* get the lost_found key. */
    if (!(reiserfs_tree_search_name (fs, &root_dir_key,
				     "lost+found", &gen_counter,
				     &lost_found_dir_key)))
    {
	    reiserfs_panic("Lost&found entry cannot be found in the root dir.");
    }

    if (reiserfs_tree_search_item (fs, &lost_found_dir_key, 
				   &path) == ITEM_NOT_FOUND)
    {
	    reiserfs_panic("Lost&found StatData item cannot be found.");
    }

    fs->lost_format = (reiserfs_ih_get_len (REISERFS_PATH_IH (&path)) == 
		       REISERFS_SD_SIZE) ? KEY_FORMAT_2 : KEY_FORMAT_1;
    reiserfs_tree_pathrelse (&path);
}

static void before_pass_3 (reiserfs_filsys_t * fs) {
    semantic_id_map (fs) = id_map_init();
}

static void after_pass_3 (reiserfs_filsys_t * fs) {
    /* update super block: objectid map, fsck state */
    reiserfs_sb_set_state (fs->fs_ondisk_sb, SEMANTIC_DONE);
    reiserfs_buffer_mkdirty (fs->fs_super_bh);

    /* write all dirty blocks */
    fsck_progress ("Flushing..");
    id_map_flush(proper_id_map (fs), fs);
    fs->fs_dirt = 1;
    reiserfs_bitmap_flush (fsck_new_bitmap(fs), fs);
    reiserfs_fs_flush (fs);
    fsck_progress ("finished\n");

    fsck_stage_report (FS_SEMANTIC, fs);

    if (!fsck_run_one_step (fs)) {
	if (fsck_info_ask (fs, "Continue? (Yes):", "Yes\n", 1))
	    /* reiserfsck continues */
	    return;
    }

    fsck_semantic_save_result (fs);

    id_map_free(proper_id_map (fs));
    proper_id_map (fs) = 0;

    fs->fs_dirt = 1;
    reiserfs_fs_close (fs);
    exit(0);
}

/* this is part of rebuild tree */
void fsck_semantic (reiserfs_filsys_t * fs) {
    __u16 root_format;
    
    before_pass_3 (fs);

    fsck_progress ("Pass 3 (semantic):\n");

    /* when warnings go not to stderr - separate them in the log */
    if (fsck_log_file (fs) != stderr)
	fsck_log ("####### Pass 3 #########\n");


    if (!fs->hash)
	reiserfs_panic ("Hash function should be selected already");

    root_format = reiserfs_tree_root (fs, cb_item_modify, 1 << IH_Unreachable);
    make_sure_lost_found_exists (fs, root_format);

    id_map_mark(proper_id_map(fs), reiserfs_key_get_oid(&root_dir_key));
    id_map_mark(proper_id_map(fs), reiserfs_key_get_oid(&lost_found_dir_key));
    
    /* link all relocated files into /lost+found directory */
    fsck_relocate_link_all();

    rebuild_semantic_pass ((reiserfs_key_t *)&root_dir_key, 
			   &parent_root_dir_key, ET_NAME, 0/*reloc_ih*/);

    if (!fsck_quiet(fs))
	util_misc_fini_name(fsck_progress_file(fs));
    
    reiserfs_badblock_flush(fs, 1);

    after_pass_3 (fs);
}
