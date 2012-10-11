/*
 *  Copyright 2000-2004 by Hans Reiser, licensing governed by 
 *  reiserfsprogs/README
 */

#ifndef REISERFS_EXTENT_H
#define REISERFS_EXTENT_H

#include "reiserfs/types.h"

/* Size of pointer to the unformatted node. */
#define REISERFS_EXT_SIZE (sizeof(__u32))

/* number of blocks pointed to by the extent item */
#define reiserfs_ext_count(p_s_ih) \
	(reiserfs_ih_get_len(p_s_ih) / REISERFS_EXT_SIZE)
	
extern int reiserfs_ext_check (reiserfs_filsys_t * fs, 
			       reiserfs_ih_t * ih, char * item,
			       unfm_func_t func);

extern void reiserfs_ext_print(FILE * fp, 
			       reiserfs_bh_t * bh, 
			       int item_num);

#endif
