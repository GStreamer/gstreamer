/* GStreamer
 * Copyright (C) 2008 David Schleef <ds@entropywave.com>
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
#include <string.h>
#include <cog/cog.h>
#include <cog/cogvirtframe.h>
#include <gst/math-compat.h>

#include "gstcogutils.h"

#include "gstcms.h"

#define GST_TYPE_COLORCONVERT \
  (gst_colorconvert_get_type())
#define GST_COLORCONVERT(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_COLORCONVERT,GstColorconvert))
#define GST_COLORCONVERT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_COLORCONVERT,GstColorconvertClass))
#define GST_IS_COLORCONVERT(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_COLORCONVERT))
#define GST_IS_COLORCONVERT_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_COLORCONVERT))

typedef struct _GstColorconvert GstColorconvert;
typedef struct _GstColorconvertClass GstColorconvertClass;

struct _GstColorconvert
{
  GstBaseTransform base_transform;

  gchar *location;

  GstVideoFormat format;
  int width;
  int height;

};

struct _GstColorconvertClass
{
  GstBaseTransformClass parent_class;

};

enum
{
  ARG_0,
};

GType gst_colorconvert_get_type (void);

static void gst_colorconvert_base_init (gpointer g_class);
static void gst_colorconvert_class_init (gpointer g_class, gpointer class_data);
static void gst_colorconvert_init (GTypeInstance * instance, gpointer g_class);

static void gst_colorconvert_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_colorconvert_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_colorconvert_set_caps (GstBaseTransform * base_transform,
    GstCaps * incaps, GstCaps * outcaps);
static GstFlowReturn gst_colorconvert_transform_ip (GstBaseTransform *
    base_transform, GstBuffer * buf);
static CogFrame *cog_virt_frame_new_color_transform (CogFrame * frame);

static GstStaticPadTemplate gst_colorconvert_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("{I420,YUY2,UYVY,AYUV}"))
    );

static GstStaticPadTemplate gst_colorconvert_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("{I420,YUY2,UYVY,AYUV}"))
    );

GType
gst_colorconvert_get_type (void)
{
  static GType compress_type = 0;

  if (!compress_type) {
    static const GTypeInfo compress_info = {
      sizeof (GstColorconvertClass),
      gst_colorconvert_base_init,
      NULL,
      gst_colorconvert_class_init,
      NULL,
      NULL,
      sizeof (GstColorconvert),
      0,
      gst_colorconvert_init,
    };

    compress_type = g_type_register_static (GST_TYPE_BASE_TRANSFORM,
        "GstColorconvert", &compress_info, 0);
  }
  return compress_type;
}


static void
gst_colorconvert_base_init (gpointer g_class)
{

  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_static_pad_template (element_class,
      &gst_colorconvert_src_template);
  gst_element_class_add_static_pad_template (element_class,
      &gst_colorconvert_sink_template);

  gst_element_class_set_details_simple (element_class, "Convert colorspace",
      "Filter/Effect/Video",
      "Convert between SDTV and HDTV colorspace",
      "David Schleef <ds@schleef.org>");
}

static void
gst_colorconvert_class_init (gpointer g_class, gpointer class_data)
{
  GObjectClass *gobject_class;
  GstBaseTransformClass *base_transform_class;

  gobject_class = G_OBJECT_CLASS (g_class);
  base_transform_class = GST_BASE_TRANSFORM_CLASS (g_class);

  gobject_class->set_property = gst_colorconvert_set_property;
  gobject_class->get_property = gst_colorconvert_get_property;

  base_transform_class->set_caps = gst_colorconvert_set_caps;
  base_transform_class->transform_ip = gst_colorconvert_transform_ip;
}

static void
gst_colorconvert_init (GTypeInstance * instance, gpointer g_class)
{

  GST_DEBUG ("gst_colorconvert_init");
}

static void
gst_colorconvert_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  g_return_if_fail (GST_IS_COLORCONVERT (object));

  GST_DEBUG ("gst_colorconvert_set_property");
  switch (prop_id) {
    default:
      break;
  }
}

static void
gst_colorconvert_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  g_return_if_fail (GST_IS_COLORCONVERT (object));

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_colorconvert_set_caps (GstBaseTransform * base_transform,
    GstCaps * incaps, GstCaps * outcaps)
{
  GstColorconvert *li;

  g_return_val_if_fail (GST_IS_COLORCONVERT (base_transform), GST_FLOW_ERROR);
  li = GST_COLORCONVERT (base_transform);

  gst_video_format_parse_caps (incaps, &li->format, &li->width, &li->height);

  return TRUE;
}

static GstFlowReturn
gst_colorconvert_transform_ip (GstBaseTransform * base_transform,
    GstBuffer * buf)
{
  GstColorconvert *li;
  CogFrame *frame;
  CogFrame *vf;

  g_return_val_if_fail (GST_IS_COLORCONVERT (base_transform), GST_FLOW_ERROR);
  li = GST_COLORCONVERT (base_transform);

  frame = gst_cog_buffer_wrap (gst_buffer_ref (buf),
      li->format, li->width, li->height);

  vf = cog_virt_frame_new_unpack (cog_frame_ref (frame));
  vf = cog_virt_frame_new_subsample (vf, COG_FRAME_FORMAT_U8_444,
      COG_CHROMA_SITE_MPEG2, 2);
  vf = cog_virt_frame_new_color_transform (vf);
  if (frame->format == COG_FRAME_FORMAT_YUYV) {
    vf = cog_virt_frame_new_subsample (vf, COG_FRAME_FORMAT_U8_422,
        COG_CHROMA_SITE_MPEG2, 2);
    vf = cog_virt_frame_new_pack_YUY2 (vf);
  } else if (frame->format == COG_FRAME_FORMAT_UYVY) {
    vf = cog_virt_frame_new_subsample (vf, COG_FRAME_FORMAT_U8_422,
        COG_CHROMA_SITE_MPEG2, 2);
    vf = cog_virt_frame_new_pack_UYVY (vf);
  } else if (frame->format == COG_FRAME_FORMAT_AYUV) {
    vf = cog_virt_frame_new_pack_AYUV (vf);
  } else if (frame->format == COG_FRAME_FORMAT_U8_420) {
    vf = cog_virt_frame_new_subsample (vf, COG_FRAME_FORMAT_U8_420,
        COG_CHROMA_SITE_MPEG2, 2);
  } else {
    g_assert_not_reached ();
  }

  cog_virt_frame_render (vf, frame);

  cog_frame_unref (frame);
  cog_frame_unref (vf);

  return GST_FLOW_OK;
}



static void
color_transform (CogFrame * frame, void *_dest, int component, int j)
{
  uint8_t *dest = _dest;
  uint8_t *src_y;
  uint8_t *src_u;
  uint8_t *src_v;
  uint8_t *table;
  int i;

  table = COG_OFFSET (frame->virt_priv2, component * 0x1000000);

  src_y = cog_virt_frame_get_line (frame->virt_frame1, 0, j);
  src_u = cog_virt_frame_get_line (frame->virt_frame1, 1, j);
  src_v = cog_virt_frame_get_line (frame->virt_frame1, 2, j);

  for (i = 0; i < frame->width; i++) {
    dest[i] = table[(src_y[i] << 16) | (src_u[i] << 8) | (src_v[i])];
  }
}

static uint8_t *get_color_transform_table (void);

static CogFrame *
cog_virt_frame_new_color_transform (CogFrame * frame)
{
  CogFrame *virt_frame;

  g_return_val_if_fail (frame->format == COG_FRAME_FORMAT_U8_444, NULL);

  virt_frame = cog_frame_new_virtual (NULL, COG_FRAME_FORMAT_U8_444,
      frame->width, frame->height);
  virt_frame->virt_frame1 = frame;
  virt_frame->render_line = color_transform;

  virt_frame->virt_priv2 = get_color_transform_table ();

  return virt_frame;
}


static uint8_t *
get_color_transform_table (void)
{
  static uint8_t *color_transform_table = NULL;

#if 1
  if (!color_transform_table) {
    ColorMatrix bt601_to_rgb;
    ColorMatrix bt601_to_yuv;
    ColorMatrix bt601_rgb_to_XYZ;
    ColorMatrix dell_XYZ_to_rgb;
    uint8_t *table_y;
    uint8_t *table_u;
    uint8_t *table_v;
    int y, u, v;

    color_matrix_build_yuv_to_rgb_601 (&bt601_to_rgb);
    color_matrix_build_rgb_to_yuv_601 (&bt601_to_yuv);
    color_matrix_build_rgb_to_XYZ_601 (&bt601_rgb_to_XYZ);
    color_matrix_build_XYZ_to_rgb_dell (&dell_XYZ_to_rgb);

    color_transform_table = g_malloc (0x1000000 * 3);

    table_y = COG_OFFSET (color_transform_table, 0 * 0x1000000);
    table_u = COG_OFFSET (color_transform_table, 1 * 0x1000000);
    table_v = COG_OFFSET (color_transform_table, 2 * 0x1000000);

    for (y = 0; y < 256; y++) {
      for (u = 0; u < 256; u++) {
        for (v = 0; v < 256; v++) {
          Color c;

          c.v[0] = y;
          c.v[1] = u;
          c.v[2] = v;
          color_matrix_apply (&bt601_to_rgb, &c, &c);
          color_gamut_clamp (&c, &c);
          color_transfer_function_apply (&c, &c);
          color_matrix_apply (&bt601_rgb_to_XYZ, &c, &c);
          color_matrix_apply (&dell_XYZ_to_rgb, &c, &c);
          color_transfer_function_unapply (&c, &c);
          color_gamut_clamp (&c, &c);
          color_matrix_apply (&bt601_to_yuv, &c, &c);

          table_y[(y << 16) | (u << 8) | (v)] = rint (c.v[0]);
          table_u[(y << 16) | (u << 8) | (v)] = rint (c.v[1]);
          table_v[(y << 16) | (u << 8) | (v)] = rint (c.v[2]);
        }
      }
    }
  }
#endif
#if 0
  if (!color_transform_table) {
    ColorMatrix bt709_to_bt601;
    uint8_t *table_y;
    uint8_t *table_u;
    uint8_t *table_v;
    int y, u, v;

    color_matrix_build_bt709_to_bt601 (&bt709_to_bt601);

    color_transform_table = g_malloc (0x1000000 * 3);

    table_y = COG_OFFSET (color_transform_table, 0 * 0x1000000);
    table_u = COG_OFFSET (color_transform_table, 1 * 0x1000000);
    table_v = COG_OFFSET (color_transform_table, 2 * 0x1000000);

    for (y = 0; y < 256; y++) {
      for (u = 0; u < 256; u++) {
        for (v = 0; v < 256; v++) {
          Color c;

          c.v[0] = y;
          c.v[1] = u;
          c.v[2] = v;
          color_matrix_apply (&bt709_to_bt601, &c, &c);

          table_y[(y << 16) | (u << 8) | (v)] = rint (c.v[0]);
          table_u[(y << 16) | (u << 8) | (v)] = rint (c.v[1]);
          table_v[(y << 16) | (u << 8) | (v)] = rint (c.v[2]);
        }
      }
    }
  }
#endif

  return color_transform_table;
}
