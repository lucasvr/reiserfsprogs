/*
 * Copyright 1996-2004 by Hans Reiser, licensing governed by 
 * reiserfsprogs/README
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "reiserfs/libreiserfs.h"

/* returns 1 if buf looks like an internal node, 0 otherwise */
int reiserfs_internal_correct (reiserfs_bh_t *bh) {
    reiserfs_node_head_t * blkh;
    unsigned int nr;
    int used_space;

    blkh = (reiserfs_node_head_t *)bh->b_data;

    if (!reiserfs_int_head (bh))
	return 0;
    
    nr = reiserfs_nh_get_items (blkh);
    if (nr > (bh->b_size - REISERFS_NODEH_SIZE - REISERFS_DC_SIZE) / 
	(REISERFS_KEY_SIZE + REISERFS_DC_SIZE))
    {
	/* for internal which is not root we might check min number of keys */
	return 0;
    }

    used_space = REISERFS_NODEH_SIZE + REISERFS_KEY_SIZE * nr + 
	    REISERFS_DC_SIZE * (nr + 1);
    
    if (used_space != bh->b_size - reiserfs_nh_get_free (blkh))
	return 0;

    // one may imagine much more checks
    return 1;
}

/* this prints internal nodes (4 keys/items in line) (dc_number,
   dc_size)[k_dirid, k_objectid, k_offset, k_uniqueness](dc_number,
   dc_size)...*/
int reiserfs_internal_print (FILE * fp, 
			     reiserfs_bh_t * bh, 
			     int first, int last)
{
    reiserfs_key_t * key;
    reiserfs_dc_t * dc;
    int i;
    int from, to;

    if (!reiserfs_int_head (bh))
	return 1;

    if (first == -1) {
	from = 0;
	to = reiserfs_node_items (bh);
    } else {
	from = first;
	to = last < reiserfs_node_items (bh) ? last : reiserfs_node_items (bh);
    }

    reiserfs_warning (fp, "INTERNAL NODE (%ld) contains %b\n",  
		      bh->b_blocknr, bh);

    dc = reiserfs_int_at (bh, from);
    reiserfs_warning (fp, "PTR %d: %y ", from, dc);

    for (i = from, key = reiserfs_int_key_at (bh, from), dc ++; 
	 i < to; i ++, key ++, dc ++) 
    {
	reiserfs_warning (fp, "KEY %d: %20k PTR %d: %20y ", 
			  i, key, i + 1, dc);
	
	if (i && i % 4 == 0)
	    reiserfs_warning (fp, "\n");
    }

    reiserfs_warning (fp, "\n");
    return 0;
}

/* internal node bh must point to block */
int reiserfs_internal_get_pos (reiserfs_bh_t * bh, 
			       unsigned long block) 
{
    int i;

    for (i = 0; i <= reiserfs_node_items (bh); i ++) {
	if (reiserfs_dc_get_nr (reiserfs_int_at (bh, i)) == block)
	    return i;
    }
    
    misc_die ("An internal pointer to the block (%lu) cannot be "
	      "found in the node (%lu)", block, bh->b_blocknr);
    
    return 0;
}
