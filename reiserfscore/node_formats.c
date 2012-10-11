/*
 *  Copyright 2000, 2001, 2002 by Hans Reiser, licensing governed by
 *  reiserfs/README
 */


#include "includes.h"

int correct_direct_item_offset (__u64 offset, int format) {
    if (format == KEY_FORMAT_2) {
        return (offset && ((offset - 1) % 8 == 0));
    } else {
	return (offset);
    }
    return 0;
}

/* this only checks block header and item head array (ih_location-s
   and ih_item_len-s). Item internals are not checked */
static int does_node_look_like_a_leaf (char * buf, int blocksize)
{
    struct block_head * blkh;
    struct item_head * ih;
    int used_space;
    int prev_location;
    int i;
    int nr;

    blkh = (struct block_head *)buf;
    if (!is_leaf_block_head (buf))
	return 0;

    nr = get_blkh_nr_items (blkh);
    if (nr < 0 || nr > ((blocksize - BLKH_SIZE) / (IH_SIZE + MIN_ITEM_LEN)))
	/* item number is too big or too small */
	return 0;

    ih = (struct item_head *)(buf + BLKH_SIZE) + nr - 1;
    used_space = BLKH_SIZE + IH_SIZE * nr + (blocksize - (nr ? get_ih_location (ih) : blocksize));
    if (used_space != blocksize - get_blkh_free_space (blkh))
	/* free space does not match to calculated amount of use space */
	return 0;

    // FIXME: it is_leaf will hit performance too much - we may have
    // return 1 here

    /* check tables of item heads */
    ih = (struct item_head *)(buf + BLKH_SIZE);
    prev_location = blocksize;
    for (i = 0; i < nr; i ++, ih ++) {
	/* items of zero length are allowed - they may exist for short time
           during balancing */
	if (get_ih_location (ih) > blocksize || get_ih_location (ih) < IH_SIZE * nr)
	    return 0;
	if (/*ih_item_len (ih) < 1 ||*/ get_ih_item_len (ih) > MAX_ITEM_LEN (blocksize))
	    return 0;
	if (prev_location - get_ih_location (ih) != get_ih_item_len (ih))
	    return 0;
	prev_location = get_ih_location (ih);
    }

    // one may imagine much more checks
    return 1;
}


/* check ih_item_len and ih_location. Should be useful when block head is
   corrupted */
int does_node_have_ih_array (char * buf, int blocksize)
{
    struct item_head * ih;
    int prev_location;
    int nr;

    /* look at the table of item head */
    prev_location = blocksize;
    ih = (struct item_head *)(buf + BLKH_SIZE);
    nr = 0;
    while (1) {
	if (get_ih_location (ih) + get_ih_item_len (ih) != prev_location)
	    break;

	prev_location = get_ih_location (ih);
	ih ++;
	nr ++;
    }
    if (nr < 2)
	return 0;
    return nr;
}


/* returns 1 if buf looks like an internal node, 0 otherwise */
static int is_correct_internal (char * buf, int blocksize)
{
    struct block_head * blkh;
    int nr;
    int used_space;

    blkh = (struct block_head *)buf;

    if (!is_internal_block_head (buf))
	return 0;
    
    nr = get_blkh_nr_items (blkh);
    if (nr > (blocksize - BLKH_SIZE - DC_SIZE) / (KEY_SIZE + DC_SIZE))
	/* for internal which is not root we might check min number of keys */
	return 0;

    used_space = BLKH_SIZE + KEY_SIZE * nr + DC_SIZE * (nr + 1);
    if (used_space != blocksize - get_blkh_free_space (blkh))
	return 0;

    // one may imagine much more checks
    return 1;
}


// make sure that bh contains formatted node of reiserfs tree of
// 'level'-th level
int is_tree_node (struct buffer_head * bh, int level)
{
    if (B_LEVEL (bh) != level)
	return 0;
    if (is_leaf_node (bh))
	return does_node_look_like_a_leaf (bh->b_data, bh->b_size);

    return is_correct_internal (bh->b_data, bh->b_size);
}


static int is_desc_block (char * buf, unsigned long buf_size)
{
    if (!memcmp(buf + buf_size - 12, JOURNAL_DESC_MAGIC, 8) &&
	//get_jdesc_len (desc) > 0)
	le32_to_cpu(*((__u32*)(buf + 4))) > 0)
	return 1;
    return 0;
}


int is_reiserfs_3_5_magic_string (struct reiserfs_super_block * rs)
{
    return (!strncmp (rs->s_v1.s_magic, REISERFS_3_5_SUPER_MAGIC_STRING, 
		      strlen ( REISERFS_3_5_SUPER_MAGIC_STRING)));
}


int is_reiserfs_3_6_magic_string (struct reiserfs_super_block * rs)
{
    return (!strncmp (rs->s_v1.s_magic, REISERFS_3_6_SUPER_MAGIC_STRING, 
		      strlen ( REISERFS_3_6_SUPER_MAGIC_STRING)));
}


int is_reiserfs_jr_magic_string (struct reiserfs_super_block * rs)
{
    return (!strncmp (rs->s_v1.s_magic, REISERFS_JR_SUPER_MAGIC_STRING, 
		      strlen ( REISERFS_JR_SUPER_MAGIC_STRING)));
}


int is_any_reiserfs_magic_string (struct reiserfs_super_block * rs)
{
    if (is_reiserfs_3_5_magic_string (rs) ||
	is_reiserfs_3_6_magic_string (rs) ||
	is_reiserfs_jr_magic_string (rs))
	return 1;
    return 0;
}


int get_reiserfs_format (struct reiserfs_super_block * sb)
{
    /* after conversion to 3.6 format we change magic correctly, 
       but do not change sb_format. When we create non-standard journal 
       field format in sb get adjusted correctly. Thereby, for standard 
       journal we should rely on magic and for non-standard - on format */
    if (is_reiserfs_3_5_magic_string (sb) ||
	(is_reiserfs_jr_magic_string (sb) && 
	 get_sb_version (sb) == REISERFS_FORMAT_3_5))
	return REISERFS_FORMAT_3_5;

    if (is_reiserfs_3_6_magic_string (sb) ||
	(is_reiserfs_jr_magic_string (sb) &&
	 get_sb_version (sb) == REISERFS_FORMAT_3_6))
	return REISERFS_FORMAT_3_6;

    return REISERFS_FORMAT_UNKNOWN;
}



int reiserfs_super_block_size (struct reiserfs_super_block * sb)
{
    switch (get_reiserfs_format (sb)) {
    case REISERFS_FORMAT_3_5:
	return SB_SIZE_V1;
    case REISERFS_FORMAT_3_6:
	return SB_SIZE;
    }
    reiserfs_panic ("Unknown format found");
    return 0;
}



#if 0
/* returns code of             0 -  here means 3.5 format,
                               2 - either pure 3.6 or converted 3.5
                               3 - journal-relocated 3.6*/
int magic_2_version (struct reiserfs_super_block * rs)
{
    int ret;

    ret = 0;
    if (is_reiser2fs_magic_string (rs)) 
	ret = REISERFS_VERSION_2 ;
    else if (is_reiser2fs_jr_magic_string (rs))
	ret = REISERFS_VERSION_3;
    else if (is_reiserfs_magic_string (rs))
	ret = REISERFS_VERSION_1;
    else
	reiserfs_panic ("magic_2_version: unknown magic string found");
    return ret;
}
#endif


/* this one had signature in different place of the super_block
   structure */
int is_prejournaled_reiserfs (struct reiserfs_super_block * rs)
{
    return (!strncmp((char*)rs + REISERFS_SUPER_MAGIC_STRING_OFFSET_NJ,
		     REISERFS_3_5_SUPER_MAGIC_STRING,
		     strlen(REISERFS_3_5_SUPER_MAGIC_STRING)));
}

int does_look_like_super_block (struct reiserfs_super_block * sb) {
    if (!is_any_reiserfs_magic_string (sb))
    	return 0;
    
    if (!is_blocksize_correct(get_sb_block_size (sb)))
	return 0;
	
    return 1;
}


/* returns code of reiserfs metadata block (leaf, internal, super
   block, journal descriptor), unformatted */
int who_is_this (char * buf, int blocksize)
{
    /* super block? */
    if (does_look_like_super_block ((void *)buf))
	return THE_SUPER;

    if (does_node_look_like_a_leaf (buf, blocksize))
	/* block head and item head array seem matching (node level, free
           space, item number, item locations and length) */
	return THE_LEAF;

    if (does_node_have_ih_array (buf, blocksize)) {
	/* item header array found */
	return HAS_IH_ARRAY;
    }

    if (is_correct_internal (buf, blocksize))
	return THE_INTERNAL;


    /* journal descriptor block? */
    if (is_desc_block (buf, blocksize))
	return THE_JDESC;

    /* contents of buf does not look like reiserfs metadata. Bitmaps
       are possible here */
    return THE_UNKNOWN;
}


char * which_block (int code)
{
    static char * leaf = "leaf";
    static char * broken_leaf = "leaf w/o block head";
    static char * internal = "internal";
    static char * other = "unknown";

    switch (code) {
    case THE_LEAF:
	return leaf;
    case HAS_IH_ARRAY:
	return broken_leaf;
    case THE_INTERNAL:
	return internal;
    }
    return other;
}


/** */
int block_of_journal (reiserfs_filsys_t * fs, unsigned long block)
{
    if (!is_reiserfs_jr_magic_string (fs->fs_ondisk_sb)) {
	/* standard journal */
	if (block >= get_journal_start_must (fs) &&
	    block <= get_journal_start_must (fs) + get_jp_journal_size (sb_jp (fs->fs_ondisk_sb)))
	    return 1;
	return 0;
    }
    
    if (get_sb_reserved_for_journal (fs->fs_ondisk_sb))
	/* there is space reserved for the journal on the host device */
	if (block >= get_journal_start_must (fs) && 
	    block < get_journal_start_must (fs) + get_sb_reserved_for_journal (fs->fs_ondisk_sb))
	    return 1;

    return 0;
}


int block_of_bitmap (reiserfs_filsys_t * fs, unsigned long block)
{
    if (spread_bitmaps (fs)) {
	if (!(block % (fs->fs_blocksize * 8)))
	    /* bitmap block */
	    return 1;
	return (block == (REISERFS_DISK_OFFSET_IN_BYTES / fs->fs_blocksize + 1)) ;
    } else {
	/* bitmap in */
	if (block > 2 && block < 3 + get_sb_bmap_nr (fs->fs_ondisk_sb))
	    return 1;
	return 0;
    }
    return 0;
}


/* check whether 'block' can be pointed to by an indirect item */
int not_data_block (reiserfs_filsys_t * fs, unsigned long block)
{
    if (block_of_bitmap (fs, block))
	/* it is one of bitmap blocks */
	return 1;

    if (block > 32768)
	return 0;

    if (block_of_journal (fs, block))
	/* block of journal area */
	return 1;

    if (block <= fs->fs_super_bh->b_blocknr)
	/* either super block or a block from skipped area at the
           beginning of filesystem */
	return 1;

    return 0;
}


/* check whether 'block' can be logged */
int not_journalable (reiserfs_filsys_t * fs, unsigned long block)
{   
    /* we should not update SB with journal copy during fsck */
    if (block < fs->fs_super_bh->b_blocknr)
	return 1;

    if (block_of_journal (fs, block))
	return 1;

    if (block >= get_sb_block_count (fs->fs_ondisk_sb))
	return 1;

    return 0;
}

// in reiserfs version 0 (undistributed bitmap)
// FIXME: what if number of bitmaps is 15?
int get_journal_old_start_must (reiserfs_filsys_t * fs)
{
    return (REISERFS_OLD_DISK_OFFSET_IN_BYTES / fs->fs_blocksize) + 1 + get_sb_bmap_nr (fs->fs_ondisk_sb);
}

int get_journal_new_start_must (reiserfs_filsys_t * fs)
{
    return (REISERFS_DISK_OFFSET_IN_BYTES / fs->fs_blocksize) + 2;
}

int get_journal_start_must (reiserfs_filsys_t * fs) {
//    if (fs->fs_super_bh->b_size == 4096 && fs->fs_super_bh->b_blocknr == 2)
    if (is_old_sb_location (fs->fs_super_bh->b_blocknr, fs->fs_blocksize))
    	return get_journal_old_start_must (fs);

    return get_journal_new_start_must (fs);
}

__u64 get_bytes_number (struct item_head * ih, int blocksize)
{
    switch (get_type (&ih->ih_key)) {
    case TYPE_DIRECT:
	return get_ih_item_len (ih);
    case TYPE_INDIRECT:
	return I_UNFM_NUM(ih) * blocksize;// - get_ih_free_space (ih);
    case TYPE_STAT_DATA:
    case TYPE_DIRENTRY:
	return 0;
    }
    reiserfs_warning (stderr, "get_bytes_number: called for wrong type of item %h", ih);
    return 0;
}


int check_item_f (reiserfs_filsys_t * fs, struct item_head * ih, char * item);

#if 0
/* look at the 16 byte and try to find: does it look like a reiserfs
   key. Returns 0 if something looks broken in the key, 1 otherwise */
static int does_key_look_correct (reiserfs_filsys_t * fs, struct key * key)
{
    int type;

    if (key->k_dir_id == key->k_objectid ||
	!key->k_dir_id || key->k_objectid < 2)
	/* k_dir_id and k_objectid must be different, and > 0 */
	return 0;

    if (key->u.k_offset_v1.k_offset == 0 &&
	key->u.k_offset_v1.k_uniqueness == 0)
	/* looks like a stat data */
	return 1;

    if (!key->u.k_offset_v1.k_offset ||
	!key->u.k_offset_v1.k_uniqueness)
	/* these should be either both 0 or both !0 */
	return 0;

    type = key->u.k_offset_v2.k_type;

    if (key->u.k_offset_v1.k_uniqueness == 500)
	/* looks like a key of directory entry */
	return (type == 0) ? 1 : 0;

    if (type == 15) {
	/* looks like old key of */
	if (key->u.k_offset_v1.k_uniqueness == 0xffffffff) {
	    /* direct item. Check offset */
	    if (key->u.k_offset_v1.k_offset >= fs->s_blocksize * 4 + 1)
		/* fixme: this could be more accurate */
		return 0;
	    return 1;
	}

	if (key->u.k_offset_v1.k_uniqueness == 0xfffffffe) {
	    /* indirect item. Check offset */
	    if (key->u.k_offset_v1.k_offset % fs->s_blocksize != 1)
		return 0;
	    return 1;
	}

	return 0;
    }

    /* there should be a new key of indirect or direct item */

    if (type == TYPE_DIRECT) {
	/* check offset */
	if (key->u.k_offset_v2.k_offset >= fs->s_blocksize * 4 + 1) /* fixme*/
	    /* fixme: this could be more accurate */
	    return 0;

	if ((key->u.k_offset_v2.k_offset - 1) % 8 != 0)
	    /* "new" direct items get split on the boundary of 8 byte */
	    return 0;
	
	return 1;
    }

    if (type == TYPE_INDIRECT) {
	/* check offset */
	if ((key->u.k_offset_v2.k_offset % fs->s_blocksize) != 1)
	    return 0;
	return 1;
    }

    return 0;
}
#endif
/* ih_key, ih_location and ih_item_len seem correct, check other fields */
static int does_ih_look_correct (struct item_head * ih)
{
    int ih_key_format;
    int key_key_format;

    /* key format from item_head */
    ih_key_format = get_ih_key_format (ih);
    if (ih_key_format != KEY_FORMAT_1 && ih_key_format != KEY_FORMAT_2)
	return 0;

    /* key format calculated on key */
    key_key_format = key_format (&ih->ih_key);
    if (is_stat_data_ih (ih)) {
	/* for stat data we can not find key format from a key itself, so look at
           the item length */
	if (get_ih_item_len (ih) == SD_SIZE)
	    key_key_format = KEY_FORMAT_2;
	else if (get_ih_item_len (ih) == SD_V1_SIZE)
	    key_key_format = KEY_FORMAT_1;
	else
	    return 0;
    }
    if (key_key_format != ih_key_format)
	return 0;

    /* we do not check ih_format.fsck_need as fsck might change it. So,
       debugreiserfs -p will have to dump it */
    return 1;
}

/* check item length, ih_free_space for pure 3.5 format, unformatted node
   pointers */
static int is_bad_indirect (reiserfs_filsys_t * fs, struct item_head * ih, char * item,
			    check_unfm_func_t check_unfm_func)
{
    int i;
    __u32 * ind = (__u32 *)item;

    if (get_ih_item_len (ih) % UNFM_P_SIZE)
	return 1;

    for (i = 0; i < I_UNFM_NUM (ih); i ++) {
	if (!ind [i])
	    continue;
	if (check_unfm_func && check_unfm_func (fs, le32_to_cpu(ind [i])))
	    return 1;
    }

    if (fs->fs_format == REISERFS_FORMAT_3_5) {
	/* check ih_free_space for 3.5 format only */
	if (get_ih_free_space (ih) > fs->fs_blocksize - 1)
	    return 1;
    }
    
    return 0;
}


static const struct {
    hashf_t func;
    char * name;
} hashes[] = {{0, "not set"},
	      {keyed_hash, "\"tea\""},
	      {yura_hash, "\"rupasov\""},
	      {r5_hash, "\"r5\""}};        

#define HASH_AMOUNT (sizeof (hashes) / sizeof (hashes [0]))



int known_hashes (void)
{
    return HASH_AMOUNT;
}


#define good_name(hashfn,name,namelen,deh_offset) \
(GET_HASH_VALUE ((hashfn) (name, namelen)) == GET_HASH_VALUE (deh_offset))


/* this also sets hash function */
int is_properly_hashed (reiserfs_filsys_t * fs,
			char * name, int namelen, __u32 offset)
{
    int i;

    if (namelen == 1 && name[0] == '.') {
	if (offset == DOT_OFFSET)
	    return 1;
	return 0;
    }

    if (namelen == 2 && name[0] == '.' && name[1] == '.') {
	if (offset == DOT_DOT_OFFSET)
	    return 1;
	return 0;
    }

    if (hash_func_is_unknown (fs)) {
	/* try to find what hash function the name is sorted with */
	for (i = 1; i < HASH_AMOUNT; i ++) {
	    if (good_name (hashes [i].func, name, namelen, offset)) {
		if (!hash_func_is_unknown (fs)) {
		    /* two or more hash functions give the same value for this
                       name */
		    fprintf (stderr, "Detecting hash code: could not detect hash with name \"%.*s\"\n",
			     namelen, name);
		    reiserfs_hash (fs) = 0;
		    return 1;
		}

		/* set hash function */
 		reiserfs_hash(fs) = hashes [i].func;
 	    }
 	}

        if (hash_func_is_unknown (fs)) {
            return 0;
        }
    }

    if (good_name (reiserfs_hash(fs), name, namelen, offset))
	return 1;
#if 0
    fprintf (stderr, "is_properly_hashed: namelen %d, name \"%s\", offset %u, hash %u\n",
	    namelen, name_from_entry (name, namelen), GET_HASH_VALUE (offset),
	    GET_HASH_VALUE (reiserfs_hash(fs) (name, namelen)));

    /* we could also check whether more than one hash function match on the
       name */
    for (i = 1; i < sizeof (hashes) / sizeof (hashes [0]); i ++) {
	if (i == g_real_hash)
	    continue;
	if (good_name (hashes[i], name, namelen, deh_offset)) {
	    die ("bad_hash: at least two hashes got screwed up with this name: \"%s\"",
		 bad_name (name, namelen));
	}
    }
#endif
    return 0;
}


int find_hash_in_use (char * name, int namelen, __u32 hash_value_masked, int code_to_try_first)
{
    int i;


    if (!namelen || !name[0])
	return UNSET_HASH;

    if (code_to_try_first) {
	if (hash_value_masked == GET_HASH_VALUE (hashes [code_to_try_first].func (name, namelen)))
	    return code_to_try_first;
    }
    for (i = 1; i < HASH_AMOUNT; i ++) {
	if (i == code_to_try_first)
	    continue;
	if (hash_value_masked == GET_HASH_VALUE (hashes [i].func (name, namelen)))
	    return i;
    }

    /* not matching hash found */
    return UNSET_HASH;
}


char * code2name (int code)
{
    if (code >= HASH_AMOUNT || code < 0)
        return 0;
    return hashes [code].name;
}


int func2code (hashf_t func)
{
    int i;
    
    for (i = 0; i < HASH_AMOUNT; i ++)
	if (func == hashes [i].func)
	    return i;

    reiserfs_panic ("func2code: no hashes matches this function\n");
    return 0;
}


hashf_t code2func (int code)
{
    if (code >= HASH_AMOUNT) {
	reiserfs_warning (stderr, "code2func: wrong hash code %d.\n"
			  "Using default %s hash function\n", code,
			  code2name (DEFAULT_HASH));
	code = DEFAULT_HASH;
    }
    return hashes [code].func;
}


hashf_t name2func (char * hash)
{
    int i;
 
    for (i = 0; i < HASH_AMOUNT; i ++)
	if (!strcmp (hash, hashes [i].name))
	    return hashes [i].func;
    return 0;
}


int dir_entry_bad_location (struct reiserfs_de_head * deh, struct item_head * ih, int first)
{
    if (get_deh_location (deh) < DEH_SIZE * get_ih_entry_count (ih))
	return 1;
    
    if (get_deh_location (deh) >= get_ih_item_len (ih))
	return 1;

    if (!first && get_deh_location (deh) >= get_deh_location (deh - 1))
	return 1;

    return 0;
}


/* the only corruption which is not considered fatal - is hash mismatching. If
   bad_dir is set - directory item having such names is considered bad */
static int is_bad_directory (reiserfs_filsys_t * fs, struct item_head * ih, char * item,
			     int bad_dir)
{
    int i;
    int namelen;
    struct reiserfs_de_head * deh = (struct reiserfs_de_head *)item;
    __u32 prev_offset = 0;
    __u16 prev_location = get_ih_item_len (ih);
    
    for (i = 0; i < get_ih_entry_count (ih); i ++, deh ++) {
	if (get_deh_location (deh) >= prev_location)
	    return 1;
	prev_location = get_deh_location (deh);
	    
	namelen = name_in_entry_length (ih, deh, i);
	if (namelen > REISERFS_MAX_NAME_LEN (fs->fs_blocksize)) {
	    return 1;
	}
	if (get_deh_offset (deh) <= prev_offset)
	    return 1;
	prev_offset = get_deh_offset (deh);
	
	/* check hash value */
	if (!is_properly_hashed (fs, item + prev_location, namelen, prev_offset)) {
	    if (bad_dir)
		/* make is_bad_leaf to not insert whole leaf. Node will be
		   marked not-insertable and put into tree item by item in
		   pass 2 */
		return 1;
	}
    }

    return 0;
}


/* used by debugreisrefs -p only yet */
int is_it_bad_item (reiserfs_filsys_t * fs, struct item_head * ih, char * item,
		    check_unfm_func_t check_unfm, int bad_dir)
{
    int retval;

/*
    if (!does_key_look_correct (fs, &ih->ih_key))
	return 1;

    if (!does_ih_look_correct (ih))
	return 1;
*/
    if (!does_ih_look_correct (ih))
	return 1;

    
    if (is_stat_data_ih (ih) || is_direct_ih (ih))
	return 0;

    if (is_direntry_ih (ih)) {
	retval =  is_bad_directory (fs, ih, item, bad_dir);
	/*
	if (retval)
	reiserfs_warning (stderr, "is_bad_directory %H\n", ih);*/
	return retval;
    }
    if (is_indirect_ih (ih)) {
	retval = is_bad_indirect (fs, ih, item, check_unfm);
	/*
	if (retval)
	reiserfs_warning (stderr, "is_bad_indirect %H\n", ih);*/
	return retval;
    }

    return 1;
}


/* prepare new or old stat data for the new directory */
void make_dir_stat_data (int blocksize, int key_format, 
			 __u32 dirid, __u32 objectid, 
			 struct item_head * ih, void * sd)
{
    memset (ih, 0, IH_SIZE);
    set_key_dirid (&ih->ih_key, dirid);
    set_key_objectid (&ih->ih_key, objectid);
    set_key_offset_v1 (&ih->ih_key, SD_OFFSET);
    set_key_uniqueness (&ih->ih_key, 0);

    set_ih_key_format (ih, key_format);
    set_ih_free_space (ih, MAX_US_INT);


    if (key_format == KEY_FORMAT_2) {
        struct stat_data *sd_v2 = (struct stat_data *)sd;

	set_ih_item_len (ih, SD_SIZE);
        set_sd_v2_mode (sd_v2, S_IFDIR + 0755);
        set_sd_v2_nlink (sd_v2, 2);
        set_sd_v2_uid (sd_v2, 0);
        set_sd_v2_gid (sd_v2, 0);
        set_sd_v2_size (sd_v2, EMPTY_DIR_SIZE);
        set_sd_v2_atime (sd_v2, time(NULL));
        sd_v2->sd_ctime = sd_v2->sd_mtime = sd_v2->sd_atime; /* all le */
        set_sd_v2_rdev (sd_v2, 0);
        set_sd_v2_blocks (sd_v2, dir_size2st_blocks (EMPTY_DIR_SIZE));
    }else{
        struct stat_data_v1 *sd_v1 = (struct stat_data_v1 *)sd;

	set_ih_item_len (ih, SD_V1_SIZE);
        set_sd_v1_mode(sd_v1, S_IFDIR + 0755);
        set_sd_v1_nlink(sd_v1, 2);
        set_sd_v1_uid (sd_v1, 0);
        set_sd_v1_gid (sd_v1, 0);
        set_sd_v1_size(sd_v1, EMPTY_DIR_SIZE_V1);
        set_sd_v1_atime(sd_v1, time(NULL));
        sd_v1->sd_ctime = sd_v1->sd_mtime = sd_v1->sd_atime; /* all le */
        set_sd_v1_blocks(sd_v1, dir_size2st_blocks(EMPTY_DIR_SIZE_V1));
        set_sd_v1_first_direct_byte(sd_v1, NO_BYTES_IN_DIRECT_ITEM);
    }
}


static void _empty_dir_item (int format, char * body, __u32 dirid, __u32 objid,
			     __u32 par_dirid, __u32 par_objid)
{
    struct reiserfs_de_head * deh;
    __u16 state;

    memset (body, 0, (format == KEY_FORMAT_2 ? EMPTY_DIR_SIZE : EMPTY_DIR_SIZE_V1));
    deh = (struct reiserfs_de_head *)body;
    
    /* direntry header of "." */
    set_deh_offset (deh, DOT_OFFSET);
    set_deh_dirid (deh, dirid);
    set_deh_objectid (deh, objid);
    state = (1 << DEH_Visible2);
    set_deh_state (deh, state);

    /* direntry header of ".." */
    set_deh_offset (deh + 1, DOT_DOT_OFFSET);
    /* key of ".." for the root directory */
    set_deh_dirid (deh + 1, par_dirid);
    set_deh_objectid (deh + 1, par_objid);
    set_deh_state (deh + 1, state);

    if (format == KEY_FORMAT_2) {
	set_deh_location (deh, EMPTY_DIR_SIZE - ROUND_UP (strlen (".")));
	set_deh_location (deh + 1, get_deh_location (deh) - ROUND_UP (strlen ("..")));
    } else {
	set_deh_location (deh, EMPTY_DIR_SIZE_V1 - strlen ("."));
	set_deh_location (deh + 1, get_deh_location (deh) - strlen (".."));
    }

    /* copy ".." and "." */
    memcpy (body + get_deh_location (deh), ".", 1);
    memcpy (body + get_deh_location (deh + 1), "..", 2);
    
}


void make_empty_dir_item_v1 (char * body, __u32 dirid, __u32 objid,
			     __u32 par_dirid, __u32 par_objid)
{
    _empty_dir_item (KEY_FORMAT_1, body, dirid, objid, par_dirid, par_objid);
}


void make_empty_dir_item (char * body, __u32 dirid, __u32 objid,
			  __u32 par_dirid, __u32 par_objid)
{
    _empty_dir_item (KEY_FORMAT_2, body, dirid, objid, par_dirid, par_objid);
}



/* for every item call common action and an action corresponding to
   item type */
void for_every_item (struct buffer_head * bh, item_head_action_t action,
		     item_action_t * actions)
{
    int i;
    struct item_head * ih;
    item_action_t iaction;

    ih = B_N_PITEM_HEAD (bh, 0);
    for (i = 0; i < get_blkh_nr_items (B_BLK_HEAD (bh)); i ++, ih ++) {
	if (action)
	    action (ih);

	iaction = actions[get_type (&ih->ih_key)];
	if (iaction)
	    iaction (bh, ih);
    }
}

#if __BYTE_ORDER == __LITTLE_ENDIAN
# define get_key_offset_v2(key)     (__u64)((key->u.k2_offset_v2.k_offset))
# define set_key_offset_v2(key,val) (void)(key->u.k2_offset_v2.k_offset = (val))
# define get_key_type_v2(key)       (__u16)((key->u.k2_offset_v2.k_type))
# define set_key_type_v2(key,val)   (void)(key->u.k2_offset_v2.k_type = (val))
#elif __BYTE_ORDER == __BIG_ENDIAN
typedef union {
    struct offset_v2 offset_v2;
    __u64 linear;
} __attribute__ ((__packed__)) offset_v2_esafe_overlay;

static inline __u64 get_key_offset_v2 (const struct key *key)
{
    offset_v2_esafe_overlay tmp =
                        *(offset_v2_esafe_overlay *) (&(key->u.k2_offset_v2));
    tmp.linear = le64_to_cpu( tmp.linear );
    return tmp.offset_v2.k_offset;
}

static inline __u32 get_key_type_v2 (const struct key *key)
{
    offset_v2_esafe_overlay tmp =
                        *(offset_v2_esafe_overlay *) (&(key->u.k2_offset_v2));
    tmp.linear = le64_to_cpu( tmp.linear );
    return tmp.offset_v2.k_type;
}

static inline void set_key_offset_v2 (struct key *key, __u64 offset)
{
    offset_v2_esafe_overlay *tmp =
                        (offset_v2_esafe_overlay *)(&(key->u.k2_offset_v2));
    tmp->linear = le64_to_cpu(tmp->linear);
    tmp->offset_v2.k_offset = offset;
    tmp->linear = cpu_to_le64(tmp->linear);
}

static inline void set_key_type_v2 (struct key *key, __u32 type)
{
    offset_v2_esafe_overlay *tmp =
                        (offset_v2_esafe_overlay *)(&(key->u.k2_offset_v2));
    if (type > 15)
        reiserfs_panic ("set_key_type_v2: type is too big %d", type);

    tmp->linear = le64_to_cpu(tmp->linear);
    tmp->offset_v2.k_type = type;
    tmp->linear = cpu_to_le64(tmp->linear);
}
#else
# error "nuxi/pdp-endian archs are not supported"
#endif

static inline int is_key_format_1 (int type) {
    return ( (type == 0 || type == 15) ? 1 : 0);
}

/* old keys (on i386) have k_offset_v2.k_type == 15 (direct and
   indirect) or == 0 (dir items and stat data) */

/* */
int key_format (const struct key * key)
{
    int type;

    type = get_key_type_v2 (key);

    if (is_key_format_1 (type))
	return KEY_FORMAT_1;

    return KEY_FORMAT_2;
}


loff_t get_offset (const struct key * key)
{
    if (key_format (key) == KEY_FORMAT_1)
	return get_key_offset_v1 (key);

    return get_key_offset_v2 (key);
}


int uniqueness2type (__u32 uniqueness)
{
    switch (uniqueness) {
    case V1_SD_UNIQUENESS: return TYPE_STAT_DATA;
    case V1_INDIRECT_UNIQUENESS: return TYPE_INDIRECT;
    case V1_DIRECT_UNIQUENESS: return TYPE_DIRECT;
    case V1_DIRENTRY_UNIQUENESS: return TYPE_DIRENTRY;
    }
    return TYPE_UNKNOWN;
}


__u32 type2uniqueness (int type)
{
    switch (type) {
    case TYPE_STAT_DATA: return V1_SD_UNIQUENESS;
    case TYPE_INDIRECT: return V1_INDIRECT_UNIQUENESS;
    case TYPE_DIRECT: return V1_DIRECT_UNIQUENESS;
    case TYPE_DIRENTRY: return V1_DIRENTRY_UNIQUENESS;
    } 
    return V1_UNKNOWN_UNIQUENESS;
}


int get_type (const struct key * key)
{
    int type_v2 = get_key_type_v2 (key);

    if (is_key_format_1 (type_v2))
	return uniqueness2type (get_key_uniqueness (key));

    return type_v2;
}


char * key_of_what (const struct key * key)
{
    switch (get_type (key)) {
    case TYPE_STAT_DATA: return "SD";
    case TYPE_INDIRECT: return "IND";
    case TYPE_DIRECT: return "DRCT";
    case TYPE_DIRENTRY: return "DIR";
    default: return "???";
    }
}


int type_unknown (struct key * key)
{
    int type = get_type (key);
    
    switch (type) {
    case TYPE_STAT_DATA:
    case TYPE_INDIRECT:
    case TYPE_DIRECT:
    case TYPE_DIRENTRY:
	return 0;
    default:
	break;
    }
    return 1;
}


// this sets key format as well as type of item key belongs to
//
void set_type (int format, struct key * key, int type)
{
    if (format == KEY_FORMAT_1)
	set_key_uniqueness (key, type2uniqueness (type));
    else
	set_key_type_v2 (key, type);
}


// 
void set_offset (int format, struct key * key, loff_t offset)
{
    if (format == KEY_FORMAT_1)
	set_key_offset_v1 (key, offset);
    else
	set_key_offset_v2 (key, offset);
	
}


void set_type_and_offset (int format, struct key * key, loff_t offset, int type)
{
    set_type (format, key, type);
    set_offset (format, key, offset);
}


/* length of the directory entry in directory item. This define calculates
   length of i-th directory entry using directory entry locations from dir
   entry head. When it calculates length of 0-th directory entry, it uses
   length of whole item in place of entry location of the non-existent
   following entry in the calculation.  See picture above.*/


// NOTE: this is not name length. This is length of whole entry
int entry_length (struct item_head * ih, struct reiserfs_de_head * deh, int pos_in_item)
{
    if (pos_in_item)
	return (get_deh_location (deh - 1) - get_deh_location (deh));
    return (get_ih_item_len (ih) - get_deh_location (deh));
}


char * name_in_entry (struct reiserfs_de_head * deh, int pos_in_item)
{
    return ((char *)(deh - pos_in_item) + get_deh_location(deh));
}

int name_in_entry_length (struct item_head * ih,
		 struct reiserfs_de_head * deh, int pos_in_item)
{
    int len;
    char * name;

    len = entry_length (ih, deh, pos_in_item);
    name = name_in_entry (deh, pos_in_item);

    // name might be padded with 0s
    while (!name [len - 1] && len > 0)
	len --;

    return len;
}

int name_length (char * name, int key_format)
{
    if (key_format == KEY_FORMAT_2)
    	return ROUND_UP (strlen(name));
    else if (key_format == KEY_FORMAT_1)
    	return strlen(name);

    return -1;
}

/* key format is stored in 12 bits starting from 0-th of item_head's ih2_format*/
__u16 get_ih_key_format (const struct item_head * ih)
{
    get_bit_field_XX (16, &ih->ih_format, 0, 12);
}


__u16 get_ih_flags (const struct item_head * ih)
{
    get_bit_field_XX (16, &ih->ih_format, 12, 4);
}


void set_ih_key_format (struct item_head * ih, __u16 val)
{
    set_bit_field_XX (16, &ih->ih_format, val, 0, 12);
}


void set_ih_flags (struct item_head * ih, __u16 val)
{
    set_bit_field_XX (16, &ih->ih_format, val, 12, 4);
}



/* access to fields of stat data (both v1 and v2) */

void get_set_sd_field (int field, struct item_head * ih, void * sd,
		       void * value, int set)
{
    if (get_ih_key_format (ih) == KEY_FORMAT_1) {
	struct stat_data_v1 * sd_v1 = sd;

	switch (field) {
	case GET_SD_MODE:
	    if (set)
		sd_v1->sd_mode = cpu_to_le16 (*(__u16 *)value);
	    else
		*(__u16 *)value = le16_to_cpu (sd_v1->sd_mode);
	    break;

	case GET_SD_SIZE:
	    /* value must point to 64 bit int */
	    if (set)
		sd_v1->sd_size = cpu_to_le32 (*(__u64 *)value);
	    else
		*(__u64 *)value = le32_to_cpu (sd_v1->sd_size);
	    break;

	case GET_SD_BLOCKS:
	    if (set)
		sd_v1->u.sd_blocks = cpu_to_le32 (*(__u32 *)value);
	    else
		*(__u32 *)value = le32_to_cpu (sd_v1->u.sd_blocks);
	    break;

	case GET_SD_NLINK:
	    /* value must point to 32 bit int */
	    if (set)
		sd_v1->sd_nlink = cpu_to_le16 (*(__u32 *)value);
	    else
		*(__u32 *)value = le16_to_cpu (sd_v1->sd_nlink);
	    break;

	case GET_SD_FIRST_DIRECT_BYTE:
	    if (set)
		sd_v1->sd_first_direct_byte = cpu_to_le32 (*(__u32 *)value);
	    else
		*(__u32 *)value = le32_to_cpu (sd_v1->sd_first_direct_byte);
	    break;
	    
	default:
	    reiserfs_panic ("get_set_sd_field: unknown field of old stat data");
	}
    } else {
	struct stat_data * sd_v2 = sd;

	switch (field) {
	case GET_SD_MODE:
	    if (set)
		sd_v2->sd_mode = cpu_to_le16 (*(__u16 *)value);
	    else
		*(__u16 *)value = le16_to_cpu (sd_v2->sd_mode);
	    break;

	case GET_SD_SIZE:
	    if (set)
		sd_v2->sd_size = cpu_to_le64 (*(__u64 *)value);
	    else
		*(__u64 *)value = le64_to_cpu (sd_v2->sd_size);
	    break;

	case GET_SD_BLOCKS:
	    if (set)
		sd_v2->sd_blocks = cpu_to_le32 (*(__u32 *)value);
	    else
		*(__u32 *)value = le32_to_cpu (sd_v2->sd_blocks);
	    break;

	case GET_SD_NLINK:
	    if (set)
		sd_v2->sd_nlink = cpu_to_le32 (*(__u32 *)value);
	    else
		*(__u32 *)value = le32_to_cpu (sd_v2->sd_nlink);
	    break;

	case GET_SD_FIRST_DIRECT_BYTE:
	default:
	    reiserfs_panic ("get_set_sd_field: unknown field of new stat data");
	}
    }
}

int comp_ids (const void * p1, const void * p2)
{
    __u32 id1 = le32_to_cpu(*(__u32 *)p1) ;
    __u32 id2 = le32_to_cpu(*(__u32 *)p2) ;

    if (id1 < id2)
        return -1;
    if (id1 > id2)
        return 1 ;
    return 0 ;
}

/* functions to manipulate with super block's objectid map */

int is_objectid_used (reiserfs_filsys_t * fs, __u32 objectid)
{
    __u32 * objectid_map;
    __u32 count = get_sb_oid_cursize(fs->fs_ondisk_sb);
    int ret;
    __u32 pos;
    __u32 le_id = cpu_to_le32(objectid);


    objectid_map = (__u32 *)((char *)fs->fs_ondisk_sb + reiserfs_super_block_size (fs->fs_ondisk_sb));
    
    ret = reiserfs_bin_search(&le_id, objectid_map, count, sizeof(__u32), &pos, comp_ids);

    /* if the position returned is odd, the oid is in use */
    if (ret == POSITION_NOT_FOUND)
	return (pos & 1) ;

    /* if the position returned is even, the oid is in use */
    return !(pos & 1) ;
}


void mark_objectid_used (reiserfs_filsys_t * fs, __u32 objectid)
{
    int i;
    __u32 * objectid_map;
    int cursize;


    if (is_objectid_used (fs, objectid)) {
	return;
    }

    objectid_map = (__u32 *)((char *)fs->fs_ondisk_sb + reiserfs_super_block_size (fs->fs_ondisk_sb));
    cursize = get_sb_oid_cursize (fs->fs_ondisk_sb);

    for (i = 0; i < cursize; i += 2) {
	if (objectid >= le32_to_cpu (objectid_map [i]) &&
	    objectid < le32_to_cpu (objectid_map [i + 1]))
	    /* it is used */
	    return;
	
	if (objectid + 1 == le32_to_cpu (objectid_map[i])) {
	    /* size of objectid map does not change */
	    objectid_map[i] = cpu_to_le32 (objectid);
	    return;
	}
	
	if (objectid == le32_to_cpu (objectid_map[i + 1])) {
	    /* size of objectid map is decreased */
	    objectid_map [i + 1] = cpu_to_le32 (le32_to_cpu (objectid_map [i + 1]) + 1);

	    if (i + 2 < cursize) {
		if (objectid_map[i + 1] == objectid_map[i + 2]) {
		    memmove (objectid_map + i + 1, objectid_map + i + 1 + 2, 
			     (cursize - (i + 2 + 2 - 1)) * sizeof (__u32));
		    set_sb_oid_cursize (fs->fs_ondisk_sb, cursize - 2);
		}
	    }
	    return;
	}
	
	if (objectid < le32_to_cpu (objectid_map[i])) {
	    /* size of objectid map must be increased */
	    if (cursize == get_sb_oid_maxsize (fs->fs_ondisk_sb)) {
		/* here all objectids between objectid and objectid_map[i] get
                   used */
		objectid_map[i] = cpu_to_le32 (objectid);
		return;
	    } else {
		memmove (objectid_map + i + 2, objectid_map + i, (cursize - i) * sizeof (__u32));
		set_sb_oid_cursize (fs->fs_ondisk_sb, cursize + 2);
	    }
	    
	    objectid_map[i] = cpu_to_le32 (objectid);
	    objectid_map[i+1] = cpu_to_le32 (objectid + 1);
	    return;
	}
	
    }
    
    /* append to current objectid map, if we have space */
    if (i < get_sb_oid_maxsize (fs->fs_ondisk_sb)) {
	objectid_map[i] = cpu_to_le32 (objectid);
	objectid_map[i + 1] = cpu_to_le32 (objectid + 1);
	set_sb_oid_cursize (fs->fs_ondisk_sb, cursize + 2);
    } else if (i == get_sb_oid_maxsize (fs->fs_ondisk_sb)) {
	objectid_map[i - 1] = cpu_to_le32 (objectid + 1);
    } else
	die ("mark_objectid_as_used: objectid map corrupted");
    
    return;
}


int is_blocksize_correct (int blocksize)
{
    return ((blocksize == 0) || (blocksize % 1024) ? 0 : 1);
}

