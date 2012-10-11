/*
 * Copyright 2000-2002 by Hans Reiser, licensing governed by reiserfs/README
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <malloc.h>
#include <sys/types.h>
#include <asm/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "io.h"
#include "misc.h"
#include "reiserfs_lib.h"
#include "../include/config.h"
#include "../version.h"

extern reiserfs_filsys_t * fs;


/*
 *  modes
 */
#define DO_DUMP    		1  /* not a real dump, just printing to stdout contents of
                      			tree nodes */
#define DO_CORRUPT 		2 /* used to make filesystem corruption and then test fsck */
#define DO_SCAN    		3
#define DO_RECOVER 		4
#define DO_TEST    		6
#define DO_PACK    		7  /* -p extract meta data of reiserfs filesystem */
#define DO_STAT    		8
#define DO_SCAN_FOR_NAME 	9 /* -n */
#define DO_LOOK_FOR_NAME 	10 /* -N */
#define DO_SCAN_JOURNAL  	11 /* -J */
#define DO_EXTRACT_BADBLOCKS	12
#define DO_FILE_MAP 		13


/*first bits are in reiserfs_fs.b*/
#define PRINT_JOURNAL 		0x10
#define PRINT_JOURNAL_HEADER	0x20
#define PRINT_BITMAP 		0x40
#define PRINT_OBJECTID_MAP	0x80
#define BE_QUIET 		0x100

/* these moved to reiserfs_fs.h */
//#define PRINT_TREE_DETAILS 		
//#define PRINT_DETAILS 	
//#define PRINT_ITEM_DETAILS 	 	
//#define PRINT_DIRECT_ITEMS 	 	


// the leaf is stored in compact form:
// start magic number
// block number __u32
// item number __u16
// struct packed_item
// ..
// end magic number

/* we store hash code in high byte of 16 bits */
#define LEAF_START_MAGIC 0xa6
#define LEAF_END_MAGIC 0x5a



#define FULL_BLOCK_START_MAGIC 0xb6
#define FULL_BLOCK_END_MAGIC 0x6b
#define UNFORMATTED_BITMAP_START_MAGIC 0xc7
#define UNFORMATTED_BITMAP_END_MAGIC 0x7c
#define END_MAGIC 0x8d
#define INTERNAL_START_MAGIC
#define INTERNAL_START_MAGIC
#define SEPARATED_JOURNAL_START_MAGIC 0xf8
#define SEPARATED_JOURNAL_END_MAGIC   0x8f

#define ITEM_START_MAGIC 0x476576
#define ITEM_END_MAGIC 0x2906504

#define MAP_MAGIC 0xe9
#define MAP_END_MAGIC 0x9e


/* 12 bits of mask are used to define */
#define NEW_FORMAT 			0x01   /* 1. 0 here means - old format, 1 - new format */
#define DIR_ID     			0x02   /* 2. dir_id is stored */
#define OBJECT_ID  			0x04   /* 3. objectid is stored */
#define OFFSET_BITS_32 			0x08   /* 4. offset is stored as 32 bit */
#define OFFSET_BITS_64 			0x10   /* 5. offset is stored as 64 bit */
#define IH_ENTRY_COUNT 			0x20   /* 6. ih_free_space/ih_entry_count is stored */
#define IH_FREE_SPACE  			0x20
#define IH_FORMAT 			0x40   /* 7. ih_format is stored */
#define WITH_SD_FIRST_DIRECT_BYTE 	0x80   /* 8. for old stat data first_direct_byte is stored */
#define NLINK_BITS_32 			0x0100 /* 9. nlinks stored in 32 bits */
#define SIZE_BITS_64  			0x0200 /* 10. size has to be stored in 64 bit */
#define WHOLE_INDIRECT 			0x0400 /* 11. indirect item is stored with compression */
#define SAFE_LINK			0x0800 /* 11. indirect item is stored with compression */


#define TYPE_MASK 0x3 /* two lowest bits are used to store item type */
//#define MASK_MASK 0xffffc /* what is packed: dirid, objectid, etc */
#define ITEM_LEN_MASK 0xfff00000 /* contents of ih_item_len of item_head */

struct packed_item {
    __u32 type_2_mask_18_len_12;
};


/* defined as inlines in both pack.c and unpack.c */
inline void set_pi_type( struct packed_item *pi, __u32 val );
inline __u32 get_pi_type( const struct packed_item *pi );
inline void set_pi_mask( struct packed_item *pi, __u32 val );
inline __u32 get_pi_mask( const struct packed_item *pi );
inline void set_pi_item_len( struct packed_item *pi, __u32 val );
inline __u32 get_pi_item_len( const struct packed_item *pi );


#define HAS_DIR_ID      0x01
#define HAS_GEN_COUNTER 0x02
#define HAS_STATE       0x04
#define YURA            0x08
#define TEA             0x10
#define R5              0x20

struct packed_dir_entry {
    __u8 mask;
    __u16 entrylen;
};

/* packed_dir_entry.mask is *always* endian safe, since it's 8 bit */
#define get_pe_entrylen(pe)     (le16_to_cpu((pe)->entrylen))
#define set_pe_entrylen(pe,v)   ((pe)->entrylen = cpu_to_le16(v))


#define fread8(pv) fread (pv, sizeof (__u8), 1, stdin)

#define fread_le16(pv)\
{\
    __u16 tmp; \
    fread16(&tmp); \
    *pv = le16_to_cpu(tmp); \
}

#define fread_le32(pv)\
{\
    __u32 tmp; \
    fread32(&tmp); \
    *pv = le32_to_cpu(tmp); \
}

#define fread_le64(pv)\
{\
    __u64 tmp; \
    fread64(&tmp); \
    *pv = le64_to_cpu(tmp); \
}


#define fread8(pv) fread (pv, sizeof (__u8), 1, stdin)
#define fread16(pv) fread (pv, sizeof (__u16), 1, stdin)
#define fread32(pv) fread (pv, sizeof (__u32), 1, stdin)
#define fread64(pv) fread (pv, sizeof (__u64), 1, stdin)


#define fwrite_le16(pv)\
{\
    __u16 tmp = cpu_to_le16(*(pv));\
    fwrite16(&tmp);\
}

#define fwrite_le32(pv)\
{\
    __u32 tmp = cpu_to_le32(*(pv));\
    fwrite32(&tmp);\
}

#define fwrite_le64(pv)\
{\
    __u64 tmp = cpu_to_le64(*(pv));\
    fwrite64(&tmp);\
}

#define fwrite8(pv) {\
if (fwrite (pv, sizeof (__u8), 1, stdout) != 1)\
    reiserfs_panic ("fwrite8 failed: %m");\
sent_bytes ++;\
}
#define fwrite16(pv) {\
if (fwrite (pv, sizeof (__u16), 1, stdout) != 1)\
    reiserfs_panic ("fwrite16 failed: %m");\
sent_bytes += 2;\
}
#define fwrite32(pv) {\
if (fwrite (pv, sizeof (__u32), 1, stdout) != 1)\
    reiserfs_panic ("fwrite32 failed: %m");\
sent_bytes += 4;\
}
#define fwrite64(pv) {\
if (fwrite (pv, sizeof (__u64), 1, stdout) != 1)\
    reiserfs_panic ("fwrite64 failed: %m");\
sent_bytes += 8;\
}


struct debugreiserfs_data {
    int mode; /* DO_DUMP | DO_PACK | DO_CORRUPT... */
#define USED_BLOCKS 	1
#define EXTERN_BITMAP 	2
#define ALL_BLOCKS 	3
#define UNUSED_BLOCKS 	4

    int scan_area; /* for -p, -s and -n */
    char * input_bitmap; /* when ->scan_area is set to EXTERN_BITMAP */
    reiserfs_bitmap_t * bitmap; /* bitmap is read from ->input_bitmap */
    unsigned long block; /* set by -B. this is a must for -C, option for -p and -d */
    char * pattern; /* for -n */
    char * device_name;
    char * journal_device_name; /* for -j */
    char * map_file; /* for -n, -N and -f */
    char * recovery_file; /* for -r */

    unsigned long options; /* -q only yet*/
    int JJ;
};

#define data(fs) ((struct debugreiserfs_data *)((fs)->fs_vp))

#define debug_mode(fs) (data(fs)->mode)
#define certain_block(fs) (data(fs)->block)
#define input_bitmap(fs) (data(fs)->bitmap)
#define input_bitmap_file_name(fs) (data(fs)->input_bitmap)
#define scan_area(fs) (data(fs)->scan_area)
#define name_pattern(fs) (data(fs)->pattern)
#define device_name(fs) (data(fs)->device_name)
#define journal_device_name(fs) (data(fs)->journal_device_name)
#define map_file(fs) (data(fs)->map_file)
#define recovery_file(fs) (data(fs)->recovery_file)

#define be_quiet(fs)  (data(fs)->options & BE_QUIET)

/* stat.c */
void do_stat (reiserfs_filsys_t * fs);

/* corruption.c */
void do_corrupt_one_block (reiserfs_filsys_t * fs);

/* recover.c */
void do_recover (reiserfs_filsys_t * fs);

/* scan.c */
void do_scan (reiserfs_filsys_t * fs);

/* journal.c */
void scan_journal (reiserfs_filsys_t * fs);

void print_map(reiserfs_filsys_t * fs);

struct saved_item {
    struct item_head si_ih;
    unsigned long si_block;
    int si_item_num;
    struct saved_item * si_next; /* list of items having the same key */
};


