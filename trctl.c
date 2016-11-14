#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include "trfsglobal.h"

int
main(int argc, char *argv[]) {

	char *cmd = NULL;
	unsigned int bitmap = 0;
	char *mountpoint = NULL;
	char *ptr = NULL;
	int err = 0;
	int fd;
	int c;
	int index = 0;

	if (argc != 3 && argc != 2) {
		printf("[TRCTL] improper number of arguments\n");
		goto out;
	}
	if (argc == 3) {
		cmd = argv[1];
		if (strcmp(cmd, "none") == 0) {
			bitmap = bitmap | 0x00;
		} else if (strcmp(cmd, "all") == 0) {
			bitmap = bitmap | 0xFFFF;
		} else if (cmd[0] == '0' && cmd[1] == 'x') {
			bitmap = bitmap | strtol(cmd, &ptr, 0);
			if (strlen(ptr) != 0) {
				printf("[IOCTL] invalid hex value\n");
				goto out;
			}
		} else {
			printf("[IOCTL] Invalid argument\n");
			goto out;
		}
		c = IOCTL_SET_BITMAP;
		index++;
	} else{
		c = IOCTL_GET_BITMAP;
	}

	mountpoint = argv[index+1];
	fd = open(mountpoint, O_RDONLY);
	if (fd < 0) {
		perror("[IOCTL]");
		goto out;
	}
	err = ioctl(fd, c, &bitmap);
	if (err < 0)
		perror("[IOCTL]");
	if (c == IOCTL_GET_BITMAP)
		printf("bitmap : %x\n", bitmap);
out:
exit(err);
}
