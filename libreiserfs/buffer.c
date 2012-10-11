/*
 * Copyright 1996-2004 by Hans Reiser, licensing governed by 
 * reiserfsprogs/README
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "reiserfs/libreiserfs.h"
#include "misc/malloc.h"

#include <sys/types.h>
#include <unistd.h>
#include <errno.h>

static int is_bad_block (unsigned long block)
{
#ifdef IO_FAILURE_EMULATION
    
    /* this array similates bad blocks on the device */
    unsigned long bad_blocks [] =
	{
	    8208, 8209, 8210
/*, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 17, 18, 19*/
	};
    int i;
    
    for (i = 0; i < sizeof (bad_blocks) / sizeof (bad_blocks[0]); i ++)
	if (bad_blocks [i] == block)
	    return 1;

#endif

    return 0;
}



/* All buffers are in double linked cycled list.  If reiserfs_buffer_open found buffer with
   wanted block number in hash queue it moves buffer to the end of list. */

static int g_nr_buffers;


static unsigned long buffers_memory;

/* create buffers until we spend this fraction of system memory, this
** is a hard limit on the amount of buffer ram used
*/
#define BUFFER_MEMORY_FRACTION 10

/* number of bytes in local buffer cache before we start forcing syncs
** of dirty data and reusing unused buffers instead of allocating new
** ones.  If a flush doesn't find reusable buffers, new ones are
** still allocated up to the BUFFER_MEMORY_FRACTION percentage
**
*/
#define BUFFER_SOFT_LIMIT (500 * 1024)
static unsigned long buffer_soft_limit = BUFFER_SOFT_LIMIT;


#define NR_HASH_QUEUES 4096
static reiserfs_bh_t * g_a_hash_queues [NR_HASH_QUEUES];
static reiserfs_bh_t * Buffer_list_head;
static reiserfs_bh_t * g_free_buffers = NULL ;
static reiserfs_bh_t * g_buffer_heads;
static int buffer_hits = 0 ;
static int buffer_misses = 0 ;
static int buffer_reads = 0 ;
static int buffer_writes = 0 ;



static void _show_buffers(reiserfs_bh_t **list, int dev, unsigned long size) {
    int all = 0;
    int dirty = 0;
    int in_use = 0; /* count != 0 */
    int free = 0;
    reiserfs_bh_t * next;

    next = *list;
    if (!next)
        return ;

    for (;;) {
	if (next->b_dev == dev && next->b_size == size) {
	    all ++;
	    if (next->b_count != 0) {
		in_use ++;
	    }
	    if (reiserfs_buffer_isdirty (next)) {
		dirty ++;
	    }
	    if (reiserfs_buffer_isclean (next) && next->b_count == 0) {
		free ++;
	    }
	}
	next = next->b_next;
	if (next == *list)
	    break;
    }

    printf("show_buffers (dev %d, size %lu): free %d, count != 0 %d, dirty %d, "
	"all %d\n", dev, size, free, in_use, dirty, all);
}


static void show_buffers (int dev, int size)
{
    _show_buffers(&Buffer_list_head, dev, size) ;
    _show_buffers(&g_free_buffers, dev, size) ;
}


static void insert_into_hash_queue (reiserfs_bh_t * bh)
{
    int index = bh->b_blocknr % NR_HASH_QUEUES;

    if (bh->b_hash_prev || bh->b_hash_next)
	misc_die ("insert_into_hash_queue: hash queue corrupted");

    if (g_a_hash_queues[index]) {
	g_a_hash_queues[index]->b_hash_prev = bh;
	bh->b_hash_next = g_a_hash_queues[index];
    }
    g_a_hash_queues[index] = bh;
}


static void remove_from_hash_queue (reiserfs_bh_t * bh)
{
    if (bh->b_hash_next == 0 && 
	bh->b_hash_prev == 0 && 
	bh != g_a_hash_queues[bh->b_blocknr % NR_HASH_QUEUES])
    {
	/* (b_dev == -1) ? */
	return;
    }

    if (bh == g_a_hash_queues[bh->b_blocknr % NR_HASH_QUEUES]) {
	if (bh->b_hash_prev != 0)
	    misc_die ("remove_from_hash_queue: hash queue corrupted");
	g_a_hash_queues[bh->b_blocknr % NR_HASH_QUEUES] = bh->b_hash_next;
    }
    if (bh->b_hash_next)
	bh->b_hash_next->b_hash_prev = bh->b_hash_prev;

    if (bh->b_hash_prev)
	bh->b_hash_prev->b_hash_next = bh->b_hash_next;

    bh->b_hash_prev = bh->b_hash_next = 0;
}


static void put_buffer_list_end (reiserfs_bh_t **list,
                                 reiserfs_bh_t * bh)
{
    reiserfs_bh_t * last = 0;

    if (bh->b_prev || bh->b_next)
	misc_die ("put_buffer_list_end: buffer list corrupted");

    if (*list == 0) {
	bh->b_next = bh;
	bh->b_prev = bh;
	*list = bh;
    } else {
	last = (*list)->b_prev;

	bh->b_next = last->b_next;
	bh->b_prev = last;
	last->b_next->b_prev = bh;
	last->b_next = bh;
    }
}


static void remove_from_buffer_list (reiserfs_bh_t **list,
                                     reiserfs_bh_t * bh)
{
    if (bh == bh->b_next) {
	*list = 0;
    } else {
	bh->b_prev->b_next = bh->b_next;
	bh->b_next->b_prev = bh->b_prev;
	if (bh == *list)
	    *list = bh->b_next;
    }

    bh->b_next = bh->b_prev = 0;
}


static void put_buffer_list_head (reiserfs_bh_t **list,
                                  reiserfs_bh_t * bh)
{
    put_buffer_list_end (list, bh);
    *list = bh;
}

/*
#include <sys/mman.h>

static size_t estimate_memory_amount (void)
{
    size_t len = 1;
    size_t max = 0;
    void * addr;

    while (len > 0) {
	addr = mmap (0, len, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (addr == MAP_FAILED) {
	    if (errno != ENOMEM)
		misc_die ("mmap failed: %s\n", strerror(errno));
	    break;
	}
	if (mlock (addr, len) != 0) {
	    if (errno == EPERM)
		misc_die ("No permission to run mlock");
	    break;
	}

	munlock (addr, len);
	munmap (addr, len);
	max = len;
	len *= 2;
    }

    // * If we've looped, we don't want to return 0, we want to return the
    // * last successful len before we looped. In the event that mmap/mlock
    // * failed for len = 1, max will still be 0, so we don't get an invalid
    // * result
    return max;
}
*/

#define GROW_BUFFERS__NEW_BUFERS_PER_CALL 10

/* creates number of new buffers and insert them into head of buffer list */
static int grow_buffers (int size)
{
    int i;
    reiserfs_bh_t * bh, * tmp;


    /* get memory for array of buffer heads */
    bh = (reiserfs_bh_t *)misc_getmem (GROW_BUFFERS__NEW_BUFERS_PER_CALL * 
				       sizeof (reiserfs_bh_t) + sizeof (reiserfs_bh_t *));
    if (g_buffer_heads == 0)
	g_buffer_heads = bh;
    else {
	/* link new array to the end of array list */
	tmp = g_buffer_heads;
	while (*(reiserfs_bh_t **)(tmp + GROW_BUFFERS__NEW_BUFERS_PER_CALL) != 0)
	    tmp = *(reiserfs_bh_t **)(tmp + GROW_BUFFERS__NEW_BUFERS_PER_CALL);
	*(reiserfs_bh_t **)(tmp + GROW_BUFFERS__NEW_BUFERS_PER_CALL) = bh;
    }

    for (i = 0; i < GROW_BUFFERS__NEW_BUFERS_PER_CALL; i ++) {

	tmp = bh + i;
	memset (tmp, 0, sizeof (reiserfs_bh_t));
	tmp->b_data = misc_getmem (size);
	if (tmp->b_data == 0)
	    misc_die ("grow_buffers: no memory for new buffer data");
	tmp->b_dev = -1;
	tmp->b_size = size;
	put_buffer_list_head (&g_free_buffers, tmp);
    }
    buffers_memory += GROW_BUFFERS__NEW_BUFERS_PER_CALL * size;
    g_nr_buffers += GROW_BUFFERS__NEW_BUFERS_PER_CALL;
    return GROW_BUFFERS__NEW_BUFERS_PER_CALL;
}


reiserfs_bh_t *reiserfs_buffer_find (int dev, 
				     unsigned long block, 
				     unsigned long size)
{
    reiserfs_bh_t * next;

    next = g_a_hash_queues[block % NR_HASH_QUEUES];
    for (;;) {
	reiserfs_bh_t *tmp = next;
	if (!next)
	    break;
	next = tmp->b_hash_next;
	if (tmp->b_blocknr != block || tmp->b_size != size || tmp->b_dev != dev)
	    continue;
	next = tmp;
	break;
    }
    return next;
}


static reiserfs_bh_t * get_free_buffer (reiserfs_bh_t **list,
					unsigned long size)
{
    reiserfs_bh_t * next;

    next = *list;
    if (!next)
	return 0;

    for (;;) {
	if (!next)
	    misc_die ("get_free_buffer: buffer list is corrupted");
	if (next->b_count == 0 && reiserfs_buffer_isclean (next) && next->b_size == size) {
	    remove_from_hash_queue (next);
	    remove_from_buffer_list (list, next);
	    return next;
	}
	next = next->b_next;
	if (next == *list)
	    break;
    }
    return 0;
}


/* to_write == 0 when all blocks have to be flushed. Otherwise - write only
   buffers with b_count == 0 */
static int sync_buffers (reiserfs_bh_t **list, int dev, int to_write) {
    reiserfs_bh_t * next;
    int written = 0;


restart:
    next = *list;
    if (!next)
	return 0;
    for (;;) {
	if (!next)
	    misc_die ("sync_buffers: buffer list is corrupted");
 
	if (next->b_dev == dev && reiserfs_buffer_isdirty (next) && reiserfs_buffer_uptodate (next)) {
	    if ((to_write == 0 || next->b_count == 0) && !reiserfs_buffer_noflush (next)) {
		reiserfs_buffer_write (next);
	    }
	}
    
	/* if this buffer is reusable, put it onto the end of the free list */
	if (next->b_count == 0 && reiserfs_buffer_isclean(next)) {
	    remove_from_hash_queue (next);
	    remove_from_buffer_list (list, next);
	    put_buffer_list_end (&g_free_buffers, next);
	    written++ ;
	    if (written == to_write)
		return written;
	    goto restart;
	}
	if (to_write && written >= to_write)
	    return written;

	next = next->b_next;
	if (next == *list)
	    break;
    }

    return written;
}

void reiserfs_buffer_flush_all (int dev) {
    if (dev == -1)
	misc_die ("reiserfs_buffer_flush_all: device is not specified");

    sync_buffers (&Buffer_list_head, dev, 0/*all*/);
    buffer_soft_limit = BUFFER_SOFT_LIMIT;
}


reiserfs_bh_t * reiserfs_buffer_open (int dev, unsigned long block, 
				      unsigned long size)
{
    reiserfs_bh_t * bh;

    bh = reiserfs_buffer_find (dev, block, size);
    if (bh) {
	/* move the buffer to the end of list */

	/*checkmem (bh->b_data, bh->b_size);*/

	remove_from_buffer_list (&Buffer_list_head, bh);
	put_buffer_list_end (&Buffer_list_head, bh);
	bh->b_count ++;
	buffer_hits++ ;
	return bh;
    }
    buffer_misses++ ;

    bh = get_free_buffer (&g_free_buffers, size);
    if (bh == NULL) {
	if (buffers_memory >= buffer_soft_limit) {
	    if (sync_buffers (&Buffer_list_head, dev, 32) == 0) {
		grow_buffers(size);
		buffer_soft_limit = buffers_memory + 
			GROW_BUFFERS__NEW_BUFERS_PER_CALL * size;
	    }
	} else {
	    if (grow_buffers(size) == 0)
		sync_buffers (&Buffer_list_head, dev, 32);
	}

	bh = get_free_buffer (&g_free_buffers, size);
	if (bh == NULL) {
	    show_buffers (dev, size);
	    misc_die ("reiserfs_buffer_open: no free buffers after grow_buffers "
		 "and refill (%d)", g_nr_buffers);
	}
    }

    bh->b_count = 1;
    bh->b_dev = dev;
    bh->b_size = size;
    bh->b_blocknr = block;
    bh->b_end_io = NULL ;
    memset (bh->b_data, 0, size);
    misc_clear_bit(BH_Dirty, &bh->b_state);
    misc_clear_bit(BH_Uptodate, &bh->b_state);

    put_buffer_list_end (&Buffer_list_head, bh);
    insert_into_hash_queue (bh);
    /*checkmem (bh->b_data, bh->b_size);*/

    return bh;
}


void reiserfs_buffer_close (reiserfs_bh_t * bh)
{
    if (bh == 0)
	return;
    
    if (bh->b_count == 0)
	misc_die ("reiserfs_buffer_close: can not free a free buffer %lu", bh->b_blocknr);
    
    /*checkmem (bh->b_data, misc_memsize (bh->b_data));*/
    
    bh->b_count --;
}


void reiserfs_buffer_forget (reiserfs_bh_t * bh) {
    if (bh) {
	bh->b_state = 0;
	reiserfs_buffer_close (bh);
	remove_from_hash_queue (bh);
	remove_from_buffer_list(&Buffer_list_head, bh);
	put_buffer_list_head(&Buffer_list_head, bh);
    }
}

/* Returns 0 on success; 1 - end of file; 0 - OK. */
static int f_read(reiserfs_bh_t * bh)
{
    unsigned long long offset;
    ssize_t bytes;

    buffer_reads++ ;

    offset = (unsigned long long)bh->b_size * bh->b_blocknr;
    if (lseek (bh->b_dev, offset, SEEK_SET) < 0)
	return -1;

    bytes = read (bh->b_dev, bh->b_data, bh->b_size);
    
    return bytes < 0 ? -1 : (bytes != (ssize_t)bh->b_size ? 1 : 0);
}

#define check_hd_msg								\
"\nThe problem has occurred looks like a hardware problem. If you have\n"	\
"bad blocks, we advise you to get a new hard drive, because once you\n"		\
"get one bad block  that the disk  drive internals  cannot hide from\n"		\
"your sight,the chances of getting more are generally said to become\n"		\
"much higher  (precise statistics are unknown to us), and  this disk\n"		\
"drive is probably not expensive enough  for you to you to risk your\n"		\
"time and  data on it.  If you don't want to follow that follow that\n"		\
"advice then  if you have just a few bad blocks,  try writing to the\n"		\
"bad blocks  and see if the drive remaps  the bad blocks (that means\n"		\
"it takes a block  it has  in reserve  and allocates  it for use for\n"		\
"of that block number).  If it cannot remap the block,  use badblock\n"		\
"option (-B) with  reiserfs utils to handle this block correctly.\n"		\

reiserfs_bh_t *reiserfs_buffer_read (int dev, unsigned long block, 
				     unsigned long size)
{
    reiserfs_bh_t * bh;
    int ret;

    if (is_bad_block (block))
	return 0;

    bh = reiserfs_buffer_open (dev, block, size);
    
    /*checkmem (bh->b_data, misc_memsize(bh->b_data));*/
    
    if (reiserfs_buffer_uptodate (bh))
	return bh;

    ret = f_read(bh);
    
    if (ret > 0) {
	misc_die ("%s: End of file, cannot read the block (%lu).\n", 
	     __FUNCTION__, block);
    } else if (ret < 0) {
	/* BAD BLOCK LIST SUPPORT
	 * misc_die ("%s: Cannot read a block # %lu. Specify list of badblocks\n",*/

	if (errno == EIO) {
	    fprintf(stderr, check_hd_msg);
	    misc_die ("%s: Cannot read the block (%lu): (%s).\n", 
		 __FUNCTION__, block, strerror(errno));
	} else	{
	    fprintf (stderr, "%s: Cannot read the block (%lu): (%s).\n", 
		     __FUNCTION__, block, strerror(errno));
	    return NULL;
	}
    }
       
    reiserfs_buffer_mkuptodate (bh, 0);
    return bh;
}

/* for now - just make sure that bad blocks did not get here */
int reiserfs_buffer_write (reiserfs_bh_t * bh)
{
    off_t offset;
    long long bytes, size;

    if (is_bad_block (bh->b_blocknr)) {
	fprintf (stderr, "reiserfs_buffer_write: bad block is going to be written: %lu\n",
		 bh->b_blocknr);
	exit(8);
    }

    if (!reiserfs_buffer_isdirty (bh) || !reiserfs_buffer_uptodate (bh))
	return 0;

    buffer_writes++ ;
    if (bh->b_start_io)
	/* this is used by undo feature of reiserfsck */
	bh->b_start_io (bh->b_blocknr);

    size = bh->b_size;
    offset = (off_t)size * bh->b_blocknr;

    if (lseek (bh->b_dev, offset, SEEK_SET) == (long long int)-1){
	fprintf (stderr, "reiserfs_buffer_write: lseek to position %llu (block=%lu, dev=%d): %s\n",
	    offset, bh->b_blocknr, bh->b_dev, strerror(errno));
	exit(8); /* File system errors left uncorrected */
    }

#ifdef ROLLBACK_READY
    if (s_rollback_file != NULL && bh->b_size == (unsigned long)rollback_blocksize) {
        struct stat buf;
        int position;
	struct block_handler block_h;
        
        /*log previous content into the log*/
        if (!fstat (bh->b_dev, &buf)) {
	    block_h.blocknr = bh->b_blocknr;
	    block_h.device = buf.st_rdev;
	    if (misc_bin_search(&block_h, rollback_blocks_array, 
				rollback_blocks_number, sizeof (block_h), 
				&position, blockdev_list_compare) != 1)
	    {
                /*read initial data from the disk*/
                if (read(bh->b_dev, rollback_data, bh->b_size) == (long long)bh->b_size) {
                    fwrite(&buf.st_rdev, sizeof (buf.st_rdev), 1, s_rollback_file);
                    fwrite(&offset, sizeof (offset), 1, s_rollback_file);
                    fwrite(rollback_data, rollback_blocksize, 1, s_rollback_file);
                    fflush(s_rollback_file);
                    blocklist__insert_in_position(&block_h, (void *)(&rollback_blocks_array),
			&rollback_blocks_number, sizeof(block_h), &position);
		    
                    /*if you want to know what gets saved, uncomment it*/
/*                    if (log_file != 0 && log_file != stdout) {
                        fprintf (log_file, "rollback: block %lu of device %Lu was "
			    "backed up\n", bh->b_blocknr, buf.st_rdev);
                    }
*/
                    
                } else {
                    fprintf (stderr, "reiserfs_buffer_write: read (block=%lu, dev=%d): %s\n", 
			bh->b_blocknr, bh->b_dev, strerror (errno));
                    exit(8);
                }
		
                if (lseek (bh->b_dev, offset, SEEK_SET) == (long long int)-1) {
                    fprintf (stderr, "reiserfs_buffer_write: lseek to position %llu (block=%lu, "
			"dev=%d): %s\n", offset, bh->b_blocknr, bh->b_dev, 
			strerror(errno));
                    exit(8);
                }
            }
        } else {
            fprintf (stderr, "reiserfs_buffer_write: fstat of (%d) returned -1: %s\n", 
		bh->b_dev, strerror(errno));
        }
    } else if (s_rollback_file != NULL) {
	fprintf (stderr, "rollback: block (%lu) has the size different from "
	    "the fs uses, block skipped\n", bh->b_blocknr);
    }
#endif

    bytes = write(bh->b_dev, bh->b_data, size);
    if (bytes != size) {
	fprintf (stderr, "reiserfs_buffer_write: write %lld bytes returned %lld (block=%ld, "
	    "dev=%d): %s\n", size, bytes, bh->b_blocknr, bh->b_dev, 
	    strerror(errno));
	exit(8);
    }

    reiserfs_buffer_mkclean (bh);

    if (bh->b_end_io) {
	bh->b_end_io(bh, 1) ;
    }

    return 0;
}


static int _check_and_free_buffer_list(reiserfs_bh_t *list) {
    reiserfs_bh_t *next = list ;
    int count = 0 ;
    if (!list)
	return 0 ;

    for(;;) {
	if (next->b_count != 0)
	    fprintf (stderr, "check_and_free_buffer_mem: not free buffer "
		"(%d, %ld, %ld, %d)\n", next->b_dev, next->b_blocknr, 
		next->b_size, next->b_count);

	if (reiserfs_buffer_isdirty (next) && reiserfs_buffer_uptodate (next))
	    fprintf (stderr, "check_and_free_buffer_mem: dirty buffer "
		"(%d %lu) found\n", next->b_dev, next->b_blocknr);

	misc_freemem (next->b_data);
	count++;
	next = next->b_next;
	if (next == list)
            break;
    }
    return count;
}

void reiserfs_buffer_free_all (void) {
    int count = 0;
    reiserfs_bh_t * next ;

//    printf("check and free buffer mem, hits %d misses %d reads %d writes %d\n", 
//	    buffer_hits, buffer_misses, buffer_reads, buffer_writes) ;
    /*sync_buffers (0, 0);*/

    count = _check_and_free_buffer_list(Buffer_list_head);
    count += _check_and_free_buffer_list(g_free_buffers);

    if (count != g_nr_buffers)
       misc_die ("check_and_free_buffer_mem: found %d buffers, must be %d", 
	    count, g_nr_buffers);

    /* free buffer heads */
    while ((next = g_buffer_heads)) {
	g_buffer_heads = *(reiserfs_bh_t **)
		(next + GROW_BUFFERS__NEW_BUFERS_PER_CALL);

	misc_freemem (next);
    }
  
    return;
}

static void _invalidate_buffer_list(reiserfs_bh_t *list, int dev)
{
    reiserfs_bh_t * next;

    if (!list)
	return;

    next = list;

    for (;;) {
	if (next->b_dev == dev) {
	    if (reiserfs_buffer_isdirty (next) || next->b_count)
		fprintf (stderr, "invalidate_buffer_list: dirty "
			 "buffer or used buffer (%d %lu) found\n", 
			 next->b_count, next->b_blocknr);
	    
	    next->b_state = 0;
	    remove_from_hash_queue (next);
	}
	
	next = next->b_next;
	if (next == list)
	    break;
    }
}

/* forget all buffers of the given device */
void reiserfs_buffer_invalidate_all (int dev) {
    _invalidate_buffer_list(Buffer_list_head, dev) ;
    _invalidate_buffer_list(g_free_buffers, dev) ;
}
