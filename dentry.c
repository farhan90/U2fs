/*
 * Copyright (c) 1998-2011 Erez Zadok
 * Copyright (c) 2009	   Shrikar Archak
 * Copyright (c) 2003-2011 Stony Brook University
 * Copyright (c) 2003-2011 The Research Foundation of SUNY
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "wrapfs.h"

/*
 * returns: -ERRNO if error (returned to user)
 *          0: tell VFS to invalidate dentry
 *          1: dentry is valid
 */
static int wrapfs_d_revalidate(struct dentry *dentry, struct nameidata *nd)
{
	struct path lower_path; 
	struct path saved_path;
	struct dentry *lower_dentry;
	int err;
	int i;

	printk("The revalidate method is called\n");

	err=1;
	if (nd && nd->flags & LOOKUP_RCU)
		return -ECHILD;

	for(i=0;i<MAX_BRANCHES;i++){
		if(i==0)
			wrapfs_get_lower_path(dentry, &lower_path);

		if(i==1)
			wrapfs_get_lower_path_right(dentry,&lower_path);

		
		lower_dentry = lower_path.dentry;
		//printk("This is where I suspect the error to be\n");

		if(lower_dentry){
			 printk("The revalidate method is getting lower dentry\n");
			if (!lower_dentry->d_op || !lower_dentry->d_op->d_revalidate)
				goto out;
			pathcpy(&saved_path, &nd->path);
			pathcpy(&nd->path, &lower_path);
			printk("The revalidate method calling lower\n");
			err = lower_dentry->d_op->d_revalidate(lower_dentry, nd);
			pathcpy(&nd->path, &saved_path);
			if(err==0)
				goto out;
		}
		
	}	
	
	

out:
	wrapfs_put_lower_path(dentry, &lower_path);
	//printk("put the lower the paths\n");
	return err;
}

static void wrapfs_d_release(struct dentry *dentry)
{
	printk("D releasing\n");
	/* release and reset the lower paths */
	wrapfs_put_reset_lower_path(dentry);
	free_dentry_private_data(dentry);
	return;
}

const struct dentry_operations wrapfs_dops = {
	.d_revalidate	= wrapfs_d_revalidate,
	.d_release	= wrapfs_d_release,
};
