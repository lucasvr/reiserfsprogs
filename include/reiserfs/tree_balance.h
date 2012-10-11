/*
 *  Copyright 2000-2004 by Hans Reiser, licensing governed by 
 *  reiserfsprogs/README
 */

#ifndef REISERFS_TB_H
#define REISERFS_TB_H

/* This temporary structure is used in tree balance algorithms, and
   constructed as we go to the extent that its various parts are needed.  It
   contains arrays of nodes that can potentially be involved in the balancing
   of node S, and parameters that define how each of the nodes must be
   balanced.  Note that in these algorithms for balancing the worst case is to
   need to balance the current node S and the left and right neighbors and all
   of their parents plus create a new node.  We implement S1 balancing for the
   leaf nodes and S0 balancing for the internal nodes (S1 and S0 are defined
   in our papers.)*/

/* size of the array of buffers to free at end of reiserfs_tb_balance */
#define TB_2FREE_MAX 7	

#define TB_FEB_MAX (REISERFS_TREE_HEIGHT_MAX + 1)

/* someday somebody will prefix every field in this struct with tb_ */
struct reiserfs_tb {
    reiserfs_filsys_t * tb_fs;
    
    reiserfs_path_t * tb_path;

    /* array of left neighbors of nodes in the path */
    reiserfs_bh_t * L[REISERFS_TREE_HEIGHT_MAX];
    
    /* array of right neighbors of nodes in the path*/
    reiserfs_bh_t * R[REISERFS_TREE_HEIGHT_MAX];
    
    /* array of fathers of the left  neighbors      */
    reiserfs_bh_t * FL[REISERFS_TREE_HEIGHT_MAX];
    
    /* array of fathers of the right neighbors      */
    reiserfs_bh_t * FR[REISERFS_TREE_HEIGHT_MAX];
    
    /* array of common parents of center node and its left neighbor  */
    reiserfs_bh_t * CFL[REISERFS_TREE_HEIGHT_MAX];
    
    /* array of common parents of center node and its right neighbor */
    reiserfs_bh_t * CFR[REISERFS_TREE_HEIGHT_MAX];
    
    /* array of blocknr's that are free and are the nearest to the left 
       node that are usable for writing dirty formatted leaves, using 
       the write_next_to algorithm. */
    
    /*unsigned long free_and_near[MAX_DIRTIABLE];*/
    
    /* array of empty buffers. Number of buffers in array equals cur_blknum. */
    reiserfs_bh_t * FEB[TB_FEB_MAX]; 
    
    reiserfs_bh_t * used[TB_FEB_MAX];
    
    /* array of number of items which must be shifted to the left in order to 
       balance the current node; for leaves includes item that will be partially
       shifted; for internal nodes, it is the number of child pointers rather 
       than items. It includes the new item being created. For preserve_shifted
       purposes the code sometimes subtracts one from this number to get the
       number of currently existing items being shifted, and even more often 
       for leaves it subtracts one to get the number of wholly shifted items 
       for other purposes. */
    short int lnum[REISERFS_TREE_HEIGHT_MAX];	

    /* substitute right for left in comment above */
    short int rnum[REISERFS_TREE_HEIGHT_MAX];	
    
    /* array indexed by height h mapping the key delimiting L[h] and
       S[h] to its item number within the node CFL[h] */
    short int lkey[REISERFS_TREE_HEIGHT_MAX];
    
    /* substitute r for l in comment above */
    short int rkey[REISERFS_TREE_HEIGHT_MAX];
    
    /* the number of bytes by we are trying to add or remove from S[h]. 
       A negative value means removing.  */
    short int insert_size[REISERFS_TREE_HEIGHT_MAX];
     
    /* number of nodes that will replace node S[h] after balancing on the level 
       h of the tree.  If 0 then S is being deleted, if 1 then S is remaining 
       and no new nodes are being created, if 2 or 3 then 1 or 2 new nodes is
       being created */
    short int blknum[REISERFS_TREE_HEIGHT_MAX];

    /* fields that are used only for balancing leaves of the tree */
    
    /* number of empty blocks having been already allocated */
    short int cur_blknum;
    
    /* number of items that fall into left most  node when S[0] splits	*/
    short int s0num;
    
    /* number of items that fall into first  new node when S[0] splits	*/
    short int s1num;
    
    /* number of items that fall into second new node when S[0] splits	*/
    short int s2num;
    
    /* number of bytes which can flow to the left neighbor from the left */
    short int lbytes;
    
    /* most liquid item that cannot be shifted from S[0] entirely
       if -1 then nothing will be partially shifted */
    
    /* number of bytes which will flow to the right neighbor from the right */
    short int rbytes;            
    
    /* number of bytes which flow to the first  new node when S[0] splits
       note: if S[0] splits into 3 nodes, then items do not need to be cut */
    short int s1bytes;
    short int s2bytes;
    
    /* buffers which are to be freed after reiserfs_tb_balance finishes 
       by reiserfs_unfix_nodes */
    reiserfs_bh_t * buf_to_free[TB_2FREE_MAX]; 
    
    /* kmalloced memory. Used to create virtual node and keep map of dirtied 
       bitmap blocks */
    char * vn_buf;

    /* size of the vn_buf */
    int vn_buf_size;		
    
    /* VN starts after bitmap of bitmap blocks */
    struct reiserfs_virtual_node * tb_vn;	
};

typedef struct reiserfs_tb reiserfs_tb_t;

enum tb_return {
	CARRY_ON		= 0x0,
	NO_DISK_SPACE		= 0x3,
	IO_ERROR		= 0x4,
	NO_BALANCING_NEEDED	= 0x5,
	ITEM_FOUND		= 0x6,
	ITEM_NOT_FOUND		= 0x7,
	POSITION_FOUND          = 0x8,
	POSITION_NOT_FOUND      = 0x9,
	GOTO_PREVIOUS_ITEM	= 0xa,
	POSITION_FOUND_INVISIBLE= 0xb,
	FILE_NOT_FOUND		= 0xc,
	DIRECTORY_NOT_FOUND	= 0xd, 
	DIRECTORY_FOUND		= 0xf,
	LAST_SEARCH
};

/* These are modes of balancing */

/* When inserting an item. */
#define M_INSERT	'i'
/* When inserting into (directories only) or appending onto an already
   existant item. */
#define M_PASTE		'p'
/* When deleting an item. */
#define M_DELETE	'd'
/* When truncating an item or removing an entry from a (directory) item. */
#define M_CUT 		'c'

/* used when balancing on leaf level skipped (in reiserfsck) */
#define M_INTERNAL	'n'

#define FIRST_TO_LAST 0
#define LAST_TO_FIRST 1

/* used in reiserfs_tb_balance for passing parent of node information that 
   has been gotten from tb struct */
struct reiserfs_bufinfo {
    reiserfs_bh_t * bi_bh;
    reiserfs_bh_t * bi_parent;
    int bi_position;
};

typedef struct reiserfs_bufinfo reiserfs_bufinfo_t;


extern void reiserfs_tb_attach_new (reiserfs_bufinfo_t * bi);

extern reiserfs_bh_t * reiserfs_tb_FEB (reiserfs_tb_t *tb);

extern int reiserfs_tb_lpos (reiserfs_tb_t * tb, int h);

extern int reiserfs_tb_rpos (reiserfs_tb_t * tb, int h);

extern void reiserfs_tb_balance (reiserfs_tb_t * tb,
				 reiserfs_ih_t * ih, 
				 const char * body, 
				 int flag, 
				 int zeros_num);

extern void reiserfs_tb_init (reiserfs_tb_t * tb, 
			      reiserfs_filsys_t *, 
			      reiserfs_path_t * path, 
			      int size);

extern void reiserfs_tb_print_path (reiserfs_tb_t * tb, 
				    reiserfs_path_t * path);

#endif
