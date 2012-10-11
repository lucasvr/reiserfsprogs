/*
 * Copyright 1996-2004 by Hans Reiser, licensing governed by 
 * reiserfsprogs/README
 */

#ifndef MISC_TYPES_H
#define MISC_TYPES_H

#include <string.h>

typedef unsigned char           __u8;
typedef unsigned short int      __u16;
typedef unsigned int            __u32;
typedef unsigned long long int  __u64;

#define INVAL_PTR	(void *)-1
#define MAX_INT		2147483647
#define LONG_LONG_MAX	9223372036854775807LL
#define LONG_LONG_MIN	(-LONG_LONG_MAX - 1LL)

#endif
