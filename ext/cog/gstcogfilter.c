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
#include <cog/cogframe.h>
#include <string.h>
#include <math.h>

#define GST_TYPE_COG_FILTER \
  (gst_cog_filter_get_type())
#define GST_COG_FILTER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_COG_FILTER,GstCogFilter))
#define GST_COG_FILTER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_COG_FILTER,GstCogFilterClass))
#define GST_IS_COG_FILTER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_COG_FILTER))
#define GST_IS_COG_FILTER_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_COG_FILTER))

typedef struct _GstCogFilter GstCogFilter;
typedef struct _GstCogFilterClass GstCogFilterClass;

struct _GstCogFilter
{
  GstBaseTransform base_transform;

  int wavelet_type;
  int level;

  GstVideoFormat format;

  CogFrame *tmp_frame;
  int16_t *tmpbuf;

  int frame_number;

};

struct _GstCogFilterClass
{
  GstBaseTransformClass parent_class;

};


/* GstCogFilter signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_WAVELET_TYPE,
  ARG_LEVEL
      /* FILL ME */
};

GType gst_cog_filter_get_type (void);

static void gst_cog_filter_base_init (gpointer g_class);
static void gst_cog_filter_class_init (gpointer g_class, gpointer class_data);
static void gst_cog_filter_init (GTypeInstance * instance, gpointer g_class);

static void gst_cog_filter_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_cog_filter_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstFlowReturn gst_cog_filter_transform_ip (GstBaseTransform *
    base_transform, GstBuffer * buf);

static GstStaticPadTemplate gst_cog_filter_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("I420"))
    );

static GstStaticPadTemplate gst_cog_filter_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("I420"))
    );

GType
gst_cog_filter_get_type (void)
{
  static GType compress_type = 0;

  if (!compress_type) {
    static const GTypeInfo compress_info = {
      sizeof (GstCogFilterClass),
      gst_cog_filter_base_init,
      NULL,
      gst_cog_filter_class_init,
      NULL,
      NULL,
      sizeof (GstCogFilter),
      0,
      gst_cog_filter_init,
    };

    compress_type = g_type_register_static (GST_TYPE_BASE_TRANSFORM,
        "GstCogFilter", &compress_info, 0);
  }
  return compress_type;
}


static void
gst_cog_filter_base_init (gpointer g_class)
{

  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_cog_filter_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_cog_filter_sink_template));

  gst_element_class_set_details_simple (element_class, "Cog Video Filter",
      "Filter/Effect/Video",
      "Applies a cog pre-filter to video", "David Schleef <ds@schleef.org>");
}

static void
gst_cog_filter_class_init (gpointer g_class, gpointer class_data)
{
  GObjectClass *gobject_class;
  GstBaseTransformClass *base_transform_class;
  GstCogFilterClass *filter_class;

  gobject_class = G_OBJECT_CLASS (g_class);
  base_transform_class = GST_BASE_TRANSFORM_CLASS (g_class);
  filter_class = GST_COG_FILTER_CLASS (g_class);

  gobject_class->set_property = gst_cog_filter_set_property;
  gobject_class->get_property = gst_cog_filter_get_property;

  g_object_class_install_property (gobject_class, ARG_WAVELET_TYPE,
      g_param_spec_int ("wavelet-type", "wavelet type", "wavelet type",
          0, 4, 0, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_LEVEL,
      g_param_spec_int ("level", "level", "level",
          0, 100, 0, G_PARAM_READWRITE));

  base_transform_class->transform_ip = gst_cog_filter_transform_ip;
}

static void
gst_cog_filter_init (GTypeInstance * instance, gpointer g_class)
{

  GST_DEBUG ("gst_cog_filter_init");
}

static void
gst_cog_filter_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstCogFilter *src;

  g_return_if_fail (GST_IS_COG_FILTER (object));
  src = GST_COG_FILTER (object);

  GST_DEBUG ("gst_cog_filter_set_property");
  switch (prop_id) {
    case ARG_WAVELET_TYPE:
      src->wavelet_type = g_value_get_int (value);
      break;
    case ARG_LEVEL:
      src->level = g_value_get_int (value);
      break;
    default:
      break;
  }
}

static void
gst_cog_filter_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstCogFilter *src;

  g_return_if_fail (GST_IS_COG_FILTER (object));
  src = GST_COG_FILTER (object);

  switch (prop_id) {
    case ARG_WAVELET_TYPE:
      g_value_set_int (value, src->wavelet_type);
      break;
    case ARG_LEVEL:
      g_value_set_int (value, src->level);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstFlowReturn
gst_cog_filter_transform_ip (GstBaseTransform * base_transform, GstBuffer * buf)
{
  GstCogFilter *compress;
  CogFrame *frame;
  int width, height;

  g_return_val_if_fail (GST_IS_COG_FILTER (base_transform), GST_FLOW_ERROR);
  compress = GST_COG_FILTER (base_transform);

  gst_structure_get_int (gst_caps_get_structure (buf->caps, 0),
      "width", &width);
  gst_structure_get_int (gst_caps_get_structure (buf->caps, 0),
      "height", &height);

  frame = cog_frame_new_from_data_I420 (GST_BUFFER_DATA (buf), width, height);

  /* FIXME do something here */

  return GST_FLOW_OK;
}
