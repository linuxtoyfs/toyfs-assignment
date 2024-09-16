# SPDX-License-Identifier: GPL-2.0-only

HOST_KVER=`uname -r`
KDIR=/lib/modules/$(HOST_KVER)/build/
obj-m := toyfs.o
toyfs-objs := toyfs_super.o toyfs_dir.o toyfs_file.o toyfs_inode.o toyfs_aops.o toyfs_iops.o toyfs_balloc.o
ccflags-y := -DDEBUG

all:
	make -C $(KDIR) M=$(PWD) modules
clean:
	make -C $(KDIR) M=$(PWD) clean
help:
	make -C $(KDIR) M=$(PWD) help
