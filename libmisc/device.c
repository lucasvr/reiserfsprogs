/*
 * Copyright 1996-2004 by Hans Reiser, licensing governed by 
 * reiserfsprogs/README
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "misc/device.h"
#include "misc/misc.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>

#define MISC_DEVICE_STAT(Field, Type)					\
Type misc_device_##Field(char *device) {				\
	struct stat st;							\
									\
	if (stat(device, &st) == 0)					\
		return st.st_##Field;					\
									\
	fprintf(stderr, "Stat of the device '%s' failed.\n", device);	\
	return (Type)-1;						\
}

MISC_DEVICE_STAT(mode, mode_t);
MISC_DEVICE_STAT(rdev, dev_t);

int misc_device_valid_offset(int fd, long long int offset) {
    long long int res;
    char ch;

    /*res = reiserfs_llseek (fd, offset, 0);*/
    res = lseek (fd, (off_t)offset, SEEK_SET);
    if (res < 0)
	return 0;

    /* if (read (fd, &ch, 1) < 0) does not wirk on files */
    if (read (fd, &ch, 1) < 1)
	return 0;


    return 1;
}

/* To not have problem with last sectors on the block device when switching 
   to smaller one. */
#define MAX_BS (64 * 1024)

/* calculates number of blocks in a file. Returns 0 for "sparse"
   regular files and files other than regular files and block devices */
unsigned long misc_device_count_blocks (char * filename, int blocksize)
{
    long long int high, low;
    int fd;

    fd = open (filename, O_RDONLY);
    if (fd == -1) {
	fprintf(stderr, "Failed to open '%s': %s.\n", filename, strerror(errno));
	return 0;
    }

    if (!S_ISBLK(misc_device_mode(filename)) && 
	!S_ISREG(misc_device_mode(filename))) 
    {
	close(fd);
	return 0;
    }

#ifdef BLKGETSIZE64
    {
	unsigned long long size;
	unsigned long sz;
	
	if (ioctl (fd, BLKGETSIZE64, &size) >= 0) {
	    size = (size / MAX_BS) * MAX_BS / blocksize;
	    sz = size;
	    if ((unsigned long long)sz != size)
		    misc_die ("misc_device_count_blocks: block device too large");

	    close(fd);
	    return sz;
	}
    }
#endif


#ifdef BLKGETSIZE
    {
	unsigned long long size;
	unsigned long sz;
	
	if (ioctl (fd, BLKGETSIZE, &sz) >= 0) {
	    size = sz;

	    close(fd);
	    return (size * 512 / MAX_BS) * MAX_BS / blocksize;
	}
    }
#endif

    low = 0;
    for( high = 1; misc_device_valid_offset (fd, high); high *= 2 )
	low = high;
    while (low < high - 1) {
	const long long int mid = ( low + high ) / 2;

	if (misc_device_valid_offset (fd, mid))
	    low = mid;
	else
	    high = mid;
    }
    
    misc_device_valid_offset (fd, 0);

    close (fd);
    return (low + 1) * MAX_BS / MAX_BS / blocksize;
}

char misc_device_typec (unsigned short mode)
{
    if (S_ISBLK ((mode_t)mode))
	return 'b';
    if (S_ISCHR (mode))
	return 'c';
    if (S_ISDIR (mode))
	return 'd';
    if (S_ISREG (mode))
	return '-';
    if (S_ISFIFO (mode))
	return 'p';
    if (S_ISLNK (mode))
	return 'l';
    if (S_ISSOCK (mode))
	return 's';
    return '?';
}

