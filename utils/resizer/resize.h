/* 
 * Copyright 2000-2004 by Hans Reiser, licensing governed by 
 * reiserfsprogs/README
 */

#include "reiserfs/libreiserfs.h"

#define print_usage_and_exit() {\
 fprintf (stderr, "Usage: %s  [-s[+|-]#[G|M|K]] [-fqvV] device\n\n", argv[0]);\
 exit(16);\
}

/* reiserfs_resize.c */
extern reiserfs_bh_t * g_sb_bh;
extern char * g_progname;

extern int opt_force;
extern int opt_verbose;
extern int opt_nowrite;
extern int opt_safe;

/* fe.c */
extern int resize_fs_online(char * devname, long long int blocks);

/* do_shrink.c */
extern int shrink_fs(reiserfs_filsys_t *, long long int blocks);
