/*
 *  Copyright 2000-2004 by Hans Reiser, licensing governed by 
 *  reiserfsprogs/README
 */

#ifndef REISERFS_JOURNAL_H
#define REISERFS_JOURNAL_H

#define reiserfs_jp_get_start(jp)	   get_le32(jp, jp_start)
#define reiserfs_jp_set_start(jp,val)	   set_le32(jp, jp_start, val)

#define reiserfs_jp_get_dev(jp)		   get_le32 (jp, jp_dev)
#define reiserfs_jp_set_dev(jp,val)	   set_le32 (jp, jp_dev, val)

#define reiserfs_jp_get_size(jp)	   get_le32 (jp, jp_size)
#define reiserfs_jp_set_size(jp,val)	   set_le32 (jp, jp_size, val)

#define reiserfs_jp_get_tlen(jp)	   get_le32 (jp, jp_tlen)
#define reiserfs_jp_set_tlen(jp,val)	   set_le32 (jp, jp_tlen, val)

#define reiserfs_jp_get_magic(jp)	   get_le32 (jp, jp_magic)
#define reiserfs_jp_set_magic(jp,val)	   set_le32 (jp, jp_magic, val)

#define NEED_TUNE 0xffffffff

#define reiserfs_jp_get_max_batch(jp)	   get_le32 (jp, jp_max_batch)
#define reiserfs_jp_set_max_batch(jp,val)  set_le32 (jp, jp_max_batch, val)

#define reiserfs_jp_get_commit_age(jp)     get_le32 (jp, jp_commit_age)
#define reiserfs_jp_set_commit_age(jp,val) set_le32 (jp, jp_commit_age, val)

#define reiserfs_jp_get_trans_age(jp)	   get_le32 (jp, jp_trans_age)
#define reiserfs_jp_set_trans_age(jp,val)  set_le32 (jp, jp_trans_age, val)

/* must be correct to keep the desc and commit structs at 4k */
/* first block written in a commit.  BUG, not 64bit safe */
struct reiserfs_journal_desc {
    /* id of commit */
    __u32 j2_trans_id ;	
    /* length of commit. len +1 is the commit block */
    __u32 j2_len ;	
    /* mount id of this trans*/
    __u32 j2_mount_id ;	
    /* real locations for each block */    
    __u32 j2_realblock[1] ; 
};

#define reiserfs_jd_magic(bh) (bh->b_data + bh->b_size - 12)

#define reiserfs_jd_head(bh)		((struct reiserfs_journal_desc *)bh->b_data)

#define reiserfs_jd_get_trans(bh)	get_le32 (reiserfs_jd_head (bh), j2_trans_id)
#define reiserfs_jd_set_trans(bh,val)	set_le32 (reiserfs_jd_head (bh), j2_trans_id, val)

#define reiserfs_jd_get_len(bh)		get_le32 (reiserfs_jd_head (bh), j2_len)
#define reiserfs_jd_set_len(bh,val)	set_le32 (reiserfs_jd_head (bh), j2_len, val)

#define reiserfs_jd_get_mount(bh)	get_le32 (reiserfs_jd_head (bh), j2_mount_id)
#define reiserfs_jd_set_mount(bh,val)	set_le32 (reiserfs_jd_head (bh), j2_mount_id, val)
    

/* last block written in a commit BUG, not 64bit safe */
struct reiserfs_journal_commit {
    __u32 j3_trans_id ;	 /* must match j_trans_id from the desc block */
    __u32 j3_len ;	 /* ditto */
    __u32 j3_realblock[1] ; /* real locations for each block */
} ;

#define reiserfs_jc_head(bh) ((struct reiserfs_journal_commit *)bh->b_data)

#define reiserfs_jc_get_trans(bh)	 get_le32 (reiserfs_jc_head(bh), j3_trans_id)
#define reiserfs_jc_set_trans(bh,val)	 set_le32 (reiserfs_jc_head(bh), j3_trans_id, val)

#define reiserfs_jc_get_len(bh)	get_le32 (reiserfs_jc_head(bh), j3_len)
#define reiserfs_jc_set_len(bh,val)	 set_le32 (reiserfs_jc_head(bh), j3_len, val)


/* set/get fields of journal header with these defines */
#define reiserfs_jh_get_mount(jh)	get_le32 (jh, jh_mount_id)
#define reiserfs_jh_set_mount(jh,val)	set_le32 (jh, jh_mount_id, val)

#define reiserfs_jh_get_flushed(jh)	get_le32 (jh, jh_last_flush_trans_id)
#define reiserfs_jh_set_flushed(jh,val)	set_le32 (jh, jh_last_flush_trans_id, val)

#define reiserfs_jh_get_start(jh)	get_le32 (jh, jh_flush_offset)
#define reiserfs_jh_set_start(jh,val)	set_le32 (jh, jh_flush_offset, val)


/* Journal Transaction Half */
#define reiserfs_jt_half(blocksize)				\
	((blocksize - sizeof (struct reiserfs_journal_desc) +	\
	  sizeof (__u32) - 12) / sizeof (__u32))


/* journal default settings */
#define JOURNAL_MIN_SIZE 	512

/* biggest possible single transaction, don't change for now (8/3/99) */
#define JOURNAL_TRANS_MAX 	1024

/* need to check whether it works */
#define JOURNAL_TRANS_MIN 	256
/* default journal size / max trans length */
#define JOURNAL_DEFAULT_RATIO 	8
#define JOURNAL_MIN_RATIO 	2
/* max blocks to batch into one transaction, don't make this 
   any bigger than 900 */
#define JOURNAL_MAX_BATCH   	900

#define JOURNAL_MAX_COMMIT_AGE 	30
#define JOURNAL_MAX_TRANS_AGE 	30

/* journal max size is a maximum number of blocks pointed by first bitmap -
   REISERFS_DISK_OFFSET - superblock - first bitmap - journal herader */
#define reiserfs_journal_max(sb_nr,blocksize)			\
	blocksize * 8 - (sb_nr + 1 + 1 + 1)

#define reiserfs_journal_default(sb_nr,blocksize)		\
	(unsigned long long)					\
	((8192 > reiserfs_journal_max (sb_nr,blocksize)) ?	\
	 reiserfs_journal_max (sb_nr,blocksize) : 8192)

struct reiserfs_trans {
    unsigned long mount_id;
    unsigned long trans_id;
    unsigned long desc_blocknr;
    unsigned long trans_len;
    unsigned long commit_blocknr;
    unsigned long next_trans_offset;
};

typedef struct reiserfs_trans reiserfs_trans_t;

extern unsigned long reiserfs_journal_hostsize (reiserfs_sb_t *sb);

extern int reiserfs_journal_block (reiserfs_filsys_t *, 
				   unsigned long block);

extern int reiserfs_journal_desc_valid (reiserfs_bh_t *);

extern int reiserfs_journal_get_transactions (reiserfs_filsys_t *, 
					      reiserfs_trans_t *,
					      reiserfs_trans_t *);

typedef void (*action_on_trans_t) (reiserfs_filsys_t *, 
				   reiserfs_trans_t *);

extern void reiserfs_journal_by_transaction (reiserfs_filsys_t *, 
					     action_on_trans_t);

typedef void (*action_on_block_t) (reiserfs_filsys_t *, 
				   reiserfs_trans_t *,
				   unsigned int index,
				   unsigned long in_journal,
				   unsigned long in_place);

extern void reiserfs_journal_foreach (reiserfs_filsys_t * fs, 
				      reiserfs_trans_t * trans,
				      action_on_block_t action);

extern int reiserfs_journal_open (reiserfs_filsys_t *, 
				  char *, int flags);

extern int reiserfs_journal_params_check(reiserfs_filsys_t *fs);

extern int reiserfs_journal_create (reiserfs_filsys_t * fs, 
				    char * j_filename,
				    unsigned long offset, 
				    unsigned long len, 
				    int transaction_max_size);

extern int reiserfs_journal_opened (reiserfs_filsys_t *);

extern void reiserfs_journal_flush (reiserfs_filsys_t * fs);

extern void reiserfs_journal_free (reiserfs_filsys_t * fs);

extern void reiserfs_journal_close (reiserfs_filsys_t *);

extern void reiserfs_journal_reopen (reiserfs_filsys_t * fs, int flag);

extern int reiserfs_journal_replay (reiserfs_filsys_t * fs);

extern __u32 reiserfs_journal_trans_age (void);

extern __u32 reiserfs_journal_commit_age (void);

extern __u32 reiserfs_journal_batch (unsigned long journal_trans_max);

extern __u32 reiserfs_journal_tlen (__u32 desired, 
				    __u32 journal_size, 
				    int blocksize, 
				    int verbose);

extern unsigned int reiserfs_journal_start_must (reiserfs_filsys_t * fs);

extern int reiserfs_journal_fits (unsigned long sb_nr, 
				  unsigned int block_size,
				  unsigned long block_count, 
				  unsigned long journal_size);

extern void reiserfs_journal_print_params (FILE * fp, 
					   reiserfs_journal_param_t * jp);

extern void reiserfs_journal_print_header (reiserfs_filsys_t * fs);

extern void reiserfs_journal_print (reiserfs_filsys_t *);

extern int reiserfs_print_jdesc (FILE * fp, reiserfs_bh_t * bh);

#endif
