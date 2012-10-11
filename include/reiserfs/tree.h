/*
 *  Copyright 2000-2004 by Hans Reiser, licensing governed by 
 *  reiserfsprogs/README
 */

#ifndef REISERFS_TREE_H
#define REISERFS_TREE_H

#include <reiserfs/types.h>

/* Search_by_key fills up the path from the root to the leaf as it
   descends the tree looking for the key.  It uses reiserfs_buffer_read to
   try to find buffers in the cache given their block number.  If it
   does not find them in the cache it reads them from disk.  For each
   node reiserfs_tree_search_item finds using reiserfs_buffer_read it then uses
   bin_search to look through that node.  bin_search will find the
   position of the block_number of the next node if it is looking
   through an internal node.  If it is looking through a leaf node
   bin_search will find the position of the item which has key either
   equal to given key, or which is the maximal key less than the given
   key. */


#define REISERFS_PATH_INIT(var)						\
	reiserfs_path_t var = {REISERFS_PATH_OFFILL, }

/* Get path element by path and path position. */
#define REISERFS_PATH_ELEM(p_s_path,nr_elem)				\
	((p_s_path)->path_elements + (nr_elem))

/* Get buffer header at the path by path and path position. */
#define REISERFS_PATH_BUFFER(p_s_path,nr_elem)				\
	(REISERFS_PATH_ELEM(p_s_path, nr_elem)->pe_buffer)

/* Get position in the element at the path by path and path position. */
#define REISERFS_PATH_POS(p_s_path,nr_elem)				\
	(REISERFS_PATH_ELEM(p_s_path, nr_elem)->pe_position)

#define REISERFS_PATH_LEAF(p_s_path)					\
	(REISERFS_PATH_BUFFER((p_s_path), (p_s_path)->path_length))

#define REISERFS_PATH_LEAF_POS(p_s_path)				\
	(REISERFS_PATH_POS((p_s_path), (p_s_path)->path_length))

#define REISERFS_PATH_IH(p_s_path)					\
	reiserfs_ih_at(REISERFS_PATH_LEAF(p_s_path),			\
		       REISERFS_PATH_LEAF_POS(p_s_path))

/* in reiserfs_tb_balance leaf has h == 0 in contrast with path structure,
   where root has level == 0. That is why we need these defines */
#define REISERFS_PATH_UPBUFFER(p_s_path, h)				\
	(REISERFS_PATH_BUFFER (p_s_path, p_s_path->path_length - (h)))

/* tb->F[h] or tb->S[0]->b_parent */
#define REISERFS_PATH_UPPARENT(path, h)					\
	REISERFS_PATH_UPBUFFER (path, (h) + 1)

#define REISERFS_PATH_UPPOS(path, h)					\
	REISERFS_PATH_POS (path, path->path_length - (h))	

/* tb->S[h]->b_item_order */
#define REISERFS_PATH_UPPARENT_POS(path, h)				\
	REISERFS_PATH_UPPOS(path, h + 1)

#define REISERFS_PATH_LEVEL(p_s_path, n_h)				\
	((p_s_path)->path_length - (n_h))

#define REISERFS_PATH_ITEM(path)					\
	((void *) reiserfs_item_at(REISERFS_PATH_LEAF(path),		\
				   REISERFS_PATH_LEAF_POS (path)))

extern int reiserfs_tree_search_entry (reiserfs_filsys_t *fs, 
				       const reiserfs_key_t * key, 
				       reiserfs_path_t * path);

extern int reiserfs_tree_search_position (reiserfs_filsys_t *fs, 
					  const reiserfs_key_t * key, 
					  reiserfs_path_t * path);

extern const reiserfs_key_t * reiserfs_tree_lkey (reiserfs_path_t * path,
						  reiserfs_filsys_t *fs);

extern const reiserfs_key_t * reiserfs_tree_rkey (reiserfs_path_t * path, 
						  reiserfs_filsys_t *fs);

extern const reiserfs_key_t *reiserfs_tree_next_key (reiserfs_path_t * path,
						     reiserfs_filsys_t *fs);

extern int reiserfs_tree_delete_entry (reiserfs_filsys_t *, 
				       reiserfs_key_t *);

extern void reiserfs_tree_delete (reiserfs_filsys_t *, 
				  reiserfs_path_t *, int);

extern void reiserfs_tree_delete_unit (reiserfs_filsys_t *, 
				       reiserfs_path_t *, int);

extern void reiserfs_tree_insert_unit (reiserfs_filsys_t *, 
				       reiserfs_path_t * path,
				       const void * body, 
				       int size);

extern void reiserfs_tree_insert (reiserfs_filsys_t *, 
				  reiserfs_path_t * ,
				  reiserfs_ih_t *, 
				  const void *);

extern int reiserfs_tree_search_name (reiserfs_filsys_t *, 
				      const reiserfs_key_t * dir, 
				      char * name,
				      unsigned int * min_gen_counter, 
				      reiserfs_key_t * key);

extern int reiserfs_tree_search_body (reiserfs_filsys_t * fs, 
				      const reiserfs_key_t * key,
				      reiserfs_path_t * path);

extern int reiserfs_tree_search_item (reiserfs_filsys_t * fs, 
				      const reiserfs_key_t * key,
				      reiserfs_path_t * path);

extern int reiserfs_tree_scan_name (reiserfs_filsys_t *, 
				    reiserfs_key_t * dir, 
				    char * name,
				    reiserfs_path_t * path);

extern int reiserfs_tree_insert_entry (reiserfs_filsys_t *, 
				       const reiserfs_key_t * dir, 
				       char * name, 
				       int name_len,
				       const reiserfs_key_t * key, 
				       __u16 fsck_need);

typedef void (*item_modify_t) (reiserfs_ih_t *, void *item);

extern __u16 reiserfs_tree_root (reiserfs_filsys_t * fs,
				 item_modify_t modify,
				 __u16 ih_flags);

extern int reiserfs_tree_create_stat (reiserfs_filsys_t * fs,
				      reiserfs_path_t * path, 
				      const reiserfs_key_t * key,
				      item_modify_t modify);

extern void reiserfs_tree_pathrelse (reiserfs_path_t * p_s_search_path);

extern int reiserfs_tree_left_mergeable (reiserfs_filsys_t * s, 
					 reiserfs_path_t * path);

extern int reiserfs_tree_right_mergeable (reiserfs_filsys_t * s, 
					  reiserfs_path_t * path);

extern int reiserfs_tree_node_mergeable (reiserfs_bh_t *left, 
					 reiserfs_bh_t *right);

extern int reiserfs_tree_merge(reiserfs_filsys_t *fs, 
			       reiserfs_path_t *dst_path, 
			       reiserfs_path_t *src_path);

#endif
