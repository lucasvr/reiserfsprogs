/*
 * Copyright 1996-2004 by Hans Reiser, licensing governed by 
 * reiserfsprogs/README
 */

#ifndef MISC_MALLOC_H
#define MISC_MALLOC_H

extern void *misc_getmem (int size);

extern void *misc_malloc(int size);

extern void misc_freemem (void * p);

extern void misc_checkmem (char * p, int size);

extern void *misc_expandmem (void * p, int size, int by);

extern unsigned int misc_memsize (char * p);

#endif
