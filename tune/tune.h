/*
 * Copyright 2002-2004 by Hans Reiser, licensing governed by 
 * reiserfsprogs/README
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <asm/types.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/vfs.h>
#include <time.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <linux/kdev_t.h>
#include <sys/utsname.h>
#include <getopt.h>
#include <stdarg.h>

#include "io.h"
#include "misc.h"
#include "reiserfs_lib.h"
#include "../include/config.h"
#include "../version.h"

#if defined(HAVE_LIBUUID) && defined(HAVE_UUID_UUID_H)
#  include <uuid/uuid.h>
#endif

/* 
 * options
 */
#define OPT_SUPER_FORCE  0x0010
#define OPT_OLD_J  0x0100
#define OPT_NEW_J  0x0200
#define OPT_SIZEJ  0x0400
#define OPT_TMAXS  0x0800
#define OPT_OFSET  0x1000
#define OPT_SKIP_J  0x2000
#define OPT_KEEPO  0x4000
#define OPT_FORCE  0x8000
#define OPT_STANDARD  0x0020
