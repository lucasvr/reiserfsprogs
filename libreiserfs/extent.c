/*
 *  Copyright 2000-2004 by Hans Reiser, licensing governed by 
 *  reiserfsprogs/README
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "reiserfs/libreiserfs.h"
#include "misc/unaligned.h"

/* check item length, ih_free_space for pure 3.5 format, unformatted node
   pointers */
int reiserfs_ext_check (reiserfs_filsys_t * fs, 
			     reiserfs_ih_t * ih, 
			     char * item,
			     unfm_func_t func)
{
    unsigned int i;
    __u32 * ind = (__u32 *)item;

    if (reiserfs_ih_get_len (ih) % REISERFS_EXT_SIZE)
	return 1;

    for (i = 0; i < reiserfs_ext_count (ih); i ++) {
	if (!ind [i])
	    continue;
	if (func && func (fs, d32_get (ind, i)))
	    return 1;
    }

    if (fs->fs_format == REISERFS_FORMAT_3_5) {
	/* check ih_free_space for 3.5 format only */
	if (reiserfs_ih_get_free (ih) > fs->fs_blocksize - 1)
	    return 1;
    }
    
    return 0;
}

//
// printing of extent item
//
static void start_new_sequence (__u32 * start, int * len, __u32 new)
{
    *start = new;
    *len = 1;
}

static int sequence_finished (__u32 start, int * len, __u32 new)
{
    if (le32_to_cpu (start) == MAX_INT)
	return 1;

    if (start == 0 && new == 0) {
	(*len) ++;
	return 0;
    }
    if (start != 0 && (le32_to_cpu (start) + *len) == le32_to_cpu (new)) {
	(*len) ++;
	return 0;
    }
    return 1;
}

static void print_sequence (FILE * fp, __u32 start, int len)
{
    if (start == MAX_INT)
	return;

    if (len == 1)
	reiserfs_warning (fp, " %d", le32_to_cpu (start));
    else
	reiserfs_warning (fp, " %d(%d)", le32_to_cpu (start), len);
}

void reiserfs_ext_print(FILE * fp, 
			     reiserfs_bh_t * bh, 
			     int item_num)
{
    reiserfs_ih_t * ih;
    unsigned int j;
    __u32 * unp, prev = MAX_INT;
    int num = 0;

    ih = reiserfs_ih_at (bh, item_num);
    unp = (__u32 *)reiserfs_item_by_ih (bh, ih);

    if (reiserfs_ih_get_len (ih) % REISERFS_EXT_SIZE)
	reiserfs_warning (fp, "reiserfs_ext_print: invalid item len");  

    reiserfs_warning (fp, "%d pointer%s\n[", reiserfs_ext_count (ih),
                      reiserfs_ext_count (ih) != 1 ? "s" : "" );
    for (j = 0; j < reiserfs_ext_count (ih); j ++) {
	if (sequence_finished (prev, &num, d32_get(unp, j))) {
	    print_sequence (fp, prev, num);
	    start_new_sequence (&prev, &num, d32_get(unp, j));
	}
    }
    print_sequence (fp, prev, num);
    reiserfs_warning (fp, "]\n");
}
