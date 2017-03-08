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
 * Compositor can accept AYUV, ARGB and BGRA video streams. For each of the requested
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
 *
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

#define DEFAULT_PAD_XPOS   0
#define DEFAULT_PAD_YPOS   0
#define DEFAULT_PAD_WIDTH  0
#define DEFAULT_PAD_HEIGHT 0
#define DEFAULT_PAD_ALPHA  1.0
enum
{
  PROP_PAD_0,
  PROP_PAD_XPOS,
  PROP_PAD_YPOS,
  PROP_PAD_WIDTH,
  PROP_PAD_HEIGHT,
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
      break;
    case PROP_PAD_HEIGHT:
      pad->height = g_value_get_int (value);
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
_mixer_pad_get_output_size (GstCompositor * comp,
    GstCompositorPad * comp_pad, gint out_par_n, gint out_par_d, gint * width,
    gint * height)
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

static gboolean
gst_compositor_pad_set_info (GstVideoAggregatorPad * pad,
    GstVideoAggregator * vagg G_GNUC_UNUSED,
    GstVideoInfo * current_info, GstVideoInfo * wanted_info)
{
  GstCompositor *comp = GST_COMPOSITOR (vagg);
  GstCompositorPad *cpad = GST_COMPOSITOR_PAD (pad);
  gchar *colorimetry, *best_colorimetry;
  const gchar *chroma, *best_chroma;
  gint width, height;

  if (!current_info->finfo)
    return TRUE;

  if (GST_VIDEO_INFO_FORMAT (current_info) == GST_VIDEO_FORMAT_UNKNOWN)
    return TRUE;

  if (cpad->convert)
    gst_video_converter_free (cpad->convert);

  cpad->convert = NULL;

  if (GST_VIDEO_INFO_MULTIVIEW_MODE (current_info) !=
      GST_VIDEO_MULTIVIEW_MODE_NONE
      && GST_VIDEO_INFO_MULTIVIEW_MODE (current_info) !=
      GST_VIDEO_MULTIVIEW_MODE_MONO) {
    GST_FIXME_OBJECT (pad, "Multiview support is not implemented yet");
    return FALSE;
  }

  colorimetry = gst_video_colorimetry_to_string (&(current_info->colorimetry));
  chroma = gst_video_chroma_to_string (current_info->chroma_site);

  best_colorimetry =
      gst_video_colorimetry_to_string (&(wanted_info->colorimetry));
  best_chroma = gst_video_chroma_to_string (wanted_info->chroma_site);

  _mixer_pad_get_output_size (comp, cpad, GST_VIDEO_INFO_PAR_N (&vagg->info),
      GST_VIDEO_INFO_PAR_D (&vagg->info), &width, &height);

  if (GST_VIDEO_INFO_FORMAT (wanted_info) !=
      GST_VIDEO_INFO_FORMAT (current_info)
      || g_strcmp0 (colorimetry, best_colorimetry)
      || g_strcmp0 (chroma, best_chroma)
      || width != current_info->width || height != current_info->height) {
    GstVideoInfo tmp_info;

    /* Initialize with the wanted video format and our original width and
     * height as we don't want to rescale. Then copy over the wanted
     * colorimetry, and chroma-site and our current pixel-aspect-ratio
     * and other relevant fields.
     */
    gst_video_info_set_format (&tmp_info, GST_VIDEO_INFO_FORMAT (wanted_info),
        width, height);
    tmp_info.chroma_site = wanted_info->chroma_site;
    tmp_info.colorimetry = wanted_info->colorimetry;
    tmp_info.par_n = wanted_info->par_n;
    tmp_info.par_d = wanted_info->par_d;
    tmp_info.fps_n = current_info->fps_n;
    tmp_info.fps_d = current_info->fps_d;
    tmp_info.flags = current_info->flags;
    tmp_info.interlace_mode = current_info->interlace_mode;

    GST_DEBUG_OBJECT (pad, "This pad will be converted from %d to %d",
        GST_VIDEO_INFO_FORMAT (current_info),
        GST_VIDEO_INFO_FORMAT (&tmp_info));

    cpad->convert = gst_video_converter_new (current_info, &tmp_info, NULL);
    cpad->conversion_info = tmp_info;
    if (!cpad->convert) {
      g_free (colorimetry);
      g_free (best_colorimetry);
      GST_WARNING_OBJECT (pad, "No path found for conversion");
      return FALSE;
    }
  } else {
    cpad->conversion_info = *current_info;
    GST_DEBUG_OBJECT (pad, "This pad will not need conversion");
  }
  g_free (colorimetry);
  g_free (best_colorimetry);

  return TRUE;
}

/* Test whether rectangle2 contains rectangle 1 (geometrically) */
static gboolean
is_rectangle_contained (GstVideoRectangle rect1, GstVideoRectangle rect2)
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

static gboolean
gst_compositor_pad_prepare_frame (GstVideoAggregatorPad * pad,
    GstVideoAggregator * vagg)
{
  GstCompositor *comp = GST_COMPOSITOR (vagg);
  GstCompositorPad *cpad = GST_COMPOSITOR_PAD (pad);
  guint outsize;
  GstVideoFrame *converted_frame;
  GstBuffer *converted_buf = NULL;
  GstVideoFrame *frame;
  static GstAllocationParams params = { 0, 15, 0, 0, };
  gint width, height;
  gboolean frame_obscured = FALSE;
  GList *l;
  /* The rectangle representing this frame, clamped to the video's boundaries.
   * Due to the clamping, this is different from the frame width/height above. */
  GstVideoRectangle frame_rect;

  if (!pad->buffer)
    return TRUE;

  /* There's three types of width/height here:
   * 1. GST_VIDEO_FRAME_WIDTH/HEIGHT:
   *     The frame width/height (same as pad->buffer_vinfo.height/width;
   *     see gst_video_frame_map())
   * 2. cpad->width/height:
   *     The optional pad property for scaling the frame (if zero, the video is
   *     left unscaled)
   * 3. conversion_info.width/height:
   *     Equal to cpad->width/height if it's set, otherwise it's the pad
   *     width/height. See ->set_info()
   * */

  _mixer_pad_get_output_size (comp, cpad, GST_VIDEO_INFO_PAR_N (&vagg->info),
      GST_VIDEO_INFO_PAR_D (&vagg->info), &width, &height);

  /* The only thing that can change here is the width
   * and height, otherwise set_info would've been called */
  if (GST_VIDEO_INFO_WIDTH (&cpad->conversion_info) != width ||
      GST_VIDEO_INFO_HEIGHT (&cpad->conversion_info) != height) {
    gchar *colorimetry, *wanted_colorimetry;
    const gchar *chroma, *wanted_chroma;

    /* We might end up with no converter afterwards if
     * the only reason for conversion was a different
     * width or height
     */
    if (cpad->convert)
      gst_video_converter_free (cpad->convert);
    cpad->convert = NULL;

    colorimetry =
        gst_video_colorimetry_to_string (&pad->buffer_vinfo.colorimetry);
    chroma = gst_video_chroma_to_string (pad->buffer_vinfo.chroma_site);

    wanted_colorimetry =
        gst_video_colorimetry_to_string (&cpad->conversion_info.colorimetry);
    wanted_chroma =
        gst_video_chroma_to_string (cpad->conversion_info.chroma_site);

    if (GST_VIDEO_INFO_FORMAT (&pad->buffer_vinfo) !=
        GST_VIDEO_INFO_FORMAT (&cpad->conversion_info)
        || g_strcmp0 (colorimetry, wanted_colorimetry)
        || g_strcmp0 (chroma, wanted_chroma)
        || width != GST_VIDEO_INFO_WIDTH (&pad->buffer_vinfo)
        || height != GST_VIDEO_INFO_HEIGHT (&pad->buffer_vinfo)) {
      GstVideoInfo tmp_info;

      gst_video_info_set_format (&tmp_info, cpad->conversion_info.finfo->format,
          width, height);
      tmp_info.chroma_site = cpad->conversion_info.chroma_site;
      tmp_info.colorimetry = cpad->conversion_info.colorimetry;
      tmp_info.par_n = vagg->info.par_n;
      tmp_info.par_d = vagg->info.par_d;
      tmp_info.fps_n = cpad->conversion_info.fps_n;
      tmp_info.fps_d = cpad->conversion_info.fps_d;
      tmp_info.flags = cpad->conversion_info.flags;
      tmp_info.interlace_mode = cpad->conversion_info.interlace_mode;

      GST_DEBUG_OBJECT (pad, "This pad will be converted from %d to %d",
          GST_VIDEO_INFO_FORMAT (&pad->buffer_vinfo),
          GST_VIDEO_INFO_FORMAT (&tmp_info));

      cpad->convert =
          gst_video_converter_new (&pad->buffer_vinfo, &tmp_info, NULL);
      cpad->conversion_info = tmp_info;

      if (!cpad->convert) {
        GST_WARNING_OBJECT (pad, "No path found for conversion");
        g_free (colorimetry);
        g_free (wanted_colorimetry);
        return FALSE;
      }
    } else {
      GST_VIDEO_INFO_WIDTH (&cpad->conversion_info) = width;
      GST_VIDEO_INFO_HEIGHT (&cpad->conversion_info) = height;
    }

    g_free (colorimetry);
    g_free (wanted_colorimetry);
  }

  if (cpad->alpha == 0.0) {
    GST_DEBUG_OBJECT (vagg, "Pad has alpha 0.0, not converting frame");
    converted_frame = NULL;
    goto done;
  }

  frame_rect = clamp_rectangle (cpad->xpos, cpad->ypos, width, height,
      GST_VIDEO_INFO_WIDTH (&vagg->info), GST_VIDEO_INFO_HEIGHT (&vagg->info));

  if (frame_rect.w == 0 || frame_rect.h == 0) {
    GST_DEBUG_OBJECT (vagg, "Resulting frame is zero-width or zero-height "
        "(w: %i, h: %i), skipping", frame_rect.w, frame_rect.h);
    converted_frame = NULL;
    goto done;
  }

  GST_OBJECT_LOCK (vagg);
  /* Check if this frame is obscured by a higher-zorder frame
   * TODO: Also skip a frame if it's obscured by a combination of
   * higher-zorder frames */
  for (l = g_list_find (GST_ELEMENT (vagg)->sinkpads, pad)->next; l;
      l = l->next) {
    GstVideoRectangle frame2_rect;
    GstVideoAggregatorPad *pad2 = l->data;
    GstCompositorPad *cpad2 = GST_COMPOSITOR_PAD (pad2);
    gint pad2_width, pad2_height;

    _mixer_pad_get_output_size (comp, cpad2, GST_VIDEO_INFO_PAR_N (&vagg->info),
        GST_VIDEO_INFO_PAR_D (&vagg->info), &pad2_width, &pad2_height);

    /* We don't need to clamp the coords of the second rectangle */
    frame2_rect.x = cpad2->xpos;
    frame2_rect.y = cpad2->ypos;
    /* This is effectively what set_info and the above conversion
     * code do to calculate the desired width/height */
    frame2_rect.w = pad2_width;
    frame2_rect.h = pad2_height;

    /* Check if there's a buffer to be aggregated, ensure it can't have an alpha
     * channel, then check opacity and frame boundaries */
    if (pad2->buffer && cpad2->alpha == 1.0 &&
        !GST_VIDEO_INFO_HAS_ALPHA (&pad2->info) &&
        is_rectangle_contained (frame_rect, frame2_rect)) {
      frame_obscured = TRUE;
      GST_DEBUG_OBJECT (pad, "%ix%i@(%i,%i) obscured by %s %ix%i@(%i,%i) "
          "in output of size %ix%i; skipping frame", frame_rect.w, frame_rect.h,
          frame_rect.x, frame_rect.y, GST_PAD_NAME (pad2), frame2_rect.w,
          frame2_rect.h, frame2_rect.x, frame2_rect.y,
          GST_VIDEO_INFO_WIDTH (&vagg->info),
          GST_VIDEO_INFO_HEIGHT (&vagg->info));
      break;
    }
  }
  GST_OBJECT_UNLOCK (vagg);

  if (frame_obscured) {
    converted_frame = NULL;
    goto done;
  }

  frame = g_slice_new0 (GstVideoFrame);

  if (!gst_video_frame_map (frame, &pad->buffer_vinfo, pad->buffer,
          GST_MAP_READ)) {
    GST_WARNING_OBJECT (vagg, "Could not map input buffer");
    return FALSE;
  }

  if (cpad->convert) {
    gint converted_size;

    converted_frame = g_slice_new0 (GstVideoFrame);

    /* We wait until here to set the conversion infos, in case vagg->info changed */
    converted_size = GST_VIDEO_INFO_SIZE (&cpad->conversion_info);
    outsize = GST_VIDEO_INFO_SIZE (&vagg->info);
    converted_size = converted_size > outsize ? converted_size : outsize;
    converted_buf = gst_buffer_new_allocate (NULL, converted_size, &params);

    if (!gst_video_frame_map (converted_frame, &(cpad->conversion_info),
            converted_buf, GST_MAP_READWRITE)) {
      GST_WARNING_OBJECT (vagg, "Could not map converted frame");

      g_slice_free (GstVideoFrame, converted_frame);
      gst_video_frame_unmap (frame);
      g_slice_free (GstVideoFrame, frame);
      return FALSE;
    }

    gst_video_converter_frame (cpad->convert, frame, converted_frame);
    cpad->converted_buffer = converted_buf;
    gst_video_frame_unmap (frame);
    g_slice_free (GstVideoFrame, frame);
  } else {
    converted_frame = frame;
  }

done:
  pad->aggregated_frame = converted_frame;

  return TRUE;
}

static void
gst_compositor_pad_clean_frame (GstVideoAggregatorPad * pad,
    GstVideoAggregator * vagg)
{
  GstCompositorPad *cpad = GST_COMPOSITOR_PAD (pad);

  if (pad->aggregated_frame) {
    gst_video_frame_unmap (pad->aggregated_frame);
    g_slice_free (GstVideoFrame, pad->aggregated_frame);
    pad->aggregated_frame = NULL;
  }

  if (cpad->converted_buffer) {
    gst_buffer_unref (cpad->converted_buffer);
    cpad->converted_buffer = NULL;
  }
}

static void
gst_compositor_pad_finalize (GObject * object)
{
  GstCompositorPad *pad = GST_COMPOSITOR_PAD (object);

  if (pad->convert)
    gst_video_converter_free (pad->convert);
  pad->convert = NULL;

  G_OBJECT_CLASS (gst_compositor_pad_parent_class)->finalize (object);
}

static void
gst_compositor_pad_class_init (GstCompositorPadClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstVideoAggregatorPadClass *vaggpadclass =
      (GstVideoAggregatorPadClass *) klass;

  gobject_class->set_property = gst_compositor_pad_set_property;
  gobject_class->get_property = gst_compositor_pad_get_property;
  gobject_class->finalize = gst_compositor_pad_finalize;

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

  vaggpadclass->set_info = GST_DEBUG_FUNCPTR (gst_compositor_pad_set_info);
  vaggpadclass->prepare_frame =
      GST_DEBUG_FUNCPTR (gst_compositor_pad_prepare_frame);
  vaggpadclass->clean_frame =
      GST_DEBUG_FUNCPTR (gst_compositor_pad_clean_frame);
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

static GstCaps *
_fixate_caps (GstVideoAggregator * vagg, GstCaps * caps)
{
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
    _mixer_pad_get_output_size (GST_COMPOSITOR (vagg), compositor_pad, par_n,
        par_d, &width, &height);

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
_negotiated_caps (GstVideoAggregator * vagg, GstCaps * caps)
{
  GstVideoInfo v_info;

  GST_DEBUG_OBJECT (vagg, "Negotiated caps %" GST_PTR_FORMAT, caps);

  if (!gst_video_info_from_caps (&v_info, caps))
    return FALSE;

  if (!set_functions (GST_COMPOSITOR (vagg), &v_info)) {
    GST_ERROR_OBJECT (vagg, "Failed to setup vfuncs");
    return FALSE;
  }

  return TRUE;
}

static GstFlowReturn
gst_compositor_aggregate_frames (GstVideoAggregator * vagg, GstBuffer * outbuf)
{
  GList *l;
  GstCompositor *self = GST_COMPOSITOR (vagg);
  BlendFunction composite;
  GstVideoFrame out_frame, *outframe;

  if (!gst_video_frame_map (&out_frame, &vagg->info, outbuf, GST_MAP_WRITE)) {
    GST_WARNING_OBJECT (vagg, "Could not map output buffer");
    return GST_FLOW_ERROR;
  }

  outframe = &out_frame;
  /* default to blending */
  composite = self->blend;
  /* TODO: If the frames to be composited completely obscure the background,
   * don't bother drawing the background at all. */
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

  agg_class->sinkpads_type = GST_TYPE_COMPOSITOR_PAD;
  agg_class->sink_query = _sink_query;
  videoaggregator_class->fixate_caps = _fixate_caps;
  videoaggregator_class->negotiated_caps = _negotiated_caps;
  videoaggregator_class->aggregate_frames = gst_compositor_aggregate_frames;

  g_object_class_install_property (gobject_class, PROP_BACKGROUND,
      g_param_spec_enum ("background", "Background", "Background type",
          GST_TYPE_COMPOSITOR_BACKGROUND,
          DEFAULT_BACKGROUND, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_add_static_pad_template (gstelement_class, &src_factory);
  gst_element_class_add_static_pad_template (gstelement_class, &sink_factory);

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
