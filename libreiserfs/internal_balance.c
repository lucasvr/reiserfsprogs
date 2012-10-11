/*
 * Copyright 1996-2004 by Hans Reiser, licensing governed by 
 * reiserfsprogs/README
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "reiserfs/libreiserfs.h"

/* modes of reiserfs_ib_shift_left, reiserfs_ib_shift_right and 
   reiserfs_ib_insert */
#define INTERNAL_SHIFT_FROM_S_TO_L 0
#define INTERNAL_SHIFT_FROM_R_TO_S 1
#define INTERNAL_SHIFT_FROM_L_TO_S 2
#define INTERNAL_SHIFT_FROM_S_TO_R 3
#define INTERNAL_INSERT_TO_S 4
#define INTERNAL_INSERT_TO_L 5
#define INTERNAL_INSERT_TO_R 6

static void reiserfs_ib_shift_prep (int shift_mode,
				    reiserfs_tb_t * tb,
				    int h,
				    reiserfs_bufinfo_t * dest_bi,
				    reiserfs_bufinfo_t * src_bi,
				    int * d_key,
				    reiserfs_bh_t ** cf)
{
  /* define dest, src, dest parent, dest position */
  switch (shift_mode) {
  case INTERNAL_SHIFT_FROM_S_TO_L:	
    /* used in reiserfs_ib_shift_left */
    src_bi->bi_bh = REISERFS_PATH_UPBUFFER (tb->tb_path, h);
    src_bi->bi_parent = REISERFS_PATH_UPPARENT (tb->tb_path, h);
    src_bi->bi_position = REISERFS_PATH_UPPOS (tb->tb_path, h + 1);
    dest_bi->bi_bh = tb->L[h];
    dest_bi->bi_parent = tb->FL[h];
    dest_bi->bi_position = reiserfs_tb_lpos (tb, h);
    *d_key = tb->lkey[h];
    *cf = tb->CFL[h];
    break;
  case INTERNAL_SHIFT_FROM_L_TO_S:
    src_bi->bi_bh = tb->L[h];
    src_bi->bi_parent = tb->FL[h];
    src_bi->bi_position = reiserfs_tb_lpos (tb, h);
    dest_bi->bi_bh = REISERFS_PATH_UPBUFFER (tb->tb_path, h);
    dest_bi->bi_parent = REISERFS_PATH_UPPARENT (tb->tb_path, h);
    /* dest position is analog of dest->b_item_order */
    dest_bi->bi_position = REISERFS_PATH_UPPOS (tb->tb_path, h + 1); 
    *d_key = tb->lkey[h];
    *cf = tb->CFL[h];
    break;

  case INTERNAL_SHIFT_FROM_R_TO_S:	
    /* used in reiserfs_ib_shift_left */
    src_bi->bi_bh = tb->R[h];
    src_bi->bi_parent = tb->FR[h];
    src_bi->bi_position = reiserfs_tb_rpos (tb, h);
    dest_bi->bi_bh = REISERFS_PATH_UPBUFFER (tb->tb_path, h);
    dest_bi->bi_parent = REISERFS_PATH_UPPARENT (tb->tb_path, h);
    dest_bi->bi_position = REISERFS_PATH_UPPOS (tb->tb_path, h + 1);
    *d_key = tb->rkey[h];
    *cf = tb->CFR[h];
    break;
  case INTERNAL_SHIFT_FROM_S_TO_R:
    src_bi->bi_bh = REISERFS_PATH_UPBUFFER (tb->tb_path, h);
    src_bi->bi_parent = REISERFS_PATH_UPPARENT (tb->tb_path, h);
    src_bi->bi_position = REISERFS_PATH_UPPOS (tb->tb_path, h + 1);
    dest_bi->bi_bh = tb->R[h];
    dest_bi->bi_parent = tb->FR[h];
    dest_bi->bi_position = reiserfs_tb_rpos (tb, h);
    *d_key = tb->rkey[h];
    *cf = tb->CFR[h];
    break;

  case INTERNAL_INSERT_TO_L:
    dest_bi->bi_bh = tb->L[h];
    dest_bi->bi_parent = tb->FL[h];
    dest_bi->bi_position = reiserfs_tb_lpos (tb, h);
    break;

  case INTERNAL_INSERT_TO_S:
    dest_bi->bi_bh = REISERFS_PATH_UPBUFFER (tb->tb_path, h);
    dest_bi->bi_parent = REISERFS_PATH_UPPARENT (tb->tb_path, h);
    dest_bi->bi_position = REISERFS_PATH_UPPOS (tb->tb_path, h + 1);
    break;

  case INTERNAL_INSERT_TO_R:
    dest_bi->bi_bh = tb->R[h];
    dest_bi->bi_parent = tb->FR[h];
    dest_bi->bi_position = reiserfs_tb_rpos (tb, h);
    break;

  default:
      reiserfs_panic ("reiserfs_ib_shift_prep", 
		      "shift type is unknown (%d)", shift_mode);
  }
}



/* Insert 'count' node pointers into buffer cur before position 'to' + 1.
 * Insert count items into buffer cur before position to.
 * Items and node pointers are specified by inserted and bh respectively.
 */ 
static void reiserfs_ib_insert (reiserfs_filsys_t * fs, 
				reiserfs_bufinfo_t * cur_bi,
				int to, int count,
				reiserfs_ih_t * inserted,
				reiserfs_bh_t ** bh)
{
    reiserfs_bh_t * cur = cur_bi->bi_bh;
    reiserfs_node_head_t * blkh;
    int nr;
    reiserfs_key_t * key;
    reiserfs_dc_t new_dc[2];
    reiserfs_dc_t * dc;
    int i;
    int from;
    
    if (count <= 0)
	return;
    
    blkh = NODE_HEAD (cur);
    nr = reiserfs_nh_get_items (blkh);
    
    /* prepare space for count disk_child */
    dc = reiserfs_int_at (cur,to+1);
    
    memmove (dc + count, dc, (nr+1-(to+1)) * REISERFS_DC_SIZE);
    
    /* make disk child array for insertion */
    for (i = 0; i < count; i ++) {
	reiserfs_dc_init(new_dc + i, REISERFS_NODE_SPACE(bh[i]->b_size) - 
	       reiserfs_nh_get_free (NODE_HEAD (bh[i])),
	       bh[i]->b_blocknr);
	/*
	reiserfs_dc_set_size (new_dc + i,
			   REISERFS_NODE_SPACE(bh[i]->b_size) - 
			   reiserfs_nh_get_free (NODE_HEAD (bh[i])));
	reiserfs_dc_set_nr (new_dc + i, bh[i]->b_blocknr);*/
    }
    memcpy (dc, new_dc, REISERFS_DC_SIZE * count);
    
    /* prepare space for 'count' items  */
    from = ((to == -1) ? 0 : to);
    key = reiserfs_int_key_at (cur, from);
    
    memmove (key + count, key, (nr - from/*to*/) * REISERFS_KEY_SIZE + 
	     (nr + 1 + count) * REISERFS_DC_SIZE);

    /* copy keys */
    memcpy (key, inserted, REISERFS_KEY_SIZE);
    if ( count > 1 )
	memcpy (key + 1, inserted + 1, REISERFS_KEY_SIZE);
    
    /* sizes, item number */
    reiserfs_nh_set_items (blkh, nr + count);
    reiserfs_nh_set_free (blkh, reiserfs_nh_get_free (blkh) - 
			 count * (REISERFS_DC_SIZE + REISERFS_KEY_SIZE));

    reiserfs_buffer_mkdirty (cur);
    
    if (cur_bi->bi_parent) {
	dc = reiserfs_int_at (cur_bi->bi_parent,cur_bi->bi_position);
	reiserfs_dc_set_size (dc, reiserfs_dc_get_size (dc) + 
			   count * (REISERFS_DC_SIZE + REISERFS_KEY_SIZE));
	reiserfs_buffer_mkdirty (cur_bi->bi_parent);
    }
    
}

/* Delete del_num items and node pointers from buffer cur starting from *
 * the first_i'th item and first_p'th pointers respectively.		*/
static void reiserfs_ib_delete (reiserfs_filsys_t * fs,
				reiserfs_bufinfo_t * cur_bi,
				int first_p, 
				int first_i, 
				int del_num)
{
    reiserfs_bh_t * cur = cur_bi->bi_bh;
    int nr;
    reiserfs_node_head_t * blkh;
    reiserfs_key_t * key;
    reiserfs_dc_t * dc;

    if ( del_num == 0 )
	return;

    blkh = NODE_HEAD(cur);
    nr = reiserfs_nh_get_items (blkh);

    if ( first_p == 0 && del_num == nr + 1 ) {
	reiserfs_tb_attach_new(cur_bi);
	return;
    }

    /* deleting */
    dc = reiserfs_int_at (cur, first_p);

    memmove (dc, dc + del_num, (nr + 1 - first_p - del_num) * 
	     REISERFS_DC_SIZE);
    key = reiserfs_int_key_at (cur, first_i);
    memmove (key, key + del_num, (nr - first_i - del_num) * REISERFS_KEY_SIZE + 
	     (nr + 1 - del_num) * REISERFS_DC_SIZE);


    /* sizes, item number */
    reiserfs_nh_set_items (blkh, reiserfs_nh_get_items (blkh) - del_num);
    reiserfs_nh_set_free (blkh, reiserfs_nh_get_free (blkh) +
			 del_num * (REISERFS_KEY_SIZE +  REISERFS_DC_SIZE));

    reiserfs_buffer_mkdirty (cur);
 
    if (cur_bi->bi_parent) {
	dc = reiserfs_int_at (cur_bi->bi_parent, cur_bi->bi_position);
	reiserfs_dc_set_size (dc, reiserfs_dc_get_size (dc) - 
			   del_num * (REISERFS_KEY_SIZE +  REISERFS_DC_SIZE));
	reiserfs_buffer_mkdirty (cur_bi->bi_parent);
    }
}

/* copy cpy_num node pointers and cpy_num - 1 items from buffer src to buffer 
   dest last_first == FIRST_TO_LAST means, that we copy first items from src 
   to tail of dest last_first == LAST_TO_FIRST means, that we copy last items 
   from src to head of dest */
static void reiserfs_ib_copy (reiserfs_filsys_t * fs,
			      reiserfs_bufinfo_t * dest_bi,
			      reiserfs_bh_t * src,
			      int last_first, 
			      int cpy_num)
{
    /* ATTENTION! Number of node pointers in DEST is equal to number of 
       items in DEST as delimiting key have already inserted to buffer dest.*/
    reiserfs_bh_t * dest = dest_bi->bi_bh;
    int nr_dest, nr_src;
    int dest_order, src_order;
    reiserfs_node_head_t * blkh;
    reiserfs_key_t * key;
    reiserfs_dc_t * dc;

    nr_src = reiserfs_node_items (src);

    if ( cpy_num == 0 )
	return;

    /* coping */
    blkh = NODE_HEAD (dest);
    nr_dest = reiserfs_nh_get_items (blkh);

    /*dest_order = (last_first == LAST_TO_FIRST) ? 0 : nr_dest;*/
    /*src_order = (last_first == LAST_TO_FIRST) ? (nr_src - cpy_num + 1) : 0;*/
    (last_first == LAST_TO_FIRST) ? 
	    (dest_order = 0, src_order = nr_src - cpy_num + 1) :
	    (dest_order = nr_dest, src_order = 0);

    /* prepare space for cpy_num pointers */
    dc = reiserfs_int_at (dest, dest_order);

    memmove (dc + cpy_num, dc, (nr_dest - dest_order) * REISERFS_DC_SIZE);

    /* insert pointers */
    memcpy (dc, reiserfs_int_at (src, src_order), REISERFS_DC_SIZE * cpy_num);

    /* prepare space for cpy_num - 1 item headers */
    key = reiserfs_int_key_at(dest, dest_order);
    memmove (key + cpy_num - 1, key, REISERFS_KEY_SIZE * (nr_dest - dest_order) + 
	     REISERFS_DC_SIZE * (nr_dest + cpy_num));


    /* insert headers */
    memcpy (key, reiserfs_int_key_at (src, src_order), REISERFS_KEY_SIZE * (cpy_num - 1));

    /* sizes, item number */
    reiserfs_nh_set_items (blkh, reiserfs_nh_get_items (blkh) + cpy_num - 1);
    reiserfs_nh_set_free (blkh, reiserfs_nh_get_free (blkh) -
			 (REISERFS_KEY_SIZE * (cpy_num - 1) + 
			  REISERFS_DC_SIZE * cpy_num));

    reiserfs_buffer_mkdirty (dest);
    
    if (dest_bi->bi_parent) {
	dc = reiserfs_int_at(dest_bi->bi_parent,dest_bi->bi_position);
	reiserfs_dc_set_size (dc, reiserfs_dc_get_size (dc) + 
			   REISERFS_KEY_SIZE * (cpy_num - 1) + 
			   REISERFS_DC_SIZE * cpy_num);
	reiserfs_buffer_mkdirty (dest_bi->bi_parent);
    }
}


/* Copy cpy_num node pointers and cpy_num - 1 items from buffer src to 
   buffer dest. Delete cpy_num - del_par items and node pointers from 
   buffer src. last_first == FIRST_TO_LAST means, that we copy/delete 
   first items from src. last_first == LAST_TO_FIRST means, that we 
   copy/delete last items from src. */
static void reiserfs_ib_move (reiserfs_filsys_t * fs,
			      reiserfs_bufinfo_t * dest_bi, 
			      reiserfs_bufinfo_t * src_bi,
			      int last_first, 
			      int cpy_num, 
			      int del_par)
{
    int first_pointer;
    int first_item;
    
    reiserfs_ib_copy (fs, dest_bi, src_bi->bi_bh, last_first, cpy_num);
    
    if (last_first == FIRST_TO_LAST) {	/* shift_left occurs */
	first_pointer = 0;
	first_item = 0;
	/* delete cpy_num - del_par pointers and keys starting for pointers 
	   with first_pointer, for key - with first_item */
	reiserfs_ib_delete (fs, src_bi, first_pointer, 
			    first_item, cpy_num - del_par);
    } else {			
	/* shift_right occurs */
	int i, j;
	
	i = ( cpy_num - del_par == ( j = reiserfs_node_items(src_bi->bi_bh)) + 1 ) ? 
		0 : j - cpy_num + del_par;
	
	reiserfs_ib_delete (fs, src_bi, j + 1 - cpy_num + del_par, 
			    i, cpy_num - del_par);
    }
}

/* Insert n_src'th key of buffer src before n_dest'th key of buffer dest. */
static void reiserfs_ib_insert_key (reiserfs_filsys_t * fs,
				    reiserfs_bufinfo_t * dest_bi, 
				    /* insert key before key with 
				       n_dest number */
				    int dest_position_before,                 
				    reiserfs_bh_t * src, 
				    int src_position )
{
    reiserfs_bh_t * dest = dest_bi->bi_bh;
    int nr;
    reiserfs_node_head_t * blkh;
    reiserfs_key_t * key;

    blkh = NODE_HEAD(dest);
    nr = reiserfs_nh_get_items (blkh);

    /* prepare space for inserting key */
    key = reiserfs_int_key_at (dest, dest_position_before);
    memmove (key + 1, key, (nr - dest_position_before) * REISERFS_KEY_SIZE + 
	     (nr + 1) * REISERFS_DC_SIZE);

    /* insert key */
    memcpy (key, reiserfs_int_key_at(src, src_position), REISERFS_KEY_SIZE);

    /* Change dirt, free space, item number fields. */
    reiserfs_nh_set_items (blkh, reiserfs_nh_get_items (blkh) + 1);
    reiserfs_nh_set_free (blkh, reiserfs_nh_get_free (blkh) - REISERFS_KEY_SIZE);

    reiserfs_buffer_mkdirty (dest);

    if (dest_bi->bi_parent) {
	reiserfs_dc_t * dc;
	
	dc = reiserfs_int_at(dest_bi->bi_parent,dest_bi->bi_position);
	reiserfs_dc_set_size (dc, reiserfs_dc_get_size (dc) + REISERFS_KEY_SIZE);
	reiserfs_buffer_mkdirty (dest_bi->bi_parent);
    }
}

/* Insert d_key'th (delimiting) key from buffer cfl to tail of dest. 
 * Copy pointer_amount node pointers and pointer_amount - 1 items from 
 * buffer src to buffer dest.
 * Replace  d_key'th key in buffer cfl.
 * Delete pointer_amount items and node pointers from buffer src. */
/* this can be invoked both to shift from S to L and from R to S */
static void reiserfs_ib_shift_left (/* INTERNAL_FROM_S_TO_L | 
				       INTERNAL_FROM_R_TO_S */
				    int mode,	
				    reiserfs_tb_t * tb, 
				    int h, int pointer_amount)
{
    reiserfs_bufinfo_t dest_bi, src_bi;
    reiserfs_bh_t * cf;
    int d_key_position;

    reiserfs_ib_shift_prep (mode, tb, h, &dest_bi, &src_bi, 
			    &d_key_position, &cf);

    /*printk("pointer_amount = %d\n",pointer_amount);*/

    if (pointer_amount) {
	/* insert delimiting key from common father of dest and src 
	   to node dest into position B_NR_ITEM(dest) */
	reiserfs_ib_insert_key (tb->tb_fs, &dest_bi, 
				reiserfs_node_items(dest_bi.bi_bh), 
				cf, d_key_position);

	if (reiserfs_node_items(src_bi.bi_bh) == pointer_amount - 1) {
	    if (src_bi.bi_position/*src->b_item_order*/ == 0)
		reiserfs_node_replace_key (cf, d_key_position, 
					   src_bi.bi_parent, 0);
	} else {
	    reiserfs_node_replace_key (cf, d_key_position, 
				       src_bi.bi_bh, pointer_amount - 1);
	}
    }
    
    /* last parameter is del_parameter */
    reiserfs_ib_move (tb->tb_fs, &dest_bi, &src_bi, 
		      FIRST_TO_LAST, pointer_amount, 0);
}

/* Insert delimiting key to L[h].
 * Copy n node pointers and n - 1 items from buffer S[h] to L[h].
 * Delete n - 1 items and node pointers from buffer S[h].
 */
/* it always shifts from S[h] to L[h] */
static void reiserfs_ib_shift1_left (reiserfs_tb_t * tb, 
				     int h, int pointer_amount)
{
    reiserfs_bufinfo_t dest_bi, src_bi;
    reiserfs_bh_t * cf;
    int d_key_position;
    
    reiserfs_ib_shift_prep (INTERNAL_SHIFT_FROM_S_TO_L, tb, h, 
			    &dest_bi, &src_bi, &d_key_position, &cf);
    
    if ( pointer_amount > 0 ) 
	    /* insert lkey[h]-th key  from CFL[h] to left neighbor L[h] */
	reiserfs_ib_insert_key (tb->tb_fs, &dest_bi, 
				reiserfs_node_items(dest_bi.bi_bh), 
				cf, d_key_position);
    
    /* last parameter is del_parameter */
    reiserfs_ib_move (tb->tb_fs, &dest_bi, &src_bi, 
		      FIRST_TO_LAST, pointer_amount, 1);
}


/* Insert d_key'th (delimiting) key from buffer cfr to head of dest. 
 * Copy n node pointers and n - 1 items from buffer src to buffer dest.
 * Replace  d_key'th key in buffer cfr.
 * Delete n items and node pointers from buffer src.
 */
static void reiserfs_ib_shift_right (/* INTERNAL_FROM_S_TO_R | 
					INTERNAL_FROM_L_TO_S */
				     int mode,	
				     reiserfs_tb_t * tb, 
				     int h, int pointer_amount)
{
    reiserfs_bufinfo_t dest_bi, src_bi;
    reiserfs_bh_t * cf;
    int d_key_position;
    int nr;


    reiserfs_ib_shift_prep (mode, tb, h, &dest_bi, &src_bi, 
			    &d_key_position, &cf);

    nr = reiserfs_node_items (src_bi.bi_bh);

    if (pointer_amount > 0) {
	/* insert delimiting key from common father of dest and src 
	   to dest node into position 0 */
	reiserfs_ib_insert_key (tb->tb_fs, &dest_bi, 
				0, cf, d_key_position);
	if (nr == pointer_amount - 1) {
	    /* when S[h] disappers replace left delemiting key as well */
	    if (tb->CFL[h]) {
		reiserfs_node_replace_key(cf, d_key_position, 
					  tb->CFL[h], tb->lkey[h]);
	    }
	} else {
	    reiserfs_node_replace_key(cf, d_key_position, 
				      src_bi.bi_bh, nr - pointer_amount);
	}
    }      

    /* last parameter is del_parameter */
    reiserfs_ib_move (tb->tb_fs, &dest_bi, &src_bi, 
		      LAST_TO_FIRST, pointer_amount, 0);
}

/* Insert delimiting key to R[h].
 * Copy n node pointers and n - 1 items from buffer S[h] to R[h].
 * Delete n - 1 items and node pointers from buffer S[h].
 */
/* it always shift from S[h] to R[h] */
static void reiserfs_ib_shift1_right (reiserfs_tb_t * tb, 
				      int h, int pointer_amount)
{
    reiserfs_bufinfo_t dest_bi, src_bi;
    reiserfs_bh_t * cf;
    int d_key_position;

    reiserfs_ib_shift_prep (INTERNAL_SHIFT_FROM_S_TO_R, tb, h, 
			    &dest_bi, &src_bi, &d_key_position, &cf);
    
    if (pointer_amount > 0) /* insert rkey from CFR[h] to right neighbor R[h] */
	reiserfs_ib_insert_key (tb->tb_fs, &dest_bi, 
				0, cf, d_key_position);

    /* last parameter is del_parameter */
    reiserfs_ib_move (tb->tb_fs, &dest_bi, &src_bi, 
		      LAST_TO_FIRST, pointer_amount, 1);
}


/* Delete insert_num node pointers together with their left items
 * and balance current node.*/
static void reiserfs_ib_balance_delete (reiserfs_tb_t * tb, 
					int h, int child_pos)
{
    int insert_num;
    int n;
    reiserfs_bh_t * tbSh = REISERFS_PATH_UPBUFFER (tb->tb_path, h);
    reiserfs_bufinfo_t bi;

    insert_num = tb->insert_size[h] / ((int)(REISERFS_DC_SIZE + REISERFS_KEY_SIZE));
  
    /* delete child-node-pointer(s) together with their left item(s) */
    bi.bi_bh = tbSh;

    bi.bi_parent = REISERFS_PATH_UPPARENT (tb->tb_path, h);

    bi.bi_position = REISERFS_PATH_UPPOS (tb->tb_path, h + 1);

    reiserfs_ib_delete (tb->tb_fs, &bi, child_pos, 
			(child_pos == 0) ? child_pos : child_pos - 1, 
			-insert_num);

    n = reiserfs_node_items(tbSh);

    if ( tb->lnum[h] == 0 && tb->rnum[h] == 0 ) {
	if ( tb->blknum[h] == 0 ) {
	    /* node S[h] (root of the tree) is empty now */
	    reiserfs_bh_t *new_root;
	    reiserfs_sb_t * sb;

	    /* choose a new root */
	    if ( ! tb->L[h-1] || ! reiserfs_node_items(tb->L[h-1]) )
		new_root = tb->R[h-1];
	    else
		new_root = tb->L[h-1];

	    /* update super block's tree height and pointer to a root block */
	    sb = tb->tb_fs->fs_ondisk_sb;
	    reiserfs_sb_set_root (sb, new_root->b_blocknr);
	    reiserfs_sb_set_height (sb, reiserfs_sb_get_height (sb) - 1);

	    reiserfs_buffer_mkdirty (tb->tb_fs->fs_super_bh);
	    tb->tb_fs->fs_dirt = 1;

	    /* mark buffer S[h] not uptodate and put it in free list */
	    reiserfs_node_forget(tb->tb_fs, tbSh->b_blocknr);
	    return;
	}
	return;
    }

    if ( tb->L[h] && tb->lnum[h] == -reiserfs_node_items(tb->L[h]) - 1 ) { 
	/* join S[h] with L[h] */
	/*tb->L[h], tb->CFL[h], tb->lkey[h], tb->S[h], n+1);*/
	reiserfs_ib_shift_left (INTERNAL_SHIFT_FROM_S_TO_L, 
				tb, h, n + 1);
	/* preserve not needed, internal, 1 mean free block */
	reiserfs_node_forget(tb->tb_fs, tbSh->b_blocknr);
	return;
    }

    if ( tb->R[h] &&  tb->rnum[h] == -reiserfs_node_items(tb->R[h]) - 1 ) { 
	/* join S[h] with R[h] */
	reiserfs_ib_shift_right (INTERNAL_SHIFT_FROM_S_TO_R, 
				 tb, h, n + 1);
	reiserfs_node_forget(tb->tb_fs, tbSh->b_blocknr);
	return;
    }

    if ( tb->lnum[h] < 0 ) { 
	/* borrow from left neighbor L[h] */
	reiserfs_ib_shift_right (INTERNAL_SHIFT_FROM_L_TO_S, 
				 tb, h, -tb->lnum[h]);
	return;
    }

    if ( tb->rnum[h] < 0 ) { 
	/* borrow from right neighbor R[h] */
	/*tb->S[h], tb->CFR[h], tb->rkey[h], tb->R[h], -tb->rnum[h]);*/
	reiserfs_ib_shift_left (INTERNAL_SHIFT_FROM_R_TO_S, 
				tb, h, -tb->rnum[h]);
	return;
    }

    if ( tb->lnum[h] > 0 ) { 
	/* split S[h] into two parts and put them into neighbors */
	/*tb->L[h], tb->CFL[h], tb->lkey[h], tb->S[h], tb->lnum[h]);*/
	reiserfs_ib_shift_left (INTERNAL_SHIFT_FROM_S_TO_L, 
				tb, h, tb->lnum[h]);
	reiserfs_ib_shift_right (INTERNAL_SHIFT_FROM_S_TO_R, 
				 tb, h, tb->rnum[h]);
	reiserfs_node_forget(tb->tb_fs, tbSh->b_blocknr);
	return;
    }
    
    reiserfs_panic ("reiserfs_ib_balance_delete", "unexpected "
		    "tb->lnum[%d]==%d or tb->rnum[%d]==%d",
		    h, tb->lnum[h], h, tb->rnum[h]);
}

/* Replace delimiting key of buffers L[h] and S[h] by the given key.*/
static void reiserfs_ib_set_lkey (reiserfs_tb_t * tb,
				  int h, reiserfs_ih_t * key)
{
    if (reiserfs_node_items(REISERFS_PATH_UPBUFFER(tb->tb_path, h)) == 0)
	return;

    memcpy (reiserfs_int_key_at(tb->CFL[h],tb->lkey[h]), key, REISERFS_KEY_SIZE);
    reiserfs_buffer_mkdirty (tb->CFL[h]);
}


/* Replace delimiting key of buffers S[h] and R[h] by the given key.*/
static void reiserfs_ib_set_rkey (reiserfs_tb_t * tb,
				  int h, reiserfs_ih_t * key)
{
    memcpy (reiserfs_int_key_at(tb->CFR[h],tb->rkey[h]), key, REISERFS_KEY_SIZE);
    reiserfs_buffer_mkdirty (tb->CFR[h]);
}

int reiserfs_ib_balance (reiserfs_tb_t * tb,
			 /* level of the tree */
			 int h,
			 int child_pos,
			 /* key for insertion on higher level */
			 reiserfs_ih_t * insert_key,
			 /* node for insertion on higher level*/
			 reiserfs_bh_t ** insert_ptr)
  /* if inserting/pasting
   {
   child_pos is the position of the node-pointer in S[h] that	 *
   pointed to S[h-1] before balancing of the h-1 level;		 *
   this means that new pointers and items must be inserted AFTER *
   child_pos
   }
   else 
   {
   it is the position of the leftmost pointer that must be deleted (together with
   its corresponding key to the left of the pointer)
   as a result of the previous level's balancing.
   }
*/
{
    reiserfs_bh_t * tbSh = REISERFS_PATH_UPBUFFER (tb->tb_path, h);
    reiserfs_bufinfo_t bi;
    
    /* we return this: it is 0 if there is no S[h], 
       else it is tb->S[h]->b_item_order */
    int order;
    
    int insert_num, n, k;
    reiserfs_bh_t * S_new;
    reiserfs_ih_t new_insert_key;
    reiserfs_bh_t * new_insert_ptr = NULL;
    reiserfs_ih_t * new_insert_key_addr = insert_key;

    order = ( tbSh ) ? REISERFS_PATH_UPPOS (tb->tb_path, h + 1)
	    /*tb->S[h]->b_item_order*/ : 0;

  /* Using insert_size[h] calculate the number insert_num of items
     that must be inserted to or deleted from S[h]. */
    insert_num = tb->insert_size[h]/((int)(REISERFS_KEY_SIZE + REISERFS_DC_SIZE));

    /* Check whether insert_num is proper **/
    /* Make balance in case insert_num < 0 */
    if ( insert_num < 0 ) {
	reiserfs_ib_balance_delete (tb, h, child_pos);
	return order;
    }
 
    k = 0;
    if ( tb->lnum[h] > 0 ) {
	/* shift lnum[h] items from S[h] to the left neighbor L[h]. check 
	   how many of new items fall into L[h] or CFL[h] after shifting */
	
	/* number of items in L[h] */
	n = reiserfs_nh_get_items (NODE_HEAD(tb->L[h])); 
	if ( tb->lnum[h] <= child_pos ) {
	    /* new items don't fall into L[h] or CFL[h] */
	    reiserfs_ib_shift_left (INTERNAL_SHIFT_FROM_S_TO_L, 
				    tb, h, tb->lnum[h]);
	    child_pos -= tb->lnum[h];
	} else if ( tb->lnum[h] > child_pos + insert_num ) {
	    /* all new items fall into L[h] */
	    reiserfs_ib_shift_left (INTERNAL_SHIFT_FROM_S_TO_L, 
				    tb, h, tb->lnum[h] - insert_num);

	    /* insert insert_num keys and node-pointers into L[h] */
	    bi.bi_bh = tb->L[h];
	    bi.bi_parent = tb->FL[h];
	    bi.bi_position = reiserfs_tb_lpos (tb, h);
	    reiserfs_ib_insert (tb->tb_fs, &bi,
				/*tb->L[h], tb->S[h-1]->b_next*/ 
				n + child_pos + 1,
				insert_num,insert_key,insert_ptr);

	    insert_num = 0; 
	} else {
	    reiserfs_dc_t * dc;

	    /* some items fall into L[h] or CFL[h], but some don't fall */
	    reiserfs_ib_shift1_left (tb, h, child_pos + 1);
	    /* calculate number of new items that fall into L[h] */
	    k = tb->lnum[h] - child_pos - 1;

	    bi.bi_bh = tb->L[h];
	    bi.bi_parent = tb->FL[h];
	    bi.bi_position = reiserfs_tb_lpos (tb, h);
	    reiserfs_ib_insert (tb->tb_fs, &bi,
				/*tb->L[h], tb->S[h-1]->b_next,*/ 
				n + child_pos + 1,k,
				insert_key,insert_ptr);

	    reiserfs_ib_set_lkey(tb, h, insert_key + k);

	    /* replace the first node-ptr in S[h] by 
	       node-ptr to insert_ptr[k] */
	    dc = reiserfs_int_at(tbSh, 0);

	    reiserfs_dc_init(dc, REISERFS_NODE_SPACE(insert_ptr[k]->b_size) - 
		   reiserfs_nh_get_free (NODE_HEAD(insert_ptr[k])), 
		   insert_ptr[k]->b_blocknr);
	/*  reiserfs_dc_set_size (dc, REISERFS_NODE_SPACE(insert_ptr[k]->b_size) -
			       reiserfs_nh_get_free (NODE_HEAD(insert_ptr[k])));
	    reiserfs_dc_set_nr (dc, insert_ptr[k]->b_blocknr); */
	    reiserfs_buffer_mkdirty (tbSh);

	    k++;
	    insert_key += k;
	    insert_ptr += k;
	    insert_num -= k;
	    child_pos = 0;
	}
    }	/* tb->lnum[h] > 0 */

    if ( tb->rnum[h] > 0 ) {
	/*shift rnum[h] items from S[h] to the right neighbor R[h]*/
	/* check how many of new items fall into R or CFR after shifting */
	n = reiserfs_nh_get_items (NODE_HEAD (tbSh)); /* number of items in S[h] */
	if ( n - tb->rnum[h] >= child_pos )
	    /* new items fall into S[h] */
	    /*reiserfs_ib_shift_right(tb,h,tbSh,tb->CFR[h],
			tb->rkey[h],tb->R[h], tb->rnum[h]);*/
	    reiserfs_ib_shift_right (INTERNAL_SHIFT_FROM_S_TO_R, 
				     tb, h, tb->rnum[h]);
	else
	    if ( n + insert_num - tb->rnum[h] < child_pos ) {
		/* all new items fall into R[h] */
		reiserfs_ib_shift_right (INTERNAL_SHIFT_FROM_S_TO_R, 
					 tb, h, tb->rnum[h] - insert_num);

		/* insert insert_num keys and node-pointers into R[h] */
		bi.bi_bh = tb->R[h];
		bi.bi_parent = tb->FR[h];
		bi.bi_position = reiserfs_tb_rpos (tb, h);
		reiserfs_ib_insert (tb->tb_fs, &bi, 
				    /*tb->R[h],tb->S[h-1]->b_next*/ 
				    child_pos - n - insert_num + 
				    tb->rnum[h] - 1, insert_num,
				    insert_key,insert_ptr);
		insert_num = 0;
	    } else {
		reiserfs_dc_t * dc;

		/* one of the items falls into CFR[h] */
		reiserfs_ib_shift1_right(tb, h, n - child_pos + 1);
		/* calculate number of new items that fall into R[h] */
		k = tb->rnum[h] - n + child_pos - 1;

		bi.bi_bh = tb->R[h];
		bi.bi_parent = tb->FR[h];
		bi.bi_position = reiserfs_tb_rpos (tb, h);
		reiserfs_ib_insert (tb->tb_fs, &bi, 
				    /*tb->R[h], tb->R[h]->b_child,*/ 0, 
				    k, insert_key + 1, insert_ptr + 1);

		reiserfs_ib_set_rkey(tb, h, insert_key + 
				     insert_num - k - 1);

		/* replace the first node-ptr in R[h] by node-ptr 
		   insert_ptr[insert_num-k-1]*/
		dc = reiserfs_int_at(tb->R[h], 0);
		reiserfs_dc_init(dc, REISERFS_NODE_SPACE(
			insert_ptr[insert_num-k-1]->b_size) -
			reiserfs_nh_get_free(NODE_HEAD(insert_ptr[insert_num-k-1])),
			insert_ptr[insert_num-k-1]->b_blocknr);
		/*
		reiserfs_dc_set_size (dc, 
			REISERFS_NODE_SPACE(insert_ptr[insert_num-k-1]->b_size) -
			reiserfs_nh_get_free (NODE_HEAD(insert_ptr[insert_num-k-1])));
		reiserfs_dc_set_nr (dc, insert_ptr[insert_num-k-1]->b_blocknr);
		*/
		reiserfs_buffer_mkdirty (tb->R[h]);
		    
		insert_num -= (k + 1);
	    }
    }

    /** Fill new node that appears instead of S[h] **/
    if ( ! tb->blknum[h] )
    { /* node S[h] is empty now */
	/* Mark buffer as invalid and put it to head of free list. */
	/* do not preserve, internal node*/
	reiserfs_node_forget(tb->tb_fs, tbSh->b_blocknr);
	return order;
    }

    if ( ! tbSh ) {
	/* create new root */
	reiserfs_dc_t  * dc;
	reiserfs_bh_t * tbSh_1 = REISERFS_PATH_UPBUFFER (tb->tb_path, h - 1);
	reiserfs_sb_t * sb;

	if ( tb->blknum[h] != 1 )
	    reiserfs_panic(0, "reiserfs_ib_balance", 
			   "One new node required for creating the new root");
	/* S[h] = empty buffer from the list FEB. */
	tbSh = reiserfs_tb_FEB (tb);
	reiserfs_nh_set_level (NODE_HEAD(tbSh), h + 1);
	
	/* Put the unique node-pointer to S[h] that points to S[h-1]. */

	dc = reiserfs_int_at(tbSh, 0);

	reiserfs_dc_init(dc, REISERFS_NODE_SPACE (tbSh_1->b_size) - 
	       reiserfs_nh_get_free (NODE_HEAD(tbSh_1)),
	       tbSh_1->b_blocknr);
	/*
	reiserfs_dc_set_size (dc, REISERFS_NODE_SPACE (tbSh_1->b_size) - 
		reiserfs_nh_get_free (NODE_HEAD(tbSh_1)));
	reiserfs_dc_set_nr (dc, tbSh_1->b_blocknr); */
	tb->insert_size[h] -= REISERFS_DC_SIZE;
	reiserfs_nh_set_free (NODE_HEAD(tbSh),
			     reiserfs_nh_get_free (NODE_HEAD(tbSh)) - 
			     REISERFS_DC_SIZE);

	reiserfs_buffer_mkdirty (tbSh);
	
	/* put new root into path structure */
	REISERFS_PATH_BUFFER(tb->tb_path, REISERFS_PATH_OFFILL) = tbSh;
	
	/* Change root in structure super block. */
	sb = tb->tb_fs->fs_ondisk_sb;
	reiserfs_sb_set_root (sb, tbSh->b_blocknr);
	reiserfs_sb_set_height (sb, reiserfs_sb_get_height (sb) + 1);
	
	reiserfs_buffer_mkdirty (tb->tb_fs->fs_super_bh);
	tb->tb_fs->fs_dirt = 1;
    }
    
    if ( tb->blknum[h] == 2 ) {
	int snum;
	reiserfs_bufinfo_t dest_bi, src_bi;


	/* S_new = free buffer from list FEB */
	S_new = reiserfs_tb_FEB(tb);

	reiserfs_nh_set_level (NODE_HEAD(S_new), h + 1);

	dest_bi.bi_bh = S_new;
	dest_bi.bi_parent = 0;
	dest_bi.bi_position = 0;
	src_bi.bi_bh = tbSh;
	src_bi.bi_parent = REISERFS_PATH_UPPARENT (tb->tb_path, h);
	src_bi.bi_position = REISERFS_PATH_UPPOS (tb->tb_path, h + 1);
		
	n = reiserfs_nh_get_items (NODE_HEAD(tbSh)); /* number of items in S[h] */
	snum = (insert_num + n + 1)/2;
	if ( n - snum >= child_pos ) {
	    /* new items don't fall into S_new */
	    /*	store the delimiting key for the next level */
	    /* new_insert_key = (n - snum)'th key in S[h] */
	    memcpy (&new_insert_key,reiserfs_int_key_at(tbSh,n - snum),
		    REISERFS_KEY_SIZE);
	    /* last parameter is del_par */
	    reiserfs_ib_move (tb->tb_fs, &dest_bi, &src_bi, 
			      LAST_TO_FIRST, snum, 0);
	} else if ( n + insert_num - snum < child_pos ) {
	    /* all new items fall into S_new */
	    /*	store the delimiting key for the next level */
	    /* new_insert_key = (n + insert_item - snum)'th key in S[h] */
	    memcpy(&new_insert_key,reiserfs_int_key_at(tbSh,n + insert_num - snum),
		   REISERFS_KEY_SIZE);
	    /* last parameter is del_par */
	    reiserfs_ib_move (tb->tb_fs, &dest_bi, &src_bi, 
			      LAST_TO_FIRST, snum - insert_num, 0);
	    
	    /* reiserfs_ib_move(S_new,tbSh,1,snum - insert_num,0);*/

	    /* insert insert_num keys and node-pointers into S_new */
	    reiserfs_ib_insert (tb->tb_fs, &dest_bi, 
				/*S_new,tb->S[h-1]->b_next,*/
				child_pos - n - insert_num + snum - 1,
				insert_num,insert_key,insert_ptr);
	    insert_num = 0;
	} else {
	    reiserfs_dc_t * dc;

	    /* some items fall into S_new, but some don't fall */
	    /* last parameter is del_par */
	    reiserfs_ib_move (tb->tb_fs, &dest_bi, &src_bi, 
			      LAST_TO_FIRST, n - child_pos + 1, 1);
	    /* reiserfs_ib_move(S_new,tbSh,1,n - child_pos + 1,1);*/
	    /* calculate number of new items that fall into S_new */
	    k = snum - n + child_pos - 1;

	    reiserfs_ib_insert (tb->tb_fs, &dest_bi, 
				/*S_new,*/ 0, k, insert_key + 1, 
				insert_ptr+1);

	    /* new_insert_key = insert_key[insert_num - k - 1] */
	    memcpy(&new_insert_key,insert_key + insert_num - k - 1, REISERFS_KEY_SIZE);
	    /* replace first node-ptr in S_new by node-ptr to 
	       insert_ptr[insert_num-k-1] */

	    dc = reiserfs_int_at(S_new,0);
	    reiserfs_dc_init(dc, REISERFS_NODE_SPACE(
		   insert_ptr[insert_num-k-1]->b_size) -
		   reiserfs_nh_get_free (NODE_HEAD(insert_ptr[insert_num-k-1])),
		   insert_ptr[insert_num-k-1]->b_blocknr);
	    /*
	    reiserfs_dc_set_size (dc, REISERFS_NODE_SPACE(insert_ptr[insert_num-k-1]->b_size) -
		reiserfs_nh_get_free (NODE_HEAD(insert_ptr[insert_num-k-1])));
	    reiserfs_dc_set_nr (dc, insert_ptr[insert_num-k-1]->b_blocknr);
	    */
	    reiserfs_buffer_mkdirty (S_new);
			
	    insert_num -= (k + 1);
	}
	/* new_insert_ptr = node_pointer to S_new */
	new_insert_ptr = S_new;
	
	// S_new->b_count --;
    }

    n = reiserfs_nh_get_items (NODE_HEAD(tbSh)); /*number of items in S[h] */

    if ( -1 <= child_pos && child_pos <= n && insert_num > 0 ) {
	bi.bi_bh = tbSh;
	bi.bi_parent = REISERFS_PATH_UPPARENT (tb->tb_path, h);
	bi.bi_position = REISERFS_PATH_UPPOS (tb->tb_path, h + 1);
	if (child_pos == -1) {
	    /* this is a little different from original do_balance:
	       here we insert the minimal keys in the tree, that has never 
	       happened when file system works */
	    if (tb->CFL[h-1] || insert_num != 1 || h != 1)
		misc_die ("reiserfs_ib_balance: invalid child_pos");
	    /* insert_child (tb->S[h], tb->S[h-1], child_pos, insert_num, 
			B_N_ITEM_HEAD(tb->S[0],0), insert_ptr);*/
	    reiserfs_ib_insert (tb->tb_fs, &bi, child_pos, insert_num, 
				reiserfs_ih_at(REISERFS_PATH_LEAF (tb->tb_path), 0), 
				insert_ptr);
	} else
	    reiserfs_ib_insert (tb->tb_fs, &bi, child_pos,insert_num,
				insert_key,insert_ptr);
    }

    memcpy (new_insert_key_addr,&new_insert_key,REISERFS_KEY_SIZE);
    insert_ptr[0] = new_insert_ptr;

    return order;
}
