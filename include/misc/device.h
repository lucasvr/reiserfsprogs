/*
 * Copyright 1996-2004 by Hans Reiser, licensing governed by 
 * reiserfsprogs/README
 */

#ifndef MISC_DEVICE_H
#define MISC_DEVICE_H

#include <sys/types.h>

extern int misc_device_valid_offset(int fd, long long int offset);

extern unsigned long misc_device_count_blocks (char * filename, 
					       int blocksize);

extern char misc_device_typec (unsigned short mode);

#define STAT_FIELD_H(Field, Type)			\
Type misc_device_##Field(char *device);

STAT_FIELD_H(mode, mode_t);
STAT_FIELD_H(rdev, dev_t);

#endif
