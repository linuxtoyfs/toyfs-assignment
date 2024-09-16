// SPDX-License-Identifier: GPL-2.0-only

#define pr_fmt(fmt) "%s:%s: " fmt, KBUILD_MODNAME, __func__

#include <linux/fs.h>
#include <linux/mm.h>
#include "toyfs_types.h"
#include "toyfs_file.h"
#include "toyfs_aops.h"

/*
 * Lookup for a directory entry within the FS and
 * instantiate a new dcache entry pointing to it.
 */
static struct dentry *
toyfs_lookup(
	struct inode	*parent,
	struct dentry	*dentry,
	unsigned int	flags)
{
	const char	*name = dentry->d_name.name;
	struct inode	*ip = NULL;
	int		inum = -1; /* Inode 0 is the rootdir */

	pr_debug("Attempting to lookup name: %s\n", name);
	if (strlen(name) > TFS_MAX_NLEN)
		return ERR_PTR(-ENAMETOOLONG);

	inum = toyfs_find_entry(parent, name);

	/*
	 * Inode 0 is a valid inode value, usually the rootdir inode
	 *
	 * -ENOENT is a valid error here. Just means we will instantiate
	 *  a negative dentry. But toyfs_find_entry can also fail with -ENOMEM,
	 *  so we need to bail if the error is anything other than -ENOENT.
	 */
	if (inum >= 0)
		ip = toyfs_read_inode(parent->i_sb, inum);
	else if (inum != -ENOENT)
		return ERR_PTR(inum);


	if (!ip) {
		pr_debug("Inode not found. Negative dentry instantiated: 0x%px",
			 dentry);
	} else {
		pr_debug("Inode: %d - Dentry 0x%px to the cache\n",
			 inum, dentry);
	}

	return d_splice_alias(ip, dentry);
}

/*
 * toyfs_create
 * - Create a new file within the filesystem
 * - Allocate a new inode
 * - Add a new directory entry
 */
static int
toyfs_create(
	struct mnt_idmap	*idmap,
	struct inode		*parent,
	struct dentry		*dentry,
	umode_t			mode,
	bool			excl)
{
	struct inode		*inode;

	pr_debug("Creating regular file inode\n");

	inode = toyfs_new_inode(parent, dentry, S_IFREG | mode, NULL);
	if (IS_ERR(inode))
		return PTR_ERR(inode);
	else
		return 0;
}

/*
 * toyfs_mkdir()
 *	- Create a new directory into the filesystem
 */
static struct dentry *
toyfs_mkdir(
	struct mnt_idmap	*idmap,
	struct inode		*parent,
	struct dentry		*dentry,
	umode_t			mode)
{
	struct inode		*inode;
	int error = 0;

	pr_debug("Creating directory inode: \"%s\"\n",
		dentry->d_name.name);

	inode = toyfs_new_inode(parent, dentry, S_IFDIR | mode, NULL);

	if (IS_ERR(inode))
	    error = PTR_ERR(inode);

	return ERR_PTR(error);
}

static int
toyfs_link(
	struct dentry	*old_dentry,
	struct inode	*parent,
	struct dentry	*new_dentry)
{
	int		error;
	struct inode	*inode;

	inode = d_inode(old_dentry);

	pr_debug("Creating hardlink for inode: %lu\n", inode->i_ino);

	error = toyfs_dir_add_entry(parent,
				    new_dentry->d_name.name,
				    inode->i_ino);
	if (error)
		return error;

	inode_set_ctime_current(inode);
	inode_inc_link_count(inode);
	ihold(inode);
	d_instantiate(new_dentry, inode);
	return 0;
}

static int
toyfs_symlink(
	struct mnt_idmap	*idmap,
	struct inode		*parent,
	struct dentry		*dentry,
	const char		*target)
{
	struct inode		*inode;

	pr_debug("Creating symlink\n");

	inode = toyfs_new_inode(parent, dentry, S_IFLNK | S_IRWXUGO, target);

	return 0;
}

static int
toyfs_unlink(
	struct inode	*parent,
	struct dentry	*dentry)
{
	struct inode	*inode = d_inode(dentry);
	const char	*name;
	int		ret;

	name = dentry->d_name.name;
	ret = toyfs_find_entry(parent, name);

	pr_debug("Unlinking inode %px\n", inode);
	pr_debug("\tInitial link count - parent: %d - ino: %d\n",
		parent->i_nlink, inode->i_nlink);

	if (ret < 0)
		return ret;

	ret = toyfs_dir_del_entry(parent, name);

	if (ret)
		return ret;

	inode_dec_link_count(inode);

	pr_debug("\tfinal link count - parent: %d - ino: %d\n",
		parent->i_nlink, inode->i_nlink);
	return 0;

}

static int
toyfs_rmdir(
	struct inode	*parent,
	struct dentry	*dentry)
{
	struct inode	*inode = d_inode(dentry);
	int		error;

	if (inode->i_nlink > 2)
		return -ENOTEMPTY;

	error = toyfs_unlink(parent, dentry);

	if (!error) {
		inode_dec_link_count(inode);
		pr_debug("Dropping last nlink for dir: %px\n",
			inode);
	}
	return error;
}

static int
toyfs_rename(
	struct mnt_idmap	*idmap,
	struct inode		*old_dir,
	struct dentry		*old_dentry,
	struct inode		*new_dir,
	struct dentry		*new_dentry,
	unsigned int		flags)
{
	int			error = -EINVAL;
	struct tfs_inode_info	*tino;
	struct inode		*inode = d_inode(old_dentry);

	tino = container_of(old_dir, struct tfs_inode_info, vfs_inode);

	/*
	 * BUG: There are a few bugs in this function,
	 * let's see if students realize which ones...
	 */

	/*
	 * When renaming on top of an already existing file, we must make sure to
	 * decrease its link count
	 */
	if (d_inode(new_dentry) != NULL)
		toyfs_unlink(new_dir, new_dentry);

	error = toyfs_dir_del_entry(old_dir, old_dentry->d_name.name);

	error = toyfs_dir_add_entry(new_dir,
				    new_dentry->d_name.name,
				    inode->i_ino);

	/* FIXME: we need to cleanup and remove the added entry if we fail */
	return error;
}

struct inode_operations toyfs_dir_inode_operations = {
	.lookup		= toyfs_lookup,
	.create		= toyfs_create,
	.mkdir		= toyfs_mkdir,
	.link		= toyfs_link,
	.symlink	= toyfs_symlink,
	.unlink		= toyfs_unlink,
	.rmdir		= toyfs_rmdir,
	.rename		= toyfs_rename,
};

struct inode_operations toyfs_inode_operations = {
	.unlink		= toyfs_unlink,
	.rmdir		= toyfs_rmdir,
};

