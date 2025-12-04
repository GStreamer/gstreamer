/* GStreamer
 * Copyright (C) 2025 Seungha Yang <seungha@centricular.com>
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

#pragma once

#include <gst/gst.h>
#include <gst/base/gstbasesrc.h>

G_BEGIN_DECLS

#define GST_TYPE_WIN32_IPC_BASE_SRC             (gst_win32_ipc_base_src_get_type())
#define GST_WIN32_IPC_BASE_SRC(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_WIN32_IPC_BASE_SRC,GstWin32IpcBaseSrc))
#define GST_WIN32_IPC_BASE_SRC_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_WIN32_IPC_BASE_SRC,GstWin32IpcBaseSrcClass))
#define GST_WIN32_IPC_BASE_SRC_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS((obj), GST_TYPE_WIN32_IPC_BASE_SRC,GstWin32IpcBaseSrcClass))
#define GST_IS_WIN32_IPC_BASE_SRC(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_WIN32_IPC_BASE_SRC))
#define GST_IS_WIN32_IPC_BASE_SRC_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_WIN32_IPC_BASE_SRC))

typedef struct _GstWin32IpcBaseSrc GstWin32IpcBaseSrc;
typedef struct _GstWin32IpcBaseSrcClass GstWin32IpcBaseSrcClass;
typedef struct _GstWin32IpcBaseSrcPrivate GstWin32IpcBaseSrcPrivate;

struct _GstWin32IpcBaseSrc
{
  GstBaseSrc parent;

  GstWin32IpcBaseSrcPrivate *priv;
};

struct _GstWin32IpcBaseSrcClass
{
  GstBaseSrcClass parent_class;
};

GType gst_win32_ipc_base_src_get_type (void);
G_DEFINE_AUTOPTR_CLEANUP_FUNC(GstWin32IpcBaseSrc, gst_object_unref)

G_END_DECLS