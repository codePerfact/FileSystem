#include "trfs.h"
#include "trfsglobal.h"
#include <linux/workqueue.h>

static struct workqueue_struct *wq;

static struct trfs_record*
getrecord(void) {
	struct trfs_record *record = NULL;

	record = kzalloc(sizeof(struct trfs_record), GFP_KERNEL);
	if (record == NULL)
		goto out;
	out: return record;
}

void
__putrecord(struct work_struct *work_arg) {
	int err = 0;
	mm_segment_t oldfs;
	unsigned long id;
	unsigned int recordsize = 0;
	int curr_buf_size = 0;
	struct trfs_sb_info *sb_info;
	struct work_cont
	*c_ptr = container_of(work_arg, struct work_cont, real_work);
	void *data = c_ptr->data;
	struct trfs_record *record = (struct trfs_record *) data;
	char *buffer = kmalloc(PAGE_SIZE, GFP_KERNEL);

	if (!buffer) {
		err = -ENOMEM;
		goto out;
	}

	if (record == NULL)
		goto out;
	sb_info = (struct trfs_sb_info *) record->sb->s_fs_info;
	if (sb_info == NULL)
		goto out;
	atomic_inc(&sb_info->curr_record_id);
	id = atomic_read(&sb_info->curr_record_id);
	if (record->op_type == OP_READ) {
		printk("[record] recording read\n");
		recordsize = 4 + sizeof(size_t) + 4 + 4;
	} else if (record->op_type == OP_OPEN) {
		printk("[record] recording open\n");
		if (!record->record_path_len || !record->record_path_name) {
			atomic_dec(&sb_info->curr_record_id);
			goto out;
		}
		recordsize = 4 + sizeof(size_t) + 4 + record->record_path_len + 4
+ sizeof(mode_t) + 4;
	} else if (record->op_type == OP_CLOSE) {
		printk("[record] recording close\n");
		recordsize = 4 + sizeof(size_t) + 4;
	} else if (record->op_type == OP_WRITE) {
		printk("[record] recording write\n");
		if (!record->record_buf_len || !record->buffer) {
			atomic_dec(&sb_info->curr_record_id);
			goto out;
		}
		recordsize = 4 + sizeof(size_t) + 4 + record->record_buf_len + 4 + 4;
	} else if (record->op_type == OP_LINK) {
		printk("[record] recording link\n");
		if (!record->record_path_len || !record->record_path_name
		|| !record->dest_path_len || !record->dest_path_name) {
			atomic_dec(&sb_info->curr_record_id);
			goto out;
		}
		recordsize = 4 + 4 + record->record_path_len + 4 + record->dest_path_len
		+ 4;
	} else if (record->op_type == OP_UNLINK) {
		printk("[record] recording unlink\n");
		if (!record->record_path_len || !record->record_path_name) {
			atomic_dec(&sb_info->curr_record_id);
			goto out;
		}
		recordsize = 4 + 4 + record->record_path_len + 4;
	} else if (record->op_type == OP_SYMLINK) {
		printk("recording symlink\n");
		if (!record->record_path_len || !record->record_path_name
		|| !record->buffer || !record->record_buf_len) {
			atomic_dec(&sb_info->curr_record_id);
			goto out;
		}
		recordsize = 4 + 4 + record->record_path_len + 4
		+ record->record_buf_len + 4;
	} else if (record->op_type == OP_MKDIR) {
		printk("recording mkdir\n");
		if (!record->record_path_name || !record->record_path_len) {
			atomic_dec(&sb_info->curr_record_id);
			goto out;
		}
		recordsize = 4 + 4 + record->record_path_len + sizeof(mode_t) + 4;
	} else if (record->op_type == OP_RMDIR) {
		printk("recording rmdir\n");
		if (!record->record_path_name || !record->record_path_len) {
			atomic_dec(&sb_info->curr_record_id);
			goto out;
		}
		recordsize = 4 + 4 + record->record_path_len + 4;
	} else if (record->op_type == OP_MKNOD) {
		printk("recording mknod\n");
		if (!record->record_path_len || !record->record_path_name) {
			atomic_dec(&sb_info->curr_record_id);
			goto out;
		}
		recordsize = 4 + 4 + record->record_path_len + sizeof(mode_t)
		+ sizeof(dev_t) + 4;
	} else if (record->op_type == OP_RENAME) {
		printk("we are recording rename\n");
		if (!record->record_path_len || !record->record_path_name
		|| !record->dest_path_len || !record->dest_path_name) {
			atomic_dec(&sb_info->curr_record_id);
			goto out;
		}
		recordsize = 4 + 4 + record->record_path_len + 4 + record->dest_path_len
		+ 4;
	} else if (record->op_type == OP_READLN) {
		printk("we are recording read link\n");
		if (!record->record_path_len || !record->record_path_name) {
			atomic_dec(&sb_info->curr_record_id);
			goto out;
		}
		recordsize = 4 + 4 + record->record_path_len + 4 + 4;
	} else {
		printk("[record] unknown operation type\n");
		goto out;
	}

	printk("[record] id : %lu\n", id);
	memcpy(buffer + curr_buf_size, &id, 4);
	curr_buf_size += 4;
	printk("[record] record size : %d\n", recordsize);
	memcpy(buffer + curr_buf_size, &recordsize, 4);
	curr_buf_size += 4;
	if (!record->op_type) {
		atomic_dec(&sb_info->curr_record_id);
		goto out;
	}
	memcpy(buffer + curr_buf_size, &record->op_type, 4);
	curr_buf_size += 4;
	if (record->file_address) {
		printk("[record] file address %u\n",
		       (unsigned int) record->file_address);
		memcpy(buffer + curr_buf_size, &record->file_address, sizeof(size_t));
		curr_buf_size += sizeof(size_t);
	}
	if (record->record_path_len && record->record_path_name) {
		printk("[record] pathname %s and path length %d\n",
		       record->record_path_name, record->record_path_len);
		memcpy(buffer + curr_buf_size, &record->record_path_len, 4);
		curr_buf_size += 4;
		memcpy(buffer + curr_buf_size, record->record_path_name,
		       record->record_path_len);
		curr_buf_size += record->record_path_len;
	}
	if (record->dest_path_len && record->dest_path_name) {
		printk("[record] dest pathname %s and dest path length %d\n",
		       record->dest_path_name, record->dest_path_len);
		memcpy(buffer + curr_buf_size, &record->dest_path_len, 4);
		curr_buf_size += 4;
		memcpy(buffer + curr_buf_size, record->dest_path_name,
		       record->dest_path_len);
		curr_buf_size += record->dest_path_len;
	}

	if (record->record_flags) {
		printk("[record] record flags %d\n", record->record_flags);
		memcpy(buffer + curr_buf_size, &record->record_flags, 4);
		curr_buf_size += 4;
	}

	if (record->record_mode) {
		printk("[record] record mode %d\n", (unsigned int) record->record_mode);
		memcpy(buffer + curr_buf_size, &record->record_mode, sizeof(mode_t));
		curr_buf_size += sizeof(mode_t);
	}

	if (record->record_buf_len && record->buffer) {
		printk("[record] record buffer and len %s and %d\n", record->buffer,
		       record->record_buf_len);
		memcpy(buffer + curr_buf_size, &record->record_buf_len, 4);
		curr_buf_size += 4;
		memcpy(buffer + curr_buf_size, record->buffer, record->record_buf_len);
		curr_buf_size += record->record_buf_len;
	}
	if (record->count) {
		printk("[record] record count %d\n", record->count);
		memcpy(buffer + curr_buf_size, &record->count, 4);
		curr_buf_size += 4;
	}

	if (record->op_type == OP_MKNOD) {
		printk("[record] record dev %d\n", (unsigned int) record->dev);
		memcpy(buffer + curr_buf_size, &record->dev, sizeof(dev_t));
		curr_buf_size += sizeof(dev_t);
	}

	printk("[record] ret value is %d\n", record->record_ret_val);
	memcpy(buffer + curr_buf_size, &record->record_ret_val, 4);
	curr_buf_size += 4;
	/* Extra credit part */

	/* charcalCheckSum(buffer, curr_buf_size); */

	/* End of extra credit */
	oldfs = get_fs();
	set_fs(KERNEL_DS);
	err = vfs_write(sb_info->recordFile, buffer, curr_buf_size,
	&sb_info->recordFile->f_pos);
	set_fs(oldfs);
	if (err < 0) {
		atomic_dec(&sb_info->curr_record_id);
		printk("[record] we could not write to the file\n");
	}
out:
	kfree(buffer);
	buffer = NULL;
	kfree(data);
	kfree(c_ptr);
}

static void
putrecord(void *data) {
	struct work_cont *record_w;

	record_w = kmalloc(sizeof(struct work_cont), GFP_KERNEL);
	INIT_WORK(&record_w->real_work, __putrecord);
	record_w->data = data;
	if (!wq) {
		printk("we are creating work queue\n");
		wq = create_singlethread_workqueue("mywq");
	}
	queue_work(wq, &record_w->real_work);
}

static void
destroywq(void) {
	if (wq)
		destroy_workqueue(wq);
}

const struct record_operations trfs_record_ops = { .getrecord = getrecord,
.putrecord = putrecord, .destroywq = destroywq };
