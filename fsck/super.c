#include "fsck.h"
#include <stdlib.h>

#define fsck_conditional_log(sb_found, fmt, list...) {	\
    if (sb_found)					\
    	fsck_log(fmt, ## list);				\
}

int what_fs_version ()
{
    size_t n = 0;
    char * answer = 0;
    int version;
    printf("\nwhat is version of ReiserFS you use[1-4]\n"
        "\t(1)   3.6.x\n"
        "\t(2) >=3.5.9 (introduced in the middle of 1999) (if you use linux 2.2, choose this one)\n"
        "\t(3) < 3.5.9 converted to new format (don't choose if unsure)\n"
        "\t(4) < 3.5.9 (this is very old format, don't choose if unsure)\n"
        "\t(X)   exit\n");
    getline (&answer, &n, stdin);
    version = atoi (answer);
    if (version < 1 || version > 4)
        die ("rebuild_sb: wrong version");
    return version;
}

void rebuild_sb (reiserfs_filsys_t * fs, char * filename, struct fsck_data * data)
{
    int version = 0;
    struct reiserfs_super_block * ondisk_sb = 0;
    struct reiserfs_super_block * sb = 0;

    int magic_was_found = 0;
    unsigned long block_count = 0;
    __u16 p_oid_maxsize;
    __u16 p_bmap_nr;
    __u32 p_jp_journal_1st_block = 0;
    __u32 p_jp_dev_size = 0;
    int standard_journal = -1;
    char * journal_dev_name = 0;
    char * tmp;
    int sb_size;

    char * answer = 0;
    size_t n = 0;
    struct stat stat_buf;
    int retval;

    if (!no_reiserfs_found (fs)) {
        sb = getmem (sizeof (*sb));
        if (!is_opened_rw (fs)) {
            close (fs->fs_dev);
            fs->fs_dev = open (fs->fs_file_name, O_RDWR | O_LARGEFILE);
        }

	if (!is_blocksize_correct (fs->fs_blocksize)) {
	    printf("\nCannot find a proper blocksize, enter block size [4096]: \n");
	    getline (&answer, &n, stdin);
	    if (strcmp(answer, "\n")) {
		retval = (int) strtol (answer, &tmp, 0);
		if ((*tmp && strcmp(tmp, "\n")) || retval < 0)
		    reiserfs_exit (1, "rebuild_sb: wrong block size specified\n");
		if (!is_blocksize_correct (retval))
		    reiserfs_exit (1, "rebuild_sb: wrong block size specified, only divisible by 1024 are supported currently\n");
	    } else
		retval = 4096;
	
	    fs->fs_blocksize = retval;
	}

        block_count = count_blocks (filename, fs->fs_blocksize);

        /* save ondisk_sb somewhere and work in temp area */
        ondisk_sb = fs->fs_ondisk_sb;
        memcpy (sb, fs->fs_ondisk_sb, sizeof (*sb));
        fs->fs_ondisk_sb = sb;

        if (is_reiserfs_3_6_magic_string (sb)) {
            /* 3_6 magic */
            if (fsck_data (fs)->journal_dev_name)
                /* journal dev must not be specified with standard journal */
                reiserfs_exit (1, "Reiserfs with standard journal found, but there was specified a journal dev");

            if (get_jp_journal_1st_block(sb_jp(sb)) == get_journal_new_start_must (fs))
                version = 1;
            else if (get_jp_journal_1st_block(sb_jp(sb)) == get_journal_old_start_must (fs))
                version = 3;
            magic_was_found = 2;
        } else if (is_reiserfs_3_5_magic_string (sb)) {
            if (fsck_data (fs)->journal_dev_name)
                /* journal dev must not be specified with standard journal */
                reiserfs_exit (1, "Reiserfs with standard journal found, but there was specified a journal dev");

            /* 3_5 magic */
            if (get_jp_journal_1st_block(sb_jp(sb)) == get_journal_new_start_must (fs))
                version = 2;
            else if (get_jp_journal_1st_block(sb_jp(sb)) == get_journal_old_start_must (fs))
                version = 4;
            magic_was_found = 1;
        } else if (is_reiserfs_jr_magic_string (sb)) {
            if (!fsck_data (fs)->journal_dev_name)
                /* journal dev must be specified with non standard journal */
                reiserfs_exit (1, "Reiserfs with non standard journal found, but there was not specified any journal dev");

            if (get_sb_version (sb) == REISERFS_FORMAT_3_6) {
                /*non-standard magic + sb_format == 3_6*/
                if (get_jp_journal_1st_block(sb_jp(sb)) == get_journal_new_start_must (fs))
                    version = 1;
                else if (get_jp_journal_1st_block(sb_jp(sb)) == get_journal_old_start_must (fs))
                    version = 3;
                magic_was_found = 3;
            } else if (get_sb_version (sb) == REISERFS_FORMAT_3_5) {
                /* non-standard magic + sb_format == 3_5 */
                if (get_jp_journal_1st_block(sb_jp(sb)) == get_journal_new_start_must (fs))
                    version = 2;
                else if (get_jp_journal_1st_block(sb_jp(sb)) == get_journal_old_start_must (fs))
                    version = 4;
                magic_was_found = 3;
            } else {
                /* non-standard magic + bad sb_format */
                version = 0;
                magic_was_found = 3;
            }
        } else
            reiserfs_exit (1, "we opened device but there is no magic and there is no correct superbblock format found");

        if (magic_was_found == 1 || magic_was_found == 2)
            standard_journal = 1;
        else
            standard_journal = 0;

        if (version == 0)
            version = what_fs_version ();

        if (get_sb_block_count (sb) != block_count) {
            do {
                printf("\nDid you use resiser(y/n)[n]: ");
                getline (&answer, &n, stdin);
            } while (strcmp ("y\n", answer) && strcmp ("n\n", answer) && strcmp ("\n", answer));
            if (!strcmp ("y\n", answer)) {
                printf("\nEnter partition size [%lu]: ", block_count);
                getline (&answer, &n, stdin);
                if (strcmp ("\n", answer))
                    block_count = atoi (answer);
                set_sb_block_count (sb, block_count);
            } else {
                fsck_conditional_log (magic_was_found, "rebuild-sb: wrong block count occured (%lu), fixed (%lu)\n",
                     get_sb_block_count(sb), block_count);
                set_sb_block_count (sb, block_count);
            }
        }

        if (get_sb_block_size (sb) != fs->fs_blocksize) {
            fsck_log("rebuild-sb: wrong block size occured (%lu), fixed (%lu)\n", get_sb_block_size (sb), fs->fs_blocksize);
  	    set_sb_block_size (sb, fs->fs_blocksize);
        }
    }

    /* if no reiserfs_found or bad data found in that SB, what was checked in previous clause */
    if (no_reiserfs_found (fs)) {
        int fd;

        fd = open (filename, O_RDWR | O_LARGEFILE);

        if (fd == -1)
            reiserfs_exit (1, "rebuils_sb: cannot open device %s", filename);

        version = what_fs_version ();

        if (version == 3 || version == 4) {
            retval = 4096;
        } else {
	    printf("\nEnter block size [4096]: \n");
	    getline (&answer, &n, stdin);
	    if (strcmp(answer, "\n")) {
		retval = (int) strtol (answer, &tmp, 0);
		if ((*tmp && strcmp(tmp, "\n")) || retval < 0)
		    reiserfs_exit (1, "rebuild_sb: wrong block size specified\n");
		if (!is_blocksize_correct (retval))
		    reiserfs_exit (1, "rebuild_sb: wrong block size specified, only divisible by 1024 are supported currently\n");
	    } else
		retval = 4096;
	}

        block_count = count_blocks (filename, retval);

        switch(version){
        case 1:
            fs = reiserfs_create (filename, REISERFS_FORMAT_3_6, block_count, retval, 1, 1);
            break;
        case 2:
            fs = reiserfs_create (filename, REISERFS_FORMAT_3_5, block_count, retval, 1, 1);
            break;
        case 3:
            fs = reiserfs_create (filename, REISERFS_FORMAT_3_6, block_count, retval, 1, 0);
            break;
        case 4:
            fs = reiserfs_create (filename, REISERFS_FORMAT_3_5, block_count, retval, 1, 0);
            break;
        }

        sb = fs->fs_ondisk_sb;
        fs->fs_vp = data;

        if (!fsck_skip_journal (fs)) {
            if (!fsck_data (fs)->journal_dev_name) {
                do {
                    printf("\nNo journal device was specified. (If journal is not available, re-run with --no-journal-available).\n"\
                            "Is journal standard? (y/n)[y]: ");
                    getline (&answer, &n, stdin);
                } while (strcmp ("y\n", answer) && strcmp ("n\n", answer) && strcmp ("\n", answer));
                if (!strcmp ("n\n", answer)) {
                    printf("\nSpecify journal device with -j option. Use \n");
                    exit(1);
                }
                standard_journal = 1;
            } else {
                standard_journal = 0;
                memcpy (fs->fs_ondisk_sb->s_v1.s_magic, REISERFS_JR_SUPER_MAGIC_STRING,
                    strlen (REISERFS_JR_SUPER_MAGIC_STRING));
            }
        }

        do {
            printf("\nDid you use resiser(y/n)[n]: ");
            getline (&answer, &n, stdin);
        } while (strcmp ("y\n", answer) && strcmp ("n\n", answer) && strcmp ("\n", answer));
        if (!strcmp ("y\n", answer)) {
            printf("\nEnter partition size [%lu]: ", block_count);
            getline (&answer, &n, stdin);
            if (strcmp ("\n", answer))
                block_count = atoi (answer);
            set_sb_block_count (sb, block_count);
        }
	
	set_sb_fs_state (sb, REISERFS_CORRUPTED);
    }


    if (version == 1 || version == 3) {
        if (get_reiserfs_format (sb) != REISERFS_FORMAT_3_6) {
            fsck_log("rebuild-sb: wrong reiserfs version occured (%lu), fixed (%lu)\n", get_reiserfs_format (sb), REISERFS_FORMAT_3_6);
            set_sb_version (sb, REISERFS_FORMAT_3_6);
        }
    } else if (version == 2 || version == 4) {
        if (get_reiserfs_format (sb) != REISERFS_FORMAT_3_5) {
            fsck_log("rebuild-sb: wrong reiserfs version occured (%lu), fixed (%lu)\n", get_reiserfs_format (sb), REISERFS_FORMAT_3_5);
            set_sb_version (sb, REISERFS_FORMAT_3_5);
        }
    }

    p_oid_maxsize = (fs->fs_blocksize - reiserfs_super_block_size (sb)) / sizeof(__u32) / 2 * 2;
    if (get_sb_oid_maxsize (sb) != p_oid_maxsize) {
	fsck_log("rebuild-sb: wrong objectid map max size occured (%lu), fixed (%lu)\n", get_sb_oid_maxsize (sb), p_oid_maxsize);
	set_sb_oid_maxsize (sb, p_oid_maxsize);
    }

    p_bmap_nr = (block_count + (fs->fs_blocksize * 8 - 1)) / (fs->fs_blocksize * 8);
    if (get_sb_bmap_nr (sb) != p_bmap_nr) {
	fsck_log("rebuild-sb: wrong bitmap number occured (%lu), fixed (%lu)\n", get_sb_bmap_nr (sb), p_bmap_nr);
	set_sb_bmap_nr (sb, (block_count + (fs->fs_blocksize * 8 - 1)) / (fs->fs_blocksize * 8));
    }

    if (get_sb_root_block (sb) > block_count) {
	fsck_log("rebuild-sb: wrong root block occured (%lu), zeroed\n", get_sb_root_block (sb));
	set_sb_root_block (sb, 0);
    }

    if (get_sb_free_blocks (sb) > block_count) {
	fsck_log ("rebuild-sb: wrong free block count occured (%lu), zeroed\n", get_sb_free_blocks (sb));
        set_sb_free_blocks (sb, 0);
    }

    if (get_sb_umount_state (sb) != REISERFS_CLEANLY_UMOUNTED && get_sb_umount_state (sb) != REISERFS_NOT_CLEANLY_UMOUNTED) {
	fsck_conditional_log (magic_was_found, "rebuild-sb: wrong umount state, fixed to (REISERFS_NOT_CLEANLY_UMOUNTED)\n",
                  get_sb_umount_state (sb));
        set_sb_umount_state (sb, REISERFS_NOT_CLEANLY_UMOUNTED);
    }

    if (get_sb_oid_cursize (sb) == 1 || get_sb_oid_cursize (sb) > get_sb_oid_maxsize (sb)) {
        fsck_log("rebuild-sb: wrong objectid map occured (%lu), zeroed\n", get_sb_oid_cursize (sb));
        set_sb_oid_cursize (sb, 0);
    }

    if ( get_sb_tree_height (sb) &&
        ((get_sb_tree_height (sb) < DISK_LEAF_NODE_LEVEL + 1) ||
	(get_sb_tree_height (sb) > MAX_HEIGHT)) ) {
	fsck_log("rebuild-sb: wrong tree height occured, zeroed\n", get_sb_tree_height (sb));
	set_sb_tree_height (sb, 0);
    }

    if (get_sb_hash_code (sb) && code2name (get_sb_hash_code (sb)) == 0) {
	fsck_log("rebuild-sb: wrong hash occured (%lu), zeroed\n", get_sb_hash_code (sb));
	set_sb_hash_code (sb, 0);
    }

    if (version == 1 || version == 3) {
        if (!uuid_is_correct(sb->s_uuid)) {
	    if (generate_random_uuid (sb->s_uuid)) {
		fsck_log ("rebuild-sb: no uuid found, failed to genetate UUID\n");
	    } else {
		fsck_log ("rebuild-sb: no uuid found, a new uuid generated (%U)\n", sb->s_uuid);
	    }
        }
	if (sb->s_flags != 0 && sb->s_flags != 1) {
	    fsck_log ("rebuild-sb: super block flags found, zeroed\n",
                  sb->s_flags);
	    sb->s_flags = 0;
	}
    }


    /*
    if we have a standard journal
        reserved = 0
        dev - same
        size = journal_default_size(fs->fs_super_bh->b_blocknr, fs)
        offset = journal_default_size(fs)
    if we have a non standard journal
        if we found magic string
            try to find a jhead and comare dev, size, offset there
            if params are not equal move to "if we did not find a magic string" clause
        if we did not find a magic string
            ask user about his journal
            try to find a jhead and comare dev, size, offset there
            if params are not equal exit with error
    */


    p_jp_journal_1st_block = get_journal_start_must (fs);

    if (!fsck_skip_journal (fs) && standard_journal == 1) {
        if (get_jp_journal_dev (sb_jp(sb)) != 0) {
 	    fsck_conditional_log (magic_was_found, "rebuild-sb: wrong journal device occured (%lu), fixed (0)\n",
 	                get_jp_journal_dev (sb_jp(sb)));
 	    set_jp_journal_dev (sb_jp(sb), 0);
        }
        if (get_sb_reserved_for_journal (sb) != 0) {
 	    fsck_log ("rebuild-sb: wrong size reserved for standard journal occured (%lu), fixed (0)\n",
 	                get_sb_reserved_for_journal (sb));
            set_sb_reserved_for_journal (sb, 0);
        }
        if (get_jp_journal_1st_block (sb_jp(sb)) != p_jp_journal_1st_block) {
            fsck_conditional_log (magic_was_found, "rebuild-sb: wrong journal first block occured (%lu), fixed (%lu)\n",
                get_jp_journal_1st_block (sb_jp(sb)), p_jp_journal_1st_block);
            set_jp_journal_1st_block (sb_jp(sb) , p_jp_journal_1st_block);
        }
        if (get_jp_journal_size (sb_jp(sb)) != journal_default_size(fs->fs_super_bh->b_blocknr, fs->fs_blocksize)) {
 	    fsck_conditional_log (magic_was_found, "rebuild-sb: wrong journal size occured (%lu), fixed (%lu)\n",
 	                get_jp_journal_size (sb_jp(sb)) + 1,
 	                journal_default_size (fs->fs_super_bh->b_blocknr, fs->fs_blocksize) + 1);
            set_jp_journal_size (sb_jp(sb), journal_default_size (fs->fs_super_bh->b_blocknr, fs->fs_blocksize));
        }
        if (get_jp_journal_max_trans_len (sb_jp(sb)) !=
	    advise_journal_max_trans_len(	get_jp_journal_max_trans_len (sb_jp(sb)),
	    					get_jp_journal_size (sb_jp(sb)),
	    					fs->fs_blocksize))
	{
 	    fsck_conditional_log (magic_was_found, "rebuild-sb: wrong journal max transaction length occured (%lu), fixed (%d)\n",
 	        get_jp_journal_max_trans_len (sb_jp(sb)),
        	advise_journal_max_trans_len (	get_jp_journal_max_trans_len (sb_jp(sb)),
        					get_jp_journal_size (sb_jp(sb)),
        					fs->fs_blocksize));
 	    set_jp_journal_max_trans_len (sb_jp(sb),
 	    	advise_journal_max_trans_len(	get_jp_journal_max_trans_len (sb_jp(sb)),
 	    					get_jp_journal_size (sb_jp(sb)),
 	    					fs->fs_blocksize));
        }
/*        if (get_jp_journal_magic (sb_jp(sb)) != 0) {
 	    fsck_log ("rebuild-sb: wrong journal magic occured (%lu), fixed (0)\n", get_jp_journal_magic (sb_jp(sb)));
 	    set_jp_journal_magic (sb_jp(sb), 0);
        }*/
        if (get_jp_journal_max_batch (sb_jp(sb)) != advise_journal_max_batch(get_jp_journal_max_trans_len (sb_jp(sb)))) {
 	    fsck_conditional_log (magic_was_found, "rebuild-sb: wrong journal max batch size occured (%lu), fixed (%d)\n",
 	        get_jp_journal_max_batch (sb_jp(sb)), advise_journal_max_batch(get_jp_journal_max_trans_len (sb_jp(sb))));
 	    set_jp_journal_max_batch (sb_jp(sb), advise_journal_max_batch(get_jp_journal_max_trans_len (sb_jp(sb))));
        }
        if (get_jp_journal_max_commit_age (sb_jp(sb)) != advise_journal_max_commit_age()) {
 	    fsck_conditional_log (magic_was_found, "rebuild-sb: wrong journal  max commit age occured (%lu), fixed (%d)\n",
 	        get_jp_journal_max_commit_age (sb_jp(sb)), advise_journal_max_commit_age());
 	    set_jp_journal_max_commit_age (sb_jp(sb), advise_journal_max_commit_age());
        }
        if (get_jp_journal_max_trans_age (sb_jp(sb)) != advise_journal_max_trans_age()) {
 	    fsck_log ("rebuild-sb: wrong journal  max commit age occured (%lu), fixed (0)\n",
 	        get_jp_journal_max_trans_age (sb_jp(sb)), advise_journal_max_trans_age());
 	    set_jp_journal_max_trans_age (sb_jp(sb), advise_journal_max_trans_age());
        }



    } else if (!fsck_skip_journal(fs) && standard_journal == 0) {
        /* non standard journal */
        struct reiserfs_journal_header * jh;

        journal_dev_name = fsck_data (fs)->journal_dev_name;
        retval = stat(journal_dev_name, &stat_buf);
        if (retval == -1)
            reiserfs_exit (1, "rebuild_sb: wrong journal device specified\n");
        if (strcmp (fs->fs_file_name, journal_dev_name))
            set_jp_journal_dev (sb_jp(sb), stat_buf.st_rdev);
        else
            set_jp_journal_dev (sb_jp(sb), 0);

/*
        if (journal_dev_name == 0) {
            // ask user about the dev
            printf("\nEnter dev with journal[%s]: \n", fs->fs_file_name);
            getline (&answer, &n, stdin);
            if (strcmp(answer, "\n")) {
		journal_dev_name = malloc (strlen(answer));
                strncpy(journal_dev_name, answer, strlen(answer) - 1);
                retval = stat(journal_dev_name, &stat_buf);
                if (retval == -1)
                    die ("rebuild_sb: wrong device specified\n");
                if (strcmp (fs->fs_file_name, journal_dev_name))
                    set_jp_journal_dev (sb_jp(sb), stat_buf.st_rdev);
                else
                    set_jp_journal_dev (sb_jp(sb), 0);
            } else {
                    set_jp_journal_dev (sb_jp(sb), 0);
		    journal_dev_name = fs->fs_file_name;
            }
        }
*/
        retval = -1;
        if (magic_was_found == 0 || (retval = reiserfs_open_journal (fs, journal_dev_name, O_RDONLY)) == 0) {
            __u64 default_value;
            /* journal header was not found or journal cannot be opened -> adjust journal size and offset */

            /* default offset if magic was not found is 0 for relocated journal and get_journal_start_must
               for any journal on the same device;
               default offset if magic was found is found value */
            if (retval == 0)
                fsck_log ("Journal cannot be opened, assuming specified journal device is correct\n");

            if (magic_was_found == 0)
                default_value = (!strcmp(fs->fs_file_name, journal_dev_name)) ? p_jp_journal_1st_block : 0;
            else
                default_value = get_jp_journal_1st_block (sb_jp(sb));

            printf("\nEnter journal offset on %s in blocks [%Lu]: \n",
            		journal_dev_name, (unsigned long long)default_value);

            getline (&answer, &n, stdin);
            if (strcmp(answer, "\n")) {
                retval = (int) strtol (answer, &tmp, 0);
                if ((*tmp && strcmp(tmp, "\n")) || retval < 0)
                    reiserfs_exit (1, "rebuild_sb: wrong offset specified\n");
                set_jp_journal_1st_block (sb_jp(sb), retval);
            } else
                set_jp_journal_1st_block (sb_jp(sb), default_value);

            p_jp_dev_size = count_blocks (journal_dev_name, fs->fs_blocksize);
            /* some checks for journal offset */
            if (strcmp(fs->fs_file_name, journal_dev_name) != 0) {
                if (p_jp_dev_size < get_jp_journal_1st_block (sb_jp(sb)) + 1)
        	    reiserfs_exit (1, "rebuild_sb: offset is much then device size\n");
            }

            /* default size if magic was not found is device size - journal_1st_block;
               default size if magic was found is found value + 1 block for journal header */
            if (magic_was_found == 0)
                default_value = (!strcmp(fs->fs_file_name, journal_dev_name)) ?
                                journal_default_size (fs->fs_super_bh->b_blocknr, fs->fs_blocksize) + 1 :
                                p_jp_dev_size - get_jp_journal_1st_block (sb_jp(sb));
            else
                default_value = get_jp_journal_size (sb_jp(sb)) + 1;
		

            printf("\nEnter journal size (including 1 block for journal header) on %s in blocks [%Lu]: \n",
                        journal_dev_name, (unsigned long long)default_value);

            getline (&answer, &n, stdin);
            if (strcmp(answer, "\n")) {
                retval = (int) strtol (answer, &tmp, 0);
                if ((*tmp && strcmp(tmp, "\n")) || retval < 0)
        	    reiserfs_exit (1, "rebuild_sb: wrong offset specified\n");
                set_jp_journal_size (sb_jp(sb), retval - 1);
            } else {
                set_jp_journal_size (sb_jp(sb), default_value - 1);
            }

            /* some checks for journal size */
            if (get_jp_journal_size (sb_jp(sb)) +
                get_jp_journal_1st_block (sb_jp(sb)) + 1 > p_jp_dev_size)
        	    reiserfs_exit (1, "rebuild_sb: journal offset + journal size is much then device size\n");

            if (reiserfs_open_journal (fs, journal_dev_name, O_RDONLY) == 0)
                reiserfs_exit (1, "rebuild-sb: journal header is not found, wrong dev/offset/size configuration\n");
        }

	jh = (struct reiserfs_journal_header *)fs->fs_jh_bh->b_data;
        memcpy (sb_jp(sb), &jh->jh_journal, sizeof (struct journal_params));
    } else {
        fsck_log ("Journal was specified as not available, zeroing all journal fields in super block..");
        set_jp_journal_dev (sb_jp(sb), 0);
        set_sb_reserved_for_journal (sb, 0);
        set_jp_journal_1st_block (sb_jp(sb) , 0);
        set_jp_journal_size (sb_jp(sb), 0);
        set_jp_journal_max_trans_len (sb_jp(sb), 0);
        set_jp_journal_max_batch (sb_jp(sb), 0);
        set_jp_journal_max_commit_age (sb_jp(sb), 0);
        set_jp_journal_max_commit_age (sb_jp(sb), 0);
    }

    /*  whether journal header contains params with the same dev, offset, size will be 
	checked in open_journal */

    if (version == 1 || version == 3)
        sb_size = SB_SIZE;
    else
        sb_size = SB_SIZE_V1;

    if (!magic_was_found || memcmp(ondisk_sb, sb, sb_size - 
		((sb_size == SB_SIZE) ? sizeof(fs->fs_ondisk_sb->s_unused) : 0)) ) 
    {
        /* smth was changed in SB or a new one has been built */
	set_sb_fs_state (sb, get_sb_fs_state (sb) || REISERFS_CORRUPTED);
	
	if (ondisk_sb) {
	    /* if super_block was found, we keep sb in ondisk_sb */
	    fs->fs_ondisk_sb = ondisk_sb;
	    memcpy (ondisk_sb, sb, sb_size);
	    freemem(sb);
	}

	print_block (stderr, fs, fs->fs_super_bh);
	
	if (user_confirmed (stderr, "Is this ok ? (y/n)[n]: ", "y\n")) {
	    mark_buffer_uptodate (fs->fs_super_bh, 1);
	    mark_buffer_dirty (fs->fs_super_bh);
	    bwrite (fs->fs_super_bh);
	    fsck_progress ("\nDo not forget to run reiserfsck --rebuild-tree\n\n");
	} else {
	    mark_buffer_clean (fs->fs_super_bh);
	    fsck_progress ("Super block was not written\n");
	}
    } else {
	print_block (stderr, fs, fs->fs_super_bh);
	
	mark_buffer_clean (fs->fs_super_bh);
	fsck_progress ("\nSuper block seems to be correct\n\n");
    }
}

/*	if (version == 0) {
	    brelse (fs->fs_super_bh);
            freemem (fs);
            close (fs->fs_dev);
            fs = NULL;
	}
*/
