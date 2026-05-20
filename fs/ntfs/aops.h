/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _LINUX_NTFS_AOPS_H
#define _LINUX_NTFS_AOPS_H
#include <linux/fs.h>
extern const struct address_space_operations ntfs_aops;
extern const struct address_space_operations ntfs_mft_aops;
#endif
