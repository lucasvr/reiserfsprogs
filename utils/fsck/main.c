/*
 * Copyright 1996-2004 by Hans Reiser, licensing governed by 
 * reiserfsprogs/README
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "fsck.h"
#include "misc/malloc.h"
#include "util/print.h"
#include "util/device.h"
#include "util/badblock.h"

#include <getopt.h>
#include <sys/resource.h>
#include <sys/mman.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>

reiserfs_filsys_t * fs;
char * badblocks_file;

#define print_usage_and_exit() {						\
fsck_progress ("Usage: %s [mode] [options] "					\
" device\n"									\
"\n"										\
"Modes:\n"									\
"  --check\t\t\tconsistency checking (default)\n"				\
"  --fix-fixable\t\t\tfix corruptions which can be fixed without \n"		\
"  \t\t\t\t--rebuild-tree\n"							\
"  --rebuild-sb\t\t\tsuper block checking and rebuilding if needed\n"		\
"  \t\t\t\t(may require --rebuild-tree afterwards)\n"				\
"  --rebuild-tree\t\tforce fsck to rebuild filesystem from scratch\n"		\
"  \t\t\t\t(takes a long time)\n"						\
"  --clean-attributes\t\tclean garbage in reserved fields in StatDatas \n"	\
"Options:\n"									\
"  -j | --journal device\t\tspecify journal if relocated\n"			\
"  -B | --badblocks file\t\tfile with list of all bad blocks on the fs\n"			\
"  -l | --logfile file\t\tmake fsck to complain to specifed file\n"		\
"  -n | --nolog\t\t\tmake fsck to not complain\n"				\
"  -z | --adjust-size\t\tfix file sizes to real size\n"				\
"  -q | --quiet\t\t\tno speed info\n"						\
"  -y | --yes\t\t\tno confirmations\n"						\
"  -V\t\t\t\tprints version and exits\n"					\
"  -a and -p\t\t\tsome light-weight auto checks for bootup\n"			\
"  -f and -r\t\t\tignored\n"							\
"Expert options:\n"								\
"  --no-journal-available\tdo not open nor replay journal\n"			\
"  -S | --scan-whole-partition\tbuild tree of all blocks of the device\n\n",	\
  argv[0]);									\
										\
  exit(EXIT_OK);								\
}

/*
   -B works with --fix-fixable
        fixes extent pointers pointed to
	badblocks, adds badblocks to badblock list in fs.
    and with --rebuild
        builds the tree without pointers to badblocks (internal,
	extent), adds badblocks to badblock list in fs.  
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
"  -d dumpfile\n"\
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
	    {"badblocks", required_argument, 0, 'B'},
	    {"interactive", no_argument, 0, 'i'},
	    {"adjust-size", no_argument, 0, 'z'},
	    {"quiet", no_argument, 0, 'q'},
	    {"yes", no_argument, 0, 'y'},
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
	    
	    {"journal", required_argument, 0, 'j'},
	    {"no-journal-available", no_argument, &flag, OPT_SKIP_JOURNAL},
	    
	    {"bad-block-file", required_argument, 0, 'B'},

	    /* start reiserfsck in background and exit */
	    {"background", no_argument, 0, 'g'},

	    {0, 0, 0, 0}
	};
	int option_index;
      
	c = getopt_long (argc, argv, "iql:nb:Szd:R:h:j:gafVrpyt:B:",
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

	case 'y': /* --quiet */
	    data->options |= OPT_YES;
	    break;

	case 'l': /* --logfile */
	    data->log_file_name = optarg;
	    data->log = fopen (optarg, "w");
	    if (!data->log)
		fprintf (stderr, "reiserfsck: Cannot not open \'%s\': %s", 
			 optarg, strerror(errno));	    
	    break;

	case 'n': /* --nolog */
	    data->options |= OPT_SILENT;
	    break;

	case 'b': /* --scan-marked-in-bitmap */
	    /* will try to load a bitmap from a file and read only
               blocks marked in it. That bitmap could be created by
               previous run of reiserfsck with -c */
	    data->rebuild.bitmap_file_name = optarg;
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
	    data->rebuild.passes_dump_file_name = optarg;
	    data->options |= OPT_SAVE_PASSES_DUMP;
	    break;
	
	case 'z': /* --adjust-file-size */
	    data->options |= OPT_ADJUST_FILE_SIZE;
	    break;

	case 'h': /* --hash: suppose that this hash was used on a filesystem */
	    data->rebuild.defined_hash = optarg;
	    if (reiserfs_hash_get (data->rebuild.defined_hash) == 0)
		reiserfs_panic ("reiserfsck: Unknown hash is specified: %s",
				data->rebuild.defined_hash);
	    data->options |= OPT_HASH_DEFINED;
	    break;

	case 'j': /* specified relocated journal device */
	    data->journal_dev_name = optarg;
	    break;

	case 'R': /* preparing rollback data */
	    data->rebuild.rollback_file = optarg;
	    data->options |= OPT_SAVE_ROLLBACK;
	    break;
	
	case 'B': /* list of phisically corrupted blocks */
	    badblocks_file = optarg;
	    data->options |= BADBLOCKS_FILE;
	    break;

	case 'g': /* --background */
	    data->options |= OPT_BACKGROUND;
	    break;

	case 'a':
	case 'p':
		data->options |= OPT_QUIET;
		mode = FSCK_AUTO;
		break;
	
	case 'f':
	case 'r': /* ignored */
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

/*
    if (data->options & OPT_ADJUST_FILE_SIZE) {
        if ((mode != FSCK_REBUILD) && (mode != FSCK_FIX_FIXABLE)) 
            print_usage_and_exit();
    }
*/    
    
    if (data->options & OPT_SAVE_ROLLBACK) {
        if (mode == FSCK_SB)
            print_usage_and_exit();        
    }

    if (mode == FSCK_ROLLBACK_CHANGES) {
        if ((data->options & OPT_SAVE_ROLLBACK) == 0)
            print_usage_and_exit();        
    }

    if ((data->options & BADBLOCKS_FILE) && mode != FSCK_REBUILD && 
	mode != FSCK_FIX_FIXABLE)
    {
	fprintf(stderr, "Badblocks can be specified with --fix-fixable or "
		"--rebuild-tree only.\n");
	print_usage_and_exit();
    }

    if ((mode == FSCK_REBUILD) && (data->options & OPT_YES))
	data->options &= ~OPT_YES;
    
    data->mode = mode;
    if (!data->log)
	data->log = stdout;   

    return argv[optind];
}


#define REBUILD_WARNING \
"*************************************************************\n\
** Do not  run  the  program  with  --rebuild-tree  unless **\n\
** something is broken and MAKE A BACKUP  before using it. **\n\
** If you have bad sectors on a drive  it is usually a bad **\n\
** idea to continue using it. Then you probably should get **\n\
** a working hard drive, copy the file system from the bad **\n\
** drive  to the good one -- dd_rescue is  a good tool for **\n\
** that -- and only then run this program.                 **\n\
** If you are using the latest reiserfsprogs and  it fails **\n\
** please  email bug reports to reiserfs-list@namesys.com, **\n\
** providing  as  much  information  as  possible --  your **\n\
** hardware,  kernel,  patches,  settings,  all reiserfsck **\n\
** messages  (including version),  the reiserfsck logfile, **\n\
** check  the  syslog file  for  any  related information. **\n\
** If you would like advice on using this program, support **\n\
** is available  for $25 at  www.namesys.com/support.html. **\n\
*************************************************************\n\
\nWill rebuild the filesystem on (%s)\n"

#define START_WARNING \
"*************************************************************\n\
** If you are using the latest reiserfsprogs and  it fails **\n\
** please  email bug reports to reiserfs-list@namesys.com, **\n\
** providing  as  much  information  as  possible --  your **\n\
** hardware,  kernel,  patches,  settings,  all reiserfsck **\n\
** messages  (including version),  the reiserfsck logfile, **\n\
** check  the  syslog file  for  any  related information. **\n\
** If you would like advice on using this program, support **\n\
** is available  for $25 at  www.namesys.com/support.html. **\n\
*************************************************************\n\
\n"


static void warn_what_will_be_done (char * file_name, struct fsck_data * data) {
    FILE * warn_to;

    warn_to = (data->progress ? data->progress : stderr);

    if (data->mode == FSCK_REBUILD)
	reiserfs_warning (warn_to, REBUILD_WARNING, file_name);
    else
	reiserfs_warning (warn_to, START_WARNING);
    
    /* warn about fsck mode */
    switch (data->mode) {
    case FSCK_CHECK:
	reiserfs_warning (warn_to, "Will read-only check consistency of the "
	    "filesystem on %s\n", file_name);
	
	break;

    case FSCK_FIX_FIXABLE:
        
	reiserfs_warning (warn_to, "Will check consistency of the filesystem "
	    "on %s\n", file_name);
        reiserfs_warning (warn_to, "and will fix what can be fixed without "
	    "--rebuild-tree\n");
	
	break;

    case FSCK_SB:
	reiserfs_warning (warn_to, "Will check superblock and rebuild it if "
	    "needed\n");
	
	break;

    case FSCK_REBUILD:
	if (data->options & OPT_SAVE_PASSES_DUMP) {
	    reiserfs_warning (warn_to, "Will run only 1 step of the rebuilding, "
		"write state file '%s' and exit\n", 
		data->rebuild.passes_dump_file_name);
	} else if (data->options & OPT_INTERACTIVE)
	    reiserfs_warning (warn_to, "Will stop after every stage and ask for "
                "confirmation before continuing\n");

	if (data->rebuild.bitmap_file_name)
	    reiserfs_warning (warn_to, "Will try to obtain the list of ReiserFS"
		" leaves from the file '%s'\n", data->rebuild.bitmap_file_name);

	if (data->options & OPT_ADJUST_FILE_SIZE)
	    reiserfs_warning (warn_to, "\tWill set file sizes in their metadata "
		"to real file sizes actually found by fsck.\n");

	if (data->options & OPT_HASH_DEFINED)
	    reiserfs_warning (warn_to, "\tSuppose \"%s\" hash is in use\n",
		data->rebuild.defined_hash);
	
	break;
	
    case FSCK_ROLLBACK_CHANGES:
	reiserfs_warning (warn_to, "Will rollback all data saved in %s into %s\n",
	    data->rebuild.rollback_file, file_name);
	
        break;
    case FSCK_CLEAN_ATTRIBUTES:
	reiserfs_warning (warn_to, "Will clean file attributes on %s\n", 
	    file_name);
        break;
    case FSCK_AUTO:
	return;
    }

    if (data->options & OPT_SAVE_ROLLBACK && data->mode != FSCK_ROLLBACK_CHANGES)
        reiserfs_warning (warn_to, "Will save all blocks to be changed into "
	    "file '%s'\n", data->rebuild.rollback_file);

    if (data->options & BADBLOCKS_FILE)
        reiserfs_warning (warn_to,
            "Bad block list will contain only blocks specified in '%s' "
	    "file\n", badblocks_file);

    reiserfs_warning (warn_to, "Will put log info to '%s'\n", 
	(data->log == stdout) ? "stdout" : 
	(data->log_file_name ? data->log_file_name : "fsck.run"));
    
    if (!(data->options & OPT_YES)) {
	    if (!util_user_confirmed(warn_to, "\nDo you want to run this "
				     "program?[N/Yes] (note need to type "
				     "Yes if you do):", "Yes\n"))
	    {
		    exit (EXIT_USER);
	    }
    }
}

#define DMA_IS_OFF								\
"\n********************************************************************\n"	\
"* Warning: It was just detected that dma mode was turned off while *\n"	\
"* operating -- probably  due  to some  problem with your hardware. *\n"	\
"* Please check your hardware and have a look into the syslog file. *\n"	\
"* Note: running with --rebuild-tree on faulty hardware may destroy *\n"	\
"* your data.                                                       *\n"	\
"********************************************************************\n"

#define DMA_IS_CHANGED								\
"\n********************************************************************\n"	\
"* Warning: It was just detected that dma speed was descreased while*\n"	\
"* operating -- probably  due  to some  problem with your hardware. *\n"	\
"* Please check your hardware and have a look into the syslog file. *\n"	\
"* Note: running with --rebuild-tree on faulty hardware may destroy *\n"	\
"* your data.                                                       *\n"	\
"********************************************************************\n"

static util_device_dma_t dma_info;
static util_device_dma_t old_dma_info;

static void check_dma() {
    old_dma_info = dma_info;
    if (util_device_get_dma(&dma_info) == -1) {
	fsck_log("util_device_get_dma failed %s\n", strerror (errno));
	return;
    }
    
    if (dma_info.dma != old_dma_info.dma) {
	if (dma_info.dma == 0) {
	    printf(DMA_IS_OFF);
	    if (fsck_log_file (fs) != stdout)
		fsck_log("WARNING: dma mode has been turned off.\n");
	}
    }
    if (dma_info.speed != old_dma_info.speed) {
	if (dma_info.speed < old_dma_info.speed) {
	    printf(DMA_IS_CHANGED);
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

    if (util_device_dma_prep(&dma_info) != 0)
	return;

    if (util_device_get_dma(&dma_info) == -1) {
	fsck_log("util_device_get_dma failed %s\n", strerror (errno));
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
    reiserfs_sb_t * sb;
    struct reiserfs_journal_header * jh;
    
    sb = fs->fs_ondisk_sb;

    reiserfs_sb_set_free (sb, reiserfs_sb_get_blocks (sb));
    reiserfs_sb_set_root (sb, 0);
    reiserfs_sb_set_height (sb, ~0);

    /* make file system invalid unless fsck finished () */
    reiserfs_sb_set_state (sb, reiserfs_sb_get_state (sb) | FS_FATAL);

/*
    if ( is_reiser2fs_jr_magic_string (sb) ) {???
	reiserfs_sb_set_version (sb, REISERFS_VERSION_3);
    }
    if (is_reiser2fs_magic_string (sb)) {
	reiserfs_sb_set_version (sb, REISERFS_FORMAT_3_6);
    }
    if (is_reiserfs_magic_string (sb)) {
	reiserfs_sb_set_version (sb, REISERFS_FORMAT_3_5);
    }
*/
    /* make sure that hash code in super block match to set hash function */
    reiserfs_sb_set_hash (sb, reiserfs_hash_code (fs->hash));
    
    if (fsck_hash_defined (fs)) {
	/* --hash was specifed */
	fs->hash = reiserfs_hash_get (fsck_data (fs)->rebuild.defined_hash);
	reiserfs_sb_set_hash (sb, reiserfs_hash_code (fs->hash));
    }
    
    if (reiserfs_journal_opened (fs)) {	
	jh = (struct reiserfs_journal_header *)fs->fs_jh_bh->b_data;
    
	/* reset journal params if needed. */
	if (memcmp(reiserfs_sb_jp(sb), &jh->jh_journal, sizeof (reiserfs_journal_param_t))) {
	    if (reiserfs_super_jr_magic (sb)) 
		memcpy (reiserfs_sb_jp(sb), &jh->jh_journal, sizeof (reiserfs_journal_param_t));
	    else {
		reiserfs_sb_set_reserved (sb, 0);
	    
		reiserfs_jp_set_dev (reiserfs_sb_jp(sb), 0);
		reiserfs_jp_set_magic (reiserfs_sb_jp(sb), misc_random());
		reiserfs_jp_set_start (reiserfs_sb_jp(sb), 
		    reiserfs_journal_start_must (fs));
		reiserfs_jp_set_size (reiserfs_sb_jp(sb),
		    reiserfs_journal_default (fs->fs_super_bh->b_blocknr, 
					      fs->fs_blocksize));	    
		
		reiserfs_jp_set_tlen (reiserfs_sb_jp(sb),
		    reiserfs_journal_tlen(
			reiserfs_jp_get_tlen (reiserfs_sb_jp(sb)),
			reiserfs_jp_get_size (reiserfs_sb_jp(sb)),
			fs->fs_blocksize, 0));
		reiserfs_jp_set_max_batch (reiserfs_sb_jp(sb), 
		    reiserfs_journal_batch(reiserfs_jp_get_tlen (reiserfs_sb_jp(sb))));
		reiserfs_jp_set_commit_age (reiserfs_sb_jp(sb), 
		    reiserfs_journal_commit_age());
		reiserfs_jp_set_trans_age (reiserfs_sb_jp(sb), 
		    reiserfs_journal_trans_age());
		
		reiserfs_jp_set_dev (&jh->jh_journal, 0);
		reiserfs_jp_set_magic (&jh->jh_journal, 
				      reiserfs_jp_get_magic(reiserfs_sb_jp(sb)));
		reiserfs_jp_set_start (&jh->jh_journal, 
		    reiserfs_jp_get_start(reiserfs_sb_jp(sb)));
		reiserfs_jp_set_size (&jh->jh_journal, 
		    reiserfs_jp_get_size(reiserfs_sb_jp(sb)));
		reiserfs_jp_set_tlen (&jh->jh_journal, 
		    reiserfs_jp_get_tlen(reiserfs_sb_jp(sb)));
		reiserfs_jp_set_max_batch (&jh->jh_journal, 
		    reiserfs_jp_get_max_batch(reiserfs_sb_jp(sb)));
		reiserfs_jp_set_commit_age (&jh->jh_journal, 
		    reiserfs_jp_get_commit_age(reiserfs_sb_jp(sb)));
		reiserfs_jp_set_trans_age (&jh->jh_journal, 
		    reiserfs_jp_get_trans_age(reiserfs_sb_jp(sb))); 
	    }
	}
    }

    /* objectid map is not touched */
    reiserfs_buffer_mkdirty (fs->fs_super_bh);
    reiserfs_buffer_write (fs->fs_super_bh);
    if (!(fsck_data(fs)->options & OPT_SAVE_PASSES_DUMP))
        reiserfs_buffer_mknoflush (fs->fs_super_bh);
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
    
    last_run_state = reiserfs_sb_get_state (fs->fs_ondisk_sb);
    if (last_run_state == 0 || !fsck_run_one_step (fs))
	/**/
	return START_FROM_THE_BEGINNING;
    
    /* We are able to perform the next step only if there is a file with the previous 
     * step results. */
    fp = util_file_open (state_dump_file (fs), "r");
    if (fp == 0) {
	reiserfs_sb_set_state (fs->fs_ondisk_sb, 0);
	return START_FROM_THE_BEGINNING;
    }

    /* check start and end magics of dump file */
    ret = fsck_stage_magic_check (fp);
    
    if (ret <= 0 || ret != last_run_state)
	return START_FROM_THE_BEGINNING;


    switch (last_run_state) {
    case PASS_0_DONE:
	/* skip pass 0 */
	if (!fsck_info_ask (fs, "Pass 0 seems finished. Start "
			    "from pass 1?(Yes)", "Yes\n", 1))
	{
	    fsck_exit ("Run without -d then\n");
	}
	
	fsck_pass0_load_result (fp, fs);
	fclose (fp);
	return START_FROM_PASS_1;
	
    case PASS_1_DONE:
	/* skip pass 1 */
	if (!fsck_info_ask (fs, "Passes 0 and 1 seems finished. "
			    "Start from pass 2?(Yes)", "Yes\n", 1)) 
	{
	    fsck_exit ("Run without -d then\n");
	}
	
	fsck_pass1_load_result (fp, fs);
	fclose (fp);
	return START_FROM_PASS_2;
	
    case TREE_IS_BUILT:
	if (!fsck_info_ask (fs, "Internal tree of filesystem looks built. "
			    "Skip rebuilding?(Yes)", "Yes\n", 1))
	{
	    fsck_exit ("Run without -d then\n");
	}
	
	fsck_pass2_load_result (fs);
	fclose (fp);
	return START_FROM_SEMANTIC;
    case SEMANTIC_DONE:
	if (!fsck_info_ask (fs, "Passes 0 and 1 seems finished. Start from "
			    "pass 2?(Yes)", "Yes\n", 1))
	{
	    fsck_exit ("Run without -d then\n");
	}
	
	fsck_semantic_load_result (fp, fs);
	fclose (fp);
	return START_FROM_LOST_FOUND;
    case LOST_FOUND_DONE:
	if (!fsck_info_ask (fs, "Passes 0 and 1 seems finished. Start from "
			    "pass 2?(Yes)", "Yes\n", 1))
	{
	    fsck_exit ("Run without -d then\n");
	}
	
	fsck_lost_load_result (fs);
	fclose (fp);
	return START_FROM_PASS_4;
    }
    
    return START_FROM_THE_BEGINNING;
}


static void mark_filesystem_consistent (reiserfs_filsys_t * fs)
{
    if (!reiserfs_fs_rw (fs))
        return;

    if (!reiserfs_journal_opened (fs)) {
	/* make sure journal is not standard */
	if (!reiserfs_super_jr_magic (fs->fs_ondisk_sb))
	    reiserfs_exit(EXIT_OPER, "Filesystem with default journal "
			  "must be opened.");
	
	fsck_progress ("WARNING: You must use reiserfstune to specify a new "
	    "journal before mounting it.\n");
	
	/* mark filesystem such that it is not mountable until 
	 * new journaldevice is defined */	
	reiserfs_jp_set_magic (reiserfs_sb_jp (fs->fs_ondisk_sb), NEED_TUNE);
    }

    reiserfs_sb_set_umount (fs->fs_ondisk_sb, FS_CLEANLY_UMOUNTED);
    reiserfs_sb_set_state (fs->fs_ondisk_sb, FS_CONSISTENT);
    
    reiserfs_buffer_mkdirty (fs->fs_super_bh);
}

static void reiserfsck_journal_replay (reiserfs_filsys_t * fs) {
    reiserfs_sb_t *on_place_sb;
    int sb_size = reiserfs_super_size(fs->fs_ondisk_sb);
    
    /* keep the super_block in the separate memory to avoid problems with replaying 
     * broken parameters. */
    on_place_sb = (reiserfs_sb_t *)fs->fs_super_bh->b_data;
    fs->fs_ondisk_sb = misc_getmem (sb_size);
    memcpy (fs->fs_ondisk_sb, on_place_sb, sb_size);

    reiserfs_journal_replay (fs);

    /* Copy checked reliable sb fields from backed up sb to a new one. */
    reiserfs_sb_set_blocks(on_place_sb, reiserfs_sb_get_blocks(fs->fs_ondisk_sb));
    memcpy(reiserfs_sb_jp(on_place_sb), reiserfs_sb_jp(fs->fs_ondisk_sb), 
	sizeof(reiserfs_journal_param_t));    
    reiserfs_sb_set_blksize(on_place_sb, reiserfs_sb_get_blksize(fs->fs_ondisk_sb));
    reiserfs_sb_set_mapmax(on_place_sb, reiserfs_sb_get_mapmax(fs->fs_ondisk_sb));
    memcpy(on_place_sb->s_v1.s_magic, fs->fs_ondisk_sb->s_v1.s_magic, 10);
    reiserfs_sb_set_hash(on_place_sb, reiserfs_sb_get_hash(fs->fs_ondisk_sb));
    reiserfs_sb_set_bmaps(on_place_sb, reiserfs_sb_get_bmaps(fs->fs_ondisk_sb));
    reiserfs_sb_set_version(on_place_sb, reiserfs_sb_get_version(fs->fs_ondisk_sb));
    reiserfs_sb_set_reserved(on_place_sb, 
	reiserfs_sb_get_reserved(fs->fs_ondisk_sb));
    
    if (sb_size == REISERFS_SB_SIZE) {
	reiserfs_sb_set_flags(on_place_sb, reiserfs_sb_get_flags(fs->fs_ondisk_sb));
	memcpy(on_place_sb->s_uuid, fs->fs_ondisk_sb->s_uuid, 16);
	memcpy(on_place_sb->s_label, fs->fs_ondisk_sb->s_label, 16);
    }

    /* get rid of SB copy */
    misc_freemem (fs->fs_ondisk_sb);
    fs->fs_ondisk_sb = on_place_sb;
}

static int the_end (reiserfs_filsys_t * fs)
{
    reiserfs_sb_t * sb;
    int ret = EXIT_FIXED;

    sb = fs->fs_ondisk_sb;

    /* put bitmap and objectid map on place */
    reiserfs_bitmap_delete (fs->fs_bitmap2);
    fs->fs_bitmap2 = fsck_new_bitmap (fs);
    if (!fs->fs_bitmap2->bm_dirty)
	misc_die ("Bitmap not dirty");

//    id_map_flush(proper_id_map (fs), fs);
//    id_map_flush(semantic_id_map (fs), fs);
//    id_map_free(proper_id_map (fs));
//    id_map_free(semantic_id_map (fs));

/*    reiserfs_sb_set_free (sb, reiserfs_bitmap_zeros (fsck_new_bitmap (fs)));*/

    mark_filesystem_consistent (fs);
    reiserfs_buffer_clnoflush (fs->fs_super_bh);

    if (fsck_data(fs)->mounted == MF_RO) {
	reiserfs_warning(stderr, "\nThe partition is mounted ro. It "
			 "is better to umount and mount it again.\n\n");
	ret = EXIT_REBOOT;
    }

    /* write all dirty blocks */
    fsck_progress ("Syncing..");
    fs->fs_dirt = 1;
    util_device_dma_fini(fs->fs_dev, &dma_info);
    reiserfs_fs_close (fs);
    fs = NULL;
    fsck_progress ("finished\n");

    return ret;
}

/* check umounted or read-only mounted filesystems only */
static void prepare_fs_for_check(reiserfs_filsys_t * fs) {
    /* The method could be called from auto_check already. */
    if (fs->fs_flags == O_RDWR) 
	return;

    reiserfs_fs_reopen (fs, O_RDWR);
    
    fsck_data(fs)->mounted = util_device_mounted(fs->fs_file_name);
    
    if (fsck_data(fs)->mounted > 0) {
	if (fsck_data(fs)->mounted == MF_RW) {
	    fsck_progress ("Partition %s is mounted with write permissions, "
		"cannot check it\n", fs->fs_file_name);
	    reiserfs_fs_close(fs);
	    exit(EXIT_USER);
	}
	
	/* If not CHECK mode, lock the process in the memory. */
	if (fsck_mode (fs) != FSCK_CHECK) {
	    if (mlockall(MCL_CURRENT)) {
		    reiserfs_exit(EXIT_OPER, "Failed to lock the process to "
				  "fsck the mounted ro partition. %s.\n", 
				  strerror(errno));
	    }
	}
	
	if (fsck_skip_journal (fs)) {
		reiserfs_exit(EXIT_USER, "Jounrnal of the mounted "
			      "filesystem must be specified.\n");
	}
	
	if (!reiserfs_journal_opened (fs)) {
	    /* just to make sure */
	    reiserfs_panic ("Journal is not opened");
	} else if (reiserfs_journal_params_check(fs)) {
	    reiserfs_fs_close (fs);
	    exit(EXIT_FATAL);
	}
	
	fsck_progress ("Filesystem seems mounted read-only. Skipping journal "
		       "replay.\n");
    } else if (!fsck_skip_journal (fs)) {
	if (reiserfs_journal_params_check(fs)) {
	    reiserfs_fs_close (fs);
	    exit(EXIT_FATAL);
	}
	
	/* filesystem is not mounted, replay journal before checking */
        reiserfsck_journal_replay (fs);
    }
}

static void rebuild_tree (reiserfs_filsys_t * fs) {
    time_t t;
    int ret;

    fsck_rollback_init (state_rollback_file(fs), &fs->fs_blocksize, 
			fsck_data(fs)->log);
    
    prepare_fs_for_check(fs);
    
    ret = reiserfs_bitmap_open(fs);
    if (ret < 0) {
        fsck_progress ("reiserfsck: Could not open bitmap\n");
	reiserfs_fs_close (fs);
	exit(EXIT_OPER);
    } else if (ret > 0) {
	fsck_log("Zero bit found in on-disk bitmap after the last valid bit. "
	    "Fixed.\n");
    }
 
    time (&t);
    fsck_progress ("###########\n"
		   "reiserfsck --rebuild-tree started at %s"
		   "###########\n", ctime (&t));

    switch (where_to_start_from (fs)) {
    case START_FROM_THE_BEGINNING:
	reset_super_block (fs);
	fsck_pass0 (fs);

    case START_FROM_PASS_1:
	reset_super_block (fs);
	fsck_pass1 (fs);
	
    case START_FROM_PASS_2:
	fsck_pass2 (fs);

    case START_FROM_SEMANTIC:
	fsck_semantic (fs);

	/* if --lost+found is set - link unaccessed directories to lost+found
	   directory */
    case START_FROM_LOST_FOUND:	
	fsck_lost (fs);
	
    case START_FROM_PASS_4:
	/* 4. look for unaccessed items in the leaves */
	fsck_cleanup ();
	
	ret = the_end (fs);
    }

    fsck_rollback_fini ();
       
    time (&t);
    fsck_progress ("###########\n"
		   "reiserfsck finished at %s"
		   "###########\n", ctime (&t));
    exit (ret);
}

static void clean_attributes (reiserfs_filsys_t * fs) {
    time_t t;

    time (&t);

    if (reiserfs_sb_get_umount (fs->fs_ondisk_sb) != FS_CLEANLY_UMOUNTED) {
        fsck_progress ("Filesystem is not clean\n"
	    "Check consistency of the partition first.\n");
        exit(EXIT_USER);
    }
    if (reiserfs_sb_get_state (fs->fs_ondisk_sb) != FS_CONSISTENT) {
        fsck_progress ("Filesystem seems to be in unconsistent state.\n"
	    "Check consistency of the partition first.\n");
        exit(EXIT_USER);
    }

    if (reiserfs_super_format (fs->fs_ondisk_sb) != REISERFS_FORMAT_3_6) {
        fsck_progress ("Filesystems of 3_5 format do not support extended "
	    "attributes.\n");
	
        exit(EXIT_USER);
    }
    fsck_progress ("###########\n"
	           "reiserfsck --clean-attributes started at %s"
                   "###########\n", ctime (&t));

    fsck_rollback_init (state_rollback_file(fs), &fs->fs_blocksize, 
			fsck_data(fs)->log);

    prepare_fs_for_check(fs);

    fsck_tree_clean_attr(fs);

    util_device_dma_fini(fs->fs_dev, &dma_info);
    reiserfs_fs_close (fs);
    fs = NULL;
    
    fsck_rollback_fini();

    fsck_progress ("###########\n"
		   "reiserfsck finished at %s"
		   "###########\n", ctime (&t));

    exit (EXIT_FIXED);

}

/* Do not allow buffers to be flushed after finishing to avoid another bitmap 
 * reading on mounting. */
static void fsck_sleep() {
    int res;
    
    res = fork();
    
    if (res == -1) {
	reiserfs_panic ("reiserfsck: Fork failed: %s", strerror(errno));
    } else if (res == 0) {
	/* Make the child process to sleep for 5 secs. */
	sleep(5);
    }
}

static int auto_check (reiserfs_filsys_t *fs) {
    __u16 state;
    int retval = 0;
    
    reiserfs_super_print (stdout, fs, fs->fs_file_name, fs->fs_super_bh, 1);
    
    state = reiserfs_sb_get_state (fs->fs_ondisk_sb);
    if ((state & FS_FATAL) == FS_FATAL) {
	fprintf(stderr, "Filesystem seems to have fatal corruptions. Running "
	    "with --rebuild-tree is required.\n");
	goto error;
    }

    if ((state & FS_ERROR) == FS_ERROR) {
	fprintf(stderr, "Some corruptions on the filesystem were detected. Switching to "
	    "the --fix-fixable mode.\n");
	/* run fixable pass. */
	return 0;
    }
    
    if (state != FS_CONSISTENT)
	fprintf(stderr, "Some strange state was specified in the super block. "
	    "Do usual check.\n");

    prepare_fs_for_check(fs);

    /* Check bitmaps. */
    retval = reiserfs_bitmap_open (fs);
    
    if (retval > 0) {
	fsck_log("Zero bit found in on-disk bitmap after the last valid bit. "
	    "Switching to --fix-fixable mode.\n");
	/* run fixable pass. */
	return 0;
    } else if (retval < 0) {
        fsck_progress ("reiserfsck: Could not open bitmap\n");
	goto error;
    }
    
    if (reiserfs_sb_get_blocks (fs->fs_ondisk_sb) - 
	reiserfs_sb_get_free(fs->fs_ondisk_sb) != 
	fs->fs_bitmap2->bm_set_bits)
    {
	fsck_log("Wrong amount of used blocks. Switching to the --fix-fixable mode.\n");
	/* run fixable pass. */
	return 0;
    }

    check_fs_tree (fs);
    
    if (fsck_data (fs)->check.fatal_corruptions) {		
	fprintf(stderr, "%lu fatal corruption(s) found in the root block. Running "
	    "with the --rebuild-tree is required.\n", 
	    fsck_data (fs)->check.fatal_corruptions);
	goto fatal_error;
    } else if (fsck_data (fs)->check.fixable_corruptions) {
        /* seems that this cannot happen. */
	fprintf(stderr, "%lu fixable corruption(s) found. Switching to "
	    "the --fix-fixable mode.\n", fsck_data (fs)->check.fixable_corruptions);
	fsck_data (fs)->check.fixable_corruptions = 0;
	/* run fixable pass. */
	return 0;
    }
    
    util_device_dma_fini(fs->fs_dev, &dma_info);
    
    fsck_sleep();
    
    reiserfs_fs_close (fs);
    /* do not do anything else. */    
    exit (EXIT_OK);

fatal_error:
    reiserfs_sb_set_state(fs->fs_ondisk_sb, FS_FATAL);
    reiserfs_buffer_mkdirty (fs->fs_super_bh);
    reiserfs_buffer_write(fs->fs_super_bh);	
error:
    util_device_dma_fini(fs->fs_dev, &dma_info);
    reiserfs_fs_close(fs);
    exit(EXIT_FATAL);
}

/* check umounted or read-only mounted filesystems only */
static void check_fs (reiserfs_filsys_t * fs)
{
    int retval = EXIT_OK;
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

    fsck_rollback_init (state_rollback_file(fs), &fs->fs_blocksize, 
			fsck_data(fs)->log);
    
    prepare_fs_for_check (fs);

    if (!fs->fs_bitmap2)
	/* It could be done on auto_check already. */
	retval = reiserfs_bitmap_open (fs);

    if (retval > 0) {
	if (fsck_mode(fs) != FSCK_FIX_FIXABLE) {
	    fsck_log("Zero bit found in on-disk bitmap after the last valid "
		"bit.\n");
	    
	    one_more_corruption(fs, FIXABLE);
	} else {
	    fsck_log("Zero bit found in on-disk bitmap after the last valid "
		"bit. Fixed.\n");
	}
    } else if (retval < 0) {
        fsck_progress ("reiserfsck: Could not open bitmap\n");
	reiserfs_fs_close (fs);
	exit(EXIT_OPER);
    }

    check_fs_tree (fs);

    fsck_semantic_check ();
    
    if (fsck_data (fs)->check.fatal_corruptions) {
	fsck_progress ("%lu found corruptions can be fixed only when running with "
	    "--rebuild-tree\n", fsck_data (fs)->check.fatal_corruptions);
	
        reiserfs_sb_set_state (fs->fs_ondisk_sb, FS_FATAL);
        reiserfs_buffer_mkdirty (fs->fs_super_bh);
	retval = EXIT_FATAL;
    } else if (fsck_data (fs)->check.fixable_corruptions) {
        /* fixable corruptions found */
	if (fsck_mode (fs) == FSCK_FIX_FIXABLE) {
            /* fixable corruptions found and fix-fixable has not fixed them, 
	     * do rebuild-tree */
	    
            fsck_progress ("Fatal error: %lu fixable corruptions found after "
		"--fix-fixable.\n", fsck_data (fs)->check.fixable_corruptions);
	    retval = EXIT_OPER;
        } else {
	    fsck_progress ("%lu found corruptions can be fixed when running with "
		"--fix-fixable\n", fsck_data (fs)->check.fixable_corruptions);
	    
	    retval = EXIT_FIXABLE;
        }
        reiserfs_sb_set_state (fs->fs_ondisk_sb, FS_ERROR);
        reiserfs_buffer_mkdirty (fs->fs_super_bh);
    } else {
	fsck_stage_report (FS_CHECK, fs);
	fsck_progress ("No corruptions found\n");

	if (fsck_mode(fs) != FSCK_CHECK) {
		if (util_device_mounted(fs->fs_file_name) == MF_RO) {
			reiserfs_warning(stderr, "\nThe partition is mounted ro. It is better "
					 "to umount and mount it again.\n\n");
			retval = EXIT_REBOOT;
		} else 
			retval = EXIT_FIXED;
	} else
		retval = EXIT_OK;

	mark_filesystem_consistent (fs);
    }
   
    if (fsck_mode (fs) == FSCK_FIX_FIXABLE && !fsck_data (fs)->check.fatal_corruptions)
        id_map_flush(proper_id_map (fs), fs);
        
    id_map_free(proper_id_map (fs));
    util_device_dma_fini(fs->fs_dev, &dma_info);
    reiserfs_fs_close (fs);
    fs = NULL;
    
    fsck_rollback_fini();
    
    //clear_relocated_list();    
    
    time (&t);
    fsck_progress ("###########\n"
		   "reiserfsck finished at %s"
		   "###########\n", ctime (&t));

    exit(retval);
}

static int open_devices_for_rollback (char * file_name, 
    struct fsck_data * data) 
{
    int fd;

    fd = open (file_name, O_RDWR 
#ifdef O_LARGEFILE
	       | O_LARGEFILE
#endif
	       );

    if (fd == -1) {
        reiserfs_warning (stderr, "reiserfsck: Cannot not open the fs "
	    "partition %s\n", file_name);

        return -1;
    }

    fs = misc_getmem (sizeof (*fs));
    fs->fs_dev = fd;
    fs->fs_vp = data;
    strncpy(fs->fs_file_name, file_name, sizeof(fs->fs_file_name));

    if (data->journal_dev_name && 
	strcmp (data->journal_dev_name, file_name)) 
    {
	fs->fs_journal_dev = open (data->journal_dev_name, O_RDWR 
#ifdef O_LARGEFILE
				   | O_LARGEFILE
#endif
				   );
	
	if (fs->fs_journal_dev == -1) {
	    reiserfs_warning (stderr, "Cannot open journal partition\n");
     	    return -1;
	}
    }

    if (fsck_rollback_prep (state_rollback_file(fs), fsck_data(fs)->log))
        return -1;

    return 0;    	
}

static void fsck_perform_rollback (reiserfs_filsys_t * fs) {
    time_t t;

    time (&t);
    fsck_progress ("###########\n"
	           "reiserfsck --rollback-fsck-changes started at %s"
                   "###########\n", ctime (&t));

    fsck_rollback (fs->fs_dev, fs->fs_journal_dev, fsck_progress_file (fs));
    fsck_rollback_fini ();

    close (fs->fs_journal_dev);
    close (fs->fs_dev);
    misc_freemem (fs);

    time (&t);
    fsck_progress ("###########\n"
		   "reiserfsck finished at %s"
		   "###########\n", ctime (&t));

    exit(EXIT_FIXED);
}

int main (int argc, char * argv [])
{
    char * file_name;
    struct fsck_data * data;
    struct rlimit rlim = {RLIM_INFINITY, RLIM_INFINITY};
    int retval;
    int error;
    
    lost_found_dir_key.k2_dir_id = cpu_to_le32(lost_found_dir_key.k2_dir_id);
    lost_found_dir_key.k2_objectid = cpu_to_le32(lost_found_dir_key.k2_objectid);
    /* this is only needed (and works) when running under 2.4 on regular files */
    if (setrlimit (RLIMIT_FSIZE, &rlim) == -1) {
	reiserfs_warning (stderr, "Cannot change the system limit for file size "
	    "with setrlimit: %s\n", strerror(errno));
    }

    data = misc_getmem (sizeof (struct fsck_data));

    file_name = parse_options (data, argc, argv);

    if (data->mode != FSCK_AUTO)
	util_print_banner ("reiserfsck");

    if (data->mode == DO_NOTHING) {
	misc_freemem (data);
	exit(EXIT_OK);
    }

    if (data->options & OPT_BACKGROUND) {
	/* running in background reiserfsck appends progress information into
           'fsck.run'. Logs get there if log file was not specified*/
	data->options |= OPT_QUIET;
	data->progress = fopen ("fsck.run", "a+");
	if (!data->progress) {
	    reiserfs_exit(EXIT_OPER, "reiserfsck: Cannot not open \"fsck.run\"");
	}

	if (data->log == stdout)
	    /* no log file specifed - redirect log into 'fsck.run' */
	    data->log = data->progress;

	retval = fork ();
	if (retval == -1) {
	    reiserfs_panic ("reiserfsck: Fork failed: %s", strerror(errno));
	} else if (retval != 0) {
	    exit(EXIT_OPER);
	}
	reiserfs_warning (stderr, "\nReiserfsck is running in background as "
	    "[%d],\nmake sure that it gets all the confirmations from stdin "
	    "that it requests.\n\n", getpid ());
    }

    /* This asks for confirmation also. */
    if (data->mode != FSCK_AUTO)
	warn_what_will_be_done(file_name, data);

    if (data->mode == FSCK_ROLLBACK_CHANGES) {
    	if (open_devices_for_rollback (file_name, data) == -1)
    	    exit(EXIT_OPER);
    } else {
	fs = reiserfs_fs_open (file_name, O_RDONLY, &error, data, 
			       data->mode != FSCK_SB);
		
	if (error) {
		reiserfs_exit(EXIT_OPER, "Failed to open the device "
			      "'%s': %s\n\n", file_name, strerror(error));
	} 
	
	if (data->mode != FSCK_SB) {
	    if (fs == NULL) {
		reiserfs_exit(EXIT_OPER, "Failed to open the filesystem.\n\n"
			      "If the partition table has not been changed, "
			      "and the partition is\nvalid  and  it really  "
			      "contains  a reiserfs  partition,  then the\n"
			      "superblock  is corrupted and you need to run "
			      "this utility with\n--rebuild-sb.\n");
	    }
	    if (fsck_skip_journal (fs) && 
		!reiserfs_super_jr_magic (fs->fs_ondisk_sb)) 
	    {
		reiserfs_warning (stderr, "Filesystem with default journal found, "
			"--no-journal-available is ignored\n");
		fsck_data(fs)->options &= ~OPT_SKIP_JOURNAL;
	    }
	
	    if (!fsck_skip_journal (fs)) {
		retval = reiserfs_journal_open(fs, data->journal_dev_name, O_RDONLY);
		
		if (retval) {
	            fsck_progress ("Failed to open the journal device (%s).\n", 
			data->journal_dev_name);
		    
		    if (retval == 1) {
			    fsck_progress ("Run --rebuild-sb to rebuild journal parameters.\n");
		    }
		    
		    reiserfs_fs_close (fs);
		    exit(EXIT_OPER);
	        }
	    }
	
	    if (data->options & BADBLOCKS_FILE) {
		if (util_badblock_load (fs, badblocks_file) != 0) 
		    exit(EXIT_OPER);
	    }
	    
	    register_timer();
    	}
    }

    switch (data->mode) {
    case FSCK_SB:
	rebuild_sb (fs, file_name, data);
	break;
    
    case FSCK_AUTO:
	/* perform some light-weight checks. If error, do fixable job. */
	if (auto_check (fs))
	    break;
	data->mode = FSCK_FIX_FIXABLE;
    case FSCK_CHECK:
    case FSCK_FIX_FIXABLE:
	check_fs (fs);
	break;

    case FSCK_REBUILD:
    case DO_TEST:
	rebuild_tree (fs);
	break;

    case FSCK_ROLLBACK_CHANGES:
	fsck_perform_rollback (fs);
 	break;
    case FSCK_CLEAN_ATTRIBUTES:
	clean_attributes (fs);
    }
    
    exit(EXIT_OPER);
}

