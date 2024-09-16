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

/**
 * toyfs_alloc_inode() - Allocate a new in-core inode object
 * @sb: The filesystem in question
 *
 * Allocate a new toyfs in-core inode object, giving we already have
 * an VFS inode embedded, all we need to do is alloc the tfs_inode_info
 *
 * Return: The vfs inode pointer or negative ERR_PTR
 */
struct inode* toyfs_alloc_inode(struct super_block *sb)
{
	struct tfs_inode_info	*tino;

	tino = kzalloc(sizeof(struct tfs_inode_info), GFP_NOFS);
	if (!tino)
		return ERR_PTR(-ENOMEM);

	/* We must do some basic initialization before returning it */
	inode_init_once(&tino->vfs_inode);
	return &tino->vfs_inode;
}

/**
 * toyfs_free_inode() - Free memory allocated for an inode object
 * @inode: The vfs inode embedded within the toyfs inode.
 *
 * There is nothing much to do here other than kfree() the toyfs inode
 */
void toyfs_free_inode(struct inode *inode)
{
	struct tfs_inode_info *tino;
	tino = container_of(inode, struct tfs_inode_info, vfs_inode);
	kfree(tino);
	pr_debug("Freeing inode %lu\n", inode->i_ino);
}

/**
 * toyfs_write_inode() - Write the toyfs inode back to disk
 * @inode: The vfs inode to be written
 * @wbc: writeback control structure
 *
 * Format the on-disk inode as needed and write it back to disk.
 *
 * We do not support DIO, so, there is not much to be done other than
 * mark the buffer dirty.
 * Only in case wbc tells us this should be done synchronously, then, we
 * need to call sync_dirty_buffer().
 *
 * Return: Zero in case of success or negative value otherwise
 */
int toyfs_write_inode(struct inode *inode, struct writeback_control *wbc)
{
	struct tfs_dinode	*i_array;
	struct tfs_fs_info	*tfi;
	struct buffer_head	*bh;
	struct tfs_inode_info	*tino;
	int			ino;
	int			i;

	tfi  = inode->i_sb->s_fs_info;
	ino = inode->i_ino;
	tino = container_of(inode, struct tfs_inode_info, vfs_inode);
	bh = tfi->s_inode_bh;
	i_array = (struct tfs_dinode *)bh->b_data;
	pr_debug("Writing inode %d to disk\n", ino);

	i_array[ino].i_mode = inode->i_mode;
	i_array[ino].i_nlink = inode->i_nlink;
	i_array[ino].i_uid = i_uid_read(inode);
	i_array[ino].i_gid = i_gid_read(inode);
	i_array[ino].i_size = inode->i_size;

	i_array[ino].i_blocks = tino->i_blocks;

	for (i = 0; i < TFS_MAX_INO_BLKS; i++)
		i_array[ino].i_addr[i] = tino->i_addr[i];

	mark_buffer_dirty(bh);
	if (wbc->sync_mode == WB_SYNC_ALL) {
		sync_dirty_buffer(bh);
		if (buffer_req(bh) && !buffer_uptodate(bh)) {
			return -EIO;
		}
	}

	/*
	 * Inode buffer is pinned in-core for the duration of the mount, we don't call brelse()
	 * here, but instead, it should be called when unmounting the FS
	 */
	return 0;
}

/**
 * toyfs_evict_inode() - Free the on-disk inode
 * @inode: inode to be freed
 *
 * Inodes can't be freed until the last reference to it is gone, so,
 * toyfs_evict_inode() is called once the last user to have this inode
 * opened, actually calls close()
 *
 * We can only proceed if there are no more links to the inode,
 * decreasing link count is not this function's job.
 *
 */
void toyfs_evict_inode(struct inode *inode)
{
#if 0 /* ASSIGNMENT */
	struct tfs_fs_info	*tfi;
	struct tfs_inode_info	*tino;
	struct buffer_head	*bh;
	char			*bmap;
	int			i;

	/*
	 * We need to truncate ALL the pages associated
	 * with this inode before we get rid of the inode
	 */
	truncate_inode_pages_final(&inode->i_data);
	invalidate_inode_buffers(inode);
	clear_inode(inode);

	/* We need to check if the current inode can actually be freed */

	/*
	 * This function is 'simple', we must:
	 *	- set the inode as free in the inode table
	 *	- increase the inode free count
	 *	- increase the block free count
	 *	- Free all blocks this inode might still have
	 *	  associated.
	 */

#endif /* ASSIGNMENT */
}

/**
 * toyfs_read_inode() - Read an inode from disk
 *
 * Return: The vfs inode pointer associated with
 *	   the inode read from disk
 */
struct inode* toyfs_read_inode(struct super_block *sb, unsigned int inum)
{
	return ERR_PTR(-EOPNOTSUPP);

#if 0 /* ASSIGNMENT */
	struct inode		*ip;
	struct tfs_fs_info	*tfi;
	struct tfs_dinode	*di_arr, *dip;
	struct tfs_inode_info	*tino;
	struct buffer_head	*i_bh;
	int i = 0;


	/*
	 * iget_locked() allocate a new -empty- in-core
	 * inode for us. This includes both the toyfs
	 * inode and the vfs inode counterpart.
	 */
	ip = iget_locked(sb, inum);

	/* Some inode fields should be initialized for every file type */

	/* Inodes should be initialized differently, depending on the file type */
	if (S_ISDIR(dip->i_mode)) {
		/* Write something here */
	} else if (S_ISREG(dip->i_mode)) {
		/* Write something here */
	} else if (S_ISLNK(dip->i_mode)) {
		/*
		 * A symbolik link has the target location stored in
		 * the inode's first data block.
		 *
		 * We should read the content from there and copy it
		 * into tino->i_link.
		 */
	} else {
		pr_debug("Inode with invalid mode - FS corrupted\n");
		BUG();
	}

	unlock_new_inode(ip);
	return ip;

#endif /* ASSIGNMENT */
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

