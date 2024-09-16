#!/bin/bash

TEST_DIR="/toyfs_mnt/"
ORIG_IMG="./toyfs.img.pristine"
TEST_IMG="./toyfs.img"
LOGFILE="/tmp/toyfs_tests.log"
SUBDIR="sudir"

LOOP_DEV=""
MOUNT_DEV=""

failed(){
	local RED='\033[1;31m'
	local NC='\033[0m'

	echo -e "[${RED}FAILED${NC}] - $1"
}
passed(){
	local GREEN='\033[1;32m'
	local NC='\033[0m'
	echo -e "[${GREEN}PASSED${NC}] - $1"
}

report_test() {
	if [ $1 -ne 0 ]; then
		failed "$2"
	else
		passed "$2"
	fi
}

cleanup() {
	local loop_dev=`sudo losetup | grep toyfs | awk '{print $1}'`
	sudo umount $TEST_DIR &>> $LOGFILE
	sudo rm -rf $TEST_DIR &>> $LOGFILE

	if [ -n $loop_dev ]; then
		sudo losetup -D &>> $LOGFILE
	fi

	rm $TEST_IMG &>> $LOGFILE

	sudo rmmod toyfs &>> /dev/null
	sudo dmesg -c > /dev/null
}

setup() {
	sudo mkdir $TEST_DIR

	make clean &> /dev/null
	make &>> $LOGFILE

	[ -e toyfs.ko ] || failed "setup"
	passed "build module"

	sudo insmod ./toyfs.ko || failed "insmod"
	cp $ORIG_IMG $TEST_IMG
	passed "insmod"

	sudo losetup -f $TEST_IMG

	LOOP_DEV=`sudo losetup | grep toyfs | awk '{print $1}'`

	if [ -n $LOOP_DEV ]; then
		passed "losetup"
	else
		failed "losetup"
		cleanup
		echo "\tTest failed..."
		exit 1
	fi
}

test_mount() {
	sudo mount -t toyfs $LOOP_DEV $TEST_DIR &>> /dev/null
	local mounted_dev=`sudo cat /proc/mounts|grep toyfs | awk '{print $1}'`

	if [ $mounted_dev ]; then
		passed "mount"
	else
		failed "mount"
		echo "\tFilesystem can't be mounted - test suite finished"
		exit 1
	fi

}

test_umount() {
	cd $TEST_DIR
	sudo umount -l $TEST_DIR
	sleep 5
	local out=`sudo cat /proc/mounts|grep toyfs | awk '{print $1}'`

	if [ -z $out ]; then
		passed "umount"
	else
		failed "umount"
	fi
}

test_stat() {
	stat $TEST_DIR/sunshine.txt &>> $LOGFILE
	report_test $? "stat"
}

test_mkdir() {
	sudo mkdir $TEST_DIR/$SUBDIR &> /dev/null
	report_test $? "mkdir"
}

test_write() {
	local dest_dir=$TEST_DIR/$SUBDIR

	sudo cp /etc/passwd $dest_dir &> /dev/null
	report_test $? "write_1"

	sudo cp /etc/group  $dest_dir
	report_test $? "write_2"
}

test_read() {
	cat $TEST_DIR/sunshine.txt &> /dev/null
	report_test $? "read"
}

test_unlink() {
	sudo rm -f $TEST_DIR/$SUBDIR/passwd &> /dev/null
	report_test $? "unlink"
}

test_rmdir() {
	sudo rm -rf TEST_DIR/$SUBDIR &> /dev/null
	report_test $? "rmdir"
}

test_readdir() {
	ls -l $TEST_DIR &> /dev/null
	report_test $? "readdir"
}

test_rename() {
	sudo mkdir $TEST_DIR/dir1 &> /dev/null
	sudo mkdir $TEST_DIR/dir2 &> /dev/null

	sudo sh -c "echo KOMETA > $TEST_DIR/dir1/file1"
	sudo sh -c "echo SPARTA > $TEST_DIR/dir2/file2"

	local ino=`ls -i $TEST_DIR/dir1/file1 | awk '{print $1}'`

	sudo sync
	sudo sh -c "echo 3 > /proc/sys/vm/drop_caches"

	sudo mv $TEST_DIR/dir1/file1 $TEST_DIR/dir2/file2

	report_test $? "rename_1"

	sudo sync
	local new_ino=`ls -i $TEST_DIR/dir2/file2 | awk '{print $1}'`

	[ $ino = $new_ino ]
	report_test $? "rename_2"

}

test_link() {
	sudo mkdir $TEST_DIR/dir1 &> /dev/null
	sudo mkdir $TEST_DIR/dir2 &> /dev/null
	sudo touch $TEST_DIR/dir1/target
	sudo ln $TEST_DIR/dir1/target $TEST_DIR/dir2/my_link
	report_test $? "link_1"

	local old_ino=`ls -i $TEST_DIR/dir1/target | awk '{print $1}'`
	local new_ino=`ls -i $TEST_DIR/dir2/my_link | awk '{print $1}'`

	[ $old_ino = $new_ino ]

	report_test $? "link_2"

}

test_symlink() {
	sudo mkdir $TEST_DIR/dir1 &> /dev/null
	sudo mkdir $TEST_DIR/dir2 &> /dev/null
	sudo touch $TEST_DIR/dir1/sl_target
	sudo sh -c "echo KOMETA > $TEST_DIR/dir1/sl_target"
	sudo ln -s $TEST_DIR/dir1/sl_target $TEST_DIR/dir2/my_symlink

	[ -L $TEST_DIR/dir2/my_symlink ]
	report_test $? "symlink_1"

	local content=`cat $TEST_DIR/dir2/my_symlink`

	[ $content = "KOMETA" ]
	report_test $? "symlink_2"
}

#sudo sh -c "echo -n 'module toyfs -p' > /sys/kernel/debug/dynamic_debug/control"
cleanup
setup
test_mount

test_stat
test_read
test_readdir
test_write
test_read
test_readdir
test_unlink
test_rmdir
test_rename
test_link
test_symlink
test_umount
cleanup
exit 0

sudo sync

# Test directory deletion
sudo rm -r mnt/dir

# Test inode eviction
sudo sync

sudo mkdir mnt/dir1 mnt/dir2
sudo touch mnt/dir1/foo
sudo ln mnt/dir1/foo mnt/dir1/hardLN
