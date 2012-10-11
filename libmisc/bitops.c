/*
 * Copyright 1996-2004 by Hans Reiser, licensing governed by 
 * reiserfsprogs/README
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "misc/bitops.h"
#include "misc/types.h"

/* Only le bitops operations are used. */
inline int misc_set_bit (unsigned long long nr, void * addr) {
    __u8 * p, mask;
    int retval;

    p = (__u8 *)addr;
    p += nr >> 3;
    mask = 1 << (nr & 0x7);
    /*cli();*/
    retval = (mask & *p) != 0;
    *p |= mask;
    /*sti();*/
    return retval;
}


inline int misc_clear_bit (unsigned long long nr, void * addr) {
    __u8 * p, mask;
    int retval;

    p = (__u8 *)addr;
    p += nr >> 3;
    mask = 1 << (nr & 0x7);
    /*cli();*/
    retval = (mask & *p) != 0;
    *p &= ~mask;
    /*sti();*/
    return retval;
}

inline int misc_test_bit(unsigned long long nr, const void * addr) {
    __u8 * p, mask;

    p = (__u8 *)addr;
    p += nr >> 3;
    mask = 1 << (nr & 0x7);
    return ((mask & *p) != 0);
}

inline unsigned long long misc_find_first_zero_bit (const void *vaddr, 
    unsigned long long size) 
{
    const __u8 *p = vaddr, *addr = vaddr;
    unsigned long long res;

    if (!size)
        return 0;

    size = (size >> 3) + ((size & 0x7) > 0);
    while (*p++ == 255) {
        if (--size == 0)
            return (unsigned long long)(p - addr) << 3;
    }

    --p;
    for (res = 0; res < 8; res++)
        if (!misc_test_bit (res, p))
            break;
    return res + (p - addr) * 8;
}


inline unsigned long long misc_find_next_zero_bit (const void *vaddr, 
    unsigned long long size, unsigned long long offset) 
{
    const __u8 *addr = vaddr;
    const __u8 *p = addr + (offset >> 3);
    int bit = offset & 7;
    unsigned long long res;

    if (offset >= size)
        return size;

    if (bit) {
        /* Look for zero in first char */
        for (res = bit; res < 8; res++)
            if (!misc_test_bit (res, p))
                return res + (p - addr) * 8;
        p++;
    }
    /* No zero yet, search remaining full bytes for a zero */
    res = misc_find_first_zero_bit (p, size - 8 * (p - addr));
    return res + (p - addr) * 8;
}

inline unsigned long long misc_find_first_set_bit (const void *vaddr, 
    unsigned long long size) 
{
    const __u8 *p = vaddr, *addr = vaddr;
    unsigned long long res;

    if (!size)
        return 0;

    size = (size >> 3) + ((size & 0x7) > 0);
    while (*p++ == 0) {
        if (--size == 0)
            return (unsigned long long)(p - addr) << 3;
    }

    --p;
    for (res = 0; res < 8; res++)
        if (misc_test_bit (res, p))
            break;

    return res + (p - addr) * 8;
}

inline unsigned long long misc_find_next_set_bit(const void *vaddr, 
    unsigned long long size, unsigned long long offset)
{
    const __u8 *addr = vaddr;
    const __u8 *p = addr + (offset >> 3);
    int bit = offset & 7;
    unsigned long long res;

    if (offset >= size)
        return size;

    if (bit) {
        /* Look for zero in first char */
        for (res = bit; res < 8; res++)
            if (misc_test_bit (res, p))
                return res + (p - addr) * 8;
        p++;
    }
    /* No set bit yet, search remaining full bytes for a 1 */
    res = misc_find_first_set_bit (p, size - 8 * (p - addr));
    return res + (p - addr) * 8;
}

/* there are masks for certain bits  */
__u16 mask16 (int from, int count)
{
    __u16 mask;


    mask = (0xffff >> from);
    mask <<= from;
    mask <<= (16 - from - count);
    mask >>= (16 - from - count);
    return mask;
}


__u32 mask32 (int from, int count)
{
    __u32 mask;


    mask = (0xffffffff >> from);
    mask <<= from;
    mask <<= (32 - from - count);
    mask >>= (32 - from - count);
    return mask;
}


__u64 mask64 (int from, int count)
{
    __u64 mask;


    mask = (0xffffffffffffffffLL >> from);
    mask <<= from;
    mask <<= (64 - from - count);
    mask >>= (64 - from - count);
    return mask;
}
