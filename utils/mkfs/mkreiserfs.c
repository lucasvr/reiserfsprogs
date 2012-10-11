/*
 * Copyright 1996-2004 by Hans Reiser, licensing governed by 
 * reiserfsprogs/README
 */

/* mkreiserfs is very simple. It skips first 64k of device, and then 
   writes the super block, the needed amount of bitmap blocks (this 
   amount is calculated based on file system size), and root block. 
   Bitmap policy is primitive: it assumes, that device does not have 
   unreadable blocks, and it occupies first blocks for super, bitmap 
   and root blocks. bitmap blocks are interleaved across the disk, 
   mainly to make resizing faster. */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "reiserfs/libreiserfs.h"
#include "misc/device.h"
#include "util/device.h"
#include "util/misc.h"
#include "util/credits.h"
#include "util/print.h"
#include "util/badblock.h"

#include <getopt.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>

#ifdef HAVE_UNAME
# include <sys/utsname.h>
#endif

#if defined(HAVE_LIBUUID) && defined(HAVE_UUID_UUID_H)
#  include <uuid/uuid.h>
#endif


char *program_name;

static void message( const char * fmt, ... ) 
	__attribute__ ((format (printf, 1, 2)));

static void message( const char * fmt, ... ) {
    char buf[4096];
    va_list args;
	
    buf[0] = '\0';
    va_start( args, fmt );
    vsprintf( buf, fmt, args );
    va_end( args );

    if( buf[0] != '\0') {
	    fprintf( stderr, "%s: %s\n", program_name, buf );
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
	"  -B | --badblocks file            store all bad blocks given in file on the fs\n"
	"  -h | --hash rupasov|tea|r5       hash function to use by default\n"
	"  -u | --uuid UUID                 store UUID in the superblock\n"
	"  -l | --label LABEL               store LABEL in the superblock\n"
	"  --format 3.5|3.6                 old 3.5 format or newer 3.6\n"
	"  -f | --force                     specified once, make mkreiserfs the whole\n"
	"                                   disk, not block device or mounted partition;\n"
	"                                   specified twice, do not ask for confirmation\n"
	"  -q | --quiet                     quiet work without messages, progress and\n"
	"                                   questions. Useful if run in a script. For use\n" 
	"                                   by end users only.\n"
	"  -d | --debug                     print debugging information during mkreiser\n"
	"  -V                               print version and exit\n",
	program_name);
    exit (1);
}


int Create_default_journal = 1;
int Block_size = 4096;

/* size of journal + 1 block for journal header */
unsigned long Journal_size = 0;
int Max_trans_size = 0; //JOURNAL_TRANS_MAX;
int Hash = DEFAULT_HASH;
int Offset = 0;
char * Format;
char UUID[16];
char * LABEL = NULL;
char * badblocks_file;

enum mkfs_mode {
    DEBUG_MODE = 1 << 0,
    QUIET_MODE = 1 << 1,
    DO_NOTHING = 1 << 2
};

int mode;

/* form super block (old one) */
static void make_super_block (reiserfs_filsys_t * fs)
{
    reiserfs_sb_set_umount (fs->fs_ondisk_sb, FS_CLEANLY_UMOUNTED);
    reiserfs_sb_set_height (fs->fs_ondisk_sb, 2);
    reiserfs_sb_set_hash (fs->fs_ondisk_sb, Hash);
    if (fs->fs_format == REISERFS_FORMAT_3_6) {
#if defined(HAVE_LIBUUID) && defined(HAVE_UUID_UUID_H)
        if (uuid_is_null(UUID))
	    uuid_generate(UUID);

	memcpy (fs->fs_ondisk_sb->s_uuid, UUID, 16);
#endif
	if (LABEL != NULL) {
	    if (strlen (LABEL) > 16)
	        reiserfs_warning (stderr, "\nSpecified LABEL is longer then 16 "
		"characters, will be truncated\n\n");
	    strncpy ((char *)fs->fs_ondisk_sb->s_label, LABEL, 16);
	}
	reiserfs_sb_mkflag (fs->fs_ondisk_sb, reiserfs_attrs_cleared);
    }

    if (!reiserfs_super_jr_magic (fs->fs_ondisk_sb) ||
	strcmp (fs->fs_file_name, fs->fs_j_file_name))
	/* either standard journal (and we leave all new fields to be 0) or
	   journal is created on separate device so there is no space on data
	   device which can be used as a journal */
	reiserfs_sb_set_reserved (fs->fs_ondisk_sb, 0);
    else
	reiserfs_sb_set_reserved (fs->fs_ondisk_sb,
		reiserfs_jp_get_size (reiserfs_sb_jp (fs->fs_ondisk_sb)) + 1);

    if (fs->fs_badblocks_bm)
	reiserfs_sb_set_free(fs->fs_ondisk_sb, reiserfs_sb_get_free(fs->fs_ondisk_sb) - 
			   fs->fs_badblocks_bm->bm_set_bits);
}


/* wipe out first 64 k of a device and both possible reiserfs super block */
static void invalidate_other_formats (int dev)
{
    reiserfs_bh_t * bh;
    
    bh = reiserfs_buffer_read (dev, 0, 64 * 1024);
    if (!bh) {
	    reiserfs_exit(1, "Unable to read first blocks of the device");
    }
#if defined(__sparc__) || defined(__sparc_v9__)
    memset (bh->b_data + 1024, 0, bh->b_size - 1024);
#else
    memset (bh->b_data, 0, bh->b_size);
#endif
    reiserfs_buffer_mkuptodate (bh, 1);
    reiserfs_buffer_mkdirty (bh);
    reiserfs_buffer_write (bh);
    reiserfs_buffer_close (bh);
}


void zero_journal (reiserfs_filsys_t * fs)
{
    unsigned long start, len, done;
    reiserfs_bh_t * bh;
    unsigned int i;

    fprintf (stdout, "Initializing journal - ");

    start = reiserfs_jp_get_start (reiserfs_sb_jp (fs->fs_ondisk_sb));
    len = reiserfs_jp_get_size (reiserfs_sb_jp (fs->fs_ondisk_sb));

    done = 0;
    for (i = 0; i < len; i ++) {
        util_misc_progress (stdout, &done, len, 1, 2);
        bh = reiserfs_buffer_open (fs->fs_journal_dev, start + i, fs->fs_blocksize);
	if (!bh) {
		reiserfs_exit(1, "zero_journal: reiserfs_buffer_open failed");
	}
        memset (bh->b_data, 0, bh->b_size);
        reiserfs_buffer_mkdirty (bh);
        reiserfs_buffer_mkuptodate (bh, 1);
        reiserfs_buffer_write (bh);
        reiserfs_buffer_close (bh);
    }

    fprintf (stdout, "\n");
    fflush (stdout);
}


/* this only sets few first bits in bitmap block. Fills not initialized fields
   of super block (root block and bitmap block numbers) */
static void make_bitmap (reiserfs_filsys_t * fs) {
    reiserfs_sb_t * sb;
    unsigned int i;
    unsigned long block;
    unsigned long count, bmap_nr;
    int marked;
    
    sb = fs->fs_ondisk_sb;
    count = reiserfs_sb_get_blocks (sb);
    marked = 0;

    /* mark skipped area and super block */
    for (i = 0; i <= fs->fs_super_bh->b_blocknr; i ++) {
	reiserfs_bitmap_set_bit (fs->fs_bitmap2, i);
	marked ++;
    }
    
    if (fs->fs_badblocks_bm) {
	for (i = 0; i < count; i ++) {
	    if (reiserfs_bitmap_test_bit (fs->fs_badblocks_bm, i)) {
		reiserfs_bitmap_set_bit (fs->fs_bitmap2, i);
		marked ++;
	    }
	}
    }

    /* mark bitmaps as used */
    block = fs->fs_super_bh->b_blocknr + 1;
    
    bmap_nr = reiserfs_bmap_nr(count, fs->fs_blocksize);

    for (i = 0; i < bmap_nr; i ++) {
	reiserfs_bitmap_set_bit (fs->fs_bitmap2, block);
	marked ++;
	if (reiserfs_bitmap_spread (fs))
	    block = (block / (fs->fs_blocksize * 8) + 1) * 
		    (fs->fs_blocksize * 8);
	else
	    block ++;
    }

    if (!reiserfs_journal_hostsize (fs->fs_ondisk_sb))
	/* root block follows directly super block and first bitmap */
	block = fs->fs_super_bh->b_blocknr + 1 + 1;
    else {
	/* makr journal blocks as used */
	for (i = 0; i <= reiserfs_jp_get_size (reiserfs_sb_jp (sb)); i ++) {
	    reiserfs_bitmap_set_bit (fs->fs_bitmap2,
				     i + reiserfs_jp_get_start (reiserfs_sb_jp (sb)));
	    marked ++;
	}
	block = reiserfs_jp_get_start (reiserfs_sb_jp (sb)) + i;
    }

    /*get correct block - not journal nor bitmap*/
    while (reiserfs_journal_block (fs, block) || reiserfs_bitmap_block (fs, block)) {
	block++;
    }

    while ((block < count) && reiserfs_bitmap_test_bit (fs->fs_bitmap2, block))
    {
    	block++;
    }

    if (block >= count)
    	reiserfs_exit(1, "mkreiserfs: too many bad blocks");

    reiserfs_bitmap_set_bit (fs->fs_bitmap2, block);
    marked ++;

    reiserfs_sb_set_root (sb, block);
    reiserfs_sb_set_free (sb, count - marked);
}

static void set_root_dir_nlink (reiserfs_ih_t *ih, void *sd) {
    __u32 nlink;

    nlink = 3;
    reiserfs_stat_set_nlink (ih, sd, &nlink);
}

/* form the root block of the tree (the block head, the item head, the
   root directory) */
static void make_root_block (reiserfs_filsys_t * fs)
{
    reiserfs_sb_t * sb;
    reiserfs_bh_t * bh;


    sb = fs->fs_ondisk_sb;
    /* get memory for root block */
    bh = reiserfs_buffer_open (fs->fs_dev, reiserfs_sb_get_root (sb), 
			       reiserfs_sb_get_blksize (sb));
    
    if (!bh) {
	    reiserfs_exit(1, "reiserfs_buffer_open failed");
    }

    reiserfs_buffer_mkuptodate (bh, 1);

    reiserfs_leaf_mkempty (bh);
    reiserfs_tree_root (fs, set_root_dir_nlink, 0);
    reiserfs_buffer_close (bh);

    reiserfs_objmap_set (fs, REISERFS_ROOT_PARENT_OBJECTID);
    reiserfs_objmap_set (fs, REISERFS_ROOT_OBJECTID);
}



static void report (reiserfs_filsys_t * fs, char * j_filename)
{
    reiserfs_sb_t * sb = (reiserfs_sb_t *)(fs->fs_super_bh->b_data);

    struct stat st;
    dev_t rdev;

    if (!reiserfs_super_magic (sb))
	    return;

    if (fstat (fs->fs_super_bh->b_dev, &st) == -1) {
	    /*reiserfs_warning (stderr, "fstat failed: %s\n", strerror(errno));*/
	    rdev = 0;
    } else	
	    rdev = st.st_rdev;

    if (mode & DEBUG_MODE) {
	    reiserfs_warning (stdout, "Block %lu (0x%x) contains super block. ",
			      fs->fs_super_bh->b_blocknr, rdev);
    }
    switch (reiserfs_super_format (sb)) {
    case REISERFS_FORMAT_3_5:
	    reiserfs_warning (stdout, " Format 3.5 with ");
	    break;
    case REISERFS_FORMAT_3_6:
	    reiserfs_warning (stdout, "Format 3.6 with ");
	    break;
    }
    if (reiserfs_super_jr_magic (sb))
	    reiserfs_warning (stdout, "non-");
    reiserfs_warning (stdout, "standard journal\n");
    reiserfs_warning (stdout, "Count of blocks on the device: %u\n", 
		      reiserfs_sb_get_blocks (sb));
    reiserfs_warning (stdout, "Number of blocks consumed by mkreiserfs "
		      "formatting process: %u\n", reiserfs_sb_get_blocks (sb) 
		      - reiserfs_sb_get_free (sb));
    if (mode & DEBUG_MODE)
	    reiserfs_warning (stdout, "Free blocks: %u\n", 
			      reiserfs_sb_get_free (sb));
    reiserfs_warning (stdout, "Blocksize: %d\n", reiserfs_sb_get_blksize (sb));
    reiserfs_warning (stdout, "Hash function used to sort names: %s\n",
		      reiserfs_hash_name (reiserfs_sb_get_hash (sb)));
    if (mode & DEBUG_MODE) {
        reiserfs_warning (stdout, "Number of bitmaps: %u\n", reiserfs_sb_get_bmaps (sb));
        reiserfs_warning (stdout, "Root block: %u\n", reiserfs_sb_get_root (sb));
        reiserfs_warning (stdout, "Tree height: %d\n", reiserfs_sb_get_height (sb));
        reiserfs_warning (stdout, "Objectid map size %d, max %d\n", 
			  reiserfs_sb_get_mapcur (sb), reiserfs_sb_get_mapmax (sb));
        reiserfs_warning (stdout, "Journal parameters:\n");
        reiserfs_journal_print_params (stdout, reiserfs_sb_jp (sb));
    } else {
        if (j_filename && strcmp (j_filename, fs->fs_file_name)) 
		reiserfs_warning (stdout, "Journal Device [0x%x]\n", 
				  reiserfs_jp_get_dev (reiserfs_sb_jp (sb)));
	
        reiserfs_warning (stdout, "Journal Size %u blocks (first block %u)\n",
			  reiserfs_jp_get_size (reiserfs_sb_jp (sb)) + 1,
			  reiserfs_jp_get_start (reiserfs_sb_jp (sb)));
        reiserfs_warning (stdout, "Journal Max transaction length %u\n", 
			  reiserfs_jp_get_tlen (reiserfs_sb_jp (sb)));
    }

    if (j_filename && strcmp (j_filename, fs->fs_file_name)) {
	    reiserfs_warning (stdout, "Space on this device reserved by journal: "
			      "%u\n", reiserfs_sb_get_reserved (sb));
    }
    
    if (mode & DEBUG_MODE) {
	reiserfs_warning (stdout, "Filesystem state 0x%x\n", 
			  reiserfs_sb_get_state (sb));
	reiserfs_warning (stdout, "sb_version %u\n", 
			  reiserfs_sb_get_version (sb));
    }

    if (reiserfs_super_format (sb) == REISERFS_FORMAT_3_6) {
        reiserfs_warning (stdout, "inode generation number: %u\n", 
			  reiserfs_sb_get_gen (sb));
        reiserfs_warning (stdout, "UUID: %U\n", sb->s_uuid);
        if (strcmp ((char *)sb->s_label, ""))
		reiserfs_warning (stdout, "LABEL: %s\n", sb->s_label);
    }

    return;
}




static void set_hash_function (char * str)
{
    if (!strcmp (str, "tea"))
	    Hash = REISERFS_HASH_TEA;
    else if (!strcmp (str, "rupasov"))
	    Hash = REISERFS_HASH_YURA;
    else if (!strcmp (str, "r5"))
	    Hash = REISERFS_HASH_R5;
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
		    message("wrong reiserfs version specified. "
			    "Using default 3.6 format");
    }
}

static int str2int (char * str)
{
    int val;
    char * tmp;

    val = (int) strtol (str, &tmp, 0);
    
    if (*tmp) {
	reiserfs_exit (1, "%s: strtol is unable to make an integer of %s\n",
		       program_name, str);
    }
    
    return val;
}


static void set_block_size (char * str, int *b_size)
{
    *b_size = str2int (str);
      
    if (!reiserfs_fs_blksize_check (*b_size))
        reiserfs_exit (1, "%s: wrong blocksize %s specified, "
		       "only power of 2 from 512-8192 interval "
		       "are supported", program_name, str);
}


static void set_transaction_max_size (char * str)
{
	Max_trans_size = str2int( str );
}


/* reiserfs_journal_create will check this */
static void set_journal_device_size (char * str)
{
    Journal_size = str2int (str);
/*
    if (Journal_size < JOURNAL_MIN_SIZE)
		misc_die ("%s: wrong journal size specified: %lu. Should be at least %u",
			 program_name, 
			 Journal_size + 1, JOURNAL_MIN_SIZE + 1);
*/
}


/* reiserfs_journal_create will check this */
static void set_offset_in_journal_device (char * str)
{
	Offset = str2int( str );
}


static int is_journal_default (char * name, char * jname, int blocksize)
{
    if (jname && strcmp (name, jname))
	return 0;
    
    if (Journal_size && Journal_size != 
	reiserfs_journal_default(REISERFS_DISK_OFFSET_IN_BYTES / blocksize, 
				 blocksize) + 1)
	/* journal size is set and it is not default size */
	return 0;
    
    if (Max_trans_size && Max_trans_size != JOURNAL_TRANS_MAX)
	return 0;

    return 1;
}

	
		
/* if running kernel is 2.2 - mkreiserfs creates 3.5 format, if 2.4 - 3.6,
   otherwise - mkreiserfs fails */
static int select_format (void)
{
#ifdef HAVE_UNAME
	struct utsname sysinfo;
#endif
	char *release = NULL;
	
	if (Format) {
		if (!strcmp (Format, "3.5"))
			return REISERFS_FORMAT_3_5;

		if (strcmp (Format, "3.6")) {
			message ("Unknown fromat %s specified\n", Format);
			exit (1);
		}
		return REISERFS_FORMAT_3_6;
	}
	
#ifdef HAVE_UNAME
	reiserfs_warning (stdout, "Guessing about desired format.. ");
	
	if (uname (&sysinfo) == -1) {
		message ("could not get system info: %s", strerror(errno));
		exit (1);
	}
	
	reiserfs_warning(stdout, "Kernel %s is running.\n", sysinfo.release);
	release = sysinfo.release;
#endif
	if (!release) {
		message("Failed to detect the running kernel. To create "
			"reiserfs filesystem\nspecify desired format with "
			"--format. Choose 3.5 if you use 2.2 kernel;\n3.6 "
			"if 2.4 or higher.");
		exit(1);
	}
	
	if (strncmp (release, "2.4", 3) >= 0)
		return REISERFS_FORMAT_3_6;

	if (strncmp (release, "2.2", 3)) {
		message( "You should run either 2.2 or 2.4 or higher to be able "
		    "to create reiserfs filesystem or specify desired format with --format");
		exit (1);
	}

    reiserfs_warning(stdout, "Creating filesystem of format 3.5\n");
    return REISERFS_FORMAT_3_5;
}

/* Reads the "CREDITS" file and prints one paragraph from it. */
static void mkfs_print_credit(FILE *out) {
    char *line;
    __u32 num1, num2;
    
    fprintf(out, "A pair of credits:\n");
    
    srandom (time (0));
    
    num1 = random() % CREDITS_COUNT;
    line = credits[num1];
    fprintf(out, "%s\n", line);
    
    while ((num1 == (num2 = random() % CREDITS_COUNT))) {}
    
    line = credits[num2];
    
    fprintf(out, "%s\n", line);
}



int main (int argc, char **argv)
{
    reiserfs_filsys_t * fs;
    int force = 0;
    char * device_name = NULL;
    char * jdevice_name = NULL;
    unsigned long fs_size = 0;
    int c;
    static int flag;

    program_name = strrchr( argv[ 0 ], '/' );
    
    if (program_name)
	program_name++;
    else
	program_name = argv[ 0 ];
    
    if (argc < 2)
	print_usage_and_exit ();
    
    memset(UUID, 0, 16);
    
    while (1) {
		static struct option options[] = {
			{"block-size", required_argument, 0, 'b'},
			{"journal-device", required_argument, 0, 'j'},
			{"journal-size", required_argument, 0, 's'},
			{"transaction-max-size", required_argument, 0, 't'},
			{"journal-offset", required_argument, 0, 'o'},
			{"badblocks", required_argument, 0, 'B'},
			{"hash", required_argument, 0, 'h'},
			{"uuid", required_argument, 0, 'u'},
			{"label", required_argument, 0, 'l'},
			{"format", required_argument, &flag, 1},
			{0, 0, 0, 0}
		};
		int option_index;
      
		c = getopt_long (argc, argv, "b:j:s:t:o:h:u:l:VfdB:q",
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
			badblocks_file = optarg;
			break;
		
		case 'h': /* --hash */
			set_hash_function (optarg);
			break;

		case 'v': /* --format */
			set_reiserfs_version (optarg);
			break;

		case 'V':
			mode = DO_NOTHING;
			break;

		case 'f':
			force ++;
			break;

		case 'd':
			mode |= DEBUG_MODE;
			break;
		
		case 'u':
#if defined(HAVE_LIBUUID) && defined(HAVE_UUID_UUID_H)
			if (uuid_parse(optarg, UUID) < 0) {
			    reiserfs_warning(stderr, "Invalid UUID '%s' is "
					     "specified\n", optarg);
			    return 1;
			}
#else
			message ("Cannot set up the UUID, uuidlib was not "
				 "found by configure.\n");
			return 1;
#endif
			break;
		
		case 'l':
			LABEL = optarg;
			break;
		case 'q':
			mode |= QUIET_MODE;
			break;
		default:
			print_usage_and_exit();
		}
    }

    util_print_banner(program_name);

    mkfs_print_credit(stdout);
    printf("\n");
    
    if (mode & QUIET_MODE)
	fclose(stdout);
    
    if (mode == DO_NOTHING)
	    exit(0);

    /* device to be formatted */
    device_name = argv [optind];
    
    if (optind == argc - 2) {
        /* number of blocks for filesystem is specified */
        fs_size = str2int (argv[optind + 1]);
    } else if (optind == argc - 1) {
        /* number of blocks is not specified */
        if (!(fs_size = misc_device_count_blocks (device_name, Block_size)))
		exit(1);
    } else {
        print_usage_and_exit ();
    }

    if (is_journal_default (device_name, jdevice_name, Block_size))
        Create_default_journal = 1;
    
    if (!(mode & QUIET_MODE) && !util_device_formatable (device_name, force))
        return 1;
	
    if (jdevice_name)
        if (!(mode & QUIET_MODE) && !util_device_formatable (jdevice_name, force))
            return 1;

    fs = reiserfs_fs_create (device_name, select_format(), fs_size, Block_size, 
	Create_default_journal, 1);
    
    if (!fs) {
        return 1;
    }
		
    if (!reiserfs_journal_create (fs, jdevice_name, Offset, Journal_size, 
	Max_trans_size)) 
    {
        return 1;
    }

    if (!(fs->fs_bitmap2 = reiserfs_bitmap_create(fs_size)))
        return 1;

    /* these fill buffers (super block, first bitmap, root block) with
       reiserfs structures */
#if defined(HAVE_LIBUUID) && defined(HAVE_UUID_UUID_H)
    if (!uuid_is_null(UUID) && fs->fs_format != REISERFS_FORMAT_3_6) {
	reiserfs_warning(stderr, "UUID can be specified only with 3.6 format\n");
	return 1;
    }
#endif

    if (badblocks_file) {
	if (util_badblock_load (fs, badblocks_file))
	    exit(1);
    }

    make_super_block (fs);
    make_bitmap (fs);
    make_root_block (fs);
    reiserfs_badblock_flush (fs, 1);

    report (fs, jdevice_name);

    if (!force && !(mode & QUIET_MODE)) {
        fprintf (stderr, "ATTENTION: YOU SHOULD REBOOT AFTER FDISK!\n"
                "\tALL DATA WILL BE LOST ON '%s'", device_name);
        if (jdevice_name && strcmp (jdevice_name, device_name))
            fprintf (stderr, " AND ON JOURNAL DEVICE '%s'", jdevice_name);

        if (!util_user_confirmed (stderr, "!\nContinue (y/n):", "y\n"))
            return 1;
    }


    invalidate_other_formats (fs->fs_dev);

    zero_journal (fs);

    reiserfs_fs_close (fs);

    printf ("Syncing.."); fflush (stdout);
    sync ();
    printf ("ok\n");
 
    if (mode & DEBUG_MODE)
	return 0;
    
    printf("\nTell your friends to use a kernel based on 2.4.18 or "
	"later, and especially not a\nkernel based on 2.4.9, "
	"when you use reiserFS. Have fun.\n\n");
        
    printf("ReiserFS is successfully created on %s.\n", device_name);

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
