// SPDX-License-Identifier: GPL-2.0-only
/*
 * SELinux AVC vendor hook tracepoint definitions.
 *
 * Follows the drivers/android/vendor_hooks.c pattern from Android common
 * kernels: hook tracepoints are defined once here, other compilation units
 * only include <trace/hooks/avc.h> for the declarations.
 */
#define CREATE_TRACE_POINTS
#include <trace/hooks/avc.h>

EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_selinux_avc_insert);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_selinux_avc_node_delete);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_selinux_avc_node_replace);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_selinux_avc_lookup);
