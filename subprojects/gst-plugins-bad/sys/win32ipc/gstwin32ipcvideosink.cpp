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
 * SECTION:element-win32ipcvideosink
 * @title: win32ipcvideosink
 * @short_description: Windows shared memory video sink
 *
 * win32ipcvideosink provides raw video memory to connected win32ipcvideossrc
 * elements
 *
 * ## Example launch line
 * ```
 * gst-launch-1.0 videotestsrc ! queue ! win32ipcvideosink
 * ```
 *
 * Since: 1.22
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstwin32ipcvideosink.h"
#include "gstwin32ipcutils.h"
#include "protocol/win32ipcpipeserver.h"
#include <string>
#include <string.h>

GST_DEBUG_CATEGORY_STATIC (gst_win32_ipc_video_sink_debug);
#define GST_CAT_DEFAULT gst_win32_ipc_video_sink_debug

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE (GST_VIDEO_FORMATS_ALL)));

enum
{
  PROP_0,
  PROP_PIPE_NAME,
};

#define DEFAULT_PIPE_NAME "\\\\.\\pipe\\gst.win32.ipc.video"

struct _GstWin32IpcVideoSink
{
  GstBaseSink parent;

  GstVideoInfo info;
  Win32IpcPipeServer *pipe;
  gchar *mmf_prefix;
  guint64 seq_num;
  LARGE_INTEGER frequency;

  Win32IpcMmf *mmf;
  Win32IpcVideoInfo minfo;

  /* properties */
  gchar *pipe_name;
};

static void gst_win32_ipc_video_sink_finalize (GObject * object);
static void gst_win32_ipc_video_sink_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_win32_video_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstClock *gst_win32_ipc_video_sink_provide_clock (GstElement * elem);

static gboolean gst_win32_ipc_video_sink_start (GstBaseSink * sink);
static gboolean gst_win32_ipc_video_sink_stop (GstBaseSink * sink);
static gboolean gst_win32_ipc_video_sink_unlock_stop (GstBaseSink * sink);
static gboolean gst_win32_ipc_video_sink_set_caps (GstBaseSink * sink,
    GstCaps * caps);
static void gst_win32_ipc_video_sink_get_time (GstBaseSink * sink,
    GstBuffer * buf, GstClockTime * start, GstClockTime * end);
static gboolean gst_win32_ipc_video_sink_propose_allocation (GstBaseSink * sink,
    GstQuery * query);
static GstFlowReturn gst_win32_ipc_video_sink_prepare (GstBaseSink * sink,
    GstBuffer * buf);
static GstFlowReturn gst_win32_ipc_video_sink_render (GstBaseSink * sink,
    GstBuffer * buf);

#define gst_win32_ipc_video_sink_parent_class parent_class
G_DEFINE_TYPE (GstWin32IpcVideoSink, gst_win32_ipc_video_sink,
    GST_TYPE_BASE_SINK);

static void
gst_win32_ipc_video_sink_class_init (GstWin32IpcVideoSinkClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstBaseSinkClass *sink_class = GST_BASE_SINK_CLASS (klass);

  object_class->finalize = gst_win32_ipc_video_sink_finalize;
  object_class->set_property = gst_win32_ipc_video_sink_set_property;
  object_class->get_property = gst_win32_video_sink_get_property;

  g_object_class_install_property (object_class, PROP_PIPE_NAME,
      g_param_spec_string ("pipe-name", "Pipe Name",
          "The name of Win32 named pipe to communicate with clients. "
          "Validation of the pipe name is caller's responsibility",
          DEFAULT_PIPE_NAME, (GParamFlags) (G_PARAM_READWRITE |
              G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY)));

  gst_element_class_set_static_metadata (element_class,
      "Win32 IPC Video Sink", "Sink/Video",
      "Send video frames to win32ipcvideosrc elements",
      "Seungha Yang <seungha@centricular.com>");
  gst_element_class_add_static_pad_template (element_class, &sink_template);

  element_class->provide_clock =
      GST_DEBUG_FUNCPTR (gst_win32_ipc_video_sink_provide_clock);

  sink_class->start = GST_DEBUG_FUNCPTR (gst_win32_ipc_video_sink_start);
  sink_class->stop = GST_DEBUG_FUNCPTR (gst_win32_ipc_video_sink_stop);
  sink_class->unlock_stop =
      GST_DEBUG_FUNCPTR (gst_win32_ipc_video_sink_unlock_stop);
  sink_class->set_caps = GST_DEBUG_FUNCPTR (gst_win32_ipc_video_sink_set_caps);
  sink_class->propose_allocation =
      GST_DEBUG_FUNCPTR (gst_win32_ipc_video_sink_propose_allocation);
  sink_class->get_times = GST_DEBUG_FUNCPTR (gst_win32_ipc_video_sink_get_time);
  sink_class->prepare = GST_DEBUG_FUNCPTR (gst_win32_ipc_video_sink_prepare);
  sink_class->render = GST_DEBUG_FUNCPTR (gst_win32_ipc_video_sink_render);

  GST_DEBUG_CATEGORY_INIT (gst_win32_ipc_video_sink_debug, "win32ipcvideosink",
      0, "win32ipcvideosink");
}

static void
gst_win32_ipc_video_sink_init (GstWin32IpcVideoSink * self)
{
  self->pipe_name = g_strdup (DEFAULT_PIPE_NAME);
  QueryPerformanceFrequency (&self->frequency);

  GST_OBJECT_FLAG_SET (self, GST_ELEMENT_FLAG_PROVIDE_CLOCK);
  GST_OBJECT_FLAG_SET (self, GST_ELEMENT_FLAG_REQUIRE_CLOCK);
}

static void
gst_win32_ipc_video_sink_finalize (GObject * object)
{
  GstWin32IpcVideoSink *self = GST_WIN32_IPC_VIDEO_SINK (object);

  g_free (self->pipe_name);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_win32_ipc_video_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstWin32IpcVideoSink *self = GST_WIN32_IPC_VIDEO_SINK (object);

  switch (prop_id) {
    case PROP_PIPE_NAME:
      GST_OBJECT_LOCK (self);
      g_free (self->pipe_name);
      self->pipe_name = g_value_dup_string (value);
      if (!self->pipe_name)
        self->pipe_name = g_strdup (DEFAULT_PIPE_NAME);
      GST_OBJECT_UNLOCK (self);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_win32_video_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstWin32IpcVideoSink *self = GST_WIN32_IPC_VIDEO_SINK (object);

  switch (prop_id) {
    case PROP_PIPE_NAME:
      GST_OBJECT_LOCK (self);
      g_value_set_string (value, self->pipe_name);
      GST_OBJECT_UNLOCK (self);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstClock *
gst_win32_ipc_video_sink_provide_clock (GstElement * elem)
{
  return gst_system_clock_obtain ();
}

static gboolean
gst_win32_ipc_video_sink_start (GstBaseSink * sink)
{
  GstWin32IpcVideoSink *self = GST_WIN32_IPC_VIDEO_SINK (sink);

  GST_DEBUG_OBJECT (self, "Start");

  self->pipe = win32_ipc_pipe_server_new (self->pipe_name);
  if (!self->pipe) {
    GST_ERROR_OBJECT (self, "Couldn't create pipe server");
    return FALSE;
  }

  self->mmf_prefix = gst_win32_ipc_get_mmf_prefix ();
  self->seq_num = 0;

  return TRUE;
}

static gboolean
gst_win32_ipc_video_sink_stop (GstBaseSink * sink)
{
  GstWin32IpcVideoSink *self = GST_WIN32_IPC_VIDEO_SINK (sink);

  GST_DEBUG_OBJECT (self, "Stop");

  g_clear_pointer (&self->pipe, win32_ipc_pipe_server_unref);
  g_clear_pointer (&self->mmf_prefix, g_free);
  g_clear_pointer (&self->mmf, win32_ipc_mmf_unref);

  return TRUE;
}

static gboolean
gst_win32_ipc_video_sink_unlock_stop (GstBaseSink * sink)
{
  GstWin32IpcVideoSink *self = GST_WIN32_IPC_VIDEO_SINK (sink);

  g_clear_pointer (&self->mmf, win32_ipc_mmf_unref);

  return TRUE;
}

static void
gst_win32_ipc_video_sink_get_time (GstBaseSink * sink, GstBuffer * buf,
    GstClockTime * start, GstClockTime * end)
{
  GstWin32IpcVideoSink *self = GST_WIN32_IPC_VIDEO_SINK (sink);
  GstClockTime timestamp;

  timestamp = GST_BUFFER_PTS (buf);
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
gst_win32_ipc_video_sink_set_caps (GstBaseSink * sink, GstCaps * caps)
{
  GstWin32IpcVideoSink *self = GST_WIN32_IPC_VIDEO_SINK (sink);

  if (!gst_video_info_from_caps (&self->info, caps)) {
    GST_WARNING_OBJECT (self, "Invalid caps");
    return FALSE;
  }

  memset (&self->minfo, 0, sizeof (Win32IpcVideoInfo));
  self->minfo.format =
      (Win32IpcVideoFormat) GST_VIDEO_INFO_FORMAT (&self->info);
  self->minfo.width = GST_VIDEO_INFO_WIDTH (&self->info);
  self->minfo.height = GST_VIDEO_INFO_HEIGHT (&self->info);
  self->minfo.fps_n = self->info.fps_n;
  self->minfo.fps_d = self->info.fps_d;
  self->minfo.par_n = self->info.par_n;
  self->minfo.par_d = self->info.par_d;

  return TRUE;
}

static gboolean
gst_win32_ipc_video_sink_propose_allocation (GstBaseSink * sink,
    GstQuery * query)
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

  if (!gst_video_info_from_caps (&info, caps)) {
    GST_WARNING_OBJECT (sink, "Invalid caps %" GST_PTR_FORMAT, caps);
    return FALSE;
  }

  /* the normal size of a frame */
  size = info.size;
  if (need_pool) {
    GstStructure *config;

    pool = gst_video_buffer_pool_new ();
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
gst_win32_ipc_video_sink_prepare (GstBaseSink * sink, GstBuffer * buf)
{
  GstWin32IpcVideoSink *self = GST_WIN32_IPC_VIDEO_SINK (sink);
  std::string mmf_name;
  GstVideoFrame frame;
  GstMapInfo info;

  g_clear_pointer (&self->mmf, win32_ipc_mmf_unref);

  if (!gst_video_frame_map (&frame, &self->info, buf, GST_MAP_READ)) {
    GST_ERROR_OBJECT (self, "Couldn't map frame");
    return GST_FLOW_ERROR;
  }

  mmf_name = std::string (self->mmf_prefix) + std::to_string (self->seq_num);
  self->seq_num++;

  self->mmf = win32_ipc_mmf_alloc (GST_VIDEO_FRAME_SIZE (&frame),
      mmf_name.c_str ());
  if (!self->mmf) {
    GST_ERROR_OBJECT (self, "Couldn't create memory with name %s",
        mmf_name.c_str ());
    gst_video_frame_unmap (&frame);
    return GST_FLOW_ERROR;
  }

  self->minfo.size = GST_VIDEO_FRAME_SIZE (&frame);
  for (guint i = 0; i < GST_VIDEO_FRAME_N_PLANES (&frame); i++) {
    self->minfo.offset[i] = GST_VIDEO_FRAME_PLANE_OFFSET (&frame, i);
    self->minfo.stride[i] = GST_VIDEO_FRAME_PLANE_STRIDE (&frame, i);
  }
  gst_video_frame_unmap (&frame);

  gst_buffer_map (buf, &info, GST_MAP_READ);
  memcpy (win32_ipc_mmf_get_raw (self->mmf), info.data, self->minfo.size);
  gst_buffer_unmap (buf, &info);

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_win32_ipc_video_sink_render (GstBaseSink * sink, GstBuffer * buf)
{
  GstWin32IpcVideoSink *self = GST_WIN32_IPC_VIDEO_SINK (sink);
  LARGE_INTEGER cur_time;
  GstClockTime pts;
  GstClockTime now_qpc;
  GstClockTime buf_pts;
  GstClockTime buffer_clock = GST_CLOCK_TIME_NONE;

  QueryPerformanceCounter (&cur_time);
  pts = now_qpc = gst_util_uint64_scale (cur_time.QuadPart, GST_SECOND,
      self->frequency.QuadPart);

  buf_pts = GST_BUFFER_PTS (buf);
  if (!GST_CLOCK_TIME_IS_VALID (buf_pts))
    buf_pts = GST_BUFFER_DTS (buf);

  if (GST_CLOCK_TIME_IS_VALID (buf_pts)) {
    buffer_clock = gst_segment_to_running_time (&sink->segment,
        GST_FORMAT_TIME, buf_pts) +
        GST_ELEMENT_CAST (sink)->base_time + gst_base_sink_get_latency (sink);
  }

  if (GST_CLOCK_TIME_IS_VALID (buffer_clock)) {
    GstClock *clock = gst_element_get_clock (GST_ELEMENT_CAST (sink));
    gboolean is_qpc = TRUE;

    is_qpc = gst_win32_ipc_clock_is_qpc (clock);
    if (!is_qpc) {
      GstClockTime now_gst = gst_clock_get_time (clock);
      GstClockTimeDiff converted = buffer_clock;

      GST_LOG_OBJECT (self, "Clock is not QPC");

      converted -= now_gst;
      converted += now_qpc;

      if (converted < 0) {
        /* Shouldn't happen */
        GST_WARNING_OBJECT (self, "Negative buffer clock");
        pts = 0;
      } else {
        pts = converted;
      }
    } else {
      GST_LOG_OBJECT (self, "Clock is QPC already");
      /* buffer clock is already QPC time */
      pts = buffer_clock;
    }
    gst_object_unref (clock);
  }

  self->minfo.qpc = pts;

  if (!self->pipe) {
    GST_ERROR_OBJECT (self, "Pipe server was not configured");
    return GST_FLOW_ERROR;
  }

  /* win32_ipc_pipe_server_send_mmf() takes ownership of mmf */
  if (!win32_ipc_pipe_server_send_mmf (self->pipe,
          (Win32IpcMmf *) g_steal_pointer (&self->mmf), &self->minfo)) {
    GST_ERROR_OBJECT (self, "Couldn't send buffer");
    return GST_FLOW_ERROR;
  }

  return GST_FLOW_OK;
}
