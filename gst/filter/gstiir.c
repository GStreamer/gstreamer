/* -*- c-basic-offset: 2 -*-
 * 
 * GStreamer
 * Copyright (C) 1999-2001 Erik Walthinsen <omega@cse.ogi.edu>
 *               2006 Dreamlab Technologies Ltd. <mathis.hofer@dreamlab.net>
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

#include <string.h>
#include <math.h>
#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>
#include <gst/controller/gstcontroller.h>

#include "gstiir.h"

#define GST_CAT_DEFAULT gst_iir_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

/* Filter signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_A,
  PROP_B,
  PROP_GAIN,
  PROP_STAGES
};

static GstStaticPadTemplate iir_sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-float, "
        "rate = (int) [ 1, MAX ], "
        "channels = (int) [ 1, MAX ], "
        "endianness = (int) BYTE_ORDER, " "width = (int) 32")
    );

static GstStaticPadTemplate iir_src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-float, "
        "rate = (int) [ 1, MAX ], "
        "channels = (int) [ 1, MAX ], "
        "endianness = (int) BYTE_ORDER, " "width = (int) 32")
    );

#define DEBUG_INIT(bla) \
  GST_DEBUG_CATEGORY_INIT (gst_iir_debug, "iir", 0, "Infinite Impulse Response (IIR) filter plugin");

GST_BOILERPLATE_FULL (GstIIR, gst_iir, GstBaseTransform,
    GST_TYPE_BASE_TRANSFORM, DEBUG_INIT);

static void iir_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void iir_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstFlowReturn iir_transform_ip (GstBaseTransform * base,
    GstBuffer * outbuf);
static gboolean iir_set_caps (GstBaseTransform * base, GstCaps * incaps,
    GstCaps * outcaps);

/* Element class */

static void
gst_iir_dispose (GObject * object)
{
  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_iir_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&iir_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&iir_sink_template));
  gst_element_class_set_details_simple (element_class,
      "Infinite Impulse Response (IIR) filter", "Filter/Effect/Audio",
      "IIR filter based on vorbis code",
      "Monty <monty@xiph.org>, "
      "Thomas Vander Stichele <thomas at apestaart dot org>, "
      "Dreamlab Technologies Ltd. <mathis.hofer@dreamlab.net>");
}

static void
gst_iir_class_init (GstIIRClass * klass)
{
  GObjectClass *gobject_class;
  GstBaseTransformClass *trans_class;

  gobject_class = (GObjectClass *) klass;
  trans_class = (GstBaseTransformClass *) klass;

  gobject_class->set_property = iir_set_property;
  gobject_class->get_property = iir_get_property;
  gobject_class->dispose = gst_iir_dispose;

  g_object_class_install_property (gobject_class, PROP_A,
      g_param_spec_double ("A", "A", "A filter coefficient",
          -G_MAXDOUBLE, G_MAXDOUBLE, 0, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_B,
      g_param_spec_double ("B", "B", "B filter coefficient",
          -G_MAXDOUBLE, G_MAXDOUBLE, 0, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_GAIN,
      g_param_spec_double ("gain", "Gain", "Filter gain",
          -G_MAXDOUBLE, G_MAXDOUBLE, 0, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_STAGES,
      g_param_spec_int ("stages", "Stages", "Number of filter stages",
          1, G_MAXINT, 1, G_PARAM_READWRITE));

  trans_class->transform_ip = GST_DEBUG_FUNCPTR (iir_transform_ip);
  trans_class->set_caps = GST_DEBUG_FUNCPTR (iir_set_caps);
}

static void
gst_iir_init (GstIIR * this, GstIIRClass * g_class)
{
  this->A = 0.0;
  this->B = 0.0;
  this->gain = 1.0;             /* unity gain as default */
  this->stages = 1;
  this->state = NULL;
}


/* GstBaseTransform vmethod implementations */

/* get notified of caps and plug in the correct process function */
static gboolean
iir_set_caps (GstBaseTransform * base, GstCaps * incaps, GstCaps * outcaps)
{
  GstIIR *this = GST_IIR (base);

  GST_DEBUG_OBJECT (this,
      "set_caps: in %" GST_PTR_FORMAT " out %" GST_PTR_FORMAT, incaps, outcaps);

  /* FIXME: remember to free it */
  this->state = (IIR_state *) g_malloc (sizeof (IIR_state));
  IIR_init (this->state, this->stages, this->gain, &(this->A), &(this->B));

  return TRUE;
}

static GstFlowReturn
iir_transform_ip (GstBaseTransform * base, GstBuffer * outbuf)
{
  GstIIR *this = GST_IIR (base);
  GstClockTime timestamp;

  gfloat *src;
  int i;

  /* don't process data in passthrough-mode */
  if (gst_base_transform_is_passthrough (base))
    return GST_FLOW_OK;

  /* FIXME: subdivide GST_BUFFER_SIZE into small chunks for smooth fades */
  timestamp = GST_BUFFER_TIMESTAMP (outbuf);

  if (GST_CLOCK_TIME_IS_VALID (timestamp))
    gst_object_sync_values (G_OBJECT (this), timestamp);

  src = (gfloat *) GST_BUFFER_DATA (outbuf);

  /* do an in-place edit */
  for (i = 0; i < GST_BUFFER_SIZE (outbuf) / sizeof (gfloat); ++i)
    *(src + i) = (gfloat) IIR_filter (this->state, (double) *(src + i));

  return GST_FLOW_OK;
}

static void
iir_set_property (GObject * object, guint prop_id, const GValue * value,
    GParamSpec * pspec)
{
  GstIIR *this = GST_IIR (object);

  g_return_if_fail (GST_IS_IIR (this));

  switch (prop_id) {
    case PROP_A:
      this->A = g_value_get_double (value);
      break;
    case PROP_B:
      this->B = g_value_get_double (value);
      break;
    case PROP_GAIN:
      this->gain = g_value_get_double (value);
      break;
    case PROP_STAGES:
      this->stages = g_value_get_int (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
iir_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstIIR *this = GST_IIR (object);

  switch (prop_id) {
    case PROP_A:
      g_value_set_double (value, this->A);
      break;
    case PROP_B:
      g_value_set_double (value, this->B);
      break;
    case PROP_GAIN:
      g_value_set_double (value, this->gain);
      break;
    case PROP_STAGES:
      g_value_set_int (value, this->stages);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}
