/*
 * Copyright 1996-2004 by Hans Reiser, licensing governed by 
 * reiserfsprogs/README
 */

/* Now we have all buffers that must be used in balancing of the tree 	*/
/* Further calculations can not cause schedule(), and thus the buffer 	*/
/* tree will be stable until the balancing will be finished 		*/
/* balance the tree according to the analysis made before,		*/
/* and using buffers obtained after all above.				*/


#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "reiserfs/libreiserfs.h"

/* modes of reiserfs_lb_move */
#define LEAF_FROM_S_TO_L 0
#define LEAF_FROM_S_TO_R 1
#define LEAF_FROM_R_TO_L 2
#define LEAF_FROM_L_TO_R 3
#define LEAF_FROM_S_TO_SNEW 4


/* summary:
 if deleting something ( tb->insert_size[0] < 0 )
   return(reiserfs_lb_balance_delete()); (flag d handled here)
 else
   if lnum is larger than 0 we put items into the left node
   if rnum is larger than 0 we put items into the right node
   if snum1 is larger than 0 we put items into the new node s1
   if snum2 is larger than 0 we put items into the new node s2 
Note that all *num* count new items being created.

It would be easier to read reiserfs_lb_balance() if each of these summary
lines was a separate procedure rather than being inlined.  I think
that there are many passages here and in reiserfs_lb_balance_delete() in
which two calls to one procedure can replace two passages, and it
might save cache space and improve software maintenance costs to do so.  

Vladimir made the perceptive comment that we should offload most of
the decision making in this function into reiserfs_fix_nodes/check_balance, 
and then create some sort of structure in tb that says what actions should
be performed by reiserfs_tb_balance.

-Hans */

static void print_tb (int mode, int item_pos, int pos_in_item, 
		      reiserfs_tb_t * tb, char * mes);

/* insert item into the leaf node in position before */
static void reiserfs_lb_insert (reiserfs_filsys_t * s,
				reiserfs_bufinfo_t * bi,
				int before,
				reiserfs_ih_t * inserted_item_ih,
				const char * inserted_item_body,
				int zeros_number)
{
    reiserfs_bh_t * bh = bi->bi_bh;
    int nr;
    reiserfs_node_head_t * blkh;
    reiserfs_ih_t * ih;
    int i;
    int last_loc, unmoved_loc;
    char * to;

    blkh = NODE_HEAD (bh);
    nr = reiserfs_nh_get_items (blkh);

    /* get item new item must be inserted before */
    ih = reiserfs_ih_at (bh, before);

    /* prepare space for the body of new item */
    last_loc = nr ? reiserfs_ih_get_loc (&ih[nr - before - 1]) : bh->b_size;
    unmoved_loc = before ? reiserfs_ih_get_loc (ih-1) : bh->b_size;

    memmove (bh->b_data + last_loc - reiserfs_ih_get_len (inserted_item_ih), 
	     bh->b_data + last_loc, unmoved_loc - last_loc);

    to = bh->b_data + unmoved_loc - reiserfs_ih_get_len (inserted_item_ih);
    memset (to, 0, zeros_number);
    to += zeros_number;

    /* copy body to prepared space */
    if (inserted_item_body)
	/* if (mem_mode == REISERFS_USER_MEM)
	    copy_from_user (to, inserted_item_body, 
			    inserted_item_ih->ih_item_len - zeros_number);
	else { */
	memmove (to, inserted_item_body, 
		 reiserfs_ih_get_len (inserted_item_ih) - zeros_number);
    //}
    else
	memset(to, '\0', reiserfs_ih_get_len (inserted_item_ih) - zeros_number);
  
    /* insert item header */
    memmove (ih + 1, ih, REISERFS_IH_SIZE * (nr - before));
    memmove (ih, inserted_item_ih, REISERFS_IH_SIZE);
  
    /* change locations */
    for (i = before; i < nr + 1; i ++) {
	unmoved_loc -= reiserfs_ih_get_len (&ih[i-before]);
	reiserfs_ih_set_loc (&ih[i-before], unmoved_loc);
    }
  
    /* sizes, free space, item number */
    reiserfs_nh_set_items (blkh, reiserfs_nh_get_items (blkh) + 1);
    reiserfs_nh_set_free (blkh, reiserfs_nh_get_free (blkh) -
			 (REISERFS_IH_SIZE + reiserfs_ih_get_len (inserted_item_ih)));

    reiserfs_buffer_mkdirty(bh) ;

    if (bi->bi_parent) { 
	reiserfs_dc_t * dc;

	dc = reiserfs_int_at (bi->bi_parent, bi->bi_position);
	reiserfs_dc_set_size (dc, reiserfs_dc_get_size (dc) + REISERFS_IH_SIZE + 
			   reiserfs_ih_get_len (inserted_item_ih));
	reiserfs_buffer_mkdirty(bi->bi_parent) ;
    }

    if (reiserfs_leaf_valid(bh) != NT_LEAF)
	reiserfs_panic ("reiserfs_lb_insert: bad leaf %lu: %b", 
			bh->b_blocknr, bh);
}


/* paste paste_size bytes to affected_item_num-th item. 
   When item is a directory, this only prepare space for new entries */
static void reiserfs_lb_insert_unit (reiserfs_filsys_t * fs,
				     reiserfs_bufinfo_t * bi,
				     int affected_item_num,
				     int pos_in_item,
				     int paste_size,
				     const char * body,
				     int zeros_number)
{
    reiserfs_bh_t * bh = bi->bi_bh;
    int nr;
    reiserfs_node_head_t * blkh;
    reiserfs_ih_t * ih;
    int i;
    int last_loc, unmoved_loc;


    blkh = NODE_HEAD (bh);
    nr = reiserfs_nh_get_items (blkh);

    /* item to be appended */
    ih = reiserfs_ih_at(bh, affected_item_num);

    last_loc = reiserfs_ih_get_loc (&ih[nr - affected_item_num - 1]);
    unmoved_loc = affected_item_num ? reiserfs_ih_get_loc (ih-1) : bh->b_size;  

    /* prepare space */
    memmove (bh->b_data + last_loc - paste_size, bh->b_data + last_loc,
	     unmoved_loc - last_loc);


    /* change locations */
    for (i = affected_item_num; i < nr; i ++)
	reiserfs_ih_set_loc (&ih[i-affected_item_num], 
			 reiserfs_ih_get_loc (&ih[i-affected_item_num]) - 
			 paste_size);

    if ( body ) {
	if (!reiserfs_ih_dir(ih)) {
	    /*if (mem_mode == REISERFS_USER_MEM) {
	        memset (bh->b_data + unmoved_loc - paste_size, 0, zeros_number);
		copy_from_user (bh->b_data + unmoved_loc - paste_size + 
				zeros_number, body, paste_size - zeros_number);
	    } else */
	    {
		if (!pos_in_item) {
		    /* shift data to right */
		    memmove (bh->b_data + reiserfs_ih_get_loc (ih) + paste_size,
			     bh->b_data + reiserfs_ih_get_loc (ih), 
			     reiserfs_ih_get_len (ih));
		    /* paste data in the head of item */
		    memset (bh->b_data + reiserfs_ih_get_loc (ih), 0, zeros_number);
		    memcpy (bh->b_data + reiserfs_ih_get_loc (ih) + zeros_number, 
			    body, paste_size - zeros_number);
		} else {
		    memset (bh->b_data + unmoved_loc - paste_size, 
			    0, zeros_number);
		    memcpy (bh->b_data + unmoved_loc - paste_size + zeros_number,
			    body, paste_size - zeros_number);
		}
	    }
	}
    }
    else
	memset(bh->b_data + unmoved_loc - paste_size,'\0',paste_size);

    reiserfs_ih_set_len (ih, reiserfs_ih_get_len (ih) + paste_size);

    /* change free space */
    reiserfs_nh_set_free (blkh, reiserfs_nh_get_free (blkh) - paste_size);

    reiserfs_buffer_mkdirty(bh) ;

    if (bi->bi_parent) { 
	reiserfs_dc_t * dc;

	dc = reiserfs_int_at (bi->bi_parent, bi->bi_position);
	reiserfs_dc_set_size (dc, reiserfs_dc_get_size (dc) + paste_size);
	reiserfs_buffer_mkdirty(bi->bi_parent);
    }
    if (reiserfs_leaf_valid(bh) != NT_LEAF)
	reiserfs_panic ("reiserfs_lb_insert_unit: bad leaf %lu: %b", 
			bh->b_blocknr, bh);
}

/* paste new_entry_count entries (new_dehs, records) into position before 
   to item_num-th item */
static void reiserfs_lb_insert_entry (reiserfs_bh_t * bh,
				      int item_num, int before, 
				      int new_entry_count,
				      reiserfs_deh_t * new_dehs,
				      const char * records, 
				      int paste_size)
{
    reiserfs_ih_t * ih;
    char * item;
    reiserfs_deh_t * deh;
    char * insert_point;
    int i, old_entry_num;

    if (new_entry_count == 0)
        return;

    ih = reiserfs_ih_at(bh, item_num);

    /* first byte of dest item */
    item = reiserfs_item_by_ih (bh, ih);

    /* entry head array */
    deh = reiserfs_deh (bh, ih);

    /* new records will be pasted at this point */
    insert_point = item + (before ? reiserfs_deh_get_loc (&deh[before - 1]) : 
			   (reiserfs_ih_get_len (ih) - paste_size));

    /* adjust locations of records that will be AFTER new records */
    for (i = reiserfs_ih_get_entries (ih) - 1; i >= before; i --)
	reiserfs_deh_set_loc (deh + i, reiserfs_deh_get_loc (deh + i) + 
			  REISERFS_DEH_SIZE * new_entry_count);

    /* adjust locations of records that will be BEFORE new records */
    for (i = 0; i < before; i ++)
	reiserfs_deh_set_loc (deh + i, reiserfs_deh_get_loc (deh + i) + paste_size);

    old_entry_num = reiserfs_ih_get_entries (ih);
    //I_ENTRY_COUNT(ih) += new_entry_count;
    reiserfs_ih_set_entries (ih, old_entry_num + new_entry_count);

    /* prepare space for pasted records */
    memmove (insert_point + paste_size, insert_point, 
	     item + (reiserfs_ih_get_len (ih) - paste_size) - insert_point);

    /* copy new records */
    memcpy (insert_point + REISERFS_DEH_SIZE * new_entry_count, records,
	    paste_size - REISERFS_DEH_SIZE * new_entry_count);
  
    /* prepare space for new entry heads */
    deh += before;
    memmove ((char *)(deh + new_entry_count), deh, insert_point - (char *)deh);

    /* copy new entry heads */
    memcpy (deh, new_dehs, REISERFS_DEH_SIZE * new_entry_count);

    /* set locations of new records */
    for (i = 0; i < new_entry_count; i ++)
	reiserfs_deh_set_loc (deh + i, reiserfs_deh_get_loc (deh + i) + 
			  (- reiserfs_deh_get_loc (&new_dehs[new_entry_count - 1]) +
			   insert_point + REISERFS_DEH_SIZE * new_entry_count - item));


    /* change item key if neccessary (when we paste before 0-th entry */
    if (!before)
	reiserfs_key_set_off1 (&ih->ih_key, reiserfs_deh_get_off (new_dehs));
}



/* copy copy_count entries from source directory item to dest buffer 
   (creating new item if needed) */
static void reiserfs_lb_copy_entry (reiserfs_filsys_t * fs,
				    reiserfs_bufinfo_t * dest_bi, 
				    reiserfs_bh_t * source, 
				    int last_first, int item_num, 
				    int from, int copy_count)
{
    reiserfs_bh_t * dest = dest_bi->bi_bh;
    int ditem_num;			/* either the number of target item,
					   or if we must create a new item,
					   the number of the item we will
					   create it next to */
    reiserfs_ih_t * ih;
    reiserfs_deh_t * deh;
    
    /* length of all records in item to be copied */
    int copy_records_len;	
    char * records;

    ih = reiserfs_ih_at (source, item_num);

    /* length of all record to be copied and first byte of the last of them */
    deh = reiserfs_deh (source, ih);
    if (copy_count) {
	copy_records_len = (from ? 
			    reiserfs_deh_get_loc (&deh[from - 1]) : 
			    reiserfs_ih_get_len (ih)) - 
		reiserfs_deh_get_loc (&deh[from + copy_count - 1]);
	
	records = source->b_data + reiserfs_ih_get_loc (ih) + 
		reiserfs_deh_get_loc (&deh[from + copy_count - 1]);
    } else {
	copy_records_len = 0;
	records = 0;
    }

    /* when copy last to first, dest buffer can contain 0 items */
    ditem_num = (last_first == LAST_TO_FIRST) ? 
	    (( reiserfs_node_items(dest) ) ? 0 : -1) : (reiserfs_node_items(dest) - 1);

    /* if there are no items in dest or the first/last item in dest 
       is not item of the same directory */
    if ( (ditem_num == - 1) ||
	 (last_first == FIRST_TO_LAST && 
	  reiserfs_leaf_mergeable (reiserfs_ih_at (dest, ditem_num), 
				   ih, dest->b_size) == 0) ||
	 (last_first == LAST_TO_FIRST && 
	  reiserfs_leaf_mergeable (ih, reiserfs_ih_at (dest, ditem_num), 
				   dest->b_size) == 0)) 
    {
	/* create new item in dest */
	reiserfs_ih_t new_ih;

	/* form item header */
	memcpy (&new_ih.ih_key, &ih->ih_key, REISERFS_KEY_SIZE);

	/* calculate item len */
	reiserfs_ih_set_len (&new_ih, REISERFS_DEH_SIZE * copy_count + copy_records_len);
	reiserfs_ih_set_entries (&new_ih, 0);
    
	if (last_first == LAST_TO_FIRST) {
	    /* form key by the following way */
	    if (from < reiserfs_ih_get_entries (ih)) {
		reiserfs_key_set_off1 (&new_ih.ih_key, reiserfs_deh_get_off (&deh[from]));
	    } else {
		/* no entries will be copied to this item in this function */
		reiserfs_key_set_off1 (&new_ih.ih_key, MAX_KEY1_OFFSET);
	    }
	    reiserfs_key_set_uni (&new_ih.ih_key, UNI_DE);
	}
	reiserfs_ih_set_format (&new_ih, reiserfs_ih_get_format (ih));
	reiserfs_ih_set_flags (&new_ih, reiserfs_ih_get_flags (ih));
    
	/* insert item into dest buffer */
	reiserfs_lb_insert (fs, dest_bi, (last_first == LAST_TO_FIRST)
			    ? 0 : reiserfs_node_items(dest), &new_ih, NULL, 0);
    } else {
	/* prepare space for entries */
	reiserfs_lb_insert_unit (fs, dest_bi, (last_first == FIRST_TO_LAST) ?
				 (reiserfs_node_items(dest) - 1) : 0, 0xffff,
				 REISERFS_DEH_SIZE * copy_count + copy_records_len, 
				 records, 0);
    }
  
    ditem_num = (last_first == FIRST_TO_LAST) ? 
	    (reiserfs_node_items(dest)-1) : 0;
  
    reiserfs_lb_insert_entry (dest_bi->bi_bh, ditem_num, 
			      (last_first == FIRST_TO_LAST) ?
			      reiserfs_ih_get_entries (reiserfs_ih_at (dest, ditem_num))
			      : 0, copy_count, deh + from, records, 
			      REISERFS_DEH_SIZE * copy_count + copy_records_len);
}


/* Copy the first (if last_first == FIRST_TO_LAST) or last 
   (last_first == LAST_TO_FIRST) item or part of it or nothing 
   (see the return 0 below) from SOURCE to the end (if last_first) 
   or beginning (!last_first) of the DEST */
/* returns 1 if anything was copied, else 0 */
static int reiserfs_lb_copy_boundary (reiserfs_filsys_t * fs,
				      reiserfs_bufinfo_t * dest_bi, 
				      reiserfs_bh_t * src, 
				      int last_first,
				      int bytes_or_entries)
{
    reiserfs_bh_t * dest = dest_bi->bi_bh;
    
    /* number of items in the source and destination buffers */
    int dest_nr_item, src_nr_item; 
    reiserfs_ih_t * ih;
    reiserfs_ih_t * dih;
  
    dest_nr_item = reiserfs_node_items(dest);
  
    if ( last_first == FIRST_TO_LAST ) {
	/* if ( DEST is empty or first item of SOURCE and last item of DEST 
	   are the items of different objects or of different types ) then 
	   there is no need to treat this item differently from the other 
	   items that we copy, so we return */
	ih = reiserfs_ih_at (src, 0);
	dih = reiserfs_ih_at (dest, dest_nr_item - 1);
	if (!dest_nr_item || 
	    reiserfs_leaf_mergeable (dih, ih, src->b_size) == 0)
	{
	    /* there is nothing to merge */
	    return 0;
	}
      
	if ( reiserfs_ih_dir(ih) ) {
	    if ( bytes_or_entries == -1 )
		/* copy all entries to dest */
		bytes_or_entries = reiserfs_ih_get_entries(ih);
	    
	    reiserfs_lb_copy_entry (fs, dest_bi, src, FIRST_TO_LAST, 
				    0, 0, bytes_or_entries);
	    return 1;
	}
      
	/* copy part of the body of the first item of SOURCE to the end of 
	   the body of the last item of the DEST part defined by 
	   'bytes_or_entries'; if bytes_or_entries == -1 copy whole body; 
	   don't create new item header */
	if ( bytes_or_entries == -1 )
	    bytes_or_entries = reiserfs_ih_get_len (ih);

	/* merge first item (or its part) of src buffer with the last
	   item of dest buffer. Both are of the same file */
	reiserfs_lb_insert_unit (fs, dest_bi, dest_nr_item - 1, 
				 reiserfs_ih_get_len (dih),
				 bytes_or_entries, 
				 reiserfs_item_by_ih(src,ih), 0);
      
	if (reiserfs_ih_ext(dih)) {
	    if (bytes_or_entries == reiserfs_ih_get_len (ih))
		//dih->u.ih_free_space = ih->u.ih_free_space;
		reiserfs_ih_set_free (dih, reiserfs_ih_get_free (ih));
	}
    
	return 1;
    }
  

    /* copy boundary item to right (last_first == LAST_TO_FIRST) */

    /* ( DEST is empty or last item of SOURCE and first item of DEST
       are the items of different object or of different types )
    */
    src_nr_item = reiserfs_node_items (src);
    ih = reiserfs_ih_at (src, src_nr_item - 1);
    dih = reiserfs_ih_at (dest, 0);

    if (!dest_nr_item || reiserfs_leaf_mergeable (ih, dih, src->b_size) == 0)
	return 0;
  
    if ( reiserfs_ih_dir(ih)) {
	if ( bytes_or_entries == -1 )
	    /* bytes_or_entries = entries number in last item body of SOURCE */
	    bytes_or_entries = reiserfs_ih_get_entries(ih);
    
	reiserfs_lb_copy_entry (fs, dest_bi, src, LAST_TO_FIRST, 
				src_nr_item - 1, reiserfs_ih_get_entries(ih) - 
				bytes_or_entries, bytes_or_entries);
	return 1;
    }

    /* copy part of the body of the last item of SOURCE to the begin of the 
       body of the first item of the DEST; part defined by 'bytes_or_entries';
       if byte_or_entriess == -1 copy whole body; change first item key of the 
       DEST; don't create new item header */
  
    if ( bytes_or_entries == -1 ) {
	/* bytes_or_entries = length of last item body of SOURCE */
	bytes_or_entries = reiserfs_ih_get_len (ih);

	/* change first item key of the DEST */
	//dih->ih_key.k_offset = ih->ih_key.k_offset;
	reiserfs_key_set_off (reiserfs_key_format (&dih->ih_key), 
				 &dih->ih_key, 
				 reiserfs_key_get_off (&ih->ih_key));

	/* item becomes non-mergeable */
	/* or mergeable if left item was */
	//dih->ih_key.k_uniqueness = ih->ih_key.k_uniqueness;
	reiserfs_key_set_type (reiserfs_key_format (&dih->ih_key), 
			       &dih->ih_key, 
			       reiserfs_key_get_type (&ih->ih_key));
    } else {
	/* merge to right only part of item */
	/* change first item key of the DEST */
	if ( reiserfs_ih_direct(dih) ) {
	    //dih->ih_key.k_offset -= bytes_or_entries;
	    reiserfs_key_set_off (reiserfs_key_format (&dih->ih_key), 
				     &dih->ih_key, 
				     reiserfs_key_get_off (&dih->ih_key) -
				     bytes_or_entries);
	} else {
	    //dih->ih_key.k_offset -= ((bytes_or_entries/REISERFS_EXT_SIZE)*dest->b_size);
	    reiserfs_key_set_off (reiserfs_key_format (&dih->ih_key), 
				     &dih->ih_key, 
				     reiserfs_key_get_off (&dih->ih_key) - 
				     ((bytes_or_entries/REISERFS_EXT_SIZE) * 
				      dest->b_size));
	}
    }
  
    reiserfs_lb_insert_unit (fs, dest_bi, 0, 0, bytes_or_entries, 
			     reiserfs_item_by_ih(src,ih) + reiserfs_ih_get_len (ih) 
			     - bytes_or_entries, 0);
    return 1;
}

/* This function splits the (liquid) item into two items (useful when
   shifting part of an item into another node.) */
static void reiserfs_lb_split (reiserfs_filsys_t * fs,
			       reiserfs_bufinfo_t * dest_bi, 
			       reiserfs_bh_t * src, 
			       int last_first,
			       int item_num, 
			       int cpy_bytes)
{
    reiserfs_bh_t * dest = dest_bi->bi_bh;
    reiserfs_ih_t * ih;
  
    if ( last_first == FIRST_TO_LAST ) {
	/* if ( if item in position item_num in buffer 
	   SOURCE is directory item ) */
	if (reiserfs_ih_dir(ih = reiserfs_ih_at(src,item_num)))
	    reiserfs_lb_copy_entry (fs, dest_bi, src, FIRST_TO_LAST, 
				    item_num, 0, cpy_bytes);
	else {
	    reiserfs_ih_t n_ih;
      
	    /* copy part of the body of the item number 'item_num' of SOURCE 
	       to the end of the DEST part defined by 'cpy_bytes'; create new 
	       item header; change old item_header (????);
	       n_ih = new item_header;
	    */
	    memcpy (&n_ih, ih, REISERFS_IH_SIZE);
	    reiserfs_ih_set_len (&n_ih, cpy_bytes);
	    if (reiserfs_ih_ext(ih)) {
		//n_ih.u.ih_free_space = 0;
		reiserfs_ih_set_free (&n_ih, 0);;
	    }

	    //n_ih.ih_version = ih->ih_version;
	    reiserfs_ih_set_format (&n_ih, reiserfs_ih_get_format (ih));
	    reiserfs_ih_set_flags (&n_ih, reiserfs_ih_get_flags (ih));
	    reiserfs_lb_insert (fs, dest_bi, reiserfs_node_items(dest), &n_ih, 
				reiserfs_item_at (src, item_num), 0);
	}
    } else {
	/*  if ( if item in position item_num in buffer 
	    SOURCE is directory item ) */
	if (reiserfs_ih_dir(ih = reiserfs_ih_at (src, item_num)))
	    reiserfs_lb_copy_entry (fs, dest_bi, src, LAST_TO_FIRST, 
				    item_num, reiserfs_ih_get_entries(ih) - 
				    cpy_bytes, cpy_bytes);
	else {
	    reiserfs_ih_t n_ih;
      
	    /* copy part of the body of the item number 'item_num' of 
	       SOURCE to the begin of the DEST part defined by 'cpy_bytes'; 
	       create new item header;
	       
	       n_ih = new item_header;
	    */
	    memcpy (&n_ih, ih, REISERFS_KEY_SHSIZE);
      
	    if (reiserfs_ih_direct(ih)) {
		/*n_ih.ih_key.k_offset = ih->ih_key.k_offset + 
			ih->ih_item_len - cpy_bytes; */
		reiserfs_key_set_off (reiserfs_key_format (&ih->ih_key), 
					 &n_ih.ih_key, 
					 reiserfs_key_get_off (&ih->ih_key)
					 + reiserfs_ih_get_len (ih) - cpy_bytes);
		
		//n_ih.ih_key.k_uniqueness = TYPE_DIRECT;
		reiserfs_key_set_type (reiserfs_key_format (&ih->ih_key), 
				       &n_ih.ih_key, TYPE_DIRECT);
		//n_ih.u.ih_free_space = USHRT_MAX;
		reiserfs_ih_set_free (&n_ih, USHRT_MAX);
	    } else {
		/* extent item */
		/*n_ih.ih_key.k_offset = ih->ih_key.k_offset + 
		    (ih->ih_item_len - cpy_bytes) / REISERFS_EXT_SIZE * dest->b_size;*/
		reiserfs_key_set_off (reiserfs_key_format (&ih->ih_key), 
					 &n_ih.ih_key,
					 reiserfs_key_get_off (&ih->ih_key) 
					 + (reiserfs_ih_get_len (ih) - cpy_bytes) / 
					 REISERFS_EXT_SIZE * dest->b_size);
		
		//n_ih.ih_key.k_uniqueness = TYPE_EXTENT;
		reiserfs_key_set_type (reiserfs_key_format (&ih->ih_key), 
				       &n_ih.ih_key, TYPE_EXTENT);
		//n_ih.u.ih_free_space = ih->u.ih_free_space;
		reiserfs_ih_set_free (&n_ih, reiserfs_ih_get_free (ih));
	    }
      
	    /* set item length */
	    reiserfs_ih_set_len (&n_ih, cpy_bytes);
	    //n_ih.ih_version = ih->ih_version;
	    reiserfs_ih_set_format (&n_ih, reiserfs_ih_get_format (ih));
	    reiserfs_ih_set_flags (&n_ih, reiserfs_ih_get_flags (ih));
	    reiserfs_lb_insert (fs, dest_bi, 0, &n_ih, 
				reiserfs_item_at(src,item_num) + 
				reiserfs_ih_get_len (ih) - cpy_bytes, 0);
	}
    }
}

/* copy cpy_mun items from buffer src to buffer dest
 * last_first == FIRST_TO_LAST means, that we copy cpy_num  
   items beginning from first-th item in src to tail of dest
 * last_first == LAST_TO_FIRST means, that we copy cpy_num  items 
   beginning from first-th item in src to head of dest
 */
static void reiserfs_lb_copy_item (reiserfs_filsys_t * fs, 
				   reiserfs_bufinfo_t * dest_bi, 
				   reiserfs_bh_t * src, 
				   int last_first, 
				   int first, 
				   int cpy_num)
{
    reiserfs_bh_t * dest;
    int nr;
    int dest_before;
    int last_loc, last_inserted_loc, location;
    int i, j;
    reiserfs_node_head_t * blkh;
    reiserfs_ih_t * ih;

    dest = dest_bi->bi_bh;

    if (cpy_num == 0)
	return;

    blkh = NODE_HEAD(dest);
    nr = reiserfs_nh_get_items (blkh);
  
    /* we will insert items before 0-th or nr-th item in dest buffer. 
       It depends of last_first parameter */
    dest_before = (last_first == LAST_TO_FIRST) ? 0 : nr;

    /* location of head of first new item */
    ih = reiserfs_ih_at (dest, dest_before);

    /* prepare space for headers */
    memmove (ih + cpy_num, ih, (nr-dest_before) * REISERFS_IH_SIZE);

    /* copy item headers */
    memcpy (ih, reiserfs_ih_at (src, first), cpy_num * REISERFS_IH_SIZE);

    reiserfs_nh_set_free (blkh, reiserfs_nh_get_free (blkh) - REISERFS_IH_SIZE * cpy_num);

    /* location of unmovable item */
    j = location = (dest_before == 0) ? dest->b_size : reiserfs_ih_get_loc (ih-1);
    for (i = dest_before; i < nr + cpy_num; i ++) {
	location -= reiserfs_ih_get_len (&ih[i-dest_before]);
	reiserfs_ih_set_loc (&ih[i-dest_before], location);
    }

    /* prepare space for items */
    last_loc = reiserfs_ih_get_loc (&ih[nr+cpy_num-1-dest_before]);
    last_inserted_loc = reiserfs_ih_get_loc (&ih[cpy_num-1]);

    /* check free space */
    memmove (dest->b_data + last_loc,
	     dest->b_data + last_loc + j - last_inserted_loc,
	     last_inserted_loc - last_loc);

    /* copy items */
    memcpy (dest->b_data + last_inserted_loc, 
	    reiserfs_item_at(src,(first + cpy_num - 1)),
	    j - last_inserted_loc);

    /* sizes, item number */
    reiserfs_nh_set_items (blkh, reiserfs_nh_get_items (blkh) + cpy_num);
    reiserfs_nh_set_free (blkh, reiserfs_nh_get_free (blkh) - 
			 (j - last_inserted_loc));
    reiserfs_buffer_mkdirty (dest);

    if (dest_bi->bi_parent) {
	reiserfs_dc_t * dc;
	dc = reiserfs_int_at (dest_bi->bi_parent, dest_bi->bi_position);
	reiserfs_dc_set_size (dc, reiserfs_dc_get_size (dc) + j - 
			   last_inserted_loc + REISERFS_IH_SIZE * cpy_num);
	reiserfs_buffer_mkdirty(dest_bi->bi_parent);
    }
}

/* If cpy_bytes equals minus one than copy cpy_num whole items from SOURCE 
   to DEST. If cpy_bytes not equal to minus one than copy cpy_num-1 whole 
   items from SOURCE to DEST. From last item copy cpy_num bytes for regular 
   item and cpy_num directory entries for directory item. */
int reiserfs_lb_copy (reiserfs_filsys_t * fs,
		      reiserfs_bufinfo_t * dest_bi, 
		      reiserfs_bh_t * src,
		      int last_first, 
		      int cpy_num,
		      int cpy_bytes)
{
    reiserfs_bh_t * dest;
    int pos, i, src_nr_item, bytes;

    dest = dest_bi->bi_bh;
    if ( cpy_num == 0 )
	return 0;
 
    if ( last_first == FIRST_TO_LAST ) {
	/* copy items to left */
	pos = 0;
	if ( cpy_num == 1 )
	    bytes = cpy_bytes;
	else
	    bytes = -1;
   
	/* copy the first item or it part or nothing to the end of 
	   the DEST (i = reiserfs_lb_copy_boundary(DEST,SOURCE,0,bytes)) */
	i = reiserfs_lb_copy_boundary (fs, dest_bi, src, 
				       FIRST_TO_LAST, bytes);
	cpy_num -= i;
	if ( cpy_num == 0 )
	    return i;
	pos += i;
	if ( cpy_bytes == -1 )
	    /* copy first cpy_num items starting from position 'pos' of 
	       SOURCE to end of DEST */
	    reiserfs_lb_copy_item(fs, dest_bi, src, FIRST_TO_LAST, 
				  pos, cpy_num);
	else {
	    /* copy first cpy_num-1 items starting from position 'pos-1' 
	       of the SOURCE to the end of the DEST */
	    reiserfs_lb_copy_item(fs, dest_bi, src, FIRST_TO_LAST, 
				  pos, cpy_num-1);
	     
	    /* copy part of the item which number is cpy_num+pos-1 
	       to the end of the DEST */
	    reiserfs_lb_split (fs, dest_bi, src, FIRST_TO_LAST, 
			       cpy_num+pos-1, cpy_bytes);
	} 
    } else {
	/* copy items to right */
	src_nr_item = reiserfs_node_items (src);
	if ( cpy_num == 1 )
	    bytes = cpy_bytes;
	else
	    bytes = -1;
   
	/* copy the last item or it part or nothing to the begin of 
	   the DEST (i = reiserfs_lb_copy_boundary(DEST,SOURCE,1,bytes)); */
	i = reiserfs_lb_copy_boundary (fs, dest_bi, src, 
				       LAST_TO_FIRST, bytes);
   
	cpy_num -= i;
	if ( cpy_num == 0 )
	    return i;
   
	pos = src_nr_item - cpy_num - i;
	if ( cpy_bytes == -1 ) {
	    /* starting from position 'pos' copy last cpy_num 
	       items of SOURCE to begin of DEST */
	    reiserfs_lb_copy_item(fs, dest_bi, src, 
				  LAST_TO_FIRST, 
				  pos, cpy_num);
	} else {
	    /* copy last cpy_num-1 items starting from position 'pos+1' 
	       of the SOURCE to the begin of the DEST; */
	    reiserfs_lb_copy_item(fs, dest_bi, src, 
				  LAST_TO_FIRST, 
				  pos+1, cpy_num-1);

	    /* copy part of the item which number 
	       is pos to the begin of the DEST */
	    reiserfs_lb_split (fs, dest_bi, src, 
			       LAST_TO_FIRST, 
			       pos, cpy_bytes);
	}
    }
    return i;
}




/* there are types of coping: from S[0] to L[0], from S[0] to R[0],
   from R[0] to L[0]. for each of these we have to define parent and
   positions of destination and source buffers */
static void reiserfs_lb_move_prep (int shift_mode, 
				   reiserfs_tb_t * tb, 
				   reiserfs_bufinfo_t * dest_bi,
				   reiserfs_bufinfo_t * src_bi, 
				   int * first_last,
				   reiserfs_bh_t * Snew)
{
    /* define dest, src, dest parent, dest position */
    switch (shift_mode) {
    case LEAF_FROM_S_TO_L:    /* it is used in reiserfs_lb_shift_left */
	src_bi->bi_bh = REISERFS_PATH_LEAF (tb->tb_path);
	src_bi->bi_parent = REISERFS_PATH_UPPARENT (tb->tb_path, 0);
	/* src->b_item_order */
	src_bi->bi_position = REISERFS_PATH_UPPARENT_POS (tb->tb_path, 0);	
	dest_bi->bi_bh = tb->L[0];
	dest_bi->bi_parent = tb->FL[0];
	dest_bi->bi_position = reiserfs_tb_lpos (tb, 0);
	*first_last = FIRST_TO_LAST;
	break;

    case LEAF_FROM_S_TO_R:  /* it is used in reiserfs_lb_shift_right */
	src_bi->bi_bh = REISERFS_PATH_LEAF (tb->tb_path);
	src_bi->bi_parent = REISERFS_PATH_UPPARENT (tb->tb_path, 0);
	src_bi->bi_position = REISERFS_PATH_UPPARENT_POS (tb->tb_path, 0);
	dest_bi->bi_bh = tb->R[0];
	dest_bi->bi_parent = tb->FR[0];
	dest_bi->bi_position = reiserfs_tb_rpos (tb, 0);
	*first_last = LAST_TO_FIRST;
	break;

    case LEAF_FROM_R_TO_L:  /* it is used in balance_leaf_when_delete */
	src_bi->bi_bh = tb->R[0];
	src_bi->bi_parent = tb->FR[0];
	src_bi->bi_position = reiserfs_tb_rpos (tb, 0);
	dest_bi->bi_bh = tb->L[0];
	dest_bi->bi_parent = tb->FL[0];
	dest_bi->bi_position = reiserfs_tb_lpos (tb, 0);
	*first_last = FIRST_TO_LAST;
	break;
    
    case LEAF_FROM_L_TO_R:  /* it is used in balance_leaf_when_delete */
	src_bi->bi_bh = tb->L[0];
	src_bi->bi_parent = tb->FL[0];
	src_bi->bi_position = reiserfs_tb_lpos (tb, 0);
	dest_bi->bi_bh = tb->R[0];
	dest_bi->bi_parent = tb->FR[0];
	dest_bi->bi_position = reiserfs_tb_rpos (tb, 0);
	*first_last = LAST_TO_FIRST;
	break;

    case LEAF_FROM_S_TO_SNEW:
	src_bi->bi_bh = REISERFS_PATH_LEAF (tb->tb_path);
	src_bi->bi_parent = REISERFS_PATH_UPPARENT (tb->tb_path, 0);
	src_bi->bi_position = REISERFS_PATH_UPPARENT_POS (tb->tb_path, 0);
	dest_bi->bi_bh = Snew;
	dest_bi->bi_parent = 0;
	dest_bi->bi_position = 0;
	*first_last = LAST_TO_FIRST;
	break;
    
    default:
	reiserfs_panic (0, "vs-10250: reiserfs_lb_move_prep: "
			"shift type is unknown (%d)", shift_mode);
    }
}

/* cuts DEL_COUNT entries beginning from FROM-th entry. Directory item
   does not have free space, so it moves DEHs and remaining records as
   necessary. Return value is size of removed part of directory item
   in bytes. */
static int reiserfs_lb_delete_entry (reiserfs_bh_t * bh,
				     reiserfs_ih_t * ih, 
				     int from, 
				     int del_count)
{
    char * item;
    reiserfs_deh_t * deh;
    int prev_record_offset;	/* offset of record, that is (from-1)th */
    char * prev_record;		/* */
    int cut_records_len;		/* length of all removed records */
    int i;
    int entry_count;


    /* first byte of item */
    item = reiserfs_item_by_ih (bh, ih);

    /* entry head array */
    deh = reiserfs_deh (bh, ih);
    entry_count = reiserfs_ih_get_entries (ih);

    if (del_count == 0) {
	int shift;
	int last_location;

	last_location = reiserfs_deh_get_loc (deh + entry_count - 1);
	shift = last_location - REISERFS_DEH_SIZE * entry_count;
	
	memmove (deh + entry_count, item + last_location,
		 reiserfs_ih_get_len (ih) - last_location);
	for (i = 0; i < entry_count; i ++)
	    reiserfs_deh_set_loc (&deh[i], reiserfs_deh_get_loc (&deh[i]) - shift);

	return shift;
    }

    /* first byte of remaining entries, those are BEFORE cut entries
       (prev_record) and length of all removed records (cut_records_len) */
    prev_record_offset = (from ? reiserfs_deh_get_loc (&deh[from - 1]) : 
			  reiserfs_ih_get_len (ih));
    
    if (from + del_count == entry_count) {
	cut_records_len = prev_record_offset/*from_record*/ - 
		REISERFS_DEH_SIZE * entry_count;
    } else {
	cut_records_len = prev_record_offset/*from_record*/ - 
		reiserfs_deh_get_loc (&deh[from + del_count - 1]);
    }
    
    prev_record = item + prev_record_offset;


    /* adjust locations of remaining entries */
    for (i = reiserfs_ih_get_entries (ih) - 1; i > from + del_count - 1; i --) {
	reiserfs_deh_set_loc (deh + i, reiserfs_deh_get_loc (deh + i) - 
			  (REISERFS_DEH_SIZE * del_count));
    }

    for (i = 0; i < from; i ++) {
	reiserfs_deh_set_loc (deh + i, reiserfs_deh_get_loc (deh + i) - 
			  (REISERFS_DEH_SIZE * del_count + cut_records_len));
    }

    reiserfs_ih_set_entries (ih, reiserfs_ih_get_entries (ih) - del_count);

    /* shift entry head array and entries those are AFTER removed entries */
    memmove ((char *)(deh + from),
	     deh + from + del_count, 
	     prev_record - cut_records_len - (char *)(deh + from + del_count));
  
    /* shift records, those are BEFORE removed entries */
    memmove (prev_record - cut_records_len - REISERFS_DEH_SIZE * del_count,
	     prev_record, item + reiserfs_ih_get_len (ih) - prev_record);

    return REISERFS_DEH_SIZE * del_count + cut_records_len;
}


/*  when cut item is part of regular file
        pos_in_item - first byte that must be cut
        cut_size - number of bytes to be cut beginning from pos_in_item
 
   when cut item is part of directory
        pos_in_item - number of first deleted entry
        cut_size - count of deleted entries
    */
void reiserfs_lb_delete_unit (reiserfs_filsys_t * fs,
			      reiserfs_bufinfo_t * bi, 
			      int cut_item_num,
			      int pos_in_item, 
			      int cut_size)
{
    int nr;
    reiserfs_bh_t * bh = bi->bi_bh;
    reiserfs_node_head_t * blkh;
    reiserfs_ih_t * ih;
    int last_loc, unmoved_loc;
    int i;

    blkh = NODE_HEAD (bh);
    nr = reiserfs_nh_get_items (blkh);

    /* item head of truncated item */
    ih = reiserfs_ih_at (bh, cut_item_num);

    if (reiserfs_ih_dir (ih)) {
        /* first cut entry ()*/
        cut_size = reiserfs_lb_delete_entry (bh, ih, pos_in_item, cut_size);
        if (pos_in_item == 0) {
            /* change item key by key of first entry in the item */
	    reiserfs_key_set_off1 (&ih->ih_key, reiserfs_deh_get_off (reiserfs_deh (bh, ih)));
            /*memcpy (&ih->ih_key.k_offset, 
		      &(reiserfs_deh (bh, ih)->deh_offset), 
		      REISERFS_KEY_SHSIZE);*/
	}
    } else {
        /* item is direct or extent */
        /* shift item body to left if cut is from the head of item */
        if (pos_in_item == 0) {
	    memmove (bh->b_data + reiserfs_ih_get_loc (ih),
		     bh->b_data + reiserfs_ih_get_loc (ih) + cut_size,
		     reiserfs_ih_get_len (ih) - cut_size);

            /* change key of item */
            if (reiserfs_ih_direct(ih)) {
                //ih->ih_key.k_offset += cut_size;
                reiserfs_key_set_off (reiserfs_key_format (&ih->ih_key), 
					 &ih->ih_key, 
					 reiserfs_key_get_off (&ih->ih_key)
					 + cut_size);
            } else {
                //ih->ih_key.k_offset += (cut_size / REISERFS_EXT_SIZE) * bh->b_size;
                reiserfs_key_set_off (reiserfs_key_format (&ih->ih_key), 
					 &ih->ih_key, 
					 reiserfs_key_get_off (&ih->ih_key) + 
					 (cut_size / REISERFS_EXT_SIZE) * bh->b_size);
	    }
	}
    }
  

    /* location of the last item */
    last_loc = reiserfs_ih_get_loc (&ih[nr - cut_item_num - 1]);

    /* location of the item, which is remaining at the same place */
    unmoved_loc = cut_item_num ? reiserfs_ih_get_loc (ih-1) : bh->b_size;


    /* shift */
    memmove (bh->b_data + last_loc + cut_size, bh->b_data + last_loc,
	       unmoved_loc - last_loc - cut_size);

    /* change item length */
    reiserfs_ih_set_len (ih, reiserfs_ih_get_len (ih) - cut_size);
  
    if (reiserfs_ih_ext(ih)) {
        if (pos_in_item)
            //ih->u.ih_free_space = 0;
            reiserfs_ih_set_free (ih, 0);
    }

    /* change locations */
    for (i = cut_item_num; i < nr; i ++) {
	reiserfs_ih_set_loc (&ih[i-cut_item_num], 
			 reiserfs_ih_get_loc (&ih[i-cut_item_num]) + cut_size);
    }

    /* size, free space */
    reiserfs_nh_set_free (blkh, reiserfs_nh_get_free (blkh) + cut_size);

    reiserfs_buffer_mkdirty(bh);
    
    if (bi->bi_parent) {
	reiserfs_dc_t * dc;

	dc = reiserfs_int_at (bi->bi_parent, bi->bi_position);
	reiserfs_dc_set_size (dc, reiserfs_dc_get_size (dc) - cut_size);
	reiserfs_buffer_mkdirty(bi->bi_parent);
    }
    if (reiserfs_leaf_valid(bh) != NT_LEAF && 
	reiserfs_leaf_valid(bh) != NT_IH_ARRAY)
    {
	reiserfs_panic ("reiserfs_lb_delete_unit: bad leaf %lu: %b",
			bh->b_blocknr, bh);
    }
}


/* delete del_num items from buffer starting from the first'th item */
void reiserfs_lb_delete_item (reiserfs_filsys_t * fs,
			      reiserfs_bufinfo_t * bi,
			      int first, 
			      int del_num)
{
    reiserfs_bh_t * bh = bi->bi_bh;
    int nr;
    int i, j;
    int last_loc, last_removed_loc;
    reiserfs_node_head_t * blkh;
    reiserfs_ih_t * ih;

    if (del_num == 0)
	return;
    
    blkh = NODE_HEAD (bh);
    nr = reiserfs_nh_get_items (blkh);

    if (first == 0 && del_num == nr) {
	/* this does not work */
	reiserfs_tb_attach_new (bi);
	
	reiserfs_buffer_mkdirty(bh);
	return;
    }
    
    ih = reiserfs_ih_at (bh, first);
    
    /* location of unmovable item */
    j = (first == 0) ? bh->b_size : reiserfs_ih_get_loc (ih-1);
    
    /* delete items */
    last_loc = reiserfs_ih_get_loc (&ih[nr - 1 - first]);
    last_removed_loc = reiserfs_ih_get_loc (&ih[del_num-1]);
    
    memmove (bh->b_data + last_loc + j - last_removed_loc,
	     bh->b_data + last_loc, last_removed_loc - last_loc);
    
    /* delete item headers */
    memmove (ih, ih + del_num, (nr - first - del_num) * REISERFS_IH_SIZE);
    
    /* change item location */
    for (i = first; i < nr - del_num; i ++) {
	reiserfs_ih_set_loc (&ih[i-first], 
			 reiserfs_ih_get_loc (&ih[i-first]) + j - last_removed_loc);
    }
    
    /* sizes, item number */
    reiserfs_nh_set_items (blkh, reiserfs_nh_get_items (blkh)/*nr*/ - del_num);
    reiserfs_nh_set_free (blkh, reiserfs_nh_get_free (blkh) + j - 
			 last_removed_loc + REISERFS_IH_SIZE * del_num);
    
    reiserfs_buffer_mkdirty(bh);
    
    if (bi->bi_parent) {
	reiserfs_dc_t * dc;

	dc = reiserfs_int_at (bi->bi_parent, bi->bi_position);
	reiserfs_dc_set_size (dc, reiserfs_dc_get_size (dc) - 
	    (j - last_removed_loc + REISERFS_IH_SIZE * del_num));
	reiserfs_buffer_mkdirty(bi->bi_parent);
    }
    if (reiserfs_leaf_valid(bh) != NT_LEAF && 
	reiserfs_leaf_valid(bh) != NT_IH_ARRAY)
    {
	reiserfs_panic ("reiserfs_lb_delete_item: bad leaf %lu: %b", 
			bh->b_blocknr, bh);
    }
}

/*  If del_bytes == -1, starting from position 'first' delete del_num items in 
    whole in buffer CUR. If not. If last_first == 0. Starting from position 
    'first' delete del_num-1 items in whole. Delete part of body of the first 
    item. Part defined by del_bytes. Don't delete first item header If 
    last_first == 1. Starting from position 'first+1' delete del_num-1 items 
    in whole. Delete part of body of the last item . Part defined by del_bytes.
    Don't delete last item header. */
void reiserfs_lb_delete (reiserfs_filsys_t * fs,
			 reiserfs_bufinfo_t * cur_bi,
			 int last_first, 
			 int first, 
			 int del_num, 
			 int del_bytes)
{
    reiserfs_bh_t * bh;
    int item_amount = reiserfs_node_items (bh = cur_bi->bi_bh);
    
    if ( del_num == 0 )
	return;
    
    if ( first == 0 && del_num == item_amount && del_bytes == -1 ) {
	reiserfs_tb_attach_new (cur_bi);
	reiserfs_buffer_mkdirty (bh);
	return;
    }
    
    if ( del_bytes == -1 )
	/* delete del_num items beginning from item in position first */
	reiserfs_lb_delete_item (fs, cur_bi, first, del_num);
    else {
	if ( last_first == FIRST_TO_LAST ) {
	    /* delete del_num-1 items beginning from item in position first  */
	    reiserfs_lb_delete_item (fs, cur_bi, first, del_num-1);
	    
	    /* delete the part of the first item of the bh do not
	       delete item header */
	    reiserfs_lb_delete_unit (fs, cur_bi, 0, 0, del_bytes);
	} else  {
	    reiserfs_ih_t * ih;
	    int len;
	    
	    /* delete del_num-1 items beginning from item in position first+1  */
	    reiserfs_lb_delete_item (fs, cur_bi, first+1, del_num-1);
	    
	    /* the last item is directory  */
	    if (reiserfs_ih_dir(ih = reiserfs_ih_at(bh, reiserfs_node_items(bh)-1))) 	
	        /* len = numbers of directory entries in this item */
	        len = reiserfs_ih_get_entries(ih);
	    else
		/* len = body len of item */
 	        len = reiserfs_ih_get_len (ih);
	    
	    /* delete the part of the last item of the bh 
	       do not delete item header */
	    reiserfs_lb_delete_unit (fs, cur_bi, reiserfs_node_items(bh) - 1, 
				     len - del_bytes, del_bytes);
	}
    }
}

/* copy mov_num items and mov_bytes of the (mov_num-1)th item to
   neighbor. Delete them from source */
int reiserfs_lb_move (int shift_mode, 
		      reiserfs_tb_t * tb, 
		      int mov_num, 
		      int mov_bytes, 
		      reiserfs_bh_t * Snew)
{
    int ret_value;
    reiserfs_bufinfo_t dest_bi, src_bi;
    int first_last;

    reiserfs_lb_move_prep (shift_mode, tb, &dest_bi, 
			   &src_bi, &first_last, Snew);

    ret_value = reiserfs_lb_copy (tb->tb_fs, &dest_bi, src_bi.bi_bh, 
				  first_last, mov_num, mov_bytes);

    reiserfs_lb_delete (tb->tb_fs, &src_bi, first_last, 
			(first_last == FIRST_TO_LAST) ? 0 : 
			(reiserfs_node_items(src_bi.bi_bh) - mov_num), 
			mov_num, mov_bytes);

    return ret_value;
}


/* Shift shift_num items (and shift_bytes of last shifted item if 
   shift_bytes != -1) from S[0] to L[0] and replace the delimiting key */
int reiserfs_lb_shift_left (reiserfs_tb_t * tb, 
			    int shift_num, 
			    int shift_bytes)
{
    reiserfs_bh_t * S0 = REISERFS_PATH_LEAF (tb->tb_path);
    int i;

    /* move shift_num (and shift_bytes bytes) items from S[0] to 
       left neighbor L[0] */
    i = reiserfs_lb_move (LEAF_FROM_S_TO_L, tb, shift_num, shift_bytes, 0);

    if ( shift_num ) {
	if (reiserfs_node_items (S0) == 0) { 
	    /* everything is moved from S[0] */
	    if (REISERFS_PATH_UPPOS (tb->tb_path, 1) == 0)
		reiserfs_node_replace_key(tb->CFL[0], tb->lkey[0], 
					  REISERFS_PATH_UPPARENT (tb->tb_path, 0), 0);
	} else {
	    /* replace lkey in CFL[0] by 0-th key from S[0]; */
	    reiserfs_node_replace_key(tb->CFL[0], tb->lkey[0], S0, 0);
	}
    }

    return i;
}

/* Shift shift_num (shift_bytes) items from S[0] to the right neighbor, 
   and replace the delimiting key */
int reiserfs_lb_shift_right (reiserfs_tb_t * tb, 
			     int shift_num, 
			     int shift_bytes)
{
    int ret_value;

    /* move shift_num (and shift_bytes) items from S[0] 
       to right neighbor R[0] */
    ret_value = reiserfs_lb_move (LEAF_FROM_S_TO_R, tb, 
				    shift_num, shift_bytes, 0);

    /* replace rkey in CFR[0] by the 0-th key from R[0] */
    if (shift_num) {
	reiserfs_node_replace_key(tb->CFR[0], tb->rkey[0], tb->R[0], 0);
    }

    return ret_value;
}

/* Balance leaf node in case of delete or cut: insert_size[0] < 0
 *
 * lnum, rnum can have values >= -1
 *	-1 means that the neighbor must be joined with S
 *	 0 means that nothing should be done with the neighbor
 *	>0 means to shift entirely or partly the specified number of 
 *         items to the neighbor
 */
static int reiserfs_lb_balance_delete(reiserfs_tb_t * tb, int flag)
{
    reiserfs_bh_t * tbS0 = REISERFS_PATH_LEAF (tb->tb_path);
    int item_pos = REISERFS_PATH_LEAF_POS (tb->tb_path);
    int pos_in_item = tb->tb_path->pos_in_item;
    reiserfs_bufinfo_t bi;
    int n;
    reiserfs_ih_t * ih;

    ih = reiserfs_ih_at (tbS0, item_pos);

    /* Delete or truncate the item */

    switch (flag) {
    case M_DELETE:   /* delete item in S[0] */

	bi.bi_bh = tbS0;
	bi.bi_parent = REISERFS_PATH_UPPARENT (tb->tb_path, 0);
	bi.bi_position = REISERFS_PATH_UPPOS (tb->tb_path, 1);
	reiserfs_lb_delete (tb->tb_fs, &bi, 0, item_pos, 1, -1);

	if ( ! item_pos ) {
	    /* we have removed first item in the node - 
		update left delimiting key */
	    if ( reiserfs_node_items(tbS0) ) {
		reiserfs_node_replace_key(tb->CFL[0], tb->lkey[0], tbS0, 0);
	    } else {
		if ( ! REISERFS_PATH_UPPOS (tb->tb_path, 1) ) {
		    reiserfs_node_replace_key(tb->CFL[0], tb->lkey[0],
					      REISERFS_PATH_UPPARENT(tb->tb_path, 0),0);
		}
	    }
	} 
    
	break;

    case M_CUT: {  /* cut item in S[0] */
	bi.bi_bh = tbS0;
	bi.bi_parent = REISERFS_PATH_UPPARENT (tb->tb_path, 0);
	bi.bi_position = REISERFS_PATH_UPPOS (tb->tb_path, 1);
	if (reiserfs_ih_dir (ih)) {
	    /* UFS unlink semantics are such that you can only delete
               one directory entry at a time. */
	    /* when we cut a directory tb->insert_size[0] means number
               of entries to be cut (always 1) */
	    tb->insert_size[0] = -1;
	    reiserfs_lb_delete_unit (tb->tb_fs, &bi, item_pos, 
				     pos_in_item, -tb->insert_size[0]);

	    if ( ! item_pos && ! pos_in_item ) {
		reiserfs_node_replace_key(tb->CFL[0], tb->lkey[0], tbS0, 0);
	    }
	} else {
	    reiserfs_lb_delete_unit (tb->tb_fs, &bi, item_pos, 
				     pos_in_item, -tb->insert_size[0]);
	}
	break;
    }

    default:
	print_tb(flag, item_pos, pos_in_item, tb,"when_del");
	reiserfs_panic ("PAP-12040: reiserfs_lb_balance_delete: "
			"unexpectable mode: %s(%d)", 
			(flag == M_PASTE) ? "PASTE" : 
			((flag == M_INSERT) ? "INSERT" : "UNKNOWN"), flag);
    }

    /* the rule is that no shifting occurs unless by 
       shifting a node can be freed */
    n = reiserfs_node_items(tbS0);
    if ( tb->lnum[0] )     /* L[0] takes part in balancing */
    {
	if ( tb->lnum[0] == -1 )    /* L[0] must be joined with S[0] */
	{
	    if ( tb->rnum[0] == -1 )    /* R[0] must be also joined with S[0] */
	    {			
		if ( tb->FR[0] == REISERFS_PATH_UPPARENT(tb->tb_path, 0) )
		{
		    /* all contents of all the 3 buffers will be in L[0] */
		    if ((REISERFS_PATH_UPPOS (tb->tb_path, 1) == 0) && 
			(1 < reiserfs_node_items(tb->FR[0])))
		    {
			reiserfs_node_replace_key(tb->CFL[0], tb->lkey[0],
						  tb->FR[0], 1);
		    }

		    reiserfs_lb_move (LEAF_FROM_S_TO_L, tb, n, -1, 0);
		    reiserfs_lb_move (LEAF_FROM_R_TO_L, tb, 
					reiserfs_node_items(tb->R[0]), -1, 0);

		    reiserfs_node_forget (tb->tb_fs, tbS0->b_blocknr);
		    reiserfs_node_forget (tb->tb_fs, tb->R[0]->b_blocknr);

		    return 0;
		}
		/* all contents of all the 3 buffers will be in R[0] */
		reiserfs_lb_move(LEAF_FROM_S_TO_R, tb, n, -1, 0);
		reiserfs_lb_move(LEAF_FROM_L_TO_R, tb, 
				   reiserfs_node_items(tb->L[0]), -1, 0);

		/* right_delimiting_key is correct in R[0] */
		reiserfs_node_replace_key(tb->CFR[0], tb->rkey[0], tb->R[0], 0);

		reiserfs_node_forget (tb->tb_fs, tbS0->b_blocknr);
		reiserfs_node_forget (tb->tb_fs, tb->L[0]->b_blocknr);

		return -1;
	    }

	    /* all contents of L[0] and S[0] will be in L[0] */
	    reiserfs_lb_shift_left(tb, n, -1);

	    reiserfs_node_forget (tb->tb_fs, tbS0->b_blocknr);

	    return 0;
	}
	/* a part of contents of S[0] will be in L[0] and the rest 
	   part of S[0] will be in R[0] */

	reiserfs_lb_shift_left (tb, tb->lnum[0], tb->lbytes);
	reiserfs_lb_shift_right(tb, tb->rnum[0], tb->rbytes);

	reiserfs_node_forget (tb->tb_fs, tbS0->b_blocknr);

	return 0;
    }

    if ( tb->rnum[0] == -1 ) {
	/* all contents of R[0] and S[0] will be in R[0] */
	reiserfs_lb_shift_right(tb, n, -1);
	reiserfs_node_forget (tb->tb_fs, tbS0->b_blocknr);
	return 0;
    }
    return 0;
}

int reiserfs_lb_balance(reiserfs_tb_t * tb,
			  /* item header of inserted item */
			  reiserfs_ih_t * ih, 
			  /* body  of inserted item or bytes to paste */
			  const char * body,		
			  /* i - insert, d - delete, c - cut, p - paste
			     (see comment to reiserfs_tb_balance) */
			  int flag,			
			  /* it is always 0 */
			  int zeros_number,
			  /* in our processing of one level we sometimes 
			     determine what must be inserted into the next 
			     higher level.  This insertion consists of a 
			     key or two keys and their corresponding
			     pointers */
			  reiserfs_ih_t * insert_key,  
			  /* inserted node-ptrs for the next level */
			  reiserfs_bh_t ** insert_ptr)
{
    /* position in item, in bytes for direct and
       extent items, in entries for directories (for
       which it is an index into the array of directory
       entry headers.) */
    int pos_in_item = tb->tb_path->pos_in_item; 
    reiserfs_bh_t * tbS0 = REISERFS_PATH_LEAF (tb->tb_path);
/*  reiserfs_bh_t * tbF0 = REISERFS_PATH_UPPARENT (tb->tb_path, 0);
    int S0_b_item_order = REISERFS_PATH_UPPARENT_POS (tb->tb_path, 0);*/
    /*  index into the array of item headers in S[0] 
	of the affected item */
    int item_pos = REISERFS_PATH_LEAF_POS (tb->tb_path);	
    reiserfs_bufinfo_t bi;
    /* new nodes allocated to hold what could not fit into S */
    reiserfs_bh_t *S_new[2];  
    /* number of items that will be placed into S_new (includes partially 
       shifted items) */
    int snum[2];
    /* if an item is partially shifted into S_new then 
       if it is a directory item 
       it is the number of entries from the item that are shifted into S_new
       else
       it is the number of bytes from the item that are shifted into S_new */
    int sbytes[2];
    int n, i;
    int ret_val;

    /* Make balance in case insert_size[0] < 0 */
    if ( tb->insert_size[0] < 0 )
	return reiserfs_lb_balance_delete (/*th,*/ tb, flag);
  
    /* for extent item pos_in_item is measured in unformatted node
       pointers. Recalculate to bytes */
    if (flag != M_INSERT && reiserfs_ih_ext (reiserfs_ih_at (tbS0, item_pos)))
	pos_in_item *= REISERFS_EXT_SIZE;

    if ( tb->lnum[0] > 0 ) {
	/* Shift lnum[0] items from S[0] to the left neighbor L[0] */
	if ( item_pos < tb->lnum[0] ) {
	    /* new item or it part falls to L[0], shift it too */
	    n = reiserfs_node_items(tb->L[0]);

	    switch (flag) {
	    case M_INSERT:   /* insert item into L[0] */

		if ( item_pos == tb->lnum[0] - 1 && tb->lbytes != -1 ) {
		    /* part of new item falls into L[0] */
		    int new_item_len;

		    ret_val = reiserfs_lb_shift_left (tb, tb->lnum[0]-1, -1);
		    /* Calculate item length to insert to S[0] */
		    new_item_len = reiserfs_ih_get_len (ih) - tb->lbytes;
		    /* Calculate and check item length to insert to L[0] */
		    reiserfs_ih_set_len (ih, reiserfs_ih_get_len (ih) - new_item_len);
		    /* Insert new item into L[0] */
		    bi.bi_bh = tb->L[0];
		    bi.bi_parent = tb->FL[0];
		    bi.bi_position = reiserfs_tb_lpos (tb, 0);
		    reiserfs_lb_insert (tb->tb_fs, &bi, n + 
					item_pos - ret_val, ih, body,
					zeros_number > reiserfs_ih_get_len (ih) ? 
					reiserfs_ih_get_len (ih) : zeros_number);

		    /* Calculate key component, item length and body to insert 
		       into S[0] */
		    //ih->ih_key.k_offset += tb->lbytes;
		    reiserfs_key_set_off (
			reiserfs_key_format (&ih->ih_key), &ih->ih_key, 
			reiserfs_key_get_off (&ih->ih_key) + tb->lbytes * 
			(reiserfs_ih_ext(ih) ? tb->tb_fs->fs_blocksize / 
			 REISERFS_EXT_SIZE : 1) );
		    
		    reiserfs_ih_set_len (ih, new_item_len);
		    if ( tb->lbytes >  zeros_number ) {
			body += (tb->lbytes - zeros_number);
			zeros_number = 0;
		    }
		    else
			zeros_number -= tb->lbytes;
		} else {
		    /* new item in whole falls into L[0] */
		    /* Shift lnum[0]-1 items to L[0] */
		    ret_val = reiserfs_lb_shift_left(tb, tb->lnum[0]-1, 
						       tb->lbytes);

		    /* Insert new item into L[0] */
		    bi.bi_bh = tb->L[0];
		    bi.bi_parent = tb->FL[0];
		    bi.bi_position = reiserfs_tb_lpos (tb, 0);
		    reiserfs_lb_insert (tb->tb_fs, &bi, n + item_pos - ret_val,
					ih, body, zeros_number);

		    tb->insert_size[0] = 0;
		    zeros_number = 0;
		}
		break;

	    case M_PASTE:   /* append item in L[0] */

		if ( item_pos == tb->lnum[0] - 1 && tb->lbytes != -1 ) {
		    /* we must shift the part of the appended item */
		    if ( reiserfs_ih_dir (reiserfs_ih_at (tbS0, item_pos))) {
			/* directory item */
			if ( tb->lbytes > pos_in_item ) {
			    /* new directory entry falls into L[0] */
			    reiserfs_ih_t * pasted;
			    int l_pos_in_item = pos_in_item;
							  
			    /* Shift lnum[0] - 1 items in whole. Shift 
			       lbytes - 1 entries from given directory item */
			    ret_val = reiserfs_lb_shift_left(tb, tb->lnum[0], 
							       tb->lbytes - 1);
			    if ( ret_val && ! item_pos ) {
				pasted =  reiserfs_ih_at(tb->L[0],
						     reiserfs_node_items(tb->L[0])-1);
				
				l_pos_in_item += reiserfs_ih_get_entries(pasted) - 
					(tb->lbytes-1);
			    }

			    /* Append given directory entry to directory item */
			    bi.bi_bh = tb->L[0];
			    bi.bi_parent = tb->FL[0];
			    bi.bi_position = reiserfs_tb_lpos(tb, 0);
			    reiserfs_lb_insert_unit (tb->tb_fs, &bi, n + 
						     item_pos - ret_val, 
						     l_pos_in_item,
						     tb->insert_size[0], 
						     body, zeros_number);

			    /* previous string prepared space for pasting new 
			       entry, following string pastes this entry */
			    /* when we have merge directory item, pos_in_item 
			       has been changed too */
			    /* paste new directory entry. 1 is entry number */
			    reiserfs_lb_insert_entry (bi.bi_bh, n + item_pos - 
						      ret_val, l_pos_in_item, 1,
						      (reiserfs_deh_t *)body,
						      body + REISERFS_DEH_SIZE, 
						      tb->insert_size[0]
				);
			    tb->insert_size[0] = 0;
			} else {
			    /* new directory item doesn't fall into L[0] */
			    /* Shift lnum[0]-1 items in whole. Shift lbytes 
			       directory entries from directory item number 
			       lnum[0] */
			    reiserfs_lb_shift_left (tb, tb->lnum[0], 
						      tb->lbytes);
			}
			/* Calculate new position to append in item body */
			pos_in_item -= tb->lbytes;
		    }
		    else {
			/* regular object */
			if ( tb->lbytes >= pos_in_item ) {
			    /* appended item will be in L[0] in whole */
			    int l_n, temp_n;
                            reiserfs_key_t * key;

			    /* this bytes number must be appended to the last 
			       item of L[h] */
			    l_n = tb->lbytes - pos_in_item;

			    /* Calculate new insert_size[0] */
			    tb->insert_size[0] -= l_n;

			    ret_val = reiserfs_lb_shift_left(tb, tb->lnum[0],
					reiserfs_ih_get_len(reiserfs_ih_at(tbS0, item_pos)));
			    
			    /* Append to body of item in L[0] */
			    bi.bi_bh = tb->L[0];
			    bi.bi_parent = tb->FL[0];
			    bi.bi_position = reiserfs_tb_lpos (tb, 0);
			    reiserfs_lb_insert_unit(tb->tb_fs, 
					&bi,n + item_pos - ret_val,
					reiserfs_ih_get_len (reiserfs_ih_at(tb->L[0], 
							n+item_pos-ret_val)),
					l_n,body, zeros_number > l_n ? 
					l_n : zeros_number);


			    /* 0-th item in S0 can be only of DIRECT type 
			       when l_n != 0*/
			    //reiserfs_ih_key_at (tbS0, 0)->k_offset += l_n;z
			    key = reiserfs_ih_key_at (tbS0, 0);
                            temp_n = reiserfs_ih_ext(reiserfs_ih_at (tb->L[0], 
					n + item_pos - ret_val)) ?
				    (int)((l_n / REISERFS_EXT_SIZE) * 
					  tb->tb_fs->fs_blocksize) : l_n;

			    reiserfs_key_set_off (
				reiserfs_key_format (key), key, 
				reiserfs_key_get_off (key) + temp_n);

			    /*reiserfs_int_key_at(tb->CFL[0],
				tb->lkey[0])->k_offset += l_n; */
			    key = reiserfs_int_key_at(tb->CFL[0],tb->lkey[0]);
			    reiserfs_key_set_off (
				reiserfs_key_format (key), key, 
				reiserfs_key_get_off (key) + temp_n);

			    /* Calculate new body, position in item and 
			       insert_size[0] */
			    if ( l_n > zeros_number ) {
				body += (l_n - zeros_number);
				zeros_number = 0;
			    }
			    else
				zeros_number -= l_n;
			    pos_in_item = 0;	
			}
			else {
			    /* only part of the appended item will be in L[0] */

			    /* Calculate position in item for append in S[0] */
			    pos_in_item -= tb->lbytes;

			    /* Shift lnum[0] - 1 items in whole. Shift 
			       lbytes - 1 byte from item number lnum[0] */
			    reiserfs_lb_shift_left(tb,tb->lnum[0],
						     tb->lbytes);
			}
		    }
		} else {
		    /* appended item will be in L[0] in whole */
		    reiserfs_ih_t * pasted;
		
		    if ( ! item_pos  && 
			 reiserfs_tree_left_mergeable (tb->tb_fs, 
						       tb->tb_path) == 1 )
			{ /* if we paste into first item of S[0] and it 
			     is left mergable then increment pos_in_item 
			     by the size of the last item in L[0] */
			    pasted = reiserfs_ih_at(tb->L[0],n-1);
			    if ( reiserfs_ih_dir(pasted) )
				pos_in_item += reiserfs_ih_get_entries (pasted);
			    else
				pos_in_item += reiserfs_ih_get_len (pasted);
			}

		    /* Shift lnum[0] - 1 items in whole. Shift lbytes - 1 
		       byte from item number lnum[0] */
		    ret_val = reiserfs_lb_shift_left(tb, tb->lnum[0], 
						       tb->lbytes);
		    /* Append to body of item in L[0] */
		    bi.bi_bh = tb->L[0];
		    bi.bi_parent = tb->FL[0];
		    bi.bi_position = reiserfs_tb_lpos (tb, 0);
		    reiserfs_lb_insert_unit (tb->tb_fs, &bi, n + item_pos - 
					     ret_val, pos_in_item, 
					     tb->insert_size[0],
					     body, zeros_number);

		    /* if appended item is directory, paste entry */
		    pasted = reiserfs_ih_at (tb->L[0], n + item_pos - ret_val);
		    if (reiserfs_ih_dir (pasted))
			reiserfs_lb_insert_entry (bi.bi_bh, n + item_pos - 
						  ret_val, pos_in_item, 1, 
						  (reiserfs_deh_t *)body,
						  body + REISERFS_DEH_SIZE, 
						  tb->insert_size[0]);

		    /* if appended item is extent item, put 
		       unformatted node into un list */
		    if (reiserfs_ih_ext (pasted))
			reiserfs_ih_set_free (pasted, 0);

		    tb->insert_size[0] = 0;
		    zeros_number = 0;
		}
		break;
	    default:    /* cases d and t */
		reiserfs_panic ("PAP-12130: reiserfs_lb_balance: "
				"lnum > 0: unexpectable mode: %s(%d)", 
				(flag == M_DELETE) ? "DELETE" : 
				((flag == M_CUT) ? "CUT" : "UNKNOWN"), flag);
	    }
	} else { 
	    /* new item doesn't fall into L[0] */
	    reiserfs_lb_shift_left (tb, tb->lnum[0], tb->lbytes);
	}
    }	/* tb->lnum[0] > 0 */

    /* Calculate new item position */
    item_pos -= ( tb->lnum[0] - (( tb->lbytes != -1 ) ? 1 : 0));

    if ( tb->rnum[0] > 0 ) {
	/* shift rnum[0] items from S[0] to the right neighbor R[0] */
	n = reiserfs_node_items(tbS0);
	switch ( flag ) {

	case M_INSERT:   /* insert item */
	    if ( n - tb->rnum[0] < item_pos ) {
		/* new item or its part falls to R[0] */
		if ( item_pos == n - tb->rnum[0] + 1 && tb->rbytes != -1 ) {
		    /* part of new item falls into R[0] */
		    long long int old_key_comp, old_len, r_zeros_number;
		    const char * r_body;
		    long long int multiplyer;

		    reiserfs_lb_shift_right(tb, tb->rnum[0] - 1, -1);

		    /* Remember key component and item length */
		    old_key_comp = reiserfs_key_get_off (&ih->ih_key);
		    old_len = reiserfs_ih_get_len (ih);

		    multiplyer = reiserfs_ih_ext(ih) ? 
			    tb->tb_fs->fs_blocksize / REISERFS_EXT_SIZE : 1;
		    /* Calculate key component and item length 
		       to insert into R[0] */
		    //ih->ih_key.k_offset += (old_len - tb->rbytes);
		    reiserfs_key_set_off (reiserfs_key_format (&ih->ih_key), 
					     &ih->ih_key, old_key_comp + 
					     (old_len - tb->rbytes) * 
					     multiplyer );

		    reiserfs_ih_set_len (ih, tb->rbytes);
		    /* Insert part of the item into R[0] */
		    bi.bi_bh = tb->R[0];
		    bi.bi_parent = tb->FR[0];
		    bi.bi_position = reiserfs_tb_rpos (tb, 0);
		    if (old_len - tb->rbytes > zeros_number ) {
			r_zeros_number = 0;
			r_body = body + old_len - tb->rbytes - zeros_number;
		    }
		    else { /* zeros_number is always 0 */
			r_body = body;
			r_zeros_number = zeros_number - old_len - tb->rbytes;
			zeros_number -= r_zeros_number;
		    }

		    reiserfs_lb_insert (tb->tb_fs, &bi, 0, ih, 
					r_body, r_zeros_number);

		    /* Replace right delimiting key by first key in R[0] */
		    reiserfs_node_replace_key(tb->CFR[0], tb->rkey[0], 
					      tb->R[0], 0);

		    /* Calculate key component and item length to 
		       insert into S[0] */
		    //ih->ih_key.k_offset = old_key_comp;
		    reiserfs_key_set_off (reiserfs_key_format (&ih->ih_key), 
					     &ih->ih_key, old_key_comp);

		    reiserfs_ih_set_len (ih, old_len - tb->rbytes);

		    tb->insert_size[0] -= tb->rbytes;

		} else {
		    /* whole new item falls into R[0] */

		    /* Shift rnum[0]-1 items to R[0] */
		    ret_val = reiserfs_lb_shift_right(tb, tb->rnum[0] - 1,
							tb->rbytes);

		    /* Insert new item into R[0] */
		    bi.bi_bh = tb->R[0];
		    bi.bi_parent = tb->FR[0];
		    bi.bi_position = reiserfs_tb_rpos (tb, 0);
		    reiserfs_lb_insert (tb->tb_fs, &bi, item_pos - n + 
					tb->rnum[0] - 1, ih, body, 
					zeros_number);

		    /* If we insert new item in the begin of R[0] change 
		       the right delimiting key */
		    if ( item_pos - n + tb->rnum[0] - 1 == 0 ) {
			reiserfs_node_replace_key (tb->CFR[0], 
						   tb->rkey[0],
						   tb->R[0], 0);
		    }
		    
		    zeros_number = tb->insert_size[0] = 0;
		}
	    } else {
		/* new item or part of it doesn't fall into R[0] */
		reiserfs_lb_shift_right (tb, tb->rnum[0], tb->rbytes);
	    }
	    break;

	case M_PASTE:   /* append item */

	    if ( n - tb->rnum[0] <= item_pos ) { 
		/* pasted item or part of it falls to R[0] */
		if ( item_pos == n - tb->rnum[0] && tb->rbytes != -1 ) {
		    /* we must shift the part of the appended item */
		    if ( reiserfs_ih_dir (reiserfs_ih_at(tbS0, item_pos))) {
			/* we append to directory item */
			int entry_count;

			entry_count = 
				reiserfs_ih_get_entries(reiserfs_ih_at(tbS0, item_pos));
			if ( entry_count - tb->rbytes < pos_in_item ) {
			    /* new directory entry falls into R[0] */
			    int paste_entry_position;

			    /* Shift rnum[0]-1 items in whole. Shift 
			       rbytes-1 directory entries from directory 
			       item number rnum[0] */
			    reiserfs_lb_shift_right (tb, tb->rnum[0], 
						       tb->rbytes - 1);

			    /* Paste given directory entry to directory item */
			    paste_entry_position = pos_in_item - 
				    entry_count + tb->rbytes - 1;

			    bi.bi_bh = tb->R[0];
			    bi.bi_parent = tb->FR[0];
			    bi.bi_position = reiserfs_tb_rpos (tb, 0);
			    reiserfs_lb_insert_unit (tb->tb_fs, &bi, 0, 
						     paste_entry_position,
						     tb->insert_size[0],
						     body,zeros_number);
			    /* paste entry */
			    reiserfs_lb_insert_entry (
				bi.bi_bh, 0, paste_entry_position, 1, 
				(reiserfs_deh_t *)body, 
				body + REISERFS_DEH_SIZE, tb->insert_size[0]);								
						
			    if ( paste_entry_position == 0 ) {
				/* change delimiting keys */
				reiserfs_node_replace_key(tb->CFR[0], 
							  tb->rkey[0],
							  tb->R[0], 0);
			    }

			    tb->insert_size[0] = 0;
			    pos_in_item++;
			} else {
			    /* new directory entry doesn't fall into R[0] */
			    reiserfs_lb_shift_right (tb, tb->rnum[0],
						       tb->rbytes);
			}
		    }
		    else {
			/* regular object */

			int n_shift, n_rem, r_zeros_number;
			const char * r_body;
			reiserfs_key_t * key;

			/* Calculate number of bytes which must be shifted from 
			   appended item */
			if ( (n_shift = tb->rbytes - tb->insert_size[0]) < 0 )
			    n_shift = 0;

			reiserfs_lb_shift_right (tb, tb->rnum[0], n_shift);

			/* Calculate number of bytes which must remain in body 
			   after appending to R[0] */
			if ( (n_rem = tb->insert_size[0] - tb->rbytes) < 0 )
			    n_rem = 0;
			{
			    unsigned long temp_rem = n_rem;
			
			    if (reiserfs_key_ext(reiserfs_ih_key_at(tb->R[0],0)))
				temp_rem = (n_rem / REISERFS_EXT_SIZE) * 
					tb->tb_fs->fs_blocksize;

			    //reiserfs_ih_key_at(tb->R[0],0)->k_offset += n_rem;
			    key = reiserfs_ih_key_at(tb->R[0],0);
			    reiserfs_key_set_off (
				reiserfs_key_format (key), key, 
				reiserfs_key_get_off (key) + temp_rem);

			    /*reiserfs_int_key_at(tb->CFR[0],
				tb->rkey[0])->k_offset += n_rem; */
			    key = reiserfs_int_key_at(tb->CFR[0],tb->rkey[0]);
			    reiserfs_key_set_off (
				reiserfs_key_format (key), key, 
				reiserfs_key_get_off (key) + temp_rem);
                        }

			reiserfs_buffer_mkdirty (tb->CFR[0]);

			/* Append part of body into R[0] */
			bi.bi_bh = tb->R[0];
			bi.bi_parent = tb->FR[0];
			bi.bi_position = reiserfs_tb_rpos (tb, 0);
			if ( n_rem > zeros_number ) {
			    r_zeros_number = 0;
			    r_body = body + n_rem - zeros_number;
			}
			else {
			    r_body = body;
			    r_zeros_number = zeros_number - n_rem;
			    zeros_number -= r_zeros_number;
			}

			reiserfs_lb_insert_unit(tb->tb_fs, &bi, 0, n_shift, 
						tb->insert_size[0] - n_rem, 
						r_body, r_zeros_number);

			if (reiserfs_ih_ext(reiserfs_ih_at(tb->R[0],0))) {
			    reiserfs_ih_set_free (reiserfs_ih_at(tb->R[0],0), 0);
			}

			tb->insert_size[0] = n_rem;
			if ( ! n_rem )
			    pos_in_item ++;
		    }
		}
		else { 
		    /* pasted item falls into R[0] entirely */

		    reiserfs_ih_t * pasted;

		    ret_val = reiserfs_lb_shift_right (tb, tb->rnum[0], 
							 tb->rbytes);

		    /* append item in R[0] */
		    if ( pos_in_item >= 0 ) {
			bi.bi_bh = tb->R[0];
			bi.bi_parent = tb->FR[0];
			bi.bi_position = reiserfs_tb_rpos (tb, 0);
			reiserfs_lb_insert_unit(tb->tb_fs, &bi,item_pos - n + 
						tb->rnum[0], pos_in_item,
						tb->insert_size[0], body, 
						zeros_number);
		    }

		    /* paste new entry, if item is directory item */
		    pasted = reiserfs_ih_at(tb->R[0], item_pos - n + tb->rnum[0]);
		    if (reiserfs_ih_dir (pasted) && pos_in_item >= 0 ) {
			reiserfs_lb_insert_entry (bi.bi_bh, item_pos - n + 
						  tb->rnum[0], pos_in_item, 1, 
						  (reiserfs_deh_t *)body, 
						  body + REISERFS_DEH_SIZE, 
						  tb->insert_size[0]);
			if ( ! pos_in_item ) {
			    /* update delimiting keys */
			    reiserfs_node_replace_key (tb->CFR[0], 
						       tb->rkey[0],
						       tb->R[0], 0);
			}
		    }

		    if (reiserfs_ih_ext (pasted))
			reiserfs_ih_set_free (pasted, 0);
		    zeros_number = tb->insert_size[0] = 0;
		}
	    }
	    else {
		/* new item doesn't fall into R[0] */
		reiserfs_lb_shift_right (tb, tb->rnum[0], tb->rbytes);
	    }
	    break;

	default:    /* cases d and t */
	    reiserfs_panic ("PAP-12175: reiserfs_lb_balance: "
			    "rnum > 0: unexpectable mode: %s(%d)", 
			    (flag == M_DELETE) ? "DELETE" : 
			    ((flag == M_CUT) ? "CUT" : "UNKNOWN"), flag);
	}
    
    }	/* tb->rnum[0] > 0 */

    /* if while adding to a node we discover that it is possible to split
       it in two, and merge the left part into the left neighbor and the
       right part into the right neighbor, eliminating the node */
    if ( tb->blknum[0] == 0 ) { /* node S[0] is empty now */
        /* if insertion was done before 0-th position in R[0], right
           delimiting key of the tb->L[0]'s and left delimiting key are
           not set correctly */
        if (tb->CFL[0]) {
            if (!tb->CFR[0])
                reiserfs_panic (tb->tb_fs, "vs-12195: reiserfs_lb_balance: "
				"CFR not initialized");
	    
            reiserfs_key_copy (reiserfs_int_key_at (tb->CFL[0], tb->lkey[0]), 
			       reiserfs_int_key_at (tb->CFR[0], tb->rkey[0]));
            reiserfs_buffer_mkdirty (tb->CFL[0]);
        }
        
	reiserfs_node_forget (tb->tb_fs, tbS0->b_blocknr);
	return 0;
    }


    /* Fill new nodes that appear in place of S[0] */

    /* I am told that this copying is because we need an array to enable
       the looping code. -Hans */
    snum[0] = tb->s1num,
	snum[1] = tb->s2num;
    sbytes[0] = tb->s1bytes;
    sbytes[1] = tb->s2bytes;
    for( i = tb->blknum[0] - 2; i >= 0; i-- ) {
	/* here we shift from S to S_new nodes */
	S_new[i] = reiserfs_tb_FEB(tb);

	/* set block_head's level to leaf level */
	reiserfs_nh_set_level (NODE_HEAD (S_new[i]), LEAF_LEVEL);

	n = reiserfs_node_items(tbS0);
	
	switch (flag) {
	case M_INSERT:   /* insert item */

	    if ( n - snum[i] < item_pos ) {
		/* new item or it's part falls to first new node S_new[i]*/
		if ( item_pos == n - snum[i] + 1 && sbytes[i] != -1 ) {
		    /* part of new item falls into S_new[i] */
		    long long int old_key_comp, old_len, r_zeros_number;
		    const char * r_body;
		    long long int multiplyer;

		    /* Move snum[i]-1 items from S[0] to S_new[i] */
		    reiserfs_lb_move (LEAF_FROM_S_TO_SNEW, tb, 
					snum[i] - 1, -1, S_new[i]);

		    /* Remember key component and item length */
		    old_key_comp = reiserfs_key_get_off (&ih->ih_key);
		    old_len = reiserfs_ih_get_len (ih);
		    multiplyer = reiserfs_ih_ext(ih) ? 
			    tb->tb_fs->fs_blocksize / REISERFS_EXT_SIZE : 1;

		    /* Calculate key component and item length to insert 
		       into S_new[i] */
		    //ih->ih_key.k_offset += (old_len - sbytes[i]);
		    reiserfs_key_set_off (reiserfs_key_format (&ih->ih_key), 
					     &ih->ih_key, old_key_comp + 
					     (old_len - sbytes[i]) *multiplyer);

		    reiserfs_ih_set_len (ih, sbytes[i]);

		    /* Insert part of the item into S_new[i] before 0-th item */
		    bi.bi_bh = S_new[i];
		    bi.bi_parent = 0;
		    bi.bi_position = 0;

		    if ( old_len - sbytes[i] > zeros_number ) {
			r_zeros_number = 0;
			r_body = body + (old_len - sbytes[i]) - zeros_number;
		    }
		    else {
			r_body = body;
			r_zeros_number = zeros_number - (old_len - sbytes[i]);
			zeros_number -= r_zeros_number;
		    }

		    reiserfs_lb_insert (tb->tb_fs, &bi, 0, ih, 
					r_body, r_zeros_number);

		    /* Calculate key component and item length 
		       to insert into S[i] */
		    //ih->ih_key.k_offset = old_key_comp;
		    reiserfs_key_set_off (reiserfs_key_format (&ih->ih_key), 
					     &ih->ih_key, old_key_comp);
		    reiserfs_ih_set_len (ih, old_len - sbytes[i]);
		    tb->insert_size[0] -= sbytes[i];
		}
		else /* whole new item falls into S_new[i] */
		{
		    /* Shift snum[0] - 1 items to S_new[i] 
		       (sbytes[i] of split item) */
		    reiserfs_lb_move (LEAF_FROM_S_TO_SNEW, tb, snum[i] - 1,
					sbytes[i], S_new[i]);

		    /* Insert new item into S_new[i] */
		    bi.bi_bh = S_new[i];
		    bi.bi_parent = 0;
		    bi.bi_position = 0;
		    reiserfs_lb_insert (tb->tb_fs, &bi, item_pos - n + 
					snum[i] - 1, ih, body, zeros_number);
		    zeros_number = tb->insert_size[0] = 0;
		}
	    }

	    else /* new item or it part don't falls into S_new[i] */
	    {
		reiserfs_lb_move (LEAF_FROM_S_TO_SNEW, tb, snum[i], 
				    sbytes[i], S_new[i]);
	    }
	    break;

	case M_PASTE:   /* append item */

	    if ( n - snum[i] <= item_pos )  
		/* pasted item or part if it falls to S_new[i] */
	    {
		if ( item_pos == n - snum[i] && sbytes[i] != -1 )
		{ /* we must shift part of the appended item */
		    reiserfs_ih_t * aux_ih;

		    aux_ih = reiserfs_ih_at(tbS0,item_pos);
		    
		    if (reiserfs_ih_dir (aux_ih)) {
			/* we append to directory item */

			int entry_count;
		
			entry_count = reiserfs_ih_get_entries(aux_ih);

			if ( entry_count - sbytes[i] < pos_in_item  && 
			     pos_in_item <= entry_count ) 
			{
			    /* new directory entry falls into S_new[i] */
		  
			    /* Shift snum[i]-1 items in whole. Shift sbytes[i] 
			       directory entries from directory item number 
			       snum[i] */
			    reiserfs_lb_move (LEAF_FROM_S_TO_SNEW, tb,snum[i],
						sbytes[i]-1, S_new[i]);

			    /* Paste given directory entry to directory item */
			    bi.bi_bh = S_new[i];
			    bi.bi_parent = 0;
			    bi.bi_position = 0;
			    reiserfs_lb_insert_unit (tb->tb_fs, &bi, 0, 
						     pos_in_item - entry_count +
						     sbytes[i] - 1,
						     tb->insert_size[0], body,
						     zeros_number);
			    
			    /* paste new directory entry */
			    reiserfs_lb_insert_entry (bi.bi_bh, 0, 
				pos_in_item - entry_count + sbytes[i] - 1, 
				1, (reiserfs_deh_t *)body, 
				body + REISERFS_DEH_SIZE, tb->insert_size[0]);
			    
			    tb->insert_size[0] = 0;
			    pos_in_item++;
			} else { /* new directory entry doesn't fall 
				    into S_new[i] */
			    reiserfs_lb_move (LEAF_FROM_S_TO_SNEW, tb, 
						snum[i], sbytes[i], S_new[i]);
			}
		    }
		    else /* regular object */
		    {
			int n_shift, n_rem, r_zeros_number;
			const char * r_body;
			reiserfs_ih_t * tmp;

			/* Calculate number of bytes which must be shifted from 
			   appended item */
			n_shift = sbytes[i] - tb->insert_size[0];
			if ( n_shift < 0 )
			    n_shift = 0;
			reiserfs_lb_move (LEAF_FROM_S_TO_SNEW, tb, snum[i], 
					    n_shift, S_new[i]);

			/* Calculate number of bytes which must remain in body 
			   after append to S_new[i] */
			n_rem = tb->insert_size[0] - sbytes[i];
			if ( n_rem < 0 )
			    n_rem = 0;
			/* Append part of body into S_new[0] */
			bi.bi_bh = S_new[i];
			bi.bi_parent = 0;
			bi.bi_position = 0;

			if ( n_rem > zeros_number ) {
			    r_zeros_number = 0;
			    r_body = body + n_rem - zeros_number;
			}
			else {
			    r_body = body;
			    r_zeros_number = zeros_number - n_rem;
			    zeros_number -= r_zeros_number;
			}

			reiserfs_lb_insert_unit(tb->tb_fs, &bi, 0, n_shift, 
						tb->insert_size[0]-n_rem, 
						r_body,r_zeros_number);
			tmp = reiserfs_ih_at (S_new[i], 0);
			if (reiserfs_ih_ext(tmp)) {
/*			
			    if (n_rem)
				reiserfs_panic ("PAP-12230: reiserfs_lb_balance: "
						"invalid action with extent item");
			    reiserfs_ih_set_free (tmp, 0);
*/
			    reiserfs_ih_set_free (tmp, 0);
			    reiserfs_key_set_off(
				reiserfs_key_format (&tmp->ih_key), 
				&tmp->ih_key,
				reiserfs_key_get_off(&tmp->ih_key) +
				(n_rem / REISERFS_EXT_SIZE) * 
				tb->tb_fs->fs_blocksize);
			} else
			    reiserfs_key_set_off (
				reiserfs_key_format (&tmp->ih_key), 
				&tmp->ih_key,
				reiserfs_key_get_off(&tmp->ih_key) + n_rem);

			//reiserfs_ih_key_at(S_new[i],0)->k_offset += n_rem;
//			

			tb->insert_size[0] = n_rem;
			if ( ! n_rem )
			    pos_in_item++;
		    }
		}
		else
		    /* item falls wholly into S_new[i] */
		{
		    reiserfs_ih_t * pasted;
		
		    reiserfs_lb_move (LEAF_FROM_S_TO_SNEW, tb, snum[i], 
					sbytes[i], S_new[i]);
		    /* paste into item */
		    bi.bi_bh = S_new[i];
		    bi.bi_parent = 0;
		    bi.bi_position = 0;
		    reiserfs_lb_insert_unit(tb->tb_fs, &bi, item_pos - 
					    n + snum[i], pos_in_item, 
					    tb->insert_size[0], body, 
					    zeros_number);

		    pasted = reiserfs_ih_at(S_new[i], item_pos - n + snum[i]);
		    if (reiserfs_ih_dir (pasted)) {
			reiserfs_lb_insert_entry (bi.bi_bh, item_pos - n + 
						  snum[i], pos_in_item, 1, 
						  (reiserfs_deh_t *)body,
						  body + REISERFS_DEH_SIZE, 
						  tb->insert_size[0]);
		    }

		    /* if we paste to extent item update ih_free_space */
		    if (reiserfs_ih_ext (pasted))
			reiserfs_ih_set_free (pasted, 0);
		    zeros_number = tb->insert_size[0] = 0;
		}
	    } else {
		/* pasted item doesn't fall into S_new[i] */
		reiserfs_lb_move (LEAF_FROM_S_TO_SNEW, tb, snum[i], 
				    sbytes[i], S_new[i]);
	    }
	    break;

	default:    /* cases d and t */
	    reiserfs_panic ("PAP-12245: reiserfs_lb_balance: blknum > 2: "
			    "unexpectable mode: %s(%d)", (flag == M_DELETE) ? 
			    "DELETE" : ((flag == M_CUT) ? "CUT" : "UNKNOWN"), 
			    flag);
	}

	memcpy (insert_key + i,reiserfs_ih_key_at(S_new[i],0),REISERFS_KEY_SIZE);
	insert_ptr[i] = S_new[i];
    }

    /* if the affected item was not wholly shifted then we perform all
       necessary operations on that part or whole of the affected item which
       remains in S */
    if ( 0 <= item_pos && item_pos < tb->s0num ) {
	/* if we must insert or append into buffer S[0] */

	switch (flag) {
	case M_INSERT:   /* insert item into S[0] */
	    bi.bi_bh = tbS0;
	    bi.bi_parent = REISERFS_PATH_UPPARENT (tb->tb_path, 0);
	    bi.bi_position = REISERFS_PATH_UPPOS (tb->tb_path, 1);
	    reiserfs_lb_insert (tb->tb_fs, &bi, item_pos, 
				ih, body, zeros_number);

	    /* If we insert the first key change the delimiting key */
	    if( item_pos == 0 ) {
		if (tb->CFL[0]) /* can be 0 in reiserfsck */
		    reiserfs_node_replace_key (tb->CFL[0], 
					       tb->lkey[0],
					       tbS0, 0);
	    }
	    
	    break;

	case M_PASTE: {  /* append item in S[0] */
	    reiserfs_ih_t * pasted;

	    pasted = reiserfs_ih_at (tbS0, item_pos);
	    /* when directory, may be new entry already pasted */
	    if (reiserfs_ih_dir (pasted)) {
		if ( pos_in_item >= 0 && 
		     pos_in_item <= reiserfs_ih_get_entries (pasted) ) 
		{
		    /* prepare space */
		    bi.bi_bh = tbS0;
		    bi.bi_parent = REISERFS_PATH_UPPARENT (tb->tb_path, 0);
		    bi.bi_position = REISERFS_PATH_UPPOS (tb->tb_path, 1);
		    reiserfs_lb_insert_unit(tb->tb_fs, &bi, item_pos, 
					    pos_in_item, tb->insert_size[0],
					    body, zeros_number);

		    /* paste entry */
		    reiserfs_lb_insert_entry (bi.bi_bh, item_pos, pos_in_item, 
					      1, (reiserfs_deh_t *)body,
					body + REISERFS_DEH_SIZE, tb->insert_size[0]);
		    
		    if ( ! item_pos && ! pos_in_item ) {
			if (tb->CFL[0])  // can be 0 in reiserfsck
			    reiserfs_node_replace_key(tb->CFL[0], tb->lkey[0],
						      tbS0, 0);
		    }
		    tb->insert_size[0] = 0;
		}
	    } else { /* regular object */
		if ( pos_in_item == reiserfs_ih_get_len (pasted) ) {
		    bi.bi_bh = tbS0;
		    bi.bi_parent = REISERFS_PATH_UPPARENT (tb->tb_path, 0);
		    bi.bi_position = REISERFS_PATH_UPPOS (tb->tb_path, 1);
		    reiserfs_lb_insert_unit (tb->tb_fs, &bi, item_pos, 
					     pos_in_item, tb->insert_size[0],
					     body, zeros_number);

		    if (reiserfs_ih_ext (pasted)) {
			reiserfs_ih_set_free (pasted, 0);
		    }
		    tb->insert_size[0] = 0;
		}
	    }
	} /* case M_PASTE: */
	}
    }

    return 0;
} /* Leaf level of the tree is balanced (end of reiserfs_lb_balance) */

static void print_tb (int mode, 
		      int item_pos, 
		      int pos_in_item, 
		      reiserfs_tb_t * tb, 
		      char * mes)
{
  unsigned int h = 0;
  reiserfs_bh_t * tbSh, * tbFh;


  if (!tb)
    return;

  printf ("\n********************** PRINT_TB for %s *******************\n", mes);
  printf ("MODE=%c, ITEM_POS=%d POS_IN_ITEM=%d\n", mode, item_pos, pos_in_item);
  printf ("*********************************************************************\n");

  printf ("* h *    S    *    L    *    R    *   F   *   FL  *   FR  *  CFL  *  CFR  *\n");
  
  for (h = 0; h < sizeof(tb->insert_size) / sizeof (tb->insert_size[0]); h ++) {
    if (REISERFS_PATH_LEVEL (tb->tb_path, h) <= tb->tb_path->path_length && 
	REISERFS_PATH_LEVEL (tb->tb_path, h) > REISERFS_PATH_OFFILL) {
      tbSh = REISERFS_PATH_UPBUFFER (tb->tb_path, h);
      tbFh = REISERFS_PATH_UPPARENT (tb->tb_path, h);
    } else {
      /*      printk ("print_tb: h=%d, REISERFS_PATH_LEVEL=%d, path_length=%d\n", 
	      h, REISERFS_PATH_LEVEL (tb->tb_path, h), tb->tb_path->path_length);*/
      tbSh = 0;
      tbFh = 0;
    }
    printf ("* %u * %3lu(%2lu) * %3lu(%2lu) * %3lu(%2lu) * %5ld * "
	    "%5ld * %5ld * %5ld * %5ld *\n",
	    h, 
	    tbSh ? tbSh->b_blocknr : ~0ul,
	    tbSh ? tbSh->b_count : ~0ul,
	    tb->L[h] ? tb->L[h]->b_blocknr : ~0ul,
	    tb->L[h] ? tb->L[h]->b_count : ~0ul,
	    tb->R[h] ? tb->R[h]->b_blocknr : ~0ul,
	    tb->R[h] ? tb->R[h]->b_count : ~0ul,
	    tbFh ? tbFh->b_blocknr : ~0ul,
	    tb->FL[h] ? tb->FL[h]->b_blocknr : ~0ul,
	    tb->FR[h] ? tb->FR[h]->b_blocknr : ~0ul,
	    tb->CFL[h] ? tb->CFL[h]->b_blocknr : ~0ul,
	    tb->CFR[h] ? tb->CFR[h]->b_blocknr : ~0ul);
  }

  printf ("*********************************************************************\n");


  /* print balance parameters for leaf level */
  h = 0;
  printf ("* h * size * ln * lb * rn * rb * blkn * s0 * "
	  "s1 * s1b * s2 * s2b * curb * lk * rk *\n");
  printf ("* %d * %4d * %2d * %2d * %2d * %2d * %4d * %2d "
	  "* %2d * %3d * %2d * %3d * %4d * %2d * %2d *\n",
	  h, tb->insert_size[h], tb->lnum[h], tb->lbytes, 
	  tb->rnum[h],tb->rbytes, tb->blknum[h], 
	  tb->s0num, tb->s1num,tb->s1bytes,  tb->s2num, 
	  tb->s2bytes, tb->cur_blknum, tb->lkey[h], tb->rkey[h]);

  /* this prints balance parameters for non-leaf levels */
  do {
    h++;
    printf ("* %d * %4d * %2d *    * %2d *    * %2d *\n",
    h, tb->insert_size[h], tb->lnum[h], tb->rnum[h], tb->blknum[h]);
  } while (tb->insert_size[h]);

  printf ("*********************************************************************\n");


  /* print FEB list (list of buffers in form (bh (b_blocknr, b_count), 
     that will be used for new nodes) */
  for (h = 0; h < sizeof (tb->FEB) / sizeof (tb->FEB[0]); h++)
    printf("%s%p (%lu %d)", h == 0 ? "FEB list: " : ", ", 
	   tb->FEB[h], tb->FEB[h] ? tb->FEB[h]->b_blocknr : 0,
	    tb->FEB[h] ? tb->FEB[h]->b_count : 0);
  
  printf ("\n");
  printf ("********************** END OF PRINT_TB *******************\n\n");
}

