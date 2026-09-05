/*
 * Cryptographic API.
 *
 * ZXC compression algorithm (kernel port of upstream libzxc 0.14.0).
 *
 * Wraps the block-level API exported by lib/zxc (see include/linux/zxc.h) in
 * the standard Crypto API compress / scompress interfaces, so any Crypto API
 * user (zswap, IPComp, ...) can select "zxc" alongside lz4/zstd/lzo.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */
#include <linux/crypto.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/zxc.h>
#include <crypto/internal/scompress.h>

/*
 * The context holds the two opaque reusable handles the block API needs. They
 * are created lazily (NULL opts), so the compression context sizes its
 * internal buffers on first use from the actual input size, and is reused
 * across calls of the same size (e.g. PAGE_SIZE pages from zram/zswap).
 */
struct zxc_ctx {
	zxc_cctx *cctx;
	zxc_dctx *dctx;
};

static void zxc_release_ctx(struct zxc_ctx *ctx)
{
	zxc_free_cctx(ctx->cctx);
	zxc_free_dctx(ctx->dctx);
	ctx->cctx = NULL;
	ctx->dctx = NULL;
}

static int zxc_init_ctx(struct zxc_ctx *ctx)
{
	ctx->cctx = zxc_create_cctx(NULL);
	if (!ctx->cctx)
		return -ENOMEM;

	ctx->dctx = zxc_create_dctx();
	if (!ctx->dctx) {
		zxc_free_cctx(ctx->cctx);
		ctx->cctx = NULL;
		return -ENOMEM;
	}

	return 0;
}

static void *zxc_alloc_ctx(struct crypto_scomp *tfm)
{
	struct zxc_ctx *ctx;
	int ret;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return ERR_PTR(-ENOMEM);

	ret = zxc_init_ctx(ctx);
	if (ret) {
		kfree(ctx);
		return ERR_PTR(ret);
	}

	return ctx;
}

static void zxc_free_ctx(struct crypto_scomp *tfm, void *mem)
{
	struct zxc_ctx *ctx = mem;

	zxc_release_ctx(ctx);
	kfree(ctx);
}

static int zxc_init(struct crypto_tfm *tfm)
{
	struct zxc_ctx *ctx = crypto_tfm_ctx(tfm);

	return zxc_init_ctx(ctx);
}

static void zxc_exit(struct crypto_tfm *tfm)
{
	struct zxc_ctx *ctx = crypto_tfm_ctx(tfm);

	zxc_release_ctx(ctx);
}

static int __zxc_compress(const u8 *src, unsigned int slen,
			  u8 *dst, unsigned int *dlen, struct zxc_ctx *ctx)
{
	int64_t out_len;

	/* NULL opts: reuse the level/block_size/checksum pinned in the context. */
	out_len = zxc_compress_block(ctx->cctx, src, slen, dst, *dlen, NULL);
	if (out_len < 0)
		return -EINVAL;

	*dlen = (unsigned int)out_len;
	return 0;
}

static int zxc_compress(struct crypto_tfm *tfm, const u8 *src,
			unsigned int slen, u8 *dst, unsigned int *dlen)
{
	struct zxc_ctx *ctx = crypto_tfm_ctx(tfm);

	return __zxc_compress(src, slen, dst, dlen, ctx);
}

static int zxc_scompress(struct crypto_scomp *tfm, const u8 *src,
			 unsigned int slen, u8 *dst, unsigned int *dlen,
			 void *ctx)
{
	return __zxc_compress(src, slen, dst, dlen, ctx);
}

static int __zxc_decompress(const u8 *src, unsigned int slen,
			    u8 *dst, unsigned int *dlen, struct zxc_ctx *ctx)
{
	int64_t out_len;

	/*
	 * Safe variant: *dlen is the caller's exactly-sized destination (e.g. a
	 * PAGE_SIZE zram page) and must not be written past its end; the wild-copy
	 * fast path of the regular block decoder needs a tail pad we do not have.
	 */
	out_len = zxc_decompress_block_safe(ctx->dctx, src, slen, dst, *dlen, NULL);
	if (out_len < 0)
		return -EINVAL;

	*dlen = (unsigned int)out_len;
	return 0;
}

static int zxc_decompress(struct crypto_tfm *tfm, const u8 *src,
			  unsigned int slen, u8 *dst, unsigned int *dlen)
{
	struct zxc_ctx *ctx = crypto_tfm_ctx(tfm);

	return __zxc_decompress(src, slen, dst, dlen, ctx);
}

static int zxc_sdecompress(struct crypto_scomp *tfm, const u8 *src,
			   unsigned int slen, u8 *dst, unsigned int *dlen,
			   void *ctx)
{
	return __zxc_decompress(src, slen, dst, dlen, ctx);
}

static struct crypto_alg zxc_alg = {
	.cra_name		= "zxc",
	.cra_driver_name	= "zxc-generic",
	.cra_priority		= 100,
	.cra_flags		= CRYPTO_ALG_TYPE_COMPRESS,
	.cra_ctxsize		= sizeof(struct zxc_ctx),
	.cra_module		= THIS_MODULE,
	.cra_init		= zxc_init,
	.cra_exit		= zxc_exit,
	.cra_u			= { .compress = {
	.coa_compress		= zxc_compress,
	.coa_decompress		= zxc_decompress } }
};

static struct scomp_alg scomp = {
	.alloc_ctx		= zxc_alloc_ctx,
	.free_ctx		= zxc_free_ctx,
	.compress		= zxc_scompress,
	.decompress		= zxc_sdecompress,
	.base			= {
		.cra_name	= "zxc",
		.cra_driver_name = "zxc-scomp",
		.cra_module	 = THIS_MODULE,
	}
};

static int __init zxc_mod_init(void)
{
	int ret;

	ret = crypto_register_alg(&zxc_alg);
	if (ret)
		return ret;

	ret = crypto_register_scomp(&scomp);
	if (ret) {
		crypto_unregister_alg(&zxc_alg);
		return ret;
	}

	return ret;
}

static void __exit zxc_mod_fini(void)
{
	crypto_unregister_alg(&zxc_alg);
	crypto_unregister_scomp(&scomp);
}

module_init(zxc_mod_init);
module_exit(zxc_mod_fini);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("ZXC Compression Algorithm");
MODULE_ALIAS_CRYPTO("zxc");
