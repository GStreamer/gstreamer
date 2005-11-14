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

#include <liboil/liboil.h>
#include <string.h>

GST_DEBUG_CATEGORY (videobox_debug);
#define GST_CAT_DEFAULT videobox_debug

#define GST_TYPE_VIDEO_BOX \
  (gst_video_box_get_type())
#define GST_VIDEO_BOX(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_VIDEO_BOX,GstVideoBox))
#define GST_VIDEO_BOX_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_VIDEO_BOX,GstVideoBoxClass))
#define GST_IS_VIDEO_BOX(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_VIDEO_BOX))
#define GST_IS_VIDEO_BOX_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_VIDEO_BOX))

typedef struct _GstVideoBox GstVideoBox;
typedef struct _GstVideoBoxClass GstVideoBoxClass;

typedef enum
{
  VIDEO_BOX_FILL_BLACK,
  VIDEO_BOX_FILL_GREEN,
  VIDEO_BOX_FILL_BLUE,
}
GstVideoBoxFill;

struct _GstVideoBox
{
  GstBaseTransform element;

  /* caps */
  gint in_width, in_height;
  gint out_width, out_height;

  gint box_left, box_right, box_top, box_bottom;

  gint border_left, border_right, border_top, border_bottom;
  gint crop_left, crop_right, crop_top, crop_bottom;

  gboolean use_alpha;
  gdouble alpha;
  gdouble border_alpha;

  GstVideoBoxFill fill_type;
};

struct _GstVideoBoxClass
{
  GstBaseTransformClass parent_class;
};

/* elementfactory information */
static GstElementDetails gst_video_box_details =
GST_ELEMENT_DETAILS ("video box filter",
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
  PROP_BORDER_ALPHA,
  /* FILL ME */
};

static GstStaticPadTemplate gst_video_box_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("{ AYUV, I420 }"))
    );

static GstStaticPadTemplate gst_video_box_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("I420"))
    );


GST_BOILERPLATE (GstVideoBox, gst_video_box, GstBaseTransform,
    GST_TYPE_BASE_TRANSFORM);

static void gst_video_box_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_video_box_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

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
  static GEnumValue video_box_fill[] = {
    {VIDEO_BOX_FILL_BLACK, "0", "Black"},
    {VIDEO_BOX_FILL_GREEN, "1", "Colorkey green"},
    {VIDEO_BOX_FILL_BLUE, "2", "Colorkey blue"},
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

  trans_class->transform_caps = gst_video_box_transform_caps;
  trans_class->set_caps = gst_video_box_set_caps;
  trans_class->get_unit_size = gst_video_box_get_unit_size;
  trans_class->transform = gst_video_box_transform;

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
  GstCaps *to;
  GstStructure *structure;
  GValue list_value = { 0 }, value = {
  0};
  gint dir, i, tmp;

  video_box = GST_VIDEO_BOX (trans);

  g_value_init (&list_value, GST_TYPE_LIST);
  g_value_init (&value, GST_TYPE_FOURCC);
  gst_value_set_fourcc (&value, GST_MAKE_FOURCC ('I', '4', '2', '0'));
  gst_value_list_append_value (&list_value, &value);
  g_value_unset (&value);

  to = gst_caps_copy (from);
  dir = (direction == GST_PAD_SINK) ? -1 : 1;

  for (i = 0; i < gst_caps_get_size (to); i++) {
    structure = gst_caps_get_structure (to, i);
    if (direction == GST_PAD_SINK) {    /* I420 to { I420, AYUV } */
      g_value_init (&value, GST_TYPE_FOURCC);
      gst_value_set_fourcc (&value, GST_MAKE_FOURCC ('A', 'Y', 'U', 'V'));
      gst_value_list_append_value (&list_value, &value);
      g_value_unset (&value);
      gst_structure_set_value (structure, "format", &list_value);
    } else if (direction == GST_PAD_SRC) {
      gst_structure_set_value (structure, "format", &list_value);
    }
    if (gst_structure_get_int (structure, "width", &tmp))
      gst_structure_set (structure, "width", G_TYPE_INT,
          tmp + dir * (video_box->box_left + video_box->box_right), NULL);
    if (gst_structure_get_int (structure, "height", &tmp))
      gst_structure_set (structure, "height", G_TYPE_INT,
          tmp + dir * (video_box->box_top + video_box->box_bottom), NULL);
  }

  g_value_unset (&list_value);

  GST_DEBUG_OBJECT (video_box, "direction %d, transformed %" GST_PTR_FORMAT
      " to %" GST_PTR_FORMAT, direction, from, to);

  return to;
}

static gboolean
gst_video_box_set_caps (GstBaseTransform * trans, GstCaps * in, GstCaps * out)
{
  GstVideoBox *video_box;
  GstStructure *structure;
  gboolean ret;
  guint32 fourcc = 0;

  video_box = GST_VIDEO_BOX (trans);

  structure = gst_caps_get_structure (in, 0);
  ret = gst_structure_get_int (structure, "width", &video_box->in_width);
  ret &= gst_structure_get_int (structure, "height", &video_box->in_height);

  structure = gst_caps_get_structure (out, 0);
  ret &= gst_structure_get_int (structure, "width", &video_box->out_width);
  ret &= gst_structure_get_int (structure, "height", &video_box->out_height);
  ret &= gst_structure_get_fourcc (structure, "format", &fourcc);

  if (fourcc == GST_STR_FOURCC ("AYUV")) {
    video_box->use_alpha = TRUE;
  } else {
    if (video_box->box_left == 0 && video_box->box_right == 0 &&
        video_box->box_top == 0 && video_box->box_bottom == 0) {
      gst_base_transform_set_passthrough (GST_BASE_TRANSFORM (video_box), TRUE);
      GST_LOG ("we are using passthrough");
    } else {
      gst_base_transform_set_passthrough (GST_BASE_TRANSFORM (video_box),
          FALSE);
      GST_LOG ("we are not using passthrough");
    }
  }

  return ret;
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

  g_return_val_if_fail (size, FALSE);
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

  return TRUE;
}

static int yuv_colors_Y[] = { 16, 150, 29 };
static int yuv_colors_U[] = { 128, 46, 255 };
static int yuv_colors_V[] = { 128, 21, 107 };

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
gst_video_box_i420 (GstVideoBox * video_box, guint8 * src, guint8 * dest)
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

/* Note the source image is always I420, we
 * are converting to AYUV on the fly here */
static void
gst_video_box_ayuv (GstVideoBox * video_box, guint8 * src, guint8 * dest)
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
  guint32 *destp = (guint32 *) dest;
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

  crop_width =
      video_box->in_width - (video_box->crop_left + video_box->crop_right);
  crop_width2 = crop_width / 2;
  crop_height =
      video_box->in_height - (video_box->crop_top + video_box->crop_bottom);

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

    oil_splat_u32_ns (destp, &ayuv, nb_pixels);
    destp += nb_pixels;
  }
  for (i = 0; i < crop_height; i++) {
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
    srcY += src_stridey - crop_width;

    destp = (guint32 *) dest;
    /* right border */
    if (br) {
      oil_splat_u32_ns (destp, &ayuv, br);
      destp += br;
    }
  }
  /* bottom border */
  if (bb) {
    size_t nb_pixels = bb * out_width;

    oil_splat_u32_ns (destp, &ayuv, nb_pixels);
    destp += nb_pixels;
  }
}

static GstFlowReturn
gst_video_box_transform (GstBaseTransform * trans, GstBuffer * in,
    GstBuffer * out)
{
  GstVideoBox *video_box;

  video_box = GST_VIDEO_BOX (trans);

  if (video_box->use_alpha) {
    gst_video_box_ayuv (video_box, GST_BUFFER_DATA (in), GST_BUFFER_DATA (out));
  } else {
    gst_video_box_i420 (video_box, GST_BUFFER_DATA (in), GST_BUFFER_DATA (out));
  }

  return GST_FLOW_OK;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "videobox", GST_RANK_NONE,
      GST_TYPE_VIDEO_BOX);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "videobox",
    "resizes a video by adding borders or cropping",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
