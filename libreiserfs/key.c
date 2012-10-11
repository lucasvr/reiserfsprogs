/*
 *  Copyright 2000-2004 by Hans Reiser, licensing governed by 
 *  reiserfsprogs/README
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "reiserfs/libreiserfs.h"
#include "misc/unaligned.h"

const reiserfs_key_t root_dir_key = 
	{cpu_to_le32(REISERFS_ROOT_PARENT_OBJECTID),
	 cpu_to_le32(REISERFS_ROOT_OBJECTID), 
	 {{0, 0},}};

const reiserfs_key_t parent_root_dir_key = {
	0, cpu_to_le32(REISERFS_ROOT_PARENT_OBJECTID), 
	{{0, 0},}};

reiserfs_key_t lost_found_dir_key = 
	{0, 0, {{0, 0}, }};

/* Minimal possible key. It is never in the tree. */
const reiserfs_key_t MIN_KEY = {0, 0, {{0, 0},}};

/* Maximal possible key. It is never in the tree. */
const reiserfs_key_t MAX_KEY = 
	{0xffffffff, 0xffffffff, 
	 {{0xffffffff, 0xffffffff},}};

void reiserfs_key_copy (reiserfs_key_t * to, const reiserfs_key_t * from) {
    memcpy (to, from, REISERFS_KEY_SIZE);
}

void reiserfs_key_copy2 (reiserfs_key_t * to, const reiserfs_key_t * from) {
    memcpy (to, from, REISERFS_KEY_SHSIZE);
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

static inline __u64 get_key_offset_v2 (const reiserfs_key_t *key)
{
    offset_v2_esafe_overlay tmp =
                        *(offset_v2_esafe_overlay *) (&(key->u.k2_offset_v2));
    tmp.linear = le64_to_cpu( tmp.linear );
    return tmp.offset_v2.k_offset;
}

static inline __u32 get_key_type_v2 (const reiserfs_key_t *key)
{
    offset_v2_esafe_overlay tmp =
                        *(offset_v2_esafe_overlay *) (&(key->u.k2_offset_v2));
    tmp.linear = le64_to_cpu( tmp.linear );
    return tmp.offset_v2.k_type;
}

static inline void set_key_offset_v2 (reiserfs_key_t *key, __u64 offset)
{
    offset_v2_esafe_overlay *tmp =
                        (offset_v2_esafe_overlay *)(&(key->u.k2_offset_v2));
    tmp->linear = le64_to_cpu(tmp->linear);
    tmp->offset_v2.k_offset = offset;
    tmp->linear = cpu_to_le64(tmp->linear);
}

static inline void set_key_type_v2 (reiserfs_key_t *key, __u32 type)
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
   extent) or == 0 (dir items and stat data) */

/* */
int reiserfs_key_format (const reiserfs_key_t * key)
{
    int type;

    type = get_key_type_v2 (key);

    if (is_key_format_1 (type))
	return KEY_FORMAT_1;

    return KEY_FORMAT_2;
}


unsigned long long reiserfs_key_get_off (const reiserfs_key_t * key) {
    if (reiserfs_key_format (key) == KEY_FORMAT_1)
	return reiserfs_key_get_off1 (key);

    return get_key_offset_v2 (key);
}


static int uniqueness2type (__u32 uniqueness) {
    switch (uniqueness) {
    case UNI_SD: return TYPE_STAT_DATA;
    case UNI_EXT: return TYPE_EXTENT;
    case UNI_DIRECT: return TYPE_DIRECT;
    case UNI_DE: return TYPE_DIRENTRY;
    }
    return TYPE_UNKNOWN;
}


static __u32 type2uniqueness (int type) {
    switch (type) {
    case TYPE_STAT_DATA: return UNI_SD;
    case TYPE_EXTENT: return UNI_EXT;
    case TYPE_DIRECT: return UNI_DIRECT;
    case TYPE_DIRENTRY: return UNI_DE;
    } 
    return UNI_UNKN;
}


int reiserfs_key_get_type (const reiserfs_key_t * key)
{
    int type_v2 = get_key_type_v2 (key);

    if (is_key_format_1 (type_v2))
	return uniqueness2type (reiserfs_key_get_uni (key));

    return type_v2;
}

char *key_type_name[TYPE_UNKNOWN + 1] = {
	[TYPE_STAT_DATA] = "SD",
	[TYPE_EXTENT] = "EXT",
	[TYPE_DIRECT] = "DRCT",
	[TYPE_DIRENTRY] = "DIR",
	[TYPE_UNKNOWN] = "???"
};

char * reiserfs_key_name (const reiserfs_key_t * key)
{
    __u32 type = reiserfs_key_get_type (key);
    return key_type_name[type <= TYPE_DIRENTRY ? type : TYPE_UNKNOWN];
}

/* this sets key format as well as type of item key belongs to */
void reiserfs_key_set_type (int format, reiserfs_key_t * key, int type)
{
    if (format == KEY_FORMAT_1)
	reiserfs_key_set_uni (key, type2uniqueness (type));
    else
	set_key_type_v2 (key, type);
}


void reiserfs_key_set_off (int format, reiserfs_key_t * key, 
			   unsigned long long offset)
{
    if (format == KEY_FORMAT_1)
	reiserfs_key_set_off1 (key, offset);
    else
	set_key_offset_v2 (key, offset);
	
}

/* Set secondary fields. */
void reiserfs_key_set_sec (int format, reiserfs_key_t * key, 
			   unsigned long long offset, int type)
{
    reiserfs_key_set_type (format, key, type);
    reiserfs_key_set_off (format, key, offset);
}

/*
 Compare keys using REISERFS_KEY_SHORT_LEN fields.
 Returns:  -1 if key1 < key2
            0 if key1 = key2
            1 if key1 > key2
*/

int reiserfs_key_comp2 (const void * k1, const void * k2)
{
    int n_key_length = REISERFS_KEY_SHORT_LEN;
    __u32 * p_s_key1 = (__u32 *)k1;
    __u32 * p_s_key2 = (__u32 *)k2;
    __u32 u1, u2;

    for( ; n_key_length--; ++p_s_key1, ++p_s_key2 ) {
	u1 = d32_get(p_s_key1, 0) ;
	u2 = d32_get(p_s_key2, 0) ;
	if ( u1 < u2 )
	    return -1;
	if ( u1 > u2 )
	    return 1;
    }

    return 0;
}


int reiserfs_key_comp3 (const void * p1, const void * p2)
{
    int retval;
    const reiserfs_key_t * k1 = p1;
    const reiserfs_key_t * k2 = p2;
    unsigned long long off1, off2;

    retval = reiserfs_key_comp2 (k1, k2);
    if (retval)
	return retval;
    off1 = reiserfs_key_get_off(k1) ;
    off2 = reiserfs_key_get_off(k2) ;
    if (off1 < off2)
	return -1;

    if (off1 > off2)
	return 1;

    return 0;
}


/*
 Compare keys using all 4 key fields.
 Returns:  -1 if key1 < key2
            0 if key1 = key2
            1 if key1 > key2
*/
int reiserfs_key_comp (const void * p1, const void * p2)
{
    int retval;
    const reiserfs_key_t * k1 = p1;
    const reiserfs_key_t * k2 = p2;
    __u32 u1, u2;

    retval = reiserfs_key_comp3 (k1, k2);
    if (retval)
	return retval;

    u1 = reiserfs_key_get_type (k1);
    u2 = reiserfs_key_get_type (k2);

    if (u1 < u2)
        return -1;

    if (u1 > u2)
        return 1;

    return 0;
}


