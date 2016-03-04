/* GStreamer
 * Copyright (C) <2011> Jon Nordby <jononor@gmail.com>
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
 * SECTION:element-cairooverlay
 *
 * cairooverlay renders an overlay using a application provided render function.
 *
 * The full example can be found in tests/examples/cairo/cairo_overlay.c
 * <refsect2>
 * <title>Example code</title>
 * |[
 *
 * #include &lt;gst/gst.h&gt;
 * #include &lt;gst/video/video.h&gt;
 *
 * ...
 *
 * typedef struct {
 *   gboolean valid;
 *   int width;
 *   int height;
 * } CairoOverlayState;
 * 
 * ...
 *
 * static void
 * prepare_overlay (GstElement * overlay, GstCaps * caps, gpointer user_data)
 * {
 *   CairoOverlayState *state = (CairoOverlayState *)user_data;
 *
 *   gst_video_format_parse_caps (caps, NULL, &amp;state-&gt;width, &amp;state-&gt;height);
 *   state-&gt;valid = TRUE;
 * }
 *
 * static void
 * draw_overlay (GstElement * overlay, cairo_t * cr, guint64 timestamp, 
 *   guint64 duration, gpointer user_data)
 * {
 *   CairoOverlayState *s = (CairoOverlayState *)user_data;
 *   double scale;
 *
 *   if (!s-&gt;valid)
 *     return;
 *
 *   scale = 2*(((timestamp/(int)1e7) % 70)+30)/100.0;
 *   cairo_translate(cr, s-&gt;width/2, (s-&gt;height/2)-30);
 *   cairo_scale (cr, scale, scale);
 *
 *   cairo_move_to (cr, 0, 0);
 *   cairo_curve_to (cr, 0,-30, -50,-30, -50,0);
 *   cairo_curve_to (cr, -50,30, 0,35, 0,60 );
 *   cairo_curve_to (cr, 0,35, 50,30, 50,0 ); *  
 *   cairo_curve_to (cr, 50,-30, 0,-30, 0,0 );
 *   cairo_set_source_rgba (cr, 0.9, 0.0, 0.1, 0.7);
 *   cairo_fill (cr);
 * }
 *
 * ...
 *
 * cairo_overlay = gst_element_factory_make (&quot;cairooverlay&quot;, &quot;overlay&quot;);
 *
 * g_signal_connect (cairo_overlay, &quot;draw&quot;, G_CALLBACK (draw_overlay),
 *   overlay_state);
 * g_signal_connect (cairo_overlay, &quot;caps-changed&quot;, 
 *   G_CALLBACK (prepare_overlay), overlay_state);
 * ...
 *
 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstcairooverlay.h"

#include <gst/video/video.h>

#include <cairo.h>

/* RGB16 is native-endianness in GStreamer */
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
#define TEMPLATE_CAPS GST_VIDEO_CAPS_MAKE("{ BGRx, BGRA, RGB16 }")
#else
#define TEMPLATE_CAPS GST_VIDEO_CAPS_MAKE("{ xRGB, ARGB, RGB16 }")
#endif

static GstStaticPadTemplate gst_cairo_overlay_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (TEMPLATE_CAPS)
    );

static GstStaticPadTemplate gst_cairo_overlay_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (TEMPLATE_CAPS)
    );

G_DEFINE_TYPE (GstCairoOverlay, gst_cairo_overlay, GST_TYPE_VIDEO_FILTER);

enum
{
  SIGNAL_DRAW,
  SIGNAL_CAPS_CHANGED,
  N_SIGNALS
};

static guint gst_cairo_overlay_signals[N_SIGNALS];

static gboolean
gst_cairo_overlay_set_info (GstVideoFilter * vfilter, GstCaps * in_caps,
    GstVideoInfo * in_info, GstCaps * out_caps, GstVideoInfo * out_info)
{
  GstCairoOverlay *overlay = GST_CAIRO_OVERLAY (vfilter);

  g_signal_emit (overlay, gst_cairo_overlay_signals[SIGNAL_CAPS_CHANGED], 0,
      in_caps, NULL);

  return TRUE;
}

static GstFlowReturn
gst_cairo_overlay_transform_frame_ip (GstVideoFilter * vfilter,
    GstVideoFrame * frame)
{
  GstCairoOverlay *overlay = GST_CAIRO_OVERLAY (vfilter);
  cairo_surface_t *surface;
  cairo_t *cr;
  cairo_format_t format;

  switch (GST_VIDEO_FRAME_FORMAT (frame)) {
    case GST_VIDEO_FORMAT_ARGB:
    case GST_VIDEO_FORMAT_BGRA:
      format = CAIRO_FORMAT_ARGB32;
      break;
    case GST_VIDEO_FORMAT_xRGB:
    case GST_VIDEO_FORMAT_BGRx:
      format = CAIRO_FORMAT_RGB24;
      break;
    case GST_VIDEO_FORMAT_RGB16:
      format = CAIRO_FORMAT_RGB16_565;
      break;
    default:
    {
      GST_WARNING ("No matching cairo format for %s",
          gst_video_format_to_string (GST_VIDEO_FRAME_FORMAT (frame)));
      return GST_FLOW_ERROR;
    }
  }

  surface =
      cairo_image_surface_create_for_data (GST_VIDEO_FRAME_PLANE_DATA (frame,
          0), format, GST_VIDEO_FRAME_WIDTH (frame),
      GST_VIDEO_FRAME_HEIGHT (frame), GST_VIDEO_FRAME_PLANE_STRIDE (frame, 0));

  if (G_UNLIKELY (!surface))
    return GST_FLOW_ERROR;

  cr = cairo_create (surface);
  if (G_UNLIKELY (!cr)) {
    cairo_surface_destroy (surface);
    return GST_FLOW_ERROR;
  }

  g_signal_emit (overlay, gst_cairo_overlay_signals[SIGNAL_DRAW], 0,
      cr, GST_BUFFER_PTS (frame->buffer), GST_BUFFER_DURATION (frame->buffer),
      NULL);

  cairo_destroy (cr);
  cairo_surface_destroy (surface);

  return GST_FLOW_OK;
}

static void
gst_cairo_overlay_class_init (GstCairoOverlayClass * klass)
{
  GstVideoFilterClass *vfilter_class;
  GstElementClass *element_class;

  vfilter_class = (GstVideoFilterClass *) klass;
  element_class = (GstElementClass *) klass;

  vfilter_class->set_info = gst_cairo_overlay_set_info;
  vfilter_class->transform_frame_ip = gst_cairo_overlay_transform_frame_ip;

  /**
   * GstCairoOverlay::draw:
   * @overlay: Overlay element emitting the signal.
   * @cr: Cairo context to draw to.
   * @timestamp: Timestamp (see #GstClockTime) of the current buffer.
   * @duration: Duration (see #GstClockTime) of the current buffer.
   * 
   * This signal is emitted when the overlay should be drawn.
   */
  gst_cairo_overlay_signals[SIGNAL_DRAW] =
      g_signal_new ("draw",
      G_TYPE_FROM_CLASS (klass),
      0,
      0,
      NULL,
      NULL,
      g_cclosure_marshal_generic,
      G_TYPE_NONE, 3, CAIRO_GOBJECT_TYPE_CONTEXT, G_TYPE_UINT64, G_TYPE_UINT64);

  /**
   * GstCairoOverlay::caps-changed:
   * @overlay: Overlay element emitting the signal.
   * @caps: The #GstCaps of the element.
   * 
   * This signal is emitted when the caps of the element has changed.
   */
  gst_cairo_overlay_signals[SIGNAL_CAPS_CHANGED] =
      g_signal_new ("caps-changed",
      G_TYPE_FROM_CLASS (klass),
      0,
      0, NULL, NULL, g_cclosure_marshal_generic, G_TYPE_NONE, 1, GST_TYPE_CAPS);

  gst_element_class_set_static_metadata (element_class, "Cairo overlay",
      "Filter/Editor/Video",
      "Render overlay on a video stream using Cairo",
      "Jon Nordby <jononor@gmail.com>");

  gst_element_class_add_static_pad_template (element_class,
      &gst_cairo_overlay_sink_template);
  gst_element_class_add_static_pad_template (element_class,
      &gst_cairo_overlay_src_template);
}

static void
gst_cairo_overlay_init (GstCairoOverlay * overlay)
{
  /* nothing to do */
}
