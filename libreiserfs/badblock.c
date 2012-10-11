/*
 *  Copyright 2000-2004 by Hans Reiser, licensing governed by 
 *  reiserfsprogs/README
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "reiserfs/libreiserfs.h"
#include "misc/unaligned.h"
#include "misc/misc.h"

reiserfs_key_t badblock_key = {REISERFS_BAD_DID, REISERFS_BAD_OID, {{0, 0},}};

void reiserfs_badblock_traverse(reiserfs_filsys_t * fs, 
				badblock_func_t action, 
				void *data) 
{
    reiserfs_path_t badblock_path;
    reiserfs_key_t rd_key = badblock_key;
    const reiserfs_key_t *key;
    
    badblock_path.path_length = REISERFS_PATH_OFFILL;
    reiserfs_key_set_sec (KEY_FORMAT_2, &badblock_key, 1, TYPE_EXTENT);
    
    while (1) {
	    if (reiserfs_tree_search_item (fs, &rd_key, &badblock_path) == IO_ERROR) {
		fprintf (stderr, "%s: Some problems while searching by the key "
			 "occured. Probably due to tree corruptions.\n",
			 __FUNCTION__);
		reiserfs_tree_pathrelse (&badblock_path);
		break;
	    }
		    

	    if (reiserfs_nh_get_items (NODE_HEAD (REISERFS_PATH_LEAF (&badblock_path))) <= 
		REISERFS_PATH_LEAF_POS (&badblock_path)) 
	    {
		reiserfs_tree_pathrelse (&badblock_path);
		break;
	    }

	    rd_key = REISERFS_PATH_IH(&badblock_path)->ih_key;

	    if (reiserfs_key_get_did(&rd_key) != REISERFS_BAD_DID || 
		reiserfs_key_get_oid(&rd_key) != REISERFS_BAD_OID ||
		!reiserfs_key_ext(&rd_key)) 
	    {
		reiserfs_tree_pathrelse (&badblock_path);
		break;
	    }
	
	    if ((key = reiserfs_tree_next_key(&badblock_path, fs)))
		rd_key = *key;
	    else
	        memset(&rd_key, 0, sizeof(rd_key));
	    
	    action(fs, &badblock_path, data);

	    if (reiserfs_key_get_did(&rd_key) == 0)
		break;
    }
}

static void callback_badblock_rm(reiserfs_filsys_t *fs,
				 reiserfs_path_t *badblock_path, 
				 void *data) 
{
	reiserfs_tb_t tb;
	reiserfs_ih_t * tmp_ih;

	tmp_ih = REISERFS_PATH_IH(badblock_path);
	memset (REISERFS_PATH_ITEM (badblock_path), 
		0, reiserfs_ih_get_len (tmp_ih));

	reiserfs_tb_init (&tb, fs, badblock_path, 
			-(REISERFS_IH_SIZE + 
			  reiserfs_ih_get_len(REISERFS_PATH_IH(badblock_path))));

	if (reiserfs_fix_nodes (M_DELETE, &tb, 0) != CARRY_ON)
		misc_die ("%s: reiserfs_fix_nodes failed", __FUNCTION__);

	reiserfs_tb_balance (&tb, 0, 0, M_DELETE, 0/*zero num*/);
}

void reiserfs_badblock_extract(reiserfs_filsys_t *fs, 
			       reiserfs_path_t *badblock_path, 
			       void *data) 
{
	reiserfs_ih_t *tmp_ih;
	__u32 *ind_item;
	__u32 i;

	if (!fs->fs_badblocks_bm) {
		fs->fs_badblocks_bm = 
			reiserfs_bitmap_create(reiserfs_sb_get_blocks(fs->fs_ondisk_sb));
    
		reiserfs_bitmap_zero (fs->fs_badblocks_bm);
	}
	
	tmp_ih = REISERFS_PATH_IH(badblock_path);
	ind_item = (__u32 *)REISERFS_PATH_ITEM(badblock_path);

	for (i = 0; i < reiserfs_ext_count(tmp_ih); i++) {
		reiserfs_bitmap_set_bit(fs->fs_badblocks_bm, 
					d32_get(ind_item, i));
	}
	
	reiserfs_tree_pathrelse (badblock_path);
}

static int reiserfs_alloc_blocks (reiserfs_filsys_t * fs,
				  unsigned long *blknr,
				  unsigned long start,
				  int count)
{
    int i;
    
    for (i = 0; i < count; i ++) {
	blknr[i] = 0;
	if (reiserfs_bitmap_find_zero_bit(fs->fs_bitmap2, blknr + i))
	    misc_die ("%s: failed to allocate a block.", __FUNCTION__);
	
	reiserfs_bitmap_set_bit(fs->fs_bitmap2, blknr[i]);
    }
    
    return CARRY_ON;
}

void reiserfs_badblock_flush (reiserfs_filsys_t * fs, int replace) {
    reiserfs_tb_t tb;
    reiserfs_path_t badblock_path;
    reiserfs_ih_t badblock_ih;
    __u32 ni;

    __u64 offset;
    __u32 i, j;

    if (fs->fs_badblocks_bm == NULL)
    	return;

    /* delete all items with badblock_key */
    if (replace)
	reiserfs_badblock_traverse(fs, callback_badblock_rm, NULL);

    memset(&badblock_ih, 0, sizeof(badblock_ih));
    reiserfs_ih_set_format (&badblock_ih, KEY_FORMAT_2);
    reiserfs_ih_set_len (&badblock_ih, REISERFS_EXT_SIZE);
    reiserfs_ih_set_free (&badblock_ih, 0);
    reiserfs_ih_set_loc (&badblock_ih, 0);
    reiserfs_key_set_did (&badblock_ih.ih_key, REISERFS_BAD_DID);
    reiserfs_key_set_oid (&badblock_ih.ih_key, REISERFS_BAD_OID);
    reiserfs_key_set_type (KEY_FORMAT_2, &badblock_ih.ih_key, TYPE_EXTENT);

    j = 0;
    
    fs->block_allocator = reiserfs_alloc_blocks;
    
    /* insert all badblock pointers */
    for (i = 0; i < fs->fs_badblocks_bm->bm_bit_size; i++) {
        int retval;

	if (!reiserfs_bitmap_test_bit (fs->fs_badblocks_bm, i))	
	    continue;
	
	offset = j * fs->fs_blocksize + 1;
	reiserfs_key_set_off (KEY_FORMAT_2, &badblock_ih.ih_key, offset);
	ni = cpu_to_le32 (i);
	
	retval = reiserfs_tree_search_position (fs, &badblock_ih.ih_key, 
					      &badblock_path);

	switch (retval) {
	case (FILE_NOT_FOUND):
		reiserfs_tb_init (&tb, fs, &badblock_path, 
				  REISERFS_IH_SIZE + reiserfs_ih_get_len(&badblock_ih));
		
		if (reiserfs_fix_nodes (M_INSERT, &tb, &badblock_ih) != CARRY_ON)
		    misc_die ("reiserfs_badblock_flush: reiserfs_fix_nodes failed");
		
		reiserfs_tb_balance (&tb, &badblock_ih, (void *)&ni , M_INSERT, 0);

		break;
	
	case (POSITION_NOT_FOUND):
	case (POSITION_FOUND):
		/* Insert the new item to the found position. */
	
		reiserfs_tb_init (&tb, fs, &badblock_path, REISERFS_EXT_SIZE);
		
		if (reiserfs_fix_nodes (M_PASTE, &tb, 0) != CARRY_ON)
		    misc_die ("reiserfs_badblock_flush: reiserfs_fix_nodes failed");

		reiserfs_tb_balance (&tb, 0, (const char *)&ni, M_PASTE, 0);
		break;
	}
	
	j++;
    }
}

