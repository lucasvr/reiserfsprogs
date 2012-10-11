/*
 * Copyright 1996-2002  Hans Reiser
 */
#include "fsck.h"
#include <getopt.h>
#include <sys/resource.h>

#include "../include/config.h"
#include "../version.h"

extern int screen_width;
extern int screen_savebuffer_len;
extern char *screen_savebuffer;

reiserfs_filsys_t * fs;
char * badblocks_file;

#define print_usage_and_exit() {\
fsck_progress ("Usage: %s [mode] [options] "\
" device\n"\
"\n"\
"Modes:\n"\
"  --check\t\tconsistency checking (default)\n"\
"  --fix-fixable\t\tfix corruptions which can be fixed w/o --rebuild-tree\n"\
"  --rebuild-sb\t\tsuper block checking and rebuilding if needed\n"\
"  \t\t\t(require rebuild-tree afterwards)\n"\
"  --rebuild-tree\tforce fsck to rebuild filesystem from scratch\n"\
"  \t\t\t(takes a long time)\n"\
"  --clean-attributes\tclean garbage in reserved fields in StatDatas on fs\n"\
"Options:\n"\
"  -j | --journal-device device\n"\
"  \t\t\tspecify journal if relocated\n"\
"  -l | --logfile logfile\n"\
"  \t\t\tmake fsck to complain to specifed file\n"\
"  -n | --nolog\t\tmake fsck to not complain\n"\
"  -z | --adjust-file-size\n"\
"  \t\t\tfix file sizes to real size\n"\
"  \t\t\tlist of all bad blocks on the fs\n"\
"  -q | --quiet\t\tno speed info\n"\
"  -V\t\t\tprints version and exits\n"\
"  -a and -p\t\tprint fs info and exits\n"\
"  -f, -r and -y\t\tignored\n"\
"Expert options:\n"\
"  --no-journal-available\n"\
"  \t\t\tdo not open nor replay journal\n"\
"  -S | --scan-whole-partition\n"\
"  \t\t\tbuild tree of all blocks of the device\n", argv[0]);\
  exit(16);\
}

/*
"  -B badblocks-file\n"\

   -B works with --fix-fixable
        fixes indirect pointers pointed to
	badblocks, adds badblocks to badblock list in fs.
    and with --rebuild
        builds the tree w/out pointers to badblocks (internal,
	indirect), adds badblocks to badblock list in fs.  
*/

/*
Hidden usage:
Modes:
"  --rollback-fsck-changes\n\t\t\trollback all changes made by fsck\n"\
Options:
"  -b | --scan-marked-in-bitmap file\n"\
"  \t\t\tbuild tree of blocks marked in the bitmapfile\n"\
"  -R | --rollback-data file\n"\
"  \t\t\tback up all changes to this file or rollback from this file\n"\
"  \t\t\tpreviously backed up changes with --rollback-fsck-changes\n"\
"  -d dumpfile\n\"\
"  \t\t\tto test fsck pass by pass - dump into dumpfile all needed\n"\
"  \t\t\tinfo for the next pass and load on the start of the next pass\n"\
"  -i | --interactive\tmake fsck to stop after every stage\n"\
"  -h | --hash hashname\n"\
"  -g | --background\n"\
"  -t \t\tdo test\n"\
*/


/* fsck is called with one non-optional argument - file name of device
   containing reiserfs. This function parses other options, sets flags
   based on parsing and returns non-optional argument */
static char * parse_options (struct fsck_data * data, int argc, char * argv [])
{
    int c;
    static int mode = FSCK_CHECK;
    static int flag;

    data->rebuild.scan_area = USED_BLOCKS;
    while (1) {
	static struct option options[] = {
	    /* modes */
	    {"check", no_argument, &mode, FSCK_CHECK},
	    {"fix-fixable", no_argument, &mode, FSCK_FIX_FIXABLE},
	    {"rebuild-sb", no_argument, &mode, FSCK_SB},
	    {"rebuild-tree", no_argument, &mode, FSCK_REBUILD},
	    {"rollback-fsck-changes", no_argument, &mode, FSCK_ROLLBACK_CHANGES},
	    {"clean-attributes", no_argument, &mode, FSCK_CLEAN_ATTRIBUTES},
	    /* options */
	    {"logfile", required_argument, 0, 'l'},
	    {"interactive", no_argument, 0, 'i'},
	    {"adjust-file-size", no_argument, 0, 'z'},
	    {"quiet", no_argument, 0, 'q'},
	    {"nolog", no_argument, 0, 'n'},
	    
	    /* if file exists ad reiserfs can be load of it - only
               blocks marked used in that bitmap will be read */
	    {"scan-marked-in-bitmap", required_argument, 0, 'b'},

	    {"create-passes-dump", required_argument, 0, 'd'},
	
	    /* all blocks will be read */
	    {"scan-whole-partition", no_argument, 0, 'S'},
	    /* useful for -S */
	    {"hash", required_argument, 0, 'h'},

            /* preparing rollback data*/
	    {"rollback-data", required_argument, 0, 'R'},
	    
	    {"journal-device", required_argument, 0, 'j'},
	    {"no-journal-available", no_argument, &flag, OPT_SKIP_JOURNAL},
	    
//	    {"bad-block-file", required_argument, 0, 'B'},

	    /* start reiserfsck in background and exit */
	    {"background", no_argument, 0, 'g'},

	    {0, 0, 0, 0}
	};
	int option_index;
      
	c = getopt_long (argc, argv, "iql:nb:Szd:R:h:j:gafVrpyt:",
			 options, &option_index);
	if (c == -1)
	    break;
	
	switch (c) {
	case 0:
	    /* long-only options */
	    if (flag == OPT_SKIP_JOURNAL) {
		/* no journal available */
		data->options |= OPT_SKIP_JOURNAL;
		flag = 0;
	    }
	    break; 

	case 'i': /* --interactive */
	    data->options |= OPT_INTERACTIVE;
	    break;

	case 'q': /* --quiet */
	    data->options |= OPT_QUIET;
	    break;

	case 'l': /* --logfile */
	    data->log_file_name = optarg;
	    /*asprintf (&data->log_file_name, "%s", optarg);*/
	    data->log = fopen (optarg, "w");
	    if (!data->log)
		fprintf (stderr, "reiserfsck: Cannot not open \'%s\': %m", optarg);
	    break;

	case 'n': /* --nolog */
	    data->options |= OPT_SILENT;
	    break;

	case 'b': /* --scan-marked-in-bitmap */
	    /* will try to load a bitmap from a file and read only
               blocks marked in it. That bitmap could be created by
               previous run of reiserfsck with -c */
	    data->rebuild.bitmap_file_name = optarg;
	    /*asprintf (&data->rebuild.bitmap_file_name, "%s", optarg);*/
	    data->rebuild.scan_area = EXTERN_BITMAP;
	    break;

	case 'S': /* --scan-whole-partition */
	    data->rebuild.scan_area = ALL_BLOCKS;
	    break;

#if 0
	case 'J': /* take all blocks which are leaves in journal area and put
                     them into tree item by item (DO NOT USE IT UNTIL YOU KNOW
                     WHAT ARE YOU DOING) */
	    data->rebuild.use_journal_area = 1;
	    break;
#endif
	case 'd': /* --create-passes-dump */
	    asprintf (&data->rebuild.passes_dump_file_name, "%s", optarg);
	    data->options |= OPT_SAVE_PASSES_DUMP;
	    break;
	
	case 'z': /* --adjust-file-size */
	    data->options |= OPT_ADJUST_FILE_SIZE;
	    break;

	case 'h': /* --hash: suppose that this hash was used on a filesystem */
	    asprintf (&data->rebuild.defined_hash, "%s", optarg);
	    if (name2func (data->rebuild.defined_hash) == 0)
		reiserfs_panic ("reiserfsck: Unknown hash is defined: %s",
				data->rebuild.defined_hash);
	    data->options |= OPT_HASH_DEFINED;
	    break;

	case 'j': /* specified relocated journal device */
	    data->journal_dev_name = optarg;
	    break;

	case 'R': /* preparing rollback data */
	    asprintf (&data->rebuild.rollback_file, "%s", optarg);
	    data->options |= OPT_SAVE_ROLLBACK;
	    break;
	
	case 'B': /* list of phisically corrupted blocks */
	    asprintf (&badblocks_file, "%s", optarg);
	    data->options |= BADBLOCKS_FILE;
	    break;

	case 'g': /* --background */
	    data->options |= OPT_BACKGROUND;
	    break;

	case 'a':
	case 'p':
		mode = AUTO;
		break;
	
	case 'f':
	case 'r': /* ignored */
	case 'y':
	    break;
	    
	case 'V': /* cause fsck to do nothing */
	    mode = DO_NOTHING;
	    break;
	
	case 't':
	    mode = DO_TEST;
	    data->rebuild.test = atoi (optarg);
	    break;


	default:
	    print_usage_and_exit();
	}
    }

    if (optind != argc - 1 && mode != DO_NOTHING)
	/* only one non-option argument is permitted */
	print_usage_and_exit();

    if (mode != FSCK_REBUILD && 
               (data->rebuild.scan_area == EXTERN_BITMAP ||
                data->rebuild.scan_area == ALL_BLOCKS || 
                data->options & OPT_SAVE_PASSES_DUMP))
	/* wrong options for this mode */
	print_usage_and_exit();

    if (data->options & OPT_ADJUST_FILE_SIZE) {
        if ((mode != FSCK_REBUILD) && (mode != FSCK_FIX_FIXABLE)) 
            print_usage_and_exit();
    }
    
    
    if (data->options & OPT_SAVE_ROLLBACK) {
        if (mode == FSCK_SB)
            print_usage_and_exit();        
    }

    if (mode == FSCK_ROLLBACK_CHANGES) {
        if ((data->options & OPT_SAVE_ROLLBACK) == 0)
            print_usage_and_exit();        
    }

    if ((data->options & BADBLOCKS_FILE) && ((mode == FSCK_SB)
    	|| (mode == FSCK_CLEAN_ATTRIBUTES) || (mode == FSCK_CHECK)))
	print_usage_and_exit();

    data->mode = mode;
    if (!data->log)
	data->log = stdout;   

    if (data->journal_dev_name == NULL)
	data->journal_dev_name = argv[optind];
    
    return argv[optind];
}


#define REBUILD_WARNING \
"  *************************************************************\n\
  ** Do not run rebuild-tree unless something is broken  and **\n\
  ** MAKE A BACKUP before using it.  If you have bad sectors **\n\
  ** on a drive  it is usually a bad idea  to continue using **\n\
  ** it.  Then you probably should get a working hard drive, **\n\
  ** copy the file system from the bad drive to the good one **\n\
  ** -- dd_rescue is  a good tool for  that -- and only then **\n\
  ** run this program.                                       **\n\
  ** If you are using the latest reiserfsprogs and  it fails **\n\
  ** please  email bug reports to reiserfs-list@namesys.com, **\n\
  ** providing  as  much  information  as  possible --  your **\n\
  ** hardware,  kernel,  patches,  settings,  all reiserfsck **\n\
  ** messages  (including version),  the reiserfsck logfile, **\n\
  ** check  the  syslog file  for  any  related information. **\n\
  ** If you would like advice on using this program, support **\n\
  ** is available  for $25 at  www.namesys.com/support.html. **\n\
  *************************************************************\n\
\nWill rebuild the filesystem (%s) tree\n"

#define START_WARNING \
"  *************************************************************\n\
  ** If you are using the latest reiserfsprogs and  it fails **\n\
  ** please  email bug reports to reiserfs-list@namesys.com, **\n\
  ** providing  as  much  information  as  possible --  your **\n\
  ** hardware,  kernel,  patches,  settings,  all  reiserfsk **\n\
  ** messages  (including version),  the reiserfsck logfile, **\n\
  ** check  the  syslog file  for  any  related information. **\n\
  ** If you would like advice on using this program, support **\n\
  ** is available  for $25 at  www.namesys.com/support.html. **\n\
  *************************************************************\n\
\n"

#define AUTO_WARNING \
"  **********************************************************\n\
  ** WARNING:  You seem to be running this automatically. **\n\
  ** You are almost certainly  doing  it  by  mistake  as **\n\
  ** a result of some script  that  doesn't  know what it **\n\
  ** does. Nothing will be done. If  you really intend to **\n\
  ** run reiserfsck, rerun it without -a and -p options.  **\n\
  **********************************************************\n\n"

void warn_what_will_be_done (char * file_name, struct fsck_data * data)
{
    FILE * warn_to;

    warn_to = (data->progress ?: stderr);

    if (data->mode == FSCK_REBUILD)
	reiserfs_warning (warn_to, REBUILD_WARNING, file_name);
    else
	reiserfs_warning (warn_to, START_WARNING);
    
    /* warn about fsck mode */
    switch (data->mode) {
    case FSCK_CHECK:
	reiserfs_warning (warn_to,
	    "Will read-only check consistency of the filesystem on %s\n", file_name);
	break;

    case FSCK_FIX_FIXABLE:
        
	reiserfs_warning (warn_to, 
	    "Will check consistency of the filesystem on %s\n", file_name);
        reiserfs_warning (warn_to, 
	    "and will fix what can be fixed w/o --rebuild-tree\n");
	break;

    case FSCK_SB:
	reiserfs_warning (warn_to,
            "Will check superblock and rebuild it if needed\n");
	break;

    case FSCK_REBUILD:
	if (data->options & OPT_SAVE_PASSES_DUMP) {
	    reiserfs_warning (warn_to,
		"Will run only 1 step of the rebuilding, write state file '%s' and exit\n",
			      data->rebuild.passes_dump_file_name);
	} else if (data->options & OPT_INTERACTIVE)
	    reiserfs_warning (warn_to,
                "Will stop after every stage and ask for "
                "confirmation before continuing\n");

	if (data->rebuild.bitmap_file_name)
	    reiserfs_warning (warn_to,
                "Will try to load a bitmap--of all ReiserFS leaves in the partition--from the file \n'%s'\n",
		data->rebuild.bitmap_file_name);

	if (data->options & OPT_ADJUST_FILE_SIZE)
	    reiserfs_warning (warn_to,
		"\tWill set file sizes in their metadata to real file sizes actually found by fsck.\n");

	if (data->options & OPT_HASH_DEFINED)
	    reiserfs_warning (warn_to,
                "\tSuppose \"%s\" hash is in use\n",
			      data->rebuild.defined_hash);
	break;
	
    case FSCK_ROLLBACK_CHANGES:
	reiserfs_warning (warn_to,
	        "Will rollback all data saved in %s into %s\n", 
	                data->rebuild.rollback_file, file_name);
        break;
    case FSCK_CLEAN_ATTRIBUTES:
	reiserfs_warning (warn_to,
	        "Will clean file attributes on %s\n", file_name);
        break;
    case AUTO:
	reiserfs_warning (warn_to, AUTO_WARNING);
	exit(0);
    }

    if (data->options & OPT_SAVE_ROLLBACK && data->mode != FSCK_ROLLBACK_CHANGES)
        reiserfs_warning (warn_to, 
                "Will save all blocks to be changed into file '%s'\n", 
                data->rebuild.rollback_file);

    if (data->options & BADBLOCKS_FILE)
        reiserfs_warning (warn_to,
                "Bad block list will contain only blocks specified in '%s' file\n",
                badblocks_file);

    reiserfs_warning (warn_to,
                "Will put log info to '%s'\n", 
                        (data->log == stdout) ? "stdout" : (data->log_file_name ?: "fsck.run"));

    if (!user_confirmed (warn_to, 
                "\nDo you want to run this program?[N/Yes] (note need to type Yes if you do):", "Yes\n"))
	exit (0);
}

static dma_info_t dma_info;
static dma_info_t old_dma_info;

void check_dma() {
    old_dma_info = dma_info;
    if (get_dma_info(&dma_info) == -1) {
	fsck_log("get_dma_info failed %s\n", strerror (errno));
	return;
    }
    
    if (dma_info.dma != old_dma_info.dma) {
	if (dma_info.dma == 0) {
	    printf("\n********************************************************************\n");
	    printf("* Warning: It was just detected that dma mode was turned off while *\n");
	    printf("* operating -- probably  due  to some  problem with your hardware. *\n");
	    printf("* Please check your hardware and have a look into the syslog file. *\n");
	    printf("* Note:  run of  rebuild-tree on faulty  hardware may destroy your *\n");
	    printf("* data.                                                            *\n");
	    printf("********************************************************************\n");
	    if (fsck_log_file (fs) != stdout)
		fsck_log("WARNING: dma mode has been turned off.\n");
	}
    }
    if (dma_info.speed != old_dma_info.speed) {
	if (dma_info.speed < old_dma_info.speed) {
	    printf("\n********************************************************************\n");
	    printf("* Warning:It was just detected that dma speed was descreased while *\n");
	    printf("* operating -- probably  due  to some  problem with your hardware. *\n");
	    printf("* Please check your hardware and have a look into the syslog file. *\n");
	    printf("* Note:  run of  rebuild-tree on faulty  hardware may destroy your *\n");
	    printf("* data.                                                            *\n");
	    printf("********************************************************************\n");
	    if (fsck_log_file (fs) != stdout)
		fsck_log("WARNING: dma speed has been descreased.\n");	    
	}
    }
    
    alarm(1);
}

void register_timer() {
    memset(&dma_info, 0, sizeof(dma_info));
    memset(&old_dma_info, 0, sizeof(old_dma_info));
    
    dma_info.fd = fs->fs_dev;

    if (prepare_dma_check(&dma_info) != 0)
	return;

    if (get_dma_info(&dma_info) == -1) {
	fsck_log("get_dma_info failed %s\n", strerror (errno));
	return;
    }

    if (dma_info.dma == 0) {
	printf("\n******************************************************\n");
	printf("* Warning: The dma on your hard drive is turned off. *\n");
	printf("* This may really slow down the fsck process.        *\n");
	printf("******************************************************\n");
	if (fsck_log_file (fs) != stdout)
	    fsck_log("WARNING: DMA is turned off\n");
    } 
    
    signal(SIGALRM, check_dma);
    alarm(1);
}

static void reset_super_block (reiserfs_filsys_t * fs)
{
    struct reiserfs_super_block * sb;

    sb = fs->fs_ondisk_sb;

    set_sb_free_blocks (sb, get_sb_block_count (sb));
    set_sb_root_block (sb, ~0);
    set_sb_tree_height (sb, ~0);

    /* make file system invalid unless fsck finished () */
    set_sb_fs_state (sb, get_sb_fs_state (sb) || REISERFS_CORRUPTED);

/*
    if ( is_reiser2fs_jr_magic_string (sb) ) {???
	set_sb_version (sb, REISERFS_VERSION_3);
    }
    if (is_reiser2fs_magic_string (sb)) {
	set_sb_version (sb, REISERFS_FORMAT_3_6);
    }
    if (is_reiserfs_magic_string (sb)) {
	set_sb_version (sb, REISERFS_FORMAT_3_5);
    }
*/
    /* make sure that hash code in super block match to set hash function */
    set_sb_hash_code (sb, func2code (fs->fs_hash_function));
    
    if (fsck_hash_defined (fs)) {
	/* --hash was specifed */
	fs->fs_hash_function = name2func (fsck_data (fs)->rebuild.defined_hash);
	set_sb_hash_code (sb, func2code (fs->fs_hash_function));
    }
   
    /* reset journal params */
    if (!fsck_skip_journal(fs)) {
        if (is_reiserfs_jr_magic_string (sb)) {
	    struct reiserfs_journal_header * jh;

	    jh = (struct reiserfs_journal_header *)fs->fs_jh_bh->b_data;
            memcpy (sb_jp(sb), &jh->jh_journal, sizeof (struct journal_params));
            /* FIXME: copy reserved from jheader to SB */
        } else {
	    /* standard journal */
            set_jp_journal_dev (sb_jp(sb), 0);
            set_jp_journal_magic (sb_jp(sb) ,0);
            set_sb_reserved_for_journal (sb, 0);
	    set_jp_journal_1st_block (sb_jp(sb), get_journal_start_must (fs));
	    set_jp_journal_size (sb_jp(sb), journal_default_size (fs->fs_super_bh->b_blocknr, fs->fs_blocksize));	    
 	    set_jp_journal_max_trans_len (sb_jp(sb),
 	    	advise_journal_max_trans_len(	get_jp_journal_max_trans_len (sb_jp(sb)),
 	    					get_jp_journal_size (sb_jp(sb)),
 	    					fs->fs_blocksize));
 	    set_jp_journal_max_batch (sb_jp(sb), advise_journal_max_batch(get_jp_journal_max_trans_len (sb_jp(sb))));
 	    set_jp_journal_max_commit_age (sb_jp(sb), advise_journal_max_commit_age());
 	    set_jp_journal_max_trans_age (sb_jp(sb), advise_journal_max_trans_age());
        }
    }

    /* objectid map is not touched */
    mark_buffer_dirty (fs->fs_super_bh);
    bwrite (fs->fs_super_bh);
    if (!(fsck_data(fs)->options & OPT_SAVE_PASSES_DUMP))
        mark_buffer_do_not_flush (fs->fs_super_bh);
}


#define START_FROM_THE_BEGINNING 	1
#define START_FROM_PASS_1 		2
#define START_FROM_PASS_2 		3
#define START_FROM_SEMANTIC 		4
#define START_FROM_LOST_FOUND 		5
#define START_FROM_PASS_4 		6

/* this decides where to start from  */
static int where_to_start_from (reiserfs_filsys_t * fs)
{
    int ret;
    FILE * fp = 0;
    int last_run_state;
    
    last_run_state = get_sb_fs_state (fs->fs_ondisk_sb);
    if (last_run_state == 0 || !fsck_run_one_step (fs))
	/**/
	return START_FROM_THE_BEGINNING;
    
    /* We are able to perform the next step only if there is a file with the previous 
     * step results. */
    fp = open_file (state_dump_file (fs), "r");
    if (fp == 0) {
	set_sb_fs_state (fs->fs_ondisk_sb, 0);
	return START_FROM_THE_BEGINNING;
    }

    /* check start and end magics of dump file */
    ret = is_stage_magic_correct (fp);
    
    if (ret <= 0 || ret != last_run_state)
	return START_FROM_THE_BEGINNING;


    switch (last_run_state) {
    case PASS_0_DONE:
	/* skip pass 0 */
	if (!fsck_user_confirmed (fs, "Pass 0 seems finished. Start from pass 1?(Yes)",
				  "Yes\n", 1))
	    fsck_exit ("Run without -d then\n");
	
	load_pass_0_result (fp, fs);
	fclose (fp);
	return START_FROM_PASS_1;
	
    case PASS_1_DONE:
	/* skip pass 1 */
	if (!fsck_user_confirmed (fs, "Passes 0 and 1 seems finished. Start from pass 2?(Yes)",
				  "Yes\n", 1))
	    fsck_exit ("Run without -d then\n");
	
	load_pass_1_result (fp, fs);
	fclose (fp);
	return START_FROM_PASS_2;
	
    case TREE_IS_BUILT:
	if (!fsck_user_confirmed (fs, "Internal tree of filesystem looks built. Skip rebuilding?(Yes)",
				  "Yes\n", 1))
	    fsck_exit ("Run without -d then\n");
	
	load_pass_2_result (fs);
	fclose (fp);
	return START_FROM_SEMANTIC;
    case SEMANTIC_DONE:
	if (!fsck_user_confirmed (fs, "Passes 0 and 1 seems finished. Start from pass 2?(Yes)",
				  "Yes\n", 1))
	    fsck_exit ("Run without -d then\n");
	load_semantic_result (fp, fs);
	fclose (fp);
	return START_FROM_LOST_FOUND;
    case LOST_FOUND_DONE:
	if (!fsck_user_confirmed (fs, "Passes 0 and 1 seems finished. Start from pass 2?(Yes)",
				  "Yes\n", 1))
	    fsck_exit ("Run without -d then\n");
	load_lost_found_result (fs);
	fclose (fp);
	return START_FROM_PASS_4;
    }
    
    return START_FROM_THE_BEGINNING;
}


static void mark_filesystem_consistent (reiserfs_filsys_t * fs)
{
    if (!is_opened_rw (fs))
        return;

    if (!reiserfs_journal_opened (fs)) {
	/* make sure journal is not standard */
	if (!is_reiserfs_jr_magic_string (fs->fs_ondisk_sb))
	    die ("Filesystem with standard journal must be opened.");
	
	fsck_progress ("WARNING: You must use reiserfstune to specify a new journal before mounting it.\n");
	/* mark filesystem such that it is not mountable until 
	 * new journaldevice is defined */	
	set_jp_journal_magic (sb_jp (fs->fs_ondisk_sb), NEED_TUNE);
    }

    set_sb_umount_state (fs->fs_ondisk_sb, REISERFS_CLEANLY_UMOUNTED);
    set_sb_fs_state (fs->fs_ondisk_sb, REISERFS_CONSISTENT);
    
    mark_buffer_dirty (fs->fs_super_bh);
}

static void reiserfsck_replay_journal () {
    /* keep the super_block in the separate memory to avoid problems with replayed broken parameters */
    fs->fs_ondisk_sb = getmem (fs->fs_blocksize);
    memcpy (fs->fs_ondisk_sb, fs->fs_super_bh->b_data, fs->fs_blocksize);

    replay_journal (fs);

    /* get rid of SB copy */
    memcpy (fs->fs_super_bh->b_data, fs->fs_ondisk_sb, fs->fs_blocksize);
    freemem (fs->fs_ondisk_sb);
    fs->fs_ondisk_sb = (struct reiserfs_super_block *)fs->fs_super_bh->b_data;

}

static void the_end (reiserfs_filsys_t * fs)
{
    struct reiserfs_super_block * sb;

    sb = fs->fs_ondisk_sb;

    /* put bitmap and objectid map on place */
    reiserfs_delete_bitmap (fs->fs_bitmap2);
    fs->fs_bitmap2 = fsck_new_bitmap (fs);
    if (!fs->fs_bitmap2->bm_dirty)
	die ("Bitmap not dirty");

    flush_objectid_map (proper_id_map (fs), fs);
    free_id_map(proper_id_map (fs));
    free_id_map(semantic_id_map (fs));

/*    set_sb_free_blocks (sb, reiserfs_bitmap_zeros (fsck_new_bitmap (fs)));*/

    mark_filesystem_consistent (fs);
    clear_buffer_do_not_flush (fs->fs_super_bh);

    /* write all dirty blocks */
    fsck_progress ("Syncing..");
    fs->fs_dirt = 1;
    clean_after_dma_check(fs->fs_dev, &dma_info);
    reiserfs_close (fs);
    fsck_progress ("finished\n");
}


static void rebuild_tree (reiserfs_filsys_t * fs)
{
    time_t t;


    if (is_mounted (fs->fs_file_name)) {
	fsck_progress ("rebuild_tree: Cannot rebuild tree of mounted filesystem\n");
	exit(16);
    }

    init_rollback_file (state_rollback_file(fs), &fs->fs_blocksize, fsck_data(fs)->log);
    reiserfs_reopen (fs, O_RDWR);

    /* FIXME: for regular file take care of of file size */

    if (!reiserfs_open_ondisk_bitmap (fs)) {
        fsck_progress ("reiserfsck: Could not open bitmap\n");
	reiserfs_close (fs);
	exit(8);
    }

    /* rebuild starts with journal replaying */
    if (!fsck_skip_journal (fs))
        reiserfsck_replay_journal ();

    time (&t);
    fsck_progress ("###########\n"
		   "reiserfsck --rebuild-tree started at %s"
		   "###########\n", ctime (&t));

    switch (where_to_start_from (fs)) {
    case START_FROM_THE_BEGINNING:
	reset_super_block (fs);
	pass_0 (fs);

    case START_FROM_PASS_1:
	reset_super_block (fs);
	pass_1 (fs);
	
    case START_FROM_PASS_2:
	pass_2 (fs);

    case START_FROM_SEMANTIC:
	pass_3_semantic (fs);

	/* if --lost+found is set - link unaccessed directories to lost+found
	   directory */
    case START_FROM_LOST_FOUND:	
	pass_3a_look_for_lost (fs);
	
    case START_FROM_PASS_4:
	/* 4. look for unaccessed items in the leaves */
	pass_4_check_unaccessed_items ();
	
	the_end (fs);
    }

    close_rollback_file ();
    
    time (&t);
    fsck_progress ("###########\n"
		   "reiserfsck finished at %s"
		   "###########\n", ctime (&t));
    exit (0);
}


/* check umounted or read-only mounted filesystems only */
static void prepare_fs_for_pass_through_tree (reiserfs_filsys_t * fs)
{
    if (is_mounted (fs->fs_file_name)) {
	/* filesystem seems mounted. */
        if (fsck_mode (fs) == FSCK_CLEAN_ATTRIBUTES) {
	    fsck_progress ("Partition %s is mounted, cannot clean attributes on mounted device\n",
			   fs->fs_file_name);
	    reiserfs_close (fs);
	    exit(16);
        }

	if (!is_mounted_read_only (fs->fs_file_name)) {
	    fsck_progress ("Partition %s is mounted w/ write permissions, cannot check it\n",
			   fs->fs_file_name);
	    reiserfs_close (fs);
	    exit(16);
	}
	if (!reiserfs_journal_opened (fs))
	    /* just to make sure */
	    reiserfs_panic ("Journal is not opened");

	fsck_progress ("Filesystem seems mounted read-only. Skipping journal replay.\n");

	if (fsck_mode (fs) == FSCK_FIX_FIXABLE) {
	    fsck_progress ("--fix-fixable ignored\n");
	    fsck_mode (fs) = FSCK_CHECK;
	}
    } else {
        reiserfs_reopen (fs, O_RDWR);
	if (!fsck_skip_journal (fs))
	    /* filesystem is not mounted, replay journal before checking */
            reiserfsck_replay_journal ();
    }
}

static void clean_attributes (reiserfs_filsys_t * fs) {
    time_t t;

    time (&t);

    if (get_sb_umount_state (fs->fs_ondisk_sb) != REISERFS_CLEANLY_UMOUNTED) {
        fsck_progress ("Filesystem does not look cleanly umounted\n"
	    "Check consistency of the partition first.\n");
        exit(16);
    }
    if (get_sb_fs_state (fs->fs_ondisk_sb) != REISERFS_CONSISTENT) {
        fsck_progress ("Filesystem seems to be in unconsistent state.\n"
				  "Check consistency of the partition first.\n");
        exit(16);
    }

    if (get_reiserfs_format (fs->fs_ondisk_sb) != REISERFS_FORMAT_3_6) {
        fsck_progress ("Filesystems of 3_5 format do not support extended attributes.\n");
        exit(16);
    }
    fsck_progress ("###########\n"
	           "reiserfsck --clean-attributes started at %s"
                   "###########\n", ctime (&t));

    init_rollback_file (state_rollback_file(fs), &fs->fs_blocksize, fsck_data(fs)->log);

    prepare_fs_for_pass_through_tree (fs);

    do_clean_attributes (fs);

    clean_after_dma_check(fs->fs_dev, &dma_info);
    reiserfs_close (fs);
    close_rollback_file ();

    fsck_progress ("###########\n"
		   "reiserfsck finished at %s"
		   "###########\n", ctime (&t));

    exit (0);

}

/* check umounted or read-only mounted filesystems only */
static void check_fs (reiserfs_filsys_t * fs)
{
    int retval;
    time_t t;

    time (&t);

    if (fsck_mode (fs) != FSCK_FIX_FIXABLE) {
        fsck_progress ("###########\n"
	               "reiserfsck --check started at %s"
                       "###########\n", ctime (&t));
    } else {
        fsck_progress ("###########\n"
	               "reiserfsck --fix-fixable started at %s"
                       "###########\n", ctime (&t));
    }

    init_rollback_file (state_rollback_file(fs), &fs->fs_blocksize, fsck_data(fs)->log);
    
    if (!reiserfs_open_ondisk_bitmap (fs)) {
        fsck_progress ("reiserfsck: Could not open bitmap\n");
	reiserfs_close (fs);
	exit(8);
    }

    prepare_fs_for_pass_through_tree (fs);

    check_fs_tree (fs);

    semantic_check ();

    if (fsck_data (fs)->check.fatal_corruptions) {
	fsck_progress ("%d found corruptions can be fixed only during --rebuild-tree\n",
		       fsck_data (fs)->check.fatal_corruptions);
        set_sb_fs_state (fs->fs_ondisk_sb, REISERFS_CORRUPTED);
        mark_buffer_dirty (fs->fs_super_bh);
	retval = 2;
    } else if (fsck_data (fs)->check.fixable_corruptions) {
        /* fixable corruptions found */
	if (fsck_mode (fs) == FSCK_FIX_FIXABLE) {
            /* fixable corruptions found and fix-fixable has not fixed them, do rebuild-tree */
            fsck_log ("Fatal error: %d fixable corruptions found after fix-fixable.\n",
                                fsck_data (fs)->check.fixable_corruptions);
	    retval = 2;
        } else {
	    fsck_progress ("%d found corruptions can be fixed with --fix-fixable\n",
                          fsck_data (fs)->check.fixable_corruptions);
	    retval = 1;
        }
        set_sb_fs_state (fs->fs_ondisk_sb, REISERFS_CORRUPTED);
        mark_buffer_dirty (fs->fs_super_bh);
    } else {
	fsck_progress ("No corruptions found\n");
	stage_report (5, fs);
	retval = 0;

	mark_filesystem_consistent (fs);
    }

    if (fsck_mode (fs) == FSCK_FIX_FIXABLE) {
        flush_objectid_map (proper_id_map (fs), fs);

        fs->fs_dirt = 1;
    }
        
    free_id_map (proper_id_map (fs));
    clean_after_dma_check(fs->fs_dev, &dma_info);
    reiserfs_close (fs);
    close_rollback_file ();
    
    time (&t);
    fsck_progress ("###########\n"
		   "reiserfsck finished at %s"
		   "###########\n", ctime (&t));

    exit(retval);
}

static int open_devices_for_rollback (char * file_name, struct fsck_data * data) {
    int fd;

    fd = open (file_name, O_RDWR | O_LARGEFILE);

    if (fd == -1) {
        reiserfs_warning (stderr, "reiserfsck: Cannot not open the fs partition %s\"", file_name);
        return -1;
    }

    fs = getmem (sizeof (*fs));
    fs->fs_dev = fd;
    fs->fs_vp = data;
    asprintf (&fs->fs_file_name, "%s", file_name);

    if (data->journal_dev_name && strcmp (data->journal_dev_name, file_name)) {
	fs->fs_journal_dev = open (data->journal_dev_name, O_RDWR | O_LARGEFILE);
	if (fs->fs_journal_dev == -1) {
	    reiserfs_warning (stderr, "Cannot open journal partition\n");
     	    return -1;
	}
    }

    if (open_rollback_file (state_rollback_file(fs), fsck_data(fs)->log))
        return -1;

    return 0;    	
}

static void fsck_rollback (reiserfs_filsys_t * fs) {
    time_t t;

    time (&t);
    fsck_progress ("###########\n"
	           "reiserfsck --rollback-fsck-changes started at %s"
                   "###########\n", ctime (&t));

    do_fsck_rollback (fs->fs_dev, fs->fs_journal_dev, fsck_progress_file (fs));
    close_rollback_file ();

    close (fs->fs_journal_dev);
    free (fs->fs_file_name);
    fs->fs_file_name = 0;
    close (fs->fs_dev);
    freemem (fs);

    time (&t);
    fsck_progress ("###########\n"
		   "reiserfsck finished at %s"
		   "###########\n", ctime (&t));

    exit(0);
}

int main (int argc, char * argv [])
{
    char * file_name;
    struct fsck_data * data;
    struct rlimit rlim = {0xffffffff, 0xffffffff};
    int retval;
    char *width;
    
    width = getenv("COLUMNS");
    if ( width )
	screen_width = atoi(width);
    
    if (screen_width == 0)
	screen_width = 80; // We default to 80 characters wide screen
    screen_width--;
    screen_savebuffer_len=screen_width;
    screen_savebuffer=getmem(screen_width+1);
    memset(screen_savebuffer,0,screen_savebuffer_len+1);
    
    lost_found_dir_key.k2_dir_id = cpu_to_le32(lost_found_dir_key.k2_dir_id);
    lost_found_dir_key.k2_objectid = cpu_to_le32(lost_found_dir_key.k2_objectid);
    /* this is only needed (and works) when running under 2.4 on regular files */
    if (setrlimit (RLIMIT_FSIZE, &rlim) == -1) {
	reiserfs_warning (stderr, "Cannot change the system limit for file size with setrlimit: %m\n");
    }

    data = getmem (sizeof (struct fsck_data));

    file_name = parse_options (data, argc, argv);

    if (data->mode != AUTO)
	print_banner ("reiserfsck");

    if (data->mode == DO_NOTHING) {
	freemem (data);
	return 0;
    }

    if (data->options & OPT_BACKGROUND) {
	/* running in background reiserfsck appends progress information into
           'fsck.run'. Logs get there if log file was not specified*/
	data->options |= OPT_QUIET;
	data->progress = fopen ("fsck.run", "a+");
	if (!data->progress)
	    reiserfs_panic ("reiserfsck: Cannot not open \"fsck.run\"");

	if (data->log == stdout)
	    /* no log file specifed - redirect log into 'fsck.run' */
	    data->log = data->progress;

	retval = fork ();
	if (retval == -1)
	    reiserfs_panic ("reiserfsck: Fork failed: %m");
	if (retval != 0) {
	    return 8;
	}
	reiserfs_warning (stderr, "\nReiserfsck is running in background as [%d],\n"
	    "make sure that it gets all the confirmations from stdin that it requests.\n\n",
	    getpid ());
    }


    if (data->mode != AUTO)
	warn_what_will_be_done (file_name, data); /* and ask confirmation Yes */

    if (data->mode == FSCK_ROLLBACK_CHANGES) {
    	if (open_devices_for_rollback (file_name, data) == -1)
    	    exit(8);
    } else {
	fs = reiserfs_open (file_name, O_RDONLY, 0, data);

	if (data->mode != FSCK_SB) {
	    if (no_reiserfs_found (fs))
    	    	die ("reiserfsck: Cannot not open filesystem on \"%s\"", file_name);

	    if (data->mode == AUTO) {
		print_super_block (stdout, fs, fs->fs_file_name, fs->fs_super_bh, 1);
		reiserfs_close(fs);
	
		exit(0);
	    }
	
	    if (fsck_skip_journal (fs) && !is_reiserfs_jr_magic_string (fs->fs_ondisk_sb)) {
		reiserfs_warning (stderr, "Filesystem with standard journal found, "
			"--no-journal-available is ignored\n");
		fsck_data(fs)->options &= ~OPT_SKIP_JOURNAL;
	    }
	
	    if (!fsck_skip_journal (fs)) {
		if (!reiserfs_open_journal (fs, data->journal_dev_name, O_RDONLY)) {	
	            fsck_progress ("\nEither make journal partition available or use --no-journal-available\n");
		    fsck_progress ("If you have the standard journal or if your partition is available\n");
        	    fsck_progress ("and you specified it correctly, you must run rebuild-sb\n");
		    reiserfs_close (fs);
      		    return 8;
	        }
	    }
	
	    if (data->options & BADBLOCKS_FILE) {
		if (create_badblock_bitmap (fs, badblocks_file) != 0) {
		    badblocks_file = NULL;
		    data->options &= ~BADBLOCKS_FILE;
		}
	    }
	    register_timer();
    	}
    }

    switch (data->mode) {
    case FSCK_SB:
	rebuild_sb (fs, file_name, data);
	break;
	
    case FSCK_CHECK:
    case FSCK_FIX_FIXABLE:
	check_fs (fs);
	break;

    case FSCK_REBUILD:
    case DO_TEST:
	rebuild_tree (fs);
	break;

    case FSCK_ROLLBACK_CHANGES:
	fsck_rollback (fs);
 	break;
    case FSCK_CLEAN_ATTRIBUTES:
	clean_attributes (fs);
    }
    
    return 8;
}

