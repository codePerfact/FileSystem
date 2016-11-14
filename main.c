#include "trfs.h"
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/string.h>
#include <linux/workqueue.h>

/*
 * This method is being used to initiate appropriate data structures
 * while mount, these data structures will be used to record and user commands
 * @sb : super block
 * @filename : file path in which records to be written
 * Returns : -ve number of ERR, 0 on success
 */
int
preprocess_trfs(struct super_block *sb, char *filename) {
	int err = 0;
	struct file *recordfile = NULL;
	struct trfs_sb_info *sb_info = NULL;

	/* Opening infile */
	if (filename) {
		recordfile = filp_open(filename + 6, O_WRONLY | O_CREAT, 0644);
		if (!recordfile || IS_ERR(recordfile)) {
			err = -EACCES;
			goto out;
		}
	} else {
		printk("filename was not passed properly\n");
		err = -EFAULT;
		goto out;
	}

	/*Attaching private data structures to super block*/
	sb_info = (struct trfs_sb_info *) sb->s_fs_info;
	sb_info->recordFile = recordfile;
	atomic_set(&sb_info->curr_record_id, 0);
	sb_info->bitmap = 0xffff;
	printk("bit map value is %x\n", sb_info->bitmap);
out:
	return err;
}

/*
 * There is no need to lock the trfs_super_info's rwsem as there is no
 * way anyone can have a reference to the superblock at this point in time.
 */
static int
trfs_read_super(struct super_block *sb, void *raw_data, int silent) {
	int err = 0;
	struct super_block *lower_sb;
	struct path lower_path;
	struct inode *inode;
	struct options *opt = (struct options *) raw_data;
	char *dev_name = (char *) opt->lower_path_name;
	char *filename = (char *) opt->tracefile;

	if (!dev_name) {
		printk(KERN_ERR
		"trfs: read_super: missing dev_name argument\n");
		err = -EINVAL;
		goto out;
	}
	if (!filename) {
		printk(KERN_ERR
		"trfs: read_super: missing trace file name argument\n");
		err = -EINVAL;
		goto out;
	}

	/* parse lower path */
	err = kern_path(dev_name, LOOKUP_FOLLOW |
	LOOKUP_DIRECTORY, &lower_path);
	if (err) {
		printk(KERN_ERR
		"trfs: error accessing lower directory '%s'\n", dev_name);
		goto out;
	}

	/* allocate superblock private data */
	sb->s_fs_info = kzalloc(sizeof(struct trfs_sb_info), GFP_KERNEL);
	if (!TRFS_SB(sb)) {
		printk(KERN_CRIT
		"trfs: read_super: out of memory\n");
		err = -ENOMEM;
		goto out_free;
	}

	/* creating appropriate data structures for record action*/
	printk("filename passing from read super to preprocess : %s\n",
	filename);
	err = preprocess_trfs(sb, filename);
	if (err < 0) {
		printk("trfs: read_super: error in preprocessing\n");
		goto out_preproces;
	}

	/* set the lower superblock field of upper superblock */
	lower_sb = lower_path.dentry->d_sb;
	atomic_inc(&lower_sb->s_active);
	trfs_set_lower_super(sb, lower_sb);

	/* inherit maxbytes from lower file system */
	sb->s_maxbytes = lower_sb->s_maxbytes;

	/*
	 * Our c/m/atime granularity is 1 ns because we may stack on file
	 * systems whose granularity is as good.
	 */
	sb->s_time_gran = 1;

	sb->s_op = &trfs_sops;

	sb->s_export_op = &trfs_export_ops; /* adding NFS support */

	/* get a new inode and allocate our root dentry */
	inode = trfs_iget(sb, d_inode(lower_path.dentry));
	if (IS_ERR(inode)) {
		err = PTR_ERR(inode);
		goto out_sput;
	}
	sb->s_root = d_make_root(inode);
	if (!sb->s_root) {
		err = -ENOMEM;
		goto out_iput;
	}
	d_set_d_op(sb->s_root, &trfs_dops);

	/* link the upper and lower dentries */
	sb->s_root->d_fsdata = NULL;
	err = new_dentry_private_data(sb->s_root);
	if (err)
		goto out_freeroot;

	/* if get here: cannot have error */

	/* set the lower dentries for s_root */
	trfs_set_lower_path(sb->s_root, &lower_path);

	/*
	 * No need to call interpose because we already have a positive
	 * dentry, which was instantiated by d_make_root.  Just need to
	 * d_rehash it.
	 */
	d_rehash(sb->s_root);
	if (!silent)
	printk(KERN_INFO
	"trfs: mounted on top of %s type %s\n",
	dev_name, lower_sb->s_type->name);
	goto out;
	/* all is well */

	/* no longer needed: free_dentry_private_data(sb->s_root); */
out_freeroot:
	dput(sb->s_root);
out_iput:
	iput(inode);
out_sput:
	/* drop refs we took earlier */
	atomic_dec(&lower_sb->s_active);
out_preproces:
	kfree(TRFS_SB(sb));
	sb->s_fs_info = NULL;
out_free:
	path_put(&lower_path);
out:
	return err;
}

struct dentry *
trfs_mount(struct file_system_type *fs_type, int flags, const char *dev_name,
void *raw_data) {
	struct options *opt = NULL;
	struct dentry *ret = NULL;

	opt = kzalloc(sizeof(struct options *), GFP_KERNEL);
	if (opt == NULL)
		goto out;
	opt->lower_path_name = (void *) dev_name;
	opt->tracefile = (void *) raw_data;
	if (!opt->tracefile) {
		printk("[TRFS_MOUNT] : trace file not noticed\n");
		goto out;
	}
	printk("filename passing from mount to read_super %s\n",
	       (char *) opt->tracefile);
	ret = mount_nodev(fs_type, flags, (void *) opt, trfs_read_super);
out:
	if (opt) {
		kfree(opt);
		opt = NULL;
	}
	return ret;
}

void
trfs_umount(struct super_block *sb) {
	struct trfs_sb_info *sb_info = NULL;

	sb_info = (struct trfs_sb_info *) sb->s_fs_info;
	if (sb_info == NULL)
		goto out;

	/*Cleaning all the data structures created during mount*/
	if (sb_info->recordFile) {
		filp_close(sb_info->recordFile, NULL);
		sb_info->recordFile = NULL;
	}
	printk("bit map value is %x\n", sb_info->bitmap);
out:
	if (sb_info) {
		kfree(sb_info);
		sb->s_fs_info = NULL;
	}
	trfs_record_ops.destroywq();
	generic_shutdown_super(sb);
}

static struct file_system_type trfs_fs_type = { .owner = THIS_MODULE, .name =
TRFS_NAME, .mount = trfs_mount, .kill_sb = trfs_umount, .fs_flags = 0, };
MODULE_ALIAS_FS(TRFS_NAME);

static int __init

init_trfs_fs(void) {
	int err;

	pr_info("Registering trfs "
	TRFS_VERSION
	"\n");

	err = trfs_init_inode_cache();
	if (err)
		goto out;
	err = trfs_init_dentry_cache();
	if (err)
		goto out;
	err = register_filesystem(&trfs_fs_type);
out:
	if (err) {
		trfs_destroy_inode_cache();
		trfs_destroy_dentry_cache();
	}
	return err;
}

static void __exit

exit_trfs_fs(void) {
	trfs_destroy_inode_cache();
	trfs_destroy_dentry_cache();
	unregister_filesystem(&trfs_fs_type);
	pr_info("Completed trfs module unload\n");
}

MODULE_AUTHOR("Shilpa Gupta, Filesystems and Storage Lab, Stony Brook University"
"for tracing user actions");
MODULE_DESCRIPTION("trfs " TRFS_VERSION
"for tracing user events");
MODULE_LICENSE("GPL");

module_init(init_trfs_fs);
module_exit(exit_trfs_fs);
