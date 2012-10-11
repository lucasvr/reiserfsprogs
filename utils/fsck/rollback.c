/*
 * Copyright 1996-2004 by Hans Reiser, licensing governed by 
 * reiserfsprogs/README
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "fsck.h"
#include "misc/malloc.h"
#include "util/misc.h"

#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

#define ROLLBACK_FILE_START_MAGIC       "_RollBackFileForReiserfsFSCK"

static struct block_handler * rollback_blocks_array;
static __u32 rollback_blocks_number = 0;
static FILE * s_rollback_file = 0;
static FILE * log_file;
static int do_rollback = 0;

static char * rollback_data;
static unsigned int rollback_blocksize;

void fsck_rollback_init (char * rollback_file, unsigned int *blocksize, FILE * log) {
    char * string;
    struct stat buf;
    
    if (rollback_file == NULL)
        return;
        
    stat(rollback_file, &buf);
    
    s_rollback_file = fopen (rollback_file, "w+");    
    if (s_rollback_file == NULL) {
	fprintf (stderr, "Cannot create file %s, work without "
		 "a rollback file\n", rollback_file);
        return;
    }

    rollback_blocksize = *blocksize;

    string = ROLLBACK_FILE_START_MAGIC;
    fwrite (string, 28, 1, s_rollback_file);
    fwrite (&rollback_blocksize, sizeof(rollback_blocksize), 
	    1, s_rollback_file);
    
    fwrite (&rollback_blocks_number, sizeof(rollback_blocks_number), 
	    1, s_rollback_file);
    
    fflush(s_rollback_file);
        
    rollback_data = misc_getmem(rollback_blocksize);
    
//    printf("\ncheckmem1");
//    fflush (stdout);
//    checkmem (rollback_data, misc_memsize((char *)rollback_data));
//    printf(" OK");
    
    log_file = log;
    if (log_file)
        fprintf (log_file, "rollback: file (%s) initialize\n", rollback_file);

    do_rollback = 0;
}

#if 0
static void erase_rollback_file (char * rollback_file) {
    close_rollback_file ();
    unlink (rollback_file);    
}
#endif

int fsck_rollback_prep (char * rollback_file, FILE * log) {
    char string [28];
    struct stat buf;
    
    if (rollback_file == NULL)
        return -1;
    
    if (stat(rollback_file, &buf)) {
	fprintf (stderr, "Cannot stat rollback file (%s)\n", rollback_file);
	return -1;
    }
        
    s_rollback_file = fopen (rollback_file, "r+");
    if (s_rollback_file == NULL) {
	fprintf (stderr, "Cannot open file (%s)\n", rollback_file);
	return -1;
    }
    
    fread (string, 28, 1, s_rollback_file);
    if (!strcmp (string, ROLLBACK_FILE_START_MAGIC)) {
        fprintf (stderr, "Specified file (%s) does not look like "
		 "a rollback file\n", rollback_file);
	
        fclose (s_rollback_file);
        s_rollback_file = 0;
        return -1;
    }
    
    fread (&rollback_blocksize, sizeof (rollback_blocksize), 
	   1, s_rollback_file);
    
    if (rollback_blocksize <= 0) {
        fprintf(stderr, "rollback: wrong rollback blocksize, exit\n");
        return -1;
    }
    
    log_file = log;
    if (log_file)
        fprintf (log_file, "rollback: file (%s) opened\n", rollback_file);
    
    do_rollback = 1;
    return 0;
}

void fsck_rollback_fini () {
    if (s_rollback_file == 0)
        return;   

    if (!do_rollback) {
        if (fseek (s_rollback_file, 28 + sizeof(int), SEEK_SET) == (off_t)-1)
            return;
	
        fwrite (&rollback_blocks_number, 
		sizeof (rollback_blocksize), 
		1, s_rollback_file);
	
        if (log_file != 0) {
            fprintf (log_file, "rollback: %d blocks backed up\n", 
		     rollback_blocks_number);
	}
    }
        
    fclose (s_rollback_file);

    misc_freemem (rollback_data);
    misc_freemem (rollback_blocks_array);
/*
    fprintf (stdout, "rollback: (%u) blocks saved, \n", 
	     rollback_blocks_number);
    
    for (i = 0; i < rollback_blocks_number; i++) 
        fprintf(stdout, "device (%Lu), block number (%u)\n", 
                rollback_blocks_array [i].device, 
                rollback_blocks_array [i].blocknr);
    fprintf(stdout, "\n");
*/

}

void fsck_rollback (int fd_device, int fd_journal_device, FILE * progress) {
    long long int offset;
    
    struct stat buf;
    int descriptor;
    ssize_t retval;
    int count_failed = 0;
    int count_rollbacked = 0;
    
    int b_dev;
    int n_dev = 0;
    int n_journal_dev = 0;
    unsigned long total, done = 0;

    if (fd_device == 0) {
        fprintf(stderr, "rollback: unspecified device, exit\n");
        return;
    }
        
    if (fd_journal_device) {
        if (!fstat (fd_journal_device, &buf)) {
            n_journal_dev = buf.st_rdev;
        } else {
            fprintf(stderr, "rollback: specified journal device "
		    "cannot be stated\n");
        }
    }
    
    if (!fstat (fd_device, &buf)) {
        n_dev = buf.st_rdev;
    } else {
        fprintf(stderr, "rollback: specified device cannot "
		"be stated, exit\n");
        return;
    }
    
    rollback_data = misc_getmem (rollback_blocksize);
//    printf("\ncheckmem2");
//    fflush (stdout);
//    checkmem (rollback_data, misc_memsize((char *)rollback_data));
//   printf(" OK");
    
    fread (&rollback_blocks_number, 
	   sizeof (rollback_blocks_number), 
	   1, s_rollback_file);

    total = rollback_blocks_number;
    
    while (1) {
	if (!fsck_quiet(fs)) {
	    util_misc_progress (progress, &done, 
				rollback_blocks_number, 
				1, 0);
	}
	
        descriptor = 0;
        if ((retval = fread (&b_dev, sizeof (b_dev), 
			     1, s_rollback_file)) <= 0) 
	{
            if (retval) 
                fprintf (stderr, "rollback: fread: %s\n", strerror (errno));
            
	    break;            
        }
	
        if ((retval = fread (&offset, sizeof (offset), 
			     1, s_rollback_file)) <= 0) 
	{
            if (retval) 
                fprintf (stderr, "rollback: fread: %s\n", strerror (errno));
        
	    break;
        }

        if ((retval = fread (rollback_data, rollback_blocksize, 
			     1, s_rollback_file)) <= 0) 
	{
            if (retval) 
                fprintf (stderr, "rollback: fread: %s\n", strerror (errno));
            
	    break;
        }
                
        if (n_dev == b_dev)
            descriptor = fd_device;
        
	if ((n_journal_dev) && (n_journal_dev == b_dev))
            descriptor = fd_journal_device;
        
        if (descriptor == 0) {
            fprintf(stderr, "rollback: block from unknown "
		    "device, skip block\n");
            count_failed ++;
            continue;
        }
        
        if (lseek (descriptor, offset, SEEK_SET) == (off_t)-1) {
            fprintf(stderr, "device cannot be lseeked, skip block\n");
            count_failed ++;
            continue;
        }
        
        if (write (descriptor, rollback_data, rollback_blocksize) == -1) {
            fprintf (stderr, "rollback: write %d bytes returned error "
		"(block=%lld, dev=%d): %s\n", rollback_blocksize, 
		offset/rollback_blocksize, b_dev, strerror (errno));
            count_failed ++;
        } else {
            count_rollbacked ++;
              /*if you want to know what gets rollbacked, uncomment it*/
/*            if (log_file != 0 && log_file != stdout) 
                fprintf (log_file, "rollback: block %Lu of "
			 "device %Lu was restored\n",
                        (unsigned long long)offset/rollback_blocksize, b_dev);

	    
            fprintf (stdout, "rollback: block (%Ld) written\n", 
		     (long long int)offset/rollback_blocksize);
*/
        }
    }
    
    printf ("\n");
    
    if (log_file != 0) {
        fprintf (log_file, "rollback: (%u) blocks restored\n", 
		 count_rollbacked);
    }
}


/*
static void rollback__mark_block_saved (struct block_handler * rb_e) {
    if (rollback_blocks_array == NULL)
        rollback_blocks_array = 
		misc_getmem (ROLLBACK__INCREASE_BLOCK_NUMBER * sizeof (*rb_e));
    
    if (rollback_blocks_number == 
	misc_memsize ((void *)rollback_blocks_array) / sizeof (*rb_e))
    {
        rollback_blocks_array = 
		misc_expandmem (rollback_blocks_array, 
				misc_memsize((void *)rollback_blocks_array),
				ROLLBACK__INCREASE_BLOCK_NUMBER * 
				sizeof (*rb_e));
    }

//    checkmem ((char *)rollback_blocks_array, 
		misc_memsize((char *)rollback_blocks_array));

    rollback_blocks_array[rollback_blocks_number] = *rb_e;
    rollback_blocks_number ++;
    qsort (rollback_blocks_array, rollback_blocks_number, 
	   sizeof (*rb_e), rollback_compare);
    
//    printf("\ncheckmem3");
//    fflush (stdout);
//    checkmem ((char *)rollback_blocks_array, 
		misc_memsize((char *)rollback_blocks_array));
//    printf(" OK");
}
*/

