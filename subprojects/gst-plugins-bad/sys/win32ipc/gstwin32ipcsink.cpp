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

/**
 * SECTION:element-win32ipcsrc
 * @title: win32ipcsink
 *
 * Receive Windows memory mapped file backed buffers over Windows named pipe from
 * win32ipcsink
 *
 * ## Example launch line
 * ```
 * gst-launch-1.0 win32ipcsrc ! queue ! videoconvert ! d3d12videosink
 * ```
 *
 * Since: 1.28
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstwin32ipcsink.h"
#include "gstwin32ipcbufferpool.h"
#include "gstwin32ipcmemory.h"
#include "gstwin32ipc.h"
#include <string.h>

GST_DEBUG_CATEGORY_STATIC (gst_win32_ipc_sink_debug);
#define GST_CAT_DEFAULT gst_win32_ipc_sink_debug

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

struct _GstWin32IpcSink
{
  GstWin32IpcBaseSink parent;

  GstVideoInfo info;
  GstCaps *caps;
  GstBufferPool *fallback_pool;
  gboolean is_raw_video;
  gsize pool_size;
};

static gboolean gst_win32_ipc_sink_set_caps (GstBaseSink * sink,
    GstCaps * caps);
static gboolean gst_win32_ipc_sink_stop (GstBaseSink * sink);
static void gst_win32_ipc_sink_get_times (GstBaseSink * sink,
    GstBuffer * buf, GstClockTime * start, GstClockTime * end);
static gboolean gst_win32_ipc_sink_propose_allocation (GstBaseSink * sink,
    GstQuery * query);
static GstFlowReturn
gst_win32_ipc_sink_upload (GstWin32IpcBaseSink * sink, GstBuffer * buffer,
    GstBuffer ** uploaded, gsize * size);

#define gst_win32_ipc_sink_parent_class parent_class
G_DEFINE_TYPE (GstWin32IpcSink, gst_win32_ipc_sink,
    GST_TYPE_WIN32_IPC_BASE_SINK);

static void
gst_win32_ipc_sink_class_init (GstWin32IpcSinkClass * klass)
{
  auto element_class = GST_ELEMENT_CLASS (klass);
  auto sink_class = GST_BASE_SINK_CLASS (klass);
  auto win32_class = GST_WIN32_IPC_BASE_SINK_CLASS (klass);

  gst_element_class_set_static_metadata (element_class,
      "Win32 IPC Sink", "Sink/Generic", "Windows shared memory sink",
      "Seungha Yang <seungha@centricular.com>");
  gst_element_class_add_static_pad_template (element_class, &sink_template);

  sink_class->stop = GST_DEBUG_FUNCPTR (gst_win32_ipc_sink_stop);
  sink_class->get_times = GST_DEBUG_FUNCPTR (gst_win32_ipc_sink_get_times);
  sink_class->set_caps = GST_DEBUG_FUNCPTR (gst_win32_ipc_sink_set_caps);
  sink_class->propose_allocation =
      GST_DEBUG_FUNCPTR (gst_win32_ipc_sink_propose_allocation);
  win32_class->upload = GST_DEBUG_FUNCPTR (gst_win32_ipc_sink_upload);

  GST_DEBUG_CATEGORY_INIT (gst_win32_ipc_sink_debug, "win32ipcsink",
      0, "win32ipcsink");
}

static void
gst_win32_ipc_sink_init (GstWin32IpcSink * self)
{
}

static gboolean
gst_win32_ipc_sink_stop (GstBaseSink * sink)
{
  auto self = GST_WIN32_IPC_SINK (sink);

  GST_DEBUG_OBJECT (self, "Stop");
  if (self->fallback_pool) {
    gst_clear_caps (&self->caps);
    gst_buffer_pool_set_active (self->fallback_pool, FALSE);
    gst_clear_object (&self->fallback_pool);
    self->pool_size = 0;
  }

  return GST_BASE_SINK_CLASS (parent_class)->stop (sink);
}

static void
gst_win32_ipc_sink_get_times (GstBaseSink * sink, GstBuffer * buf,
    GstClockTime * start, GstClockTime * end)
{
  auto self = GST_WIN32_IPC_SINK (sink);
  if (!self->is_raw_video) {
    GST_BASE_SINK_CLASS (parent_class)->get_times (sink, buf, start, end);
    return;
  }

  auto timestamp = GST_BUFFER_PTS (buf);
  if (!GST_CLOCK_TIME_IS_VALID (timestamp))
    timestamp = GST_BUFFER_DTS (buf);

  if (GST_CLOCK_TIME_IS_VALID (timestamp)) {
    *start = timestamp;
    if (GST_BUFFER_DURATION_IS_VALID (buf)) {
      *end = timestamp + GST_BUFFER_DURATION (buf);
    } else if (self->info.fps_n > 0) {
      *end = timestamp +
          gst_util_uint64_scale_int (GST_SECOND, self->info.fps_d,
          self->info.fps_n);
    } else if (sink->segment.rate < 0) {
      *end = timestamp;
    }
  }
}

static gboolean
gst_win32_ipc_sink_set_caps (GstBaseSink * sink, GstCaps * caps)
{
  auto self = GST_WIN32_IPC_SINK (sink);

  gst_caps_replace (&self->caps, caps);

  auto s = gst_caps_get_structure (caps, 0);
  self->is_raw_video = gst_structure_has_name (s, "video/x-raw");

  if (!self->is_raw_video)
    return GST_BASE_SINK_CLASS (parent_class)->set_caps (sink, caps);

  if (!gst_video_info_from_caps (&self->info, caps)) {
    GST_WARNING_OBJECT (self, "Invalid caps %" GST_PTR_FORMAT, caps);
    return FALSE;
  }

  if (self->fallback_pool) {
    gst_buffer_pool_set_active (self->fallback_pool, FALSE);
    gst_object_unref (self->fallback_pool);
  }

  self->fallback_pool = gst_win32_ipc_buffer_pool_new ();
  auto config = gst_buffer_pool_get_config (self->fallback_pool);
  gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META);
  gst_buffer_pool_config_set_params (config, caps, (guint) self->info.size,
      0, 0);
  gst_buffer_pool_set_config (self->fallback_pool, config);
  gst_buffer_pool_set_active (self->fallback_pool, TRUE);
  self->pool_size = self->info.size;

  return GST_BASE_SINK_CLASS (parent_class)->set_caps (sink, caps);
}

static gboolean
gst_win32_ipc_sink_propose_allocation (GstBaseSink * sink, GstQuery * query)
{
  GstCaps *caps;
  GstBufferPool *pool = nullptr;
  GstVideoInfo info;
  guint size;
  gboolean need_pool;

  gst_query_parse_allocation (query, &caps, &need_pool);
  if (!caps) {
    GST_WARNING_OBJECT (sink, "No caps specified");
    return FALSE;
  }

  auto s = gst_caps_get_structure (caps, 0);
  if (!gst_structure_has_name (s, "video/x-raw"))
    return FALSE;

  if (!gst_video_info_from_caps (&info, caps)) {
    GST_WARNING_OBJECT (sink, "Invalid caps %" GST_PTR_FORMAT, caps);
    return FALSE;
  }

  /* the normal size of a frame */
  size = info.size;
  if (need_pool) {
    GstStructure *config;

    pool = gst_win32_ipc_buffer_pool_new ();
    config = gst_buffer_pool_get_config (pool);
    gst_buffer_pool_config_add_option (config,
        GST_BUFFER_POOL_OPTION_VIDEO_META);

    size = GST_VIDEO_INFO_SIZE (&info);

    gst_buffer_pool_config_set_params (config, caps, (guint) size, 0, 0);

    if (!gst_buffer_pool_set_config (pool, config)) {
      GST_ERROR_OBJECT (pool, "Couldn't set config");
      gst_object_unref (pool);

      return FALSE;
    }
  }

  gst_query_add_allocation_pool (query, pool, size, 0, 0);
  gst_clear_object (&pool);

  gst_query_add_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL);

  return TRUE;
}

static GstFlowReturn
gst_win32_ipc_sink_upload_raw_video (GstWin32IpcSink * self,
    GstBuffer * buf, GstBuffer ** uploaded, gsize * size)
{
  GstBuffer *prepared = nullptr;
  gst_buffer_pool_acquire_buffer (self->fallback_pool, &prepared, nullptr);
  if (!prepared) {
    GST_ERROR_OBJECT (self, "Couldn't acquire fallback buffer");
    return GST_FLOW_ERROR;
  }

  GstVideoFrame src_frame, dst_frame;
  if (!gst_video_frame_map (&src_frame, &self->info, buf, GST_MAP_READ)) {
    GST_ERROR_OBJECT (self, "Couldn't map input buffer");
    gst_buffer_unref (prepared);
    return GST_FLOW_ERROR;
  }

  if (!gst_video_frame_map (&dst_frame, &self->info, prepared, GST_MAP_WRITE)) {
    GST_ERROR_OBJECT (self, "Couldn't map fallback buffer");
    gst_video_frame_unmap (&src_frame);
    gst_buffer_unref (prepared);
    return GST_FLOW_ERROR;
  }

  auto copy_ret = gst_video_frame_copy (&dst_frame, &src_frame);
  gst_video_frame_unmap (&dst_frame);
  gst_video_frame_unmap (&src_frame);

  if (!copy_ret) {
    GST_ERROR_OBJECT (self, "Couldn't copy frame");
    gst_buffer_unref (prepared);
    return GST_FLOW_ERROR;
  }

  gst_buffer_copy_into (prepared, buf, GST_BUFFER_COPY_METADATA, 0, -1);
  *uploaded = prepared;
  *size = gst_buffer_get_size (prepared);
  return GST_FLOW_OK;
}

static GstFlowReturn
gst_win32_ipc_sink_upload (GstWin32IpcBaseSink * sink, GstBuffer * buf,
    GstBuffer ** uploaded, gsize * size)
{
  auto self = GST_WIN32_IPC_SINK (sink);

  auto mem = gst_buffer_peek_memory (buf, 0);
  if (gst_is_win32_ipc_memory (mem) && gst_buffer_n_memory (buf) == 1) {
    GST_TRACE_OBJECT (self, "Upstream win32 memory");
    *uploaded = gst_buffer_ref (buf);
    *size = gst_buffer_get_size (buf);
    return GST_FLOW_OK;
  }

  if (self->is_raw_video)
    return gst_win32_ipc_sink_upload_raw_video (self, buf, uploaded, size);

  auto buf_size = gst_buffer_get_size (buf);
  if (self->fallback_pool) {
    if (self->pool_size < buf_size) {
      gst_buffer_pool_set_active (self->fallback_pool, FALSE);
      gst_clear_object (&self->fallback_pool);
    }
  }

  if (!self->fallback_pool) {
    self->fallback_pool = gst_win32_ipc_buffer_pool_new ();
    self->pool_size = buf_size + 1024;
    auto config = gst_buffer_pool_get_config (self->fallback_pool);
    gst_buffer_pool_config_set_params (config, self->caps, self->pool_size,
        0, 0);
    gst_buffer_pool_set_config (self->fallback_pool, config);
    gst_buffer_pool_set_active (self->fallback_pool, TRUE);
  }

  GstBuffer *prepared = nullptr;
  gst_buffer_pool_acquire_buffer (self->fallback_pool, &prepared, nullptr);
  if (!prepared) {
    GST_ERROR_OBJECT (self, "Couldn't acquire fallback buffer");
    return GST_FLOW_ERROR;
  }

  GstMapInfo src_info, dst_info;
  if (!gst_buffer_map (buf, &src_info, GST_MAP_READ)) {
    GST_ERROR_OBJECT (self, "Couldn't map input buffer");
    gst_buffer_unref (prepared);
    return GST_FLOW_ERROR;
  }

  if (!gst_buffer_map (prepared, &dst_info, GST_MAP_WRITE)) {
    GST_ERROR_OBJECT (self, "Couldn't map output buffer");
    gst_buffer_unmap (buf, &src_info);
    gst_buffer_unref (prepared);
    return GST_FLOW_ERROR;
  }

  memcpy (dst_info.data, src_info.data, src_info.size);
  gst_buffer_unmap (buf, &src_info);
  gst_buffer_unmap (prepared, &dst_info);

  gst_buffer_copy_into (prepared, buf, GST_BUFFER_COPY_METADATA, 0, -1);
  *uploaded = prepared;
  *size = buf_size;

  return GST_FLOW_OK;
}
