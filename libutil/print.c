/*
 * Copyright 1996-2004 by Hans Reiser, licensing governed by 
 * reiserfsprogs/README
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "util/print.h"
#include <string.h>

int util_user_confirmed (FILE * fp, char * q, char * yes) {
    char answer[4096];

    fprintf (fp, "%s", q);
    
    if (fgets(answer, sizeof(answer), stdin) == NULL ||
	(strlen(answer) != strlen(yes) || strcmp(yes, answer)))
    {
	return 0;
    }

    return 1;
}

#define BANNER						\
	"Copyright (C) 2001-2005 by Hans Reiser, "	\
	"licensing governed by reiserfsprogs/COPYING."

void util_print_banner(char *name) {
	fprintf(stderr, "%s %s\n%s\n", name, VERSION, BANNER);
}
