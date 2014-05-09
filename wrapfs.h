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

#ifndef _WRAPFS_H_
#define _WRAPFS_H_

#include <linux/dcache.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/mount.h>
#include <linux/namei.h>
#include <linux/seq_file.h>
#include <linux/statfs.h>
#include <linux/fs_stack.h>
#include <linux/magic.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/sched.h>

/* the file system name */
#define WRAPFS_NAME "u2fs"

/* wrapfs root inode number */
#define WRAPFS_ROOT_INO     1

#define MAX_BRANCHES 2

#define U2FS_WHLEN 4

#define U2FS_WHPFX ".wh."

/* useful for tracking code reachability */
#define UDBG printk(KERN_DEFAULT "DBG:%s:%s:%d\n", __FILE__, __func__, __LINE__)

/* operations vectors defined in specific files */
extern const struct file_operations wrapfs_main_fops;
extern const struct file_operations wrapfs_dir_fops;
extern const struct inode_operations wrapfs_main_iops;
extern const struct inode_operations wrapfs_dir_iops;
extern const struct inode_operations wrapfs_symlink_iops;
extern const struct super_operations wrapfs_sops;
extern const struct dentry_operations wrapfs_dops;
extern const struct address_space_operations wrapfs_aops, wrapfs_dummy_aops;
extern const struct vm_operations_struct wrapfs_vm_ops;

extern int wrapfs_init_inode_cache(void);
extern void wrapfs_destroy_inode_cache(void);
extern int wrapfs_init_dentry_cache(void);
extern void wrapfs_destroy_dentry_cache(void);
extern int new_dentry_private_data(struct dentry *dentry);
extern void free_dentry_private_data(struct dentry *dentry);
extern struct dentry *wrapfs_lookup(struct inode *dir, struct dentry *dentry,
				    struct nameidata *nd);
extern struct inode *wrapfs_iget(struct super_block *sb,
				 struct inode *lower_inode);
extern struct inode *u2fs_iget(struct super_block *sb);
extern int  u2fs_fill_inode(struct dentry *dent,struct inode *inode);

extern int u2fs_interpose(struct dentry *dentry, struct super_block *sb);
extern int wrapfs_interpose(struct dentry *dentry, struct super_block *sb,
			    struct path *lower_path);


extern struct dentry *create_parents(struct inode *dir,struct dentry *dentry,
					const char*name);

extern char *alloc_whname(const char *name, const char*pname, int len, int plen);

/* file private data */
struct wrapfs_file_info {
	struct file *lower_file;
	const struct vm_operations_struct *lower_vm_ops;
	struct file *lower_file_right;
};

/* wrapfs inode data in memory */
struct wrapfs_inode_info {
	struct inode *lower_inode;
	struct inode *lower_inode_right;
	struct inode vfs_inode;
};

/* wrapfs dentry data in memory */
struct wrapfs_dentry_info {
	spinlock_t lock;	/* protects lower_path */
	struct path lower_path;
	struct path lower_path_right;
};

/* wrapfs super-block data in memory */
struct wrapfs_sb_info {
	struct super_block *lower_sb;
	struct super_block *lower_sb_right;
	struct rw_semaphore rwsem;
	pid_t write_lock_owner;
	
};

/*
 * inode to private data
 *
 * Since we use containers and the struct inode is _inside_ the
 * wrapfs_inode_info structure, WRAPFS_I will always (given a non-NULL
 * inode pointer), return a valid non-NULL pointer.
 */
static inline struct wrapfs_inode_info *WRAPFS_I(const struct inode *inode)
{
	return container_of(inode, struct wrapfs_inode_info, vfs_inode);
}

/* dentry to private data */
#define WRAPFS_D(dent) ((struct wrapfs_dentry_info *)(dent)->d_fsdata)

/* superblock to private data */

/* superblock to private data */
#define WRAPFS_SB(super) ((struct wrapfs_sb_info *)(super)->s_fs_info)

/* file to private Data */
#define WRAPFS_F(file) ((struct wrapfs_file_info *)((file)->private_data))

/* file to lower file */
static inline struct file *wrapfs_lower_file(const struct file *f)
{
	return WRAPFS_F(f)->lower_file;
}

static inline struct file *wrapfs_lower_file_right(const struct file *f){
	return WRAPFS_F(f)->lower_file_right;
}

static inline void wrapfs_set_lower_file(struct file *f, struct file *val,int i)
{
	if(i==0)
		WRAPFS_F(f)->lower_file = val;
	if(i==1)
		WRAPFS_F(f)->lower_file_right=val;
}



/* inode to lower inode. */
static inline struct inode *wrapfs_lower_inode(const struct inode *i)
{
	return WRAPFS_I(i)->lower_inode;
}

static inline struct inode *wrapfs_lower_inode_right(const struct inode *i){
	return WRAPFS_I(i)->lower_inode_right;
}

static inline void wrapfs_set_lower_inode(struct inode *i, struct inode *val,int idx)
{
	if(idx==0)
		WRAPFS_I(i)->lower_inode = val;
	if(idx==1)
		WRAPFS_I(i)->lower_inode_right=val;
}



/* superblock to lower superblock */
static inline struct super_block *wrapfs_lower_super(
	const struct super_block *sb)
{
	return WRAPFS_SB(sb)->lower_sb;
}

static inline struct super_block *wrapfs_lower_super_right(const struct super_block *sb){
	return WRAPFS_SB(sb)->lower_sb_right;
}

static inline void wrapfs_set_lower_super(struct super_block *sb,
					  struct super_block *val)
{
	WRAPFS_SB(sb)->lower_sb = val;
}

static inline void wrapfs_set_lower_super_right(struct super_block *sb,
						struct super_block *val)
{
	WRAPFS_SB(sb)->lower_sb_right=val;
}



/* path based (dentry/mnt) macros */
static inline struct dentry *lookup_lck_len(const char*name,struct dentry *base,int len){

	struct dentry *d;
	
	d=NULL;
	mutex_lock(&base->d_inode->i_mutex);
	d=lookup_one_len(name,base,len);
	mutex_unlock(&base->d_inode->i_mutex);

	return d;
	

}



static inline void pathcpy(struct path *dst, const struct path *src)
{
	dst->dentry = src->dentry;
	dst->mnt = src->mnt;
}
/* Returns struct path.  Caller must path_put it. */
static inline void wrapfs_get_lower_path(const struct dentry *dent,
					 struct path *lower_path)
{
	spin_lock(&WRAPFS_D(dent)->lock);
	pathcpy(lower_path, &WRAPFS_D(dent)->lower_path);
	path_get(lower_path);
	spin_unlock(&WRAPFS_D(dent)->lock);
	return;
}

static inline void wrapfs_get_lower_path_right(const struct dentry *dent,
                                         struct path *lower_path)
{
        spin_lock(&WRAPFS_D(dent)->lock);
        pathcpy(lower_path, &WRAPFS_D(dent)->lower_path_right);
        path_get(lower_path);
        spin_unlock(&WRAPFS_D(dent)->lock);
        return;
}


static inline struct dentry* wrapfs_get_lower_dentry_idx(const struct dentry *dent, int i){
	if(i==0)
		return WRAPFS_D(dent)->lower_path.dentry;

	if(i==1)
		return WRAPFS_D(dent)->lower_path_right.dentry;
	
	return NULL;

}

static inline void wrapfs_put_lower_path(const struct dentry *dent,
					 struct path *lower_path)
{
	path_put(lower_path);
	return;
}
static inline void wrapfs_set_lower_path(const struct dentry *dent,
					 struct path *lower_path)
{
	spin_lock(&WRAPFS_D(dent)->lock);
	pathcpy(&WRAPFS_D(dent)->lower_path, lower_path);
	spin_unlock(&WRAPFS_D(dent)->lock);
	return;
}

static inline void wrapfs_set_lower_path_right(const struct dentry *dent,
                                         struct path *lower_path)
{
        spin_lock(&WRAPFS_D(dent)->lock);
        pathcpy(&WRAPFS_D(dent)->lower_path_right, lower_path);
        spin_unlock(&WRAPFS_D(dent)->lock);
        return;
}


static inline void wrapfs_reset_lower_path(const struct dentry *dent)
{
	WRAPFS_D(dent)->lower_path.mnt = NULL;
	spin_unlock(&WRAPFS_D(dent)->lock);
	return;
}

static inline void wrapfs_reset_lower_path_right(const struct dentry *dent){
	spin_lock(&WRAPFS_D(dent)->lock);
        WRAPFS_D(dent)->lower_path_right.dentry = NULL;
        WRAPFS_D(dent)->lower_path_right.mnt = NULL;
        spin_unlock(&WRAPFS_D(dent)->lock);
        return;


}

static inline void wrapfs_put_reset_lower_path(const struct dentry *dent)
{
	struct path lower_path, lower_path_right;
	int x=0,y=0;
	spin_lock(&WRAPFS_D(dent)->lock);
	printk("In the spin lock of lower_path and name is %s\n",dent->d_name.name);
	if(WRAPFS_D(dent)->lower_path.dentry){
		printk("In the if for lower_path\n");
		pathcpy(&lower_path, &WRAPFS_D(dent)->lower_path);
		WRAPFS_D(dent)->lower_path.dentry = NULL;
		WRAPFS_D(dent)->lower_path.mnt = NULL;
		x=1;
	}
	if(WRAPFS_D(dent)->lower_path_right.dentry){
                printk("In the if for lower_path_right\n");
                pathcpy(&lower_path_right, &WRAPFS_D(dent)->lower_path_right);
                WRAPFS_D(dent)->lower_path_right.dentry = NULL;
                WRAPFS_D(dent)->lower_path_right.mnt = NULL;
		y=1;
        }
	spin_unlock(&WRAPFS_D(dent)->lock);
	printk("spin lock released for lower_path\n");
	if(x==1)
		path_put(&lower_path);
	if(y==1)
		path_put(&lower_path_right);
	return;
}


static inline void wrapfs_put_reset_lower_path_right(const struct dentry *dent){
        struct path lower_path;
        spin_lock(&WRAPFS_D(dent)->lock);
	printk("In the spin lock for lower_path_right\n");
	if(WRAPFS_D(dent)->lower_path_right.dentry){
		printk("In the if for lower_path_right\n");
        	pathcpy(&lower_path, &WRAPFS_D(dent)->lower_path_right);
        	WRAPFS_D(dent)->lower_path.dentry = NULL;
        	WRAPFS_D(dent)->lower_path.mnt = NULL;
	}
        spin_unlock(&WRAPFS_D(dent)->lock);
	printk("Spinlock released for lower_path_right\n");
        path_put(&lower_path);
        return;


}


/* locking helpers */
static inline struct dentry *lock_parent(struct dentry *dentry)
{
	struct dentry *dir = dget_parent(dentry);
	mutex_lock_nested(&dir->d_inode->i_mutex, I_MUTEX_PARENT);
	return dir;
}

static inline void unlock_dir(struct dentry *dir)
{
	mutex_unlock(&dir->d_inode->i_mutex);
	dput(dir);
}
#endif	/* not _WRAPFS_H_ */
