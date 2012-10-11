/*
 * Copyright 2002 Hans Reiser
 */

#include "debugreiserfs.h"


extern unsigned long * badblocks;
extern int badblocks_nr;

#if 0
static void look_in_transaction (reiserfs_filsys_t * fs,
				 reiserfs_trans_t * trans)
{
    struct buffer_head * d_bh, * c_bh, * logged, * in_place;
    struct reiserfs_journal_desc * desc;
    struct reiserfs_journal_commit * commit;
    int i, j;
    int who;


    d_bh = bread (fs->fs_dev, trans->desc_blocknr, fs->fs_blocksize);
    if (!d_bh) {
	reiserfs_warning (stdout, "reading descriptor block %lu failed\n",
			  trans->desc_blocknr);
	return;
    }

    c_bh = bread (fs->fs_dev, trans->commit_blocknr, fs->fs_blocksize);
    if (!c_bh) {
	reiserfs_warning (stdout, "reading commit block %lu failed\n",
			  trans->commit_blocknr);
	brelse (d_bh);
	return;
    }

//    desc = (struct reiserfs_journal_desc *)(d_bh->b_data);
//    commit = (struct reiserfs_journal_commit *)(c_bh->b_data);

    reiserfs_warning (stdout, "Transaction %lu\n", trans->trans_id);

    for (i = 0; i < badblocks_nr; i ++) {
	for (j = 0; j < get_jd_len (d_bh); j ++) {
	    if ((j < JOURNAL_TRANS_HALF && badblocks [i] == le32_to_cpu (desc->j2_realblock[j])) ||
		(j >= JOURNAL_TRANS_HALF && badblocks [i] == le32_to_cpu (commit->j3_realblock[j - JOURNAL_TRANS_HALF]))) {
		reiserfs_warning (stdout,
				  "\tblock %lu is logged in it (block of journal area %lu - ",
				  badblocks [i], d_bh->b_blocknr + j + 1);
		
		logged = 0;
		in_place = 0;

		logged = bread (fs->fs_dev, d_bh->b_blocknr + j + 1, fs->fs_blocksize);
		in_place = bread (fs->fs_dev, badblocks [i], fs->fs_blocksize);
		if (!logged || !in_place) {
		    reiserfs_warning (stdout, "Could not read blocks\n");
		    brelse (logged);
		    brelse (in_place);
		    continue;
		}

		who = who_is_this (logged->b_data, logged->b_size);
		if (who != THE_LEAF && who != HAS_IH_ARRAY) {
		    brelse (logged);
		    brelse (in_place);
		    continue;
		}

		reiserfs_warning (stdout, "%s\n", which_block (who));
		if (data (fs)->JJ > 1) {
		    /* overwrite in-place block with a logged version */
		    if (who_is_this (in_place->b_data, in_place->b_size) == THE_LEAF)
			reiserfs_warning (stderr, "Block %d is leaf already, skip it\n",
					  in_place->b_blocknr);
		    else {
			memcpy (in_place->b_data, logged->b_data, fs->fs_blocksize);
			mark_buffer_dirty (in_place);
			bwrite (in_place);
			if (input_bitmap (fs))
			    reiserfs_bitmap_set_bit (input_bitmap (fs), in_place->b_blocknr);
		    }
		}

		brelse (logged);
		brelse (in_place);
	    }
	}
    }

    brelse (d_bh);
    brelse (c_bh);
}
    
/* go through all transactions in the journal to check whether block is there */
static void find_in_journal (reiserfs_filsys_t * fs)
{
    for_each_transaction (fs, look_in_transaction);
}
#endif

#define BLOCK_IN_Q 12762

static void check_block_9555 (reiserfs_filsys_t * fs, reiserfs_trans_t * trans,
			      int index,
			      unsigned long in_journal, unsigned long in_place)
{
    struct buffer_head * bh;
    

    if (in_place == 17) {
	/* read block of journal which contains block 17 abd check whether bit
	   BLOCK_IN_Q is set */
	bh = bread (fs->fs_journal_dev, in_journal, fs->fs_blocksize);
	if (!bh) {
	    reiserfs_warning (stderr, "could not read block %lu (of journal area)\n",
			      in_journal);
	    return;
	}
	
	if (test_bit (BLOCK_IN_Q, bh->b_data))
	    reiserfs_warning (stdout, "trans %lu, pos %d - block 17: block %d is marked used in it\n",
			      trans->trans_id, index, BLOCK_IN_Q);
	else
	    reiserfs_warning (stdout, "trans %lu, pos %d - block 17: block %d is marked free in it\n",
			      trans->trans_id, index, BLOCK_IN_Q);
	brelse (bh);
    }

    if (in_place == BLOCK_IN_Q) {
	reiserfs_warning (stdout, "trans %lu, block %d is logged at pos %d\n",
			  trans->trans_id, BLOCK_IN_Q, index);
    }
}


/* remove when the problem is found */
static void check_transaction (reiserfs_filsys_t * fs, reiserfs_trans_t * trans)
{
    for_each_block (fs, trans, check_block_9555);
}


void scan_journal (reiserfs_filsys_t * fs)
{
    for_each_transaction (fs, check_transaction);
}



#if 0
/* read block numbers from stdin, look for them in the journal, and (if -JJ is
   given) write then "in-place" amd mark overwritten blocks used in the bitmap
   specified with -b */
void scan_journal (reiserfs_filsys_t * fs)
{
    char * str = 0;
    size_t n = 0;


    /* store all blocks which come from stdin */
    while (1) {
	if (getline (&str, &n, stdin) == -1)
	    break;
	if (!strcmp (str, "\n"))
	    break;
	str [strlen (str) - 1] = 0;

	badblocks = realloc (badblocks, (badblocks_nr + 1) * 4);
	if (!badblocks)
	    reiserfs_panic ("realloc failed");
	badblocks [badblocks_nr ++] = atol (str);

	free (str);
	str = 0;
	n = 0;
    }

    if (data (fs)->JJ > 1)
	reiserfs_reopen (fs, O_RDWR);
    reiserfs_warning (stdout, "%d blocks given\n", badblocks_nr); fflush (stdout);
    find_in_journal (fs);

    if (data (fs)->JJ > 1 && input_bitmap (fs)) {
	FILE * fp;

	fp = fopen (input_bitmap_file_name (fs), "w+");
	if (!fp) {
	    reiserfs_warning (stderr, "reiserfs_bitmap_save: could not save bitmap in %s: %m",
			      input_bitmap_file_name (fs));
	    return;
	}
	reiserfs_bitmap_save (fp, input_bitmap (fs));
	fclose (fp);
    }
}
#endif
