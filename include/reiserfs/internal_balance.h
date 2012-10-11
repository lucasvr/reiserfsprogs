/*
 *  Copyright 2000-2004 by Hans Reiser, licensing governed by 
 *  reiserfsprogs/README
 */

#ifndef REISERFS_IB_H
#define REISERFS_IB_H

extern int reiserfs_ib_balance (reiserfs_tb_t * tb,
				int h, int child_pos,
				reiserfs_ih_t * insert_key,
				reiserfs_bh_t ** insert_ptr);

#endif
