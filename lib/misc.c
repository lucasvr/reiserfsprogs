/*
 * Copyright 1996-2002 Hans Reiser
 */

/* for stat64() */
#define _FILE_OFFSET_BITS 64

/* for getline() proto and _LARGEFILE64_SOURCE */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <asm/types.h>
#include <stdlib.h>
#include <mntent.h>
#include <sys/vfs.h>
#include <fcntl.h>
#include <time.h>
#include <utime.h>
#include <ctype.h>
#include <linux/hdreg.h>
#include <dirent.h>

#include <unistd.h>
//#include <linux/unistd.h>
//#include <sys/stat.h>

#if defined(__linux__) && defined(_IOR) && !defined(BLKGETSIZE64)
#   define BLKGETSIZE64 _IOR(0x12, 114, sizeof(__u64))
#endif
    

#include "swab.h"

#include "io.h"
#include "misc.h"

/* Debian modifications by Ed Boraas <ed@debian.org> */
#include <sys/mount.h>
/* End Debian mods */


void die (char * fmt, ...)
{
    static char buf[1024];
    va_list args;

    va_start (args, fmt);
    vsprintf (buf, fmt, args);
    va_end (args);

    fprintf (stderr, "\n%s\n", buf);
    abort ();
}


#define MEM_BEGIN "_mem_begin_"
#define MEM_END "mem_end"
#define MEM_FREED "__free_"
#define CONTROL_SIZE (strlen (MEM_BEGIN) + 1 + sizeof (int) + strlen (MEM_END) + 1)


int get_mem_size (char * p)
{
    char * begin;

    begin = p - strlen (MEM_BEGIN) - 1 - sizeof (int);
    return *(int *)(begin + strlen (MEM_BEGIN) + 1);
}


void checkmem (char * p, int size)
{
    char * begin;
    char * end;
  
    begin = p - strlen (MEM_BEGIN) - 1 - sizeof (int);
    if (strcmp (begin, MEM_BEGIN))
	die ("checkmem: memory corrupted - invalid head sign");

    if (*(int *)(begin + strlen (MEM_BEGIN) + 1) != size)
	die ("checkmem: memory corrupted - invalid size");

    end = begin + size + CONTROL_SIZE - strlen (MEM_END) - 1;
    if (strcmp (end, MEM_END))
	die ("checkmem: memory corrupted - invalid end sign");
}


void * getmem (int size)
{
    char * p;
    char * mem;

    p = (char *)malloc (CONTROL_SIZE + size);
    if (!p)
	die ("getmem: no more memory (%d)", size);

    strcpy (p, MEM_BEGIN);
    p += strlen (MEM_BEGIN) + 1;
    *(int *)p = size;
    p += sizeof (int);
    mem = p;
    memset (mem, 0, size);
    p += size;
    strcpy (p, MEM_END);

//    checkmem (mem, size);

    return mem;
}


void * expandmem (void * vp, int size, int by)
{
    int allocated;
    char * mem, * p = vp;
    int expand_by = by;

    if (p) {
	checkmem (p, size);
	allocated = CONTROL_SIZE + size;
	p -= (strlen (MEM_BEGIN) + 1 + sizeof (int));
    } else {
	allocated = 0;
	/* add control bytes to the new allocated area */
	expand_by += CONTROL_SIZE;
    }
    p = realloc (p, allocated + expand_by);
    if (!p)
	die ("expandmem: no more memory (%d)", size);
    if (!vp) {
	strcpy (p, MEM_BEGIN);
    }
    mem = p + strlen (MEM_BEGIN) + 1 + sizeof (int);

    *(int *)(p + strlen (MEM_BEGIN) + 1) = size + by;
    /* fill new allocated area by 0s */
    if(by > 0)
        memset (mem + size, 0, by);
    strcpy (mem + size + by, MEM_END);
//    checkmem (mem, size + by);

    return mem;
}


void freemem (void * vp)
{
    char * p = vp;
    int size;
  
    if (!p)
	return;
    size = get_mem_size (vp);
    checkmem (p, size);

    p -= (strlen (MEM_BEGIN) + 1 + sizeof (int));
    strcpy (p, MEM_FREED);
    strcpy (p + size + CONTROL_SIZE - strlen (MEM_END) - 1, MEM_FREED);
    free (p);
}


typedef int (*func_t) (char *);

static int is_readonly_dir (char * dir)
{
/*
    int fd;
    char template [1024];

    snprintf (template, 1024, "%s/testXXXXXX", dir);
    fd = mkstemp (template);
    if (fd >= 0) {
	close (fd);
	return 0;
    }
*/
    if (utime (dir, 0) != -1)
	/* this is not ro mounted fs */
	return 0;
    return (errno == EROFS) ? 1 : 0;
}


#ifdef __i386__

#include <unistd.h>
#include <linux/unistd.h>

#define __NR_bad_stat64 195
_syscall2(long, bad_stat64, char *, filename, struct stat64 *, statbuf);

#else

#define bad_stat64 stat64

#endif

/* yes, I know how ugly it is */
#define return_stat_field(field) \
    struct stat st;\
    struct stat64 st64;\
\
    if (bad_stat64 (file_name, &st64) == 0) {\
	return st64.st_##field;\
    } else if (stat (file_name, &st) == 0)\
	return st.st_##field;\
\
    perror ("stat failed");\
    exit (8);\


mode_t get_st_mode (char * file_name)
{
    return_stat_field (mode);
}


/* may I look at this undocumented (at least in the info of libc 2.3.1-58)
   field? */
dev_t get_st_rdev (char * file_name)
{
    return_stat_field (rdev);
}


off64_t get_st_size (char * file_name)
{
    return_stat_field (size);
}


blkcnt64_t get_st_blocks (char * file_name)
{
    return_stat_field (blocks);
}



static int _is_mounted (char * device_name, func_t f)
{
    int retval;
    FILE *fp;
    struct mntent *mnt;
    struct statfs stfs;
    struct stat root_st;
    mode_t mode;

    if (stat ("/", &root_st) == -1)
	die ("is_mounted: could not stat \"/\": %m\n");


    mode = get_st_mode (device_name);
    if (S_ISREG (mode))
	/* regular file can not be mounted */
	return 0;

    if (!S_ISBLK (mode))
	die ("is_mounted: %s is neither regular file nor block device", device_name);

    if (root_st.st_dev == get_st_rdev (device_name)) {
	/* device is mounted as root. Check whether it is mounted read-only */
	return (f ? f ("/") : 1);
    }

    /* if proc filesystem is mounted */
    if (statfs ("/proc", &stfs) == -1 || stfs.f_type != 0x9fa0/*procfs magic*/ ||
	(fp = setmntent ("/proc/mounts", "r")) == NULL) {
	/* proc filesystem is not mounted, or /proc/mounts does not
           exist */
	if (f)
	    return (user_confirmed (stderr, " (could not figure out) Is filesystem mounted read-only? (Yes)",
				    "Yes\n"));
	else
	    return (user_confirmed (stderr, " (could not figure out) Is filesystem mounted? (Yes)",
				    "Yes\n"));
    }
    
    retval = 0;
    while ((mnt = getmntent (fp)) != NULL)
	if (strcmp (device_name, mnt->mnt_fsname) == 0) {
	    retval = (f ? f (mnt->mnt_dir) : 1/*mounted*/);
	    break;
	}
    endmntent (fp);

    return retval;
}


int is_mounted_read_only (char * device_name)
{
    return _is_mounted (device_name, is_readonly_dir);
}


int is_mounted (char * device_name)
{
    return _is_mounted (device_name, 0);
}


char buf1 [100];
char buf2 [100];

void print_how_fast (unsigned long passed, unsigned long total,
		     int cursor_pos, int reset_time)
{
    static time_t t0 = 0, t1 = 0, t2 = 0;
    int speed;
    int indent;

    if (reset_time)
	time (&t0);

    time (&t1);
    if (t1 != t0) {
	speed = passed / (t1 - t0);
	if (total - passed) {
	    if (t1 - t2 < 1)
	        return;
	    t2 = t1;
	}	
    } else
	speed = 0;

    /* what has to be written */
    if (total)
      sprintf (buf1, "left %lu, %d /sec", total - passed, speed);
    else {
	/*(*passed) ++;*/
	sprintf (buf1, "done %lu, %d /sec", passed, speed);
    }
    
    /* make indent */
    indent = 79 - cursor_pos - strlen (buf1);
    memset (buf2, ' ', indent);
    buf2[indent] = 0;
    fprintf (stderr, "%s%s", buf2, buf1);

    memset (buf2, '\b', indent + strlen (buf1));
    buf2 [indent + strlen (buf1)] = 0;
    fprintf (stderr, "%s", buf2);
    fflush (stderr);
}


static char * strs[] =
{"0%",".",".",".",".","20%",".",".",".",".","40%",".",".",".",".","60%",".",".",".",".","80%",".",".",".",".","100%"};

static char progress_to_be[1024];
static char current_progress[1024];

static void str_to_be (char * buf, int prosents)
{
    int i;
    prosents -= prosents % 4;
    buf[0] = 0;
    for (i = 0; i <= prosents / 4; i ++)
	strcat (buf, strs[i]);
}


void print_how_far (FILE * fp,
		    unsigned long * passed, unsigned long total,
		    int inc, int quiet)
{
    int percent;

    if (*passed == 0)
	current_progress[0] = 0;

    (*passed) += inc;
    if (*passed > total) {
/*	fprintf (fp, "\nprint_how_far: total %lu has been reached already. cur=%lu\n",
	total, *passed);*/
	return;
    }

    percent = ((*passed) * 100) / total;

    str_to_be (progress_to_be, percent);

    if (strlen (current_progress) != strlen (progress_to_be)) {
	fprintf (fp, "%s", progress_to_be + strlen (current_progress));
    }

    strcat (current_progress, progress_to_be + strlen (current_progress));

    if (!quiet)
	print_how_fast (*passed/* - inc*/, total, strlen (progress_to_be),
			(*passed == inc) ? 1 : 0);

    fflush (fp);
}




/* calculates number of blocks in a file. Returns 0 for "sparse"
   regular files and files other than regular files and block devices */
unsigned long count_blocks (char * filename, int blocksize)
{
    loff_t high, low;
    int fd;
    unsigned long sz;
    __u64 size;

    if (!S_ISBLK (get_st_mode (filename)) && !S_ISREG (get_st_mode (filename)))
	return 0;

    fd = open (filename, O_RDONLY);
    if (fd == -1)
	die ("count_blocks: open failed (%s)", strerror (errno));

#ifdef BLKGETSIZE64
    {
	if (ioctl (fd, BLKGETSIZE64, &size) >= 0) {
	    sz = size;
	    if ((__u64)sz != size)
		    die ("count_blocks: block device too large");
	    return (size / 4096) * 4096 / blocksize;
	}
    }
#endif


#ifdef BLKGETSIZE
    {
	if (ioctl (fd, BLKGETSIZE, &sz) >= 0) {
	    size = sz;
	    return (size * 512 / 4096) * 4096 / blocksize;
	}
    }
#endif

    low = 0;
    for( high = 1; valid_offset (fd, high); high *= 2 )
	low = high;
    while (low < high - 1) {
	const loff_t mid = ( low + high ) / 2;

	if (valid_offset (fd, mid))
	    low = mid;
	else
	    high = mid;
    }
    valid_offset (fd, 0);

    close (fd);

    return (low + 1) * 4096 / 4096 / blocksize ;
}



/* there are masks for certain bits  */
__u16 mask16 (int from, int count)
{
    __u16 mask;


    mask = (0xffff >> from);
    mask <<= from;
    mask <<= (16 - from - count);
    mask >>= (16 - from - count);
    return mask;
}


__u32 mask32 (int from, int count)
{
    __u32 mask;


    mask = (0xffffffff >> from);
    mask <<= from;
    mask <<= (32 - from - count);
    mask >>= (32 - from - count);
    return mask;
}


__u64 mask64 (int from, int count)
{
    __u64 mask;


    mask = (0xffffffffffffffffLL >> from);
    mask <<= from;
    mask <<= (64 - from - count);
    mask >>= (64 - from - count);
    return mask;
}


__u32 get_random (void)
{
    srandom (time (0));
    return random ();
}

/* this implements binary search in the array 'base' among 'num' elements each
   of those is 'width' bytes long. 'comp_func' is used to compare keys */
int reiserfs_bin_search (void * key, void * base, __u32 num, int width,
			 __u32 * ppos, comparison_fn_t comp_func)
{
    __u32 rbound, lbound, j;
    int ret;

    if (num == 0 || base == NULL) {
	/* objectid map may be 0 elements long */
	*ppos = 0;
	return POSITION_NOT_FOUND;
    }

    lbound = 0;
    rbound = num - 1;

    for (j = (rbound + lbound) / 2; lbound <= rbound; j = (rbound + lbound) / 2) {
	ret =  comp_func ((void *)((char *)base + j * width), key ) ;
	if (ret < 0) { /* second is greater */
	    lbound = j + 1;
	    continue;

	} else if (ret > 0) { /* first is greater */
	    if (j == 0)
	    	break;
	    rbound = j - 1;
	    continue;
	} else { /* equal */
	    *ppos = j;
	    return POSITION_FOUND;
	}
    }

    *ppos = lbound;
    return POSITION_NOT_FOUND;
}


#define BLOCKLIST__ELEMENT_NUMBER 10

/*element is block number and device*/
int blockdev_list_compare (const void * block1, const void * block2) {
    if (*(__u32 *)block1 < *(__u32 *)block2)
        return -1;
    if (*(__u32 *)block1 > *(__u32 *)block2)
        return 1;
        
    if (*((__u32 *)block1 + 1) < *((__u32 *)block2 + 1))
        return -1;        
    if (*((__u32 *)block1 + 1) > *((__u32 *)block2 + 1))
        return 1;
        
    return 0;
}

/* return -1 if smth found, otherwise return position which new item should be inserted into */
/*
int blocklist__is_block_saved (struct block_handler ** base, __u32 * count,
				__u32 blocknr, dev_t device, __u32 * position) {
    struct block_handler block_h;
    
    *position = 0;
           
    if (*base == NULL) 
        return 0;

    block_h.blocknr = blocknr;
    block_h.device = device;
    
    if (reiserfs_bin_search (&block_h, *base, *count, sizeof (block_h),
		position, blocklist_compare) == POSITION_FOUND)
        return 1;
        
    return 0;
}
*/
void blocklist__insert_in_position (void *elem, void **base, __u32 *count, int elem_size, 
    __u32 * position) 
{
    if (elem_size == 0)
    	return;
    	
    if (*base == NULL)
        *base = getmem (BLOCKLIST__ELEMENT_NUMBER * elem_size);
    
    if (*count == get_mem_size ((void *)*base) / elem_size)
        *base = expandmem (*base, get_mem_size((void *)*base), 
                        BLOCKLIST__ELEMENT_NUMBER * elem_size);
    
    if (*position < *count) {
        memmove (*base + (*position + 1), 
                 *base + (*position),
                 (*count - *position) * elem_size);
    }

    memcpy (*base + (char) *position * elem_size, elem, elem_size);
    *count+=1;
}

static int get_random_bytes (void *out, int size) {
    int fd;
    
    if ((fd = open("/dev/urandom", O_RDONLY)) == -1)
        return 1;
    
    if (read(fd, out, size) <= 0) {
        close (fd);
        return 1;
    }
    
    close (fd);
    return 0;
}

int generate_random_uuid (unsigned char * uuid)
{
    if (get_random_bytes(uuid, 16)) {
        return -1;
    }

    /* Set the UUID variant to DCE */
    uuid[8] = (uuid[8] & 0x3F) | 0x80;
    /* Set UUID version to 4 --- truely random generation */
    uuid[6] = (uuid[6] & 0x0F) | 0x40;

    return 0;
}

int uuid_is_correct (unsigned char * uuid)
{
    int i;

    for (i = 0; i < 16; i++)
        if (uuid[i])
            break;

    if (i == 16)
	return 0;

    if (!test_bit(7, &uuid[8]) || test_bit(6, &uuid[8]))
    	return 0;

    if (test_bit(7, &uuid[6]) || !test_bit(6, &uuid[6]) ||
    	test_bit(5, &uuid[6]) ||  test_bit(4, &uuid[6]))
    	return 0;
    	
    return 1;
}

static int parse_uuid (const unsigned char * in, unsigned char * uuid)
{
    int i, j = 0;
    unsigned char frame[3];

    if (strlen(in) != 36)
	return -1;
	
    for (i = 0; i < 36; i++) {
	if ((i == 8) || (i == 13) || (i == 18) || (i == 23)) {
	    if (in[i] != '-')
		return 0;
	} else if (!isxdigit(in[i])) {
	    return -1;
	}
    }

    frame[2] = 0;
    for (i = 0; i < 36; i ++) {
	if ((i == 8) || (i == 13) || (i == 18) || (i == 23))
	    continue;
	
	frame[0] = in[i++];
	frame[1] = in[i];
	uuid[j++] = strtoul(frame, NULL, 16);
    }
    return 0;
}

int set_uuid (const unsigned char * text, unsigned char * UUID)
{
    if (parse_uuid (text, UUID) || !uuid_is_correct(UUID))
	return -1;

    return 0;
}

/* 0 - dma is not supported, scsi or regular file */
/* 1 - xt drive                                   */
/* 2 - ide drive */
static void get_dma_support(dma_info_t *dma_info){
    if (S_ISREG(dma_info->stat.st_mode))
	dma_info->stat.st_rdev = dma_info->stat.st_dev;

    if (IDE_DISK_MAJOR(MAJOR(dma_info->stat.st_rdev))) {
        dma_info->support_type = 2;
	return;
    }
    
#ifdef XT_DISK_MAJOR
    if (MAJOR(dma_info->stat.st_rdev) == XT_DISK_MAJOR) {
	dma_info->support_type = 1;
	return;
    }
#endif    
    dma_info->support_type = 0;
}

/* 
 * Return values: 
 * 0 - ok;
 * 1 - preparation cannot be done 
 * -1 - preparation failed
 */
int prepare_dma_check(dma_info_t *dma_info) {
    DIR *dir;
    struct dirent *dirent;
    struct stat64 stat;    
    dev_t rdev;
    int rem;
    char buf[256];

#ifndef HDIO_GET_DMA
        return -1;
#endif
	
    if (fstat64(dma_info->fd, &dma_info->stat))
	die("stat64 on device failed\n");
   
    get_dma_support(dma_info);
   
    /* dma should be supported */
    if (dma_info->support_type == 0)
	return 1;
    
    if (dma_info->support_type == 2) {
	rdev = dma_info->stat.st_rdev;

	if ((rem = (MINOR(rdev) % 64)) != 0) {
	    rdev -= rem;
	    if(!(dir = opendir("/dev/"))) {
		dma_info->support_type = 1;
		return 0;
	    }
	    
	    while ((dirent = readdir(dir)) != NULL) {
		if (strncmp(dirent->d_name, ".", 1) == 0 || strncmp(dirent->d_name, "..", 2) == 0)
		    continue;
		memset(buf, 0, 256);
		strncat(buf, "/dev/", 5);
		strncat(buf, dirent->d_name, strlen(dirent->d_name));
		if (stat64(buf, &stat)) 
		    break; 
		if (S_ISBLK(stat.st_mode) && stat.st_rdev == rdev) 
		{
		    dma_info->stat = stat;
		    dma_info->fd = open(buf, O_RDONLY | O_LARGEFILE);
		    closedir(dir);
		    return 0;
		}
	    }
	    closedir(dir);
	    dma_info->support_type = 1;
	    return 1;
	}
    }
    
    return 0;
}

static int is_dma_on (int fd) {
#ifdef HDIO_GET_DMA    
    static long parm;
    if (ioctl(fd, HDIO_GET_DMA, &parm))
	return -1;
    else 
	return parm;
#endif
    return 0;
}


static __u64 dma_speed(int fd, int support_type) {
    static struct hd_driveid id;
    __u64 speed = 0;
            
    if (support_type != 2) 
	return 0;

#ifdef HDIO_OBSOLETE_IDENTITY
    if (!ioctl(fd, HDIO_GET_IDENTITY, &id) || 
	!ioctl(fd, HDIO_OBSOLETE_IDENTITY)) {
#else
    if (!ioctl(fd, HDIO_GET_IDENTITY, &id)) {
#endif
	speed |= (__u64)id.dma_1word &  ~(__u64)0xff;
	speed |= ((__u64)id.dma_mword & ~(__u64)0xff) << 16;
	speed |= ((__u64)id.dma_ultra & ~(__u64)0xff) << 32;
    } else if (errno == -ENOMSG)
	return -1;
    else 
	return -1;
    
    return speed;
}

int get_dma_info(dma_info_t *dma_info) {
    if ((dma_info->dma = is_dma_on(dma_info->fd)) == -1)
	return -1;
    if ((dma_info->speed = dma_speed(dma_info->fd, dma_info->support_type)) == (__u64)-1) 
	return -1;
    return 0;
}

void clean_after_dma_check(int fd, dma_info_t *dma_info) {
    signal(SIGALRM, SIG_IGN);
    if (dma_info->fd && fd != dma_info->fd)
	close(dma_info->fd);
}
