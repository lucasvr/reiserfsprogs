/*
 * Copyright 1996-2004 by Hans Reiser, licensing governed by 
 * reiserfsprogs/README
 */

#ifndef REISERFS_BUFFER_H
#define REISERFS_BUFFER_H

#include "reiserfs/types.h"

#define BH_Uptodate	0
#define BH_Dirty	1
#define BH_Lock		2
#define BH_Do_not_flush 3

#define reiserfs_buffer_uptodate(bh)	 misc_test_bit(BH_Uptodate, &(bh)->b_state)
#define reiserfs_buffer_mkuptodate(bh,i) misc_set_bit(BH_Uptodate, &(bh)->b_state)

#define reiserfs_buffer_isdirty(bh)	 misc_test_bit(BH_Dirty, &(bh)->b_state)
#define reiserfs_buffer_isclean(bh)	 !misc_test_bit(BH_Dirty, &(bh)->b_state)
#define reiserfs_buffer_mkdirty(bh)	 misc_set_bit(BH_Dirty, &(bh)->b_state)
#define reiserfs_buffer_mkclean(bh)	 misc_clear_bit(BH_Dirty, &(bh)->b_state)

#define reiserfs_buffer_noflush(bh)	 misc_test_bit(BH_Do_not_flush, &(bh)->b_state)
#define reiserfs_buffer_mknoflush(bh)	 misc_set_bit(BH_Do_not_flush, &(bh)->b_state)
#define reiserfs_buffer_clnoflush(bh)	 misc_clear_bit(BH_Do_not_flush, &(bh)->b_state)

extern reiserfs_bh_t * reiserfs_buffer_open (int, unsigned long, unsigned long);
extern reiserfs_bh_t * reiserfs_buffer_find (int, unsigned long, unsigned long);
extern reiserfs_bh_t * reiserfs_buffer_read (int, unsigned long, unsigned long);

extern int reiserfs_buffer_write (reiserfs_bh_t *);

extern void reiserfs_buffer_close (reiserfs_bh_t *);

extern void reiserfs_buffer_forget (reiserfs_bh_t *);

extern void reiserfs_buffer_flush_all(int);

extern void reiserfs_buffer_free_all (void);

extern void reiserfs_buffer_invalidate_all (int);

#endif
