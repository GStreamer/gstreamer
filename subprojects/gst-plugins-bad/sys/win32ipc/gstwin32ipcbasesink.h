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
#include <gst/base/gstbasesink.h>

G_BEGIN_DECLS

#define GST_TYPE_WIN32_IPC_BASE_SINK             (gst_win32_ipc_base_sink_get_type())
#define GST_WIN32_IPC_BASE_SINK(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_WIN32_IPC_BASE_SINK,GstWin32IpcBaseSink))
#define GST_WIN32_IPC_BASE_SINK_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_WIN32_IPC_BASE_SINK,GstWin32IpcBaseSinkClass))
#define GST_WIN32_IPC_BASE_SINK_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS((obj), GST_TYPE_WIN32_IPC_BASE_SINK,GstWin32IpcBaseSinkClass))
#define GST_IS_WIN32_IPC_BASE_SINK(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_WIN32_IPC_BASE_SINK))
#define GST_IS_WIN32_IPC_BASE_SINK_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_WIN32_IPC_BASE_SINK))

typedef struct _GstWin32IpcBaseSink GstWin32IpcBaseSink;
typedef struct _GstWin32IpcBaseSinkClass GstWin32IpcBaseSinkClass;
typedef struct _GstWin32IpcBaseSinkPrivate GstWin32IpcBaseSinkPrivate;

struct _GstWin32IpcBaseSink
{
  GstBaseSink parent;

  GstWin32IpcBaseSinkPrivate *priv;
};

struct _GstWin32IpcBaseSinkClass
{
  GstBaseSinkClass parent_class;

  GstFlowReturn (*upload) (GstWin32IpcBaseSink * sink,
                           GstBuffer * buffer,
                           GstBuffer ** uploaded,
                           gsize * size);
};

GType gst_win32_ipc_base_sink_get_type (void);
G_DEFINE_AUTOPTR_CLEANUP_FUNC(GstWin32IpcBaseSink, gst_object_unref)

G_END_DECLS
