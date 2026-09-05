// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * zram backend using the ZXC compression library (lib/zxc, upstream 0.14.0).
 *
 * Uses the ZXC "static context" API: the per-CPU context is a caller-owned,
 * vmalloc'd workspace pinned to one block size (PAGE_SIZE) and level, so the
 * compression hot path performs no runtime allocation. Both the compression
 * and decompression workspaces are allocated once in ->create_ctx and freed
 * in ->destroy_ctx.
 *
 * Only the no-dictionary path is wired up (zram never supplies a dictionary
 * in this tree); a dict request is rejected at create time.
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/zxc.h>

#include "backend_zxc.h"

struct zxc_ctx {
	void *mem;		/* compression workspace (vmalloc) */
	zxc_cctx *cctx;		/* static context carved inside ->mem */
	zxc_dctx *dctx;		/* static decompression context */
	void *dctx_mem;		/* decompression workspace (vmalloc) */
	int level;		/* pinned compression level */
};

struct zxc_drv {
	zxc_compress_opts_t opts;
};

static void zxc_release_params(struct zcomp_params *params)
{
	kfree(params->drv_data);
	params->drv_data = NULL;
}

static int zxc_setup_params(struct zcomp_params *params)
{
	struct zxc_drv *drv;

	if (params->level == ZCOMP_PARAM_NO_LEVEL)
		params->level = ZXC_LEVEL_DEFAULT;

	if (params->level < ZXC_LEVEL_FASTEST || params->level > ZXC_LEVEL_ULTRA)
		return -EINVAL;

	drv = kzalloc(sizeof(*drv), GFP_KERNEL);
	if (!drv)
		return -ENOMEM;

	/*
	 * The static cctx pins level, block_size and checksum_enabled at init
	 * time. block_size must be explicit: zxc_init_static_cctx treats 0 as
	 * ZXC_BLOCK_SIZE_DEFAULT (512 KB), which would mismatch the PAGE_SIZE
	 * blocks zram always passes.
	 */
	drv->opts.level = params->level;
	drv->opts.block_size = PAGE_SIZE;
	drv->opts.checksum_enabled = 0;
	params->drv_data = drv;

	return 0;
}

static void zxc_destroy(struct zcomp_ctx *ctx)
{
	struct zxc_ctx *zctx = ctx->context;

	if (!zctx)
		return;

	/* zxc_free_cctx/zxc_free_dctx are no-ops on static contexts: the
	 * caller owns the workspaces, so just release the vmalloc regions. */
	vfree(zctx->dctx_mem);
	vfree(zctx->mem);
	kfree(zctx);
	ctx->context = NULL;
}

static int zxc_create(struct zcomp_params *params, struct zcomp_ctx *ctx)
{
	struct zxc_ctx *zctx;
	struct zxc_drv *drv = params->drv_data;
	size_t cctx_sz, dctx_sz;

	/* Dictionary compression is not supported by this backend. */
	if (params->dict_sz)
		return -EINVAL;

	cctx_sz = zxc_static_cctx_workspace_size(PAGE_SIZE, params->level);
	dctx_sz = zxc_static_dctx_workspace_size(PAGE_SIZE);
	if (!cctx_sz || !dctx_sz)
		return -EINVAL;

	zctx = kzalloc(sizeof(*zctx), GFP_KERNEL);
	if (!zctx)
		return -ENOMEM;

	ctx->context = zctx;
	zctx->level = params->level;

	/* vmalloc returns page-aligned memory, which satisfies the cache-line
	 * alignment the static workspaces require. */
	zctx->mem = vzalloc(cctx_sz);
	if (!zctx->mem)
		goto error;

	zctx->dctx_mem = vzalloc(dctx_sz);
	if (!zctx->dctx_mem)
		goto error;

	zctx->cctx = zxc_init_static_cctx(zctx->mem, cctx_sz, &drv->opts);
	if (!zctx->cctx)
		goto error;

	zctx->dctx = zxc_init_static_dctx(zctx->dctx_mem, dctx_sz, PAGE_SIZE);
	if (!zctx->dctx)
		goto error;

	return 0;

error:
	zxc_destroy(ctx);
	return -EINVAL;
}

static int zxc_compress(struct zcomp_params *params, struct zcomp_ctx *ctx,
			struct zcomp_req *req)
{
	struct zxc_ctx *zctx = ctx->context;
	int64_t ret;

	/* NULL opts: reuse the level/block_size/checksum pinned at init. */
	ret = zxc_compress_block(zctx->cctx, req->src, req->src_len, req->dst,
				 req->dst_len, NULL);
	if (ret < 0)
		return -EINVAL;
	req->dst_len = (size_t)ret;
	return 0;
}

static int zxc_decompress(struct zcomp_params *params, struct zcomp_ctx *ctx,
			  struct zcomp_req *req)
{
	struct zxc_ctx *zctx = ctx->context;
	int64_t ret;

	/*
	 * Safe variant: req->dst is an exactly PAGE_SIZE-sized zram page and
	 * must not be written past its end (the wild-copy fast path of the
	 * regular block decoder needs a tail pad we do not have here).
	 */
	ret = zxc_decompress_block_safe(zctx->dctx, req->src, req->src_len,
					req->dst, req->dst_len, NULL);
	if (ret < 0 || ret != (int64_t)req->dst_len)
		return -EINVAL;
	return 0;
}

const struct zcomp_ops backend_zxc = {
	.compress	= zxc_compress,
	.decompress	= zxc_decompress,
	.create_ctx	= zxc_create,
	.destroy_ctx	= zxc_destroy,
	.setup_params	= zxc_setup_params,
	.release_params	= zxc_release_params,
	.name		= "zxc",
};
