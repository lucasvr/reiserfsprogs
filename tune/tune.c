/*
 * Copyright 2002  Hans Reiser
 */
#include "tune.h"

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


reiserfs_filsys_t * fs;

static void print_usage_and_exit(void)
{
    message ("Usage: %s [options] "
	     " device [block-count]\n"
	     "\n"
	     "Options:\n\n"
	     "  -j | --journal-device file\n"
	     "  --journal-new-device file\n"
	     "  -o | --journal-new-offset N\n"
	     "  -s | --journal-new-size N\n"
	     "  -t | --transaction-max-size N\n"
	     "  --no-journal-available\n"
	     "  --make-journal-standard\n"
	     /*"\t-p | --keep-old-journal-param  (keep parametrs from old journal to new one)\n"*/
	     "  -u | --uuid UUID|random\n"
	     "  -l | --label LABEL\n"
	     "  -f | --force \n", program_name);
    exit (1);
}

unsigned long Journal_size = 0;
int Max_trans_size = JOURNAL_TRANS_MAX;
int Offset = 0;
__u16 Options = 0;
int Force = 0;
char * LABEL;
unsigned char UUID[16];

static int should_make_journal_non_standard (int force)
{
    message ("ATTENTION! Filesystem with standard journal found. ");
    check_forcing_ask_confirmation (force);
    return 1;
}

static int should_make_journal_standard (reiserfs_filsys_t * fs, char * j_new_dev_name)
{
    if (!is_reiserfs_jr_magic_string (fs->fs_ondisk_sb))
	return 0;
    
    if (!user_confirmed (stderr, "ATTENTION! Filesystem with non-standard journal "
			 "found. Continue? (y/n):", "y\n")) {
	exit(1);
    }
    /* make sure journal is on main device, it has default size
     and the file system has non-standard magic */

    if (j_new_dev_name) {
	/* new journal was specified - check if it is available */
	if (strcmp (j_new_dev_name, fs->fs_file_name)) {
	    message ("Can not create standard journal on separated device %s",
		     j_new_dev_name);
	    return 0;
	}
	if (Journal_size && (Journal_size != journal_default_size (fs->fs_super_bh->b_blocknr, fs->fs_blocksize))) {
	    message ("Can not create standard journal of the size %lu",
		     Journal_size);
	    return 0;
	}
	if (Max_trans_size && (Max_trans_size != JOURNAL_TRANS_MAX)) {
		message ("Can not create standard journal with the transaction "
			 "max size %u", Max_trans_size);
		return 0;
	}
    }
    else {
	/* new journal was not specified - check ondisk journal params */
	if (get_jp_journal_dev(sb_jp(fs->fs_ondisk_sb))) {
	    message ("Can not create standard journal on separated device [0x%x]",
		    get_jp_journal_dev(sb_jp(fs->fs_ondisk_sb)));
	    return 0;
	}
	if(get_jp_journal_size(sb_jp(fs->fs_ondisk_sb))!= journal_default_size (fs->fs_super_bh->b_blocknr, fs->fs_blocksize)){
	    message ("Can not create standard journal of the size %u",
		     get_jp_journal_size(sb_jp(fs->fs_ondisk_sb)));
	    return 0;
	}
    }
    return 1;
}

static int set_standard_journal_params (reiserfs_filsys_t * fs)
{
    /* ondisk superblock update */

    if (get_sb_version(fs->fs_ondisk_sb) == 0)
	memcpy (fs->fs_ondisk_sb->s_v1.s_magic, REISERFS_3_5_SUPER_MAGIC_STRING,
		strlen (REISERFS_3_5_SUPER_MAGIC_STRING));
    else if (get_sb_version(fs->fs_ondisk_sb) == 2)
	memcpy (fs->fs_ondisk_sb->s_v1.s_magic, REISERFS_3_6_SUPER_MAGIC_STRING,
		strlen (REISERFS_3_6_SUPER_MAGIC_STRING));
    else {
	message ("Can not set standard reiserfs magic: unknown format found %u,"
		 " try reiserfsck first", get_sb_version(fs->fs_ondisk_sb));
	return 0;
    }
    if (get_jp_journal_max_trans_len(sb_jp(fs->fs_ondisk_sb)) != JOURNAL_TRANS_MAX)
	set_jp_journal_max_trans_len(sb_jp(fs->fs_ondisk_sb), JOURNAL_TRANS_MAX);
    if (get_jp_journal_max_batch(sb_jp(fs->fs_ondisk_sb)) != JOURNAL_MAX_BATCH)
	set_jp_journal_max_batch(sb_jp(fs->fs_ondisk_sb), JOURNAL_MAX_BATCH);
    if (get_jp_journal_max_commit_age(sb_jp(fs->fs_ondisk_sb)) != JOURNAL_MAX_COMMIT_AGE)
	set_jp_journal_max_commit_age(sb_jp(fs->fs_ondisk_sb), JOURNAL_MAX_COMMIT_AGE);
    if (get_jp_journal_max_trans_age(sb_jp(fs->fs_ondisk_sb)) != JOURNAL_MAX_TRANS_AGE)
	set_jp_journal_max_trans_age(sb_jp(fs->fs_ondisk_sb), JOURNAL_MAX_TRANS_AGE);
    set_sb_reserved_for_journal (fs->fs_ondisk_sb, 0);
    
    /* journal_header update */
    
    ((struct reiserfs_journal_header *)(fs->fs_jh_bh->b_data)) -> jh_journal =
	*(sb_jp(fs->fs_ondisk_sb));
    return 1;
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


static int str2int (char * str)
{
    int val;
    char * tmp;

    val = (int) strtol (str, &tmp, 0);
    if (*tmp)
	die ("%s: strtol is unable to make an integer of %s\n", program_name, str);
    return val;
}


static void set_transaction_max_size (char * str)
{
    Max_trans_size = str2int( str );
}


/* journal must fit into number of blocks pointed by first bitmap */
static void set_journal_device_size (char * str)
{
    Journal_size = str2int (str) ;
}


static void set_offset_in_journal_device (char * str)
{
    Offset = str2int( str );
}


int main (int argc, char **argv)
{
    reiserfs_filsys_t * fs;
    char * device_name;
    char * jdevice_name;
    char * j_new_device_name;
    int c;
    static int flag;
    struct reiserfs_journal_header * j_head;
    reiserfs_trans_t old, new;
    int Is_journal_or_maxtrans_size_specified = 0;

    program_name = strrchr( argv[ 0 ], '/' );
    program_name = program_name ? ++ program_name : argv[ 0 ];
    
    print_banner (program_name);

    if (argc < 2)
	print_usage_and_exit ();
    
    device_name = 0;
    jdevice_name = 0;
    j_new_device_name = 0;

    while (1) {
	static struct option options[] = {
	    {"journal-device", required_argument, 0, 'j'},
	    {"journal-new-device", required_argument, &flag, OPT_NEW_J},
	    {"journal-new-size", required_argument, 0, 's'},
	    {"transaction-max-size", required_argument, 0, 't'},
	    {"journal-new-offset", required_argument, 0, 'o'},
	    {"no-journal-available", no_argument, &flag, OPT_SKIP_J},
	    /*{"keep-old-journal-param", no_argument, 0, 'p'},*/
	    {"uuid", required_argument, 0, 'u'},
	    {"label", required_argument, 0, 'l'},
	    {"force", no_argument, 0, 'f'},
	    {"really-force", no_argument, &flag, OPT_SUPER_FORCE},
	    {"make-journal-standard", no_argument, &flag, OPT_STANDARD},
	    {0, 0, 0, 0}
	};
	int option_index;
      
	c = getopt_long (argc, argv, "j:s:t:o:fu:l:",
			 options, &option_index);
	if (c == -1)
	    break;
	
	switch (c) {
	case 0:
	    /* long-only optins */
	    if (flag == OPT_NEW_J) {
		Options |= OPT_NEW_J;
		j_new_device_name = optarg;
	    }
	    if (flag == OPT_SKIP_J) {
		Options |= OPT_SKIP_J;
	    }
	    if (flag == OPT_SUPER_FORCE) {
		Options |=OPT_SUPER_FORCE;
	    }
	    if (flag == OPT_STANDARD) {
		Options |=OPT_STANDARD;
	    } 
	    break;
	case 'j': /* --journal-device */
	    jdevice_name = optarg;
	    break;

	case 's': /* --journal-new-size */
	    set_journal_device_size (optarg);
	    Is_journal_or_maxtrans_size_specified = 1;
	    break;
	    
	case 't': /* --transaction-max-size */
	    set_transaction_max_size (optarg);
	    Is_journal_or_maxtrans_size_specified = 1;
	    break;
	    
	case 'o': /* --offset */
	    set_offset_in_journal_device (optarg);
	    break;
	    
	case 'f':
	    /* forces replacing standard journal with non-standard
	       one. Specified more than once - allows to avoid asking for
	       confirmation */
	    Force ++;
	    break;
	case 'u':
	    /* UUID */
	    if (!strcmp(optarg, "random")) {
		if (generate_random_uuid (UUID))
		    message ("failed to genetate UUID\n");
	    } else {
		if (set_uuid (optarg, UUID)) {
		    message ("wrong UUID specified\n");
		    return 1;
		}
	    }
	
            break;
	case 'l':
	    /* LABEL */
	    LABEL = optarg;
            break;
#if 0
	case 'J': /* --journal-new-device */
	    Options |= OPT_NEW_J;
	    j_new_device_name = optarg;
	    break;

	case 'u':  /* --no-journal-available */
	    Options |= OPT_SKIPJ;
	    break;

	case 'p':  /* --keep-old-journal-param */
	    Options |= OPT_KEEPO;
	    break;
#endif    
	default:
	    print_usage_and_exit();
	}
    }
    
    if (optind != argc - 1)
	print_usage_and_exit ();

    /* device to be formatted */
    device_name = argv [optind];

    if (!jdevice_name && !(Options & OPT_SKIP_J))
        jdevice_name = device_name;

    if (jdevice_name && (Options & OPT_SKIP_J)) {
	    message ("either specify journal device, "
		     "or choose the option --no-journal-awailable");
	    return 1;
    }
    fs = reiserfs_open (device_name, O_RDONLY, 0, NULL);
    if (no_reiserfs_found(fs)) {
	message ("Cannot open reiserfs on %s", device_name);
        return 1;
    }

    /* now we try to open journal, it makes sence if there is no the flag
       NEED_TUNE  in ondisk superblock */
    if (get_jp_journal_magic(sb_jp(fs->fs_ondisk_sb)) != NEED_TUNE) {
      if (!reiserfs_open_journal (fs, jdevice_name, O_RDONLY)) {
	if (!(Options & OPT_SKIP_J)) {
	    message ("Unable to open old journal.");
	    reiserfs_close (fs);
	    return 1;
	}
	else
	  if (!reiserfs_is_fs_consistent (fs)) {
	    message ("Check filesystem consistency first");
	    reiserfs_close (fs);
	    return 1;
	  }
	/* forced to continue without journal available/specifed */
      }
    }

    /* journal was opened or it wasn't opened but the option
       --no-journal-available has been specified by user */

    /* make sure filesystem is not mounted */
    if (is_mounted (fs->fs_file_name)) {
	/* fixme: it can not be mounted, btw */
        message ("can not rebuild journal of mounted filesystem");
	reiserfs_close (fs);
        return 1;
    }

    /* in spite of journal was opened, the file system can be non-consistent or
       there are non-replayed transaction in journal, 
       make sure it isn't (if there is no the flag NEED_TUNE in ondisk superblock */
    if (get_jp_journal_magic(sb_jp(fs->fs_ondisk_sb)) != NEED_TUNE &&
	reiserfs_journal_opened (fs)) {
	j_head = (struct reiserfs_journal_header *)(fs->fs_jh_bh->b_data);
	if (get_boundary_transactions(fs, &old, &new)) {
	    if (new.trans_id != get_jh_last_flushed(j_head)) {
		if (!(Options & OPT_SUPER_FORCE)) {
		    message ("There are non-replayed transaction in old journal,"
			     " check filesystem consistency first");
		    reiserfs_close (fs);
		    return 1;
		}
	    }
	}
	if (!reiserfs_is_fs_consistent (fs)) {
	    message ("Check filesystem consistency first");
	    reiserfs_close (fs);
	    return 1;
	} 
    }

    reiserfs_reopen (fs, O_RDWR);

    /* set UUID and LABEL if specified */
    if (fs->fs_format == REISERFS_FORMAT_3_6) {
        if (uuid_is_correct (UUID)) {
	    memcpy (fs->fs_ondisk_sb->s_uuid, UUID, 16);
	    mark_buffer_dirty (fs->fs_super_bh);
	}
	
	if (LABEL != NULL) {
	    if (strlen (LABEL) > 16)
	        message ("Specified LABEL is longer then 16 characters, will be truncated\n");
	    strncpy (fs->fs_ondisk_sb->s_label, LABEL, 16);
	    mark_buffer_dirty (fs->fs_super_bh);
	}
	fs->fs_dirt = 1;
    } else {
        if (uuid_is_correct (UUID))
            reiserfs_panic ("UUID cannot be specified for 3.5 format\n");
        if (LABEL)
            reiserfs_panic ("LABEL cannot be specified for 3.5 format\n");
    }

    if (!j_new_device_name) {
	/* new journal device hasn't been specify */
	printf ("Current parameters:\n");
	print_filesystem_state (stdout, fs);
	print_block (stdout, fs, fs->fs_super_bh);
	printf ("Current journal parameters:\n");
	print_journal_params (stdout, sb_jp (fs->fs_ondisk_sb));

	if ((Options & OPT_STANDARD)
	    && should_make_journal_standard(fs, j_new_device_name)) {
	    if (set_standard_journal_params (fs)) {
		printf ("\nNew parameters:\n");
		print_filesystem_state (stdout, fs);
		print_block (stdout, fs, fs->fs_super_bh);
		printf ("New journal parameters:\n");
		print_journal_params (stdout, sb_jp (fs->fs_ondisk_sb));
		mark_buffer_dirty (fs->fs_super_bh);
		mark_buffer_uptodate (fs->fs_super_bh, 1);
		mark_buffer_dirty (fs->fs_jh_bh);
		mark_buffer_uptodate (fs->fs_jh_bh, 1);
		reiserfs_close (fs);
		printf ("Syncing.."); fflush (stdout);
		sync ();
		printf ("ok\n");
		return 0;
	    }
	}
	if (Is_journal_or_maxtrans_size_specified)
	    /* new journal device hasn't been specified, but
	       journal size or max transaction size have been, so we suppose
	       that journal device remains the same */
	    j_new_device_name = jdevice_name;
	else {	
	    /* the only parameter has been specified is device_name, so
	       there is nothing to do */
	    reiserfs_close (fs);
	    return 0;
	}
    }
		    
    /* new journal device has been specified */
    /* make sure new journal device is block device file */
    if (!can_we_format_it (j_new_device_name, Force)) {
	reiserfs_close (fs);
        return 1;
    }

    if (!strcmp (device_name, j_new_device_name)) {
	unsigned long reserved, journal_size;
	/* we have to put journal on main device. It is only possible if there
	   is enough space reserved by mkreiserfs */

	if (!is_reiserfs_jr_magic_string (fs->fs_ondisk_sb))
	    /* standard journal */
	    reserved = get_jp_journal_size(sb_jp(fs->fs_ondisk_sb)) + 1;
	else
	    /* non-standard journal */
	    reserved = get_sb_reserved_for_journal (fs->fs_ondisk_sb);
	
	journal_size = (Journal_size ? Journal_size : journal_default_size(fs->fs_super_bh->b_blocknr, fs->fs_blocksize));
	
/*	journal_size = (Journal_size ? Journal_size : // specified
			(fs->fs_blocksize == 1024 ? (fs->fs_blocksize) * 8 - 3 -
			 REISERFS_DISK_OFFSET_IN_BYTES / fs->fs_blocksize :
			 JOURNAL_DEFAULT_SIZE + 1));	// default
*/
	if (journal_size > reserved) {
		message ("There is no enough space reserved for journal on main "
			 "device (journal_size=%lu, reserved=%lu\n", journal_size,
			 reserved);
	    reiserfs_close (fs);
	    return 1;
	}
    }

    message ("Current journal parameters:");
    print_journal_params (stdout, sb_jp (fs->fs_ondisk_sb));

    if (!is_reiserfs_jr_magic_string (fs->fs_ondisk_sb)) {
	/* we have standard journal, so check if we can convert it
	   to non-standard one */
	if (!should_make_journal_non_standard (Force)) {
	    reiserfs_close (fs);
	    return 1;
	}

        if (is_reiserfs_3_6_magic_string (fs->fs_ondisk_sb))
	    set_sb_version (fs->fs_ondisk_sb, REISERFS_FORMAT_3_6);
        else if (is_reiserfs_3_5_magic_string (fs->fs_ondisk_sb))
	    set_sb_version (fs->fs_ondisk_sb, REISERFS_FORMAT_3_5);
        else {
            message ("Could not convert from unknown version, try reiserfsck first");
	    reiserfs_close (fs);
            return 1;
        }
        
	memcpy (fs->fs_ondisk_sb->s_v1.s_magic, REISERFS_JR_SUPER_MAGIC_STRING,
		strlen (REISERFS_JR_SUPER_MAGIC_STRING));
	set_sb_reserved_for_journal (fs->fs_ondisk_sb,
				     get_jp_journal_size (sb_jp(fs->fs_ondisk_sb)) + 1);
    }

    /* now we are going to close old journal and to create a new one */
    reiserfs_close_journal (fs);

    if (!reiserfs_create_journal (fs, j_new_device_name, Offset,
				  Journal_size, Max_trans_size)) {
	message ("Could not create new journal");
	reiserfs_close (fs);
        return 1;
    }

    if (Options & OPT_STANDARD) 
	if (should_make_journal_standard (fs, j_new_device_name))
	    set_standard_journal_params (fs);
    
    message ("New journal parameters:");
    print_journal_params (stdout, sb_jp (fs->fs_ondisk_sb));

    print_block (stdout, fs, fs->fs_super_bh);

    if (Force < 2) {
        message ("ATTENTION: YOU ARE ABOUT TO SETUP THE NEW JOURNAL FOR THE \"%s\"!\n"
                 "AREA OF \"%s\" DEDICATED FOR JOURNAL WILL BE ZEROED!",
		 device_name, j_new_device_name);
        
        if (!user_confirmed (stderr, "Continue (y/n):", "y\n")) {
            return 1;
	}
    }

    zero_journal (fs);    
    reiserfs_close (fs);
    
    printf ("Syncing.."); fflush (stdout);
    sync ();
    printf ("ok\n");

    return 0;
}
