CONFIG_MODULE_SIG=n
TRFS_VERSION="0.1"

EXTRA_CFLAGS += -DTRFS_VERSION=\"$(TRFS_VERSION)\"

obj-m += trfs.o

trfs-objs := dentry.o file.o inode.o main.o super.o lookup.o mmap.o record.o

all: kernel user ioctl

user : treplay.c
	gcc -Wall -Werror -I/lib/modules/$(shell uname -r)/build/arch/x86/include treplay.c -o treplay

test : treplaytest.c
	gcc -Wall -Werror -I/lib/modules/$(shell uname -r)/build/arch/x86/include treplaytest.c -o treplaytest

ioctl : trctl.c
	gcc -Wall -Werror -I/lib/modules/$(shell uname -r)/build/arch/x86/include trctl.c -o trctl

multiioctl : multitrctl.c
	gcc -Wall -Werror -I/lib/modules/$(shell uname -r)/build/arch/x86/include multitrctl.c -o multitrctl

kernel:
	make -Wall -Werror -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

default:
	make -Wall -Werror -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -Wall -Werror -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
