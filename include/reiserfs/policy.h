/*
 *  Copyright 2000-2004 by Hans Reiser, licensing governed by 
 *  reiserfsprogs/README
 */

#ifndef REISERFS_POLICY_H
#define REISERFS_POLICY_H

/* this is aggressive tail suppression policy taken from the kernel */
/* It should be REISERFS_DIRECT_MAX used here, but sometimes it is not enough,
 * and items got deleted. */
#define reiserfs_policy_ext(n_file_size,n_tail_size,n_block_size)	\
(									\
  (!(n_tail_size)) ||							\
  (((n_tail_size) > REISERFS_ITEM_MAX(n_block_size)) ||			\
   ( (n_file_size) >= (n_block_size) * 4 ) ||				\
   ( ( (n_file_size) >= (n_block_size) * 3 ) &&				\
     ( (n_tail_size) >=   (REISERFS_ITEM_MAX(n_block_size))/4) ) ||	\
   ( ( (n_file_size) >= (n_block_size) * 2 ) &&				\
     ( (n_tail_size) >=   (REISERFS_ITEM_MAX(n_block_size))/2) ) ||	\
   ( ( (n_file_size) >= (n_block_size) ) &&				\
     ( (n_tail_size) >=   (REISERFS_ITEM_MAX(n_block_size) * 3)/4) ) )	\
)

#endif
