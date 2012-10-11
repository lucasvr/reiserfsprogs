/*
 * Copyright 1996-2002 Hans Reiser
 */
#include "fsck.h"


/* when --check fsck builds a map of objectids of files it finds in the tree
   when --rebuild-tree - fsck builds map of objectids it inserts into tree
   FIXME: objectid gets into map when stat data item
*/


#if 0
/* is it marked used in super block's objectid map. fixme: binary search could
   be used here */
int is_objectid_used (reiserfs_filsys_t * fs, __u32 objectid)
{
    __u32 * objectid_map;
    int i = 0;


    objectid_map = (__u32 *)((char *)(fs->fs_ondisk_sb) + (sb_size(fs)));
  
    while (i < get_sb_oid_cursize (fs->fs_ondisk_sb)) {
	if (objectid == le32_to_cpu (objectid_map[i])) {
	    return 1;      /* objectid is used */
	}
	
	if (objectid > le32_to_cpu(objectid_map[i]) &&
             objectid < le32_to_cpu(objectid_map[i+1])) {
	    return 1;	/* objectid is used */
	}
	
	if (objectid < le32_to_cpu(objectid_map[i]))
	    break;

	i += 2;
    }
    
    /* objectid is free */
    return 0;
}
#endif



/* true objectid map */


/* increase area by MAP_SIZE bytes */
static void grow_id_map (struct id_map * map)
{
/*
    if (map->m_page_count && ((map->m_page_count % 5) == 0)) {
	fsck_log ("grow_id_map: objectid map expanded: used %lu, %d blocks\n",
		  map->m_used_slots_count, map->m_page_count);
    }
*/
    map->m_begin = expandmem (map->m_begin, map->m_page_count * MAP_SIZE, MAP_SIZE);
    map->m_page_count ++;
}


static void try_to_shrink_id_map (struct id_map * map)
{
    if (map->m_used_slots_count * sizeof(__u32) <= (map->m_page_count - 1) * MAP_SIZE) {
	/*
	if (map->m_page_count && ((map->m_page_count % 5) == 0))
	    fsck_log ("shrink_id_map: objectid map shrinked: used %lu, %d blocks\n",
		      map->m_used_slots_count, map->m_page_count);
	*/
	map->m_begin = expandmem (map->m_begin, map->m_page_count * MAP_SIZE,
				  -MAP_SIZE);
        map->m_page_count--;
    }
}


/* ubin_search_id is used to find id in the map (or proper place to
   insert the new id).  if smth goes wrong or ubin_search_id stops
   working properly check_id_search_result should help to find raised
   problems */
static void check_id_search_result(struct id_map * map, int res, __u32 pos,
				   __u32 id)
{
    if (res != POSITION_FOUND && res != POSITION_NOT_FOUND)
        die("check_id_search_result: get wrong result from ubin_search (%d)", res);

    if (res == 1 && le32_to_cpu (map->m_begin[pos]) != id)
        die("check_id_search_result: wrong id found %u %u", id, le32_to_cpu(map->m_begin[pos]));

    if (res == 1)
    {
        if (pos > map->m_used_slots_count)
            die("check_id_search_result: get bad position (%u), used %u",
		pos, map->m_used_slots_count);
        if (pos >= 0 && pos <= map->m_used_slots_count && le32_to_cpu(map->m_begin[pos - 1]) >= id)
            die("check_id_search_result: previous id (%u) not less than (%u)",
		le32_to_cpu(map->m_begin[pos - 1]), id);
        if (pos >= 0 && pos < map->m_used_slots_count && le32_to_cpu(map->m_begin[pos]) < id)
            die("check_id_search_result: found id (%u) not much than (%u)",
		le32_to_cpu(map->m_begin[pos]), id);
    }
}

/* */
struct id_map * init_id_map (void)
{
    struct id_map * map;

    map = getmem (sizeof (struct id_map));
    map->m_begin = NULL;
    map->m_used_slots_count = 0;
    map->m_page_count = 0;
    mark_objectid_really_used (map, 1);
    return map;
}


/* free whole map */
void free_id_map (struct id_map * map)
{
    freemem (map->m_begin);
    freemem (map);
}


/* return 1 if id is marked used, 0 otherwise */
int is_objectid_really_used (struct id_map * map, __u32 id, __u32 * ppos)
{
    int res;
    __u32 le_id = cpu_to_le32 (id);

    *ppos = 0;

    if (map->m_begin == NULL)
        return 0;

    /* smth exists in the map, find proper place to insert or this id */
    res = reiserfs_bin_search (&le_id, map->m_begin, map->m_used_slots_count,
			       sizeof (__u32), ppos, comp_ids);
#if 1
    check_id_search_result (map, res, *ppos, id);
#endif
    /* *ppos is position in objectid map of the element which is equal id
       or position of an element which is smallest and greater than id */
    if (res == POSITION_NOT_FOUND)
	/* id is not found in the map. if returned position is odd -
	   id is marked used */
	return (*ppos % 2);

    /* if returned position is odd - id is marked free */
    return !(*ppos % 2);
}

static void check_objectid_map (struct id_map * map, int pos)
{
    /* check only previous id, current and 2 next ones,
       if not specified to check evth */
    int count = 3;
    int i;
    int first = pos;

    if (first == 0) {
    	first = 1;    	
    } else if (first == -1) {
	count = map->m_used_slots_count - 1;
    	first = 1;
    }

    if (first + count > map->m_used_slots_count)
        count = map->m_used_slots_count - first;

    for (i = first; i < first + count; i ++)
	if (le32_to_cpu(map->m_begin [i - 1]) >= le32_to_cpu(map->m_begin [i]))
	    die ("check_objectid_map: map corrupted");
}

int __mark_objectid_really_used(struct id_map *map, __u32 id, __u32 pos) {

    map->objectids_marked ++;
    if (pos % 2 == 0){
        /* id not found in the map. why? is_id_used() knows */

        if (map->m_begin == NULL)
	    /* map is empty */
            grow_id_map (map);

        /* id + 1 is used, change id + 1 to id and exit */
        if ( id + 1 == le32_to_cpu (map->m_begin[pos]) ) {
	    /* we can mark id as used w/o expanding of id map */
	    map->m_begin[pos] = cpu_to_le32 (id);

	    check_objectid_map (map, pos);
	    return 0;
        }

        if (map->m_page_count * MAP_SIZE == map->m_used_slots_count * sizeof(__u32))
	    /* fixme: do not grow too much */
            grow_id_map (map);

        if (map->m_used_slots_count - pos > 0)
            memmove (map->m_begin + pos + 2, map->m_begin + pos, (map->m_used_slots_count - pos) * sizeof (__u32));

        map->m_used_slots_count += 2;
        map->m_begin[pos] = cpu_to_le32 (id);
        map->m_begin[pos+1] = cpu_to_le32 (id + 1);

	check_objectid_map (map, pos);

        return 0;
    }

    /* id found in the map. pos is odd position () */
    map->m_begin[pos] = cpu_to_le32 (id + 1);
    
    /* if end id of current interval == start id of next interval we
       eliminated a sequence of unused objectids */
    if (pos + 1 < map->m_used_slots_count &&
	map->m_begin[pos + 1] == map->m_begin[pos]) { /* safe, both are le */
	memmove (map->m_begin + pos, map->m_begin + pos + 2, (map->m_used_slots_count - pos - 2) * sizeof (__u32));
	map->m_used_slots_count -= 2;
	try_to_shrink_id_map (map);
    }

    check_objectid_map (map, pos);

    return 0;
}

/* returns 1 objectid is marked used already, 0 otherwise */
int mark_objectid_really_used (struct id_map * map, __u32 id)
{
    __u32 pos;

    /* check whether id is used and get place if used or place to insert if not */
    if (is_objectid_really_used (map, id, &pos) == 1)
        return 1;

    return __mark_objectid_really_used(map, id, pos) ;
}


static __u32 get_free_id (reiserfs_filsys_t * fs)
{
    struct id_map * map;

    map = proper_id_map (fs);

    /* If map is not NULL return the second element (first position in
       the map).  This allocates the first unused objectid. That is to
       say, the first entry on the objectid map is the first unused
       objectid. */
    if (map->m_begin == NULL) {
	fprintf (stderr, "get_free_id: hmm, 1 is allocated as objectid\n");
	return 1;
    }
    return (le32_to_cpu (map->m_begin[1]));
}


__u32 get_unused_objectid (reiserfs_filsys_t * fs)
{
    __u32 objectid;

    objectid = get_free_id (fs);
    if (mark_objectid_really_used (proper_id_map (fs), objectid))
	die ("get_unused_objectid: could not mark %lu used", 
	     ( long unsigned ) objectid);

    return objectid;
}


#define objectid_map(fs) ((char *)((char *)((fs)->s_rs) + sb_size (fs)))


#if 0
/* returns 0 if on-disk objectid map matches to the correct one, 1
   otherwise */
int compare_id_maps (reiserfs_filsys_t * fs)
{
    struct id_map * map;
    int disk_size;
  
    map = proper_id_map (fs);
    
    disk_size = rs_objectid_map_size (fs->s_rs);
    if (disk_size != map->m_used_slots_count ||
	memcmp ((char *)((char *)((fs)->s_rs) + sb_size (fs)), map->m_begin, sizeof(__u32) * disk_size)) {
	fprintf (stderr, "Objectid maps mismatch\n");
	return 1;
    }

    return 0;
}


/* copy objectid map into buffer containing super block */
void correct_objectid_map (reiserfs_filsys_t * fs)
{
    struct id_map * map;
    int size, disk_max;
 
    map = proper_id_map (fs);
    
    size = map->m_used_slots_count;
    disk_max = rs_objectid_map_max_size (fs->s_rs);
    if (disk_max < size) {
	size = disk_max;
    } else {
	memset (fu_objectid_map (fs) + size, 0, (disk_max - size) * sizeof (__u32));
    }
    
    memcpy (fu_objectid_map (fs), map->m_begin, size * sizeof (__u32));
    set_objectid_map_size (fs->s_rs, size);
    mark_buffer_dirty (SB_BUFFER_WITH_SB (fs));

/*
    if (fs->fu_job->verbose)
	fprintf (stderr, "Objectid map corrected\n");
*/
}
#endif


/* print the map of objectids */
void print_objectid_list (__u32 *map, int count)
{
    int i;
    for (i = 0; i < count ; i += 2)
           printf ("\n[%u-%u]", le32_to_cpu(map[i]),le32_to_cpu(map[i+1]));
}

#if 0
/* print on-disk map of objectids */
void print_disk_objectid_list (void)
{
    int i;
    __u32 * objectid_map = (__u32 *)((char *)SB_DISK_SUPER_BLOCK (&g_sb) + (sb_size(&g_sb)));
    printf ("\n on-disk id map. used:%lu", SB_OBJECTID_MAP_SIZE(&g_sb));

    for (i = 0; i < SB_OBJECTID_MAP_SIZE(&g_sb); i += 2)
	printf ("\n[%u-%u]", objectid_map[i], objectid_map[i + 1] - 1);
}
#endif


void flush_objectid_map (struct id_map * map, reiserfs_filsys_t * fs)
{
    int size, max;
    int sb_size;
    __u32 * sb_objectid_map;

    sb_size = reiserfs_super_block_size (fs->fs_ondisk_sb);
    sb_objectid_map = (__u32 *)((char *)(fs->fs_ondisk_sb) + sb_size);

    max = ((fs->fs_blocksize - sb_size) >> 3 << 1);
    set_sb_oid_maxsize (fs->fs_ondisk_sb, max);
    if (map->m_used_slots_count > max)
	size = max;
    else
	size = map->m_used_slots_count;

    check_objectid_map (map, -1);

    memcpy (sb_objectid_map, map->m_begin, size * sizeof (__u32));
    memset (sb_objectid_map + size, 0, (max - size) * sizeof (__u32));

    set_sb_oid_cursize (fs->fs_ondisk_sb, size);
    if (size == max)
	sb_objectid_map [max - 1] = map->m_begin [map->m_used_slots_count - 1];

//    check_objectid_map (map);

}


void fetch_objectid_map (struct id_map * map, reiserfs_filsys_t * fs)
{
    int sb_size;
    __u32 * sb_objectid_map;

    sb_size = reiserfs_super_block_size (fs->fs_ondisk_sb);
    sb_objectid_map = (__u32 *)((char *)(fs->fs_ondisk_sb) + sb_size);

    if (map->m_page_count != 1)
	die ("fetch_objectid_map: can not fetch long map");
    grow_id_map (map);
    memcpy (map->m_begin, sb_objectid_map, get_sb_oid_cursize (fs->fs_ondisk_sb) * sizeof (__u32));
    map->m_used_slots_count = get_sb_oid_cursize (fs->fs_ondisk_sb);
}

#define OBJMAP_START_MAGIC 375331
#define OBJMAP_END_MAGIC 7700472

void reiserfs_objectid_map_save (FILE * fp, struct id_map * id_map)
{
    __u32 v;
    int i;

    v = OBJMAP_START_MAGIC;
    fwrite (&v, 4, 1, fp);

    v = id_map->m_used_slots_count;
    fwrite (&v, 4, 1, fp);

    for (i = 0; i < id_map->m_page_count - 1; i++) {
	fwrite ((char *)id_map->m_begin + MAP_SIZE * i, 4, 1, fp);
    }

    for (i = 0; i < id_map->m_used_slots_count * sizeof(__u32) - (id_map->m_page_count - 1) * MAP_SIZE; i++) {
	fwrite ((char *)id_map->m_begin + MAP_SIZE * (id_map->m_page_count - 1) + i, 4, 1, fp);
    }

    v = OBJMAP_END_MAGIC;
    fwrite (&v, 4, 1, fp);
}

struct id_map * reiserfs_objectid_map_load (FILE * fp)
{
    __u32 v;
    int i;
    struct id_map * id_map;

    fread (&v, 4, 1, fp);
    if (v != OBJMAP_START_MAGIC) {
	reiserfs_warning (stderr, "reiserfs_objectid_map_load: no objectid map begin magic found");
	return 0;
    }
	
    /* read bit size of objectid map */
    fread (&v, 4, 1, fp);

    id_map = init_id_map ();

    id_map->m_used_slots_count = v;
    id_map->m_page_count = v / MAP_SIZE + 1;

    id_map->m_begin = expandmem (id_map->m_begin, 0, id_map->m_page_count * MAP_SIZE);

    for (i = 0; i < id_map->m_page_count - 1; i++) {
	fread ((char *)id_map->m_begin + MAP_SIZE * i, 4, 1, fp);
    }

    for (i = 0; i < id_map->m_used_slots_count * sizeof(__u32) - (id_map->m_page_count - 1) * MAP_SIZE; i++) {
	fread ((char *)id_map->m_begin + MAP_SIZE * (id_map->m_page_count - 1) + i, 4, 1, fp);
    }

    fread (&v, 4, 1, fp);

    if (v != OBJMAP_END_MAGIC) {
	reiserfs_warning (stderr, "reiserfs_objectid_map_load: no objectid map end magic found");
	return 0;
    }

    fflush (stderr);
    return id_map;
}

