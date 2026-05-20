// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * iomap callbacks — bare stub that will be filled in.
 * Adapted for Linux 4.19 (no writepage_ctx, no folio_ops, no srcmap, etc.).
 */
#include <linux/writeback.h>
#include <linux/blkdev.h>

#include "attrib.h"
#include "mft.h"
#include "ntfs.h"
/* iomap decls now in ntfs.h */

/* ---------- read path ---------- */

static int ntfs_read_iomap_begin(struct inode *inode, loff_t offset, loff_t length,
		unsigned int flags, struct iomap *iomap)
{
	/* TODO: implement real iomap_begin for reads */
	return -EIO;
}

static int ntfs_read_iomap_end(struct inode *inode, loff_t pos, loff_t length,
		ssize_t written, unsigned int flags, struct iomap *iomap)
{
	return written;
}

const struct iomap_ops ntfs_read_iomap_ops = {
	.iomap_begin = ntfs_read_iomap_begin,
	.iomap_end   = ntfs_read_iomap_end,
};

const struct iomap_ops ntfs_seek_iomap_ops = {
	.iomap_begin = ntfs_read_iomap_begin,
	.iomap_end   = ntfs_read_iomap_end,
};

/* ---------- write path ---------- */

static int ntfs_write_iomap_begin(struct inode *inode, loff_t offset, loff_t length,
		unsigned int flags, struct iomap *iomap)
{
	return -EIO;
}

static int ntfs_write_iomap_end(struct inode *inode, loff_t pos, loff_t length,
		ssize_t written, unsigned int flags, struct iomap *iomap)
{
	return written;
}

const struct iomap_ops ntfs_write_iomap_ops = {
	.iomap_begin = ntfs_write_iomap_begin,
	.iomap_end   = ntfs_write_iomap_end,
};

/* ---------- mmap / dio ---------- */

const struct iomap_ops ntfs_page_mkwrite_iomap_ops = {
	.iomap_begin = ntfs_write_iomap_begin,
	.iomap_end   = ntfs_write_iomap_end,
};

const struct iomap_ops ntfs_dio_iomap_ops = {
	.iomap_begin = ntfs_write_iomap_begin,
	.iomap_end   = ntfs_write_iomap_end,
};

int ntfs_dio_zero_range(struct inode *inode, loff_t offset, loff_t length)
{
	if ((offset | length) & (512 - 1))
		return -EINVAL;

	return blkdev_issue_zeroout(inode->i_sb->s_bdev,
			offset >> SECTOR_SHIFT,
			length >> SECTOR_SHIFT,
			GFP_NOFS,
			BLKDEV_ZERO_NOUNMAP);
}
