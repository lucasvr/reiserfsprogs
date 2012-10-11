/*
 * Copyright 1996-2004 by Hans Reiser, licensing governed by 
 * reiserfsprogs/README
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "reiserfs/libreiserfs.h"
#include "misc/device.h"

#include <sys/stat.h>
#include <printf.h>
#include <stdarg.h>

#if defined(HAVE_LIBUUID) && defined(HAVE_UUID_UUID_H)
#  include <uuid/uuid.h>
#endif

#define PA_KEY		(PA_LAST)
#define PA_BUFFER_HEAD	(PA_LAST + 1)
#define PA_ITEM_HEAD	(PA_LAST + 2)
#define PA_DISK_CHILD	(PA_LAST + 3)

static int _arginfo_b (const struct printf_info *info, size_t n, int *argtypes) {
    if (n > 0)
	argtypes[0] = PA_BUFFER_HEAD | PA_FLAG_PTR;
    return 1;
}

static int _arginfo_K (const struct printf_info *info, size_t n, int *argtypes) {
    if (n > 0)
	argtypes[0] = PA_KEY | PA_FLAG_PTR;
    return 1;
}

static int _arginfo_H (const struct printf_info *info, size_t n, int *argtypes) {
    if (n > 0)
	argtypes[0] = PA_ITEM_HEAD | PA_FLAG_PTR;
    return 1;
}

static int _arginfo_y (const struct printf_info *info, size_t n, int *argtypes) {
    if (n > 0)
	argtypes[0] = PA_DISK_CHILD | PA_FLAG_PTR;
    return 1;
}

static int _arginfo_M (const struct printf_info *info, size_t n, int *argtypes) {
    if (n > 0)
	argtypes[0] = PA_INT | PA_FLAG_SHORT | PA_FLAG_PTR;
    return 1;
}

static int _arginfo_U (const struct printf_info *info, size_t n, int *argtypes) {
    if (n > 0)
	argtypes[0] = (PA_CHAR|PA_FLAG_PTR);
    return 1;
}

/* %b */
static int print_block_head (FILE * stream,
			     const struct printf_info *info,
			     const void *const *args)
{
    const reiserfs_bh_t * bh;

    bh = *((const reiserfs_bh_t **)(args[0]));
    return fprintf(stream, "level=%d, nr_items=%d, free_space=%d rdkey",
		    reiserfs_node_level (bh), reiserfs_node_items (bh), 
		    reiserfs_node_free (bh));
}


/* %K */
static int print_short_key (FILE * stream,
			    const struct printf_info *info,
			    const void *const *args)
{
    const reiserfs_key_t * key;

    key = *((const reiserfs_key_t **)(args[0]));
    return fprintf(stream, "[%u %u]", reiserfs_key_get_did (key),
		    reiserfs_key_get_oid (key));
}


/* %k */
static int print_key (FILE * stream,
		      const struct printf_info *info,
		      const void *const *args)
{
    const reiserfs_key_t * key;

    key = *((const reiserfs_key_t **)(args[0]));
    return fprintf(stream, "[%u %u 0x%Lx %s (%d)]",  
		    reiserfs_key_get_did (key), reiserfs_key_get_oid (key),
		    (unsigned long long)reiserfs_key_get_off (key), 
		    reiserfs_key_name (key), reiserfs_key_get_type (key));
}


/* %H */
static int print_item_head (FILE * stream,
			    const struct printf_info *info,
			    const void *const *args)
{
    const reiserfs_ih_t * ih;

    ih = *((const reiserfs_ih_t **)(args[0]));
    return fprintf(stream, "%u %u 0x%Lx %s (%d), len %u, location "
		   "%u entry count %u, fsck need %u, format %s",
		   reiserfs_key_get_did (&ih->ih_key), 
		   reiserfs_key_get_oid (&ih->ih_key),
		    (unsigned long long)reiserfs_key_get_off (&ih->ih_key), 
		    reiserfs_key_name (&ih->ih_key),
		    reiserfs_key_get_type (&ih->ih_key), 
		    reiserfs_ih_get_len (ih), reiserfs_ih_get_loc (ih),
		    reiserfs_ih_get_entries (ih), reiserfs_ih_get_flags (ih),
		    reiserfs_ih_get_format (ih) == KEY_FORMAT_2 ? "new" : 
		    ((reiserfs_ih_get_format (ih) == KEY_FORMAT_1) ? "old" : "BAD"));
}


static int print_disk_child (FILE * stream,
			     const struct printf_info *info,
			     const void *const *args)
{
    const reiserfs_dc_t * dc;

    dc = *((const reiserfs_dc_t **)(args[0]));
    return fprintf(stream, "[dc_number=%u, dc_size=%u]", 
		   reiserfs_dc_get_nr (dc), reiserfs_dc_get_size (dc));
}

static int rwx (FILE * stream, mode_t mode)
{
    return fprintf (stream, "%c%c%c",
		    (mode & S_IRUSR) ? 'r' : '-',
		    (mode & S_IWUSR) ? 'w' : '-',
		    (mode & S_IXUSR) ? 'x' : '-');
}


/* %M */
static int print_sd_mode (FILE * stream,
			  const struct printf_info *info,
			  const void *const *args)
{
    int len = 0;
    __u16 mode;

    mode = *(mode_t *)args[0];
    len = fprintf (stream, "%c", misc_device_typec (mode));
    len += rwx (stream, (mode & 0700) << 0);
    len += rwx (stream, (mode & 0070) << 3);
    len += rwx (stream, (mode & 0007) << 6);
    return len;
}

/* %U */
static int print_sd_uuid (FILE * stream,
			  const struct printf_info *info,
			  const void *const *args)
{
#if defined(HAVE_LIBUUID) && defined(HAVE_UUID_UUID_H)
    const unsigned char *uuid = *((const unsigned char **)(args[0]));
    char buf[37];

    buf[36] = '\0';
    uuid_unparse(uuid, buf);
    return fprintf(stream, "%s", buf);
#else
    return fprintf(stream, "<no libuuid installed>");
#endif
}

void reiserfs_warning (FILE * fp, const char * fmt, ...) 
{
    static int registered = 0;
    va_list args;

    if (!registered) {
	registered = 1;
	
	register_printf_function ('K', print_short_key, _arginfo_K);
	register_printf_function ('k', print_key, _arginfo_K);
	register_printf_function ('H', print_item_head, _arginfo_H);
	register_printf_function ('b', print_block_head, _arginfo_b);
	register_printf_function ('y', print_disk_child, _arginfo_y);
	register_printf_function ('M', print_sd_mode, _arginfo_M);
	register_printf_function ('U', print_sd_uuid, _arginfo_U);
    }

    va_start (args, fmt);
    vfprintf (fp, fmt, args);
    va_end (args);
}

