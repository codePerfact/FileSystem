#define IOCTL_ADD_BITMAP 11
#define IOCTL_SET_BITMAP 10
#define IOCTL_GET_BITMAP 12
/* operation type  */
#define OP_OPEN 1
#define OP_READ 2
#define OP_WRITE 4
#define OP_CLOSE 8
#define OP_LINK 16
#define OP_UNLINK 32
#define OP_SYMLINK 64
#define OP_MKDIR 128
#define OP_RMDIR 256
#define OP_MKNOD 512
#define OP_RENAME 1024
#define OP_READLN 2048

/*
 * This Structure is to hold a single
 * record which represent a single action taken by a user
 */
struct trfs_record {
	unsigned int op_type;
	mode_t record_mode;
	unsigned int record_flags;
	int record_ret_val;
	unsigned short record_path_len;
	unsigned short dest_path_len;
	unsigned int record_buf_len;
	char *buffer;
	unsigned int count;
	char *record_path_name;
	char *dest_path_name;
	size_t file_address;
	struct super_block *sb;
	dev_t dev;
};




