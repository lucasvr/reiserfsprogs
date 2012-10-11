/*
 * Copyright 1996-2004 by Hans Reiser, licensing governed by 
 * reiserfsprogs/README
 */

#ifndef UTIL_MISC_H
#define UTIL_MISC_H

#include <stdio.h>

extern void util_misc_progress (FILE * fp, 
				unsigned long *passed, 
				unsigned long total, 
				unsigned int inc, 
				int forward);

extern void util_misc_speed (FILE *fp, 
			     unsigned long total, 
			     unsigned long passed, 
			     int cursor_pos, 
			     int reset_time);


extern void util_misc_print_name (FILE * fp, char * name, int len);

extern void util_misc_erase_name (FILE * fp, int len);

extern void util_misc_fini_name (FILE *fp);

#endif
