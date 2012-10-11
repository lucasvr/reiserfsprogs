/*
 * Copyright 1996-2004 by Hans Reiser, licensing governed by 
 * reiserfsprogs/README
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "fsck.h"
#include "util/print.h"
#include "assert.h"

void fsck_info_checkmem (void) {
    fprintf(stderr, 
	"\nThe problem has occurred looks like a hardware problem (perhaps\n"
        "memory). Send us the bug report only if the second run dies at\n"
	"the same place with the same block number.\n");
}

int fsck_info_ask(reiserfs_filsys_t * fs, char * q, 
		  char * a, int default_answer)
{
    if (!fsck_interactive (fs))
	return default_answer;
    
    return util_user_confirmed (fsck_progress_file (fs), q, a);
}

void fsck_stage_report (int pass, reiserfs_filsys_t * fs) {
    if (pass == FS_CHECK) {
	fsck_progress ("There are on the filesystem:\n"
		       "\tLeaves %lu\n\tInternal nodes %lu\n"
		       "\tDirectories %lu\n\tOther files %lu\n"
		       "\tData block pointers %lu (%lu of them are zero)\n"
		       "\tSafe links %lu\n",
		       fsck_check_stat (fs)->leaves,
		       fsck_check_stat (fs)->internals,
		       fsck_check_stat (fs)->dirs,
		       fsck_check_stat (fs)->files,
		       fsck_check_stat (fs)->unfm_pointers,
		       fsck_check_stat (fs)->zero_unfm_pointers,
		       fsck_check_stat (fs)->safe);
	
	return;
    }	

    {
	if (pass == FS_PASS0)
		fsck_progress("\tSelected hash: %s\n", 
			      reiserfs_hash_name(reiserfs_hash_code(fs->hash)));
	
	/* what has been done on pass 0 */
	if (pass_0_stat(fs)->dealt_with)
		fsck_progress ("\tRead formatted blocks: %lu\n",
			       pass_0_stat(fs)->dealt_with);
	
	if (pass_0_stat(fs)->leaves)
		fsck_progress ("\tRead leaves (corrected/empty/wrong hash): "
			       "%lu (%lu/%lu/%lu)\n", 
			       pass_0_stat(fs)->leaves, 
			       pass_0_stat(fs)->leaves_corrected,
			       pass_0_stat(fs)->all_contents_removed,
			       pass_0_stat(fs)->too_old_leaves);
	
	if (pass_0_stat(fs)->wrong_pointers)
		fsck_progress ("\tWrong indirect pointers (zeroed): %lu\n",
			       pass_0_stat(fs)->wrong_pointers);
    }
    
    {
	/* what has been done on pass 1 */
	if (pass_1_stat(fs)->leaves)
		fsck_progress ("\tRead leaves (not inserted): %lu (%lu)\n", 
			       pass_1_stat(fs)->leaves,
			       pass_1_stat(fs)->uninsertable_leaves);
	
	assert(pass_1_stat(fs)->leaves == pass_1_stat(fs)->inserted_leaves + 
	       pass_1_stat(fs)->uninsertable_leaves);
	
	if (pass_1_stat(fs)->pointed_leaves || 
	    pass_1_stat(fs)->non_unique_pointers)
	{
		fsck_progress ("\tDouble indirect pointers to leaves/to "
			       "unformatted (zeroed): %lu/%lu\n",
			       pass_1_stat(fs)->pointed_leaves,
			       pass_1_stat(fs)->non_unique_pointers);
	}
    }
    
    {
	/* what has been done on pass 2 */
	
	if (pass_2_stat(fs)->leaves)
		fsck_progress ("\tLeaves inserted item by item: %lu\n", 
			       pass_2_stat(fs)->leaves);
	
	/* FIXME: oid_sharing is the same */
	if (pass_2_stat(fs)->relocated && pass == FS_PASS2)
		fsck_progress ("\tFiles relocated because of key "
			       "conflicts with a directory: %lu\n", 
			       pass_2_stat(fs)->relocated);
	
	if (pass_2_stat(fs)->rewritten)
		fsck_progress ("\tFiles rewritten: %lu\n", 
			       pass_2_stat(fs)->rewritten);
    }
    
    {
	/* what has been done on the semantic pass */
	
	if (sem_pass_stat(fs)->regular_files || sem_pass_stat(fs)->directories ||
	    sem_pass_stat(fs)->symlinks || sem_pass_stat(fs)->others)
	{
		fsck_progress ("\tFound files/dirs/symlinks/others: "
			       "%lu/%lu/%lu/%lu\n", 
			       sem_pass_stat(fs)->regular_files,
			       sem_pass_stat(fs)->directories,
			       sem_pass_stat(fs)->symlinks,
			       sem_pass_stat(fs)->others);
	}

	if (sem_pass_stat(fs)->lost_found)
		fsck_progress ("\tLinked to /lost+found (files/dirs): "
			       "%lu (%lu/%lu)\n",
			       sem_pass_stat(fs)->lost_found,
			       sem_pass_stat(fs)->lost_found_files,
			       sem_pass_stat(fs)->lost_found_dirs);

	if (sem_pass_stat(fs)->broken_files)
		fsck_progress ("\tBroken file bodies: %lu\n",
			       sem_pass_stat(fs)->broken_files);
	
	if (sem_pass_stat(fs)->fixed_sizes)
		fsck_progress ("\tFiles with fixed size: %lu\n", 
			       sem_pass_stat(fs)->fixed_sizes);
	
	if (sem_pass_stat(fs)->added_sd)
		fsck_progress ("\tInserted missed StatDatas: %lu\n",
			       sem_pass_stat(fs)->added_sd);

	if (sem_pass_stat(fs)->empty_lost_dirs)
		fsck_progress ("\tEmpty lost dirs (removed): %lu\n",
			       sem_pass_stat(fs)->empty_lost_dirs);

	if (sem_pass_stat(fs)->deleted_entries)
		fsck_progress ("\tNames pointing to nowhere (removed): %lu\n", 
			       sem_pass_stat(fs)->deleted_entries);
	
	if (sem_pass_stat(fs)->oid_sharing) {
		fsck_progress ("\tObjects having used objectids (files, dirs): "
			       "%lu (%lu/%lu)\n", 
			       sem_pass_stat(fs)->oid_sharing,
			       sem_pass_stat(fs)->oid_sharing_files_relocated,
			       sem_pass_stat(fs)->oid_sharing_dirs_relocated);

		assert(pass_2_stat(fs)->relocated == 
		       sem_pass_stat(fs)->oid_sharing);
	}
    }
    
    if (pass != FS_CLEANUP && pass != FS_PASS1) {
	    if (proper_id_map(fs)->count)
		    fsck_progress ("\tObjectids found: %lu\n", 
				   proper_id_map(fs)->count);
    } else {
	/* what has been done on the semantic pass */
	if (pass_4_stat(fs)->deleted_items)
	    fsck_progress ("\tDeleted unreachable items: %lu\n",
			   pass_4_stat(fs)->deleted_items);
    }

    memset (&fsck_data (fs)->rebuild.pass_u, 0, 
	    sizeof (fsck_data (fs)->rebuild.pass_u));
}

