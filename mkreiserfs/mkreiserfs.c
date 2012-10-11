/*
 * Copyright 1996-2002 Hans Reiser, licensing governed by ../README
 */

/* mkreiserfs is very simple. It supports only 4k blocks. It skips
   first 64k of device, and then writes the super
   block, the needed amount of bitmap blocks (this amount is calculated
   based on file system size), and root block. Bitmap policy is
   primitive: it assumes, that device does not have unreadable blocks,
   and it occupies first blocks for super, bitmap and root blocks.
   bitmap blocks are interleaved across the disk, mainly to make
   resizing faster. */

//
// FIXME: not 'not-i386' safe. ? Ed
//
#define _GNU_SOURCE

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <asm/types.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/vfs.h>
#include <time.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <linux/major.h>
#include <sys/stat.h>
#include <linux/kdev_t.h>
#include <sys/utsname.h>
#include <getopt.h>
#include <stdarg.h>

#include "io.h"
#include "misc.h"
#include "reiserfs_lib.h"
#include "../include/config.h"
#include "../version.h"


char *program_name;

static void message( const char * fmt, ... ) 
	__attribute__ ((format (printf, 1, 2)));

	static void message( const char * fmt, ... ) 
{
    char *buf;
    va_list args;
	
    buf = NULL;
    va_start( args, fmt );
    vasprintf( &buf, fmt, args );
    va_end( args );

    if( buf ) {
	    fprintf( stderr, "%s: %s\n", program_name, buf );
	    free( buf );
    }
}

static void print_usage_and_exit(void)
{
	fprintf(stderr, "Usage: %s [options] "
			" device [block-count]\n"
			"\n"
			"Options:\n\n"
			"  -b | --block-size N              size of file-system block, in bytes\n"
			"  -j | --journal-device FILE       path to separate device to hold journal\n"
			"  -s | --journal-size N            size of the journal in blocks\n"
			"  -o | --journal-offset N          offset of the journal from the start of\n"
			"                                   the separate device, in blocks\n"
			"  -t | --transaction-max-size N    maximal size of transaction, in blocks\n"
			"  -h | --hash rupasov|tea|r5       hash function to use by default\n"
			"  -u | --uuid UUID                 store UUID in the superblock\n"
			"  -l | --label LABEL               store LABEL in the superblock\n"
			"  --format 3.5|3.6                 old 3.5 format or newer 3.6\n"
			"  -f | --force                     specified once, make mkreiserfs the whole\n"
			"                                   disk, not block device or mounted partition;\n"
			"                                   specified twice, do not ask for confirmation\n"
			"  -d | --debug                     print debugging information during mkreiser\n"
			"  -V                               print version and exit\n",
			program_name);
	exit (1);
}

//			"  -B badblocks-file                list of all bad blocks on the fs\n"

int Create_default_journal = 1;
int Block_size = 4096;
int DEBUG_MODE = 0;

/* size of journal + 1 block for journal header */
unsigned long Journal_size = 0;
int Max_trans_size = 0; //JOURNAL_TRANS_MAX;
int Hash = DEFAULT_HASH;
int Offset = 0;
char * Format;
unsigned char UUID[16];
unsigned char * LABEL = NULL;
char * badblocks_file;


/* form super block (old one) */
static void make_super_block (reiserfs_filsys_t * fs)
{
    set_sb_umount_state (fs->fs_ondisk_sb, REISERFS_CLEANLY_UMOUNTED);
    set_sb_tree_height (fs->fs_ondisk_sb, 2);
    set_sb_hash_code (fs->fs_ondisk_sb, Hash);
    if (fs->fs_format == REISERFS_FORMAT_3_6) {
        if (!uuid_is_correct (UUID) && generate_random_uuid (UUID))
	    reiserfs_warning (stdout, "failed to genetate UUID\n");

	memcpy (fs->fs_ondisk_sb->s_uuid, UUID, 16);
	if (LABEL != NULL) {
	    if (strlen (LABEL) > 16)
	        reiserfs_warning (stderr, "\nSpecified LABEL is longer then 16 characters, will be truncated\n\n");
	    strncpy (fs->fs_ondisk_sb->s_label, LABEL, 16);
	}
	set_sb_v2_flag (fs->fs_ondisk_sb, reiserfs_attrs_cleared);
    }

    if (!is_reiserfs_jr_magic_string (fs->fs_ondisk_sb) ||
	strcmp (fs->fs_file_name, fs->fs_j_file_name))
	/* either standard journal (and we leave all new fields to be 0) or
	   journal is created on separate device so there is no space on data
	   device which can be used as a journal */
	set_sb_reserved_for_journal (fs->fs_ondisk_sb, 0);
    else
	set_sb_reserved_for_journal (fs->fs_ondisk_sb,
		get_jp_journal_size (sb_jp (fs->fs_ondisk_sb)) + 1);
}


/* wipe out first 64 k of a device and both possible reiserfs super block */
static void invalidate_other_formats (int dev)
{
    struct buffer_head * bh;
    
    bh = bread (dev, 0, 64 * 1024);
	if (!bh)
		die ("Unable to read first blocks of the device");
#if defined(__sparc__) || defined(__sparc_v9__)
    memset (bh->b_data + 1024, 0, bh->b_size - 1024);
#else
    memset (bh->b_data, 0, bh->b_size);
#endif
    mark_buffer_uptodate (bh, 1);
    mark_buffer_dirty (bh);
    bwrite (bh);
    brelse (bh);
}


void zero_journal (reiserfs_filsys_t * fs)
{
    int i;
    struct buffer_head * bh;
    unsigned long done;
    unsigned long start, len;


    fprintf (stderr, "Initializing journal - ");

    start = get_jp_journal_1st_block (sb_jp (fs->fs_ondisk_sb));
    len = get_jp_journal_size (sb_jp (fs->fs_ondisk_sb));

    done = 0;
    for (i = 0; i < len; i ++) {
        print_how_far (stderr, &done, len, 1, 1/*be quiet*/);
        bh = getblk (fs->fs_journal_dev, start + i, fs->fs_blocksize);
		if (!bh)
			die ("zero_journal: getblk failed");
        memset (bh->b_data, 0, bh->b_size);
        mark_buffer_dirty (bh);
        mark_buffer_uptodate (bh, 1);
        bwrite (bh);
        brelse (bh);
    }

    fprintf (stderr, "\n");
    fflush (stderr);
}


/* this only sets few first bits in bitmap block. Fills not initialized fields
   of super block (root block and bitmap block numbers) */
static void make_bitmap (reiserfs_filsys_t * fs)
{
    struct reiserfs_super_block * sb = fs->fs_ondisk_sb;
    int i;
    unsigned long block;
    int marked;
    

    marked = 0;

    /* mark skipped area and super block */
    for (i = 0; i <= fs->fs_super_bh->b_blocknr; i ++) {
		reiserfs_bitmap_set_bit (fs->fs_bitmap2, i);
		marked ++;
    }


    if (fs->fs_badblocks_bm) {
	for (i = 0; i < get_sb_block_count (sb); i ++) {
	    if (reiserfs_bitmap_test_bit (fs->fs_badblocks_bm, i)) {
		reiserfs_bitmap_set_bit (fs->fs_bitmap2, i);
		marked ++;
	    }
	}
    }

    /* mark bitmaps as used */
    block = fs->fs_super_bh->b_blocknr + 1;

    for (i = 0; i < get_sb_bmap_nr (sb); i ++) {
		reiserfs_bitmap_set_bit (fs->fs_bitmap2, block);
		marked ++;
		if (spread_bitmaps (fs))
			block = (block / (fs->fs_blocksize * 8) + 1) * (fs->fs_blocksize * 8);
		else
			block ++;
    }

	if (!get_size_of_journal_or_reserved_area (fs->fs_ondisk_sb))
		/* root block follows directly super block and first bitmap */
		block = fs->fs_super_bh->b_blocknr + 1 + 1;
    else {
		/* makr journal blocks as used */
		for (i = 0; i <= get_jp_journal_size (sb_jp (sb)); i ++) {
			reiserfs_bitmap_set_bit (fs->fs_bitmap2,
									 i + get_jp_journal_1st_block (sb_jp (sb)));
			marked ++;
		}
		block = get_jp_journal_1st_block (sb_jp (sb)) + i;
    }

    /*get correct block - not journal nor bitmap*/
    while (block_of_journal (fs, block) || block_of_bitmap (fs, block)) {
        block++;
    }

    while ((block < get_sb_block_count (sb)) && reiserfs_bitmap_test_bit (fs->fs_bitmap2, block)) {
    	block++;
    }

    if (block >= get_sb_block_count (sb))
    	die ("mkreiserfs: too many bad blocks");

    reiserfs_bitmap_set_bit (fs->fs_bitmap2, block);
    marked ++;

    set_sb_root_block (sb, block);
    set_sb_free_blocks (sb, get_sb_block_count (sb) - marked);
}


static void set_root_dir_nlink (struct item_head * ih, void * sd)
{
    __u32 nlink;

    nlink = 3;
    set_sd_nlink (ih, sd, &nlink);
}


/* form the root block of the tree (the block head, the item head, the
   root directory) */
static void make_root_block (reiserfs_filsys_t * fs)
{
    struct reiserfs_super_block * sb;
    struct buffer_head * bh;


    sb = fs->fs_ondisk_sb;
    /* get memory for root block */
    bh = getblk (fs->fs_dev, get_sb_root_block (sb), get_sb_block_size (sb));
    if (!bh)
		die ("make_root_block: getblk failed");

    mark_buffer_uptodate (bh, 1);

    make_empty_leaf (bh);
    make_sure_root_dir_exists (fs, set_root_dir_nlink, 0);
    brelse (bh);


    /**/
    mark_objectid_used (fs, REISERFS_ROOT_PARENT_OBJECTID);
    mark_objectid_used (fs, REISERFS_ROOT_OBJECTID);
    
}



static void report (reiserfs_filsys_t * fs, char * j_filename)
{
//    print_block (stdout, fs, fs->fs_super_bh);
    struct reiserfs_super_block * sb = (struct reiserfs_super_block *)(fs->fs_super_bh->b_data);
    struct stat st;
    dev_t rdev;

    if (!is_any_reiserfs_magic_string (sb))
		return;

    if (fstat (fs->fs_super_bh->b_dev, &st) == -1) {
		/*reiserfs_warning (stderr, "fstat failed: %m\n");*/
		rdev = 0;
    } else	
		rdev = st.st_rdev;

    if (DEBUG_MODE) {
        reiserfs_warning (stdout, "Block %lu (0x%x) contains super block. ",
						  fs->fs_super_bh->b_blocknr, rdev);
    }
    switch (get_reiserfs_format (sb)) {
    case REISERFS_FORMAT_3_5:
		reiserfs_warning (stdout, " Format 3.5 with ");
		break;
    case REISERFS_FORMAT_3_6:
		reiserfs_warning (stdout, "Format 3.6 with ");
		break;
    }
    if (is_reiserfs_jr_magic_string (sb))
		reiserfs_warning (stdout, "non-");
    reiserfs_warning (stdout, "standard journal\n");
    reiserfs_warning (stdout, "Count of blocks on the device: %u\n", get_sb_block_count (sb));
    reiserfs_warning (stdout, "Number of blocks consumed by mkreiserfs formatting process: %u\n", 
					  get_sb_block_count (sb) - get_sb_free_blocks (sb));    
    if (DEBUG_MODE)
        reiserfs_warning (stdout, "Free blocks: %u\n", get_sb_free_blocks (sb));
    reiserfs_warning (stdout, "Blocksize: %d\n", get_sb_block_size (sb));
    reiserfs_warning (stdout, "Hash function used to sort names: %s\n",
					  code2name (get_sb_hash_code (sb)));
    if (DEBUG_MODE) {
        reiserfs_warning (stdout, "Number of bitmaps: %u\n", get_sb_bmap_nr (sb));
        reiserfs_warning (stdout, "Root block: %u\n", get_sb_root_block (sb));
        reiserfs_warning (stdout, "Tree height: %d\n", get_sb_tree_height (sb));
        reiserfs_warning (stdout, "Objectid map size %d, max %d\n", get_sb_oid_cursize (sb),
						  get_sb_oid_maxsize (sb));
        reiserfs_warning (stdout, "Journal parameters:\n");
        print_journal_params (stdout, sb_jp (sb));
    } else {
        if (j_filename && strcmp (j_filename, fs->fs_file_name)) 
            reiserfs_warning (stdout, "Journal Device [0x%x]\n", get_jp_journal_dev (sb_jp (sb)));
        reiserfs_warning (stdout, "Journal Size %u blocks (first block %u)\n",
						  get_jp_journal_size (sb_jp (sb)) + 1,
						  get_jp_journal_1st_block (sb_jp (sb)));
        reiserfs_warning (stdout, "Journal Max transaction length %u\n", 
						  get_jp_journal_max_trans_len (sb_jp (sb)));
    }

    if (j_filename && strcmp (j_filename, fs->fs_file_name)) {
        reiserfs_warning (stdout, "Space on this device reserved by journal: %u\n", 
						  get_sb_reserved_for_journal (sb));
    }
    
    if (DEBUG_MODE) {
		reiserfs_warning (stdout, "Filesystem state 0x%x\n", get_sb_fs_state (sb));
		reiserfs_warning (stdout, "sb_version %u\n", get_sb_version (sb));
    }

    if (get_reiserfs_format (sb) == REISERFS_FORMAT_3_6) {
        reiserfs_warning (stdout, "inode generation number: %u\n", get_sb_v2_inode_generation (sb));
        reiserfs_warning (stdout, "UUID: %U\n", sb->s_uuid);
        if (strcmp (sb->s_label, ""))
            reiserfs_warning (stdout, "LABEL: %s\n", sb->s_label);
    }

    return;
}




static void set_hash_function (char * str)
{
    if (!strcmp (str, "tea"))
		Hash = TEA_HASH;
    else if (!strcmp (str, "rupasov"))
		Hash = YURA_HASH;
    else if (!strcmp (str, "r5"))
		Hash = R5_HASH;
    else
		message("wrong hash type specified. Using default");
}


static void set_reiserfs_version (char * str)
{
    if (!strcmp (str, "3.5"))
		Format = "3.5";
    else  {
		Format = "3.6";
		if (strcmp (str, "3.6"))
			message("wrong reiserfs version specified. Using default 3.6 format");
    }
}

static int str2int (char * str)
{
    int val;
    char * tmp;

    val = (int) strtol (str, &tmp, 0);
    if (*tmp)
		die ("%s: strtol is unable to make an integer of %s\n", program_name, str);
    return val;
}


static void set_block_size (char * str, int *b_size)
{
    *b_size = str2int (str);
      
    if (!is_blocksize_correct (*b_size))
        die ("%s: wrong blocksize %s specified, only divisible by 1024 are supported currently",
        	program_name, str);
}


static void set_transaction_max_size (char * str)
{
	Max_trans_size = str2int( str );
}


/* reiserfs_create_journal will check this */
static void set_journal_device_size (char * str)
{
    Journal_size = str2int (str);
/*
    if (Journal_size < JOURNAL_MIN_SIZE)
		die ("%s: wrong journal size specified: %lu. Should be at least %u",
			 program_name, 
			 Journal_size + 1, JOURNAL_MIN_SIZE + 1);
*/
}


/* reiserfs_create_journal will check this */
static void set_offset_in_journal_device (char * str)
{
	Offset = str2int( str );
}


static int is_journal_default (char * name, char * jname, int blocksize)
{
	if (jname && strcmp (name, jname))
		return 0;
	if (Journal_size &&
	    Journal_size != journal_default_size (REISERFS_DISK_OFFSET_IN_BYTES / blocksize, blocksize) + 1)
		/* journal size is set and it is not default size */
		return 0;
	if (Max_trans_size != JOURNAL_TRANS_MAX)
		return 0;
	return 1;
}

	
		
/* if running kernel is 2.2 - mkreiserfs creates 3.5 format, if 2.4 - 3.6,
   otherwise - mkreiserfs fails */
static int select_format (void)
{
	struct utsname sysinfo;


	if (Format) {
		if (!strcmp (Format, "3.5"))
			return REISERFS_FORMAT_3_5;

		if (strcmp (Format, "3.6")) {
			message ("Unknown fromat %s specified\n", Format);
			exit (1);
		}
		return REISERFS_FORMAT_3_6;
	}
	
	message ("Guessing about desired format.. ");
	
	if (uname (&sysinfo) == -1) {
		message ("could not get system info: %m");
		exit (1);
	}
	
	message ("Kernel %s is running.", sysinfo.release);
	if (!strncmp (sysinfo.release, "2.5", 3))
		return REISERFS_FORMAT_3_6;
	
	if (!strncmp (sysinfo.release, "2.4", 3))
		return REISERFS_FORMAT_3_6;

	if (strncmp (sysinfo.release, "2.2", 3)) {
		message( "You should run either 2.4 or 2.2 to be able "
				 "to create reiserfs filesystem or specify desired format with -v");
		exit (1);
	}

    message ("Creating filesystem of format 3.5");
    return REISERFS_FORMAT_3_5;
}


int main (int argc, char **argv)
{
    reiserfs_filsys_t * fs;
    int force;
    char * device_name;
    char * jdevice_name;
    unsigned long fs_size;
    int c;
    static int flag;


    program_name = strrchr( argv[ 0 ], '/' );
    program_name = program_name ? ++ program_name : argv[ 0 ];
    
    print_banner (program_name);

    if (argc < 2)
		print_usage_and_exit ();
    
    force = 0;
    fs_size = 0;
    device_name = 0;
    jdevice_name = 0;


    while (1) {
		static struct option options[] = {
			{"block-size", required_argument, 0, 'b'},
			{"journal-device", required_argument, 0, 'j'},
			{"journal-size", required_argument, 0, 's'},
			{"transaction-max-size", required_argument, 0, 't'},
			{"journal-offset", required_argument, 0, 'o'},
			{"hash", required_argument, 0, 'h'},
			{"uuid", required_argument, 0, 'u'},
			{"label", required_argument, 0, 'l'},
			{"format", required_argument, &flag, 1},
			{0, 0, 0, 0}
		};
		int option_index;
      
		c = getopt_long (argc, argv, "b:j:s:t:o:h:u:l:Vfd",
						 options, &option_index);
		if (c == -1)
			break;
	
		switch (c) {
		case 0:
			if (flag) {
				Format = optarg;
				flag = 0;
			}
			break;
		case 'b': /* --block-size */
			set_block_size (optarg, &Block_size);
			break;

		case 'j': /* --journal-device */
			Create_default_journal = 0;
			jdevice_name = optarg;
			break;

		case 's': /* --journal-size */
			Create_default_journal = 0;
			set_journal_device_size (optarg);	    
			break;

		case 't': /* --transaction-max-size */
			Create_default_journal = 0;
			set_transaction_max_size (optarg);
			break;

		case 'o': /* --offset */
			Create_default_journal = 0;
			set_offset_in_journal_device (optarg);
			break;

		case 'B': /* --badblock-list */
			asprintf (&badblocks_file, "%s", optarg);			
			break;
		
		case 'h': /* --hash */
			set_hash_function (optarg);
			break;

		case 'v': /* --format */
			set_reiserfs_version (optarg);
			break;

		case 'V':
			exit (1);

		case 'f':
			force ++;
			break;

		case 'd':
			DEBUG_MODE = 1;
			break;
		
		case 'u':
			if (set_uuid (optarg, UUID)) {
			    reiserfs_warning(stdout, "wrong UUID specified\n");
			    return 1;
			}
			
			break;
		
		case 'l':
			LABEL = optarg;
			break;
		
		default:
			print_usage_and_exit();
		}
    }


    /* device to be formatted */
    device_name = argv [optind];
    
    if (optind == argc - 2) {
        /* number of blocks for filesystem is specified */
        fs_size = str2int (argv[optind + 1]);
    } else if (optind == argc - 1) {
        /* number of blocks is not specified */
        fs_size = count_blocks (device_name, Block_size);
    } else {
        print_usage_and_exit ();
    }

    if (is_journal_default (device_name, jdevice_name, Block_size))
        Create_default_journal = 1;
    
    if (!Max_trans_size) {
        /* max transaction size has not been specified,
           for blocksize >= 4096 - max transaction size is 1024. For block size < 4096
           - trans max size is decreased proportionally */
        Max_trans_size = JOURNAL_TRANS_MAX;
        if (Block_size < 4096)
            Max_trans_size = JOURNAL_TRANS_MAX / (4096 / Block_size);
    }
	
    if (!can_we_format_it (device_name, force))
        return 1;
	
    if (jdevice_name)
        if (!can_we_format_it (jdevice_name, force))
            return 1;

    fs = reiserfs_create (device_name, select_format (), fs_size, Block_size, Create_default_journal, 1);
    if (!fs) {
        return 1;
    }
		
    if (!reiserfs_create_journal (fs, jdevice_name, Offset, Journal_size, Max_trans_size)) {
        return 1;
    }

    if (!reiserfs_create_ondisk_bitmap (fs)) {
        return 1;
    }

    /* these fill buffers (super block, first bitmap, root block) with
       reiserfs structures */
    if (uuid_is_correct (UUID) && fs->fs_format != REISERFS_FORMAT_3_6) {
	reiserfs_warning(stdout, "UUID can be specified only with 3.6 format\n");
	return 1;
    }

    if (badblocks_file)
	create_badblock_bitmap (fs, badblocks_file);


    make_super_block (fs);
    make_bitmap (fs);
    make_root_block (fs);
    add_badblock_list (fs, 1);
  
    report (fs, jdevice_name);

    if (!force) {
        fprintf (stderr, "ATTENTION: YOU SHOULD REBOOT AFTER FDISK!\n"
                "\tALL DATA WILL BE LOST ON '%s'", device_name);
        if (jdevice_name && strcmp (jdevice_name, device_name))
            fprintf (stderr, " AND ON JOURNAL DEVICE '%s'", jdevice_name);

        if (!user_confirmed (stderr, "!\nContinue (y/n):", "y\n"))
            return 1;
    }


    invalidate_other_formats (fs->fs_dev);

		
    zero_journal (fs);

    reiserfs_close (fs);

    printf ("Syncing.."); fflush (stdout);
    sync ();
    printf ("ok\n");
 
	if (DEBUG_MODE)
		return 0;
	printf ("\nThe Defense Advanced Research Projects Agency (DARPA) is the primary sponsor of"
			"\nReiser4. DARPA does not endorse this project; it merely sponsors it."
			"\n"
			"\nContinuing core development of version 3 is mostly paid for by Hans Reiser from"
			"\nmoney made selling licenses in addition to the GPL to companies who don't want"
			"\nit known that they use ReiserFS as a foundation for their proprietary product."
			"\nAnd my lawyer asked 'People pay you money for this?'.  Yup.  Hee Hee.  Life is"
			"\ngood.  If you buy ReiserFS, you can focus on your value add rather than"
			"\nreinventing an entire FS.  You should buy some free software too...."
			"\n"
			"\nSuSE pays for continuing work on journaling for version 3, and paid for much of"
			"\nthe previous version 3 work.  Reiserfs integration in their distro is"
			"\nconsistently solid."
			"\n"
			"\nMP3.com paid for initial journaling development."
			"\n"
			"\nBigstorage.com contributes to our general fund every month, and has done so for"
			"\nquite a long time."
			"\n"
			"\nThanks to all of those sponsors, including the secret ones.  Without you, Hans"
			"\nwould still have that day job, and the merry band of hackers would be missing"
			"\nquite a few...."
			"\n"
			"\nHave fun.\n"); 
    return 0;
}



/* 
 * Use BSD fomatting.
 * Local variables:
 * c-indentation-style: "bsd"
 * mode-name: "BSDC"
 * c-basic-offset: 4
 * tab-width: 4
 * End:
 */
