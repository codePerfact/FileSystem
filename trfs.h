/*
 * Copyright (c) 1998-2015 Erez Zadok
 * Copyright (c) 2009	   Shrikar Archak
 * Copyright (c) 2003-2015 Stony Brook University
 * Copyright (c) 2003-2015 The Research Foundation of SUNY
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _trFS_H_
#define _trFS_H_

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
#include <linux/xattr.h>
#include <linux/exportfs.h>

/* the file system name */
#define TRFS_NAME "trfs"

/* trfs root inode number */
#define trFS_ROOT_INO     1

/* useful for tracking code reachability */
#define UDBG printk(KERN_DEFAULT "DBG:%s:%s:%d\n", __FILE__, __func__, __LINE__)

/* operations vectors defined in specific files */
extern const struct file_operations trfs_main_fops;
extern const struct file_operations trfs_dir_fops;
extern const struct inode_operations trfs_main_iops;
extern const struct inode_operations trfs_dir_iops;
extern const struct inode_operations trfs_symlink_iops;
extern const struct record_operations trfs_record_ops;
extern const struct super_operations trfs_sops;
extern const struct dentry_operations trfs_dops;
extern const struct address_space_operations trfs_aops, trfs_dummy_aops;
extern const struct vm_operations_struct trfs_vm_ops;
extern const struct export_operations trfs_export_ops;

extern int trfs_init_inode_cache(void);
extern void trfs_destroy_inode_cache(void);
extern int trfs_init_dentry_cache(void);
extern void trfs_destroy_dentry_cache(void);
extern int new_dentry_private_data(struct dentry *dentry);
extern void free_dentry_private_data(struct dentry *dentry);
extern struct dentry *trfs_lookup(struct inode *dir, struct dentry *dentry,
				    unsigned int flags);
extern struct inode *trfs_iget(struct super_block *sb,
				 struct inode *lower_inode);
extern int trfs_interpose(struct dentry *dentry, struct super_block *sb,
			    struct path *lower_path);

/* file private data */
struct trfs_file_info {
	struct file *lower_file;
	const struct vm_operations_struct *lower_vm_ops;
};

struct work_cont {
	struct work_struct real_work;
	void *data;
};


/* trfs inode data in memory */
struct trfs_inode_info {
	struct inode *lower_inode;
	struct inode vfs_inode;
};

/* trfs dentry data in memory */
struct trfs_dentry_info {
	spinlock_t lock;	/* protects lower_path */
	struct path lower_path;
};

/*
 * Structure to contain the out buffer
 * information which will be used to write record to out file
 */
struct trfs_buf_info {
	char *buffer;
	short int start;
	short int curr_size;
	short int MAX_SIZE;
};



/* these is operation table which can be taken on record structure*/
struct record_operations {
    struct trfs_record *(*getrecord)(void);
    void (*putrecord)(void *);
    void (*destroywq)(void);
};

/* trfs super-block data in memory */
struct trfs_sb_info {
	struct super_block *lower_sb;
	struct file *recordFile;
	atomic_t curr_record_id;
	unsigned int bitmap;
};

/*put records parameter structure*/
struct put_params {
	struct trfs_record *record;
	struct super_block *sb;
};


/* options structure to contain mount options*/
struct options {
	void *lower_path_name;
	void *tracefile;
};

/*
 * inode to private data
 *
 * Since we use containers and the struct inode is _inside_ the
 * trfs_inode_info structure, trFS_I will always (given a non-NULL
 * inode pointer), return a valid non-NULL pointer.
 */
static inline struct trfs_inode_info *TRFS_I(const struct inode *inode)
{
	return container_of(inode, struct trfs_inode_info, vfs_inode);
}

/* dentry to private data */
#define trFS_D(dent) ((struct trfs_dentry_info *)(dent)->d_fsdata)

/* superblock to private data */
#define TRFS_SB(super) ((struct trfs_sb_info *)(super)->s_fs_info)

/* file to private Data */
#define trFS_F(file) ((struct trfs_file_info *)((file)->private_data))

/* file to lower file */
static inline struct file *trfs_lower_file(const struct file *f)
{
	return trFS_F(f)->lower_file;
}

static inline void trfs_set_lower_file(struct file *f, struct file *val)
{
	trFS_F(f)->lower_file = val;
}

/* inode to lower inode. */
static inline struct inode *trfs_lower_inode(const struct inode *i)
{
	return TRFS_I(i)->lower_inode;
}

static inline void trfs_set_lower_inode(struct inode *i, struct inode *val)
{
	TRFS_I(i)->lower_inode = val;
}

/* superblock to lower superblock */
static inline struct super_block *trfs_lower_super(
	const struct super_block *sb)
{
	return TRFS_SB(sb)->lower_sb;
}

static inline void trfs_set_lower_super(struct super_block *sb,
					  struct super_block *val)
{
	TRFS_SB(sb)->lower_sb = val;
}

/* path based (dentry/mnt) macros */
static inline void pathcpy(struct path *dst, const struct path *src)
{
	dst->dentry = src->dentry;
	dst->mnt = src->mnt;
}
/* Returns struct path.  Caller must path_put it. */
static inline void trfs_get_lower_path(const struct dentry *dent,
					 struct path *lower_path)
{
	spin_lock(&trFS_D(dent)->lock);
	pathcpy(lower_path, &trFS_D(dent)->lower_path);
	path_get(lower_path);
	spin_unlock(&trFS_D(dent)->lock);
	return;
}
static inline void trfs_put_lower_path(const struct dentry *dent,
					 struct path *lower_path)
{
	path_put(lower_path);
	return;
}
static inline void trfs_set_lower_path(const struct dentry *dent,
					 struct path *lower_path)
{
	spin_lock(&trFS_D(dent)->lock);
	pathcpy(&trFS_D(dent)->lower_path, lower_path);
	spin_unlock(&trFS_D(dent)->lock);
	return;
}
static inline void trfs_reset_lower_path(const struct dentry *dent)
{
	spin_lock(&trFS_D(dent)->lock);
	trFS_D(dent)->lower_path.dentry = NULL;
	trFS_D(dent)->lower_path.mnt = NULL;
	spin_unlock(&trFS_D(dent)->lock);
	return;
}
static inline void trfs_put_reset_lower_path(const struct dentry *dent)
{
	struct path lower_path;

	spin_lock(&trFS_D(dent)->lock);
	pathcpy(&lower_path, &trFS_D(dent)->lower_path);
	trFS_D(dent)->lower_path.dentry = NULL;
	trFS_D(dent)->lower_path.mnt = NULL;
	spin_unlock(&trFS_D(dent)->lock);
	path_put(&lower_path);
	return;
}

/* locking helpers */
static inline struct dentry *lock_parent(struct dentry *dentry)
{
	struct dentry *dir = dget_parent(dentry);

	inode_lock_nested(d_inode(dir), I_MUTEX_PARENT);
	return dir;
}

static inline void unlock_dir(struct dentry *dir)
{
	inode_unlock(d_inode(dir));
	dput(dir);
}
#endif	/* not _TRFS_H_ */
