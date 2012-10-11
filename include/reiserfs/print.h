/*
 *  Copyright 2000-2004 by Hans Reiser, licensing governed by 
 *  reiserfsprogs/README
 */

#ifndef REISERFS_PRINT_H
#define REISERFS_PRINT_H

#include <stdlib.h>

extern void reiserfs_warning (FILE * fp, const char * fmt, ...);

#define reiserfs_panic(fmt, list...) \
{\
	fflush (stdout);\
	fprintf (stderr, "%s %d %s\n", __FILE__, __LINE__, __FUNCTION__);\
	reiserfs_warning (stderr, (const char *)fmt, ## list);\
        reiserfs_warning (stderr, "\n" );\
        abort ();\
}
#define reiserfs_exit(val, fmt, list...) \
{\
	fflush (stdout);\
	reiserfs_warning (stderr, (const char *)fmt, ## list);\
        reiserfs_warning (stderr, "\n" );\
        exit (val);\
}

#endif
