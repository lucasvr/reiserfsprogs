/*
 *  Copyright 2000-2004 by Hans Reiser, licensing governed by 
 *  reiserfsprogs/README
 */

#ifndef UTIL_PRINT_H
#define UTIL_PRINT_H

#include <stdio.h>

extern int util_user_confirmed(FILE * fp, char * q, char * yes);

extern void util_print_banner(char *name);

#endif
