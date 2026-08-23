/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file zxc_exports.c
 * @brief Kernel-module symbol exports for the ZXC public API.
 *
 * The core library sources are a verbatim port of upstream libzxc and carry no
 * EXPORT_SYMBOL of their own. These exports let out-of-tree consumers such as
 * crypto/zxc.ko (or a modular zram/zswap) resolve the block-level entry points
 * when lib/zxc is built as a module or when those consumers are modular on top
 * of a built-in lib/zxc.
 *
 * Kept in a separate translation unit so the ported sources stay untouched.
 */

#include <linux/export.h>
#include <linux/module.h>
#include <linux/zxc.h>

EXPORT_SYMBOL(zxc_create_cctx);
EXPORT_SYMBOL(zxc_free_cctx);
EXPORT_SYMBOL(zxc_create_dctx);
EXPORT_SYMBOL(zxc_free_dctx);
EXPORT_SYMBOL(zxc_compress_block);
EXPORT_SYMBOL(zxc_decompress_block_safe);

/*
 * The library sources are BSD-3-Clause; declare the combined module as
 * dual-licensed so it loads cleanly (no "unspecified license" taint) when
 * lib/zxc is built as a standalone module (CRYPTO_ZXC=m with no zram backend).
 */
MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("ZXC lossless compression library (kernel port)");
