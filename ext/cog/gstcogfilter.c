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

#define SCHRO_ENABLE_UNSTABLE_API

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>
#include <gst/video/video.h>
#include <string.h>
#include <schroedinger/schro.h>
#include <schroedinger/schrotables.h>
#include <liboil/liboil.h>
#include <math.h>

#define GST_TYPE_SCHROFILTER \
  (gst_schrofilter_get_type())
#define GST_SCHROFILTER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_SCHROFILTER,GstSchrofilter))
#define GST_SCHROFILTER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_SCHROFILTER,GstSchrofilterClass))
#define GST_IS_SCHROFILTER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_SCHROFILTER))
#define GST_IS_SCHROFILTER_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_SCHROFILTER))

typedef struct _GstSchrofilter GstSchrofilter;
typedef struct _GstSchrofilterClass GstSchrofilterClass;

struct _GstSchrofilter
{
  GstBaseTransform base_transform;

  int wavelet_type;
  int level;

  SchroVideoFormat format;

  SchroFrame *tmp_frame;
  int16_t *tmpbuf;

  int frame_number;

};

struct _GstSchrofilterClass
{
  GstBaseTransformClass parent_class;

};


/* GstSchrofilter signals and args */
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

GType gst_schrofilter_get_type (void);

static void gst_schrofilter_base_init (gpointer g_class);
static void gst_schrofilter_class_init (gpointer g_class, gpointer class_data);
static void gst_schrofilter_init (GTypeInstance * instance, gpointer g_class);

static void gst_schrofilter_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_schrofilter_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstFlowReturn gst_schrofilter_transform_ip (GstBaseTransform *
    base_transform, GstBuffer * buf);

static GstStaticPadTemplate gst_schrofilter_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("I420"))
    );

static GstStaticPadTemplate gst_schrofilter_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("I420"))
    );

GType
gst_schrofilter_get_type (void)
{
  static GType compress_type = 0;

  if (!compress_type) {
    static const GTypeInfo compress_info = {
      sizeof (GstSchrofilterClass),
      gst_schrofilter_base_init,
      NULL,
      gst_schrofilter_class_init,
      NULL,
      NULL,
      sizeof (GstSchrofilter),
      0,
      gst_schrofilter_init,
    };

    compress_type = g_type_register_static (GST_TYPE_BASE_TRANSFORM,
        "GstSchrofilter", &compress_info, 0);
  }
  return compress_type;
}


static void
gst_schrofilter_base_init (gpointer g_class)
{
  static GstElementDetails compress_details =
      GST_ELEMENT_DETAILS ("Schroedinger Video Filters",
      "Filter/Effect/Video",
      "Applies a Schroedinger compression pre-filter to video",
      "David Schleef <ds@schleef.org>");
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);
  //GstBaseTransformClass *base_transform_class = GST_BASE_TRANSFORM_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_schrofilter_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_schrofilter_sink_template));

  gst_element_class_set_details (element_class, &compress_details);
}

static void
gst_schrofilter_class_init (gpointer g_class, gpointer class_data)
{
  GObjectClass *gobject_class;
  GstBaseTransformClass *base_transform_class;
  GstSchrofilterClass *filter_class;

  gobject_class = G_OBJECT_CLASS (g_class);
  base_transform_class = GST_BASE_TRANSFORM_CLASS (g_class);
  filter_class = GST_SCHROFILTER_CLASS (g_class);

  gobject_class->set_property = gst_schrofilter_set_property;
  gobject_class->get_property = gst_schrofilter_get_property;

  g_object_class_install_property (gobject_class, ARG_WAVELET_TYPE,
      g_param_spec_int ("wavelet-type", "wavelet type", "wavelet type",
          0, 4, 0, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_LEVEL,
      g_param_spec_int ("level", "level", "level",
          0, 100, 0, G_PARAM_READWRITE));

  base_transform_class->transform_ip = gst_schrofilter_transform_ip;
}

static void
gst_schrofilter_init (GTypeInstance * instance, gpointer g_class)
{
  //GstSchrofilter *compress = GST_SCHROFILTER (instance);
  //GstBaseTransform *btrans = GST_BASE_TRANSFORM (instance);

  GST_DEBUG ("gst_schrofilter_init");
}

static void
gst_schrofilter_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstSchrofilter *src;

  g_return_if_fail (GST_IS_SCHROFILTER (object));
  src = GST_SCHROFILTER (object);

  GST_DEBUG ("gst_schrofilter_set_property");
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
gst_schrofilter_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstSchrofilter *src;

  g_return_if_fail (GST_IS_SCHROFILTER (object));
  src = GST_SCHROFILTER (object);

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
gst_schrofilter_transform_ip (GstBaseTransform * base_transform,
    GstBuffer * buf)
{
  GstSchrofilter *compress;
  SchroFrame *frame;
  int width, height;

  g_return_val_if_fail (GST_IS_SCHROFILTER (base_transform), GST_FLOW_ERROR);
  compress = GST_SCHROFILTER (base_transform);

  gst_structure_get_int (gst_caps_get_structure (buf->caps, 0),
      "width", &width);
  gst_structure_get_int (gst_caps_get_structure (buf->caps, 0),
      "height", &height);

  frame = schro_frame_new_from_data_I420 (GST_BUFFER_DATA (buf), width, height);
  schro_frame_filter_lowpass2 (frame, 5.0);
  //schro_frame_filter_wavelet (frame);

  return GST_FLOW_OK;
}
