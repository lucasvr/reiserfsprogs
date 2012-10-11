/*
 * Copyright 1996-2004 by Hans Reiser, licensing governed by 
 * reiserfsprogs/README
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "fsck.h"
#include <sys/stat.h>
#include "misc/unaligned.h"
#include "misc/malloc.h"
#include "util/misc.h"

/* this works for both new and old stat data */
#define st_mode(sd) le16_to_cpu(((reiserfs_sd_t *)(sd))->sd_mode)

#define st_mtime_v1(sd) le32_to_cpu(((reiserfs_sd_v1_t *)(sd))->sd_mtime)
#define st_mtime_v2(sd) le32_to_cpu(((reiserfs_sd_t *)(sd))->sd_mtime)


static int fsck_tree_insert_file (reiserfs_ih_t * ih, 
				  char * item, int check);

/* either both sd-s are new of both are old */
static void fsck_tree_overwrite_stat (reiserfs_ih_t * new_ih,
				      void * new_item, 
				      reiserfs_path_t * path)
{
    __u16 new_mode, old_mode;

    reiserfs_stat_get_mode (new_ih, new_item, &new_mode);
    reiserfs_stat_get_mode (REISERFS_PATH_IH (path), 
			    REISERFS_PATH_ITEM (path), &old_mode);


    if ((S_ISREG (new_mode) && S_ISDIR (old_mode)) ||
	(S_ISDIR (new_mode) && S_ISREG (old_mode)))
    {
	reiserfs_panic("Cannot overwrite SD [%H] with SD [%H].\n", 
		       REISERFS_PATH_IH (path), new_ih);
    }
    
    if (S_ISREG (new_mode) && !S_ISREG (old_mode)) {
	/* in tree we have not regular file - overwrite its stat data
           with stat data of regular file */
	memcpy (REISERFS_PATH_ITEM (path), new_item, 
		reiserfs_ih_get_len (REISERFS_PATH_IH (path)));
	reiserfs_buffer_mkdirty (REISERFS_PATH_LEAF (path));
	return;
    }

    if (S_ISREG (old_mode) && !S_ISREG (new_mode)) {
	/* new stat data is not a stat data of regular file, keep
           regular file stat data in tree */
	return;
    }
    
    /* if coming stat data has newer mtime - use that */
    if (reiserfs_ih_format_v1 (new_ih)) {	
	if (st_mtime_v1 (new_item) > st_mtime_v1 (REISERFS_PATH_ITEM (path))) {
	    memcpy (REISERFS_PATH_ITEM (path), new_item, REISERFS_SD_SIZE_V1);
	    reiserfs_buffer_mkdirty (REISERFS_PATH_LEAF (path));
	}
    } else {
	if (st_mtime_v2 (new_item) > st_mtime_v2 (REISERFS_PATH_ITEM (path))) {
	    memcpy (REISERFS_PATH_ITEM (path), new_item, REISERFS_SD_SIZE);
	    reiserfs_buffer_mkdirty (REISERFS_PATH_LEAF (path));
	}
    }
    return;
}

/* insert sd item if it does not exist, overwrite it otherwise */
static void fsck_tree_insert_stat (reiserfs_ih_t * new_ih, 
				   char * new_item, int check)
{
    reiserfs_path_t path;
    __u32 oid;

    if (check) {
	oid = fsck_relocate_check (new_ih, !not_a_directory(new_item));
    
	if (oid == 1) {
	    fsck_file_relocate (&new_ih->ih_key, 
				not_a_directory(new_item) ? 1 : 0);
	} else if (oid) {
	    reiserfs_key_set_oid (&new_ih->ih_key, oid);
	}
    }
    
    /* if we will have to insert item into tree - it is ready */
    zero_nlink (new_ih, new_item);
    fsck_item_mkunreach (new_ih);
    
    /* we are sure now that if we are inserting stat data of a
       directory - there are no items with the same key which are not
       items of a directory, and that if we are inserting stat data is
       of not a directory - it either has new key already or there are
       no items with this key which are items of a directory */
    if (reiserfs_tree_search_item (fs, &(new_ih->ih_key), &path) == ITEM_FOUND) {
	/* this stat data is found */
        if (reiserfs_ih_get_format (REISERFS_PATH_IH(&path)) != 
	    reiserfs_ih_get_format (new_ih)) 
	{
	    /* in tree stat data and a new one are of different
               formats */
	    fsck_log ("%s: Inserting the StatData %K, mode (%M)...", 
		      __FUNCTION__, &(new_ih->ih_key), st_mode (new_item));
	    
	    if (reiserfs_ih_format_v1 (new_ih)) {
		/* sd to be inserted is of V1, where as sd in 
		   the tree is of V2 */
		fsck_log ("found newer in the tree, mode (%M), insersion was "
			  "skipped.\n", st_mode (REISERFS_PATH_ITEM (&path)));
	    	reiserfs_tree_pathrelse (&path);
	    } else {
		/* the stat data in the tree is sd_v1 */
		fsck_log ("older sd, mode (%M), is replaced with it.\n",
			  st_mode (REISERFS_PATH_ITEM (&path)));
		
		reiserfs_tree_delete (fs, &path, 0/*not temporary*/);
		reiserfs_tree_search_item (fs, &new_ih->ih_key, &path);
		reiserfs_tree_insert (fs, &path, new_ih, new_item);
	    }
	} else {
	    /* both stat data are of the same version */
	    fsck_tree_overwrite_stat (new_ih, new_item, &path);
	    reiserfs_tree_pathrelse (&path);
	}
	
	return;
    }
    
    /* item not found, insert a new one */
    reiserfs_tree_insert (fs, &path, new_ih, new_item);
}

static int fsck_tree_insert_prep (reiserfs_filsys_t *fs, 
				  reiserfs_ih_t *ih, 
				  int directory) 
{
    reiserfs_path_t path;
    reiserfs_key_t key;
    __u32 oid = 0;
    int step = 0;

    /* Check if the ois has been already relocated. */
    if (!directory && (oid = fsck_relocate_get_oid(&ih->ih_key)))
	reiserfs_key_set_oid(&ih->ih_key, oid);
    
    memset(&key, 0, sizeof(key));
    
    while (1) {
	reiserfs_key_copy2(&key, &ih->ih_key);
	
	/* Check if the StatData item presents. */
	if ((reiserfs_tree_search_item (fs, &key, &path) != ITEM_FOUND) ||
	    (directory && not_a_directory(REISERFS_PATH_ITEM(&path))) || 
	    (!directory && !not_a_directory(REISERFS_PATH_ITEM(&path))))
	{
	    fsck_log ("vpf-10260: The file we are inserting the new item "
		      "(%H) into has no StatData, insertion is skipped\n", ih);

	    reiserfs_tree_pathrelse (&path);
	    return 1;
	}
	
	reiserfs_tree_pathrelse(&path);
	
	if (!step && !oid) {
	    oid = fsck_relocate_check(ih, directory);
	    
	    if (oid == 1) {
		fsck_file_relocate (&ih->ih_key, directory ? 0 : 1);
	    } else if (oid) {
		reiserfs_key_set_oid(&ih->ih_key, oid);
	    } else {
		return 0;
	    }
	    
	    step = 1;
	    continue;
	}
	
	
	/* Check/Fix the format. */
#if 0
	format = reiserfs_ih_get_format (REISERFS_PATH_IH(&path));
	reiserfs_stat_get_mode (REISERFS_PATH_IH(&path), 
				REISERFS_PATH_ITEM(&path), &mode);

	if (format != reiserfs_ih_get_format (ih)) {
	    /* Not for symlinks and not for items which should be relocted. */
	    if (((S_ISDIR(mode) && reiserfs_ih_dir(ih)) || 
		 (!S_ISDIR(mode) && !reiserfs_ih_dir(ih))) && 
		!S_ISLNK(mode))
	    {
		reiserfs_key_set_sec (format, &ih->ih_key, 
				      reiserfs_key_get_off (&ih->ih_key), 
				      reiserfs_key_get_type (&ih->ih_key));

		reiserfs_ih_set_format(ih, file_format);		
	    }
	}
#endif
	
	return 0;
    }
}

/* this tries to put each item entry to the tree, if there is no items
   of the directory, insert item containing 1 entry */
static void fsck_tree_insert_entry (reiserfs_ih_t * comingih, 
				    char * item, int check)
{
    char buf[REISERFS_NAME_MAX];
    reiserfs_deh_t *deh;
    char *name;
    int namelen;
    int i, step;

    step = 0;
    
    /* Check if the SD item exists */
    if (check) {
	if (fsck_relocate_check (comingih, 1))
	    fsck_file_relocate (&comingih->ih_key, 0);
    }
    
    deh = (reiserfs_deh_t *)item;
    
    for (i = 0; i < reiserfs_ih_get_entries (comingih); i ++, deh ++) {
	name = reiserfs_deh_name (deh, i);
	namelen = reiserfs_direntry_name_len (comingih, deh, i);

	if (!reiserfs_hash_correct (&fs->hash, name, namelen, 
				    reiserfs_deh_get_off (deh)))
	{
	    reiserfs_panic ("%s: The entry (%d) \"%.*s\" of the directory "
			    "%k has badly hashed entry", __FUNCTION__, i, 
			    namelen, name, &comingih->ih_key);
	}

	buf[0] = '\0';
	sprintf (buf, "%.*s", namelen, name);
	/* 1 for fsck is important: if there is no any items of this
           directory in the tree yet - new item will be inserted
           marked not reached */
	reiserfs_tree_insert_entry (fs, &(comingih->ih_key), buf, 
				    reiserfs_direntry_entry_len (comingih, deh, i),
				    (reiserfs_key_t *)&(deh->deh2_dir_id), 
				    1 << IH_Unreachable);
    }
}

/*  start_key is the key after which N items need to be deleted
    save_here is a pointer where deleted items need to be saved if save is set.
    start_key is the first undeleted item.
    return whether we are sure there is nothing else of this file
 */
int fsck_tree_delete(reiserfs_key_t * start_key, 
		     saveitem_t ** save_here, 
		     int skip_dir_items, 
		     tree_modify_t func,
		     void *data)
{
    const reiserfs_key_t *rkey;
    reiserfs_path_t path;
    int count = 0;
    int ret;

    while (1) {
	reiserfs_tree_search_body (fs, start_key, &path);
	
	if (REISERFS_PATH_LEAF_POS (&path) == 
	    reiserfs_node_items (REISERFS_PATH_LEAF (&path))) 
	{
	    rkey = reiserfs_tree_rkey (&path, fs);
	    if (rkey && !reiserfs_key_comp2 (start_key, rkey)) {
		/* file continues in the right neighbor */
		reiserfs_key_copy (start_key, rkey);
		reiserfs_tree_pathrelse (&path);
		continue;
	    }
	    /* there is no more items with this key */
	    reiserfs_tree_pathrelse (&path);
	    return count;
	}

	rkey = &(REISERFS_PATH_IH (&path))->ih_key;
	if (reiserfs_key_comp2 (start_key, rkey)) {
	    /* there are no more item with this key */
	    reiserfs_tree_pathrelse (&path);
	    return count;
	}

	reiserfs_key_copy (start_key, rkey);

	/* Let it to be here to return either the last deleted key in 
	   @start_key or the first not deleted. */
/*	if (n_to_delete && count == n_to_delete) {
	    reiserfs_tree_pathrelse (&path);
	    break;
	}
*/	
	/* ok, item found, but make sure that it is not a directory one */
	if ((reiserfs_key_stat (rkey) && 
	     !not_a_directory (REISERFS_PATH_ITEM (&path))) ||
	    (reiserfs_key_dir (rkey)))
	{
	    if (skip_dir_items) {
	    	/* item of directory found. Leave it in the tree */
		reiserfs_key_set_off(KEY_FORMAT_1, start_key, 
				     reiserfs_key_get_off(start_key) + 1);
	    	reiserfs_tree_pathrelse (&path);
	    	continue;
	    } else {
	    	reiserfs_panic ("%s: No directory item of %K are expected", 
				__FUNCTION__, rkey);
	    }
	}
	
	/* If removing not by the counter, break when it is save to do --
	   an indirect item is encountered. */
	if ((ret = func(&path, data)) < 0) {
	    // if (n_to_delete == 0 && count && reiserfs_key_ext(rkey)) {
	    reiserfs_tree_pathrelse (&path);
	    break;
	}
	
	if (save_here != NULL)
	    fsck_item_save(&path, save_here);
	
	reiserfs_tree_delete (fs, &path, save_here ? 1 : 0);
	
	count++;

	if (ret > 0)
	    break;
    }

    return count;
}

/* this is for check only. With this we make sure that all pointers we
   put into tree on pass 1 do not point to leaves (FIXME), do not
   point to journal, bitmap, etc, do not point out of fs boundary (and
   are marked used in on-disk bitmap - this condition skipped for now). */

/* pointers to data block which get into tree are checked with this */
static int still_bad_unfm_ptr (unsigned long block) {
    if (!block)
	return 0;
    if (is_block_used (block))
	return 1;
    if (block >= reiserfs_sb_get_blocks (fs->fs_ondisk_sb))
	return 1;
    return 0;
}

static int fsck_tree_create_item (reiserfs_filsys_t *fs, 
				  reiserfs_path_t * path, 
				  reiserfs_ih_t * ih, 
				  char * item,
				  int pos, 
				  int alloc)
{
    reiserfs_ih_t newih;
    __u32 body_off;
    __u64 key_off;
    int count, i;
    int format;

    body_off = pos * (reiserfs_ih_direct(ih) ? 1 : REISERFS_EXT_SIZE);
    key_off  = pos * (reiserfs_ih_direct(ih) ? 1 : fs->fs_blocksize);
    format = reiserfs_key_format(&ih->ih_key);
    
    memcpy(&newih, ih, sizeof(newih));
    fsck_item_mkunreach (&newih);
    reiserfs_ih_set_len (&newih, reiserfs_ih_get_len (ih) - body_off);
    reiserfs_ih_set_format (&newih, reiserfs_ih_get_format (ih));
    reiserfs_ih_set_loc(&newih, 0);
    
    reiserfs_key_set_off (format, &newih.ih_key, 
			  reiserfs_key_get_off(&newih.ih_key) + key_off);
    
    if (alloc && reiserfs_ih_ext(ih)) {
	count = reiserfs_ext_count (ih);
	for (i = pos; i < count; i++) {
	    if (still_bad_unfm_ptr (d32_get(item, i)))
		reiserfs_panic ("%s: The file %K has a pointer to "
				"the bad block (%u)", __FUNCTION__,
				&ih->ih_key, d32_get(item, i));
		
	    mark_block_used (d32_get(item, i), 0);
	}
    }

    reiserfs_tree_insert (fs, path, &newih, item + body_off);
    return reiserfs_leaf_ibytes (ih, fs->fs_blocksize) - key_off;
}

int fsck_tree_insert_zero_ptr (reiserfs_filsys_t *fs,
			       reiserfs_key_t *key,
			       long long int p_count,
			       __u16 flags)
{
    reiserfs_path_t path;
    long long int count;
    reiserfs_ih_t *ih;
    int format;
    __u32 * ni;
    int ret;

    ret = reiserfs_tree_search_position (fs, key, &path);
    
    if (ret == DIRECTORY_FOUND || ret == POSITION_FOUND) {
	    reiserfs_panic("%s: The object [%k] must be a file and the "
			   "position must be absent.\n", __FUNCTION__, 
			   key);
    }

    count = REISERFS_ITEM_MAX(fs->fs_blocksize) / REISERFS_EXT_SIZE;

    p_count = (p_count + fs->fs_blocksize - 1) / 
	    fs->fs_blocksize * fs->fs_blocksize;
    
    if (p_count / fs->fs_blocksize <= count)
	count = p_count / fs->fs_blocksize;

    ni = misc_getmem (count * REISERFS_EXT_SIZE);

    if (path.pos_in_item) {
	/* Position is not found, append to the existent item. */
	ih = REISERFS_PATH_IH (&path);
	
	if (reiserfs_item_count(ih) != path.pos_in_item) {
	    reiserfs_panic("%s: not expected position (%u) in the "
			   "middle of the item %k.\n", __FUNCTION__,
			   path.pos_in_item, &ih->ih_key);
	}

	if ((flags & (1 << IH_Unreachable)) || 
	    reiserfs_ih_direct(ih) || must_there_be_a_hole(ih, key))
	{
	    REISERFS_PATH_LEAF_POS (&path) ++;
	    path.pos_in_item = 0;
	} else {
	    reiserfs_tree_insert_unit(fs, &path, ni, count * REISERFS_EXT_SIZE);
	    
	    if (path.pos_in_item == 0)
		misc_die("Not expected position");
	} 
    } 
    
    if (path.pos_in_item == 0) {
	/* Either file not found or position not found, create a new item. */
	reiserfs_ih_t indih;
	
	memset(&indih, 0, sizeof(indih));
	
	reiserfs_ih_set_flags(&indih, flags);

	reiserfs_key_copy (&(indih.ih_key), key);
	reiserfs_ih_set_len (&indih, count * REISERFS_EXT_SIZE);

	reiserfs_ih_set_free (&indih, 0);
	format = reiserfs_key_format(key);
	reiserfs_ih_set_format (&indih, format);
	reiserfs_key_set_type (format, &indih.ih_key, TYPE_EXTENT);
	reiserfs_tree_insert (fs, &path, &indih, ni);
    }

    misc_freemem(ni);
    return count * fs->fs_blocksize;
}

long long int must_there_be_a_hole (const reiserfs_ih_t *ih, 
				    const reiserfs_key_t *key)
{
    return (reiserfs_key_get_off (key) - 
	    reiserfs_key_get_off(&ih->ih_key) - 
	    reiserfs_leaf_ibytes(ih, fs->fs_blocksize));
}

void fsck_tree_merge(reiserfs_path_t *path) {
    int start, len, next_start, next_len;
    reiserfs_ih_t *next;
    reiserfs_ih_t *ih;
    reiserfs_bh_t *bh;
    char *buf;
    int pos;
    
    bh = REISERFS_PATH_LEAF(path);
    pos = REISERFS_PATH_LEAF_POS(path);
    
    if (pos >= reiserfs_node_items(bh) || pos < 1)
	misc_die("%s: block (%lu), item (%d), nothing to merge with.", 
		 __FUNCTION__, bh->b_blocknr, pos);

    ih = reiserfs_ih_at(bh, pos) - 1;
    next = ih + 1;
    
    start = reiserfs_ih_get_loc(ih);
    len = reiserfs_ih_get_len(ih);
    
    next_start = reiserfs_ih_get_loc(next);
    next_len = reiserfs_ih_get_len(next);
    
    if (!(buf = misc_malloc(next_len)))
	misc_die("%s: Failed to allocate %d bytes.", __FUNCTION__, next_len);

    if (!reiserfs_ih_dir(ih)) {
	memmove(buf, bh->b_data + next_start, next_len);
	memmove(bh->b_data + next_start, bh->b_data + start, len);
	memmove(bh->b_data + next_start + len, buf, next_len);
	
	reiserfs_ih_set_free(ih, reiserfs_ih_get_free(next));
    } else {
	reiserfs_deh_t *deh;
	int move;
	int i;
	
	move = reiserfs_ih_get_entries(ih) * REISERFS_DEH_SIZE;
	memmove(buf, bh->b_data + next_start, next_len);
	memmove(bh->b_data + next_start, bh->b_data + start, move);
	memmove(bh->b_data + next_start + move, buf, next_len);
	
	move /= REISERFS_DEH_SIZE;
	reiserfs_ih_set_entries(ih, reiserfs_ih_get_entries(next) + move);
	deh = (reiserfs_deh_t *)(bh->b_data + next_start);
	
	for (i = 0; i < move; i++, deh++)
	    reiserfs_deh_set_loc(deh, reiserfs_deh_get_loc(deh) + next_len);
	
	move *= REISERFS_DEH_SIZE;
	for (i = 0; i < reiserfs_ih_get_entries(next); i++, deh++)
	    reiserfs_deh_set_loc(deh, reiserfs_deh_get_loc(deh) + move);
    }
    
    misc_freemem(buf);
    
    reiserfs_ih_set_loc(ih, next_start);
    reiserfs_ih_set_len(ih, len + next_len);
    reiserfs_ih_set_len(next, 0);
    
    reiserfs_tree_delete (fs, path, 0);
}

static int cb_tree_rewrite(reiserfs_path_t *path, void *data) {
    reiserfs_ih_t *ih;
    __u64 *off;

    ih = REISERFS_PATH_IH(path);
    off = (__u64 *)data;
    
    if (reiserfs_key_get_off(&ih->ih_key) >= *off)
	return -1;

    return 0;
}

void fsck_tree_rewrite(reiserfs_filsys_t *fs, 
		       const reiserfs_key_t *start_key,
		       __u64 end_offset, __u16 flags)
{
    reiserfs_key_t key;
    saveitem_t *si;
    __u64 start;
    int format;
    
    si = NULL;
    
    start = reiserfs_key_get_off(start_key);
    start = MISC_DOWN(start, fs->fs_blocksize) + 1;
    
    end_offset = MISC_UP(end_offset - 1, fs->fs_blocksize) + 1;
    format = reiserfs_key_format(start_key);

    reiserfs_key_copy(&key, start_key);
    reiserfs_key_set_off (format, &key, start);
    
    fsck_tree_delete(&key, &si, 0, cb_tree_rewrite, &end_offset);
    
    /* Insert needed zero pointers. */
    while (start < end_offset) {
	reiserfs_key_set_off (format, &key, start);
	
	start += fsck_tree_insert_zero_ptr(fs, &key, 
			end_offset - start, flags);
    }
    
    /* Insert removed items back, over have inserted zero pointers. */
    while (si) {
	fsck_tree_insert_file(&si->si_ih, si->si_dnm_data, 0);
	si = fsck_item_free (si);
    }
}

/* Check if an overlapping exists and if it does delete all items overlapped 
   with the one pointed by @ih, insert zeroes since @ih offset and insert have
   removed items back. */
static int fsck_tree_overwrite_prep(reiserfs_filsys_t *fs,
				    const reiserfs_key_t *found_key,
				    const reiserfs_key_t *start_key, 
				    __u64 count)
{
    __u64 end;

    if (reiserfs_key_comp2(found_key, start_key))
	return 0;
    
    end = reiserfs_key_get_off (start_key) + count;
    end = MISC_UP(end - 1, fs->fs_blocksize) + 1;
    if (end <= reiserfs_key_get_off(found_key))
	return 0;

    fsck_tree_rewrite(fs, start_key, end, 1 << IH_Unreachable);
    return 1;
}

/* put unformatted node pointers from incoming item over the in-tree ones */
static int fsck_tree_overwrite_by_extent (reiserfs_ih_t * comingih, 
					  __u32 * coming_item,
					  reiserfs_path_t * path, 
					  int pos, int check)
{
    reiserfs_bh_t * bh = REISERFS_PATH_LEAF (path);
    reiserfs_ih_t * ih = REISERFS_PATH_IH (path);
    int written;
    __u32 * item_in_tree;
    int src_unfm_ptrs, dest_unfm_ptrs, to_copy, i, dirty = 0;

    item_in_tree = (__u32 *)reiserfs_item_by_ih (bh, ih) + path->pos_in_item;
    coming_item += pos;

    dest_unfm_ptrs = reiserfs_ext_count (ih) - path->pos_in_item;
    src_unfm_ptrs = reiserfs_ext_count (comingih) - pos;
  
    if (dest_unfm_ptrs >= src_unfm_ptrs) {
	/* whole coming item (comingih) fits into item in tree (ih) starting 
	   with path->pos_in_item */

	//free_sp = ih_get_free_space(0, comingih, (char *)coming_item);

	written = reiserfs_leaf_ibytes (comingih, fs->fs_blocksize) -
	    /* free_sp - */ pos * fs->fs_blocksize;
	to_copy = src_unfm_ptrs;
	if (dest_unfm_ptrs == src_unfm_ptrs)
	    reiserfs_ih_set_free(ih, 0 /* free_sp */ );
    } else {
	/* only part of coming item overlaps item in the tree */
	written = dest_unfm_ptrs * fs->fs_blocksize;
	to_copy = dest_unfm_ptrs;
	reiserfs_ih_set_free(ih, 0);
    }

    for (i = 0; i < to_copy; i ++) {
	if (d32_get (coming_item, i) != 0 && d32_get (item_in_tree, i) == 0) {
	    /* overwrite holes only by correct a pointer in the coming item
               which must be correct */
	    d32_put (item_in_tree, i, d32_get (coming_item, i));

	    if (check) {
		if (still_bad_unfm_ptr (d32_get (coming_item, i)))
		    misc_die ("%s: The unformatted block pointer "
			      "(%u) points to the bad area.",
			      __FUNCTION__, d32_get (coming_item, i));
		
		mark_block_used (d32_get (coming_item, i), 0);
	    }
	    
	    dirty ++;
	}
    }
    
    if (dirty)
	reiserfs_buffer_mkdirty (bh);
    
    return written;
}

/* write direct item to unformatted node */
/* coming item is direct */
static int fsck_tree_overwrite_by_direct(reiserfs_ih_t * comingih, 
					 char * item, 
					 reiserfs_path_t * path)
{
    reiserfs_bh_t * unbh, * bh;
    reiserfs_ih_t * ih;
    __u64 coming_len;
    __u32 unfm_ptr;
    int offset;

    bh = REISERFS_PATH_LEAF (path);
    ih = REISERFS_PATH_IH (path);

    unfm_ptr = d32_get ((__u32 *)reiserfs_item_by_ih(bh, ih), 
			path->pos_in_item);
    unbh = 0;

    if (unfm_ptr != 0 && unfm_ptr < reiserfs_sb_get_blocks (fs->fs_ondisk_sb)) {
	unbh = reiserfs_buffer_read (fs->fs_dev, unfm_ptr, bh->b_size);
	if (!is_block_used (unfm_ptr)) {
	    misc_die ("%s: block %lu, item %d, pointer %d: The pointed "
		      "block (%u) being overwritten is marked as unused.", 
		      __FUNCTION__, bh->b_blocknr, 
		      REISERFS_PATH_LEAF_POS(path),
		      path->pos_in_item, unfm_ptr);
	}
	
	if (unbh == 0)
	    unfm_ptr = 0;
    }
    
    if (unfm_ptr == 0 || 
	unfm_ptr >= reiserfs_sb_get_blocks (fs->fs_ondisk_sb)) 
    {
	if ((unbh = reiserfsck_get_new_buffer (bh->b_blocknr)) != NULL) {
	    memset (unbh->b_data, 0, unbh->b_size);
	    
	    d32_put ((__u32 *)reiserfs_item_by_ih(bh, ih), 
		     path->pos_in_item,  unbh->b_blocknr);
	    
	    reiserfs_buffer_mkdirty (bh);
	} else {
	    misc_die ("%s: Could not allocate a new block "
		      "for new data", __FUNCTION__);
	}

	/* If zero extent is overwritten with the direct, mark WAS_TAIL. */
	reiserfs_ih_mktail(ih);
    }

    coming_len = reiserfs_leaf_ibytes (comingih, fs->fs_blocksize);
    offset = (reiserfs_key_get_off (&comingih->ih_key) % bh->b_size) - 1;
    
    if (offset + coming_len > bh->b_size) {
    	misc_die ("%s: The length of the file after insertion "
		  "(offset=%lu, length=%u) will exceed the maximal "
		  "possible length.",  __FUNCTION__, 
		  (long unsigned)reiserfs_key_get_off(&comingih->ih_key),
		  (unsigned)coming_len);
    }

    memcpy (unbh->b_data + offset, item, coming_len);

    if ((path->pos_in_item == (reiserfs_ext_count (ih) - 1)) && 
	(bh->b_size - 0/*ih_free_space (ih)*/) < (offset + coming_len)) {
	reiserfs_ih_set_free (ih, bh->b_size - (offset + coming_len)) ;
	reiserfs_buffer_mkdirty (bh);
    }
    
    reiserfs_buffer_mkdirty (unbh);
    reiserfs_buffer_mkuptodate (unbh, 1);
    reiserfs_buffer_close (unbh);
    return coming_len;
}

static int fsck_tree_overwrite_file (reiserfs_ih_t * comingih, 
				     char * item,
				     reiserfs_path_t * path, 
				     int pos, int check)
{
    reiserfs_ih_t * ih = REISERFS_PATH_IH (path);
    reiserfs_key_t key;
    int written = 0;
    __u64 off;
    int ret;

    if (reiserfs_key_comp2 (ih, &(comingih->ih_key)))
	    reiserfs_panic ("%s: The file to be overwritten %K must be of "
			    "the same as the new data %K", __FUNCTION__, 
			    &ih->ih_key, &comingih->ih_key);

    if (reiserfs_ih_direct (ih)) {
	reiserfs_key_copy(&key, &comingih->ih_key);
	off = reiserfs_key_get_off(&comingih->ih_key);
	off += pos * (reiserfs_ih_direct(comingih) ? 1 : fs->fs_blocksize);
	reiserfs_key_set_off(reiserfs_key_format(&key), &key, off);

	reiserfs_tree_pathrelse(path);
	
	fsck_tree_rewrite(fs, &ih->ih_key, 
			  reiserfs_key_get_off(&ih->ih_key) + 1, 
			  1 << IH_Unreachable);

	ret = reiserfs_tree_search_position(fs, &key, path);
	if (ret == DIRECTORY_FOUND || ret == FILE_NOT_FOUND ||
	    !reiserfs_ih_ext(REISERFS_PATH_IH(path)))
	{
	    reiserfs_panic ("%s: The data %k, which are supposed to be "
			    "converted, are not found", __FUNCTION__, &key);
	}
    }
    
    if (reiserfs_ih_direct (comingih)) {
	written = fsck_tree_overwrite_by_direct(comingih, item, path);
    } else {
	written = fsck_tree_overwrite_by_extent(comingih, (__u32 *)item,
						path, pos, check);
    }

    return written;
}

/* this appends file with one unformatted node pointer (since balancing
   algorithm limitation). This pointer can be 0, or new allocated block or
   pointer from extent item that is being inserted into tree */
static int fsck_tree_append_file (reiserfs_ih_t * comingih, 
				  char * item, int pos, 
				  reiserfs_path_t * path,
				  int check)
{
    __u32 * ni;
    reiserfs_bh_t * unbh;
    int retval;
    reiserfs_ih_t * ih = REISERFS_PATH_IH (path);
    __u32 bytes_number;
    int i, count = 0;

    if (!reiserfs_ih_ext (ih))
	reiserfs_panic ("%s: Operation is not allowed for non-extent "
			"item %k", __FUNCTION__, &ih->ih_key);

    if (reiserfs_ih_direct (comingih)) {
	unsigned int coming_len = 
		reiserfs_leaf_ibytes (comingih, fs->fs_blocksize);

	reiserfs_ih_mktail (ih);
        reiserfs_buffer_mkdirty (REISERFS_PATH_LEAF(path));
	
	if (reiserfs_key_get_off (&comingih->ih_key) < 
	    reiserfs_key_get_off (&ih->ih_key) + 
	    fs->fs_blocksize * reiserfs_ext_count (ih)) 
	{
	    /* direct item fits to free space of extent item */
	    reiserfs_panic("%s: inserting [%H], pos (%d) into [%H], "
			   "should not get here.\n", __FUNCTION__, 
			   comingih, pos, ih);
	}

	unbh = reiserfsck_get_new_buffer (REISERFS_PATH_LEAF (path)->b_blocknr);
	memset (unbh->b_data, 0, unbh->b_size);
	memcpy (unbh->b_data + reiserfs_key_get_off (&comingih->ih_key) % 
		unbh->b_size - 1, item, coming_len);


	reiserfs_buffer_mkdirty (unbh);
	reiserfs_buffer_mkuptodate (unbh, 1);

	ni = misc_getmem (REISERFS_EXT_SIZE);
	d32_put (ni, 0, unbh->b_blocknr);
	count = 1;
	
	reiserfs_buffer_close (unbh);
	retval = coming_len;
    } else {
	/* coming item is extent item */

	bytes_number = reiserfs_leaf_ibytes (ih, fs->fs_blocksize);
	if (reiserfs_key_get_off (&comingih->ih_key) + 
	    pos * fs->fs_blocksize != 
	    reiserfs_key_get_off (&ih->ih_key) + bytes_number)
	{
	    reiserfs_panic ("%s: file %K: Cannot append extent pointers "
			    "of the offset (%llu) at the position %llu\n", 
			    __FUNCTION__, &comingih->ih_key, 
			    reiserfs_key_get_off (&comingih->ih_key) + 
			    pos * fs->fs_blocksize, 
			    reiserfs_key_get_off (&ih->ih_key) + 
			    bytes_number);
	}
	
	/* take unformatted pointer from an extent item */
	count = reiserfs_ext_count(comingih) - pos;
	ni = misc_getmem (count * REISERFS_EXT_SIZE);
	memcpy (ni, (item + pos * REISERFS_EXT_SIZE), count * REISERFS_EXT_SIZE);
	
	if (check) {
	    for (i = 0; i < count; i++ ) {
		if (still_bad_unfm_ptr (d32_get (ni, i)))
		    misc_die ("%s: Trying to insert a pointer to illegal "
			      "block (%u)", __FUNCTION__, d32_get (ni, i));
		
		mark_block_used (d32_get (ni, i), 0);
	    }
	}

	retval = fs->fs_blocksize * count;
    }

    reiserfs_tree_insert_unit (fs, path, (const char *)ni, count * REISERFS_EXT_SIZE);
    misc_freemem (ni);
    return retval;
}

static int fsck_tree_insert_file (reiserfs_ih_t * ih, 
				  char * item, 
				  int check)
{
    long long int retval, written;
    const reiserfs_key_t *p_key;
    const reiserfs_ih_t *p_ih;
    reiserfs_path_t path;
    reiserfs_key_t key;
    int count, pos;

    /* Check if the SD item exists */
    if (check && fsck_tree_insert_prep(fs, ih, 0))
	return 0;
    
    pos = 0;
    count = reiserfs_leaf_ibytes (ih, fs->fs_blocksize);
    reiserfs_key_copy (&key, &(ih->ih_key));

    while (count) {
	if (count < 0) {
	    reiserfs_panic ("%s: We wrote into the file %K more "
			    "bytes than needed - count (%d) < 0.", 
			    __FUNCTION__, &key, count);
	}

	retval = reiserfs_tree_search_position (fs, &key, &path);
	
	if (retval == DIRECTORY_FOUND) {
	    reiserfs_panic ("The directory was found at the place of the file "
			    "we are going to insert the item %k into", &key);
	}

	written = 0;

	if (retval == POSITION_FOUND) {
	    written = fsck_tree_overwrite_file (ih, item, &path, 
						pos, check);
	}

	if (retval == POSITION_NOT_FOUND) {
	    if (REISERFS_PATH_LEAF_POS (&path) >= 
		reiserfs_node_items (REISERFS_PATH_LEAF(&path)))
	    {
		p_key = reiserfs_tree_next_key(&path, fs);
	    } else {
		p_key = &REISERFS_PATH_IH(&path)->ih_key;
	    }
	    
	    if (path.pos_in_item == 0) {
		/* There is nothing of this file before the found item. 
		   Check the overlapping with the found/next item. */
		if (fsck_tree_overwrite_prep(fs, p_key, &key, count)) {
		    reiserfs_tree_pathrelse (&path);
		    continue;
		}
		
		/* No overlapping, insert a new item. */
		written = fsck_tree_create_item(fs, &path, ih, 
						item, pos, check);
	    } else {
		const reiserfs_key_t *next = reiserfs_tree_next_key(&path, fs);
		
		/* Check the overlapping with the next item. */
		if (fsck_tree_overwrite_prep(fs, next, &key, count)) {
		    reiserfs_tree_pathrelse (&path);
		    continue;
		}

		/* No overlapping, convert the current into the extent and
		   insert a new / append to the existent. */
		p_ih = REISERFS_PATH_IH(&path);
		written = must_there_be_a_hole (p_ih, &key);
		
		if (written <= 0 && reiserfs_key_direct(p_key)) {
		    reiserfs_tree_pathrelse(&path);
		    fsck_tree_rewrite(fs, &p_ih->ih_key, 
				      reiserfs_key_get_off(&key) + 1, 
				      1 << IH_Unreachable);
		    continue;
                }
		
		if (written > 0) {
		    REISERFS_PATH_LEAF_POS (&path) ++;
		    path.pos_in_item = 0;
		    
		    written = fsck_tree_create_item(fs, &path, ih, 
						    item, pos, check);
		} else {
		    written = fsck_tree_append_file (ih, item, pos,
                                                     &path, check);
		}
	    }
	}
	
	if (retval == FILE_NOT_FOUND) {
	    written = fsck_tree_create_item(fs, &path, ih, 
					    item, pos, check);
	}
	
	reiserfs_tree_pathrelse (&path);
	count -= written;
	pos += written / (reiserfs_ih_direct(ih) ? 1 : fs->fs_blocksize);
	
	reiserfs_key_set_off (reiserfs_key_format (&key), &key, 
			      reiserfs_key_get_off (&key) + written);
    }


    /* This is a test for writing into the file. If not sure that file data 
       are consistent after fsck_tree_insert_file - uncomment this clause: */

/*    if (!check && are_file_items_correct (&ih->ih_key,
    	(format == KEY_FORMAT_UNDEFINED) ?	
	reiserfs_ih_get_format (ih) : format,
    	&size, &blocks, 0, symlink, 0) == 0)
        reiserfs_panic ("%s: item was not inserted properly\n", __FUNCTION__);*/

    return reiserfs_leaf_ibytes (ih, fs->fs_blocksize);
}


void fsck_tree_insert_item (reiserfs_ih_t * ih,
			    char * item, int check)
{
    if (reiserfs_key_get_did (&ih->ih_key) == 
	reiserfs_key_get_oid (&ih->ih_key))
    {
	reiserfs_panic ("%s: The item being inserted has "
			"the bad key %H", __FUNCTION__, ih);
    }
	
    if (reiserfs_ih_stat (ih)) {
	fsck_tree_insert_stat (ih, item, check);
    } else if (reiserfs_ih_dir (ih)) {
	fsck_tree_insert_entry (ih, item, check);
    } else {
	fsck_tree_insert_file (ih, item, check);
    }
}


void fsck_tree_trav (reiserfs_filsys_t * fs, 
		     path_func_t action1,
		     path_func_t action2, 
		     int depth)
{
    reiserfs_path_t path;
    reiserfs_bh_t *bh;
    int total[REISERFS_PATH_MAX] = {0,};
    unsigned long limit, done;
    unsigned long block;
    int problem;
    int pos;

    memset(&path, 0, sizeof(path));
    path.path_length = REISERFS_PATH_OFFINIT;
    
    done = 0;
    limit = 1;
    block = reiserfs_sb_get_root (fs->fs_ondisk_sb);

    if (reiserfs_fs_block(fs, block) != BT_UNKNOWN) {
	misc_die ("\nBad root block %lu. (--rebuild-tree "
		  "did not complete)\n", block);
    }

    while ( 1 ) {
	problem = 0;

        if (REISERFS_PATH_LEAF(&path))
            misc_die ("%s: empty slot expected.\n", __FUNCTION__);

	if (fs->fs_badblocks_bm && 
	    reiserfs_bitmap_test_bit (fs->fs_badblocks_bm, block)) 
	{
	    fsck_log ("%s: block %lu specified in badblock list found in "
		      "tree, whole subtree skipped\n", __FUNCTION__, block);
	    
	    fsck_data (fs)->check.bad_nodes++;
	    one_more_corruption (fs, FATAL);

	    if (path.path_length == REISERFS_PATH_OFFINIT) {
		reiserfs_buffer_close (REISERFS_PATH_LEAF(&path));
		REISERFS_PATH_LEAF(&path) = 0;
		break;
	    }
	    
	    problem = 1;		
	} else {
	    REISERFS_PATH_LEAF(&path) = 
		    reiserfs_buffer_read (fs->fs_dev, block, fs->fs_blocksize);
	    
	    if (REISERFS_PATH_LEAF(&path) == 0)
		/* FIXME: handle case when read failed */
		misc_die ("%s: unable to read %lu block on device 0x%x\n",
			  __FUNCTION__, block, fs->fs_dev);

	    if (action1)
		if ((problem = action1 (fs, &path))) {
		    fsck_log (" the problem in the internal node occured "
			      "(%lu), whole subtree is skipped\n", block);
		    
		    fsck_check_stat(fs)->bad_nodes++;
 
		    if (path.path_length == REISERFS_PATH_OFFINIT) {
			reiserfs_buffer_close (REISERFS_PATH_LEAF(&path));
			REISERFS_PATH_LEAF(&path) = 0;
			break;
		    }
		}
	}

	/* Time to stop. */
	if (path.path_length - REISERFS_PATH_OFFINIT == depth)
	    problem ++;

        if (problem || reiserfs_leaf_head(REISERFS_PATH_LEAF(&path))) {
	    if ((path.path_length > REISERFS_PATH_OFFINIT) && 
		!fsck_quiet(fs)) 
	    {
		util_misc_progress (fsck_progress_file (fs), 
				    &done, limit, 1, 2);
	    }
	    
            if (!problem && action2)
                action2 (fs, &path);

	    reiserfs_buffer_close (REISERFS_PATH_LEAF(&path));

            while ((path.path_length > REISERFS_PATH_OFFINIT) && 
		   (REISERFS_PATH_POS(&path, path.path_length - 1) == 
		    total[path.path_length - 1] - 1 || problem)) 
	    {
                problem = 0;
                REISERFS_PATH_LEAF(&path) = 0;
                path.path_length --;
                reiserfs_buffer_close (REISERFS_PATH_LEAF(&path));
		limit /= total[path.path_length];
		done /= total[path.path_length];
	    }

	    REISERFS_PATH_LEAF(&path) = 0;
	    
    	    if (path.path_length == REISERFS_PATH_OFFINIT)
		break;

	    bh = REISERFS_PATH_BUFFER(&path, path.path_length - 1);
            REISERFS_PATH_POS(&path, path.path_length - 1) ++;
	    
	    pos = REISERFS_PATH_POS(&path, path.path_length - 1);
            block = reiserfs_dc_get_nr(reiserfs_int_at(bh, pos));
	    
            continue;
	}
	
        total[path.path_length] = 
		reiserfs_node_items (REISERFS_PATH_LEAF(&path)) + 1;
	
	limit *= total[path.path_length];
	done = done * total[path.path_length];
	
        REISERFS_PATH_LEAF_POS(&path) = 0;
        block = reiserfs_dc_get_nr (reiserfs_int_at (REISERFS_PATH_LEAF(&path), 0)); 
	path.path_length ++;
    }
}
