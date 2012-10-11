/*
 * Copyright 1996-2002 Hans Reiser
 */
#include "fsck.h"

/*
void get_next_key (struct path * path, int i, struct key * key)
{
    struct buffer_head * bh = PATH_PLAST_BUFFER (path);
    struct key * rkey;


    if (i < B_NR_ITEMS (bh) - 1) {
	// next item is in this block
	copy_key (key, B_N_PKEY (bh, i + 1));
	return;
    }

    rkey = uget_rkey (path);
    if (rkey) {
	// got next item key from right delimiting key
	copy_key (key, rkey);
    } else {
	// there is no next item
	memset (key, 0xff, KEY_SIZE);
    }
}
*/

void pass_4_check_unaccessed_items (void)
{
    struct key key;
    struct path path;
    int i;
    struct buffer_head * bh;
    struct item_head * ih;
    unsigned long items;
    struct key * rdkey;

    path.path_length = ILLEGAL_PATH_ELEMENT_OFFSET;
    key = root_dir_key;
    
    fsck_progress ("Pass 4 - ");
    items = 0;

    while (reiserfs_search_by_key_4 (fs, &key, &path) == ITEM_FOUND) {
	bh = PATH_PLAST_BUFFER (&path);

	/* print ~ how many leaves were scanned and how fast it was */
	if (!fsck_quiet (fs))
	    print_how_fast (items++, 0, 50, 0);

	for (i = get_item_pos (&path), ih = get_ih (&path); i < B_NR_ITEMS (bh); i ++, ih ++) {


	    if (!is_item_reachable (ih)) {
		PATH_LAST_POSITION (&path) = i;
		rdkey = get_next_key_2 (&path);
		if (rdkey)
		    key = *rdkey;
		else
		    memset (&key, 0xff, KEY_SIZE);	
		
		pass_4_stat (fs)->deleted_items ++;
	
		reiserfsck_delete_item (&path, 0);

		goto cont;
	    }
	}
	PATH_LAST_POSITION(&path) = i - 1;
	rdkey = reiserfs_next_key (&path);
	if (rdkey)
	    key = *rdkey;
	else
	    memset (&key, 0xff, KEY_SIZE);	
	
	pathrelse (&path);

    cont:
    }

    pathrelse (&path);

    fsck_progress ("done\n");
    stage_report (4, fs);

    /* after pass 4 */

    /* put bitmap on place */
    reiserfs_bitmap_copy (fs->fs_bitmap2, fsck_new_bitmap (fs));

    /* update super block */
    set_sb_free_blocks (fs->fs_ondisk_sb, reiserfs_bitmap_zeros (fsck_new_bitmap (fs)));
    mark_buffer_dirty (fs->fs_super_bh);

    /* write all dirty blocks */
    fsck_progress ("Flushing..");
    fs->fs_dirt = 1;
    flush_objectid_map (proper_id_map (fs), fs);
    reiserfs_flush_to_ondisk_bitmap (fs->fs_bitmap2, fs);
    reiserfs_flush (fs);
    fsck_progress ("done\n");
    return;
}
