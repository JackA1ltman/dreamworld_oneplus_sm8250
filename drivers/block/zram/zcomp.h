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

struct zcomp *zcomp_create(const char *comp);
void zcomp_destroy(struct zcomp *comp);

struct zcomp_strm *zcomp_stream_get(struct zcomp *comp);
void zcomp_stream_put(struct zcomp *comp);

int zcomp_compress(struct zcomp_strm *zstrm,
		const void *src, unsigned int *dst_len);

int zcomp_decompress(struct zcomp_strm *zstrm,
		const void *src, unsigned int src_len, void *dst);

bool zcomp_set_max_streams(struct zcomp *comp, int num_strm);
#endif /* _ZCOMP_H_ */
