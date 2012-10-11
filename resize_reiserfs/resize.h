/* 
 * Copyright 2000-2003 by Hans Reiser, licensing governed by 
 * reiserfsprogs/README
 */

#define _GNU_SOURCE

#include <sys/types.h>
#include <sys/stat.h>
#include <asm/types.h>
#include <errno.h>
#include <stdio.h>
#include <mntent.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>

#if __GLIBC__ >= 2
#include <sys/mount.h>
#else
#include <linux/fs.h>
#endif


#include "io.h"
#include "misc.h"
#include "reiserfs_lib.h"
#include "../include/config.h"
#include "../version.h"


#define print_usage_and_exit() {\
 fprintf (stderr, "Usage: %s  [-s[+|-]#[G|M|K]] [-fqv] device\n\n", argv[0]);\
 exit(16);\
}

#define DIE(form, args...) die("%s: " form "\n", g_progname , ## args)

/* reiserfs_resize.c */
extern struct buffer_head * g_sb_bh;
extern char * g_progname;

extern int opt_force;
extern int opt_verbose;
extern int opt_nowrite;
extern int opt_safe;

/* fe.c */
int resize_fs_online(char * devname, unsigned long blocks);

/* do_shrink.c */
int shrink_fs(reiserfs_filsys_t *, unsigned long blocks);
