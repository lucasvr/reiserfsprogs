/*
 *  Copyright 2000-2004 by Hans Reiser, licensing governed by 
 *  reiserfsprogs/README
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "reiserfs/libreiserfs.h"

/* Make empty node */
void reiserfs_tb_attach_new (reiserfs_bufinfo_t * bi)
{
    reiserfs_leaf_mkempty (bi->bi_bh);

    if (bi->bi_parent)
	reiserfs_dc_set_size (reiserfs_int_at (bi->bi_parent, bi->bi_position), 0);
}


/* Get first empty buffer */
reiserfs_bh_t * reiserfs_tb_FEB (reiserfs_tb_t * tb) {
    int i;
    reiserfs_bh_t * first_b;
    reiserfs_bufinfo_t bi;

    for (i = 0; i < TB_FEB_MAX; i ++)
	if (tb->FEB[i] != 0)
	    break;

    if (i == TB_FEB_MAX)
	reiserfs_panic("vs-12300: reiserfs_tb_FEB: FEB list is empty");

    bi.bi_bh = first_b = tb->FEB[i];
    bi.bi_parent = 0;
    bi.bi_position = 0;
    reiserfs_tb_attach_new (&bi);
    misc_set_bit(BH_Uptodate, &first_b->b_state);

    tb->FEB[i] = 0;
    tb->used[i] = first_b;

    return(first_b);
}

int reiserfs_tb_lpos (reiserfs_tb_t * tb, int h) 
{
  int Sh_position = REISERFS_PATH_UPPOS (tb->tb_path, h + 1);

  if (Sh_position == 0)
    return reiserfs_node_items (tb->FL[h]);
  else
    return Sh_position - 1;
}


int reiserfs_tb_rpos (reiserfs_tb_t * tb, int h)
{
  int Sh_position = REISERFS_PATH_UPPOS (tb->tb_path, h + 1);

  if (Sh_position == reiserfs_node_items (REISERFS_PATH_UPPARENT (tb->tb_path, h)))
    return 0;
  else
    return Sh_position + 1;
}

/* Now we have all of the buffers that must be used in balancing of the tree.
   We rely on the assumption that schedule() will not occur while reiserfs_tb_balance
   works. ( Only interrupt handlers are acceptable.)  We balance the tree
   according to the analysis made before this, using buffers already obtained.
   For SMP support it will someday be necessary to add ordered locking of
   tb. */

/* Some interesting rules of balancing:

   we delete a maximum of two nodes per level per balancing: we never delete R, when we delete two
   of three nodes L, S, R then we move them into R.

   we only delete L if we are deleting two nodes, if we delete only one node we delete S

   if we shift leaves then we shift as much as we can: this is a deliberate policy of extremism in
   node packing which results in higher average utilization after repeated random balance
   operations at the cost of more memory copies and more balancing as a result of small insertions
   to full nodes.

   if we shift internal nodes we try to evenly balance the node utilization, with consequent less
   balancing at the cost of lower utilization.

   one could argue that the policy for directories in leaves should be that of internal nodes, but
   we will wait until another day to evaluate this....  It would be nice to someday measure and
   prove these assumptions as to what is optimal....

*/

void reiserfs_tb_balance (
	reiserfs_tb_t * tb,	/* tree_balance structure */
	reiserfs_ih_t * ih,	/* item header of inserted item */
	const char * body, /* body  of inserted item or bytes to paste */
	int flag,  /* i - insert, d - delete
		      c - cut, p - paste			
		      Cut means delete part of an item (includes
		      removing an entry from a directory).

		      Delete means delete whole item.

		      Insert means add a new item into the tree.

		      Paste means to append to the end of an existing
		      file or to insert a directory entry.  */
	int zeros_num)
{
    //int pos_in_item = tb->tb_path->pos_in_item;
    int child_pos, /* position of a child node in its parent */
	h;	   /* level of the tree being processed */
    reiserfs_ih_t insert_key[2]; /* in our processing of one level we
				       sometimes determine what must be
				       inserted into the next higher level.
				       This insertion consists of a key or two
				       keys and their corresponding pointers */
    reiserfs_bh_t *insert_ptr[2]; /* inserted node-ptrs for the next
					  level */

    /* if we have no real work to do  */
    if ( ! tb->insert_size[0] ) {
	reiserfs_unfix_nodes(/*th,*/ tb);
	return;
    }

    if (flag == M_INTERNAL) {
	insert_ptr[0] = (reiserfs_bh_t *)body;
	/* we must prepare insert_key */

	if (REISERFS_PATH_UPPARENT_POS (tb->tb_path, 0)
	    /*LAST_POSITION (tb->tb_path)*//*item_pos*/ == -1) 
	{
		/* get delimiting key from buffer in tree */
		reiserfs_key_copy (&insert_key[0].ih_key, 
				   reiserfs_ih_key_at (REISERFS_PATH_LEAF (tb->tb_path), 0));
		/*insert_ptr[0]->b_item_order = 0;*/
	} else {
	    /* get delimiting key from new buffer */
	    reiserfs_key_copy (&insert_key[0].ih_key, 
			       reiserfs_ih_key_at((reiserfs_bh_t *)body,0));
	    
	    /*insert_ptr[0]->b_item_order = item_pos;*/
	}
      
	/* and insert_ptr instead of reiserfs_lb_balance */
	child_pos = REISERFS_PATH_UPPARENT_POS (tb->tb_path, 0)/*item_pos*/;
    } else
	/* balance leaf returns 0 except if combining L R and S into 
	   one node. see reiserfs_ib_balance() for explanation 
	   of this line of code.*/
	child_pos = REISERFS_PATH_UPPARENT_POS (tb->tb_path, 0) +
	    reiserfs_lb_balance (tb, ih, body, flag, zeros_num, 
				 insert_key, insert_ptr);

    /* Balance internal level of the tree. */
    for ( h = 1; h < REISERFS_TREE_HEIGHT_MAX && tb->insert_size[h]; h++ )
	child_pos = reiserfs_ib_balance (tb, h, child_pos, 
					 insert_key, insert_ptr);

    /* Release all (except for S[0]) non NULL buffers fixed by reiserfs_fix_nodes() */
    reiserfs_unfix_nodes(/*th,*/ tb);
}

void reiserfs_tb_init (reiserfs_tb_t * tb, 
		       reiserfs_filsys_t * fs,
		       reiserfs_path_t * path, 
		       int size)
{
    memset (tb, '\0', sizeof(reiserfs_tb_t));
    tb->tb_fs = fs;
    tb->tb_path = path;

    REISERFS_PATH_BUFFER(path, REISERFS_PATH_OFFILL) = NULL;
    REISERFS_PATH_POS(path, REISERFS_PATH_OFFILL) = 0;
    tb->insert_size[0] = size;
}

void reiserfs_tb_print_path (reiserfs_tb_t * tb, reiserfs_path_t * path)
{
    int offset = path->path_length;
    reiserfs_bh_t * bh;

    printf ("Offset    Bh     (b_blocknr, b_count) Position Nr_item\n");
    while ( offset > REISERFS_PATH_OFFILL ) {
	bh = REISERFS_PATH_BUFFER (path, offset);
	printf ("%6d %10p (%9lu, %7d) %8d %7d\n", offset, 
		bh, bh ? bh->b_blocknr : 0, bh ? bh->b_count : 0,
		REISERFS_PATH_POS (path, offset), bh ? reiserfs_node_items (bh) : -1);
	
	offset --;
    }
}

