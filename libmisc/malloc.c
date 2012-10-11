/*
 * Copyright 1996-2004 by Hans Reiser, licensing governed by 
 * reiserfsprogs/README
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "misc/malloc.h"
#include "misc/misc.h"

#include <stdlib.h>
#include <string.h>

#define MEM_BEGIN "_mem_begin_"
#define MEM_END "mem_end"
#define MEM_FREED "__free_"
#define CONTROL_SIZE (strlen (MEM_BEGIN) + 1 + sizeof (int) + strlen (MEM_END) + 1)


unsigned int misc_memsize(char *p) {
    char *begin;
    
    begin = p - strlen (MEM_BEGIN) - 1 - sizeof(int);
    return *(int *)(begin + strlen (MEM_BEGIN) + 1);
}


void misc_checkmem (char * p, int size)
{
    char * begin;
    char * end;
  
    begin = p - strlen (MEM_BEGIN) - 1 - sizeof (int);
    if (strcmp (begin, MEM_BEGIN))
	misc_die ("misc_checkmem: memory corrupted - invalid head sign");

    if (*(int *)(begin + strlen (MEM_BEGIN) + 1) != size)
	misc_die ("misc_checkmem: memory corrupted - invalid size");

    end = begin + size + CONTROL_SIZE - strlen (MEM_END) - 1;
    if (strcmp (end, MEM_END))
	misc_die ("misc_checkmem: memory corrupted - invalid end sign");
}


void *misc_getmem (int size)
{
    char * mem;

    if ((mem = misc_malloc(size)) == NULL)
	misc_die ("misc_getmem: no more memory (%d)", size);
    
    memset (mem, 0, size);
//    misc_checkmem (mem, size);

    return mem;
}

void *misc_malloc(int size) {
    char * p;
    char * mem;

    p = (char *)malloc (CONTROL_SIZE + size);
    if (!p)
	misc_die ("misc_getmem: no more memory (%d)", size);

    /* Write the MEM_BEGIN magic in the beginning of allocated memory. */
    strcpy (p, MEM_BEGIN);
    p += strlen (MEM_BEGIN) + 1;
    /* Write the size after the magic. */
    *(int *)p = size;
    p += sizeof (int);
    mem = p;
    p += size;
    strcpy (p, MEM_END);

    return mem;
}

void * misc_expandmem (void * vp, int size, int by)
{
    int allocated;
    char * mem, * p = vp;
    int expand_by = by;

    if (p) {
	misc_checkmem (p, size);
	allocated = CONTROL_SIZE + size;
	p -= (strlen (MEM_BEGIN) + 1 + sizeof (int));
    } else {
	allocated = 0;
	/* add control bytes to the new allocated area */
	expand_by += CONTROL_SIZE;
    }
    p = realloc (p, allocated + expand_by);
    if (!p)
	misc_die ("misc_expandmem: no more memory (%d)", size);
    if (!vp) {
	strcpy (p, MEM_BEGIN);
    }
    mem = p + strlen (MEM_BEGIN) + 1 + sizeof (int);

    *(int *)(p + strlen (MEM_BEGIN) + 1) = size + by;
    /* fill new allocated area by 0s */
    if(by > 0)
        memset (mem + size, 0, by);
    strcpy (mem + size + by, MEM_END);
//    misc_checkmem (mem, size + by);

    return mem;
}


void misc_freemem (void * vp)
{
    char * p = vp;
    int size;
  
    if (!p)
	return;
    size = misc_memsize (vp);
    misc_checkmem (p, size);

    p -= (strlen (MEM_BEGIN) + 1 + sizeof (int));
    strcpy (p, MEM_FREED);
    strcpy (p + size + CONTROL_SIZE - strlen (MEM_END) - 1, MEM_FREED);
    free (p);
}


