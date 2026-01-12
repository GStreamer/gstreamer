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


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstwin32ipcbasesrc.h"
#include "gstwin32ipcclient.h"
#include "gstwin32ipc.h"
#include <string>
#include <mutex>

GST_DEBUG_CATEGORY_STATIC (gst_win32_ipc_base_src_debug);
#define GST_CAT_DEFAULT gst_win32_ipc_base_src_debug

enum
{
  PROP_0,
  PROP_PIPE_NAME,
  PROP_PROCESSING_DEADLINE,
  PROP_LEAKY_TYPE,
  PROP_MAX_BUFFERS,
  PROP_CURRENT_LEVEL_BUFFERS,
};

#define DEFAULT_PIPE_NAME "\\\\.\\pipe\\gst.win32.ipc"
#define DEFAULT_PROCESSING_DEADLINE (20 * GST_MSECOND)
#define DEFAULT_MAX_BUFFERS 2
#define DEFAULT_LEAKY_TYPE GST_WIN32_IPC_LEAKY_NONE

/* *INDENT-OFF* */
struct _GstWin32IpcBaseSrcPrivate
{
  _GstWin32IpcBaseSrcPrivate ()
  {
    pipe_name = g_strdup (DEFAULT_PIPE_NAME);
  }

  ~_GstWin32IpcBaseSrcPrivate ()
  {
    g_free (pipe_name);
  }

  GstWin32IpcClient *client = nullptr;
  GstCaps *caps = nullptr;
  std::mutex lock;

  /* properties */
  gchar *pipe_name;
  GstClockTime processing_deadline = DEFAULT_PROCESSING_DEADLINE;
  guint64 max_buffers = DEFAULT_MAX_BUFFERS;
  GstWin32IpcLeakyType leaky = DEFAULT_LEAKY_TYPE;
};
/* *INDENT-ON* */

static void gst_win32_ipc_base_src_finalize (GObject * object);
static void gst_win32_ipc_base_src_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_win32_base_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstClock *gst_win32_base_src_provide_clock (GstElement * elem);

static gboolean gst_win32_ipc_base_src_start (GstBaseSrc * src);
static gboolean gst_win32_ipc_base_src_stop (GstBaseSrc * src);
static gboolean gst_win32_ipc_base_src_unlock (GstBaseSrc * src);
static gboolean gst_win32_ipc_base_src_unlock_stop (GstBaseSrc * src);
static gboolean gst_win32_ipc_base_src_query (GstBaseSrc * src,
    GstQuery * query);
static GstCaps *gst_win32_ipc_base_src_get_caps (GstBaseSrc * src,
    GstCaps * filter);
static GstFlowReturn gst_win32_ipc_base_src_create (GstBaseSrc * src,
    guint64 offset, guint size, GstBuffer ** buf);

/**
 * GstWin32IpcBaseSrc:
 *
 * Since: 1.28
 */
#define gst_win32_ipc_base_src_parent_class parent_class
G_DEFINE_ABSTRACT_TYPE (GstWin32IpcBaseSrc,
    gst_win32_ipc_base_src, GST_TYPE_BASE_SRC);

static void
gst_win32_ipc_base_src_class_init (GstWin32IpcBaseSrcClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstBaseSrcClass *src_class = GST_BASE_SRC_CLASS (klass);

  object_class->finalize = gst_win32_ipc_base_src_finalize;
  object_class->set_property = gst_win32_ipc_base_src_set_property;
  object_class->get_property = gst_win32_base_src_get_property;

  g_object_class_install_property (object_class, PROP_PIPE_NAME,
      g_param_spec_string ("pipe-name", "Pipe Name",
          "The name of Win32 named pipe to communicate with server. "
          "Validation of the client name is caller's responsibility",
          DEFAULT_PIPE_NAME, (GParamFlags) (G_PARAM_READWRITE |
              G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY)));
  g_object_class_install_property (object_class, PROP_PROCESSING_DEADLINE,
      g_param_spec_uint64 ("processing-deadline", "Processing deadline",
          "Maximum processing time for a buffer in nanoseconds", 0, G_MAXUINT64,
          DEFAULT_PROCESSING_DEADLINE, (GParamFlags) (G_PARAM_READWRITE |
              G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));

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

  element_class->provide_clock =
      GST_DEBUG_FUNCPTR (gst_win32_base_src_provide_clock);

  src_class->start = GST_DEBUG_FUNCPTR (gst_win32_ipc_base_src_start);
  src_class->stop = GST_DEBUG_FUNCPTR (gst_win32_ipc_base_src_stop);
  src_class->unlock = GST_DEBUG_FUNCPTR (gst_win32_ipc_base_src_unlock);
  src_class->unlock_stop =
      GST_DEBUG_FUNCPTR (gst_win32_ipc_base_src_unlock_stop);
  src_class->query = GST_DEBUG_FUNCPTR (gst_win32_ipc_base_src_query);
  src_class->get_caps = GST_DEBUG_FUNCPTR (gst_win32_ipc_base_src_get_caps);
  src_class->create = GST_DEBUG_FUNCPTR (gst_win32_ipc_base_src_create);

  GST_DEBUG_CATEGORY_INIT (gst_win32_ipc_base_src_debug, "win32ipcbasesrc",
      0, "win32ipcbasesrc");

  gst_type_mark_as_plugin_api (GST_TYPE_WIN32_IPC_BASE_SRC,
      (GstPluginAPIFlags) 0);
  gst_type_mark_as_plugin_api (GST_TYPE_WIN32_IPC_LEAKY_TYPE,
      (GstPluginAPIFlags) 0);
}

static void
gst_win32_ipc_base_src_init (GstWin32IpcBaseSrc * self)
{
  self->priv = new GstWin32IpcBaseSrcPrivate ();

  gst_base_src_set_format (GST_BASE_SRC (self), GST_FORMAT_TIME);
  gst_base_src_set_live (GST_BASE_SRC (self), TRUE);

  GST_OBJECT_FLAG_SET (self, GST_ELEMENT_FLAG_PROVIDE_CLOCK);
  GST_OBJECT_FLAG_SET (self, GST_ELEMENT_FLAG_REQUIRE_CLOCK);
}

static void
gst_win32_ipc_base_src_finalize (GObject * object)
{
  auto self = GST_WIN32_IPC_BASE_SRC (object);

  delete self->priv;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_win32_ipc_base_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  auto self = GST_WIN32_IPC_BASE_SRC (object);
  auto priv = self->priv;

  std::unique_lock < std::mutex > lk (priv->lock);
  switch (prop_id) {
    case PROP_PIPE_NAME:
      g_free (priv->pipe_name);
      priv->pipe_name = g_value_dup_string (value);
      if (!priv->pipe_name)
        priv->pipe_name = g_strdup (DEFAULT_PIPE_NAME);
      break;
    case PROP_PROCESSING_DEADLINE:
    {
      GstClockTime prev_val, new_val;
      prev_val = priv->processing_deadline;
      new_val = g_value_get_uint64 (value);
      priv->processing_deadline = new_val;

      if (prev_val != new_val) {
        GST_DEBUG_OBJECT (self, "Posting latency message");
        lk.unlock ();
        gst_element_post_message (GST_ELEMENT_CAST (self),
            gst_message_new_latency (GST_OBJECT_CAST (self)));
      }
      break;
    }
    case PROP_LEAKY_TYPE:
      priv->leaky = (GstWin32IpcLeakyType) g_value_get_enum (value);
      if (priv->client)
        gst_win32_ipc_client_set_leaky (priv->client, priv->leaky);
      break;
    case PROP_MAX_BUFFERS:
      priv->max_buffers = g_value_get_uint64 (value);
      if (priv->client)
        gst_win32_ipc_client_set_max_buffers (priv->client, priv->max_buffers);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_win32_base_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  auto self = GST_WIN32_IPC_BASE_SRC (object);
  auto priv = self->priv;

  std::lock_guard < std::mutex > lk (priv->lock);

  switch (prop_id) {
    case PROP_PIPE_NAME:
      g_value_set_string (value, priv->pipe_name);
      break;
    case PROP_PROCESSING_DEADLINE:
      g_value_set_uint64 (value, priv->processing_deadline);
      break;
    case PROP_LEAKY_TYPE:
      g_value_set_enum (value, priv->leaky);
      break;
    case PROP_MAX_BUFFERS:
      g_value_set_uint64 (value, priv->max_buffers);
      break;
    case PROP_CURRENT_LEVEL_BUFFERS:
      if (priv->client) {
        auto level =
            gst_win32_ipc_client_get_current_level_buffers (priv->client);
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
gst_win32_base_src_provide_clock (GstElement * elem)
{
  return gst_system_clock_obtain ();
}

static gboolean
gst_win32_ipc_base_src_start (GstBaseSrc * src)
{
  auto self = GST_WIN32_IPC_BASE_SRC (src);
  auto priv = self->priv;

  GST_DEBUG_OBJECT (self, "Start");

  std::lock_guard < std::mutex > lk (priv->lock);
  priv->client = gst_win32_ipc_client_new (priv->pipe_name,
      5, priv->max_buffers, priv->leaky);

  return TRUE;
}

static gboolean
gst_win32_ipc_base_src_stop (GstBaseSrc * src)
{
  auto self = GST_WIN32_IPC_BASE_SRC (src);
  auto priv = self->priv;

  GST_DEBUG_OBJECT (self, "Stop");

  std::lock_guard < std::mutex > lk (priv->lock);
  if (priv->client)
    gst_win32_ipc_client_stop (priv->client);

  gst_clear_object (&priv->client);
  gst_clear_caps (&priv->caps);

  return TRUE;
}

static gboolean
gst_win32_ipc_base_src_unlock (GstBaseSrc * src)
{
  auto self = GST_WIN32_IPC_BASE_SRC (src);
  auto priv = self->priv;

  GST_DEBUG_OBJECT (self, "Unlock");

  std::lock_guard < std::mutex > lk (priv->lock);
  if (priv->client)
    gst_win32_ipc_client_set_flushing (priv->client, true);

  return TRUE;
}

static gboolean
gst_win32_ipc_base_src_unlock_stop (GstBaseSrc * src)
{
  auto self = GST_WIN32_IPC_BASE_SRC (src);
  auto priv = self->priv;

  GST_DEBUG_OBJECT (self, "Unlock stop");

  std::lock_guard < std::mutex > lk (priv->lock);
  if (priv->client)
    gst_win32_ipc_client_set_flushing (priv->client, false);

  return TRUE;
}

static gboolean
gst_win32_ipc_base_src_query (GstBaseSrc * src, GstQuery * query)
{
  auto self = GST_WIN32_IPC_BASE_SRC (src);
  auto priv = self->priv;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_LATENCY:
    {
      GST_OBJECT_LOCK (self);
      if (GST_CLOCK_TIME_IS_VALID (priv->processing_deadline)) {
        gst_query_set_latency (query, TRUE, priv->processing_deadline,
            GST_CLOCK_TIME_NONE);
      } else {
        gst_query_set_latency (query, TRUE, 0, 0);
      }
      GST_OBJECT_UNLOCK (self);
      return TRUE;
    }
    default:
      break;
  }

  return GST_BASE_SRC_CLASS (parent_class)->query (src, query);
}

static GstCaps *
gst_win32_ipc_base_src_get_caps (GstBaseSrc * src, GstCaps * filter)
{
  auto self = GST_WIN32_IPC_BASE_SRC (src);
  auto priv = self->priv;
  GstWin32IpcClient *client = nullptr;
  GstCaps *caps = nullptr;

  GST_DEBUG_OBJECT (self, "Get caps");

  priv->lock.lock ();
  if (priv->caps)
    caps = gst_caps_ref (priv->caps);
  else if (priv->client)
    client = (GstWin32IpcClient *) gst_object_ref (priv->client);
  priv->lock.unlock ();

  if (!caps && client)
    caps = gst_win32_ipc_client_get_caps (priv->client);

  if (!caps)
    caps = gst_pad_get_pad_template_caps (GST_BASE_SRC_PAD (src));

  if (filter) {
    GstCaps *tmp = gst_caps_intersect_full (filter,
        caps, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (caps);
    caps = tmp;
  }

  gst_clear_object (&client);
  GST_DEBUG_OBJECT (self, "Returning caps %" GST_PTR_FORMAT, caps);

  return caps;
}

static GstClockTime
gst_win32_ipc_base_src_get_buffer_time (GstBaseSrc * src,
    GstClockTime base_time, gboolean clock_is_qpc,
    GstClockTime now_qpc, GstClockTime now_gst, GstClockTime timestamp)
{
  if (!GST_CLOCK_TIME_IS_VALID (timestamp) ||
      !GST_CLOCK_TIME_IS_VALID (base_time)) {
    return GST_CLOCK_TIME_NONE;
  }

  if (clock_is_qpc) {
    if (timestamp >= base_time)
      return timestamp - base_time;

    return 0;
  }

  GstClockTimeDiff running_time = now_gst - base_time + timestamp - now_qpc;
  if (running_time >= 0)
    return running_time;

  return 0;
}

static GstFlowReturn
gst_win32_ipc_base_src_create (GstBaseSrc * src, guint64 offset, guint size,
    GstBuffer ** buf)
{
  auto self = GST_WIN32_IPC_BASE_SRC (src);
  auto priv = self->priv;
  GstFlowReturn ret;
  GstSample *sample = nullptr;

  GST_TRACE_OBJECT (self, "Create");

  ret = gst_win32_ipc_client_run (priv->client);
  if (ret != GST_FLOW_OK)
    return ret;

  ret = gst_win32_ipc_client_get_sample (priv->client, &sample);
  if (ret != GST_FLOW_OK)
    return ret;

  auto now_qpc = gst_util_get_timestamp ();
  auto clock = gst_element_get_clock (GST_ELEMENT_CAST (self));
  auto now_gst = gst_clock_get_time (clock);
  auto base_time = GST_ELEMENT_CAST (self)->base_time;
  auto is_qpc = gst_clock_is_system_monotonic (clock);
  gst_object_unref (clock);

  auto buffer = gst_sample_get_buffer (sample);
  auto pts = gst_win32_ipc_base_src_get_buffer_time (src, base_time,
      is_qpc, now_qpc, now_gst, GST_BUFFER_PTS (buffer));
  auto dts = gst_win32_ipc_base_src_get_buffer_time (src, base_time,
      is_qpc, now_qpc, now_gst, GST_BUFFER_DTS (buffer));

  GST_BUFFER_PTS (buffer) = pts;
  GST_BUFFER_DTS (buffer) = dts;

  std::unique_lock < std::mutex > lk (priv->lock);
  auto caps = gst_sample_get_caps (sample);
  if (!priv->caps || !gst_caps_is_equal (priv->caps, caps)) {
    gst_caps_replace (&priv->caps, caps);
    lk.unlock ();
    gst_base_src_set_caps (src, priv->caps);
  }

  *buf = gst_buffer_ref (buffer);
  gst_sample_unref (sample);

  return GST_FLOW_OK;
}
