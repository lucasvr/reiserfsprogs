/*
 * Copyright 2000-2003 by Hans Reiser, licensing governed by 
 * reiserfsprogs/README
 */

#include "debugreiserfs.h"

reiserfs_filsys_t * fs;

#define print_usage_and_exit() {\
fprintf (stderr, "Usage: %s [options] device\n\n\
Options:\n\
  -d\t\tprint blocks details of the internal tree\n\
  -D\t\tprint blocks details of all used blocks\n\
  -m\t\tprint bitmap blocks\n\
  -o\t\tprint objectid map\n\n\
  -J\t\tprint journal header\n\
  -j journal_device\n\t\tprint journal\n\
  -p\t\tsend filesystem metadata to stdout\n\
  -S\t\thandle all blocks, not only used\n\
  -1 block\tblock to print\n\
  -q\t\tno speed info (for -p, -s and -n)\n\n", argv[0]);\
  exit (16);\
}

/*
   -B file\textract list of badblocks\n\

 Undocumented options:
  -a map_file\n\tstore to the file the map of file. Is used with -n, -N, -r, -f\n
  -f creates a file map.\n
  -S\t\tgo through whole device when running -p, -s or -n\n\
  -U\t\tgo through unused blocks only when running -p, -s or -n\n\
  -D\t\tprint blocks details scanning the device, not the tree as -d does\n\
  -b bitmap_file\n\t\trunning -p, -s or -n read blocks marked in this bitmap only\n\
  -n\tscan device for specific name in reiserfs directories\n\
  -N\tscan tree for specific key in reiserfs directories\n\
  -s\tscan device either for specific key or for any metadata\n\
  -C\tallow to change reiserfs metadata\n\
  -r\tprint map file content.\n\
  -J\tsearch block numbers in the journal\n\
  -t\tstat the device\n\
*/

#if 1
struct reiserfs_fsstat {
    int nr_internals;
    int nr_leaves;
    int nr_files;
    int nr_directories;
    int nr_unformatted;
} g_stat_info;
#endif

static void print_disk_tree (reiserfs_filsys_t * fs, unsigned long block_nr)
{
    struct buffer_head * bh;
    int i, count;
    static int level = -1;
	
    if (level == -1)
		level = get_sb_tree_height (fs->fs_ondisk_sb);

    bh = bread (fs->fs_dev, block_nr, fs->fs_blocksize);
    if (!bh) {
		die ("Could not read block %lu\n", block_nr);
    }
    level --;

    if (level < 1)
		die ("level too small");
	
    if (level != get_blkh_level (B_BLK_HEAD (bh))) {
		printf ("%d expected, %d found in %lu\n", level, get_blkh_level (B_BLK_HEAD (bh)),
				bh->b_blocknr);
    }

    if (is_internal_node (bh)) {
		struct disk_child * dc;

		g_stat_info.nr_internals ++;
		print_block (stdout, fs, bh, data (fs)->options & PRINT_TREE_DETAILS, -1, -1);

		dc = B_N_CHILD (bh, 0);
		count = B_NR_ITEMS(bh);
		for (i = 0; i <= count; i++, dc++)
			print_disk_tree (fs, get_dc_child_blocknr (dc));
      
    } else if (is_leaf_node (bh)) {
		struct item_head *ih;
	
		g_stat_info.nr_leaves ++;
		print_block (stdout, fs, bh, data (fs)->options & PRINT_TREE_DETAILS, -1, -1);
	
		ih = B_N_PITEM_HEAD (bh, 0);
		count = leaf_item_number_estimate(bh);
		for (i = 0; i <= count; i++, ih++) {
			if (is_indirect_ih(ih))
				g_stat_info.nr_unformatted += I_UNFM_NUM(ih);
		}
    } else {
		print_block (stdout, fs, bh, data (fs)->options & PRINT_TREE_DETAILS, -1, -1);
		reiserfs_warning (stdout,  "print_disk_tree: bad block type (%b)\n", bh);
    }
    brelse (bh);
    level ++;
}

static void print_disk_blocks (reiserfs_filsys_t * fs)
{
    int type;
    unsigned long done = 0, total;
    struct buffer_head * bh;
    unsigned int j;
	
    total = reiserfs_bitmap_ones (input_bitmap(fs));
	
    for (j = 0; j < get_sb_block_count (fs->fs_ondisk_sb); j ++) {
		if (!reiserfs_bitmap_test_bit (input_bitmap (fs), j))
			continue;
		
		print_how_far (stderr, &done, total, 1, be_quiet (fs));
		
		bh = bread (fs->fs_dev, j, fs->fs_blocksize);
		if (!bh) {
			reiserfs_warning (stderr, "could not read block %lu\n", j);
			continue;
		}
		
		type = who_is_this (bh->b_data, bh->b_size);
		if (type != THE_UNKNOWN)
			print_block (stdout, fs, bh, PRINT_TREE_DETAILS | PRINT_DIRECT_ITEMS, -1, -1);
		if (type == THE_INTERNAL)
			g_stat_info.nr_internals ++;
		else if (type == THE_LEAF || type == HAS_IH_ARRAY)
			g_stat_info.nr_leaves ++;
	    
		brelse (bh);
    }
    fprintf (stderr, "\n");
}


void pack_one_block (reiserfs_filsys_t * fs, unsigned long block);
static void print_one_block (reiserfs_filsys_t * fs, unsigned long block)
{
    struct buffer_head * bh;
    
    if (!fs->fs_bitmap2) {
        struct buffer_head * bm_bh;
        unsigned long bm_block;
        
        if (spread_bitmaps (fs))
            bm_block = ( block / (fs->fs_blocksize * 8) ) ? 
				(block / (fs->fs_blocksize * 8)) * (fs->fs_blocksize * 8) : 
			fs->fs_super_bh->b_blocknr + 1;
        else
            bm_block = fs->fs_super_bh->b_blocknr + 1 + (block / (fs->fs_blocksize * 8));
        
        bm_bh = bread (fs->fs_dev, bm_block, fs->fs_blocksize);
        if (bm_bh) {
            if ( misc_test_bit((block % (fs->fs_blocksize * 8)), bm_bh->b_data) )
                fprintf (stderr, "%lu is used in ondisk bitmap\n", block);
            else
				fprintf (stderr, "%lu is free in ondisk bitmap\n", block);
	        
            brelse (bm_bh);
        }
    } else {
        if (reiserfs_bitmap_test_bit (fs->fs_bitmap2, block))
			fprintf (stderr, "%lu is used in ondisk bitmap\n", block);
        else
			fprintf (stderr, "%lu is free in ondisk bitmap\n", block);
    }
    
    bh = bread (fs->fs_dev, block, fs->fs_blocksize);
    if (!bh) {
		printf ("print_one_block: bread failed\n");
		return;
    }
	
    if (debug_mode (fs) == DO_PACK) {
		pack_one_block (fs, bh->b_blocknr);
		brelse (bh);
		return;
    }
	
    if (who_is_this (bh->b_data, bh->b_size) != THE_UNKNOWN)
		print_block (stdout, fs, bh, PRINT_TREE_DETAILS, -1, -1);
    else
		printf ("Looks like unformatted\n");
    brelse (bh);
    return;
}

/* debugreiserfs -p or -P compresses reiserfs meta data: super block, journal,
   bitmap blocks and blocks looking like leaves. It may save "bitmap" of
   blocks they packed in the file of special format. Reiserfsck can then load
   "bitmap" saved in that file and build the tree of blocks marked used in
   that "bitmap"
*/
char * where_to_save;
char * badblocks_file;
char * corruption_list_file;

static char * parse_options (struct debugreiserfs_data * data, int argc, char * argv [])
{
    int c;
    char * tmp;
    
    data->scan_area = USED_BLOCKS;
    data->mode = DO_DUMP;

    while ((c = getopt (argc, argv, "a:b:C:F:SU1:psn:Nfr:dDomj:J:qtZl:L")) != EOF) {
		switch (c) {
		case 'a': /* -r will read this, -n and -N will write to it */
			asprintf (&data->map_file, "%s", optarg);
			break;
			
		case 'b':
			/* will load bitmap from a file and read only blocks
               marked in it. This is for -p and -s */
			asprintf (&data->input_bitmap, "%s", optarg);
			data->scan_area = EXTERN_BITMAP;
			break;
			
		case 'S':
			/* have debugreiserfs -p or -s to read all the device */
			data->scan_area = ALL_BLOCKS;
			break;
			
		case 'U':
			/* have debugreiserfs -p or -s to read unused blocks only */
			data->scan_area = UNUSED_BLOCKS;
			break;
			
		case '1':	/* print a single node */
			data->block = strtol (optarg, &tmp, 0);
			if (*tmp)
				die ("parse_options: bad block number");
			break;
			
		case 'C':
			data->mode = DO_CORRUPT_ONE;
			data->block = strtol (optarg, &tmp, 0);
			if (*tmp) {
				die ("parse_options: bad block number");
			}
			break;
			
		case 'F':
			data->mode = DO_CORRUPT_FILE;
			if (asprintf (&corruption_list_file, "%s", optarg) == -1 ) {    
				die ("parse_options: bad list corruption file");
			}
			break;
			
		case 'p':
			data->mode = DO_PACK;
			break;
			
		case 't':
			data->mode = DO_STAT;
			break;
			
		case 's':
			/* read the device and print reiserfs blocks which contain defined key */
			data->mode = DO_SCAN;
			break;
			
		case 'n': /* scan for names matching a specified pattern */
			data->mode = DO_SCAN_FOR_NAME;
			data->pattern = optarg;
			/*asprintf (&data->pattern, "%s", optarg);*/
			break;
			
		case 'N': /* search name in the tree */
			data->mode = DO_LOOK_FOR_NAME;
			break;
			
		case 'f':
			data->mode = DO_FILE_MAP;
			break;

		case 'r':
			asprintf (&data->recovery_file, "%s", optarg);
			data->mode = DO_RECOVER;
			break;

		case 'd':
			/*  print leaf details from internal tree */
			data->options |= PRINT_TREE_DETAILS;
			break;
			
		case 'D':
			/*  print leaf details accordingly the bitmap - can be used with -S */
			data->options |= PRINT_DETAILS;
			break;
			
		case 'o':
			/* print objectid map */
			data->options |= PRINT_OBJECTID_MAP;
			break;
			
		case 'm':	/* print a block map */
		case 'M':	/* print a block map with details */
			data->options |= PRINT_BITMAP;
			break;
			
		case 'j': /* -j must have a parameter */
			data->options |= PRINT_JOURNAL;
			data->journal_device_name = optarg;
			break;
			
		case 'J':
			data->options |= PRINT_JOURNAL_HEADER;
			data->journal_device_name = optarg;
			break;
			
		case 'R': /* read block numbers from stdin and look for them in the
                     journal */
			data->mode = DO_SCAN_JOURNAL;
			data->JJ ++;
			break;
			
		case 'B':              /*disabled for a while*/
			asprintf (&badblocks_file, "%s", optarg);
			data->mode = DO_EXTRACT_BADBLOCKS;
			break;
		case 'q':
			/* this makes packing to not show speed info during -p or -P */
			data->options |= BE_QUIET;
			break;
		case 'Z':
			data->mode = DO_ZERO_BITMAP;
			break;
			
		case 'l': /* --logfile */
			data->log_file_name = optarg;
			data->log = fopen (optarg, "w");
			if (!data->log)
				fprintf (stderr, "debugreiserfs: Cannot not open \'%s\': %s", 
						 optarg, strerror(errno));	    
			break;
		case 'L' :
			/* random fs corruption */
			data->mode = DO_RANDOM_CORRUPTION;
			break;
		}
    }
    
    if (optind != argc - 1)
		/* only one non-option argument is permitted */
		print_usage_and_exit();
	
    data->device_name = argv[optind];
    return argv[optind];
}

void pack_partition (reiserfs_filsys_t * fs);

static void do_pack (reiserfs_filsys_t * fs)
{
    if (certain_block (fs))
		pack_one_block (fs, certain_block (fs));
    else
		pack_partition (fs);
}

/*
static int comp (const void * vp1, const void * vp2)
{
    const int * p1, * p2;

    p1 = vp1; p2 = vp2;

    if (*p1 < *p2)
	return -1;
    if (*p1 > *p2)
	return 1;
    return 0;
}
*/

static void init_bitmap (reiserfs_filsys_t * fs)
{
    FILE * fp;
    unsigned long block_count;
	
	
    if (reiserfs_open_ondisk_bitmap (fs) < 0)
		reiserfs_panic ("Could not open ondisk bitmap");
	
    block_count = get_sb_block_count (fs->fs_ondisk_sb);
	
    switch (scan_area (fs)) {
    case ALL_BLOCKS:
		input_bitmap (fs) = reiserfs_create_bitmap (block_count);
		reiserfs_bitmap_fill (input_bitmap (fs));
		reiserfs_warning (stderr, "Whole device (%d blocks) is to be scanned\n", 
						  reiserfs_bitmap_ones (input_bitmap (fs)));	
		break;
		
    case USED_BLOCKS:
		reiserfs_warning (stderr, "Loading on-disk bitmap .. ");
		input_bitmap (fs) = reiserfs_create_bitmap (block_count);
		reiserfs_bitmap_copy (input_bitmap (fs), fs->fs_bitmap2);
		reiserfs_warning (stderr, "%d bits set - done\n",
						  reiserfs_bitmap_ones (input_bitmap (fs)));
		break;
		
    case UNUSED_BLOCKS:
		reiserfs_warning (stderr, "Loading on-disk bitmap .. ");
		input_bitmap (fs) = reiserfs_create_bitmap (block_count);
		reiserfs_bitmap_copy (input_bitmap (fs), fs->fs_bitmap2);
		reiserfs_bitmap_invert (input_bitmap (fs));
		reiserfs_warning (stderr, "%d bits set - done\n",
						  reiserfs_bitmap_ones (input_bitmap (fs)));
		break;
		
    case EXTERN_BITMAP:
		fp = fopen (input_bitmap_file_name(fs), "r");
		if (!fp)
			reiserfs_panic ("init_bitmap: could not load bitmap: %m\n");
		
		input_bitmap (fs) = reiserfs_bitmap_load (fp);
		if (!input_bitmap (fs))
			reiserfs_panic ("could not load fitmap from \"%s\"", 
							input_bitmap_file_name(fs));
		reiserfs_warning (stderr, "%d blocks marked in the given bitmap\n",
						  reiserfs_bitmap_ones (input_bitmap (fs)));
		fclose (fp);
		break;
		
    default:
		reiserfs_panic ("No area to scan specified");
    }
}

/* FIXME: statistics does not work */
static void do_dump_tree (reiserfs_filsys_t * fs)
{
    if (certain_block (fs)) {
		print_one_block (fs, certain_block (fs));
		return;
    }
	
    if (data (fs)->options & PRINT_JOURNAL || data (fs)->options & PRINT_JOURNAL_HEADER) {
		if (!reiserfs_journal_opened (fs) && 
			reiserfs_open_journal (fs, data (fs)->journal_device_name, O_RDONLY)) 
			{
				printf ("Could not open journal\n");
				return;
			}
    }
	
    print_filesystem_state (stdout, fs);
    print_block (stdout, fs, fs->fs_super_bh);
	
    if (data (fs)->options & PRINT_JOURNAL)
		print_journal (fs);

    if (data (fs)->options & PRINT_JOURNAL_HEADER)
		print_journal_header (fs);
	
    if (data (fs)->options & PRINT_OBJECTID_MAP)
		print_objectid_map (stdout, fs);
	
    if (data (fs)->options & PRINT_BITMAP)
		print_bmap (stdout, fs, 0/*opt_print_block_map == 1 ? 1 : 0*/);
	
    if (data (fs)->options & PRINT_DETAILS)
        init_bitmap (fs);
	
    if (data (fs)->options & PRINT_DETAILS || data (fs)->options & PRINT_TREE_DETAILS) {
		if (data (fs)->options & PRINT_DETAILS)
			print_disk_blocks (fs);
		else
			print_disk_tree (fs, get_sb_root_block (fs->fs_ondisk_sb));
		
		/* print the statistic */
		printf ("File system uses %d internal + %d leaves + %d unformatted nodes = %d blocks\n",
				g_stat_info.nr_internals, g_stat_info.nr_leaves, g_stat_info.nr_unformatted,
				g_stat_info.nr_internals + g_stat_info.nr_leaves + g_stat_info.nr_unformatted);
    }
}

void extract_badblock_list ()
{
    FILE * fd;
    struct key badblock_key = {-1, -1, {{0, 0}}};
    struct key * rdkey;
    struct path badblock_path;
    struct item_head * tmp_ih;
    __u32 i;
    __u32 * ind_item;
	
	
    fd = fopen (badblocks_file, "w");
	
    if (fd == NULL) {
        reiserfs_warning (stderr, "debugreiserfs: could not open badblock file %s\n",
						  badblocks_file);
        exit (1);
    }
	
    badblock_path.path_length = ILLEGAL_PATH_ELEMENT_OFFSET;
	
    set_type_and_offset (KEY_FORMAT_2, &badblock_key, 1, TYPE_INDIRECT);
	
    while (1) {
		reiserfs_search_by_key_4 (fs, &badblock_key, &badblock_path);
		
		if (get_blkh_nr_items ( B_BLK_HEAD (get_bh(&badblock_path))) <= PATH_LAST_POSITION (&badblock_path)) {
            break;
		}
		
		tmp_ih = get_ih(&badblock_path);
		
		if (get_key_dirid(&tmp_ih->ih_key) != (__u32)-1 ||
			get_key_objectid(&tmp_ih->ih_key) != (__u32)-1) {
			break;
		}
		
		/* even if offset or type are corrupted it could be a badblock list */
		if (!is_indirect_ih (tmp_ih)) {
			reiserfs_warning (stderr, "$s: a badblock item which was found has a wrong type.\n"
							  " It probable contains not correct bad block list, check it.\n", __FUNCTION__);
		}
		
		ind_item = (__u32 *)B_I_PITEM (get_bh (&badblock_path), tmp_ih);
		for (i = 0; i < get_ih_item_len (tmp_ih) / UNFM_P_SIZE; i++, ind_item++)
			fprintf (fd, "%u\n", le32_to_cpu (*ind_item));
		
		if ((rdkey = get_next_key_2 (&badblock_path)) == NULL)	
			break;
		else
			badblock_key = *rdkey;
		
		pathrelse (&badblock_path);
    }
	
    pathrelse (&badblock_path);
    fclose (fd);
}

static int str2int (char * str, int * res)
{
    int val;
    char * tmp;
	
    val = (int) strtol (str, &tmp, 0);
    if (tmp == str)
		/* could not convert string into a number */
		return 0;
    *res = val;
    return 1;
}

void do_corrupt_blocks (reiserfs_filsys_t * fs)
{
	char * line;
	FILE * fd;
	size_t n = 0;
	int numblock;
	
	fd = fopen (corruption_list_file, "r");
	if (fd == NULL) {
		reiserfs_warning (stderr, "debugreiserfs: could not open corruption_list_file %s\n",
						  corruption_list_file);
		exit (1);
	}
	
	while (1) {
		line = NULL;
		n = 0;
		if (getline (&line, &n, fd) == -1) {
			break;
		}
		
		/* remove '\n' */
		line [strlen (line) - 1] = '\0';
		
		if (str2int(line, &numblock)) {
			data (fs)->block = (unsigned long) numblock;
		} else {
			do_corrupt_one_block(fs, line);
		}
		printf ("before free line : %s\n", line);
		free (line);
		printf ("after free\n");
		reiserfs_reopen (fs, O_RDWR);
	}
	fclose (fd);
	return;
}

void zero_bitmap_blocks(reiserfs_filsys_t * fs) {
    unsigned long done, total, i;
    struct buffer_head * bh;
	
    reiserfs_reopen (fs, O_RDWR);
    
    total = reiserfs_bitmap_ones (input_bitmap (fs));
    done = 0;
	
    for (i = 0; i < input_bitmap(fs)->bm_bit_size; i ++) {
		if (!reiserfs_bitmap_test_bit (input_bitmap(fs), i))
			continue;
		
		bh = getblk(fs->fs_dev, i, fs->fs_blocksize);
		
		if (!bh)
			die("Could not get block %lu\n", i);
		
		memset(bh->b_data, 0, fs->fs_blocksize);
		mark_buffer_dirty(bh);
		mark_buffer_uptodate(bh, 0);
		
		bwrite(bh);
		
		brelse (bh);
		print_how_far (stderr, &done, total, 1, be_quiet (fs));
    }
	
    fprintf(stderr, "\n");
    fflush(stderr);
}

/* FIXME: need to open reiserfs filesystem first */
int main (int argc, char * argv[])
{
    char * file_name;
    int error;
    struct debugreiserfs_data * data;
	
    print_banner ("debugreiserfs");
	
    data = getmem (sizeof (struct debugreiserfs_data));
    file_name = parse_options (data, argc, argv);
    
    fs = reiserfs_open (file_name, O_RDONLY, &error, data, 0);
    
    if (no_reiserfs_found (fs)) {
		fprintf (stderr, "\n\ndebugreiserfs: can not open reiserfs on \"%s\": %s\n\n",
				 file_name, error ? strerror (error) : "no filesystem found");
		exit(1) ;
    }
    
    if (reiserfs_open_journal (fs, data (fs)->journal_device_name, O_RDONLY)) {
		fprintf(stderr, "\ndebugreiserfs: Failed to open the fs journal.\n");
		exit(1);
    }
	
    switch (debug_mode (fs)) {
    case DO_STAT:
		init_bitmap (fs);
		do_stat (fs);
		break;
		
    case DO_PACK:
		init_bitmap (fs);
		do_pack (fs);
		break;
		
    case DO_CORRUPT_ONE:
        reiserfs_reopen (fs, O_RDWR);
		do_corrupt_one_block (fs, (char *)NULL);
		break;
		
    case DO_CORRUPT_FILE:
        reiserfs_reopen (fs, O_RDWR);
        do_corrupt_blocks (fs);
		break;
	case DO_RANDOM_CORRUPTION:
        reiserfs_reopen (fs, O_RDWR);
		/*
		do_leaves_corruption (fs);
		do_bitmap_corruption (fs);
		*/		
		do_fs_random_corrupt (fs);
		break;
		
    case DO_DUMP:
		do_dump_tree (fs);
		break;
		
    case DO_SCAN:
    case DO_SCAN_FOR_NAME:
    case DO_LOOK_FOR_NAME:
    case DO_SCAN_JOURNAL:
		init_bitmap (fs);
		do_scan (fs);
		break;
		
    case DO_FILE_MAP:
		print_map(fs);
		break;
		
		
    case DO_RECOVER:
		do_recover (fs);
		break;
		
    case DO_TEST:
		/*do_test (fs);*/
		break;
    case DO_EXTRACT_BADBLOCKS:
        reiserfs_warning (stderr, "Will try to extract list of bad blocks and save it to '%s' file\n", badblocks_file);
		extract_badblock_list ();
        reiserfs_warning (stderr, "Done\n\n");
    	break;
    }
	
    reiserfs_close (fs);
    if (data(fs)->log) {
		fclose (data(fs)->log); 
    }
    return 0;
}

/*
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 4
   tab-width: 4
   fill-column: 80
   End:
*/



