// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/kernel.h>
#include <linux/lz4.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/vmalloc.h>

#include "backend_lz4.h"

struct lz4_ctx {
	void *mem;
	LZ4_streamDecode_t *dstrm;
	LZ4_stream_t *cstrm;
};

struct lz4_drv {
	/* template compression stream preloaded with the current dictionary */
	LZ4_stream_t base_cstream;
	u32 dict_gen;
	bool base_c_valid;
};

static void lz4_release_params(struct zcomp_params *params)
{
	kfree(params->drv_data);
	params->drv_data = NULL;
}

static int lz4_setup_params(struct zcomp_params *params)
{
	struct lz4_drv *drv;

	if (params->level == ZCOMP_PARAM_NO_LEVEL)
		params->level = LZ4_ACCELERATION_DEFAULT;

	drv = kzalloc(sizeof(*drv), GFP_KERNEL);
	if (!drv)
		return -ENOMEM;

	drv->dict_gen = 0;
	drv->base_c_valid = false;
	params->drv_data = drv;

	return 0;
}

static void lz4_destroy(struct zcomp_ctx *ctx)
{
	struct lz4_ctx *zctx = ctx->context;

	if (!zctx)
		return;

	vfree(zctx->mem);
	kfree(zctx->dstrm);
	kfree(zctx->cstrm);
	kfree(zctx);
}

static int lz4_create(struct zcomp_params *params, struct zcomp_ctx *ctx)
{
	struct lz4_ctx *zctx;

	zctx = kzalloc(sizeof(*zctx), GFP_KERNEL);
	if (!zctx)
		return -ENOMEM;

	ctx->context = zctx;
	if (params->dict_sz == 0) {
		zctx->mem = vmalloc(LZ4_MEM_COMPRESS);
		if (!zctx->mem)
			goto error;
	} else {
		zctx->dstrm = kzalloc(sizeof(*zctx->dstrm), GFP_KERNEL);
		if (!zctx->dstrm)
			goto error;

		zctx->cstrm = kzalloc(sizeof(*zctx->cstrm), GFP_KERNEL);
		if (!zctx->cstrm)
			goto error;
	}

	return 0;

error:
	lz4_destroy(ctx);
	return -ENOMEM;
}

static int lz4_build_base_cstream(struct zcomp_params *params)
{
	struct lz4_drv *drv = params->drv_data;
	int ret;

	if (!params->dict || !params->dict_sz)
		return -EINVAL;

	memset(&drv->base_cstream, 0, sizeof(drv->base_cstream));

	ret = LZ4_loadDict(&drv->base_cstream, params->dict, params->dict_sz);
	if (ret != params->dict_sz)
		return -EINVAL;

	drv->dict_gen = params->dict_gen;
	drv->base_c_valid = true;
	return 0;
}

static int lz4_compress(struct zcomp_params *params, struct zcomp_ctx *ctx,
			struct zcomp_req *req)
{
	struct lz4_ctx *zctx = ctx->context;
	struct lz4_drv *drv = params->drv_data;
	int ret;

	if (!zctx->cstrm) {
		ret = LZ4_compress_fast(req->src, req->dst, req->src_len,
					req->dst_len, params->level,
					zctx->mem);
	} else {
		/*
		 * The dictionary preprocessing in LZ4_loadDict() is expensive.
		 * Build a template stream once and clone it into the per-stream
		 * context for subsequent compressions.
		 */
		if (!drv->base_c_valid || drv->dict_gen != params->dict_gen) {
			ret = lz4_build_base_cstream(params);
			if (ret)
				return ret;
		}

		memcpy(zctx->cstrm, &drv->base_cstream, sizeof(*zctx->cstrm));
		ret = LZ4_compress_fast_continue(zctx->cstrm, req->src,
						 req->dst, req->src_len,
						 req->dst_len, params->level);
	}
	if (!ret)
		return -EINVAL;
	req->dst_len = ret;
	return 0;
}

static int lz4_decompress(struct zcomp_params *params, struct zcomp_ctx *ctx,
			  struct zcomp_req *req)
{
	struct lz4_ctx *zctx = ctx->context;
	int ret;

	if (!zctx->dstrm) {
		ret = LZ4_decompress_safe(req->src, req->dst, req->src_len,
					  req->dst_len);
	} else {
		ret = LZ4_setStreamDecode(zctx->dstrm, params->dict,
					  params->dict_sz);
		if (!ret)
			return -EINVAL;
		ret = LZ4_decompress_safe_continue(zctx->dstrm, req->src,
						   req->dst, req->src_len,
						   req->dst_len);
	}
	if (ret < 0)
		return -EINVAL;
	return 0;
}

const struct zcomp_ops backend_lz4 = {
	.compress	= lz4_compress,
	.decompress	= lz4_decompress,
	.create_ctx	= lz4_create,
	.destroy_ctx	= lz4_destroy,
	.setup_params	= lz4_setup_params,
	.release_params	= lz4_release_params,
	.name		= "lz4",
};
