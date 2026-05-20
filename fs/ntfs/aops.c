// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * NTFS kernel address space operations and page cache handling
 * (adapted for Linux 4.19).
 *
 * Copyright (c) 2001-2014 Anton Altaparmakov and Tuxera Inc.
 * Copyright (C) 2002 Richard Russon
 * Copyright (c) 2025 LG Electronics Co., Ltd.
 */

#include <linux/writeback.h>
#include <linux/iomap.h>

#include "attrib.h"
#include "mft.h"
#include "ntfs.h"
#include "debug.h"
/* iomap decls now in ntfs.h */

extern const struct iomap_ops ntfs_read_iomap_ops;

/*
 * ntfs_readpage - Read data for a page from the device
 * @file:	open file to which the page belongs or NULL
 * @page:	page cache page to fill with data
 *
 * Uses the iomap infrastructure via iomap_readpage().
 *
 * Return: 0 on success, or -errno on error.
 */
static int ntfs_readpage(struct file *file, struct page *page)
{
	loff_t i_size;
	struct inode *vi;
	struct ntfs_inode *ni;

	BUG_ON(!PageLocked(page));
	vi = page->mapping->host;
	ni = NTFS_I(vi);
	i_size = i_size_read(vi);

	/* Is the page fully outside i_size? (truncate in progress) */
	if (unlikely(page->index >= (i_size + PAGE_SIZE - 1) >>
			PAGE_SHIFT)) {
		zero_user(page, 0, PAGE_SIZE);
		ntfs_debug("Read outside i_size - truncated?");
		SetPageUptodate(page);
		unlock_page(page);
		return 0;
	}

	/*
	 * This can potentially happen because we clear PageUptodate()
	 * during ntfs_writepage() of MstProtected() attributes.
	 */
	if (PageUptodate(page)) {
		unlock_page(page);
		return 0;
	}

	/*
	 * Only $DATA attributes can be encrypted and only unnamed $DATA
	 * attributes can be compressed.  Index root can have the flags
	 * set but this means to create compressed/encrypted files, not
	 * that the attribute is compressed/encrypted.
	 */
	if (ni->type != AT_INDEX_ALLOCATION) {
		/* If attribute is encrypted, deny access, just like NT4. */
		if (NInoEncrypted(ni)) {
			BUG_ON(ni->type != AT_DATA);
			unlock_page(page);
			return -EACCES;
		}
		/* Compressed data streams are handled in compress.c. */
		if (NInoNonResident(ni) && NInoCompressed(ni)) {
			BUG_ON(ni->type != AT_DATA);
			BUG_ON(ni->name_len);
			return ntfs_read_compressed_block(page);
		}
	}

	return iomap_readpage(page, &ntfs_read_iomap_ops);
}

/*
 * ntfs_readpages - Read multiple pages at once
 * @mapping:	address space mapping
 * @pages:	list of pages to read
 * @nr_pages:	number of pages
 *
 * For regular non-resident files, delegates to iomap_readpages.
 * Compressed and resident files are skipped.
 *
 * Return: 0 on success, or -errno on error.
 */
static int ntfs_readpages(struct file *file, struct address_space *mapping,
		struct list_head *pages, unsigned int nr_pages)
{
	struct inode *inode = mapping->host;
	struct ntfs_inode *ni = NTFS_I(inode);

	/*
	 * Resident files are not cached in the page cache,
	 * and readpages is not implemented for compressed files.
	 */
	if (!NInoNonResident(ni) || NInoCompressed(ni))
		return 0;

	return iomap_readpages(mapping, pages, nr_pages, &ntfs_read_iomap_ops);
}

/*
 * ntfs_bmap - map logical file block to physical device block
 */
static sector_t ntfs_bmap(struct address_space *mapping, sector_t block)
{
	s64 ofs, size;
	loff_t i_size;
	s64 lcn;
	unsigned long blocksize, flags;
	struct ntfs_inode *ni = NTFS_I(mapping->host);
	struct ntfs_volume *vol = ni->vol;
	unsigned int delta;
	unsigned char blocksize_bits;

	ntfs_debug("Entering for mft_no 0x%llx, logical block 0x%llx.",
			ni->mft_no, (unsigned long long)block);
	if (ni->type != AT_DATA || !NInoNonResident(ni) || NInoEncrypted(ni) ||
	    NInoMstProtected(ni)) {
		ntfs_error(vol->sb,
			"BMAP does not make sense for %s attributes, returning 0.",
			(ni->type != AT_DATA) ? "non-data" :
			(!NInoNonResident(ni) ? "resident" : "encrypted"));
		return 0;
	}

	blocksize = vol->sb->s_blocksize;
	blocksize_bits = vol->sb->s_blocksize_bits;
	ofs = (s64)block << blocksize_bits;
	read_lock_irqsave(&ni->size_lock, flags);
	size = ni->initialized_size;
	i_size = i_size_read(VFS_I(ni));
	read_unlock_irqrestore(&ni->size_lock, flags);

	if (unlikely(ofs >= size || (ofs + blocksize > size && size < i_size)))
		goto hole;
	down_read(&ni->runlist.lock);
	lcn = ntfs_attr_vcn_to_lcn_nolock(ni,
			ntfs_bytes_to_cluster(vol, ofs), false);
	up_read(&ni->runlist.lock);
	if (unlikely(lcn < LCN_HOLE)) {
		switch ((int)lcn) {
		case LCN_ENOENT:
			goto hole;
		case LCN_ENOMEM:
			ntfs_error(vol->sb,
				"Not enough memory for mapping inode 0x%llx.",
				ni->mft_no);
			break;
		default:
			ntfs_error(vol->sb,
				"Failed mapping for inode 0x%llx. Run chkdsk.",
				ni->mft_no);
			break;
		}
		return 0;
	}
	if (lcn < 0)
		goto hole;

	delta = ofs & vol->cluster_size_mask;
	if (unlikely(sizeof(block) < sizeof(lcn))) {
		block = lcn = (ntfs_cluster_to_bytes(vol, lcn) + delta) >>
				blocksize_bits;
		if (unlikely(block != lcn)) {
			ntfs_error(vol->sb,
				"Physical block 0x%llx too large.",
				(long long)lcn);
			return 0;
		}
	} else
		block = (ntfs_cluster_to_bytes(vol, lcn) + delta) >>
				blocksize_bits;
	ntfs_debug("Done (returning block 0x%llx).", (unsigned long long)lcn);
	return block;

hole:
	ntfs_debug("Done (returning hole).");
	return 0;
}

static int ntfs_swap_activate(struct swap_info_struct *sis,
		struct file *swap_file, sector_t *span)
{
	return iomap_swapfile_activate(sis, swap_file, span,
			&ntfs_read_iomap_ops);
}

const struct address_space_operations ntfs_aops = {
	.readpage		= ntfs_readpage,
	.readpages		= ntfs_readpages,
	.set_page_dirty		= __set_page_dirty_nobuffers,
	.bmap			= ntfs_bmap,
	.migratepage		= iomap_migrate_page,
	.is_partially_uptodate	= iomap_is_partially_uptodate,
	.error_remove_page	= generic_error_remove_page,
	.releasepage		= iomap_releasepage,
	.invalidatepage		= iomap_invalidatepage,
	.direct_IO		= noop_direct_IO,
	.swap_activate		= ntfs_swap_activate,
};

const struct address_space_operations ntfs_mft_aops = {
	.readpage		= ntfs_readpage,
	.readpages		= ntfs_readpages,
	.set_page_dirty		= __set_page_dirty_nobuffers,
	.bmap			= ntfs_bmap,
	.migratepage		= iomap_migrate_page,
	.is_partially_uptodate	= iomap_is_partially_uptodate,
	.error_remove_page	= generic_error_remove_page,
	.releasepage		= iomap_releasepage,
	.invalidatepage		= iomap_invalidatepage,
};
