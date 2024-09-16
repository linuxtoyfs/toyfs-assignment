// SPDX-License-Identifier: GPL-2.0-only

#define pr_fmt(fmt) "%s:%s: " fmt, KBUILD_MODNAME, __func__

#include <linux/fs.h>
#include <linux/buffer_head.h>
#include <linux/writeback.h>
#include "toyfs_types.h"
#include "toyfs_file.h"
#include "toyfs_iops.h"
#include "toyfs_aops.h"

/**
 * toyfs_ialloc() - Alloc a new inode on-disk
 * @sb: The filesystem in question
 *
 * Allocate a free inode from the filesystem
 *
 * Return: The inode number of the new allocated inode or
 *	   -ENOSPC if we ran out of inodes
 */
int toyfs_ialloc(struct super_block *sb)
{
	struct tfs_fs_info	*tfi = sb->s_fs_info;
	int i;

	if (!tfi->s_ifree) {
		pr_debug("We ran out of inodes\n");
		return -ENOSPC;
	}

	tfi->s_ifree--;
	for (i = 0; i < TFS_INODE_COUNT; i++ ) {
		if (tfi->s_inodes[i] == TFS_INODE_FREE) {
			tfi->s_inodes[i] = TFS_INODE_INUSE;
			pr_debug("Allocated inode %d\n", i);
			return i;
		}
	}

	/*
	 * If we reach here, the filesystem is corrupted. The inode free
	 * count shows free inodes, but none has been found in the inode list
	 */
	BUG();
}

/*
 * Read an inode from disk and allocate a new tfs inode and
 * a vfs inode, populating them with content read from disk.
 */
struct inode* toyfs_read_inode(struct super_block *sb, unsigned int inum)
{
	struct inode		*ip;
	struct tfs_fs_info	*tfi;
	struct tfs_dinode	*di_arr, *dip;
	struct tfs_inode_info	*tino;
	struct buffer_head	*i_bh;
	int i = 0;


	pr_debug("Reading inode: %d\n", inum);
	if (inum >= TFS_INODE_COUNT)
		return ERR_PTR(-EINVAL);

	ip = iget_locked(sb, inum);

	if (!(ip->i_state & I_NEW))
		return ip;

	/*
	 * We can't call inode_init_once() here because iget_locked
	 * assumes inode->i_sb_list is already initialized, look at
	 * iget_locked() calling inode_sb_list_add().
	 * */
	tino = container_of(ip, struct tfs_inode_info, vfs_inode);
	tfi = (struct tfs_fs_info *)sb->s_fs_info;

	i_bh = tfi->s_inode_bh;

	di_arr = (struct tfs_dinode *)i_bh->b_data;
	dip = &di_arr[inum];

	/* Initialize vfs inode */
	ip->i_private = tino;
	ip->i_mode = dip->i_mode;

	i_uid_write(ip, (uid_t)dip->i_uid);
	i_gid_write(ip, (uid_t)dip->i_gid);
	set_nlink(ip, dip->i_nlink);

	/* this should use i_size_write() */
	ip->i_size = dip->i_size;
	ip->i_blocks = dip->i_blocks;

	inode_set_atime(ip, dip->i_atime, 0);
	inode_set_mtime(ip, dip->i_mtime, 0);
	inode_set_ctime(ip, dip->i_ctime, 0);

	tino->i_blocks = dip->i_blocks;
	for (i = 0; i < TFS_MAX_INO_BLKS; i++)
		tino->i_addr[i] = dip->i_addr[i];

	if (S_ISDIR(dip->i_mode)) {
		ip->i_op = &toyfs_dir_inode_operations;
		ip->i_fop = &toyfs_dir_file_operations;
		ip->i_mapping->a_ops = &toyfs_aops;
	} else if (S_ISREG(dip->i_mode)) {
		/*
		 * If we forget to init a_ops, the kernel blow up when reading a file, should this
		 * happen?
		 */
		ip->i_op = &toyfs_inode_operations;
		ip->i_fop = &toyfs_file_operations;
		ip->i_mapping->a_ops = &toyfs_aops;
	} else if (S_ISLNK(dip->i_mode)) {
		struct buffer_head *lbh;
		char *lnk;

		/* Always, we only have the first block allocated for symlinks*/
		lbh = sb_bread(sb, tino->i_addr[0]);
		lnk = (char *)lbh->b_data;
		pr_debug("Reading link inode pointing to: %s\n", lnk);
		ip->i_op = &simple_symlink_inode_operations;
		memcpy(tino->i_link, lnk, TFS_MAX_NLEN);
		ip->i_link = tino->i_link;
		brelse(lbh);
	} else {
		pr_debug("Inode with invalid mode - FS corrupted\n");
		BUG();
	}

	unlock_new_inode(ip);
	return ip;
}


struct inode* toyfs_new_inode(struct inode *parent,
			      struct dentry* dentry,
			      umode_t mode,
			      const char *lnk_target)
{
	struct inode *ip;
	struct timespec64 tv;
	struct tfs_inode_info *tino;
	struct super_block *sb = parent->i_sb;
	struct buffer_head *bh;
	struct tfs_dentry *d_array;
	int inum, blk;
	int error = 0;
	int i = 0;

	ip = new_inode(sb);
	if (!ip)
		return ERR_PTR(-ENOMEM);

	inum = toyfs_ialloc(sb);
	if (inum < 0) {
		iput(ip);
		return ERR_PTR(-ENOSPC);
	}

	tino = container_of(ip, struct tfs_inode_info, vfs_inode);

	pr_debug("Initial link count parent: %d\n",
		parent->i_nlink);
	inode_init_owner(&nop_mnt_idmap, ip, parent, mode);

	tv = inode_set_ctime_current(ip);
	inode_set_mtime_to_ts(ip, tv);
	inode_set_atime_to_ts(ip, tv);
	ip->i_ino = inum;
	ip->i_private = tino;

	for (i = 0; i < TFS_MAX_INO_BLKS; i++)
		tino->i_addr[i] = TFS_INVALID;

	insert_inode_hash(ip);

	if (S_ISREG(mode)) {
		tino->i_blocks = 0;
		ip->i_blocks = 0;
		ip->i_size = 0;
		ip->i_op = &toyfs_inode_operations;
		ip->i_fop = &toyfs_file_operations;
		ip->i_mapping->a_ops = &toyfs_aops;
	} else if (S_ISDIR(mode)) {
		blk = toyfs_balloc(sb);
		if (blk < 0)
			return ERR_PTR(blk);

		bh = sb_bread(sb, blk);
		d_array = (struct tfs_dentry*)bh->b_data;
		for (i = 0; i < (TFS_BSIZE / sizeof(struct tfs_dentry)); i++)
			d_array[i].d_ino = TFS_INVALID;

		strcpy(d_array[0].d_name, ".");
		d_array[0].d_ino = ip->i_ino;
		strcpy(d_array[1].d_name, "..");
		d_array[1].d_ino = parent->i_ino;

		tino->i_blocks = 1;
		tino->i_addr[0] = blk;

		ip->i_blocks = 1;
		ip->i_size = 2 * sizeof(struct tfs_dentry); /* . and .. */
		ip->i_op = &toyfs_dir_inode_operations;
		ip->i_fop = &toyfs_dir_file_operations;
		ip->i_mapping->a_ops = &toyfs_aops;

		inode_inc_link_count(ip);
		mark_buffer_dirty(bh);
		brelse(bh);
	} else if (S_ISLNK(mode)) {
		char *dst;
		int len = strnlen(lnk_target, TFS_MAX_NLEN);

		if (len >= TFS_MAX_NLEN)
			return ERR_PTR(-ENAMETOOLONG);

		blk = toyfs_balloc(sb);
		if (blk < 0)
			return ERR_PTR(blk);

		bh = sb_bread(sb, blk);
		dst = (char *)bh->b_data;
		strncpy(dst, lnk_target, len + 1);

		pr_debug("Link created to: %s\n", dst);
		tino->i_blocks = 1;
		tino->i_addr[0] = blk;

		ip->i_link = tino->i_link;
		ip->i_blocks = 1;
		ip->i_size = len;
		ip->i_op = &simple_symlink_inode_operations;
		memcpy(ip->i_link, lnk_target, len + 1);
		mark_buffer_dirty(bh);
		brelse(bh);
	} else {
		BUG();
	}

	mark_inode_dirty(ip);

	error = toyfs_dir_add_entry(parent, dentry->d_name.name, inum);
	if (error)
		/*need to cleanup */
		return ERR_PTR(error);

	d_instantiate(dentry, ip);

	pr_debug("Link counts - parent: %d inode: %d\n",
		parent->i_nlink, ip->i_nlink);
	return ip;
}

