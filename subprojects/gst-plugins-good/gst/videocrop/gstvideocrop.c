/* GStreamer video frame cropping
 * Copyright (C) 2006 Tim-Philipp Müller <tim centricular net>
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
 * SECTION:element-videocrop
 * @title: videocrop
 * @see_also: #GstVideoBox
 *
 * This element crops video frames, meaning it can remove parts of the
 * picture on the left, right, top or bottom of the picture and output
 * a smaller picture than the input picture, with the unwanted parts at the
 * border removed.
 *
 * The videocrop element is similar to the videobox element, but its main
 * goal is to support a multitude of formats as efficiently as possible.
 * Unlike videbox, it cannot add borders to the picture and unlike videbox
 * it will always output images in exactly the same format as the input image.
 *
 * If there is nothing to crop, the element will operate in pass-through mode.
 *
 * Note that no special efforts are made to handle chroma-subsampled formats
 * in the case of odd-valued cropping and compensate for sub-unit chroma plane
 * shifts for such formats in the case where the #GstVideoCrop:left or
 * #GstVideoCrop:top property is set to an odd number. This doesn't matter for
 * most use cases, but it might matter for yours.
 *
 * ## Example launch line
 * |[
 * gst-launch-1.0 -v videotestsrc ! videocrop top=42 left=1 right=4 bottom=0 ! ximagesink
 * ]|
 *
 */

/* TODO:
 *  - for packed formats, we could avoid memcpy() in case crop_left
 *    and crop_right are 0 and just create a sub-buffer of the input
 *    buffer
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/video/video.h>

#include "gstvideocrop.h"
#include "gstaspectratiocrop.h"
/* include private header which contains the supported formats */
#include "gstvideocrop-private.h"

#include <string.h>

GST_DEBUG_CATEGORY_STATIC (videocrop_debug);
#define GST_CAT_DEFAULT videocrop_debug

enum
{
  PROP_0,
  PROP_LEFT,
  PROP_RIGHT,
  PROP_TOP,
  PROP_BOTTOM
};

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (VIDEO_CROP_CAPS)
    );

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (VIDEO_CROP_CAPS)
    );

#define gst_video_crop_parent_class parent_class
G_DEFINE_TYPE (GstVideoCrop, gst_video_crop, GST_TYPE_VIDEO_FILTER);
GST_ELEMENT_REGISTER_DEFINE (videocrop, "videocrop", GST_RANK_NONE,
    GST_TYPE_VIDEO_CROP);

static void gst_video_crop_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_video_crop_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static void gst_video_crop_before_transform (GstBaseTransform * trans,
    GstBuffer * in);
static GstCaps *gst_video_crop_transform_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter_caps);
static gboolean gst_video_crop_src_event (GstBaseTransform * trans,
    GstEvent * event);

static gboolean gst_video_crop_set_info (GstVideoFilter * vfilter, GstCaps * in,
    GstVideoInfo * in_info, GstCaps * out, GstVideoInfo * out_info);
static GstFlowReturn gst_video_crop_transform_frame (GstVideoFilter * vfilter,
    GstVideoFrame * in_frame, GstVideoFrame * out_frame);

static gboolean gst_video_crop_decide_allocation (GstBaseTransform * trans,
    GstQuery * query);
static gboolean gst_video_crop_propose_allocation (GstBaseTransform * trans,
    GstQuery * decide_query, GstQuery * query);
static GstFlowReturn gst_video_crop_transform_ip (GstBaseTransform * trans,
    GstBuffer * buf);

static gboolean
gst_video_crop_src_event (GstBaseTransform * trans, GstEvent * event)
{
  double x, y, new_x, new_y;

  GstVideoCrop *vcrop = GST_VIDEO_CROP (trans);
  GST_OBJECT_LOCK (vcrop);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_NAVIGATION:
      if ((vcrop->crop_left != 0 || vcrop->crop_top != 0)
          && gst_navigation_event_get_coordinates (event, &x, &y)) {

        new_x = x + vcrop->crop_left;
        new_y = y + vcrop->crop_top;

        event = gst_event_make_writable (event);

        GST_TRACE_OBJECT (vcrop, "from %fx%f to %fx%f", x, y, new_x, new_y);
        gst_navigation_event_set_coordinates (event, new_x, new_y);
      }
      break;
    default:
      break;
  }

  GST_OBJECT_UNLOCK (vcrop);

  return GST_BASE_TRANSFORM_CLASS (parent_class)->src_event (trans, event);
}

static void
gst_video_crop_class_init (GstVideoCropClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *element_class;
  GstBaseTransformClass *basetransform_class;
  GstVideoFilterClass *vfilter_class;

  gobject_class = (GObjectClass *) klass;
  element_class = (GstElementClass *) klass;
  basetransform_class = (GstBaseTransformClass *) klass;
  vfilter_class = (GstVideoFilterClass *) klass;

  gobject_class->set_property = gst_video_crop_set_property;
  gobject_class->get_property = gst_video_crop_get_property;

  g_object_class_install_property (gobject_class, PROP_LEFT,
      g_param_spec_int ("left", "Left",
          "Pixels to crop at left (-1 to auto-crop)", -1, G_MAXINT, 0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_PLAYING | GST_PARAM_CONTROLLABLE));
  g_object_class_install_property (gobject_class, PROP_RIGHT,
      g_param_spec_int ("right", "Right",
          "Pixels to crop at right (-1 to auto-crop)", -1, G_MAXINT, 0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_PLAYING | GST_PARAM_CONTROLLABLE));
  g_object_class_install_property (gobject_class, PROP_TOP,
      g_param_spec_int ("top", "Top", "Pixels to crop at top (-1 to auto-crop)",
          -1, G_MAXINT, 0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_PLAYING | GST_PARAM_CONTROLLABLE));
  g_object_class_install_property (gobject_class, PROP_BOTTOM,
      g_param_spec_int ("bottom", "Bottom",
          "Pixels to crop at bottom (-1 to auto-crop)", -1, G_MAXINT, 0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_PLAYING | GST_PARAM_CONTROLLABLE));

  gst_element_class_add_static_pad_template (element_class, &sink_template);
  gst_element_class_add_static_pad_template (element_class, &src_template);
  gst_element_class_set_static_metadata (element_class, "Crop",
      "Filter/Effect/Video",
      "Crops video into a user-defined region",
      "Tim-Philipp Müller <tim centricular net>");

  basetransform_class->before_transform =
      GST_DEBUG_FUNCPTR (gst_video_crop_before_transform);
  basetransform_class->transform_ip_on_passthrough = FALSE;
  basetransform_class->transform_caps =
      GST_DEBUG_FUNCPTR (gst_video_crop_transform_caps);
  basetransform_class->src_event = GST_DEBUG_FUNCPTR (gst_video_crop_src_event);
  basetransform_class->decide_allocation =
      GST_DEBUG_FUNCPTR (gst_video_crop_decide_allocation);
  basetransform_class->propose_allocation =
      GST_DEBUG_FUNCPTR (gst_video_crop_propose_allocation);
  basetransform_class->transform_ip =
      GST_DEBUG_FUNCPTR (gst_video_crop_transform_ip);

  vfilter_class->set_info = GST_DEBUG_FUNCPTR (gst_video_crop_set_info);
  vfilter_class->transform_frame =
      GST_DEBUG_FUNCPTR (gst_video_crop_transform_frame);
}

static void
gst_video_crop_init (GstVideoCrop * vcrop)
{
  GST_DEBUG_CATEGORY_INIT (videocrop_debug, "videocrop", 0, "videocrop");

  vcrop->crop_right = 0;
  vcrop->crop_left = 0;
  vcrop->crop_top = 0;
  vcrop->crop_bottom = 0;
}

static void
gst_video_crop_transform_packed_yvyu (GstVideoCrop * vcrop,
    GstVideoFrame * in_frame, GstVideoFrame * out_frame, gint x, gint y)
{
  guint8 *in_data, *out_data;
  guint i, dx;
  gint width, height;
  gint in_stride;
  gint out_stride;

  width = GST_VIDEO_FRAME_WIDTH (out_frame);
  height = GST_VIDEO_FRAME_HEIGHT (out_frame);

  in_data = GST_VIDEO_FRAME_PLANE_DATA (in_frame, 0);
  out_data = GST_VIDEO_FRAME_PLANE_DATA (out_frame, 0);

  in_stride = GST_VIDEO_FRAME_PLANE_STRIDE (in_frame, 0);
  out_stride = GST_VIDEO_FRAME_PLANE_STRIDE (out_frame, 0);

  in_data += vcrop->crop_top * in_stride;

  /* rounding down here so we end up at the start of a macro-pixel and not
   * in the middle of one */
  in_data += GST_ROUND_DOWN_2 (vcrop->crop_left) *
      GST_VIDEO_FRAME_COMP_PSTRIDE (in_frame, 0);

  dx = width * GST_VIDEO_FRAME_COMP_PSTRIDE (out_frame, 0);

  /* UYVY = 4:2:2 - [U0 Y0 V0 Y1] [U2 Y2 V2 Y3] [U4 Y4 V4 Y5]
   * YUYV = 4:2:2 - [Y0 U0 Y1 V0] [Y2 U2 Y3 V2] [Y4 U4 Y5 V4] = YUY2 */
  if ((vcrop->crop_left % 2) != 0) {
    for (i = 0; i < height; ++i) {
      gint j;

      memcpy (out_data, in_data, dx);

      /* move just the Y samples one pixel to the left, don't worry about
       * chroma shift */
      for (j = vcrop->macro_y_off; j < out_stride - 2; j += 2)
        out_data[j] = in_data[j + 2];

      in_data += in_stride;
      out_data += out_stride;
    }
  } else {
    for (i = 0; i < height; ++i) {
      memcpy (out_data, in_data, dx);
      in_data += in_stride;
      out_data += out_stride;
    }
  }
}

static void
gst_video_crop_transform_packed_v210 (GstVideoCrop * vcrop,
    GstVideoFrame * in_frame, GstVideoFrame * out_frame, gint x, gint y)
{
  guint8 *in_data, *out_data;
  guint i, dx;
  gint width, height;
  gint in_stride;
  gint out_stride;

  width = GST_VIDEO_FRAME_WIDTH (out_frame);
  height = GST_VIDEO_FRAME_HEIGHT (out_frame);

  in_data = GST_VIDEO_FRAME_PLANE_DATA (in_frame, 0);
  out_data = GST_VIDEO_FRAME_PLANE_DATA (out_frame, 0);

  in_stride = GST_VIDEO_FRAME_PLANE_STRIDE (in_frame, 0);
  out_stride = GST_VIDEO_FRAME_PLANE_STRIDE (out_frame, 0);

  in_data += vcrop->crop_top * in_stride;

  /* rounding down here so we end up at the start of a macro-pixel and not
   * in the middle of one */
  in_data += (vcrop->crop_left / 6) * 16;

  /* copy a whole set of macro-pixels */
  dx = ((width + 5) / 6) * 16;

  for (i = 0; i < height; ++i) {
    memcpy (out_data, in_data, dx);
    in_data += in_stride;
    out_data += out_stride;
  }
}

static void
gst_video_crop_transform_packed_simple (GstVideoCrop * vcrop,
    GstVideoFrame * in_frame, GstVideoFrame * out_frame, gint x, gint y)
{
  guint8 *in_data, *out_data;
  gint width, height;
  guint i, dx;
  gint in_stride, out_stride;

  width = GST_VIDEO_FRAME_WIDTH (out_frame);
  height = GST_VIDEO_FRAME_HEIGHT (out_frame);

  in_data = GST_VIDEO_FRAME_PLANE_DATA (in_frame, 0);
  out_data = GST_VIDEO_FRAME_PLANE_DATA (out_frame, 0);

  in_stride = GST_VIDEO_FRAME_PLANE_STRIDE (in_frame, 0);
  out_stride = GST_VIDEO_FRAME_PLANE_STRIDE (out_frame, 0);

  in_data += (vcrop->crop_top + y) * in_stride;
  in_data +=
      (vcrop->crop_left + x) * GST_VIDEO_FRAME_COMP_PSTRIDE (in_frame, 0);

  dx = width * GST_VIDEO_FRAME_COMP_PSTRIDE (out_frame, 0);

  for (i = 0; i < height; ++i) {
    memcpy (out_data, in_data, dx);
    in_data += in_stride;
    out_data += out_stride;
  }
}

static void
gst_video_crop_transform_planar (GstVideoCrop * vcrop,
    GstVideoFrame * in_frame, GstVideoFrame * out_frame, gint x, gint y)
{
  const GstVideoFormatInfo *format_info;
  gint crop_top, crop_left;
  guint p;

  format_info = in_frame->info.finfo;
  crop_left = vcrop->crop_left + x;
  crop_top = vcrop->crop_top + y;

  for (p = 0; p < GST_VIDEO_FRAME_N_PLANES (in_frame); ++p) {
    guint8 *plane_in, *plane_out;
    guint sub_w_factor, sub_h_factor;
    guint subsampled_crop_left, subsampled_crop_top;
    guint copy_width;
    gint i;
    gsize bytes_per_pixel;

    /* plane */
    plane_in = GST_VIDEO_FRAME_PLANE_DATA (in_frame, p);
    plane_out = GST_VIDEO_FRAME_PLANE_DATA (out_frame, p);

    /* To support > 8bit, we need to add a byte-multiplier that specifies
     * how many bytes are used per pixel value */
    bytes_per_pixel = GST_VIDEO_FRAME_COMP_PSTRIDE (in_frame, p);

    /* apply crop top/left
     * crop_top and crop_left have to be rounded down to the corresponding
     * subsampling factor, since, e.g.: the first line in a subsampled plane
     * describes 2 lines in the actual image. A crop_top of 1 thus should
     * not shift the pointer of the input plane. */
    sub_w_factor = 1 << GST_VIDEO_FORMAT_INFO_W_SUB (format_info, p);
    sub_h_factor = 1 << GST_VIDEO_FORMAT_INFO_H_SUB (format_info, p);
    subsampled_crop_left = GST_ROUND_DOWN_N ((guint) crop_left, sub_w_factor);
    subsampled_crop_top = GST_ROUND_DOWN_N ((guint) crop_top, sub_h_factor);

    plane_in +=
        GST_VIDEO_FORMAT_INFO_SCALE_HEIGHT (format_info, p,
        subsampled_crop_top) * GST_VIDEO_FRAME_PLANE_STRIDE (in_frame, p);
    plane_in +=
        GST_VIDEO_FORMAT_INFO_SCALE_WIDTH (format_info, p,
        subsampled_crop_left) * bytes_per_pixel;
    copy_width = GST_VIDEO_FRAME_COMP_WIDTH (out_frame, p) * bytes_per_pixel;


    for (i = 0; i < GST_VIDEO_FRAME_COMP_HEIGHT (out_frame, p); ++i) {
      memcpy (plane_out, plane_in, copy_width);
      plane_in += GST_VIDEO_FRAME_PLANE_STRIDE (in_frame, p);
      plane_out += GST_VIDEO_FRAME_PLANE_STRIDE (out_frame, p);
    }
  }
}

static void
gst_video_crop_transform_semi_planar (GstVideoCrop * vcrop,
    GstVideoFrame * in_frame, GstVideoFrame * out_frame, gint x, gint y)
{
  gint width, height;
  gint crop_top, crop_left;
  guint8 *y_out, *uv_out;
  guint8 *y_in, *uv_in;
  guint i, dx;

  width = GST_VIDEO_FRAME_WIDTH (out_frame);
  height = GST_VIDEO_FRAME_HEIGHT (out_frame);
  crop_left = vcrop->crop_left + x;
  crop_top = vcrop->crop_top + y;

  /* Y plane */
  y_in = GST_VIDEO_FRAME_PLANE_DATA (in_frame, 0);
  y_out = GST_VIDEO_FRAME_PLANE_DATA (out_frame, 0);

  /* UV plane */
  uv_in = GST_VIDEO_FRAME_PLANE_DATA (in_frame, 1);
  uv_out = GST_VIDEO_FRAME_PLANE_DATA (out_frame, 1);

  y_in += crop_top * GST_VIDEO_FRAME_PLANE_STRIDE (in_frame, 0) + crop_left;
  dx = width;

  for (i = 0; i < height; ++i) {
    memcpy (y_out, y_in, dx);
    y_in += GST_VIDEO_FRAME_PLANE_STRIDE (in_frame, 0);
    y_out += GST_VIDEO_FRAME_PLANE_STRIDE (out_frame, 0);
  }

  uv_in += (crop_top / 2) * GST_VIDEO_FRAME_PLANE_STRIDE (in_frame, 1);
  uv_in += GST_ROUND_DOWN_2 (crop_left);
  dx = GST_ROUND_UP_2 (width);

  for (i = 0; i < GST_ROUND_UP_2 (height) / 2; i++) {
    memcpy (uv_out, uv_in, dx);
    uv_in += GST_VIDEO_FRAME_PLANE_STRIDE (in_frame, 1);
    uv_out += GST_VIDEO_FRAME_PLANE_STRIDE (out_frame, 1);
  }
}

static GstFlowReturn
gst_video_crop_transform_frame (GstVideoFilter * vfilter,
    GstVideoFrame * in_frame, GstVideoFrame * out_frame)
{
  GstVideoCrop *vcrop = GST_VIDEO_CROP (vfilter);
  GstVideoCropMeta *meta = gst_buffer_get_video_crop_meta (in_frame->buffer);
  gint x = 0, y = 0;

  if (G_UNLIKELY (vcrop->need_update)) {
    if (!gst_video_crop_set_info (vfilter, NULL, &vcrop->in_info, NULL,
            &vcrop->out_info)) {
      return GST_FLOW_ERROR;
    }
  }

  if (meta) {
    x = meta->x;
    y = meta->y;
  }

  switch (vcrop->packing) {
    case VIDEO_CROP_PIXEL_FORMAT_PACKED_SIMPLE:
      gst_video_crop_transform_packed_simple (vcrop, in_frame, out_frame, x, y);
      break;
    case VIDEO_CROP_PIXEL_FORMAT_PACKED_YVYU:
      gst_video_crop_transform_packed_yvyu (vcrop, in_frame, out_frame, x, y);
      break;
    case VIDEO_CROP_PIXEL_FORMAT_PACKED_v210:
      gst_video_crop_transform_packed_v210 (vcrop, in_frame, out_frame, x, y);
      break;
    case VIDEO_CROP_PIXEL_FORMAT_PLANAR:
      gst_video_crop_transform_planar (vcrop, in_frame, out_frame, x, y);
      break;
    case VIDEO_CROP_PIXEL_FORMAT_SEMI_PLANAR:
      gst_video_crop_transform_semi_planar (vcrop, in_frame, out_frame, x, y);
      break;
    default:
      g_assert_not_reached ();
  }

  return GST_FLOW_OK;
}

static gboolean
gst_video_crop_decide_allocation (GstBaseTransform * trans, GstQuery * query)
{
  GstVideoCrop *crop = GST_VIDEO_CROP (trans);
  gboolean use_crop_meta;

  use_crop_meta = (gst_query_find_allocation_meta (query,
          GST_VIDEO_CROP_META_API_TYPE, NULL) &&
      gst_query_find_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL));

  if ((crop->crop_left | crop->crop_right | crop->crop_top | crop->
          crop_bottom) == 0) {
    GST_INFO_OBJECT (crop, "we are using passthrough");
    gst_base_transform_set_passthrough (GST_BASE_TRANSFORM (crop), TRUE);
    gst_base_transform_set_in_place (GST_BASE_TRANSFORM (crop), FALSE);
  } else if (use_crop_meta) {
    GST_INFO_OBJECT (crop, "we are doing in-place transform using crop meta");
    gst_base_transform_set_passthrough (GST_BASE_TRANSFORM (crop), FALSE);
    gst_base_transform_set_in_place (GST_BASE_TRANSFORM (crop), TRUE);
  } else if (crop->raw_caps) {
    GST_INFO_OBJECT (crop, "we are not using passthrough");
    gst_base_transform_set_passthrough (GST_BASE_TRANSFORM (crop), FALSE);
    gst_base_transform_set_in_place (GST_BASE_TRANSFORM (crop), FALSE);
  } else {
    GST_ELEMENT_ERROR (crop, STREAM, WRONG_TYPE,
        ("Dowstream doesn't support crop for non-raw caps"), (NULL));
    return FALSE;
  }

  return GST_BASE_TRANSFORM_CLASS (parent_class)->decide_allocation (trans,
      query);
}

static gboolean
gst_video_crop_propose_allocation (GstBaseTransform * trans,
    GstQuery * decide_query, GstQuery * query)
{
  /* if we are not passthrough, we can handle video meta and crop meta */
  if (decide_query) {
    GST_DEBUG_OBJECT (trans, "Advertising video meta and crop meta support");
    gst_query_add_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL);
    gst_query_add_allocation_meta (query, GST_VIDEO_CROP_META_API_TYPE, NULL);
  }

  return GST_BASE_TRANSFORM_CLASS (parent_class)->propose_allocation (trans,
      decide_query, query);
}

static void
gst_video_crop_before_transform (GstBaseTransform * trans, GstBuffer * in)
{
  GstVideoCrop *video_crop = GST_VIDEO_CROP (trans);
  GstClockTime timestamp, stream_time;

  timestamp = GST_BUFFER_TIMESTAMP (in);
  stream_time =
      gst_segment_to_stream_time (&trans->segment, GST_FORMAT_TIME, timestamp);

  GST_DEBUG_OBJECT (video_crop, "sync to %" GST_TIME_FORMAT,
      GST_TIME_ARGS (timestamp));

  if (GST_CLOCK_TIME_IS_VALID (stream_time))
    gst_object_sync_values (GST_OBJECT (video_crop), stream_time);
}

static GstFlowReturn
gst_video_crop_transform_ip (GstBaseTransform * trans, GstBuffer * buf)
{
  GstVideoCrop *vcrop = GST_VIDEO_CROP (trans);
  GstVideoFilter *vfilter = GST_VIDEO_FILTER (trans);
  GstVideoMeta *video_meta;
  GstVideoCropMeta *crop_meta;

  GST_LOG_OBJECT (trans, "Transforming in-place");

  if (G_UNLIKELY (vcrop->need_update)) {
    if (!gst_video_crop_set_info (vfilter, NULL, &vcrop->in_info, NULL,
            &vcrop->out_info)) {
      return GST_FLOW_ERROR;
    }
  }

  /* The video meta is required since we are going to make the caps
   * width/height smaller, which would not result in a usable GstVideoInfo for
   * mapping the buffer. */
  video_meta = gst_buffer_get_video_meta (buf);
  if (!video_meta) {
    video_meta = gst_buffer_add_video_meta (buf, GST_VIDEO_FRAME_FLAG_NONE,
        GST_VIDEO_INFO_FORMAT (&vcrop->in_info), vcrop->in_info.width,
        vcrop->in_info.height);
  }

  crop_meta = gst_buffer_get_video_crop_meta (buf);
  if (!crop_meta)
    crop_meta = gst_buffer_add_video_crop_meta (buf);

  crop_meta->x += vcrop->crop_left;
  crop_meta->y += vcrop->crop_top;
  crop_meta->width = GST_VIDEO_INFO_WIDTH (&vcrop->out_info);
  crop_meta->height = GST_VIDEO_INFO_HEIGHT (&vcrop->out_info);

  return GST_FLOW_OK;
}

static gint
gst_video_crop_transform_dimension (gint val, gint delta)
{
  gint64 new_val = (gint64) val + (gint64) delta;

  new_val = CLAMP (new_val, 1, G_MAXINT);

  return (gint) new_val;
}

static gboolean
gst_video_crop_transform_dimension_value (const GValue * src_val,
    gint delta, GValue * dest_val, GstPadDirection direction, gboolean dynamic)
{
  gboolean ret = TRUE;

  if (G_VALUE_HOLDS_INT (src_val)) {
    gint ival = g_value_get_int (src_val);
    ival = gst_video_crop_transform_dimension (ival, delta);

    if (dynamic) {
      if (direction == GST_PAD_SRC) {
        if (ival == G_MAXINT) {
          g_value_init (dest_val, G_TYPE_INT);
          g_value_set_int (dest_val, ival);
        } else {
          g_value_init (dest_val, GST_TYPE_INT_RANGE);
          gst_value_set_int_range (dest_val, ival, G_MAXINT);
        }
      } else {
        if (ival == 1) {
          g_value_init (dest_val, G_TYPE_INT);
          g_value_set_int (dest_val, ival);
        } else {
          g_value_init (dest_val, GST_TYPE_INT_RANGE);
          gst_value_set_int_range (dest_val, 1, ival);
        }
      }
    } else {
      g_value_init (dest_val, G_TYPE_INT);
      g_value_set_int (dest_val, ival);
    }
  } else if (GST_VALUE_HOLDS_INT_RANGE (src_val)) {
    gint min = gst_value_get_int_range_min (src_val);
    gint max = gst_value_get_int_range_max (src_val);

    min = gst_video_crop_transform_dimension (min, delta);
    max = gst_video_crop_transform_dimension (max, delta);

    if (dynamic) {
      if (direction == GST_PAD_SRC)
        max = G_MAXINT;
      else
        min = 1;
    }

    if (min == max) {
      g_value_init (dest_val, G_TYPE_INT);
      g_value_set_int (dest_val, min);
    } else {
      g_value_init (dest_val, GST_TYPE_INT_RANGE);
      gst_value_set_int_range (dest_val, min, max);
    }
  } else if (GST_VALUE_HOLDS_LIST (src_val)) {
    gint i;

    g_value_init (dest_val, GST_TYPE_LIST);

    for (i = 0; i < gst_value_list_get_size (src_val); ++i) {
      const GValue *list_val;
      GValue newval = G_VALUE_INIT;

      list_val = gst_value_list_get_value (src_val, i);
      if (gst_video_crop_transform_dimension_value (list_val, delta, &newval,
              direction, dynamic))
        gst_value_list_append_value (dest_val, &newval);
      g_value_unset (&newval);
    }

    if (gst_value_list_get_size (dest_val) == 0) {
      g_value_unset (dest_val);
      ret = FALSE;
    }
  } else {
    ret = FALSE;
  }

  return ret;
}

static GstCaps *
gst_video_crop_transform_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter_caps)
{
  GstVideoCrop *vcrop;
  GstCaps *other_caps;
  gint dy, dx, i, left, right, bottom, top;
  gboolean w_dynamic, h_dynamic;

  vcrop = GST_VIDEO_CROP (trans);

  GST_OBJECT_LOCK (vcrop);

  GST_LOG_OBJECT (vcrop, "l=%d,r=%d,b=%d,t=%d",
      vcrop->prop_left, vcrop->prop_right, vcrop->prop_bottom, vcrop->prop_top);

  w_dynamic = (vcrop->prop_left == -1 || vcrop->prop_right == -1);
  h_dynamic = (vcrop->prop_top == -1 || vcrop->prop_bottom == -1);

  left = (vcrop->prop_left == -1) ? 0 : vcrop->prop_left;
  right = (vcrop->prop_right == -1) ? 0 : vcrop->prop_right;
  bottom = (vcrop->prop_bottom == -1) ? 0 : vcrop->prop_bottom;
  top = (vcrop->prop_top == -1) ? 0 : vcrop->prop_top;

  GST_OBJECT_UNLOCK (vcrop);

  if (direction == GST_PAD_SRC) {
    dx = left + right;
    dy = top + bottom;
  } else {
    dx = 0 - (left + right);
    dy = 0 - (top + bottom);
  }

  GST_LOG_OBJECT (vcrop, "transforming caps %" GST_PTR_FORMAT, caps);

  other_caps = gst_caps_new_empty ();

  for (i = 0; i < gst_caps_get_size (caps); ++i) {
    const GValue *v;
    GstStructure *structure, *new_structure;
    GValue w_val = G_VALUE_INIT, h_val = G_VALUE_INIT;
    GstCapsFeatures *features;

    structure = gst_caps_get_structure (caps, i);
    features = gst_caps_get_features (caps, i);

    v = gst_structure_get_value (structure, "width");
    if (!gst_video_crop_transform_dimension_value (v, dx, &w_val, direction,
            w_dynamic)) {
      GST_WARNING_OBJECT (vcrop, "could not transform width value with dx=%d"
          ", caps structure=%" GST_PTR_FORMAT, dx, structure);
      continue;
    }

    v = gst_structure_get_value (structure, "height");
    if (!gst_video_crop_transform_dimension_value (v, dy, &h_val, direction,
            h_dynamic)) {
      g_value_unset (&w_val);
      GST_WARNING_OBJECT (vcrop, "could not transform height value with dy=%d"
          ", caps structure=%" GST_PTR_FORMAT, dy, structure);
      continue;
    }

    new_structure = gst_structure_copy (structure);
    gst_structure_set_value (new_structure, "width", &w_val);
    gst_structure_set_value (new_structure, "height", &h_val);
    g_value_unset (&w_val);
    g_value_unset (&h_val);

    GST_LOG_OBJECT (vcrop, "transformed structure %2d: %" GST_PTR_FORMAT
        " => %" GST_PTR_FORMAT "features %" GST_PTR_FORMAT, i, structure,
        new_structure, features);
    gst_caps_append_structure (other_caps, new_structure);

    gst_caps_set_features (other_caps, i, gst_caps_features_copy (features));
  }

  if (!gst_caps_is_empty (other_caps) && filter_caps) {
    GstCaps *tmp = gst_caps_intersect_full (filter_caps, other_caps,
        GST_CAPS_INTERSECT_FIRST);
    gst_caps_replace (&other_caps, tmp);
    gst_caps_unref (tmp);
  }

  return other_caps;
}

static gboolean
gst_video_crop_set_info (GstVideoFilter * vfilter, GstCaps * in,
    GstVideoInfo * in_info, GstCaps * out, GstVideoInfo * out_info)
{
  GstVideoCrop *crop = GST_VIDEO_CROP (vfilter);
  GstCapsFeatures *features;
  int dx, dy;

  GST_OBJECT_LOCK (crop);
  crop->need_update = FALSE;
  crop->crop_left = crop->prop_left;
  crop->crop_right = crop->prop_right;
  crop->crop_top = crop->prop_top;
  crop->crop_bottom = crop->prop_bottom;
  GST_OBJECT_UNLOCK (crop);

  dx = GST_VIDEO_INFO_WIDTH (in_info) - GST_VIDEO_INFO_WIDTH (out_info);
  dy = GST_VIDEO_INFO_HEIGHT (in_info) - GST_VIDEO_INFO_HEIGHT (out_info);

  if (crop->crop_left == -1 && crop->crop_right == -1) {
    crop->crop_left = dx / 2;
    crop->crop_right = dx / 2 + (dx & 1);
  } else if (crop->crop_left == -1) {
    if (G_UNLIKELY (crop->crop_right > dx))
      goto cropping_too_much;
    crop->crop_left = dx - crop->crop_right;
  } else if (crop->crop_right == -1) {
    if (G_UNLIKELY (crop->crop_left > dx))
      goto cropping_too_much;
    crop->crop_right = dx - crop->crop_left;
  }

  if (crop->crop_top == -1 && crop->crop_bottom == -1) {
    crop->crop_top = dy / 2;
    crop->crop_bottom = dy / 2 + (dy & 1);
  } else if (crop->crop_top == -1) {
    if (G_UNLIKELY (crop->crop_bottom > dy))
      goto cropping_too_much;
    crop->crop_top = dy - crop->crop_bottom;
  } else if (crop->crop_bottom == -1) {
    if (G_UNLIKELY (crop->crop_top > dy))
      goto cropping_too_much;
    crop->crop_bottom = dy - crop->crop_top;
  }

  if (G_UNLIKELY ((crop->crop_left + crop->crop_right) >=
          GST_VIDEO_INFO_WIDTH (in_info)
          || (crop->crop_top + crop->crop_bottom) >=
          GST_VIDEO_INFO_HEIGHT (in_info)))
    goto cropping_too_much;

  if (in && out)
    GST_LOG_OBJECT (crop, "incaps = %" GST_PTR_FORMAT ", outcaps = %"
        GST_PTR_FORMAT, in, out);

  if (in) {
    features = gst_caps_get_features (in, 0);
    crop->raw_caps = gst_caps_features_is_equal (features,
        GST_CAPS_FEATURES_MEMORY_SYSTEM_MEMORY);
  }

  if (!crop->raw_caps)
    goto beach;

  switch (GST_VIDEO_INFO_FORMAT (in_info)) {
    case GST_VIDEO_FORMAT_RGB:
    case GST_VIDEO_FORMAT_BGR:
    case GST_VIDEO_FORMAT_RGB16:
    case GST_VIDEO_FORMAT_RGB15:
    case GST_VIDEO_FORMAT_RGBx:
    case GST_VIDEO_FORMAT_xRGB:
    case GST_VIDEO_FORMAT_BGRx:
    case GST_VIDEO_FORMAT_xBGR:
    case GST_VIDEO_FORMAT_RGBA:
    case GST_VIDEO_FORMAT_ARGB:
    case GST_VIDEO_FORMAT_BGRA:
    case GST_VIDEO_FORMAT_ABGR:
    case GST_VIDEO_FORMAT_GRAY8:
    case GST_VIDEO_FORMAT_GRAY16_LE:
    case GST_VIDEO_FORMAT_GRAY16_BE:
    case GST_VIDEO_FORMAT_AYUV:
      crop->packing = VIDEO_CROP_PIXEL_FORMAT_PACKED_SIMPLE;
      break;
    case GST_VIDEO_FORMAT_YVYU:
    case GST_VIDEO_FORMAT_YUY2:
    case GST_VIDEO_FORMAT_UYVY:
      crop->packing = VIDEO_CROP_PIXEL_FORMAT_PACKED_YVYU;
      if (GST_VIDEO_INFO_FORMAT (in_info) == GST_VIDEO_FORMAT_UYVY) {
        /* UYVY = 4:2:2 - [U0 Y0 V0 Y1] [U2 Y2 V2 Y3] [U4 Y4 V4 Y5] */
        crop->macro_y_off = 1;
      } else {
        /* YUYV = 4:2:2 - [Y0 U0 Y1 V0] [Y2 U2 Y3 V2] [Y4 U4 Y5 V4] = YUY2 */
        crop->macro_y_off = 0;
      }
      break;
    case GST_VIDEO_FORMAT_v210:
      crop->packing = VIDEO_CROP_PIXEL_FORMAT_PACKED_v210;
      break;
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_I420_10BE:
    case GST_VIDEO_FORMAT_I420_10LE:
    case GST_VIDEO_FORMAT_I420_12BE:
    case GST_VIDEO_FORMAT_I420_12LE:
    case GST_VIDEO_FORMAT_A420:
    case GST_VIDEO_FORMAT_A420_10BE:
    case GST_VIDEO_FORMAT_A420_10LE:
    case GST_VIDEO_FORMAT_YV12:
    case GST_VIDEO_FORMAT_Y444:
    case GST_VIDEO_FORMAT_Y444_10BE:
    case GST_VIDEO_FORMAT_Y444_10LE:
    case GST_VIDEO_FORMAT_Y444_12BE:
    case GST_VIDEO_FORMAT_Y444_12LE:
    case GST_VIDEO_FORMAT_A444_10BE:
    case GST_VIDEO_FORMAT_A444_10LE:
    case GST_VIDEO_FORMAT_Y42B:
    case GST_VIDEO_FORMAT_I422_10BE:
    case GST_VIDEO_FORMAT_I422_10LE:
    case GST_VIDEO_FORMAT_A422_10BE:
    case GST_VIDEO_FORMAT_A422_10LE:
    case GST_VIDEO_FORMAT_I422_12BE:
    case GST_VIDEO_FORMAT_I422_12LE:
    case GST_VIDEO_FORMAT_GBR:
    case GST_VIDEO_FORMAT_GBR_10BE:
    case GST_VIDEO_FORMAT_GBR_10LE:
    case GST_VIDEO_FORMAT_GBR_12BE:
    case GST_VIDEO_FORMAT_GBR_12LE:
    case GST_VIDEO_FORMAT_GBRA:
    case GST_VIDEO_FORMAT_GBRA_10BE:
    case GST_VIDEO_FORMAT_GBRA_10LE:
    case GST_VIDEO_FORMAT_GBRA_12BE:
    case GST_VIDEO_FORMAT_GBRA_12LE:
    case GST_VIDEO_FORMAT_Y41B:
      crop->packing = VIDEO_CROP_PIXEL_FORMAT_PLANAR;
      break;
    case GST_VIDEO_FORMAT_NV12:
    case GST_VIDEO_FORMAT_NV21:
      crop->packing = VIDEO_CROP_PIXEL_FORMAT_SEMI_PLANAR;
      break;
    default:
      goto unknown_format;
  }

beach:
  crop->in_info = *in_info;
  crop->out_info = *out_info;

  /* Ensure our decide_allocation will be called again when needed */
  if (gst_base_transform_is_passthrough (GST_BASE_TRANSFORM (crop))) {
    gst_base_transform_set_passthrough (GST_BASE_TRANSFORM (crop), FALSE);
    gst_base_transform_set_in_place (GST_BASE_TRANSFORM (crop), FALSE);
  }

  return TRUE;

  /* ERROR */
cropping_too_much:
  {
    GST_WARNING_OBJECT (crop, "we are cropping too much");
    return FALSE;
  }
unknown_format:
  {
    GST_WARNING_OBJECT (crop, "Unsupported format");
    return FALSE;
  }
}

/* called with object lock */
static inline void
gst_video_crop_set_crop (GstVideoCrop * vcrop, gint new_value, gint * prop)
{
  if (*prop != new_value) {
    *prop = new_value;
    vcrop->need_update = TRUE;
  }
}

static void
gst_video_crop_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVideoCrop *video_crop;

  video_crop = GST_VIDEO_CROP (object);

  GST_OBJECT_LOCK (video_crop);
  switch (prop_id) {
    case PROP_LEFT:
      gst_video_crop_set_crop (video_crop, g_value_get_int (value),
          &video_crop->prop_left);
      break;
    case PROP_RIGHT:
      gst_video_crop_set_crop (video_crop, g_value_get_int (value),
          &video_crop->prop_right);
      break;
    case PROP_TOP:
      gst_video_crop_set_crop (video_crop, g_value_get_int (value),
          &video_crop->prop_top);
      break;
    case PROP_BOTTOM:
      gst_video_crop_set_crop (video_crop, g_value_get_int (value),
          &video_crop->prop_bottom);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  GST_LOG_OBJECT (video_crop, "l=%d,r=%d,b=%d,t=%d, need_update:%d",
      video_crop->prop_left, video_crop->prop_right, video_crop->prop_bottom,
      video_crop->prop_top, video_crop->need_update);

  GST_OBJECT_UNLOCK (video_crop);

  gst_base_transform_reconfigure_src (GST_BASE_TRANSFORM (video_crop));
}

static void
gst_video_crop_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstVideoCrop *video_crop;

  video_crop = GST_VIDEO_CROP (object);

  GST_OBJECT_LOCK (video_crop);
  switch (prop_id) {
    case PROP_LEFT:
      g_value_set_int (value, video_crop->prop_left);
      break;
    case PROP_RIGHT:
      g_value_set_int (value, video_crop->prop_right);
      break;
    case PROP_TOP:
      g_value_set_int (value, video_crop->prop_top);
      break;
    case PROP_BOTTOM:
      g_value_set_int (value, video_crop->prop_bottom);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (video_crop);
}
