/*
 * Copyright 1996-2004 by Hans Reiser, licensing governed by 
 * reiserfsprogs/README
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "fsck.h"
#include "misc/malloc.h"
#include "misc/unaligned.h"
#include "util/misc.h"
#include "util/device.h"

#include <assert.h>
#include <sys/stat.h>
#include <errno.h>
#include <time.h>


/* 
 * Pass0 scans the used part of the partition. It creates two maps which will 
 * be used on the pass 1. These are a map of nodes looking like leaves and 
 * a map of "bad" unformatted nodes. After pass 0 we can detect unformatted 
 * node pointers pointing to leaves. 
 */


/* leaves */
reiserfs_bitmap_t * leaves_bitmap;

#define pass0_block_mkleaf(block) \
	reiserfs_bitmap_set_bit(leaves_bitmap, block)

int pass0_block_isleaf(unsigned long block) {
	return reiserfs_bitmap_test_bit(leaves_bitmap, block);
}

void pass0_block_clleaf(unsigned long block) {
	reiserfs_bitmap_clear_bit(leaves_bitmap, block);
}

/* nodes which are referred to from only one extent item */
reiserfs_bitmap_t * good_unfm_bitmap;
#define pass0_block_mkgood_unfm(block)	\
	reiserfs_bitmap_set_bit(good_unfm_bitmap, block)
#define pass0_block_clgood_unfm(block)	\
	reiserfs_bitmap_clear_bit(good_unfm_bitmap, block)

int pass0_block_isgood_unfm(unsigned long block) {
	return reiserfs_bitmap_test_bit(good_unfm_bitmap, block);
}

/* nodes which are referred to from more than one extent item */
reiserfs_bitmap_t * bad_unfm_bitmap;
#define pass0_block_mkbad_unfm(block)	\
	reiserfs_bitmap_set_bit(bad_unfm_bitmap, block)
#define pass0_block_clbad_unfm(block)	\
	reiserfs_bitmap_clear_bit(bad_unfm_bitmap, block)

int pass0_block_isbad_unfm(unsigned long block) {
	return reiserfs_bitmap_test_bit(bad_unfm_bitmap, block);
}

static int correct_direct_item_offset (reiserfs_ih_t *ih, 
				       reiserfs_filsys_t *fs) 
{
    __u64 offset;
    __u64 len;
    int format;

    offset = reiserfs_key_get_off (&ih->ih_key);
    format = reiserfs_key_format (&ih->ih_key);
    
    if (offset == 0)
	return 0;
    
    if (format == KEY_FORMAT_2) {
	if ((offset - 1) % 8 != 0)
	    return 0;
    } 

    len = reiserfs_ih_get_len(ih) + offset - 1;
    offset = MISC_DOWN(offset - 1, fs->fs_blocksize);
    
    if (reiserfs_policy_ext (len, len - offset, fs->fs_blocksize))
	return 0;
    
    return 1;
}

/* bitmaps which are built on pass 0 and are used on pass 1 */
static void fsck_pass0_aux_prep (reiserfs_filsys_t * fs)
{
    reiserfs_sb_t * sb;

    sb = fs->fs_ondisk_sb;

    /* bitmap of leaves found on the device */
    leaves_bitmap = reiserfs_bitmap_create (reiserfs_sb_get_blocks (sb));

    good_unfm_bitmap = reiserfs_bitmap_create (reiserfs_sb_get_blocks (sb));

    bad_unfm_bitmap = reiserfs_bitmap_create (reiserfs_sb_get_blocks (sb));
}

void fsck_pass0_aux_fini (void) {
    reiserfs_bitmap_delete (leaves_bitmap);
    reiserfs_bitmap_delete (good_unfm_bitmap);
    reiserfs_bitmap_delete (bad_unfm_bitmap);
}


/* register block some extent item points to */
static void register_unfm (unsigned long block)
{
    if (!pass0_block_isgood_unfm (block) && 
	!pass0_block_isbad_unfm (block)) 
    {
	/* this block was not pointed by other extent items yet */
	pass0_block_mkgood_unfm (block);
	return;
    }

    if (pass0_block_isgood_unfm (block)) {
	/* block was pointed once already, unmark it in bitmap of good
           unformatted nodes and mark in bitmap of bad pointers */
	pass0_block_clgood_unfm (block);
	pass0_block_mkbad_unfm (block);
	return;
    }

    assert (pass0_block_isbad_unfm (block));
}


/* 'upper' item is correct if 'upper + 2' exists and its key is greater than
   key of 'upper' */
static int upper_correct (reiserfs_bh_t * bh, reiserfs_ih_t * upper,
			  int upper_item_num)
{
    if (upper_item_num + 2 < reiserfs_node_items (bh)) {
	if (reiserfs_key_comp (&upper->ih_key, &(upper + 2)->ih_key) != -1)
	    /* item-num's item is out of order of order */
	    return 0;
	return 1;
    }
    
    /* there is no item above the "bad pair" */
    return 2;
}


/* 'lower' item is correct if 'lower - 2' exists and its key is smaller than
   key of 'lower' */
static int lower_correct (reiserfs_bh_t * bh, reiserfs_ih_t * lower,
			  int lower_item_num)
{
    if (lower_item_num - 2 >= 0) {
	if (reiserfs_key_comp (&(lower - 2)->ih_key, &lower->ih_key) != -1)
	    return 0;
	return 1;
    }
    return 2;
}


/* return 1 if something was changed */
static int correct_key_format (reiserfs_ih_t * ih, int symlink) 
{
    int dirty = 0;

    if (reiserfs_ih_stat (ih)) {
	/* for stat data we have no way to check whether key format in item
	   head matches to the key format found from the key directly */
	if (reiserfs_ih_get_len (ih) == REISERFS_SD_SIZE_V1) {
	    if (reiserfs_ih_get_format (ih) != KEY_FORMAT_1) {
		/*fsck_log ("correct_key_format: ih key format of (%H) is "
			    "set to format 1\n", ih);*/
		    
		reiserfs_ih_set_format (ih, KEY_FORMAT_1);
		return 1;
	    }
	    return 0;
	}
	if (reiserfs_ih_get_len (ih) == REISERFS_SD_SIZE) {
	    if (reiserfs_ih_get_format (ih) != KEY_FORMAT_2) {
		/*fsck_log ("correct_key_format: ih key format of (%H) "
			    "is set to format 2\n", ih);*/
		reiserfs_ih_set_format (ih, KEY_FORMAT_2);
		return 1;
	    }
	    return 0;
	}
	
	misc_die ("stat_data item of the wrong length");
    }
    
    if (symlink && (reiserfs_key_format(&ih->ih_key) != KEY_FORMAT_1)) {
	/* All symlinks are of 3.5 format */
	/*fsck_log ("correct_key_format: Symlink keys should be of 3.5 "
		    "format. %k - fixed.\n", &ih->ih_key); */
	reiserfs_key_set_sec(KEY_FORMAT_1, &ih->ih_key, 
			     reiserfs_key_get_off(&ih->ih_key), 
			     reiserfs_key_get_type(&ih->ih_key));
    }
	
    if (reiserfs_key_format (&ih->ih_key) != reiserfs_ih_get_format (ih)) {
	/*fsck_log ("correct_key_format: ih key format of (%H) is "
		    "set to format found in the key\n", ih);*/
	reiserfs_ih_set_format (ih, reiserfs_key_format (&ih->ih_key));
	dirty = 1;
    }
    
    return dirty;
}

static void hash_hits_init (reiserfs_filsys_t * fs)
{
    fsck_data (fs)->rebuild.hash_amount = REISERFS_HASH_LAST;
    fsck_data (fs)->rebuild.hash_hits = misc_getmem (sizeof (unsigned long) * 
		    fsck_data (fs)->rebuild.hash_amount);
}


static void add_hash_hit (reiserfs_filsys_t * fs, int hash_code)
{
    fsck_data (fs)->rebuild.hash_hits [hash_code] ++;
}


/* deh_location look reasonable, try to find name length. return 0 if
   we failed */
static int try_to_get_name_length (reiserfs_ih_t * ih, 
				   reiserfs_deh_t * deh,
				   int i)
{
    int len;

    if (i == 0 || !reiserfs_deh_locbad (deh - 1)) {
	len = reiserfs_direntry_name_len (ih, deh, i);
	return (len > 0) ? len : 0;
    }

    /* previous entry had bad location so we had no way to find
       name length */
    return 0;
}



/* define this if you are using -t to debug recovering of corrupted directory
   item */
#define DEBUG_VERIFY_DENTRY
#undef DEBUG_VERIFY_DENTRY


/* check directory item and try to recover something */
static int verify_directory_item (reiserfs_filsys_t * fs, 
				  reiserfs_bh_t * bh,
				  int item_num)
{
    reiserfs_ih_t * ih;
    reiserfs_ih_t tmp;
    char * item;
    reiserfs_deh_t * deh;
    char * name;
    int name_len;
    int bad, lost_found;
    int i, j;
    int dirty;
    int hash_code;
    int bad_locations;
    int min_entry_size = 1;

#ifdef DEBUG_VERIFY_DENTRY
    char * direntries;
#endif


    ih = reiserfs_ih_at (bh, item_num);
    item = reiserfs_item_by_ih (bh,ih);
    deh = (reiserfs_deh_t *)item;

    dirty = 0;
    bad_locations = 0;
 
    if ( (reiserfs_ih_get_entries (ih) > 
	  (reiserfs_ih_get_len(ih) / (REISERFS_DEH_SIZE + min_entry_size))) ||
          (reiserfs_ih_get_entries (ih) == 0))
    {
        reiserfs_ih_set_entries (ih, (int)reiserfs_ih_get_len(ih) / 
			    (REISERFS_DEH_SIZE + min_entry_size));
	
        reiserfs_buffer_mkdirty (bh);
    }

    if (reiserfs_ih_get_entries (ih) == 0) {
	reiserfs_leaf_delete_item (fs, bh, item_num);
	return -1;
    }


    /* check deh_location */
    for (i = 0; i < reiserfs_ih_get_entries (ih); i ++) {
	/* silently fix deh_state */
	if (reiserfs_deh_get_state (deh + i) != (1 << DEH_Visible2)) {
	    reiserfs_deh_set_state (deh + i, (1 << DEH_Visible2));
	    reiserfs_buffer_mkdirty (bh);
	}
	if (reiserfs_direntry_loc_check (deh + i, ih, !i))
	    reiserfs_deh_set_locbad (deh + i);
    }    

#ifdef DEBUG_VERIFY_DENTRY
    direntries = misc_getmem (ih_entry_count (ih) * sizeof (int));

    printf ("Entries with bad locations within the directory: ");
    for (i = 0; i < ih_entry_count (ih); i ++) {
	if (reiserfs_deh_locbad (deh + i))
	    printf ("%d ", i);
    }
    
    printf ("\n");
#endif /* DEBUG_VERIFY_DENTRY */


    /* find entries names in which have mismatching deh_offset */
    for (i = reiserfs_ih_get_entries (ih) - 1; i >= 0; i --) {
	if (reiserfs_deh_bad (deh + i))
	    /* bad location */
	    continue;

	if (i) {
	    if (reiserfs_deh_get_loc (deh + i - 1) < 
		reiserfs_deh_get_loc (deh + i))
	    {
		reiserfs_deh_set_locbad (deh + i - 1);
	    }
	}

	name = reiserfs_deh_name (deh + i, i);
	/* Although we found a name, we not always can get its length as
           it depends on deh_location of previous entry. */
	name_len = try_to_get_name_length (ih, deh + i, i);

#ifdef DEBUG_VERIFY_DENTRY
	if (name_len == 0)
	    printf ("Trying to find the name length for %d-th entry\n", i);
#endif /* DEBUG_VERIFY_DENTRY */
	if (is_dot (name, name_len)) {
	    if (i != 0)
		fsck_log ("block %lu: item %d: \".\" must be the first entry, "
			  "but it is the %d-th entry\n", bh->b_blocknr, 
			  item_num, i);
	    
	    /* check and fix "." */
	    
	    if (reiserfs_deh_get_off (deh + i) != OFFSET_DOT) {
		reiserfs_deh_set_off (deh + i, OFFSET_DOT);
		reiserfs_buffer_mkdirty (bh);
	    }
	    /* "." must point to the directory it is in */
/*
	    if (reiserfs_key_comp2 (&(deh[i].deh2_dir_id), &(ih->ih_key))) {
		fsck_log ("verify_direntry: block %lu, item %H has entry \".\" "
			  "pointing to (%K) instead of (%K)\n", 
			  bh->b_blocknr, ih,
			  &(deh[i].deh2_dir_id), &(ih->ih_key));
		reiserfs_deh_set_did (deh + i, reiserfs_key_get_did (&ih->ih_key));
		reiserfs_deh_set_obid (deh + i, reiserfs_key_get_oid (&ih->ih_key));
		reiserfs_buffer_mkdirty (bh);
	    }
*/
	} else if (is_dot_dot (name, name_len)) {
	    if (i != 1)
		fsck_log ("block %lu: item %d: \"..\" is %d-th entry\n",
			  bh->b_blocknr, item_num, i);
	    
	    /* check and fix ".." */
	    if (reiserfs_deh_get_off (deh + i) != OFFSET_DOT_DOT) {
		reiserfs_deh_set_off (deh + i, OFFSET_DOT_DOT);
		reiserfs_buffer_mkdirty (bh);
	    }
	} else {
	    int min_length, max_length;

	    /* check other name */

	    if (name_len == 0) {
		/* we do not know the length of name - we will try to find it */
		min_length = 1;
		max_length = item + reiserfs_ih_get_len (ih) - name;
	    } else
		/* we kow name length, so we will try only one name length */
		min_length = max_length = name_len;

	    hash_code = 0;

	    for (j = min_length; j <= max_length; j ++) {
		hash_code = reiserfs_hash_find (name, j, 
			reiserfs_deh_get_off(deh + i), 
			reiserfs_sb_get_hash(fs->fs_ondisk_sb));
		
/*		add_hash_hit (fs, hash_code);*/
		if (reiserfs_hash_func (hash_code) != 0) {
		    /* deh_offset matches to some hash of the name */
		    if (fsck_hash_defined (fs) && 
			hash_code != reiserfs_hash_code (fs->hash)) 
		    {
			/* wrong hash selected - so we can skip this leaf */
			return 1;
		    }

		    if (!name_len) {
			fsck_log ("%s: block %lu, item %H: Found a name "
				  "\"%.*s\" for %d-th entry matching to the "
				  "hash %u.\n", __FUNCTION__, bh->b_blocknr, 
				  ih, j, name, i, reiserfs_deh_get_off (deh + i));
			
			/* FIXME: if next byte is 0 we think that the name is 
			   aligned to 8 byte boundary */
			
			if (i) {
			    reiserfs_deh_set_loc (&deh[i - 1], 
					      reiserfs_deh_get_loc (deh + i) +
					      ((name[j] || fs->fs_format == 
						REISERFS_FORMAT_3_5) ? j : 
					       MISC_ROUND_UP (j)));
			    
			    reiserfs_deh_set_locok (deh + i - 1);
			    reiserfs_buffer_mkdirty (bh);
			}
		    }
		    
		    break;
		}
	    }

	    add_hash_hit (fs, hash_code);

	    if (j == max_length + 1) {
		/* deh_offset does not match to anything. it will be
		   deleted for now, but maybe we could just fix a
		   deh_offset if it is in ordeer */
		reiserfs_deh_set_offbad (deh + i);
	    }
	}
    } /* for */

#ifdef DEBUG_VERIFY_DENTRY
    printf ("Entries with mismatching hash: ");
    for (i = 0; i < ih_entry_count (ih); i ++) {
	if (reiserfs_deh_offbad (deh + i))
	    printf ("%d ", i);
    }
    printf ("\n");
#endif /* DEBUG_VERIFY_DENTRY */


    /* correct deh_locations such that code cutting entries will not get
       screwed up */
    {
	int prev_loc;
	int loc_fixed;


	prev_loc = reiserfs_ih_get_len (ih);
	for (i = 0; i < reiserfs_ih_get_entries (ih); i ++) {
	    loc_fixed = 0;
	    if (reiserfs_deh_locbad (deh + i)) {
		reiserfs_deh_set_loc (deh + i, prev_loc/* - 1*/);
		reiserfs_buffer_mkdirty (bh);
		loc_fixed = 1;
	    } else {
		if (reiserfs_deh_get_loc (deh + i) >= prev_loc) {
		    reiserfs_deh_set_loc (deh + i, prev_loc/* - 1*/);
		    reiserfs_buffer_mkdirty (bh);
		    loc_fixed = 1;
		}
	    }

	    prev_loc = reiserfs_deh_get_loc (deh + i);
	    
	    if (i == reiserfs_ih_get_entries (ih) - 1) {
		/* last entry starts right after an array of direntry headers */
		if (!reiserfs_deh_bad (deh + i) &&
		    reiserfs_deh_get_loc (deh + i) != 
		    (REISERFS_DEH_SIZE * reiserfs_ih_get_entries (ih))) 
		{
		    /* free space in the directory item */
		    fsck_log ("%s: block %lu, item %H: Directory item has a "
			      "free space - deleting\n", __FUNCTION__, 
			      bh->b_blocknr, ih);
		    
		    reiserfs_leaf_delete_entry (fs, bh, item_num, 
			    reiserfs_ih_get_entries (ih), 0);
		}
		
		if (reiserfs_deh_get_loc (deh + i) != 
		    (REISERFS_DEH_SIZE * reiserfs_ih_get_entries (ih))) 
		{
		    reiserfs_deh_set_loc (&deh[i], 
			    (REISERFS_DEH_SIZE * reiserfs_ih_get_entries (ih)));
		    loc_fixed = 1;
		    reiserfs_buffer_mkdirty (bh);
		}
	    }

#ifdef DEBUG_VERIFY_DENTRY
	    if (loc_fixed)
		direntries [i] = 1;
#endif
	} /* for */

#ifdef DEBUG_VERIFY_DENTRY
	printf ("Entries with fixed deh_locations: ");
	for (i = 0; i < ih_entry_count (ih); i ++) {
	    if (direntries [i])
		printf ("%d ", i);
	}
	printf ("\n");
#endif /* DEBUG_VERIFY_DENTRY */

    }

#ifdef DEBUG_VERIFY_DENTRY
    printf (" N  location name\n");
    for (i = 0; i < ih_entry_count (ih); i ++) {
	if (reiserfs_deh_bad (deh + i) ||
	    (i && reiserfs_deh_bad (deh + i - 1)) || /* previous entry marked bad */
	    (i < ih_entry_count (ih) - 1 && reiserfs_deh_bad (deh + i + 1))) 
	{
	    /* next entry is marked bad */
	    /* print only entries to be deleted and their nearest neighbors */
	    printf ("%3d: %8d ", i, deh_location (deh + i));
	    if (reiserfs_deh_bad (deh + i))
		printf ("will be deleted\n");
	    else
		printf ("\"%.*s\"\n", 
			reiserfs_direntry_name_len (ih, deh + i, i),
			reiserfs_deh_name (deh + i, i));
	}
    }
    
#endif

    bad = lost_found = 0;
    tmp = *ih;

    /* mark enries of /lost+found as bad */
    deh = reiserfs_deh (bh, ih);
    for (i = 0; i < reiserfs_ih_get_entries (ih); i ++, deh ++) {
	unsigned int dirid, objectid;
	char buf[REISERFS_NAME_MAX];

	if (reiserfs_deh_bad (deh))
	    continue;

	buf[0] = '\0';
	sprintf (buf, "%.*s", reiserfs_direntry_name_len (ih, deh, i), 
		 reiserfs_deh_name (deh, i));
	
	if (sscanf (buf, "%d_%d", &dirid, &objectid) != 2)
	    continue;
	
	if (reiserfs_deh_get_did (deh) != dirid || reiserfs_deh_get_obid (deh) != objectid)
	    continue;
	
	/* entry in lost+found */
//	printf ("%s - will be deleted\n", buf);
	lost_found++;
	reiserfs_deh_set_offbad (deh);
    }

    /* delete entries which are marked bad */
    for (i = 0; i < reiserfs_ih_get_entries (ih); i ++) {
	deh = reiserfs_deh (bh, ih) + i;
	if (reiserfs_deh_bad (deh)) {
	    bad ++;
	    if (reiserfs_ih_get_entries (ih) == 1) {
		reiserfs_leaf_delete_item (fs, bh, item_num);
		
		fsck_log ("%s: block %lu, item %H: All entries were deleted from "
		      "the directory\n", __FUNCTION__, bh->b_blocknr, &tmp);
		
		return -1;
	    } else {
		reiserfs_leaf_delete_entry (fs, bh, item_num, i, 1);
	    }
	    i --;
	}
    }
    
    deh = reiserfs_deh (bh, ih);
    if (reiserfs_key_get_off (&ih->ih_key) != reiserfs_deh_get_off (deh)) {
	fsck_log ("verify_direntry: block %lu, item %H: Key's offset %k must "
		  "match the directory's hash (%u) - changed.\n", bh->b_blocknr,
		  ih, &ih->ih_key, reiserfs_deh_get_off (deh));
	
	reiserfs_key_set_off (reiserfs_key_format (&ih->ih_key), 
				 &ih->ih_key, reiserfs_deh_get_off (deh));
	
	reiserfs_buffer_mkdirty (bh);
    }
    
    if (bad > lost_found)
	fsck_log ("pass0: block %lu, item %H: %d entries were deleted\n",
		  bh->b_blocknr, &tmp, bad - lost_found);
	
    return 0;

}


static __inline__ int does_it_fit_into_dev (__u64 offset, __u64 fs_size) {
/* 
   Count of unformatted pointers :
	offset / blocksize
   Count of blocks to store them :
	REISERFS_EXT_SIZE * offset / blocksize / REISERFS_ITEM_MAX
   Size to store it :
	blocksize * REISERFS_EXT_SIZE * offset / blocksize / REISERFS_ITEM_MAX
*/

   return ( REISERFS_EXT_SIZE * offset / REISERFS_ITEM_MAX(fs->fs_blocksize) < 
	    fs_size) ? 1 : 0;
}


static int is_wrong_short_key (reiserfs_key_t * key) {
    if (reiserfs_key_get_did (key) == 0 || 
	reiserfs_key_get_oid (key) == 0 || 
	reiserfs_key_get_oid (key) == 1 ||
	reiserfs_key_get_did (key) == ~(__u32)0 || 
	reiserfs_key_get_oid (key) == ~(__u32)0 ||
	reiserfs_key_get_did (key) == reiserfs_key_get_oid (key) ||
	/* the alloc=packing_groups used to allow dirid = 1
	(reiserfs_key_get_did (key) == 1 && reiserfs_key_get_oid (key) != 2) || */
	(reiserfs_key_get_did (key) != 1 && reiserfs_key_get_oid (key) == 2) )
    {
	return 1;
    }

    return 0;
}

/* do this on pass 0 with every leaf marked used */

/* FIXME: we can improve fixing of broken keys: we can ssfe direct items 
   which go after stat data and have broken keys */
static void pass0_correct_leaf (reiserfs_filsys_t * fs,
				reiserfs_bh_t * bh)
{
    int file_format = KEY_FORMAT_UNDEFINED;
    reiserfs_ih_t * ih;
    
    __u32 * ind_item;
    __u64 fs_size;
    __u64 offset;
    int symlnk = 0;
    int bad_order;
    
    unsigned long unfm_ptr;
//    unsigned int nr_items;
    int i, j, nr_items;
    int dirty = 0;

    fsck_leaf_check_header(fs, bh);

    /* Delete all safe links. */
    for (i = reiserfs_nh_get_items (NODE_HEAD (bh)) - 1; i >= 0; i--) {
	if (reiserfs_key_get_did (&reiserfs_ih_at (bh, i)->ih_key) == ~(__u32)0)
	{
	    reiserfs_leaf_delete_item (fs, bh, i);
	    pass_0_stat(fs)->removed++;
	}
	
	if (reiserfs_key_get_did (&reiserfs_ih_at (bh, i)->ih_key) == 
	    REISERFS_BAD_DID && 
	    reiserfs_key_get_oid (&reiserfs_ih_at (bh, i)->ih_key) == 
	    REISERFS_BAD_OID) 
	{
	    reiserfs_leaf_delete_item (fs, bh, i);
	    pass_0_stat(fs)->removed++;
	}
    }

    fs_size = (__u64)fs->fs_blocksize * 
	    reiserfs_sb_get_blocks (fs->fs_ondisk_sb);

 start_again:

    ih = reiserfs_ih_at (bh, 0);
    bad_order = 0;
    nr_items = reiserfs_nh_get_items (NODE_HEAD (bh));
    for (i = 0; i < nr_items; i ++, ih ++) {
	if (reiserfs_ih_ext(ih) && (reiserfs_ih_get_len (ih) % 4 != 0)) {
	    reiserfs_key_set_type(reiserfs_ih_get_format(ih), 
				  &ih->ih_key, TYPE_UNKNOWN);
	    
	    dirty = 1;
	}
	    
        if (reiserfs_key_unkn (&ih->ih_key)) {
            if ((reiserfs_ih_get_len (ih) == REISERFS_SD_SIZE) || 
		(reiserfs_ih_get_len (ih) == REISERFS_SD_SIZE_V1)) 
	    {
                reiserfs_key_set_sec (KEY_FORMAT_1, &ih->ih_key, 
				      OFFSET_SD, TYPE_STAT_DATA);
		dirty = 1;
                fsck_log("pass0: vpf-10100: block %lu, item (%d): Unknown item "
			 "type of StatData size, type set to StatData %k\n", 
			 bh->b_blocknr, i, &ih->ih_key);
            } else {
                fsck_log("pass0: vpf-10110: block %lu, item (%d): Unknown item "
			 "type found %k - deleted\n", bh->b_blocknr, i, 
			 &ih->ih_key);
		
                reiserfs_leaf_delete_item (fs, bh, i);
		pass_0_stat(fs)->removed++;
                goto start_again;
            }
        }

	if (is_wrong_short_key (&ih->ih_key)) {
	    /* sometimes stat datas get k_objectid==0 or k_dir_id==0 */
	    
/*	    if (i == (nr_items - 1)) {
		
		if (i == 0) {
		    fsck_log ("block %lu: item %d: (%H) id equals 0\n",
			      bh->b_blocknr, i, ih);
		    return;
		}
		// delete last item 
                fsck_log ("block %lu: item %d: (%H) id equals 0\n",
			      bh->b_blocknr, i, ih);
		reiserfs_leaf_delete_item (fs, bh, i - 1);
		pass_0_stat(fs)->removed++;
		return;
	    }
*/

            /* FIXME:
		18842 19034    0x1 EXT  (2)
		19035 19035    0x0 SD   (0)
		18842     1    0x1 EXT  (1)
		18842     1 0x1001 DRCT (2)
             */
	    /* there is next item: if it is not stat data - take its k_dir_id
               and k_objectid. if key order will be still wrong - the changed
               item will be deleted */

	    if (i && !reiserfs_ih_stat (ih)) {
	        /* previous item has a wrong short_key */
		fsck_log ("pass0: vpf-10120: block %lu: item %d: Wrong key %k, "
			  "corrected to ", bh->b_blocknr, i, &ih->ih_key);
		reiserfs_key_set_did (&ih->ih_key, 
			reiserfs_key_get_did (&(ih - 1)->ih_key));
		reiserfs_key_set_oid (&ih->ih_key, 
			reiserfs_key_get_oid (&(ih - 1)->ih_key));
		
		fsck_log ("%k\n", &ih->ih_key);
		dirty = 1;
	    } else if ((i < nr_items - 1) && !reiserfs_ih_stat (ih + 1)) {
	        if (!is_wrong_short_key(&(ih + 1)->ih_key)) {
		    fsck_log ("pass0: vpf-10130: block %lu: item %d: Wrong "
			      "key %k, corrected to ", bh->b_blocknr, i, 
			      &ih->ih_key);
		    
		    reiserfs_key_set_did (&ih->ih_key, 
			    reiserfs_key_get_did (&(ih + 1)->ih_key));
		    
		    reiserfs_key_set_oid (&ih->ih_key, 
			    reiserfs_key_get_oid (&(ih + 1)->ih_key));
		    
/*		    reiserfs_key_set_off (KEY_FORMAT_1, &ih->ih_key, 0);
		    reiserfs_key_set_type (KEY_FORMAT_1, &ih->ih_key, 
					   TYPE_STAT_DATA);*/
		    fsck_log ("%k\n", &ih->ih_key);
		    dirty = 1;
		    goto start_again;
		} else {
	            fsck_log ("pass0: vpf-10140: block %lu: items %d and %d "
			      "have bad short keys %k, %k, both deleted\n",
			      bh->b_blocknr, i, i+1, &ih->ih_key, 
			      &(ih + 1)->ih_key);
		    
		    reiserfs_leaf_delete_item (fs, bh, i);
		    reiserfs_leaf_delete_item (fs, bh, i);
		    pass_0_stat(fs)->removed += 2;
		    goto start_again;
		}
	    } else {
                fsck_log ("pass0: vpf-10150: block %lu: item %d: Wrong key "
			  "%k, deleted\n", bh->b_blocknr, i, &ih->ih_key);
		
		reiserfs_leaf_delete_item (fs, bh, i);
		pass_0_stat(fs)->removed++;
		goto start_again;
	    } 
	}

#if 0
	if (i && i + 1 < nr_items) {
            if (reiserfs_ih_stat (ih - 1) && !reiserfs_ih_stat (ih) &&
                !reiserfs_ih_direct (ih + 1) && !reiserfs_ih_stat (ih + 1)) {
                /* i or i+1 item should be SD or i+1 should be direct item */
                if ((reiserfs_ih_get_len (ih) == REISERFS_SD_SIZE) || 
		    (reiserfs_ih_get_len (ih) == REISERFS_SD_SIZE_V1)) 
		{
                    /* make i as SD */
                    fsck_log("pass0: vpf-10400: block %lu, item %d: Wrong "
			     "order of items - change the type of the key %k "
			     "to StatData\n",  bh->b_blocknr, i, &ih->ih_key);
                    reiserfs_key_set_sec (KEY_FORMAT_1, &ih->ih_key, 
					  OFFSET_SD, TYPE_STAT_DATA);
		    dirty = 1;
                } else if ((reiserfs_ih_get_len (ih+1) == REISERFS_SD_SIZE) || 
			   (reiserfs_ih_get_len (ih+1) == REISERFS_SD_SIZE_V1)) 
		{
                    /* make i+1 as SD */
                    fsck_log("pass0: vpf-10410: block %lu, item %d: Wrong "
			     "order of items - change the type of the key %k "
			     "to StatData\n", bh->b_blocknr, i + 1, 
			     &(ih + 1)->ih_key);
		    
                    reiserfs_key_set_sec (KEY_FORMAT_1, &(ih + 1)->ih_key, 
					  OFFSET_SD, TYPE_STAT_DATA);
		    dirty = 1;
                } else if (reiserfs_ih_ext(ih)) {
                    fsck_log("pass0: vpf-10420: block %lu, item %d: Wrong "
			     "order of items - change the type of the key %k "
			     "to Direct\n", bh->b_blocknr, i+1, 
			     &(ih + 1)->ih_key);
		    
                    reiserfs_key_set_type (reiserfs_ih_get_format(ih+1), 
					   &(ih + 1)->ih_key, 
					   TYPE_DIRECT);
		    dirty = 1;
                }
            }
        }
#endif
	if (i && ((reiserfs_ih_stat (ih - 1) && !reiserfs_ih_stat (ih) &&
		   (reiserfs_key_get_did (&(ih - 1)->ih_key) != 
		    reiserfs_key_get_did (&ih->ih_key))) ||
		  (reiserfs_ih_stat (ih) && 
		   (reiserfs_key_get_did (&(ih - 1)->ih_key) >
		    reiserfs_key_get_did (&ih->ih_key)))))
        {
            /* not the same dir_id of the same file or not increasing dir_id 
	       of different files */
            if ((i > 1) && reiserfs_key_get_did (&(ih - 2)->ih_key) == 
		reiserfs_key_get_did (&(ih - 1)->ih_key)) 
	    {
                /* fix i-th */
                if (!reiserfs_ih_stat (ih)) {
                    fsck_log("pass0: vpf-10430: block %lu, item %d: Wrong "
			     "order of items - change the dir_id of the key "
			     "%k to %lu\n", bh->b_blocknr, i, &ih->ih_key, 
			     reiserfs_key_get_did (&(ih - 1)->ih_key));
		    
		    reiserfs_key_set_did (&ih->ih_key, 
				   reiserfs_key_get_did (&(ih - 1)->ih_key));
		    
		    dirty = 1;
                } else if (i + 1 < nr_items) {
                    fsck_log("pass0: vpf-10440: block %lu, item %d: Wrong "
			     "order of items - change the dir_id of the key "
			     "%k to %lu\n", bh->b_blocknr, i, &ih->ih_key, 
			     reiserfs_key_get_did (&(ih + 1)->ih_key));
		    
		    reiserfs_key_set_did (&ih->ih_key, 
			    reiserfs_key_get_did (&(ih + 1)->ih_key));
		    dirty = 1;
                }
            } else if ((i + 1 < nr_items) && 
		       reiserfs_key_get_did (&ih->ih_key) == 
		       reiserfs_key_get_did (&(ih + 1)->ih_key)) 
	    {
                fsck_log("pass0: vpf-10450: block %lu, item %d: Wrong order of "
			 "items - change the dir_id of the key %k to %lu\n",
			 bh->b_blocknr, i - 1, &(ih - 1)->ih_key, 
			 reiserfs_key_get_did (&ih->ih_key));
		
                /* fix (i - 1)-th */
		reiserfs_key_set_did (&(ih - 1)->ih_key, 
				      reiserfs_key_get_did (&ih->ih_key));
		dirty = 1;
            }	
        }

 	if (i && i + 1 < nr_items) {
      	    /* there is a previous and a next items */
	    if ((reiserfs_key_get_did (&(ih - 1)->ih_key) == 
		 reiserfs_key_get_did (&(ih + 1)->ih_key)) &&
		(reiserfs_key_get_did (&(ih - 1)->ih_key) != 
		 reiserfs_key_get_did (&ih->ih_key)))
	    {
                fsck_log("pass0: vpf-10460: block %lu, item %d: Wrong order of "
			 "items - change the dir_id of the key %k to %lu\n",
			 bh->b_blocknr, i, &ih->ih_key, 
			 reiserfs_key_get_did (&(ih - 1)->ih_key));
		
		reiserfs_key_set_did (&ih->ih_key, reiserfs_key_get_did (&(ih - 1)->ih_key));
		dirty = 1;
            }

	    if ((reiserfs_key_get_oid (&(ih - 1)->ih_key) == 
		 reiserfs_key_get_oid (&(ih + 1)->ih_key)) &&
		(reiserfs_key_get_oid (&(ih - 1)->ih_key) != 
		 reiserfs_key_get_oid (&ih->ih_key)))
	    {
		fsck_log("pass0: vpf-10470: block %lu, item %d: Wrong "
			 "order of items - change the object_id of the "
			 "key %k to %lu\n",	bh->b_blocknr, i, &ih->ih_key,
			 reiserfs_key_get_oid (&(ih - 1)->ih_key));

		reiserfs_key_set_oid (&ih->ih_key, 
				      reiserfs_key_get_oid (&(ih - 1)->ih_key));
		dirty = 1;
	    }

            if (reiserfs_ih_stat (ih - 1) && 
		(reiserfs_ih_ext (ih) && reiserfs_ih_direct (ih + 1)))
	    {
		
	    	if ((reiserfs_key_get_oid (&(ih - 1)->ih_key) == 
		     reiserfs_key_get_oid (&ih->ih_key)) &&
		    (reiserfs_key_get_oid (&(ih - 1)->ih_key) != 
		     reiserfs_key_get_oid (&(ih + 1)->ih_key)))
            	{
		    fsck_log("pass0: vpf-10480: block %lu, item %d: Wrong "
			     "order of items - change the object_id of the "
			     "key %k to %lu\n", bh->b_blocknr, i + 1, 
			     &(ih+1)->ih_key, 
			     reiserfs_key_get_oid (&(ih - 1)->ih_key));
		    
		    reiserfs_key_set_oid (&(ih + 1)->ih_key, 
				      reiserfs_key_get_oid (&(ih - 1)->ih_key));
		    dirty = 1;
		}
		
	    	if ((reiserfs_key_get_oid (&ih->ih_key) == 
		     reiserfs_key_get_oid (&(ih + 1)->ih_key)) &&
		    (reiserfs_key_get_oid (&(ih - 1)->ih_key) != 
		     reiserfs_key_get_oid (&(ih + 1)->ih_key)))
            	{
		    fsck_log("pass0: vpf-10490: block %lu, item %d: Wrong "
			     "order of items - change the object_id of the "
			     "key %k to %lu\n", bh->b_blocknr, i - 1, 
			     &(ih-1)->ih_key, 
			     reiserfs_key_get_oid (&(ih + 1)->ih_key));
		    
		    reiserfs_key_set_oid (&(ih - 1)->ih_key, 
				      reiserfs_key_get_oid (&(ih + 1)->ih_key));
		    dirty = 1;
		}
		
		if ((reiserfs_key_get_did (&(ih - 1)->ih_key) == 
		     reiserfs_key_get_did (&ih->ih_key)) &&
		    (reiserfs_key_get_did (&(ih - 1)->ih_key) != 
		     reiserfs_key_get_did (&(ih + 1)->ih_key)))
	    	{
		    fsck_log("pass0: vpf-10500: block %lu, item %d: Wrong "
			     "order of items - change the dir_id of the key "
			     "%k to %lu\n", bh->b_blocknr, i + 1, 
			     &(ih+1)->ih_key,
			     reiserfs_key_get_did (&(ih - 1)->ih_key));
		    reiserfs_key_set_did (&(ih + 1)->ih_key, 
				   reiserfs_key_get_did (&(ih - 1)->ih_key));
		    dirty = 1;
		}
		
		if ((reiserfs_key_get_did (&ih->ih_key) == 
		     reiserfs_key_get_did (&(ih + 1)->ih_key)) &&
		    (reiserfs_key_get_did (&(ih - 1)->ih_key) != 
		     reiserfs_key_get_did (&(ih + 1)->ih_key)))
	    	{
		    fsck_log("pass0: vpf-10510: block %lu, item %d: Wrong "
			     "order of items - change the dir_id of the key %k "
			     "to %lu\n", bh->b_blocknr, i - 1, &(ih-1)->ih_key, 
			     reiserfs_key_get_did (&(ih + 1)->ih_key));
		    
		    reiserfs_key_set_did (&(ih - 1)->ih_key, 
				   reiserfs_key_get_did (&(ih + 1)->ih_key));
		    
		    dirty = 1;
		}
            }
     	}

	if (i && reiserfs_ih_stat (ih) &&
	    reiserfs_key_get_did (&(ih - 1)->ih_key) == 
	    reiserfs_key_get_did (&ih->ih_key) &&
	    reiserfs_key_get_oid (&(ih - 1)->ih_key) >= 
	    reiserfs_key_get_oid (&ih->ih_key)) 
	{
	    if ((i + 1 < nr_items) && !reiserfs_ih_stat (ih + 1)) {
         	if (reiserfs_key_get_oid (&(ih - 1)->ih_key) < 
		    reiserfs_key_get_oid (&(ih + 1)->ih_key)) 
		{
		    fsck_log("pass0: vpf-10520: block %lu, item %d: Wrong "
			     "order of items - change the object_id of the key "
			     "%k to %lu\n", bh->b_blocknr, i - 1, 
			     &(ih-1)->ih_key, 
			     reiserfs_key_get_oid (&(ih + 1)->ih_key));
		    
		    reiserfs_key_set_oid (&ih->ih_key, 
				      reiserfs_key_get_oid (&(ih + 1)->ih_key));
		    
		    dirty = 1;
		}
	    }
	}

        if (i && reiserfs_ih_stat (ih - 1) && 
	    !reiserfs_ih_stat (ih) &&
	    (reiserfs_key_get_oid (&(ih - 1)->ih_key) != 
	     reiserfs_key_get_oid (&ih->ih_key)))
        {
            int err = 0;
            if (i > 1) {
                if (reiserfs_key_comp2 (&(ih - 2)->ih_key, 
					&(ih - 1)->ih_key) != -1)
		{
                    misc_set_bit (1, &err);
		}
		
                if (reiserfs_key_comp2 (&(ih - 2)->ih_key, 
					&ih->ih_key) != -1)
		{
                    misc_set_bit (2, &err);
		}
            }
            if (i + 1 < nr_items) {
                if (reiserfs_key_comp2 (&(ih - 1)->ih_key, 
					&(ih + 1)->ih_key) != -1)
		{
                    misc_set_bit (3, &err);
		}
		
                if (reiserfs_key_comp2 (&ih->ih_key, 
					&(ih + 1)->ih_key) != -1)
		{
                    misc_set_bit (4, &err);
		}
            }
/*
            if ((test_bit (1, err) || test_bit (3, err)) &&
		(test_bit (2, err) || test_bit (4, err))) {
      		// thera are no problem-free keys, delete them both
                reiserfs_leaf_delete_item (fs, bh, i - 1);
                reiserfs_leaf_delete_item (fs, bh, i - 1);
		pass_0_stat(fs)->removed += 2;
                goto start_again;
      	    }
*/
            if (!misc_test_bit (1, &err) && !misc_test_bit (3, &err) &&
		!misc_test_bit (2, &err) && !misc_test_bit (4, &err)) 
	    {
      		if (i <= 1) {
            	    /* take bigger */
                    if (reiserfs_key_comp2 (&(ih - 1)->ih_key, 
					    &ih->ih_key) == 1) 
		    {
			fsck_log("pass0: vpf-10530: block %lu, item %d: Wrong "
				 "order of items - change the object_id of the "
				 "key %k to %lu\n", 
				 bh->b_blocknr, i, &ih->ih_key, 
				 reiserfs_key_get_oid (&(ih - 1)->ih_key));
			
			reiserfs_key_set_oid (&ih->ih_key, 
					  reiserfs_key_get_oid (&(ih - 1)->ih_key));
                    } else {
			fsck_log("pass0: vpf-10540: block %lu, item %d: Wrong "
				 "order of items - change the object_id of the "
				 "key %k to %lu\n", bh->b_blocknr, i - 1, 
				 &(ih - 1)->ih_key, 
				 reiserfs_key_get_oid (&ih->ih_key));
			
                        reiserfs_key_set_oid (&(ih - 1)->ih_key, 
					  reiserfs_key_get_oid (&ih->ih_key));
		    }
            	} else {
                    /* take smaller */
                    if (reiserfs_key_comp2 (&(ih - 1)->ih_key, 
					    &ih->ih_key) == 1)
		    {
			fsck_log("pass0: vpf-10550: block %lu, item %d: Wrong "
				 "order of items - change the object_id of the "
				 "key %k to %lu\n", 
				 bh->b_blocknr, i - 1, &(ih - 1)->ih_key, 
				 reiserfs_key_get_oid (&ih->ih_key));
			
                        reiserfs_key_set_oid (&(ih - 1)->ih_key, 
					  reiserfs_key_get_oid (&ih->ih_key));
                    } else {
			fsck_log("pass0: vpf-10560: block %lu, item %d: Wrong "
				 "order of items - change the object_id of the "
				 "key %k to %lu\n", 
				 bh->b_blocknr, i, &ih->ih_key, 
				 reiserfs_key_get_oid (&(ih - 1)->ih_key));
			
			reiserfs_key_set_oid (&ih->ih_key, 
					  reiserfs_key_get_oid (&(ih - 1)->ih_key));
		    }
                }
		
		dirty = 1;
	    } else if (!misc_test_bit (1, &err) && !misc_test_bit (3, &err)) {
      		/* take i - 1 */
		fsck_log("pass0: vpf-10590: block %lu, item %d: Wrong order "
			 "of items - change the object_id of the key %k to "
			 "%lu\n", bh->b_blocknr, i, &ih->ih_key, 
			 reiserfs_key_get_oid (&(ih - 1)->ih_key));
		
		reiserfs_key_set_oid (&ih->ih_key, 
				  reiserfs_key_get_oid (&(ih - 1)->ih_key));
		
		dirty = 1;
 	    } else if (!misc_test_bit (2, &err) && !misc_test_bit (4, &err)) {
      		/* take i */
		fsck_log("pass0: vpf-10600: block %lu, item %d: Wrong order "
			 "of items - change the object_id of the key %k to "
			 "%lu\n", bh->b_blocknr, i - 1, &(ih - 1)->ih_key, 
			 reiserfs_key_get_oid (&ih->ih_key));
		
                reiserfs_key_set_oid (&(ih - 1)->ih_key, 
				  reiserfs_key_get_oid (&ih->ih_key));
		
		dirty = 1;
	    }
        }

	/* this recovers corruptions like the below: 
	   1774 1732 0 0
	   116262638 1732 1 3
	   1774 1736 0 0 */
	if (i && reiserfs_ih_stat (ih - 1) && !reiserfs_ih_stat (ih)) {
	    if (reiserfs_key_get_oid (&ih->ih_key) != 
		reiserfs_key_get_oid (&(ih - 1)->ih_key) ||
		reiserfs_key_get_did (&ih->ih_key) != 
		reiserfs_key_get_did (&(ih - 1)->ih_key)) 
	    {
		fsck_log ("pass0: vpf-10170: block %lu: item %d: Wrong "
			  "order of items - the item \n\t%H fixed to ", 
			  bh->b_blocknr, i, ih);
		
		reiserfs_key_set_did (&ih->ih_key, 
				      reiserfs_key_get_did (&(ih - 1)->ih_key));
		reiserfs_key_set_oid (&ih->ih_key, 
				      reiserfs_key_get_oid (&(ih - 1)->ih_key));

		dirty = 1;
		fsck_log ("\n\t%H\n", ih);
	    }
#if 0
	    if (!reiserfs_ih_dir (ih) && 
		reiserfs_key_get_off (&ih->ih_key) != 1)
	    {
		fsck_log ("pass0: vpf-10170: block %lu: item %d: Wrong "
			  "offset of the item\n\t%H which follows a StatDtat "
			  "item, fixed to 1", bh->b_blocknr, i, ih);
		
		if (reiserfs_ih_get_len (ih - 1) == REISERFS_SD_SIZE) {
		    /* stat data is new, therefore this item is new too */
		    reiserfs_key_set_off (KEY_FORMAT_2, &(ih->ih_key), 1);
		    if ((reiserfs_ih_get_entries (ih) != 0xffff) && 
			(reiserfs_ih_get_len (ih) % 4 == 0))
		    {
			reiserfs_key_set_type (KEY_FORMAT_2, 
					       &(ih->ih_key), 
					       TYPE_EXTENT);
		    } else {
			reiserfs_key_set_type (KEY_FORMAT_2, 
					       &(ih->ih_key), 
					       TYPE_DIRECT);
		    }

		    reiserfs_ih_set_format (ih, KEY_FORMAT_2);
		} else {
		    /* stat data is old, therefore this item is old too */
		    reiserfs_key_set_off(KEY_FORMAT_1, &(ih->ih_key), 1);
		    if ((reiserfs_ih_get_entries (ih) != 0xffff) && 
			(reiserfs_ih_get_len (ih) % 4 == 0))
		    {
			reiserfs_key_set_type (KEY_FORMAT_1, 
					       &(ih->ih_key), 
					       TYPE_EXTENT);
		    } else {
			reiserfs_key_set_type (KEY_FORMAT_1, 
					       &(ih->ih_key), 
					       TYPE_DIRECT);
			reiserfs_ih_set_free (ih, 0xffff);
		    }
		    reiserfs_ih_set_format (ih, KEY_FORMAT_1);
		}
	    }
#endif
	}

	/* FIXME: corruptions like:
	   56702 66802 1 2
	   56702 65536 0 0
	   56702 66803 1 2
	   do not get recovered (both last items will be deleted) */
	
	/* delete item if it is not in correct order of object items */
	if (i && reiserfs_key_comp2 (&ih->ih_key, &(ih - 1)->ih_key) &&
	    !reiserfs_ih_stat (ih)) {
	    fsck_log ("pass0: vpf-10180: block %lu: item %d: The item %k, "
		      "which follows non StatData item %k, is deleted\n",
		      bh->b_blocknr, i, &ih->ih_key, &(ih - 1)->ih_key);
	    reiserfs_leaf_delete_item (fs, bh, i);
	    pass_0_stat(fs)->removed++;
	    goto start_again;
	}
	
	if (reiserfs_ih_stat (ih)) {
            if (reiserfs_key_get_off (&ih->ih_key) != 0) {
                reiserfs_key_set_off(KEY_FORMAT_1, &ih->ih_key, 0);
                dirty = 1;
            }
	} else if (!reiserfs_ih_dir (ih)) {
	    int format;

	    format = reiserfs_key_format(&ih->ih_key);
	    /* not SD, not direntry */
            if (i && reiserfs_key_comp2 (&(ih - 1)->ih_key, 
					 &ih->ih_key) == 0) 
	    {
                if (reiserfs_ih_stat (ih - 1)) {
        	    if (reiserfs_key_get_off (&ih->ih_key) != 1) {
                        reiserfs_key_set_off(format, &ih->ih_key, 1);
                        dirty = 1;
                    }
                } else if (!reiserfs_ih_dir (ih - 1)) {
		    /* Both @(ih-1) and @ih are not SD not DIR items. */
        	    if (reiserfs_key_get_off (&ih->ih_key) < 
        	        reiserfs_key_get_off(&(ih - 1)->ih_key) + 
			reiserfs_leaf_ibytes(ih - 1, fs->fs_blocksize))
        	    {
                        fsck_log("pass0: vpf-10250: block %lu, item %d: The "
				 "item %k with wrong offset, fixed to %llu\n",
				 bh->b_blocknr, i, &ih->ih_key,
				 reiserfs_key_get_off(&(ih - 1)->ih_key) +
				 reiserfs_ext_count (ih - 1) * fs->fs_blocksize);

                        reiserfs_key_set_off(
				format, &ih->ih_key,
				reiserfs_key_get_off(&(ih - 1)->ih_key) +
				reiserfs_ext_count (ih - 1) * fs->fs_blocksize);
                        dirty = 1;
        	    }
                } else {
                    /* if extent item or not the first direct item in leaf */
                    fsck_log("pass0: vpf-10250: block %lu, item %d: The item "
			     "%k with wrong type is deleted\n", bh->b_blocknr, 
			     i, &ih->ih_key);
		    
                    reiserfs_leaf_delete_item (fs, bh, i);
		    pass_0_stat(fs)->removed++;
                    goto start_again;
                }
	    } else {
		/*first item in the node or first item of the file */
  		if (i) {
        	    /* first item of the file, but not SD - delete */
		    fsck_log("pass0: vpf-10190: block %lu, item %d: The item "
			     "%k, which follows non StatData item %k, is "
			     "deleted\n", bh->b_blocknr, i, &ih->ih_key, 
			     &(ih - 1)->ih_key );
		    
		    reiserfs_leaf_delete_item (fs, bh, i);
		    pass_0_stat(fs)->removed++;
		    goto start_again;
        	}
	    }

	    /* extent or direct is the first in the leaf */
	    offset = (__u64)reiserfs_key_get_off (&ih->ih_key);
	    if (reiserfs_ih_ext (ih)) {
		if (offset % fs->fs_blocksize != 1) {
		    fsck_log("pass0: vpf-10200: block %lu, item %d: The "
			     "item %k with wrong offset is deleted\n",
			     bh->b_blocknr, i, &ih->ih_key);
			
		    reiserfs_leaf_delete_item (fs, bh, i);
		    pass_0_stat(fs)->removed++;
		    goto start_again;
		}
	    }
	    
	    /* Check the lenght of the direct item; offset should 
	       be ok already. */
	    if (reiserfs_ih_direct (ih) && 
		!correct_direct_item_offset(ih, fs)) 
	    {
		fsck_log("pass0: vpf-10700: block %lu, item %d: "
			 "The item with wrong offset or length "
			 "found %k, len % lu - deleted\n", 
			 bh->b_blocknr, i, &ih->ih_key, 
			 reiserfs_ih_get_len (ih));

		reiserfs_leaf_delete_item (fs, bh, i);
		pass_0_stat(fs)->removed++;
		goto start_again;
	    }

	    offset += reiserfs_leaf_ibytes (ih, fs->fs_blocksize);
	    if (!does_it_fit_into_dev (offset, fs_size) || 
		(format == KEY_FORMAT_1 && offset >= (1ull << 32)) ||
		(format == KEY_FORMAT_2 && offset >= (1ull << 60)))
	    {
		fsck_log("pass0: vpf-10230: block %lu, item %d: The item "
			 "offset is is too big %k - deleted\n",
			 bh->b_blocknr, i, &ih->ih_key);
		reiserfs_leaf_delete_item (fs, bh, i);
		pass_0_stat(fs)->removed++;
		goto start_again;
	    }
        }
        
	if (i &&  reiserfs_key_comp (&(ih - 1)->ih_key, &ih->ih_key) != -1) {
	    /* previous item has key not smaller than the key of currect item */
	    if (reiserfs_ih_stat (ih - 1) && !reiserfs_ih_stat (ih)) {
		/* fix stat data key such as if it was stat data of that item */
		fsck_log ("pass0: block %lu, items %d, %d: Wrong order of "
			  "items - make the StatData item %k of the file %k\n",
		    bh->b_blocknr, i - 1, i, &(ih - 1)->ih_key, &ih->ih_key);
		
		reiserfs_key_set_did (&(ih - 1)->ih_key, 
				      reiserfs_key_get_did (&ih->ih_key));
		
		reiserfs_key_set_oid (&(ih - 1)->ih_key, 
				      reiserfs_key_get_oid (&ih->ih_key));
		
		reiserfs_key_set_off (KEY_FORMAT_1, &(ih - 1)->ih_key, 0);
		
		reiserfs_key_set_type (KEY_FORMAT_1, 
				       &(ih - 1)->ih_key, 
				       TYPE_STAT_DATA);
		dirty = 1;
	    } else {
		/* ok, we have to delete one of these two - decide which one */
		int retval;

		/* something will be deleted */
		dirty = 1;
		retval = upper_correct (bh, ih - 1, i - 1);
		switch (retval) {
		case 0:
		    /* delete upper item */
		    fsck_log ("pass0: block %lu, item %d (upper): Item %k is "
			      "out of order - deleted\n", bh->b_blocknr, i - 1,
			      &(ih - 1)->ih_key);
		    
		    reiserfs_leaf_delete_item (fs, bh, i - 1);
		    pass_0_stat(fs)->removed++;
		    goto start_again;

		case 1:
		    /* delete lower item */
		    fsck_log ("pass0: block %lu, item %d (lower): Item %k is "
			      "out of order - deleted\n", bh->b_blocknr, i, 
			      &ih->ih_key);
		    
		    reiserfs_leaf_delete_item (fs, bh, i);
		    pass_0_stat(fs)->removed++;
		    goto start_again;

		default:
		    /* upper item was the first item of a node */
		    /* to make gcc 3.2 do not sware here */;
		}

		retval = lower_correct (bh, ih, i);
		switch (retval) {
		case 0:
		    /* delete lower item */
		    fsck_log ("pass0: block %lu, item %d (lower): Item %k is "
			      "out of order - deleted\n", bh->b_blocknr, i, 
			      &ih->ih_key);
		    
		    reiserfs_leaf_delete_item (fs, bh, i);
		    pass_0_stat(fs)->removed++;
		    goto start_again;

		case 1:
		    /* delete upper item */
		    fsck_log ("pass0: block %lu, %d (upper): Item %k is "
			      "out of order - deleted\n", bh->b_blocknr, 
			      i - 1, &(ih - 1)->ih_key);
		    
		    reiserfs_leaf_delete_item (fs, bh, i - 1);
		    pass_0_stat(fs)->removed++;
		    goto start_again;

		default:
		    /* only 2 items in the node, how to decide what to delete,
		       go and ask user */
		    /* to make gcc 3.2 do not sware here */;
		}
		
		fsck_log ("pass0: block %lu, items %d and %d: Which of these "
			  "items looks better (the other will be deleted.)?\n"
			  "%k\n%k\n", bh->b_blocknr, i-1, i, &(ih - 1)->ih_key,
			  &ih->ih_key);
		
		if (fsck_info_ask (fs, "1 or 2?", "1\n", 1)) {
		    reiserfs_leaf_delete_item (fs, bh, i - 1);
		} else {
		    reiserfs_leaf_delete_item (fs, bh, i);
		}

		pass_0_stat(fs)->removed++;
		goto start_again;
	    }
	}

	if (reiserfs_ih_stat (ih) && 
	    (reiserfs_ih_get_len (ih) != REISERFS_SD_SIZE &&
	     reiserfs_ih_get_len (ih) != REISERFS_SD_SIZE_V1))
	{
	    fsck_log ("pass0: block %lu, item %d: StatData item of wrong "
		      "length found %H - deleted\n", bh->b_blocknr, i, ih);
	    reiserfs_leaf_delete_item (fs, bh, i);
	    pass_0_stat(fs)->removed++;
	    goto start_again;
	}

	dirty += correct_key_format (ih, symlnk);
	
	if (reiserfs_ih_stat (ih)) {
	    __u16 mode;

            file_format = reiserfs_ih_get_format (ih);
	
	    reiserfs_stat_get_mode (ih, reiserfs_item_at(bh,i), &mode);
            symlnk = ( S_ISLNK(mode) ? 1 : 0);
	    ;/*correct_stat_data (fs, bh, i);*/
        } else if ( !reiserfs_ih_dir(ih) && !symlnk &&
        	    (file_format != KEY_FORMAT_UNDEFINED) &&
        	    (file_format != reiserfs_ih_get_format (ih)))
        {
            fsck_log("pass0: vpf-10240: block %lu, item (%d): Item %k, which "
		     "format (%d) is not equal to StatData format (%d), is "
		     "deleted\n", bh->b_blocknr, i, &ih->ih_key, 
		     reiserfs_ih_get_format(ih), file_format);
	    
            reiserfs_leaf_delete_item (fs, bh, i);
	    pass_0_stat(fs)->removed++;
            goto start_again;
        } else {
	    file_format = KEY_FORMAT_UNDEFINED;
	    symlnk = 0;
	}


	if (i && reiserfs_ih_stat (ih - 1) && 
	    !reiserfs_key_comp2 (&ih->ih_key, &(ih - 1)->ih_key)) 
	{
	    __u16 mode;

	    reiserfs_stat_get_mode (ih - 1, 
				    reiserfs_item_by_ih (bh, ih - 1), 
				    &mode);
	    
	    if (not_a_directory (reiserfs_item_by_ih (bh, ih - 1)) && 
		reiserfs_ih_dir (ih)) 
	    {
		/* make SD mode SD of dir */
		fsck_log ("pass0: block %lu, item %d: Not the directory %K has "
			  "the wrong mode (%M), corrected to ",
			  bh->b_blocknr, i, &ih->ih_key, mode);
		mode &= ~S_IFMT;
		mode |= S_IFDIR;
		fsck_log ("(%M)\n", mode);
		reiserfs_stat_set_mode (ih - 1, 
					reiserfs_item_by_ih (bh, ih - 1), 
					&mode);
		dirty = 1;
	    } else if (!not_a_directory (reiserfs_item_by_ih (bh, ih - 1)) && 
		       !reiserfs_ih_dir (ih)) 
	    {
		/* make SD mode SD of regular file */
		fsck_log ("pass0: block %lu, item %d: the directory %K "
			  "has the wrong mode (%M), corrected to ",
			  bh->b_blocknr, i, &ih->ih_key, mode);
		mode &= ~S_IFMT;
		mode |= S_IFREG;
		fsck_log ("(%M)\n", mode);
		reiserfs_stat_set_mode (ih - 1, 
					reiserfs_item_by_ih (bh, ih - 1), 
					&mode);
		dirty = 1;
	    }
#if 0
	    if (not_a_regfile (reiserfs_item_by_ih (bh, ih - 1)) && 
		reiserfs_ih_ext (ih)) 
	    {
		fsck_log ("pass0: block %lu, item %d: The file %K has the "
			  "wrong mode (%M), corrected to ", bh->b_blocknr, i,
			  &ih->ih_key, mode);
		mode &= ~S_IFMT;
		mode |= S_IFREG;
		fsck_log ("(%M)\n", mode);
		reiserfs_stat_set_mode (ih - 1, 
					reiserfs_item_by_ih (bh, ih - 1), 
					&mode);
		dirty = 1;
	    }
#endif
	}

	if (reiserfs_ih_dir (ih)) {
	    j = verify_directory_item (fs, bh, i);
	
	    if (j == 1) {
	        /* wrong hash, skip the leaf */
		pass_0_stat (fs)->too_old_leaves ++;
		reiserfs_buffer_mkclean (bh);
		return;
	    } else if (j == -1) {
		/* item was deleted */
		goto start_again;
	    }
	    continue;
	}

	/*DEBUG*/
        if (!reiserfs_ih_stat (ih) && 
	    reiserfs_key_get_off (&ih->ih_key) == 0)
	{
            reiserfs_panic ("block %lu, item %d: Zero offset can have "
			    "StatData items only, but found %k\n", 
			    bh->b_blocknr, i, &ih->ih_key);
	}

	if (!reiserfs_ih_ext (ih))
	    continue;
	
	ind_item = (__u32 *)reiserfs_item_by_ih (bh, ih);
	for (j = 0; j < (int)reiserfs_ext_count (ih); j ++) {
	    unfm_ptr = d32_get (ind_item, j);
	    if (!unfm_ptr)
		continue;

	    if ((reiserfs_fs_block(fs, unfm_ptr) != BT_UNKNOWN) ||
		(fs->fs_badblocks_bm && 
		 reiserfs_bitmap_test_bit(fs->fs_badblocks_bm, unfm_ptr))) 
	    {
		pass_0_stat (fs)->wrong_pointers ++;
		/*
		fsck_log ("pass0: %d-th pointer (%lu) in item %k (leaf "
			  "block %lu) is wrong\n", j, unfm_ptr, &ih->ih_key, 
			  bh->b_blocknr);
		*/
		d32_put(ind_item, j, 0);
		dirty = 1;
		continue;
	    }
	    /* mark block in bitmaps of unformatted nodes */
	    register_unfm (unfm_ptr);
	}
    }

    /* mark all objectids in use */
    ih = reiserfs_ih_at (bh, 0);
    for (i = 0; i < reiserfs_nh_get_items (NODE_HEAD (bh)); i ++, ih ++) {
	reiserfs_deh_t * deh;

	id_map_mark(proper_id_map (fs), reiserfs_key_get_did (&ih->ih_key));
	id_map_mark(proper_id_map (fs), reiserfs_key_get_oid (&ih->ih_key));
	if (reiserfs_ih_dir(ih)) {
	    for (j = 0, deh = reiserfs_deh (bh, ih); 
		 j < reiserfs_ih_get_entries (ih); 
		 j ++, deh++) 
	    {
		id_map_mark(proper_id_map(fs), reiserfs_deh_get_obid(deh));
	    }
	}
    }

    if (reiserfs_nh_get_items (NODE_HEAD (bh)) < 1) {
	/* pass 1 will skip this */
	pass_0_stat (fs)->all_contents_removed ++;
	fsck_log ("pass0: block %lu: no correct item is found. "
		  "Leave block untouched.\n", bh->b_blocknr);
	dirty = 0;
	reiserfs_buffer_mkclean (bh);
    } else {
	/* pass1 will use this bitmap */
	pass0_block_mkleaf (bh->b_blocknr);
	/*fsck_data (fs)->rebuild.leaves ++;*/
    }
    
    if (dirty) {
	pass_0_stat (fs)->leaves_corrected ++;
	reiserfs_buffer_mkdirty (bh);
    }
}


static int is_to_be_read (reiserfs_filsys_t * fs, unsigned long block) {
    return reiserfs_bitmap_test_bit (fsck_source_bitmap (fs), block);
}

static void do_pass_0 (reiserfs_filsys_t * fs) {
    reiserfs_bh_t * bh;
    unsigned long i;
    int what_node;
    unsigned long done = 0, total;


    if (fsck_mode (fs) == DO_TEST) {
	/* just to test pass0_correct_leaf */
	bh = reiserfs_buffer_read (fs->fs_dev, 
				   fsck_data (fs)->rebuild.test, 
				   fs->fs_blocksize);

	if (!bh) {
	    fsck_progress ("%s: Reading of the block %lu failed\n",
			   __FUNCTION__, fsck_data (fs)->rebuild.test);
	    reiserfs_fs_free (fs);
	    exit(0);
	}

	if (fsck_leaf_check(bh))
	    fsck_progress ("###############  bad #################\n");

	pass0_correct_leaf (fs, bh);
	reiserfs_node_print (stdout, fs, bh, 3, -1, -1);

	if (fsck_leaf_check(bh))
	    fsck_progress ("############### still bad #################\n");
	
	reiserfs_buffer_close (bh);
	reiserfs_fs_free (fs);
	exit(0);
    }

    total = reiserfs_bitmap_ones (fsck_source_bitmap (fs));
    for (i = 0; i < reiserfs_sb_get_blocks (fs->fs_ondisk_sb); i ++) {
	if (!is_to_be_read (fs, i))
	    continue;

	if (!fsck_quiet(fs)) {
	    util_misc_progress (fsck_progress_file (fs), 
				&done, total, 1, 0);
	}

	if (!(bh = reiserfs_buffer_read (fs->fs_dev, i, fs->fs_blocksize))) {
	    fsck_progress ("%s: Reading of the block %lu failed\n", 
			   __FUNCTION__, i);
	    continue;
	}

	if (fs->fs_badblocks_bm && 
	    reiserfs_bitmap_test_bit(fs->fs_badblocks_bm, i))
	{
	    reiserfs_panic ("The block (%lu), specified in badblock "
			    "list, was read.", i);
	}
	
	if (reiserfs_fs_block(fs, i) != BT_UNKNOWN) {
	    /* block which could not be pointed by extent item */
	    if (!(reiserfs_journal_block (fs, i) && 
		  fsck_data(fs)->rebuild.use_journal_area))
	    {
		reiserfs_panic ("The block (%lu) from non "
				"data area was read.", i);
	    }
	}

	pass_0_stat (fs)->dealt_with ++;
	what_node = reiserfs_node_type (bh);
	if ( what_node != NT_LEAF && what_node != NT_IH_ARRAY ) {
	    reiserfs_buffer_close (bh);
	    continue;
	}
	
	pass_0_stat (fs)->leaves ++;
	pass0_correct_leaf (fs, bh);
	reiserfs_buffer_close (bh);
    }

    if (!fsck_quiet(fs))
	fsck_progress ("\n");


    /* just in case */
    id_map_mark(proper_id_map (fs), REISERFS_ROOT_OBJECTID);

}

static void choose_hash_function (reiserfs_filsys_t * fs) {
    unsigned long max;
    unsigned int hash_code;
    int i;

    if (fsck_hash_defined (fs))
	return;

    max = 0;
    hash_code = reiserfs_hash_code (0);
    
    for (i = 0; i < fsck_data (fs)->rebuild.hash_amount; i ++) {
	/* remember hash whihc got more hits */
	if (fsck_data (fs)->rebuild.hash_hits [i] > max) {
	    hash_code = i;
	    max = fsck_data (fs)->rebuild.hash_hits [i];
	}

	if (fsck_data (fs)->rebuild.hash_hits [i]) {
	    fsck_log ("%lu directory entries were hashed with %s hash.\n",
		      fsck_data (fs)->rebuild.hash_hits [i], 
		      reiserfs_hash_name(i));
	}
    }

    if (max == 0 || hash_code == 0) {
	/* no names were found. take either super block value or default */
        hash_code = reiserfs_sb_get_hash (fs->fs_ondisk_sb);
	if (!hash_code)
	    hash_code = DEFAULT_HASH;
	
        fsck_log ("Could not find a hash in use. Using %s\n",
		  reiserfs_hash_name (hash_code));
    }
    
    /* compare the most appropriate hash with the hash set in super block */
    if (hash_code != reiserfs_sb_get_hash (fs->fs_ondisk_sb)) {
        fsck_progress ("Selected hash (%s) does not match to "
		       "the hash set in the super block (%s).\n", 
		       reiserfs_hash_name (hash_code), 
		       reiserfs_hash_name (
				reiserfs_sb_get_hash (fs->fs_ondisk_sb)));
	
        reiserfs_sb_set_hash (fs->fs_ondisk_sb, hash_code);
    }
    
    fs->hash = reiserfs_hash_func (hash_code);
}

/* create bitmap of blocks the tree is to be built off */
/* debugreiserfs and pass0 should share this code -s should show
the same as we could recover - test: zero first 32M */
static void init_source_bitmap (reiserfs_filsys_t * fs) {
    unsigned long count, block, bmap_nr, bits_amount;
    unsigned long i, j, tmp, reserved;
    FILE * fp;

    count = reiserfs_sb_get_blocks (fs->fs_ondisk_sb);

    switch (fsck_data (fs)->rebuild.scan_area) {
    case ALL_BLOCKS:
	fsck_source_bitmap (fs) = reiserfs_bitmap_create (count);
	reiserfs_bitmap_fill (fsck_source_bitmap (fs));
	fsck_progress ("The whole partition (%d blocks) is to be scanned\n", 
		       reiserfs_bitmap_ones (fsck_source_bitmap (fs)));	
	break;

    case USED_BLOCKS:
	fsck_progress ("Loading on-disk bitmap .. ");
	fsck_source_bitmap (fs) = reiserfs_bitmap_create (count);	
	reiserfs_bitmap_copy (fsck_source_bitmap (fs), fs->fs_bitmap2);
	
	fsck_progress ("ok, %d blocks marked used\n", 
		       reiserfs_bitmap_ones (fsck_source_bitmap (fs)));
	break;

    case EXTERN_BITMAP:
	fp = fopen (fsck_data (fs)->rebuild.bitmap_file_name, "r");
	
	if (!fp) {
	    reiserfs_exit (EXIT_OPER, "Could not load bitmap: %s\n", 
			   strerror(errno));
	}
	
	fsck_source_bitmap (fs) = reiserfs_bitmap_load (fp);
	
	if (!fsck_source_bitmap (fs)) {
	    reiserfs_exit (EXIT_OPER, "Could not load fitmap from \"%s\"", 
			   fsck_data (fs)->rebuild.bitmap_file_name);
	}

	fsck_progress ("%d blocks marked used in extern bitmap\n", 
		       reiserfs_bitmap_ones (fsck_source_bitmap (fs)));
	fclose (fp);
	break;

    default:
	reiserfs_panic ("No area to scan specified");
    }

    tmp = 0;

    /* unmark bitmaps */
    block = fs->fs_super_bh->b_blocknr + 1;
    reserved = fsck_source_bitmap(fs)->bm_bit_size;
    
    bmap_nr = reiserfs_bmap_nr(count, fs->fs_blocksize);
    
    for (i = 0; i < bmap_nr; i ++) {
	if (!reiserfs_bitmap_test_bit (fsck_source_bitmap (fs), block)) {
	    /* bitmap is definitely broken, mark all blocks of this 
	       bitmap block as used */
	    bits_amount = (reserved < fs->fs_blocksize * 8) ? 
		    reserved : fs->fs_blocksize * 8;
	    
	    fsck_log("%s: Bitmap %lu (of %lu bits) is wrong - "
		     "mark all blocks [%lu - %lu] as used\n",
		     __FUNCTION__, i, bits_amount, i * fs->fs_blocksize * 8,
		     fs->fs_blocksize * 8 * i + bits_amount); 	
				
	    for (j = i * fs->fs_blocksize * 8; 
		 j < i * fs->fs_blocksize * 8 + bits_amount; j++) 
	    {
	        if (!reiserfs_bitmap_test_bit (fsck_source_bitmap(fs), j))
		    reiserfs_bitmap_set_bit (fsck_source_bitmap(fs), j);
	    }
        }
        
	reiserfs_bitmap_clear_bit (fsck_source_bitmap (fs), block);
	reserved -= fs->fs_blocksize * 8;
	tmp ++;
	
	/* next block fo bitmap */
	if (reiserfs_bitmap_spread (fs)) {
	    block = (block / (fs->fs_blocksize * 8) + 1) * 
		    (fs->fs_blocksize * 8);
	} else {
	    block ++;
	}
    }

    /* pass 0 will skip super block and journal areas and bitmap blocks, find
       how many blocks have to be read */
    for (i = 0; i <= fs->fs_super_bh->b_blocknr; i ++) {
	if (!reiserfs_bitmap_test_bit (fsck_source_bitmap (fs), i))
	    continue;
	
	reiserfs_bitmap_clear_bit (fsck_source_bitmap (fs), i);
	tmp ++;
    }

    /* unmark journal area as used if journal is standard or it is non 
       standard and initialy has been created on a main device */

    reserved = reiserfs_journal_hostsize (fs->fs_ondisk_sb);
    /* where does journal area (or reserved journal area) start from */

    if (!reiserfs_new_location (fs->fs_super_bh->b_blocknr, 
				fs->fs_blocksize) &&
    	!reiserfs_old_location (fs->fs_super_bh->b_blocknr, 
				fs->fs_blocksize))
    {
	misc_die ("init_source_bitmap: Wrong super block location, "
		  "you must run --rebuild-sb.");
    }

    block = reiserfs_journal_start_must (fs);

    for (i = block; i < reserved + block; i ++) {
	if (!reiserfs_bitmap_test_bit (fsck_source_bitmap (fs), i))
	    continue;
	reiserfs_bitmap_clear_bit (fsck_source_bitmap (fs), i);
	tmp ++;	
    }

    if (fs->fs_badblocks_bm)
    	for (i = 0; i < count; i ++) {
	    if (reiserfs_bitmap_test_bit (fs->fs_badblocks_bm, i))
		reiserfs_bitmap_clear_bit (fsck_source_bitmap (fs), i);
    	}

    fsck_source_bitmap (fs)->bm_set_bits = 
	    reiserfs_bitmap_ones (fsck_source_bitmap (fs));

    fsck_progress ("Skipping %d blocks (super block, journal, "
		   "bitmaps) %d blocks will be read\n", tmp, 
		   fsck_source_bitmap (fs)->bm_set_bits);
		
}

static void fsck_pass0_prep (reiserfs_filsys_t * fs) {
    /* bitmap of blocks to be read */
    init_source_bitmap (fs);

    /* bitmap of leaves, good and bad unformatted */
    fsck_pass0_aux_prep (fs);

    /* on pass0 all objectids will be marked here as used */
    proper_id_map (fs) = id_map_init();

    /* pass0 gathers statistics about hash hits */
    hash_hits_init (fs);
}


static void fsck_pass0_save_result (reiserfs_filsys_t * fs) {
    FILE * file;
    int retval;

    /* save bitmaps with which we will be able start reiserfs from
       pass 1 */
    file = util_file_open ("temp_fsck_file.deleteme", "w+");
    if (!file)
	return;

    fsck_stage_start_put (file, PASS_0_DONE);
    reiserfs_bitmap_save (file,  leaves_bitmap);
    reiserfs_bitmap_save (file,  good_unfm_bitmap);
    reiserfs_bitmap_save (file,  bad_unfm_bitmap);
    fsck_stage_end_put (file);
    fclose (file);
    retval = rename ("temp_fsck_file.deleteme", state_dump_file (fs));
    
    if (retval != 0)
	fsck_progress ("%s: Could not rename the temporary file "
		       "temp_fsck_file.deleteme to %s",
		       __FUNCTION__, state_dump_file (fs));
}


/* file 'fp' must contain 3 bitmaps saved during last pass 0: bitmap
   of leaves, bitmaps of good and bad unfms*/
void fsck_pass0_load_result (FILE * fp, reiserfs_filsys_t * fs) {
    leaves_bitmap = reiserfs_bitmap_load (fp);
    good_unfm_bitmap = reiserfs_bitmap_load (fp);
    bad_unfm_bitmap = reiserfs_bitmap_load (fp);
    if (!leaves_bitmap || !good_unfm_bitmap || !bad_unfm_bitmap)
	fsck_exit ("State dump file seems corrupted. Run without -d");

    fsck_source_bitmap (fs) = leaves_bitmap;

    /* on pass 1 we do not need proper objectid map */

    fsck_progress ("Pass 0 result loaded. %d leaves, %d/%d good/bad data blocks\n",
		   reiserfs_bitmap_ones (leaves_bitmap),
		   reiserfs_bitmap_ones (good_unfm_bitmap),
		   reiserfs_bitmap_ones (bad_unfm_bitmap));
}


static void fsck_pass0_fini (reiserfs_filsys_t * fs) {
    time_t t;

    /* update super block: hash, objectid map, fsck state */
    choose_hash_function (fs);
    id_map_flush(proper_id_map (fs), fs);
    reiserfs_sb_set_state (fs->fs_ondisk_sb, PASS_0_DONE);
    reiserfs_buffer_mkdirty (fs->fs_super_bh);

    /* write all dirty blocks */
    fsck_progress ("Flushing..");
    fs->fs_dirt = 1;
    reiserfs_fs_flush (fs);
    fsck_progress ("finished\n");

    fsck_stage_report (FS_PASS0, fs);

    /* free what we do not need anymore */

    if (!fsck_run_one_step (fs)) {
	if (fsck_info_ask (fs, "Continue? (Yes):", "Yes\n", 1)) {
	    /* reiserfsck continues */
	    reiserfs_bitmap_delete (fsck_source_bitmap (fs));
	    fsck_source_bitmap (fs) = 0;
	    fsck_source_bitmap (fs) = leaves_bitmap;
	    return;
	}
    } else
	fsck_pass0_save_result (fs);

    reiserfs_bitmap_flush (fsck_source_bitmap (fs), fs);
    reiserfs_bitmap_delete (fsck_source_bitmap (fs));
    fsck_source_bitmap (fs) = 0;
    id_map_free(proper_id_map (fs));
    proper_id_map (fs) = 0;
    
    time (&t);
    fsck_progress ("###########\n"
		   "reiserfsck finished pass 0 at %s"
		   "###########\n", ctime (&t));
    fs->fs_dirt = 1;
    reiserfs_fs_close (fs);
    exit(0);
}


void fsck_pass0 (reiserfs_filsys_t * fs) {
    if (reiserfs_super_format (fs->fs_ondisk_sb) != fs->fs_format || 
        reiserfs_super_format (fs->fs_ondisk_sb) == REISERFS_FORMAT_UNKNOWN)
    {
	reiserfs_exit (EXIT_OPER, "pass 0: ReiserFS format version mismatch "
		       "found, you should run --rebuild-sb");
    }
  
    fsck_progress ("Pass 0:\n");
    if (fsck_log_file (fs) != stderr)
	/* this is just to separate warnings in the log file */
	fsck_log ("####### Pass 0 #######\n");


    fsck_pass0_prep (fs);

    /* scan the partition, find leaves and correct them */
    do_pass_0 (fs);

    fsck_pass0_fini (fs);
}

