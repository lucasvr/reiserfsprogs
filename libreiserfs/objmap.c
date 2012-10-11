/*
 *  Copyright 2000-2004 by Hans Reiser, licensing governed by 
 *  reiserfsprogs/README
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "reiserfs/libreiserfs.h"

static int comp_ids (const void * p1, const void * p2) {
    __u32 id1 = le32_to_cpu(*(__u32 *)p1) ;
    __u32 id2 = le32_to_cpu(*(__u32 *)p2) ;

    if (id1 < id2)
        return -1;
    if (id1 > id2)
        return 1 ;
    return 0 ;
}

/* functions to manipulate with super block's objectid map */

int reiserfs_objmap_test (reiserfs_filsys_t * fs, __u32 objectid)
{
    __u32 * objectid_map;
    __u32 count = reiserfs_sb_get_mapcur(fs->fs_ondisk_sb);
    int ret;
    int pos;

    objectid_map = (__u32 *)((char *)fs->fs_ondisk_sb + 
			     reiserfs_super_size (fs->fs_ondisk_sb));
    
    ret = misc_bin_search(&objectid, objectid_map, count, 
			  sizeof(__u32), &pos, comp_ids);

    /* if the position returned is odd, the oid is in use */
    if (ret == 0)
	return ((__u32)pos & 1);

    /* if the position returned is even, the oid is in use */
    return !((__u32)pos & 1) ;
}


void reiserfs_objmap_set (reiserfs_filsys_t * fs, __u32 objectid)
{
    int i;
    __u32 * objectid_map;
    int cursize;


    if (reiserfs_objmap_test (fs, objectid)) {
	return;
    }

    objectid_map = (__u32 *)((char *)fs->fs_ondisk_sb + 
			     reiserfs_super_size (fs->fs_ondisk_sb));
    
    cursize = reiserfs_sb_get_mapcur (fs->fs_ondisk_sb);

    for (i = 0; i < cursize; i += 2) {
	if (objectid >= le32_to_cpu (objectid_map [i]) &&
	    objectid < le32_to_cpu (objectid_map [i + 1]))
	    /* it is used */
	    return;
	
	if (objectid + 1 == le32_to_cpu (objectid_map[i])) {
	    /* size of objectid map does not change */
	    objectid_map[i] = cpu_to_le32 (objectid);
	    return;
	}
	
	if (objectid == le32_to_cpu (objectid_map[i + 1])) {
	    /* size of objectid map is decreased */
	    objectid_map [i + 1] = 
		    cpu_to_le32 (le32_to_cpu (objectid_map [i + 1]) + 1);

	    if (i + 2 < cursize) {
		if (objectid_map[i + 1] == objectid_map[i + 2]) {
		    memmove (objectid_map + i + 1, objectid_map + i + 1 + 2, 
			     (cursize - (i + 2 + 2 - 1)) * sizeof (__u32));
		    reiserfs_sb_set_mapcur (fs->fs_ondisk_sb, cursize - 2);
		}
	    }
	    return;
	}
	
	if (objectid < le32_to_cpu (objectid_map[i])) {
	    /* size of objectid map must be increased */
	    if (cursize == reiserfs_sb_get_mapmax (fs->fs_ondisk_sb)) {
		/* here all objectids between objectid and objectid_map[i] get
                   used */
		objectid_map[i] = cpu_to_le32 (objectid);
		return;
	    } else {
		memmove (objectid_map + i + 2, objectid_map + i, 
			 (cursize - i) * sizeof (__u32));
		
		reiserfs_sb_set_mapcur (fs->fs_ondisk_sb, cursize + 2);
	    }
	    
	    objectid_map[i] = cpu_to_le32 (objectid);
	    objectid_map[i+1] = cpu_to_le32 (objectid + 1);
	    return;
	}
	
    }
    
    /* append to current objectid map, if we have space */
    if (i < reiserfs_sb_get_mapmax (fs->fs_ondisk_sb)) {
	objectid_map[i] = cpu_to_le32 (objectid);
	objectid_map[i + 1] = cpu_to_le32 (objectid + 1);
	reiserfs_sb_set_mapcur (fs->fs_ondisk_sb, cursize + 2);
    } else if (i == reiserfs_sb_get_mapmax (fs->fs_ondisk_sb)) {
	objectid_map[i - 1] = cpu_to_le32 (objectid + 1);
    } else
	misc_die ("mark_objectid_as_used: objectid map corrupted");
    
    return;
}


void reiserfs_objmap_print (FILE * fp, reiserfs_filsys_t * fs)
{
    int i;
    reiserfs_sb_t * sb;
    __u32 * omap;


    sb = fs->fs_ondisk_sb;
    if (fs->fs_format == REISERFS_FORMAT_3_6)
	omap = (__u32 *)(sb + 1);
    else if (fs->fs_format == REISERFS_FORMAT_3_5)
	omap = (__u32 *)((reiserfs_sb_v1_t *)sb + 1);
    else {
	reiserfs_warning (fp, "reiserfs_objmap_print: proper "
			  "signature is not found\n");
	return;
    }
	
    reiserfs_warning (fp, "Map of objectids (super block size %d)\n",
		      (char *)omap - (char *)sb);
      
    for (i = 0; i < reiserfs_sb_get_mapcur (sb); i ++) {
	if (i % 2 == 0) {
	    reiserfs_warning(fp, "busy(%u-%u) ", le32_to_cpu (omap[i]),
			     le32_to_cpu (omap[i+1]) - 1); 
	} else {
	    reiserfs_warning(fp, "free(%u-%u) ", le32_to_cpu (omap[i]),
			    ((i+1) == reiserfs_sb_get_mapcur (sb)) ? 
			    ~(__u32)0 : (le32_to_cpu (omap[i+1]) - 1));
	}
    }

    reiserfs_warning (fp, "\nObject id array has size %d (max %d):", 
		      reiserfs_sb_get_mapcur (sb), reiserfs_sb_get_mapmax (sb));
  
    for (i = 0; i < reiserfs_sb_get_mapcur (sb); i ++)
	reiserfs_warning (fp, "%s%u ", i % 2 ? "" : "*", le32_to_cpu (omap[i])); 
    reiserfs_warning (fp, "\n");

}

