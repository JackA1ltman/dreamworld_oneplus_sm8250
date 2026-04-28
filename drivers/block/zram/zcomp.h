/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef _ZCOMP_H_
#define _ZCOMP_H_

#include <linux/kernel.h>

struct zcomp;

#define ZCOMP_PARAM_NO_LEVEL	INT_MIN

struct zcomp_params {
	void *dict;
	size_t dict_sz;
	s32 level;
	u32 dict_gen;

	void *drv_data;
};

struct zcomp_ctx {
	void *context;
};

struct zcomp_strm {
	struct zcomp *comp;
	void *buffer;
	void *local_copy;
	struct zcomp_ctx ctx;
};

struct zcomp_req {
	const unsigned char *src;
	const size_t src_len;

	unsigned char *dst;
	size_t dst_len;
};

struct zcomp_ops {
	int (*compress)(struct zcomp_params *params, struct zcomp_ctx *ctx,
			struct zcomp_req *req);
	int (*decompress)(struct zcomp_params *params, struct zcomp_ctx *ctx,
			  struct zcomp_req *req);

	int (*create_ctx)(struct zcomp_params *params, struct zcomp_ctx *ctx);
	void (*destroy_ctx)(struct zcomp_ctx *ctx);

	int (*setup_params)(struct zcomp_params *params);
	void (*release_params)(struct zcomp_params *params);

	const char *name;
};

struct zcomp {
	struct zcomp_strm * __percpu *stream;
	const struct zcomp_ops *ops;
	struct zcomp_params params;
	const char *name;
	struct hlist_node node;
};

int zcomp_cpu_up_prepare(unsigned int cpu, struct hlist_node *node);
int zcomp_cpu_dead(unsigned int cpu, struct hlist_node *node);
ssize_t zcomp_available_show(const char *comp, char *buf);
bool zcomp_available_algorithm(const char *comp);

struct zcomp *__zcomp_create(const char *comp,
			     const struct zcomp_params *params);
void zcomp_destroy(struct zcomp *comp);

struct zcomp_strm *zcomp_stream_get(struct zcomp *comp);
void __zcomp_stream_put_comp(struct zcomp *comp);

int __zcomp_compress(struct zcomp_strm *zstrm,
		     const void *src, unsigned int *dst_len);

int __zcomp_decompress(struct zcomp_strm *zstrm,
		       const void *src, unsigned int src_len, void *dst);

static inline struct zcomp *zcomp_create1(const char *comp)
{
	return __zcomp_create(comp, NULL);
}

static inline struct zcomp *zcomp_create2(const char *comp,
					  const struct zcomp_params *params)
{
	return __zcomp_create(comp, params);
}

#define __ZCOMP_CREATE_DISPATCH(_1, _2, NAME, ...) NAME
#define zcomp_create(...) \
	__ZCOMP_CREATE_DISPATCH(__VA_ARGS__, zcomp_create2, zcomp_create1) \
	(__VA_ARGS__)

static inline void __zcomp_stream_put_strm(struct zcomp_strm *zstrm)
{
	__zcomp_stream_put_comp(zstrm->comp);
}

#define zcomp_stream_put(arg) \
	__builtin_choose_expr( \
		__builtin_types_compatible_p(typeof(arg), struct zcomp_strm *), \
		__zcomp_stream_put_strm((struct zcomp_strm *)(arg)), \
		__zcomp_stream_put_comp((struct zcomp *)(arg)))

static inline int zcomp_compress3(struct zcomp_strm *zstrm,
				  const void *src, unsigned int *dst_len)
{
	return __zcomp_compress(zstrm, src, dst_len);
}

static inline int zcomp_compress4(struct zcomp *comp,
				  struct zcomp_strm *zstrm,
				  const void *src, unsigned int *dst_len)
{
	(void)comp;
	return __zcomp_compress(zstrm, src, dst_len);
}

#define __ZCOMP_COMPRESS_DISPATCH(_1, _2, _3, _4, NAME, ...) NAME
#define zcomp_compress(...) \
	__ZCOMP_COMPRESS_DISPATCH(__VA_ARGS__, zcomp_compress4, \
				  zcomp_compress3)(__VA_ARGS__)

static inline int zcomp_decompress4(struct zcomp_strm *zstrm,
				    const void *src, unsigned int src_len,
				    void *dst)
{
	return __zcomp_decompress(zstrm, src, src_len, dst);
}

static inline int zcomp_decompress5(struct zcomp *comp,
				    struct zcomp_strm *zstrm,
				    const void *src, unsigned int src_len,
				    void *dst)
{
	(void)comp;
	return __zcomp_decompress(zstrm, src, src_len, dst);
}

#define __ZCOMP_DECOMPRESS_DISPATCH(_1, _2, _3, _4, _5, NAME, ...) NAME
#define zcomp_decompress(...) \
	__ZCOMP_DECOMPRESS_DISPATCH(__VA_ARGS__, zcomp_decompress5, \
				    zcomp_decompress4)(__VA_ARGS__)

bool zcomp_set_max_streams(struct zcomp *comp, int num_strm);
#endif /* _ZCOMP_H_ */
