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

/* this windowed sinc filter is taken from the freely downloadable DSP book,
 * "The Scientist and Engineer's Guide to Digital Signal Processing",
 * chapter 16
 * available at http://www.dspguide.com/
 */

/* FIXME:
 * - this filter is totally unoptimized !
 * - we do not destroy the allocated memory for filters and residue
 * - this might be improved upon with bytestream
 */

#include <gst/gst.h>
#include "gstfilter.h"
#include <math.h>		/* M_PI */
#include <string.h>		/* memmove */

GstElementDetails gst_wsinc_details = {
  "WSinc",
  "Filter/Audio/Effect",
  "Windowed sinc filter",
  VERSION,
  "Thomas <thomas@apestaart.org>",
  "(C) 2002 Steven W. Smith",
};

enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_LENGTH,
  ARG_FREQUENCY,
};

#define GST_TYPE_WSINC \
  (gst_wsinc_get_type())
#define GST_WSINC(obj) \
      (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_WSINC,GstWSinc))
#define GST_WSINC_CLASS(klass) \
      (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_ULAW,GstWSinc))
#define GST_IS_WSINC(obj) \
      (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_WSINC))
#define GST_IS_WSINC_CLASS(obj) \
      (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_WSINC))

typedef struct _GstWSinc GstWSinc;
typedef struct _GstWSincClass GstWSincClass;

struct _GstWSinc
{
  GstElement element;

  GstPad *sinkpad, *srcpad;

  double frequency;
  int wing_size;	/* length of a "wing" of the filter; 
			   actual length is 2 * wing_size + 1 */

  gfloat *residue;	/* buffer for left-over samples from previous buffer */
  double *kernel;
};

struct _GstWSincClass
{
    GstElementClass parent_class;
};

static void gst_wsinc_class_init		(GstWSincClass * klass);
static void gst_wsinc_init               	(GstWSinc * filter);

static void gst_wsinc_set_property	(GObject * object, guint prop_id,
                                         const GValue * value, 
					 GParamSpec * pspec);
static void gst_wsinc_get_property	(GObject * object, guint prop_id,
                                         GValue * value, GParamSpec * pspec);

static void gst_wsinc_chain		(GstPad * pad, GstBuffer * buf);
static GstPadConnectReturn
       gst_wsinc_sink_connect 		(GstPad * pad, GstCaps * caps);

static GstElementClass *parent_class = NULL;
/*static guint gst_wsinc_signals[LAST_SIGNAL] = { 0 }; */

GType gst_wsinc_get_type (void)
{
  static GType wsinc_type = 0;

  if (!wsinc_type) {
    static const GTypeInfo wsinc_info = {
      sizeof (GstWSincClass), NULL, NULL,
      (GClassInitFunc) gst_wsinc_class_init, NULL, NULL,
      sizeof (GstWSinc), 0,
      (GInstanceInitFunc) gst_wsinc_init,
    };

    wsinc_type = g_type_register_static (GST_TYPE_ELEMENT, "GstWSinc", 
	                                   &wsinc_info, 0);
  }
  return wsinc_type;
}

static void
gst_wsinc_class_init (GstWSincClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_FREQUENCY,
      g_param_spec_double ("frequency", "Frequency", 
	                   "Cut-off Frequency relative to sample rate)", 
	                   0.0, 0.5,
	                   0, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_LENGTH,
      g_param_spec_int ("length", "Length", 
	                "N such that the filter length = 2N + 1",
	                   1, G_MAXINT, 
	                   1, G_PARAM_READWRITE));

  gobject_class->set_property = gst_wsinc_set_property;
  gobject_class->get_property = gst_wsinc_get_property;
}

static void
gst_wsinc_init (GstWSinc * filter)
{
  filter->sinkpad = gst_pad_new_from_template (gst_filter_sink_factory (), "sink");
  gst_pad_set_chain_function (filter->sinkpad, gst_wsinc_chain);
  gst_pad_set_connect_function (filter->sinkpad, gst_wsinc_sink_connect);
  gst_element_add_pad (GST_ELEMENT (filter), filter->sinkpad);

  filter->srcpad = gst_pad_new_from_template (gst_filter_src_factory (), "src");
  gst_element_add_pad (GST_ELEMENT (filter), filter->srcpad);

  filter->wing_size = 50;
  filter->frequency = 0.25;
  filter->kernel = NULL;
}

static GstPadConnectReturn
gst_wsinc_sink_connect (GstPad * pad, GstCaps * caps)
{
  int i = 0;
  double sum = 0.0;
  int len = 0;
  GstWSinc *filter = GST_WSINC (gst_pad_get_parent (pad));

  g_assert (GST_IS_PAD (pad));
  g_assert (caps != NULL);

  if (!GST_CAPS_IS_FIXED (caps))
    return GST_PAD_CONNECT_DELAYED;
    
  if (gst_pad_try_set_caps (filter->srcpad, caps)) 
  {
    /* connection works, so init the filter */
    /* FIXME: remember to free it */
    /* fill the kernel */
    g_print ("DEBUG: initing filter kernel\n");
    len = filter->wing_size;
    GST_DEBUG (GST_CAT_PLUGIN_INFO, 
	       "wsinc: initializing filter kernel of length %d", len * 2 + 1);
    filter->kernel = (double *) g_malloc (sizeof (double) * (2 * len + 1));

    for (i = 0; i <= len * 2; ++i)
    {
      if (i == len)
	filter->kernel[i] = 2 * M_PI * filter->frequency;
      else
	filter->kernel[i] = sin (2 * M_PI * filter->frequency * (i - len)) 
	                  / (i - len);
      /* windowing */
      filter->kernel[i] *= (0.54 - 0.46 * cos (M_PI * i / len));
    }

    /* normalize for unity gain at DC
     * FIXME: sure this is not supposed to be quadratic ? */
    for (i = 0; i <= len * 2; ++i) sum += filter->kernel[i];
    for (i = 0; i <= len * 2; ++i) filter->kernel[i] /= sum;

    /* set up the residue memory space */
    filter->residue = (gfloat *) g_malloc (sizeof (gfloat) * (len * 2 + 1));
    for (i = 0; i <= len * 2; ++i) filter->residue[i] = 0.0;

    return GST_PAD_CONNECT_OK;
  }

  return GST_PAD_CONNECT_REFUSED;
}

static void
gst_wsinc_chain (GstPad * pad, GstBuffer * buf)
{
  GstWSinc *filter;
  gfloat *src;
  gfloat *input;
  gint residue_samples;
  gint input_samples;
  gint total_samples;
  int i, j;

  filter = GST_WSINC (gst_pad_get_parent (pad));

  /* FIXME: out of laziness, we copy the left-over bit from last buffer
   * together with the incoming buffer to a new buffer to make the loop
   * easy; this could be a lot more optimized though
   * to make amends we keep the incoming buffer around and write our
   * output samples there */

  src = (gfloat *) GST_BUFFER_DATA (buf);
  residue_samples = filter->wing_size * 2 + 1;
  input_samples = GST_BUFFER_SIZE (buf) / sizeof (gfloat);
  total_samples = residue_samples + input_samples;

  input = (gfloat *) g_malloc (sizeof (gfloat) * total_samples);

  /* copy the left-over bit */
  memcpy (input, filter->residue, sizeof (gfloat) * residue_samples);

  /* copy the new buffer */
  memcpy (&input[residue_samples], src, sizeof (gfloat) * input_samples);
  /* copy the tail of the current input buffer to the residue */
  memcpy (filter->residue, &src[input_samples - residue_samples],
          sizeof (gfloat) * residue_samples);

  /* convolution */
  /* since we copied the previous set of samples we needed before the actual
   * input data, we need to add the filter length to our indices for input */
  for (i = 0; i < input_samples; ++i)
  {
    src[i] = 0.0;
    for (j = 0; j < residue_samples; ++j)
      src[i] += input[i - j + residue_samples] * filter->kernel[j];
  }
  
  g_free (input);
  gst_pad_push (filter->srcpad, buf);
}

static void
gst_wsinc_set_property (GObject * object, guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstWSinc *filter;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_WSINC (object));

  filter = GST_WSINC (object);

  switch (prop_id) {
    case ARG_LENGTH:
     filter->wing_size = g_value_get_int (value);
     break; 
    case ARG_FREQUENCY:
     filter->frequency = g_value_get_double (value);
     break; 
    default:
      break;
  }
}

static void
gst_wsinc_get_property (GObject * object, guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstWSinc *filter;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_WSINC (object));
  
  filter = GST_WSINC (object);

  switch (prop_id) {
    case ARG_LENGTH:
      g_value_set_int (value, filter->wing_size);
      break;
    case ARG_FREQUENCY:
      g_value_set_double (value, filter->frequency);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
} 

