/*
 *  Copyright 2000-2004 by Hans Reiser, licensing governed by 
 *  reiserfsprogs/README
 */

#ifndef REISERFS_KEY_H
#define REISERFS_KEY_H

/* set/get fields of keys on disk with these defines */
#define reiserfs_key_get_did(k)		get_le32 (k, k2_dir_id)
#define reiserfs_key_set_did(k,val)	set_le32 (k, k2_dir_id, val)

#define reiserfs_key_get_oid(k)		get_le32 (k, k2_objectid)
#define reiserfs_key_set_oid(k,val)	set_le32 (k, k2_objectid, val)

#define reiserfs_key_get_off1(k)	get_le32 (k, u.k2_offset_v1.k_offset)
#define reiserfs_key_set_off1(k,val)	set_le32 (k, u.k2_offset_v1.k_offset, val)

#define reiserfs_key_get_uni(k)		get_le32 (k, u.k2_offset_v1.k_uniqueness)
#define reiserfs_key_set_uni(k,val)	set_le32 (k, u.k2_offset_v1.k_uniqueness, val)

#define REISERFS_KEY_SIZE		(sizeof(reiserfs_key_t))
#define REISERFS_KEY_SHSIZE		8


// values for k_uniqueness field of the reiserfs_key_t
#define UNI_SD		0
#define UNI_DE		500
#define UNI_DE		500
#define UNI_DIRECT	0xffffffff
#define UNI_EXT		0xfffffffe
#define UNI_UNKN	555

/* values for k_type field of the reiserfs_key_t */
#define TYPE_STAT_DATA 0x0
#define TYPE_EXTENT    0x1
#define TYPE_DIRECT    0X2
#define TYPE_DIRENTRY  0X3
#define TYPE_LAST      0x4
#define TYPE_UNKNOWN   0xf

#define KEY_FORMAT_1 0
#define KEY_FORMAT_2 1
#define KEY_FORMAT_UNDEFINED 15

 /* Our function for comparing keys can compare keys of different lengths.
    It takes as a parameter the length of the keys it is to compare. These
    defines are used in determining what is to be passed to it as that
    parameter. */
#define REISERFS_KEY_LEN     4
#define REISERFS_KEY_SHORT_LEN    2

/* there are 4 types of items: stat data, directory item, extent, direct.
   FIXME: This table does not describe new key format
+-------------------+------------+--------------+------------+
|	            |  k_offset  | k_uniqueness | mergeable? |
+-------------------+------------+--------------+------------+
|     stat data     |	0        |      0       |   no       |
+-------------------+------------+--------------+------------+
| 1st directory item| OFFSET_DOT |DIRENTRY_UNIQU|   no       | 
| non 1st directory | hash value |              |   yes      |
|     item          |            |              |            |
+-------------------+------------+--------------+------------+
| extent item     | offset + 1 |TYPE_EXTENT | if not the first object EXT item
+-------------------+------------+--------------+------------+
| direct item       | offset + 1 |TYPE_DIRECT   | if not the first object direct item 
+-------------------+------------+--------------+------------+
*/

#define reiserfs_key_stat(p_s_key) 	\
	(reiserfs_key_get_type (p_s_key) == TYPE_STAT_DATA )
#define reiserfs_key_dir(p_s_key)	\
	(reiserfs_key_get_type (p_s_key) == TYPE_DIRENTRY )
#define reiserfs_key_direct(p_s_key) 	\
	(reiserfs_key_get_type (p_s_key) == TYPE_DIRECT )
#define reiserfs_key_ext(p_s_key)	\
	(reiserfs_key_get_type (p_s_key) == TYPE_EXTENT )
#define reiserfs_key_unkn(p_s_key)	\
	(reiserfs_key_get_type (p_s_key) >= TYPE_LAST)

#define MAX_KEY1_OFFSET	 0xffffffff
#define MAX_KEY2_OFFSET  0xfffffffffffffffLL

/* hash value occupies 24 bits starting from 7 up to 30 */
#define OFFSET_HASH(offset) ((offset) & 0x7fffff80)
/* generation number occupies 7 bits starting from 0 up to 6 */
#define OFFSET_GEN(offset) ((offset) & 0x0000007f)

#define OFFSET_SD  0
#define OFFSET_DOT 1
#define OFFSET_DOT_DOT 2

extern const reiserfs_key_t MIN_KEY;
extern const reiserfs_key_t MAX_KEY;
extern const reiserfs_key_t root_dir_key;
extern const reiserfs_key_t parent_root_dir_key;
extern reiserfs_key_t lost_found_dir_key;

extern void reiserfs_key_copy (reiserfs_key_t * to, const reiserfs_key_t * from);
extern void reiserfs_key_copy2 (reiserfs_key_t * to, const reiserfs_key_t * from);
extern int  reiserfs_key_comp (const void * k1, const void * k2);
extern int  reiserfs_key_comp3 (const void * k1, const void * k2);
extern int  reiserfs_key_comp2 (const void * p_s_key1, const void * p_s_key2);

extern int reiserfs_key_format (const reiserfs_key_t * key);

extern unsigned long long reiserfs_key_get_off (const reiserfs_key_t * key);

extern int reiserfs_key_get_type (const reiserfs_key_t * key);

extern char * reiserfs_key_name (const reiserfs_key_t * key);

extern void reiserfs_key_set_type (int format, 
				   reiserfs_key_t * key, 
				   int type);

extern void reiserfs_key_set_off (int format, 
				  reiserfs_key_t * key, 
				  unsigned long long offset);

extern void reiserfs_key_set_sec (int format, 
				  reiserfs_key_t * key, 
				  unsigned long long offset, 
				  int type);

#endif
