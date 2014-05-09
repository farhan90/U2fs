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
#include <linux/module.h>


static struct wrapfs_dentry_info *parse_options(struct super_block *sb,char *options){

	struct wrapfs_dentry_info *lower_root_info;
	char *optname;
	char *lpath_name,*rpath_name;
	struct path lpath,rpath;
	int err=0;
	int i=0;
	
	lower_root_info=NULL;
	lpath_name=NULL;
	rpath_name=NULL;

	printk("Parsing the options\n");
	
	lower_root_info=kzalloc(sizeof(struct wrapfs_dentry_info),GFP_KERNEL);
	if(!lower_root_info){
		err=-ENOMEM;
		goto out_error;
	}

	 while((optname=strsep(&options,","))!=NULL){
                printk("The optname is %s\n",optname);
		
		if(!optname)
			continue;
		
		if(!*optname){
			err=-EINVAL;
			goto out_error;
		}
	
		
	
		if(i<2 && strncmp(optname,"ldir",4)==0){
				
			lpath_name=strchr(optname,'=');
			if(lpath_name)
				*lpath_name++='\0';
		}
		if(i<2 && strncmp(optname,"rdir",4)==0){
			rpath_name=strchr(optname,'=');
			if(rpath_name)
				*rpath_name++='\0';
		}
		i++;
		
        }
	if(lpath_name!=NULL && rpath_name!=NULL){
		printk("paths are %s and %s \n",lpath_name,rpath_name);
		err=kern_path(lpath_name,LOOKUP_FOLLOW,&lpath);
		if(err){
			printk(KERN_ERR "Wrapfs : error accessing the path %s (errno %d)\n",lpath_name,err);
			goto out_error;
		}	

		err=kern_path(rpath_name,LOOKUP_FOLLOW,&rpath);
		if(err){
			printk(KERN_ERR "Wrapfs : error accessing the path %s (errno %d)\n",rpath_name,err);
                	goto out_error;
		}
	
		lower_root_info->lower_path.dentry=lpath.dentry;
		lower_root_info->lower_path.mnt=lpath.mnt;
		lower_root_info->lower_path_right.dentry=rpath.dentry;
		lower_root_info->lower_path_right.mnt=rpath.mnt;
		goto out;
	}
	else{
		err=-EINVAL;
		
	}

out_error:
	kfree(lower_root_info);
	lower_root_info=ERR_PTR(err);
out:
	return lower_root_info;
}


/*
 * There is no need to lock the wrapfs_super_info's rwsem as there is no
 * way anyone can have a reference to the superblock at this point in time.
 */
static int wrapfs_read_super(struct super_block *sb,void *raw_data, int silent)
{
	int err = 0;
	struct super_block *lower_sb,*lower_sb_right;
	struct path lower_path,lower_path_right;
	struct wrapfs_dentry_info *lower_root_info=NULL;
	char *dev_name = (char *) raw_data;
	struct inode *inode;

	if (!dev_name) {
		printk(KERN_ERR
		       "wrapfs: read_super: missing dev_name argument\n");
		err = -EINVAL;
		goto out;
	}
	
	printk("The mount method\n");
	lower_root_info=parse_options(sb,raw_data);
	if(IS_ERR(lower_root_info)){
		printk(KERN_ERR 
			"wrapfs: read_super error while parsing options\n");
		err=PTR_ERR(lower_root_info);
		lower_root_info=NULL;
		goto out_lower_info;
		
	}

	
	/* parse lower path 
	err = kern_path(dev_name, LOOKUP_FOLLOW | LOOKUP_DIRECTORY,
			&lower_path);
	if (err) {
		printk(KERN_ERR	"wrapfs: error accessing "
		       "lower directory '%s'\n", dev_name);
		goto out;
	}
	*/

	lower_path=lower_root_info->lower_path;
	lower_path_right=lower_root_info->lower_path_right;
	
	/* allocate superblock private data */
	sb->s_fs_info = kzalloc(sizeof(struct wrapfs_sb_info), GFP_KERNEL);
	if (!WRAPFS_SB(sb)) {
		printk(KERN_CRIT "wrapfs: read_super: out of memory\n");
		err = -ENOMEM;
		goto out_free;
	}
	


	/* set the lower superblock field of upper superblock */
	lower_sb = lower_path.dentry->d_sb;
	atomic_inc(&lower_sb->s_active);
	wrapfs_set_lower_super(sb, lower_sb);

	/*set lower right sb*/
	lower_sb_right=lower_path_right.dentry->d_sb;
	atomic_inc(&lower_sb_right->s_active);
	wrapfs_set_lower_super_right(sb, lower_sb_right);

	/* inherit maxbytes from lower file system */
	sb->s_maxbytes = lower_sb->s_maxbytes;

	/*
	 * Our c/m/atime granularity is 1 ns because we may stack on file
	 * systems whose granularity is as good.
	 */
	sb->s_time_gran = 1;

	sb->s_op = &wrapfs_sops;

	/* get a new inode and allocate our root dentry */
	inode = u2fs_iget(sb); //wrapfs_iget(sb, lower_path.dentry->d_inode);
	if (IS_ERR(inode)) {
		err = PTR_ERR(inode);
		goto out_sput;
	}
	inode->i_mode=S_IFDIR|0755;
	
	sb->s_root = d_alloc_root(inode);
	if (!sb->s_root) {
		err = -ENOMEM;
		goto out_iput;
	}

	
	d_set_d_op(sb->s_root, &wrapfs_dops);

	/* link the upper and lower dentries */
	sb->s_root->d_fsdata = NULL;
	err = new_dentry_private_data(sb->s_root);
	if (err)
		goto out_freeroot;

	/* if get here: cannot have error */

	/* set the lower dentries for s_root */
	wrapfs_set_lower_path(sb->s_root, &lower_path);
	wrapfs_set_lower_path_right(sb->s_root,&lower_path_right);

	if(atomic_read(&inode->i_count)<=1){
		printk("Setting the inode for root");
		u2fs_fill_inode(sb->s_root,inode);	
	}
	/*
	 * No need to call interpose because we already have a positive
	 * dentry, which was instantiated by d_alloc_root.  Just need to
	 * d_rehash it.
	 */
	d_rehash(sb->s_root);
	if (!silent)
		printk(KERN_INFO
		       "wrapfs: mounted on top of %s type %s\n",
		       dev_name, lower_sb->s_type->name);
	goto out; /* all is well */

	/* no longer needed: free_dentry_private_data(sb->s_root); */
out_freeroot:
	dput(sb->s_root);
out_iput:
	iput(inode);
out_sput:
	/* drop refs we took earlier */
	atomic_dec(&lower_sb->s_active);
	atomic_dec(&lower_sb_right->s_active);
	kfree(WRAPFS_SB(sb));
	sb->s_fs_info = NULL;
out_free:
	path_put(&lower_path);
	path_put(&lower_path_right);
out_lower_info:
	kfree(lower_root_info);
out:
	return err;
}

struct dentry *wrapfs_mount(struct file_system_type *fs_type, int flags,
			    const char *dev_name, void *raw_data)
{
	//void *lower_path_name = (void *) dev_name;

	return mount_nodev(fs_type, flags,raw_data,
			   wrapfs_read_super);
}

static struct file_system_type wrapfs_fs_type = {
	.owner		= THIS_MODULE,
	.name		= WRAPFS_NAME,
	.mount		= wrapfs_mount,
	.kill_sb	= generic_shutdown_super,
	.fs_flags	=FS_REVAL_DOT,
};

static int __init init_wrapfs_fs(void)
{
	int err;

	pr_info("Registering wrapfs " WRAPFS_VERSION "\n");

	err = wrapfs_init_inode_cache();
	if (err)
		goto out;
	err = wrapfs_init_dentry_cache();
	if (err)
		goto out;
	err = register_filesystem(&wrapfs_fs_type);
out:
	if (err) {
		wrapfs_destroy_inode_cache();
		wrapfs_destroy_dentry_cache();
	}
	return err;
}

static void __exit exit_wrapfs_fs(void)
{
	wrapfs_destroy_inode_cache();
	wrapfs_destroy_dentry_cache();
	unregister_filesystem(&wrapfs_fs_type);
	pr_info("Completed wrapfs module unload\n");
}

MODULE_AUTHOR("Erez Zadok, Filesystems and Storage Lab, Stony Brook University"
	      " (http://www.fsl.cs.sunysb.edu/)");
MODULE_DESCRIPTION("Wrapfs " WRAPFS_VERSION
		   " (http://wrapfs.filesystems.org/)");
MODULE_LICENSE("GPL");

module_init(init_wrapfs_fs);
module_exit(exit_wrapfs_fs);
