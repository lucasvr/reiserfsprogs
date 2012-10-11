/*
 *  Copyright 2002-2004 by Hans Reiser, licensing governed by 
 *  reiserfsprogs/README
 */

#ifndef REISERFSPROGS_SWAB_H
#define REISERFSPROGS_SWAB_H

#include <endian.h>
#include <asm/types.h>

#define __swab16(x) \
({ \
        __u16 __x = (x); \
        ((__u16)( \
                (((__u16)(__x) & (__u16)0x00ffU) << 8) | \
                (((__u16)(__x) & (__u16)0xff00U) >> 8) )); \
})

#define __swab32(x) \
({ \
        __u32 __x = (x); \
        ((__u32)( \
                (((__u32)(__x) & (__u32)0x000000ffUL) << 24) | \
                (((__u32)(__x) & (__u32)0x0000ff00UL) <<  8) | \
                (((__u32)(__x) & (__u32)0x00ff0000UL) >>  8) | \
                (((__u32)(__x) & (__u32)0xff000000UL) >> 24) )); \
})

#define __swab64(x) \
({ \
        __u64 __x = (x); \
        ((__u64)( \
                (__u64)(((__u64)(__x) & (__u64)0x00000000000000ffULL) << 56) | \
                (__u64)(((__u64)(__x) & (__u64)0x000000000000ff00ULL) << 40) | \
                (__u64)(((__u64)(__x) & (__u64)0x0000000000ff0000ULL) << 24) | \
                (__u64)(((__u64)(__x) & (__u64)0x00000000ff000000ULL) <<  8) | \
                (__u64)(((__u64)(__x) & (__u64)0x000000ff00000000ULL) >>  8) | \
                (__u64)(((__u64)(__x) & (__u64)0x0000ff0000000000ULL) >> 24) | \
                (__u64)(((__u64)(__x) & (__u64)0x00ff000000000000ULL) >> 40) | \
                (__u64)(((__u64)(__x) & (__u64)0xff00000000000000ULL) >> 56) )); \
})


#if __BYTE_ORDER == __LITTLE_ENDIAN

# define cpu_to_le16(val)                 (val)
# define le16_to_cpu(val)                 (val)
# define cpu_to_le32(val)                 (val)
# define le32_to_cpu(val)                 (val)
# define cpu_to_le64(val)                 (val)
# define le64_to_cpu(val)                 (val)

#elif __BYTE_ORDER == __BIG_ENDIAN

# define cpu_to_le16(val)                 __swab16(val)
# define le16_to_cpu(val)                 __swab16(val)
# define cpu_to_le32(val)                 __swab32(val)
# define le32_to_cpu(val)                 __swab32(val)
# define cpu_to_le64(val)                 __swab64(val)
# define le64_to_cpu(val)                 __swab64(val)

#else
# error "nuxi/pdp-endian archs are not supported"
#endif

#endif /* REISERFS_SWAB_H */
