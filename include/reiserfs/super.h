/*
 *  Copyright 2000-2004 by Hans Reiser, licensing governed by 
 *  reiserfsprogs/README
 */

#ifndef REISERFS_SUPER_H
#define REISERFS_SUPER_H

#define REISERFS_SB_SIZE_V1 (sizeof(reiserfs_sb_v1_t)) /* 76 bytes */

#define reiserfs_sb_jp(sb) (&((sb)->s_v1.sb_journal))

/* values for sb_version field of struct reiserfs_super_block. sb_version is
   only reliable on filesystem with non-standard journal */
#define REISERFS_FORMAT_3_5 0
#define REISERFS_FORMAT_3_6 2
#define REISERFS_FORMAT_UNKNOWN -1
                                   

/* values for sb_mount_state field */
#define FS_CLEANLY_UMOUNTED    1 /* this was REISERFS_VALID_FS */
#define FS_NOT_CLEANLY_UMOUNTED    2 /* this was REISERFS_ERROR. It
                                              means that filesystem was not
                                              cleanly unmounted */

typedef enum {
  reiserfs_attrs_cleared       = 0x00000001,
} reiserfs_super_block_flags;

#define REISERFS_SB_SIZE (sizeof(reiserfs_sb_t)) /* 204 bytes */

/* set/get fields of super block with these defines */
#define reiserfs_sb_get_blocks(sb)	get_le32 (sb, s_v1.sb_block_count)
#define reiserfs_sb_set_blocks(sb,val)	set_le32 (sb, s_v1.sb_block_count, val)

#define reiserfs_sb_get_free(sb)	get_le32 (sb, s_v1.sb_free_blocks)
#define reiserfs_sb_set_free(sb,val)	set_le32 (sb, s_v1.sb_free_blocks, val)

#define reiserfs_sb_get_root(sb)	get_le32 (sb,s_v1.sb_root_block)
#define reiserfs_sb_set_root(sb,val)	set_le32 (sb, s_v1.sb_root_block, val)

#if 0
#define get_sb_mount_id(sb)		get_le32 (sb,s_v1.sb_mountid)
#define set_sb_mount_id(sb,val)		set_le32 (sb, s_v1.sb_mountid, val)

#define get_sb_journal_magic(sb)	get_le32 (sb, s_v1.sb_journal_magic)
#define set_sb_journal_magic(sb,val)	set_le32 (sb, s_v1.sb_journal_magic, val)
#endif

#define reiserfs_sb_get_blksize(sb)	get_le16 (sb, s_v1.sb_blocksize)
#define reiserfs_sb_set_blksize(sb,val)	set_le16 (sb, s_v1.sb_blocksize, val)

#define reiserfs_sb_get_mapmax(sb)	get_le16 (sb, s_v1.sb_oid_maxsize)
#define reiserfs_sb_set_mapmax(sb,val)	set_le16 (sb, s_v1.sb_oid_maxsize, val)

#define reiserfs_sb_get_mapcur(sb)	get_le16 (sb, s_v1.sb_oid_cursize)
#define reiserfs_sb_set_mapcur(sb,val)	set_le16 (sb, s_v1.sb_oid_cursize, val)

#define reiserfs_sb_get_umount(sb)	get_le16 (sb, s_v1.sb_umount_state)
#define reiserfs_sb_set_umount(sb,val)	set_le16 (sb, s_v1.sb_umount_state, val)

#define reiserfs_sb_get_state(sb)	get_le16 (sb, s_v1.sb_fs_state)
#define reiserfs_sb_set_state(sb,flag)	set_le16 (sb, s_v1.sb_fs_state, flag)

#define reiserfs_sb_get_hash(sb)	get_le32 (sb, s_v1.sb_hash_function_code)
#define reiserfs_sb_set_hash(sb,val)	set_le32 (sb, s_v1.sb_hash_function_code, val)

#define reiserfs_sb_get_height(sb)	get_le16 (sb, s_v1.sb_tree_height)
#define reiserfs_sb_set_height(sb,val)	set_le16 (sb, s_v1.sb_tree_height, val)

#define reiserfs_sb_get_bmaps(sb)	get_le16 (sb, s_v1.sb_bmap_nr)
#define reiserfs_sb_set_bmaps(sb,val)	set_le16 (sb, s_v1.sb_bmap_nr, val)

#define reiserfs_sb_get_version(sb)	get_le16 (sb, s_v1.sb_version)
#define reiserfs_sb_set_version(sb,val)	set_le16 (sb, s_v1.sb_version, val)

#define reiserfs_sb_get_reserved(sb)				\
	get_le16 (sb, s_v1.sb_reserved_for_journal)
#define reiserfs_sb_set_reserved(sb,val)			\
	set_le16 (sb, s_v1.sb_reserved_for_journal, val)

#define reiserfs_sb_get_gen(sb)		get_le32 (sb, sb_inode_generation)
#define reiserfs_sb_set_gen(sb,val)	set_le32 (sb, sb_inode_generation, val)

#define reiserfs_sb_get_flags(sb)	get_le32 (sb, s_flags)
#define reiserfs_sb_set_flags(sb, val)	set_le32 (sb, s_flags, val)

#define reiserfs_sb_isflag(sb, flag)				\
	(get_le32 (sb, s_flags) & flag)
#define reiserfs_sb_mkflag(sb, flag)				\
	set_le32 (sb, s_flags, get_le32 (sb, s_flags) | flag)
#define reiserfs_sb_clflag(sb, flag)				\
	set_le32 (sb, s_flags, get_le32 (sb, s_flags) & ~(flag))

/* 0 does not guarantee that fs is consistent */
#define reiserfs_sb_state_ok(fs) \
	((reiserfs_sb_get_umount (fs->fs_ondisk_sb) == FS_CLEANLY_UMOUNTED) && \
	 (reiserfs_sb_get_state (fs->fs_ondisk_sb) == FS_CONSISTENT))


/* prejournalled reiserfs had signature in the other place in super block */
#define REISERFS_SUPER_MAGIC_STRING_OFFSET_NJ 20

/* f_type of struct statfs will be set at this value by statfs(2) */
#define REISERFS_SUPER_MAGIC 0x52654973

/* various reiserfs signatures. We have 3 so far. ReIsErFs for 3.5 format with
   standard journal, ReIsEr2Fs for 3.6 (or converted 3.5) and ReIsEr3Fs for
   filesystem with non-standard journal (formats are distinguished by
   sb_version in that case). Those signatures should be looked for at the
   64-th and at the 8-th 1k block of the device */
#define REISERFS_3_5_SUPER_MAGIC_STRING "ReIsErFs"
#define REISERFS_3_6_SUPER_MAGIC_STRING "ReIsEr2Fs"
#define REISERFS_JR_SUPER_MAGIC_STRING "ReIsEr3Fs" /* JR stands for Journal
						      Relocation */

/* these are possible values for sb_fs_state */
enum reiserfs_state_flags {
	/* this is set by mkreiserfs and by reiserfsck */
	FS_CONSISTENT   = 0x0,
	/* this is set by the kernel when fsck is wanted. */
	FS_ERROR	= 0x1,
	/* this is set by fsck when fatal corruption is found */
	FS_FATAL	= 0x2,
	/* this is set by kernel when io error occures */
	/* IO_ERROR	= 0x4, */
	
	FS_STATE_LAST   = 0x5
};

extern int reiserfs_super_35_magic (reiserfs_sb_t * rs);

extern int reiserfs_super_36_magic (reiserfs_sb_t * rs);

extern int reiserfs_super_jr_magic (reiserfs_sb_t * rs);

extern int reiserfs_super_magic (reiserfs_sb_t * rs);

extern int reiserfs_super_format (reiserfs_sb_t * sb);

extern int reiserfs_super_size (reiserfs_sb_t * rs);

extern int reiserfs_super_prejournaled (reiserfs_sb_t * rs);

extern void reiserfs_super_print_state (FILE * fp, 
					reiserfs_filsys_t * fs);

extern int reiserfs_super_print (FILE * fp, 
				 reiserfs_filsys_t *, 
				 char * file_name, 
				 reiserfs_bh_t * bh, 
				 int short_print);

extern int reiserfs_super_valid(reiserfs_bh_t *bh);

#endif
