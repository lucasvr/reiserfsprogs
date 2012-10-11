/*
 *  Copyright 2000-2004 by Hans Reiser, licensing governed by 
 *  reiserfsprogs/README
 */

#ifndef UTIL_BADBLOCK_H
#define UTIL_BADBLOCK_H

#include "reiserfs/libreiserfs.h"

extern int util_badblock_load (reiserfs_filsys_t * fs, 
			       char * badblocks_file);

#endif
