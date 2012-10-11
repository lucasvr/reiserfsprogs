/*
 *  Copyright 2000-2004 by Hans Reiser, licensing governed by 
 *  reiserfsprogs/README
 */

#ifndef REISERFS_LIBREISER4_H
#define REISERFS_LIBREISER4_H

#ifdef __cplusplus
extern "C" {
#endif

#include <reiserfs/types.h>
#include <reiserfs/bitmap.h>
#include <reiserfs/buffer.h>
#include <reiserfs/filesystem.h>
#include <reiserfs/journal.h>
#include <reiserfs/super.h>
#include <reiserfs/tree.h>
#include <reiserfs/key.h>
#include <reiserfs/node.h>
#include <reiserfs/leaf.h>
#include <reiserfs/direntry.h>
#include <reiserfs/internal.h>
#include <reiserfs/extent.h>
#include <reiserfs/stat.h>
#include <reiserfs/fix_node.h>
#include <reiserfs/tree_balance.h>
#include <reiserfs/internal_balance.h>
#include <reiserfs/leaf_balance.h>
#include <reiserfs/badblock.h>
#include <reiserfs/objmap.h>
#include <reiserfs/policy.h>
#include <reiserfs/print.h>
#include <reiserfs/hash.h>

#ifdef LIBREISERFS_READY
extern const char *libreiserfs_version(void);
extern int libreiserfs_max_interface_version(void);
extern int libreiserfs_min_interface_version(void);
#endif

#ifdef __cplusplus
}
#endif
#endif
