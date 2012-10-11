/*
 *  Copyright 2000-2004 by Hans Reiser, licensing governed by 
 *  reiserfsprogs/README
 */

#ifndef REISERFS_NODE_H
#define REISERFS_NODE_H

/* 
 * Picture represents a leaf of internal tree
 *  ______________________________________________________
 * |      |  Array of     |                   |           |
 * |Block |  Object-Item  |      F r e e      |  Objects- |
 * | head |  Headers      |     S p a c e     |   Items   |
 * |______|_______________|___________________|___________|
 */

/* Header of a disk block.  More precisely, header of a formatted leaf
   or internal node, and not the header of an unformatted node. */
struct reiserfs_node_head {       
    __u16 blk2_level;        /* Level of a block in the tree. */
    __u16 blk2_nr_item;      /* Number of keys/items in a block. */
    __u16 blk2_free_space;   /* Block free space in bytes. */
    __u16 blk_reserved;
    __u32 reserved [4];
};

typedef struct reiserfs_node_head reiserfs_node_head_t;

#define REISERFS_NODEH_SIZE (sizeof(reiserfs_node_head_t))

/* set/get fields of block head on disk with these defines */
#define reiserfs_nh_get_level(blkh)	get_le16 (blkh, blk2_level)
#define reiserfs_nh_set_level(blkh,val)	set_le16 (blkh, blk2_level, val)

#define reiserfs_nh_get_items(blkh)	get_le16 (blkh, blk2_nr_item)
#define reiserfs_nh_set_items(blkh,val)	set_le16 (blkh, blk2_nr_item, val)

#define reiserfs_nh_get_free(blkh)	get_le16 (blkh, blk2_free_space)
#define reiserfs_nh_set_free(blkh,val)	set_le16 (blkh, blk2_free_space, val)

/*
 * values for blk_type field
 */

#define FREE_LEVEL  0 /* Node of this level is out of the tree. */
#define LEAF_LEVEL  1 /* Leaf node level.                       */

/* Given the buffer head of a formatted node, resolve to the block head of that node. */
#define NODE_HEAD(p_s_bh)  ((reiserfs_node_head_t *)((p_s_bh)->b_data))

#define reiserfs_node_items(bh)	reiserfs_nh_get_items (NODE_HEAD(bh))
#define reiserfs_node_level(bh)	reiserfs_nh_get_level (NODE_HEAD(bh))
#define reiserfs_node_free(bh)	reiserfs_nh_get_free (NODE_HEAD(bh))

/* Does the buffer contain a disk block which is in the tree. */
#define REISERFS_NODE_INTREE(p_s_bh) \
	(reiserfs_nh_get_level(NODE_HEAD (p_s_bh)) != FREE_LEVEL)

#define REISERFS_NODE_SPACE(blocksize)	((blocksize) - REISERFS_NODEH_SIZE)

/* amount of used space in buffer (not including block head) */
#define reiserfs_node_used(cur) (REISERFS_NODE_SPACE(cur->b_size) -	\
				 (reiserfs_node_free(cur)))

enum node_type {
    NT_LEAF	= 0x1,
    NT_INTERNAL = 0x2,
    NT_SUPER	= 0x3,
    NT_JDESC	= 0x4,
    NT_IH_ARRAY = 0x5,
    NT_UNKNOWN
};

typedef enum node_type node_type_t;

extern void reiserfs_node_replace_key (reiserfs_bh_t * dest, int n_dest,
				       reiserfs_bh_t * src, int n_src);

extern void reiserfs_node_forget(reiserfs_filsys_t *fs, 
				 unsigned long blk);


extern int reiserfs_node_formatted (reiserfs_bh_t * bh, int level);
extern int reiserfs_node_type (reiserfs_bh_t *bh);
extern char *reiserfs_node_type_name(int code);

extern void reiserfs_node_print (FILE * fp, 
				 reiserfs_filsys_t *, 
				 reiserfs_bh_t * bh, ...);

#endif
