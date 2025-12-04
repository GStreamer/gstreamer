/* GStreamer
 * Copyright (C) 2022 Seungha Yang <seungha@centricular.com>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstwin32ipcsrc.h"
#include "gstwin32ipc.h"
#include <string>
#include <mutex>

GST_DEBUG_CATEGORY_STATIC (gst_win32_ipc_src_debug);
#define GST_CAT_DEFAULT gst_win32_ipc_src_debug

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

struct _GstWin32IpcSrc
{
  GstWin32IpcBaseSrc parent;
};

#define gst_win32_ipc_src_parent_class parent_class
G_DEFINE_TYPE (GstWin32IpcSrc, gst_win32_ipc_src, GST_TYPE_WIN32_IPC_BASE_SRC);

static void
gst_win32_ipc_src_class_init (GstWin32IpcSrcClass * klass)
{
  auto element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_set_static_metadata (element_class,
      "Win32 IPC Source", "Source/Generic", "Windows shared memory source",
      "Seungha Yang <seungha@centricular.com>");
  gst_element_class_add_static_pad_template (element_class, &src_template);

  GST_DEBUG_CATEGORY_INIT (gst_win32_ipc_src_debug, "win32ipcsrc",
      0, "win32ipcsrc");
}

static void
gst_win32_ipc_src_init (GstWin32IpcSrc * self)
{
}
