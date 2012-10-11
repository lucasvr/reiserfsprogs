/*
 * Copyright 2000-2004 by Hans Reiser, licensing governed by 
 * reiserfsprogs/README
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "debugreiserfs.h"
#include "misc/unaligned.h"

#include <errno.h>

#if 0

/* this reads stdin and recover file of given key:  */
/* the input has to be in the follwong format:
   K dirid objectid
   N name
   B blocknumber
   ..
   then recover_file will read every block, look there specified file and put it into
*/
void do_recover (reiserfs_filsys_t fs)
{
    char name [100];
    char * line = 0;
    int n = 0;
    int fd;
    reiserfs_key_t key = {0, 0, };
    reiserfs_bh_t * bh;
    reiserfs_ih_t * ih;
    unsigned long block;
    char code;
    long long int recovered = 0;
    int i, j;
    reiserfs_bitmap_t bitmap;
    int used, not_used;

    bitmap = reiserfs_bitmap_create (SB_BLOCK_COUNT (fs));
    reiserfs_fetch_disk_bitmap (bitmap, fs);
    /* we check how many blocks recoverd items point to are free or used */
    used = 0;
    not_used = 0;

    fd = 0;
    while (getline (&line, &n, stdin) != -1) {
	if (line [0] == '#' || line [0] == '\n')
	    continue;
	switch (line [0]) {
	case 'K':
	    /* get a key of file which is to be recovered */
	    if (sscanf (line, "%c %u %u\n", &code, &key.k_dir_id, &key.k_objectid) != 3) {
		misc_die ("recover_file: wrong input K format");
	    }
	    printf ("Recovering file (%u, %u)\n", key.k_dir_id, key.k_objectid);
	    break;

	case 'N':
	    /* get a file name */
	    recovered = 0;
	    if (sscanf (line, "%c %s\n", &code, name) != 2) {
		misc_die ("recover_file: wrong input N format");
	    }
	    fd = open (name, O_RDWR | O_CREAT | O_EXCL, 0644);
	    if (fd == -1)
		misc_die ("recover_file: could not create file %s: %s",
		     name,strerror (errno));
	    printf ("Recovering file %s..\n", name);
	    break;

	case 'B':
	    if (!fd)
		misc_die ("recover_file: file name is not specified");
	    if (sscanf (line, "%c %lu\n", &code, &block) != 2) {
		misc_die ("recover_file: wrong input B format");
	    }
	    bh = reiserfs_buffer_read (fs->s_dev, block, fs->s_blocksize);
	    if (!bh) {
		printf ("reading block %lu failed\n", block);
		continue;
	    }

	    printf ("working with block %lu..\n", block);

	    ih = reiserfs_ih_at (bh, 0);
	    for (i = 0; i < node_item_number (bh); i ++, ih ++) {
		__u32 * extent;
		reiserfs_bh_t * tmp_bh;

		if (!reiserfs_ih_ext (ih) || key.k_dir_id != ih->ih_key.k_dir_id ||
		    key.k_objectid != ih->ih_key.k_objectid)
		    continue;

		extent = (__u32 *)reiserfs_item_by_ih (bh, ih);
		for (j = 0; j < reiserfs_ext_count (ih); j ++) {
		    block = le32_to_cpu (extent [j]);
		    if (!block)
			continue;
		    tmp_bh = reiserfs_buffer_read (fs->s_dev, block, fs->s_blocksize);
		    if (!tmp_bh) {
			printf ("reading block %Lu failed\n", (long long int)block * fs->s_blocksize);
			continue;
		    }
		    if (lseek64 (fd, reiserfs_key_get_off (&ih->ih_key) + j * fs->s_blocksize - 1,
				 SEEK_SET) == (off_t)-1) {
			printf ("llseek failed to pos %Ld\n", (long long int)block * fs->s_blocksize);
			reiserfs_buffer_close (tmp_bh);
			continue;
		    }
		    if (reiserfs_bitmap_test_bit (bitmap, block))
			used ++;
		    else
			not_used ++;
		    /*printf ("block of file %Ld gets block %lu\n",
		      (reiserfs_key_get_off (&ih->ih_key) - 1) / fs->s_blocksize + j, block);*/
		    if (write (fd, tmp_bh->b_data, tmp_bh->b_size) != tmp_bh->b_size) {
			printf ("write failed to pos %Ld\n", (long long int)block * fs->s_blocksize);
			reiserfs_buffer_close (tmp_bh);
			continue;
		    }
		    recovered += fs->s_blocksize;
		    reiserfs_buffer_close (tmp_bh);
		}
	    }
	    reiserfs_buffer_close (bh);
	    break;
	}
    }
    printf ("recover_file: %Ld bytes recovered of file %s, key %u %u, %d blocks are free and %d are used\n",
	    recovered, name, key.k_dir_id, key.k_objectid, not_used, used);
}
#endif



/* read a file containing map of one or more files and either recover
   them or just print info */
/*
static void read_map (FILE * fp)
{
    int i;
    __u32 v32;
    char * buf;
    __u32 ids [4];
    int do_recover = 0;


    buf = 0;
    while (1) {
	if (fread (&v32, sizeof (v32), 1, fp) != 1)
	    break;
	if (v32 != MAP_MAGIC)
	    reiserfs_panic ("read_map: no magic found");

	// device name length and name itself 
	fread (&v32, sizeof (v32), 1, fp);
	buf = realloc (buf, v32);
	if (!buf)
	    reiserfs_panic ("realloc failed");
	fread (buf, v32, 1, fp);
	reiserfs_warning (stdout, "\"%s\": ", buf);

	// file name length and name itself
	fread (&v32, sizeof (v32), 1, fp);
	buf = realloc (buf, v32);
	if (!buf)
	    reiserfs_panic ("realloc failed");
	fread (buf, v32, 1, fp);

	// read directory key and poined object key 
	fread (ids, sizeof (ids), 1, fp);
	reiserfs_warning (stdout, "[%K]:\"%s\"-->[%K]\n",
			  &ids[0], buf, &ids[2]);

	
	//do_recover = util_user_confirmed (stdout, "recover? (Y):", "Y\n");
	//if (do_recover)
	//    reiserfs_warning (stderr, "recovering not ready\n");
	

	// how many data blocks are there 
	fread (&v32, sizeof (v32), 1, fp);
	if (v32) {
	    buf = realloc (buf, v32 * 4);
	    if (!buf)
		reiserfs_panic ("realloc failed (%u)", v32);
	    
	    // read list of data block numbers 
	    fread (buf, 4, v32, fp);
	    
	    if (!do_recover) {
		for (i = 0; i < v32; i ++)
		    reiserfs_warning (stdout, "%d ", ((__u32 *)buf)[i]);
		reiserfs_warning (stdout, "\n");
	    }
	}
	
	// main tail length 
	fread (&v32, sizeof (v32), 1, fp);
	if (v32) {
	    // there is tail 
	    buf = realloc (buf, v32);
	    if (!buf)
		reiserfs_panic ("realloc failed");
	    fread (buf, v32, 1, fp);
	    if (!do_recover)
		reiserfs_warning (stdout, "%d bytes long tail\n", v32);
	} else {
	    if (!do_recover)
		reiserfs_warning (stdout, "No tail\n");
	}

	if (fread (&v32, sizeof (v32), 1, fp) != 1)
	    break;
	if (v32 != MAP_END_MAGIC)
	    reiserfs_panic ("read_map: no magic found");
    }

    free (buf);
}


void do_recover (reiserfs_filsys_t * fs)
{
    FILE * fp;

    if (map_file (fs) [0] != '\0') {
	fp = fopen (map_file (fs), "r");
	if (fp == 0) {
	    reiserfs_warning (stderr, "do_recover: fopen failed: %m");
	    return;
	}
    } else {
	reiserfs_warning (stderr, "Reading file map from stdin..\n");
	fflush (stderr);
	fp = stdin;
    }

    read_map (fp);

    if (fp != stdin)
	fclose (fp);
    
}
*/

#include <limits.h>

static long int get_answer(long int max) {
    char answer[256], *tmp;    
    long int result = 0;

    do {
	printf("Which should be left?: ");
	fgets(answer, sizeof(answer), stdin);
	result = strtol (answer, &tmp, 0);
	if ((errno != ERANGE) && 
	    (result < max) && 
	    (result >= 0) &&
	    (answer != tmp))
	{
	    break;
	}
    } while (1);
    
    return result;
}

static void recover_items(FILE *fp, reiserfs_filsys_t * fs, FILE *target_file) {
    reiserfs_bh_t *bh, *bh_pointed;
    reiserfs_ih_t *ih;
    struct saved_item item, *cur;
    int size = sizeof(struct saved_item) - sizeof(struct saved_item *);
    struct saved_item *map = NULL;
    __u32 map_size = 0;
    int start = -1;
    unsigned int i, j;
    __u64 offset = 0, length;
    long int result = 0;
    unsigned long unfm_ptr;

    while (fread(&item, size, 1, fp) == 1) {
	map_size += sizeof(struct saved_item);
	map = realloc(map, map_size);
	memcpy((void *)map + map_size - sizeof(struct saved_item), &item, size);
    }
    
    for (i = 1, cur = map + 1; 
	 i <= map_size / sizeof(struct saved_item); 
	 i++, cur++) 
    {
	bh = reiserfs_buffer_read (fs->fs_dev, (cur - 1)->si_block, 
			     fs->fs_blocksize);
	if (!bh) {
	    reiserfs_warning (fp, "reiserfs_buffer_read failed\n");
	    continue;
	}

	if (i == map_size / sizeof(struct saved_item)) {
	    if (start != -1) {
		reiserfs_leaf_print(stdout, fs, bh, 0, 
				    (cur - 1)->si_item_num, 
				    (cur - 1)->si_item_num + 1);
		
		result = get_answer(i - start) + start;
	    } else {
		result = i - 1;
	    }
	    
	    start = -1;
	} else if (reiserfs_ih_dir(&(cur - 1)->si_ih) || 
		   reiserfs_ih_stat(&(cur - 1)->si_ih)) 
	{
	    reiserfs_buffer_close(bh);
	    continue;
	} else { 
	    length = reiserfs_leaf_ibytes(&(cur - 1)->si_ih, fs->fs_blocksize);
	    if (offset < reiserfs_key_get_off(&(cur - 1)->si_ih.ih_key) + 
		reiserfs_leaf_ibytes(&(cur - 1)->si_ih, fs->fs_blocksize))
		offset = reiserfs_key_get_off(&(cur - 1)->si_ih.ih_key) + 
			(length ? length - 1 : 0);
	
	    if (offset >= reiserfs_key_get_off(&cur->si_ih.ih_key)) {
		/* Problem interval */
		if (start == -1)
		    start = i - 1;
	    
		printf("Problem item %d:\n", i - start - 1);
		reiserfs_leaf_print(stdout, fs, bh, 0, 
				    (cur - 1)->si_item_num,
				    (cur - 1)->si_item_num + 1);
	    } else if (start != -1) {
		/* problem interval finished */
		printf("Problem item %d:\n", i - start - 1);
		reiserfs_leaf_print(stdout, fs, bh, 0, 
				    (cur - 1)->si_item_num,
				    (cur - 1)->si_item_num + 1);
	    
		result = get_answer((long int)i - start) + start;
		start = -1;
	    } else {
		result = i - 1;
	    }
	}
	reiserfs_buffer_close(bh);
	
	if (start != -1)
	    continue;

	bh = reiserfs_buffer_read (fs->fs_dev, (map + result)->si_block, 
			     fs->fs_blocksize);
	if (!bh) {
	    reiserfs_warning (fp, "reiserfs_buffer_read failed\n");
	    continue;
	}

	fseek(target_file, 
	      reiserfs_key_get_off(&(map + result)->si_ih.ih_key) - 1, 
	      SEEK_SET);
	
	ih = reiserfs_ih_at (bh, (map + result)->si_item_num);
	
	if (reiserfs_ih_direct(ih)) {
	    fwrite(reiserfs_item_by_ih(bh, ih), 
		   (map + result)->si_ih.ih2_item_len, 1, target_file);
	} else if (reiserfs_ih_ext(ih)) {
	    for (j = 0; j < reiserfs_ext_count (ih); j ++) {
		unfm_ptr = d32_get((__u32 *)reiserfs_item_by_ih(bh, ih), j);
		if (!unfm_ptr) {
		    fseek(target_file, fs->fs_blocksize, SEEK_CUR);
		    continue;
		}
		bh_pointed = reiserfs_buffer_read (fs->fs_dev, unfm_ptr, 
					     fs->fs_blocksize);
		if (!bh_pointed) {
		    reiserfs_warning (fp, "reiserfs_buffer_read failed\n");
		    continue;
		}
		fwrite(bh_pointed->b_data, 
		       fs->fs_blocksize, 1, target_file);
		
		reiserfs_buffer_close(bh_pointed);
	    }
	}
	
	reiserfs_buffer_close(bh);
    }

    free(map);
}

void do_recover(reiserfs_filsys_t * fs) {
    FILE *fp, *recovery;

    if (map_file (fs)[0] != '\0') {
	fp = fopen (map_file (fs), "r");
	if (fp == 0) {
	    reiserfs_warning (stderr, "fopen failed: %m\n");
	    return;
	}
    } else {
	reiserfs_warning (stderr, "Reading file map from stdin..\n");
	fflush (stderr);
	fp = stdin;
    }

    if (!(recovery = fopen(recovery_file(fs), "w+"))) {
	reiserfs_warning (stderr, "fopen failed: %m\n");
	return;
    }

    
    recover_items(fp, fs, recovery);
    
    if (fp != stdin)
	fclose (fp);
    
    fclose(recovery);
}
