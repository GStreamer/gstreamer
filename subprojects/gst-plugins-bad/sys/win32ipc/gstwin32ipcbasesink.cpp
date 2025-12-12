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

#include "gstwin32ipcbasesink.h"
#include "gstwin32ipcserver.h"
#include "gstwin32ipc.h"
#include <string>
#include <string.h>
#include <mutex>
#include <condition_variable>

GST_DEBUG_CATEGORY_STATIC (gst_win32_ipc_base_sink_debug);
#define GST_CAT_DEFAULT gst_win32_ipc_base_sink_debug

enum
{
  PROP_0,
  PROP_PIPE_NAME,
  PROP_LEAKY_TYPE,
  PROP_MAX_BUFFERS,
  PROP_CURRENT_LEVEL_BUFFERS,
  PROP_NUM_CLIENTS,
  PROP_WAIT_FOR_CONNECTION,
  PROP_LAST,
};

static GParamSpec *props[PROP_LAST];

#define DEFAULT_PIPE_NAME "\\\\.\\pipe\\gst.win32.ipc"
#define DEFAULT_MAX_BUFFERS 2
#define DEFAULT_LEAKY_TYPE GST_WIN32_IPC_LEAKY_NONE
#define DEFAULT_WAIT_FOR_CONNECTION FALSE

/* *INDENT-OFF* */
struct _GstWin32IpcBaseSinkPrivate
{
  _GstWin32IpcBaseSinkPrivate ()
  {
    meta = g_byte_array_new ();
    pipe_name = g_strdup (DEFAULT_PIPE_NAME);
  }

  ~_GstWin32IpcBaseSinkPrivate ()
  {
    reset ();

    gst_clear_object (&server);
    g_byte_array_unref (meta);
    g_free (pipe_name);
  }

  void reset ()
  {
    gst_clear_caps (&caps);
    num_clients = 0;
  }

  std::mutex lock;
  std::condition_variable cond;

  GstWin32IpcServer *server = nullptr;
  GstCaps *caps = nullptr;
  GByteArray *meta = nullptr;
  guint num_clients = 0;
  bool flushing = false;

  /* properties */
  gchar *pipe_name;
  guint64 max_buffers = DEFAULT_MAX_BUFFERS;
  GstWin32IpcLeakyType leaky = DEFAULT_LEAKY_TYPE;
  gboolean wait_for_connection = DEFAULT_WAIT_FOR_CONNECTION;
};
/* *INDENT-ON* */

static void gst_win32_ipc_base_sink_finalize (GObject * object);
static void gst_win32_ipc_base_sink_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_win32_base_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstClock *gst_win32_ipc_base_sink_provide_clock (GstElement * elem);

static gboolean gst_win32_ipc_base_sink_start (GstBaseSink * sink);
static gboolean gst_win32_ipc_base_sink_stop (GstBaseSink * sink);
static gboolean gst_win32_ipc_base_sink_unlock (GstBaseSink * sink);
static gboolean gst_win32_ipc_base_sink_unlock_stop (GstBaseSink * sink);
static gboolean gst_win32_ipc_base_sink_set_caps (GstBaseSink * sink,
    GstCaps * caps);
static GstFlowReturn gst_win32_ipc_base_sink_render (GstBaseSink * sink,
    GstBuffer * buf);
static gboolean gst_win32_ipc_base_sink_event (GstBaseSink * sink,
    GstEvent * event);

#define gst_win32_ipc_base_sink_parent_class parent_class
G_DEFINE_ABSTRACT_TYPE (GstWin32IpcBaseSink, gst_win32_ipc_base_sink,
    GST_TYPE_BASE_SINK);

static void
gst_win32_ipc_base_sink_class_init (GstWin32IpcBaseSinkClass * klass)
{
  auto object_class = G_OBJECT_CLASS (klass);
  auto element_class = GST_ELEMENT_CLASS (klass);
  auto sink_class = GST_BASE_SINK_CLASS (klass);

  object_class->finalize = gst_win32_ipc_base_sink_finalize;
  object_class->set_property = gst_win32_ipc_base_sink_set_property;
  object_class->get_property = gst_win32_base_sink_get_property;

  props[PROP_PIPE_NAME] =
      g_param_spec_string ("pipe-name", "Pipe Name",
      "The name of Win32 named pipe to communicate with clients. "
      "Validation of the pipe name is caller's responsibility",
      DEFAULT_PIPE_NAME, (GParamFlags) (G_PARAM_READWRITE |
          G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY));

  props[PROP_LEAKY_TYPE] =
      g_param_spec_enum ("leaky-type", "Leaky Type",
      "Whether to drop buffers once the internal queue is full",
      GST_TYPE_WIN32_IPC_LEAKY_TYPE, DEFAULT_LEAKY_TYPE,
      (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  props[PROP_MAX_BUFFERS] =
      g_param_spec_uint64 ("max-buffers", "Max Buffers",
      "Maximum number of buffers in queue (0=unlimited)",
      0, G_MAXUINT64, DEFAULT_MAX_BUFFERS,
      (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  props[PROP_CURRENT_LEVEL_BUFFERS] =
      g_param_spec_uint64 ("current-level-buffers", "Current Level Buffers",
      "The number of currently queued buffers",
      0, G_MAXUINT64, 0,
      (GParamFlags) (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  props[PROP_WAIT_FOR_CONNECTION] =
      g_param_spec_boolean ("wait-for-connection", "Wait for Connection",
      "Blocks the stream until at least one client is connected",
      DEFAULT_WAIT_FOR_CONNECTION,
      (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  props[PROP_NUM_CLIENTS] =
      g_param_spec_uint ("num-clients", "Number of Clients",
      "The number of connected clients",
      0, G_MAXUINT, 0,
      (GParamFlags) (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, PROP_LAST, props);

  element_class->provide_clock =
      GST_DEBUG_FUNCPTR (gst_win32_ipc_base_sink_provide_clock);

  sink_class->start = GST_DEBUG_FUNCPTR (gst_win32_ipc_base_sink_start);
  sink_class->stop = GST_DEBUG_FUNCPTR (gst_win32_ipc_base_sink_stop);
  sink_class->unlock = GST_DEBUG_FUNCPTR (gst_win32_ipc_base_sink_unlock);
  sink_class->unlock_stop =
      GST_DEBUG_FUNCPTR (gst_win32_ipc_base_sink_unlock_stop);
  sink_class->set_caps = GST_DEBUG_FUNCPTR (gst_win32_ipc_base_sink_set_caps);
  sink_class->render = GST_DEBUG_FUNCPTR (gst_win32_ipc_base_sink_render);
  sink_class->event = GST_DEBUG_FUNCPTR (gst_win32_ipc_base_sink_event);

  GST_DEBUG_CATEGORY_INIT (gst_win32_ipc_base_sink_debug, "win32ipcbasesink",
      0, "win32ipcbasesink");
}

static void
gst_win32_ipc_base_sink_init (GstWin32IpcBaseSink * self)
{
  self->priv = new GstWin32IpcBaseSinkPrivate ();

  GST_OBJECT_FLAG_SET (self, GST_ELEMENT_FLAG_PROVIDE_CLOCK);
  GST_OBJECT_FLAG_SET (self, GST_ELEMENT_FLAG_REQUIRE_CLOCK);
}

static void
gst_win32_ipc_base_sink_finalize (GObject * object)
{
  auto self = GST_WIN32_IPC_BASE_SINK (object);

  delete self->priv;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_win32_ipc_base_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  auto self = GST_WIN32_IPC_BASE_SINK (object);
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
      if (priv->server)
        gst_win32_ipc_server_set_max_buffers (priv->server, priv->max_buffers);
      break;
    case PROP_WAIT_FOR_CONNECTION:
    {
      auto wait = g_value_get_boolean (value);
      if (priv->wait_for_connection != wait) {
        priv->wait_for_connection = wait;
        priv->cond.notify_all ();
      }
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_win32_base_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  auto self = GST_WIN32_IPC_BASE_SINK (object);
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
    case PROP_WAIT_FOR_CONNECTION:
      g_value_set_boolean (value, priv->wait_for_connection);
      break;
    case PROP_NUM_CLIENTS:
      g_value_set_uint (value, priv->num_clients);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstClock *
gst_win32_ipc_base_sink_provide_clock (GstElement * elem)
{
  return gst_system_clock_obtain ();
}

static void
gst_win32_ipc_base_sink_on_num_clients (GObject * server,
    GParamSpec * pspec, GstWin32IpcBaseSink * self)
{
  auto priv = self->priv;

  guint num_clients = 0;
  g_object_get (server, "num-clients", &num_clients, nullptr);

  GST_DEBUG_OBJECT (self, "num-clients %u", num_clients);

  {
    std::lock_guard < std::mutex > lk (priv->lock);
    priv->num_clients = num_clients;
    priv->cond.notify_all ();
  }

  /* This is server's event loop thread. Use other thread to notify */
  gst_object_call_async (GST_OBJECT (self),
      [](GstObject * object, gpointer user_data)->void
      {
        g_object_notify_by_pspec (G_OBJECT (object), props[PROP_NUM_CLIENTS]);
      }, nullptr);
}

static gboolean
gst_win32_ipc_base_sink_start (GstBaseSink * sink)
{
  auto self = GST_WIN32_IPC_BASE_SINK (sink);
  auto priv = self->priv;

  GST_DEBUG_OBJECT (self, "Start");

  {
    std::lock_guard < std::mutex > lk (priv->lock);
    priv->server = gst_win32_ipc_server_new (priv->pipe_name,
        priv->max_buffers, priv->leaky);
    if (!priv->server) {
      GST_ERROR_OBJECT (self, "Couldn't create pipe server");
      return FALSE;
    }
  }

  g_signal_connect (priv->server, "notify::num-clients",
      G_CALLBACK (gst_win32_ipc_base_sink_on_num_clients), self);

  return TRUE;
}

static gboolean
gst_win32_ipc_base_sink_stop (GstBaseSink * sink)
{
  auto self = GST_WIN32_IPC_BASE_SINK (sink);
  auto priv = self->priv;

  GST_DEBUG_OBJECT (self, "Stop");

  std::lock_guard < std::mutex > lk (priv->lock);
  if (priv->server) {
    g_signal_handlers_disconnect_by_data (priv->server, self);
    gst_clear_object (&priv->server);
  }

  priv->reset ();

  return TRUE;
}

static gboolean
gst_win32_ipc_base_sink_unlock (GstBaseSink * sink)
{
  auto self = GST_WIN32_IPC_BASE_SINK (sink);
  auto priv = self->priv;

  GST_DEBUG_OBJECT (self, "Unlock");

  std::lock_guard < std::mutex > lk (priv->lock);
  if (priv->server)
    gst_win32_ipc_server_set_flushing (priv->server, TRUE);
  priv->flushing = true;
  priv->cond.notify_all ();

  return TRUE;
}

static gboolean
gst_win32_ipc_base_sink_unlock_stop (GstBaseSink * sink)
{
  auto self = GST_WIN32_IPC_BASE_SINK (sink);
  auto priv = self->priv;

  GST_DEBUG_OBJECT (self, "Unlock stop");

  std::lock_guard < std::mutex > lk (priv->lock);
  if (priv->server)
    gst_win32_ipc_server_set_flushing (priv->server, FALSE);
  priv->flushing = false;
  priv->cond.notify_all ();

  return TRUE;
}

static gboolean
gst_win32_ipc_base_sink_set_caps (GstBaseSink * sink, GstCaps * caps)
{
  auto self = GST_WIN32_IPC_BASE_SINK (sink);
  auto priv = self->priv;

  gst_caps_replace (&priv->caps, caps);

  return TRUE;
}

static GstClockTime
gst_win32_ipc_base_sink_get_buffer_time (GstBaseSink * sink,
    GstClockTime base_time, GstClockTime latency, gboolean clock_is_qpc,
    GstClockTime now_qpc, GstClockTime now_gst, GstClockTime timestamp)
{
  if (!GST_CLOCK_TIME_IS_VALID (timestamp) ||
      !GST_CLOCK_TIME_IS_VALID (base_time)) {
    return GST_CLOCK_TIME_NONE;
  }

  GstClockTime running_time;
  auto ret = gst_segment_to_running_time_full (&sink->segment,
      GST_FORMAT_TIME, timestamp, &running_time);
  if (!ret)
    return GST_CLOCK_TIME_NONE;

  if (ret > 0)
    running_time += base_time;
  else if (base_time > timestamp)
    running_time = base_time - timestamp;
  else
    running_time = 0;

  if (GST_CLOCK_TIME_IS_VALID (latency))
    running_time += latency;

  if (clock_is_qpc)
    return running_time;

  if (running_time < now_gst)
    return 0;

  running_time -= now_gst;
  running_time += now_qpc;

  return running_time;
}

static GstFlowReturn
gst_win32_ipc_base_sink_render (GstBaseSink * sink, GstBuffer * buf)
{
  auto self = GST_WIN32_IPC_BASE_SINK (sink);
  auto priv = self->priv;

  if (!priv->server) {
    GST_ERROR_OBJECT (self, "Pipe server was not configured");
    return GST_FLOW_ERROR;
  }

  auto now_qpc = gst_util_get_timestamp ();
  auto base_time = GST_ELEMENT_CAST (sink)->base_time;
  auto latency = gst_base_sink_get_latency (sink);
  GstClockTime now_gst = GST_CLOCK_TIME_NONE;
  gboolean is_qpc = TRUE;

  auto clock = gst_element_get_clock (GST_ELEMENT_CAST (sink));
  if (clock) {
    now_gst = gst_clock_get_time (clock);
    is_qpc = gst_clock_is_system_monotonic (clock);
    gst_object_unref (clock);
  }

  auto pts = gst_win32_ipc_base_sink_get_buffer_time (sink,
      base_time, latency, is_qpc, now_qpc, now_gst, GST_BUFFER_PTS (buf));
  auto dts = gst_win32_ipc_base_sink_get_buffer_time (sink,
      base_time, latency, is_qpc, now_qpc, now_gst, GST_BUFFER_DTS (buf));

  GstBuffer *prepared;
  gsize size;
  auto klass = GST_WIN32_IPC_BASE_SINK_GET_CLASS (self);
  auto ret = klass->upload (self, buf, &prepared, &size);
  if (ret != GST_FLOW_OK)
    return ret;

  g_byte_array_set_size (priv->meta, 0);
  gst_buffer_foreach_meta (prepared,[](GstBuffer * prepared, GstMeta ** meta,
          gpointer user_data)->gboolean {
        auto self = GST_WIN32_IPC_BASE_SINK (user_data);
        gst_meta_serialize_simple (*meta, self->priv->meta);
        return TRUE;
      }
      , self);

  {
    std::unique_lock < std::mutex > lk (priv->lock);
    while (priv->wait_for_connection && priv->num_clients == 0 &&
        !priv->flushing) {
      priv->cond.wait (lk);
    }

    if (priv->flushing) {
      GST_DEBUG_OBJECT (self, "We are flushing");
      gst_buffer_unref (prepared);
      return GST_FLOW_FLUSHING;
    }
  }

  ret = gst_win32_ipc_server_send_data (priv->server,
      prepared, priv->caps, priv->meta, pts, dts, size);
  gst_buffer_unref (prepared);

  return ret;
}

static gboolean
gst_win32_ipc_base_sink_event (GstBaseSink * sink, GstEvent * event)
{
  auto self = GST_WIN32_IPC_BASE_SINK (sink);
  auto priv = self->priv;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
    {
      std::lock_guard < std::mutex > lk (priv->lock);
      if (priv->server) {
        GST_DEBUG_OBJECT (self, "Sending null data on EOS");
        gst_win32_ipc_server_send_data (priv->server,
            nullptr, nullptr, nullptr, GST_CLOCK_TIME_NONE,
            GST_CLOCK_TIME_NONE, 0);
      }
      break;
    }
    default:
      break;
  }

  return GST_BASE_SINK_CLASS (parent_class)->event (sink, event);
}
