/*
 * Copyright 1996-2004 by Hans Reiser, licensing governed by 
 * reiserfsprogs/README
 */

#ifndef MISC_BITOPS_H
#define MISC_BITOPS_H

#include "misc/types.h"
#include "misc/misc.h"

extern __u16 mask16 (int from, int count);
extern __u32 mask32 (int from, int count);
extern __u64 mask64 (int from, int count);

#define misc_set_bitfield_XX(XX,vp,val,from,count)				\
({										\
    __u##XX * p, tmp;								\
										\
    /* make sure that given value can be put in 'count' bits */			\
    if (val > (1 << count))							\
	misc_die ("misc_set_bitfield: val %d is too big for %d bits", val, count);	\
										\
    p = (__u##XX *)vp;								\
    tmp = le##XX##_to_cpu (*p);							\
										\
    /* clear 'count' bits starting from 'from'-th one */			\
    tmp &= ~mask##XX (from, count);						\
										\
    /* put given value in proper bits */					\
    tmp |= (val << from);							\
										\
    *p = cpu_to_le##XX (tmp);							\
})


#define misc_get_bitfield_XX(XX,vp,from,count)					\
({										\
    __u##XX * p, tmp;								\
										\
    p = (__u##XX *)vp;								\
    tmp = le##XX##_to_cpu (*p);							\
										\
    /* clear all bits but 'count' bits starting from 'from'-th one */		\
    tmp &= mask##XX (from, count);						\
										\
    /* get value written in specified bits */					\
    tmp >>= from;								\
    tmp;									\
})

extern inline int misc_set_bit (unsigned long long nr, 
				void * addr);

extern inline int misc_clear_bit (unsigned long long nr, 
				  void * addr);

extern inline int misc_test_bit(unsigned long long nr, 
				const void * addr);

extern inline unsigned long long 
misc_find_first_zero_bit (const void *vaddr, 
			  unsigned long long size);

extern inline unsigned long long 
misc_find_next_zero_bit (const void *vaddr, 
			 unsigned long long size, 
			 unsigned long long offset);

extern inline unsigned long long 
misc_find_next_set_bit(const void *vaddr, 
		       unsigned long long size, 
		       unsigned long long offset);

extern inline unsigned long long 
misc_find_first_set_bit (const void *vaddr, 
			 unsigned long long size);

#endif
