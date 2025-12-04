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

/**
 * SECTION:element-win32ipcvideosrc
 * @title: win32ipcvideosrc
 * @short_description: Windows shared memory video source
 *
 * win32ipcvideosrc receives raw video frames from win32ipcvideosink
 * and outputs the received video frames
 *
 * ## Example launch line
 * ```
 * gst-launch-1.0 win32ipcvideosrc ! queue ! videoconvert ! d3d11videosink
 * ```
 *
 * Since: 1.22
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstwin32ipcvideosrc.h"
#include "gstwin32ipc.h"
#include <string>
#include <mutex>

GST_DEBUG_CATEGORY_STATIC (gst_win32_ipc_video_src_debug);
#define GST_CAT_DEFAULT gst_win32_ipc_video_src_debug

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE (GST_VIDEO_FORMATS_ALL)));

#define DEFAULT_PIPE_NAME "\\\\.\\pipe\\gst.win32.ipc.video"
#define DEFAULT_LEAKY_TYPE GST_WIN32_IPC_LEAKY_DOWNSTREAM

struct _GstWin32IpcVideoSrc
{
  GstWin32IpcBaseSrc parent;
};

static GstCaps *gst_win32_ipc_video_src_fixate (GstBaseSrc * src,
    GstCaps * caps);

#define gst_win32_ipc_video_src_parent_class parent_class
G_DEFINE_TYPE (GstWin32IpcVideoSrc,
    gst_win32_ipc_video_src, GST_TYPE_WIN32_IPC_BASE_SRC);

static void
gst_win32_ipc_video_src_class_init (GstWin32IpcVideoSrcClass * klass)
{
  auto element_class = GST_ELEMENT_CLASS (klass);
  auto src_class = GST_BASE_SRC_CLASS (klass);

  gst_element_class_set_static_metadata (element_class,
      "Win32 IPC Video Source", "Source/Video",
      "Receive video frames from the win32ipcvideosink",
      "Seungha Yang <seungha@centricular.com>");
  gst_element_class_add_static_pad_template (element_class, &src_template);

  src_class->fixate = GST_DEBUG_FUNCPTR (gst_win32_ipc_video_src_fixate);

  GST_DEBUG_CATEGORY_INIT (gst_win32_ipc_video_src_debug, "win32ipcvideosrc",
      0, "win32ipcvideosrc");
}

static void
gst_win32_ipc_video_src_init (GstWin32IpcVideoSrc * self)
{
  g_object_set (self, "pipe-name", DEFAULT_PIPE_NAME,
      "leaky-type", DEFAULT_LEAKY_TYPE, nullptr);
}

static GstCaps *
gst_win32_ipc_video_src_fixate (GstBaseSrc * src, GstCaps * caps)
{
  /* We don't negotiate with server. In here, we do fixate resolution to
   * 320 x 240 (same as default of videotestsrc) which makes a little more
   * sense than 1x1 */
  caps = gst_caps_make_writable (caps);

  for (guint i = 0; i < gst_caps_get_size (caps); i++) {
    GstStructure *s = gst_caps_get_structure (caps, i);

    gst_structure_fixate_field_nearest_int (s, "width", 320);
    gst_structure_fixate_field_nearest_int (s, "height", 240);
  }

  return gst_caps_fixate (caps);
}
