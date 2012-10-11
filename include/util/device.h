/*
 * Copyright 1996-2004 by Hans Reiser, licensing governed by 
 * reiserfsprogs/README
 */

#ifndef UTIL_DEVICE_H
#define UTIL_DEVICE_H

#include "misc/types.h"
#include <linux/major.h>
#include <sys/types.h>
#include <stdio.h>

#ifndef major
#define major(rdev)      ((rdev)>>8)
#define minor(rdev)      ((rdev) & 0xff)
#endif /* major */

#ifndef SCSI_DISK_MAJOR
#define SCSI_DISK_MAJOR(maj) ((maj) == SCSI_DISK0_MAJOR || \
			     ((maj) >= SCSI_DISK1_MAJOR && (maj) <= SCSI_DISK7_MAJOR))
#endif /* SCSI_DISK_MAJOR */
    
#ifndef SCSI_BLK_MAJOR
#define SCSI_BLK_MAJOR(maj)  (SCSI_DISK_MAJOR(maj) || (maj) == SCSI_CDROM_MAJOR)
#endif /* SCSI_BLK_MAJOR */

#ifndef IDE_DISK_MAJOR
#ifdef IDE9_MAJOR
#define IDE_DISK_MAJOR(maj) ((maj) == IDE0_MAJOR || (maj) == IDE1_MAJOR || \
			     (maj) == IDE2_MAJOR || (maj) == IDE3_MAJOR || \
			     (maj) == IDE4_MAJOR || (maj) == IDE5_MAJOR || \
			     (maj) == IDE6_MAJOR || (maj) == IDE7_MAJOR || \
			     (maj) == IDE8_MAJOR || (maj) == IDE9_MAJOR)
#else
#define IDE_DISK_MAJOR(maj) ((maj) == IDE0_MAJOR || (maj) == IDE1_MAJOR || \
			     (maj) == IDE2_MAJOR || (maj) == IDE3_MAJOR || \
			     (maj) == IDE4_MAJOR || (maj) == IDE5_MAJOR)
#endif /* IDE9_MAJOR */
#endif /* IDE_DISK_MAJOR */

typedef enum mount_flags {
	MF_NOT_MOUNTED  = 0x0,
	MF_RO		= 0x1,
	MF_RW		= 0x2
} mount_flags_t;

extern FILE * util_file_open (char * filename, char * option);

typedef struct util_device_dma {
    int fd;
    int support_type;
    int dma;
    __u64 speed;
    dev_t st_rdev;
} util_device_dma_t;

extern int util_device_dma_prep(util_device_dma_t *dma_info);

extern int util_device_get_dma(util_device_dma_t *dma_info);

extern void util_device_dma_fini(int fd, util_device_dma_t *dma_info);

extern int util_device_formatable (char * device_name, int force);

extern int util_root_mounted(char *device);

extern int util_device_mounted(char *device);

extern int util_file_ro(char *file);

#endif
