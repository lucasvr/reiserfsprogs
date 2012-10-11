/*
 *  Copyright 2000-2004 by Hans Reiser, licensing governed by 
 *  reiserfsprogs/README
 */

#ifndef REISERFS_BADBLOCK_H
#define REISERFS_BADBLOCK_H

#include "reiserfs/types.h"

#define REISERFS_BAD_DID	1
#define REISERFS_BAD_OID  (__u32)-1

typedef void (*badblock_func_t) (reiserfs_filsys_t *fs, 
				 reiserfs_path_t *badblock_path, 
				 void *data);

extern void reiserfs_badblock_extract(reiserfs_filsys_t *fs, 
				   reiserfs_path_t *badblock_path, 
				   void *data);

extern void reiserfs_badblock_flush (reiserfs_filsys_t * fs, 
				     int no_badblock_in_tree_yet);

extern void reiserfs_badblock_traverse(reiserfs_filsys_t * fs, 
				       badblock_func_t action, 
				       void *data);

#endif
