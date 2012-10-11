/*
 * Copyright 1996-2004 by Hans Reiser, licensing governed by 
 * reiserfsprogs/README
 */

/*
 * Keyed 32-bit hash function using TEA in a Davis-Meyer function
 *   H0 = Key
 *   Hi = E Mi(Hi-1) + Hi-1
 *
 * (see Applied Cryptography, 2nd edition, p448).
 *
 * Jeremy Fitzhardinge <jeremy@zip.com.au> 1998
 * 
 * Jeremy has agreed to the contents of reiserfs/README. -Hans
 * Yura's function is added (04/07/2000)
 */

//
// reiserfs_hash_keyed
// reiserfs_hash_yura
// r5
//

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "reiserfs/libreiserfs.h"

#define DELTA 0x9E3779B9
#define FULLROUNDS 10		/* 32 is overkill, 16 is strong crypto */
#define PARTROUNDS 6		/* 6 gets complete mixing */

/* a, b, c, d - data; h0, h1 - accumulated hash */
#define TEACORE(rounds)							\
	do {								\
		__u32 sum = 0;						\
		int n = rounds;						\
		__u32 b0, b1;						\
									\
		b0 = h0;						\
		b1 = h1;						\
									\
		do							\
		{							\
			sum += DELTA;					\
			b0 += ((b1 << 4)+a) ^ (b1+sum) ^ ((b1 >> 5)+b);	\
			b1 += ((b0 << 4)+c) ^ (b0+sum) ^ ((b0 >> 5)+d);	\
		} while(--n);						\
									\
		h0 += b0;						\
		h1 += b1;						\
	} while(0);


__u32 reiserfs_hash_keyed(const char *msg, int len)
{
	__u32 k[] = { 0x9464a485, 0x542e1a94, 0x3e846bff, 0xb75bcfc3}; 

	__u32 h0 = k[0], h1 = k[1];
	__u32 a, b, c, d;
	__u32 pad;
	int i;
 

	pad = (__u32)len | ((__u32)len << 8);
	pad |= pad << 16;

	while(len >= 16)
	{
		a = (__u32)msg[ 0]      |
		    (__u32)msg[ 1] << 8 |
		    (__u32)msg[ 2] << 16|
		    (__u32)msg[ 3] << 24;
		b = (__u32)msg[ 4]      |
		    (__u32)msg[ 5] << 8 |
		    (__u32)msg[ 6] << 16|
		    (__u32)msg[ 7] << 24;
		c = (__u32)msg[ 8]      |
		    (__u32)msg[ 9] << 8 |
		    (__u32)msg[10] << 16|
		    (__u32)msg[11] << 24;
		d = (__u32)msg[12]      |
		    (__u32)msg[13] << 8 |
		    (__u32)msg[14] << 16|
		    (__u32)msg[15] << 24;
		
		TEACORE(PARTROUNDS);

		len -= 16;
		msg += 16;
	}

	if (len >= 12)
	{
		if (len >= 16)
		    *(int *)0 = 0;

		a = (__u32)msg[ 0]      |
		    (__u32)msg[ 1] << 8 |
		    (__u32)msg[ 2] << 16|
		    (__u32)msg[ 3] << 24;
		b = (__u32)msg[ 4]      |
		    (__u32)msg[ 5] << 8 |
		    (__u32)msg[ 6] << 16|
		    (__u32)msg[ 7] << 24;
		c = (__u32)msg[ 8]      |
		    (__u32)msg[ 9] << 8 |
		    (__u32)msg[10] << 16|
		    (__u32)msg[11] << 24;

		d = pad;
		for(i = 12; i < len; i++)
		{
			d <<= 8;
			d |= msg[i];
		}
	}
	else if (len >= 8)
	{
		if (len >= 12)
		    *(int *)0 = 0;
		a = (__u32)msg[ 0]      |
		    (__u32)msg[ 1] << 8 |
		    (__u32)msg[ 2] << 16|
		    (__u32)msg[ 3] << 24;
		b = (__u32)msg[ 4]      |
		    (__u32)msg[ 5] << 8 |
		    (__u32)msg[ 6] << 16|
		    (__u32)msg[ 7] << 24;

		c = d = pad;
		for(i = 8; i < len; i++)
		{
			c <<= 8;
			c |= msg[i];
		}
	}
	else if (len >= 4)
	{
		if (len >= 8)
		    *(int *)0 = 0;
		a = (__u32)msg[ 0]      |
		    (__u32)msg[ 1] << 8 |
		    (__u32)msg[ 2] << 16|
		    (__u32)msg[ 3] << 24;

		b = c = d = pad;
		for(i = 4; i < len; i++)
		{
			b <<= 8;
			b |= msg[i];
		}
	}
	else
	{
		if (len >= 4)
		    *(int *)0 = 0;
		a = b = c = d = pad;
		for(i = 0; i < len; i++)
		{
			a <<= 8;
			a |= msg[i];
		}
	}

	TEACORE(FULLROUNDS);

	return h0^h1;
}


__u32 reiserfs_hash_yura (const char *msg, int len)
{
    int j, pow;
    __u32 a, c;
    int i;
    
    for (pow=1,i=1; i < len; i++) pow = pow * 10; 
    
    if (len == 1) 
	a = msg[0]-48;
    else
	a = (msg[0] - 48) * pow;
    
    for (i=1; i < len; i++) {
	c = msg[i] - 48; 
	for (pow=1,j=i; j < len-1; j++) pow = pow * 10; 
	a = a + c * pow;
    }
    
    for (; i < 40; i++) {
	c = '0' - 48; 
	for (pow=1,j=i; j < len-1; j++) pow = pow * 10; 
	a = a + c * pow;
    }
    
    for (; i < 256; i++) {
	c = i; 
	for (pow=1,j=i; j < len-1; j++) pow = pow * 10; 
	a = a + c * pow;
    }
    
    a = a << 7;
    return a;
}


__u32 reiserfs_hash_r5 (const char *msg, int len)
{
    __u32 a=0;
    int i;
    
    for (i = 0; i < len; i ++) {
	a += msg[i] << 4;
	a += msg[i] >> 4;
	a *= 11;
    } 
    return a;
}

static const struct {
    hashf_t func;
    char * name;
} hashes[REISERFS_HASH_LAST] = {{0, "not set"},
				{reiserfs_hash_keyed, "\"tea\""},
				{reiserfs_hash_yura, "\"rupasov\""},
				{reiserfs_hash_r5, "\"r5\""}};        

#define HASH_AMOUNT (sizeof (hashes) / sizeof (hashes [0]))

#define good_name(hashfn,name,namelen,deh_offset) \
	(reiserfs_hash_value (hashfn, name, namelen) == OFFSET_HASH (deh_offset))


/* this also sets hash function */
int reiserfs_hash_correct (hashf_t *func, char * name, 
			   int namelen, __u32 offset)
{
    unsigned int i;

    if (namelen == 1 && name[0] == '.') {
	if (offset == OFFSET_DOT)
	    return 1;
	return 0;
    }

    if (namelen == 2 && name[0] == '.' && name[1] == '.') {
	if (offset == OFFSET_DOT_DOT)
	    return 1;
	return 0;
    }

    if (*func == 0) {
	/* try to find what hash function the name is sorted with */
	for (i = 1; i < HASH_AMOUNT; i ++) {
	    if (good_name (hashes [i].func, name, namelen, offset)) {
		if (*func) {
		    /* two or more hash functions give ok for this name */
		    fprintf (stderr, "Detecting hash code: could not detect "
			     "hash with name \"%.*s\"\n", namelen, name);
		    *func = 0;
		    return 0;
		}

		/* set hash function */
 		*func = hashes [i].func;
 	    }
 	}

        if (*func == 0)
            return 0;
    }

    if (good_name (*func, name, namelen, offset))
	return 1;

    return 0;
}


int reiserfs_hash_find (char * name, int namelen, __u32 offset, 
			unsigned int code_to_try_first)
{
    unsigned int i;

    if (!namelen || !name[0])
	return UNSET_HASH;

    if (code_to_try_first) {
	if (good_name (hashes [code_to_try_first].func, name, namelen, offset))
	    return code_to_try_first;
    }
    
    for (i = 1; i < HASH_AMOUNT; i ++) {
	if (i == code_to_try_first)
	    continue;
	if (good_name (hashes [i].func, name, namelen, offset))
	    return i;
    }

    /* not matching hash found */
    return UNSET_HASH;
}


char * reiserfs_hash_name(unsigned int code) {
    if (code >= HASH_AMOUNT || code < 0)
        return 0;
    return hashes [code].name;
}


int reiserfs_hash_code (hashf_t func)
{
    unsigned int i;
    
    for (i = 0; i < HASH_AMOUNT; i ++)
	if (func == hashes [i].func)
	    return i;

    reiserfs_panic ("reiserfs_hash_code: no hashes matches this function\n");
    return 0;
}


hashf_t reiserfs_hash_func(unsigned int code) {
    if (code >= HASH_AMOUNT) {
	reiserfs_warning (stderr, "reiserfs_hash_func: wrong hash code %d.\n"
			  "Using default %s hash function\n", code,
			  reiserfs_hash_name (DEFAULT_HASH));
	code = DEFAULT_HASH;
    }
    return hashes [code].func;
}


hashf_t reiserfs_hash_get (char * hash) {
    unsigned int i;
 
    for (i = 0; i < HASH_AMOUNT; i ++)
	if (!strcmp (hash, hashes [i].name))
	    return hashes [i].func;
    return 0;
}

__u32 reiserfs_hash_value (hashf_t func, char * name, int namelen) {
    __u32 res;

    res = func (name, namelen);
    res = OFFSET_HASH(res);
    if (res == 0)
	res = 128;

    return res;
}

