/*
 * Copyright 2000-2004 by Hans Reiser, licensing governed by 
 * reiserfsprogs/README
 */
 
#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif
 
#include "debugreiserfs.h"
#include "misc/unaligned.h"
#include "util/device.h"
#include "util/misc.h"

#include <sys/resource.h>
#include <fcntl.h>
#include <unistd.h>

#define print_usage_and_exit() misc_die ("Usage: %s [-v] [-b filename] device\n\
-v		prints blocks number of every block unpacked\n\
-b filename	saves bitmap of blocks unpacked to filename\n\
-j filename     stores journal in the filename\n", argv[0]);


/* when super block gets unpacked for the first time - create a bitmap
   and mark in it what have been unpacked. Save that bitmap at the end */
reiserfs_bitmap_t * what_unpacked = 0;


int leaves, full;

int verbose = 0;

int Default_journal = 1;

static void unpack_offset (struct packed_item * pi, reiserfs_ih_t * ih, int blocksize)
{

    if (get_pi_mask(pi) & OFFSET_BITS_64) {
	__u64 v64;

	if (reiserfs_ih_get_format (ih) != KEY_FORMAT_2)
	    misc_die ("unpack_offset: key format is not set or wrong");
	fread_le64 (&v64);
	reiserfs_key_set_off (KEY_FORMAT_2, &ih->ih_key, v64);
	return;
    }

    if (get_pi_mask(pi) & OFFSET_BITS_32) {
	__u32 v32;

	fread_le32 (&v32);
	reiserfs_key_set_off (reiserfs_ih_get_format (ih), &ih->ih_key, v32);
	return;
    }

    if ((get_pi_mask(pi) & DIR_ID) == 0 && (get_pi_mask(pi) & OBJECT_ID) == 0) {
	/* offset was not sent, as it can be calculated looking at the
           previous item */
	if (reiserfs_ih_stat (ih - 1))
	    reiserfs_key_set_off (reiserfs_ih_get_format (ih), &ih->ih_key, 1);
	if (reiserfs_ih_ext (ih - 1))
	    reiserfs_key_set_off(reiserfs_ih_get_format (ih), &ih->ih_key, 
				    reiserfs_key_get_off (&(ih - 1)->ih_key)
				    + reiserfs_leaf_ibytes (ih - 1, blocksize));
    }

    // offset is 0
    return;
}


static void unpack_type (struct packed_item * pi, reiserfs_ih_t * ih)
{
    reiserfs_key_set_type (reiserfs_ih_get_format (ih), &ih->ih_key, get_pi_type(pi));
    if (reiserfs_key_unkn (&ih->ih_key))
	reiserfs_panic ("unpack_type: unknown type %d unpacked for %H\n",
			get_pi_type(pi), ih);
}


/* direntry item comes in the following format: 
   for each entry
      mask - 8 bits
      entry length - 16 bits
      entry itself
      deh_objectid - 32 bits
      	maybe deh_dir_id (32 bits)
	maybe gencounter (16)
	maybe deh_state (16)
*/
static void unpack_direntry (struct packed_item * pi, reiserfs_bh_t * bh,
			     reiserfs_ih_t * ih, hashf_t hash_func)
{
    __u16 entry_count, namelen, gen_counter, entry_len;
    __u8 mask;
    int i;
    reiserfs_deh_t * deh;
    int location;
    char * item;

/*    if (!hash_func)
	misc_die ("unpack_direntry: hash function is not set");*/

    if (!(get_pi_mask(pi) & IH_FREE_SPACE))
        misc_die ("ih_entry_count must be packed for directory items");

    entry_count = reiserfs_ih_get_entries (ih);
/*    if (!entry_count)
	reiserfs_panic ("unpack_direntry: entry count should be set already");*/

    item = bh->b_data + reiserfs_ih_get_loc (ih);
    deh = (reiserfs_deh_t *)item;
    location = get_pi_item_len(pi);
    for (i = 0; i < entry_count; i ++, deh ++) {
	fread8 (&mask);
	fread_le16 (&entry_len);
	location -= entry_len;
	reiserfs_deh_set_loc (deh, location);
	fread (item + location, entry_len, 1, stdin);

	/* find name length */
	namelen = strlen (item + location);
	if (namelen > entry_len)
	    namelen = entry_len;

	fread32 (&deh->deh2_objectid);
	if (mask & HAS_DIR_ID)
	    fread32 (&deh->deh2_dir_id);
	else
	    reiserfs_deh_set_did (deh, reiserfs_key_get_oid (&ih->ih_key));

	if (*(item + location) == '.' && namelen == 1) {
	    /* old or new "." */
	    reiserfs_deh_set_off (deh, OFFSET_DOT);
	} else if (*(item + location) == '.' && 
		   *(item + location + 1) == '.' && namelen == 2) 
	{
	    /* old or new ".." */
	    reiserfs_deh_set_off (deh, OFFSET_DOT_DOT);
	} else if (hash_func) {
	    reiserfs_deh_set_off (deh, reiserfs_hash_value(hash_func, 
				  item + location, namelen));
	}
	
	if (mask & HAS_GEN_COUNTER) {
	    fread_le16 (&gen_counter);
	    reiserfs_deh_set_off (deh, reiserfs_deh_get_off (deh) | gen_counter);
	}

	if (mask & HAS_STATE)
	    fread16 (&deh->deh2_state);
	else
	    reiserfs_deh_set_state (deh, (1 << DEH_Visible2));
    }

    return;
}


/* struct packed_item is already unpacked */
static void unpack_stat_data (struct packed_item * pi, reiserfs_bh_t * bh,
			      reiserfs_ih_t * ih)
{
    if (!(get_pi_mask(pi) & IH_FREE_SPACE)) {
        /* ih_free_space was not packed - set default */
        reiserfs_ih_set_entries (ih, 0xffff);
    }

    if (reiserfs_ih_get_format (ih) == KEY_FORMAT_1) {
	/* stat data comes in the following format:
	   if this is old stat data:
	   mode - 16 bits
	   nlink - 16 bits
	   size - 32 bits
	   blocks/rdev - 32 bits
	   maybe first_direct byte 32 bits
	*/
	reiserfs_sd_v1_t * sd;

	sd = (reiserfs_sd_v1_t *)reiserfs_item_by_ih (bh, ih);
	memset (sd, 0, sizeof (sd));

	fread16 (&sd->sd_mode);
	fread16 (&sd->sd_nlink);
	fread32 (&sd->sd_size);
	fread32 (&sd->u.sd_blocks);
	
	if (get_pi_mask(pi) & WITH_SD_FIRST_DIRECT_BYTE) {
	    fread32 (&sd->sd_fdb);
	} else {
	    sd->sd_fdb = 0xffffffff;
	}
    } else {
	/* for new stat data
	   mode - 16 bits
	   nlink in either 16 or 32 bits
	   size in either 32 or 64 bits
	   blocks - 32 bits
	*/
	reiserfs_sd_t * sd;

	sd = (reiserfs_sd_t *)reiserfs_item_by_ih (bh, ih);
	memset (sd, 0, sizeof (sd));
	
	fread16 (&sd->sd_mode);

	if (get_pi_mask(pi) & NLINK_BITS_32) {
	    fread32 (&sd->sd_nlink);
	} else {
	    __u16 nlink16;

	    fread16 (&nlink16);
            reiserfs_set_sd_v2_nlink (sd, le16_to_cpu(nlink16));
	}

	if (get_pi_mask(pi) & SIZE_BITS_64) {
	    fread64 (&sd->sd_size);
	} else {
	    __u32 size32;

            /* We need the endian conversions since sd->sd_size is 64 bit */
	    fread_le32 (&size32);
            reiserfs_set_sd_v2_size (sd, size32 );
	}

	fread32 (&sd->sd_blocks);
    }

    return;
}


/* extent item comes either in packed form or as is. ih_free_space
   can go first */
static void unpack_extent (struct packed_item * pi, reiserfs_bh_t * bh,
			     reiserfs_ih_t * ih)
{
    __u32 * ind_item, * end;
    int i;
    __u16 v16;

    if (!(get_pi_mask(pi) & IH_FREE_SPACE)) {
        /* ih_free_space was not packed - set default */
        reiserfs_ih_set_entries (ih, 0);
    }

    ind_item = (__u32 *)reiserfs_item_by_ih (bh, ih);

    if (get_pi_mask(pi) & SAFE_LINK) {
	d32_put(ind_item, 0, reiserfs_key_get_did(&ih->ih_key));
	reiserfs_key_set_did(&ih->ih_key, (__u32)-1);
	return;
    }

    if (get_pi_mask(pi) & WHOLE_EXTENT) {
	fread (ind_item, get_pi_item_len(pi), 1, stdin);
	return;
    }

    end = ind_item + reiserfs_ext_count (ih);
    while (ind_item < end) {
        __u32 base;
	fread32 (ind_item);
	fread_le16 (&v16);
        base = d32_get(ind_item, 0);
	for (i = 1; i < v16; i ++) {
	    if (base != 0)
		d32_put(ind_item, i, base + i);
	    else
		d32_put(ind_item, i, 0);
	}
	ind_item += i;
    }
    return;
}


// FIXME: we have no way to preserve symlinks
static void unpack_direct (struct packed_item * pi, reiserfs_bh_t * bh,
			   reiserfs_ih_t * ih)
{
    __u32 * d_item = (__u32 *)reiserfs_item_by_ih (bh, ih);

    if (!(get_pi_mask(pi) & IH_FREE_SPACE))
        /* ih_free_space was not packed - set default */
        reiserfs_ih_set_entries (ih, 0xffff);

    if (get_pi_mask(pi) & SAFE_LINK) {
	d32_put(d_item, 0, reiserfs_key_get_did(&ih->ih_key));
	reiserfs_key_set_did(&ih->ih_key, (__u32)-1);
    } else {
	memset (d_item, 'a', get_pi_item_len(pi));
    }
    return;
}


static void unpack_leaf (int dev, hashf_t hash_func, __u16 blocksize)
{
    static int unpacked_leaves = 0;
    reiserfs_bh_t * bh;
    struct packed_item pi;
    reiserfs_ih_t * ih;
    int i;
    __u16 v16;
    __u32 v32;
    
    /* block number */
    fread_le32 (&v32);


    /* item number */
    fread_le16 (&v16);

    if (verbose)
	reiserfs_warning (stderr, "leaf %d: %d items\n", v32, v16);

    bh = reiserfs_buffer_open (dev, v32, blocksize);
    if (!bh)
	misc_die ("unpack_leaf: reiserfs_buffer_open failed");

    reiserfs_nh_set_items (NODE_HEAD (bh), v16);
    reiserfs_nh_set_level (NODE_HEAD (bh), LEAF_LEVEL);
    reiserfs_nh_set_free (NODE_HEAD (bh), REISERFS_NODE_SPACE (bh->b_size));

    ih = reiserfs_ih_at (bh, 0);
    for (i = 0; i < reiserfs_nh_get_items (NODE_HEAD (bh)); i ++, ih ++) {
#if 0
	fread32 (&v32);
	if (v32 != ITEM_START_MAGIC)
	    misc_die ("unpack_leaf: no start item magic found: block %lu, item %i",
		 bh->b_blocknr, i);
#endif	

	fread (&pi, sizeof (struct packed_item), 1, stdin);
	
	/* dir_id - if it is there */
	if (get_pi_mask(&pi) & DIR_ID) {
	    fread32 (&v32);
            reiserfs_key_set_did (&ih->ih_key, le32_to_cpu(v32));
	} else {
	    if (!i)
		misc_die ("unpack_leaf: dir_id is not set");
	    reiserfs_key_set_did (&ih->ih_key, reiserfs_key_get_did (&(ih - 1)->ih_key));
	}

	/* object_id - if it is there */
	if (get_pi_mask(&pi) & OBJECT_ID) {
	    fread32 (&v32);
            reiserfs_key_set_oid (&ih->ih_key, le32_to_cpu(v32));
	} else {
	    if (!i)
		misc_die ("unpack_leaf: object_id is not set");
	    reiserfs_key_set_oid (&ih->ih_key, reiserfs_key_get_oid (&(ih - 1)->ih_key));
	}

	// we need to set item format before offset unpacking
	reiserfs_ih_set_format (ih, (get_pi_mask(&pi) & NEW_FORMAT) ? KEY_FORMAT_2 : KEY_FORMAT_1);

	// offset
	unpack_offset (&pi, ih, bh->b_size);

	/* type */
	unpack_type (&pi, ih);

	/* ih_free_space and ih_format */
	if (get_pi_mask(&pi) & IH_FREE_SPACE) {
	    fread16 (&v16);
	    reiserfs_ih_set_entries (ih, le16_to_cpu(v16));
	}

	if (get_pi_mask(&pi) & IH_FORMAT)
	    fread16 (&ih->ih_format);

	/* item length and item location */
	reiserfs_ih_set_len (ih, get_pi_item_len(&pi));
	reiserfs_ih_set_loc (ih, (i ? reiserfs_ih_get_loc (ih - 1) : bh->b_size) - get_pi_item_len(&pi));

	// item itself
	if (reiserfs_ih_direct (ih)) {
	    unpack_direct (&pi, bh, ih);
	} else if (reiserfs_ih_ext (ih)) {
	    unpack_extent (&pi, bh, ih);
	} else if (reiserfs_ih_dir (ih)) {
	    unpack_direntry (&pi, bh, ih, hash_func);
	} else if (reiserfs_ih_stat (ih)) {
	    unpack_stat_data (&pi, bh, ih);
	}
	reiserfs_nh_set_free (NODE_HEAD (bh), reiserfs_nh_get_free (NODE_HEAD (bh)) -
			     (REISERFS_IH_SIZE + reiserfs_ih_get_len (ih)));

#if 0
	fread32 (&v32);
	if (v32 != ITEM_END_MAGIC)
	    misc_die ("unpack_leaf: no end item magic found: block %lu, item %i",
		 bh->b_blocknr, i);
	if (verbose)
	    reiserfs_warning (stderr, "%d: %H\n", i, ih);
#endif
    }

    fread_le16 (&v16);
    if (v16 != LEAF_END_MAGIC)
	misc_die ("unpack_leaf: wrong end signature found - %x, block %lu", 
	     v16, bh->b_blocknr);

    reiserfs_buffer_mkuptodate (bh, 1);
    reiserfs_buffer_mkdirty (bh);
    reiserfs_buffer_write (bh);
    /*
    if (reiserfs_fs_block(fs, bh->b_blocknr) == BT_UNKNOWN)
	data_blocks_unpacked ++;
    */
    reiserfs_buffer_close (bh);

    if (what_unpacked)
	reiserfs_bitmap_set_bit (what_unpacked, bh->b_blocknr);
    /*unpacked ++;*/

    if (!(++ unpacked_leaves % 10))
	fprintf (stderr, "#");
}


static void unpack_full_block (int dev, int blocksize)
{
    static int full_blocks_unpacked = 0;
    __u32 block;
    reiserfs_bh_t * bh;

    fread_le32 (&block);

    if (verbose)
	fprintf (stderr, "full #%d\n", block);

    bh = reiserfs_buffer_open (dev, block, blocksize);
    if (!bh)
	misc_die ("unpack_full_block: reiserfs_buffer_open failed");

    fread (bh->b_data, bh->b_size, 1, stdin);

    if (reiserfs_node_type (bh) == NT_SUPER && !what_unpacked) {
	unsigned long blocks;
	reiserfs_bh_t * tmp;
	
	blocks = reiserfs_sb_get_blocks ((reiserfs_sb_t *)(bh->b_data));
	fprintf (stderr, "There were %lu blocks on the device\n", blocks);
	what_unpacked = reiserfs_bitmap_create (blocks);

	/* make file as long as filesystem is */
	tmp = reiserfs_buffer_open (dev, blocks - 1, blocksize);
	reiserfs_buffer_mkdirty (tmp);
	reiserfs_buffer_mkuptodate (tmp, 0);
	reiserfs_buffer_write (tmp);
	reiserfs_buffer_close (tmp);
    }

    reiserfs_buffer_mkuptodate (bh, 1);
    reiserfs_buffer_mkdirty (bh);
    reiserfs_buffer_write (bh);
/*
    if (reiserfs_fs_block(fs, bh->b_blocknr) == BT_UNKNOWN)
	data_blocks_unpacked ++;
*/
    reiserfs_buffer_close (bh);

    if (what_unpacked)
	reiserfs_bitmap_set_bit (what_unpacked, block);
    /*unpacked ++;*/

    if (!(++ full_blocks_unpacked % 50))
	fprintf (stderr, ".");
}


/* just skip bitmaps of unformatted nodes */
static void unpack_unformatted_bitmap (int dev, int blocksize)
{
    __u16 bmap_num;
    __u32 block_count;
    int i;
    char * buf;
 
    fread_le16 (&bmap_num);
    fread_le32 (&block_count);
    
    buf = malloc (blocksize);
    if (!buf)
	reiserfs_panic ("unpack_unformatted_bitmap: malloc failed: %m");

    for (i = 0; i < bmap_num; i ++) {
	if (fread (buf, blocksize, 1, stdin) != 1)
	    reiserfs_panic ("unpack_unformatted_bitmap: "
			    "could not read bitmap #%d: %m", i);
    }
    free (buf);
}


// read packed reiserfs partition metadata from stdin
void unpack_partition (int fd, int jfd)
{
    __u32 magic32;
    long position;
    __u16 magic16;
    __u16 blocksize;
    int dev = fd;
    
    fread_le32 (&magic32);
    if (magic32 != REISERFS_SUPER_MAGIC)
	misc_die ("unpack_partition: reiserfs magic number (0x%x) not found - %x\n",
             REISERFS_SUPER_MAGIC, magic32);
    
    fread_le16 (&blocksize);
    
    if (verbose)
	fprintf (stderr, "Blocksize %d\n", blocksize);
    
    while (!feof (stdin)) {
	char c[2];

	fread (c, 1, 1, stdin);
	switch (c[0]) {
	case '.':
	    if (verbose)
		fprintf (stderr, "\".\" skipped\n");
	    continue;

	case '1':
	    fread (c, 1, 1, stdin); /* that was 100%, read in first 0 */
	case '2':
	case '4':
	case '6':
	case '8':
	    fread (c, 1, 1, stdin);
	case '0':
	    fread (c + 1, 1, 1, stdin); /* read % */
		
	    if (c[0] != '0' || c[1] != '%')
		misc_die ("0%% expected\n");

	    if (verbose)
		fprintf (stderr, "0%% skipped\n");
	    continue;
	}

	fread (c + 1, 1, 1, stdin);
	magic16 = le16_to_cpu(*(__u16 *)c);
	/*fread16 (&magic16);*/
	
	switch (magic16 & 0xff) {
	case LEAF_START_MAGIC:
	    leaves ++;
	    unpack_leaf (dev, reiserfs_hash_func (magic16 >> 8), blocksize);
	    break;
	    
	case SEPARATED_JOURNAL_START_MAGIC:
	    if (Default_journal)
		misc_die ("file name for separated journal has to be specified");	
	    dev = jfd;
	    break;    

	case SEPARATED_JOURNAL_END_MAGIC:
	    dev = fd;
	    break;
	    
	case FULL_BLOCK_START_MAGIC:
	    full ++;
	    unpack_full_block (dev, blocksize);
	    break;

	case UNFORMATTED_BITMAP_START_MAGIC:
	    fprintf (stderr, "\nBitmap of unformatted - ignored\n");
	    unpack_unformatted_bitmap (dev, blocksize);
	    break;
	    
	case END_MAGIC:
	    goto out;

	default:
	    position = ftell(stdin);
	    if (position == ~(long)0)
		misc_die ("unpack_partition: bad magic found - %x", magic16 & 0xff);
	    else
		misc_die ("unpack_partition: bad magic found - %x, position %lu", 
		    magic16 & 0xff, ftell(stdin));
	}
    }
out:
    fprintf (stderr, "Unpacked %d leaves, %d full blocks\n", leaves, full);


    /*    fclose (block_list);*/
}


int do_unpack(char *host, char *j_filename, char *filename, int verbose) {
    int fd, fdj = -2;
    struct rlimit lim = {RLIM_INFINITY, RLIM_INFINITY};

    if (filename == NULL)
	    filename = ".bitmap";
    
    if (j_filename)
	    Default_journal = 0;
    
    /* with this 2.4.0-test9's file_write does not send SIGXFSZ */
    if (setrlimit (RLIMIT_FSIZE, &lim)) {
	fprintf  (stderr, "sertlimit failed: %m\n");
    }

    if (util_device_mounted(host) > 0) {
	fprintf(stderr, "%s seems mounted, umount it first\n", host);
	return 0;
    }
  
    fd = open (host, O_RDWR 
#ifdef O_LARGEFILE
	       | O_LARGEFILE
#endif
	       );
    if (fd == -1) {
	perror ("open failed");
	return 0;
    }

    if (!Default_journal) {
	fdj = open (j_filename, O_RDWR 
#ifdef O_LARGEFILE
		    | O_LARGEFILE
#endif
		    );
	if (fdj == -1) {
	    perror ("open failed");
	    return 0;
	}
    }

    unpack_partition (fd, fdj);

    if (what_unpacked && filename) {
        FILE * file = util_file_open(filename, "w+");
        reiserfs_bitmap_save (file,  what_unpacked);
        fclose (file);
    }

    close (fd);
    if (!Default_journal)
	close (fdj);	
    return 0;
}
