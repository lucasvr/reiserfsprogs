/*
 * Copyright 1996, 1997, 1998 Hans Reiser
 */
/*#define _GNU_SOURCE*/
/*#define _FILE_OFFSET_BITS 64*/

#include <stdio.h>
#include <unistd.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <asm/types.h>
#include <stdlib.h>
#include <mntent.h>
#include <sys/vfs.h>
#include <time.h>
#include <fcntl.h>
#include <sys/stat.h>

#include "io.h"

/*
 * These have been stolen somewhere from linux
 */
int set_bit (int nr, void * addr)
{
    __u8 * p, mask;
    int retval;

    p = (__u8 *)addr;
    p += nr >> 3;
    mask = 1 << (nr & 0x7);
    /*cli();*/
    retval = (mask & *p) != 0;
    *p |= mask;
    /*sti();*/
    return retval;
}


int clear_bit (int nr, void * addr)
{
    __u8 * p, mask;
    int retval;

    p = (__u8 *)addr;
    p += nr >> 3;
    mask = 1 << (nr & 0x7);
    /*cli();*/
    retval = (mask & *p) != 0;
    *p &= ~mask;
    /*sti();*/
    return retval;
}

int test_bit(int nr, const void * addr)
{
    __u8 * p, mask;
  
    p = (__u8 *)addr;
    p += nr >> 3;
    mask = 1 << (nr & 0x7);
    return ((mask & *p) != 0);
}

int find_first_zero_bit (const void *vaddr, unsigned size)
{
    const __u8 *p = vaddr, *addr = vaddr;
    int res;

    if (!size)
	return 0;

    size = (size >> 3) + ((size & 0x7) > 0);
    while (*p++ == 255) {
	if (--size == 0)
	    return (p - addr) << 3;
    }
  
    --p;
    for (res = 0; res < 8; res++)
	if (!test_bit (res, p))
	    break;
    return (p - addr) * 8 + res;
}


int find_next_zero_bit (const void *vaddr, unsigned size, unsigned offset)
{
    const __u8 *addr = vaddr;
    const __u8 *p = addr + (offset >> 3);
    int bit = offset & 7, res;
  
    if (offset >= size)
	return size;
  
    if (bit) {
	/* Look for zero in first char */
	for (res = bit; res < 8; res++)
	    if (!test_bit (res, p))
		return (p - addr) * 8 + res;
	p++;
    }
    /* No zero yet, search remaining full bytes for a zero */
    res = find_first_zero_bit (p, size - 8 * (p - addr));
    return (p - addr) * 8 + res;
}


/*int test_and_set_bit (int nr, void * addr)
{
  int oldbit = test_bit (nr, addr);
  set_bit (nr, addr);
  return oldbit;
}


int test_and_clear_bit (int nr, void * addr)
{
  int oldbit = test_bit (nr, addr);
  clear_bit (nr, addr);
  return oldbit;
}*/


void die (char * fmt, ...)
{
    static char buf[1024];
    va_list args;

    va_start (args, fmt);
    vsprintf (buf, fmt, args);
    va_end (args);

    fprintf (stderr, "\n%s\n\n\n", buf);
    exit (-1);
}



#define MEM_BEGIN "_mem_begin_"
#define MEM_END "mem_end"
#define MEM_FREED "__free_"
#define CONTROL_SIZE (strlen (MEM_BEGIN) + 1 + sizeof (int) + strlen (MEM_END) + 1)


static int get_mem_size (char * p)
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

    checkmem (mem, size);

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
    checkmem (mem, size + by);

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
    char * name;
    FILE * f;

    name = tempnam (dir, 0);
    if (!name) {	
	fprintf (stderr, "is_readonly: tempnam failed, think fs is not readonly\n");
	return 0;
    }

    f = fopen (name, "w");
    if (f) {
	unlink (name);
	free (name);
	return 0;
    }
    free (name);
    return (errno == EROFS) ? 1 : 0;
}

int user_confirmed (char * q, char * yes)
{
    char * answer = 0;
    size_t n = 0;

    fprintf (stderr, "%s", q);
    if (getline (&answer, &n, stdin) != strlen (yes) || strcmp (yes, answer))
	return 0;

    return 1;
}


#include <unistd.h>
#include <linux/unistd.h>

#define __NR_stat64 195
_syscall2(long, stat64, char *, filename, struct stat *, statbuf);


static int _is_mounted (char * device_name, func_t f)
{
    int retval;
    FILE *fp;
    struct mntent *mnt;
    struct statfs stfs;
    struct stat root_st;
    struct stat device_st;
    /*    struct stat64 device_st64;*/
    int used_stat64 = 1;

    if (stat ("/", &root_st) == -1)
	die ("is_mounted: could not stat \"/\": %m\n");

    if (stat64 (device_name, &device_st) == -1) {
	used_stat64 = 0;
	if (stat (device_name, &device_st) == -1)
	    die ("is_mounted: could not stat file \"%s\": %m",
		 device_name);
    }

    if ((used_stat64 && !S_ISBLK (device_st.st_mode)) || !S_ISBLK (device_st.st_mode))
	/* not block device file could not be mounted */
	return 0;

    if ((used_stat64 && root_st.st_dev == device_st.st_rdev) ||
	root_st.st_dev == device_st.st_rdev) {
	/* device is mounted as root */
	return (f ? f ("/") : 1);
    }

    /* if proc filesystem is mounted */
    if (statfs ("/proc", &stfs) == -1 || stfs.f_type != 0x9fa0/*procfs magic*/ ||
	(fp = setmntent ("/proc/mounts", "r")) == NULL) {
	/* proc filesystem is not mounted, or /proc/mounts does not
           exist */
	if (f)
	    return (user_confirmed (" (could not figure out) Is filesystem mounted read-only? (Yes)",
				    "Yes\n"));
	else
	    return (user_confirmed (" (could not figure out) Is filesystem mounted? (Yes)",
				    "Yes\n"));
    }
    
    retval = 0;
    while ((mnt = getmntent (fp)) != NULL)
	if (strcmp (device_name, mnt->mnt_fsname) == 0) {
	    retval = (f ? f (mnt->mnt_dir) : 1);
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
    static time_t t0, t1;
    int speed;
    int indent;

    if (reset_time)
	time (&t0);

    time (&t1);
    if (t1 != t0)
	speed = passed / (t1 - t0);
    else
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


void print_how_far (unsigned long * passed, unsigned long total,
		    int inc, int quiet)
{
    int percent;

    if (*passed == 0)
	current_progress[0] = 0;

    (*passed) += inc;
    if (*passed > total) {
	fprintf (stderr, "\nprint_how_far: total %lu has been reached already. cur=%lu\n",
		 total, *passed);
	return;
    }

    percent = ((*passed) * 100) / total;

    str_to_be (progress_to_be, percent);

    if (strlen (current_progress) != strlen (progress_to_be)) {
	fprintf (stderr, "%s", progress_to_be + strlen (current_progress));
    }

    strcat (current_progress, progress_to_be + strlen (current_progress));

    if (!quiet)
	print_how_fast (*passed/* - inc*/, total, strlen (progress_to_be),
			(*passed == inc) ? 1 : 0);

    fflush (stderr);
}



#define ENDIANESS_NOT_DEFINED 0
#define LITTLE_ENDIAN_ARCH 1
#define BIG_ENDIAN_ARCH 2

static int endianess = ENDIANESS_NOT_DEFINED;


static void find_endianess (void)
{
    __u32 x = 0x0f0d0b09;
    char * s;

    s = (char *)&x;

    // little-endian is 1234
    if (s[0] == '\11' && s[1] == '\13' && s[2] == '\15' && s[3] == '\17')
	endianess = LITTLE_ENDIAN_ARCH;

    // big-endian is 4321
    if (s[0] == '\17' && s[1] == '\15' && s[2] == '\13' && s[3] == '\11')
	die ("big-endian archs are not supported");

    // nuxi/pdp-endian is 3412
    if (s[0] == '\15' && s[1] == '\17' && s[2] == '\11' && s[3] == '\13')
	die ("nuxi/pdp-endian archs are not supported");
}


// we used to use such function in the kernel stuff of reiserfs. Lets
// have them in utils as well
inline __u32 cpu_to_le32 (__u32 val)
{
    if (endianess == ENDIANESS_NOT_DEFINED)
	find_endianess ();

    if (endianess == LITTLE_ENDIAN_ARCH)
	return val;

    die ("neither big- nor any other endian archs are supported yet ");

    return ((val>>24) | ((val>>8)&0xFF00) |
	    ((val<<8)&0xFF0000) | (val<<24));
}


inline __u32 le32_to_cpu (__u32 val)
{
    return cpu_to_le32 (val);
}


inline __u16 cpu_to_le16 (__u16 val)
{
    return val;

    if (endianess == ENDIANESS_NOT_DEFINED)
	find_endianess ();

    if (endianess == LITTLE_ENDIAN_ARCH)
	return val;
    die ("neither big- nor pdp- endian arch are supported yet ");

    return (val >> 8) | (val << 8);
}


inline __u16 le16_to_cpu (__u16 val)
{
    /*printf ("%s:%u %p %p %p\n", __FILE__, __LINE__,
	    __builtin_return_address (0),
	    __builtin_return_address (1),
	    __builtin_return_address (2));*/
    return val;
    return cpu_to_le16 (val);
}


inline __u64 cpu_to_le64 (__u64 val)
{
    if (endianess == ENDIANESS_NOT_DEFINED)
	find_endianess ();

    if (endianess == LITTLE_ENDIAN_ARCH)
	return val;
    die ("neither big- nor pdp- endian arch are supported yet ");

    return 0;
}


inline __u64 le64_to_cpu (__u64 val)
{
    return cpu_to_le64 (val);
}


/* Given a file descriptor and an offset, check whether the offset is
   a valid offset for the file - return 0 if it isn't valid or 1 if it
   is */
loff_t reiserfs_llseek (unsigned int fd, loff_t offset, unsigned int origin);
#if 0
static int valid_offset( int fd, loff_t offset )
{
    char ch;
    loff_t res;

    /*res = reiserfs_llseek (fd, offset, 0);*/
    res = lseek64 (fd, offset, 0);
    if (res < 0)
	return 0;

    if (read (fd, &ch, 1) < 1)
	return 0;

    return 1;
}
#endif

/* calculates number of blocks on device */
unsigned long count_blocks (char * filename, int blocksize, int fd)
{
    loff_t high, low;
    int opened_here = 0;

    if (fd < 0) {
	fd = open (filename, O_RDONLY);
	opened_here = 1;
    }
    if (fd < 0)
	die ("count_blocks: open failed (%s)", strerror (errno));

#ifdef BLKGETSIZE
    {
	long size;

	if (ioctl (fd, BLKGETSIZE, &size) >= 0) {
	    if (opened_here)
		close (fd);
	    return  size / (blocksize / 512);
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
    if (opened_here)
        close (fd);

    return (low + 1) / (blocksize);
}



