// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/cpuhotplug.h>
#include <linux/vmalloc.h>

#include "zcomp.h"

#include "backend_lzo.h"
#include "backend_lzorle.h"
#include "backend_lz4.h"
#include "backend_lz4hc.h"
#include "backend_zstd.h"
#include "backend_deflate.h"
#include "backend_842.h"

static const struct zcomp_ops *backends[] = {
#if IS_ENABLED(CONFIG_ZRAM_BACKEND_LZO)
	&backend_lzorle,
	&backend_lzo,
#endif
#if IS_ENABLED(CONFIG_ZRAM_BACKEND_LZ4)
	&backend_lz4,
#endif
#if IS_ENABLED(CONFIG_ZRAM_BACKEND_LZ4HC)
	&backend_lz4hc,
#endif
#if IS_ENABLED(CONFIG_ZRAM_BACKEND_ZSTD)
	&backend_zstd,
#endif
#if IS_ENABLED(CONFIG_ZRAM_BACKEND_DEFLATE)
	&backend_deflate,
#endif
#if IS_ENABLED(CONFIG_ZRAM_BACKEND_842)
	&backend_842,
#endif
	NULL
};

static void zcomp_strm_free(struct zcomp_strm *zstrm)
{
	if (!zstrm)
		return;

	zstrm->comp->ops->destroy_ctx(&zstrm->ctx);
	vfree(zstrm->local_copy);
	vfree(zstrm->buffer);
	zstrm->local_copy = NULL;
	zstrm->buffer = NULL;
	kfree(zstrm);
}

static struct zcomp_strm *zcomp_strm_alloc(struct zcomp *comp)
{
	struct zcomp_strm *zstrm = kmalloc(sizeof(*zstrm), GFP_KERNEL);
	int ret;

	if (!zstrm)
		return NULL;

	zstrm->comp = comp;
	ret = comp->ops->create_ctx(&comp->params, &zstrm->ctx);
	if (ret) {
		kfree(zstrm);
		return NULL;
	}

	zstrm->local_copy = vzalloc(PAGE_SIZE);
	zstrm->buffer = vzalloc(2 * PAGE_SIZE);
	if (!zstrm->buffer || !zstrm->local_copy) {
		zcomp_strm_free(zstrm);
		zstrm = NULL;
	}
	return zstrm;
}

static const struct zcomp_ops *lookup_backend_ops(const char *comp)
{
	int i = 0;

	while (backends[i]) {
		if (sysfs_streq(comp, backends[i]->name))
			break;
		i++;
	}
	return backends[i];
}

bool zcomp_available_algorithm(const char *comp)
{
	return lookup_backend_ops(comp) != NULL;
}

ssize_t zcomp_available_show(const char *comp, char *buf)
{
	ssize_t sz = 0;
	int i;

	for (i = 0; i < ARRAY_SIZE(backends) - 1; i++) {
		if (!strcmp(comp, backends[i]->name)) {
			sz += scnprintf(buf + sz, PAGE_SIZE - sz - 2,
					"[%s] ", backends[i]->name);
		} else {
			sz += scnprintf(buf + sz, PAGE_SIZE - sz - 2,
					"%s ", backends[i]->name);
		}
	}

	sz += scnprintf(buf + sz, PAGE_SIZE - sz, "\n");
	return sz;
}

struct zcomp_strm *zcomp_stream_get(struct zcomp *comp)
{
	return *get_cpu_ptr(comp->stream);
}

void __zcomp_stream_put_comp(struct zcomp *comp)
{
	put_cpu_ptr(comp->stream);
}

int __zcomp_compress(struct zcomp_strm *zstrm,
		     const void *src, unsigned int *dst_len)
{
	struct zcomp_req req = {
		.src = src,
		.dst = zstrm->buffer,
		.src_len = PAGE_SIZE,
		.dst_len = 2 * PAGE_SIZE,
	};
	int ret;

	ret = zstrm->comp->ops->compress(&zstrm->comp->params, &zstrm->ctx,
					 &req);
	if (!ret)
		*dst_len = req.dst_len;
	return ret;
}

int __zcomp_decompress(struct zcomp_strm *zstrm,
		       const void *src, unsigned int src_len, void *dst)
{
	struct zcomp_req req = {
		.src = src,
		.dst = dst,
		.src_len = src_len,
		.dst_len = PAGE_SIZE,
	};

	return zstrm->comp->ops->decompress(&zstrm->comp->params, &zstrm->ctx,
					    &req);
}

int zcomp_cpu_up_prepare(unsigned int cpu, struct hlist_node *node)
{
	struct zcomp *comp = hlist_entry(node, struct zcomp, node);
	struct zcomp_strm *zstrm;

	if (WARN_ON(*per_cpu_ptr(comp->stream, cpu)))
		return 0;

	zstrm = zcomp_strm_alloc(comp);
	if (IS_ERR_OR_NULL(zstrm)) {
		pr_err("Can't allocate a compression stream\n");
		return -ENOMEM;
	}
	*per_cpu_ptr(comp->stream, cpu) = zstrm;
	return 0;
}

int zcomp_cpu_dead(unsigned int cpu, struct hlist_node *node)
{
	struct zcomp *comp = hlist_entry(node, struct zcomp, node);
	struct zcomp_strm *zstrm;

	zstrm = *per_cpu_ptr(comp->stream, cpu);
	if (!IS_ERR_OR_NULL(zstrm))
		zcomp_strm_free(zstrm);
	*per_cpu_ptr(comp->stream, cpu) = NULL;
	return 0;
}

static int zcomp_init(struct zcomp *comp)
{
	int ret;

	comp->stream = alloc_percpu(struct zcomp_strm *);
	if (!comp->stream)
		return -ENOMEM;

	ret = comp->ops->setup_params(&comp->params);
	if (ret)
		goto cleanup;

	ret = cpuhp_state_add_instance(CPUHP_ZCOMP_PREPARE, &comp->node);
	if (ret < 0)
		goto cleanup;
	return 0;

cleanup:
	comp->ops->release_params(&comp->params);
	free_percpu(comp->stream);
	return ret;
}

void zcomp_destroy(struct zcomp *comp)
{
	cpuhp_state_remove_instance(CPUHP_ZCOMP_PREPARE, &comp->node);
	comp->ops->release_params(&comp->params);
	free_percpu(comp->stream);
	kfree(comp);
}

struct zcomp *__zcomp_create(const char *compress,
			     const struct zcomp_params *params)
{
	struct zcomp *comp;
	int error;

	comp = kzalloc(sizeof(struct zcomp), GFP_KERNEL);
	if (!comp)
		return ERR_PTR(-ENOMEM);

	comp->name = compress;
	if (params)
		comp->params = *params;
	else
		comp->params.level = ZCOMP_PARAM_NO_LEVEL;
	comp->ops = lookup_backend_ops(compress);
	if (!comp->ops) {
		kfree(comp);
		return ERR_PTR(-EINVAL);
	}

	error = zcomp_init(comp);
	if (error) {
		kfree(comp);
		return ERR_PTR(error);
	}
	return comp;
}

bool zcomp_set_max_streams(struct zcomp *comp, int num_strm)
{
	return true;
}
