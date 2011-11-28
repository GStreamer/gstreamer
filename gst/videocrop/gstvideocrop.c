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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/**
 * SECTION:element-videocrop
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
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v videotestsrc ! videocrop top=42 left=1 right=4 bottom=0 ! ximagesink
 * ]|
 * </refsect2>
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

#include <string.h>

GST_DEBUG_CATEGORY_STATIC (videocrop_debug);
#define GST_CAT_DEFAULT videocrop_debug

enum
{
  ARG_0,
  ARG_LEFT,
  ARG_RIGHT,
  ARG_TOP,
  ARG_BOTTOM
};

/* the formats we support */
#define GST_VIDEO_CAPS_GRAY "video/x-raw-gray, "			\
    "bpp = (int) 8, "                                                  \
    "width = " GST_VIDEO_SIZE_RANGE ", "                                \
    "height = " GST_VIDEO_SIZE_RANGE ", "                               \
    "framerate = " GST_VIDEO_FPS_RANGE

#define VIDEO_CROP_CAPS                          \
  GST_VIDEO_CAPS_RGBx ";"                        \
  GST_VIDEO_CAPS_xRGB ";"                        \
  GST_VIDEO_CAPS_BGRx ";"                        \
  GST_VIDEO_CAPS_xBGR ";"                        \
  GST_VIDEO_CAPS_RGBA ";"                        \
  GST_VIDEO_CAPS_ARGB ";"                        \
  GST_VIDEO_CAPS_BGRA ";"                        \
  GST_VIDEO_CAPS_ABGR ";"                        \
  GST_VIDEO_CAPS_RGB ";"                         \
  GST_VIDEO_CAPS_BGR ";"                         \
  GST_VIDEO_CAPS_YUV ("AYUV") ";"                \
  GST_VIDEO_CAPS_YUV ("YUY2") ";"                \
  GST_VIDEO_CAPS_YUV ("YVYU") ";"                \
  GST_VIDEO_CAPS_YUV ("UYVY") ";"                \
  GST_VIDEO_CAPS_YUV ("Y800") ";"                \
  GST_VIDEO_CAPS_YUV ("I420") ";"                \
  GST_VIDEO_CAPS_YUV ("YV12") ";"                \
  GST_VIDEO_CAPS_RGB_16 ";"                      \
  GST_VIDEO_CAPS_RGB_15 ";"			 \
  GST_VIDEO_CAPS_GRAY

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

GST_BOILERPLATE (GstVideoCrop, gst_video_crop, GstBaseTransform,
    GST_TYPE_BASE_TRANSFORM);

static void gst_video_crop_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_video_crop_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstCaps *gst_video_crop_transform_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps);
static GstFlowReturn gst_video_crop_transform (GstBaseTransform * trans,
    GstBuffer * inbuf, GstBuffer * outbuf);
static gboolean gst_video_crop_get_unit_size (GstBaseTransform * trans,
    GstCaps * caps, guint * size);
static gboolean gst_video_crop_set_caps (GstBaseTransform * trans,
    GstCaps * in_caps, GstCaps * outcaps);
static gboolean gst_video_crop_src_event (GstBaseTransform * trans,
    GstEvent * event);

static void
gst_video_crop_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details_simple (element_class, "Crop",
      "Filter/Effect/Video",
      "Crops video into a user-defined region",
      "Tim-Philipp Müller <tim centricular net>");

  gst_element_class_add_static_pad_template (element_class,
      &sink_template);
  gst_element_class_add_static_pad_template (element_class, &src_template);
}

static gboolean
gst_video_crop_src_event (GstBaseTransform * trans, GstEvent * event)
{
  GstEvent *new_event;
  GstStructure *new_structure;
  const GstStructure *structure;
  const gchar *event_name;
  double pointer_x;
  double pointer_y;

  GstVideoCrop *vcrop = GST_VIDEO_CROP (trans);
  new_event = NULL;

  GST_OBJECT_LOCK (vcrop);
  if (GST_EVENT_TYPE (event) == GST_EVENT_NAVIGATION &&
      (vcrop->crop_left != 0 || vcrop->crop_top != 0)) {
    structure = gst_event_get_structure (event);
    event_name = gst_structure_get_string (structure, "event");

    if (event_name &&
        (strcmp (event_name, "mouse-move") == 0 ||
            strcmp (event_name, "mouse-button-press") == 0 ||
            strcmp (event_name, "mouse-button-release") == 0)) {

      if (gst_structure_get_double (structure, "pointer_x", &pointer_x) &&
          gst_structure_get_double (structure, "pointer_y", &pointer_y)) {

        new_structure = gst_structure_copy (structure);
        gst_structure_set (new_structure,
            "pointer_x", G_TYPE_DOUBLE, (double) (pointer_x + vcrop->crop_left),
            "pointer_y", G_TYPE_DOUBLE, (double) (pointer_y + vcrop->crop_top),
            NULL);

        new_event = gst_event_new_navigation (new_structure);
        gst_event_unref (event);
      } else {
        GST_WARNING_OBJECT (vcrop, "Failed to read navigation event");
      }
    }
  }

  GST_OBJECT_UNLOCK (vcrop);
  return GST_BASE_TRANSFORM_CLASS (parent_class)->src_event (trans,
      (new_event ? new_event : event));
}

static void
gst_video_crop_class_init (GstVideoCropClass * klass)
{
  GObjectClass *gobject_class;
  GstBaseTransformClass *basetransform_class;

  gobject_class = (GObjectClass *) klass;
  basetransform_class = (GstBaseTransformClass *) klass;

  gobject_class->set_property = gst_video_crop_set_property;
  gobject_class->get_property = gst_video_crop_get_property;

  g_object_class_install_property (gobject_class, ARG_LEFT,
      g_param_spec_int ("left", "Left", "Pixels to crop at left",
          0, G_MAXINT, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, ARG_RIGHT,
      g_param_spec_int ("right", "Right", "Pixels to crop at right",
          0, G_MAXINT, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, ARG_TOP,
      g_param_spec_int ("top", "Top", "Pixels to crop at top",
          0, G_MAXINT, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, ARG_BOTTOM,
      g_param_spec_int ("bottom", "Bottom", "Pixels to crop at bottom",
          0, G_MAXINT, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  basetransform_class->transform = GST_DEBUG_FUNCPTR (gst_video_crop_transform);
  basetransform_class->transform_caps =
      GST_DEBUG_FUNCPTR (gst_video_crop_transform_caps);
  basetransform_class->set_caps = GST_DEBUG_FUNCPTR (gst_video_crop_set_caps);
  basetransform_class->get_unit_size =
      GST_DEBUG_FUNCPTR (gst_video_crop_get_unit_size);

  basetransform_class->passthrough_on_same_caps = FALSE;
  basetransform_class->src_event = GST_DEBUG_FUNCPTR (gst_video_crop_src_event);
}

static void
gst_video_crop_init (GstVideoCrop * vcrop, GstVideoCropClass * klass)
{
  vcrop->crop_right = 0;
  vcrop->crop_left = 0;
  vcrop->crop_top = 0;
  vcrop->crop_bottom = 0;
}

static gboolean
gst_video_crop_get_image_details_from_caps (GstVideoCrop * vcrop,
    GstVideoCropImageDetails * details, GstCaps * caps)
{
  GstStructure *structure;
  gint width, height;

  structure = gst_caps_get_structure (caps, 0);
  if (!gst_structure_get_int (structure, "width", &width) ||
      !gst_structure_get_int (structure, "height", &height)) {
    goto incomplete_format;
  }

  details->width = width;
  details->height = height;

  if (gst_structure_has_name (structure, "video/x-raw-rgb") ||
      gst_structure_has_name (structure, "video/x-raw-gray")) {
    gint bpp = 0;

    if (!gst_structure_get_int (structure, "bpp", &bpp) || (bpp & 0x07) != 0)
      goto incomplete_format;

    details->packing = VIDEO_CROP_PIXEL_FORMAT_PACKED_SIMPLE;
    details->bytes_per_pixel = bpp / 8;
    details->stride = GST_ROUND_UP_4 (width * details->bytes_per_pixel);
    details->size = details->stride * height;
  } else if (gst_structure_has_name (structure, "video/x-raw-yuv")) {
    guint32 format = 0;

    if (!gst_structure_get_fourcc (structure, "format", &format))
      goto incomplete_format;

    switch (format) {
      case GST_MAKE_FOURCC ('A', 'Y', 'U', 'V'):
        details->packing = VIDEO_CROP_PIXEL_FORMAT_PACKED_SIMPLE;
        details->bytes_per_pixel = 4;
        details->stride = GST_ROUND_UP_4 (width * 4);
        details->size = details->stride * height;
        break;
      case GST_MAKE_FOURCC ('Y', 'V', 'Y', 'U'):
      case GST_MAKE_FOURCC ('Y', 'U', 'Y', '2'):
      case GST_MAKE_FOURCC ('U', 'Y', 'V', 'Y'):
        details->packing = VIDEO_CROP_PIXEL_FORMAT_PACKED_COMPLEX;
        details->bytes_per_pixel = 2;
        details->stride = GST_ROUND_UP_4 (width * 2);
        details->size = details->stride * height;
        if (format == GST_MAKE_FOURCC ('U', 'Y', 'V', 'Y')) {
          /* UYVY = 4:2:2 - [U0 Y0 V0 Y1] [U2 Y2 V2 Y3] [U4 Y4 V4 Y5] */
          details->macro_y_off = 1;
        } else {
          /* YUYV = 4:2:2 - [Y0 U0 Y1 V0] [Y2 U2 Y3 V2] [Y4 U4 Y5 V4] = YUY2 */
          details->macro_y_off = 0;
        }
        break;
      case GST_MAKE_FOURCC ('Y', '8', '0', '0'):
        details->packing = VIDEO_CROP_PIXEL_FORMAT_PACKED_SIMPLE;
        details->bytes_per_pixel = 1;
        details->stride = GST_ROUND_UP_4 (width);
        details->size = details->stride * height;
        break;
      case GST_MAKE_FOURCC ('I', '4', '2', '0'):
      case GST_MAKE_FOURCC ('Y', 'V', '1', '2'):{
        details->packing = VIDEO_CROP_PIXEL_FORMAT_PLANAR;

        details->y_stride = GST_ROUND_UP_4 (width);
        details->u_stride = GST_ROUND_UP_8 (width) / 2;
        details->v_stride = GST_ROUND_UP_8 (width) / 2;

        /* I420 and YV12 have U/V planes swapped, but doesn't matter for us */
        details->y_off = 0;
        details->u_off = 0 + details->y_stride * GST_ROUND_UP_2 (height);
        details->v_off = details->u_off +
            details->u_stride * (GST_ROUND_UP_2 (height) / 2);
        details->size = details->v_off +
            details->v_stride * (GST_ROUND_UP_2 (height) / 2);
        break;
      }
      default:
        goto unknown_format;
    }
  } else {
    goto unknown_format;
  }

  return TRUE;

  /* ERRORS */
unknown_format:
  {
    GST_ELEMENT_ERROR (vcrop, STREAM, NOT_IMPLEMENTED, (NULL),
        ("Unsupported format"));
    return FALSE;
  }

incomplete_format:
  {
    GST_ELEMENT_ERROR (vcrop, CORE, NEGOTIATION, (NULL),
        ("Incomplete caps, some required field is missing"));
    return FALSE;
  }
}

static gboolean
gst_video_crop_get_unit_size (GstBaseTransform * trans, GstCaps * caps,
    guint * size)
{
  GstVideoCropImageDetails img_details = { 0, };
  GstVideoCrop *vcrop = GST_VIDEO_CROP (trans);

  if (!gst_video_crop_get_image_details_from_caps (vcrop, &img_details, caps))
    return FALSE;

  *size = img_details.size;
  return TRUE;
}

#define ROUND_DOWN_2(n)  ((n)&(~1))

static void
gst_video_crop_transform_packed_complex (GstVideoCrop * vcrop,
    GstBuffer * inbuf, GstBuffer * outbuf)
{
  guint8 *in_data, *out_data;
  guint i, dx;

  in_data = GST_BUFFER_DATA (inbuf);
  out_data = GST_BUFFER_DATA (outbuf);

  in_data += vcrop->crop_top * vcrop->in.stride;

  /* rounding down here so we end up at the start of a macro-pixel and not
   * in the middle of one */
  in_data += ROUND_DOWN_2 (vcrop->crop_left) * vcrop->in.bytes_per_pixel;

  dx = vcrop->out.width * vcrop->out.bytes_per_pixel;

  /* UYVY = 4:2:2 - [U0 Y0 V0 Y1] [U2 Y2 V2 Y3] [U4 Y4 V4 Y5]
   * YUYV = 4:2:2 - [Y0 U0 Y1 V0] [Y2 U2 Y3 V2] [Y4 U4 Y5 V4] = YUY2 */
  if ((vcrop->crop_left % 2) != 0) {
    for (i = 0; i < vcrop->out.height; ++i) {
      gint j;

      memcpy (out_data, in_data, dx);

      /* move just the Y samples one pixel to the left, don't worry about
       * chroma shift */
      for (j = vcrop->in.macro_y_off; j < vcrop->out.stride - 2; j += 2)
        out_data[j] = in_data[j + 2];

      in_data += vcrop->in.stride;
      out_data += vcrop->out.stride;
    }
  } else {
    for (i = 0; i < vcrop->out.height; ++i) {
      memcpy (out_data, in_data, dx);
      in_data += vcrop->in.stride;
      out_data += vcrop->out.stride;
    }
  }
}

static void
gst_video_crop_transform_packed_simple (GstVideoCrop * vcrop,
    GstBuffer * inbuf, GstBuffer * outbuf)
{
  guint8 *in_data, *out_data;
  guint i, dx;

  in_data = GST_BUFFER_DATA (inbuf);
  out_data = GST_BUFFER_DATA (outbuf);

  in_data += vcrop->crop_top * vcrop->in.stride;
  in_data += vcrop->crop_left * vcrop->in.bytes_per_pixel;

  dx = vcrop->out.width * vcrop->out.bytes_per_pixel;

  for (i = 0; i < vcrop->out.height; ++i) {
    memcpy (out_data, in_data, dx);
    in_data += vcrop->in.stride;
    out_data += vcrop->out.stride;
  }
}

static void
gst_video_crop_transform_planar (GstVideoCrop * vcrop, GstBuffer * inbuf,
    GstBuffer * outbuf)
{
  guint8 *y_out, *u_out, *v_out;
  guint8 *y_in, *u_in, *v_in;
  guint i, dx;

  /* Y plane */
  y_in = GST_BUFFER_DATA (inbuf);
  y_out = GST_BUFFER_DATA (outbuf);

  y_in += (vcrop->crop_top * vcrop->in.y_stride) + vcrop->crop_left;
  dx = vcrop->out.width * 1;

  for (i = 0; i < vcrop->out.height; ++i) {
    memcpy (y_out, y_in, dx);
    y_in += vcrop->in.y_stride;
    y_out += vcrop->out.y_stride;
  }

  /* U + V planes */
  u_in = GST_BUFFER_DATA (inbuf) + vcrop->in.u_off;
  u_out = GST_BUFFER_DATA (outbuf) + vcrop->out.u_off;

  u_in += (vcrop->crop_top / 2) * vcrop->in.u_stride;
  u_in += vcrop->crop_left / 2;

  v_in = GST_BUFFER_DATA (inbuf) + vcrop->in.v_off;
  v_out = GST_BUFFER_DATA (outbuf) + vcrop->out.v_off;

  v_in += (vcrop->crop_top / 2) * vcrop->in.v_stride;
  v_in += vcrop->crop_left / 2;

  dx = GST_ROUND_UP_2 (vcrop->out.width) / 2;

  for (i = 0; i < GST_ROUND_UP_2 (vcrop->out.height) / 2; ++i) {
    memcpy (u_out, u_in, dx);
    memcpy (v_out, v_in, dx);
    u_in += vcrop->in.u_stride;
    u_out += vcrop->out.u_stride;
    v_in += vcrop->in.v_stride;
    v_out += vcrop->out.v_stride;
  }
}

static GstFlowReturn
gst_video_crop_transform (GstBaseTransform * trans, GstBuffer * inbuf,
    GstBuffer * outbuf)
{
  GstVideoCrop *vcrop = GST_VIDEO_CROP (trans);

  switch (vcrop->in.packing) {
    case VIDEO_CROP_PIXEL_FORMAT_PACKED_SIMPLE:
      gst_video_crop_transform_packed_simple (vcrop, inbuf, outbuf);
      break;
    case VIDEO_CROP_PIXEL_FORMAT_PACKED_COMPLEX:
      gst_video_crop_transform_packed_complex (vcrop, inbuf, outbuf);
      break;
    case VIDEO_CROP_PIXEL_FORMAT_PLANAR:
      gst_video_crop_transform_planar (vcrop, inbuf, outbuf);
      break;
    default:
      g_assert_not_reached ();
  }

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
    gint delta, GValue * dest_val)
{
  gboolean ret = TRUE;

  g_value_init (dest_val, G_VALUE_TYPE (src_val));

  if (G_VALUE_HOLDS_INT (src_val)) {
    gint ival = g_value_get_int (src_val);

    ival = gst_video_crop_transform_dimension (ival, delta);
    g_value_set_int (dest_val, ival);
  } else if (GST_VALUE_HOLDS_INT_RANGE (src_val)) {
    gint min = gst_value_get_int_range_min (src_val);
    gint max = gst_value_get_int_range_max (src_val);

    min = gst_video_crop_transform_dimension (min, delta);
    max = gst_video_crop_transform_dimension (max, delta);
    gst_value_set_int_range (dest_val, min, max);
  } else if (GST_VALUE_HOLDS_LIST (src_val)) {
    gint i;

    for (i = 0; i < gst_value_list_get_size (src_val); ++i) {
      const GValue *list_val;
      GValue newval = { 0, };

      list_val = gst_value_list_get_value (src_val, i);
      if (gst_video_crop_transform_dimension_value (list_val, delta, &newval))
        gst_value_list_append_value (dest_val, &newval);
      g_value_unset (&newval);
    }

    if (gst_value_list_get_size (dest_val) == 0) {
      g_value_unset (dest_val);
      ret = FALSE;
    }
  } else {
    g_value_unset (dest_val);
    ret = FALSE;
  }

  return ret;
}

static GstCaps *
gst_video_crop_transform_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps)
{
  GstVideoCrop *vcrop;
  GstCaps *other_caps;
  gint dy, dx, i;

  vcrop = GST_VIDEO_CROP (trans);

  GST_OBJECT_LOCK (vcrop);

  GST_LOG_OBJECT (vcrop, "l=%d,r=%d,b=%d,t=%d",
      vcrop->crop_left, vcrop->crop_right, vcrop->crop_bottom, vcrop->crop_top);

  if (direction == GST_PAD_SRC) {
    dx = vcrop->crop_left + vcrop->crop_right;
    dy = vcrop->crop_top + vcrop->crop_bottom;
  } else {
    dx = 0 - (vcrop->crop_left + vcrop->crop_right);
    dy = 0 - (vcrop->crop_top + vcrop->crop_bottom);
  }
  GST_OBJECT_UNLOCK (vcrop);

  GST_LOG_OBJECT (vcrop, "transforming caps %" GST_PTR_FORMAT, caps);

  other_caps = gst_caps_new_empty ();

  for (i = 0; i < gst_caps_get_size (caps); ++i) {
    const GValue *v;
    GstStructure *structure, *new_structure;
    GValue w_val = { 0, }, h_val = {
    0,};

    structure = gst_caps_get_structure (caps, i);

    v = gst_structure_get_value (structure, "width");
    if (!gst_video_crop_transform_dimension_value (v, dx, &w_val)) {
      GST_WARNING_OBJECT (vcrop, "could not tranform width value with dx=%d"
          ", caps structure=%" GST_PTR_FORMAT, dx, structure);
      continue;
    }

    v = gst_structure_get_value (structure, "height");
    if (!gst_video_crop_transform_dimension_value (v, dy, &h_val)) {
      g_value_unset (&w_val);
      GST_WARNING_OBJECT (vcrop, "could not tranform height value with dy=%d"
          ", caps structure=%" GST_PTR_FORMAT, dy, structure);
      continue;
    }

    new_structure = gst_structure_copy (structure);
    gst_structure_set_value (new_structure, "width", &w_val);
    gst_structure_set_value (new_structure, "height", &h_val);
    g_value_unset (&w_val);
    g_value_unset (&h_val);
    GST_LOG_OBJECT (vcrop, "transformed structure %2d: %" GST_PTR_FORMAT
        " => %" GST_PTR_FORMAT, i, structure, new_structure);
    gst_caps_append_structure (other_caps, new_structure);
  }

  if (gst_caps_is_empty (other_caps)) {
    gst_caps_unref (other_caps);
    other_caps = NULL;
  }

  return other_caps;
}

static gboolean
gst_video_crop_set_caps (GstBaseTransform * trans, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstVideoCrop *crop = GST_VIDEO_CROP (trans);

  if (!gst_video_crop_get_image_details_from_caps (crop, &crop->in, incaps))
    goto wrong_input;

  if (!gst_video_crop_get_image_details_from_caps (crop, &crop->out, outcaps))
    goto wrong_output;

  if (G_UNLIKELY ((crop->crop_left + crop->crop_right) >= crop->in.width ||
          (crop->crop_top + crop->crop_bottom) >= crop->in.height))
    goto cropping_too_much;

  GST_LOG_OBJECT (crop, "incaps = %" GST_PTR_FORMAT ", outcaps = %"
      GST_PTR_FORMAT, incaps, outcaps);

  if ((crop->crop_left | crop->crop_right | crop->crop_top | crop->
          crop_bottom) == 0) {
    GST_LOG_OBJECT (crop, "we are using passthrough");
    gst_base_transform_set_passthrough (GST_BASE_TRANSFORM (crop), TRUE);
  } else {
    GST_LOG_OBJECT (crop, "we are not using passthrough");
    gst_base_transform_set_passthrough (GST_BASE_TRANSFORM (crop), FALSE);
  }

  return TRUE;

  /* ERROR */
wrong_input:
  {
    GST_DEBUG_OBJECT (crop, "failed to parse input caps %" GST_PTR_FORMAT,
        incaps);
    return FALSE;
  }
wrong_output:
  {
    GST_DEBUG_OBJECT (crop, "failed to parse output caps %" GST_PTR_FORMAT,
        outcaps);
    return FALSE;
  }
cropping_too_much:
  {
    GST_DEBUG_OBJECT (crop, "we are cropping too much");
    return FALSE;
  }
}

static void
gst_video_crop_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVideoCrop *video_crop;

  video_crop = GST_VIDEO_CROP (object);

  /* don't modify while we are transforming */
  GST_BASE_TRANSFORM_LOCK (GST_BASE_TRANSFORM_CAST (video_crop));

  /* protect with the object lock so that we can read them */
  GST_OBJECT_LOCK (video_crop);
  switch (prop_id) {
    case ARG_LEFT:
      video_crop->crop_left = g_value_get_int (value);
      break;
    case ARG_RIGHT:
      video_crop->crop_right = g_value_get_int (value);
      break;
    case ARG_TOP:
      video_crop->crop_top = g_value_get_int (value);
      break;
    case ARG_BOTTOM:
      video_crop->crop_bottom = g_value_get_int (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (video_crop);

  GST_LOG_OBJECT (video_crop, "l=%d,r=%d,b=%d,t=%d",
      video_crop->crop_left, video_crop->crop_right, video_crop->crop_bottom,
      video_crop->crop_top);

  gst_base_transform_reconfigure (GST_BASE_TRANSFORM (video_crop));
  GST_BASE_TRANSFORM_UNLOCK (GST_BASE_TRANSFORM_CAST (video_crop));
}

static void
gst_video_crop_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstVideoCrop *video_crop;

  video_crop = GST_VIDEO_CROP (object);

  GST_OBJECT_LOCK (video_crop);
  switch (prop_id) {
    case ARG_LEFT:
      g_value_set_int (value, video_crop->crop_left);
      break;
    case ARG_RIGHT:
      g_value_set_int (value, video_crop->crop_right);
      break;
    case ARG_TOP:
      g_value_set_int (value, video_crop->crop_top);
      break;
    case ARG_BOTTOM:
      g_value_set_int (value, video_crop->crop_bottom);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (video_crop);
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (videocrop_debug, "videocrop", 0, "videocrop");

  if (gst_element_register (plugin, "videocrop", GST_RANK_NONE,
          GST_TYPE_VIDEO_CROP)
      && gst_element_register (plugin, "aspectratiocrop", GST_RANK_NONE,
          GST_TYPE_ASPECT_RATIO_CROP))
    return TRUE;

  return FALSE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "videocrop",
    "Crops video into a user-defined region",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
