/*
 * Copyright 2002-2004 by Hans Reiser, licensing governed by 
 * reiserfsprogs/README
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "reiserfs/libreiserfs.h"
#include "misc/device.h"

#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

/* check whether 'block' can be logged */
static int not_journalable (reiserfs_filsys_t * fs, unsigned long block)
{
    reiserfs_blktype_t type;
    
    type = reiserfs_fs_block(fs, block);
    return (type == BT_INVAL || type == BT_JOURNAL);
}

/* compares description block with commit block. returns 0 if they differ, 1
   if they match */
static int does_desc_match_commit (reiserfs_bh_t *d_bh, 
			    reiserfs_bh_t *c_bh) 
{
    return (reiserfs_jc_get_trans (c_bh) == reiserfs_jd_get_trans (d_bh) &&
	    reiserfs_jc_get_len (c_bh) == reiserfs_jd_get_len (d_bh));
}


/* d_bh is descriptor, return number of block where commit block of this
   transaction is to be */
static unsigned long commit_expected (reiserfs_filsys_t * fs, 
				      reiserfs_bh_t * d_bh)
{
    unsigned long offset;
    reiserfs_journal_param_t * sb_jp;


    sb_jp = reiserfs_sb_jp (fs->fs_ondisk_sb);
    //desc = (struct reiserfs_journal_desc *)d_bh->b_data;
    offset = d_bh->b_blocknr - reiserfs_jp_get_start (sb_jp);
    return reiserfs_jp_get_start (sb_jp) + 
	((offset + reiserfs_jd_get_len (d_bh) + 1) % reiserfs_jp_get_size (sb_jp));
}


/* d_bh contains journal descriptor, returns number of block where descriptor
   block of next transaction should be */
static unsigned long next_desc_expected (reiserfs_filsys_t * fs, 
					 reiserfs_bh_t * d_bh)
{
    unsigned long offset;
    reiserfs_journal_param_t * sb_jp;


    sb_jp = reiserfs_sb_jp (fs->fs_ondisk_sb);
    //desc = (struct reiserfs_journal_desc *)d_bh->b_data;
    offset = d_bh->b_blocknr - reiserfs_jp_get_start (sb_jp);
    return reiserfs_jp_get_start (sb_jp) + 
	((offset + reiserfs_jd_get_len (d_bh) + 2) % reiserfs_jp_get_size (sb_jp));
}

/* common checks for validness of a transaction */
static int transaction_check_content (reiserfs_filsys_t * fs, 
				      reiserfs_trans_t * trans) 
{
    reiserfs_bh_t *d_bh, *c_bh;
    struct reiserfs_journal_desc * desc;
    struct reiserfs_journal_commit * commit;
    unsigned long block;
    unsigned int trans_half, i;

    d_bh = reiserfs_buffer_read (fs->fs_journal_dev, trans->desc_blocknr, fs->fs_blocksize);

    if (!d_bh || reiserfs_node_type (d_bh) != NT_JDESC)
	goto error_desc_brelse;

    /* read expected commit block and compare with descriptor block */
    c_bh = reiserfs_buffer_read (fs->fs_journal_dev, 
		  commit_expected (fs, d_bh), 
		  fs->fs_blocksize);
    
    if (!c_bh)
	goto error_desc_brelse;
 
    if (!does_desc_match_commit (d_bh, c_bh)) 
	goto error_commit_brelse;

    /* Check that all target blocks are journalable */
    desc = (struct reiserfs_journal_desc *)(d_bh->b_data);
    commit = (struct reiserfs_journal_commit *)(c_bh->b_data);

    trans_half = reiserfs_jt_half (d_bh->b_size);
    for (i = 0; i < reiserfs_jd_get_len(d_bh); i++) {
	if (i < trans_half)
	    block = le32_to_cpu (desc->j2_realblock[i]);
	else
	    block = le32_to_cpu (commit->j3_realblock[i - trans_half]);

	if (not_journalable(fs, block)) 
	    goto error_commit_brelse;
    }
    
    reiserfs_buffer_close (d_bh);
    reiserfs_buffer_close (c_bh);
    return 1;
    
error_commit_brelse:
    reiserfs_buffer_close (c_bh);
error_desc_brelse:
    reiserfs_buffer_close(d_bh);
    return 0;
}

/* common checks for validness of a transaction */
static int transaction_check_desc(reiserfs_filsys_t * fs, 
				  reiserfs_bh_t * d_bh) 
{
    reiserfs_bh_t * c_bh;
    int ret = 1;

    if (!d_bh || reiserfs_node_type (d_bh) != NT_JDESC)
	return 0;

    /* read expected commit block and compare with descriptor block */
    c_bh = reiserfs_buffer_read (fs->fs_journal_dev, commit_expected (fs, d_bh), fs->fs_blocksize);
    if (!c_bh)
	return 0;
 
    if (!does_desc_match_commit (d_bh, c_bh)) 
	ret = 0;

    reiserfs_buffer_close (c_bh);
    return ret;
}


/* read the journal and find the oldest and newest transactions, return number
   of transactions found */
int reiserfs_journal_get_transactions (reiserfs_filsys_t * fs,
				       reiserfs_trans_t * oldest,
				       reiserfs_trans_t * newest)
{
    reiserfs_sb_t * sb;
    unsigned long j_cur;
    unsigned long j_start;
    unsigned long j_size;
    reiserfs_bh_t * d_bh;
    __u32 newest_trans_id, oldest_trans_id, trans_id;
    int trans_nr;

    sb = fs->fs_ondisk_sb;
    
    j_start = reiserfs_jp_get_start (reiserfs_sb_jp (sb));
    j_size = reiserfs_jp_get_size (reiserfs_sb_jp (sb));
    
    oldest_trans_id = 0xffffffff;
    newest_trans_id = 0;

    trans_nr = 0;
    for (j_cur = 0; j_cur < j_size; j_cur ++) {
	d_bh = reiserfs_buffer_read (fs->fs_journal_dev, j_start + j_cur, fs->fs_blocksize);
	if (!transaction_check_desc (fs, d_bh)) {
	    reiserfs_buffer_close (d_bh);
	    continue;
	}

	trans_nr ++;

	trans_id = reiserfs_jd_get_trans (d_bh);
	if (trans_id < oldest_trans_id) {
	    oldest_trans_id = trans_id;

	    oldest->mount_id = reiserfs_jd_get_mount (d_bh);
	    oldest->trans_id = reiserfs_jd_get_trans (d_bh);
	    oldest->desc_blocknr = d_bh->b_blocknr;
	    oldest->trans_len = reiserfs_jd_get_len (d_bh);
	    oldest->commit_blocknr = commit_expected (fs, d_bh);
	    oldest->next_trans_offset = next_desc_expected (fs, d_bh) - j_start;
	}

	if (trans_id > newest_trans_id) {
	    newest_trans_id = trans_id;

	    newest->mount_id = reiserfs_jd_get_mount (d_bh);
	    newest->trans_id = reiserfs_jd_get_trans (d_bh);
	    newest->desc_blocknr = d_bh->b_blocknr;
	    newest->trans_len = reiserfs_jd_get_len (d_bh);
	    newest->commit_blocknr = commit_expected (fs, d_bh);
	    newest->next_trans_offset = next_desc_expected (fs, d_bh) - j_start;
	}

	j_cur += reiserfs_jd_get_len (d_bh) + 1;
	reiserfs_buffer_close (d_bh);
    }

    return trans_nr;
}

#define TRANS_FOUND     1
#define TRANS_NOT_FOUND 0

/* trans is a valid transaction. Look for valid transaction with smallest
   trans id which is greater than the id of the current one */
static int next_transaction (reiserfs_filsys_t * fs, 
			     reiserfs_trans_t * trans, 
			     reiserfs_trans_t break_trans)
{
    reiserfs_bh_t * d_bh, * next_d_bh;
    int found;
    unsigned long j_start;
    unsigned long j_offset;
    unsigned long block;


    j_start = reiserfs_jp_get_start (reiserfs_sb_jp (fs->fs_ondisk_sb));

    found = TRANS_NOT_FOUND;

    if (trans->trans_id == break_trans.trans_id)
	return found;
	
    /* make sure that 'trans' is a valid transaction */
    d_bh = reiserfs_buffer_read (fs->fs_journal_dev, trans->desc_blocknr, fs->fs_blocksize);
    if (!transaction_check_desc (fs, d_bh))
	misc_die ("next_transaction: valid transaction is expected");

    block = next_desc_expected (fs, d_bh);
    j_offset = block - j_start;

    while (1) {
	next_d_bh = reiserfs_buffer_read (fs->fs_journal_dev, block, fs->fs_blocksize);
	if (transaction_check_desc (fs, next_d_bh))
	    break;

	reiserfs_buffer_close (next_d_bh);
	j_offset ++;
	block = j_start + 
		(j_offset % reiserfs_jp_get_size (reiserfs_sb_jp (fs->fs_ondisk_sb)));
    }

    //next_desc = (struct reiserfs_journal_desc *)next_d_bh->b_data;
    
    if (break_trans.trans_id >= reiserfs_jd_get_trans (next_d_bh)) {
	/* found transaction is newer */
	trans->mount_id = reiserfs_jd_get_mount (next_d_bh);
	trans->trans_id = reiserfs_jd_get_trans (next_d_bh);
	trans->desc_blocknr = next_d_bh->b_blocknr;
	trans->trans_len = reiserfs_jd_get_len (next_d_bh);
	trans->commit_blocknr = commit_expected (fs, next_d_bh);
	trans->next_trans_offset = next_desc_expected (fs, next_d_bh) - j_start;
	found = TRANS_FOUND;
    }

    reiserfs_buffer_close (d_bh);
    reiserfs_buffer_close (next_d_bh);
    return found;
}

static void read_journal_write_in_place (reiserfs_filsys_t * fs, 
					 reiserfs_trans_t * trans, 
					 unsigned int index, 
                                         unsigned long in_journal, 
					 unsigned long in_place)
{
    reiserfs_bh_t * j_bh, * bh;

    j_bh = reiserfs_buffer_read (fs->fs_journal_dev, in_journal, fs->fs_blocksize);
    if (!j_bh) {
	fprintf (stderr, "replay_one_transaction: transaction %lu: reading "
		 "%lu block failed\n", trans->trans_id, in_journal);
	return;
    }
    if (not_journalable (fs, in_place)) {
	fprintf (stderr, "replay_one_transaction: transaction %lu: block "
		 "%ld should not be journalled (%lu)\n",
		 trans->trans_id, in_journal, in_place);
	reiserfs_buffer_close (j_bh);
	return;
    }

    bh = reiserfs_buffer_open (fs->fs_dev, in_place, fs->fs_blocksize);
    
    memcpy (bh->b_data, j_bh->b_data, bh->b_size);
    reiserfs_buffer_mkdirty (bh);
    reiserfs_buffer_mkuptodate (bh, 1);
    reiserfs_buffer_write (bh);
    reiserfs_buffer_close (bh);
    reiserfs_buffer_close (j_bh);
    
}


/* go through all blocks of transaction and call 'action' each of them */
void reiserfs_journal_foreach (reiserfs_filsys_t * fs, 
			       reiserfs_trans_t * trans,
			       action_on_block_t action)
{
    reiserfs_bh_t * d_bh, * c_bh;
    struct reiserfs_journal_desc * desc;
    struct reiserfs_journal_commit * commit;
    unsigned long j_start, j_offset, j_size;
    unsigned int i, trans_half;
    unsigned long block;
 
    d_bh = reiserfs_buffer_read (fs->fs_journal_dev, trans->desc_blocknr, fs->fs_blocksize);
    if (!d_bh) {
	reiserfs_warning (stdout, "reading descriptor block %lu failed\n", 
			  trans->desc_blocknr);
	return;
    }

    c_bh = reiserfs_buffer_read (fs->fs_journal_dev, trans->commit_blocknr, fs->fs_blocksize);
    if (!c_bh) {
	reiserfs_warning (stdout, "reading commit block %lu failed\n", 
			  trans->commit_blocknr);
	reiserfs_buffer_close (d_bh);
	return;
    }

    desc = (struct reiserfs_journal_desc *)(d_bh->b_data);
    commit = (struct reiserfs_journal_commit *)(c_bh->b_data);

    /* first block of journal and size of journal */
    j_start = reiserfs_jp_get_start (reiserfs_sb_jp (fs->fs_ondisk_sb));
    j_size = reiserfs_jp_get_size (reiserfs_sb_jp (fs->fs_ondisk_sb));

    /* offset in the journal where the transaction starts */
    j_offset = trans->desc_blocknr - j_start + 1;

    trans_half = reiserfs_jt_half (d_bh->b_size);
    for (i = 0; i < trans->trans_len; i ++, j_offset ++) {
	if (i < trans_half)
	    block = le32_to_cpu (desc->j2_realblock[i]);
	else
	    block = le32_to_cpu (commit->j3_realblock[i - trans_half]);
	action (fs, trans, i, j_start + (j_offset % j_size), block);
    }

    reiserfs_buffer_close (d_bh);
    reiserfs_buffer_close (c_bh);
}


/* transaction is supposed to be valid */
static int replay_one_transaction (reiserfs_filsys_t * fs,
				   reiserfs_trans_t * trans)
{
    reiserfs_journal_foreach (fs, trans, read_journal_write_in_place);
    fsync(fs->fs_dev);
    return 0;
}


void reiserfs_journal_by_transaction (reiserfs_filsys_t * fs, 
				      action_on_trans_t action)
{
    reiserfs_trans_t oldest, newest;
    int ret = 0;

    if (!reiserfs_journal_get_transactions (fs, &oldest, &newest))
	return;

    while (1) {
	action (fs, &oldest);	
	if ((ret = next_transaction (fs, &oldest, newest)) == TRANS_NOT_FOUND)
	    break;
    }
}

/* Get the size of the journal or reserved area. */
unsigned long reiserfs_journal_hostsize(reiserfs_sb_t * sb)
{
	if (reiserfs_super_jr_magic (sb))
		return reiserfs_sb_get_reserved (sb);

	/* with standard journal */
	return reiserfs_jp_get_size (reiserfs_sb_jp (sb)) + 1;
}


__u32 reiserfs_journal_tlen (__u32 desired, __u32 journal_size, 
			     int blocksize, int verbose)
{
    __u32 saved;
    __u32 ratio = 1;

    if (blocksize < 4096)
	ratio = 4096/blocksize;
	
    saved = desired;
    if (!desired)
		desired = JOURNAL_TRANS_MAX/ratio;
    
    if (journal_size / desired < JOURNAL_MIN_RATIO)
		desired = journal_size / JOURNAL_MIN_RATIO;
    
    if (desired > JOURNAL_TRANS_MAX/ratio)
		desired = JOURNAL_TRANS_MAX/ratio;
    
    if (desired < JOURNAL_TRANS_MIN/ratio)
		desired = JOURNAL_TRANS_MIN/ratio;

    if (verbose) {
	if (saved && saved != desired)
		reiserfs_warning (stderr,
		    "WARNING: wrong transaction max size (%u). Changed to %u\n", 
		    saved, desired);
    }

    return desired;
}
#if 0
    __u32 ret_val;
    ret_val = 0;
    if (!desired)                   ret_val = JOURNAL_TRANS_MAX;
    if (desired<journal_size/8)     ret_val = journal_size/8;
    if (desired>journal_size/2)     ret_val = journal_size/2;
    if (desired>JOURNAL_TRANS_MAX)  ret_val = JOURNAL_TRANS_MAX;
    if (ret_val) {
        reiserfs_warning (stderr, "WARNING: Journal max trans length "
			  "is wrong seting: %u, resetting to available "
			  "possible %u\n", desired, ret_val);
    } else {
        ret_val = desired;
    }
    return ret_val;
}
#endif

__u32 reiserfs_journal_batch (unsigned long journal_trans_max) {
    return journal_trans_max*JOURNAL_MAX_BATCH/JOURNAL_TRANS_MAX;
}

__u32 reiserfs_journal_commit_age (void) {
    return JOURNAL_MAX_COMMIT_AGE;
}


__u32 reiserfs_journal_trans_age (void) {
    return JOURNAL_MAX_TRANS_AGE;
}

int reiserfs_journal_params_check (reiserfs_filsys_t * fs) {
    struct reiserfs_journal_header * j_head;
    reiserfs_sb_t * sb = fs->fs_ondisk_sb;
    
    j_head = (struct reiserfs_journal_header *)(fs->fs_jh_bh->b_data);
	
    /* Check the superblock's journal parameters. */
    if (!reiserfs_super_jr_magic (sb)) {    
	if ((reiserfs_jp_get_dev (reiserfs_sb_jp(sb)) != 0) || 
	    (reiserfs_jp_get_start (reiserfs_sb_jp(sb)) != 
	     reiserfs_journal_start_must (fs)) || 
	    (reiserfs_jp_get_size (reiserfs_sb_jp(sb)) != 
	     reiserfs_journal_default(fs->fs_super_bh->b_blocknr, fs->fs_blocksize)))
	{
	    reiserfs_warning (stderr, 
		"\nreiserfs_journal_open: wrong journal parameters found in the "
		"super block. \nYou should run reiserfsck with --rebuild-sb to "
		"check your superblock consistency.\n\n");
		
	    return 1;
	}
    }
	
    if (memcmp(&j_head->jh_journal, reiserfs_sb_jp (sb), sizeof(reiserfs_journal_param_t))) {
	if (!reiserfs_super_jr_magic (sb)) {
	    reiserfs_warning (stderr, "\nreiserfs_journal_open: journal "
			      "parameters from the superblock does not "
			      "match \nto the journal headers ones. It "
			      "looks like that you created your fs with "
			      "old\nreiserfsprogs. Journal header is "
			      "fixed.\n\n", fs->fs_j_file_name);
		
	    memcpy(&j_head->jh_journal, reiserfs_sb_jp(sb), 
		   sizeof(reiserfs_journal_param_t));
	    
	    reiserfs_buffer_mkdirty(fs->fs_jh_bh);
	    reiserfs_buffer_write(fs->fs_jh_bh);
	} else {
	    reiserfs_warning (stderr, "\nreiserfs_journal_open: journal "
			      "parameters from the super block does not "
			      "match \nto journal parameters from the "
			      "journal. You should run  reiserfsck with "
			      "--rebuild-sb to check your superblock "
			      "consistency.\n\n");
	    return 1;	
	}
    }
    
    return 0;
}

/* read journal header and make sure that it matches with the filesystem
   opened */
int reiserfs_journal_open (reiserfs_filsys_t * fs, char * j_filename, int flags) {
    reiserfs_sb_t * sb;
    char buf[4096];
    __u64 count;
    dev_t rdev;
    
    sb = fs->fs_ondisk_sb;

    if (j_filename && j_filename[0] != '\0') {
	if (!reiserfs_super_jr_magic (sb) && 
	    strcmp (j_filename, fs->fs_file_name)) 
	{
	    reiserfs_warning (stderr, "Filesystem with standard journal found, "
			      "wrong name of specified journal device %s \n", 
			      j_filename);
	    return 2;
	}
    } else {
	if (!reiserfs_super_jr_magic (sb)) {
	    j_filename = fs->fs_file_name;
	} else {
	    /* Not standard journal and no device is specified.
	       Find the device by rdev. */
	    strcpy(buf, "/dev");

	    rdev = reiserfs_sb_jp(sb)->jp_dev;
	    if ((misc_dir_walk(buf, misc_device_rdev_match, &rdev)) <= 0) {
		reiserfs_warning (stderr, "Filesystem with non-standard "
				  "journal found, failed to find a block "
				  "device (%u:%u) in /dev.\nCreate such "
				  "a device in /dev or specify the journal "
				  "device with -j option.\n",
				  major(rdev), minor(rdev));

		return 2;
	    }

	    j_filename = buf;
	}
    }
    
    fs->fs_journal_dev = open (j_filename, flags 
#if defined(O_LARGEFILE)
			       | O_LARGEFILE
#endif
			       );
    if (fs->fs_journal_dev == -1) 
        return -1;
    
    strncpy(fs->fs_j_file_name, j_filename, sizeof(fs->fs_j_file_name));
    
    if (reiserfs_jp_get_size(reiserfs_sb_jp(sb)) < JOURNAL_MIN_SIZE) {
	reiserfs_warning (stderr, "Journal of (%lu) block size found on "
	    "specified journal device %s.\nMust be not less than (%lu).\n",
	    reiserfs_jp_get_size (reiserfs_sb_jp (sb)) + 1, j_filename, 
	    JOURNAL_MIN_SIZE + 1);
	close(fs->fs_journal_dev);
	return 1;
    }
    
    if (!(count = misc_device_count_blocks (j_filename, fs->fs_blocksize))) {
	close(fs->fs_journal_dev);
	return -1;
    }

    if (reiserfs_jp_get_start (reiserfs_sb_jp (sb)) + 
	reiserfs_jp_get_size (reiserfs_sb_jp (sb)) + 1 > count) 
    {
	reiserfs_warning (stderr, "Detected journal on specified device %s "
			  "does not fit to the device.\nStart block (%lu) + "
			  "size (%lu) less than device size (%lu).\n", 
			  j_filename, reiserfs_jp_get_start(reiserfs_sb_jp (sb)), 
			  reiserfs_jp_get_size(reiserfs_sb_jp (sb)) + 1, count);
	close(fs->fs_journal_dev);
	return 1;
    }
    
    /* read journal header */
    fs->fs_jh_bh = reiserfs_buffer_read (fs->fs_journal_dev, 
			  reiserfs_jp_get_start (reiserfs_sb_jp (sb)) + 
			  reiserfs_jp_get_size (reiserfs_sb_jp (sb)), 
			  fs->fs_blocksize);

    if (!fs->fs_jh_bh) {
	reiserfs_warning (stderr, "reiserfs_journal_open: reiserfs_buffer_read failed "
			  "reading journal  header.\n");
	close(fs->fs_journal_dev);
	return -1;
    }

    return 0;
}

/* initialize super block's journal related fields and journal header fields. 
 * If len is 0 - make journal of default size */
int reiserfs_journal_create(
    reiserfs_filsys_t * fs, 
    char * j_device,		/* journal device name */
    unsigned long offset,	/* journal offset on the j_device */
    unsigned long len,		/* including journal header */
    int transaction_max_size)
{
    struct stat st;
    reiserfs_bh_t * bh;
    struct reiserfs_journal_header * jh;
    reiserfs_sb_t * sb;
    unsigned long blocks;

    sb = fs->fs_ondisk_sb;
    
    if (!j_device || !strcmp (j_device, fs->fs_file_name)) {
	/* Journal is to be on the host device, check the amount space for the 
	 * journal on it. */
	len = len ? len : reiserfs_journal_default(fs->fs_super_bh->b_blocknr, 
	    fs->fs_blocksize) + 1;
		
	offset = offset ? offset : reiserfs_journal_start_must(fs);
	
	if (offset < reiserfs_journal_start_must(fs)) {
	    reiserfs_warning (stderr, "reiserfs_journal_create: offset is "
		"%lu, but it cannot be less then %llu on the device %s\n", 
		offset, reiserfs_journal_start_must(fs), j_device);
	    return 0;
	}
	
	if (!reiserfs_journal_fits(offset, fs->fs_blocksize, 
				   reiserfs_sb_get_blocks(sb), len))
	{
	    /* host device does not contain enough blocks */
	    reiserfs_warning (stderr, "reiserfs_journal_create: cannot create "
		"a journal of %lu blocks with %lu offset on %d blocks\n", 
		len, offset, reiserfs_sb_get_blocks(sb));
		return 0;
	}
	
	j_device = fs->fs_file_name;
	
	
	st.st_rdev = 0;
    } else {
	/* journal is to be on separate device */
	if (!(blocks = misc_device_count_blocks (j_device, fs->fs_blocksize)))
		return 0;

	if (!len) {
	    /* default size of a journal on a separate device is whole device */
	    if (blocks < offset) {
		reiserfs_warning (stderr, "reiserfs_journal_create: offset is "
		    "%lu, blocks on device %lu\n", offset, blocks);
		return 0;
	    }
	    len = blocks - offset;
	}

	if (len > reiserfs_journal_default (fs->fs_super_bh->b_blocknr, 
	    fs->fs_blocksize) + 1) 
	{
	    fflush(stderr);
	    
	    reiserfs_warning (stdout, "NOTE: journal new size %lu is greater "
		"than default size %lu:\nthis may slow down initializing and "
		"mounting of the journal. Hope it is ok.\n\n", len, 
		reiserfs_journal_default(fs->fs_super_bh->b_blocknr, 
		fs->fs_blocksize) + 1);
	}

	if (blocks < offset + len) {
	    reiserfs_warning (stderr, "reiserfs_journal_create: no enough "
		"blocks on device %lu, needed %lu\n", blocks, offset + len);
	    return 0;
	}
	
	if (stat (j_device, &st) == -1) {
	    reiserfs_warning (stderr, "reiserfs_journal_create: stat %s failed"
		": %s\n", j_device, strerror(errno));
	    return 0;
	}
/*
	if (!S_ISBLK (st.st_mode)) {
		reiserfs_warning (stderr, "reiserfs_journal_create: "
		"%s is not a block device (%x)\n", j_device, st.st_rdev);
		return 0;
	}
*/
    }

    fs->fs_journal_dev = open (j_device, O_RDWR 
#if defined(O_LARGEFILE)
			       | O_LARGEFILE
#endif
			       );
    if (fs->fs_journal_dev == -1) {
	reiserfs_warning (stderr, "reiserfs_journal_create: could not open "
	    "%s: %s\n", j_device, strerror(errno));
	return 0;
    }

    strncpy(fs->fs_j_file_name, j_device, sizeof(fs->fs_j_file_name));

    if (len < JOURNAL_MIN_SIZE + 1) {
	reiserfs_warning (stderr, "WARNING: Journal size (%u) is less, than "
	    "minimal supported journal size (%u).\n", len, JOURNAL_MIN_SIZE + 1);
        return 0;
    }
    /* get journal header */
    bh = reiserfs_buffer_open (fs->fs_journal_dev, offset + len - 1, fs->fs_blocksize);
    if (!bh) {
	reiserfs_warning (stderr, "reiserfs_journal_create: reiserfs_buffer_open failed\n");
	return 0;
    }

    /* fill journal header */
    jh = (struct reiserfs_journal_header *)bh->b_data;
    reiserfs_jp_set_start(&jh->jh_journal, offset);
    reiserfs_jp_set_dev(&jh->jh_journal, st.st_rdev);
    reiserfs_jp_set_magic(&jh->jh_journal, misc_random());

    reiserfs_jp_set_size(&jh->jh_journal, len - 1);
    reiserfs_jp_set_tlen(&jh->jh_journal, reiserfs_journal_tlen(
	transaction_max_size, len - 1, fs->fs_blocksize, 1));
    reiserfs_jp_set_max_batch(&jh->jh_journal, reiserfs_journal_batch(
	reiserfs_jp_get_tlen(&jh->jh_journal)));
    reiserfs_jp_set_commit_age(&jh->jh_journal, 
	reiserfs_journal_commit_age());
    reiserfs_jp_set_trans_age(&jh->jh_journal, 
	reiserfs_journal_trans_age ());

    reiserfs_buffer_mkuptodate (bh, 1);
    reiserfs_buffer_mkdirty (bh);
    
    fs->fs_jh_bh = bh;
    
    /* make a copy of journal header in the super block */
    memcpy (reiserfs_sb_jp (sb), &jh->jh_journal, sizeof (reiserfs_journal_param_t));
    reiserfs_buffer_mkdirty (fs->fs_super_bh);

    return 1;
}

/* reiserfs_buffer_close journal header, flush all dirty buffers, close device, open, read
   journal header */
void reiserfs_journal_reopen (reiserfs_filsys_t * fs, int flag)
{
    unsigned long jh_block;


    if (!reiserfs_journal_opened (fs))
	return;

    jh_block = fs->fs_jh_bh->b_blocknr;
    reiserfs_buffer_close (fs->fs_jh_bh);
    reiserfs_buffer_flush_all (fs->fs_journal_dev);
    reiserfs_buffer_invalidate_all(fs->fs_journal_dev);
    if (close (fs->fs_journal_dev))
	misc_die ("reiserfs_journal_reopen: closed failed: %s", strerror(errno));

    fs->fs_journal_dev = open (fs->fs_j_file_name, flag 
#if defined(O_LARGEFILE)
			       | O_LARGEFILE
#endif
			       );
    if (fs->fs_journal_dev == -1)
	misc_die ("reiserfs_journal_reopen: could not reopen journal device");

    fs->fs_jh_bh = reiserfs_buffer_read (fs->fs_journal_dev, jh_block, fs->fs_blocksize);
    if (!fs->fs_jh_bh)
	misc_die ("reiserfs_journal_reopen: reading journal header failed");
}


int reiserfs_journal_opened (reiserfs_filsys_t * fs)
{
    return fs->fs_jh_bh ? 1 : 0;
}


void reiserfs_journal_flush (reiserfs_filsys_t * fs)
{
    if (!reiserfs_journal_opened (fs))
		return;
    reiserfs_buffer_flush_all(fs->fs_journal_dev);
}


void reiserfs_journal_free (reiserfs_filsys_t * fs)
{
    if (!reiserfs_journal_opened (fs))
		return;
    reiserfs_buffer_close (fs->fs_jh_bh);
    fs->fs_jh_bh = 0;
    fs->fs_j_file_name[0] = '\0';
}


void reiserfs_journal_close (reiserfs_filsys_t * fs)
{
    reiserfs_journal_flush (fs);
    reiserfs_journal_free (fs);

}

/* update journal header */
static void update_journal_header (reiserfs_filsys_t * fs, 
				   reiserfs_bh_t * bh_jh, 
				   reiserfs_trans_t *trans) 
{
    struct reiserfs_journal_header * j_head;
	
    j_head = (struct reiserfs_journal_header *)(bh_jh->b_data);

    /* update journal header */
    reiserfs_jh_set_flushed (j_head, trans->trans_id);
    reiserfs_jh_set_mount (j_head, trans->mount_id);
    reiserfs_jh_set_start (j_head, trans->next_trans_offset);
    reiserfs_buffer_mkdirty (bh_jh);
    reiserfs_buffer_write (bh_jh);
    fsync(fs->fs_journal_dev);
}

/* FIXME: what should be done when not all transactions can be replayed in 
   proper order? */
int reiserfs_journal_replay (reiserfs_filsys_t * fs)
{
    reiserfs_bh_t * bh;
    struct reiserfs_journal_header * j_head;
    reiserfs_trans_t cur, newest, control;
    int replayed, ret;

    if (!reiserfs_journal_opened (fs))
        reiserfs_panic ("reiserfs_journal_replay: journal is not opened");
    
    if (!reiserfs_fs_rw (fs)) {
        reiserfs_panic ("reiserfs_journal_replay: fs is not opened with "
			"write perms");
    }

    reiserfs_warning (stderr, "Replaying journal..\n");
    bh = fs->fs_jh_bh;
	
    j_head = (struct reiserfs_journal_header *)(bh->b_data);
    control.mount_id = reiserfs_jh_get_mount (j_head);
    control.trans_id = reiserfs_jh_get_flushed (j_head);
    control.desc_blocknr = reiserfs_jh_get_start (j_head);

    if (!reiserfs_journal_get_transactions (fs, &cur, &newest)) {
	reiserfs_warning (stderr, "No transactions found\n");
	return 0;
    }

    /*  Smth strange with journal header or journal. We cannot say for sure 
	what was the last replaied transaction, but relying on JH data is 
	preferable. */

    replayed = 0;
    ret = TRANS_FOUND;
    
    /* Looking to the first valid not replayed transaction. */
    while (1) {
	if (cur.mount_id == control.mount_id && 
	    cur.trans_id > control.trans_id)
	    break;

	if ((ret = next_transaction (fs, &cur, newest)) != TRANS_FOUND)
	    break;
    }
    
    while (ret == TRANS_FOUND) {
	/* If not the next transaction to be replayed, break out here. */
	if ((cur.mount_id != control.mount_id) || 
	    (cur.trans_id != control.trans_id + 1 && control.trans_id))
	    break;
	
	if (!transaction_check_content(fs, &cur)) {
	    reiserfs_warning (stderr, "Trans broken: mountid %lu, transid %lu, "
			      "desc %lu, len %lu, commit %lu, next trans "
			      "offset %lu\n", cur.mount_id, cur.trans_id, 
			      cur.desc_blocknr, cur.trans_len, 
			      cur.commit_blocknr, cur.next_trans_offset);
	    break;
	}

        reiserfs_warning (stderr, "Trans replayed: mountid %lu, transid %lu, "
			  "desc %lu, len %lu, commit %lu, next trans offset "
			  "%lu\n", cur.mount_id, cur.trans_id, 
			  cur.desc_blocknr, cur.trans_len, 
			  cur.commit_blocknr, cur.next_trans_offset);
	replay_one_transaction (fs, &cur);
	update_journal_header (fs, bh, &cur);
	control = cur;
        replayed ++;

	ret = next_transaction (fs, &cur, newest);
    }

    reiserfs_warning (stderr, "Reiserfs journal '%s' in blocks [%u..%u]: %d "
		      "transactions replayed\n", fs->fs_j_file_name, 
		      reiserfs_jp_get_start(reiserfs_sb_jp(fs->fs_ondisk_sb)),
		      reiserfs_jp_get_start(reiserfs_sb_jp(fs->fs_ondisk_sb)) + 
		      reiserfs_jp_get_size(reiserfs_sb_jp(fs->fs_ondisk_sb)) + 1,
		      replayed);
	
    reiserfs_buffer_mkdirty (fs->fs_super_bh);
    reiserfs_buffer_write (fs->fs_super_bh);
	
    update_journal_header (fs, bh, &newest);

    return 0;
}

int reiserfs_journal_desc_valid (reiserfs_bh_t *bh) {
    struct reiserfs_journal_desc *desc;
    
    desc = (struct reiserfs_journal_desc *)bh->b_data;
    
    if (!memcmp(bh->b_data + bh->b_size - 12, JOURNAL_DESC_MAGIC, 8) &&
	le32_to_cpu (desc->j2_len) > 0)
    {
	return 1;
    }

    return 0;
}

int reiserfs_journal_block (reiserfs_filsys_t * fs, unsigned long block) {
    unsigned long start;

    start = reiserfs_journal_start_must(fs);
    
    if (!reiserfs_super_jr_magic (fs->fs_ondisk_sb)) {
	/* standard journal */
	if (block >= start && 
	    block <= start + reiserfs_jp_get_size (reiserfs_sb_jp (fs->fs_ondisk_sb)))
	{
	    return 1;
	}
	
	return 0;
    }
    
    if (reiserfs_sb_get_reserved (fs->fs_ondisk_sb)) {
	/* there is space reserved for the journal on the host device */
	if (block >= start && 
	    block < start + reiserfs_sb_get_reserved (fs->fs_ondisk_sb))
	{
	    return 1;
	}
    }

    return 0;
}

// in reiserfs version 0 (undistributed bitmap)
// FIXME: what if number of bitmaps is 15?
static unsigned int reiserfs_journal_oldstart_must (reiserfs_filsys_t * fs) {
    unsigned int bmap_nr;
    
    bmap_nr = reiserfs_bmap_nr(reiserfs_sb_get_blocks(fs->fs_ondisk_sb),
			       fs->fs_blocksize);
    
    return (REISERFS_OLD_DISK_OFFSET_IN_BYTES / fs->fs_blocksize) + 1 + bmap_nr;
}

static unsigned int reiserfs_journal_newstart_must (reiserfs_filsys_t * fs) {
    return (REISERFS_DISK_OFFSET_IN_BYTES / fs->fs_blocksize) + 2;
}

unsigned int reiserfs_journal_start_must (reiserfs_filsys_t * fs) {
    if (reiserfs_old_location(fs->fs_super_bh->b_blocknr, fs->fs_blocksize))
    	return reiserfs_journal_oldstart_must (fs);

    return reiserfs_journal_newstart_must(fs);
}



void reiserfs_journal_print_params (FILE * fp, reiserfs_journal_param_t * jp)
{
    reiserfs_warning (fp, "\tDevice [0x%x]\n", reiserfs_jp_get_dev (jp));
    reiserfs_warning (fp, "\tMagic [0x%x]\n", reiserfs_jp_get_magic (jp));

    reiserfs_warning (fp, "\tSize %u blocks (including 1 for journal header) (first block %u)\n",
		      reiserfs_jp_get_size (jp) + 1,
		      reiserfs_jp_get_start (jp));
    reiserfs_warning (fp, "\tMax transaction length %u blocks\n", reiserfs_jp_get_tlen (jp));
    reiserfs_warning (fp, "\tMax batch size %u blocks\n", reiserfs_jp_get_max_batch (jp));
    reiserfs_warning (fp, "\tMax commit age %u\n", reiserfs_jp_get_commit_age (jp));
    reiserfs_warning (fp, "\tMax transaction age %u\n", reiserfs_jp_get_trans_age (jp));
}



void reiserfs_journal_print_header (reiserfs_filsys_t * fs) {
    struct reiserfs_journal_header * j_head;


    j_head = (struct reiserfs_journal_header *)(fs->fs_jh_bh->b_data);
    reiserfs_warning (stdout, "Journal header (block #%lu of %s):\n"
		      "\tj_last_flush_trans_id %ld\n"
		      "\tj_first_unflushed_offset %ld\n"
		      "\tj_mount_id %ld\n", 
		      fs->fs_jh_bh->b_blocknr, fs->fs_j_file_name,
		      reiserfs_jh_get_flushed (j_head),
		      reiserfs_jh_get_start (j_head),
		      reiserfs_jh_get_mount (j_head));
    reiserfs_journal_print_params (stdout, &j_head->jh_journal);
}

static void print_trans_element (reiserfs_filsys_t * fs, 
				 reiserfs_trans_t * trans,
				 unsigned int index, 
				 unsigned long in_journal, 
				 unsigned long in_place)
{
    if (index % 8 == 0)
	reiserfs_warning (stdout, "#%d\t", index);

    reiserfs_warning (stdout, "%lu->%lu%s ",  in_journal, in_place,
		      reiserfs_bitmap_block (fs, in_place) ? "B" : "");
    if ((index + 1) % 8 == 0 || index == trans->trans_len - 1)
	reiserfs_warning (stdout, "\n");
}


static void print_one_transaction (reiserfs_filsys_t * fs, 
				   reiserfs_trans_t * trans)
{
    reiserfs_warning (stdout, "Mountid %u, transid %u, desc %lu, length %u, "
		      "commit %lu\n", trans->mount_id, trans->trans_id,
		      trans->desc_blocknr, trans->trans_len, 
		      trans->commit_blocknr);
    
    reiserfs_journal_foreach (fs, trans, print_trans_element);
}


/* print all valid transactions and found dec blocks */
void reiserfs_journal_print (reiserfs_filsys_t * fs)
{
    if (!reiserfs_journal_opened (fs)) {
	reiserfs_warning (stderr, "reiserfs_journal_print: journal is not opened\n");
	return;
    }
    reiserfs_journal_print_header (fs);

    reiserfs_journal_by_transaction (fs, print_one_transaction);
}

int reiserfs_print_jdesc (FILE * fp, reiserfs_bh_t * bh) {
    if (memcmp(reiserfs_jd_magic (bh), JOURNAL_DESC_MAGIC, 8))
	return 1;

    reiserfs_warning (fp, "Desc block %lu (j_trans_id %ld, j_mount_id %ld, j_len %ld)\n",
		      bh->b_blocknr, reiserfs_jd_get_trans (bh),
		      reiserfs_jd_get_mount (bh), reiserfs_jd_get_len (bh));

    return 0;
}

/* reiserfs needs at least: enough blocks for journal, 64 k at the beginning,
   one block for super block, bitmap block and root block. Note that first
   bitmap block must point to all of them */
int reiserfs_journal_fits(unsigned long journal_offset, 
			  unsigned int block_size,
			  unsigned long block_count, 
			  unsigned long journal_size) 
{
	unsigned long blocks;

    /* RESERVED, MD RAID SBs, super block, bitmap, root, journal size with journal header */
    blocks = journal_offset + journal_size;

    /* we have a limit: skipped area, super block, journal and root block
    all have to be addressed by one first bitmap */
    if (blocks > block_size * 8)
    	return 0;
    	
    if (blocks > block_count)
    	return 0;
    	
    return 1;
}
