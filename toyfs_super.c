// SPDX-License-Identifier: GPL-2.0-only

#define pr_fmt(fmt) "%s:%s: " fmt, KBUILD_MODNAME, __func__

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/statfs.h>
#include <linux/mm.h>
#include <linux/buffer_head.h>
#include "toyfs_types.h"
#include "toyfs_iops.h"
#include "toyfs_aops.h"

int toyfs_statfs(struct dentry *dentry, struct kstatfs *kst)
{
	struct super_block *sb		= dentry->d_sb;
	struct tfs_fs_info *tfi	= sb->s_fs_info;
	int error = 0;

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

void toyfs_put_super(struct super_block *sb)
{
	struct tfs_fs_info	*tfi = sb->s_fs_info;
	struct buffer_head	*sbh;
	struct tfs_dsb		*dsb;
	int i;

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

int toyfs_fill_super(
	struct super_block *sb,
	void *data,
	int flags)
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

struct dentry * toyfs_mount(
	struct file_system_type *fs_type,
	int flags,
	const char *dev_name,
	void *data)
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

static int __init toyfs_mod_init(void)
{
	int error = 0;

	error = register_filesystem(&toyfs_fs_type);
	pr_debug("ToyFS module loaded\n");
	return error;
}

static void __exit toyfs_mod_exit(void)
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
