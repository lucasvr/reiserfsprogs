/*
 *  Copyright 2000-2004 by Hans Reiser, licensing governed by 
 *  reiserfsprogs/README
 */

#ifndef REISERFS_OBJMAP_H
#define REISERFS_OBJMAP_H

/* object identifier for root dir */
#define REISERFS_ROOT_OBJECTID 2
#define REISERFS_ROOT_PARENT_OBJECTID 1

extern int reiserfs_objmap_test (reiserfs_filsys_t * fs, __u32 objectid);

extern void reiserfs_objmap_set (reiserfs_filsys_t * fs, __u32 objectid);

extern void reiserfs_objmap_print (FILE * fp, reiserfs_filsys_t * fs);

#endif
