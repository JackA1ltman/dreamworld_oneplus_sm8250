# SPDX-License-Identifier: BSD-3-Clause
#
# ZXC compression library (kernel port of upstream 0.13.3).
#
# Global flags:
#   -DZXC_STATIC_DEFINE      No shared-library export attributes.
#   -DZXC_DISABLE_SIMD       Portable code only: no CPUID/getauxval/intrinsic
#                            includes; forces ZXC_ONLY_DEFAULT in the dispatcher.
#   -DZXC_NO_FRAME_API       Drop the framed-archive API (file header / EOF
#                            block / footer). zram needs only the block API.
#
# zxc_compress.c / zxc_decompress.c / zxc_huffman.c are compiled with
# ZXC_FUNCTION_SUFFIX=_default so their entry points carry the "_default"
# suffix the zxc_dispatch.c trampolines bind to (the same mechanism upstream
# uses for its per-ISA runtime variants). The other TUs must NOT get the
# suffix, hence per-file CFLAGS rather than ccflags.
#
# The kernel builds with -std=gnu89 and C90 declaration checks under -Werror.
# ZXC is C11 code (mixed declarations, for-loop initializers, _Alignas), so
# compile it as C11 and silence the -Wdeclaration-after-statement diagnostic
# instead of rewriting the ported sources. -Wno-gcc-compat keeps clang from
# flagging the loop-declaration extension under the strict default mode.

ccflags-y += -DZXC_STATIC_DEFINE -DZXC_DISABLE_SIMD -DZXC_NO_FRAME_API
ccflags-y += -std=gnu11
ccflags-y += -Wno-declaration-after-statement -Wno-gcc-compat

CFLAGS_zxc_compress.o := -DZXC_FUNCTION_SUFFIX=_default
CFLAGS_zxc_decompress.o := -DZXC_FUNCTION_SUFFIX=_default
CFLAGS_zxc_huffman.o := -DZXC_FUNCTION_SUFFIX=_default

obj-y += zxc_compress.o zxc_decompress.o zxc_common.o zxc_huffman.o \
	 zxc_dispatch.o zxc_pivco_tables.o zxc_exports.o
