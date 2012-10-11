/*
 * Copyright 2000-2002 by Hans Reiser, licensing governed by reiserfs/README
 */

#include "debugreiserfs.h"


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


static void edit_journal_params (struct journal_params * jp)
{
    char * str;
    size_t n;
    int num;

    printf ("Journal parameters:\n");
    printf ("\tDevice: current: %x: new:", get_jp_journal_dev (jp));
    getline (&str, &n, stdin);
    if (str2int (str, &num))
	set_jp_journal_dev (jp, num);

    printf ("\tFirst block: current: %d: new:",
	    get_jp_journal_1st_block (jp));
    getline (&str, &n, stdin);
    if (str2int (str, &num))
	set_jp_journal_1st_block (jp, num);

    printf ("\tSize: current: %d: new:", get_jp_journal_size (jp));
    getline (&str, &n, stdin);
    if (str2int (str, &num))
	set_jp_journal_size (jp, num);

    printf ("\tMagic number: current: %d: new:", get_jp_journal_magic (jp));
    getline (&str, &n, stdin);
    if (str2int (str, &num))
	set_jp_journal_magic (jp, num);

    printf ("\tMax transaction size: current: %d: new:",
	    get_jp_journal_max_trans_len (jp));
    getline (&str, &n, stdin);
    if (str2int (str, &num))
	set_jp_journal_max_trans_len (jp, num);

    printf ("\tMax batch size: current: %d: new:",
	    get_jp_journal_max_batch (jp));
    getline (&str, &n, stdin);
    if (str2int (str, &num))
	set_jp_journal_max_batch (jp, num);

    printf ("\tMax commit age: current: %d: new:",
	    get_jp_journal_max_commit_age (jp));
    getline (&str, &n, stdin);
    if (str2int (str, &num))
	set_jp_journal_max_commit_age (jp, num);
}


/* this allows to edit all super block fields */
static void edit_super_block (reiserfs_filsys_t * fs)
{
    char * str;
    size_t n;
    int num;


    str = 0;
    n = 0;

    /* bs_block_count */
    printf ("\tBlock count: current: %u: new:",
	    get_sb_block_count (fs->fs_ondisk_sb));
    getline (&str, &n, stdin);
    if (str2int (str, &num))
	set_sb_block_count (fs->fs_ondisk_sb, num);

    
    /* sb_free_blocks */
    printf ("\tFree block count: current: %u: new:",
	    get_sb_free_blocks (fs->fs_ondisk_sb));
    getline (&str, &n, stdin);
    if (str2int (str, &num))
	set_sb_free_blocks (fs->fs_ondisk_sb, num);

    /* sb_root_block */
    printf ("\tRoot block: current: %u: new:",
	    get_sb_root_block (fs->fs_ondisk_sb));
    getline (&str, &n, stdin);
    if (str2int (str, &num))
	set_sb_root_block (fs->fs_ondisk_sb, num);

    /* sb_journal */
    edit_journal_params (sb_jp (fs->fs_ondisk_sb));
    
    /* sb_blocksize */
    printf ("\tBlocksize: current: %u: new:",
	    get_sb_block_size (fs->fs_ondisk_sb));
    getline (&str, &n, stdin);
    if (str2int (str, &num))
	set_sb_block_size (fs->fs_ondisk_sb, num);

    /* sb_oid_maxsize */
    printf ("\tMax objectid size: current: %u: new:",
	    get_sb_oid_maxsize (fs->fs_ondisk_sb));
    getline (&str, &n, stdin);
    if (str2int (str, &num))
	set_sb_oid_maxsize (fs->fs_ondisk_sb, num);

    /* sb_oid_cursize */
    printf ("\tCur objectid size: current: %u: new:",
	    get_sb_oid_cursize (fs->fs_ondisk_sb));
    getline (&str, &n, stdin);
    if (str2int (str, &num))
	set_sb_oid_cursize (fs->fs_ondisk_sb, num);

    /* sb_state */
    printf ("\tUmount state: current: %u: new:",
	    get_sb_umount_state (fs->fs_ondisk_sb));
    getline (&str, &n, stdin);
    if (str2int (str, &num))
	set_sb_umount_state (fs->fs_ondisk_sb, num);

    /* char s_magic [10]; */
    printf ("\tMagic: current: \"%s\": new:", fs->fs_ondisk_sb->s_v1.s_magic);
    getline (&str, &n, stdin);
    if (strcmp (str, "\n"))
	strncpy (fs->fs_ondisk_sb->s_v1.s_magic, str, n > 10 ? 10 : n);
    
    /* __u16 sb_fsck_state; */
    printf ("\tFielsystem state: current: %u: new:", get_sb_fs_state (fs->fs_ondisk_sb));
    getline (&str, &n, stdin);
    if (str2int (str, &num))
	set_sb_fs_state (fs->fs_ondisk_sb, num);

    /* __u32 sb_hash_function_code; */
    printf ("\tHash code: current: %u: new (tea %d, r5 %d, rupasov %d):",
	    get_sb_hash_code (fs->fs_ondisk_sb), TEA_HASH, R5_HASH, YURA_HASH);
    getline (&str, &n, stdin);
    if (str2int (str, &num))
	set_sb_hash_code (fs->fs_ondisk_sb, num);
    
    /* __u16 sb_tree_height; */
    printf ("\tTree height: current: %u: new:",
	    get_sb_tree_height (fs->fs_ondisk_sb));
    getline (&str, &n, stdin);
    if (str2int (str, &num))
	set_sb_tree_height (fs->fs_ondisk_sb, num);
    
    /* __u16 sb_bmap_nr; */
    printf ("\tNumber of bitmaps: current: %u: new:",
	    get_sb_bmap_nr (fs->fs_ondisk_sb));
    getline (&str, &n, stdin);
    if (str2int (str, &num))
	set_sb_bmap_nr (fs->fs_ondisk_sb, num);

    /* __u16 sb_version; */
    printf ("\tFilesystem format: current: %u: new:",
	    le16_to_cpu (fs->fs_ondisk_sb->s_v1.sb_version));
    getline (&str, &n, stdin);
    if (str2int (str, &num))
	set_sb_version (fs->fs_ondisk_sb, num);

    /* __u16 sb_reserved_for_journal; */
    printf ("\tSpace reserved for journal: current: %u: new:",
	    get_sb_reserved_for_journal (fs->fs_ondisk_sb));
    getline (&str, &n, stdin);
    if (str2int (str, &num))
	set_sb_reserved_for_journal (fs->fs_ondisk_sb, num);


    print_block (stdout, fs, fs->fs_super_bh);
    if (user_confirmed (stderr, "Is this ok ? [N/Yes]: ", "Yes\n")) {
	mark_buffer_dirty (fs->fs_super_bh);
	bwrite (fs->fs_super_bh);
    }
}


static void corrupt_clobber_hash (char * name, struct item_head * ih, 
				  struct reiserfs_de_head * deh)
{
    printf ("\tCorrupting deh_offset of entry \"%s\" of [%u %u]\n", name,
	    get_key_dirid (&ih->ih_key), get_key_objectid (&ih->ih_key));
    set_deh_offset (deh, 700);
}


/* this reads list of desired corruptions from stdin and perform the
   corruptions. Format of that list:
   A hash_code
   C name objectid     - 'C'ut entry 'name' from directory item with 'objectid'
   H name objectid     - clobber 'H'hash of entry 'name' of directory 'objectid'
   I item_num pos_in_item  make pos_in_item-th slot of indirect item to point out of device
   O item_num          - destroy item 'O'rder - make 'item_num'-th to have key bigger than 'item_num' + 1-th item
   D item_num          - 'D'elete item_num-th item
   S item_num value    - change file size (item_num-th item must be stat data)
   F item_num value    - change sd_first_direct_byte of stat data
   J item_num objectid
   E name objectid new - change entry's deh_objectid to new
   P                   - print the block
*/
void do_corrupt_one_block (reiserfs_filsys_t * fs)
{
    struct buffer_head * bh;
    int i, j;
    struct item_head * ih;
    int item_num;
    char * line = 0;
    size_t n = 0;
    char code, name [100];
    __u32 objectid, new_objectid;
    int value;
    int hash_code;
    int pos_in_item;
    unsigned long block;
    int type, format;


    block = certain_block (fs);
    if (block == fs->fs_super_bh->b_blocknr) {
	edit_super_block (fs);
	return;
    }

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
            if ( test_bit((block % (fs->fs_blocksize * 8)), bm_bh->b_data) )
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
	printf ("corrupt_one_block: bread fialed\n");
	return;
    }

    if (who_is_this (bh->b_data, fs->fs_blocksize) != THE_LEAF) {
	printf ("Can not corrupt not a leaf node\n");
	brelse (bh);
	return;
    }

    printf ("Corrupting block %lu..\n", bh->b_blocknr);

    while (getline (&line, &n, stdin) != -1) {
	switch (line[0]) {
	case '#':
	case '\n':
	    continue;
	case '?':
	    printf ("A hash_code     - reset hAsh code in super block\n"
		    "T item_num type (0, 1, 2, 3) format (0, 1)\n"
		    "C name objectid - Cut entry 'name' from directory item with 'objectid'\n"
		    "H name objectid - clobber Hash of entry 'name' of directory 'objectid'\n"
		    "I item_num pos_in_item  make pos_in_tem-th slot of Indirect item to point out of device\n"
		    "O item_num      - destroy item Order - make 'item_num'-th to have key bigger than 'item_num' + 1-th item\n"
		    "D item_num      - Delete item_num-th item\n"
		    "S item_num value - change file Size (item_num-th item must be stat data)\n"
		    "F item_num value - change sd_First_direct_byte of stat data\n"
		    "J item_num objectid - set 'obJectid' of 'item_num'-th item\n"
		    "E name objectid objectid - set deh_objectid of an entry to objectid\n");

	    continue;

	case 'P':
	    print_block (stderr, fs, bh, 3, -1, -1);
	    break;
	    
	case 'A':
	    /* corrupt hash record in super block */
	    if (sscanf (line, "%c %d\n", &code, &hash_code) != 2) {
		printf ("Wrong format \'%c\'\n", line [0]);
		continue;
	    }
	    break;
	    
	case 'C': /* cut entry */
	case 'H': /* make hash wrong */
	    if (sscanf (line, "%c %s %u\n", &code, name, &objectid) != 3) {
		printf ("Wrong format \'%c\'\n", line [0]);
		continue;
	    }
	    break;

	case 'T': /* set type of item */
	    if (sscanf (line, "%c %d %d %d\n", &code, &item_num, &type, &format) != 4) {
		printf ("Wrong format \'%c\'\n", line [0]);
		continue;
	    }
	    break;

	case 'J': /* set objectid : used to simulate objectid sharing problem */
	    if (sscanf (line, "%c %d %d\n", &code, &item_num, &objectid) != 3) {
		printf ("Wrong format \'%c\'\n", line [0]);
		continue;
	    }
	    break;

	case 'E': /* set objectid : used to simulate objectid sharing problem */
	    if (sscanf (line, "%c %s %u %d\n", &code, name, &objectid, &new_objectid) != 4) {
		printf ("Wrong format \'%c\'\n", line [0]);
		continue;
	    }
	    break;

	case 'I': /* break unformatted node pointer */
	    if (sscanf (line, "%c %d %d\n", &code, &item_num, &pos_in_item) != 3) {
		printf ("Wrong format \'%c\'\n", line [0]);
		continue;
	    }
	    break;
	    
	case 'D': /* delete item */
	case 'O': /* make item out of order */
	    if (sscanf (line, "%c %d\n", &code, &item_num) != 2) {
		printf ("Wrong format \'%c\'\n", line [0]);
		continue;
	    }
	    break;
	    
	case 'S': /* corrupt st_size */
	case 'F': /*         st_first_direct_byte */
	    if (sscanf (line, "%c %d %d\n", &code, &item_num, &value) != 3) {
		printf ("Wrong format \'%c\'\n", line [0]);
		continue;
	    }
	    break;
	}
	
	if (code == 'A') {
	    reiserfs_warning (stderr, "Changing %s to %s\n", code2name (get_sb_hash_code (fs->fs_ondisk_sb)),
			       code2name (hash_code));
	    set_sb_hash_code (fs->fs_ondisk_sb, hash_code);
	    mark_buffer_dirty (fs->fs_super_bh);
	    continue;
	}

	ih = B_N_PITEM_HEAD (bh, 0);
	for (i = 0; i < get_blkh_nr_items (B_BLK_HEAD (bh)); i ++, ih ++) {
	    struct reiserfs_de_head * deh;

	    if (code == 'T' && i == item_num) {
		set_ih_key_format (ih, format);
		set_type (format, &ih->ih_key, type);
		mark_buffer_dirty (bh);
		goto cont;
	    }

	    if (code == 'I' && i == item_num) {
		if (!is_indirect_ih (ih) || pos_in_item >= I_UNFM_NUM (ih)) {
		    reiserfs_warning (stderr, "Not an indirect item or there is "
				       "not so many unfm ptrs in it\n");
		    continue;
		}
		* ((__u32 *)B_I_PITEM (bh, ih) + pos_in_item) = get_sb_block_count (fs->fs_ondisk_sb) + 100;
		mark_buffer_dirty (bh);
		goto cont;
	    }

	    if (code == 'J' && i == item_num) {
		set_key_objectid (&ih->ih_key, objectid);
		mark_buffer_dirty (bh);
		goto cont;
	    }

	    if (code == 'S' && i == item_num) {
		/* fixme: old stat data only */
		struct stat_data_v1 * sd;

		sd = (struct stat_data_v1 *)B_I_PITEM (bh, ih); 
		reiserfs_warning (stderr, "Changing sd_size of %k from %d to %d\n",
				   &ih->ih_key, sd_v1_size(sd), value);
                set_sd_v1_size( sd, value );
		mark_buffer_dirty (bh);
		goto cont;		
	    }

	    if (code == 'F' && i == item_num) {
		/* fixme: old stat data only */
		struct stat_data_v1 * sd;

		sd = (struct stat_data_v1 *)B_I_PITEM (bh, ih); 
		reiserfs_warning (stderr, "Changing sd_first_direct_byte of %k from %d to %d\n",
				   &ih->ih_key, sd_v1_first_direct_byte(sd), value);		
		set_sd_v1_first_direct_byte( sd, value );
		mark_buffer_dirty (bh);
		goto cont;		
	    }

	    if (code == 'D' && i == item_num) {
		delete_item (fs, bh, item_num);
		mark_buffer_dirty (bh);
		goto cont;
	    }

	    if (code == 'O' && i == item_num) {
		/* destroy item order */
		struct key * key;
		if (i == get_blkh_nr_items (B_BLK_HEAD (bh)) - 1) {
		    printf ("can not destroy order\n");
		    continue;
		}
		key = &(ih + 1)->ih_key;
		set_key_dirid (&ih->ih_key, get_key_dirid (key) + 1);
		mark_buffer_dirty (bh);
	    }

	    if (get_key_objectid (&ih->ih_key) != objectid || !is_direntry_ih (ih))
		continue;

	    deh = B_I_DEH (bh, ih);

	    for (j = 0; j < get_ih_entry_count (ih); j ++, deh ++) {
		/* look for proper entry */
		if (name_in_entry_length (ih, deh, j) != strlen (name) ||
		    strncmp (name, name_in_entry (deh, j), strlen (name)))
		    continue;

		/* ok, required entry found, make a corruption */
		switch (code) {
		case 'C': /* cut entry */
		    cut_entry (fs, bh, i, j, 1);
		    mark_buffer_dirty (bh);

		    if (!B_IS_IN_TREE (bh)) {
			printf ("NOTE: block is deleted from the tree\n");
			exit (0);
		    }
		    goto cont;
		    break;

		case 'H': /* clobber hash */
		    corrupt_clobber_hash (name, ih, deh);
		    goto cont;
		    break;

		case 'E': /* change entry's deh_objectid */
		    set_deh_objectid (deh, new_objectid);
		    break;

		default:
		    printf ("Unknown command found\n");
		}
		mark_buffer_dirty (bh);
	    }
	}
    cont:
    }
    free (line);
    printf ("Done\n");
    brelse (bh);
    return;
}


