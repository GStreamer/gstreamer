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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <gst/gst.h>
#include "gstfilter.h"
#include <math.h>               /* M_PI */
#include <string.h>             /* memmove */

static GstElementDetails gst_lpwsinc_details = GST_ELEMENT_DETAILS ("LPWSinc",
    "Filter/Effect/Audio",
    "Low-pass Windowed sinc filter",
    "Thomas <thomas@apestaart.org>, " "Steven W. Smith");

enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_LENGTH,
  ARG_FREQUENCY
};

#define GST_TYPE_LPWSINC \
  (gst_lpwsinc_get_type())
#define GST_LPWSINC(obj) \
      (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_LPWSINC,GstLPWSinc))
#define GST_LPWSINC_CLASS(klass) \
      (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_ULAW,GstLPWSinc))
#define GST_IS_LPWSINC(obj) \
      (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_LPWSINC))
#define GST_IS_LPWSINC_CLASS(obj) \
      (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_LPWSINC))

typedef struct _GstLPWSinc GstLPWSinc;
typedef struct _GstLPWSincClass GstLPWSincClass;

struct _GstLPWSinc
{
  GstElement element;

  GstPad *sinkpad, *srcpad;

  double frequency;
  int wing_size;                /* length of a "wing" of the filter; 
                                   actual length is 2 * wing_size + 1 */

  gfloat *residue;              /* buffer for left-over samples from previous buffer */
  double *kernel;
};

struct _GstLPWSincClass
{
  GstElementClass parent_class;
};

static void gst_lpwsinc_base_init (gpointer g_class);
static void gst_lpwsinc_class_init (GstLPWSincClass * klass);
static void gst_lpwsinc_init (GstLPWSinc * filter);

static void gst_lpwsinc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_lpwsinc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static void gst_lpwsinc_chain (GstPad * pad, GstData * _data);
static GstPadLinkReturn
gst_lpwsinc_sink_connect (GstPad * pad, const GstCaps * caps);

static GstElementClass *parent_class = NULL;

/*static guint gst_lpwsinc_signals[LAST_SIGNAL] = { 0 }; */

GType
gst_lpwsinc_get_type (void)
{
  static GType lpwsinc_type = 0;

  if (!lpwsinc_type) {
    static const GTypeInfo lpwsinc_info = {
      sizeof (GstLPWSincClass),
      gst_lpwsinc_base_init,
      NULL,
      (GClassInitFunc) gst_lpwsinc_class_init, NULL, NULL,
      sizeof (GstLPWSinc), 0,
      (GInstanceInitFunc) gst_lpwsinc_init,
    };

    lpwsinc_type = g_type_register_static (GST_TYPE_ELEMENT, "GstLPWSinc",
        &lpwsinc_info, 0);
  }
  return lpwsinc_type;
}

static void
gst_lpwsinc_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  /* register src pads */
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_filter_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_filter_sink_template));

  gst_element_class_set_details (element_class, &gst_lpwsinc_details);
}

static void
gst_lpwsinc_class_init (GstLPWSincClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_FREQUENCY,
      g_param_spec_double ("frequency", "Frequency",
          "Cut-off Frequency relative to sample rate)",
          0.0, 0.5, 0, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_LENGTH,
      g_param_spec_int ("length", "Length",
          "N such that the filter length = 2N + 1",
          1, G_MAXINT, 1, G_PARAM_READWRITE));

  gobject_class->set_property = gst_lpwsinc_set_property;
  gobject_class->get_property = gst_lpwsinc_get_property;
}

static void
gst_lpwsinc_init (GstLPWSinc * filter)
{
  filter->sinkpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&gst_filter_sink_template), "sink");
  gst_pad_set_chain_function (filter->sinkpad, gst_lpwsinc_chain);
  gst_pad_set_link_function (filter->sinkpad, gst_lpwsinc_sink_connect);
  gst_element_add_pad (GST_ELEMENT (filter), filter->sinkpad);

  filter->srcpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&gst_filter_src_template), "src");
  gst_element_add_pad (GST_ELEMENT (filter), filter->srcpad);

  filter->wing_size = 50;
  filter->frequency = 0.25;
  filter->kernel = NULL;
}

static GstPadLinkReturn
gst_lpwsinc_sink_connect (GstPad * pad, const GstCaps * caps)
{
  int i = 0;
  double sum = 0.0;
  int len = 0;
  GstLPWSinc *filter = GST_LPWSINC (gst_pad_get_parent (pad));
  GstPadLinkReturn set_retval;

  g_assert (GST_IS_PAD (pad));
  g_assert (caps != NULL);

  set_retval = gst_pad_try_set_caps (filter->srcpad, caps);

  if (set_retval > 0) {
    /* connection works, so init the filter */
    /* FIXME: remember to free it */
    /* fill the kernel */
    g_print ("DEBUG: initing filter kernel\n");
    len = filter->wing_size;
    GST_DEBUG ("lpwsinc: initializing filter kernel of length %d", len * 2 + 1);
    filter->kernel = (double *) g_malloc (sizeof (double) * (2 * len + 1));

    for (i = 0; i <= len * 2; ++i) {
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
    for (i = 0; i <= len * 2; ++i)
      sum += filter->kernel[i];
    for (i = 0; i <= len * 2; ++i)
      filter->kernel[i] /= sum;

    /* set up the residue memory space */
    filter->residue = (gfloat *) g_malloc (sizeof (gfloat) * (len * 2 + 1));
    for (i = 0; i <= len * 2; ++i)
      filter->residue[i] = 0.0;
  }

  return set_retval;
}

static void
gst_lpwsinc_chain (GstPad * pad, GstData * _data)
{
  GstBuffer *buf = GST_BUFFER (_data);
  GstLPWSinc *filter;
  gfloat *src;
  gfloat *input;
  gint residue_samples;
  gint input_samples;
  gint total_samples;
  int i, j;

  filter = GST_LPWSINC (gst_pad_get_parent (pad));

  /* FIXME: out of laziness, we copy the left-over bit from last buffer
   * together with the incoming buffer to a new buffer to make the loop
   * easy; this could be a lot more optimized though
   * to make amends we keep the incoming buffer around and write our
   * output samples there */

  /* get a writable buffer */
  buf = gst_buffer_copy_on_write (buf);

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
  for (i = 0; i < input_samples; ++i) {
    src[i] = 0.0;
    for (j = 0; j < residue_samples; ++j)
      src[i] += input[i - j + residue_samples] * filter->kernel[j];
  }

  g_free (input);
  gst_pad_push (filter->srcpad, GST_DATA (buf));
}

static void
gst_lpwsinc_set_property (GObject * object, guint prop_id, const GValue * value,
    GParamSpec * pspec)
{
  GstLPWSinc *filter;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_LPWSINC (object));

  filter = GST_LPWSINC (object);

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
gst_lpwsinc_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstLPWSinc *filter;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_LPWSINC (object));

  filter = GST_LPWSINC (object);

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
