/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) <2003> David Schleef <ds@schleef.org>
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

/*
 * This file was (probably) generated from
 * gstvideotemplate.c,v 1.18 2005/11/14 02:13:34 thomasvs Exp 
 * and
 * $Id: make_filter,v 1.8 2004/04/19 22:51:57 ds Exp $
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>
#include <gst/video/video.h>
#include <string.h>
#include <cog/cog.h>
#include <math.h>
#include <cog/cogvirtframe.h>

#define GST_TYPE_COGDOWNSAMPLE \
  (gst_cogdownsample_get_type())
#define GST_COGDOWNSAMPLE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_COGDOWNSAMPLE,GstCogdownsample))
#define GST_COGDOWNSAMPLE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_COGDOWNSAMPLE,GstCogdownsampleClass))
#define GST_IS_COGDOWNSAMPLE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_COGDOWNSAMPLE))
#define GST_IS_COGDOWNSAMPLE_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_COGDOWNSAMPLE))

typedef struct _GstCogdownsample GstCogdownsample;
typedef struct _GstCogdownsampleClass GstCogdownsampleClass;

struct _GstCogdownsample
{
  GstBaseTransform base_transform;

};

struct _GstCogdownsampleClass
{
  GstBaseTransformClass parent_class;

};

GType gst_cogdownsample_get_type (void);

enum
{
  ARG_0
};

static void gst_cogdownsample_base_init (gpointer g_class);
static void gst_cogdownsample_class_init (gpointer g_class,
    gpointer class_data);
static void gst_cogdownsample_init (GTypeInstance * instance, gpointer g_class);

static void gst_cogdownsample_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_cogdownsample_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstCaps *gst_cogdownsample_transform_caps (GstBaseTransform *
    base_transform, GstPadDirection direction, GstCaps * caps);
static GstFlowReturn gst_cogdownsample_transform (GstBaseTransform *
    base_transform, GstBuffer * inbuf, GstBuffer * outbuf);
static gboolean gst_cogdownsample_get_unit_size (GstBaseTransform *
    base_transform, GstCaps * caps, guint * size);

static GstStaticPadTemplate gst_cogdownsample_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("{ I420, YV12, YUY2, UYVY, AYUV }"))
    );

static GstStaticPadTemplate gst_cogdownsample_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("{ I420, YV12, YUY2, UYVY, AYUV }"))
    );

GType
gst_cogdownsample_get_type (void)
{
  static GType compress_type = 0;

  if (!compress_type) {
    static const GTypeInfo compress_info = {
      sizeof (GstCogdownsampleClass),
      gst_cogdownsample_base_init,
      NULL,
      gst_cogdownsample_class_init,
      NULL,
      NULL,
      sizeof (GstCogdownsample),
      0,
      gst_cogdownsample_init,
    };

    compress_type = g_type_register_static (GST_TYPE_BASE_TRANSFORM,
        "GstCogdownsample", &compress_info, 0);
  }
  return compress_type;
}


static void
gst_cogdownsample_base_init (gpointer g_class)
{

  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_static_pad_template (element_class,
      &gst_cogdownsample_src_template);
  gst_element_class_add_static_pad_template (element_class,
      &gst_cogdownsample_sink_template);

  gst_element_class_set_details_simple (element_class,
      "Scale down video by factor of 2", "Filter/Effect/Video",
      "Scales down video by a factor of 2", "David Schleef <ds@schleef.org>");
}

static void
gst_cogdownsample_class_init (gpointer g_class, gpointer class_data)
{
  GObjectClass *gobject_class;
  GstBaseTransformClass *base_transform_class;

  gobject_class = G_OBJECT_CLASS (g_class);
  base_transform_class = GST_BASE_TRANSFORM_CLASS (g_class);

  gobject_class->set_property = gst_cogdownsample_set_property;
  gobject_class->get_property = gst_cogdownsample_get_property;

  base_transform_class->transform = gst_cogdownsample_transform;
  base_transform_class->transform_caps = gst_cogdownsample_transform_caps;
  base_transform_class->get_unit_size = gst_cogdownsample_get_unit_size;
}

static void
gst_cogdownsample_init (GTypeInstance * instance, gpointer g_class)
{

  GST_DEBUG ("gst_cogdownsample_init");
}

static void
gst_cogdownsample_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  g_return_if_fail (GST_IS_COGDOWNSAMPLE (object));

  GST_DEBUG ("gst_cogdownsample_set_property");
  switch (prop_id) {
    default:
      break;
  }
}

static void
gst_cogdownsample_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  g_return_if_fail (GST_IS_COGDOWNSAMPLE (object));

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
transform_value (GValue * dest, const GValue * src, GstPadDirection dir)
{
  g_value_init (dest, G_VALUE_TYPE (src));

  if (G_VALUE_HOLDS_INT (src)) {
    int x;

    x = g_value_get_int (src);
    if (dir == GST_PAD_SINK) {
      g_value_set_int (dest, x / 2);
    } else {
      g_value_set_int (dest, x * 2);
    }
  } else if (GST_VALUE_HOLDS_INT_RANGE (src)) {
    int min, max;

    min = gst_value_get_int_range_min (src);
    max = gst_value_get_int_range_max (src);

    if (dir == GST_PAD_SINK) {
      min = (min + 1) / 2;
      if (max == G_MAXINT) {
        max = G_MAXINT / 2;
      } else {
        max = (max + 1) / 2;
      }
    } else {
      if (max > G_MAXINT / 2) {
        max = G_MAXINT;
      } else {
        max = max * 2;
      }
      if (min > G_MAXINT / 2) {
        min = G_MAXINT;
      } else {
        min = min * 2;
      }
    }
    gst_value_set_int_range (dest, min, max);
  } else {
    /* FIXME */
    g_warning ("case not handled");
    g_value_set_int (dest, 100);
  }
}

static GstCaps *
gst_cogdownsample_transform_caps (GstBaseTransform * base_transform,
    GstPadDirection direction, GstCaps * caps)
{
  int i;
  GstStructure *structure;
  GValue new_value = { 0 };
  const GValue *value;

  caps = gst_caps_copy (caps);

  for (i = 0; i < gst_caps_get_size (caps); i++) {
    structure = gst_caps_get_structure (caps, i);

    value = gst_structure_get_value (structure, "width");
    transform_value (&new_value, value, direction);
    gst_structure_set_value (structure, "width", &new_value);
    g_value_unset (&new_value);

    value = gst_structure_get_value (structure, "height");
    transform_value (&new_value, value, direction);
    gst_structure_set_value (structure, "height", &new_value);
    g_value_unset (&new_value);
  }

  return caps;
}

static gboolean
gst_cogdownsample_get_unit_size (GstBaseTransform * base_transform,
    GstCaps * caps, guint * size)
{
  int width, height;
  uint32_t format;

  gst_structure_get_fourcc (gst_caps_get_structure (caps, 0),
      "format", &format);
  gst_structure_get_int (gst_caps_get_structure (caps, 0), "width", &width);
  gst_structure_get_int (gst_caps_get_structure (caps, 0), "height", &height);

  switch (format) {
    case GST_MAKE_FOURCC ('I', '4', '2', '0'):
    case GST_MAKE_FOURCC ('Y', 'V', '1', '2'):
      *size = width * height * 3 / 2;
      break;
    case GST_MAKE_FOURCC ('Y', 'U', 'Y', '2'):
    case GST_MAKE_FOURCC ('U', 'Y', 'V', 'Y'):
      *size = width * height * 2;
      break;
    case GST_MAKE_FOURCC ('A', 'Y', 'U', 'V'):
      *size = width * height * 4;
      break;
    default:
      g_assert_not_reached ();
  }

  return TRUE;
}

static GstFlowReturn
gst_cogdownsample_transform (GstBaseTransform * base_transform,
    GstBuffer * inbuf, GstBuffer * outbuf)
{
  CogFrame *outframe;
  int width, height;
  uint32_t format;
  CogFrame *frame;

  g_return_val_if_fail (GST_IS_COGDOWNSAMPLE (base_transform), GST_FLOW_ERROR);

  gst_structure_get_fourcc (gst_caps_get_structure (inbuf->caps, 0),
      "format", &format);
  gst_structure_get_int (gst_caps_get_structure (inbuf->caps, 0),
      "width", &width);
  gst_structure_get_int (gst_caps_get_structure (inbuf->caps, 0),
      "height", &height);

  switch (format) {
    case GST_MAKE_FOURCC ('I', '4', '2', '0'):
      frame = cog_frame_new_from_data_I420 (GST_BUFFER_DATA (inbuf),
          width, height);
      outframe = cog_frame_new_from_data_I420 (GST_BUFFER_DATA (outbuf),
          width / 2, height / 2);
      break;
    case GST_MAKE_FOURCC ('Y', 'V', '1', '2'):
      frame = cog_frame_new_from_data_YV12 (GST_BUFFER_DATA (inbuf),
          width, height);
      outframe = cog_frame_new_from_data_YV12 (GST_BUFFER_DATA (outbuf),
          width / 2, height / 2);
      break;
    case GST_MAKE_FOURCC ('Y', 'U', 'Y', '2'):
      frame = cog_frame_new_from_data_YUY2 (GST_BUFFER_DATA (inbuf),
          width, height);
      outframe = cog_frame_new_from_data_YUY2 (GST_BUFFER_DATA (outbuf),
          width / 2, height / 2);
      break;
    case GST_MAKE_FOURCC ('U', 'Y', 'V', 'Y'):
      frame = cog_frame_new_from_data_UYVY (GST_BUFFER_DATA (inbuf),
          width, height);
      outframe = cog_frame_new_from_data_UYVY (GST_BUFFER_DATA (outbuf),
          width / 2, height / 2);
      break;
    case GST_MAKE_FOURCC ('A', 'Y', 'U', 'V'):
      frame = cog_frame_new_from_data_AYUV (GST_BUFFER_DATA (inbuf),
          width, height);
      outframe = cog_frame_new_from_data_AYUV (GST_BUFFER_DATA (outbuf),
          width / 2, height / 2);
      break;
    default:
      g_assert_not_reached ();
      return GST_FLOW_ERROR;
  }

  frame = cog_virt_frame_new_unpack (frame);
  frame = cog_virt_frame_new_horiz_downsample (frame, 3);
  frame = cog_virt_frame_new_vert_downsample (frame, 2);

  switch (format) {
    case GST_MAKE_FOURCC ('Y', 'U', 'Y', '2'):
      frame = cog_virt_frame_new_pack_YUY2 (frame);
      break;
    case GST_MAKE_FOURCC ('U', 'Y', 'V', 'Y'):
      frame = cog_virt_frame_new_pack_UYVY (frame);
      break;
    case GST_MAKE_FOURCC ('A', 'Y', 'U', 'V'):
      frame = cog_virt_frame_new_pack_AYUV (frame);
      break;
    default:
      break;
  }

  cog_virt_frame_render (frame, outframe);
  cog_frame_unref (frame);
  cog_frame_unref (outframe);

  return GST_FLOW_OK;
}
