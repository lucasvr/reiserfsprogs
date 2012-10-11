/*
 *  Copyright 2000-2004 by Hans Reiser, licensing governed by 
 *  reiserfsprogs/README
 */

#ifndef REISERFS_FIX_NODE_H
#define REISERFS_FIX_NODE_H

#include <reiserfs/types.h>
#include <reiserfs/tree_balance.h>

/* maximum number of FEB blocknrs on a single level */
#define FN_AMOUNT_MAX 2

/* To make any changes in the tree we always first find node, that contains
   item to be changed/deleted or place to insert a new item. We call this node
   S. To do balancing we need to decide what we will shift to left/right
   neighbor, or to a new node, where new item will be etc. To make this
   analysis simpler we build virtual node. Virtual node is an array of items,
   that will replace items of node S. (For instance if we are going to delete
   an item, virtual node does not contain it). Virtual node keeps information
   about item sizes and types, mergeability of first and last items, sizes of
   all entries in directory item. We use this array of items when calculating
   what we can shift to neighbors and how many nodes we have to have if we do
   not any shiftings, if we shift to left/right neighbor or to both. */
struct reiserfs_virtual_item
{
    /* item type, mergeability */
    unsigned short vi_type;	
    /* length of item that it will have after balancing */
    unsigned short vi_item_len; 
    /* offset of item that it have before balancing */
    __u64 vi_item_offset; 

    /* number of entries in directory item (including the new one if any, 
       or excluding entry if it must be cut) */
    short vi_entry_count;	
    /* array of entry lengths for directory item */
    unsigned short * vi_entry_sizes; 
};

struct reiserfs_virtual_node
{
    /* this is a pointer to the free space in the buffer */
    char * vn_free_ptr;	 
    /* number of items in virtual node */
    unsigned short vn_nr_item;	
    /* size of node , that node would have if it has unlimited size and no 
       balancing is performed */
    short vn_size; 
    /* mode of balancing (paste, insert, delete, cut) */
    short vn_mode;		
    short vn_affected_item_num; 
    short vn_pos_in_item;
    /* item header of inserted item, 0 for other modes */
    reiserfs_ih_t * vn_ins_ih;	
    /* array of items (including a new one, excluding item to be deleted) */
    struct reiserfs_virtual_item * vn_vi;	
};

extern int reiserfs_fix_nodes (int n_op_mode, 
			       reiserfs_tb_t * p_s_tb,
			       reiserfs_ih_t * p_s_ins_ih);

extern void reiserfs_unfix_nodes (reiserfs_tb_t *);

/* FIXME: move these 2 into tree.h when get rid of search_by_key */
extern reiserfs_bh_t * reiserfs_tree_right_neighbor (reiserfs_filsys_t * s, 
							  reiserfs_path_t * path);

extern reiserfs_bh_t * reiserfs_tree_left_neighbor (reiserfs_filsys_t * s, 
							 reiserfs_path_t * path);

extern void reiserfs_fix_node_print(struct reiserfs_virtual_node * vn);

#endif
