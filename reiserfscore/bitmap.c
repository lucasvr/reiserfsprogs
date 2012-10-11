/* 
 * Copyright 2000-2002 by Hans Reiser, licensing governed by reiserfs/README
 */
  
/*
 * 2000/10/26 - Initial version.
 */

#include "includes.h"
#include <assert.h>


/* create clean bitmap */
reiserfs_bitmap_t * reiserfs_create_bitmap (unsigned int bit_count)
{
    reiserfs_bitmap_t * bm;

    bm = getmem (sizeof (*bm));
    if (!bm)
	return 0;
    bm->bm_bit_size = bit_count;
    bm->bm_byte_size = ((unsigned long long)bit_count + 7) / 8;
    bm->bm_set_bits = 0;
    bm->bm_map = getmem (bm->bm_byte_size);
    if (!bm->bm_map) {
	freemem (bm);
	return 0;
    }

    return bm;
}

/* Expand existing bitmap.  Return non-zero if can't. FIXME: it is
   assumed that bit_count is new number of blocks to be addressed */
int reiserfs_expand_bitmap (reiserfs_bitmap_t * bm, unsigned int bit_count)
{
    unsigned int byte_count = ((bit_count + 7) / 8);
    char * new_map;

    new_map = expandmem (bm->bm_map, bm->bm_byte_size,
			 byte_count - bm->bm_byte_size);

    if (!new_map) {
	return 1;
    }
    
    bm->bm_map = new_map;
    bm->bm_byte_size = byte_count;
    bm->bm_bit_size = bit_count;
    return 0;
}

void reiserfs_shrink_bitmap (reiserfs_bitmap_t * bm, unsigned int bit_count)
{
    assert (bm->bm_bit_size >= bit_count);

    bm->bm_byte_size = (bit_count + 7) / 8;
    bm->bm_bit_size = bit_count;
}

/* bitmap destructor */
void reiserfs_delete_bitmap (reiserfs_bitmap_t * bm)
{
    freemem(bm->bm_map);
    bm->bm_map = NULL;		/* to not reuse bitmap handle */
    bm->bm_bit_size = 0;
    bm->bm_byte_size = 0;
    freemem(bm);
}


void reiserfs_bitmap_copy (reiserfs_bitmap_t * to, reiserfs_bitmap_t * from)
{
    assert (to->bm_byte_size == from->bm_byte_size);
    memcpy (to->bm_map, from->bm_map, from->bm_byte_size);
    to->bm_bit_size = from->bm_bit_size;
    to->bm_set_bits = from->bm_set_bits;
    to->bm_dirty = 1;
}

int reiserfs_bitmap_compare (reiserfs_bitmap_t * bm1, reiserfs_bitmap_t * bm2)
{
    int bytes, bits;
    long unsigned i, diff;

    assert (bm1->bm_byte_size == bm2->bm_byte_size &&
	    bm1->bm_bit_size == bm2->bm_bit_size);

    diff = 0;

    /* compare full bytes */
    bytes = bm1->bm_bit_size / 8;
    bits = bytes * 8;
    if (memcmp (bm1->bm_map, bm2->bm_map, bytes)) {
	for (i = 0; i < bits; i ++)
	    if (reiserfs_bitmap_test_bit(bm1, i) != reiserfs_bitmap_test_bit(bm2, i))
		diff ++;
    }
    
    /* compare last byte of bitmap which can be used partially */
    bits = bm1->bm_bit_size % 8;
    for (i = bm1->bm_bit_size / 8 * 8; i < bm1->bm_bit_size / 8 * 8 + bits; i ++)
	if (reiserfs_bitmap_test_bit(bm1, i) != reiserfs_bitmap_test_bit(bm2, i))
            diff ++;

/*	int mask;

	mask = 255 >> (8 - bits);
	if ((bm1->bm_map [bytes] & mask) != (bm2->bm_map [bytes] & mask)) {
	    diff ++;
	}
    }*/
    return diff;
}


void reiserfs_bitmap_disjunction (reiserfs_bitmap_t * to, reiserfs_bitmap_t * from)
{
    int i;

    assert (to->bm_byte_size == from->bm_byte_size &&
	    to->bm_bit_size == from->bm_bit_size);

    for (i = 0; i < to->bm_bit_size; i++) {
	if (test_bit(i, from->bm_map) && !test_bit(i, to->bm_map)) {
	    set_bit(i, to->bm_map);
	    to->bm_set_bits ++;
	    to->bm_dirty = 1;	
	}
    }
}


void reiserfs_bitmap_set_bit (reiserfs_bitmap_t * bm, unsigned int bit_number)
{
    assert(bit_number < bm->bm_bit_size);
    if (test_bit (bit_number, bm->bm_map))
	return;
    set_bit(bit_number, bm->bm_map);
    bm->bm_set_bits ++;
    bm->bm_dirty = 1;
}


void reiserfs_bitmap_clear_bit (reiserfs_bitmap_t * bm, unsigned int bit_number)
{
    assert(bit_number < bm->bm_bit_size);
    if (!test_bit (bit_number, bm->bm_map))
	return;
    clear_bit (bit_number, bm->bm_map);
    bm->bm_set_bits --;
    bm->bm_dirty = 1;
}


int reiserfs_bitmap_test_bit (reiserfs_bitmap_t * bm, unsigned int bit_number)
{
    if (bit_number >= bm->bm_bit_size)
	printf ("bit %u, bitsize %lu\n", bit_number, bm->bm_bit_size);
    assert(bit_number < bm->bm_bit_size);
    return test_bit(bit_number, bm->bm_map);
}


int reiserfs_bitmap_zeros (reiserfs_bitmap_t * bm)
{
    return bm->bm_bit_size - bm->bm_set_bits;
}


int reiserfs_bitmap_ones (reiserfs_bitmap_t * bm)
{
    return bm->bm_set_bits;
}


int reiserfs_bitmap_find_zero_bit (reiserfs_bitmap_t * bm, unsigned long * start)
{
    unsigned int  bit_nr = *start;
    assert(*start < bm->bm_bit_size);

    bit_nr = find_next_zero_bit(bm->bm_map, bm->bm_bit_size, *start);

    if (bit_nr >= bm->bm_bit_size) { /* search failed */	
	return 1;
    }

    *start = bit_nr;
    return 0;
}


/* read every bitmap block and copy their content into bitmap 'bm' */
static int reiserfs_fetch_ondisk_bitmap (reiserfs_bitmap_t * bm, reiserfs_filsys_t * fs)
{
    unsigned long to_copy;
    int copied;
    int i;
    char * p;
    int last_byte_unused_bits;
    unsigned long block;
    struct buffer_head * bh;


    to_copy = (get_sb_block_count (fs->fs_ondisk_sb) + 7) / 8;

    /*reiserfs_warning (stderr, "Fetching on-disk bitmap..");*/
    assert (bm->bm_byte_size == to_copy);

    copied = fs->fs_blocksize;
    p = bm->bm_map;
    block = fs->fs_super_bh->b_blocknr + 1;

    while (to_copy) {
	bh = bread (fs->fs_dev, block, fs->fs_blocksize);
	if (!bh) {
	    reiserfs_warning (stderr, "reiserfs_fetch_ondisk_bitmap: "
			      "bread failed reading bitmap (%lu)\n", block);
	    bh = getblk (fs->fs_dev, block, fs->fs_blocksize); 
	    if (!bh)
		reiserfs_panic ("reiserfs_fetch_ondisk_bitmap: getblk failed");
	    memset (bh->b_data, 0xff, bh->b_size);
	    mark_buffer_uptodate (bh, 1);
	}

	if (to_copy < fs->fs_blocksize)
	    copied = to_copy;
	memcpy (p, bh->b_data, copied); 
	brelse (bh);
	p += copied;
	to_copy -= copied;

	/* next bitmap block */
	if (spread_bitmaps (fs))
	    block = (block / (fs->fs_blocksize * 8) + 1) * (fs->fs_blocksize * 8);
	else
	    block ++;
    }

    /* on disk bitmap has bits out of SB_BLOCK_COUNT set to 1, where as
       reiserfs_bitmap_t has those bits set to 0 */
    last_byte_unused_bits = bm->bm_byte_size * 8 - bm->bm_bit_size;
    for (i = 0; i < last_byte_unused_bits; i ++)
	clear_bit (bm->bm_bit_size + i, bm->bm_map);

    bm->bm_set_bits = 0;
    /* FIXME: optimize that */
    for (i = 0; i < bm->bm_bit_size; i ++)
	if (reiserfs_bitmap_test_bit (bm, i))
	    bm->bm_set_bits ++;
    
    bm->bm_dirty = 0;
    return 0;
}


/* copy bitmap 'bm' to buffers which hold on-disk bitmap if bitmap was ever
   changed and return 1. Otherwise - return 0 */
int reiserfs_flush_to_ondisk_bitmap (reiserfs_bitmap_t * bm, reiserfs_filsys_t * fs)
{
    unsigned long to_copy;
    int copied;
    int i;
    char * p;
    int last_byte_unused_bits;
    unsigned long block;
    struct buffer_head * bh;


    /* make sure that the device is big enough */
    bh = bread (fs->fs_dev, bm->bm_bit_size - 1, fs->fs_blocksize);
    if (!bh) {
	reiserfs_warning (stderr, "reiserfs_flush_to_ondisk_bitmap: bread failed for block %lu\n",
			  bm->bm_bit_size - 1);
/*
	bh = getblk (fs->fs_dev, bm->bm_bit_size - 1, fs->fs_blocksize);
	if (!bh)
	    reiserfs_panic ("reiserfs_flush_to_ondisk_bitmap: getblk failed");
	mark_buffer_uptodate (bh, 1);
	mark_buffer_dirty (bh);
	bwrite (bh);*/
    }
    brelse (bh);

    if (!bm->bm_dirty)
	return 0;

    to_copy = bm->bm_byte_size;

    copied = fs->fs_blocksize;
    p = bm->bm_map;
    block = fs->fs_super_bh->b_blocknr + 1;

    while (to_copy) {
	/* we bread to make sure that filesystem contains enough blocks */
	bh = getblk (fs->fs_dev, block, fs->fs_blocksize);
	if (!bh)
	    reiserfs_panic ("reiserfs_flush_to_ondisk_bitmap: "
			    "getblk failed for (%lu)\n", block);
	memset (bh->b_data, 0xff, bh->b_size);
	mark_buffer_uptodate (bh, 1);

	if (to_copy < fs->fs_blocksize)
	    copied = to_copy;
	memcpy (bh->b_data, p, copied);
	if (copied == to_copy) {
	    /* set unused bits of last byte of a bitmap to 1 */
	    last_byte_unused_bits = bm->bm_byte_size * 8 - bm->bm_bit_size;
	    for (i = 0; i < last_byte_unused_bits; i ++)
		set_bit ((bm->bm_bit_size % (fs->fs_blocksize * 8)) + i, bh->b_data);
	}
	mark_buffer_dirty (bh);
	brelse (bh);
	p += copied;
	to_copy -= copied;

	/* next bitmap block */
	if (spread_bitmaps (fs))
	    block = (block / (fs->fs_blocksize * 8) + 1) * (fs->fs_blocksize * 8);
	else
	    block ++;
    }


    return 1;
}


void reiserfs_bitmap_zero (reiserfs_bitmap_t * bm)
{
    memset (bm->bm_map, 0, bm->bm_byte_size);
    bm->bm_set_bits = 0;
    bm->bm_dirty = 1;
}


void reiserfs_bitmap_fill (reiserfs_bitmap_t * bm)
{
    memset (bm->bm_map, 0xff, bm->bm_byte_size);
    bm->bm_set_bits = bm->bm_bit_size;
    bm->bm_dirty = 1;
}


/* format of bitmap saved in a file:
   magic number (32 bits)
   bm_bit_size (32 bits)
   number of ranges of used and free blocks (32 bits)
   number of contiguously used block, .. of free blocks, used, free, etc
   magic number (32 bits) */

#define BITMAP_START_MAGIC 374031
#define BITMAP_END_MAGIC 7786472


FILE * open_file (char * filename, char * option)
{
    FILE * fp = fopen (filename, option);
    if (!fp) {
	reiserfs_warning (stderr, "open_file: could not open file %s\n", filename);
	return 0;
    }
    reiserfs_warning (stderr, "Temp file opened by fsck: \"%s\" .. \n", filename);
    return fp;
}


void close_file (FILE * fp)
{
    fclose (fp);
    /*reiserfs_warning (stderr, "done\n"); fflush (stderr);*/
}


void reiserfs_bitmap_save (FILE * fp, reiserfs_bitmap_t * bm)
{
//    FILE * fp;
    __u32 v;
    int zeros;
    int count;
    int i;
    int extents;
    long position;

  /*  fp = fopen (filename, "w+");
    if (!fp) {
	reiserfs_warning (stderr, "reiserfs_bitmap_save: could not save bitmap in %s: %m",
			  filename);
	return;
    }*/


//    reiserfs_warning (stderr, "Saving bitmap in \"%s\" .. ", filename); fflush (stderr);

	

    v = BITMAP_START_MAGIC;
    fwrite (&v, 4, 1, fp);

    v = bm->bm_bit_size;
    fwrite (&v, 4, 1, fp);

    /*printf ("SAVE: bit_size - %d\n", v);*/

    position = ftell(fp);

    if (fseek (fp, 4, SEEK_CUR)) {
	reiserfs_warning (stderr, "reiserfs_bitmap_save: fseek failed: %m\n");
//	fclose (fp);
	return;
    }

    zeros = 0;
    count = 0;
    extents = 0;
    for (i = 0; i < v; i ++) {
	if (reiserfs_bitmap_test_bit (bm, i)) {
	    if (zeros) {
		/* previous bit was not set, write amount of not set
                   bits, switch to count set bits */
		fwrite (&count, 4, 1, fp);
		/*printf ("SAVE: Free %d\n", count);*/
		extents ++;
		count = 1;
		zeros = 0;
	    } else {
		/* one more zero bit appeared */
		count ++;
	    }
	} else {
	    /* zero bit found */
	    if (zeros) {
		count ++;
	    } else {
		/* previous bit was set, write amount of set bits,
                   switch to count not set bits */
		fwrite (&count, 4, 1, fp);
		/*printf ("SAVE: Used %d\n", count);*/
		extents ++;
		count = 1;
		zeros = 1;
	    }
	}
    }

    fwrite (&count, 4, 1, fp);
    extents ++;
/*
    if (zeros)
	printf ("SAVE: Free %d\n", count);
    else	
	printf ("SAVE: Used %d\n", count);
*/

    v = BITMAP_END_MAGIC;
    fwrite (&v, 4, 1, fp);

    if (fseek (fp, position, SEEK_SET)) {
	reiserfs_warning (stderr, "reiserfs_bitmap_save: fseek failed: %m");
	return;
    }

    fwrite (&extents, 4, 1, fp);

    if (fseek (fp, 0, SEEK_END)) {
	reiserfs_warning (stderr, "reiserfs_bitmap_save: fseek failed: %m");
	return;
    }
}


/* format of fsck dump file:
        after pass0
   magic number                 (32 bits)
   passed stage number
   bitmap of leaves
   bitmap of good_unfm
   bitmap of bad_unfm
   magic number                 (32 bits) */

#define FSCK_DUMP_START_MAGIC 374033
#define FSCK_DUMP_END_MAGIC 7786470


void reiserfs_begin_stage_info_save(FILE * file, unsigned long stage)
{
    __u32 v = FSCK_DUMP_START_MAGIC;
    fwrite (&v, 4, 1, file);
    fwrite (&stage, 4, 1, file);
}


void reiserfs_end_stage_info_save(FILE * file)
{
    __u32 v = FSCK_DUMP_END_MAGIC;
    fwrite (&v, 4, 1, file);
}


/*return last passed stage*/
int is_stage_magic_correct (FILE * fp)
{
    __u32 v;

    if (fseek (fp, -4, SEEK_END)) {
	reiserfs_warning (stderr, "is_stage_magic_correct: fseek failed: %m\n");
	return -1;
    }

    fread (&v, 4, 1, fp);
    if (v != FSCK_DUMP_END_MAGIC) {
	reiserfs_warning (stderr, "is_stage_magic_correct: no magic found\n");	
	return -1;
    }

    if (fseek (fp, 0, SEEK_SET)) {
	reiserfs_warning (stderr, "is_stage_magic_correct: fseek failed: %m\n");
	return -1;
    }

    fread (&v, 4, 1, fp);
    if (v != FSCK_DUMP_START_MAGIC) {
	reiserfs_warning (stderr, "is_stage_magic_correct: no magic found\n");	
	return -1;
    }

    fread (&v, 4, 1, fp);
    if (v != PASS_0_DONE && v != PASS_1_DONE && v != TREE_IS_BUILT && v != SEMANTIC_DONE && v != LOST_FOUND_DONE) {
	reiserfs_warning (stderr, "is_stage_magic_correct: wrong pass found");	
	return -1;
    }

    return (__u16)v;
}


reiserfs_bitmap_t * reiserfs_bitmap_load (FILE * fp)
{
//    FILE * fp;
    __u32 v;
    int count;
    int i, j;
    int extents;
    int bit;
    reiserfs_bitmap_t * bm;
    
/*    fp = fopen (filename, "r");
    if (!fp) {
	reiserfs_warning (stderr, "reiserfs_bitmap_load: fopen failed: %m\n");
	return 0;
    }*/

    fread (&v, 4, 1, fp);
    if (v != BITMAP_START_MAGIC) {
	reiserfs_warning (stderr, "reiserfs_bitmap_load: "
			  "no bitmap start magic found");	
//	fclose (fp);
	return 0;
    }
	
    /* read bit size of bitmap */
    fread (&v, 4, 1, fp);

    bm = reiserfs_create_bitmap (v);
    if (!bm) {
	reiserfs_warning (stderr, "reiserfs_bitmap_load: creation failed");	
//	fclose (fp);
	return 0;
    }
    
    /*printf ("LOAD: bit_size - %d\n", v);*/

    fread (&extents, 4, 1, fp);

    /*printf ("LOAD: extents - %d\n", extents);*/

    bit = 0;
    for (i = 0; i < extents; i ++) {
	fread (&count, 4, 1, fp);
/*
	if (i % 2)
	    printf ("LOAD: Free %d\n", count);
	else
	    printf ("LOAD: Used %d\n", count);
*/
	for (j = 0; j < count; j ++, bit ++)
	    if (i % 2 == 0) {
		reiserfs_bitmap_set_bit (bm, bit);
	    }
    }

    fread (&v, 4, 1, fp);

    /*printf ("LOAD: Endmagic %d\n", v);*/

//    fclose (fp);
    if (v != BITMAP_END_MAGIC) {
	reiserfs_warning (stderr, "reiserfs_bitmap_load: "
			  "no bitmap end magic found");
	return 0;
    }

    /*    reiserfs_warning (stderr, "%d bits set - done\n", reiserfs_bitmap_ones (bm));*/
    fflush (stderr);
    return bm;
}


void reiserfs_bitmap_invert (reiserfs_bitmap_t * bm)
{
    int i;

    /*reiserfs_warning (stderr, "Bitmap inverting..");fflush (stderr);*/
    for (i = 0; i < bm->bm_bit_size; i ++) {
	if (reiserfs_bitmap_test_bit (bm, i))
	    reiserfs_bitmap_clear_bit (bm, i);
	else
	    reiserfs_bitmap_set_bit (bm, i);
    }

    /*reiserfs_warning (stderr, "done\n");*/
}


void reiserfs_free_ondisk_bitmap (reiserfs_filsys_t * fs)
{
    if (fs->fs_bitmap2) {
	reiserfs_delete_bitmap (fs->fs_bitmap2);
	fs->fs_bitmap2 = 0;
    }
}


/* read bitmap blocks */
int reiserfs_open_ondisk_bitmap (reiserfs_filsys_t * fs)
{
    struct buffer_head * bh;

    if (fs->fs_bitmap2)
	reiserfs_panic ("%s: bitmap is initiaized already", __FUNCTION__);
    fs->fs_bitmap2 = reiserfs_create_bitmap (get_sb_block_count (fs->fs_ondisk_sb));
    if (!fs->fs_bitmap2)
	return 0;

    if ( (get_sb_block_count (fs->fs_ondisk_sb) + fs->fs_blocksize * 8 - 1) / (fs->fs_blocksize * 8) !=
    	get_sb_bmap_nr (fs->fs_ondisk_sb)) {
	reiserfs_warning (stderr, "%s: wrong either bitmaps number,\n", __FUNCTION__);
	reiserfs_warning (stderr, "count of blocks or blocksize, run with --rebuild-sb to fix it\n");
	return 0;
    }    	

    bh = bread (fs->fs_dev, fs->fs_bitmap2->bm_bit_size - 1, fs->fs_blocksize);
    if (!bh) {
	reiserfs_warning (stderr, "%s: bread failed for block %lu\n", __FUNCTION__,  fs->fs_bitmap2->bm_bit_size - 1);
	reiserfs_warning (stderr, "\tYour partition is not big enough. Enlarge your partition or\n");
	reiserfs_warning (stderr, "\trun reiserfsck with --rebuild-sb to fix super block.\n");
	return 0;
    }	

    brelse (bh);
    	
    reiserfs_fetch_ondisk_bitmap (fs->fs_bitmap2, fs);
    return 1;
}

int reiserfs_create_ondisk_bitmap (reiserfs_filsys_t * fs)
{
    if (fs->fs_bitmap2)
	reiserfs_panic ("create: bitmap is initiaized already");
    fs->fs_bitmap2 = reiserfs_create_bitmap (get_sb_block_count (fs->fs_ondisk_sb));
    if (!fs->fs_bitmap2)
	return 0;

    return 1;
}


void reiserfs_close_ondisk_bitmap (reiserfs_filsys_t * fs)
{
    if (!fs->fs_bitmap2)
	return;
    reiserfs_flush_to_ondisk_bitmap (fs->fs_bitmap2, fs);
    reiserfs_free_ondisk_bitmap (fs);
}
