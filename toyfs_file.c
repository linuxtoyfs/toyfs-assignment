// SPDX-License-Identifier: GPL-2.0-only

#define pr_fmt(fmt) "%s:%s: " fmt, KBUILD_MODNAME, __func__

#include <linux/fs.h>
#include <linux/buffer_head.h>
#include "toyfs_types.h"
#include "toyfs_iops.h"
#include "toyfs_aops.h"

int toyfs_readdir(struct file *fdir, struct dir_context *ctx)
{
#if 0 /* ASSIGNMENT */
	struct inode		*ip;
	struct tfs_inode_info	*tino;
	struct buffer_head	*bh;
	struct tfs_dentry	*d_array;
	int			start;	/* dentry we should start reading */
	int			cur;	/* dentry we are currently at */
	int			idx;	/* dentry idx within the current block */
	int			block = 0;
	int			size;
	int			ret;

	return -EOPNOTSUPP;

	ip = file_inode(fdir);
	tino = container_of(ip, struct tfs_inode_info, vfs_inode);

	/* Our starting point depends on current cursor position */
	start = ctx->pos / sizeof(struct tfs_dentry);

	while (((block + 1) * TFS_BSIZE) < ctx->pos)
		block++;

	if (block >= TFS_MAX_INO_BLKS) {
		pr_warn("Attempt to read beyond FS limitations: ino: %lu block: %d",
			ip->i_ino, block);
		return 0;
	}

	cur = 0;
	while (block < TFS_MAX_INO_BLKS) {

		/* Some code goes here... */

		for (idx = 0; idx < TFS_ENTRIES_PER_BLOCK; idx++) {

			/*
			 * We must call dir_emit() for each valid directory entry within the
			 * directory we are reading.
			 *
			 * The function returns 0 if it could add the directory entry to its
			 * internal buffer.
			 *
			 * The buffer is limited in size, which means toyfs_readdir() can be
			 * called in a loop while reading a directory.
			 *
			 * If dir_emit() returns a non-zero value, we must return. It will
			 * most likely empty its buffer and call toyfs_readdir() again.
			 *
			 * Look back at the top of the function. We may be called with a non-zero
			 * ctx->pos value. Which means we must start reading the directory data from
			 * where the ctx->pos is.
			 *
			 * After a successful call to dir_emit(), we must also update the ctx->pos
			 * value.
			 *
			 * Rememver this is in the 'file struct' context, ctx->pos is in
			 * byte granularity, so for each directory we successfully read and emit
			 * via dir_emit(), we must update ctx->pos with the proper amount of bytes.
			 */

			ret = dir_emit(ctx, d_array[idx].d_name, size,
				       d_array[idx].d_ino, DT_UNKNOWN);

			/* We filled the whole dir buffer */
			if (!ret) {
				brelse(bh);
				return 0;
			}

			/* Some code here perhaps... */
		}

		/* Some code here perhaps... */
	}
	/* Some code here perhaps... */

#endif /* ASSIGNMENT */
	return 0;
}

struct file_operations toyfs_file_operations = {
	.fsync		= generic_file_fsync,
	.llseek		= generic_file_llseek,
	.read_iter	= generic_file_read_iter,
	.write_iter	= generic_file_write_iter,
	.mmap		= generic_file_mmap,
};

struct file_operations toyfs_dir_file_operations = {
	.iterate_shared = toyfs_readdir,
};

