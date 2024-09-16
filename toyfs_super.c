// SPDX-License-Identifier: GPL-2.0-only

#define pr_fmt(fmt) "%s:%s: " fmt, KBUILD_MODNAME, __func__

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/statfs.h>
#include <linux/mm.h>
#include <linux/buffer_head.h>
#include <linux/writeback.h>
#include "toyfs_types.h"
#include "toyfs_iops.h"
#include "toyfs_aops.h"

/**
 * toyfs_alloc_inode() - Allocate a new in-core inode object
 * @sb: The filesystem in question
 *
 * Allocate a new toyfs in-core inode object, giving we already have
 * an VFS inode embedded, all we need to do is alloc the tfs_inode_info
 *
 * Return: The vfs inode pointer or negative ERR_PTR
 */
static struct inode *
toyfs_alloc_inode(
	struct super_block	*sb)
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
static int
toyfs_write_inode(
	struct inode			*inode,
	struct writeback_control	*wbc)
{
	struct tfs_dinode		*i_array;
	struct tfs_fs_info		*tfi;
	struct buffer_head		*bh;
	struct tfs_inode_info		*tino;
	int				ino;
	int				i;

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
 * toyfs_free_inode() - Free memory allocated for an inode object
 * @inode: The vfs inode embedded within the toyfs inode.
 *
 * There is nothing much to do here other than kfree() the toyfs inode
 */
static void
toyfs_free_inode(
	struct inode		*inode)
{
	struct tfs_inode_info	*tino;

	tino = container_of(inode, struct tfs_inode_info, vfs_inode);
	kfree(tino);
	pr_debug("Freeing inode %lu\n", inode->i_ino);
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
static void
toyfs_evict_inode(
	struct inode		*inode)
{
	struct tfs_fs_info	*tfi;
	struct tfs_inode_info	*tino;
	struct buffer_head	*bh;
	char			*bmap;
	int			i;

	tino = container_of(inode, struct tfs_inode_info, vfs_inode);
	tfi = inode->i_sb->s_fs_info;

	pr_debug("Evicting inode %px - link count: %d\n",
		 inode, inode->i_nlink);
	truncate_inode_pages_final(&inode->i_data);
	invalidate_inode_buffers(inode);
	clear_inode(inode);

	if (inode->i_nlink)
		return;

	bh = tfi->s_bmap_bh;
	bmap = (char *)bh->b_data;

	tfi->s_inodes[inode->i_ino] = TFS_INODE_FREE;
	tfi->s_ifree++;
	tfi->s_bfree += tino->i_blocks;

	for (i = 0; i < tino->i_blocks; i++)
		toyfs_bfree(tfi, tino->i_addr[i]);

	mark_buffer_dirty(bh);
}

static int
toyfs_statfs(
	struct dentry		*dentry,
	struct kstatfs		*kst)
{
	struct super_block	*sb   = dentry->d_sb;
	struct tfs_fs_info	*tfi  = sb->s_fs_info;
	int			error = 0;

	/*
	 * f_fsid is a 2 integer array, we can use device's major
	 * and minor to generate a fs id.
	 */
	u64 id = huge_encode_dev(sb->s_dev);

	kst->f_bsize = TFS_BSIZE;
	kst->f_blocks = TFS_MAX_BLKS;
	kst->f_bfree = tfi->s_bfree;
	kst->f_bavail = tfi->s_bfree;
	kst->f_files = TFS_INODE_COUNT;
	kst->f_ffree = tfi->s_ifree;
	kst->f_fsid = u64_to_fsid(id);
	kst->f_namelen = TFS_MAX_NLEN;
	kst->f_frsize = TFS_BSIZE;

	return error;
}

static void
toyfs_put_super(
	struct super_block	*sb)
{
	struct tfs_fs_info	*tfi = sb->s_fs_info;
	struct buffer_head	*sbh;
	struct tfs_dsb		*dsb;
	int			i;

	sbh = sb_bread(sb, TFS_SB_BLOCK);
	dsb = (struct tfs_dsb *)sbh->b_data;

	dsb->s_ifree = tfi->s_ifree;
	dsb->s_bfree = tfi->s_bfree;

	for (i = 0; i < TFS_INODE_COUNT; i++)
		dsb->s_inodes[i] = tfi->s_inodes[i];

	mark_buffer_dirty(tfi->s_bmap_bh);
	mark_buffer_dirty(tfi->s_inode_bh);
	mark_buffer_dirty(sbh);
	brelse(tfi->s_bmap_bh);
	brelse(tfi->s_inode_bh);
	brelse(sbh);

	kfree(tfi);
}

struct super_operations toyfs_sops = {
	.alloc_inode	= toyfs_alloc_inode,
	.write_inode	= toyfs_write_inode,
	.free_inode	= toyfs_free_inode,
	.evict_inode	= toyfs_evict_inode,
	.statfs		= toyfs_statfs,
	.put_super	= toyfs_put_super,
};

int
toyfs_fill_super(
	struct super_block	*sb,
	void			*data,
	int			flags)
{
	struct tfs_dsb		*tfs_dsb;
	struct tfs_fs_info	*tfi;
	struct buffer_head	*sbh;
	struct buffer_head	*bmap_bh;
	struct buffer_head	*inode_bh;
	struct inode		*root_ino;
	int i = 0;
	int error = 0;

	tfi = kzalloc(sizeof(struct tfs_fs_info), GFP_KERNEL);
	if (!tfi)
		return -ENOMEM;

	/* Basic super_block initialization */
	sb_set_blocksize(sb, TFS_BSIZE);
	sb->s_time_min = 0;
	sb->s_time_max = U32_MAX;

	/* Yes, we use buffer_heads here... for now */
	sbh = sb_bread(sb, 0);
	if (!sbh)
		goto sb_err_out;

	tfs_dsb = (struct tfs_dsb*)sbh->b_data;

	bmap_bh = sb_bread(sb, TFS_BITMAP_BLOCK);
	if (!bmap_bh) {
		pr_debug("Couldn't read bitmap block\n");
		error = -ENOMEM;
		goto sbh_err_out;
	}
	pr_debug("bitmap loaded\n");

	inode_bh = sb_bread(sb, TFS_INODE_BLOCK);
	if (!inode_bh) {
		pr_debug("Couldn't read inode block\n");
		error = -ENOMEM;
		goto bbh_err_out;
	}

	if (tfs_dsb->s_magic != TFS_MAGIC) {
		pr_debug("Invalid Magic number\n");
		error = -EFSCORRUPTED;
		goto ibh_err_out;
	}
	if (tfs_dsb->s_flags == TFS_SB_DIRTY) {
		pr_debug("Filesystem is corrupted, run fsck before mounting");
		error = -EFSCORRUPTED;
		goto ibh_err_out;
	}
	pr_debug("FS is clean\n");

	/* All in-core structures are allocated, finish initializing SB and fs_info */
	sb->s_fs_info = tfi;
	sb->s_magic = tfi->s_magic;
	sb->s_op = &toyfs_sops;

	tfi->s_magic = tfs_dsb->s_magic;
	tfi->s_ifree = tfs_dsb->s_ifree;
	tfi->s_bfree = tfs_dsb->s_bfree;
	tfi->s_bmap_bh = bmap_bh;
	tfi->s_inode_bh = inode_bh;


	pr_debug("Superblock initialization...\n");
	pr_debug("\tmagic: 0x%x - free ino: %u, free blocks: %u\n",
		tfi->s_magic, tfi->s_ifree, tfi->s_bfree);

	for (i = 0; i < TFS_INODE_COUNT; i++)
		tfi->s_inodes[i] = tfs_dsb->s_inodes[i];


	/* All set, let's setup the root inode */

	root_ino = toyfs_read_inode(sb, 0);

	/* XXX: missing error handling */
	sb->s_root = d_make_root(root_ino);

	/*
	 * BUG: Forgot to fix this.
	 * the buffers are not released but are not
	 * stored anywhere either
	 */
	return error;
	/*
	 * Fall through
	 *
	 * Mount is still incomplete, fall through here, so nobody try to mount this, or it will
	 * explode in legacy_get_tree() because we have no real root dentry setup so far.
	 */
	error = -EFSCORRUPTED;
bbh_err_out:
	brelse(bmap_bh);
ibh_err_out:
	brelse(inode_bh);
sbh_err_out:
	brelse(sbh);
sb_err_out:
	kfree(tfi);
	sb->s_fs_info = NULL;

	return error;
}

struct dentry *
toyfs_mount(
	struct file_system_type	*fs_type,
	int			flags,
	const char		*dev_name,
	void			*data)
{
	return mount_bdev(fs_type, flags, dev_name, data, toyfs_fill_super);
}

static struct file_system_type toyfs_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "toyfs",
	.fs_flags	= FS_REQUIRES_DEV,
	.mount		= toyfs_mount,
	.kill_sb	= kill_block_super,

};

static int __init
toyfs_mod_init(void)
{
	int error = 0;

	error = register_filesystem(&toyfs_fs_type);
	pr_debug("ToyFS module loaded\n");
	return error;
}

static void __exit
toyfs_mod_exit(void)
{
	unregister_filesystem(&toyfs_fs_type);
	pr_debug("ToyFS module unloaded\n");
}

module_init(toyfs_mod_init);
module_exit(toyfs_mod_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Carlos Maiolino");
MODULE_DESCRIPTION("ToyFS filesystem - a simple filesystem for teaching purposes");
MODULE_ALIAS("toyfs");
