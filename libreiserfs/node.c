/*
 *  Copyright 2000-2004 by Hans Reiser, licensing governed by 
 *  reiserfsprogs/README
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "reiserfs/libreiserfs.h"

#include <stdarg.h>

/* Replace n_dest'th key in buffer dest by n_src'th key of buffer src.*/
void reiserfs_node_replace_key (reiserfs_bh_t * dest, int n_dest,
				reiserfs_bh_t * src, int n_src)
{
    if (dest) {
	if (reiserfs_leaf_head (src)) {
	    /* source buffer contains leaf node */
	    memcpy (reiserfs_int_key_at(dest,n_dest), 
		    reiserfs_ih_at(src,n_src), REISERFS_KEY_SIZE);
	} else {
	    memcpy (reiserfs_int_key_at(dest,n_dest), 
		    reiserfs_int_key_at(src,n_src), REISERFS_KEY_SIZE);
	}
	
	reiserfs_buffer_mkdirty(dest);
    }
}

void reiserfs_node_forget(reiserfs_filsys_t *fs, unsigned long blk) {
    reiserfs_bh_t * to_be_forgotten;

    to_be_forgotten = reiserfs_buffer_find (fs->fs_dev, blk, fs->fs_blocksize);
    
    if (to_be_forgotten) {
	reiserfs_nh_set_level(NODE_HEAD(to_be_forgotten), FREE_LEVEL);
	to_be_forgotten->b_count ++;
	reiserfs_buffer_forget (to_be_forgotten);
    }

    if (fs->block_deallocator)
	fs->block_deallocator (fs, blk);
}

// make sure that bh contains formatted node of reiserfs tree of
// 'level'-th level
int reiserfs_node_formatted (reiserfs_bh_t * bh, int level)
{
    if (reiserfs_node_level (bh) != level)
	return 0;
    if (reiserfs_leaf_head (bh))
	return reiserfs_leaf_valid(bh);

    return reiserfs_internal_correct (bh);
}

/* returns code of reiserfs metadata block (leaf, internal, super
   block, journal descriptor), unformatted */
int reiserfs_node_type (reiserfs_bh_t *bh) {
    int res;
    
    /* super block? */
    if (reiserfs_super_valid(bh))
	return NT_SUPER;

    if ((res = reiserfs_leaf_valid(bh)))
	/* if block head and item head array seem matching (node level, free
           space, item number, item locations and length), then it is NT_LEAF,
	  otherwise, it is NT_IH_ARRAY */
	return res;

    if (reiserfs_internal_correct(bh))
	return NT_INTERNAL;


    /* journal descriptor block? */
    if (reiserfs_journal_desc_valid(bh))
	return NT_JDESC;

    /* contents of buf does not look like reiserfs metadata. Bitmaps
       are possible here */
    return NT_UNKNOWN;
}

char *node_name[NT_UNKNOWN + 1] = {
    "",
    "leaf", 
    "internal", 
    "super", 
    "journal descriptor", 
    "broken leaf", 
    "unknown"
};

char * reiserfs_node_type_name(int code) {
    return code >= NT_LEAF && code < NT_UNKNOWN ? 
	    node_name[code] : node_name[NT_UNKNOWN];
}

void reiserfs_node_print (FILE * fp, 
			  reiserfs_filsys_t * fs, 
			  reiserfs_bh_t * bh, ...)
		       /* int print_mode, 
			  int first, 
			  int last         */
{
    va_list args;
    int mode, first, last;
    char * file_name;

    va_start (args, bh);

    if ( ! bh ) {
	reiserfs_warning (stderr, "reiserfs_node_print: buffer is NULL\n");
	return;
    }

    mode = va_arg (args, int);
    first = va_arg (args, int);
    last = va_arg (args, int);
    file_name = (fs) ? fs->fs_file_name : NULL ;
    if (reiserfs_print_jdesc (fp, bh))
        if (reiserfs_super_print (fp, fs, file_name, bh, 0))
	    if (reiserfs_leaf_print (fp, fs, bh, mode, first, last))
		if (reiserfs_internal_print (fp, bh, first, last))
		    reiserfs_warning (fp, "Block %ld contains unformatted data\n", bh->b_blocknr);
}

