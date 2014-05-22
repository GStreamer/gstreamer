/* Video compositor plugin
 * Copyright (C) 2004, 2008 Wim Taymans <wim@fluendo.com>
 * Copyright (C) 2010 Sebastian Dröge <sebastian.droege@collabora.co.uk>
 * Copyright (C) 2014 Mathieu Duponchelle <mathieu.duponchelle@opencreed.com>
 * Copyright (C) 2014 Thibault Saunier <tsaunier@gnome.org>
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
 * SECTION:element-compositor
 *
 * Compositor can accept AYUV, ARGB and BGRA video streams. For each of the requested
 * sink pads it will compare the incoming geometry and framerate to define the
 * output parameters. Indeed output video frames will have the geometry of the
 * biggest incoming video stream and the framerate of the fastest incoming one.
 *
 * Compositor will do colorspace conversion.
 * 
 * Individual parameters for each input stream can be configured on the
 * #GstCompositorPad.
 *
 * <refsect2>
 * <title>Sample pipelines</title>
 * |[
 * gst-launch-1.0 \
 *   videotestsrc pattern=1 ! \
 *   video/x-raw,format=AYUV,framerate=\(fraction\)10/1,width=100,height=100 ! \
 *   videobox border-alpha=0 top=-70 bottom=-70 right=-220 ! \
 *   compositor name=comp sink_0::alpha=0.7 sink_1::alpha=0.5 ! \
 *   videoconvert ! xvimagesink \
 *   videotestsrc ! \
 *   video/x-raw,format=AYUV,framerate=\(fraction\)5/1,width=320,height=240 ! comp.
 * ]| A pipeline to demonstrate compositor used together with videobox.
 * This should show a 320x240 pixels video test source with some transparency
 * showing the background checker pattern. Another video test source with just
 * the snow pattern of 100x100 pixels is overlayed on top of the first one on
 * the left vertically centered with a small transparency showing the first
 * video test source behind and the checker pattern under it. Note that the
 * framerate of the output video is 10 frames per second.
 * |[
 * gst-launch-1.0 videotestsrc pattern=1 ! \
 *   video/x-raw, framerate=\(fraction\)10/1, width=100, height=100 ! \
 *   compositor name=comp ! videoconvert ! ximagesink \
 *   videotestsrc !  \
 *   video/x-raw, framerate=\(fraction\)5/1, width=320, height=240 ! comp.
 * ]| A pipeline to demostrate bgra comping. (This does not demonstrate alpha blending). 
 * |[
 * gst-launch-1.0 videotestsrc pattern=1 ! \
 *   video/x-raw,format =I420, framerate=\(fraction\)10/1, width=100, height=100 ! \
 *   compositor name=comp ! videoconvert ! ximagesink \
 *   videotestsrc ! \
 *   video/x-raw,format=I420, framerate=\(fraction\)5/1, width=320, height=240 ! comp.
 * ]| A pipeline to test I420
 * |[
 * gst-launch-1.0 compositor name=comp sink_1::alpha=0.5 sink_1::xpos=50 sink_1::ypos=50 ! \
 *   videoconvert ! ximagesink \
 *   videotestsrc pattern=snow timestamp-offset=3000000000 ! \
 *   "video/x-raw,format=AYUV,width=640,height=480,framerate=(fraction)30/1" ! \
 *   timeoverlay ! queue2 ! comp. \
 *   videotestsrc pattern=smpte ! \
 *   "video/x-raw,format=AYUV,width=800,height=600,framerate=(fraction)10/1" ! \
 *   timeoverlay ! queue2 ! comp.
 * ]| A pipeline to demonstrate synchronized compositing (the second stream starts after 3 seconds)
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include "compositor.h"
#include "compositorpad.h"

#ifdef DISABLE_ORC
#define orc_memset memset
#else
#include <orc/orcfunctions.h>
#endif

GST_DEBUG_CATEGORY_STATIC (gst_compositor_debug);
#define GST_CAT_DEFAULT gst_compositor_debug

#define FORMATS " { AYUV, BGRA, ARGB, RGBA, ABGR, Y444, Y42B, YUY2, UYVY, "\
                "   YVYU, I420, YV12, NV12, NV21, Y41B, RGB, BGR, xRGB, xBGR, "\
                "   RGBx, BGRx } "

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE (FORMATS))
    );

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink_%u",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE (FORMATS))
    );

#define DEFAULT_PAD_ZORDER 0
#define DEFAULT_PAD_XPOS   0
#define DEFAULT_PAD_YPOS   0
#define DEFAULT_PAD_ALPHA  1.0
enum
{
  PROP_PAD_0,
  PROP_PAD_ZORDER,
  PROP_PAD_XPOS,
  PROP_PAD_YPOS,
  PROP_PAD_ALPHA
};

G_DEFINE_TYPE (GstCompositorPad, gst_compositor_pad,
    GST_TYPE_VIDEO_AGGREGATOR_PAD);

static void
gst_compositor_pad_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstCompositorPad *pad = GST_COMPOSITOR_PAD (object);

  switch (prop_id) {
    case PROP_PAD_ZORDER:
      g_value_set_uint (value, pad->zorder);
      break;
    case PROP_PAD_XPOS:
      g_value_set_int (value, pad->xpos);
      break;
    case PROP_PAD_YPOS:
      g_value_set_int (value, pad->ypos);
      break;
    case PROP_PAD_ALPHA:
      g_value_set_double (value, pad->alpha);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_compositor_pad_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstCompositorPad *pad = GST_COMPOSITOR_PAD (object);

  switch (prop_id) {
    case PROP_PAD_XPOS:
      pad->xpos = g_value_get_int (value);
      break;
    case PROP_PAD_YPOS:
      pad->ypos = g_value_get_int (value);
      break;
    case PROP_PAD_ALPHA:
      pad->alpha = g_value_get_double (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_compositor_pad_class_init (GstCompositorPadClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;

  gobject_class->set_property = gst_compositor_pad_set_property;
  gobject_class->get_property = gst_compositor_pad_get_property;

  g_object_class_install_property (gobject_class, PROP_PAD_XPOS,
      g_param_spec_int ("xpos", "X Position", "X Position of the picture",
          G_MININT, G_MAXINT, DEFAULT_PAD_XPOS,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_PAD_YPOS,
      g_param_spec_int ("ypos", "Y Position", "Y Position of the picture",
          G_MININT, G_MAXINT, DEFAULT_PAD_YPOS,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_PAD_ALPHA,
      g_param_spec_double ("alpha", "Alpha", "Alpha of the picture", 0.0, 1.0,
          DEFAULT_PAD_ALPHA,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));
}

static void
gst_compositor_pad_init (GstCompositorPad * compo_pad)
{
  compo_pad->xpos = DEFAULT_PAD_XPOS;
  compo_pad->ypos = DEFAULT_PAD_YPOS;
  compo_pad->alpha = DEFAULT_PAD_ALPHA;
}


/* GstCompositor */
#define DEFAULT_BACKGROUND COMPOSITOR_BACKGROUND_CHECKER
enum
{
  PROP_0,
  PROP_BACKGROUND
};

#define GST_TYPE_COMPOSITOR_BACKGROUND (gst_compositor_background_get_type())
static GType
gst_compositor_background_get_type (void)
{
  static GType compositor_background_type = 0;

  static const GEnumValue compositor_background[] = {
    {COMPOSITOR_BACKGROUND_CHECKER, "Checker pattern", "checker"},
    {COMPOSITOR_BACKGROUND_BLACK, "Black", "black"},
    {COMPOSITOR_BACKGROUND_WHITE, "White", "white"},
    {COMPOSITOR_BACKGROUND_TRANSPARENT,
        "Transparent Background to enable further compositing", "transparent"},
    {0, NULL, NULL},
  };

  if (!compositor_background_type) {
    compositor_background_type =
        g_enum_register_static ("GstCompositorBackground",
        compositor_background);
  }
  return compositor_background_type;
}

static void
gst_compositor_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstCompositor *self = GST_COMPOSITOR (object);

  switch (prop_id) {
    case PROP_BACKGROUND:
      g_value_set_enum (value, self->background);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_compositor_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstCompositor *self = GST_COMPOSITOR (object);

  switch (prop_id) {
    case PROP_BACKGROUND:
      self->background = g_value_get_enum (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

#define gst_compositor_parent_class parent_class
G_DEFINE_TYPE (GstCompositor, gst_compositor, GST_TYPE_VIDEO_AGGREGATOR);

static gboolean
set_functions (GstCompositor * self, GstVideoInfo * info)
{
  gboolean ret = FALSE;

  self->blend = NULL;
  self->overlay = NULL;
  self->fill_checker = NULL;
  self->fill_color = NULL;

  switch (GST_VIDEO_INFO_FORMAT (info)) {
    case GST_VIDEO_FORMAT_AYUV:
      self->blend = gst_compositor_blend_ayuv;
      self->overlay = gst_compositor_overlay_ayuv;
      self->fill_checker = gst_compositor_fill_checker_ayuv;
      self->fill_color = gst_compositor_fill_color_ayuv;
      ret = TRUE;
      break;
    case GST_VIDEO_FORMAT_ARGB:
      self->blend = gst_compositor_blend_argb;
      self->overlay = gst_compositor_overlay_argb;
      self->fill_checker = gst_compositor_fill_checker_argb;
      self->fill_color = gst_compositor_fill_color_argb;
      ret = TRUE;
      break;
    case GST_VIDEO_FORMAT_BGRA:
      self->blend = gst_compositor_blend_bgra;
      self->overlay = gst_compositor_overlay_bgra;
      self->fill_checker = gst_compositor_fill_checker_bgra;
      self->fill_color = gst_compositor_fill_color_bgra;
      ret = TRUE;
      break;
    case GST_VIDEO_FORMAT_ABGR:
      self->blend = gst_compositor_blend_abgr;
      self->overlay = gst_compositor_overlay_abgr;
      self->fill_checker = gst_compositor_fill_checker_abgr;
      self->fill_color = gst_compositor_fill_color_abgr;
      ret = TRUE;
      break;
    case GST_VIDEO_FORMAT_RGBA:
      self->blend = gst_compositor_blend_rgba;
      self->overlay = gst_compositor_overlay_rgba;
      self->fill_checker = gst_compositor_fill_checker_rgba;
      self->fill_color = gst_compositor_fill_color_rgba;
      ret = TRUE;
      break;
    case GST_VIDEO_FORMAT_Y444:
      self->blend = gst_compositor_blend_y444;
      self->overlay = self->blend;
      self->fill_checker = gst_compositor_fill_checker_y444;
      self->fill_color = gst_compositor_fill_color_y444;
      ret = TRUE;
      break;
    case GST_VIDEO_FORMAT_Y42B:
      self->blend = gst_compositor_blend_y42b;
      self->overlay = self->blend;
      self->fill_checker = gst_compositor_fill_checker_y42b;
      self->fill_color = gst_compositor_fill_color_y42b;
      ret = TRUE;
      break;
    case GST_VIDEO_FORMAT_YUY2:
      self->blend = gst_compositor_blend_yuy2;
      self->overlay = self->blend;
      self->fill_checker = gst_compositor_fill_checker_yuy2;
      self->fill_color = gst_compositor_fill_color_yuy2;
      ret = TRUE;
      break;
    case GST_VIDEO_FORMAT_UYVY:
      self->blend = gst_compositor_blend_uyvy;
      self->overlay = self->blend;
      self->fill_checker = gst_compositor_fill_checker_uyvy;
      self->fill_color = gst_compositor_fill_color_uyvy;
      ret = TRUE;
      break;
    case GST_VIDEO_FORMAT_YVYU:
      self->blend = gst_compositor_blend_yvyu;
      self->overlay = self->blend;
      self->fill_checker = gst_compositor_fill_checker_yvyu;
      self->fill_color = gst_compositor_fill_color_yvyu;
      ret = TRUE;
      break;
    case GST_VIDEO_FORMAT_I420:
      self->blend = gst_compositor_blend_i420;
      self->overlay = self->blend;
      self->fill_checker = gst_compositor_fill_checker_i420;
      self->fill_color = gst_compositor_fill_color_i420;
      ret = TRUE;
      break;
    case GST_VIDEO_FORMAT_YV12:
      self->blend = gst_compositor_blend_yv12;
      self->overlay = self->blend;
      self->fill_checker = gst_compositor_fill_checker_yv12;
      self->fill_color = gst_compositor_fill_color_yv12;
      ret = TRUE;
      break;
    case GST_VIDEO_FORMAT_NV12:
      self->blend = gst_compositor_blend_nv12;
      self->overlay = self->blend;
      self->fill_checker = gst_compositor_fill_checker_nv12;
      self->fill_color = gst_compositor_fill_color_nv12;
      ret = TRUE;
      break;
    case GST_VIDEO_FORMAT_NV21:
      self->blend = gst_compositor_blend_nv21;
      self->overlay = self->blend;
      self->fill_checker = gst_compositor_fill_checker_nv21;
      self->fill_color = gst_compositor_fill_color_nv21;
      ret = TRUE;
      break;
    case GST_VIDEO_FORMAT_Y41B:
      self->blend = gst_compositor_blend_y41b;
      self->overlay = self->blend;
      self->fill_checker = gst_compositor_fill_checker_y41b;
      self->fill_color = gst_compositor_fill_color_y41b;
      ret = TRUE;
      break;
    case GST_VIDEO_FORMAT_RGB:
      self->blend = gst_compositor_blend_rgb;
      self->overlay = self->blend;
      self->fill_checker = gst_compositor_fill_checker_rgb;
      self->fill_color = gst_compositor_fill_color_rgb;
      ret = TRUE;
      break;
    case GST_VIDEO_FORMAT_BGR:
      self->blend = gst_compositor_blend_bgr;
      self->overlay = self->blend;
      self->fill_checker = gst_compositor_fill_checker_bgr;
      self->fill_color = gst_compositor_fill_color_bgr;
      ret = TRUE;
      break;
    case GST_VIDEO_FORMAT_xRGB:
      self->blend = gst_compositor_blend_xrgb;
      self->overlay = self->blend;
      self->fill_checker = gst_compositor_fill_checker_xrgb;
      self->fill_color = gst_compositor_fill_color_xrgb;
      ret = TRUE;
      break;
    case GST_VIDEO_FORMAT_xBGR:
      self->blend = gst_compositor_blend_xbgr;
      self->overlay = self->blend;
      self->fill_checker = gst_compositor_fill_checker_xbgr;
      self->fill_color = gst_compositor_fill_color_xbgr;
      ret = TRUE;
      break;
    case GST_VIDEO_FORMAT_RGBx:
      self->blend = gst_compositor_blend_rgbx;
      self->overlay = self->blend;
      self->fill_checker = gst_compositor_fill_checker_rgbx;
      self->fill_color = gst_compositor_fill_color_rgbx;
      ret = TRUE;
      break;
    case GST_VIDEO_FORMAT_BGRx:
      self->blend = gst_compositor_blend_bgrx;
      self->overlay = self->blend;
      self->fill_checker = gst_compositor_fill_checker_bgrx;
      self->fill_color = gst_compositor_fill_color_bgrx;
      ret = TRUE;
      break;
    default:
      break;
  }

  return ret;
}

static gboolean
_update_info (GstVideoAggregator * vagg, GstVideoInfo * info)
{
  GList *l;
  gint best_width = -1, best_height = -1;
  gboolean ret = FALSE;

  GST_OBJECT_LOCK (vagg);
  for (l = GST_ELEMENT (vagg)->sinkpads; l; l = l->next) {
    GstVideoAggregatorPad *vaggpad = l->data;
    GstCompositorPad *compositor_pad = GST_COMPOSITOR_PAD (vaggpad);
    gint this_width, this_height;
    gint width, height;

    width = GST_VIDEO_INFO_WIDTH (&vaggpad->info);
    height = GST_VIDEO_INFO_HEIGHT (&vaggpad->info);

    if (width == 0 || height == 0)
      continue;

    this_width = width + MAX (compositor_pad->xpos, 0);
    this_height = height + MAX (compositor_pad->ypos, 0);

    if (best_width < this_width)
      best_width = this_width;
    if (best_height < this_height)
      best_height = this_height;
  }
  GST_OBJECT_UNLOCK (vagg);

  if (best_width > 0 && best_height > 0) {
    gst_video_info_set_format (info, GST_VIDEO_INFO_FORMAT (info),
        best_width, best_height);
    ret = set_functions (GST_COMPOSITOR (vagg), info);
  }

  return ret;
}

static GstFlowReturn
gst_compositor_aggregate_frames (GstVideoAggregator * vagg, GstBuffer * outbuf)
{
  GList *l;
  GstCompositor *self = GST_COMPOSITOR (vagg);
  BlendFunction composite;
  GstVideoFrame out_frame, *outframe;

  if (!gst_video_frame_map (&out_frame, &vagg->info, outbuf, GST_MAP_WRITE)) {

    return GST_FLOW_ERROR;
  }

  outframe = &out_frame;
  /* default to blending */
  composite = self->blend;
  switch (self->background) {
    case COMPOSITOR_BACKGROUND_CHECKER:
      self->fill_checker (outframe);
      break;
    case COMPOSITOR_BACKGROUND_BLACK:
      self->fill_color (outframe, 16, 128, 128);
      break;
    case COMPOSITOR_BACKGROUND_WHITE:
      self->fill_color (outframe, 240, 128, 128);
      break;
    case COMPOSITOR_BACKGROUND_TRANSPARENT:
    {
      guint i, plane, num_planes, height;

      num_planes = GST_VIDEO_FRAME_N_PLANES (outframe);
      for (plane = 0; plane < num_planes; ++plane) {
        guint8 *pdata;
        gsize rowsize, plane_stride;

        pdata = GST_VIDEO_FRAME_PLANE_DATA (outframe, plane);
        plane_stride = GST_VIDEO_FRAME_PLANE_STRIDE (outframe, plane);
        rowsize = GST_VIDEO_FRAME_COMP_WIDTH (outframe, plane)
            * GST_VIDEO_FRAME_COMP_PSTRIDE (outframe, plane);
        height = GST_VIDEO_FRAME_COMP_HEIGHT (outframe, plane);
        for (i = 0; i < height; ++i) {
          memset (pdata, 0, rowsize);
          pdata += plane_stride;
        }
      }

      /* use overlay to keep background transparent */
      composite = self->overlay;
      break;
    }
  }

  GST_OBJECT_LOCK (vagg);
  for (l = GST_ELEMENT (vagg)->sinkpads; l; l = l->next) {
    GstVideoAggregatorPad *pad = l->data;
    GstCompositorPad *compo_pad = GST_COMPOSITOR_PAD (pad);

    if (pad->aggregated_frame != NULL) {
      composite (pad->aggregated_frame, compo_pad->xpos, compo_pad->ypos,
          compo_pad->alpha, outframe);
    }
  }
  GST_OBJECT_UNLOCK (vagg);

  gst_video_frame_unmap (outframe);

  return GST_FLOW_OK;
}

/* GObject boilerplate */
static void
gst_compositor_class_init (GstCompositorClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstElementClass *gstelement_class = (GstElementClass *) klass;
  GstVideoAggregatorClass *videoaggregator_class =
      (GstVideoAggregatorClass *) klass;
  GstAggregatorClass *agg_class = (GstAggregatorClass *) klass;

  gobject_class->get_property = gst_compositor_get_property;
  gobject_class->set_property = gst_compositor_set_property;

  agg_class->sinkpads_type = GST_TYPE_COMPOSITOR_PAD;
  videoaggregator_class->update_info = _update_info;
  videoaggregator_class->aggregate_frames = gst_compositor_aggregate_frames;

  g_object_class_install_property (gobject_class, PROP_BACKGROUND,
      g_param_spec_enum ("background", "Background", "Background type",
          GST_TYPE_COMPOSITOR_BACKGROUND,
          DEFAULT_BACKGROUND, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&src_factory));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&sink_factory));

  gst_element_class_set_static_metadata (gstelement_class, "Compositor",
      "Filter/Editor/Video/Compositor",
      "Composite multiple video streams", "Wim Taymans <wim@fluendo.com>, "
      "Sebastian Dröge <sebastian.droege@collabora.co.uk>");
}

static void
gst_compositor_init (GstCompositor * self)
{
  self->background = DEFAULT_BACKGROUND;
  /* initialize variables */
}

/* Element registration */
static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_compositor_debug, "compositor", 0, "compositor");

  gst_compositor_init_blend ();

  return gst_element_register (plugin, "compositor", GST_RANK_PRIMARY + 1,
      GST_TYPE_COMPOSITOR);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    compositor,
    "Compositor", plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME,
    GST_PACKAGE_ORIGIN)
