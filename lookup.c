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

/* The dentry cache is just so we have properly sized dentries */
static struct kmem_cache *wrapfs_dentry_cachep;

int wrapfs_init_dentry_cache(void)
{
	wrapfs_dentry_cachep =
		kmem_cache_create("wrapfs_dentry",
				  sizeof(struct wrapfs_dentry_info),
				  0, SLAB_RECLAIM_ACCOUNT, NULL);

	return wrapfs_dentry_cachep ? 0 : -ENOMEM;
}

void wrapfs_destroy_dentry_cache(void)
{
	if (wrapfs_dentry_cachep)
		kmem_cache_destroy(wrapfs_dentry_cachep);
}

void free_dentry_private_data(struct dentry *dentry)
{
	if (!dentry || !dentry->d_fsdata)
		return;
	kmem_cache_free(wrapfs_dentry_cachep, dentry->d_fsdata);
	dentry->d_fsdata = NULL;
}

/* allocate new dentry private data */
int new_dentry_private_data(struct dentry *dentry)
{
	struct wrapfs_dentry_info *info = WRAPFS_D(dentry);

	/* use zalloc to init dentry_info.lower_path */
	info = kmem_cache_zalloc(wrapfs_dentry_cachep, GFP_ATOMIC);
	if (!info)
		return -ENOMEM;

	info->lower_path_right.dentry=NULL;
	info->lower_path.dentry=NULL;
	info->lower_path_right.mnt=NULL;
	info->lower_path.mnt=NULL;
	spin_lock_init(&info->lock);
	dentry->d_fsdata = info;

	return 0;
}

static int wrapfs_inode_test(struct inode *inode, void *candidate_lower_inode)
{
	struct inode *current_lower_inode = wrapfs_lower_inode(inode);
	if (current_lower_inode == (struct inode *)candidate_lower_inode)
		return 1; /* found a match */
	else
		return 0; /* no match */
}

static int wrapfs_inode_set(struct inode *inode, void *lower_inode)
{
	/* we do actual inode initialization in wrapfs_iget */
	return 0;
}

struct inode *wrapfs_iget(struct super_block *sb, struct inode *lower_inode)
{
	struct wrapfs_inode_info *info;
	struct inode *inode; /* the new inode to return */
	int err;

	inode = iget5_locked(sb, /* our superblock */
			     /*
			      * hashval: we use inode number, but we can
			      * also use "(unsigned long)lower_inode"
			      * instead.
			      */
			     lower_inode->i_ino, /* hashval */
			     wrapfs_inode_test,	/* inode comparison function */
			     wrapfs_inode_set, /* inode init function */
			     lower_inode); /* data passed to test+set fxns */
	if (!inode) {
		err = -EACCES;
		iput(lower_inode);
		return ERR_PTR(err);
	}
	/* if found a cached inode, then just return it */
	if (!(inode->i_state & I_NEW))
		return inode;

	/* initialize new inode */
	info = WRAPFS_I(inode);

	inode->i_ino = lower_inode->i_ino;
	if (!igrab(lower_inode)) {
		err = -ESTALE;
		return ERR_PTR(err);
	}
	wrapfs_set_lower_inode(inode, lower_inode,0);

	inode->i_version++;

	/* use different set of inode ops for symlinks & directories */
	if (S_ISDIR(lower_inode->i_mode))
		inode->i_op = &wrapfs_dir_iops;
	else if (S_ISLNK(lower_inode->i_mode))
		inode->i_op = &wrapfs_symlink_iops;
	else
		inode->i_op = &wrapfs_main_iops;

	/* use different set of file ops for directories */
	if (S_ISDIR(lower_inode->i_mode))
		inode->i_fop = &wrapfs_dir_fops;
	else
		inode->i_fop = &wrapfs_main_fops;

	inode->i_mapping->a_ops = &wrapfs_aops;

	inode->i_atime.tv_sec = 0;
	inode->i_atime.tv_nsec = 0;
	inode->i_mtime.tv_sec = 0;
	inode->i_mtime.tv_nsec = 0;
	inode->i_ctime.tv_sec = 0;
	inode->i_ctime.tv_nsec = 0;

	/* properly initialize special inodes */
	if (S_ISBLK(lower_inode->i_mode) || S_ISCHR(lower_inode->i_mode) ||
	    S_ISFIFO(lower_inode->i_mode) || S_ISSOCK(lower_inode->i_mode))
		init_special_inode(inode, lower_inode->i_mode,
				   lower_inode->i_rdev);

	/* all well, copy inode attributes */
	fsstack_copy_attr_all(inode, lower_inode);
	fsstack_copy_inode_size(inode, lower_inode);

	unlock_new_inode(inode);
	return inode;
}

struct inode *u2fs_iget(struct super_block *sb){

	struct wrapfs_inode_info *info;
	struct inode *inode;
	
	unsigned long ino=iunique(sb,WRAPFS_ROOT_INO);
	

	inode=iget_locked(sb,ino);
	if(!inode)
		return ERR_PTR(-ENOMEM);
	if(!(inode->i_state & I_NEW))
		return inode;
	
	info=WRAPFS_I(inode);

	inode->i_version++;
	inode->i_op=&wrapfs_main_iops;

	inode->i_fop=&wrapfs_main_fops;


	inode->i_mapping->a_ops=&wrapfs_aops;

	inode->i_atime.tv_sec=inode->i_atime.tv_nsec=0;
	inode->i_mtime.tv_sec=inode->i_mtime.tv_nsec=0;
	inode->i_ctime.tv_sec=inode->i_ctime.tv_nsec=0;

	
	unlock_new_inode(inode);
	return inode;

}


int u2fs_fill_inode(struct dentry *dentry, struct inode *inode){

	struct inode *lnode;
	struct dentry *lower_dentry;
	int i=0;
	printk("The root name is %s\n",dentry->d_name.name);

	for(i=0;i<2;i++){
		lower_dentry=wrapfs_get_lower_dentry_idx(dentry,i);	
		
		if(!lower_dentry){
			printk("No lower dentry u2fs at branch %d\n",i);
			wrapfs_set_lower_inode(inode,NULL,i);
			continue;
		}
		if(!lower_dentry->d_inode){
			printk("No lower inode u2fs\n");
			continue;
		}
		printk("inode num  %lu\n",lower_dentry->d_inode->i_ino);	
		wrapfs_set_lower_inode(inode,igrab(lower_dentry->d_inode),i);
			
		
			
	}

	//printk("The inode number for left inode is %lu and right inode is %lu\n",wrapfs_lower_inode(inode)->i_ino,
	//wrapfs_lower_inode_right(inode)->i_ino);

	lnode=wrapfs_lower_inode(inode);

	
	if(!lnode){
		printk("Could not find in the left branch\n");
		lnode=wrapfs_lower_inode_right(inode);
		if(!lnode){
			printk("There is some serious error\n");
			return -ENOENT;
		}
	}
	
	if(S_ISLNK(lnode->i_mode))
                inode->i_op=&wrapfs_symlink_iops;
        else if (S_ISDIR(lnode->i_mode))
                inode->i_op=&wrapfs_dir_iops;
        else
                inode->i_op=&wrapfs_main_iops;

        if(S_ISDIR(lnode->i_mode))
                inode->i_fop=&wrapfs_dir_fops;
        else
                inode->i_fop=&wrapfs_main_fops;

	if (S_ISBLK(lnode->i_mode) || S_ISCHR(lnode->i_mode) ||
            S_ISFIFO(lnode->i_mode) || S_ISSOCK(lnode->i_mode))
                init_special_inode(inode, lnode->i_mode,
                                   lnode->i_rdev);

        /* all well, copy inode attributes */
        fsstack_copy_attr_all(inode, lnode);
        fsstack_copy_inode_size(inode, lnode);

	return 0;	
}



/*
 * Connect a wrapfs inode dentry/inode with several lower ones.  This is
 * the classic stackable file system "vnode interposition" action.
 *
 * @dentry: wrapfs's dentry which interposes on lower one
 * @sb: wrapfs's super_block
 * @lower_path: the lower path (caller does path_get/put)
 */
int wrapfs_interpose(struct dentry *dentry, struct super_block *sb,
		     struct path *lower_path)
{
	int err = 0;
	struct inode *inode;
	struct inode *lower_inode;
	struct super_block *lower_sb;

	lower_inode = lower_path->dentry->d_inode;
	lower_sb = wrapfs_lower_super(sb);

	/* check that the lower file system didn't cross a mount point */
	if (lower_inode->i_sb != lower_sb) {
		err = -EXDEV;
		goto out;
	}

	/*
	 * We allocate our new inode below by calling wrapfs_iget,
	 * which will initialize some of the new inode's fields
	 */

	/* inherit lower inode number for wrapfs's inode */
	inode = wrapfs_iget(sb, lower_inode);
	if (IS_ERR(inode)) {
		err = PTR_ERR(inode);
		goto out;
	}

	d_add(dentry, inode);

out:
	return err;
}

int u2fs_interpose(struct dentry *dent, struct super_block *sb){
	int err=0;
	struct inode *inode;
	
	inode=u2fs_iget(sb);

	if(IS_ERR(inode)){
		err=PTR_ERR(inode);
		goto out;
	}	

	err=u2fs_fill_inode(dent,inode);
	d_add(dent,inode);
	

out:
	return err;
}



struct dentry *lookup_whiteout(const char *name, struct dentry *lower_parent){

	char *whname;
	int err=0,namelen,tlen;
	struct dentry *wh_dentry;
	struct dentry *d;	

	namelen=strlen(name);

	printk("In the lookup whiteout the parent %s\n",lower_parent->d_name.name);
	
	whname=alloc_whname(name,lower_parent->d_name.name,namelen,lower_parent->d_name.len);
	tlen=namelen+lower_parent->d_name.len+U2FS_WHLEN+1;

	printk("In lookup whiteout func the whname is %s\n",whname);

	d=create_parents(lower_parent->d_inode,lower_parent,lower_parent->d_name.name);

	wh_dentry=lookup_one_len(whname,d,tlen);
	if(IS_ERR(wh_dentry)){
		err=PTR_ERR(wh_dentry);
		goto out;

	}

	if(!wh_dentry->d_inode)
		goto out;


out:
	kfree(whname);
	if(err)
		wh_dentry=ERR_PTR(err);
	return wh_dentry;

}


/*
 * Main driver function for wrapfs's lookup.
 *
 * Returns: NULL (ok), ERR_PTR if an error occurred.
 * Fills in lower_parent_path with <dentry,mnt> on success.
 */
static struct dentry *__wrapfs_lookup(struct dentry *dentry, int flags,
				      struct dentry *parent)
{
	int err = 0;
	struct vfsmount *lower_dir_mnt;
	struct dentry *lower_dir_dentry;
	struct dentry *lower_dentry;
	struct dentry *lower_wh_dentry;
	const char *name;
	struct path lower_parent_path,lower_path;
	struct qstr this;
	int i;
	int num_positives=0;


	lower_dir_dentry=NULL;

	/* must initialize dentry operations */
	d_set_d_op(dentry, &wrapfs_dops);

	if (IS_ROOT(dentry))
		goto out;
	
	name = dentry->d_name.name;
	
	printk("made it to the for loop and the name is %s\n",name);
	
	for(i=0;i<2;i++){
		if(i==0){
			wrapfs_get_lower_path(parent,&lower_parent_path);
			// printk("The left path name is %s\n",lower_parent_path.dentry->d_name.name);
		}
		if(i==1){
			wrapfs_get_lower_path_right(parent,&lower_parent_path);
			//printk("The right path name is %s\n",lower_parent_path.dentry->d_name.name);
		}
		/* now start the actual lookup procedure */
		if(!lower_parent_path.dentry){
			printk("No lower parent dentry found in this branch %d\n",i);
			wrapfs_put_lower_path(parent,&lower_parent_path);
			continue;
		}		

		printk("The path name is %s on branch %d\n",lower_parent_path.dentry->d_name.name,i);
		
			
		lower_wh_dentry=lookup_whiteout(name,parent);

		if(IS_ERR(lower_wh_dentry)){
			err=PTR_ERR(lower_wh_dentry);
			printk("Lookup white out error %d\n",err);	
		}
	
		if(!IS_ERR(lower_wh_dentry)){	
			if(lower_wh_dentry->d_inode){
				printk("In wrapfs lookup the lower white out exists\n");
				dput(lower_wh_dentry);
				break;

			}
			dput(lower_wh_dentry);
		}	
		
	


		lower_dir_dentry = lower_parent_path.dentry;
		lower_dir_mnt = lower_parent_path.mnt;
		printk("in wrapfs lookup, this is not the problem\n");

		
		if(!lower_dir_dentry||!lower_dir_dentry->d_inode){
			continue;
		}
		
		/* Use vfs_path_lookup to check if the dentry exists or not */
		err = vfs_path_lookup(lower_dir_dentry, lower_dir_mnt, name, 0,
			     	 &lower_path);

		//printk("in wrapfs lookup this could be the problem\n");

		if(err && err!=-ENOENT){
			printk("Error in u2fs lookup and errno is blah %d\n",err);
			goto out;
		}
		printk("This is for sanity check and err is %d\n",err);
		if(err==0){		
			if(i==0){
				printk("The lower path dentry name is %s",lower_path.dentry->d_name.name);
				wrapfs_set_lower_path(dentry,&lower_path);
				printk("In wrapfs lookup setting path for left path\n");
			}

			if(i==1){
				wrapfs_set_lower_path_right(dentry,&lower_path);
				printk("In wrapfs lookup setting path for right\n");
			}

			num_positives++;
		}
		//wrapfs_put_lower_path(parent,&lower_parent_path);		
	}

	printk("After for loop\n");

	/* no error: handle positive dentries */
	if (num_positives>0) {
		printk("Before interpose\n");
		err = u2fs_interpose(dentry,dentry->d_sb); //wrapfs_interpose(dentry, dentry->d_sb, &lower_path);
		if (err) {/* path_put underlying path on error */
			printk("in lookup interpose failed\n");
			wrapfs_put_reset_lower_path(dentry);
		}

		printk("After interpose\n");
		goto out;
	}


	/* instatiate a new negative dentry */
	/* Need to set the parent of the negative dentry to the
	left branch */

	wrapfs_get_lower_path(parent,&lower_parent_path);


	lower_dir_dentry = lower_parent_path.dentry;
	lower_dir_mnt=lower_parent_path.mnt;
	this.name = name;
	this.len = strlen(name);
	this.hash = full_name_hash(this.name, this.len);
	printk("before the d_lookup method\n");
	lower_dentry = d_lookup(lower_dir_dentry, &this);
	if (lower_dentry)
		goto setup_lower;
	
	
	if(!lower_dentry){
		err=-EPERM;
		goto out;
	}

	/*
	printk("Did not go to lower\n");
	lower_dentry = d_alloc(lower_dir_dentry, &this);
	if (!lower_dentry) {
		err = -ENOMEM;
		goto out;
	}
	d_add(lower_dentry, NULL);*/ 

setup_lower:
	printk("in the lower\n");
	lower_path.dentry = lower_dentry;
	lower_path.mnt = mntget(lower_dir_mnt);
	if(!lower_path.mnt){
		printk("In wrapfs lookup this is the shit causing problem\n");
	}
	wrapfs_set_lower_path(dentry, &lower_path);

	/*
	 * If the intent is to create a file, then don't return an error, so
	 * the VFS will continue the process of making this negative dentry
	 * into a positive one.
	 */
	if (flags & (LOOKUP_CREATE|LOOKUP_RENAME_TARGET))
		err = 0;

out:
	wrapfs_put_lower_path(parent,&lower_parent_path);
	return ERR_PTR(err);
	
}

struct dentry *wrapfs_lookup(struct inode *dir, struct dentry *dentry,
			     struct nameidata *nd)
{

	struct dentry *ret, *parent;
	struct path lower_parent_path;
	int err = 0;

	BUG_ON(!nd);
	parent = dget_parent(dentry);

	printk("In the wrapfs lookup");	
	printk("Got the parent in lookup\n");

	wrapfs_get_lower_path(parent, &lower_parent_path);

	/* allocate dentry private data.  We free it in ->d_release */
	err = new_dentry_private_data(dentry);
	if (err) {
		ret = ERR_PTR(err);
		goto out;
	}
	ret = __wrapfs_lookup(dentry, nd->flags, parent);
	if (IS_ERR(ret))
		goto out;
	if (ret)
		dentry = ret;
	if (dentry->d_inode){
		if(wrapfs_lower_inode(dentry->d_inode)){
			fsstack_copy_attr_times(dentry->d_inode,
					wrapfs_lower_inode(dentry->d_inode));
		}

		else if(wrapfs_lower_inode_right(dentry->d_inode)){
			fsstack_copy_attr_times(dentry->d_inode,
                                        wrapfs_lower_inode_right(dentry->d_inode));
		}
	}
	/* update parent directory's atime */
	if(wrapfs_lower_inode(parent->d_inode)){
		fsstack_copy_attr_atime(parent->d_inode,
				wrapfs_lower_inode(parent->d_inode));
	}

	else if(wrapfs_lower_inode_right(parent->d_inode)){
		fsstack_copy_attr_atime(parent->d_inode,
                                wrapfs_lower_inode_right(parent->d_inode));

	}

out:
	wrapfs_put_lower_path(parent, &lower_parent_path);
	dput(parent);
	return ret;
}
