/*
 * Copyright 1996-2004 by Hans Reiser, licensing governed by 
 * reiserfsprogs/README
 *
 * The code to check the leaf structure.
 * Leaf is not supposed to be in the tree, it is rather a single one.
 *
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "fsck.h"
#include "misc/unaligned.h"
#include "misc/malloc.h"
#include <assert.h>

/* 1 if some of fields in the block head of bh look bad */
int fsck_leaf_check_header(reiserfs_filsys_t * fs, reiserfs_bh_t * bh) {
    reiserfs_node_head_t * blkh;
    int free_space, counted;

    blkh = NODE_HEAD (bh);
    
    if (!reiserfs_leaf_head(bh)) {
	/* We should not get here on rebuild. */
	fsck_log ("block %lu: The block does not look like a leaf.\n", 
		  bh->b_blocknr);
	
        one_more_corruption (fs, FATAL);
	return 1;
    }

    if (reiserfs_nh_get_items (blkh) == 0)
	return 0;
    
    counted = reiserfs_leaf_count_items(bh);

    if (counted < reiserfs_nh_get_items (blkh)) {
	fsck_log ("block %lu: The number of items (%lu) is incorrect, "
		  "should be (%lu)", bh->b_blocknr, 
		  reiserfs_nh_get_items(blkh), counted);
	
	if (fsck_mode(fs) == FSCK_REBUILD) {
	    reiserfs_nh_set_items(blkh, counted);	    
	    fsck_log (" - corrected\n");
	    reiserfs_buffer_mkdirty (bh);
	} else {
	    fsck_log ("\n");
	    one_more_corruption (fs, FATAL);
	    return 1;
	}
    }
    
    free_space = reiserfs_leaf_free_count(bh);
    if (reiserfs_nh_get_free (blkh) != free_space) {
	fsck_log ("block %lu: The free space (%lu) is incorrect, should "
		  "be (%lu)", bh->b_blocknr, reiserfs_nh_get_free (blkh), 
		  free_space);
	
	if (fsck_mode(fs) != FSCK_CHECK && fsck_mode(fs) != FSCK_AUTO) {
	    reiserfs_nh_set_free (blkh, free_space);	    
	    fsck_log (" - corrected\n");
	    reiserfs_buffer_mkdirty (bh);
	} else {
	    fsck_log ("\n");
	    one_more_corruption (fs, FIXABLE);
	    return 1;
	}
    }

    return 0;
}

static int is_bad_sd (reiserfs_ih_t * ih, char * item)
{
    if (reiserfs_key_get_off1 (&ih->ih_key) || 
	reiserfs_key_get_uni (&ih->ih_key)) 
    {
	fsck_log ("vpf-10610: StatData item %k has non "
		  "zero offset found.\n", &ih->ih_key);
	return 1;
    }

    if (reiserfs_ih_get_len (ih) == REISERFS_SD_SIZE_V1) {
	/* looks like old stat data */
	if (reiserfs_ih_get_format (ih) != KEY_FORMAT_1) {
	    fsck_log ("vpf-10620: StatData item %k has wrong "
		      "format.\n", &ih->ih_key);
	}
    }

    return 0;
}


static int is_bad_directory (reiserfs_ih_t * ih, 
			     char * item, int dev,
			     int blocksize)
{
    int i;
    char * name;
    int namelen, entrylen;
    reiserfs_deh_t * deh = (reiserfs_deh_t *)item;
    __u32 prev_offset = 0;
    __u16 prev_location = reiserfs_ih_get_len (ih);
    int min_entry_size = 1;/* we have no way to understand whether the
                              filesystem wes created in 3.6 format or
                              converted to it. So, we assume that minimal 
			      name length is 1 */

    if (reiserfs_ih_get_len (ih) / (REISERFS_DEH_SIZE + min_entry_size) < 
	reiserfs_ih_get_entries (ih))
    {
	/* entry count is too big */
	return 1;
    }
    
    for (i = 0; i < reiserfs_ih_get_entries (ih); i ++, deh ++) {
	entrylen = reiserfs_direntry_entry_len(ih, deh, i);
	if (entrylen > REISERFS_NAME_MAX)
	    return 1;
	
	if (reiserfs_deh_get_off (deh) <= prev_offset)
	    return 1;
	
	prev_offset = reiserfs_deh_get_off (deh);

	if (reiserfs_deh_get_loc(deh) + entrylen != prev_location)
	    return 1;
	
	prev_location = reiserfs_deh_get_loc (deh);

	namelen = reiserfs_direntry_name_len (ih, deh, i);
	name = reiserfs_deh_name (deh, i);
	if (!reiserfs_hash_correct (&fs->hash, name, namelen, 
				    reiserfs_deh_get_off (deh)))
	{
	    return 1;
	}
    }
    return 0;
}


/* change incorrect block adresses by 0. 
   Do not consider such item as incorrect */
static int is_bad_extent (reiserfs_ih_t * ih, 
			  char * item, int dev, 
			  int blocksize)
{
    unsigned long blocks;
    unsigned int i;
    int bad = 0;

    if (reiserfs_ih_get_len(ih) % REISERFS_EXT_SIZE) {
	fsck_log ("is_bad_extent: extent item of %H of invalid length\n", ih);
	return 1;
    }

    blocks = reiserfs_sb_get_blocks (fs->fs_ondisk_sb);
  
    for (i = 0; i < reiserfs_ext_count (ih); i ++) {
	__u32 * ind = (__u32 *)item;

	if (d32_get (ind, i) >= blocks) {
	    bad ++;
	    fsck_log ("is_bad_extent: %d-th pointer of item %H "
		      "looks bad (%lu)\n", i, ih, d32_get (ind, i));
	    continue;
	}
    }
    return bad;
}


/* this is used by check.c: fsck_leaf_check */
int fsck_leaf_item_check (reiserfs_bh_t * bh, 
			  reiserfs_ih_t * ih, 
			  char * item)
{
    int blocksize, dev;

    blocksize = bh->b_size;
    dev = bh->b_dev;

    // FIXME: refuse transparently bad items
    if (reiserfs_key_get_did (&ih->ih_key) == 
	reiserfs_key_get_oid (&ih->ih_key))
    {
	return 1;
    }
    
    if (!reiserfs_key_get_did (&ih->ih_key) || 
	!reiserfs_key_get_oid (&ih->ih_key))
    {
	return 1;
    }

    if (reiserfs_ih_stat(ih))
	return is_bad_sd (ih, item);

    if (reiserfs_ih_dir (ih))
	return is_bad_directory (ih, item, dev, blocksize);

    if (reiserfs_ih_ext (ih))
	return is_bad_extent (ih, item, dev, blocksize);

    if (reiserfs_ih_direct (ih))
	return 0;

    return 1;
}

/* 1 if i-th and (i-1)-th items can not be neighbors in a leaf */
int fsck_leaf_check_neigh (reiserfs_filsys_t * fs, reiserfs_bh_t * bh, int pos) {
    reiserfs_ih_t * ih;

    ih = reiserfs_ih_at (bh, pos);

    if (reiserfs_key_comp (&((ih - 1)->ih_key), &ih->ih_key) != -1) {
	if (fsck_mode (fs) != FSCK_REBUILD)
	    one_more_corruption (fs, FATAL);	
	return 1;
    }

    if (reiserfs_ih_stat (ih))
	/* left item must be of another object */
	if (reiserfs_key_comp2 (&((ih - 1)->ih_key), &ih->ih_key) != -1) {
	    if (fsck_mode (fs) != FSCK_REBUILD)
		one_more_corruption (fs, FATAL);	
	    return 1;
	}

    if (reiserfs_key_get_oid (&ih->ih_key) == REISERFS_BAD_OID) {
	/* BAD BLOCK LIST SUPPORT. */
	if (reiserfs_key_get_did(&ih->ih_key) == REISERFS_BAD_DID && 
	    reiserfs_ih_ext(ih) && 
	    reiserfs_key_comp2(&((ih - 1)->ih_key), &ih->ih_key))
	{
		return 0;
	}
    } else {
	/* Safe link support. */
	if (reiserfs_key_get_did (&ih->ih_key) == (__u32)-1) {
	    if (reiserfs_key_comp2 (&((ih - 1)->ih_key), &ih->ih_key) == 0) {
		if (reiserfs_ih_ext (ih - 1) && reiserfs_ih_direct(ih))
		    return 0; /* safe link */
		/* they do not look like safe links */
	    } else {
		if (reiserfs_ih_ext (ih) || reiserfs_ih_direct(ih))
		    return 0; /* safe link */
		/* it does not look like safe link */
	    }
	}
    }
    
    if (reiserfs_ih_direct(ih)) {
	/* left item must be extent or stat data item of the same file */
	if (reiserfs_key_comp2 (&((ih - 1)->ih_key), &ih->ih_key)) {
	    if (fsck_mode (fs) != FSCK_REBUILD)
		one_more_corruption (fs, FATAL);
	    
	    return 1;
	}

	if (!((reiserfs_ih_stat (ih - 1) && 
	       reiserfs_key_get_off (&ih->ih_key) == 1) ||
	      (reiserfs_ih_ext (ih - 1) && 
	       reiserfs_key_get_off (&(ih - 1)->ih_key) + 
	       reiserfs_leaf_ibytes (ih-1, bh->b_size) == 
	       reiserfs_key_get_off (&ih->ih_key))))
	{
	    if (fsck_mode (fs) != FSCK_REBUILD)
		one_more_corruption (fs, FATAL);
	    
	    return 1;
	}
    }

    if (reiserfs_ih_ext (ih) || reiserfs_ih_dir (ih)) {
	/* left item must be stat data of the same object */
	if (reiserfs_key_comp2 (&((ih - 1)->ih_key), &ih->ih_key) ||
	    !reiserfs_ih_stat (ih - 1)) {
	    if (fsck_mode (fs) != FSCK_REBUILD)
		one_more_corruption (fs, FATAL);
	    return 1;
	}
    }

    return 0;
}

int fsck_leaf_check (reiserfs_bh_t * bh) {
    int i;
    reiserfs_ih_t * ih;
    int bad = 0;

    assert (reiserfs_leaf_head (bh));

    for (i = 0, ih = reiserfs_ih_at (bh,  0); 
	 i < reiserfs_node_items (bh); i ++, ih ++) 
    {
	if (fsck_leaf_item_check (bh, ih, reiserfs_item_by_ih (bh, ih))) {
	    fsck_log ("%s: block %lu, item %d: The corrupted item "
		      "found (%H)\n", __FUNCTION__, bh->b_blocknr, i, ih);
	    bad = 1;
	    continue;
	}

	if (i && fsck_leaf_check_neigh (fs, bh, i)) {
	    fsck_log ("%s: block %lu items %d and %d: Wrong order of "
		      "items:\n\t%H\n\t%H\n", __FUNCTION__, bh->b_blocknr,
		      i - 1, i, ih - 1, ih);
	    bad = 1;
	}
    }

    return bad;
}

/* this item is in tree. All unformatted pointer are correct. Do not
   check them */
void fsck_item_save(reiserfs_path_t * path, saveitem_t ** head) {
    reiserfs_ih_t * ih = REISERFS_PATH_IH(path);
    saveitem_t *si, *cur;

    si = misc_getmem (sizeof (*si));
    si->si_dnm_data = misc_getmem (reiserfs_ih_get_len(ih));
    
    /*si->si_blocknr = blocknr;*/
    memcpy (&(si->si_ih), ih, REISERFS_IH_SIZE);
    memcpy (si->si_dnm_data, REISERFS_PATH_ITEM(path), reiserfs_ih_get_len(ih));

    if (*head == 0) {
	*head = si;
    } else {
	cur = *head;
	while (cur->si_next)
	    cur = cur->si_next;
	cur->si_next = si;
    }
    
    return;
}

saveitem_t * fsck_item_free(saveitem_t * si) {
    saveitem_t * tmp = si->si_next;
    
    misc_freemem (si->si_dnm_data);
    misc_freemem (si);
    
    return tmp;
}


