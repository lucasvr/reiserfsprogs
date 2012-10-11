/*
 *  Copyright 2000-2004 by Hans Reiser, licensing governed by 
 *  reiserfsprogs/README
 */

#ifndef REISERFS_DIRENTRY_H
#define REISERFS_DIRENTRY_H

#include "reiserfs/types.h"

/***************************************************************************/
/*                      DIRECTORY STRUCTURE                                */
/***************************************************************************/
/* 
   Picture represents the structure of directory items
   ________________________________________________
   |  Array of     |   |     |        |       |   |
   | directory     |N-1| N-2 | ....   |   1st |0th|
   | entry headers |   |     |        |       |   |
   |_______________|___|_____|________|_______|___|
                    <----   directory entries         ------>

 First directory item has k_offset component 1. We store "." and ".."
 in one item, always, we never split "." and ".." into differing
 items.  This makes, among other things, the code for removing
 directories simpler. */

/* Each directory entry has its header. This header has deh_dir_id and
   deh_objectid fields, those are key of object, entry points to */

/* NOT IMPLEMENTED:   
   Directory will someday contain stat data of object */
struct reiserfs_de_head
{
    __u32 deh2_offset;  /* third component of the directory entry key */
    __u32 deh2_dir_id;  /* objectid of the parent directory of the object,
			   that is referenced by directory entry */
    __u32 deh2_objectid;/* objectid of the object, that is referenced by
			   directory entry */
    __u16 deh2_location;/* offset of name in the whole item */
    __u16 deh2_state;   /* whether 1) entry contains stat data (for future),
			   and 2) whether entry is hidden (unlinked) */
} __attribute__ ((__packed__));

typedef struct reiserfs_de_head reiserfs_deh_t;

#define REISERFS_DEH_SIZE sizeof(reiserfs_deh_t)

/* set/get fields of dir entry head these defines */
#define reiserfs_deh_get_off(deh)	get_le32 (deh, deh2_offset)
#define reiserfs_deh_set_off(deh,val)	set_le32 (deh, deh2_offset, val)

#define reiserfs_deh_get_did(deh)	get_le32 (deh, deh2_dir_id)
#define reiserfs_deh_set_did(deh,val)	set_le32 (deh, deh2_dir_id, val)

#define reiserfs_deh_get_obid(deh)	get_le32 (deh, deh2_objectid)
#define reiserfs_deh_set_obid(deh,val)	set_le32 (deh, deh2_objectid, val)

#define reiserfs_deh_get_loc(deh)	get_le16 (deh, deh2_location)
#define reiserfs_deh_set_loc(deh,val)	set_le16 (deh, deh2_location, val)

#define reiserfs_deh_get_state(deh)	get_le16 (deh, deh2_state)
#define reiserfs_deh_set_state(deh,val)	set_le16 (deh, deh2_state, val)

#define reiserfs_deh_name(deh, pos)				\
	((char *)(deh - pos) + reiserfs_deh_get_loc(deh))

/* empty directory contains two entries "." and ".." and their headers */
#define REISERFS_DIR_MIN					\
	(REISERFS_DEH_SIZE * 2 +				\
	 MISC_ROUND_UP (strlen (".")) +				\
	 MISC_ROUND_UP (strlen ("..")))

/* old format directories have this size when empty */
#define REISERFS_DIR_MIN_V1 (REISERFS_DEH_SIZE * 2 + 3)

#define DEH_Statdata 0			/* not used now */
#define DEH_Visible2 2

#define DEH_Bad_offset 4 /* fsck marks entries to be deleted with this flag */
#define DEH_Bad_location 5

#define reiserfs_deh_state_bit(deh,bit)				\
	(reiserfs_deh_get_state (deh) & (1 << bit))

#define reiserfs_deh_state_set_bit(deh,bit)			\
{								\
	__u16 state;						\
	state = reiserfs_deh_get_state (deh);			\
	state |= (1 << bit);					\
	reiserfs_deh_set_state(deh, state);			\
}

#define reiserfs_deh_state_clear_bit(deh,bit)			\
{								\
	__u16 state;						\
	state = reiserfs_deh_get_state (deh);			\
	state &= ~(1 << bit);					\
	reiserfs_deh_set_state(deh, state);			\
}

/* Bad means "hashed unproperly or/and invalid location" */
#define reiserfs_deh_locbad(deh)				\
	reiserfs_deh_state_bit (deh, DEH_Bad_location)
#define reiserfs_deh_set_locbad(deh)				\
	reiserfs_deh_state_set_bit (deh, DEH_Bad_location)
#define reiserfs_deh_set_locok(deh)				\
	reiserfs_deh_state_clear_bit (deh, DEH_Bad_location)

#define reiserfs_deh_offbad(deh)				\
	reiserfs_deh_state_bit (deh, DEH_Bad_offset)
#define reiserfs_deh_set_offbad(deh)				\
	reiserfs_deh_state_set_bit (deh, DEH_Bad_offset)

#define reiserfs_deh_bad(deh)					\
	(reiserfs_deh_locbad(deh) || reiserfs_deh_offbad(deh))


/* for directories st_blocks is number of 512 byte units which fit into dir
   size round up to blocksize */
#define REISERFS_DIR_BLOCKS(size) ((size + 511) / 512)

/* array of the entry headers */
#define reiserfs_deh(bh,ih) ((reiserfs_deh_t *)(reiserfs_item_by_ih(bh,ih)))

#define REISERFS_NAME_MAX	256
/* #define REISERFS_NAME_MAX(block_size)				\
	(block_size - REISERFS_NODEH_SIZE -			\
	 REISERFS_IH_SIZE - REISERFS_DEH_SIZE)
*/

/* -REISERFS_SD_SIZE when entry will contain stat data */

extern int reiserfs_direntry_loc_check (reiserfs_deh_t * deh, 
					reiserfs_ih_t * ih, 
					int first);

extern int reiserfs_direntry_entry_len (reiserfs_ih_t * ih, 
					reiserfs_deh_t * deh,
					int pos_in_item);

extern int reiserfs_direntry_name_len (reiserfs_ih_t * ih,
				       reiserfs_deh_t * deh, 
				       int pos_in_item);

extern int reiserfs_direntry_entry_estimate (char * name, 
					     int key_format);

extern int reiserfs_direntry_check (reiserfs_filsys_t * fs, 
				    reiserfs_ih_t * ih, 
				    char * item, 
				    int bad_dir);

extern void reiserfs_direntry_print (FILE * fp, 
				     reiserfs_filsys_t * fs,
				     reiserfs_bh_t * bh, 
				     reiserfs_ih_t * ih);
#endif
