/*
 * GStreamer gstreamer-onnxinference
 * Copyright (C) 2026 Nirbheek Chauhan <nirbheek@centricular.com>
 *
 * gstonnx-utils.h
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifndef __GST_ONNX_UTILS_H__
#define __GST_ONNX_UTILS_H__

#ifdef __linux__
#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif
#endif

#include <glib.h>

#ifdef G_OS_WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#define GST_DLOPEN_OPTS (RTLD_NOW | RTLD_LOCAL)
#endif

G_BEGIN_DECLS

gpointer gst_onnx_find_onnxrt (void);

G_END_DECLS

#endif /* __GST_ONNX_UTILS_H__ */
