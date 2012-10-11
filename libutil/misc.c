/*
 *  Copyright 2000-2004 by Hans Reiser, licensing governed by 
 *  reiserfsprogs/README
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "util/misc.h"

#include "misc/malloc.h"
#include "misc/misc.h"

#include <time.h>
#include <stdlib.h>
#include <string.h>

char buf1 [100];
char buf2 [100];

static time_t t0 = 0, t1 = 0, t2 = 0;

void util_misc_speed (FILE *fp, unsigned long passed, 
		      unsigned long total, int cursor_pos, 
		      int reset_time)
{
    int speed;
    int indent;

    if (passed == 0)
	reset_time = 1;
    
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
    } else if (reset_time) {
	speed = 0;
    } else {
	return;
    }

    /* what has to be written */
    if (reset_time)
	buf1[0] = 0;
    else if (total)
	sprintf (buf1, "left %lu, %d /sec", total - passed, speed);
    else {
	/*(*passed) ++;*/
	sprintf (buf1, "done %lu, %d /sec", passed, speed);
    }
    
    /* make indent */
    indent = 79 - cursor_pos - strlen (buf1);
    memset (buf2, ' ', indent);
    buf2[indent] = 0;
    fprintf (fp, "%s%s", buf2, buf1);

    memset (buf2, '\b', indent + strlen (buf1));
    buf2 [indent + strlen (buf1)] = 0;
    fprintf (fp, "%s", buf2);
    fflush (fp);
}


static char * strs[] =
{"0%",".",".",".",".",
 "20%",".",".",".",".",
 "40%",".",".",".",".",
 "60%",".",".",".",".",
 "80%",".",".",".",".","100%"};

static char progress_to_be[1024];
static char current_progress[1024];

static void str_to_be (char * buf, int prosents) {
    int i;
    prosents -= prosents % 4;
    buf[0] = 0;
    for (i = 0; i <= prosents / 4; i ++)
	strcat (buf, strs[i]);
}


void util_misc_progress (FILE * fp, unsigned long * passed, 
			 unsigned long total, unsigned int inc, 
			 int forward)
{
    int percent;

    if (*passed == 0)
	current_progress[0] = 0;

    (*passed) += inc;
    if (*passed > total) {
/*	fprintf (fp, "\nutil_misc_progress: total %lu has been "
		 "reached already. cur=%lu\n", total, *passed);*/
	return;
    }

    percent = ((*passed) * 100) / total;

    str_to_be (progress_to_be, percent);

    if (strlen (current_progress) != strlen (progress_to_be)) {
	fprintf (fp, "%s", progress_to_be + strlen (current_progress));
    }

    strcat (current_progress, progress_to_be + strlen (current_progress));

    if (forward != 2) {
	util_misc_speed(fp, *passed /* - inc*/, forward ? 0 : total, 
			strlen (progress_to_be), (*passed == inc) ? 1 : 0);
    }
    
    fflush (fp);
}

static int screen_width = 0;
static int screen_curr_pos = 0;
static char *screen_savebuffer;
static int screen_savebuffer_len;

void screen_init() {
    char *width;

    width = getenv("COLUMNS");
    if ( width )
	screen_width = atoi(width);
    
    if (screen_width == 0)
	screen_width = 80; // We default to 80 characters wide screen
    screen_width--;

    screen_savebuffer_len=screen_width;
    screen_savebuffer=misc_getmem(screen_width+1);
    memset(screen_savebuffer,0,screen_savebuffer_len+1);
}

void util_misc_name (FILE * fp) {
    static int printed_len = 0;
    int i;
    
    if (screen_curr_pos == 0 || t0 == 0) {
	/* Start the new progress. */
	time (&t0);
	t2 = t0;
    } else {
	time (&t1);
	if (t1 - t2 < 1)
	    return;
	t2 = t1;
    } 

    if (screen_width && screen_curr_pos >= screen_width ) {
	fprintf(fp, "... %.*s", screen_width - 4,
		screen_savebuffer + ( screen_curr_pos - (screen_width - 4)));
	printed_len = screen_width;
    } else if (screen_width) {
	fprintf(fp, "%s", screen_savebuffer);
	
	for (i = 0; i <= printed_len - screen_curr_pos ; i++)
	    fprintf(fp, " ");
	
	printed_len = screen_curr_pos;
    }
    
    fprintf(fp, "\r");
    fflush (fp);
}

/* semantic pass progress */
void util_misc_print_name (FILE *fp, char * name, int len) {
    int i;
	
    if (screen_width == 0)
	screen_init();
    
    if ( len + screen_curr_pos + 1 > screen_savebuffer_len) {
	char *t;
	
	t = misc_expandmem(screen_savebuffer, screen_savebuffer_len + 1, 
	    len + screen_curr_pos - screen_savebuffer_len + 1);
	
	if (!t) {
	    return; // ????
	}
	
	screen_savebuffer = t;
	screen_savebuffer_len = len + screen_curr_pos + 1;
    }
    
    strcat(screen_savebuffer,"/");
    strncat(screen_savebuffer,name,len);
    i = screen_curr_pos;
    screen_curr_pos += len+1;
    for ( ; i<screen_curr_pos; i++)
	if ( screen_savebuffer[i] < 32 )
	    screen_savebuffer[i] = '?';
    screen_savebuffer[screen_curr_pos]=0;

    util_misc_name(fp);
}

void util_misc_erase_name (FILE * fp, int len) {
    screen_curr_pos-=(len+1);
    if (screen_curr_pos < 0 )
	misc_die("%s: Get out of buffer's data!\n", __FUNCTION__);

    screen_savebuffer[screen_curr_pos]=0;
}


void util_misc_fini_name (FILE * fp) {
    screen_curr_pos = 0;
    util_misc_name(fp);
}
