/*
 *  Copyright 2002-2003 by Hans Reiser, licensing governed by 
 *  reiserfsprogs/README
 */

#ifndef _REISERFS_SWAB_H_
#define _REISERFS_SWAB_H_

#include <endian.h>

#if __BYTE_ORDER == __BIG_ENDIAN

#define extern static
#define __BYTEORDER_HAS_U64__
#include <linux/byteorder/swab.h>
#undef extern

#else

#include <linux/byteorder/swab.h>

#endif

#endif /* _REISERFS_SWAB_H_ */
