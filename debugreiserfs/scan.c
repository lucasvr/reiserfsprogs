/*
 * Copyright 2000-2002 by Hans Reiser, licensing governed by reiserfs/README
 */

#include "debugreiserfs.h"
#include <regex.h>
#include <obstack.h>
#include <search.h>

#define obstack_chunk_alloc malloc
#define obstack_chunk_free free


/* -n pattern scans the area (on-disk bitmap, or all the device or extern
   bitmap) and looks for every name matching the pattern. All those names get
   stored in 'name_store' and are indexed by name (name_index) and by a key
   they point to (key_index) */

struct obstack name_store;
struct obstack item_store;

int saved_names;
int saved_items;
int skipped_names;
void * key_index;
void * name_index;

regex_t pattern;


struct saved_item {
    struct item_head si_ih;
    unsigned long si_block;
    int si_item_num;
    struct saved_item * si_next; /* list of items having the same key */
};


struct saved_name {
    unsigned int dirid; /* pointed object */
    unsigned int objectid;
    struct saved_name * first_name; /* pointer to name which points to the
                                       same object and contains list of file
                                       items */

    unsigned int parent_dirid; /* parent directory */
    unsigned int parent_objectid;
    unsigned long block; /* where we saw the name for the first time */
    unsigned short count; /* how many times the name appeared */

    void * items;
    struct saved_name * name_next; /* list of identical names */

    unsigned short name_len;
    char name[1];
};



/* attach item to every name in the list */
static void store_item (struct saved_name * name, struct buffer_head * bh,
			struct item_head * ih)
{
    struct saved_item * new;
    void * vp;
    struct saved_item * item_in;

    new = obstack_alloc (&item_store, sizeof (struct saved_item));
    new->si_ih = *ih;
    new->si_block = bh->b_blocknr;
    new->si_item_num = ih - B_N_PITEM_HEAD (bh, 0);
    new->si_next = 0;
    
    vp = tfind (new, &name->items, comp_keys);
    if (vp) {
	item_in = *(void **)vp;
	/* add item to the end of list of items having this key */
	while (1) {
	    if (!item_in->si_next) {
		item_in->si_next = new;
		break;
	    }
	    item_in = item_in->si_next;
	}
    } else
	tsearch (new, &name->items, comp_keys);

    saved_items ++;
}


static int comp_names (const void * p1, const void * p2)
{
    struct saved_name * name1, * name2;

    name1 = (struct saved_name *)p1;
    name2 = (struct saved_name *)p2;

    return strcmp (name1->name, name2->name);
}


static int comp_pointed (const void * p1, const void * p2)
{
    struct saved_name * name1, * name2;

    name1 = (struct saved_name *)p1;
    name2 = (struct saved_name *)p2;

    return comp_short_keys (&name1->dirid, &name2->dirid);
}


/* we consider name found only if it points to the same object and from the
   same directory */
static int name_found (struct saved_name * name, struct saved_name ** name_in)
{
    void * vp;
    struct saved_name * cur;

    vp = tfind (name, &name_index, comp_names);
    if (!vp) {
	*name_in = 0;
	return 0;
    }

    *name_in =  *(void **)vp;

    /* check every name in the list */
    cur = *name_in;
    while (cur) {
	if (!not_of_one_file (&name->dirid, &cur->dirid) &&
	    !not_of_one_file (&name->parent_dirid, &cur->parent_dirid)) {
	    cur->count ++;
	    return 1;
	}
	cur = cur->name_next;
    }
    
    return 0;
}


/* add key name is pointing to to the index of keys. If there was already name
   pointing to this key - add pointer to that name */
static void add_key (struct saved_name * name)
{
    void * vp;
  
    vp = tfind (name, &key_index, comp_pointed);
    if (vp) {
	/* */
	name->first_name = *(void **)vp;
    } else {
	tsearch (name, &key_index, comp_pointed);
    }
}


static void add_name (struct saved_name * name, struct saved_name * name_in)
{
    if (name_in) {
	/* add name to the end of list of identical names */
	while (1) {
	    if (!name_in->name_next) {
		name_in->name_next = name;
		break;
	    }
	    name_in = name_in->name_next;
	}
    } else {
	/* add name into name index */
	tsearch (name, &name_index, comp_names);
    }
}


/* take each name matching to a given pattern, */
static void scan_for_name (struct buffer_head * bh)
{
    int i, j;
    struct item_head * ih;
    struct reiserfs_de_head * deh;
    int namelen;
    char * name;
    struct saved_name * new, *name_in;
    char ch;
    int retval;
    int min_entry_size = 1;
    int ih_entry_count = 0;

    ih = B_N_PITEM_HEAD (bh, 0);
    for (i = 0; i < B_NR_ITEMS (bh); i ++, ih ++) {
	if (!is_direntry_ih (ih))
	    continue;
	if (is_it_bad_item (fs, ih, B_I_PITEM (bh, ih), 0, 1))
	    continue;

	deh = B_I_DEH (bh, ih);
	
        if ( (get_ih_entry_count (ih) > (get_ih_item_len(ih) / (DEH_SIZE + min_entry_size))) ||
            (get_ih_entry_count (ih) == 0))
            ih_entry_count = get_ih_item_len(ih) / (DEH_SIZE + min_entry_size);
        else 
            ih_entry_count = get_ih_entry_count (ih);
	
	for (j = 0; j < ih_entry_count; j ++, deh ++) {
	    name = name_in_entry (deh, j);
	    namelen = name_in_entry_length (ih, deh, j);
	    ch = name[namelen];
	    name[namelen] = 0;
	    retval = regexec (&pattern, name, 0, NULL, 0);
	    name[namelen] = ch;	    
	    if (retval != 0)
		continue;

	    /* name matching given pattern found */

	    new = obstack_alloc (&name_store, sizeof (struct saved_name) + namelen);

	    /* pointed object */
	    new->dirid = get_deh_dirid (deh);
	    new->objectid = get_deh_objectid (deh);

	    /* pointer to first name which points the same key */
	    new->first_name = 0;

	    /* where this name is from */
	    new->parent_dirid = get_key_dirid (&ih->ih_key);
	    new->parent_objectid = get_key_objectid (&ih->ih_key);
	    new->block = bh->b_blocknr;

	    new->count = 1;
	    new->items = 0;

	    /* name */
	    new->name_len = namelen;
	    memcpy (new->name, name, namelen);
	    new->name [namelen] = 0;
	    new->name_next = 0;

	    /*
	    reiserfs_warning (stdout, "\n(%K):%s-->(%K) - ", &new->parent_dirid,
			      new->name, &new->dirid);
	    */
	    if (name_found (new, &name_in)) {
		/* there was already exactly this name */
		obstack_free (&name_store, new);
		continue;
	    }

	    saved_names ++;
	    add_name (new, name_in);		
	    add_key (new);
	} /* for each entry */
    } /* for each item */

    return;
}


/* take every item, look for its key in the key index, if it is found - store
   item in the sorted list of items of a file */
static void scan_items (struct buffer_head * bh)
{
    int i;
    struct item_head * ih;
    struct saved_name * name_in_store;
    void * res;


    ih = B_N_PITEM_HEAD (bh, 0);
    for (i = 0; i < B_NR_ITEMS (bh); i ++, ih ++) {
	res = tfind (&ih->ih_key, &key_index, comp_pointed);
	if (!res)
	    /* there were no names pointing to this key */
	    continue;

	/* name pointing to this key found */
	name_in_store = *(struct saved_name **)res;

	store_item (name_in_store, bh, ih);
    }
}


/* FIXME: does not work for long files */
struct version {
    int flag; /* direct or indirect */
    int len;
    __u32 from;
    int count;
    void * data;
};


struct tail {
    __u32 offset;
    int len;
    char * data;
};


struct file_map {
    int head_len; /* number of unfm pointers */
    void * head;

    int tail_nr; /* number of tails found */
    struct tail * tails;
    
    int version_nr;
    void * versions; /* list of range versions */
};


struct file_map map;


static int have_to_append (struct item_head * ih)
{
    loff_t off = get_offset (&ih->ih_key);

    if (is_indirect_ih (ih)) {
	if (map.head_len * fs->fs_blocksize + 1 <= off)
	    return 1;
	return 0;
    } else if (is_direct_ih (ih)) {
	int i;
	__u32 tail_start;
	
	tail_start = (off & ~(fs->fs_blocksize - 1)) + 1;

	/* find correct tail first */
	for (i = 0; i < map.tail_nr; i ++) {
	    if (map.tails[i].offset == tail_start) {
		if (map.tails[i].offset + map.tails[i].len <= off)
		    return 1;
		return 0;
	    }
	}
	/* there was no this tail yet */
	return 1;
    }
    return 0;
}


static void do_append (struct item_head * ih, void * data)
{
    int i;
    int padd;
    loff_t off = get_offset (&ih->ih_key);

    if (is_indirect_ih (ih)) {

	padd = (off - 1) / fs->fs_blocksize - map.head_len;
	map.head = realloc (map.head, (map.head_len + padd + I_UNFM_NUM (ih)) * 4);
	if (!map.head)
	    reiserfs_panic ("realloc failed");
	memset ((char *)map.head + map.head_len * 4, 0, padd * 4);
	memcpy ((char *)map.head + (map.head_len + padd) * 4, data,
		get_ih_item_len (ih));
	map.head_len += (padd + I_UNFM_NUM (ih));

    } else if (is_direct_ih (ih)) {
	int tail_start;
	int skip;
	
	/* find correct tail first */
	tail_start = (off & ~(fs->fs_blocksize - 1)) + 1;
	skip = (off - 1) & (fs->fs_blocksize - 1);
	for (i = 0; i < map.tail_nr; i ++) {
	    if (map.tails[i].offset == tail_start) {
		map.tails[i].data = realloc (map.tails[i].data,
					     off - tail_start + get_ih_item_len (ih));
		if (!map.tails[i].data)
		    reiserfs_panic ("realloc failed");
		padd = skip - map.tails[i].len;
		memset (map.tails[i].data + map.tails[i].len, 0, padd);
		memcpy (map.tails[i].data + map.tails[i].len + padd, data, get_ih_item_len (ih));
		map.tails[i].len += (padd + get_ih_item_len (ih));
		return;
	    }
	}
	/* allocate memory for new tail */
	map.tails = realloc (map.tails, (map.tail_nr + 1) * sizeof (struct tail));
	if (!map.tails)
	    reiserfs_panic ("realloc failed");

	map.tails[map.tail_nr].offset = off;
	map.tails[map.tail_nr].len = skip + get_ih_item_len (ih);
	map.tails[map.tail_nr].data = malloc (map.tails[map.tail_nr].len);
	memset (map.tails[map.tail_nr].data, 0, skip);
	memcpy (map.tails[map.tail_nr].data + skip, data, get_ih_item_len (ih));
	map.tail_nr ++;		    
    }
}


/* map contains */
static void do_overwrite (struct item_head * ih, void * data)
{
    int skip; /* now may bytes/pointers to skip */
    int to_compare;
    int to_append;
    loff_t off = get_offset (&ih->ih_key);
    char * p;
    struct item_head tmp_ih;
    
    if (is_indirect_ih (ih)) {

	skip = (off - 1) / fs->fs_blocksize;
	to_compare = (map.head_len - skip > I_UNFM_NUM (ih)) ? I_UNFM_NUM (ih) :
	    (map.head_len - skip);
	to_append = I_UNFM_NUM (ih) - to_compare;
	
	p = (char *)map.head + skip * 4;

	if (memcmp (p, data, to_compare * 4))
	    reiserfs_warning (stderr, "overwrite (indirect): %H contains different data\n", ih);

	if (to_append) {
	    tmp_ih = *ih;
	    set_ih_item_len (&tmp_ih, get_ih_item_len (ih) - to_compare * 4);
	    set_offset (key_format (&ih->ih_key), &tmp_ih.ih_key, off + to_compare * fs->fs_blocksize);
	    do_append (&tmp_ih, (char *)data + to_compare * 4);
	}

    } else if (is_direct_ih (ih)) {
	int tail_start;
	int i;

	/* find correct tail first */
	tail_start = (off & ~(fs->fs_blocksize - 1)) + 1;
	for (i = 0; i < map.tail_nr; i ++) {
	    if (map.tails[i].offset == tail_start) {
		/* ih is a part of this tail */
		skip = (off - 1) & (fs->fs_blocksize - 1);
		to_compare = (map.tails[i].len - skip > get_ih_item_len (ih) ? get_ih_item_len (ih) :
		    map.tails[i].len - skip);
		to_append = get_ih_item_len (ih) - to_compare;

		p = (char *)map.tails[i].data + skip;

		if (memcmp (p, data, to_compare))
		    reiserfs_warning (stderr, "overwrite (direct): %H contains different data\n", ih);
		
		if (to_append) {
		    tmp_ih = *ih;
		    set_ih_item_len (&tmp_ih, get_ih_item_len (ih) - to_compare);
		    set_offset (key_format (&ih->ih_key), &tmp_ih.ih_key, off + to_compare);
		    do_append (&tmp_ih, (char *)data + to_compare);
		}
		return;
	    }
	}
	reiserfs_panic ("no appropriate tail found");
    }
}


static void map_one_item (struct saved_item * item)
{
    struct buffer_head * bh;
    struct item_head * ih;
    void * data;

    /* read the block containing the item */
    bh = bread (fs->fs_dev, item->si_block, fs->fs_blocksize);
    if (!bh) {
	reiserfs_warning (stderr, "bread failed\n");
	return;
    }

    ih = B_N_PITEM_HEAD (bh, item->si_item_num);
    data = B_I_PITEM (bh, ih);
    if (memcmp (&item->si_ih, ih, sizeof (*ih)))
	reiserfs_panic ("wrong item");

    if (have_to_append (ih)) {
	do_append (ih, data);
    } else
	do_overwrite (ih, data);

    brelse (bh);
}


static void map_item_list (const void *nodep, VISIT value, int level)
{
    struct saved_item * item, * longest;
    int bytes, max_bytes;

    if (value != leaf && value != postorder)
	return;

    item = *(struct saved_item **)nodep;

    /* 1. find the longest item */
    max_bytes = get_bytes_number (&item->si_ih, fs->fs_blocksize);
    longest = item;
    while (item->si_next) {
	item = item->si_next;
	bytes = get_bytes_number (&item->si_ih, fs->fs_blocksize);
	if (bytes > max_bytes) {
	    longest = item;
	    max_bytes = bytes;
	}
    }

    map_one_item (longest);

    /* map other items */
    item = *(struct saved_item **)nodep;
    while (item) {
	if (item != longest)
	    map_one_item (item);
	item = item->si_next;
    }
}


/* flush map which is in variable map */
static void flush_map (reiserfs_filsys_t * fs,
		       struct key * dir,
		       char * name,
		       struct key * key)
{
    int i;
    FILE * fp;
    __u32 v32;


    if (!map_file (fs))
	asprintf (&map_file (fs), "%s", ".map");
	
    /*reiserfs_warning (stderr, "Saving maps into %s\n", map_file (fs));*/
    fp = fopen (map_file (fs), "a");
    if (fp == 0) {
	reiserfs_warning (stderr, "flush_map: fopen failed: %m");
	return;
    }

    v32 = MAP_MAGIC;
    fwrite (&v32, sizeof (v32), 1, fp);

    /* device name */
    v32 = strlen (device_name (fs)) + 1;
    fwrite (&v32, sizeof (v32), 1, fp);
    fwrite (device_name (fs), v32, 1, fp);

    /* name length and the name itself */
    v32 = strlen (name) + 1;
    fwrite (&v32, sizeof (v32), 1, fp);
    fwrite (name, v32, 1, fp);
    
    /* short key of a directory */
    fwrite (dir, SHORT_KEY_SIZE, 1, fp);

    /* short key of file */
    fwrite (key, SHORT_KEY_SIZE, 1, fp);

    /* list of data block pointers */
    fwrite (&map.head_len, sizeof (map.head_len), 1, fp);
    fwrite (map.head, map.head_len * 4, 1, fp);


    /* find correct tail first */
    for (i = 0; i < map.tail_nr; i ++) {
	if (map.tails [i].offset == map.head_len * fs->fs_blocksize) {
	    /* tail length and the tail itself */
	    fwrite (&map.tails [i].len, sizeof (map.tails [i].len), 1, fp);
	    fwrite (map.tails [i].data, map.tails [i].len, 1, fp);
	    break;
	}
    }
    if (i == map.tail_nr) {
	/* no tail */
	v32 = 0;
	fwrite (&v32, sizeof (v32), 1, fp);
    }

    v32 = MAP_END_MAGIC;
    fwrite (&v32, sizeof (v32), 1, fp);

    fclose (fp);
}


/* write map of file to a map file */
static void make_file_map (const void *nodep, VISIT value, int level)
{
    struct saved_name * name;
    static int nr = 0;

    name = *(struct saved_name **)nodep;

    if (value == leaf || value == postorder) {
	while (name) {
	    reiserfs_warning (stdout, "%d - (%d): [%K]:\"%s\":\n", ++nr, name->count,
			      &name->parent_dirid, name->name);
	    
	    if (name->items) {
		/* initialize the map */
		memset (&map, 0, sizeof (struct file_map));
		
		/* make a map of file */
		twalk (name->items, map_item_list);
		
		/* write map to a file */
		flush_map (fs, (struct key *)&name->parent_dirid, name->name,
			   (struct key *)&name->dirid);
		
	    } else if (name->first_name)
		reiserfs_warning (stdout, "[%K]:\"%s\" has item list\n",
				  &name->first_name->parent_dirid,
				  name->first_name->name);
	    else {
		reiserfs_warning (stdout, "No items of the file [%K] found\n",
				  &name->dirid);
	    }

	    name = name->name_next;
	}
    }
}

/* file.list will contain list of names such that it can be used as
   input for -N */
static void print_name (const void *nodep, VISIT value, int level)
{
    struct saved_name * name;
    static FILE * fp = 0;

    if (fp == 0) {
	fp = fopen ("file.list", "w+");
	if (!fp)
	    reiserfs_panic ("could open file.list: %m");
    }
    name = *(struct saved_name **)nodep;
    if (value == leaf || value == postorder) {
	while (name) {
	    if (name->items)
		/* we have found items of this file */
		reiserfs_warning (fp, "%u %u %s\n",
				  name->parent_dirid, name->parent_objectid,
				  name->name);
	    name = name->name_next;
	}
    }
}



/* store map if it is a regular file */
static void locate_file (reiserfs_filsys_t * fs, struct key * key)
{
    INITIALIZE_PATH (path);
    struct key * next_key;
    int retval;

    do {
	retval = reiserfs_search_by_key_4 (fs, key, &path);
	if (retval != ITEM_FOUND)
	    break;

	if (!is_stat_data_key (key) && !is_direntry_key (key)) {
	    struct saved_item si;

	    si.si_block = get_bh (&path)->b_blocknr;
	    si.si_item_num = get_item_pos (&path);
	    si.si_ih = *get_ih (&path);
	    map_one_item (&si);
	}

	/*reiserfs_warning (stdout, "\t");
	  reiserfs_print_item (stdout, get_bh (&path), get_ih (&path));*/
	
	next_key = reiserfs_next_key (&path);
	if (!next_key || not_of_one_file (next_key, key))
	    break;

	*key = *next_key;
	pathrelse (&path); 
    } while (1);

    pathrelse (&path); 
}


/* read stdin and look for specified name in the specified directory */
static void look_for_name (reiserfs_filsys_t * fs)
{
    INITIALIZE_PATH (path);
    char * name, * objectid, * dirid;
    size_t n;
    struct key key = {0, };

    reiserfs_warning (stderr,
		      "Enter dirid objectid \"name\" or press ^D to quit\n");
    while (1) {
	reiserfs_warning (stderr, ">");
	n = 0; dirid = 0;
	if (getdelim (&dirid, &n, ' ', stdin) == -1)
	    break;
	if (!strcmp (dirid, "\n"))
	    break;
	set_key_dirid (&key, atol (dirid));

	n = 0; objectid = 0;
	if (getdelim (&objectid, &n, ' ', stdin) == -1)
	    break;
	set_key_objectid (&key, atol (objectid));

	n = 0; name = 0;
	if (getdelim (&name, &n, '\n', stdin) == -1)
	    break;

	name [strlen (name) - 1] = 0;
	reiserfs_warning (stdout, "looking for file \"%s\" in (%K) - ",
			  name, &key);

	if (reiserfs_locate_entry (fs, &key, name, &path)) {
	    struct key fkey = {0, };
	    struct reiserfs_de_head * deh;

	    reiserfs_warning (stdout, "name is found in block %lu (item %d, entry %d)\n",
			      get_bh (&path)->b_blocknr, get_item_pos (&path),
			      path.pos_in_item);
	    deh = B_I_DEH (get_bh (&path), get_ih (&path)) + path.pos_in_item;
	    set_key_dirid (&fkey, get_deh_dirid (deh));
	    set_key_objectid (&fkey, get_deh_objectid (deh));
	    
	    pathrelse (&path);

	    /* look for file and print its layout */
	    memset (&map, 0, sizeof (struct file_map));

	    locate_file (fs, &fkey);

	    flush_map (fs, &key, name, &fkey);

	} else {
	    reiserfs_warning (stdout, "name not found\n");
	}

	free (dirid);
	free (objectid);
	free (name);
    }
}


FILE * log;

static void scan_for_key (struct buffer_head * bh, struct key * key)
{
    int i, j;
    struct item_head * ih;
    struct reiserfs_de_head * deh;
    int min_entry_size = 1;
    int ih_entry_count = 0;


    ih = B_N_PITEM_HEAD (bh, 0);
    for (i = 0; i < B_NR_ITEMS (bh); i ++, ih ++) {
	if ((get_key_dirid (&ih->ih_key) == get_key_dirid (key) || 
	     (int)get_key_dirid (key) == -1) &&
	    (get_key_objectid (&ih->ih_key) == get_key_objectid (key) || 
	     (int)get_key_objectid (key) == -1)) {
	    reiserfs_warning (log, "%d-th item of block %lu is item of file %K: %H\n",
			      i, bh->b_blocknr, key, ih);
	}
	if (!is_direntry_ih (ih))
	    continue;
	deh = B_I_DEH (bh, ih);
	
        if ( (get_ih_entry_count (ih) > (get_ih_item_len(ih) / (DEH_SIZE + min_entry_size))) ||
            (get_ih_entry_count (ih) == 0))
            ih_entry_count = get_ih_item_len(ih) / (DEH_SIZE + min_entry_size);
        else 
            ih_entry_count = get_ih_entry_count (ih);
	
	
	for (j = 0; j < ih_entry_count; j ++, deh ++) {
	    if ((get_deh_dirid (deh) == get_key_dirid (key) || (int)get_key_dirid (key) == -1) &&
		(get_deh_objectid (deh) == get_key_objectid (key) || (int)get_key_objectid (key) == -1)) {
		reiserfs_warning (log, "dir item %d (%H) of block %lu has "
				  "entry (%d-th) %.*s pointing to %K\n",
				  i, ih, bh->b_blocknr, j,
				  name_in_entry_length (ih, deh, j), name_in_entry (deh, j), key);
	    }
	}	
    }
    return;
}



void do_scan (reiserfs_filsys_t * fs)
{
    unsigned long i;
    struct buffer_head * bh;
    int type;
    char * answer = 0;
    size_t n = 0;
    struct key key = {0, 0, };
    unsigned long done, total;


    if (debug_mode (fs) == DO_SCAN_JOURNAL) {
	scan_journal (fs);
	return;
    }

    if (debug_mode (fs) == DO_LOOK_FOR_NAME) {
	/* look for a file in using tree algorithms */
	look_for_name (fs);
	return;
    }

    /* scan area of disk and store all names matching the pattern */

    /* initialize storage and two indexes */
    obstack_init (&name_store);
    obstack_init (&item_store);
    key_index = 0;
    name_index = 0;

    total = reiserfs_bitmap_ones (input_bitmap (fs));

    log = fopen ("scan.log", "w+");
    printf ("Log file 'scan.log' is opened\n");

    if (debug_mode (fs) == DO_SCAN_FOR_NAME) {
	if (regcomp (&pattern, name_pattern (fs), 0)) {
	    printf ("regcomp failed");
	    return;
	}

	printf ("Looking for names matching %s\n", name_pattern (fs));
	set_key_dirid (&key, 1);
    } else {
	printf ("What key do you want to find: dirid?");
	getline (&answer, &n, stdin);
	set_key_dirid (&key, atoi (answer));

	printf ("objectid?");
	getline (&answer, &n, stdin);
	set_key_objectid (&key, atoi (answer));
	reiserfs_warning (stderr, "looking for (%K)\n", &key);
    }

    done = 0;
    for (i = 0; i < get_sb_block_count (fs->fs_ondisk_sb); i ++) {
	if (!reiserfs_bitmap_test_bit (input_bitmap (fs), i))
	    continue;
	bh = bread (fs->fs_dev, i, fs->fs_blocksize);
	if (!bh) {
	    printf ("could not read block %lu\n", i);
	    continue;
	}
	type = who_is_this (bh->b_data, bh->b_size);
	switch (type) {
	case THE_JDESC:
	    if (!get_key_dirid (&key))
		printf ("block %lu is journal descriptor\n", i);
	    reiserfs_bitmap_clear_bit (input_bitmap (fs), i);
	    break;
	case THE_SUPER:
	    if (!get_key_dirid (&key))
		printf ("block %lu is reiserfs super block\n", i);
	    reiserfs_bitmap_clear_bit (input_bitmap (fs), i);
	    break;
	case THE_INTERNAL:
	    if (!get_key_dirid (&key))
		printf ("block %lu is reiserfs internal node\n", i);
	    reiserfs_bitmap_clear_bit (input_bitmap (fs), i);
	    break;
	case THE_LEAF:
	    if (debug_mode (fs) == DO_SCAN_FOR_NAME) {
		scan_for_name (bh);
	    } else if (get_key_dirid (&key)) {
		scan_for_key (bh, &key);
	    } else {
		printf ("block %lu is reiserfs leaf node\n", i);
	    }
	    break;
	default:
	    reiserfs_bitmap_clear_bit (input_bitmap (fs), i);
	    break;
	}
	brelse (bh);
	print_how_far (stderr, &done, total, 1, be_quiet (fs));
    }

    fprintf (stderr, "\n");
    if (debug_mode (fs) == DO_SCAN_FOR_NAME)
        fprintf (stderr, "There were found %d names matching the pattern \"%s\", %d names skipped\n",
            saved_names, name_pattern (fs), skipped_names);
    fflush (stderr);


    if (debug_mode (fs) != DO_SCAN_FOR_NAME)
	return;

    /* step 2: */
    done = 0;
    total = reiserfs_bitmap_ones (input_bitmap (fs));
    printf ("%ld bits set in bitmap\n", total);
    for (i = 0; i < get_sb_block_count (fs->fs_ondisk_sb); i ++) {
	if (!reiserfs_bitmap_test_bit (input_bitmap (fs), i))
	    continue;
	bh = bread (fs->fs_dev, i, fs->fs_blocksize);
	if (!bh) {
	    printf ("could not read block %lu\n", i);
	    continue;
	}
	if (who_is_this (bh->b_data, bh->b_size) != THE_LEAF) {
	    brelse (bh);
	    continue;
	}

	scan_items (bh);
	
	brelse (bh);
	print_how_far (stderr, &done, total, 1, be_quiet (fs));
    }
    fprintf (stderr, "\nThere were %d items saved\n", saved_items);

    /* ok, print what we found */
    /*twalk (name_index, print_file);*/

    /* create map for every file in */
    twalk (name_index, make_file_map);

    /* print names of files we have map of in a file 'file.list' */
    twalk (name_index, print_name);
}

