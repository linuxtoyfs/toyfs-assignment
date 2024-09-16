// SPDX-License-Identifier: GPL-2.0-only

#define pr_fmt(fmt) "%s:%s: " fmt, KBUILD_MODNAME, __func__

#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/mpage.h>
#include <linux/buffer_head.h>
#include "toyfs_types.h"
#include "toyfs_iops.h"


int toyfs_write_begin(struct file *filp,
		      struct address_space *mapping,
		      loff_t pos,
		      unsigned len,
		      struct page **pagep,
		      void **fsdata)
{
	int ret;
	struct inode *ip = mapping->host;

	pr_debug("inode: %px off: %lld len: %d\n",
		ip, pos, len);

	ret = block_write_begin(mapping, pos, len, pagep, toyfs_get_block);

	if (unlikely(ret)) {
		if ((pos + len) > ip->i_size)
			truncate_pagecache(ip, ip->i_size);
		pr_debug("Failed to write to inode %px\n", ip);
	} else {
		pr_debug("For inode: %px, got page: %px\n",
			ip, *pagep);
	}

	return ret;
}

int toyfs_write_end(struct file *filp,
		    struct address_space *mapping,
		    loff_t pos,
		    unsigned len,
		    unsigned copied,
		    struct page *page,
		    void *fsdata)
{
	pr_debug("inode %px off: %lld len: %d page: %px\n",
		mapping->host, pos, len, page);
	return generic_write_end(filp, mapping, pos, len, copied, page, fsdata);
}

int toyfs_writepages(
		    struct address_space *mapping,
		    struct writeback_control *wbc)
{
	pr_debug("writing pages");
	return mpage_writepages(mapping, wbc, toyfs_get_block);
}

int toyfs_read_folio(struct file *filp, struct folio *folio)
{
	pr_debug("reading file %p\n", filp);
	return block_read_full_folio(folio, toyfs_get_block);
}

struct address_space_operations	toyfs_aops = {
	.dirty_folio		= block_dirty_folio,
	.invalidate_folio	= block_invalidate_folio,
	.write_begin		= toyfs_write_begin,
	.write_end		= toyfs_write_end,
	.writepages		= toyfs_writepages,
	.read_folio		= toyfs_read_folio,
};

