/*
 *  Copyright 2000-2004 by Hans Reiser, licensing governed by 
 *  reiserfsprogs/README
 */

#ifndef REISERFS_INTERNAL_H
#define REISERFS_INTERNAL_H

/*
 * Picture represents an internal node of the reiserfs tree
 *  ______________________________________________________
 * |      |  Array of     |  Array of         |  Free     |
 * |block |    keys       |  pointers         | space     |
 * | head |      N        |      N+1          |           |
 * |______|_______________|___________________|___________|
 */


/* Disk child pointer: The pointer from an internal node of the tree
   to a node that is on disk. */
struct reiserfs_dc {
    __u32 dc2_block_number;              /* Disk child's block number. */
    __u16 dc2_size;		            /* Disk child's used space.   */
    __u16 dc2_reserved;
} __attribute__ ((__packed__));

typedef struct reiserfs_dc reiserfs_dc_t;

#define REISERFS_DC_SIZE (sizeof(reiserfs_dc_t))

/* set/get fields of disk_child with these defines */
#define reiserfs_dc_get_nr(dc)		get_le32 (dc, dc2_block_number)
#define reiserfs_dc_set_nr(dc,val)	set_le32 (dc, dc2_block_number, val)

#define reiserfs_dc_get_size(dc)	get_le16 (dc, dc2_size)
#define reiserfs_dc_set_size(dc,val)	set_le16 (dc, dc2_size, val)

#define reiserfs_dc_init(dc, size, blocknr)				\
({									\
    reiserfs_dc_set_nr(dc, blocknr);					\
    reiserfs_dc_set_size(dc, size);					\
    set_le16(dc, dc2_reserved, 0);					\
})

/* max and min number of keys in internal node */
#define REISERFS_INT_MAX(bh)	\
	((REISERFS_NODE_SPACE(bh->b_size) - REISERFS_DC_SIZE) /		\
	 (REISERFS_KEY_SIZE+REISERFS_DC_SIZE))

#define REISERFS_INT_MIN(bh)	(REISERFS_INT_MAX(bh) / 2)

/* get key */
#define reiserfs_int_key_at(bh,item_num)				\
	((reiserfs_key_t * )((bh)->b_data + REISERFS_NODEH_SIZE) +	\
	 (item_num) )

/* Get disk child by buffer header and position in the tree node. */
#define reiserfs_int_at(p_s_bh,n_pos)					\
	((reiserfs_dc_t *) ((p_s_bh)->b_data + REISERFS_NODEH_SIZE +	\
			    reiserfs_node_items(p_s_bh) * REISERFS_KEY_SIZE +\
			    REISERFS_DC_SIZE * (n_pos)))

#define reiserfs_int_head(bh)						\
	((reiserfs_nh_get_level (((reiserfs_node_head_t *)((bh)->b_data))) >	\
	  LEAF_LEVEL) &&						\
	 (reiserfs_nh_get_level (((reiserfs_node_head_t *)((bh)->b_data))) <=	\
	  REISERFS_TREE_HEIGHT_MAX))

extern int reiserfs_internal_correct (reiserfs_bh_t *bh);

extern int reiserfs_internal_print (FILE * fp, 
				    reiserfs_bh_t * bh, 
				    int first, int last);

extern int reiserfs_internal_get_pos (reiserfs_bh_t * bh, 
				      unsigned long block);

#endif
