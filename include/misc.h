/*
 * Copyright 1996-2002 Hans Reiser
 */

/* nothing abount reiserfs here */
#include <endian.h>
#include "swab.h"

#define POSITION_FOUND          8
#define POSITION_NOT_FOUND      9

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


void print_how_far (FILE * fp, unsigned long * passed, unsigned long total, int inc, int quiet);
void print_how_fast (unsigned long total, 
		     unsigned long passed, int cursor_pos, int reset_time);
__u32 get_random (void);
int uuid_is_null(unsigned char * uuid);
int generate_random_uuid (unsigned char * uuid);
int uuid_is_correct (unsigned char * uuid);
int set_uuid (const unsigned char * text, unsigned char * UUID);

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
# define set_bit(nr, addr)                le_set_bit(nr, addr)
# define clear_bit(nr, addr)              le_clear_bit(nr, addr)
# define test_bit(nr, addr)               le_test_bit(nr, addr)
# define find_first_zero_bit(vaddr, size) le_find_first_zero_bit(vaddr, size)
# define find_next_zero_bit(vaddr, size, off) \
                                          le_find_next_zero_bit(vaddr, size, off)
#elif __BYTE_ORDER == __BIG_ENDIAN

int be_set_bit (int nr, void * addr);
int be_clear_bit (int nr, void * addr);
int be_test_bit(int nr, const void * addr);
int be_find_first_zero_bit (const void *vaddr, unsigned size);
int be_find_next_zero_bit (const void *vaddr, unsigned size, unsigned offset);
# define cpu_to_le16(val)                 swab16(val)
# define le16_to_cpu(val)                 swab16(val)
# define cpu_to_le32(val)                 swab32(val)
# define le32_to_cpu(val)                 swab32(val)
# define cpu_to_le64(val)                 swab64(val)
# define le64_to_cpu(val)                 swab64(val)
# define set_bit(nr, addr)                be_set_bit(nr, addr)
# define clear_bit(nr, addr)              be_clear_bit(nr, addr)
# define test_bit(nr, addr)               be_test_bit(nr, addr)
# define find_first_zero_bit(vaddr, size) be_find_first_zero_bit(vaddr, size)
# define find_next_zero_bit(vaddr, size, off) \
                                          be_find_next_zero_bit(vaddr, size, off)
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

int  blocklist__is_block_saved (struct block_handler ** base, __u32 * count, __u32 blocknr, dev_t device, __u32 * position);
void blocklist__insert_in_position (void ** base, __u32 * count, void * block_h, int elem_size, __u32 * position);
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
