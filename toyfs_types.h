/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef __TOYFS_TYPES_H
#define __TOYFS_TYPES_H

#include <linux/fs.h>

#define EFSCORRUPTED	EUCLEAN

/* We only support 2048 block size */
#define TFS_BSIZE	2048

/* Yup. Filesystem maximum size is 1MiB */
#define TFS_MAX_BLKS	512

/*
 * Number of blocks allocated for a single inode
 * Yes, to make things simple, we hardcode it.
 *
 * Each block address is encoded within a 32-bit integer,
 * the whole tfs_dinode structure is 36 bytes + 4 * num of blocks
 * We use a amaximum of 7 blocks here so the whole inode structure
 * is rounded to a power of 2 (64 bytes).
 *
 * So, in the single inode block, we can have 32 inodes
 * Yes, the whole filesystem will have at most 32 inodes
 */
#define TFS_INODE_COUNT		32

/* Every inode can use up to 7 data blocks*/
#define	TFS_MAX_INO_BLKS	7


/*
 * directory entry name is hardcoded within the dir entry, set a maximum value
 * Set it to 28 bytes, so the whole dir entry struct is rounded to a power of 2 (32bytes)
 */
#define TFS_MAX_NLEN 28

#define TFS_MAGIC 0x5F544F59 /* _TOY */

/*
 * Invalid reference
 *
 * Can be used to identify free dentries, free inodes, etc
 * We need something like this, because we support inode 0.
 *
 * This is safe to use, because we don't access more than 32
 * inodes or 512 blocks, so we should never have a valid reference
 * pointing to this same value.
 */
#define TFS_INVALID 0xdeadbeef

/* Inode alloc flags */
#define TFS_INODE_INUSE	1
#define TFS_INODE_FREE	0

/* s_flags fields */
#define TFS_SB_CLEAN	0
#define TFS_SB_DIRTY	1

/* Disk location of metadata blocks */
#define TFS_SB_BLOCK		(0)
#define TFS_INODE_BLOCK		(1)
#define TFS_BITMAP_BLOCK	(2)
#define TFS_FIRST_DATA_BLOCK	(3)
#define TFS_LAST_DATA_BLOCK	(TFS_MAX_BLKS -1)

/* On disk superblock */
struct tfs_dsb {
	__u32	s_magic;
	__u32	s_flags;

	/* free inode and block fields require locking */
	__u32	s_ifree;
	__u32	s_bfree;
	__u32	s_inodes[32];
};

/* In memory superblock (linked to s_fs_info) */
struct tfs_fs_info {
	unsigned int		s_magic;
	unsigned int		s_flags;
	unsigned int		s_bfree;
	unsigned int		s_ifree;
	struct buffer_head	*s_bmap_bh;
	struct buffer_head	*s_inode_bh;
	unsigned int		s_inodes[32];
};

/* On disk inode */
struct tfs_dinode {
	__u32	i_mode;
	__u32	i_nlink;
	__u32	i_atime;
	__u32	i_mtime;
	__u32	i_ctime;
	__u32	i_uid;
	__u32	i_gid;
	__u32	i_size;
	__u32	i_blocks;
	__u32	i_addr[TFS_MAX_INO_BLKS];
};

/* In-core inode */
struct tfs_inode_info {
	struct inode	vfs_inode;
	unsigned int	i_blocks;
	unsigned int	i_addr[TFS_MAX_INO_BLKS];
	char		i_link[TFS_MAX_NLEN];
};

/* On disk directory entry */
struct tfs_dentry {
	__u32	d_ino;
	char	d_name[TFS_MAX_NLEN];
};

#define TFS_ENTRIES_PER_BLOCK ((TFS_BSIZE) / (sizeof(struct tfs_dentry)))

extern int toyfs_fill_super(struct super_block *sb,
			    void *data,
			    int flags);

extern struct dentry* toyfs_mount(struct file_system_type *fs_type,
				  int flags, const char *dev_name,
				  void *data);
extern struct inode* toyfs_read_inode(struct super_block *sb,
				      unsigned int inum);
extern int toyfs_balloc(struct super_block *sb);
extern void toyfs_bfree(struct tfs_fs_info *tfi, int block);
extern int toyfs_ialloc(struct super_block *sb);
extern int toyfs_dir_add_entry(struct inode *parent, const char *name,
			       int inum);
extern int toyfs_dir_del_entry(struct inode *parent, const char *name);

extern struct inode* toyfs_new_inode(struct inode *parent,
				     struct dentry *dentry,
				     umode_t mode,
				     const char *lnk_target);
extern int toyfs_readdir(struct file *fdir, struct dir_context *ctx);

extern int toyfs_get_block(struct inode *inode, sector_t block,
			   struct buffer_head *bh, int create);

extern int toyfs_find_entry(struct inode *dir, const char *name);

#endif /* __TOYFS_TYPES_H */
