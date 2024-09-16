// SPDX-License-Identifier: GPL-2.0-only

#define pr_fmt(fmt) "%s:%s: " fmt, KBUILD_MODNAME, __func__

#include <linux/fs.h>
#include <linux/buffer_head.h>
#include "toyfs_types.h"
#include "toyfs_iops.h"

/**
 * toyfs_find_entry() - Search for an entry within a directory inode
 * @dir: The directory inode to be searched
 * @name: The name we are looking for
 *
 * Walk through all the directory data blocks associated with the
 * inode, until either a directory entry matching @name is found or
 * the all the blocks are searched.
 *
 * It is important to notice we can't simply bail on the first free
 * directory entry we hit while searching. Directory entries can get
 * fragmented, and we may have free and used entries mixed up within
 * the directory blocks.
 *
 * Return: The inode number of the found entry or a negative value
 *	   on error.
 */
int toyfs_find_entry(struct inode *dir, const char *name)
{
	struct buffer_head	*bh;
	struct tfs_inode_info	*tino;
	struct tfs_dentry	*dir_array;
	int i = 0, j = 0;

	pr_debug("Searching name: %s\n", name);
	tino = container_of(dir, struct tfs_inode_info, vfs_inode);

	/* FIXME: This should not use i_blocks as a loop delimiter */
	for (i = 0; i < tino->i_blocks; i++) {
		pr_debug("searching data_block: %u\n",
			tino->i_addr[i]);
		bh = sb_bread(dir->i_sb, tino->i_addr[i]);

		if (!bh)
			return -ENOMEM;

		dir_array = (struct tfs_dentry *)bh->b_data;

		/* Up to 32 direntries per block */
		for (j = 0; j < TFS_ENTRIES_PER_BLOCK; j++) {
			if (dir_array[j].d_ino == TFS_INVALID)
				continue;

			if (strcmp(dir_array[j].d_name, name) == 0) {
				brelse(bh);
				return dir_array[j].d_ino; /* dir entry found */
			}
		}
		brelse(bh);
	}

	pr_debug("Entry not found\n");
	return -ENOENT;
}

/**
 * toyfs_dir_add_entry -  Add a new directory entry in a directory
 * @parent: The directory where the entry will be added to
 * @name: The name for the new entry.
 * @inum: The inode number of the new entry.
 *
 * To add a new entry to a directory, we first need to search if there
 * is an already existing entry with the same name, and replace it with
 * the new entry (rename()). And the only way to do that, is to walk
 * through the whole directory, as we don't use any other clever data
 * structure.
 *
 * We could have re-used toyfs_find_entry() to search for an already
 * existing entry, but this would need us to traverse the whole directory
 * tree twice in the worst case, one to search for an already existing entry
 * and a second to search for a free entry.
 *
 * To make things a bit less slow, we do everything in a single directory traversal:
 *	- We look for a free spot in the directory entry and for the name
 *	  on the same loop.
 *	- If we find a free spot, we save its block and position
 *	  in bh_tgt and idx_tgt.
 *	- If later we find an already existing entry, we simply update both.
 *
 * We need to be careful here though, to not leak a buffer_head.
 *	- While we need to walk through every single block associated to
 *	  the inode, we need to save the buffer_head belonging to block
 *	  where we found a free entry.
 *	- And in case we end up later finding an already existing entry,
 *	  we must free the saved buffer_head.
 *
 * If we have more than a single directory block associated to this inode,
 * we need to ensure when traversing to the next block, that we won't free
 * the current one, if we have found an empty slot, otherwise we'd hit a UAF.
 *
 * Also, we must account for the case when, an empty slot and an already
 * existing entry are found within the same block, one after another. And
 * ensure we won't free the same buffer twice.
 *
 * XXX: Although we can't have more than 32 inodes, hardlinks occupy dir entries,
 *	so, we can end up having more than 32 entries to the same directory, requiring
 *	more than a single directory block.
 * FIXME: I don't think the directories are working well with more than a single block,
 *	  and I've gotta fix it sometime.
 */

int toyfs_dir_add_entry(struct inode *parent, const char *name, int inum)
{
	struct tfs_inode_info	*tino;
	struct tfs_dentry	*d_array;
	struct buffer_head	*bh_cur = NULL;	/* bh cursor to loop through all dir blocks */
	struct buffer_head	*bh_tgt = NULL;	/* bh target block to write the free entry */
	struct timespec64 tv;
	int idx_tgt = TFS_INVALID;		/* free position within the block referenced by bh_tgt */
	int i = 0, j = 0;

	tino = parent->i_private;

	for (i = 0; i < tino->i_blocks; i++) {
		bh_cur = sb_bread(parent->i_sb, tino->i_addr[i]);

		if (!bh_cur) {
			if (bh_tgt)
				brelse(bh_tgt);

			return -ENOMEM;
		}

		d_array = (struct tfs_dentry*)bh_cur->b_data;
		for (j = 0; j < (TFS_BSIZE / sizeof(struct tfs_dentry)); j++) {

			/* Search for the first free entry in the directory */
			if (idx_tgt == TFS_INVALID &&
			    d_array[j].d_ino == TFS_INVALID) {
				idx_tgt = j;
				bh_tgt = bh_cur;
				continue;
			}

			/* We must search for a possible entry with the same name */
			if (strcmp(d_array[j].d_name, name) == 0) {
				/*
				 * We found an equal entry here, but we might have a different
				 * buffer with a free entry already saved.
				 * Free the old target before replacing it.
				 * This should happen only if the equal name has been
				 * found on a different dir block than the free entry.
				 */
				if (bh_tgt != bh_cur)
					brelse(bh_tgt);

				idx_tgt = j;
				bh_tgt = bh_cur;
				goto found;
			}

		}
		/*
		 * We are done searching this block, but we could have saved it.
		 * Release the buffer only if it isn't already saved.
		 */
		if (bh_cur != bh_tgt) {
			brelse(bh_cur);
			bh_cur = NULL;
		}
	}


	/* No free entry or same name has been found */
	if (idx_tgt == TFS_INVALID)
		return -ENOSPC;

found:
	d_array = (struct tfs_dentry*)bh_tgt->b_data;
	d_array[idx_tgt].d_ino = inum;
	strcpy(d_array[idx_tgt].d_name, name);
	parent->i_size += sizeof(struct tfs_dentry);

	tv = inode_set_ctime_current(parent);
	inode_set_atime_to_ts(parent, tv);
	inode_inc_link_count(parent);
	mark_buffer_dirty(bh_tgt);

	brelse(bh_tgt);
	return 0;
}

/**
 * toyfs_dir_del_entry() - Remove a directory entry
 * @parent: The inode directory to search for the entry
 * @name: The directory entry we want to remove.
 *
 * This function is pretty simple. We just need to walk through all the
 * directory blocks within the inode, and zero out the entry belonging
 * to te current name.
 *
 * We don't need to finish the whole search once we found the name, as we
 * can't have two entries with the same name, otherwise we've got a bug.
 */
int toyfs_dir_del_entry(struct inode *parent, const char *name)
{
	struct tfs_inode_info	*tino;
	struct super_block	*sb;
	struct tfs_dentry	*d_array;
	struct buffer_head	*bh;
	struct timespec64	tv;
	int i = 0;
	int j = 0;

	tino = container_of(parent, struct tfs_inode_info, vfs_inode);
	sb = tino->vfs_inode.i_sb;

	for (i = 0; i < tino->i_blocks; i++) {
		bh = sb_bread(sb, tino->i_addr[i]);
		d_array = (struct tfs_dentry *)bh->b_data;

		for (j = 0; j < TFS_BSIZE / sizeof(struct tfs_dentry); j++) {
			if ((d_array[j].d_ino == TFS_INVALID) ||
			    (strcmp(d_array[j].d_name, name))) {
				continue;
			} else {
				d_array[j].d_ino = TFS_INVALID;
				d_array[j].d_name[0] = '\0';
				goto done;
			}
		}

		brelse(bh);
	}

	return -ENOENT;

done:
	tv = inode_set_ctime_current(parent);
	inode_set_atime_to_ts(parent, tv);
	inode_dec_link_count(parent);
	mark_buffer_dirty(bh);
	brelse(bh);
	return 0;
}

