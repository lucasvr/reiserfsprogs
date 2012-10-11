/*
 *  Copyright 2000-2004 by Hans Reiser, licensing governed by 
 *  reiserfsprogs/README
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "reiserfs/libreiserfs.h"

#ifdef LIBREISERFS_READY
int libreiserfs_max_interface_version(void) {
	return LIBREISER4_MAX_INTERFACE_VERSION;
}

int libreiserfs_min_interface_version(void) {
	return LIBREISER4_MIN_INTERFACE_VERSION;
}

const char *libreiserfs_version(void) {
	return VERSION;
}
#endif
