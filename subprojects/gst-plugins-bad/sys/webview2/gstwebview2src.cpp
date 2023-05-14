/* GStreamer
 * Copyright (C) 2024 Seungha Yang <seungha@centricular.com>
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
 * SECTION:element-webview2src
 * @title: webview2src
 * @short_description: WebView2 based browser source
 *
 * Since: 1.26
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstwebview2src.h"
#include "gstwebview2object.h"
#include <gst/d3d11/gstd3d11-private.h>
#include <mutex>
#include <string>

GST_DEBUG_CATEGORY (gst_webview2_src_debug);
#define GST_CAT_DEFAULT gst_webview2_src_debug

static GstStaticPadTemplate pad_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE_WITH_FEATURES
        (GST_CAPS_FEATURE_MEMORY_D3D11_MEMORY,
            "BGRA") ", pixel-aspect-ratio = 1/1;" GST_VIDEO_CAPS_MAKE ("BGRA")
        ", pixel-aspect-ratio = 1/1"));

enum
{
  PROP_0,
  PROP_ADAPTER,
  PROP_LOCATION,
  PROP_PROCESSING_DEADLINE,
};

#define DEFAULT_LOCATION "about:blank"
#define DEFAULT_PROCESSING_DEADLINE (20 * GST_MSECOND)
#define DEFAULT_ADAPTER -1

/* *INDENT-OFF* */
struct GstWebView2SrcPrivate
{
  GstD3D11Device *device = nullptr;

  GstWebView2Object *object = nullptr;

  std::mutex lock;
  GstVideoInfo info;
  guint64 last_frame_no;
  GstClockID clock_id = nullptr;

  /* properties */
  gint adapter_index = DEFAULT_ADAPTER;
  std::string location = DEFAULT_LOCATION;
  GstClockTime processing_deadline = DEFAULT_PROCESSING_DEADLINE;
};
/* *INDENT-ON* */

struct _GstWebView2Src
{
  GstBaseSrc parent;

  GstWebView2SrcPrivate *priv;
};

static void gst_webview2_src_finalize (GObject * object);
static void gst_webview2_src_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_win32_video_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstClock *gst_webview2_src_provide_clock (GstElement * elem);
static void gst_webview2_src_set_context (GstElement * elem,
    GstContext * context);

static gboolean gst_webview2_src_start (GstBaseSrc * src);
static gboolean gst_webview2_src_stop (GstBaseSrc * src);
static gboolean gst_webview2_src_unlock (GstBaseSrc * src);
static gboolean gst_webview2_src_unlock_stop (GstBaseSrc * src);
static gboolean gst_webview2_src_query (GstBaseSrc * src, GstQuery * query);
static GstCaps *gst_webview2_src_fixate (GstBaseSrc * src, GstCaps * caps);
static gboolean gst_webview2_src_set_caps (GstBaseSrc * src, GstCaps * caps);
static gboolean gst_webview2_src_event (GstBaseSrc * src, GstEvent * event);
static GstFlowReturn gst_webview2_src_create (GstBaseSrc * src,
    guint64 offset, guint size, GstBuffer ** buf);
static void gst_webview2_src_uri_handler_init (gpointer iface, gpointer data);

#define gst_webview2_src_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstWebView2Src, gst_webview2_src, GST_TYPE_BASE_SRC,
    G_IMPLEMENT_INTERFACE (GST_TYPE_URI_HANDLER,
        gst_webview2_src_uri_handler_init));

static void
gst_webview2_src_class_init (GstWebView2SrcClass * klass)
{
  auto object_class = G_OBJECT_CLASS (klass);
  auto element_class = GST_ELEMENT_CLASS (klass);
  auto src_class = GST_BASE_SRC_CLASS (klass);

  object_class->finalize = gst_webview2_src_finalize;
  object_class->set_property = gst_webview2_src_set_property;
  object_class->get_property = gst_win32_video_src_get_property;

  g_object_class_install_property (object_class, PROP_ADAPTER,
      g_param_spec_int ("adapter", "Adapter",
          "DXGI Adapter index (-1 for any device)",
          -1, G_MAXINT32, DEFAULT_ADAPTER,
          (GParamFlags) (G_PARAM_READWRITE | GST_PARAM_MUTABLE_READY |
              G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (object_class, PROP_LOCATION,
      g_param_spec_string ("location", "location",
          "The URL to display",
          nullptr, (GParamFlags) (G_PARAM_READWRITE |
              G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));

  g_object_class_install_property (object_class, PROP_PROCESSING_DEADLINE,
      g_param_spec_uint64 ("processing-deadline", "Processing deadline",
          "Maximum processing time for a buffer in nanoseconds", 0, G_MAXUINT64,
          DEFAULT_PROCESSING_DEADLINE, (GParamFlags) (G_PARAM_READWRITE |
              G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));

  gst_element_class_set_static_metadata (element_class,
      "WebView2 Source", "Source/Video",
      "Creates a video stream rendered by WebView2",
      "Seungha Yang <seungha@centricular.com>");

  gst_element_class_add_static_pad_template (element_class, &pad_template);

  element_class->provide_clock =
      GST_DEBUG_FUNCPTR (gst_webview2_src_provide_clock);
  element_class->set_context = GST_DEBUG_FUNCPTR (gst_webview2_src_set_context);

  src_class->start = GST_DEBUG_FUNCPTR (gst_webview2_src_start);
  src_class->stop = GST_DEBUG_FUNCPTR (gst_webview2_src_stop);
  src_class->unlock = GST_DEBUG_FUNCPTR (gst_webview2_src_unlock);
  src_class->unlock_stop = GST_DEBUG_FUNCPTR (gst_webview2_src_unlock_stop);
  src_class->query = GST_DEBUG_FUNCPTR (gst_webview2_src_query);
  src_class->fixate = GST_DEBUG_FUNCPTR (gst_webview2_src_fixate);
  src_class->set_caps = GST_DEBUG_FUNCPTR (gst_webview2_src_set_caps);
  src_class->event = GST_DEBUG_FUNCPTR (gst_webview2_src_event);
  src_class->create = GST_DEBUG_FUNCPTR (gst_webview2_src_create);

  GST_DEBUG_CATEGORY_INIT (gst_webview2_src_debug, "webview2src",
      0, "webview2src");
}

static void
gst_webview2_src_init (GstWebView2Src * self)
{
  gst_base_src_set_format (GST_BASE_SRC (self), GST_FORMAT_TIME);
  gst_base_src_set_live (GST_BASE_SRC (self), TRUE);

  self->priv = new GstWebView2SrcPrivate ();

  GST_OBJECT_FLAG_SET (self, GST_ELEMENT_FLAG_PROVIDE_CLOCK);
  GST_OBJECT_FLAG_SET (self, GST_ELEMENT_FLAG_REQUIRE_CLOCK);
}

static void
gst_webview2_src_finalize (GObject * object)
{
  auto self = GST_WEBVIEW2_SRC (object);

  delete self->priv;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_webview2_src_set_location (GstWebView2Src * self, const gchar * location)
{
  auto priv = self->priv;
  priv->location.clear ();
  if (location)
    priv->location = location;
  else
    priv->location = DEFAULT_LOCATION;

  if (priv->object)
    gst_webview2_object_set_location (priv->object, priv->location);
}

static void
gst_webview2_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  auto self = GST_WEBVIEW2_SRC (object);
  auto priv = self->priv;
  std::unique_lock < std::mutex > lk (priv->lock);

  switch (prop_id) {
    case PROP_ADAPTER:
      priv->adapter_index = g_value_get_int (value);
      break;
    case PROP_LOCATION:
      gst_webview2_src_set_location (self, g_value_get_string (value));
      break;
    case PROP_PROCESSING_DEADLINE:
    {
      GstClockTime prev_val, new_val;
      prev_val = priv->processing_deadline;
      new_val = g_value_get_uint64 (value);
      priv->processing_deadline = new_val;

      if (prev_val != new_val) {
        lk.unlock ();
        GST_DEBUG_OBJECT (self, "Posting latency message");
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
  auto self = GST_WEBVIEW2_SRC (object);
  auto priv = self->priv;
  std::lock_guard < std::mutex > lk (priv->lock);

  switch (prop_id) {
    case PROP_ADAPTER:
      g_value_set_int (value, priv->adapter_index);
      break;
    case PROP_LOCATION:
      g_value_set_string (value, priv->location.c_str ());
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
gst_webview2_src_provide_clock (GstElement * elem)
{
  return gst_system_clock_obtain ();
}

static void
gst_webview2_src_set_context (GstElement * elem, GstContext * context)
{
  auto self = GST_WEBVIEW2_SRC (elem);
  auto priv = self->priv;

  gst_d3d11_handle_set_context (elem,
      context, priv->adapter_index, &priv->device);

  GST_ELEMENT_CLASS (parent_class)->set_context (elem, context);
}

static gboolean
gst_webview2_src_start (GstBaseSrc * src)
{
  auto self = GST_WEBVIEW2_SRC (src);
  auto priv = self->priv;

  GST_DEBUG_OBJECT (self, "Start");

  if (!gst_d3d11_ensure_element_data (GST_ELEMENT_CAST (self),
          priv->adapter_index, &priv->device)) {
    GST_ERROR_OBJECT (self, "Couldn't get D3D11 context");
    return FALSE;
  }

  std::lock_guard < std::mutex > lk (priv->lock);
  priv->object = gst_webview2_object_new (priv->device);
  if (!priv->object) {
    GST_ERROR_OBJECT (self, "Couldn't create object");
    return FALSE;
  }

  gst_webview2_object_set_location (priv->object, priv->location);

  priv->last_frame_no = -1;

  return TRUE;
}

static gboolean
gst_webview2_src_stop (GstBaseSrc * src)
{
  auto self = GST_WEBVIEW2_SRC (src);
  auto priv = self->priv;

  std::lock_guard < std::mutex > lk (priv->lock);

  GST_DEBUG_OBJECT (self, "Stop");

  gst_clear_object (&priv->object);
  gst_clear_object (&priv->device);

  return TRUE;
}

static gboolean
gst_webview2_src_unlock (GstBaseSrc * src)
{
  auto self = GST_WEBVIEW2_SRC (src);
  auto priv = self->priv;

  GST_DEBUG_OBJECT (self, "Unlock");

  std::lock_guard < std::mutex > lk (priv->lock);
  if (priv->object)
    gst_webview2_object_set_flushing (priv->object, true);

  if (priv->clock_id)
    gst_clock_id_unschedule (priv->clock_id);

  return TRUE;
}

static gboolean
gst_webview2_src_unlock_stop (GstBaseSrc * src)
{
  auto self = GST_WEBVIEW2_SRC (src);
  auto priv = self->priv;

  GST_DEBUG_OBJECT (self, "Unlock stop");

  std::lock_guard < std::mutex > lk (priv->lock);
  if (priv->object)
    gst_webview2_object_set_flushing (priv->object, false);

  return TRUE;
}

static gboolean
gst_webview2_src_query (GstBaseSrc * src, GstQuery * query)
{
  auto self = GST_WEBVIEW2_SRC (src);
  auto priv = self->priv;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_LATENCY:
    {
      std::lock_guard < std::mutex > lk (priv->lock);
      if (GST_CLOCK_TIME_IS_VALID (priv->processing_deadline)) {
        gst_query_set_latency (query, TRUE, priv->processing_deadline,
            GST_CLOCK_TIME_NONE);
      } else {
        gst_query_set_latency (query, TRUE, 0, 0);
      }
      return TRUE;
    }
    case GST_QUERY_CONTEXT:
      if (gst_d3d11_handle_context_query (GST_ELEMENT (self), query,
              priv->device)) {
        return TRUE;
      }
      break;
    default:
      break;
  }

  return GST_BASE_SRC_CLASS (parent_class)->query (src, query);
}

static GstCaps *
gst_webview2_src_fixate (GstBaseSrc * src, GstCaps * caps)
{
  caps = gst_caps_make_writable (caps);
  auto s = gst_caps_get_structure (caps, 0);

  gst_structure_fixate_field_nearest_int (s, "width", 1920);
  gst_structure_fixate_field_nearest_int (s, "height", 1080);
  gst_structure_fixate_field_nearest_fraction (s, "framerate", 30, 1);

  return GST_BASE_SRC_CLASS (parent_class)->fixate (src, caps);
}

static gboolean
gst_webview2_src_set_caps (GstBaseSrc * src, GstCaps * caps)
{
  auto self = GST_WEBVIEW2_SRC (src);
  auto priv = self->priv;
  std::lock_guard < std::mutex > lk (priv->lock);

  if (!gst_video_info_from_caps (&priv->info, caps)) {
    GST_ERROR_OBJECT (self, "Invalid caps %" GST_PTR_FORMAT, caps);
    return FALSE;
  }

  if (priv->object)
    gst_webview2_object_set_caps (priv->object, caps);

  return TRUE;
}

static gboolean
gst_webview2_src_event (GstBaseSrc * src, GstEvent * event)
{
  auto self = GST_WEBVIEW2_SRC (src);
  auto priv = self->priv;
  std::unique_lock < std::mutex > lk (priv->lock);

  if (priv->object || GST_EVENT_TYPE (event) == GST_EVENT_NAVIGATION) {
    gst_webview2_object_send_event (priv->object, event);
    return TRUE;
  }
  lk.unlock ();

  return GST_BASE_SRC_CLASS (parent_class)->event (src, event);
}

static bool
gst_webview2_clock_is_system (GstClock * clock)
{
  GstClockType clock_type = GST_CLOCK_TYPE_MONOTONIC;
  GstClock *mclock;

  if (G_OBJECT_TYPE (clock) != GST_TYPE_SYSTEM_CLOCK)
    return false;

  g_object_get (clock, "clock-type", &clock_type, nullptr);
  if (clock_type != GST_CLOCK_TYPE_MONOTONIC)
    return false;

  mclock = gst_clock_get_master (clock);
  if (!mclock)
    return true;

  gst_object_unref (mclock);
  return false;
}

static GstFlowReturn
gst_webview2_src_create (GstBaseSrc * src, guint64 offset, guint size,
    GstBuffer ** buf)
{
  auto self = GST_WEBVIEW2_SRC (src);
  auto priv = self->priv;
  GstFlowReturn ret;
  GstClock *clock;
  bool is_system_clock;
  GstClockTime pts;
  GstClockTime base_time;
  GstClockTime now_system;
  GstClockTime now_gst;
  GstClockTime capture_pts;
  GstClockTime next_capture_ts;
  guint64 next_frame_no = 0;
  GstBuffer *buffer;
  gint fps_n, fps_d;
  GstClockTime dur = GST_CLOCK_TIME_NONE;

  clock = gst_element_get_clock (GST_ELEMENT_CAST (self));
  now_gst = gst_clock_get_time (clock);
  base_time = GST_ELEMENT_CAST (self)->base_time;
  next_capture_ts = now_gst - base_time;
  is_system_clock = gst_webview2_clock_is_system (clock);

  fps_n = priv->info.fps_n;
  fps_d = priv->info.fps_d;

  if (fps_n > 0 && fps_d > 0) {
    next_frame_no = gst_util_uint64_scale (next_capture_ts,
        fps_n, GST_SECOND * fps_d);

    if (next_frame_no == priv->last_frame_no) {
      GstClockID id;
      GstClockReturn clock_ret;
      std::unique_lock < std::mutex > lk (priv->lock);

      next_frame_no++;

      next_capture_ts = gst_util_uint64_scale (next_frame_no,
          fps_d * GST_SECOND, fps_n);

      id = gst_clock_new_single_shot_id (GST_ELEMENT_CLOCK (self),
          next_capture_ts + base_time);
      priv->clock_id = id;
      lk.unlock ();

      clock_ret = gst_clock_id_wait (id, nullptr);

      lk.lock ();

      gst_clock_id_unref (id);
      priv->clock_id = nullptr;

      if (clock_ret == GST_CLOCK_UNSCHEDULED) {
        gst_object_unref (clock);
        return GST_FLOW_FLUSHING;
      }

      dur = gst_util_uint64_scale_int (GST_SECOND, fps_d, fps_n);
    } else {
      GstClockTime next_frame_ts = gst_util_uint64_scale (next_frame_no + 1,
          fps_d * GST_SECOND, fps_n);
      dur = next_frame_ts - next_capture_ts;
    }
  }

  priv->last_frame_no = next_frame_no;

  ret = gst_webview2_object_get_buffer (priv->object, &buffer);
  if (ret != GST_FLOW_OK) {
    gst_object_unref (clock);
    return ret;
  }

  capture_pts = GST_BUFFER_PTS (buffer);
  now_system = gst_util_get_timestamp ();
  now_gst = gst_clock_get_time (clock);
  gst_object_unref (clock);

  if (!is_system_clock) {
    GstClockTimeDiff now_pts = now_gst - base_time + capture_pts - now_system;

    if (now_pts >= 0)
      pts = now_pts;
    else
      pts = 0;
  } else {
    if (capture_pts >= base_time) {
      pts = capture_pts - base_time;
    } else {
      GST_WARNING_OBJECT (self,
          "Captured time is smaller than our base time, remote %"
          GST_TIME_FORMAT ", base_time %" GST_TIME_FORMAT,
          GST_TIME_ARGS (capture_pts), GST_TIME_ARGS (base_time));
      pts = 0;
    }
  }

  GST_BUFFER_PTS (buffer) = pts;
  GST_BUFFER_DURATION (buffer) = dur;
  *buf = buffer;

  return GST_FLOW_OK;
}

static GstURIType
gst_webview2_src_uri_get_type (GType type)
{
  return GST_URI_SRC;
}

static const gchar *const *
gst_webview2_src_get_protocols (GType type)
{
  static const gchar *protocols[] = { "web+http", "web+https", nullptr };

  return protocols;
}

static gchar *
gst_webview2_src_get_uri (GstURIHandler * handler)
{
  auto self = GST_WEBVIEW2_SRC (handler);
  auto priv = self->priv;
  std::lock_guard < std::mutex > lk (priv->lock);

  if (priv->location.empty ())
    return nullptr;

  return g_strdup (priv->location.c_str ());
}

static gboolean
gst_webview2_src_set_uri (GstURIHandler * handler, const gchar * uri_str,
    GError ** err)
{
  auto self = GST_WEBVIEW2_SRC (handler);
  auto priv = self->priv;

  auto protocol = gst_uri_get_protocol (uri_str);
  if (!g_str_has_prefix (protocol, "web+")) {
    g_free (protocol);
    return FALSE;
  }

  auto uri = gst_uri_from_string (uri_str);
  gst_uri_set_scheme (uri, protocol + 4);

  auto location = gst_uri_to_string (uri);

  std::lock_guard < std::mutex > lk (priv->lock);
  gst_webview2_src_set_location (self, location);

  gst_uri_unref (uri);
  g_free (protocol);
  g_free (location);

  return TRUE;
}

static void
gst_webview2_src_uri_handler_init (gpointer iface, gpointer data)
{
  auto handler = (GstURIHandlerInterface *) iface;

  handler->get_type = gst_webview2_src_uri_get_type;
  handler->get_protocols = gst_webview2_src_get_protocols;
  handler->get_uri = gst_webview2_src_get_uri;
  handler->set_uri = gst_webview2_src_set_uri;
}
