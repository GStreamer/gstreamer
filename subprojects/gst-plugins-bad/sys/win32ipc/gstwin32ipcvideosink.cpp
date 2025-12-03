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
#include "gstwin32ipcbufferpool.h"
#include "gstwin32ipcmemory.h"
#include "gstwin32ipcserver.h"
#include "gstwin32ipc.h"
#include <string>
#include <string.h>
#include <mutex>

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
  PROP_LEAKY_TYPE,
  PROP_MAX_BUFFERS,
  PROP_CURRENT_LEVEL_BUFFERS,
};

#define DEFAULT_PIPE_NAME "\\\\.\\pipe\\gst.win32.ipc.video"
#define DEFAULT_MAX_BUFFERS 2
#define DEFAULT_LEAKY_TYPE GST_WIN32_IPC_LEAKY_DOWNSTREAM

/* *INDENT-OFF* */
struct GstWin32IpcVideoSinkPrivate
{
  GstWin32IpcVideoSinkPrivate ()
  {
    meta = g_byte_array_new ();
    pipe_name = g_strdup (DEFAULT_PIPE_NAME);

    gst_video_info_init (&info);
  }

  ~GstWin32IpcVideoSinkPrivate ()
  {
    reset ();

    g_byte_array_unref (meta);
    g_free (pipe_name);
  }

  void reset ()
  {
    gst_clear_caps (&caps);
    gst_clear_object (&server);

    if (fallback_pool)
      gst_buffer_pool_set_active (fallback_pool, FALSE);
    gst_clear_object (&fallback_pool);
  }

  std::mutex lock;

  GstWin32IpcServer *server = nullptr;
  GstCaps *caps = nullptr;
  GstVideoInfo info;
  GByteArray *meta = nullptr;
  GstBufferPool *fallback_pool = nullptr;

  /* properties */
  gchar *pipe_name;
  guint64 max_buffers = DEFAULT_MAX_BUFFERS;
  GstWin32IpcLeakyType leaky = DEFAULT_LEAKY_TYPE;
};
/* *INDENT-ON* */

struct _GstWin32IpcVideoSink
{
  GstBaseSink parent;

  GstWin32IpcVideoSinkPrivate *priv;
};

static void gst_win32_ipc_video_sink_finalize (GObject * object);
static void gst_win32_ipc_video_sink_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_win32_video_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstClock *gst_win32_ipc_video_sink_provide_clock (GstElement * elem);

static gboolean gst_win32_ipc_video_sink_start (GstBaseSink * sink);
static gboolean gst_win32_ipc_video_sink_stop (GstBaseSink * sink);
static gboolean gst_win32_ipc_video_sink_unlock (GstBaseSink * sink);
static gboolean gst_win32_ipc_video_sink_unlock_stop (GstBaseSink * sink);
static gboolean gst_win32_ipc_video_sink_set_caps (GstBaseSink * sink,
    GstCaps * caps);
static void gst_win32_ipc_video_sink_get_time (GstBaseSink * sink,
    GstBuffer * buf, GstClockTime * start, GstClockTime * end);
static gboolean gst_win32_ipc_video_sink_propose_allocation (GstBaseSink * sink,
    GstQuery * query);
static GstFlowReturn gst_win32_ipc_video_sink_render (GstBaseSink * sink,
    GstBuffer * buf);

#define gst_win32_ipc_video_sink_parent_class parent_class
G_DEFINE_TYPE (GstWin32IpcVideoSink, gst_win32_ipc_video_sink,
    GST_TYPE_BASE_SINK);

static void
gst_win32_ipc_video_sink_class_init (GstWin32IpcVideoSinkClass * klass)
{
  auto object_class = G_OBJECT_CLASS (klass);
  auto element_class = GST_ELEMENT_CLASS (klass);
  auto sink_class = GST_BASE_SINK_CLASS (klass);

  object_class->finalize = gst_win32_ipc_video_sink_finalize;
  object_class->set_property = gst_win32_ipc_video_sink_set_property;
  object_class->get_property = gst_win32_video_sink_get_property;

  g_object_class_install_property (object_class, PROP_PIPE_NAME,
      g_param_spec_string ("pipe-name", "Pipe Name",
          "The name of Win32 named pipe to communicate with clients. "
          "Validation of the pipe name is caller's responsibility",
          DEFAULT_PIPE_NAME, (GParamFlags) (G_PARAM_READWRITE |
              G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY)));

  g_object_class_install_property (object_class, PROP_LEAKY_TYPE,
      g_param_spec_enum ("leaky-type", "Leaky Type",
          "Whether to drop buffers once the internal queue is full",
          GST_TYPE_WIN32_IPC_LEAKY_TYPE, DEFAULT_LEAKY_TYPE,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (object_class, PROP_MAX_BUFFERS,
      g_param_spec_uint64 ("max-buffers", "Max Buffers",
          "Maximum number of buffers in queue (0=unlimited)",
          0, G_MAXUINT64, DEFAULT_MAX_BUFFERS,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (object_class, PROP_CURRENT_LEVEL_BUFFERS,
      g_param_spec_uint64 ("current-level-buffers", "Current Level Buffers",
          "The number of currently queued buffers",
          0, G_MAXUINT64, 0,
          (GParamFlags) (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS)));

  gst_element_class_set_static_metadata (element_class,
      "Win32 IPC Video Sink", "Sink/Video",
      "Send video frames to win32ipcvideosrc elements",
      "Seungha Yang <seungha@centricular.com>");
  gst_element_class_add_static_pad_template (element_class, &sink_template);

  element_class->provide_clock =
      GST_DEBUG_FUNCPTR (gst_win32_ipc_video_sink_provide_clock);

  sink_class->start = GST_DEBUG_FUNCPTR (gst_win32_ipc_video_sink_start);
  sink_class->stop = GST_DEBUG_FUNCPTR (gst_win32_ipc_video_sink_stop);
  sink_class->unlock = GST_DEBUG_FUNCPTR (gst_win32_ipc_video_sink_unlock);
  sink_class->unlock_stop =
      GST_DEBUG_FUNCPTR (gst_win32_ipc_video_sink_unlock_stop);
  sink_class->set_caps = GST_DEBUG_FUNCPTR (gst_win32_ipc_video_sink_set_caps);
  sink_class->propose_allocation =
      GST_DEBUG_FUNCPTR (gst_win32_ipc_video_sink_propose_allocation);
  sink_class->get_times = GST_DEBUG_FUNCPTR (gst_win32_ipc_video_sink_get_time);
  sink_class->render = GST_DEBUG_FUNCPTR (gst_win32_ipc_video_sink_render);

  GST_DEBUG_CATEGORY_INIT (gst_win32_ipc_video_sink_debug, "win32ipcvideosink",
      0, "win32ipcvideosink");
}

static void
gst_win32_ipc_video_sink_init (GstWin32IpcVideoSink * self)
{
  self->priv = new GstWin32IpcVideoSinkPrivate ();

  GST_OBJECT_FLAG_SET (self, GST_ELEMENT_FLAG_PROVIDE_CLOCK);
  GST_OBJECT_FLAG_SET (self, GST_ELEMENT_FLAG_REQUIRE_CLOCK);
}

static void
gst_win32_ipc_video_sink_finalize (GObject * object)
{
  auto self = GST_WIN32_IPC_VIDEO_SINK (object);

  delete self->priv;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_win32_ipc_video_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  auto self = GST_WIN32_IPC_VIDEO_SINK (object);
  auto priv = self->priv;

  std::lock_guard < std::mutex > lk (priv->lock);

  switch (prop_id) {
    case PROP_PIPE_NAME:
      g_free (priv->pipe_name);
      priv->pipe_name = g_value_dup_string (value);
      if (!priv->pipe_name)
        priv->pipe_name = g_strdup (DEFAULT_PIPE_NAME);
      break;
    case PROP_LEAKY_TYPE:
      priv->leaky = (GstWin32IpcLeakyType) g_value_get_enum (value);
      if (priv->server)
        gst_win32_ipc_server_set_leaky (priv->server, priv->leaky);
      break;
    case PROP_MAX_BUFFERS:
      priv->max_buffers = g_value_get_uint64 (value);
      if (priv->server) {
        gst_win32_ipc_server_set_max_buffers (priv->server, priv->max_buffers);
      }
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
  auto self = GST_WIN32_IPC_VIDEO_SINK (object);
  auto priv = self->priv;

  std::lock_guard < std::mutex > lk (priv->lock);

  switch (prop_id) {
    case PROP_PIPE_NAME:
      g_value_set_string (value, priv->pipe_name);
      break;
    case PROP_LEAKY_TYPE:
      g_value_set_enum (value, priv->leaky);
      break;
    case PROP_MAX_BUFFERS:
      g_value_set_uint64 (value, priv->max_buffers);
      break;
    case PROP_CURRENT_LEVEL_BUFFERS:
      if (priv->server) {
        auto level =
            gst_win32_ipc_server_get_current_level_buffers (priv->server);
        g_value_set_uint64 (value, level);
      } else {
        g_value_set_uint64 (value, 0);
      }
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
  auto self = GST_WIN32_IPC_VIDEO_SINK (sink);
  auto priv = self->priv;

  GST_DEBUG_OBJECT (self, "Start");

  std::lock_guard < std::mutex > lk (priv->lock);
  priv->server = gst_win32_ipc_server_new (priv->pipe_name,
      priv->max_buffers, priv->leaky);
  if (!priv->server) {
    GST_ERROR_OBJECT (self, "Couldn't create pipe server");
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_win32_ipc_video_sink_stop (GstBaseSink * sink)
{
  auto self = GST_WIN32_IPC_VIDEO_SINK (sink);
  auto priv = self->priv;

  GST_DEBUG_OBJECT (self, "Stop");

  std::lock_guard < std::mutex > lk (priv->lock);
  priv->reset ();

  return TRUE;
}

static gboolean
gst_win32_ipc_video_sink_unlock (GstBaseSink * sink)
{
  auto self = GST_WIN32_IPC_VIDEO_SINK (sink);
  auto priv = self->priv;

  GST_DEBUG_OBJECT (self, "Unlock");

  std::lock_guard < std::mutex > lk (priv->lock);
  if (priv->server)
    gst_win32_ipc_server_set_flushing (priv->server, TRUE);

  return TRUE;
}

static gboolean
gst_win32_ipc_video_sink_unlock_stop (GstBaseSink * sink)
{
  auto self = GST_WIN32_IPC_VIDEO_SINK (sink);
  auto priv = self->priv;

  GST_DEBUG_OBJECT (self, "Unlock stop");

  std::lock_guard < std::mutex > lk (priv->lock);
  if (priv->server)
    gst_win32_ipc_server_set_flushing (priv->server, FALSE);

  return TRUE;
}

static void
gst_win32_ipc_video_sink_get_time (GstBaseSink * sink, GstBuffer * buf,
    GstClockTime * start, GstClockTime * end)
{
  auto self = GST_WIN32_IPC_VIDEO_SINK (sink);
  auto priv = self->priv;

  auto timestamp = GST_BUFFER_PTS (buf);
  if (!GST_CLOCK_TIME_IS_VALID (timestamp))
    timestamp = GST_BUFFER_DTS (buf);

  if (GST_CLOCK_TIME_IS_VALID (timestamp)) {
    *start = timestamp;
    if (GST_BUFFER_DURATION_IS_VALID (buf)) {
      *end = timestamp + GST_BUFFER_DURATION (buf);
    } else if (priv->info.fps_n > 0) {
      *end = timestamp +
          gst_util_uint64_scale_int (GST_SECOND, priv->info.fps_d,
          priv->info.fps_n);
    } else if (sink->segment.rate < 0) {
      *end = timestamp;
    }
  }
}

static gboolean
gst_win32_ipc_video_sink_set_caps (GstBaseSink * sink, GstCaps * caps)
{
  auto self = GST_WIN32_IPC_VIDEO_SINK (sink);
  auto priv = self->priv;

  if (!gst_video_info_from_caps (&priv->info, caps)) {
    GST_WARNING_OBJECT (self, "Invalid caps %" GST_PTR_FORMAT, caps);
    return FALSE;
  }

  gst_caps_replace (&priv->caps, caps);

  if (priv->fallback_pool) {
    gst_buffer_pool_set_active (priv->fallback_pool, FALSE);
    gst_object_unref (priv->fallback_pool);
  }

  priv->fallback_pool = gst_win32_ipc_buffer_pool_new ();
  auto config = gst_buffer_pool_get_config (priv->fallback_pool);
  gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META);
  gst_buffer_pool_config_set_params (config, caps, (guint) priv->info.size,
      0, 0);
  gst_buffer_pool_set_config (priv->fallback_pool, config);
  gst_buffer_pool_set_active (priv->fallback_pool, TRUE);

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
gst_win32_ipc_video_sink_render (GstBaseSink * sink, GstBuffer * buf)
{
  auto self = GST_WIN32_IPC_VIDEO_SINK (sink);
  auto priv = self->priv;

  GstClockTime pts;
  GstClockTime now_qpc;
  GstClockTime buf_pts;
  GstClockTime buffer_clock = GST_CLOCK_TIME_NONE;

  if (!priv->server) {
    GST_ERROR_OBJECT (self, "Pipe server was not configured");
    return GST_FLOW_ERROR;
  }

  pts = now_qpc = gst_util_get_timestamp ();

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

    is_qpc = gst_clock_is_system_monotonic (clock);
    if (!is_qpc) {
      GstClockTime now_gst = gst_clock_get_time (clock);
      GstClockTimeDiff converted = buffer_clock;

      GST_TRACE_OBJECT (self, "Clock is not QPC");

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
      GST_TRACE_OBJECT (self, "Clock is QPC already");
      /* buffer clock is already QPC time */
      pts = buffer_clock;
    }
    gst_object_unref (clock);
  }

  auto mem = gst_buffer_peek_memory (buf, 0);
  GstBuffer *prepared = nullptr;
  if (gst_is_win32_ipc_memory (mem) && gst_buffer_n_memory (buf) == 1) {
    GST_TRACE_OBJECT (self, "Upstream win32 memory");
    prepared = gst_buffer_ref (buf);
  } else {
    gst_buffer_pool_acquire_buffer (priv->fallback_pool, &prepared, nullptr);
    if (!prepared) {
      GST_ERROR_OBJECT (self, "Couldn't acquire fallback buffer");
      return GST_FLOW_ERROR;
    }

    GstVideoFrame src_frame, dst_frame;
    if (!gst_video_frame_map (&src_frame, &priv->info, buf, GST_MAP_READ)) {
      GST_ERROR_OBJECT (self, "Couldn't map input buffer");
      gst_buffer_unref (prepared);
      return GST_FLOW_ERROR;
    }

    if (!gst_video_frame_map (&dst_frame, &priv->info, prepared, GST_MAP_WRITE)) {
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

    gst_buffer_copy_into (prepared, buf, GST_BUFFER_COPY_META, 0, -1);
  }

  g_byte_array_set_size (priv->meta, 0);
  gst_buffer_foreach_meta (buf,[](GstBuffer * prepared, GstMeta ** meta,
          gpointer user_data)->gboolean {
        auto self = GST_WIN32_IPC_VIDEO_SINK (user_data);
        gst_meta_serialize_simple (*meta, self->priv->meta);
        return TRUE;
      }
      , self);

  auto mmem = (GstWin32IpcMemory *) gst_buffer_peek_memory (prepared, 0);
  return gst_win32_ipc_server_send_data (priv->server,
      mmem->mmf, pts, priv->caps, priv->meta, prepared,
      (GDestroyNotify) gst_buffer_unref);
}
