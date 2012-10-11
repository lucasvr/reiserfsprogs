/*
 *  Copyright 2000-2004 by Hans Reiser, licensing governed by 
 *  reiserfsprogs/README
 *
 * Leaf balancing method declarations.
 *
 */

#ifndef REISERFS_LEAF_H
#define REISERFS_LEAF_H

#include <reiserfs/types.h>
#include <misc/bitops.h>

/* size of item header     */
#define REISERFS_IH_SIZE (sizeof(reiserfs_ih_t))

/* set/get fields of item head on disk with these defines */
#define reiserfs_ih_get_entries(ih)	get_le16 (ih, u.ih2_entry_count)
#define reiserfs_ih_set_entries(ih,val)	set_le16 (ih, u.ih2_entry_count, val)

#define reiserfs_ih_get_free(ih)	get_le16 (ih, u.ih2_free_space)
#define reiserfs_ih_set_free(ih,val)	set_le16 (ih, u.ih2_free_space, 0)

#define reiserfs_ih_get_len(ih)		get_le16 (ih, ih2_item_len)
#define reiserfs_ih_set_len(ih,val)	set_le16 (ih, ih2_item_len, val)

#define reiserfs_ih_get_loc(ih)		get_le16 (ih, ih2_item_location)
#define reiserfs_ih_set_loc(ih,val)	set_le16 (ih, ih2_item_location, val)

#define reiserfs_ih_format_v1(ih) (reiserfs_ih_get_format (ih) == KEY_FORMAT_1)

#define IH_Unreachable 0
#define IH_Was_Tail    1
#define IH_Checked     2
#define IH_Writable    3

/* Unreachable bit is set on tree rebuilding and is cleared in semantic pass */
#define reiserfs_ih_clflags(ih)	reiserfs_ih_set_flags (ih, 0)

#define reiserfs_ih_isreach(ih)						\
	(!(reiserfs_ih_get_flags (ih) & (1 << IH_Unreachable)))
	
#define reiserfs_ih_clunreach(ih) reiserfs_ih_set_flags (ih,		\
	reiserfs_ih_get_flags (ih) & ~(1 << IH_Unreachable))

#define reiserfs_ih_mkunreach(ih) reiserfs_ih_set_flags (ih,		\
	reiserfs_ih_get_flags (ih) |	(1 << IH_Unreachable))

#define reiserfs_ih_wastail(ih)						\
	(reiserfs_ih_get_flags (ih) & (1 << IH_Was_Tail))
	
#define reiserfs_ih_mktail(ih) reiserfs_ih_set_flags (ih,		\
	reiserfs_ih_get_flags (ih) | (1 << IH_Was_Tail))

#define reiserfs_ih_cltail(ih) reiserfs_ih_set_flags (ih,		\
	reiserfs_ih_get_flags (ih) & ~(1 << IH_Was_Tail))

#define reiserfs_ih_ischeck(ih)						\
	(reiserfs_ih_get_flags (ih) & (1 << IH_Checked))
	
#define reiserfs_ih_mkcheck(ih) reiserfs_ih_set_flags (ih,		\
	reiserfs_ih_get_flags (ih) | (1 << IH_Checked))

#define reiserfs_ih_clcheck(ih) reiserfs_ih_set_flags (ih,		\
	reiserfs_ih_get_flags (ih) & ~(1 << IH_Checked))

#define reiserfs_ih_iswrite(ih)						\
	(reiserfs_ih_get_flags (ih) & (1 << IH_Writable))
	
#define reiserfs_ih_mkwrite(ih) reiserfs_ih_set_flags (ih,		\
	reiserfs_ih_get_flags (ih) | (1 << IH_Writable))
	
#define reiserfs_ih_clwrite(ih) reiserfs_ih_set_flags (ih,		\
	reiserfs_ih_get_flags (ih) & ~(1 << IH_Writable))


#define reiserfs_ih_stat(p_s_ih) 	reiserfs_key_stat(&((p_s_ih)->ih_key))
#define reiserfs_ih_dir(p_s_ih)		reiserfs_key_dir(&((p_s_ih)->ih_key))
#define reiserfs_ih_direct(p_s_ih) 	reiserfs_key_direct(&((p_s_ih)->ih_key))
#define reiserfs_ih_ext(p_s_ih) 	reiserfs_key_ext(&((p_s_ih)->ih_key))


/* maximal length of item */
#define REISERFS_ITEM_MAX(block_size)					\
	(block_size - REISERFS_NODEH_SIZE - REISERFS_IH_SIZE)

#define REISERFS_ITEM_MIN	1

#define REISERFS_DIRECT_MAX(size)					\
	((size) - REISERFS_NODEH_SIZE - 2 * REISERFS_IH_SIZE -		\
	 REISERFS_SD_SIZE - REISERFS_EXT_SIZE)

/* get the item header */ 
#define reiserfs_ih_at(bh,item_num)					\
	((reiserfs_ih_t * )((bh)->b_data + REISERFS_NODEH_SIZE) + (item_num))

/* get the key */
#define reiserfs_ih_key_at(bh,item_num)					\
	(&(reiserfs_ih_at(bh,item_num)->ih_key))

/* get item body */
#define reiserfs_item_at(bh,item_num)					\
	( (bh)->b_data + reiserfs_ih_get_loc (reiserfs_ih_at((bh),(item_num))))

 /* following defines use reiserfs buffer header and item header */
 /* get item body */
#define reiserfs_item_by_ih(bh,ih) ((bh)->b_data + reiserfs_ih_get_loc(ih))

#define reiserfs_leaf_head(bh)						\
	(reiserfs_nh_get_level ((reiserfs_node_head_t *)((bh)->b_data))	\
	 == LEAF_LEVEL)

/* check whether byte number 'offset' is in this item */
#define reiserfs_item_has_off(p_s_ih, n_offset, n_blocksize)		\
	(reiserfs_key_get_off(&(p_s_ih)->ih_key) <= (n_offset) &&	\
	 reiserfs_key_get_off(&(p_s_ih)->ih_key) +			\
	 reiserfs_leaf_ibytes(p_s_ih,n_blocksize) > (n_offset) )

#define reiserfs_item_has_key(p_s_ih, p_s_key, n_blocksize)		\
	(!reiserfs_key_comp2(p_s_ih, p_s_key) &&			\
	 reiserfs_item_has_off(p_s_ih, reiserfs_key_get_off (p_s_key),	\
			       n_blocksize))

#define reiserfs_item_off_at(ih, pos, bs)				\
	(reiserfs_key_get_off (&(ih)->ih_key) +				\
	 (pos) * (reiserfs_ih_direct(ih) ? 1 : (bs)))

#define reiserfs_dir(ih, item)						\
	((reiserfs_ih_stat(ih) && !not_a_directory(item)) ||		\
	 reiserfs_ih_dir(ih))

#define reiserfs_item_count(ih)						\
	(reiserfs_ih_ext(ih) ? reiserfs_ext_count(ih) :                 \
	 reiserfs_ih_direct(ih) ? reiserfs_ih_get_len(ih) :              \
	 reiserfs_ih_dir(ih) ? reiserfs_ih_get_entries(ih) :            \
	 reiserfs_ih_stat(ih) ? 1 : 0)

extern void reiserfs_leaf_mkempty (reiserfs_bh_t *bh);

extern int reiserfs_leaf_mergeable (reiserfs_ih_t *left, 
				    reiserfs_ih_t *right, 
				    int bsize);

extern int reiserfs_leaf_count_items(reiserfs_bh_t *bh);

extern int reiserfs_leaf_free_count(reiserfs_bh_t *bh);

extern int reiserfs_leaf_estimate_items(reiserfs_bh_t * bh);

extern int reiserfs_leaf_valid(reiserfs_bh_t *bh);

typedef void (*ih_func_t) (reiserfs_ih_t * ih);
typedef void (*item_func_t) (reiserfs_bh_t * bh, 
			     reiserfs_ih_t * ih);

extern void reiserfs_leaf_traverse(reiserfs_bh_t * bh, 
				   ih_func_t action,
				   item_func_t * actions);

extern void reiserfs_leaf_delete_item (reiserfs_filsys_t *, 
				       reiserfs_bh_t * bh, 
				       int item_num);

extern void reiserfs_leaf_delete_entry (reiserfs_filsys_t *, 
					reiserfs_bh_t * bh,
					int item_num, 
					int entry_num, 
					int del_count);

extern int reiserfs_leaf_correct_at (reiserfs_filsys_t *, 
				     reiserfs_ih_t *, char *,
				     unfm_func_t, int);

extern __u32 reiserfs_leaf_ibytes (const reiserfs_ih_t * ih, 
				   int blocksize);

extern int reiserfs_leaf_print(FILE * fp, 
			       reiserfs_filsys_t * fs, 
			       reiserfs_bh_t * bh,
			       int print_mode, 
			       int first, 
			       int last);

/* key format is stored in 12 bits starting from 0-th of item_head's ih2_format*/
extern __u16 reiserfs_ih_get_format(const reiserfs_ih_t *ih);
extern void reiserfs_ih_set_format(reiserfs_ih_t *ih, __u16 val);
extern __u16 reiserfs_ih_get_flags(const reiserfs_ih_t *ih);
extern void reiserfs_ih_set_flags(reiserfs_ih_t *ih, __u16 val);

enum leaf_print {
	LP_LEAF_DETAILS = 0x1,
	LP_DIRECT_ITEMS = 0x2,
	LP_LAST
};

#endif
