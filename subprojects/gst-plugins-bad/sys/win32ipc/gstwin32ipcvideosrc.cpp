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
#include "gstwin32ipcclient.h"
#include <string>
#include <mutex>

GST_DEBUG_CATEGORY_STATIC (gst_win32_ipc_video_src_debug);
#define GST_CAT_DEFAULT gst_win32_ipc_video_src_debug

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE (GST_VIDEO_FORMATS_ALL)));

enum
{
  PROP_0,
  PROP_PIPE_NAME,
  PROP_PROCESSING_DEADLINE,
};

#define DEFAULT_PIPE_NAME "\\\\.\\pipe\\gst.win32.ipc.video"
#define DEFAULT_PROCESSING_DEADLINE (20 * GST_MSECOND)

/* *INDENT-OFF* */
struct GstWin32IpcVideoSrcPrivate
{
  GstWin32IpcVideoSrcPrivate ()
  {
    pipe_name = g_strdup (DEFAULT_PIPE_NAME);
  }

  ~GstWin32IpcVideoSrcPrivate ()
  {
    g_free (pipe_name);
  }

  GstVideoInfo info;

  GstWin32IpcClient *client = nullptr;
  GstCaps *caps = nullptr;
  std::mutex lock;

  /* properties */
  gchar *pipe_name;
  GstClockTime processing_deadline = DEFAULT_PROCESSING_DEADLINE;
};
/* *INDENT-ON* */

struct _GstWin32IpcVideoSrc
{
  GstBaseSrc parent;

  GstWin32IpcVideoSrcPrivate *priv;
};

static void gst_win32_ipc_video_src_finalize (GObject * object);
static void gst_win32_ipc_video_src_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_win32_video_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstClock *gst_win32_video_src_provide_clock (GstElement * elem);

static gboolean gst_win32_ipc_video_src_start (GstBaseSrc * src);
static gboolean gst_win32_ipc_video_src_stop (GstBaseSrc * src);
static gboolean gst_win32_ipc_video_src_unlock (GstBaseSrc * src);
static gboolean gst_win32_ipc_video_src_unlock_stop (GstBaseSrc * src);
static gboolean gst_win32_ipc_video_src_query (GstBaseSrc * src,
    GstQuery * query);
static GstCaps *gst_win32_ipc_video_src_get_caps (GstBaseSrc * src,
    GstCaps * filter);
static GstCaps *gst_win32_ipc_video_src_fixate (GstBaseSrc * src,
    GstCaps * caps);
static GstFlowReturn gst_win32_ipc_video_src_create (GstBaseSrc * src,
    guint64 offset, guint size, GstBuffer ** buf);

#define gst_win32_ipc_video_src_parent_class parent_class
G_DEFINE_TYPE (GstWin32IpcVideoSrc, gst_win32_ipc_video_src, GST_TYPE_BASE_SRC);

static void
gst_win32_ipc_video_src_class_init (GstWin32IpcVideoSrcClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstBaseSrcClass *src_class = GST_BASE_SRC_CLASS (klass);

  object_class->finalize = gst_win32_ipc_video_src_finalize;
  object_class->set_property = gst_win32_ipc_video_src_set_property;
  object_class->get_property = gst_win32_video_src_get_property;

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

  gst_element_class_set_static_metadata (element_class,
      "Win32 IPC Video Source", "Source/Video",
      "Receive video frames from the win32ipcvideosink",
      "Seungha Yang <seungha@centricular.com>");
  gst_element_class_add_static_pad_template (element_class, &src_template);

  element_class->provide_clock =
      GST_DEBUG_FUNCPTR (gst_win32_video_src_provide_clock);

  src_class->start = GST_DEBUG_FUNCPTR (gst_win32_ipc_video_src_start);
  src_class->stop = GST_DEBUG_FUNCPTR (gst_win32_ipc_video_src_stop);
  src_class->unlock = GST_DEBUG_FUNCPTR (gst_win32_ipc_video_src_unlock);
  src_class->unlock_stop =
      GST_DEBUG_FUNCPTR (gst_win32_ipc_video_src_unlock_stop);
  src_class->query = GST_DEBUG_FUNCPTR (gst_win32_ipc_video_src_query);
  src_class->get_caps = GST_DEBUG_FUNCPTR (gst_win32_ipc_video_src_get_caps);
  src_class->fixate = GST_DEBUG_FUNCPTR (gst_win32_ipc_video_src_fixate);
  src_class->create = GST_DEBUG_FUNCPTR (gst_win32_ipc_video_src_create);

  GST_DEBUG_CATEGORY_INIT (gst_win32_ipc_video_src_debug, "win32ipcvideosrc",
      0, "win32ipcvideosrc");
}

static void
gst_win32_ipc_video_src_init (GstWin32IpcVideoSrc * self)
{
  self->priv = new GstWin32IpcVideoSrcPrivate ();

  gst_base_src_set_format (GST_BASE_SRC (self), GST_FORMAT_TIME);
  gst_base_src_set_live (GST_BASE_SRC (self), TRUE);

  GST_OBJECT_FLAG_SET (self, GST_ELEMENT_FLAG_PROVIDE_CLOCK);
  GST_OBJECT_FLAG_SET (self, GST_ELEMENT_FLAG_REQUIRE_CLOCK);
}

static void
gst_win32_ipc_video_src_finalize (GObject * object)
{
  auto self = GST_WIN32_IPC_VIDEO_SRC (object);

  delete self->priv;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_win32_ipc_video_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  auto self = GST_WIN32_IPC_VIDEO_SRC (object);
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
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_win32_video_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  auto self = GST_WIN32_IPC_VIDEO_SRC (object);
  auto priv = self->priv;

  std::lock_guard < std::mutex > lk (priv->lock);

  switch (prop_id) {
    case PROP_PIPE_NAME:
      g_value_set_string (value, priv->pipe_name);
      break;
    case PROP_PROCESSING_DEADLINE:
      g_value_set_uint64 (value, priv->processing_deadline);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstClock *
gst_win32_video_src_provide_clock (GstElement * elem)
{
  return gst_system_clock_obtain ();
}

static gboolean
gst_win32_ipc_video_src_start (GstBaseSrc * src)
{
  auto self = GST_WIN32_IPC_VIDEO_SRC (src);
  auto priv = self->priv;

  GST_DEBUG_OBJECT (self, "Start");

  std::lock_guard < std::mutex > lk (priv->lock);
  gst_video_info_init (&priv->info);
  priv->client = gst_win32_ipc_client_new (priv->pipe_name, 5);

  return TRUE;
}

static gboolean
gst_win32_ipc_video_src_stop (GstBaseSrc * src)
{
  auto self = GST_WIN32_IPC_VIDEO_SRC (src);
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
gst_win32_ipc_video_src_unlock (GstBaseSrc * src)
{
  auto self = GST_WIN32_IPC_VIDEO_SRC (src);
  auto priv = self->priv;

  GST_DEBUG_OBJECT (self, "Unlock");

  std::lock_guard < std::mutex > lk (priv->lock);
  if (priv->client)
    gst_win32_ipc_client_set_flushing (priv->client, true);

  return TRUE;
}

static gboolean
gst_win32_ipc_video_src_unlock_stop (GstBaseSrc * src)
{
  auto self = GST_WIN32_IPC_VIDEO_SRC (src);
  auto priv = self->priv;

  GST_DEBUG_OBJECT (self, "Unlock stop");

  std::lock_guard < std::mutex > lk (priv->lock);
  if (priv->client)
    gst_win32_ipc_client_set_flushing (priv->client, false);

  return TRUE;
}

static gboolean
gst_win32_ipc_video_src_query (GstBaseSrc * src, GstQuery * query)
{
  auto self = GST_WIN32_IPC_VIDEO_SRC (src);
  auto priv = self->priv;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_LATENCY:
    {
      GST_OBJECT_LOCK (self);
      if (GST_CLOCK_TIME_IS_VALID (priv->processing_deadline)) {
        gst_query_set_latency (query, TRUE, priv->processing_deadline,
            /* client server can hold up to 5 memory objects */
            5 * priv->processing_deadline);
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
gst_win32_ipc_video_src_get_caps (GstBaseSrc * src, GstCaps * filter)
{
  auto self = GST_WIN32_IPC_VIDEO_SRC (src);
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

static GstFlowReturn
gst_win32_ipc_video_src_create (GstBaseSrc * src, guint64 offset, guint size,
    GstBuffer ** buf)
{
  auto self = GST_WIN32_IPC_VIDEO_SRC (src);
  auto priv = self->priv;
  GstFlowReturn ret;
  GstSample *sample = nullptr;
  GstCaps *caps;
  GstClock *clock;
  bool is_system_clock = true;
  GstClockTime pts;
  GstClockTime base_time;
  GstClockTime now_system;
  GstClockTime now_gst;
  GstClockTime remote_pts;
  GstBuffer *buffer;

  GST_TRACE_OBJECT (self, "Create");

  ret = gst_win32_ipc_client_run (priv->client);
  if (ret != GST_FLOW_OK)
    return ret;

  ret = gst_win32_ipc_client_get_sample (priv->client, &sample);
  if (ret != GST_FLOW_OK)
    return ret;

  now_system = gst_util_get_timestamp ();
  clock = gst_element_get_clock (GST_ELEMENT_CAST (self));
  now_gst = gst_clock_get_time (clock);
  base_time = GST_ELEMENT_CAST (self)->base_time;
  is_system_clock = gst_clock_is_system_monotonic (clock);
  gst_object_unref (clock);

  buffer = gst_sample_get_buffer (sample);
  remote_pts = GST_BUFFER_PTS (buffer);

  if (!is_system_clock) {
    GstClockTimeDiff now_pts = now_gst - base_time + remote_pts - now_system;

    if (now_pts >= 0)
      pts = now_pts;
    else
      pts = 0;
  } else {
    if (remote_pts >= base_time) {
      pts = remote_pts - base_time;
    } else {
      GST_WARNING_OBJECT (self,
          "Remote clock is smaller than our base time, remote %"
          GST_TIME_FORMAT ", base_time %" GST_TIME_FORMAT,
          GST_TIME_ARGS (remote_pts), GST_TIME_ARGS (base_time));
      pts = 0;
    }
  }

  GST_BUFFER_PTS (buffer) = pts;

  std::unique_lock < std::mutex > lk (priv->lock);
  caps = gst_sample_get_caps (sample);
  if (!priv->caps || !gst_caps_is_equal (priv->caps, caps)) {
    gst_caps_replace (&priv->caps, caps);
    lk.unlock ();
    gst_base_src_set_caps (src, priv->caps);
  }

  *buf = gst_buffer_ref (buffer);
  gst_sample_unref (sample);

  return GST_FLOW_OK;
}
