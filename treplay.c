#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <getopt.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <openssl/sha.h>
#include "trfsglobal.h"

struct link_list {
	struct link_list *next;
	size_t file_addr;
	int fd;
};

static struct link_list *head;

#define N 0x01
#define S 0x02
void
addToList(int fd, size_t addr) {
	struct link_list *node = (struct link_list *) malloc(
	    sizeof(struct link_list));
	node->fd = fd;
	node->file_addr = addr;
	node->next = NULL;
	if (head == NULL)
		head = node;
	else
		head->next = node;
}

void
rmFromList(size_t addr) {
	struct link_list *current = head;
	struct link_list *tmp;

	if (current == NULL) {
		printf("list is empty\n");
		return;
	}
	if (current->file_addr == addr) {
		head = head->next;
		free(current);
		return;
	}
	while (current->next != NULL && current->next->file_addr != addr)
		current = current->next;
	if (current->next == NULL) {
		printf("item asked to remove was not in the list\n");
		return;
	} else{
		tmp = current->next;
		current->next = tmp->next;
		tmp->next = NULL;
		free(tmp);
	}
	return;
}

int
getfdFromAddr(size_t addr) {
	struct link_list *current = head;

	if (current == NULL) {
		return -1;
	} else{
		while (current) {
			if (current->file_addr == addr) {
				return current->fd;
			}
			current = current->next;
		}
	}
	return -1;
}

void
distroyList() {
	struct link_list *current = head;
	struct link_list *tmp = head;

	while (current != NULL) {
		close(current->fd);
		tmp = current;
		current = current->next;
		free(tmp);
	}
}

int
main(int argc, char **argv) {
	int err;
	int option;
	int flags = 0;
	char *filename = NULL;
	unsigned long *recordId = NULL;;
	unsigned short *recordSize = NULL;
	char *buff = NULL;
	char *read_buff = NULL;
	unsigned short index;
	int fd = NULL;
	int record_fd;
	 /* SHA_CTX ctx;
	unsigned char hash[SHA1_LENGTH]; */

	struct trfs_record *record = NULL;
	record = (struct trfs_record *)malloc(sizeof(struct trfs_record));
	recordId = (unsigned long *) malloc(sizeof(unsigned long));
	recordSize = (unsigned short *) malloc(sizeof(unsigned short));

	while ((option = getopt(argc, argv, "ns")) != -1) {
			switch (option) {
			case 'n':
				flags = flags | N;
				break;
			case 's':
				flags = flags | S;
				break;
			default:
				err = -1;
				printf("[MAIN] : Invalid option \n");
				goto out;
		}
	}

	if ((optind + 1) > argc) {
		printf("[main] : Inappropriate number of arguments\n");
		goto out;
	}
	if ((flags & N) && (flags & S)) {
		printf("[TREPLAY] Please choose either n or s\n");
		goto out;
	}
	filename = argv[optind];

	fd = open(filename, O_RDONLY);
	if (fd < 0) {
		goto out;
	}
	while ((err = read(fd, recordId, 4)) > 0) {
		printf("******* Playing Record id : %lu********\n", *recordId);




		err = read(fd, recordSize, 4);
		if (err < 0) {
			goto out;
		}
		buff = (char *) malloc(*recordSize);
		read(fd, buff, *recordSize);
		index = 0;
		memcpy(&record->op_type, buff + index, 4);
		index = index + 4;
		printf("op_type is %d\n", record->op_type);
			switch (record->op_type) {
			case OP_OPEN:
				memcpy(&record->file_address, buff + index, sizeof(size_t));
				index = index + sizeof(size_t);
				memcpy(&record->record_path_len, buff + index, 4);
				index = index + 4;
				record->record_path_name = (char *)malloc(record->record_path_len + 1);
				memcpy(record->record_path_name, buff + index,
				       record->record_path_len);
				index = index + record->record_path_len;
				record->record_path_name[record->record_path_len] = '\0';
				memcpy(&record->record_flags, buff + index, 4);
				index = index + 4;
				memcpy(&record->record_mode, buff + index, sizeof(mode_t));
				index = index + sizeof(mode_t);
				memcpy(&record->record_ret_val, buff + index, 4);
				index = index + 4;
				printf(
				    "we will perform open syscall with following arguments : %s, %d, %d, expected ret value : %d\n",
				    record->record_path_name, record->record_flags, record->record_mode,
				    record->record_ret_val);
				if (!(flags & N)) {
					record_fd = open(record->record_path_name+1, record->record_flags,
					                 record->record_mode);
					if (record_fd > 0) {
						addToList(record_fd, record->file_address);
					}
					if (record->record_ret_val == 0 && record_fd > 0) {
						printf("***successfully played the record\n***");
					} else{
						printf("***Deviation is return value****\n");
						if (flags & S) {
							printf("**Aborting reply**\n");
							goto out;
						}
					}
				}
				free(record->record_path_name);
				record->record_path_name = NULL;
				break;
			case OP_READ:
				memcpy(&record->file_address, buff + index, sizeof(size_t));
				index = index + sizeof(size_t);
				memcpy(&record->count, buff + index, 4);
				index += 4;
				memcpy(&record->record_ret_val, buff + index, 4);
				index += 4;
				printf(
				    "we will perform read on last open file with below arg : %d , expected ret val : %d\n",
				    record->count, record->record_ret_val);
				if (!(flags & N)) {
					record_fd = getfdFromAddr(record->file_address);
					if (record_fd > 0) {
						read_buff = (char *) malloc(record->count);
						err = read(record_fd, read_buff, record->count);
						if (err == record->record_ret_val) {
							printf("***successfully played the record\n***");
						} else{
							printf("***Deviation is return value****\n");
							if (flags & S) {
								printf("********aborting replay******\n");
								goto out;
							}
						}
						free(read_buff);
						read_buff = NULL;
					} else{
						printf("***** Out of sequence record*******\n");
					}
				}
				break;
			case OP_WRITE:
				memcpy(&record->file_address, buff + index, sizeof(size_t));
				index += sizeof(size_t);
				memcpy(&record->record_buf_len, buff + index, 4);
				index += 4;
				record->buffer = (char *) malloc(record->record_buf_len + 1);
				memcpy(record->buffer, buff + index, record->record_buf_len);
				index += record->record_buf_len;
				record->buffer[record->record_buf_len] = '\0';
				memcpy(&record->count, buff + index, 4);
				index += 4;
				memcpy(&record->record_ret_val, buff + index, 4);
				index = index + 4;
				printf("we will write to corresponding file the text %s\n",
				       record->buffer);
				if (!(flags & N)) {
					record_fd = getfdFromAddr(record->file_address);
					if (record_fd > 0) {
						err = write(record_fd, record->buffer, record->count);
						if (err == record->record_ret_val) {
							printf("successfully played the record\n");
						} else{
							printf("deviation in return value\n");
							if (flags & S) {
								printf("Aborting\n");
								goto out;
							}
						}
					} else{
						printf("out of sequence record \n");
					}
				}
					free(record->buffer);
					record->buffer = NULL;
				break;
			case OP_CLOSE:
				memcpy(&record->file_address, buff + index, sizeof(size_t));
				index += sizeof(size_t);
				memcpy(&record->record_ret_val, buff + index, 4);
				printf("we will close last opened file expected val : %d\n",
				       record->record_ret_val);
				if (!(flags & N)) {
					record_fd = getfdFromAddr(record->file_address);
					if (record_fd > 0) {
						err = close(record_fd);
						rmFromList(record->file_address);
						if (err == record->record_ret_val) {
							printf("***successfully played the record\n***");
						} else{
							printf("***Deviation is return value****\n");
							if (flags & S) {
								printf("********aborting replay******\n");
								goto out;
							}
						}
					} else{
						printf("***** Out of sequence record*******\n");
					}
				}
				break;
			case OP_LINK:
				memcpy(&record->record_path_len, buff + index, 4);
				index = index + 4;
				record->record_path_name = (char *) malloc(
				    record->record_path_len + 1);
				memcpy(record->record_path_name, buff + index,
				       record->record_path_len);
				index = index + record->record_path_len;
				record->record_path_name[record->record_path_len] = '\0';
				memcpy(&record->dest_path_len, buff + index, 4);
				index = index + 4;
				record->dest_path_name = (char *) malloc(record->dest_path_len + 1);
				memcpy(record->dest_path_name, buff + index, record->dest_path_len);
				index = index + record->dest_path_len;
				record->dest_path_name[record->dest_path_len] = '\0';
				memcpy(&record->record_ret_val, buff + index, 4);
				index += 4;
				printf(
				    "we will perform link sys call with params %s, %s, expected ret val is : %d\n",
				    record->record_path_name, record->dest_path_name,
				    record->record_ret_val);
				if (!(flags & N)) {
					err = link(record->record_path_name+1, record->dest_path_name+1);
					if (err == record->record_ret_val) {
						printf("***successfully played the record\n***");
					} else{
						printf("***Deviation is return value****\n");
						if (flags & S) {
							printf("********aborting replay******\n");
							goto out;
						}
					}
				}
				free(record->record_path_name);
				record->record_path_name = NULL;
				free(record->dest_path_name);
				record->dest_path_name = NULL;
				break;
			case OP_UNLINK:
				memcpy(&record->record_path_len, buff + index, 4);
				index = index + 4;
				record->record_path_name = (char *) malloc(
				    record->record_path_len + 1);
				memcpy(record->record_path_name, buff + index,
				       record->record_path_len);
				index = index + record->record_path_len;
				record->record_path_name[record->record_path_len] = '\0';
				memcpy(&record->record_ret_val, buff + index, 4);
				index += 4;
				printf(
				    "we will perform unlink sys call with params %s, expected ret val is : %d\n",
				    record->record_path_name, record->record_ret_val);
				if (!(flags & N)) {
					err = unlink(record->record_path_name+1);
					if (err == record->record_ret_val) {
						printf("***successfully played the record\n***");
					} else{
						printf("***Deviation is return value****\n");
						if (flags & S) {
							printf("********aborting replay******\n");
							goto out;
						}
					}
				}
				free(record->record_path_name);
				record->record_path_name = NULL;
				break;
			case OP_SYMLINK:
				memcpy(&record->record_path_len, buff + index, 4);
				index = index + 4;
				record->record_path_name = (char *) malloc(
				    record->record_path_len + 1);
				memcpy(record->record_path_name, buff + index,
				       record->record_path_len);
				index = index + record->record_path_len;
				record->record_path_name[record->record_path_len] = '\0';
				memcpy(&record->record_buf_len, buff + index, 4);
				index = index + 4;
				record->buffer = (char *) malloc(record->record_buf_len + 1);
				memcpy(record->buffer, buff + index, record->record_buf_len);
				index = index + record->record_buf_len;
				record->buffer[record->record_buf_len] = '\0';
				memcpy(&record->record_ret_val, buff + index, 4);
				index += 4;
				printf(
				    "we will perform symlink sys call with params %s,%s, expected ret val is : %d\n",
				    record->record_path_name, record->buffer, record->record_ret_val);
				if (!(flags & N)) {
					err = symlink(record->record_path_name, record->buffer+1);
					if (err == record->record_ret_val) {
						printf("***successfully played the record\n***");
					} else{
						printf("***Deviation is return value****\n");
						if (flags & S) {
							printf("********aborting replay******\n");
							goto out;
						}
					}
				}
				free(record->record_path_name);
				record->record_path_name = NULL;
				free(record->buffer);
				record->buffer = NULL;
				break;
			case OP_MKDIR:
				memcpy(&record->record_path_len, buff + index, 4);
				index = index + 4;
				record->record_path_name = (char *) malloc(
				    record->record_path_len + 1);
				memcpy(record->record_path_name, buff + index,
				       record->record_path_len);
				index = index + record->record_path_len;
				record->record_path_name[record->record_path_len] = '\0';
				memcpy(&record->record_mode, buff + index, sizeof(mode_t));
				index = index + sizeof(mode_t);
				memcpy(&record->record_ret_val, buff + index, 4);
				index += 4;
				printf(
				    "we will perform mkdir sys call with params %s,%d, expected ret val is : %d\n",
				    record->record_path_name, record->record_mode,
				    record->record_ret_val);
				if (!(flags & N)) {
					err = mkdir(record->record_path_name+1, record->record_mode);
					if (err == record->record_ret_val) {
						printf("***successfully played the record\n***");
					} else{
						printf("***Deviation is return value****\n");
						if (flags & S) {
							printf("********aborting replay******\n");
							goto out;
						}
					}
				}
				free(record->record_path_name);
				record->record_path_name = NULL;
				break;
			case OP_RMDIR:
				memcpy(&record->record_path_len, buff + index, 4);
				index = index + 4;
				record->record_path_name = (char *) malloc(
				    record->record_path_len + 1);
				memcpy(record->record_path_name, buff + index,
				       record->record_path_len);
				index = index + record->record_path_len;
				record->record_path_name[record->record_path_len] = '\0';
				memcpy(&record->record_ret_val, buff + index, 4);
				index += 4;
				printf(
				    "we will perform rmdir sys call with params %s, expected ret val is : %d\n",
				    record->record_path_name, record->record_ret_val);
				if (!(flags & N)) {
					err = rmdir(record->record_path_name+1);
					if (err == record->record_ret_val) {
						printf("***successfully played the record\n***");
					} else{
						printf("***Deviation is return value****\n");
						if (flags & S) {
							printf("********aborting replay******\n");
							goto out;
						}
					}
				}
				free(record->record_path_name);
				record->record_path_name = NULL;
				break;
			case OP_MKNOD:
				memcpy(&record->record_path_len, buff + index, 4);
				index = index + 4;
				record->record_path_name = (char *) malloc(
				    record->record_path_len + 1);
				memcpy(record->record_path_name, buff + index,
				       record->record_path_len);
				index = index + record->record_path_len;
				record->record_path_name[record->record_path_len] = '\0';
				memcpy(&record->record_mode, buff + index, sizeof(mode_t));
				index += sizeof(mode_t);
				memcpy(&record->dev, buff + index, sizeof(dev_t));
				index += sizeof(dev_t);
				memcpy(&record->record_ret_val, buff + index, 4);
				index += 4;
				printf(
				    "we will perform mknod sys call with params %s, %d, %d, expected ret val is : %d\n",
				    record->record_path_name, (unsigned int) record->record_mode,
				    (unsigned int) record->dev, record->record_ret_val);
				if (!(flags & N)) {
					err = mknod(record->record_path_name+1, record->record_mode,
					            record->dev);
					if (err == record->record_ret_val) {
						printf("***successfully played the record\n***");
					} else{
						printf("***Deviation is return value****\n");
						if (flags & S) {
							printf("********aborting replay******\n");
							goto out;
						}
					}
				}
				free(record->record_path_name);
				record->record_path_name = NULL;
				break;
			case OP_RENAME:
				memcpy(&record->record_path_len, buff + index, 4);
				index = index + 4;
				record->record_path_name = (char *) malloc(
				    record->record_path_len + 1);
				memcpy(record->record_path_name, buff + index,
				       record->record_path_len);
				index = index + record->record_path_len;
				record->record_path_name[record->record_path_len] = '\0';
				memcpy(&record->dest_path_len, buff + index, 4);
				index = index + 4;
				record->dest_path_name = (char *) malloc(record->dest_path_len + 1);
				memcpy(record->dest_path_name, buff + index, record->dest_path_len);
				index = index + record->dest_path_len;
				record->dest_path_name[record->dest_path_len] = '\0';
				memcpy(&record->record_ret_val, buff + index, 4);
				index += 4;
				printf(
				    "we will perform rename sys call with params %s, %s, expected ret val is : %d\n",
				    record->record_path_name, record->dest_path_name,
				    record->record_ret_val);
				if (!(flags & N)) {
					err = rename(record->record_path_name+1, record->dest_path_name+1);
					if (err == record->record_ret_val) {
						printf("***successfully played the record\n***");
					} else{
						printf("***Deviation is return value****\n");
						if (flags & S) {
							printf("********aborting replay******\n");
							goto out;
						}
					}
				}
				free(record->record_path_name);
				record->record_path_name = NULL;
				free(record->dest_path_name);
				record->dest_path_name = NULL;
				break;
			case OP_READLN:
				memcpy(&record->record_path_len, buff + index, 4);
				index = index + 4;
				record->record_path_name = (char *) malloc(
				    record->record_path_len + 1);
				memcpy(record->record_path_name, buff + index,
				       record->record_path_len);
				index = index + record->record_path_len;
				record->record_path_name[record->record_path_len] = '\0';
				memcpy(&record->count, buff + index, 4);
				index += 4;
				memcpy(&record->record_ret_val, buff + index, 4);
				index += 4;
				printf(
				    "we will perform readlink on on below link : %s, with buf size %d , expected ret val : %d\n",
				    record->record_path_name, record->count, record->record_ret_val);
				if (!(flags & N)) {
					read_buff = (char *) malloc(record->count);
					err = readlink(record->record_path_name+1, read_buff, record->count);
					if (err == record->record_ret_val) {
						printf("***successfully played the record\n***");
					} else{
						printf("***Deviation is return value****\n");
						if (flags & S) {
							printf("*****aborting replay****\n");
							goto out;
						}
					}
					free(read_buff);
					read_buff = NULL;
				}
				free(record->record_path_name);
				record->record_path_name = NULL;
				break;
			default:
				printf("[Treplay] unknown operation type\n");
		}
	}
out:
	distroyList();
	if (record) {
		if (record->record_path_name) {
			free(record->record_path_name);
			record->record_path_name = NULL;
		}
		if (record->dest_path_name) {
			free(record->dest_path_name);
			record->dest_path_name = NULL;
		}
		if (record->buffer) {
			free(record->buffer);
			record->buffer = NULL;
		}
		free(record);
		record = NULL;
	}
	if (recordId) {
		free(recordId);
		recordId = NULL;
	}
	if (recordSize) {
		free(recordSize);
		recordSize = NULL;
	}
	if (fd)
		close(fd);
	if (buff) {
		free(buff);
		buff = NULL;
	}
	exit(err);
}

