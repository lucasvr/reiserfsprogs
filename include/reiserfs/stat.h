/*
 *  Copyright 2000-2004 by Hans Reiser, licensing governed by 
 *  reiserfsprogs/README
 */

#ifndef REISERFS_STAT_H
#define REISERFS_STAT_H

/* Stat Data on disk (reiserfs version of UFS disk inode minus the address blocks) */

/* The sense of adding union to stat data is to keep a value of real number of
   blocks used by file.  The necessity of adding such information is caused by
   existing of files with holes.  Reiserfs should keep number of used blocks
   for file, but not calculate it from file size (that is not correct for
   holed files). Thus we have to add additional information to stat data.
   When we have a device special file, there is no need to get number of used
   blocks for them, and, accordingly, we doesn't need to keep major and minor
   numbers for regular files, which might have holes. So this field is being
   overloaded.  */

struct reiserfs_sd_v1 {
    __u16 sd_mode;	/* file type, permissions */
    __u16 sd_nlink;	/* number of hard links */
    __u16 sd_uid;		/* owner */
    __u16 sd_gid;		/* group */
    __u32 sd_size;	/* file size */
    __u32 sd_atime;	/* time of last access */
    __u32 sd_mtime;	/* time file was last modified  */
    __u32 sd_ctime;	/* time inode (stat data) was last changed (except
                           changes to sd_atime and sd_mtime) */
    union {
	__u32 sd_rdev;
	__u32 sd_blocks;	/* number of blocks file uses */
    } __attribute__ ((__packed__)) u;
    __u32 sd_fdb; /* first byte of file which is stored
				   in a direct item: except that if it
				   equals 1 it is a symlink and if it
				   equals MAX_KEY_OFFSET there is no
				   direct item.  The existence of this
				   field really grates on me. Let's
				   replace it with a macro based on
				   sd_size and our tail suppression
				   policy.  Someday.  -Hans */
} __attribute__ ((__packed__));

typedef struct reiserfs_sd_v1 reiserfs_sd_v1_t;

#define REISERFS_SD_SIZE_V1 (sizeof(reiserfs_sd_v1_t))

/* this is used to check sd_size of stat data v1 */
#define REISERFS_SD_SIZE_MAX_V1 0x7fffffff

// sd_fdb is set to this when there are no direct items in a file
#define REISERFS_SD_NODIRECT 0xffffffff

/* Stat Data on disk (reiserfs version of UFS disk inode minus the
   address blocks) */
struct reiserfs_sd {
    __u16 sd_mode;	/* file type, permissions */
    __u16 sd_attrs;
    __u32 sd_nlink;	/* 32 bit nlink! */
    __u64 sd_size;	/* 64 bit size!*/
    __u32 sd_uid;	/* 32 bit uid! */
    __u32 sd_gid;	/* 32 bit gid! */
    __u32 sd_atime;	/* time of last access */
    __u32 sd_mtime;	/* time file was last modified  */
    __u32 sd_ctime;	/* time inode (stat data) was last changed (except
                           changes to sd_atime and sd_mtime) */
    __u32 sd_blocks;
    union {
	__u32 sd_rdev;
	__u32 sd_generation;
      //__u32 sd_fdb; 
      /* first byte of file which is stored in a direct item: except that if
	 it equals 1 it is a symlink and if it equals ~(__u32)0 there is no
	 direct item.  The existence of this field really grates on me. Let's
	 replace it with a macro based on sd_size and our tail suppression
	 policy? */
  } __attribute__ ((__packed__)) u;
} __attribute__ ((__packed__));
//
// this is 44 bytes long
//

typedef struct reiserfs_sd reiserfs_sd_t;

#define REISERFS_SD_SIZE (sizeof(reiserfs_sd_t))

/* this is used to check sd_size of stat data v2: max offset which can
   be reached with a key of format 2 is 60 bits */
#define REISERFS_SD_SIZE_MAX_V2 0xfffffffffffffffLL

/* reiserfs_sd_v1_t* access macros */
#define reiserfs_sd_v1_mode(sd)                 (le16_to_cpu((sd)->sd_mode))
#define reiserfs_set_sd_v1_mode(sd,n)           ((sd)->sd_mode = cpu_to_le16((n)))
#define reiserfs_sd_v1_nlink(sd)                (le16_to_cpu((sd)->sd_nlink))
#define reiserfs_set_sd_v1_nlink(sd,n)          ((sd)->sd_nlink = cpu_to_le16((n))) 
#define reiserfs_sd_v1_uid(sd)                  (le16_to_cpu((sd)->sd_uid))
#define reiserfs_set_sd_v1_uid(sd,n)            ((sd)->sd_uid = cpu_to_le16((n)))
#define reiserfs_sd_v1_gid(sd)                  (le16_to_cpu((sd)->sd_gid))
#define reiserfs_set_sd_v1_gid(sd,n)            ((sd)->sd_gid = cpu_to_le16((n)))
#define reiserfs_sd_v1_size(sd)                 (le32_to_cpu((sd)->sd_size))
#define reiserfs_set_sd_v1_size(sd,n)           ((sd)->sd_size = cpu_to_le32((n)))
#define reiserfs_sd_v1_atime(sd)                (le32_to_cpu((sd)->sd_atime))
#define reiserfs_set_sd_v1_atime(sd,n)          ((sd)->sd_atime = cpu_to_le32((n)))
#define reiserfs_sd_v1_mtime(sd)                (le32_to_cpu((sd)->sd_mtime))
#define reiserfs_set_sd_v1_mtime(sd,n)          ((sd)->sd_mtime = cpu_to_le32((n)))
#define reiserfs_sd_v1_ctime(sd)                (le32_to_cpu((sd)->sd_ctime))
#define reiserfs_set_sd_v1_ctime(sd,n)          ((sd)->sd_ctime = cpu_to_le32((n)))
#define reiserfs_sd_v1_blocks(sd)               (le32_to_cpu((sd)->u.sd_blocks))
#define reiserfs_set_sd_v1_blocks(sd,n)         ((sd)->u.sd_blocks = cpu_to_le32((n)))
#define reiserfs_sd_v1_rdev(sd)                 (le32_to_cpu((sd)->u.sd_rdev))
#define reiserfs_set_sd_v1_rdev(sd,n)           ((sd)->u.sd_rdev = cpu_to_le32((n)))
#define reiserfs_sd_v1_fdb(sd)			(le32_to_cpu((sd)->sd_fdb))
#define reiserfs_set_sd_v1_fdb(sd,n)		((sd)->sd_fdb = cpu_to_le32((n)))

/* reiserfs_sd_t */
#define reiserfs_sd_v2_mode(sd)                 (le16_to_cpu((sd)->sd_mode))
#define reiserfs_set_sd_v2_mode(sd,n)           ((sd)->sd_mode = cpu_to_le16((n)))
#define reiserfs_sd_v2_attrs(sd)		(le16_to_cpu((sd)->sd_attrs))
#define reiserfs_set_sd_v2_attrs(sd,n)		((sd)->sd_attrs = cpu_to_le16((n)))
#define reiserfs_sd_v2_nlink(sd)                (le32_to_cpu((sd)->sd_nlink))
#define reiserfs_set_sd_v2_nlink(sd,n)          ((sd)->sd_nlink = cpu_to_le32((n))) 
#define reiserfs_sd_v2_size(sd)                 (le64_to_cpu((sd)->sd_size))
#define reiserfs_set_sd_v2_size(sd,n)           ((sd)->sd_size = cpu_to_le64((n)))
#define reiserfs_sd_v2_uid(sd)                  (le32_to_cpu((sd)->sd_uid))
#define reiserfs_set_sd_v2_uid(sd,n)            ((sd)->sd_uid = cpu_to_le32((n)))
#define reiserfs_sd_v2_gid(sd)                  (le32_to_cpu((sd)->sd_gid))
#define reiserfs_set_sd_v2_gid(sd,n)            ((sd)->sd_gid = cpu_to_le32((n)))
#define reiserfs_sd_v2_atime(sd)                (le32_to_cpu((sd)->sd_atime))
#define reiserfs_set_sd_v2_atime(sd,n)          ((sd)->sd_atime = cpu_to_le32((n)))
#define reiserfs_sd_v2_mtime(sd)                (le32_to_cpu((sd)->sd_mtime))
#define reiserfs_set_sd_v2_mtime(sd,n)          ((sd)->sd_mtime = cpu_to_le32((n)))
#define reiserfs_sd_v2_ctime(sd)                (le32_to_cpu((sd)->sd_ctime))
#define reiserfs_set_sd_v2_ctime(sd,n)          ((sd)->sd_ctime = cpu_to_le32((n)))
#define reiserfs_sd_v2_blocks(sd)               (le32_to_cpu((sd)->sd_blocks))
#define reiserfs_set_sd_v2_blocks(sd,n)         ((sd)->sd_blocks = cpu_to_le32((n)))
#define reiserfs_sd_v2_rdev(sd)                 (le32_to_cpu((sd)->u.sd_rdev))
#define reiserfs_set_sd_v2_rdev(sd,n)           ((sd)->u.sd_rdev = cpu_to_le32((n)))

extern void reiserfs_stat_init (int blocksize, int key_format, 
				__u32 dirid, __u32 objectid, 
				reiserfs_ih_t * ih, void * sd);

extern void reiserfs_stat_set (int field, reiserfs_ih_t * ih, 
			       void * sd, void * value);

extern void reiserfs_stat_get (int field, reiserfs_ih_t * ih, 
			       void * sd, void * value);

/*  access to stat data fields */
#define STAT_MODE	0
#define STAT_SIZE	1
#define STAT_NLINK	2
#define STAT_BLOCKS	3
#define STAT_FDB	4

#define reiserfs_stat_get_mode(ih, sd, pmode)		\
	reiserfs_stat_get(STAT_MODE, ih, sd, pmode)
#define reiserfs_stat_set_mode(ih, sd, pmode)		\
	reiserfs_stat_set(STAT_MODE, ih, sd, pmode)

#define reiserfs_stat_get_size(ih, sd, psize)		\
	reiserfs_stat_get(STAT_SIZE, ih, sd, psize)
#define reiserfs_stat_set_size(ih, sd, psize)		\
	reiserfs_stat_set(STAT_SIZE, ih, sd, psize)

#define reiserfs_stat_get_blocks(ih, sd, pblocks)	\
	reiserfs_stat_get(STAT_BLOCKS, ih, sd, pblocks)
	
#define reiserfs_stat_set_blocks(ih, sd, pblocks)	\
	reiserfs_stat_set(STAT_BLOCKS, ih, sd, pblocks)

#define reiserfs_stat_get_nlink(ih, sd, pnlink)		\
	reiserfs_stat_get(STAT_NLINK, ih, sd, pnlink)
	
#define reiserfs_stat_set_nlink(ih, sd, pnlink)		\
	reiserfs_stat_set(STAT_NLINK, ih, sd, pnlink)

#define reiserfs_stat_get_fdb(ih, sd, pfdb)		\
	reiserfs_stat_get(STAT_FDB, ih, sd, pfdb)
	
#define reiserfs_stat_set_fdb(ih, sd, pfdb)		\
	reiserfs_stat_set(STAT_FDB, ih, sd, pfdb)


extern int reiserfs_print_stat_data (FILE * fp, 
				     reiserfs_bh_t * bh, 
				     reiserfs_ih_t * ih, 
				     int alltimes);

#endif
