/* GStreamer
 * Copyright (C) 2019 Seungha Yang <seungha.yang@navercorp.com>
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

#include "gstd3d11videosinkbin.h"
#include "gstd3d11videosink.h"
#include "gstd3d11upload.h"
#include "gstd3d11colorconvert.h"
#include "gstd3d11memory.h"
#include "gstd3d11utils.h"
#include "gstd3d11device.h"
#include "gstd3d11format.h"

enum
{
  PROP_0,
  /* basesink */
  PROP_SYNC,
  PROP_MAX_LATENESS,
  PROP_QOS,
  PROP_ASYNC,
  PROP_TS_OFFSET,
  PROP_ENABLE_LAST_SAMPLE,
  PROP_LAST_SAMPLE,
  PROP_BLOCKSIZE,
  PROP_RENDER_DELAY,
  PROP_THROTTLE_TIME,
  PROP_MAX_BITRATE,
  PROP_PROCESSING_DEADLINE,
  PROP_STATS,
  /* videosink */
  PROP_SHOW_PREROLL_FRAME,
  /* d3d11videosink */
  PROP_ADAPTER,
  PROP_FORCE_ASPECT_RATIO,
  PROP_ENABLE_NAVIGATION_EVENTS,
  PROP_FULLSCREEN_TOGGLE_MODE,
  PROP_FULLSCREEN,
};

/* basesink */
#define DEFAULT_SYNC                TRUE
#define DEFAULT_MAX_LATENESS        -1
#define DEFAULT_QOS                 FALSE
#define DEFAULT_ASYNC               TRUE
#define DEFAULT_TS_OFFSET           0
#define DEFAULT_BLOCKSIZE           4096
#define DEFAULT_RENDER_DELAY        0
#define DEFAULT_ENABLE_LAST_SAMPLE  TRUE
#define DEFAULT_THROTTLE_TIME       0
#define DEFAULT_MAX_BITRATE         0
#define DEFAULT_DROP_OUT_OF_SEGMENT TRUE
#define DEFAULT_PROCESSING_DEADLINE (20 * GST_MSECOND)

/* videosink */
#define DEFAULT_SHOW_PREROLL_FRAME TRUE

/* d3d11videosink */
#define DEFAULT_ADAPTER                   -1
#define DEFAULT_FORCE_ASPECT_RATIO        TRUE
#define DEFAULT_ENABLE_NAVIGATION_EVENTS  TRUE
#define DEFAULT_FULLSCREEN_TOGGLE_MODE    GST_D3D11_WINDOW_FULLSCREEN_TOGGLE_MODE_NONE
#define DEFAULT_FULLSCREEN                FALSE

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE_WITH_FEATURES
        (GST_CAPS_FEATURE_MEMORY_D3D11_MEMORY, GST_D3D11_FORMATS) "; "
        GST_VIDEO_CAPS_MAKE_WITH_FEATURES
        (GST_CAPS_FEATURE_MEMORY_D3D11_MEMORY ","
            GST_CAPS_FEATURE_META_GST_VIDEO_OVERLAY_COMPOSITION,
            GST_D3D11_FORMATS) ";"
        GST_VIDEO_CAPS_MAKE (GST_D3D11_FORMATS) "; "
        GST_VIDEO_CAPS_MAKE_WITH_FEATURES
        (GST_CAPS_FEATURE_MEMORY_SYSTEM_MEMORY ","
            GST_CAPS_FEATURE_META_GST_VIDEO_OVERLAY_COMPOSITION,
            GST_D3D11_FORMATS)
    ));

GST_DEBUG_CATEGORY (d3d11_video_sink_bin_debug);
#define GST_CAT_DEFAULT d3d11_video_sink_bin_debug

static void gst_d3d11_video_sink_bin_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_d3d11_video_sink_bin_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static void
gst_d3d11_video_sink_bin_video_overlay_init (GstVideoOverlayInterface * iface);
static void
gst_d3d11_video_sink_bin_navigation_init (GstNavigationInterface * iface);

#define gst_d3d11_video_sink_bin_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstD3D11VideoSinkBin, gst_d3d11_video_sink_bin,
    GST_TYPE_BIN,
    G_IMPLEMENT_INTERFACE (GST_TYPE_VIDEO_OVERLAY,
        gst_d3d11_video_sink_bin_video_overlay_init);
    G_IMPLEMENT_INTERFACE (GST_TYPE_NAVIGATION,
        gst_d3d11_video_sink_bin_navigation_init);
    GST_DEBUG_CATEGORY_INIT (d3d11_video_sink_bin_debug,
        "d3d11videosink", 0, "Direct3D11 Video Sink"));

static void
gst_d3d11_video_sink_bin_class_init (GstD3D11VideoSinkBinClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gobject_class->set_property = gst_d3d11_video_sink_bin_set_property;
  gobject_class->get_property = gst_d3d11_video_sink_bin_get_property;

  /* basesink */
  g_object_class_install_property (gobject_class, PROP_SYNC,
      g_param_spec_boolean ("sync", "Sync", "Sync on the clock", DEFAULT_SYNC,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_MAX_LATENESS,
      g_param_spec_int64 ("max-lateness", "Max Lateness",
          "Maximum number of nanoseconds that a buffer can be late before it "
          "is dropped (-1 unlimited)", -1, G_MAXINT64, DEFAULT_MAX_LATENESS,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_QOS,
      g_param_spec_boolean ("qos", "Qos",
          "Generate Quality-of-Service events upstream", DEFAULT_QOS,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_ASYNC,
      g_param_spec_boolean ("async", "Async",
          "Go asynchronously to PAUSED", DEFAULT_ASYNC,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_TS_OFFSET,
      g_param_spec_int64 ("ts-offset", "TS Offset",
          "Timestamp offset in nanoseconds", G_MININT64, G_MAXINT64,
          DEFAULT_TS_OFFSET, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_ENABLE_LAST_SAMPLE,
      g_param_spec_boolean ("enable-last-sample", "Enable Last Buffer",
          "Enable the last-sample property", DEFAULT_ENABLE_LAST_SAMPLE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_LAST_SAMPLE,
      g_param_spec_boxed ("last-sample", "Last Sample",
          "The last sample received in the sink", GST_TYPE_SAMPLE,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_BLOCKSIZE,
      g_param_spec_uint ("blocksize", "Block size",
          "Size in bytes to pull per buffer (0 = default)", 0, G_MAXUINT,
          DEFAULT_BLOCKSIZE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_RENDER_DELAY,
      g_param_spec_uint64 ("render-delay", "Render Delay",
          "Additional render delay of the sink in nanoseconds", 0, G_MAXUINT64,
          DEFAULT_RENDER_DELAY, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_THROTTLE_TIME,
      g_param_spec_uint64 ("throttle-time", "Throttle time",
          "The time to keep between rendered buffers (0 = disabled)", 0,
          G_MAXUINT64, DEFAULT_THROTTLE_TIME,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_MAX_BITRATE,
      g_param_spec_uint64 ("max-bitrate", "Max Bitrate",
          "The maximum bits per second to render (0 = disabled)", 0,
          G_MAXUINT64, DEFAULT_MAX_BITRATE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_PROCESSING_DEADLINE,
      g_param_spec_uint64 ("processing-deadline", "Processing deadline",
          "Maximum processing deadline in nanoseconds", 0, G_MAXUINT64,
          DEFAULT_PROCESSING_DEADLINE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_STATS,
      g_param_spec_boxed ("stats", "Statistics",
          "Sink Statistics", GST_TYPE_STRUCTURE,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  /* videosink */
  g_object_class_install_property (gobject_class, PROP_SHOW_PREROLL_FRAME,
      g_param_spec_boolean ("show-preroll-frame", "Show preroll frame",
          "Whether to render video frames during preroll",
          DEFAULT_SHOW_PREROLL_FRAME,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

  /* d3d11videosink */
  g_object_class_install_property (gobject_class, PROP_ADAPTER,
      g_param_spec_int ("adapter", "Adapter",
          "Adapter index for creating device (-1 for default)",
          -1, G_MAXINT32, DEFAULT_ADAPTER,
          G_PARAM_READWRITE | GST_PARAM_MUTABLE_READY |
          G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_FORCE_ASPECT_RATIO,
      g_param_spec_boolean ("force-aspect-ratio",
          "Force aspect ratio",
          "When enabled, scaling will respect original aspect ratio",
          DEFAULT_FORCE_ASPECT_RATIO,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_ENABLE_NAVIGATION_EVENTS,
      g_param_spec_boolean ("enable-navigation-events",
          "Enable navigation events",
          "When enabled, navigation events are sent upstream",
          DEFAULT_ENABLE_NAVIGATION_EVENTS,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_FULLSCREEN_TOGGLE_MODE,
      g_param_spec_flags ("fullscreen-toggle-mode",
          "Full screen toggle mode",
          "Full screen toggle mode used to trigger fullscreen mode change",
          GST_D3D11_WINDOW_TOGGLE_MODE_GET_TYPE, DEFAULT_FULLSCREEN_TOGGLE_MODE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_FULLSCREEN,
      g_param_spec_boolean ("fullscreen",
          "fullscreen",
          "Ignored when \"fullscreen-toggle-mode\" does not include \"property\"",
          DEFAULT_FULLSCREEN, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_set_static_metadata (element_class,
      "Direct3D11 video sink bin", "Sink/Video",
      "A Direct3D11 based videosink",
      "Seungha Yang <seungha.yang@navercorp.com>");

  gst_element_class_add_static_pad_template (element_class, &sink_template);
}

static void
gst_d3d11_video_sink_bin_init (GstD3D11VideoSinkBin * self)
{
  GstPad *pad;

  self->upload = gst_element_factory_make ("d3d11upload", NULL);
  if (!self->upload) {
    GST_ERROR_OBJECT (self, "d3d11upload unavailable");
    return;
  }

  self->sink = gst_element_factory_make ("d3d11videosinkelement", NULL);
  if (!self->sink) {
    gst_clear_object (&self->upload);
    GST_ERROR_OBJECT (self, "d3d11videosinkelement unavailable");
    return;
  }

  gst_bin_add_many (GST_BIN (self), self->upload, self->sink, NULL);

  gst_element_link_many (self->upload, self->sink, NULL);

  pad = gst_element_get_static_pad (self->upload, "sink");

  self->sinkpad = gst_ghost_pad_new ("sink", pad);
  gst_element_add_pad (GST_ELEMENT_CAST (self), self->sinkpad);
  gst_object_unref (pad);
}

static void
gst_d3d11_video_sink_bin_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstD3D11VideoSinkBin *self = GST_D3D11_VIDEO_SINK_BIN (object);
  GParamSpec *sink_pspec;

  sink_pspec = g_object_class_find_property (G_OBJECT_GET_CLASS (self->sink),
      pspec->name);

  if (sink_pspec && G_PARAM_SPEC_TYPE (sink_pspec) == G_PARAM_SPEC_TYPE (pspec)) {
    g_object_set_property (G_OBJECT (self->sink), pspec->name, value);
  } else {
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
  }
}

static void
gst_d3d11_video_sink_bin_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstD3D11VideoSinkBin *self = GST_D3D11_VIDEO_SINK_BIN (object);

  g_object_get_property (G_OBJECT (self->sink), pspec->name, value);
}

/* VideoOverlay interface */
static void
gst_d3d11_video_sink_bin_set_window_handle (GstVideoOverlay * overlay,
    guintptr window_id)
{
  GstD3D11VideoSinkBin *self = GST_D3D11_VIDEO_SINK_BIN (overlay);

  gst_video_overlay_set_window_handle (GST_VIDEO_OVERLAY (self->sink),
      window_id);
}

static void
gst_d3d11_video_sink_bin_set_render_rectangle (GstVideoOverlay * overlay,
    gint x, gint y, gint width, gint height)
{
  GstD3D11VideoSinkBin *self = GST_D3D11_VIDEO_SINK_BIN (overlay);

  gst_video_overlay_set_render_rectangle (GST_VIDEO_OVERLAY (self->sink),
      x, y, width, height);
}

static void
gst_d3d11_video_sink_bin_expose (GstVideoOverlay * overlay)
{
  GstD3D11VideoSinkBin *self = GST_D3D11_VIDEO_SINK_BIN (overlay);

  gst_video_overlay_expose (GST_VIDEO_OVERLAY (self->sink));
}

static void
gst_d3d11_video_sink_bin_handle_events (GstVideoOverlay * overlay,
    gboolean handle_events)
{
  GstD3D11VideoSinkBin *self = GST_D3D11_VIDEO_SINK_BIN (overlay);

  gst_video_overlay_handle_events (GST_VIDEO_OVERLAY (self->sink),
      handle_events);
}

static void
gst_d3d11_video_sink_bin_video_overlay_init (GstVideoOverlayInterface * iface)
{
  iface->set_window_handle = gst_d3d11_video_sink_bin_set_window_handle;
  iface->set_render_rectangle = gst_d3d11_video_sink_bin_set_render_rectangle;
  iface->expose = gst_d3d11_video_sink_bin_expose;
  iface->handle_events = gst_d3d11_video_sink_bin_handle_events;
}

/* Navigation interface */
static void
gst_d3d11_video_sink_bin_navigation_send_event (GstNavigation * navigation,
    GstStructure * structure)
{
  GstD3D11VideoSinkBin *self = GST_D3D11_VIDEO_SINK_BIN (navigation);

  gst_navigation_send_event (GST_NAVIGATION (self->sink), structure);
}

static void
gst_d3d11_video_sink_bin_navigation_init (GstNavigationInterface * iface)
{
  iface->send_event = gst_d3d11_video_sink_bin_navigation_send_event;
}
