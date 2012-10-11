/*
 * Copyright 1996-2004 by Hans Reiser, licensing governed by 
 * reiserfsprogs/README
 */

/* nothing abount reiserfs here */

#ifndef MISC_H
#define MISC_H

#include "misc/types.h"

/* n must be power of 2 */
#define MISC_UP(x,n)   (((x)+(n)-1u) / (n) * (n))
#define MISC_DOWN(x,n) ((x) / (n) * (n))

/* to be ok for alpha etc we have to align structures to 8 byte boundary. */
#define MISC_ROUND_UP(x) MISC_UP(x,8LL)

extern void misc_die (char * fmt, ...) __attribute__ ((format (printf, 1, 2)));

typedef int (*dir_walk_func_t) (char *path, void *data);
extern int misc_dir_walk(char *path, dir_walk_func_t func, void *data);

extern __u32 misc_random (void);

typedef int (*misc_comp_func_t) (const void *, const void *);

extern int misc_device_rdev_match(char *path, void *data);

extern int misc_bin_search (const void * key, void * base, 
			    __u32 num, int width, int *ppos, 
			    misc_comp_func_t comp_func);

extern void blocklist__insert_in_position (void * block_h, void ** base, 
					   __u32 * count, int elem_size, 
					   int * position);

extern int blockdev_list_compare (const void * block1, const void * block2);

#endif
