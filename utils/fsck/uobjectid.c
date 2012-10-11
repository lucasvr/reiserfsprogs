/*
 * Copyright 1996-2004 by Hans Reiser, licensing governed by 
 * reiserfsprogs/README
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "fsck.h"
#include "misc/malloc.h"


/* when --check fsck builds a map of objectids of files it finds in the tree
   when --rebuild-tree - fsck builds map of objectids it inserts into tree */

#define ALLOC_SIZE			1024
#define MAX_ID				(~(__u32)0)

/* 2 bytes for the counter */
#define BM_SIZE				(ALLOC_SIZE - sizeof(__u16))
#define BM_INTERVAL			(BM_SIZE * 8)
#define INDEX_COUNT			(MAX_ID / BM_INTERVAL)

#define id_map_interval(map, id)	(map->index + (id / BM_INTERVAL))

#define id_map_local_count(interval)	(interval + BM_SIZE)

/*
typedef struct sb_id_map {
    __u32 * m_begin;
    __u32 m_size, m_used_slot_count;
} sb_id_map_t;
*/

id_map_t *id_map_init() {
    id_map_t *map;
    __u32 i;
 
    map = misc_getmem(sizeof(id_map_t));
    map->index = misc_malloc(INDEX_COUNT * sizeof(void *));

    for (i = 0; i < INDEX_COUNT; i++) {
	if (map->index[i] != (void *)0)
	    map->index[i] = (void *)0;
    }

    id_map_mark(map, 0);
    id_map_mark(map, 1);

    /* id == 0 should not be there, just for convinient usage */
    map->count--;
    
    return map;
}

void id_map_free(id_map_t *map) {
    __u32 i;
 
    for (i = 0; i < INDEX_COUNT; i++) {
	if (map->index[i] != (void *)0 && map->index[i] != (void *)1)
	    misc_freemem(map->index[i]);
    }
    
    misc_freemem(map->index);
    misc_freemem(map);
}

int id_map_test(id_map_t *map, __u32 id) {
    void **interval = id_map_interval(map, id);

    if (*interval == (void *)0)
	return 0;
    
    if (*interval == (void *)1)
	return 1;
    
    return misc_test_bit(id % BM_INTERVAL, *interval);
}

int id_map_mark(id_map_t *map, __u32 id) {
    void **interval = id_map_interval(map, id);

    if (*interval == (void *)0)
	*interval = misc_getmem(ALLOC_SIZE);

    if (*interval == (void *)1)
	return 1;

    if (misc_test_bit(id % BM_INTERVAL, *interval))
	return 1;
	
    misc_set_bit(id % BM_INTERVAL, *interval);
    
    (*(__u16 *)id_map_local_count(*interval))++;
    map->count++;

    if ((*(__u16 *)id_map_local_count(*interval)) == BM_INTERVAL) {
	/* Dealloc fully used bitmap */
	misc_freemem(*interval);
	*interval = (void *)1;
    }

    if (map->last_used < (id / BM_INTERVAL))
	map->last_used = id / BM_INTERVAL;
    
    return 0;
}

/* call this for proper_id_map only!! */
__u32 id_map_alloc(id_map_t *map) {
    __u32 i, zero_count;
    __u32 id = 0, first = ~(__u32)0;

    for (i = 0, zero_count = 0; zero_count < 10 && i < INDEX_COUNT - 1; i++) {
	if (map->index[i] == (void *)0) {
	    if (zero_count == 0)
		first = i;
	    
	    zero_count++;
	} else if (map->index[i] != (void *)1)
	    break;
    }

    if (map->index[i] != (void *)1 && map->index[i] != (void *)0) {
	id = misc_find_first_zero_bit(map->index[i], BM_INTERVAL);
	if (id >= BM_INTERVAL)
	    misc_die ("Id is out of interval size, interval looks corrupted.");
	
	id += i * BM_INTERVAL;
    } else if (first != ~(__u32)0) {
	id = first * BM_INTERVAL;
	if (id == 0) 
	    id = 2;
    } else 
	misc_die ("%s: No more free objectid is available.", __FUNCTION__);

    id_map_mark(map, id);

    return id;
}

/* this could be used if some more sofisticated flushing will be needed. */
/*
static void sb_id_map_pack(sb_id_map_t *map) {
    map->m_begin[1] = map->m_begin[map->m_used_slot_count - 1];
    memset(map->m_begin + 2, 0, map->m_used_slot_count - 2);
    map->m_used_slot_count = 2;
}*/

static __u32 id_map_next_bound(id_map_t *map, __u32 start) {
    __u32 index = start / BM_INTERVAL;
    __u32 offset = 0;
    int look_for;
    
    if (map->index[index] == (void *)0)
	look_for = 1;
    else if (map->index[index] == (void *)1)
	look_for = 0;
    else {
	offset = start % BM_INTERVAL;
	look_for = !misc_test_bit(offset, map->index[index]);
	offset++;
    }

start_again:
    
    if (look_for) {	
	while (index < INDEX_COUNT && map->index[index] == (void *)0) {
	    index++;
	}
	
	if (index == INDEX_COUNT)
	    return 0;
	
	if (map->index[index] == (void *)1)
	    return index * BM_INTERVAL;
	
	offset = misc_find_next_set_bit(map->index[index], 
					BM_INTERVAL, offset);

	if (offset >= BM_INTERVAL) {
	    offset = 0;
	    index++;
	    goto start_again;
	}
    } else {
	while (index < INDEX_COUNT && map->index[index] == (void *)1)
	    index++;
	
	if (index == INDEX_COUNT)
	    return 0;
	
	if (map->index[index] == (void *)0)
	    return index * BM_INTERVAL;

	offset = misc_find_next_zero_bit(map->index[index], 
					 BM_INTERVAL, offset);
	
	if (offset >= BM_INTERVAL) {
	    offset = 0;
	    index++;
	    goto start_again;
	}
    }

    return index * BM_INTERVAL + offset;
}

void id_map_flush(struct id_map * map, reiserfs_filsys_t * fs) {
    int size, max, i;
    __u32 id, prev_id;
    __u32 * sb_objectid_map;

    size = reiserfs_super_size (fs->fs_ondisk_sb);
    sb_objectid_map = (__u32 *)((char *)(fs->fs_ondisk_sb) + size);

    max = ((fs->fs_blocksize - size) >> 3 << 1);
    reiserfs_sb_set_mapmax (fs->fs_ondisk_sb, max);
    
    id = 1;
    sb_objectid_map[0] = cpu_to_le32(0);

    for (i = 1; i < max - 1; i++) {
	id = id_map_next_bound(map, id);
	sb_objectid_map[i] = cpu_to_le32(id);
	if (id == 0) {
	    if (i % 2) {
		misc_die ("%s: Used interval is not closed "
			  "on flushing.", __FUNCTION__);
	    }
	    
	    break;
	}
    }

    if (map->index[map->last_used] == (void *)0) {
	misc_die ("Object id map looks corrupted - last "
		  "used interval cannot be zeroed.");
    }
    
    i++;
    
    if (i == max) {
	if (id == 0) {
	    misc_die ("Objectid interval does not contain "
		      "any set bit what is expected.");
	}
	
	if (map->index[map->last_used] == (void *)1) {
	    prev_id = BM_INTERVAL - 1;
	} else {	    
	    prev_id = ~(__u32)0;
	    
	    if (id < map->last_used * BM_INTERVAL)
		id = 0;
	    else 
		id %= BM_INTERVAL;
	    
	    if (misc_test_bit(id, map->index[map->last_used]))
		prev_id = id;
	    
	    while ((id = misc_find_next_set_bit(map->index[map->last_used], 
		BM_INTERVAL, (id + 1))) != BM_INTERVAL) 
	    {
		prev_id = id;
	    }

	    if (prev_id == ~(__u32)0) {
		misc_die ("Objectid interval does not contain "
			  "any set bit what is expected.");
	    }

	    prev_id++;
	}
	
	sb_objectid_map[max - 1] = 
		cpu_to_le32(prev_id + map->last_used * BM_INTERVAL);
    } else {
	i--;
	memset(sb_objectid_map + i, 0, (max - i) * sizeof (__u32));
    }

    reiserfs_sb_set_mapcur (fs->fs_ondisk_sb, i);
}


void fetch_objectid_map (struct id_map * map, 
			 reiserfs_filsys_t * fs) 
{
    __u32 i, j, sb_size;
    __u32 * sb_objectid_map;

    sb_size = reiserfs_super_size (fs->fs_ondisk_sb);
    sb_objectid_map = (__u32 *)((char *)(fs->fs_ondisk_sb) + sb_size);

    if (map == NULL)
	misc_die ("Not map given");
    
    for (i = 0; i < reiserfs_sb_get_mapcur(fs->fs_ondisk_sb); i+=2) {
	    for (j = sb_objectid_map[i]; j <  sb_objectid_map[i + 1]; j++) {
		    id_map_mark(map, j);
		    if (j == 0)
			    map->count--;
	    }
    }
}

#define OBJMAP_START_MAGIC 375331
#define OBJMAP_END_MAGIC 7700472

void reiserfs_objectid_map_save (FILE * fp, struct id_map * id_map) {
    __u32 v, count;

    v = OBJMAP_START_MAGIC;
    fwrite (&v, 4, 1, fp);

    v = 0;
    count = 0;
    /* Write 0. */
    fwrite (&v, 4, 1, fp);
    count++;
    while ((v = id_map_next_bound(id_map, v))) {
	    fwrite (&v, 4, 1, fp);
	    count++;
    }
    
    fwrite (&count, 4, 1, fp);
    
    v = OBJMAP_END_MAGIC;
    fwrite (&v, 4, 1, fp);
}

struct id_map * reiserfs_objectid_map_load (FILE * fp) {
    __u32 v, count, i, prev, next;
    struct id_map * id_map;
    int skip, odd;

    id_map = id_map_init();
    
    fread (&v, 4, 1, fp);
    if (v != OBJMAP_START_MAGIC) {
	reiserfs_warning (stderr, "reiserfs_objectid_map_load: no "
			  "objectid map begin magic found");
	goto error;
    }

    count = 0;
    skip = 0;
    odd = 0;
    prev = 0;
    while (1) {
	    if (skip) {
		    skip = 0;
		    v = next;
	    } else if (fread(&v, 4, 1, fp) != 1) {
		    reiserfs_warning (stderr, "%s: failed to read the given", 
				      __FUNCTION__);
		    goto error;
	    }
	    
	    if (v == count) {
		    /* Check if the magic goes after. */
		    if (fread(&next, 4, 1, fp) != 1) {
			    reiserfs_warning (stderr, "%s: failed to read the given", 
				      __FUNCTION__);
			    goto error;
		    }

		    if (next != OBJMAP_END_MAGIC)
			    skip = 1;
		    else break;
	    }

	    if (odd) {
		    for (i = prev; i < v; i++) {
			    id_map_mark(id_map, i);
			    if (i == 0)
				    id_map->count--;
		    }
	    } 
	    
	    prev = v;
	    
	    odd = odd ? 0 : 1;
	    count++;
    }
    
    return id_map;
    
 error:
    fflush (stderr);
    id_map_free(id_map);
    return NULL;
}
