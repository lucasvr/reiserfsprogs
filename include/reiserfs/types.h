/*
 *  Copyright 2000-2004 by Hans Reiser, licensing governed by 
 *  reiserfsprogs/README
 */

#ifndef REISERFS_TYPES_H
#define REISERFS_TYPES_H

#include "misc/types.h"
#include "misc/swab.h"

struct reiserfs_bitmap {
    unsigned long bm_byte_size;
    unsigned long bm_bit_size;
    char * bm_map;
    unsigned long bm_set_bits;
    int bm_dirty;  /* used for fetched bitmap */
    unsigned long bm_first_zero;
};

typedef struct reiserfs_bitmap reiserfs_bitmap_t;


typedef struct reiserfs_bh reiserfs_bh_t;

struct reiserfs_bh {
    unsigned long b_blocknr;
    int b_dev;
    unsigned long b_size;
    char * b_data;
    unsigned long b_state;
    unsigned int b_count;
    unsigned int b_list ;
    void (*b_start_io) (unsigned long);
    void (*b_end_io)(reiserfs_bh_t *bh, int uptodate);
    
    reiserfs_bh_t * b_next;
    reiserfs_bh_t * b_prev;
    reiserfs_bh_t * b_hash_next;
    reiserfs_bh_t * b_hash_prev;
};


/* magic string to find desc blocks in the journal */
#define JOURNAL_DESC_MAGIC "ReIsErLB" 

struct reiserfs_journal_param {
    /* where does journal start from on its device */
    __u32 jp_start;
    /* journal device st_rdev */
    __u32 jp_dev;
    /* size of the journal on FS creation. used to make sure 
       they don't overflow it */
    __u32 jp_size;
    /* max number of blocks in a transaction.  */
    __u32 jp_tlen;
    /* random value made on fs creation (this was sb_journal_block_count) */
    __u32 jp_magic;
    /* max number of blocks to batch into a trans */
    __u32 jp_max_batch;
    /* in seconds, how old can an async commit be */
    __u32 jp_commit_age;
    /* in seconds, how old can a transaction be */
    __u32 jp_trans_age;
};

typedef struct reiserfs_journal_param reiserfs_journal_param_t;

/* this header block gets written whenever a transaction is considered fully
** flushed, and is more recent than the last fully flushed transaction.  fully
** flushed means all the log blocks and all the real blocks are on disk, and
** this transaction does not need to be replayed.  */
struct reiserfs_journal_header {    
    /* id of last fully flushed transaction */
    __u32 jh_last_flush_trans_id ;    
    /* offset in the log of where to start replay after a crash */
    __u32 jh_flush_offset ; 
    
    __u32 jh_mount_id ;

    reiserfs_journal_param_t jh_journal;

    /* the mount id of the fs during the last reiserfsck --check. */
    __u32 jh_last_check_mount_id;	
};

/* this is the super from 3.5.X */
struct reiserfs_super_block_v1
{
    __u32 sb_block_count; 	/* 0 number of block on data device */
    __u32 sb_free_blocks; 	/* 4 free blocks count */
    __u32 sb_root_block;        /* 8 root of the tree */

    reiserfs_journal_param_t sb_journal; /* 12 */

    __u16 sb_blocksize;         /* 44 */
    __u16 sb_oid_maxsize;	/* 46 max size of object id array, see
				   get_objectid() commentary */
    __u16 sb_oid_cursize; 	/* 48 current size of object id array */
    __u16 sb_umount_state;	/* 50 this is set to 1 when filesystem was
				   umounted, to 2 - when not */
    
    char s_magic[10]; 		/* 52 reiserfs magic string indicates that
				   file system is reiserfs: "ReIsErFs" or
				   "ReIsEr2Fs" or "ReIsEr3Fs" */
    __u16 sb_fs_state; 		/* 62 it is set to used by fsck to mark which phase of
				   rebuilding is done (used for fsck debugging) */
    __u32 sb_hash_function_code;/* 64 code of fuction which was/is/will be
				   used to sort names in a directory. See
				   codes in above */
    __u16 sb_tree_height;	/* 68 height of filesytem tree. Tree
				   consisting of only one root block has 2
				   here */
    __u16 sb_bmap_nr;     	/* 70 amount of bitmap blocks needed to
				   address each block of file system */
    __u16 sb_version; 		/* 72 this field is only reliable on
				   filesystem with non-standard journal */
    __u16 sb_reserved_for_journal;  /* 74 size in blocks of journal area on
				       main device, we need to keep after
				       non-standard journal relocation */
};

typedef struct reiserfs_super_block_v1 reiserfs_sb_v1_t;

/* Structure of super block on disk */
struct reiserfs_super_block {
/*  0 */    reiserfs_sb_v1_t s_v1;
/* 76 */     __u32 sb_inode_generation; 
/* Right now used only by inode-attributes, if enabled */
/* 80 */     __u32 s_flags;
/* filesystem unique identifier */
/* 84 */    unsigned char s_uuid[16];
/* filesystem volume label */
/*100 */    unsigned char s_label[16];
/* zero filled by mkreiserfs and reiserfs_convert_objectid_map_v1()
 * so any additions must be updated there as well. */ 
/*116 */    char s_unused[88];
/*204*/
} __attribute__ ((__packed__));;

typedef struct reiserfs_super_block reiserfs_sb_t;

typedef __u32 (*hashf_t) (const char *, int);

typedef struct reiserfs_filsys reiserfs_filsys_t;

struct reiserfs_filsys {
    unsigned int fs_blocksize;
    int fs_format;	      /* on-disk format version */
    hashf_t hash;	      /* pointer to function which is used to sort
				 names in directory. It is set by
				 reiserfs_fs_open if it is set in the super
				 block, otherwise it is set by first
				 reiserfs_hash_correct */
    
    char fs_file_name[4096];       /* file name of underlying device */
    int fs_dev; /* descriptor of opened block device file */
    reiserfs_bh_t * fs_super_bh;  /* buffer containing super block */
    reiserfs_sb_t * fs_ondisk_sb; /* pointer to its b_data */


    reiserfs_bitmap_t * fs_bitmap2; /* ondisk bitmap after
				       reiserfs_bitmap_open */


    /* opened journal fields */
    char fs_j_file_name[4096];  /* file name of relocated journal device */
    int fs_journal_dev;		/* descriptor of opened journal device */
    reiserfs_bh_t * fs_jh_bh;	/* buffer containing journal header */

    /* badblocks */
    reiserfs_bitmap_t * fs_badblocks_bm;

    int fs_dirt;
    int fs_flags;
    void * fs_vp;
    int (*block_allocator) (reiserfs_filsys_t * fs, 
			    unsigned long * free_blocknrs,
			    unsigned long start, int amount_needed);
    int (*block_deallocator) (reiserfs_filsys_t * fs, unsigned long block);

    __u16 lost_format;
};


#define REISERFS_TREE_HEIGHT_MAX	6
#define REISERFS_PATH_OFFINIT		2
#define REISERFS_PATH_OFFILL		1
#define REISERFS_PATH_MAX		(REISERFS_TREE_HEIGHT_MAX +	\
					 REISERFS_PATH_OFFINIT)

struct reiserfs_path_element  {
    /* Pointer to the buffer at the path in the tree. */
    reiserfs_bh_t * pe_buffer; 

    /* Position in the tree node which is placed in the buffer above. */
    int pe_position;  
};

typedef struct reiserfs_path_element reiserfs_path_element_t;


/* We need to keep track of who the ancestors of nodes are.  When we
   perform a search we record which nodes were visited while
   descending the tree looking for the node we searched for. This list
   of nodes is called the path.  This information is used while
   performing balancing.  Note that this path information may become
   invalid, and this means we must check it when using it to see if it
   is still valid. You'll need to read reiserfs_tree_search_item and the 
   comments in it, especially about decrement_counters_in_path(), 
   to understand this structure. */
struct reiserfs_path {
    /* Length of the array above.   */
    unsigned int path_length;
    
    /* Array of the path elements.  */
    reiserfs_path_element_t path_elements[REISERFS_PATH_MAX];	
    
    int pos_in_item;
};

typedef struct reiserfs_path reiserfs_path_t;

struct reiserfs_koff_v1 {
    __u32 k_offset;
    __u32 k_uniqueness;
} __attribute__ ((__packed__));

struct reiserfs_koff_v2 {
#if __BYTE_ORDER == __LITTLE_ENDIAN
    __u64 k_offset:60;
    __u64 k_type: 4;
#elif __BYTE_ORDER == __BIG_ENDIAN
    __u64 k_type: 4;
    __u64 k_offset:60;
#else
# error "nuxi/pdp-endian archs are not supported"
#endif
} __attribute__ ((__packed__));

/* Key of the object determines object's location in the tree, 
   composed of 4 components */
struct reiserfs_key {
    /* packing locality: by default parent directory object id */
    __u32 k2_dir_id;
    
    /* object identifier */
    __u32 k2_objectid;
    
    union {
	struct reiserfs_koff_v1 k2_offset_v1;
	struct reiserfs_koff_v2 k2_offset_v2;
    } __attribute__ ((__packed__)) u;
} __attribute__ ((__packed__));

typedef struct reiserfs_key reiserfs_key_t;

/* Everything in the filesystem is stored as a set of items.  The item head
   contains the key of the item, its free space (for extent items) and
   specifies the location of the item itself within the block.  */

struct reiserfs_ih {
    reiserfs_key_t ih_key; 	/* Everything in the tree is found by searching for it
                           based on its key.*/

    union {
	__u16 ih2_free_space; /* The free space in the last unformatted node
				of an extent item if this is an extent
				item.  This equals 0xFFFF iff this is a direct
				item or stat data item. Note that the key, not
				this field, is used to determine the item
				type, and thus which field this union
				contains. */
	__u16 ih2_entry_count; /* Iff this is a directory item, this field
				  equals the number of directory entries in
				  the directory item. */
    } __attribute__ ((__packed__)) u;
    __u16 ih2_item_len;           /* total size of the item body */
    __u16 ih2_item_location;      /* an offset to the item body within the
                                    block */

    __u16 ih_format;	/* key format is stored in bits 0-11 of this item
			   flags are stored in bits 12-15 */
#if 0
    struct {
	__u16 key_format : 12; /* KEY_FORMAT_1 or KEY_FORMAT_2. This is not
				  necessary, but we have space, let use it */
	__u16 flags : 4;   /* fsck set here its flag (reachable/unreachable) */
    } __attribute__ ((__packed__)) ih2_format;
#endif
} __attribute__ ((__packed__));

typedef struct reiserfs_ih reiserfs_ih_t;

typedef int (*unfm_func_t) (reiserfs_filsys_t *, __u32);

#endif 
