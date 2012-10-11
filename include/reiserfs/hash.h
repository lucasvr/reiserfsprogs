/*
 *  Copyright 2000-2004 by Hans Reiser, licensing governed by 
 *  reiserfsprogs/README
 */

#ifndef REISERFS_HASH_H
#define REISERFS_HASH_H

#include "misc/types.h"

#define UNSET_HASH 0 // read_super will guess about, what hash names
                     // in directories were sorted with

enum {
    REISERFS_HASH_TEA	= 0x1,
    REISERFS_HASH_YURA	= 0x2,
    REISERFS_HASH_R5	= 0x3,
    REISERFS_HASH_LAST
};

#define DEFAULT_HASH REISERFS_HASH_R5

/* hashes.c */
extern __u32 reiserfs_hash_keyed (const char *msg, int len);

extern __u32 reiserfs_hash_yura (const char *msg, int len);

extern __u32 reiserfs_hash_r5 (const char *msg, int len);

extern int reiserfs_hash_count (void);

extern int reiserfs_hash_correct (hashf_t *func, 
				  char * name, 
				  int namelen, 
				  __u32 offset);

extern int reiserfs_hash_find (char * name, 
			       int namelen, 
			       __u32 deh_offset, 
			       unsigned int code_to_try_first);

extern char *reiserfs_hash_name (unsigned int code);

extern int reiserfs_hash_code (hashf_t func);

extern hashf_t reiserfs_hash_func (unsigned int code);

extern hashf_t reiserfs_hash_get (char * hash);

extern __u32 reiserfs_hash_value (hashf_t func, char * name, int namelen);

#endif
