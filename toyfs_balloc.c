// SPDX-License-Identifier: GPL-2.0-only

#define pr_fmt(fmt) "%s:%s: " fmt, KBUILD_MODNAME, __func__

#include <linux/fs.h>
#include <linux/buffer_head.h>
#include "toyfs_types.h"

/**
 * toyfs_balloc() - Alloc a new block from the filesystem data blocks
 * @sb: Superblock of the target FS
 *
 * Search the bitmap block for an available block, splitting the search
 * in groups of 8-bits to make the search simpler and with more iteractions
 * than if we had used larger number of bits.
 *
 * Context: We may have different processes allocating/freeing blocks at the
 *	    same time, perhaps this should be protected somehow.
 *
 * Return: Block number of the allocated block, or
 *	   negative value in case of error
 */
int toyfs_balloc(struct super_block *sb)
{
	struct tfs_fs_info	*tfi = sb->s_fs_info;
	struct buffer_head	*bh;
	char			*bmap;
	unsigned int		group = 0;
	unsigned int		block;
	unsigned int		bit;

	pr_debug("Allocating new block\n");
	if (!tfi->s_bfree)
		return -ENOSPC;

	bh = tfi->s_bmap_bh;
	bmap = (char *)bh->b_data;

	for (group = 0; group < TFS_MAX_BLKS / 8; group++) {
		/* Is current group full? */
		pr_debug("Bitmap of group %u: 0x%x\n", group, bmap[group]);
		if (bmap[group] == 0xFF) {
			pr_debug("No free blocks in group %u\n", group);
			continue;
		}
		break;
	}

	pr_debug("Free block in group %u\n", group);
	bit = find_next_zero_bit((const long unsigned int *)&bmap[group], 8, 0);

	/* We must have a free block here */
	if (bit == 8)
		BUG();

	pr_debug("Free bit: %u\n", bit);
	block = (group * 8) + bit;

	pr_debug("Found free block: %d\n", block);

	set_bit(bit, (long unsigned int *)&bmap[group]);
	tfi->s_bfree--;

	mark_buffer_dirty(bh);
	return block;
}

/**
 * toyfs_bfree() - Mark a data block as free
 * @tfi: Toyfs in-core superblock
 * @block: Block number to be freed
 *
 * Just clear the bit tracking that specific number @block
 */
void toyfs_bfree(struct tfs_fs_info *tfi, int block)
{
	int group = block / 8;
	int bit = block % 8;
	clear_bit(bit,
		  (long unsigned int *)&tfi->s_bmap_bh->b_data[group]);
}

int toyfs_get_block(struct inode *inode, sector_t block,
		    struct buffer_head *bh, int create)
{
	unsigned long		fsblock;
	struct super_block	*sb = inode->i_sb;
	struct tfs_inode_info	*tino = container_of(inode, struct tfs_inode_info, vfs_inode);

	fsblock = tino->i_addr[block];

	if (fsblock != TFS_INVALID) {
		map_bh(bh, sb, fsblock);
		return 0;
	}

	/*
	 * We are reading and have no block on disk, just return the hole and let
	 * kernel deal with it
	 */
	if (!create)
		return 0;

	/* If we reach here, we are writing */
	if (block >= TFS_MAX_INO_BLKS)
		return -EFBIG;

	fsblock = toyfs_balloc(sb);

	/* If we got an error, just return it */
	if (fsblock < 0)
		return fsblock;

	tino->i_addr[block] = fsblock;
	tino->i_blocks++;
	mark_inode_dirty(inode);
	map_bh(bh, sb, fsblock);
	set_buffer_new(bh);

	return 0;
}
