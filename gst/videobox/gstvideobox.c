/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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
#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>
#include <gst/video/video.h>
#include <math.h>
#include <liboil/liboil.h>
#include <string.h>

GST_DEBUG_CATEGORY_STATIC (videobox_debug);
#define GST_CAT_DEFAULT videobox_debug

#define GST_TYPE_VIDEO_BOX \
  (gst_video_box_get_type())
#define GST_VIDEO_BOX(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_VIDEO_BOX,GstVideoBox))
#define GST_VIDEO_BOX_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_VIDEO_BOX,GstVideoBoxClass))
#define GST_IS_VIDEO_BOX(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_VIDEO_BOX))
#define GST_IS_VIDEO_BOX_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_VIDEO_BOX))

typedef struct _GstVideoBox GstVideoBox;
typedef struct _GstVideoBoxClass GstVideoBoxClass;

typedef enum
{
  VIDEO_BOX_FILL_BLACK,
  VIDEO_BOX_FILL_GREEN,
  VIDEO_BOX_FILL_BLUE,
  VIDEO_BOX_FILL_LAST
}
GstVideoBoxFill;

struct _GstVideoBox
{
  GstBaseTransform element;

  /* caps */
  guint32 in_fourcc;
  gint in_width, in_height;
  guint32 out_fourcc;
  gint out_width, out_height;

  gint box_left, box_right, box_top, box_bottom;

  gint border_left, border_right, border_top, border_bottom;
  gint crop_left, crop_right, crop_top, crop_bottom;

  gdouble alpha;
  gdouble border_alpha;

  GstVideoBoxFill fill_type;
};

struct _GstVideoBoxClass
{
  GstBaseTransformClass parent_class;
};

/* elementfactory information */
static const GstElementDetails gst_video_box_details =
GST_ELEMENT_DETAILS ("Video box filter",
    "Filter/Effect/Video",
    "Resizes a video by adding borders or cropping",
    "Wim Taymans <wim@fluendo.com>");


#define DEFAULT_LEFT      0
#define DEFAULT_RIGHT     0
#define DEFAULT_TOP       0
#define DEFAULT_BOTTOM    0
#define DEFAULT_FILL_TYPE VIDEO_BOX_FILL_BLACK
#define DEFAULT_ALPHA     1.0
#define DEFAULT_BORDER_ALPHA 1.0

enum
{
  PROP_0,
  PROP_LEFT,
  PROP_RIGHT,
  PROP_TOP,
  PROP_BOTTOM,
  PROP_FILL_TYPE,
  PROP_ALPHA,
  PROP_BORDER_ALPHA
      /* FILL ME */
};

static GstStaticPadTemplate gst_video_box_src_template =
    GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("AYUV") ";"
        GST_VIDEO_CAPS_YUV ("I420"))
    );

static GstStaticPadTemplate gst_video_box_sink_template =
    GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("AYUV") ";"
        GST_VIDEO_CAPS_YUV ("I420"))
    );


GST_BOILERPLATE (GstVideoBox, gst_video_box, GstBaseTransform,
    GST_TYPE_BASE_TRANSFORM);

static void gst_video_box_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_video_box_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean video_box_recalc_transform (GstVideoBox * video_box);
static GstCaps *gst_video_box_transform_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * from);
static gboolean gst_video_box_set_caps (GstBaseTransform * trans,
    GstCaps * in, GstCaps * out);
static gboolean gst_video_box_get_unit_size (GstBaseTransform * trans,
    GstCaps * caps, guint * size);
static GstFlowReturn gst_video_box_transform (GstBaseTransform * trans,
    GstBuffer * in, GstBuffer * out);


#define GST_TYPE_VIDEO_BOX_FILL (gst_video_box_fill_get_type())
static GType
gst_video_box_fill_get_type (void)
{
  static GType video_box_fill_type = 0;
  static const GEnumValue video_box_fill[] = {
    {VIDEO_BOX_FILL_BLACK, "Black", "black"},
    {VIDEO_BOX_FILL_GREEN, "Colorkey green", "green"},
    {VIDEO_BOX_FILL_BLUE, "Colorkey blue", "blue"},
    {0, NULL, NULL},
  };

  if (!video_box_fill_type) {
    video_box_fill_type =
        g_enum_register_static ("GstVideoBoxFill", video_box_fill);
  }
  return video_box_fill_type;
}


static void
gst_video_box_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details (element_class, &gst_video_box_details);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_video_box_sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_video_box_src_template));
}

static void
gst_video_box_class_init (GstVideoBoxClass * klass)
{
  GObjectClass *gobject_class;
  GstBaseTransformClass *trans_class;

  gobject_class = (GObjectClass *) klass;
  trans_class = (GstBaseTransformClass *) klass;

  gobject_class->set_property = gst_video_box_set_property;
  gobject_class->get_property = gst_video_box_get_property;

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_FILL_TYPE,
      g_param_spec_enum ("fill", "Fill", "How to fill the borders",
          GST_TYPE_VIDEO_BOX_FILL, DEFAULT_FILL_TYPE,
          (GParamFlags) G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_LEFT,
      g_param_spec_int ("left", "Left",
          "Pixels to box at left (<0  = add a border)", G_MININT, G_MAXINT,
          DEFAULT_LEFT, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_RIGHT,
      g_param_spec_int ("right", "Right",
          "Pixels to box at right (<0 = add a border)", G_MININT, G_MAXINT,
          DEFAULT_RIGHT, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_TOP,
      g_param_spec_int ("top", "Top",
          "Pixels to box at top (<0 = add a border)", G_MININT, G_MAXINT,
          DEFAULT_TOP, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_BOTTOM,
      g_param_spec_int ("bottom", "Bottom",
          "Pixels to box at bottom (<0 = add a border)", G_MININT, G_MAXINT,
          DEFAULT_BOTTOM, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_ALPHA,
      g_param_spec_double ("alpha", "Alpha", "Alpha value picture", 0.0, 1.0,
          DEFAULT_ALPHA, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_BORDER_ALPHA,
      g_param_spec_double ("border_alpha", "Border Alpha",
          "Alpha value of the border", 0.0, 1.0, DEFAULT_BORDER_ALPHA,
          G_PARAM_READWRITE));

  trans_class->transform = GST_DEBUG_FUNCPTR (gst_video_box_transform);
  trans_class->transform_caps =
      GST_DEBUG_FUNCPTR (gst_video_box_transform_caps);
  trans_class->set_caps = GST_DEBUG_FUNCPTR (gst_video_box_set_caps);
  trans_class->get_unit_size = GST_DEBUG_FUNCPTR (gst_video_box_get_unit_size);

  GST_DEBUG_CATEGORY_INIT (videobox_debug, "videobox", 0,
      "Resizes a video by adding borders or cropping");
}

static void
gst_video_box_init (GstVideoBox * video_box, GstVideoBoxClass * g_class)
{
  video_box->box_right = DEFAULT_RIGHT;
  video_box->box_left = DEFAULT_LEFT;
  video_box->box_top = DEFAULT_TOP;
  video_box->box_bottom = DEFAULT_BOTTOM;
  video_box->crop_right = 0;
  video_box->crop_left = 0;
  video_box->crop_top = 0;
  video_box->crop_bottom = 0;
  video_box->fill_type = DEFAULT_FILL_TYPE;
  video_box->alpha = DEFAULT_ALPHA;
  video_box->border_alpha = DEFAULT_BORDER_ALPHA;
}

static void
gst_video_box_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVideoBox *video_box = GST_VIDEO_BOX (object);

  GST_BASE_TRANSFORM_LOCK (GST_BASE_TRANSFORM_CAST (video_box));
  switch (prop_id) {
    case PROP_LEFT:
      video_box->box_left = g_value_get_int (value);
      if (video_box->box_left < 0) {
        video_box->border_left = -video_box->box_left;
        video_box->crop_left = 0;
      } else {
        video_box->border_left = 0;
        video_box->crop_left = video_box->box_left;
      }
      break;
    case PROP_RIGHT:
      video_box->box_right = g_value_get_int (value);
      if (video_box->box_right < 0) {
        video_box->border_right = -video_box->box_right;
        video_box->crop_right = 0;
      } else {
        video_box->border_right = 0;
        video_box->crop_right = video_box->box_right;
      }
      break;
    case PROP_TOP:
      video_box->box_top = g_value_get_int (value);
      if (video_box->box_top < 0) {
        video_box->border_top = -video_box->box_top;
        video_box->crop_top = 0;
      } else {
        video_box->border_top = 0;
        video_box->crop_top = video_box->box_top;
      }
      break;
    case PROP_BOTTOM:
      video_box->box_bottom = g_value_get_int (value);
      if (video_box->box_bottom < 0) {
        video_box->border_bottom = -video_box->box_bottom;
        video_box->crop_bottom = 0;
      } else {
        video_box->border_bottom = 0;
        video_box->crop_bottom = video_box->box_bottom;
      }
      break;
    case PROP_FILL_TYPE:
      video_box->fill_type = g_value_get_enum (value);
      break;
    case PROP_ALPHA:
      video_box->alpha = g_value_get_double (value);
      break;
    case PROP_BORDER_ALPHA:
      video_box->border_alpha = g_value_get_double (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  video_box_recalc_transform (video_box);
  gst_base_transform_reconfigure (GST_BASE_TRANSFORM (video_box));
  GST_BASE_TRANSFORM_UNLOCK (GST_BASE_TRANSFORM_CAST (video_box));
}

static void
gst_video_box_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstVideoBox *video_box = GST_VIDEO_BOX (object);

  switch (prop_id) {
    case PROP_LEFT:
      g_value_set_int (value, video_box->box_left);
      break;
    case PROP_RIGHT:
      g_value_set_int (value, video_box->box_right);
      break;
    case PROP_TOP:
      g_value_set_int (value, video_box->box_top);
      break;
    case PROP_BOTTOM:
      g_value_set_int (value, video_box->box_bottom);
      break;
    case PROP_FILL_TYPE:
      g_value_set_enum (value, video_box->fill_type);
      break;
    case PROP_ALPHA:
      g_value_set_double (value, video_box->alpha);
      break;
    case PROP_BORDER_ALPHA:
      g_value_set_double (value, video_box->border_alpha);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstCaps *
gst_video_box_transform_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * from)
{
  GstVideoBox *video_box;
  GstCaps *to, *ret;
  const GstCaps *templ;
  GstStructure *structure;
  GstPad *other;
  gint width, height;

  video_box = GST_VIDEO_BOX (trans);

  to = gst_caps_copy (from);
  structure = gst_caps_get_structure (to, 0);

  /* get rid of format */
  gst_structure_remove_field (structure, "format");

  /* calculate width and height */
  if (gst_structure_get_int (structure, "width", &width)) {
    if (direction == GST_PAD_SINK) {
      width -= video_box->box_left;
      width -= video_box->box_right;
    } else {
      width += video_box->box_left;
      width += video_box->box_right;
    }
    if (width <= 0)
      width = 1;

    GST_DEBUG ("New caps width: %d", width);
    gst_structure_set (structure, "width", G_TYPE_INT, width, NULL);
  }

  if (gst_structure_get_int (structure, "height", &height)) {
    if (direction == GST_PAD_SINK) {
      height -= video_box->box_top;
      height -= video_box->box_bottom;
    } else {
      height += video_box->box_top;
      height += video_box->box_bottom;
    }

    if (height <= 0)
      height = 1;

    GST_DEBUG ("New caps height: %d", height);
    gst_structure_set (structure, "height", G_TYPE_INT, height, NULL);
  }

  /* filter against set allowed caps on the pad */
  other = (direction == GST_PAD_SINK) ? trans->srcpad : trans->sinkpad;

  templ = gst_pad_get_pad_template_caps (other);
  ret = gst_caps_intersect (to, templ);
  gst_caps_unref (to);

  GST_DEBUG_OBJECT (video_box, "direction %d, transformed %" GST_PTR_FORMAT
      " to %" GST_PTR_FORMAT, direction, from, ret);

  return ret;
}

static gboolean
video_box_recalc_transform (GstVideoBox * video_box)
{
  gboolean res = TRUE;

  /* if we have the same format in and out and we don't need to perform and
   * cropping at all, we can just operate in passthorugh mode */
  if (video_box->in_fourcc == video_box->out_fourcc &&
      video_box->box_left == 0 && video_box->box_right == 0 &&
      video_box->box_top == 0 && video_box->box_bottom == 0) {

    GST_LOG_OBJECT (video_box, "we are using passthrough");
    gst_base_transform_set_passthrough (GST_BASE_TRANSFORM (video_box), TRUE);
  } else {
    GST_LOG_OBJECT (video_box, "we are not using passthrough");
    gst_base_transform_set_passthrough (GST_BASE_TRANSFORM (video_box), FALSE);
  }
  return res;
}

static gboolean
gst_video_box_set_caps (GstBaseTransform * trans, GstCaps * in, GstCaps * out)
{
  GstVideoBox *video_box;
  GstStructure *structure;
  gboolean ret;

  video_box = GST_VIDEO_BOX (trans);

  structure = gst_caps_get_structure (in, 0);
  ret = gst_structure_get_int (structure, "width", &video_box->in_width);
  ret &= gst_structure_get_int (structure, "height", &video_box->in_height);
  ret &= gst_structure_get_fourcc (structure, "format", &video_box->in_fourcc);

  structure = gst_caps_get_structure (out, 0);
  ret &= gst_structure_get_int (structure, "width", &video_box->out_width);
  ret &= gst_structure_get_int (structure, "height", &video_box->out_height);
  ret &= gst_structure_get_fourcc (structure, "format", &video_box->out_fourcc);

  /* something wrong getting the caps */
  if (!ret)
    goto no_caps;

  GST_DEBUG ("Input w: %d h: %d", video_box->in_width, video_box->in_height);
  GST_DEBUG ("Output w: %d h: %d", video_box->out_width, video_box->out_height);

  /* recalc the transformation strategy */
  ret = video_box_recalc_transform (video_box);

  return ret;

  /* ERRORS */
no_caps:
  {
    GST_DEBUG_OBJECT (video_box, "Could not get all caps fields");
    return FALSE;
  }
}

/* see gst-plugins/gst/games/gstvideoimage.c, paint_setup_I420() */
#define GST_VIDEO_I420_Y_ROWSTRIDE(width) (GST_ROUND_UP_4(width))
#define GST_VIDEO_I420_U_ROWSTRIDE(width) (GST_ROUND_UP_8(width)/2)
#define GST_VIDEO_I420_V_ROWSTRIDE(width) ((GST_ROUND_UP_8(GST_VIDEO_I420_Y_ROWSTRIDE(width)))/2)

#define GST_VIDEO_I420_Y_OFFSET(w,h) (0)
#define GST_VIDEO_I420_U_OFFSET(w,h) (GST_VIDEO_I420_Y_OFFSET(w,h)+(GST_VIDEO_I420_Y_ROWSTRIDE(w)*GST_ROUND_UP_2(h)))
#define GST_VIDEO_I420_V_OFFSET(w,h) (GST_VIDEO_I420_U_OFFSET(w,h)+(GST_VIDEO_I420_U_ROWSTRIDE(w)*GST_ROUND_UP_2(h)/2))

#define GST_VIDEO_I420_SIZE(w,h)     (GST_VIDEO_I420_V_OFFSET(w,h)+(GST_VIDEO_I420_V_ROWSTRIDE(w)*GST_ROUND_UP_2(h)/2))

static gboolean
gst_video_box_get_unit_size (GstBaseTransform * trans, GstCaps * caps,
    guint * size)
{

  GstVideoBox *video_box;
  GstStructure *structure = NULL;
  guint32 fourcc;
  gint width, height;

  g_assert (size);

  video_box = GST_VIDEO_BOX (trans);

  structure = gst_caps_get_structure (caps, 0);
  gst_structure_get_fourcc (structure, "format", &fourcc);
  gst_structure_get_int (structure, "width", &width);
  gst_structure_get_int (structure, "height", &height);

  switch (fourcc) {
    case GST_MAKE_FOURCC ('A', 'Y', 'U', 'V'):
      *size = width * height * 4;
      break;
    case GST_MAKE_FOURCC ('I', '4', '2', '0'):
      *size = GST_VIDEO_I420_SIZE (width, height);
      break;
    default:
      return FALSE;
      break;
  }

  GST_LOG_OBJECT (video_box, "Returning from _unit_size %d", *size);

  return TRUE;
}

static const guint8 yuv_colors_Y[VIDEO_BOX_FILL_LAST] = { 16, 150, 29 };
static const guint8 yuv_colors_U[VIDEO_BOX_FILL_LAST] = { 128, 46, 255 };
static const guint8 yuv_colors_V[VIDEO_BOX_FILL_LAST] = { 128, 21, 107 };

static void
gst_video_box_copy_plane_i420 (GstVideoBox * video_box, guint8 * src,
    guint8 * dest, gint br, gint bl, gint bt, gint bb, gint src_crop_width,
    gint src_crop_height, gint src_stride, gint dest_width, gint dest_stride,
    guint8 fill_color)
{
  gint j;

  /* top border */
  for (j = 0; j < bt; j++) {
    oil_splat_u8_ns (dest, &fill_color, dest_width);
    dest += dest_stride;
  }

  /* copy and add left and right border */
  for (j = 0; j < src_crop_height; j++) {
    oil_splat_u8_ns (dest, &fill_color, bl);
    oil_memcpy (dest + bl, src, src_crop_width);
    oil_splat_u8_ns (dest + bl + src_crop_width, &fill_color, br);
    dest += dest_stride;
    src += src_stride;
  }

  /* bottom border */
  for (j = 0; j < bb; j++) {
    oil_splat_u8_ns (dest, &fill_color, dest_width);
    dest += dest_stride;
  }
}

static void
gst_video_box_apply_alpha (guint8 * dest, guint8 alpha)
{
  if (dest[0] != 0)
    dest[0] = alpha;
}

static void
gst_video_box_ayuv_ayuv (GstVideoBox * video_box, guint8 * src, guint8 * dest)
{
  gint dblen = video_box->out_height * video_box->out_width;
  guint32 *destb = (guint32 *) dest;
  guint32 *srcb = (guint32 *) src;
  guint8 b_alpha = (guint8) (video_box->border_alpha * 255);
  guint8 i_alpha = (guint8) (video_box->alpha * 255);
  gint br, bl, bt, bb, crop_w, crop_h;
  gint i;
  guint32 *loc = destb;
  guint32 empty_pixel;

  GST_LOG ("Processing AYUV -> AYUV data");

  crop_h = 0;
  crop_w = 0;
  empty_pixel = GUINT32_FROM_BE ((b_alpha << 24) |
      (yuv_colors_Y[video_box->fill_type] << 16) |
      (yuv_colors_U[video_box->fill_type] << 8) |
      yuv_colors_V[video_box->fill_type]);

  br = video_box->box_right;
  bl = video_box->box_left;
  bt = video_box->box_top;
  bb = video_box->box_bottom;

  if (br >= 0 && bl >= 0) {
    crop_w = video_box->in_width - (br + bl);
  } else if (br >= 0 && bl < 0) {
    crop_w = video_box->in_width - (br);
  } else if (br < 0 && bl >= 0) {
    crop_w = video_box->in_width - (bl);
  } else if (br < 0 && bl < 0) {
    crop_w = video_box->in_width;
  }

  if (bb >= 0 && bt >= 0) {
    crop_h = video_box->in_height - (bb + bt);
  } else if (bb >= 0 && bt < 0) {
    crop_h = video_box->in_height - (bb);
  } else if (bb < 0 && bt >= 0) {
    crop_h = video_box->in_height - (bt);
  } else if (bb < 0 && bt < 0) {
    crop_h = video_box->in_height;
  }

  GST_DEBUG ("Borders are: L:%d, R:%d, T:%d, B:%d", bl, br, bt, bb);
  GST_DEBUG ("Alpha value is: %d", i_alpha);

  if (crop_h <= 0 || crop_w <= 0) {

    oil_splat_u32_ns (destb, &empty_pixel, dblen);

  } else {

    guint32 *src_loc = srcb;

    /* Top border */
    if (bt < 0) {
      oil_splat_u32_ns (loc, &empty_pixel, (-bt) * video_box->out_width);
      loc = loc + ((-bt) * video_box->out_width);
    } else {
      src_loc = src_loc + (bt * video_box->in_width);
    }

    if (bl >= 0)
      src_loc += bl;

    for (i = 0; i < crop_h; i++) {
      gint j;

      /* Left border */
      if (bl < 0) {
        oil_splat_u32_ns (loc, &empty_pixel, -bl);
        loc += (-bl);
      }

      /* Cropped area */
      oil_copy_u8 ((guint8 *) loc, (guint8 *) src_loc, crop_w * 4);

      for (j = 0; j < crop_w; j++)
        gst_video_box_apply_alpha ((guint8 *) & loc[j], i_alpha);

      src_loc += video_box->in_width;
      loc += crop_w;

      /* Right border */
      if (br < 0) {
        oil_splat_u32_ns (loc, &empty_pixel, -br);
        loc += (-br);
      }
    }

    /* Bottom border */
    if (bb < 0) {
      oil_splat_u32_ns (loc, &empty_pixel, (-bb) * video_box->out_width);
    }
  }

  GST_LOG ("image created");

}

static gpointer
gst_video_box_clear (gpointer dest, gint size)
{
  guint8 nil = 255;

  oil_splat_u8_ns (dest, &nil, size);

  return dest;
}

static gint
UVfloor (gint j)
{
  return floor (((float) j) / 2);
}

static gint
UVceil (gint j)
{
  return ceil (((float) j) / 2);
}

static void
gst_video_box_ayuv_i420 (GstVideoBox * video_box, guint8 * src, guint8 * dest)
{
  gint br, bl, bt, bb, crop_w, crop_h, rest;
  gint Ysize, Usize, Vsize;
  guint8 *Ydest, *Udest, *Vdest;
  guint8 *Utemp, *Vtemp;
  guint32 empty_px_values[3];
  gint i, j;
  guint Ywidth, Uwidth, Vwidth;

  GST_LOG ("AYUV to I420 conversion");

  crop_h = 0;
  crop_w = 0;
  rest = 0;

  empty_px_values[0] = yuv_colors_Y[video_box->fill_type];
  empty_px_values[1] = yuv_colors_U[video_box->fill_type];
  empty_px_values[2] = yuv_colors_V[video_box->fill_type];

  Ywidth = GST_VIDEO_I420_Y_ROWSTRIDE (video_box->out_width);
  Uwidth = GST_VIDEO_I420_U_ROWSTRIDE (video_box->out_width);
  Vwidth = GST_VIDEO_I420_V_ROWSTRIDE (video_box->out_width);

  Ydest = dest + GST_VIDEO_I420_Y_OFFSET (video_box->out_width,
      video_box->out_height);
  Udest = Ydest + GST_VIDEO_I420_U_OFFSET (video_box->out_width,
      video_box->out_height);
  Vdest = Ydest + GST_VIDEO_I420_V_OFFSET (video_box->out_width,
      video_box->out_height);

  Ysize = Ywidth * video_box->out_height;
  Usize = Uwidth * UVceil (video_box->out_height);
  Vsize = Vwidth * UVceil (video_box->out_height);

  br = video_box->box_right;
  bl = video_box->box_left;
  bt = video_box->box_top;
  bb = video_box->box_bottom;

  if (br >= 0 && bl >= 0) {
    rest = Ywidth - video_box->out_width;
    crop_w = video_box->in_width - (bl + br);
  } else if (br >= 0 && bl < 0) {
    rest = Ywidth - video_box->out_width;
    crop_w = video_box->in_width - (br);
  } else if (br < 0 && bl >= 0) {
    rest = Ywidth - video_box->out_width;
    crop_w = video_box->in_width - (bl);
  } else if (br < 0 && bl < 0) {
    rest = Ywidth - video_box->out_width;
    crop_w = video_box->in_width;
  }

  if (bb >= 0 && bt >= 0) {
    crop_h = video_box->in_height - (bb + bt);
  } else if (bb >= 0 && bt < 0) {
    crop_h = video_box->in_height - (bb);
  } else if (bb < 0 && bt >= 0) {
    crop_h = video_box->in_height - (bt);
  } else if (bb < 0 && bt < 0) {
    crop_h = video_box->in_height;
  }

  Utemp = g_malloc0 (Uwidth);
  Vtemp = g_malloc0 (Vwidth);

  GST_LOG ("Borders are: L:%d, R:%d, T:%d, B:%d", bl, br, bt, bb);

  GST_LOG ("Starting conversion");

  if (crop_h <= 0 || crop_w <= 0) {

    oil_splat_u8_ns (Ydest, (guint8 *) & empty_px_values[0], Ysize);
    oil_splat_u8_ns (Udest, (guint8 *) & empty_px_values[1], Usize);
    oil_splat_u8_ns (Vdest, (guint8 *) & empty_px_values[2], Vsize);

  } else {

    gboolean sumbuff = FALSE;
    guint32 *src_loc1;
    gint a = 0;

    src_loc1 = (guint32 *) src;

    if (bt < 0) {
      oil_splat_u8_ns (Ydest, (guint8 *) & empty_px_values[0], (-bt) * Ywidth);

      oil_splat_u8_ns (Udest, (guint8 *) & empty_px_values[1],
          (UVfloor (-bt) * Uwidth) + 7);
      oil_splat_u8_ns (Vdest, (guint8 *) & empty_px_values[2],
          UVfloor (-bt) * Vwidth);

      if ((-bt) % 2 > 0) {
        oil_splat_u8_ns (Utemp, (guint8 *) & empty_px_values[1], Uwidth);
        oil_splat_u8_ns (Vtemp, (guint8 *) & empty_px_values[2], Vwidth);
        sumbuff = TRUE;
      }

      Ydest += ((-bt) * Ywidth);
      Udest += (UVfloor (-bt) * Uwidth);
      Vdest += (UVfloor (-bt) * Vwidth);

    } else {
      src_loc1 = src_loc1 + (bt * video_box->in_width);
    }

    if (bl >= 0)
      src_loc1 += bl;

    GST_LOG ("Cropped area");
    GST_LOG ("Ydest value: %p Ywidth: %u", Ydest, Ywidth);
    GST_LOG ("Udest value: %p Uwidth: %u", Udest, Uwidth);
    GST_LOG ("Vdest value: %p Vwidth: %u", Vdest, Vwidth);
    GST_LOG ("Rest: %d", rest);
    for (i = 0; i < crop_h; i++) {

      a = 0;
      if (sumbuff) {
        /* left border */
        if (bl < 0) {
          oil_splat_u8_ns (Ydest, (guint8 *) & empty_px_values[0], -bl);

          for (j = 0; j < -bl; j++) {
            Utemp[UVfloor (j)] = (Utemp[UVfloor (j)] + empty_px_values[1]) / 2;
            Vtemp[UVfloor (j)] = (Vtemp[UVfloor (j)] + empty_px_values[2]) / 2;
          }
          Ydest += -bl;
          a = -bl;
        }

        for (j = 0; j < crop_w; j++) {
          /* check ARCH */
          Ydest[j] = ((guint8 *) & src_loc1[j])[1];
          Utemp[UVfloor (a + j)] =
              (Utemp[UVfloor (a + j)] + ((guint8 *) & src_loc1[j])[2]) / 2;
          Vtemp[UVfloor (a + j)] =
              (Vtemp[UVfloor (a + j)] + ((guint8 *) & src_loc1[j])[3]) / 2;
        }
        Ydest += crop_w;

        /* right border */
        if (br < 0) {
          oil_splat_u8_ns (Ydest, (guint8 *) & empty_px_values[0], -br);
          for (j = 0; j < -br; j++) {
            Utemp[UVfloor (a + crop_w + j)] =
                (Utemp[UVfloor (a + crop_w + j)] + empty_px_values[1]) / 2;
            Vtemp[UVfloor (a + crop_w + j)] =
                (Vtemp[UVfloor (a + crop_w + j)] + empty_px_values[2]) / 2;
          }
          Ydest += -br;
        }
        oil_copy_u8 (Udest, Utemp, Uwidth);
        oil_copy_u8 (Vdest, Vtemp, Vwidth);
        Udest += Uwidth;
        Vdest += Vwidth;
        Ydest += rest;
        gst_video_box_clear (Utemp, Uwidth);
        gst_video_box_clear (Vtemp, Vwidth);
        src_loc1 += video_box->in_width;
        sumbuff = FALSE;

      } else {

        /* left border */
        a = 0;
        if (bl < 0) {
          oil_splat_u8_ns (Ydest, (guint8 *) & empty_px_values[0], -bl);
          oil_splat_u8_ns (Vtemp, (guint8 *) & empty_px_values[1],
              UVceil (-bl));
          oil_splat_u8_ns (Utemp, (guint8 *) & empty_px_values[2],
              UVceil (-bl));
          Ydest += -bl;
          a = -bl;
        }

        for (j = 0; j < crop_w; j++) {

          /* check ARCH */
          Ydest[j] = ((guint8 *) & src_loc1[j])[1];

          if ((a + j) % 2 > 0) {
            Utemp[UVfloor (a + j)] =
                (Utemp[UVfloor (a + j)] + ((guint8 *) & src_loc1[j])[2]) / 2;
            Vtemp[UVfloor (a + j)] =
                (Vtemp[UVfloor (a + j)] + ((guint8 *) & src_loc1[j])[3]) / 2;
          } else {
            Utemp[UVfloor (a + j)] = ((guint8 *) & src_loc1[j])[2] / 2;
            Vtemp[UVfloor (a + j)] = ((guint8 *) & src_loc1[j])[3] / 2;
          }
        }
        Ydest += crop_w;

        /* right border */
        if (br < 0) {
          j = 0;
          if ((a + crop_w) % 2 > 0) {
            Utemp[UVfloor (a + crop_w)] =
                (Utemp[UVfloor (a + crop_w)] + empty_px_values[1]) / 2;
            Vtemp[UVfloor (a + crop_w)] =
                (Vtemp[UVfloor (a + crop_w)] + empty_px_values[2]) / 2;
            a++;
            j = -1;
          }

          oil_splat_u8_ns (Ydest, (guint8 *) & empty_px_values[0], -br);
          oil_splat_u8_ns (&Utemp[UVfloor (a + crop_w)],
              (guint8 *) & empty_px_values[1], UVceil ((-br) + j));
          oil_splat_u8_ns (&Vtemp[UVfloor (a + crop_w)],
              (guint8 *) & empty_px_values[2], UVceil ((-br) + j));
          Ydest += -br;
        }
        Ydest += rest;
        src_loc1 += video_box->in_width;
        sumbuff = TRUE;

      }
    }

    /* bottom border */
    if (bb < 0) {
      a = 0;
      oil_splat_u8_ns (Ydest, (guint8 *) & empty_px_values[0], (-bb) * Ywidth);
      if (sumbuff) {
        for (i = 0; i < Uwidth; i++) {
          Utemp[i] = (Utemp[i] + empty_px_values[1]) / 2;
        }
        for (i = 0; i < Vwidth; i++) {
          Vtemp[i] = (Vtemp[i] + empty_px_values[2]) / 2;
        }

        oil_copy_u8 (Udest, Utemp, Uwidth);
        oil_copy_u8 (Vdest, Vtemp, Vwidth);
        Udest += Uwidth;
        Vdest += Vwidth;
        sumbuff = FALSE;
        a = -1;
      }
      oil_splat_u8_ns (Udest, (guint8 *) & empty_px_values[1],
          (UVfloor ((-bb))) * Uwidth);
      oil_splat_u8_ns (Vdest, (guint8 *) & empty_px_values[2],
          (UVfloor ((-bb))) * Vwidth);
    }
    if (sumbuff) {
      oil_copy_u8 (Udest, Utemp, Uwidth);
      oil_copy_u8 (Vdest, Vtemp, Vwidth);
    }
  }

  GST_LOG ("image created");
  g_free (Utemp);
  g_free (Vtemp);
}

static void
gst_video_box_i420_ayuv (GstVideoBox * video_box, guint8 * src, guint8 * dest)
{
  guint8 *srcY, *srcU, *srcV;
  gint crop_width, crop_width2, crop_height;
  gint out_width, out_height;
  gint src_stridey, src_strideu, src_stridev;
  gint br, bl, bt, bb;
  gint colorY, colorU, colorV;
  gint i, j;
  guint8 b_alpha = (guint8) (video_box->border_alpha * 255);
  guint8 i_alpha = (guint8) (video_box->alpha * 255);
  guint32 *destp;
  guint32 *destb = (guint32 *) dest;
  guint32 ayuv;

  br = video_box->border_right;
  bl = video_box->border_left;
  bt = video_box->border_top;
  bb = video_box->border_bottom;

  out_width = video_box->out_width;
  out_height = video_box->out_height;

  src_stridey = GST_VIDEO_I420_Y_ROWSTRIDE (video_box->in_width);
  src_strideu = GST_VIDEO_I420_U_ROWSTRIDE (video_box->in_width);
  src_stridev = GST_VIDEO_I420_V_ROWSTRIDE (video_box->in_width);

  crop_width = video_box->in_width;
  crop_width -= (video_box->crop_left + video_box->crop_right);
  crop_width2 = crop_width / 2;
  crop_height = video_box->in_height;
  crop_height -= (video_box->crop_top + video_box->crop_bottom);

  srcY =
      src + GST_VIDEO_I420_Y_OFFSET (video_box->in_width, video_box->in_height);
  srcY += src_stridey * video_box->crop_top + video_box->crop_left;
  srcU =
      src + GST_VIDEO_I420_U_OFFSET (video_box->in_width, video_box->in_height);
  srcU += src_strideu * (video_box->crop_top / 2) + (video_box->crop_left / 2);
  srcV =
      src + GST_VIDEO_I420_V_OFFSET (video_box->in_width, video_box->in_height);
  srcV += src_stridev * (video_box->crop_top / 2) + (video_box->crop_left / 2);

  colorY = yuv_colors_Y[video_box->fill_type];
  colorU = yuv_colors_U[video_box->fill_type];
  colorV = yuv_colors_V[video_box->fill_type];

  ayuv =
      GUINT32_FROM_BE ((b_alpha << 24) | (colorY << 16) | (colorU << 8) |
      colorV);

  /* top border */
  if (bt) {
    size_t nb_pixels = bt * out_width;

    oil_splat_u32_ns (destb, &ayuv, nb_pixels);
    destb += nb_pixels;
  }
  for (i = 0; i < crop_height; i++) {
    destp = destb;
    /* left border */
    if (bl) {
      oil_splat_u32_ns (destp, &ayuv, bl);
      destp += bl;
    }
    dest = (guint8 *) destp;
    /* center */
    /* We can splat the alpha channel for the whole line */
    oil_splat_u8 (dest, 4, &i_alpha, crop_width);
    for (j = 0; j < crop_width2; j++) {
      dest++;
      *dest++ = *srcY++;
      *dest++ = *srcU;
      *dest++ = *srcV;
      dest++;
      *dest++ = *srcY++;
      *dest++ = *srcU++;
      *dest++ = *srcV++;
    }
    if (i % 2 == 0) {
      srcU -= crop_width2;
      srcV -= crop_width2;
    } else {
      srcU += src_strideu - crop_width2;
      srcV += src_stridev - crop_width2;
    }
    srcY += src_stridey - (crop_width2 * 2);

    destp = (guint32 *) dest;
    /* right border */
    if (br) {
      oil_splat_u32_ns (destp, &ayuv, br);
      destp += br;
    }
    destb += out_width;
  }
  /* bottom border */
  if (bb) {
    size_t nb_pixels = bb * out_width;

    oil_splat_u32_ns (destb, &ayuv, nb_pixels);
    destb += nb_pixels;
  }
}


static void
gst_video_box_i420_i420 (GstVideoBox * video_box, guint8 * src, guint8 * dest)
{
  guint8 *srcY, *srcU, *srcV;
  guint8 *destY, *destU, *destV;
  gint crop_width, crop_height;
  gint out_width, out_height;
  gint src_width, src_height;
  gint src_stride, dest_stride;
  gint br, bl, bt, bb;

  br = video_box->border_right;
  bl = video_box->border_left;
  bt = video_box->border_top;
  bb = video_box->border_bottom;

  out_width = video_box->out_width;
  out_height = video_box->out_height;

  src_width = video_box->in_width;
  src_height = video_box->in_height;

  crop_width = src_width - (video_box->crop_left + video_box->crop_right);
  crop_height = src_height - (video_box->crop_top + video_box->crop_bottom);

  /* Y plane */
  src_stride = GST_VIDEO_I420_Y_ROWSTRIDE (src_width);
  dest_stride = GST_VIDEO_I420_Y_ROWSTRIDE (out_width);

  destY = dest + GST_VIDEO_I420_Y_OFFSET (out_width, out_height);

  srcY = src + GST_VIDEO_I420_Y_OFFSET (src_width, src_height);
  srcY += src_stride * video_box->crop_top + video_box->crop_left;

  gst_video_box_copy_plane_i420 (video_box, srcY, destY, br, bl, bt, bb,
      crop_width, crop_height, src_stride, out_width, dest_stride,
      yuv_colors_Y[video_box->fill_type]);

  /* U plane */
  src_stride = GST_VIDEO_I420_U_ROWSTRIDE (src_width);
  dest_stride = GST_VIDEO_I420_U_ROWSTRIDE (out_width);

  destU = dest + GST_VIDEO_I420_U_OFFSET (out_width, out_height);

  srcU = src + GST_VIDEO_I420_U_OFFSET (src_width, src_height);
  srcU += src_stride * (video_box->crop_top / 2) + (video_box->crop_left / 2);

  gst_video_box_copy_plane_i420 (video_box, srcU, destU, br / 2, bl / 2, bt / 2,
      bb / 2, crop_width / 2, crop_height / 2, src_stride, out_width / 2,
      dest_stride, yuv_colors_U[video_box->fill_type]);

  /* V plane */
  src_stride = GST_VIDEO_I420_V_ROWSTRIDE (src_width);
  dest_stride = GST_VIDEO_I420_V_ROWSTRIDE (out_width);

  destV = dest + GST_VIDEO_I420_V_OFFSET (out_width, out_height);

  srcV = src + GST_VIDEO_I420_V_OFFSET (src_width, src_height);
  srcV += src_stride * (video_box->crop_top / 2) + (video_box->crop_left / 2);

  gst_video_box_copy_plane_i420 (video_box, srcV, destV, br / 2, bl / 2, bt / 2,
      bb / 2, crop_width / 2, crop_height / 2, src_stride, out_width / 2,
      dest_stride, yuv_colors_V[video_box->fill_type]);
}

static GstFlowReturn
gst_video_box_transform (GstBaseTransform * trans, GstBuffer * in,
    GstBuffer * out)
{
  GstVideoBox *video_box;
  guint8 *indata, *outdata;

  video_box = GST_VIDEO_BOX (trans);

  indata = GST_BUFFER_DATA (in);
  outdata = GST_BUFFER_DATA (out);

  switch (video_box->in_fourcc) {
    case GST_MAKE_FOURCC ('A', 'Y', 'U', 'V'):
      switch (video_box->out_fourcc) {
        case GST_MAKE_FOURCC ('A', 'Y', 'U', 'V'):
          gst_video_box_ayuv_ayuv (video_box, indata, outdata);
          break;
        case GST_MAKE_FOURCC ('I', '4', '2', '0'):
          gst_video_box_ayuv_i420 (video_box, indata, outdata);
          break;
        default:
          goto invalid_format;
      }
      break;
    case GST_MAKE_FOURCC ('I', '4', '2', '0'):
      switch (video_box->out_fourcc) {
        case GST_MAKE_FOURCC ('A', 'Y', 'U', 'V'):
          gst_video_box_i420_ayuv (video_box, indata, outdata);
          break;
        case GST_MAKE_FOURCC ('I', '4', '2', '0'):
          gst_video_box_i420_i420 (video_box, indata, outdata);
          break;
        default:
          goto invalid_format;
      }
      break;
    default:
      goto invalid_format;
  }
  return GST_FLOW_OK;

  /* ERRORS */
invalid_format:
  {
    return GST_FLOW_ERROR;
  }
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  oil_init ();

  return gst_element_register (plugin, "videobox", GST_RANK_NONE,
      GST_TYPE_VIDEO_BOX);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "videobox",
    "resizes a video by adding borders or cropping",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
