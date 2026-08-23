/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file zxc_deps.h
 * @brief Linux-kernel build of the ZXC libc-dependency override point.
 *
 * The core compression/decompression code only ever reaches the standard
 * library through the macros and headers declared here.  This file replaces
 * the stock (hosted) version shipped with libzxc: it substitutes the kernel's
 * own headers, maps ZXC_MALLOC / ZXC_FREE / ... onto the slab allocator, and
 * provides the few C-library constants the kernel headers do not define.
 */

#ifndef ZXC_DEPS_H
#define ZXC_DEPS_H

#include <linux/kernel.h>
#include <linux/limits.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/types.h>

/**
 * @name C Library Constants Missing From Kernel Headers
 * @{
 * The kernel's <linux/limits.h> only defines the U8/U16/U32/U64_MAX family.
 * The ZXC sources reference the libc spellings (and CHAR_BIT), so provide the
 * few missing ones.
 */
#ifndef CHAR_BIT
#define CHAR_BIT 8
#endif
#ifndef UINT16_MAX
#define UINT16_MAX ((uint16_t)~0U)
#endif
#ifndef UINT32_MAX
#define UINT32_MAX ((uint32_t)~0U)
#endif
#ifndef UINT64_MAX
#define UINT64_MAX ((uint64_t)~0ULL)
#endif
/** @} */

/**
 * @name C11 Atomics
 * @brief The kernel has no <stdatomic.h> in its include path.  Force the
 *        library's volatile fallback for the lazy function-pointer dispatcher
 *        (a benign race: every writer stores the same constant).
 * @{
 */
#ifndef ZXC_USE_C11_ATOMICS
#define ZXC_USE_C11_ATOMICS 0
#endif
/** @} */

/**
 * @name Heap Allocator Abstraction
 * @brief Map the library's allocator macros onto the kernel slab allocator.
 * @{
 */
#define ZXC_MALLOC(size) kmalloc((size), GFP_KERNEL)
#define ZXC_CALLOC(nmemb, size) kcalloc((nmemb), (size), GFP_KERNEL)
#define ZXC_REALLOC(ptr, size) krealloc((ptr), (size), GFP_KERNEL)
#define ZXC_FREE(ptr) kfree(ptr)

/**
 * @brief Cache-line-aligned allocation for the compression workspace.
 *
 * kmalloc() alignment (ARCH_KMALLOC_MINALIGN) is not guaranteed to satisfy
 * ZXC_CACHE_LINE_SIZE on every architecture, so over-allocate and round up,
 * keeping the original pointer in a slot just below the returned address.
 * Both helpers are static inline so each translation unit gets its own copy
 * (ZXC_ALIGNED_MALLOC is invoked from several library .c files).
 */
static inline void *zxc_kernel_aligned_alloc(size_t size, size_t alignment)
{
	void *mem;
	void *ptr;

	if (alignment < sizeof(void *))
		alignment = sizeof(void *);
	mem = kmalloc(size + alignment + sizeof(void *), GFP_KERNEL);
	if (!mem)
		return NULL;
	ptr = (void *)ALIGN((uintptr_t)mem + sizeof(void *), alignment);
	((void **)ptr)[-1] = mem;
	return ptr;
}

static inline void zxc_kernel_aligned_free(void *ptr)
{
	if (ptr)
		kfree(((void **)ptr)[-1]);
}

#define ZXC_ALIGNED_MALLOC(size, alignment) zxc_kernel_aligned_alloc((size), (alignment))
#define ZXC_ALIGNED_FREE(ptr) zxc_kernel_aligned_free(ptr)
/** @} */

#endif /* ZXC_DEPS_H */
