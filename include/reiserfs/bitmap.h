/*
 *  Copyright 2000-2004 by Hans Reiser, licensing governed by 
 *  reiserfsprogs/README
 */

#ifndef REISERFS_BITMAP_H
#define REISERFS_BITMAP_H

#include "reiserfs/types.h"
#include <stdio.h>

#define reiserfs_bmap_nr(count, blk_size) \
	((count - 1) / (blk_size * 8) + 1)

#define reiserfs_bmap_over(nr) (nr > ((1ll << 16) - 1))

extern reiserfs_bitmap_t *reiserfs_bitmap_create (unsigned int bit_count);

extern int reiserfs_bitmap_open(reiserfs_filsys_t *);

extern void reiserfs_bitmap_delete (reiserfs_bitmap_t * bm);

extern void reiserfs_bitmap_free (reiserfs_filsys_t *);

extern void reiserfs_bitmap_close (reiserfs_filsys_t *);

extern int reiserfs_bitmap_flush (reiserfs_bitmap_t * bm, 
				  reiserfs_filsys_t * fs);

extern int reiserfs_bitmap_expand (reiserfs_bitmap_t * bm, 
				   unsigned int bit_count);

extern void reiserfs_bitmap_shrink (reiserfs_bitmap_t * bm, 
				    unsigned int bit_count);

extern void reiserfs_bitmap_copy (reiserfs_bitmap_t * to, 
				  reiserfs_bitmap_t * from);

extern int reiserfs_bitmap_compare (reiserfs_bitmap_t * bm1, 
				    reiserfs_bitmap_t * bm2);

extern void reiserfs_bitmap_disjunction (reiserfs_bitmap_t * disk, 
					 reiserfs_bitmap_t * cont);

extern void reiserfs_bitmap_delta (reiserfs_bitmap_t * base, 
				   reiserfs_bitmap_t * exclude);

extern void reiserfs_bitmap_set_bit (reiserfs_bitmap_t * bm, 
				     unsigned int bit_number);

extern void reiserfs_bitmap_clear_bit (reiserfs_bitmap_t * bm, 
				       unsigned int bit_number);

extern int reiserfs_bitmap_test_bit (reiserfs_bitmap_t * bm, 
				     unsigned int bit_number);

extern int reiserfs_bitmap_find_zero_bit (reiserfs_bitmap_t * bm, 
					  unsigned long * start);

extern void reiserfs_bitmap_zero (reiserfs_bitmap_t * bm);

extern void reiserfs_bitmap_fill (reiserfs_bitmap_t * bm);

extern void reiserfs_bitmap_invert (reiserfs_bitmap_t * bm);

extern unsigned int reiserfs_bitmap_ones (reiserfs_bitmap_t * bm);

extern unsigned int reiserfs_bitmap_zeros (reiserfs_bitmap_t * bm);

extern int reiserfs_bitmap_spread (reiserfs_filsys_t *);

extern int reiserfs_bitmap_block (reiserfs_filsys_t *, 
				  unsigned long block);

/* Backup copies. */
extern void reiserfs_bitmap_save (FILE * fp, 
				  reiserfs_bitmap_t *bm);

extern reiserfs_bitmap_t * reiserfs_bitmap_load (FILE * fp);

extern void reiserfs_bitmap_print (FILE * fp, reiserfs_filsys_t * fs, 
				   int silent);

extern void reiserfs_print_bmap_block (FILE * fp, int i, 
				       unsigned long block, 
				       char * map, int blocks, 
				       int silent, int blocksize);

#endif
