/*
 * Copyright 2000-2004 by Hans Reiser, licensing governed by 
 * reiserfsprogs/README
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "debugreiserfs.h"
#include "util/misc.h"

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
static void store_item (struct saved_name * name, reiserfs_bh_t * bh,
			reiserfs_ih_t * ih, int pos)
{
    struct saved_item * new;
    void * vp;
    struct saved_item * item_in;

    new = obstack_alloc (&item_store, sizeof (struct saved_item));
    new->si_ih = *ih;
    new->si_block = bh->b_blocknr;
    new->si_item_num = ih - reiserfs_ih_at (bh, 0);
    new->si_next = 0;
    new->si_entry_pos = pos;
    
    vp = tfind (new, &name->items, reiserfs_key_comp);
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
	tsearch (new, &name->items, reiserfs_key_comp);

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

    return reiserfs_key_comp2 (&name1->dirid, &name2->dirid);
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
	if (!reiserfs_key_comp2 (&name->dirid, &cur->dirid) &&
	    !reiserfs_key_comp2 (&name->parent_dirid, &cur->parent_dirid))
	{
	    cur->count ++;
	    *name_in = cur;
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
static void scan_for_name (reiserfs_bh_t * bh)
{
    int i, j, i_num;
    reiserfs_ih_t * ih;
    reiserfs_deh_t * deh;
    int namelen;
    char * name;
    struct saved_name * new, *name_in;
    char ch;
    int retval;
    int min_entry_size = 1;
    int ih_entry_count = 0;

    ih = reiserfs_ih_at (bh, 0);
    i_num = reiserfs_leaf_estimate_items(bh);
    for (i = 0; i < i_num; i ++, ih ++) {
	if (!reiserfs_ih_dir (ih))
	    continue;
	if (reiserfs_leaf_correct_at (fs, ih, reiserfs_item_by_ih (bh, ih), 
				      0, 1))
	{
	    continue;
	}

	deh = reiserfs_deh (bh, ih);
	
        if ( (reiserfs_ih_get_entries (ih) > (reiserfs_ih_get_len(ih) / (REISERFS_DEH_SIZE + min_entry_size))) ||
            (reiserfs_ih_get_entries (ih) == 0))
            ih_entry_count = reiserfs_ih_get_len(ih) / (REISERFS_DEH_SIZE + min_entry_size);
        else 
            ih_entry_count = reiserfs_ih_get_entries (ih);
	
	for (j = 0; j < ih_entry_count; j ++, deh ++) {
	    name = reiserfs_deh_name (deh, j);
	    namelen = reiserfs_direntry_name_len (ih, deh, j);
	    ch = name[namelen];
	    name[namelen] = 0;
	    retval = regexec (&pattern, name, 0, NULL, 0);
	    name[namelen] = ch;	    
	    if (retval != 0)
		continue;

	    /* name matching given pattern found */

	    new = obstack_alloc (&name_store, sizeof (struct saved_name) + namelen);

	    /* pointed object */
	    new->dirid = reiserfs_deh_get_did (deh);
	    new->objectid = reiserfs_deh_get_obid (deh);

	    /* pointer to first name which points the same key */
	    new->first_name = 0;

	    /* where this name is from */
	    new->parent_dirid = reiserfs_key_get_did (&ih->ih_key);
	    new->parent_objectid = reiserfs_key_get_oid (&ih->ih_key);
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

static struct saved_name *scan_for_key(reiserfs_key_t *key) {
	struct saved_name * new, *name_in;
	char name[REISERFS_NAME_MAX];

	sprintf(name, "%u_%u", reiserfs_key_get_did (key), reiserfs_key_get_oid (key));
	new = obstack_alloc (&name_store, sizeof (struct saved_name) + strlen(name));

	/* pointed object */
	new->dirid = reiserfs_key_get_did (key);
	new->objectid = reiserfs_key_get_oid (key);

	/* pointer to first name which points the same key */
	new->first_name = 0;

	/* where this name is from */

	new->parent_dirid = 0;
	new->parent_objectid = 0;
	new->block = 0;
	new->count = 1;
	new->items = 0;

	/* name */
	new->name_len = strlen(name);
	memcpy (new->name, name, new->name_len);
	new->name [new->name_len] = 0;
	new->name_next = 0;

	if (name_found (new, &name_in)) {
		/* there was already exactly this name */
		obstack_free (&name_store, new);
		return name_in;
	}

	saved_names ++;
	add_name (new, name_in);

	return new;
}

static int comp_token_key(reiserfs_bh_t *bh, reiserfs_ih_t *ih, reiserfs_key_t *key) {
    reiserfs_deh_t * deh;
    int j, ih_entry_count = 0;
    int min_entry_size = 1;
    
    if ((reiserfs_key_get_did(&ih->ih_key) == reiserfs_key_get_did(key) || 
	 reiserfs_key_get_did(key) == ~(__u32)0) &&
	(reiserfs_key_get_oid(&ih->ih_key) == reiserfs_key_get_oid(key) || 
	 reiserfs_key_get_oid(key) == ~(__u32)0))
	return -1;

    if (!reiserfs_ih_dir (ih))
	return 0;
	
    deh = reiserfs_deh (bh, ih);

    if ( (reiserfs_ih_get_entries (ih) > (reiserfs_ih_get_len(ih) / (REISERFS_DEH_SIZE + min_entry_size))) ||
	 (reiserfs_ih_get_entries (ih) == 0))
	 ih_entry_count = reiserfs_ih_get_len(ih) / (REISERFS_DEH_SIZE + min_entry_size);
    else 
	 ih_entry_count = reiserfs_ih_get_entries (ih);

	
    for (j = 0; j < ih_entry_count; j ++, deh ++) {
	if ((reiserfs_deh_get_did (deh) == reiserfs_key_get_did (key) || (int)reiserfs_key_get_did (key) == -1) &&
	    (reiserfs_deh_get_obid (deh) == reiserfs_key_get_oid (key) || (int)reiserfs_key_get_oid (key) == -1)) 
	{
	    return j;
	}
    }

    return 0;
}

/* take every item, look for its key in the key index, if it is found - store
   item in the sorted list of items of a file */
static void scan_items (reiserfs_bh_t * bh, reiserfs_key_t *key) {
    int i, i_num, pos;
    reiserfs_ih_t * ih;
    struct saved_name * name_in_store;
    void * res;


    ih = reiserfs_ih_at (bh, 0);
    i_num = reiserfs_leaf_estimate_items(bh);
    for (i = 0; i < i_num; i ++, ih ++) {
	if (key) {
	    if (!(pos = comp_token_key(bh, ih, key)))
		    continue;

	    name_in_store = scan_for_key(&ih->ih_key);
	} else {
	    if (!(res = tfind (&ih->ih_key, &key_index, comp_pointed)))
		continue;

	    /* name pointing to this key found */
	    name_in_store = *(struct saved_name **)res;
	    pos = -1;
	}

	store_item (name_in_store, bh, ih, pos);
    }
}


/* FIXME: does not work for long files */
struct version {
    int flag; /* direct or extent */
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


static int have_to_append (reiserfs_ih_t * ih)
{
    unsigned long long off = reiserfs_key_get_off (&ih->ih_key);

    if (reiserfs_ih_ext (ih)) {
	if (map.head_len * fs->fs_blocksize + 1 <= off)
	    return 1;
	return 0;
    } else if (reiserfs_ih_direct (ih)) {
	int i;
	__u32 tail_start;
	
	tail_start = (off & ~(fs->fs_blocksize - 1)) + 1;

	// find correct tail first 
	for (i = 0; i < map.tail_nr; i ++) {
	    if (map.tails[i].offset == tail_start) {
		if (map.tails[i].offset + map.tails[i].len <= off)
		    return 1;
		return 0;
	    }
	}
	// there was no this tail yet 
	return 1;
    }
    return 0;
}


static void do_append (reiserfs_ih_t * ih, void * data)
{
    int i;
    int padd;
    unsigned long long off = reiserfs_key_get_off (&ih->ih_key);

    if (reiserfs_ih_ext (ih)) {

	padd = (off - 1) / fs->fs_blocksize - map.head_len;
	map.head = realloc (map.head, (map.head_len + padd + reiserfs_ext_count (ih)) * 4);
	if (!map.head)
	    reiserfs_panic ("realloc failed");
	memset ((char *)map.head + map.head_len * 4, 0, padd * 4);
	memcpy ((char *)map.head + (map.head_len + padd) * 4, data,
		reiserfs_ih_get_len (ih));
	map.head_len += (padd + reiserfs_ext_count (ih));

    } else if (reiserfs_ih_direct (ih)) {
	unsigned int tail_start, skip;
	
	// find correct tail first 
	tail_start = (off & ~(fs->fs_blocksize - 1)) + 1;
	skip = (off - 1) & (fs->fs_blocksize - 1);
	for (i = 0; i < map.tail_nr; i ++) {
	    if (map.tails[i].offset == tail_start) {
		map.tails[i].data = realloc (map.tails[i].data,
					     off - tail_start + reiserfs_ih_get_len (ih));
		if (!map.tails[i].data)
		    reiserfs_panic ("realloc failed");
		padd = skip - map.tails[i].len;
		memset (map.tails[i].data + map.tails[i].len, 0, padd);
		memcpy (map.tails[i].data + map.tails[i].len + padd, data, reiserfs_ih_get_len (ih));
		map.tails[i].len += (padd + reiserfs_ih_get_len (ih));
		return;
	    }
	}
	// allocate memory for new tail 
	map.tails = realloc (map.tails, (map.tail_nr + 1) * sizeof (struct tail));
	if (!map.tails)
	    reiserfs_panic ("realloc failed");

	map.tails[map.tail_nr].offset = off;
	map.tails[map.tail_nr].len = skip + reiserfs_ih_get_len (ih);
	map.tails[map.tail_nr].data = malloc (map.tails[map.tail_nr].len);
	memset (map.tails[map.tail_nr].data, 0, skip);
	memcpy (map.tails[map.tail_nr].data + skip, data, reiserfs_ih_get_len (ih));
	map.tail_nr ++;		    
    }
}


// map contains 
static void do_overwrite (reiserfs_ih_t * ih, void * data)
{
    unsigned long long off, skip;
    int to_compare, to_append;
    reiserfs_ih_t tmp_ih;
    char * p;
    
    off = reiserfs_key_get_off (&ih->ih_key);
    
    if (reiserfs_ih_ext (ih)) {

	skip = (off - 1) / fs->fs_blocksize;
	to_compare = (map.head_len - skip > reiserfs_ext_count (ih)) ? 
	    reiserfs_ext_count (ih) : (map.head_len - skip);
	to_append = reiserfs_ext_count (ih) - to_compare;
	
	p = (char *)map.head + skip * 4;

	if (memcmp (p, data, to_compare * 4))
	    reiserfs_warning (stderr, "overwrite (extent): %H contains different data\n", ih);

	if (to_append) {
	    tmp_ih = *ih;
	    reiserfs_ih_set_len (&tmp_ih, reiserfs_ih_get_len (ih) - to_compare * 4);
	    reiserfs_key_set_off (reiserfs_key_format (&ih->ih_key), 
				     &tmp_ih.ih_key, off + to_compare * 
				     fs->fs_blocksize);
	    
	    do_append (&tmp_ih, (char *)data + to_compare * 4);
	}

    } else if (reiserfs_ih_direct (ih)) {
	unsigned int tail_start;
	int i;

	// find correct tail first 
	tail_start = (off & ~(fs->fs_blocksize - 1)) + 1;
	for (i = 0; i < map.tail_nr; i ++) {
	    if (map.tails[i].offset == tail_start) {
		// ih is a part of this tail 
		skip = (off - 1) & (fs->fs_blocksize - 1);
		to_compare = (map.tails[i].len - skip > reiserfs_ih_get_len (ih) ? reiserfs_ih_get_len (ih) :
		    map.tails[i].len - skip);
		to_append = reiserfs_ih_get_len (ih) - to_compare;

		p = (char *)map.tails[i].data + skip;

		if (memcmp (p, data, to_compare))
		    reiserfs_warning (stderr, "overwrite (direct): %H contains "
				      "different data\n", ih);
		
		if (to_append) {
		    tmp_ih = *ih;
		    reiserfs_ih_set_len (&tmp_ih, reiserfs_ih_get_len (ih) - to_compare);
		    reiserfs_key_set_off (reiserfs_key_format (&ih->ih_key), 
					     &tmp_ih.ih_key, off + to_compare);
		    
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
    reiserfs_bh_t * bh;
    reiserfs_ih_t * ih;
    void * data;

    // read the block containing the item 
    bh = reiserfs_buffer_read (fs->fs_dev, item->si_block, fs->fs_blocksize);
    if (!bh) {
	reiserfs_warning (stderr, "reiserfs_buffer_read failed\n");
	return;
    }

    ih = reiserfs_ih_at (bh, item->si_item_num);
    data = reiserfs_item_by_ih (bh, ih);
    if (memcmp (&item->si_ih, ih, sizeof (*ih)))
	reiserfs_panic ("wrong item");

    if (have_to_append (ih)) {
	do_append (ih, data);
    } else
	do_overwrite (ih, data);

    reiserfs_buffer_close (bh);
}

// flush map which is in variable map 
static void flush_map (reiserfs_filsys_t * fs,
		       reiserfs_key_t * dir,
		       char * name,
		       reiserfs_key_t * key)
{
    int i;
    FILE * fp;
    __u32 v32;


    if (map_file (fs) == '\0')
	sprintf(map_file (fs), ".map");
	
    //reiserfs_warning (stderr, "Saving maps into %s\n", map_file (fs));
    fp = fopen (map_file (fs), "a");
    if (fp == 0) {
	reiserfs_warning (stderr, "flush_map: fopen failed: %m");
	return;
    }

    v32 = MAP_MAGIC;
    fwrite (&v32, sizeof (v32), 1, fp);

    // device name 
    v32 = strlen (device_name (fs)) + 1;
    fwrite (&v32, sizeof (v32), 1, fp);
    fwrite (device_name (fs), v32, 1, fp);

    // name length and the name itself 
    v32 = strlen (name) + 1;
    fwrite (&v32, sizeof (v32), 1, fp);
    fwrite (name, v32, 1, fp);
    
    // short key of a directory 
    fwrite (dir, REISERFS_KEY_SHSIZE, 1, fp);

    // short key of file 
    fwrite (key, REISERFS_KEY_SHSIZE, 1, fp);

    // list of data block pointers 
    fwrite (&map.head_len, sizeof (map.head_len), 1, fp);
    fwrite (map.head, map.head_len * 4, 1, fp);


    // find correct tail first 
    for (i = 0; i < map.tail_nr; i ++) {
	if (map.tails [i].offset == map.head_len * fs->fs_blocksize) {
	    // tail length and the tail itself 
	    fwrite (&map.tails [i].len, sizeof (map.tails [i].len), 1, fp);
	    fwrite (map.tails [i].data, map.tails [i].len, 1, fp);
	    break;
	}
    }
    if (i == map.tail_nr) {
	// no tail 
	v32 = 0;
	fwrite (&v32, sizeof (v32), 1, fp);
    }

    v32 = MAP_END_MAGIC;
    fwrite (&v32, sizeof (v32), 1, fp);

    fclose (fp);
}


// write map of file to a map file 
/*

static void map_item_list (const void *nodep, VISIT value, int level)
{
    struct saved_item * item, * longest;
    int bytes, max_bytes;

    if (value != leaf && value != postorder)
	return;

    item = *(struct saved_item **)nodep;

    // 1. find the longest item 
    max_bytes = reiserfs_leaf_ibytes (&item->si_ih, fs->fs_blocksize);
    longest = item;
    while (item->si_next) {
	item = item->si_next;
	bytes = reiserfs_leaf_ibytes (&item->si_ih, fs->fs_blocksize);
	if (bytes > max_bytes) {
	    longest = item;
	    max_bytes = bytes;
	}
    }

    map_one_item (longest);

    // map other items 
    item = *(struct saved_item **)nodep;
    while (item) {
	if (item != longest)
	    map_one_item (item);
	item = item->si_next;
    }
}

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
		// initialize the map 
		memset (&map, 0, sizeof (struct file_map));
		
		// make a map of file 
		twalk (name->items, map_item_list);
		
		// write map to a file 
		flush_map (fs, (reiserfs_key_t *)&name->parent_dirid, name->name,
			   (reiserfs_key_t *)&name->dirid);
		
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
*/

static void print_items(FILE *fp, reiserfs_filsys_t * fs) {
    reiserfs_bh_t *bh;
    struct saved_item item;
    int size = sizeof(struct saved_item) - sizeof(struct saved_item *);

    while (fread(&item, size, 1, fp) == 1) {
	bh = reiserfs_buffer_read (fs->fs_dev, item.si_block, fs->fs_blocksize);
	if (!bh) {
	    reiserfs_warning (fp, "reiserfs_buffer_read failed\n");
	    continue;
	}
	reiserfs_leaf_print(stdout, fs, bh, 0, 
			    item.si_item_num, 
			    item.si_item_num + 1);
	reiserfs_buffer_close(bh);
    }
}

void print_map(reiserfs_filsys_t * fs) {
    FILE * fp;

    if (map_file (fs) [0] != '\0') {
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

    print_items(fp, fs);
    
    if (fp != stdin) {
	fclose (fp); 
	fp = NULL;
    }
}


static FILE *fp = 0;
FILE * log_to;

static void save_items(const void *nodep, VISIT value, int level) {
    struct saved_item *item;
    
    
    if (value != leaf && value != postorder)
	return;

    item = *(struct saved_item **)nodep;

    while (item) {
	if (fp) {
	    fwrite(item, sizeof(struct saved_item) - sizeof(struct saved_item *), 1, fp);
	} else {
	    if (reiserfs_ih_dir (&item->si_ih) && item->si_entry_pos != -1) {
		reiserfs_warning(log_to, "block %lu, item %d (%H): entry %d\n",
				 item->si_block, item->si_item_num, 
				 &item->si_ih, item->si_entry_pos);
	    } else {
		reiserfs_warning(log_to, "block %lu, item %d belongs to file %K: %H\n",
				 item->si_block, item->si_item_num, &item->si_ih.ih_key,
				 &item->si_ih);

	    } 
	}
	
	item = item->si_next;
    }
}

static void make_map(const void *nodep, VISIT value, int level) {
    struct saved_name * name;
    char file_name[4096];
    static int nr = 0;

    name = *(struct saved_name **)nodep;
    
    if (value == leaf || value == postorder) {
	while (name) {
	    if (map_file(fs)[0] != '\0') {
		sprintf(file_name, "%s.%d", map_file(fs), ++nr);
		reiserfs_warning (log_to, "%d - (%d): [%K]:\"%s\": stored in the %s\n", 
				  nr, name->count, &name->parent_dirid, name->name, file_name);

		if (fp == 0) {
		    fp = fopen (file_name, "w+");
		    if (!fp) {
			reiserfs_exit (1, "could open %s: %m", file_name);
		    }
		}
	    }

	    if (name->items) 
		twalk (name->items, save_items);

	    name = name->name_next;
	    if (fp) {
		fclose(fp);
		fp = NULL;
	    }
	}
    }
}

/* store map if it is a regular file */
static void locate_file (reiserfs_filsys_t * fs, reiserfs_key_t * key)
{
    REISERFS_PATH_INIT (path);
    const reiserfs_key_t * next_key;
    int retval;

    do {
	retval = reiserfs_tree_search_item (fs, key, &path);
	if (retval != ITEM_FOUND)
	    break;

	if (!reiserfs_key_stat (key) && !reiserfs_key_dir (key)) {
	    struct saved_item si;

	    si.si_block = REISERFS_PATH_LEAF (&path)->b_blocknr;
	    si.si_item_num = REISERFS_PATH_LEAF_POS (&path);
	    si.si_ih = *REISERFS_PATH_IH (&path);
	    map_one_item (&si);
	}

	next_key = reiserfs_tree_next_key (&path, fs);
	if (!next_key || reiserfs_key_comp2 (next_key, key))
	    break;

	*key = *next_key;
	reiserfs_tree_pathrelse (&path); 
    } while (1);

    reiserfs_tree_pathrelse (&path); 
}


/* read stdin and look for specified name in the specified directory */
static void look_for_name (reiserfs_filsys_t * fs)
{
    char buf[256], *p;
    unsigned long dirid, objectid;
    REISERFS_PATH_INIT (path);
    reiserfs_key_t key = {0, };

    reiserfs_warning (stderr, "Enter dirid objectid "
		      "\"name\" or press ^D to quit\n");
    while (1) {
	reiserfs_warning (stderr, ">");
	
	fgets(buf, sizeof(buf), stdin);
	if ((p = (char *)strtok(buf, " \t\n")) == NULL)
		continue;
	
	if (sscanf(p, "%lu ", &dirid) <= 0)
		continue;
	
	if ((p = (char *)strtok(NULL, " \t\n")) == NULL)
		continue;
	
	if (sscanf(p, "%lu ", &objectid) <= 0)
		continue;
	
	if ((p = (char *)strtok(NULL, " \t\n")) == NULL)
		continue;
	
	reiserfs_key_set_did (&key, dirid);
	reiserfs_key_set_oid (&key, objectid);

	p [strlen(p) - 1] = '\0';
	reiserfs_warning (stdout, "looking for file \"%s\" in (%K) - ",
			  p, &key);

	if (reiserfs_tree_scan_name (fs, &key, p, &path)) {
	    reiserfs_key_t fkey = {0, };
	    reiserfs_deh_t * deh;

	    reiserfs_warning (stdout, "name is found in block %lu (item %d, entry %d)\n",
			      REISERFS_PATH_LEAF (&path)->b_blocknr, REISERFS_PATH_LEAF_POS (&path),
			      path.pos_in_item);
	    deh = reiserfs_deh (REISERFS_PATH_LEAF (&path), REISERFS_PATH_IH (&path)) + path.pos_in_item;
	    reiserfs_key_set_did (&fkey, reiserfs_deh_get_did (deh));
	    reiserfs_key_set_oid (&fkey, reiserfs_deh_get_obid (deh));
	    
	    reiserfs_tree_pathrelse (&path);

	    /* look for file and print its layout */
	    memset (&map, 0, sizeof (struct file_map));

	    locate_file (fs, &fkey);

	    flush_map (fs, &key, p, &fkey);
	} else {
	    reiserfs_warning (stdout, "name not found\n");
	}
    }
}

#if 0
static void scan_for_key (reiserfs_bh_t * bh, reiserfs_key_t * key)
{
    int i, j, i_num;
    reiserfs_ih_t * ih;
    reiserfs_deh_t * deh;
    int min_entry_size = 1;
    int ih_entry_count = 0;

    ih = reiserfs_ih_at (bh, 0);
    i_num = reiserfs_leaf_estimate_items(bh);
    for (i = 0; i < i_num; i ++, ih ++) {
	if ((reiserfs_key_get_did(&ih->ih_key) == reiserfs_key_get_did(key) || 
	     reiserfs_key_get_did(key) == ~(__u32)0) &&
	    (reiserfs_key_get_oid(&ih->ih_key) == reiserfs_key_get_oid(key) || 
	     reiserfs_key_get_oid(key) == ~(__u32)0)) 
	{
	    reiserfs_warning(log_to, "%d-th item of block %lu is item of file %K: %H\n",
			      i, bh->b_blocknr, key, ih);
	}
	if (!reiserfs_ih_dir (ih))
	    continue;
	deh = reiserfs_deh (bh, ih);
	
        if ( (reiserfs_ih_get_entries (ih) > (reiserfs_ih_get_len(ih) / (REISERFS_DEH_SIZE + min_entry_size))) ||
            (reiserfs_ih_get_entries (ih) == 0))
            ih_entry_count = reiserfs_ih_get_len(ih) / (REISERFS_DEH_SIZE + min_entry_size);
        else 
            ih_entry_count = reiserfs_ih_get_entries (ih);
	
	
	for (j = 0; j < ih_entry_count; j ++, deh ++) {
	    if ((reiserfs_deh_get_did (deh) == reiserfs_key_get_did (key) || (int)reiserfs_key_get_did (key) == -1) &&
		(reiserfs_deh_get_obid (deh) == reiserfs_key_get_oid (key) || (int)reiserfs_key_get_oid (key) == -1)) {
		reiserfs_warning (log_to, "dir item %d (%H) of block %lu has "
				  "entry (%d-th) %.*s pointing to %K\n",
				  i, ih, bh->b_blocknr, j,
				  reiserfs_direntry_name_len (ih, deh, j), reiserfs_deh_name (deh, j), key);
	    }
	}	
    }
    return;
}
#endif

void do_scan (reiserfs_filsys_t * fs)
{
    unsigned long i;
    reiserfs_bh_t * bh;
    int type;
    char answer[256];
    reiserfs_key_t key = {0, 0, };
    unsigned long done, total;

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

    log_to = fopen ("scan.log", "w+");
    printf ("Log file 'scan.log' is opened\n");

    if (debug_mode (fs) == DO_SCAN_FOR_NAME) {
	if (regcomp (&pattern, name_pattern (fs), 0)) {
	    printf ("regcomp failed");
	    return;
	}

	printf ("Looking for names matching %s\n", name_pattern (fs));
	reiserfs_key_set_did (&key, 1);
    } else {
	printf ("What key do you want to find: dirid?");
	fgets(answer, sizeof(answer), stdin);
	reiserfs_key_set_did (&key, atoi (answer));

	printf ("objectid?");
	fgets(answer, sizeof(answer), stdin);
	reiserfs_key_set_oid (&key, atoi (answer));
	reiserfs_warning (stderr, "looking for (%K)\n", &key);
    }

    if (debug_mode (fs) == DO_SCAN_FOR_NAME) {
	done = 0;
	for (i = 0; i < reiserfs_sb_get_blocks (fs->fs_ondisk_sb); i ++) {
	    if (!reiserfs_bitmap_test_bit (input_bitmap (fs), i))
		continue;
	    bh = reiserfs_buffer_read (fs->fs_dev, i, fs->fs_blocksize);
	    if (!bh) {
		printf ("could not read block %lu\n", i);
		continue;
	    }
	    type = reiserfs_node_type (bh);
	    if (type == NT_LEAF || type == NT_IH_ARRAY)
		scan_for_name (bh);
	    else
		reiserfs_bitmap_clear_bit (input_bitmap (fs), i);
	    reiserfs_buffer_close (bh);
	    
	    if (!misc_test_bit(PRINT_QUIET, &data(fs)->options))
		util_misc_progress (stderr, &done, total, 1, 0);
	}
    }

    fprintf (stderr, "\n");
    if (debug_mode (fs) == DO_SCAN_FOR_NAME)
        fprintf (stderr, "There were found %d names matching the pattern \"%s\", %d names skipped\n",
            saved_names, name_pattern (fs), skipped_names);
    fflush (stderr);


    /* step 2: */
    done = 0;
    total = reiserfs_bitmap_ones (input_bitmap (fs));
    printf ("%ld bits set in bitmap\n", total);
    for (i = 0; i < reiserfs_sb_get_blocks (fs->fs_ondisk_sb); i ++) {
	int type;
	
	if (!reiserfs_bitmap_test_bit (input_bitmap (fs), i))
	    continue;
	bh = reiserfs_buffer_read (fs->fs_dev, i, fs->fs_blocksize);
	if (!bh) {
	    printf ("could not read block %lu\n", i);
	    continue;
	}
	type = reiserfs_node_type (bh);
	switch (type) {
	case NT_JDESC:
	    if (!reiserfs_key_get_did (&key))
		printf ("block %lu is journal descriptor\n", i);
	    break;
	case NT_SUPER:
	    if (!reiserfs_key_get_did (&key))
		printf ("block %lu is reiserfs super block\n", i);
	    break;
	case NT_INTERNAL:
	    if (!reiserfs_key_get_did (&key))
		printf ("block %lu is reiserfs internal node\n", i);
	    break;
	case NT_LEAF:
	case NT_IH_ARRAY:
	    scan_items (bh, (debug_mode (fs) == DO_SCAN_FOR_NAME ? NULL : &key));
	    break;
	default:
	    break;
	}

	reiserfs_buffer_close (bh);
	
	if (!misc_test_bit(PRINT_QUIET, &data(fs)->options))
	    util_misc_progress (stderr, &done, total, 1, 0);
    }
    fprintf (stderr, "\nThere were %d items saved\n", saved_items);

    /* ok, print what we found */
    /*twalk (name_index, print_file);*/

    /* create map for every file in */
    twalk (name_index, make_map);

    /* print names of files we have map of in a file 'file.list' */
    /*twalk (name_index, print_name);*/
}


