/* GStreamer
 * Copyright (C) 2012 Roland Krikava <info@bluedigits.com>
 * Copyright (C) 2010-2011 David Hoyt <dhoyt@hoytsoft.org>
 * Copyright (C) 2010 Andoni Morales <ylatuya@gmail.com>
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "d3dvideosink.h"

#define ELEMENT_NAME  "d3dvideosink"

enum
{
  PROP_0,
  PROP_FORCE_ASPECT_RATIO,
  PROP_CREATE_RENDER_WINDOW,
  PROP_STREAM_STOP_ON_CLOSE,
  PROP_ENABLE_NAVIGATION_EVENTS,
  PROP_LAST
};

#define DEFAULT_FORCE_ASPECT_RATIO       TRUE
#define DEFAULT_CREATE_RENDER_WINDOW     TRUE
#define DEFAULT_STREAM_STOP_ON_CLOSE     TRUE
#define DEFAULT_ENABLE_NAVIGATION_EVENTS TRUE

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw, "
        "format = (string) { I420, YV12, UYVY, YUY2, NV12, BGRx, BGRx, xRGB, xBGR, RGBA, BGRA, ARGB, ABGR, RGB, BGR, RGB16, BGR16, RGB15, BGR15 }, "
        "framerate = (fraction) [ 0, MAX ], "
        "width = (int) [ 1, MAX ], " "height = (int) [ 1, MAX ]")
    );

GST_DEBUG_CATEGORY (gst_d3dvideosink_debug);
#define GST_CAT_DEFAULT gst_d3dvideosink_debug

/** FWD DECLS **/
/* Interfaces */
static void gst_d3dvideosink_init_interfaces (GType type);
/* GstXOverlay Interface */
static void
gst_d3dvideosink_video_overlay_interface_init (GstVideoOverlayInterface *
    iface);
static void gst_d3dvideosink_set_window_handle (GstVideoOverlay * overlay,
    guintptr window_id);
static void gst_d3dvideosink_set_render_rectangle (GstVideoOverlay * overlay,
    gint x, gint y, gint width, gint height);
static void gst_d3dvideosink_expose (GstVideoOverlay * overlay);
/* GstNavigation Interface */
static void gst_d3dvideosink_navigation_interface_init (GstNavigationInterface *
    iface);
static void gst_d3dvideosink_navigation_send_event (GstNavigation * navigation,
    GstStructure * structure);
/* GObject */
static void gst_d3dvideosink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_d3dvideosink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_d3dvideosink_finalize (GObject * gobject);
/* GstBaseSink */
static GstCaps *gst_d3dvideosink_get_caps (GstBaseSink * basesink,
    GstCaps * filter);
static gboolean gst_d3dvideosink_set_caps (GstBaseSink * bsink, GstCaps * caps);
static gboolean gst_d3dvideosink_stop (GstBaseSink * sink);
/* GstVideoSink */
static GstFlowReturn gst_d3dvideosink_show_frame (GstVideoSink * vsink,
    GstBuffer * buffer);

#define _do_init \
  G_IMPLEMENT_INTERFACE (GST_TYPE_NAVIGATION, gst_d3dvideosink_navigation_interface_init); \
  G_IMPLEMENT_INTERFACE (GST_TYPE_VIDEO_OVERLAY, gst_d3dvideosink_video_overlay_interface_init); \
  GST_DEBUG_CATEGORY_INIT (gst_d3dvideosink_debug, ELEMENT_NAME, 0, "Direct3D Video");

G_DEFINE_TYPE_WITH_CODE (GstD3DVideoSink, gst_d3dvideosink, GST_TYPE_VIDEO_SINK,
    _do_init);



static void
gst_d3dvideosink_class_init (GstD3DVideoSinkClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstVideoSinkClass *gstvideosink_class;
  GstBaseSinkClass *gstbasesink_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstvideosink_class = (GstVideoSinkClass *) klass;
  gstbasesink_class = (GstBaseSinkClass *) klass;

  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_d3dvideosink_finalize);
  gobject_class->set_property =
      GST_DEBUG_FUNCPTR (gst_d3dvideosink_set_property);
  gobject_class->get_property =
      GST_DEBUG_FUNCPTR (gst_d3dvideosink_get_property);

  gstbasesink_class->get_caps = GST_DEBUG_FUNCPTR (gst_d3dvideosink_get_caps);
  gstbasesink_class->set_caps = GST_DEBUG_FUNCPTR (gst_d3dvideosink_set_caps);
  gstbasesink_class->stop = GST_DEBUG_FUNCPTR (gst_d3dvideosink_stop);

  gstvideosink_class->show_frame =
      GST_DEBUG_FUNCPTR (gst_d3dvideosink_show_frame);

  /* Add properties */
  g_object_class_install_property (G_OBJECT_CLASS (klass),
      PROP_FORCE_ASPECT_RATIO, g_param_spec_boolean ("force-aspect-ratio",
          "Force aspect ratio",
          "When enabled, scaling will respect original aspect ratio",
          DEFAULT_FORCE_ASPECT_RATIO, (GParamFlags) G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass),
      PROP_CREATE_RENDER_WINDOW, g_param_spec_boolean ("create-render-window",
          "Create render window",
          "If no window ID is given, a new render window is created",
          DEFAULT_CREATE_RENDER_WINDOW, (GParamFlags) G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass),
      PROP_STREAM_STOP_ON_CLOSE, g_param_spec_boolean ("stream-stop-on-close",
          "Stop streaming on window close",
          "If the render window is closed stop stream",
          DEFAULT_STREAM_STOP_ON_CLOSE, (GParamFlags) G_PARAM_READWRITE));
#if 0
  g_object_class_install_property (G_OBJECT_CLASS (klass),
      PROP_PIXEL_ASPECT_RATIO, g_param_spec_string ("pixel-aspect-ratio",
          "Pixel Aspect Ratio",
          "The pixel aspect ratio of the device", "1/1",
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
#endif
  g_object_class_install_property (G_OBJECT_CLASS (klass),
      PROP_ENABLE_NAVIGATION_EVENTS,
      g_param_spec_boolean ("enable-navigation-events",
          "Enable navigation events",
          "When enabled, navigation events are sent upstream",
          DEFAULT_ENABLE_NAVIGATION_EVENTS, (GParamFlags) G_PARAM_READWRITE));

  gst_element_class_set_static_metadata (gstelement_class,
      "Direct3D video sink", "Sink/Video",
      "Display data using a Direct3D video renderer",
      "David Hoyt <dhoyt@hoytsoft.org>, Roland Krikava <info@bluedigits.com>");

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&sink_template));

  g_static_rec_mutex_init (&klass->lock);
}

static void
gst_d3dvideosink_init (GstD3DVideoSink * sink)
{
  GST_DEBUG_OBJECT (sink, " ");

  d3d_class_init (sink);

  g_value_init (&sink->par, GST_TYPE_FRACTION);
  gst_value_set_fraction (&sink->par, 1, 1);

  /* Init Properties */
  sink->force_aspect_ratio = DEFAULT_FORCE_ASPECT_RATIO;
  sink->create_internal_window = DEFAULT_CREATE_RENDER_WINDOW;
  sink->stream_stop_on_close = DEFAULT_STREAM_STOP_ON_CLOSE;
  sink->enable_navigation_events = DEFAULT_ENABLE_NAVIGATION_EVENTS;

  g_static_rec_mutex_init (&sink->lock);
}

/** GObject Functions **/

static void
gst_d3dvideosink_finalize (GObject * gobject)
{
  GstD3DVideoSink *sink = GST_D3DVIDEOSINK (gobject);

  GST_DEBUG_OBJECT (sink, " ");

  d3d_class_destroy (sink);

  g_static_rec_mutex_free (&sink->lock);

  G_OBJECT_CLASS (gst_d3dvideosink_parent_class)->finalize (gobject);
}


static void
gst_d3dvideosink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstD3DVideoSink *sink = GST_D3DVIDEOSINK (object);

  switch (prop_id) {
    case PROP_FORCE_ASPECT_RATIO:
      sink->force_aspect_ratio = g_value_get_boolean (value);
      break;
    case PROP_CREATE_RENDER_WINDOW:
      sink->create_internal_window = g_value_get_boolean (value);
      break;
    case PROP_STREAM_STOP_ON_CLOSE:
      sink->stream_stop_on_close = g_value_get_boolean (value);
      break;
    case PROP_ENABLE_NAVIGATION_EVENTS:
      sink->enable_navigation_events = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_d3dvideosink_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstD3DVideoSink *sink = GST_D3DVIDEOSINK (object);

  switch (prop_id) {
    case PROP_FORCE_ASPECT_RATIO:
      g_value_set_boolean (value, sink->force_aspect_ratio);
      break;
    case PROP_CREATE_RENDER_WINDOW:
      g_value_set_boolean (value, sink->create_internal_window);
      break;
    case PROP_STREAM_STOP_ON_CLOSE:
      g_value_set_boolean (value, sink->stream_stop_on_close);
      break;
    case PROP_ENABLE_NAVIGATION_EVENTS:
      g_value_set_boolean (value, sink->enable_navigation_events);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/** GstBaseSinkClass Functions **/

static GstCaps *
gst_d3dvideosink_get_caps (GstBaseSink * basesink, GstCaps * filter)
{
  GstD3DVideoSink *sink = GST_D3DVIDEOSINK (basesink);
  GstCaps *caps;

  caps = d3d_supported_caps (sink);
  if (!caps)
    caps = gst_pad_get_pad_template_caps (GST_VIDEO_SINK_PAD (sink));

  if (caps && filter) {
    GstCaps *isect;
    isect = gst_caps_intersect_full (filter, caps, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (caps);
    caps = isect;
  }

  return caps;
}

static guint16
flip_b16 (guint16 b)
{
  guint16 ret = 0, tmp = 0x8000;
  gint i;

  for (i = 0, tmp = 1; tmp != 0; i++) {
    //printf("%x\n", tmp);
    if (b & tmp)
      ret |= (0x8000 >> i);
    tmp <<= 1;
  }
  return ret;
}

static guint32
flip_b32 (guint32 b, gboolean bitshift)
{
  guint32 ret = 0, tmp = 0x80000000;
  gint i;

  for (i = 0, tmp = 1; tmp != 0; i++) {
    //printf("%x\n", tmp);
    if (b & tmp)
      ret |= (0x80000000 >> i);
    tmp <<= 1;
  }

  if (bitshift && G_BYTE_ORDER == G_LITTLE_ENDIAN)
    ret >>= 8;

  return ret;
}

typedef enum
{
  VFMT_RGBx = 0,
  VFMT_BGRx,
  VFMT_xRGB,
  VFMT_xBGR,
  VFMT_RGBA,
  VFMT_BGRA,
  VFMT_ARGB,
  VFMT_ABGR,
  VFMT_RGB,
  VFMT_BGR,
  VFMT_RGB16,
  VFMT_BGR16,
  VFMT_RGB15,
  VFMT_BGR15,
} VFmtMap;

static GstVideoFormatDetails vfmt_details[] = {
  {                             // GST_VIDEO_FORMAT_RGBx
        0xff000000, 0x00ff0000, 0x0000ff00, 0,
        0, 0, 0, 0,
        0, 8, 16, 0,
        8, 8, 8, 0,
      32, 24, G_BIG_ENDIAN, 4},
  {                             // GST_VIDEO_FORMAT_BGRx
        0x0000ff00, 0x00ff0000, 0xff000000, 0,
        0, 0, 0, 0,
        16, 8, 0, 0,
        8, 8, 8, 0,
      32, 24, G_BIG_ENDIAN, 4},
  {                             // GST_VIDEO_FORMAT_xRGB
        0x00ff0000, 0x0000ff00, 0x000000ff, 0,
        0, 0, 0, 0,
        8, 16, 24, 0,
        8, 8, 8, 0,
      32, 24, G_BIG_ENDIAN, 4},
  {                             // GST_VIDEO_FORMAT_xBGR
        0x000000ff, 0x0000ff00, 0x00ff0000, 0,
        0, 0, 0, 0,
        24, 16, 8, 0,
        8, 8, 8, 0,
      32, 24, G_BIG_ENDIAN, 4},
  {                             // GST_VIDEO_FORMAT_RGBA
        0xff000000, 0x00ff0000, 0x0000ff00, 0x000000ff,
        0, 0, 0, 0,
        0, 8, 16, 24,
        8, 8, 8, 8,
      32, 32, G_BIG_ENDIAN, 4},
  {                             // GST_VIDEO_FORMAT_BGRA
        0x0000ff00, 0x00ff0000, 0xff000000, 0x000000ff,
        0, 0, 0, 0,
        16, 8, 0, 24,
        8, 8, 8, 8,
      32, 32, G_BIG_ENDIAN, 4},
  {                             // GST_VIDEO_FORMAT_ARGB
        0x00ff0000, 0x0000ff00, 0x000000ff, 0xff000000,
        0, 0, 0, 0,
        8, 16, 24, 0,
        8, 8, 8, 8,
      32, 32, G_BIG_ENDIAN, 4},
  {                             // GST_VIDEO_FORMAT_ABGR
        0x000000ff, 0x0000ff00, 0x00ff0000, 0xff000000,
        0, 0, 0, 0,
        24, 16, 8, 0,
        8, 8, 8, 8,
      32, 32, G_BIG_ENDIAN, 4},
  {                             // GST_VIDEO_FORMAT_RGB
        0x00ff0000, 0x0000ff00, 0x000000ff, 0,
        0, 0, 0, 0,
        0, 8, 16, 0,
        8, 8, 8, 0,
      24, 24, G_BIG_ENDIAN, 3},
  {                             // GST_VIDEO_FORMAT_BGR
        0x000000ff, 0x0000ff00, 0x00ff0000, 0,
        0, 0, 0, 0,
        16, 8, 0, 0,
        8, 8, 8, 0,
      24, 24, G_BIG_ENDIAN, 3},
  {                             // GST_VIDEO_FORMAT_RGB16
        0, 0, 0, 0,
        0xf800, 0x07e0, 0x001f, 0,
        11, 5, 0, 0,
        5, 6, 5, 0,
      16, 16, G_LITTLE_ENDIAN, 2},
  {                             // GST_VIDEO_FORMAT_BGR16
        0, 0, 0, 0,
        0x001f, 0x07e0, 0xf800, 0,
        0, 5, 11, 0,
        5, 6, 5, 0,
      16, 16, G_LITTLE_ENDIAN, 2},
  {                             // GST_VIDEO_FORMAT_RGB15
        0, 0, 0, 0,
        0x7c00, 0x03e0, 0x001f, 0,
        10, 5, 0, 0,
        5, 5, 5, 0,
      16, 15, G_LITTLE_ENDIAN, 2},
  {                             // GST_VIDEO_FORMAT_BGR15
        0, 0, 0, 0,
        0x001f, 0x03e0, 0x7c00, 0,
        0, 5, 10, 0,
        5, 5, 5, 0,
      16, 15, G_LITTLE_ENDIAN, 2}
};

static gboolean
gst_video_format_get_rgb_masks (GstD3DVideoSink * sink, GstVideoFormat fmt,
    GstVideoFormatDetails * details)
{
  gboolean ret = FALSE;

  g_return_val_if_fail (details != NULL, FALSE);

  switch (fmt) {
    case GST_VIDEO_FORMAT_RGBx:
      memcpy (details, &vfmt_details[VFMT_RGBx],
          sizeof (GstVideoFormatDetails));
      ret = TRUE;
      break;
    case GST_VIDEO_FORMAT_BGRx:
      memcpy (details, &vfmt_details[VFMT_BGRx],
          sizeof (GstVideoFormatDetails));
      ret = TRUE;
      break;
    case GST_VIDEO_FORMAT_xRGB:
      memcpy (details, &vfmt_details[VFMT_xRGB],
          sizeof (GstVideoFormatDetails));
      ret = TRUE;
      break;
    case GST_VIDEO_FORMAT_xBGR:
      memcpy (details, &vfmt_details[VFMT_xBGR],
          sizeof (GstVideoFormatDetails));
      ret = TRUE;
      break;
    case GST_VIDEO_FORMAT_RGBA:
      memcpy (details, &vfmt_details[VFMT_RGBA],
          sizeof (GstVideoFormatDetails));
      ret = TRUE;
      break;
    case GST_VIDEO_FORMAT_BGRA:
      memcpy (details, &vfmt_details[VFMT_BGRA],
          sizeof (GstVideoFormatDetails));
      ret = TRUE;
      break;
    case GST_VIDEO_FORMAT_ARGB:
      memcpy (details, &vfmt_details[VFMT_ARGB],
          sizeof (GstVideoFormatDetails));
      ret = TRUE;
      break;
    case GST_VIDEO_FORMAT_ABGR:
      memcpy (details, &vfmt_details[VFMT_ABGR],
          sizeof (GstVideoFormatDetails));
      ret = TRUE;
      break;
    case GST_VIDEO_FORMAT_RGB:
      memcpy (details, &vfmt_details[VFMT_RGB], sizeof (GstVideoFormatDetails));
      ret = TRUE;
      break;
    case GST_VIDEO_FORMAT_BGR:
      memcpy (details, &vfmt_details[VFMT_BGR], sizeof (GstVideoFormatDetails));
      ret = TRUE;
      break;
    case GST_VIDEO_FORMAT_RGB16:
      memcpy (details, &vfmt_details[VFMT_RGB16],
          sizeof (GstVideoFormatDetails));
      ret = TRUE;
      break;
    case GST_VIDEO_FORMAT_BGR16:
      memcpy (details, &vfmt_details[VFMT_BGR16],
          sizeof (GstVideoFormatDetails));
      ret = TRUE;
      break;
    case GST_VIDEO_FORMAT_RGB15:
      memcpy (details, &vfmt_details[VFMT_RGB15],
          sizeof (GstVideoFormatDetails));
      ret = TRUE;
      break;
    case GST_VIDEO_FORMAT_BGR15:
      memcpy (details, &vfmt_details[VFMT_BGR15],
          sizeof (GstVideoFormatDetails));
      ret = TRUE;
      break;
    default:;
  }

  if (ret) {
    if (details->endianness != G_BYTE_ORDER) {
      gboolean bitshift = (details->bpp == 24 && details->depth == 24);
      GST_DEBUG_OBJECT (sink, "Flipping masks%s, byte order missmatch",
          bitshift ? " (w/ bitshift)" : "");
      details->r_mask = flip_b32 (details->r_mask, bitshift);
      details->g_mask = flip_b32 (details->g_mask, bitshift);
      details->b_mask = flip_b32 (details->b_mask, bitshift);
      details->a_mask = flip_b32 (details->a_mask, bitshift);
      details->r_mask16 = flip_b16 (details->r_mask16);
      details->g_mask16 = flip_b16 (details->g_mask16);
      details->b_mask16 = flip_b16 (details->b_mask16);
      details->a_mask16 = flip_b16 (details->a_mask16);
    }
    if (details->bpp == 16) {
      GST_DEBUG_OBJECT (sink, "RED   MASK: 0x%04x SHIFT: %2u BITS: %u  (%u)",
          details->r_mask16, details->r_shift, details->r_bits,
          details->r_mask16);
      GST_DEBUG_OBJECT (sink, "GREEN MASK: 0x%04x SHIFT: %2u BITS: %u  (%u)",
          details->g_mask16, details->g_shift, details->g_bits,
          details->g_mask16);
      GST_DEBUG_OBJECT (sink, "BLUE  MASK: 0x%04x SHIFT: %2u BITS: %u  (%u)",
          details->b_mask16, details->b_shift, details->b_bits,
          details->b_mask16);
      GST_DEBUG_OBJECT (sink, "ALPHA MASK: 0x%04x SHIFT: %2u BITS: %u  (%u)",
          details->a_mask16, details->a_shift, details->a_bits,
          details->a_mask16);
    } else {
      GST_DEBUG_OBJECT (sink, "RED   MASK: 0x%08x SHIFT: %2u BITS: %u  (%u)",
          details->r_mask, details->r_shift, details->r_bits, details->r_mask);
      GST_DEBUG_OBJECT (sink, "GREEN MASK: 0x%08x SHIFT: %2u BITS: %u  (%u)",
          details->g_mask, details->g_shift, details->g_bits, details->g_mask);
      GST_DEBUG_OBJECT (sink, "BLUE  MASK: 0x%08x SHIFT: %2u BITS: %u  (%u)",
          details->b_mask, details->b_shift, details->b_bits, details->b_mask);
      GST_DEBUG_OBJECT (sink, "ALPHA MASK: 0x%08x SHIFT: %2u BITS: %u  (%u)",
          details->a_mask, details->a_shift, details->a_bits, details->a_mask);
    }
    GST_DEBUG_OBJECT (sink, "ENDIANESS: %s",
        (details->endianness ==
            G_BIG_ENDIAN) ? "G_BIG_ENDIAN" : "G_LITTLE_ENDIAN");
    GST_DEBUG_OBJECT (sink, "PIXEL WIDTH: %d", details->pixel_width);
  }

  return ret;
}

static gboolean
gst_d3dvideosink_set_caps (GstBaseSink * bsink, GstCaps * caps)
{
  GstD3DVideoSink *sink;
  GstCaps *sink_caps;
  gint video_width, video_height;
  gint video_par_n, video_par_d;        /* video's PAR */
  gint display_par_n, display_par_d;    /* display's PAR */
  gint fps_n, fps_d;
  guint num, den;
  gchar *tmp = NULL;

  GST_DEBUG_OBJECT (bsink, " ");

  GST_DEBUG_OBJECT (bsink, "Caps: %s", (tmp = gst_caps_to_string (caps)));
  sink = GST_D3DVIDEOSINK (bsink);

  sink_caps = d3d_supported_caps (sink);

  if (!gst_caps_can_intersect (sink_caps, caps))
    goto incompatible_caps;

  memset (&sink->info, 0, sizeof (GstVideoInfo));
  if (!gst_video_info_from_caps (&sink->info, caps))
    goto invalid_format;

  sink->format = sink->info.finfo->format;
  video_width = sink->info.width;
  video_height = sink->info.height;
  fps_n = sink->info.fps_n;
  fps_d = sink->info.fps_d;
  video_par_n = sink->info.par_n;
  video_par_d = sink->info.par_d;

  GST_DEBUG_OBJECT (bsink, "Set Caps Format: %s",
      gst_video_format_to_string (sink->format));

  if (GST_VIDEO_INFO_IS_RGB (&sink->info)) {
    if (!gst_video_format_get_rgb_masks (sink, sink->format,
            &sink->fmt_details)) {
      GST_ERROR_OBJECT (sink, "No RGB mapping found for format: %s",
          gst_video_format_to_string (sink->format));
      goto incompatible_caps;
    }
  }

  /* get aspect ratio from caps if it's present, and
   * convert video width and height to a display width and height
   * using wd / hd = wv / hv * PARv / PARd */

  /* get video's PAR */
  display_par_n = gst_value_get_fraction_numerator (&sink->par);
  display_par_d = gst_value_get_fraction_denominator (&sink->par);

  if (!gst_video_calculate_display_ratio (&num, &den, video_width,
          video_height, video_par_n, video_par_d, display_par_n, display_par_d))
    goto no_disp_ratio;

  GST_DEBUG_OBJECT (sink,
      "video width/height: %dx%d, calculated display ratio: %d/%d format: %u",
      video_width, video_height, num, den, sink->format);

  /* now find a width x height that respects this display ratio.
   * prefer those that have one of w/h the same as the incoming video
   * using wd / hd = num / den
   */

  /* start with same height, because of interlaced video
   * check hd / den is an integer scale factor, and scale wd with the PAR
   */
  if (video_height % den == 0) {
    GST_DEBUG_OBJECT (sink, "keeping video height");
    GST_VIDEO_SINK_WIDTH (sink) = (guint)
        gst_util_uint64_scale_int (video_height, num, den);
    GST_VIDEO_SINK_HEIGHT (sink) = video_height;
  } else if (video_width % num == 0) {
    GST_DEBUG_OBJECT (sink, "keeping video width");
    GST_VIDEO_SINK_WIDTH (sink) = video_width;
    GST_VIDEO_SINK_HEIGHT (sink) = (guint)
        gst_util_uint64_scale_int (video_width, den, num);
  } else {
    GST_DEBUG_OBJECT (sink, "approximating while keeping video height");
    GST_VIDEO_SINK_WIDTH (sink) = (guint)
        gst_util_uint64_scale_int (video_height, num, den);
    GST_VIDEO_SINK_HEIGHT (sink) = video_height;
  }
  GST_DEBUG_OBJECT (sink, "scaling to %dx%d",
      GST_VIDEO_SINK_WIDTH (sink), GST_VIDEO_SINK_HEIGHT (sink));

  if (GST_VIDEO_SINK_WIDTH (sink) <= 0 || GST_VIDEO_SINK_HEIGHT (sink) <= 0)
    goto no_display_size;

  sink->width = video_width;
  sink->height = video_height;

  GST_DEBUG_OBJECT (bsink, "Selected caps: %s", (tmp =
          gst_caps_to_string (caps)));
  g_free (tmp);

  if (!d3d_set_render_format (sink))
    goto incompatible_caps;

  /* Create a window (or start using an application-supplied one, then connect the graph */
  d3d_prepare_window (sink);

  return TRUE;
  /* ERRORS */
incompatible_caps:
  {
    GST_ERROR_OBJECT (sink, "caps incompatible");
    return FALSE;
  }
invalid_format:
  {
    gchar *caps_txt = gst_caps_to_string (caps);
    GST_DEBUG_OBJECT (sink,
        "Could not locate image format from caps %s", caps_txt);
    g_free (caps_txt);
    return FALSE;
  }
no_disp_ratio:
  {
    GST_ELEMENT_ERROR (sink, CORE, NEGOTIATION, (NULL),
        ("Error calculating the output display ratio of the video."));
    return FALSE;
  }
no_display_size:
  {
    GST_ELEMENT_ERROR (sink, CORE, NEGOTIATION, (NULL),
        ("Error calculating the output display ratio of the video."));
    return FALSE;
  }
}

static gboolean
gst_d3dvideosink_stop (GstBaseSink * bsink)
{
  GstD3DVideoSink *sink = GST_D3DVIDEOSINK (bsink);
  GST_DEBUG_OBJECT (bsink, "Stop() called");
  d3d_stop (sink);
  return TRUE;
}

/** PUBLIC FUNCTIONS **/

/* Iterface Registrations */

static void
gst_d3dvideosink_video_overlay_interface_init (GstVideoOverlayInterface * iface)
{
  iface->set_window_handle = gst_d3dvideosink_set_window_handle;
  iface->set_render_rectangle = gst_d3dvideosink_set_render_rectangle;
  iface->expose = gst_d3dvideosink_expose;
}

static void
gst_d3dvideosink_navigation_interface_init (GstNavigationInterface * iface)
{
  iface->send_event = gst_d3dvideosink_navigation_send_event;
}

/* Video Render Code */

static void
gst_d3dvideosink_set_window_handle (GstVideoOverlay * overlay,
    guintptr window_id)
{
  d3d_set_window_handle (GST_D3DVIDEOSINK (overlay), window_id, FALSE);
}

static void
gst_d3dvideosink_set_render_rectangle (GstVideoOverlay * overlay, gint x,
    gint y, gint width, gint height)
{
  GstD3DVideoSink *sink = GST_D3DVIDEOSINK (overlay);
  sink->render_rect.x = x;
  sink->render_rect.y = y;
  sink->render_rect.w = width;
  sink->render_rect.h = height;
  d3d_set_render_rectangle (sink);
}

static void
gst_d3dvideosink_expose (GstVideoOverlay * overlay)
{
  GstD3DVideoSink *sink = GST_D3DVIDEOSINK (overlay);
  d3d_expose_window (sink);
}

static GstFlowReturn
gst_d3dvideosink_show_frame (GstVideoSink * vsink, GstBuffer * buffer)
{
  GstD3DVideoSink *sink = GST_D3DVIDEOSINK (vsink);
  return d3d_render_buffer (sink, buffer);
}

/* Video Navigation Events */

static void
gst_d3dvideosink_navigation_send_event (GstNavigation * navigation,
    GstStructure * structure)
{
  GstD3DVideoSink *sink = GST_D3DVIDEOSINK (navigation);
  GstEvent *e;

  if ((e = gst_event_new_navigation (structure))) {
    GstPad *pad;
    if ((pad = gst_pad_get_peer (GST_VIDEO_SINK_PAD (sink)))) {
      gst_pad_send_event (pad, e);
      gst_object_unref (pad);
    }
  }
}

/** PRIVATE FUNCTIONS **/


/* Plugin entry point */
static gboolean
plugin_init (GstPlugin * plugin)
{
  /* PRIMARY: this is the best videosink to use on windows */
  if (!gst_element_register (plugin, ELEMENT_NAME,
          GST_RANK_PRIMARY, GST_TYPE_D3DVIDEOSINK))
    return FALSE;

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    d3dsinkwrapper,
    "Direct3D sink wrapper plugin",
    plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
