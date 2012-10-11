/*
 *  Copyright 2000-2004 by Hans Reiser, licensing governed by 
 *  reiserfsprogs/README
 */

#ifndef REISERFS_LB_H
#define REISERFS_LB_H

extern void reiserfs_lb_delete_item (reiserfs_filsys_t * fs,
				     reiserfs_bufinfo_t * bi,
				     int first, 
				     int del_num);

extern void reiserfs_lb_delete_unit (reiserfs_filsys_t * fs,
				     reiserfs_bufinfo_t * bi, 
				     int cut_item_num,
				     int pos_in_item, 
				     int cut_size);

extern void reiserfs_lb_delete (reiserfs_filsys_t * fs,
				reiserfs_bufinfo_t * cur_bi,
				int last_first, 
				int first, 
				int del_num, 
				int del_bytes);

extern int reiserfs_lb_balance (reiserfs_tb_t * tb,
				reiserfs_ih_t * ih,
				const char * body,
				int flag,
				int zeros_number,
				reiserfs_ih_t * insert_key,
				reiserfs_bh_t ** insert_ptr);

extern int reiserfs_lb_copy (reiserfs_filsys_t * fs,
			     reiserfs_bufinfo_t * dest_bi, 
			     reiserfs_bh_t * src,
			     int last_first, 
			     int cpy_num,
			     int cpy_bytes);

#endif
