/*
 * Copyright 1996-2004 by Hans Reiser, licensing governed by 
 * reiserfsprogs/README
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "misc/malloc.h"
#include "misc/misc.h"

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <time.h>
#include <dirent.h>
#include <sys/stat.h>
#include <string.h>

void misc_die (char * fmt, ...)
{
    static char buf[4096];
    va_list args;

    va_start (args, fmt);
    vsprintf (buf, fmt, args);
    va_end (args);

    fprintf (stderr, "\n%s\n", buf);
    abort ();
}


__u32 misc_random (void)
{
    srandom (time (0));
    return random ();
}

/* this implements binary search in the array 'base' among 'num' elements each
   of those is 'width' bytes long. 'comp_func' is used to compare keys */
int misc_bin_search (const void * key, void * base, __u32 num, int width,
		     int * ppos, misc_comp_func_t comp_func)
{
    __u32 rbound, lbound, j;
    int ret;

    if (num == 0 || base == NULL) {
	/* objectid map may be 0 elements long */
	*ppos = 0;
	return 0;
    }

    lbound = 0;
    rbound = num - 1;

    for (j = (rbound + lbound) / 2; lbound <= rbound; j = (rbound + lbound) / 2) {
	ret =  comp_func ((void *)((char *)base + j * width), key ) ;
	if (ret < 0) { /* second is greater */
	    lbound = j + 1;
	    continue;

	} else if (ret > 0) { /* first is greater */
	    if (j == 0)
	    	break;
	    rbound = j - 1;
	    continue;
	} else { /* equal */
	    *ppos = j;
	    return 1;
	}
    }

    *ppos = lbound;
    return 0;
}


#define BLOCKLIST__ELEMENT_NUMBER 10

/*element is block number and device*/
int blockdev_list_compare (const void * block1, const void * block2) {
    if (*(__u32 *)block1 < *(__u32 *)block2)
        return -1;
    if (*(__u32 *)block1 > *(__u32 *)block2)
        return 1;
        
    if (*((__u32 *)block1 + 1) < *((__u32 *)block2 + 1))
        return -1;        
    if (*((__u32 *)block1 + 1) > *((__u32 *)block2 + 1))
        return 1;
        
    return 0;
}

void blocklist__insert_in_position (void *elem, void **base, __u32 *count, 
    int elem_size, int *position) 
{
    if (elem_size == 0)
    	return;
    	
    if (*base == NULL)
        *base = misc_getmem (BLOCKLIST__ELEMENT_NUMBER * elem_size);
    
    if (*count == misc_memsize((void *)*base) / elem_size)
        *base = misc_expandmem (*base, misc_memsize((void *)*base), 
                        BLOCKLIST__ELEMENT_NUMBER * elem_size);
    
    if (*position < *count) {
        memmove (*base + (*position + 1), 
                 *base + (*position),
                 (*count - *position) * elem_size);
    }

    memcpy (*base + (char) *position * elem_size, elem, elem_size);
    *count+=1;
}

int misc_device_rdev_match(char *path, void *data) {
    dev_t *rdev = (dev_t *)data;
    struct stat st;

    /* If stat fails just continue walking. */
    if (stat(path, &st))
	return 0;

    /* Only block files are interesting. */
    if (!S_ISBLK(st.st_mode))
	return 0;

    /* Matched rdev wanted. */
    if (st.st_rdev != *rdev)
	return 0;

    /* Stop walking. */
    return 1;
}


/* The method walks through all subtree of the path, 
   open all directories and walk through them recursevely.

   Return values:
   positive: stop walking;
   negative: error occured.
 */
int misc_dir_walk(char *path, dir_walk_func_t func, void *data) {
	struct dirent *dirent;
	struct stat st;
	off_t pos;
	DIR *dir;
	int len;
	int res;

	strcat(path, "/");

	if (!(dir = opendir(path)))
		return 0;
	
	len = strlen(path);
	
	while ((dirent = readdir(dir)) != NULL) {
		path[len] = 0;

		if (strcmp(dirent->d_name, ".") == 0 || 
		    strcmp(dirent->d_name, "..") == 0)
		{
			continue;
		}

		strcat(path, dirent->d_name);

		if ((res = func(path, data))) {
			closedir(dir);
			return res;
		}

		if (stat(path, &st) != 0)
			continue;

		if ((S_ISDIR(st.st_mode))) {
			pos = telldir(dir);
			closedir(dir);
			
			if ((res = misc_dir_walk(path, func, data)))
				return res;

			path[len] = 0;
			if (!(dir = opendir(path)))
				return -1;

			seekdir(dir, pos);
		}
	}

	closedir(dir);
	return 0;
}

