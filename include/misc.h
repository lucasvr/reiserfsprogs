/*
 * Copyright 1996-2002 Hans Reiser
 */

/* nothing abount reiserfs here */

#ifndef REISERFS_MISC_H
#define REISERFS_MISC_H

#include <endian.h>
#include "swab.h"
#include <linux/major.h>
#include <signal.h>
#include <unistd.h>

#define POSITION_FOUND          8
#define POSITION_NOT_FOUND      9

void check_memory_msg(void);
void die (char * fmt, ...) __attribute__ ((format (printf, 1, 2)));
void * getmem (int size);
void freemem (void * p);
void checkmem (char * p, int size);
void * expandmem (void * p, int size, int by);
int get_mem_size (char * p);
int is_mounted (char * device_name);
int is_mounted_read_only (char * device_name);
void check_and_free_mem (void);
char * kdevname (int dev);

typedef struct dma_info {
    int fd;
    struct stat64 stat;
    int support_type;
    int dma;
    __u64 speed;
} dma_info_t;

int prepare_dma_check(dma_info_t *dma_info);
int get_dma_info(dma_info_t *dma_info);
void clean_after_dma_check(int fd, dma_info_t *dma_info);

void print_how_far (FILE * fp, unsigned long * passed, unsigned long total, int inc, int quiet);
void print_how_fast (unsigned long total, 
		     unsigned long passed, int cursor_pos, int reset_time);
__u32 get_random (void);
int uuid_is_null(unsigned char * uuid);
int generate_random_uuid (unsigned char * uuid);
int uuid_is_correct (unsigned char * uuid);
int set_uuid (const unsigned char * text, unsigned char * UUID);

#include <asm/bitops.h>
#if __BYTE_ORDER == __LITTLE_ENDIAN
int le_set_bit (int nr, void * addr);
int le_clear_bit (int nr, void * addr);
int le_test_bit(int nr, const void * addr);
int le_find_first_zero_bit (const void *vaddr, unsigned size);
int le_find_next_zero_bit (const void *vaddr, unsigned size, unsigned offset);
# define cpu_to_le16(val)                 (val)
# define le16_to_cpu(val)                 (val)
# define cpu_to_le32(val)                 (val)
# define le32_to_cpu(val)                 (val)
# define cpu_to_le64(val)                 (val)
# define le64_to_cpu(val)                 (val)
#elif __BYTE_ORDER == __BIG_ENDIAN

# define cpu_to_le16(val)                 swab16(val)
# define le16_to_cpu(val)                 swab16(val)
# define cpu_to_le32(val)                 swab32(val)
# define le32_to_cpu(val)                 swab32(val)
# define cpu_to_le64(val)                 swab64(val)
# define le64_to_cpu(val)                 swab64(val)
#else
# error "nuxi/pdp-endian archs are not supported"
#endif



unsigned long count_blocks (char * filename, int blocksize);

mode_t get_st_mode (char * file_name);
dev_t get_st_rdev (char * file_name);
off64_t get_st_size (char * file_name);
blkcnt64_t get_st_blocks (char * file_name);


/* these are to access bitfield in endian safe manner */
__u16 mask16 (int from, int count);
__u32 mask32 (int from, int count);
__u64 mask64 (int from, int count);


int reiserfs_bin_search (void * key, void * base, __u32 num, int width,
			 __u32 *ppos, comparison_fn_t comp_func);

struct block_handler {
    __u32 blocknr;
    dev_t device;
};

int  blocklist__is_block_saved (struct block_handler ** base, __u32 * count, __u32 blocknr, 
    dev_t device, __u32 * position);
void blocklist__insert_in_position (void * block_h, void ** base, __u32 * count, 
    int elem_size, __u32 * position);
int blockdev_list_compare (const void * block1, const void * block2);
			
			 
#define set_bit_field_XX(XX,vp,val,from,count) \
{\
    __u##XX * p, tmp;\
\
    /* make sure that given value can be put in 'count' bits */\
    if (val > (1 << count))\
	die ("set_bit_field: val %d is too big for %d bits", val, count);\
\
    p = (__u##XX *)vp;\
    tmp = le##XX##_to_cpu (*p);\
\
    /* clear 'count' bits starting from 'from'-th one */\
    tmp &= ~mask##XX (from, count);\
\
    /* put given value in proper bits */\
    tmp |= (val << from);\
\
    *p = cpu_to_le##XX (tmp);\
}


#define get_bit_field_XX(XX,vp,from,count) \
\
    __u##XX * p, tmp;\
\
    p = (__u##XX *)vp;\
    tmp = le##XX##_to_cpu (*p);\
\
    /* clear all bits but 'count' bits starting from 'from'-th one */\
    tmp &= mask##XX (from, count);\
\
    /* get value written in specified bits */\
    tmp >>= from;\
    return tmp;


#ifndef MAJOR
#define MAJOR(rdev)      ((rdev)>>8)
#define MINOR(rdev)      ((rdev) & 0xff)
#endif /* MAJOR */

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

#endif /* REISERFS_MISC_H */
