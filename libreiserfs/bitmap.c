/* 
 * Copyright 2000-2004 by Hans Reiser, licensing governed by 
 * reiserfsprogs/README
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "reiserfs/libreiserfs.h"
#include "misc/malloc.h"

#include <assert.h>
#include <errno.h>

/* create clean bitmap */
reiserfs_bitmap_t * reiserfs_bitmap_create (unsigned int bit_count)
{
    reiserfs_bitmap_t * bm;

    bm = misc_getmem (sizeof (*bm));
    if (!bm)
	return 0;
    bm->bm_bit_size = bit_count;
    bm->bm_byte_size = ((unsigned long long)bit_count + 7) / 8;
    bm->bm_map = misc_getmem (bm->bm_byte_size);
    if (!bm->bm_map) {
	misc_freemem (bm);
	return 0;
    }

    return bm;
}

/* read every bitmap block and copy their content into bitmap 'bm' */
static int reiserfs_bitmap_fetch (reiserfs_bitmap_t * bm, 
				  reiserfs_filsys_t * fs)
{
    unsigned int last_byte_unused_bits;
    unsigned long block, to_copy;
    reiserfs_bh_t * bh;
    unsigned int i;
    int copied;
    int ret = 0;
    char * p;

    to_copy = (reiserfs_sb_get_blocks (fs->fs_ondisk_sb) + 7) / 8;

    /*reiserfs_warning (stderr, "Fetching on-disk bitmap..");*/
    assert (bm->bm_byte_size == to_copy);

    copied = fs->fs_blocksize;
    p = bm->bm_map;
    block = fs->fs_super_bh->b_blocknr + 1;

    while (to_copy) {
	bh = reiserfs_buffer_read (fs->fs_dev, block, fs->fs_blocksize);
	if (!bh) {
	    reiserfs_warning (stderr, "reiserfs_bitmap_fetch: "
			      "reiserfs_buffer_read failed reading bitmap "
			      "(%lu)\n", block);
	    
	    bh = reiserfs_buffer_open (fs->fs_dev, block, fs->fs_blocksize); 
	    if (!bh) {
		reiserfs_exit (1, "reiserfs_bitmap_fetch: "
			       "reiserfs_buffer_open failed");
	    }

	    memset (bh->b_data, 0xff, bh->b_size);
	    reiserfs_buffer_mkuptodate (bh, 1);
	}

	if (to_copy < fs->fs_blocksize) {
	    for (i = to_copy; i < fs->fs_blocksize; i++) {
		if (bh->b_data[i] != (char)0xff) {
		    ret = 1;
		    break;
		}
	    }
	    
	    copied = to_copy;
	}
	memcpy (p, bh->b_data, copied); 
	reiserfs_buffer_close (bh);
	p += copied;
	to_copy -= copied;

	/* next bitmap block */
	if (reiserfs_bitmap_spread (fs))
	    block = (block / (fs->fs_blocksize * 8) + 1) * 
		    (fs->fs_blocksize * 8);
	else
	    block ++;
    }

    /* on disk bitmap has bits out of SB_BLOCK_COUNT set to 1, where as
       reiserfs_bitmap_t has those bits set to 0 */
    last_byte_unused_bits = bm->bm_byte_size * 8 - bm->bm_bit_size;

    for (i = 0; i < last_byte_unused_bits; i ++) {
	if (misc_test_bit (bm->bm_bit_size + i, bm->bm_map) == 0)
	    ret = 1;
	else	    
	    misc_clear_bit (bm->bm_bit_size + i, bm->bm_map);
    }

    bm->bm_set_bits = 0;
    /* FIXME: optimize that */
    for (i = 0; i < bm->bm_bit_size; i ++)
	if (reiserfs_bitmap_test_bit (bm, i))
	    bm->bm_set_bits ++;
    
    bm->bm_dirty = 0;

    return ret;
}

/* read bitmap blocks */
int reiserfs_bitmap_open (reiserfs_filsys_t * fs) {
    unsigned int bmap_nr, count;
    
    if (fs->fs_bitmap2)
	reiserfs_panic ("bitmap is initiaized already");

    count = reiserfs_sb_get_blocks (fs->fs_ondisk_sb);
    
    fs->fs_bitmap2 = reiserfs_bitmap_create (count);
    
    if (!fs->fs_bitmap2)
	return -1;

    bmap_nr = reiserfs_bmap_nr(count, fs->fs_blocksize);
    bmap_nr = reiserfs_bmap_over(bmap_nr) ? 0 : bmap_nr;
    
    if (bmap_nr != reiserfs_sb_get_bmaps (fs->fs_ondisk_sb)) {
	reiserfs_warning (stderr, "%s: wrong either bitmaps number,\n", 
			  __FUNCTION__);
	
	reiserfs_warning (stderr, "count of blocks or blocksize, run "
			  "with --rebuild-sb to fix it\n");
	return -1;
    }    	

    return reiserfs_bitmap_fetch (fs->fs_bitmap2, fs);
}

/* bitmap destructor */
void reiserfs_bitmap_delete (reiserfs_bitmap_t * bm)
{
    misc_freemem(bm->bm_map);
    bm->bm_map = NULL;		/* to not reuse bitmap handle */
    bm->bm_bit_size = 0;
    bm->bm_byte_size = 0;
    misc_freemem(bm);
}

void reiserfs_bitmap_free (reiserfs_filsys_t * fs)
{
    if (fs->fs_bitmap2) {
	reiserfs_bitmap_delete (fs->fs_bitmap2);
	fs->fs_bitmap2 = 0;
    }
}

void reiserfs_bitmap_close (reiserfs_filsys_t * fs)
{
    if (!fs->fs_bitmap2)
	return;
    reiserfs_bitmap_flush (fs->fs_bitmap2, fs);
    reiserfs_bitmap_free (fs);
}

/* copy bitmap 'bm' to buffers which hold on-disk bitmap if bitmap was ever
   changed and return 1. Otherwise - return 0 */
int reiserfs_bitmap_flush (reiserfs_bitmap_t * bm, reiserfs_filsys_t * fs)
{
    unsigned int last_byte_unused_bits, i;
    unsigned long to_copy, copied, block;
    reiserfs_bh_t * bh;
    char * p;

    /* make sure that the device is big enough */
    bh = reiserfs_buffer_read (fs->fs_dev, bm->bm_bit_size - 1, fs->fs_blocksize);
    if (!bh) {
	reiserfs_warning (stderr, "reiserfs_bitmap_flush: reiserfs_buffer_read "
			  "failed for block %lu\n", bm->bm_bit_size - 1);
/*
	bh = reiserfs_buffer_open (fs->fs_dev, bm->bm_bit_size - 1, 
			     fs->fs_blocksize);
	if (!bh)
	    reiserfs_panic ("reiserfs_bitmap_flush: reiserfs_buffer_open failed");
	reiserfs_buffer_mkuptodate (bh, 1);
	reiserfs_buffer_mkdirty (bh);
	reiserfs_buffer_write (bh);*/
    }
    reiserfs_buffer_close (bh);

    if (!bm->bm_dirty)
	return 0;

    to_copy = bm->bm_byte_size;

    copied = fs->fs_blocksize;
    p = bm->bm_map;
    block = fs->fs_super_bh->b_blocknr + 1;

    while (to_copy) {
	/* we reiserfs_buffer_read to make sure that filesystem contains 
	   enough blocks */
	bh = reiserfs_buffer_open (fs->fs_dev, block, fs->fs_blocksize);
	if (!bh) {
	    reiserfs_exit (1, "Getblk failed for (%lu)\n", block);
	}
	
	memset (bh->b_data, 0xff, bh->b_size);
	reiserfs_buffer_mkuptodate (bh, 1);

	if (to_copy < fs->fs_blocksize)
	    copied = to_copy;
	memcpy (bh->b_data, p, copied);
	if (copied == to_copy) {
	    /* set unused bits of last byte of a bitmap to 1 */
	    last_byte_unused_bits = bm->bm_byte_size * 8 - bm->bm_bit_size;

	    for (i = 0; i < last_byte_unused_bits; i ++)
		misc_set_bit ((bm->bm_bit_size % (fs->fs_blocksize * 8)) + i,
			      bh->b_data);
	}
	
	reiserfs_buffer_mkdirty (bh);
	reiserfs_buffer_close (bh);
	p += copied;
	to_copy -= copied;

	/* next bitmap block */
	if (reiserfs_bitmap_spread (fs))
	    block = (block / (fs->fs_blocksize * 8) + 1) * 
		    (fs->fs_blocksize * 8);
	else
	    block ++;
    }


    return 1;
}

/* Expand existing bitmap.  Return non-zero if can't. FIXME: it is
   assumed that bit_count is new number of blocks to be addressed */
int reiserfs_bitmap_expand (reiserfs_bitmap_t * bm, 
			    unsigned int bit_count)
{
    unsigned int byte_count = ((bit_count + 7) / 8);
    char * new_map;

    new_map = misc_expandmem (bm->bm_map, bm->bm_byte_size,
			 byte_count - bm->bm_byte_size);

    if (!new_map) {
	return 1;
    }
    
    bm->bm_map = new_map;
    bm->bm_byte_size = byte_count;
    bm->bm_bit_size = bit_count;

    bm->bm_dirty = 1;

    return 0;
}

void reiserfs_bitmap_shrink (reiserfs_bitmap_t * bm, 
			     unsigned int bit_count)
{
    unsigned long i;
    
    assert (bm->bm_bit_size >= bit_count);

    bm->bm_byte_size = (bit_count + 7) / 8;
    bm->bm_bit_size = bit_count;
    bm->bm_set_bits = 0;
    if (bm->bm_first_zero > bm->bm_bit_size)
	bm->bm_first_zero = bm->bm_bit_size;
    
    bm->bm_dirty = 1;
    
    for (i = 0; i < bm->bm_bit_size; i++) {
	if (reiserfs_bitmap_test_bit(bm, i))
	    bm->bm_set_bits++;
    }
}

void reiserfs_bitmap_copy (reiserfs_bitmap_t * to, 
			   reiserfs_bitmap_t * from)
{
    assert (to->bm_byte_size == from->bm_byte_size);
    memcpy (to->bm_map, from->bm_map, from->bm_byte_size);
    to->bm_bit_size = from->bm_bit_size;
    to->bm_set_bits = from->bm_set_bits;
    to->bm_first_zero = from->bm_first_zero;
    to->bm_dirty = 1;
}

int reiserfs_bitmap_compare (reiserfs_bitmap_t * bm1, 
			     reiserfs_bitmap_t * bm2)
{
    unsigned long i, diff;
    unsigned long int bytes, bits;

    assert (bm1->bm_byte_size == bm2->bm_byte_size &&
	    bm1->bm_bit_size == bm2->bm_bit_size);

    diff = 0;

    /* compare full bytes */
    bytes = bm1->bm_bit_size / 8;
    bits = bytes * 8;
    if (memcmp (bm1->bm_map, bm2->bm_map, bytes)) {
	for (i = 0; i < bits; i ++)
	    if (reiserfs_bitmap_test_bit(bm1, i) != 
		reiserfs_bitmap_test_bit(bm2, i))
	    {
		diff ++;
	    }
    }
    
    /* compare last byte of bitmap which can be used partially */
    bits = bm1->bm_bit_size % 8;
    for (i = bm1->bm_bit_size / 8 * 8; 
	 i < bm1->bm_bit_size / 8 * 8 + bits; 
	 i ++)
    {
	if (reiserfs_bitmap_test_bit(bm1, i) != 
	    reiserfs_bitmap_test_bit(bm2, i))
	{
            diff ++;
	}

/*	int mask;

	mask = 255 >> (8 - bits);
	if ((bm1->bm_map [bytes] & mask) != (bm2->bm_map [bytes] & mask)) {
	    diff ++;
	} */
    }

    return diff;
}

/* 
    Does X | Y for every bit of the bitmap `to`, where 
    X - bit of the `to` bitmap, 
    Y - `from` bitmap. 
    Save result in the `to` bitmap.
*/
void reiserfs_bitmap_disjunction (reiserfs_bitmap_t * to, 
    reiserfs_bitmap_t * from) 
{
    unsigned int i;

    assert (to->bm_byte_size == from->bm_byte_size &&
	    to->bm_bit_size == from->bm_bit_size);

    for (i = 0; i < to->bm_bit_size; i++) {
	if (misc_test_bit(i, from->bm_map) && 
	    !misc_test_bit(i, to->bm_map)) 
	{
	    misc_set_bit(i, to->bm_map);
	    to->bm_set_bits ++;
	    to->bm_dirty = 1;	
	}
    }
}

/* 
    Does X & !Y for every bit of the bitmap `base`, where 
    X - bit of the `base` bitmap, 
    Y - `exclude` bitmap. 
    Save result in the `base` bitmap.
*/
void reiserfs_bitmap_delta (reiserfs_bitmap_t * base, 
			    reiserfs_bitmap_t * exclude) 
{
   unsigned int i;

    assert (base->bm_byte_size == exclude->bm_byte_size &&
	    base->bm_bit_size == exclude->bm_bit_size);

    for (i = 0; i < base->bm_bit_size; i++) {
	if (misc_test_bit(i, exclude->bm_map) && 
	    misc_test_bit(i, base->bm_map)) 
	{
	    misc_clear_bit(i, base->bm_map);
	    base->bm_set_bits --;
	    base->bm_dirty = 1;
	}
    }
}

void reiserfs_bitmap_set_bit (reiserfs_bitmap_t * bm, 
			      unsigned int bit_number)
{
    assert(bit_number < bm->bm_bit_size);
    if (misc_test_bit (bit_number, bm->bm_map))
	return;
    misc_set_bit(bit_number, bm->bm_map);
    bm->bm_set_bits ++;
    bm->bm_dirty = 1;
}


void reiserfs_bitmap_clear_bit (reiserfs_bitmap_t * bm, 
				unsigned int bit_number)
{
    assert(bit_number < bm->bm_bit_size);
    if (!misc_test_bit (bit_number, bm->bm_map))
	return;
    misc_clear_bit (bit_number, bm->bm_map);
    bm->bm_set_bits --;
    bm->bm_dirty = 1;
    if (bm->bm_first_zero > bit_number)
	bm->bm_first_zero = bit_number;
}


int reiserfs_bitmap_test_bit (reiserfs_bitmap_t * bm, 
			      unsigned int bit_number)
{
    if (bit_number >= bm->bm_bit_size)
	printf ("bit %u, bitsize %lu\n", bit_number, bm->bm_bit_size);
    assert(bit_number < bm->bm_bit_size);
    return misc_test_bit(bit_number, bm->bm_map);
}


int reiserfs_bitmap_find_zero_bit (reiserfs_bitmap_t * bm, 
				   unsigned long * first)
{
    unsigned long bit_nr;
    int upd;
    
    assert(*first < bm->bm_bit_size);
    
    upd = (bm->bm_first_zero >= *first) ? 1 : 0;
    bit_nr = misc_find_next_zero_bit(bm->bm_map, bm->bm_bit_size, 
				     upd ? bm->bm_first_zero : *first);

    if (upd)
	bm->bm_first_zero = bit_nr;
    
    if (bit_nr >= bm->bm_bit_size) 
	/* search failed */	
	return 1;

    *first = bit_nr;
    return 0;
}

void reiserfs_bitmap_zero (reiserfs_bitmap_t * bm)
{
    memset (bm->bm_map, 0, bm->bm_byte_size);
    bm->bm_set_bits = 0;
    bm->bm_dirty = 1;
    bm->bm_first_zero = 0;
}


void reiserfs_bitmap_fill (reiserfs_bitmap_t * bm)
{
    memset (bm->bm_map, 0xff, bm->bm_byte_size);
    bm->bm_set_bits = bm->bm_bit_size;
    bm->bm_dirty = 1;
    bm->bm_first_zero = bm->bm_set_bits;
}

void reiserfs_bitmap_invert (reiserfs_bitmap_t * bm)
{
    unsigned int i;

    /*reiserfs_warning (stderr, "Bitmap inverting..");fflush (stderr);*/
    for (i = 0; i < bm->bm_bit_size; i ++) {
	if (reiserfs_bitmap_test_bit (bm, i))
	    reiserfs_bitmap_clear_bit (bm, i);
	else
	    reiserfs_bitmap_set_bit (bm, i);
    }

    /*reiserfs_warning (stderr, "done\n");*/
}

unsigned int reiserfs_bitmap_zeros (reiserfs_bitmap_t * bm) {
    return bm->bm_bit_size - bm->bm_set_bits;
}


unsigned int reiserfs_bitmap_ones (reiserfs_bitmap_t * bm) {
    return bm->bm_set_bits;
}

int reiserfs_bitmap_spread (reiserfs_filsys_t * fs) {
    return fs->fs_super_bh->b_blocknr != 2;
}

/* format of bitmap saved in a file:
   magic number (32 bits)
   bm_bit_size (32 bits)
   number of ranges of used and free blocks (32 bits)
   number of contiguously used block, .. of free blocks, used, free, etc
   magic number (32 bits) */

#define BITMAP_START_MAGIC 374031
#define BITMAP_END_MAGIC 7786472

void reiserfs_bitmap_save (FILE * fp, reiserfs_bitmap_t * bm)
{
//    FILE * fp;
    __u32 v;
    int zeros;
    int count;
    unsigned int i;
    int extents;
    long position;

  /*  fp = fopen (filename, "w+");
    if (!fp) {
	reiserfs_warning (stderr, "reiserfs_bitmap_save: could not save "
			  "bitmap in %s: %s", filename, strerror(errno));
	return;
    }*/


/*  reiserfs_warning (stderr, "Saving bitmap in \"%s\" .. ", filename); 
    fflush (stderr);*/	

    v = BITMAP_START_MAGIC;
    fwrite (&v, 4, 1, fp);

    v = bm->bm_bit_size;
    fwrite (&v, 4, 1, fp);

    /*printf ("SAVE: bit_size - %d\n", v);*/

    position = ftell(fp);

    if (fseek (fp, 4, SEEK_CUR)) {
	reiserfs_warning (stderr, "reiserfs_bitmap_save: fseek failed: %s\n", 
			  strerror(errno));
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
	reiserfs_warning (stderr, "reiserfs_bitmap_save: fseek failed: %s", 
			  strerror(errno));
	return;
    }

    fwrite (&extents, 4, 1, fp);

    if (fseek (fp, 0, SEEK_END)) {
	reiserfs_warning (stderr, "reiserfs_bitmap_save: fseek failed: %s", 
			  strerror(errno));
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
	reiserfs_warning (stderr, "reiserfs_bitmap_load: fopen failed: %s\n", 
			  strerror(errno));
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

    bm = reiserfs_bitmap_create (v);
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

/*    reiserfs_warning (stderr, "%d bits set - done\n", 
			reiserfs_bitmap_ones (bm));*/
    fflush (stderr);
    return bm;
}


int reiserfs_bitmap_block (reiserfs_filsys_t * fs, unsigned long block) {
    unsigned int bmap_nr;
    
    if (reiserfs_bitmap_spread (fs)) {
	if (!(block % (fs->fs_blocksize * 8)))
	    /* bitmap block */
	    return 1;
	return ((REISERFS_DISK_OFFSET_IN_BYTES / fs->fs_blocksize + 1) 
		== block);
    } else {
	bmap_nr = reiserfs_bmap_nr(reiserfs_sb_get_blocks(fs->fs_ondisk_sb),
				   fs->fs_blocksize);
	
	/* bitmap in */
	return (block > 2ul && block < 3ul + bmap_nr) ? 1 : 0;
    }
    
    return 0;
}

/* read bitmap of disk and print details */
void reiserfs_bitmap_print (FILE * fp, reiserfs_filsys_t * fs, int silent)
{
    reiserfs_sb_t * sb;
    int bmap_nr;
    int i;
    int bits_per_block;
    int blocks;
    unsigned long block;
    reiserfs_bh_t * bh;


    sb = fs->fs_ondisk_sb;
    bits_per_block = fs->fs_blocksize * 8;
    blocks = bits_per_block;
    bmap_nr = reiserfs_bmap_nr(reiserfs_sb_get_blocks(fs->fs_ondisk_sb),
			       fs->fs_blocksize);

    reiserfs_warning (fp, "Bitmap blocks are:\n");
    block = fs->fs_super_bh->b_blocknr + 1;
    for (i = 0; i < bmap_nr; i ++) {
	bh = reiserfs_buffer_read (fs->fs_dev, block, fs->fs_blocksize);
	if (!bh) {
	    reiserfs_warning (stderr, "reiserfs_bitmap_print: reiserfs_buffer_read "
			      "failed for %d: %lu\n", i, block);
	    continue;
	}
	if (i == bmap_nr - 1)
	    if (reiserfs_sb_get_blocks (sb) % bits_per_block)
		blocks = reiserfs_sb_get_blocks (sb) % bits_per_block;
	reiserfs_print_bmap_block (fp, i, block, bh->b_data, blocks, 
				   silent, fs->fs_blocksize);
	
	reiserfs_buffer_close (bh);

	if (reiserfs_bitmap_spread (fs))
	    block = (block / (fs->fs_blocksize * 8) + 1) * 
		    (fs->fs_blocksize * 8);
	else {
	    block ++;
	}
    }
}
