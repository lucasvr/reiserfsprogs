/*
 *  Copyright 2000-2004 by Hans Reiser, licensing governed by 
 *  reiserfsprogs/README
 */

#ifndef MISC_UNALIGNED_H
#define MISC_UNALIGNED_H

#ifdef HAVE_ASM_UNALIGNED_H
#  include <asm/unaligned.h>
#endif

#ifndef get_unaligned
#define get_unaligned(ptr)                              \
({                                                      \
        __typeof__(*(ptr)) __tmp;                       \
        memcpy(&__tmp, (ptr), sizeof(*(ptr)));      \
        __tmp;                                          \
})
#endif

#ifndef put_unaligned
#define put_unaligned(val, ptr)                         \
({                                                      \
        __typeof__(*(ptr)) __tmp = (val);               \
        memcpy((ptr), &__tmp, sizeof(*(ptr)));      \
        (void)0;                                        \
})
#endif

/* these operate on extent items, where you've got an array of ints
** at a possibly unaligned location.  These are a noop on ia32
**
** p is the array of __u32, i is the index into the array, v is the value
** to store there.
*/
#define d32_get(p, i)	 le32_to_cpu(get_unaligned((__u32 *)(p) + (i)))
#define d32_put(p, i, v) put_unaligned(cpu_to_le32(v), (__u32 *)(p) + (i))

#endif
