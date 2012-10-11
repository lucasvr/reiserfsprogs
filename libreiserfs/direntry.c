/*
 *  Copyright 2000-2004 by Hans Reiser, licensing governed by 
 *  reiserfsprogs/README
 */

/* length of the directory entry in directory item. This define calculates
   length of i-th directory entry using directory entry locations from dir
   entry head. When it calculates length of 0-th directory entry, it uses
   length of whole item in place of entry location of the non-existent
   following entry in the calculation.  See picture above.*/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "reiserfs/libreiserfs.h"

// NOTE: this is not name length. This is length of whole entry
int reiserfs_direntry_entry_len (reiserfs_ih_t * ih, 
				 reiserfs_deh_t * deh, 
				 int pos_in_item)
{
    if (pos_in_item)
	return (reiserfs_deh_get_loc (deh - 1) - reiserfs_deh_get_loc (deh));
    return (reiserfs_ih_get_len (ih) - reiserfs_deh_get_loc (deh));
}


int reiserfs_direntry_name_len (reiserfs_ih_t * ih,
				reiserfs_deh_t * deh, 
				int pos_in_item)
{
    int len, i;
    char * name;

    len = reiserfs_direntry_entry_len (ih, deh, pos_in_item);
    name = reiserfs_deh_name (deh, pos_in_item);

    // name might be padded with 0s
    i = 0;
    while (name[i] && i < len)
	i++;

    return i;
}

int reiserfs_direntry_entry_estimate (char * name, int key_format) {
    if (key_format == KEY_FORMAT_2)
    	return MISC_ROUND_UP (strlen(name));
    else if (key_format == KEY_FORMAT_1)
    	return strlen(name);

    return -1;
}

/* the only corruption which is not considered fatal - is hash mismatching. If
   bad_dir is set - directory item having such names is considered bad */
int reiserfs_direntry_check (reiserfs_filsys_t * fs, 
			     reiserfs_ih_t * ih, 
			     char * item, 
			     int bad_dir)
{
    int i;
    int namelen;
    reiserfs_deh_t * deh = (reiserfs_deh_t *)item;
    __u32 prev_offset = 0;
    __u16 prev_location = reiserfs_ih_get_len (ih);
    
    for (i = 0; i < reiserfs_ih_get_entries (ih); i ++, deh ++) {
	if (reiserfs_deh_get_loc (deh) >= prev_location)
	    return 1;
	prev_location = reiserfs_deh_get_loc (deh);
	    
	namelen = reiserfs_direntry_name_len (ih, deh, i);
	if (namelen > REISERFS_NAME_MAX)
	    return 1;
	
	if (reiserfs_deh_get_off (deh) <= prev_offset)
	    return 1;
	prev_offset = reiserfs_deh_get_off (deh);
	
	/* check hash value */
	if (!reiserfs_hash_correct (&fs->hash, 
				    item + prev_location, 
				    namelen, prev_offset)) 
	{
	    if (bad_dir)
		/* make is_bad_leaf to not insert whole leaf. Node will be
		   marked not-insertable and put into tree item by item in
		   pass 2 */
		return 1;
	}
    }

    return 0;
}

int reiserfs_direntry_loc_check (reiserfs_deh_t * deh, 
				 reiserfs_ih_t * ih, 
				 int first)
{
    if (reiserfs_deh_get_loc (deh) < 
	REISERFS_DEH_SIZE * reiserfs_ih_get_entries (ih))
    {
	return 1;
    }
    
    if (reiserfs_deh_get_loc (deh) >= reiserfs_ih_get_len (ih))
	return 1;

    if (!first) {
	if (reiserfs_deh_get_loc (deh) >= reiserfs_deh_get_loc (deh - 1))
	    return 1;
    }

    return 0;
}

void reiserfs_direntry_print (FILE * fp, reiserfs_filsys_t * fs,
			      reiserfs_bh_t * bh, reiserfs_ih_t * ih)
{
    int i;
    int namelen;
    reiserfs_deh_t * deh;
    char * name;
/*    static char namebuf [80];*/

    if (!reiserfs_ih_dir (ih))
	return;

    /*printk ("\n%2%-25s%-30s%-15s%-15s%-15s\n", "    Name", "length", 
		"Object key", "Hash", "Gen number", "Status"); */
    
    reiserfs_warning (fp, "%3s: %-25s%s%-22s%-12s%s\n", "###", "Name", 
		      "length", "    Object key", "   Hash", "Gen number");
    
    deh = reiserfs_deh (bh, ih);
    for (i = 0; i < reiserfs_ih_get_entries (ih); i ++, deh ++) {
	if (reiserfs_direntry_loc_check (deh, ih, i == 0 ? 1 : 0)) {
	    reiserfs_warning (fp, "%3d: wrong entry location %u, deh_offset %u\n",
			      i, reiserfs_deh_get_loc (deh), reiserfs_deh_get_off (deh));
	    continue;
	}
	if (i && reiserfs_direntry_loc_check (deh - 1, ih, ((i - 1) == 0) ? 1 : 0))
	    /* previous entry has bad location so we can not calculate entry
               length */
	    namelen = 25;
	else
	    namelen = reiserfs_direntry_name_len (ih, deh, i);

	name = reiserfs_deh_name (deh, i);
	reiserfs_warning (fp, "%3d: \"%-25.*s\"(%3d)%20K%12d%5d, loc %u, "
			  "state %x %s\n", i, namelen, name, namelen,
			  (reiserfs_key_t *)&(deh->deh2_dir_id),
			  OFFSET_HASH (reiserfs_deh_get_off (deh)),
			  OFFSET_GEN (reiserfs_deh_get_off (deh)),
			  reiserfs_deh_get_loc (deh), reiserfs_deh_get_state (deh),
			  reiserfs_hash_name (reiserfs_hash_find (
				name, namelen, reiserfs_deh_get_off (deh), 
				fs ? reiserfs_sb_get_hash (fs->fs_ondisk_sb) : 
				UNSET_HASH)));
	
	/*fs ? (reiserfs_hash_correct (&fs->hash, name, 
		namelen, deh_offset (deh)) ? "" : "(BROKEN)") : "??");*/
    }
}
