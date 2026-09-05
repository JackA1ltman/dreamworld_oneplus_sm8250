/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * ZXC - High-performance lossless compression
 *
 * Kernel public API header (port of upstream ZXC 0.14.0 to the Linux kernel).
 *
 * This header merges the upstream zxc_constants.h, zxc_error.h, zxc_opts.h and
 * the subset of zxc_buffer.h that is compiled into the kernel build:
 *
 *   - library info (zxc_min_level ... zxc_version_string)
 *   - block-level API (zxc_compress_block / zxc_decompress_block /
 *     zxc_decompress_block_safe and their bounds helpers)
 *   - reusable context API (zxc_create_cctx / zxc_create_dctx ...)
 *   - static context API (caller-allocated workspace, designed for kernel use)
 *
 * The frame API (zxc_compress / zxc_decompress / zxc_decompress_inplace ...)
 * is not built for the kernel (see lib/zxc/Makefile, -DZXC_NO_FRAME_API) and is
 * therefore not declared here.
 *
 * The library is built with ZXC_STATIC_DEFINE (no shared-library export
 * attributes), so declarations carry no ZXC_EXPORT.
 */

#ifndef _LINUX_ZXC_H
#define _LINUX_ZXC_H

#include <linux/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Library version ----------------------------------------------------- */

#define ZXC_VERSION_MAJOR 0
#define ZXC_VERSION_MINOR 14
#define ZXC_VERSION_PATCH 0

#define ZXC_STR_HELPER(x) #x
#define ZXC_STR(x) ZXC_STR_HELPER(x)

#define ZXC_LIB_VERSION_STR \
	ZXC_STR(ZXC_VERSION_MAJOR) "." ZXC_STR(ZXC_VERSION_MINOR) "." ZXC_STR(ZXC_VERSION_PATCH)

/* ---- Block size ---------------------------------------------------------- */

#define ZXC_BLOCK_SIZE_MIN_LOG2 12
#define ZXC_BLOCK_SIZE_MAX_LOG2 21
#define ZXC_BLOCK_SIZE_DEFAULT (512 * 1024)
#define ZXC_BLOCK_SIZE_MIN (1U << ZXC_BLOCK_SIZE_MIN_LOG2)
#define ZXC_BLOCK_SIZE_MAX (1U << ZXC_BLOCK_SIZE_MAX_LOG2)

/* ---- Dictionary ---------------------------------------------------------- */

#define ZXC_DICT_SIZE_MAX ((1U << 16) - 1U)
#define ZXC_DICT_HEADER_SIZE 16
#define ZXC_HUF_TABLE_SIZE 128

/* ---- Threading ----------------------------------------------------------- */

#define ZXC_MAX_THREADS 512

/* ---- File format --------------------------------------------------------- */

#define ZXC_FILE_HEADER_SIZE 16
#define ZXC_FILE_FOOTER_SIZE 12

/* ---- Compression levels -------------------------------------------------- */

/**
 * ZXC compression levels. Pass one as the level of zxc_compress_block().
 * Higher levels spend more encoding time for a better ratio; decoding stays
 * fast at every level.
 */
typedef enum {
	ZXC_LEVEL_FASTEST = 1,  /* Fastest compression; lowest ratio. */
	ZXC_LEVEL_FAST = 2,     /* Fast compression; slightly better ratio. */
	ZXC_LEVEL_DEFAULT = 3,  /* Recommended default. */
	ZXC_LEVEL_BALANCED = 4, /* Balanced trade-off. */
	ZXC_LEVEL_COMPACT = 5,  /* Denser encoding. */
	ZXC_LEVEL_DENSITY = 6,  /* Adds Huffman-coded literals. */
	ZXC_LEVEL_ULTRA = 7     /* Maximum density. */
} zxc_compression_level_t;

/* ---- Error codes --------------------------------------------------------- */

/**
 * Error codes returned by ZXC functions. All negative; `result < 0` is the
 * failure check. zxc_error_name() turns a code into a readable string.
 */
typedef enum {
	ZXC_OK = 0,

	/* Memory errors */
	ZXC_ERROR_MEMORY = -1,

	/* Buffer/capacity errors */
	ZXC_ERROR_DST_TOO_SMALL = -2,
	ZXC_ERROR_SRC_TOO_SMALL = -3,

	/* Format/header errors */
	ZXC_ERROR_BAD_MAGIC = -4,
	ZXC_ERROR_BAD_VERSION = -5,
	ZXC_ERROR_BAD_HEADER = -6,
	ZXC_ERROR_BAD_CHECKSUM = -7,

	/* Data integrity errors */
	ZXC_ERROR_CORRUPT_DATA = -8,
	ZXC_ERROR_BAD_OFFSET = -9,
	ZXC_ERROR_OVERFLOW = -10,

	/* I/O errors */
	ZXC_ERROR_IO = -11,
	ZXC_ERROR_NULL_INPUT = -12,

	/* Block errors */
	ZXC_ERROR_BAD_BLOCK_TYPE = -13,
	ZXC_ERROR_BAD_BLOCK_SIZE = -14,

	/* Dictionary errors */
	ZXC_ERROR_DICT_REQUIRED = -15,
	ZXC_ERROR_DICT_MISMATCH = -16,
	ZXC_ERROR_DICT_TOO_LARGE = -17,

	/* Parameter errors */
	ZXC_ERROR_BAD_LEVEL = -18,
} zxc_error_t;

/* ---- Options ------------------------------------------------------------- */

/** Progress callback type (unused by the block API; kept for struct parity). */
typedef void (*zxc_progress_callback_t)(uint64_t bytes_processed, uint64_t bytes_total,
					const void *user_data);

/** Options for compression. Zero-initialise for safe defaults. */
typedef struct {
	int n_threads;		/* Worker thread count (0 = auto-detect). */
	int level;		/* Compression level 1-7 (0 = default). */
	size_t block_size;	/* Block size (0 = default; power of 2, 4K-2M). */
	int checksum_enabled;	/* 1 to write per-block and global checksums. */
	int seekable;		/* Unused by the block API. */
	const void *dict;	/* Pre-trained dictionary (NULL = none). */
	size_t dict_size;	/* Dictionary size (0 = none, max ZXC_DICT_SIZE_MAX). */
	const void *dict_huf;	/* Optional shared literal Huffman table. */
	zxc_progress_callback_t progress_cb; /* Unused by the block API. */
	void *user_data;	/* Passed through to progress_cb. */
} zxc_compress_opts_t;

/** Options for decompression. Zero-initialise for safe defaults. */
typedef struct {
	int n_threads;		/* Worker thread count (0 = auto-detect). */
	int checksum_enabled;	/* 1 to verify per-block and global checksums. */
	const void *dict;	/* Pre-trained dictionary (NULL = none). */
	size_t dict_size;	/* Dictionary size (0 = none). */
	const void *dict_huf;	/* Optional shared literal Huffman table. */
	zxc_progress_callback_t progress_cb; /* Unused by the block API. */
	void *user_data;	/* Passed through to progress_cb. */
} zxc_decompress_opts_t;

size_t zxc_compress_opts_size(void);
size_t zxc_decompress_opts_size(void);

/* ---- Library info -------------------------------------------------------- */

int zxc_min_level(void);
int zxc_max_level(void);
int zxc_default_level(void);
const char *zxc_version_string(void);
const char *zxc_error_name(int code);

/* ---- Block-level API (no file framing) ----------------------------------- */

/* Opaque reusable contexts (definitions are private to lib/zxc). */
typedef struct zxc_cctx_s zxc_cctx;
typedef struct zxc_dctx_s zxc_dctx;

/**
 * Maximum compressed size for a single block (excludes file header/EOF/footer).
 * Use to size the destination of zxc_compress_block().
 */
uint64_t zxc_compress_block_bound(size_t input_size);

/**
 * Minimum dst_capacity zxc_decompress_block() needs for a block of
 * uncompressed_size bytes (includes the decoder's wild-copy tail pad).
 */
uint64_t zxc_decompress_block_bound(size_t uncompressed_size);

/** Estimated peak cctx memory for a given block size and level. */
uint64_t zxc_estimate_cctx_size(size_t src_size, int level);

/**
 * Compresses a single block: block_header(8B) + payload + optional
 * checksum(4B). Returns compressed size (> 0) or a negative zxc_error_t.
 */
int64_t zxc_compress_block(zxc_cctx *cctx, const void *src, size_t src_size, void *dst,
			   size_t dst_capacity, const zxc_compress_opts_t *opts);

/**
 * Decompresses a single block produced by zxc_compress_block(). dst_capacity
 * must be at least the original uncompressed size (up to
 * ZXC_BLOCK_SIZE_MAX + tail pad). Returns decompressed size or negative error.
 */
int64_t zxc_decompress_block(zxc_dctx *dctx, const void *src, size_t src_size, void *dst,
			     size_t dst_capacity, const zxc_decompress_opts_t *opts);

/**
 * Safe-variant block decompressor: accepts dst_capacity == uncompressed size
 * (exactly-sized destination, no tail-pad margin). Slightly slower on the
 * GLO/GHI paths; RAW blocks route straight through. Recommended for zram.
 */
int64_t zxc_decompress_block_safe(zxc_dctx *dctx, const void *src, size_t src_size, void *dst,
				  size_t dst_capacity, const zxc_decompress_opts_t *opts);

/* ---- Reusable context API (opaque, heap-allocated) ------------------------ */

/**
 * Creates a reusable compression context. With non-NULL opts the buffers are
 * pre-allocated from the given level/block_size; with NULL allocation is
 * deferred to the first call. Returns NULL on allocation failure.
 */
zxc_cctx *zxc_create_cctx(const zxc_compress_opts_t *opts);
/** Frees a compression context. NULL is a no-op. */
void zxc_free_cctx(zxc_cctx *cctx);
/** Creates a reusable decompression context. NULL on allocation failure. */
zxc_dctx *zxc_create_dctx(void);
/** Frees a decompression context. NULL is a no-op. */
void zxc_free_dctx(zxc_dctx *dctx);

/* ---- Static context API (caller-allocated workspace) --------------------- */

/**
 * Exact size of a static compression workspace for the given block_size and
 * level. Returns 0 if the arguments are invalid. The workspace must be
 * cache-line aligned; allocate with vmalloc and align the base.
 */
size_t zxc_static_cctx_workspace_size(size_t block_size, int level);

/**
 * Initialises a compression context inside a caller-owned workspace of at
 * least zxc_static_cctx_workspace_size() bytes. block_size/level/checksum are
 * pinned at init; zxc_free_cctx is a no-op on the returned handle. Returns
 * NULL if the workspace is too small or the options are invalid.
 */
zxc_cctx *zxc_init_static_cctx(void *workspace, size_t workspace_size,
			       const zxc_compress_opts_t *opts);

/**
 * Exact size of a static decompression workspace for the given block_size.
 * Returns 0 if the argument is invalid.
 */
size_t zxc_static_dctx_workspace_size(size_t block_size);

/**
 * Initialises a decompression context inside a caller-owned workspace.
 * block_size is pinned at init. zxc_free_dctx is a no-op on the handle.
 */
zxc_dctx *zxc_init_static_dctx(void *workspace, size_t workspace_size, size_t block_size);

#ifdef __cplusplus
}
#endif

#endif /* _LINUX_ZXC_H */
