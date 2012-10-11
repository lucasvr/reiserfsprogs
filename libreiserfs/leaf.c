/*
 * Copyright 1996-2004 by Hans Reiser, licensing governed by 
 * reiserfsprogs/README
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "reiserfs/libreiserfs.h"
#include "misc/bitops.h"

void reiserfs_leaf_mkempty (reiserfs_bh_t * bh)
{
    reiserfs_nh_set_items (NODE_HEAD (bh), 0);
    reiserfs_nh_set_free (NODE_HEAD (bh), REISERFS_NODE_SPACE (bh->b_size));
    reiserfs_nh_set_level (NODE_HEAD (bh), LEAF_LEVEL);
}

__u32 reiserfs_leaf_ibytes (const reiserfs_ih_t * ih, int blocksize) {
    switch (reiserfs_key_get_type (&ih->ih_key)) {
    case TYPE_DIRECT:
	return reiserfs_ih_get_len (ih);
    case TYPE_EXTENT:
	return reiserfs_ext_count(ih) * blocksize;// - reiserfs_ih_get_free (ih);
    case TYPE_STAT_DATA:
    case TYPE_DIRENTRY:
	return 0;
    }
    
    reiserfs_warning (stderr, "reiserfs_leaf_ibytes: called for wrong "
		      "type of item %h", ih);
    return 0;
}

int reiserfs_leaf_mergeable (reiserfs_ih_t * left, 
			     reiserfs_ih_t * right, 
			     int bsize)
{
    if (reiserfs_key_comp (&left->ih_key, &right->ih_key) != -1) {
	reiserfs_panic ("vs-16070: reiserfs_leaf_mergeable: left %k, "
			"right %k", &(left->ih_key), &(right->ih_key));
    }

    if (reiserfs_key_comp2 (&left->ih_key, &right->ih_key))
	return 0;
    
    if (reiserfs_key_get_type (&left->ih_key) != 
	reiserfs_key_get_type (&right->ih_key))
    {
	    return 0;
    }

    /* Directories are always mergeable. If fsck will need to handle dir items 
       separaely too, move it lower. */
    if (reiserfs_ih_dir (left))
	return 1;

    /* Merge only items with the same flags. */
    if (reiserfs_ih_get_flags(left) != reiserfs_ih_get_flags(right))
	return 0;
    
    if ((reiserfs_ih_direct (left) || reiserfs_ih_ext (left))) {
	return (reiserfs_key_get_off (&left->ih_key) + 
		reiserfs_leaf_ibytes (left, bsize) == 
		reiserfs_key_get_off (&right->ih_key)) ? 1 : 0;
    }
    
    return 0;
}
int reiserfs_leaf_count_items(reiserfs_bh_t *bh) {
    reiserfs_ih_t * ih;
    int prev_location;
    int nr;

    /* look at the table of item head */
    prev_location = bh->b_size;
    ih = reiserfs_ih_at(bh, 0);
    nr = 0;
    while (1) {
	if (reiserfs_ih_get_loc (ih) + reiserfs_ih_get_len (ih) != prev_location)
	    break;
	
	if (reiserfs_ih_get_loc (ih) < 
	    REISERFS_IH_SIZE * (nr + 1) + REISERFS_NODEH_SIZE)
	{
	    break;
	}
	
	if (reiserfs_ih_get_len (ih) > REISERFS_ITEM_MAX (bh->b_size))
	    break;
	
	prev_location = reiserfs_ih_get_loc (ih);
	ih ++;
	nr ++;
    }
    
    return nr;
}

int reiserfs_leaf_free_count(reiserfs_bh_t *bh) {
    reiserfs_ih_t * ih;
    int nr;
    
    nr = reiserfs_nh_get_items(NODE_HEAD(bh));
    ih = reiserfs_ih_at(bh, nr - 1);
    
    return (nr ? reiserfs_ih_get_loc (ih) : bh->b_size) - 
	    REISERFS_NODEH_SIZE - REISERFS_IH_SIZE * nr;
}

static int leaf_blkh_correct(reiserfs_bh_t * bh) {
    unsigned int nr;

    nr = reiserfs_nh_get_items(NODE_HEAD(bh));
    if (nr > ((bh->b_size - REISERFS_NODEH_SIZE) / 
	      (REISERFS_IH_SIZE + REISERFS_ITEM_MIN)))
    {
	/* item number is too big or too small */
	return 0;
    }

    return reiserfs_leaf_free_count(bh) == 
	    reiserfs_nh_get_free (NODE_HEAD(bh));
}

int reiserfs_leaf_estimate_items(reiserfs_bh_t * bh) {
    int nr = reiserfs_leaf_count_items(bh);

    return nr >= reiserfs_nh_get_items (NODE_HEAD(bh)) ? 
	    reiserfs_nh_get_items (NODE_HEAD(bh)) : nr;
}

/* for every item call common action and an action corresponding to
   item type */
void reiserfs_leaf_traverse(reiserfs_bh_t * bh, 
			    ih_func_t action,
			    item_func_t * actions)
{
    int i;
    reiserfs_ih_t * ih;
    item_func_t iaction;

    ih = reiserfs_ih_at (bh, 0);
    for (i = 0; i < reiserfs_nh_get_items (NODE_HEAD (bh)); i ++, ih ++) {
	if (action)
	    action (ih);

	iaction = actions[reiserfs_key_get_type (&ih->ih_key)];
	if (iaction)
	    iaction (bh, ih);
    }
}

/* Returns 0 if not leaf, NT_LEAF if looks correct, NT_IH_ARRAY if 
   looks like corrupted leaf. */
int reiserfs_leaf_valid(reiserfs_bh_t *bh) {
    int counted;
    int num;
    
    if (!reiserfs_leaf_head (bh))
	return 0;

    counted = reiserfs_leaf_count_items(bh);
    
    /* if leaf block header is ok, check item count also. */
    if (leaf_blkh_correct(bh)) {
	num = reiserfs_nh_get_items (NODE_HEAD(bh));
	return counted >= num ? NT_LEAF : NT_IH_ARRAY;
    }
    
    /* leaf block header is corrupted, it is ih_array if 
       some items were detected.*/
    return counted ? NT_IH_ARRAY : 0;
}

/* wrappers for operations on one separated leaf */
void reiserfs_leaf_delete_item (reiserfs_filsys_t * fs,
				reiserfs_bh_t * bh, 
				int item_num)
{
    reiserfs_bufinfo_t bi;

    bi.bi_bh = bh;
    bi.bi_parent = 0;
    bi.bi_position = 0;
    reiserfs_lb_delete_item (fs, &bi, item_num, 1);
}

void reiserfs_leaf_delete_entry (reiserfs_filsys_t * fs, 
				 reiserfs_bh_t * bh,
				 int item_num, 
				 int entry_num, 
				 int del_count)
{
    reiserfs_bufinfo_t bi;

    bi.bi_bh = bh;
    bi.bi_parent = 0;
    bi.bi_position = 0;
    reiserfs_lb_delete_unit (fs, &bi, item_num, entry_num, del_count);
}

/* ih_key, ih_location and ih_item_len seem correct, check other fields */
static int reiserfs_leaf_ih_correct (reiserfs_ih_t * ih) {
    int ih_format;
    int format;

    /* key format from item_head */
    ih_format = reiserfs_ih_get_format (ih);
    if (ih_format != KEY_FORMAT_1 && ih_format != KEY_FORMAT_2)
	return 0;

    /* key format calculated on key */
    format = reiserfs_key_format (&ih->ih_key);
    if (reiserfs_ih_stat (ih)) {
	/* for stat data we can not find key format from a key itself, so look at
           the item length */
	if (reiserfs_ih_get_len (ih) == REISERFS_SD_SIZE)
	    format = KEY_FORMAT_2;
	else if (reiserfs_ih_get_len (ih) == REISERFS_SD_SIZE_V1)
	    format = KEY_FORMAT_1;
	else
	    return 0;
    }
    if (format != ih_format)
	return 0;

    /* we do not check ih_format.fsck_need as fsck might change it. So,
       debugreiserfs -p will have to dump it */
    return 1;
}

/* used by debugreisrefs -p only yet */
int reiserfs_leaf_correct_at (reiserfs_filsys_t * fs, 
				reiserfs_ih_t * ih, char * item,
				unfm_func_t func, int bad_dir)
{
/*
    if (!does_key_look_correct (fs, &ih->ih_key))
	return 1;

    if (!reiserfs_leaf_ih_correct (ih))
	return 1;
*/
    if (!reiserfs_leaf_ih_correct (ih))
	return 1;

    
    if (reiserfs_ih_stat (ih) || reiserfs_ih_direct (ih))
	return 0;

    if (reiserfs_ih_dir (ih)) {
	return reiserfs_direntry_check (fs, ih, item, bad_dir);
    }
    
    if (reiserfs_ih_ext (ih)) {
	return reiserfs_ext_check (fs, ih, item, func);
    }

    return 1;
}


static int is_symlink = 0;
int reiserfs_leaf_print(FILE * fp, 
			reiserfs_filsys_t * fs, 
			reiserfs_bh_t * bh,
			int print_mode, 
			int first, 
			int last)
{
    reiserfs_ih_t * ih;
    int i;
    int from, to;
    int real_nr, nr;

    if (!reiserfs_node_formatted (bh, LEAF_LEVEL))
	return 1;
    
    ih = reiserfs_ih_at (bh,0);
    real_nr = reiserfs_leaf_count_items(bh);
    nr = reiserfs_nh_get_items(NODE_HEAD(bh));

    reiserfs_warning (fp, "\n==========================================="
		      "========================\n");
    
    reiserfs_warning (fp, "LEAF NODE (%ld) contains %b (real items %d)\n",
		      bh->b_blocknr, bh, real_nr);

    if (!(misc_test_bit(LP_LEAF_DETAILS, &print_mode))) {
	reiserfs_warning (fp, "FIRST ITEM_KEY: %k, LAST ITEM KEY: %k\n",
			   &(ih->ih_key), &((ih + real_nr - 1)->ih_key));
	return 0;
    }

    if (first < 0 || first > real_nr - 1) 
	from = 0;
    else 
	from = first;

    if (last < 0 || last > real_nr)
	to = real_nr;
    else
	to = last;


    reiserfs_warning (fp, "---------------------------------------------"
		      "----------------------------------\n"
		      "|###|type|ilen|f/sp| loc|fmt|fsck|               "
		      "    key                      |\n"
		       "|   |    |    |e/cn|    |   |need|              "
		       "                              |\n");
    
    for (i = from; i < to; i++) {
	reiserfs_warning (fp, "-----------------------------------------"
			  "--------------------------------------\n"
			  "|%3d|%30H|%s\n", i, ih + i, i >= nr ? 
			  " DELETED" : "");

	if (reiserfs_ih_stat(ih+i)) {
	    is_symlink = reiserfs_print_stat_data (fp, bh, ih + i, 0/*all times*/);
	    continue;
	}

	if (reiserfs_ih_dir(ih+i)) {
	    reiserfs_direntry_print (fp, fs, bh, ih+i);
	    continue;
	}

	if (reiserfs_ih_ext(ih+i)) {
	    reiserfs_ext_print (fp, bh, i);
	    continue;
	}

	if (reiserfs_ih_direct(ih+i)) {
	    int j = 0;
	    if (is_symlink || misc_test_bit(LP_DIRECT_ITEMS, &print_mode)) {
		reiserfs_warning (fp, "\"");
		while (j < reiserfs_ih_get_len (&ih[i])) {
		    if (reiserfs_item_by_ih(bh,ih+i)[j] == 10)
			reiserfs_warning (fp, "\\n");
		    else
			reiserfs_warning (fp, "%c", reiserfs_item_by_ih(bh,ih+i)[j]);
		    j ++;
		}
		reiserfs_warning (fp, "\"\n");
	    }
	    continue;
	}
    }
    reiserfs_warning (fp, "============================================="
		      "======================\n");
    
    return 0;
}

__u16 reiserfs_ih_get_format(const reiserfs_ih_t *ih) {
	return misc_get_bitfield_XX (16, &ih->ih_format, 0, 12);
}

void reiserfs_ih_set_format(reiserfs_ih_t *ih, __u16 val) {
	misc_set_bitfield_XX (16, &ih->ih_format, val, 0, 12);
}

__u16 reiserfs_ih_get_flags(const reiserfs_ih_t *ih) {
	return misc_get_bitfield_XX (16, &ih->ih_format, 12, 4);
}

void reiserfs_ih_set_flags(reiserfs_ih_t *ih, __u16 val) {
	misc_set_bitfield_XX (16, &ih->ih_format, val, 12, 4);
}

