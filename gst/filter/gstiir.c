/* -*- c-basic-offset: 2 -*-
 * GStreamer
 * Copyright (C) 1999-2001 Erik Walthinsen <omega@cse.ogi.edu>
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

#include <gst/gst.h>
#include "gstfilter.h"
#include "iir.h"

GstElementDetails gst_iir_details = {
  "IIR",
  "Filter/Audio/Effect",
  "IIR filter based on vorbis code",
  VERSION,
  "Monty <monty@xiph.org>, "\
  "Thomas <thomas@apestaart.org>",
  "(C) 2001",
};

enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_A,
  ARG_B,
  ARG_GAIN,
  ARG_STAGES,
};

#define GST_TYPE_IIR \
  (gst_iir_get_type())
#define GST_IIR(obj) \
      (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_IIR,GstIIR))
#define GST_IIR_CLASS(klass) \
      (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_ULAW,GstIIR))
#define GST_IS_IIR(obj) \
      (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_IIR))
#define GST_IS_IIR_CLASS(obj) \
      (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_IIR))

typedef struct _GstIIR GstIIR;
typedef struct _GstIIRClass GstIIRClass;

struct _GstIIR
{
  GstElement element;

  GstPad *sinkpad, *srcpad;

  double A, B;
  double gain;
  int stages;
  IIR_state *state;
};

struct _GstIIRClass
{
    GstElementClass parent_class;
};

static void gst_iir_class_init		(GstIIRClass * klass);
static void gst_iir_init               	(GstIIR * filter);

static void gst_iir_set_property	(GObject * object, guint prop_id,
                                         const GValue * value, 
					 GParamSpec * pspec);
static void gst_iir_get_property	(GObject * object, guint prop_id,
                                         GValue * value, GParamSpec * pspec);

static void gst_iir_chain		(GstPad * pad, GstBuffer * buf);
static GstPadConnectReturn
       gst_iir_sink_connect 		(GstPad * pad, GstCaps * caps);

static GstElementClass *parent_class = NULL;
/*static guint gst_iir_signals[LAST_SIGNAL] = { 0 }; */

GType gst_iir_get_type (void)
{
  static GType iir_type = 0;

  if (!iir_type) {
    static const GTypeInfo iir_info = {
      sizeof (GstIIRClass), NULL, NULL,
      (GClassInitFunc) gst_iir_class_init, NULL, NULL,
      sizeof (GstIIR), 0,
      (GInstanceInitFunc) gst_iir_init,
    };

    iir_type = g_type_register_static (GST_TYPE_ELEMENT, "GstIIR", 
	                                   &iir_info, 0);
  }
  return iir_type;
}

static void
gst_iir_class_init (GstIIRClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_A,
      g_param_spec_double ("A", "A", "A filter coefficient", 
	                   -G_MAXDOUBLE, G_MAXDOUBLE, 
	                   0, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_B,
      g_param_spec_double ("B", "B", "B filter coefficient", 
	                   -G_MAXDOUBLE, G_MAXDOUBLE, 
	                   0, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_GAIN,
      g_param_spec_double ("gain", "Gain", "Filter gain", 
	                   -G_MAXDOUBLE, G_MAXDOUBLE, 
	                   0, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_STAGES,
      g_param_spec_int ("stages", "Stages", "Number of filter stages", 
	                   1, G_MAXINT, 
	                   0, G_PARAM_READWRITE));

  gobject_class->set_property = gst_iir_set_property;
  gobject_class->get_property = gst_iir_get_property;
}

static void
gst_iir_init (GstIIR * filter)
{
  filter->sinkpad = gst_pad_new_from_template (gst_filter_sink_factory (), "sink");
  gst_pad_set_chain_function (filter->sinkpad, gst_iir_chain);
  gst_pad_set_connect_function (filter->sinkpad, gst_iir_sink_connect);
  gst_element_add_pad (GST_ELEMENT (filter), filter->sinkpad);

  filter->srcpad = gst_pad_new_from_template (gst_filter_src_factory (), "src");
  gst_element_add_pad (GST_ELEMENT (filter), filter->srcpad);

  filter->A = 0.0;
  filter->B = 0.0;
  filter->gain = 1.0;	/* unity gain as default */
  filter->stages = 1;
  filter->state = NULL;
}

static GstPadConnectReturn
gst_iir_sink_connect (GstPad * pad, GstCaps * caps)
{
  GstIIR *filter;
  
  filter = GST_IIR (gst_pad_get_parent (pad));
  
  if (!GST_CAPS_IS_FIXED (caps))
    return GST_PAD_CONNECT_DELAYED;
    
  if (gst_pad_try_set_caps (filter->srcpad, caps)) {
    /* connection works, so init the filter */
    /* FIXME: remember to free it */
    filter->state = (IIR_state *) g_malloc (sizeof (IIR_state));
    IIR_init (filter->state, filter->stages, 
	      filter->gain, &(filter->A), &(filter->B));
    return GST_PAD_CONNECT_OK;
  }

  return GST_PAD_CONNECT_REFUSED;
}

static void
gst_iir_chain (GstPad * pad, GstBuffer * buf)
{
  GstIIR *filter;
  gfloat *src;
  int i;

  filter = GST_IIR (gst_pad_get_parent (pad));

  src = (gfloat *) GST_BUFFER_DATA (buf);

  /* do an in-place edit */
  for (i = 0; i < GST_BUFFER_SIZE (buf) / sizeof (gfloat); ++i)
    *(src + i) = (gfloat) IIR_filter (filter->state, (double) *(src + i));

  gst_pad_push (filter->srcpad, buf);
}

static void
gst_iir_set_property (GObject * object, guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstIIR *filter;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_IIR (object));

  filter = GST_IIR (object);

  switch (prop_id) {
    case ARG_A:
     filter->A = g_value_get_double (value);
     break; 
    case ARG_B:
     filter->B = g_value_get_double (value);
     break; 
    case ARG_GAIN:
     filter->gain = g_value_get_double (value);
     break; 
    case ARG_STAGES:
     filter->stages = g_value_get_int (value);
     break; 
    default:
      break;
  }
}

static void
gst_iir_get_property (GObject * object, guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstIIR *filter;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_IIR (object));
  
  filter = GST_IIR (object);

  switch (prop_id) {
    case ARG_A:
      g_value_set_double (value, filter->A);
      break;
    case ARG_B:
      g_value_set_double (value, filter->B);
      break;
    case ARG_GAIN:
      g_value_set_double (value, filter->gain);
      break;
    case ARG_STAGES:
      g_value_set_int (value, filter->stages);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
} 

