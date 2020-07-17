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
 * @title: compositor
 *
 * Compositor can accept AYUV, VUYA, ARGB and BGRA video streams. For each of the requested
 * sink pads it will compare the incoming geometry and framerate to define the
 * output parameters. Indeed output video frames will have the geometry of the
 * biggest incoming video stream and the framerate of the fastest incoming one.
 *
 * Compositor will do colorspace conversion.
 *
 * Individual parameters for each input stream can be configured on the
 * #GstCompositorPad:
 *
 * * "xpos": The x-coordinate position of the top-left corner of the picture (#gint)
 * * "ypos": The y-coordinate position of the top-left corner of the picture (#gint)
 * * "width": The width of the picture; the input will be scaled if necessary (#gint)
 * * "height": The height of the picture; the input will be scaled if necessary (#gint)
 * * "alpha": The transparency of the picture; between 0.0 and 1.0. The blending
 *   is a simple copy when fully-transparent (0.0) and fully-opaque (1.0). (#gdouble)
 * * "zorder": The z-order position of the picture in the composition (#guint)
 *
 * ## Sample pipelines
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
 * the snow pattern of 100x100 pixels is overlaid on top of the first one on
 * the left vertically centered with a small transparency showing the first
 * video test source behind and the checker pattern under it. Note that the
 * framerate of the output video is 10 frames per second.
 * |[
 * gst-launch-1.0 videotestsrc pattern=1 ! \
 *   video/x-raw, framerate=\(fraction\)10/1, width=100, height=100 ! \
 *   compositor name=comp ! videoconvert ! ximagesink \
 *   videotestsrc !  \
 *   video/x-raw, framerate=\(fraction\)5/1, width=320, height=240 ! comp.
 * ]| A pipeline to demonstrate bgra comping. (This does not demonstrate alpha blending).
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
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include "compositor.h"

#ifdef DISABLE_ORC
#define orc_memset memset
#else
#include <orc/orcfunctions.h>
#endif

GST_DEBUG_CATEGORY_STATIC (gst_compositor_debug);
#define GST_CAT_DEFAULT gst_compositor_debug

#define FORMATS " { AYUV, VUYA, BGRA, ARGB, RGBA, ABGR, Y444, Y42B, YUY2, UYVY, "\
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

static void gst_compositor_child_proxy_init (gpointer g_iface,
    gpointer iface_data);

#define GST_TYPE_COMPOSITOR_OPERATOR (gst_compositor_operator_get_type())
static GType
gst_compositor_operator_get_type (void)
{
  static GType compositor_operator_type = 0;

  static const GEnumValue compositor_operator[] = {
    {COMPOSITOR_OPERATOR_SOURCE, "Source", "source"},
    {COMPOSITOR_OPERATOR_OVER, "Over", "over"},
    {COMPOSITOR_OPERATOR_ADD, "Add", "add"},
    {0, NULL, NULL},
  };

  if (!compositor_operator_type) {
    compositor_operator_type =
        g_enum_register_static ("GstCompositorOperator", compositor_operator);
  }
  return compositor_operator_type;
}

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

#define DEFAULT_PAD_XPOS   0
#define DEFAULT_PAD_YPOS   0
#define DEFAULT_PAD_WIDTH  0
#define DEFAULT_PAD_HEIGHT 0
#define DEFAULT_PAD_ALPHA  1.0
#define DEFAULT_PAD_OPERATOR COMPOSITOR_OPERATOR_OVER
enum
{
  PROP_PAD_0,
  PROP_PAD_XPOS,
  PROP_PAD_YPOS,
  PROP_PAD_WIDTH,
  PROP_PAD_HEIGHT,
  PROP_PAD_ALPHA,
  PROP_PAD_OPERATOR,
};

G_DEFINE_TYPE (GstCompositorPad, gst_compositor_pad,
    GST_TYPE_VIDEO_AGGREGATOR_CONVERT_PAD);

static void
gst_compositor_pad_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstCompositorPad *pad = GST_COMPOSITOR_PAD (object);

  switch (prop_id) {
    case PROP_PAD_XPOS:
      g_value_set_int (value, pad->xpos);
      break;
    case PROP_PAD_YPOS:
      g_value_set_int (value, pad->ypos);
      break;
    case PROP_PAD_WIDTH:
      g_value_set_int (value, pad->width);
      break;
    case PROP_PAD_HEIGHT:
      g_value_set_int (value, pad->height);
      break;
    case PROP_PAD_ALPHA:
      g_value_set_double (value, pad->alpha);
      break;
    case PROP_PAD_OPERATOR:
      g_value_set_enum (value, pad->op);
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
    case PROP_PAD_WIDTH:
      pad->width = g_value_get_int (value);
      gst_video_aggregator_convert_pad_update_conversion_info
          (GST_VIDEO_AGGREGATOR_CONVERT_PAD (pad));
      break;
    case PROP_PAD_HEIGHT:
      pad->height = g_value_get_int (value);
      gst_video_aggregator_convert_pad_update_conversion_info
          (GST_VIDEO_AGGREGATOR_CONVERT_PAD (pad));
      break;
    case PROP_PAD_ALPHA:
      pad->alpha = g_value_get_double (value);
      break;
    case PROP_PAD_OPERATOR:
      pad->op = g_value_get_enum (value);
      gst_video_aggregator_pad_set_needs_alpha (GST_VIDEO_AGGREGATOR_PAD (pad),
          pad->op == COMPOSITOR_OPERATOR_ADD);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
_mixer_pad_get_output_size (GstCompositorPad * comp_pad, gint out_par_n,
    gint out_par_d, gint * width, gint * height)
{
  GstVideoAggregatorPad *vagg_pad = GST_VIDEO_AGGREGATOR_PAD (comp_pad);
  gint pad_width, pad_height;
  guint dar_n, dar_d;

  /* FIXME: Anything better we can do here? */
  if (!vagg_pad->info.finfo
      || vagg_pad->info.finfo->format == GST_VIDEO_FORMAT_UNKNOWN) {
    GST_DEBUG_OBJECT (comp_pad, "Have no caps yet");
    *width = 0;
    *height = 0;
    return;
  }

  pad_width =
      comp_pad->width <=
      0 ? GST_VIDEO_INFO_WIDTH (&vagg_pad->info) : comp_pad->width;
  pad_height =
      comp_pad->height <=
      0 ? GST_VIDEO_INFO_HEIGHT (&vagg_pad->info) : comp_pad->height;

  if (!gst_video_calculate_display_ratio (&dar_n, &dar_d, pad_width, pad_height,
          GST_VIDEO_INFO_PAR_N (&vagg_pad->info),
          GST_VIDEO_INFO_PAR_D (&vagg_pad->info), out_par_n, out_par_d)) {
    GST_WARNING_OBJECT (comp_pad, "Cannot calculate display aspect ratio");
    *width = *height = 0;
    return;
  }
  GST_LOG_OBJECT (comp_pad, "scaling %ux%u by %u/%u (%u/%u / %u/%u)", pad_width,
      pad_height, dar_n, dar_d, GST_VIDEO_INFO_PAR_N (&vagg_pad->info),
      GST_VIDEO_INFO_PAR_D (&vagg_pad->info), out_par_n, out_par_d);

  /* Pick either height or width, whichever is an integer multiple of the
   * display aspect ratio. However, prefer preserving the height to account
   * for interlaced video. */
  if (pad_height % dar_n == 0) {
    pad_width = gst_util_uint64_scale_int (pad_height, dar_n, dar_d);
  } else if (pad_width % dar_d == 0) {
    pad_height = gst_util_uint64_scale_int (pad_width, dar_d, dar_n);
  } else {
    pad_width = gst_util_uint64_scale_int (pad_height, dar_n, dar_d);
  }

  *width = pad_width;
  *height = pad_height;
}

/* Test whether rectangle2 contains rectangle 1 (geometrically) */
static gboolean
is_rectangle_contained (const GstVideoRectangle rect1,
    const GstVideoRectangle rect2)
{
  if ((rect2.x <= rect1.x) && (rect2.y <= rect1.y) &&
      ((rect2.x + rect2.w) >= (rect1.x + rect1.w)) &&
      ((rect2.y + rect2.h) >= (rect1.y + rect1.h)))
    return TRUE;
  return FALSE;
}

static GstVideoRectangle
clamp_rectangle (gint x, gint y, gint w, gint h, gint outer_width,
    gint outer_height)
{
  gint x2 = x + w;
  gint y2 = y + h;
  GstVideoRectangle clamped;

  /* Clamp the x/y coordinates of this frame to the output boundaries to cover
   * the case where (say, with negative xpos/ypos or w/h greater than the output
   * size) the non-obscured portion of the frame could be outside the bounds of
   * the video itself and hence not visible at all */
  clamped.x = CLAMP (x, 0, outer_width);
  clamped.y = CLAMP (y, 0, outer_height);
  clamped.w = CLAMP (x2, 0, outer_width) - clamped.x;
  clamped.h = CLAMP (y2, 0, outer_height) - clamped.y;

  return clamped;
}

/* Call this with the lock taken */
static gboolean
_pad_obscures_rectangle (GstVideoAggregator * vagg, GstVideoAggregatorPad * pad,
    const GstVideoRectangle rect)
{
  GstVideoRectangle pad_rect;
  GstCompositorPad *cpad = GST_COMPOSITOR_PAD (pad);

  /* No buffer to obscure the rectangle with */
  if (!gst_video_aggregator_pad_has_current_buffer (pad))
    return FALSE;

  /* Can't obscure if we introduce alpha or if the format has an alpha
   * component as we'd have to inspect every pixel to know if the frame is
   * opaque, so assume it doesn't obscure
   */
  if (cpad->alpha != 1.0 || GST_VIDEO_INFO_HAS_ALPHA (&pad->info))
    return FALSE;

  pad_rect.x = cpad->xpos;
  pad_rect.y = cpad->ypos;
  /* Handle pixel and display aspect ratios to find the actual size */
  _mixer_pad_get_output_size (cpad, GST_VIDEO_INFO_PAR_N (&vagg->info),
      GST_VIDEO_INFO_PAR_D (&vagg->info), &(pad_rect.w), &(pad_rect.h));

  if (!is_rectangle_contained (rect, pad_rect))
    return FALSE;

  GST_DEBUG_OBJECT (pad, "Pad %s %ix%i@(%i,%i) obscures rect %ix%i@(%i,%i)",
      GST_PAD_NAME (pad), pad_rect.w, pad_rect.h, pad_rect.x, pad_rect.y,
      rect.w, rect.h, rect.x, rect.y);

  return TRUE;
}

static gboolean
gst_compositor_pad_prepare_frame (GstVideoAggregatorPad * pad,
    GstVideoAggregator * vagg, GstBuffer * buffer,
    GstVideoFrame * prepared_frame)
{
  GstCompositorPad *cpad = GST_COMPOSITOR_PAD (pad);
  gint width, height;
  gboolean frame_obscured = FALSE;
  GList *l;
  /* The rectangle representing this frame, clamped to the video's boundaries.
   * Due to the clamping, this is different from the frame width/height above. */
  GstVideoRectangle frame_rect;

  /* There's three types of width/height here:
   * 1. GST_VIDEO_FRAME_WIDTH/HEIGHT:
   *     The frame width/height (same as pad->info.height/width;
   *     see gst_video_frame_map())
   * 2. cpad->width/height:
   *     The optional pad property for scaling the frame (if zero, the video is
   *     left unscaled)
   * 3. conversion_info.width/height:
   *     Equal to cpad->width/height if it's set, otherwise it's the pad
   *     width/height. See ->set_info()
   * */

  _mixer_pad_get_output_size (cpad, GST_VIDEO_INFO_PAR_N (&vagg->info),
      GST_VIDEO_INFO_PAR_D (&vagg->info), &width, &height);

  if (cpad->alpha == 0.0) {
    GST_DEBUG_OBJECT (pad, "Pad has alpha 0.0, not converting frame");
    goto done;
  }

  frame_rect = clamp_rectangle (cpad->xpos, cpad->ypos, width, height,
      GST_VIDEO_INFO_WIDTH (&vagg->info), GST_VIDEO_INFO_HEIGHT (&vagg->info));

  if (frame_rect.w == 0 || frame_rect.h == 0) {
    GST_DEBUG_OBJECT (pad, "Resulting frame is zero-width or zero-height "
        "(w: %i, h: %i), skipping", frame_rect.w, frame_rect.h);
    goto done;
  }

  GST_OBJECT_LOCK (vagg);
  /* Check if this frame is obscured by a higher-zorder frame
   * TODO: Also skip a frame if it's obscured by a combination of
   * higher-zorder frames */
  l = g_list_find (GST_ELEMENT (vagg)->sinkpads, pad)->next;
  for (; l; l = l->next) {
    if (_pad_obscures_rectangle (vagg, l->data, frame_rect)) {
      frame_obscured = TRUE;
      break;
    }
  }
  GST_OBJECT_UNLOCK (vagg);

  if (frame_obscured)
    goto done;

  return
      GST_VIDEO_AGGREGATOR_PAD_CLASS
      (gst_compositor_pad_parent_class)->prepare_frame (pad, vagg, buffer,
      prepared_frame);

done:

  return TRUE;
}

static void
gst_compositor_pad_create_conversion_info (GstVideoAggregatorConvertPad * pad,
    GstVideoAggregator * vagg, GstVideoInfo * conversion_info)
{
  GstCompositorPad *cpad = GST_COMPOSITOR_PAD (pad);
  gint width, height;

  GST_VIDEO_AGGREGATOR_CONVERT_PAD_CLASS
      (gst_compositor_pad_parent_class)->create_conversion_info (pad, vagg,
      conversion_info);
  if (!conversion_info->finfo)
    return;

  _mixer_pad_get_output_size (cpad, GST_VIDEO_INFO_PAR_N (&vagg->info),
      GST_VIDEO_INFO_PAR_D (&vagg->info), &width, &height);

  /* The only thing that can change here is the width
   * and height, otherwise set_info would've been called */
  if (GST_VIDEO_INFO_WIDTH (conversion_info) != width ||
      GST_VIDEO_INFO_HEIGHT (conversion_info) != height) {
    GstVideoInfo tmp_info;

    /* Initialize with the wanted video format and our original width and
     * height as we don't want to rescale. Then copy over the wanted
     * colorimetry, and chroma-site and our current pixel-aspect-ratio
     * and other relevant fields.
     */
    gst_video_info_set_format (&tmp_info,
        GST_VIDEO_INFO_FORMAT (conversion_info), width, height);
    tmp_info.chroma_site = conversion_info->chroma_site;
    tmp_info.colorimetry = conversion_info->colorimetry;
    tmp_info.par_n = conversion_info->par_n;
    tmp_info.par_d = conversion_info->par_d;
    tmp_info.fps_n = conversion_info->fps_n;
    tmp_info.fps_d = conversion_info->fps_d;
    tmp_info.flags = conversion_info->flags;
    tmp_info.interlace_mode = conversion_info->interlace_mode;

    *conversion_info = tmp_info;
  }
}

static void
gst_compositor_pad_class_init (GstCompositorPadClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstVideoAggregatorPadClass *vaggpadclass =
      (GstVideoAggregatorPadClass *) klass;
  GstVideoAggregatorConvertPadClass *vaggcpadclass =
      (GstVideoAggregatorConvertPadClass *) klass;

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
  g_object_class_install_property (gobject_class, PROP_PAD_WIDTH,
      g_param_spec_int ("width", "Width", "Width of the picture",
          G_MININT, G_MAXINT, DEFAULT_PAD_WIDTH,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_PAD_HEIGHT,
      g_param_spec_int ("height", "Height", "Height of the picture",
          G_MININT, G_MAXINT, DEFAULT_PAD_HEIGHT,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_PAD_ALPHA,
      g_param_spec_double ("alpha", "Alpha", "Alpha of the picture", 0.0, 1.0,
          DEFAULT_PAD_ALPHA,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_PAD_OPERATOR,
      g_param_spec_enum ("operator", "Operator",
          "Blending operator to use for blending this pad over the previous ones",
          GST_TYPE_COMPOSITOR_OPERATOR, DEFAULT_PAD_OPERATOR,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));

  vaggpadclass->prepare_frame =
      GST_DEBUG_FUNCPTR (gst_compositor_pad_prepare_frame);

  vaggcpadclass->create_conversion_info =
      GST_DEBUG_FUNCPTR (gst_compositor_pad_create_conversion_info);
}

static void
gst_compositor_pad_init (GstCompositorPad * compo_pad)
{
  compo_pad->xpos = DEFAULT_PAD_XPOS;
  compo_pad->ypos = DEFAULT_PAD_YPOS;
  compo_pad->alpha = DEFAULT_PAD_ALPHA;
  compo_pad->op = DEFAULT_PAD_OPERATOR;
}


/* GstCompositor */
#define DEFAULT_BACKGROUND COMPOSITOR_BACKGROUND_CHECKER
enum
{
  PROP_0,
  PROP_BACKGROUND,
};

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
G_DEFINE_TYPE_WITH_CODE (GstCompositor, gst_compositor,
    GST_TYPE_VIDEO_AGGREGATOR, G_IMPLEMENT_INTERFACE (GST_TYPE_CHILD_PROXY,
        gst_compositor_child_proxy_init));

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
    case GST_VIDEO_FORMAT_VUYA:
      self->blend = gst_compositor_blend_vuya;
      self->overlay = gst_compositor_overlay_vuya;
      self->fill_checker = gst_compositor_fill_checker_vuya;
      self->fill_color = gst_compositor_fill_color_vuya;
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

static GstCaps *
_fixate_caps (GstAggregator * agg, GstCaps * caps)
{
  GstVideoAggregator *vagg = GST_VIDEO_AGGREGATOR (agg);
  GList *l;
  gint best_width = -1, best_height = -1;
  gint best_fps_n = -1, best_fps_d = -1;
  gint par_n, par_d;
  gdouble best_fps = 0.;
  GstCaps *ret = NULL;
  GstStructure *s;

  ret = gst_caps_make_writable (caps);

  /* we need this to calculate how large to make the output frame */
  s = gst_caps_get_structure (ret, 0);
  if (gst_structure_has_field (s, "pixel-aspect-ratio")) {
    gst_structure_fixate_field_nearest_fraction (s, "pixel-aspect-ratio", 1, 1);
    gst_structure_get_fraction (s, "pixel-aspect-ratio", &par_n, &par_d);
  } else {
    par_n = par_d = 1;
  }

  GST_OBJECT_LOCK (vagg);
  for (l = GST_ELEMENT (vagg)->sinkpads; l; l = l->next) {
    GstVideoAggregatorPad *vaggpad = l->data;
    GstCompositorPad *compositor_pad = GST_COMPOSITOR_PAD (vaggpad);
    gint this_width, this_height;
    gint width, height;
    gint fps_n, fps_d;
    gdouble cur_fps;

    fps_n = GST_VIDEO_INFO_FPS_N (&vaggpad->info);
    fps_d = GST_VIDEO_INFO_FPS_D (&vaggpad->info);
    _mixer_pad_get_output_size (compositor_pad, par_n, par_d, &width, &height);

    if (width == 0 || height == 0)
      continue;

    this_width = width + MAX (compositor_pad->xpos, 0);
    this_height = height + MAX (compositor_pad->ypos, 0);

    if (best_width < this_width)
      best_width = this_width;
    if (best_height < this_height)
      best_height = this_height;

    if (fps_d == 0)
      cur_fps = 0.0;
    else
      gst_util_fraction_to_double (fps_n, fps_d, &cur_fps);

    if (best_fps < cur_fps) {
      best_fps = cur_fps;
      best_fps_n = fps_n;
      best_fps_d = fps_d;
    }
  }
  GST_OBJECT_UNLOCK (vagg);

  if (best_fps_n <= 0 || best_fps_d <= 0 || best_fps == 0.0) {
    best_fps_n = 25;
    best_fps_d = 1;
    best_fps = 25.0;
  }

  gst_structure_fixate_field_nearest_int (s, "width", best_width);
  gst_structure_fixate_field_nearest_int (s, "height", best_height);
  gst_structure_fixate_field_nearest_fraction (s, "framerate", best_fps_n,
      best_fps_d);
  ret = gst_caps_fixate (ret);

  return ret;
}

static gboolean
_negotiated_caps (GstAggregator * agg, GstCaps * caps)
{
  GstVideoInfo v_info;

  GST_DEBUG_OBJECT (agg, "Negotiated caps %" GST_PTR_FORMAT, caps);

  if (!gst_video_info_from_caps (&v_info, caps))
    return FALSE;

  if (!set_functions (GST_COMPOSITOR (agg), &v_info)) {
    GST_ERROR_OBJECT (agg, "Failed to setup vfuncs");
    return FALSE;
  }

  return GST_AGGREGATOR_CLASS (parent_class)->negotiated_src_caps (agg, caps);
}

static gboolean
_should_draw_background (GstVideoAggregator * vagg)
{
  GstVideoRectangle bg_rect;
  gboolean draw = TRUE;
  GList *l;

  bg_rect.x = bg_rect.y = 0;

  GST_OBJECT_LOCK (vagg);
  bg_rect.w = GST_VIDEO_INFO_WIDTH (&vagg->info);
  bg_rect.h = GST_VIDEO_INFO_HEIGHT (&vagg->info);
  /* Check if the background is completely obscured by a pad
   * TODO: Also skip if it's obscured by a combination of pads */
  for (l = GST_ELEMENT (vagg)->sinkpads; l; l = l->next) {
    if (_pad_obscures_rectangle (vagg, l->data, bg_rect)) {
      draw = FALSE;
      break;
    }
  }
  GST_OBJECT_UNLOCK (vagg);
  return draw;
}

static gboolean
_draw_background (GstVideoAggregator * vagg, GstVideoFrame * outframe,
    BlendFunction * composite)
{
  GstCompositor *comp = GST_COMPOSITOR (vagg);

  *composite = comp->blend;
  /* If one of the frames to be composited completely obscures the background,
   * don't bother drawing the background at all. We can also always use the
   * 'blend' BlendFunction in that case because it only changes if we have to
   * overlay on top of a transparent background. */
  if (!_should_draw_background (vagg))
    return FALSE;

  switch (comp->background) {
    case COMPOSITOR_BACKGROUND_CHECKER:
      comp->fill_checker (outframe);
      break;
    case COMPOSITOR_BACKGROUND_BLACK:
      comp->fill_color (outframe, 16, 128, 128);
      break;
    case COMPOSITOR_BACKGROUND_WHITE:
      comp->fill_color (outframe, 240, 128, 128);
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
      *composite = comp->overlay;
      break;
    }
  }

  return TRUE;
}

static gboolean
frames_can_copy (const GstVideoFrame * frame1, const GstVideoFrame * frame2)
{
  if (GST_VIDEO_FRAME_FORMAT (frame1) != GST_VIDEO_FRAME_FORMAT (frame2))
    return FALSE;
  if (GST_VIDEO_FRAME_HEIGHT (frame1) != GST_VIDEO_FRAME_HEIGHT (frame2))
    return FALSE;
  if (GST_VIDEO_FRAME_WIDTH (frame1) != GST_VIDEO_FRAME_WIDTH (frame2))
    return FALSE;
  return TRUE;
}

static GstFlowReturn
gst_compositor_aggregate_frames (GstVideoAggregator * vagg, GstBuffer * outbuf)
{
  GList *l;
  BlendFunction composite;
  GstVideoFrame out_frame, *outframe;
  gboolean drew_background;
  guint drawn_pads = 0;

  if (!gst_video_frame_map (&out_frame, &vagg->info, outbuf, GST_MAP_WRITE)) {
    GST_WARNING_OBJECT (vagg, "Could not map output buffer");
    return GST_FLOW_ERROR;
  }

  outframe = &out_frame;
  drew_background = _draw_background (vagg, outframe, &composite);

  GST_OBJECT_LOCK (vagg);
  for (l = GST_ELEMENT (vagg)->sinkpads; l; l = l->next) {
    GstVideoAggregatorPad *pad = l->data;
    GstCompositorPad *compo_pad = GST_COMPOSITOR_PAD (pad);
    GstVideoFrame *prepared_frame =
        gst_video_aggregator_pad_get_prepared_frame (pad);
    GstCompositorBlendMode blend_mode = COMPOSITOR_BLEND_MODE_OVER;

    switch (compo_pad->op) {
      case COMPOSITOR_OPERATOR_SOURCE:
        blend_mode = COMPOSITOR_BLEND_MODE_SOURCE;
        break;
      case COMPOSITOR_OPERATOR_OVER:
        blend_mode = COMPOSITOR_BLEND_MODE_OVER;
        break;
      case COMPOSITOR_OPERATOR_ADD:
        blend_mode = COMPOSITOR_BLEND_MODE_ADD;
        break;
      default:
        g_assert_not_reached ();
        break;
    }

    if (prepared_frame != NULL) {
      /* If this is the first pad we're drawing, and we didn't draw the
       * background, and @prepared_frame has the same format, height, and width
       * as @outframe, then we can just copy it as-is. Subsequent pads (if any)
       * will be composited on top of it. */
      if (drawn_pads == 0 && !drew_background &&
          frames_can_copy (prepared_frame, outframe))
        gst_video_frame_copy (outframe, prepared_frame);
      else
        composite (prepared_frame,
            compo_pad->xpos,
            compo_pad->ypos, compo_pad->alpha, outframe, blend_mode);
      drawn_pads++;
    }
  }
  GST_OBJECT_UNLOCK (vagg);

  gst_video_frame_unmap (outframe);

  return GST_FLOW_OK;
}

static GstPad *
gst_compositor_request_new_pad (GstElement * element, GstPadTemplate * templ,
    const gchar * req_name, const GstCaps * caps)
{
  GstPad *newpad;

  newpad = (GstPad *)
      GST_ELEMENT_CLASS (parent_class)->request_new_pad (element,
      templ, req_name, caps);

  if (newpad == NULL)
    goto could_not_create;

  gst_child_proxy_child_added (GST_CHILD_PROXY (element), G_OBJECT (newpad),
      GST_OBJECT_NAME (newpad));

  return newpad;

could_not_create:
  {
    GST_DEBUG_OBJECT (element, "could not create/add pad");
    return NULL;
  }
}

static void
gst_compositor_release_pad (GstElement * element, GstPad * pad)
{
  GstCompositor *compositor;

  compositor = GST_COMPOSITOR (element);

  GST_DEBUG_OBJECT (compositor, "release pad %s:%s", GST_DEBUG_PAD_NAME (pad));

  gst_child_proxy_child_removed (GST_CHILD_PROXY (compositor), G_OBJECT (pad),
      GST_OBJECT_NAME (pad));

  GST_ELEMENT_CLASS (parent_class)->release_pad (element, pad);
}

static gboolean
_sink_query (GstAggregator * agg, GstAggregatorPad * bpad, GstQuery * query)
{
  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_ALLOCATION:{
      GstCaps *caps;
      GstVideoInfo info;
      GstBufferPool *pool;
      guint size;
      GstStructure *structure;

      gst_query_parse_allocation (query, &caps, NULL);

      if (caps == NULL)
        return FALSE;

      if (!gst_video_info_from_caps (&info, caps))
        return FALSE;

      size = GST_VIDEO_INFO_SIZE (&info);

      pool = gst_video_buffer_pool_new ();

      structure = gst_buffer_pool_get_config (pool);
      gst_buffer_pool_config_set_params (structure, caps, size, 0, 0);

      if (!gst_buffer_pool_set_config (pool, structure)) {
        gst_object_unref (pool);
        return FALSE;
      }

      gst_query_add_allocation_pool (query, pool, size, 0, 0);
      gst_object_unref (pool);
      gst_query_add_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL);

      return TRUE;
    }
    default:
      return GST_AGGREGATOR_CLASS (parent_class)->sink_query (agg, bpad, query);
  }
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

  gstelement_class->request_new_pad =
      GST_DEBUG_FUNCPTR (gst_compositor_request_new_pad);
  gstelement_class->release_pad =
      GST_DEBUG_FUNCPTR (gst_compositor_release_pad);
  agg_class->sink_query = _sink_query;
  agg_class->fixate_src_caps = _fixate_caps;
  agg_class->negotiated_src_caps = _negotiated_caps;
  videoaggregator_class->aggregate_frames = gst_compositor_aggregate_frames;

  g_object_class_install_property (gobject_class, PROP_BACKGROUND,
      g_param_spec_enum ("background", "Background", "Background type",
          GST_TYPE_COMPOSITOR_BACKGROUND,
          DEFAULT_BACKGROUND, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_add_static_pad_template_with_gtype (gstelement_class,
      &src_factory, GST_TYPE_AGGREGATOR_PAD);
  gst_element_class_add_static_pad_template_with_gtype (gstelement_class,
      &sink_factory, GST_TYPE_COMPOSITOR_PAD);

  gst_element_class_set_static_metadata (gstelement_class, "Compositor",
      "Filter/Editor/Video/Compositor",
      "Composite multiple video streams", "Wim Taymans <wim@fluendo.com>, "
      "Sebastian Dröge <sebastian.droege@collabora.co.uk>");

  gst_type_mark_as_plugin_api (GST_TYPE_COMPOSITOR_PAD, 0);
  gst_type_mark_as_plugin_api (GST_TYPE_COMPOSITOR_OPERATOR, 0);
  gst_type_mark_as_plugin_api (GST_TYPE_COMPOSITOR_BACKGROUND, 0);
}

static void
gst_compositor_init (GstCompositor * self)
{
  /* initialize variables */
  self->background = DEFAULT_BACKGROUND;
}

/* GstChildProxy implementation */
static GObject *
gst_compositor_child_proxy_get_child_by_index (GstChildProxy * child_proxy,
    guint index)
{
  GstCompositor *compositor = GST_COMPOSITOR (child_proxy);
  GObject *obj = NULL;

  GST_OBJECT_LOCK (compositor);
  obj = g_list_nth_data (GST_ELEMENT_CAST (compositor)->sinkpads, index);
  if (obj)
    gst_object_ref (obj);
  GST_OBJECT_UNLOCK (compositor);

  return obj;
}

static guint
gst_compositor_child_proxy_get_children_count (GstChildProxy * child_proxy)
{
  guint count = 0;
  GstCompositor *compositor = GST_COMPOSITOR (child_proxy);

  GST_OBJECT_LOCK (compositor);
  count = GST_ELEMENT_CAST (compositor)->numsinkpads;
  GST_OBJECT_UNLOCK (compositor);
  GST_INFO_OBJECT (compositor, "Children Count: %d", count);

  return count;
}

static void
gst_compositor_child_proxy_init (gpointer g_iface, gpointer iface_data)
{
  GstChildProxyInterface *iface = g_iface;

  iface->get_child_by_index = gst_compositor_child_proxy_get_child_by_index;
  iface->get_children_count = gst_compositor_child_proxy_get_children_count;
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
