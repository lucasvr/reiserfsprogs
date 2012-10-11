#define _GNU_SOURCE

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#include <asm/types.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <sys/vfs.h>

#include "io.h"
#include "misc.h"
#include "reiserfs_lib.h"

enum {
	USED_BLOCKS,
	ALL_BLOCKS,
	EXTERN_BITMAP
};

typedef struct corrupter_options {
    int bitmap_kind;
    char * bm_name;
    reiserfs_bitmap_t * bm;
    int block_size;
    int offset;
} corrupter_options_t;

#define print_usage_and_exit() {\
printf ("\nUsage: %s [options] device\n"\
"Options:\n"\
"  -b bitmap\t\tscan blocks marked in bitmap\n"\
"  -S\t\t\tscan-whole-partition\n"\
"  -s blocksize\t\tfs blocksize\n"\
"  -o bitmap\t\tskip offset blocks\n\n"\
  , argv[0]);\
  exit (16);\
}

static char * parse_options (corrupter_options_t * options, int argc, char * argv [])
{
    int c;
    options->bitmap_kind = USED_BLOCKS;
    options->block_size = 4096;
    options->offset = 0;
    options->bm_name = NULL;
    options->bm = NULL;

    while (1) {
	int option_index;

	c = getopt_long (argc, argv, "Sb:s:o:",
			 NULL, &option_index);
	if (c == -1)
	    break;
	
	switch (c) {
	case 0:
	    /* long-only options */
	
	    break;

	case 'b': /* --scan-marked-in-bitmap */
	    options->bitmap_kind = EXTERN_BITMAP;
	    options->bm_name = optarg;
	    break;

	case 'S': /* --scan-whole-partition */
	    options->bitmap_kind = ALL_BLOCKS;
	    break;

	case 'o': /* --scan-marked-in-bitmap */
	    options->offset = atoi (optarg);
	    break;
	
	case 's': /* --scan-marked-in-bitmap */
	    options->block_size = atoi (optarg);
	    break;
	
	default:
	    print_usage_and_exit();
	}
    }

    if (optind != argc - 1)
	print_usage_and_exit();

    return argv[optind];
}

void load_bitmap_from_file (corrupter_options_t * options) {
    FILE * fd;

    fd = fopen (options->bm_name, "r");
	
    if (!fd)
	reiserfs_panic ("%s: could not load bitmap: %s\n", __FUNCTION__, strerror(errno));

    options->bm = reiserfs_bitmap_load (fd);
	
    if (!options->bm)
        reiserfs_panic ("could not load fitmap from \"%s\"", options->bm_name);

    fclose (fd);
}

void warn_what_will_be_done (corrupter_options_t * options, char * file_name, int bl_count) {
    printf ("====================\n");
    printf ("\tprogram will corrupt %s\n", file_name);
    printf ("\tblock size is %d\n", options->block_size);
    printf ("\toffset is %d blocks\n", options->offset);

    if (options->bitmap_kind == ALL_BLOCKS) {
	printf ("\tall formatted blocks, bitmap and journal blocks will be corrupted\n");
    } else if (options->bitmap_kind == USED_BLOCKS) {
	printf ("\tall formatted blocks, bitmap and journal blocks which are used in fs bitmap will be corrupted\n");
    } else if (options->bitmap_kind == EXTERN_BITMAP) {
	printf ("\tall formatted blocks, bitmap and journal blocks from bitmap %s will be corrupted\n", options->bm_name);
    }

    printf ("\tblocks count %d\n", bl_count);
    printf ("====================\n");
}

int main (int argc, char * argv []) {
    corrupter_options_t options;
    char * file_name;
    unsigned int block_count, i;
    reiserfs_filsys_t * fs;

    file_name = parse_options (&options, argc, argv);

    fs = reiserfs_open (file_name, O_RDONLY, 0, &options, 1);
	
    if (no_reiserfs_found (fs))
        die ("could not open filesystem on \"%s\"", file_name);

    /* keep SB to separate memory */
    fs->fs_ondisk_sb = getmem(fs->fs_blocksize);
    memcpy (fs->fs_ondisk_sb, fs->fs_super_bh->b_data, fs->fs_blocksize);

    block_count = get_sb_block_count (fs->fs_ondisk_sb);

    if (options.bm_name) {
    	load_bitmap_from_file (&options);
    } else {
	if (reiserfs_open_ondisk_bitmap (fs))
    	    die ("could not open bitmap\n");
	
	options.bm = reiserfs_create_bitmap (fs->fs_bitmap2->bm_bit_size);
	reiserfs_bitmap_copy (options.bm, fs->fs_bitmap2);
    }

    if (options.bm && block_count > options.bm->bm_bit_size) {
        printf ("fs is larger (%d blocks) then bitmap (%ld blocks), work with %ld blocks only\n",
	block_count, options.bm->bm_bit_size, options.bm->bm_bit_size);
	block_count = options.bm->bm_bit_size;
    }

    warn_what_will_be_done (&options, file_name, block_count);

    for (i = options.offset; i < block_count; i++) {
        int who;
        struct buffer_head * bh;
        /* read every block and corrupt all formatted blocks */
        	if (!reiserfs_bitmap_test_bit (options.bm, i))
	    continue;
        	
	bh = bread (fs->fs_dev, i, fs->fs_blocksize);
	
	who = who_is_this (bh->b_data, bh->b_size);
	
	/*	
	if (block of bitmap || block of journal || leaf || internal)
	take N random bytes and write them into N random place
	within the block		
	*/
	if (who != THE_SUPER && who != THE_LEAF && who != THE_INTERNAL && who != THE_JDESC &&
	    !block_of_bitmap (fs, i) && !block_of_journal (fs, i))
	    continue;
	
	printf ("\ncorrupt %d ", i);		
    }

    freemem(fs->fs_ondisk_sb);

    reiserfs_close (fs);
    printf ("\nDone\n");
    return 0;
}
