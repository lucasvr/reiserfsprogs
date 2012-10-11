/*
 * Copyright 1996-2004 by Hans Reiser, licensing governed by 
 * reiserfsprogs/README
 */

#include "reiserfs/libreiserfs.h"
#include <stdio.h>

#if 0

#include "io.h"
#include "misc.h"
#include "reiserfs_lib.h"

#include <string.h>
#include <errno.h>
#include <time.h>
#include <assert.h>

#include "../version.h"
#endif

/* main.c */
extern reiserfs_filsys_t * fs;

/* Exit codes. */
#define EXIT_OK		0
#define EXIT_FIXED	1
#define EXIT_REBOOT	2
#define EXIT_FATAL	4
#define EXIT_FIXABLE	6
#define EXIT_OPER	8   /* Some operation returns error. */
#define EXIT_USER	16

/*
 * modes
 */
#define DO_NOTHING              0
#define FSCK_CHECK              1
#define FSCK_FIX_FIXABLE        2
#define FSCK_SB                 3
#define FSCK_REBUILD            4
#define FSCK_ROLLBACK_CHANGES   5
#define FSCK_CLEAN_ATTRIBUTES	7
#define FSCK_AUTO               8 /* -a || -p specified */

/* temporary */
#define DO_TEST                 9

/*
 * options
 */

#define OPT_INTERACTIVE                 1 << 0
#define OPT_ADJUST_FILE_SIZE            1 << 1	/* not default yet */
#define OPT_QUIET                       1 << 2	/* no "speed" info */
#define OPT_SILENT                      1 << 3	/* no complains about found corruptions */
#define OPT_BACKGROUND                  1 << 4
#define OPT_SKIP_JOURNAL                1 << 5
#define OPT_HASH_DEFINED                1 << 6
#define OPT_SAVE_PASSES_DUMP            1 << 7
#define OPT_SAVE_ROLLBACK               1 << 8
#define OPT_YES				1 << 9
#define BADBLOCKS_FILE               	1 << 10

/* set by fsck when pass-by-pass (-d), FS_FATAL flag included */
enum fsck_state_flags {
	PASS_0_DONE     = 0xfa02,
	PASS_1_DONE     = 0xfb02,
	TREE_IS_BUILT   = 0xfc02,
	SEMANTIC_DONE   = 0xfd02,
	LOST_FOUND_DONE = 0xfe02

};

/* Stage.c */

extern void fsck_stage_start_put (FILE * file, unsigned long stage);
extern void fsck_stage_end_put (FILE * file);
extern int fsck_stage_magic_check (FILE * fp);

/* pass0.c */
extern int pass0_block_isbad_unfm(unsigned long block);
extern int pass0_block_isgood_unfm(unsigned long block);
extern int pass0_block_isleaf(unsigned long block);
extern void pass0_block_clleaf(unsigned long block);
extern void fsck_pass0 (reiserfs_filsys_t *);
extern void fsck_pass0_load_result (FILE *, reiserfs_filsys_t *);
extern void make_allocable (unsigned long block);
extern int fsck_leaf_check(reiserfs_bh_t * bh);
extern unsigned long alloc_block (void);
extern void fsck_pass0_aux_fini ();
extern int fsck_leaf_item_check (reiserfs_bh_t * bh, reiserfs_ih_t *, char *);
extern int are_there_allocable_blocks (unsigned int amout_needed);
extern int fsck_leaf_check_header(reiserfs_filsys_t * fs, reiserfs_bh_t * bh);

/* pass1.c */
#define fsck_item_reach(ih)	(reiserfs_ih_isreach (ih) ? 1 : 0)
#define fsck_item_mkunreach(ih)			\
({						\
	reiserfs_ih_clflags (ih);		\
	reiserfs_ih_mkunreach (ih);		\
						\
	if (reiserfs_ih_ext (ih))		\
		reiserfs_ih_set_free (ih, 0);	\
})

#define fsck_item_mkreach(ih, bh)		\
({						\
	reiserfs_ih_clunreach (ih);		\
	reiserfs_buffer_mkdirty (bh);		\
})

extern void fsck_pass1 (reiserfs_filsys_t *);
extern void fsck_pass1_load_result (FILE *, reiserfs_filsys_t *);
extern void fsck_pass2 (reiserfs_filsys_t *);
extern void fsck_info_checkmem(void);

/* pass2.c */
typedef struct saveitem saveitem_t;
struct saveitem {
    reiserfs_ih_t si_ih;
    char * si_dnm_data;
    saveitem_t * si_next;
    __u32 si_blocknr;

    // changed by XB;
    saveitem_t * last_known;
};

extern void fsck_pass2_load_result (reiserfs_filsys_t *);

extern void fsck_item_save(reiserfs_path_t * path, 
			   saveitem_t ** head);

extern saveitem_t * fsck_item_free(saveitem_t * si);

extern void fsck_tree_insert_item (reiserfs_ih_t * ih, 
				   char * item, int was_in_tree);

extern void fsck_tree_merge(reiserfs_path_t *path);
extern long long int must_there_be_a_hole (const reiserfs_ih_t *ih, 
					   const reiserfs_key_t *key);

extern int fsck_tree_insert_zero_ptr (reiserfs_filsys_t *fs,
				      reiserfs_key_t *key,
				      long long int p_count,
				      __u16 flags);

extern void fsck_tree_rewrite(reiserfs_filsys_t *fs, 
			      const reiserfs_key_t *start_key,
			      __u64 end, __u16 flags);

typedef int (*tree_modify_t) (reiserfs_path_t *path, void *data);

/* relocate.c */
extern void fsck_relocate_mklinked(reiserfs_key_t *new_key);
extern __u32 fsck_relocate_get_oid (reiserfs_key_t * key);
extern __u32 fsck_relocate_oid(reiserfs_key_t * key);
extern void fsck_relocate_link_all (void);
extern __u32 fsck_relocate_check (reiserfs_ih_t * ih, int isdir);

/* semantic.c */

/* return values for check_regular_file and check_semantic_tree */
#define OK 0
#define STAT_DATA_NOT_FOUND -1
#define DIRECTORY_HAS_NO_ITEMS -2
#define RELOCATED -3
#define LOOP_FOUND -4

extern void fsck_semantic (reiserfs_filsys_t *);
extern void fsck_semantic_load_result (FILE *, reiserfs_filsys_t *);
extern void fsck_semantic_check (void);
extern int is_dot_dot (char * name, int namelen);
extern int is_dot (char * name, int namelen);
extern int not_a_directory (void * sd);
extern int not_a_regfile (void * sd);
extern void zero_nlink (reiserfs_ih_t * ih, void * sd);
extern int fix_obviously_wrong_sd_mode (reiserfs_path_t * path);
extern int wrong_st_blocks(const reiserfs_key_t * key, 
			   __u32 * blocks, 
			   __u32 sd_blocks, 
			   __u16 mode, 
			   int new_format);
extern int wrong_st_size (const reiserfs_key_t * key, 
			  unsigned long long max_file_size, 
			  int blocksize,
			  __u64 * size, 
			  __u64 sd_size, 
			  int type);

extern int rebuild_semantic_pass (reiserfs_key_t * key, 
				  const reiserfs_key_t * parent, 
				  int is_dot_dot,
				  reiserfs_ih_t * new_ih);

extern void relocate_dir (reiserfs_ih_t * ih);

extern int rebuild_check_regular_file (reiserfs_path_t * path, 
				       void * sd,
				       reiserfs_ih_t * new_ih);

/* lost+found.c */
extern void fsck_lost (reiserfs_filsys_t *);
extern void fsck_lost_load_result (reiserfs_filsys_t *);

/* objectid.c */
typedef struct id_map {
    void **index;
    __u32 count, last_used;
} id_map_t;

extern void id_map_flush(struct id_map * map, reiserfs_filsys_t * fs);
extern void id_map_free(id_map_t *);
extern int id_map_mark(id_map_t *map, __u32 id);
extern id_map_t *id_map_init();
extern __u32 id_map_alloc(id_map_t *map);
extern int id_map_test(id_map_t *map, __u32 id);
extern void fetch_objectid_map (struct id_map * map, 
				reiserfs_filsys_t * fs);
extern void reiserfs_objectid_map_save (FILE * fp, struct id_map * id_map);
extern struct id_map * reiserfs_objectid_map_load (FILE * fp);

/*  rollback.c */
extern void fsck_rollback_fini ();

extern void fsck_rollback_init (char * rollback_file, 
				unsigned int *bloksize, FILE * log);

extern int fsck_rollback_prep (char * rollback_file, FILE * log);

extern void fsck_rollback (int fd_device, 
			   int fd_journal_device, 
			   FILE * log); 

/* pass4.c */
extern void fsck_cleanup (void);

/* check_tree.c */
extern void check_fs_tree (reiserfs_filsys_t *);
extern void fsck_tree_clean_attr (reiserfs_filsys_t * fs);

extern int fsck_leaf_check_neigh (reiserfs_filsys_t *fs, 
				  reiserfs_bh_t * bh, 
				  int i);

extern int fsck_tree_delete(reiserfs_key_t * start_key, 
			    saveitem_t ** save_here, 
			    int skip_dir_items, 
			    tree_modify_t func,
			    void *data);


/* ubitmap.c */
extern int is_block_used (unsigned long block);
extern void mark_block_used (unsigned long block, int check_hardware);
extern void fsck_bitmap_mkuninsert (unsigned long block);
extern int fsck_bitmap_isuninsert (unsigned long block);
extern void fsck_bitmap_cluninsert (unsigned long block);
extern int reiserfsck_new_blocknrs (reiserfs_filsys_t * fs,
				    unsigned long * pblocknrs,
				    unsigned long start_from,
				    int amount_needed);

extern int reiserfsck_free_block (reiserfs_filsys_t * fs, 
				  unsigned long block);

extern reiserfs_bh_t * reiserfsck_get_new_buffer (unsigned long start);

/* ufile.c */
#define FSCK_MAX_GAP(bs) \
	(100 * REISERFS_ITEM_MAX(bs) / REISERFS_EXT_SIZE * bs)

extern void fsck_file_relocate (reiserfs_key_t *key, 
				int update_key);

extern int reiserfsck_file_write (reiserfs_ih_t * ih, 
				  char * item, 
				  int was_in_tree);

extern int are_file_items_correct (reiserfs_ih_t * sd_ih, 
				   void * sd, 
				   __u64 * size, 
				   __u32 * blocks, 
				   int mark_passed_items, 
				   int * symlink);

/* ustree.c*/
typedef int (*path_func_t) (reiserfs_filsys_t *, 
			    reiserfs_path_t *);

extern void fsck_tree_trav (reiserfs_filsys_t *, 
			    path_func_t, 
			    path_func_t,
			    int depth);
enum fsck_stage {
    FS_CHECK	= 0x0,
    FS_PASS0	= 0x1,
    FS_PASS1	= 0x2,
    FS_PASS2	= 0x3,
    FS_SEMANTIC = 0x4,
    FS_LOST	= 0x5,
    FS_CLEANUP  = 0x6,
    FS_LAST
};

struct pass0_stat {
    unsigned long dealt_with; /* number of blocks read during pass 0 */
    unsigned long leaves; /* blocks looking like reiserfs leaves found */
    unsigned long leaves_corrected;
    unsigned long all_contents_removed;
    unsigned long too_old_leaves; /* these are leaves which contains
                                     direntries with different hash from the
                                     one specified with -h */
    unsigned long wrong_pointers; /* pointers in extent items pointing to
                                     wrong area */
    unsigned long pointed; /* pointers blocks of device pointed by all
                              extent items */
    
    /* number of removed items. */
    unsigned long long removed;
};


struct pass1_stat {
    unsigned long leaves; /* leaves found in pass0 to build tree off */
    unsigned long inserted_leaves; /* number of leaves inserted by pointers */
    unsigned long pointed_leaves; /* pointers in extent items which pointed
				     to leaves (zeroed) */
    unsigned long uninsertable_leaves;
    unsigned long non_unique_pointers; /* pointers to already pointed unformatted nodes */
    unsigned long correct_pointers;
    unsigned long allocable_blocks; /* allocable blocks before pass 1 starts */
};


struct pass2_stat {
    unsigned long leaves; /* leaves inserted item by item */
    unsigned long safe_non_unique_pointers; /* these are just the same pointers */
    unsigned long relocated;
    unsigned long shared_objectids;
    unsigned long rewritten;
};


struct semantic_stat {
    unsigned long regular_files;
    unsigned long directories;
    unsigned long symlinks;
    unsigned long broken_files;
    unsigned long others;
    unsigned long fixed_sizes;
    unsigned long oid_sharing;
    unsigned long oid_sharing_dirs_relocated;
    unsigned long oid_sharing_files_relocated;
    unsigned long deleted_entries;
    unsigned long added_sd;
    unsigned long empty_lost_dirs;
    unsigned long lost_found;
    unsigned long lost_found_files;
    unsigned long lost_found_dirs;
};

struct pass_4_stat {
    unsigned long deleted_items;
};


struct rebuild_info {
    struct {
	struct pass0_stat pass0;
	struct pass1_stat pass1;
	struct pass2_stat pass2;
	struct semantic_stat semantic;
	struct pass_4_stat pass4;
    } pass_u;

    /* bitmaps */
    reiserfs_bitmap_t * source_bitmap;
    reiserfs_bitmap_t * new_bitmap;
    reiserfs_bitmap_t * allocable_bitmap;
    reiserfs_bitmap_t * uninsertables;

    char * bitmap_file_name;
    /*char * new_bitmap_file_name;*/
    char * passes_dump_file_name; /* after pass 0, 1 or 2 reiserfsck can store
                                     data with which it will be able to start
                                     from the point it stopped last time at */

    unsigned short mode;
    unsigned long options;

    /* rollback file */
    char * rollback_file;
  
    /* hash hits stat */
    int hash_amount;
    unsigned long * hash_hits;
    char * defined_hash;

#define USED_BLOCKS 1
#define EXTERN_BITMAP 2
#define ALL_BLOCKS 3
    int scan_area;
    int use_journal_area;
    int test;
};


struct check_info {
    unsigned long bad_nodes;
    unsigned long fatal_corruptions;
    unsigned long fixable_corruptions;
//    unsigned long badblocks_corruptions;
    unsigned long leaves;
    unsigned long internals;
    unsigned long dirs;
    unsigned long files;
    unsigned long safe;
    unsigned long unfm_pointers;
    unsigned long zero_unfm_pointers;
    reiserfs_bitmap_t * deallocate_bitmap;
};


struct fsck_data {
    unsigned short mode; /* check, rebuild, etc*/
    unsigned long options;
    unsigned long mounted;

    struct rebuild_info rebuild;
    struct check_info check;

    char * journal_dev_name; 
    /* log file name and handle */
    char * log_file_name;
    FILE * log;

    /* this is a file where reiserfsck will explain what it is doing. This is
       usually stderr. But when -g is specified - reiserfsck runs in the
       background and append progress information into 'fsck.run' */
    FILE * progress;

    /* objectid maps */
    id_map_t * proper_id_map;
    id_map_t * semantic_id_map; /* this objectid map is used to
				   cure objectid sharing problem */
};


#define fsck_data(fs) ((struct fsck_data *)((fs)->fs_vp))
#define pass_0_stat(fs) (&(fsck_data(fs)->rebuild.pass_u.pass0))
#define pass_1_stat(fs) (&(fsck_data(fs)->rebuild.pass_u.pass1))
#define pass_2_stat(fs) (&(fsck_data(fs)->rebuild.pass_u.pass2))
#define sem_pass_stat(fs) (&(fsck_data(fs)->rebuild.pass_u.semantic))
#define pass_4_stat(fs) (&(fsck_data(fs)->rebuild.pass_u.pass4))

#define fsck_check_stat(fs) (&(fsck_data(fs)->check))


#define proper_id_map(s) fsck_data(s)->proper_id_map
#define semantic_id_map(s) fsck_data(s)->semantic_id_map

#define fsck_source_bitmap(fs) fsck_data(fs)->rebuild.source_bitmap
#define fsck_new_bitmap(fs) fsck_data(fs)->rebuild.new_bitmap
#define fsck_allocable_bitmap(fs) fsck_data(fs)->rebuild.allocable_bitmap
#define fsck_uninsertables(fs) fsck_data(fs)->rebuild.uninsertables

#define fsck_deallocate_bitmap(fs) fsck_data(fs)->check.deallocate_bitmap

#define fsck_interactive(fs) (fsck_data(fs)->options & OPT_INTERACTIVE)
//#define fsck_fix_fixable(fs) (fsck_data(fs)->options & OPT_FIX_FIXABLE)

#define fsck_run_one_step(fs) (fsck_data(fs)->options & OPT_SAVE_PASSES_DUMP)

#define fsck_save_rollback(fs) (fsck_data(fs)->options & OPT_SAVE_ROLLBACK)

/* change unknown modes (corrupted) to mode of regular files, fix file
   sizes which are bigger than a real file size, relocate files with
   shared objectids (this slows fsck down (when there are too many
   files sharing the same objectid), it will also remove other names
   pointing to this file */
#define fsck_adjust_file_size(fs) (fsck_data(fs)->options & OPT_ADJUST_FILE_SIZE)
#define fsck_quiet(fs)	(fsck_data(fs)->options & OPT_QUIET)
#define fsck_silent(fs)	(fsck_data(fs)->options & OPT_SILENT)
#define fsck_in_background(fs) (fsck_data(fs)->options & OPT_BACKGROUND)
#define fsck_hash_defined(fs) (fsck_data(fs)->options & OPT_HASH_DEFINED)
#define fsck_skip_journal(fs) (fsck_data(fs)->options & OPT_SKIP_JOURNAL)
#define fsck_yes_all(fs) (fsck_data(fs)->options & OPT_YES)


#define fsck_mode(fs) (fsck_data(fs)->mode)
#define fsck_log_file(fs) (fsck_data(fs)->log)
#define fsck_progress_file(fs) ((fs && fsck_data(fs)->progress) ? fsck_data(fs)->progress : stderr)

/* name of file where we store information for continuing */
#define state_dump_file(fs) fsck_data(fs)->rebuild.passes_dump_file_name

/* name of file where we store rollback data */
#define state_rollback_file(fs) fsck_data(fs)->rebuild.rollback_file

int fsck_info_ask (reiserfs_filsys_t * fs, 
		   char * q, char * a, 
		   int default_answer);

void fsck_stage_report (int, reiserfs_filsys_t *);

/*pass1: rebuild super block*/
void rebuild_sb (reiserfs_filsys_t * fs, char * filename, struct fsck_data * data);


/* special type for symlink not conflicting to any of item types. */
#define TYPE_SYMLINK	4


#define fsck_log(fmt, list...) \
{\
if (!fsck_silent (fs))\
    reiserfs_warning (fsck_log_file (fs), fmt, ## list);\
}

#define fsck_progress(fmt, list...) \
{\
reiserfs_warning (fsck_progress_file(fs), fmt, ## list);\
fflush (fsck_progress_file(fs));\
}

#define FATAL	1
#define FIXABLE 2

enum entry_type {
    ET_NAME		= 0x0,
    ET_DOT		= 0x1,
    ET_DOT_DOT		= 0x2,
    ET_LOST_FOUND	= 0x3,
    ET_LAST
};

#define fsck_exit(fmt, list...) \
{\
reiserfs_warning (fsck_progress_file(fs), fmt, ## list);\
exit (EXIT_USER);\
}

#define one_more_corruption(fs,kind)			\
({							\
    if (kind == FATAL)					\
	fsck_check_stat (fs)->fatal_corruptions++;	\
    else if (kind == FIXABLE)				\
	fsck_check_stat (fs)->fixable_corruptions++;	\
})

#define one_less_corruption(fs,kind)			\
({							\
    if (kind == FATAL)					\
	fsck_check_stat (fs)->fatal_corruptions--;	\
    else if (kind == FIXABLE)				\
	fsck_check_stat (fs)->fixable_corruptions--;	\
})
