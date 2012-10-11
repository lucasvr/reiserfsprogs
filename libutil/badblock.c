/*
 *  Copyright 2000-2004 by Hans Reiser, licensing governed by 
 *  reiserfsprogs/README
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "util/badblock.h"

int util_badblock_load (reiserfs_filsys_t * fs, char * badblocks_file) {
    FILE * fd;
    char buf[128];
    __u32 blocknr;
    int count;

    fs->fs_badblocks_bm = 
	    reiserfs_bitmap_create(reiserfs_sb_get_blocks(fs->fs_ondisk_sb));
    
    reiserfs_bitmap_zero (fs->fs_badblocks_bm);

    if (!badblocks_file)
	return 0;
    
    fd = fopen (badblocks_file, "r");

    if (fd == NULL) {
        fprintf (stderr, "%s: Failed to open the given badblock file '%s'.\n\n",
        	__FUNCTION__, badblocks_file);
        return 1;
    }


    while (!feof (fd)) {
	if (fgets(buf, sizeof(buf), fd) == NULL)
	    break;
	count = sscanf(buf, "%u", &blocknr);
	
	if (count <= 0)
	    continue;

	if (blocknr >= reiserfs_sb_get_blocks (fs->fs_ondisk_sb)) {
	    fprintf (stderr, "%s: block number (%u) points out of fs size "
		     "(%u).\n", __FUNCTION__, blocknr, 
		     reiserfs_sb_get_blocks(fs->fs_ondisk_sb));
	} else if (reiserfs_fs_block(fs, blocknr) != BT_UNKNOWN) {
	    fprintf (stderr, "%s: block number (%u) belongs to system "
		     "reiserfs area. It cannot be relocated.\n", 
		     __FUNCTION__, blocknr);
	    return 1;
	} else {
	    reiserfs_bitmap_set_bit (fs->fs_badblocks_bm, blocknr);
	} 
    }

    fclose (fd);

    return 0;
}


