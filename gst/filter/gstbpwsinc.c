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
 * 
 * 
 * this windowed sinc filter is taken from the freely downloadable DSP book,
 * "The Scientist and Engineer's Guide to Digital Signal Processing",
 * chapter 16
 * available at http://www.dspguide.com/
 *
 * FIXME:
 * - this filter is totally unoptimized !
 * - we do not destroy the allocated memory for filters and residue
 * - this might be improved upon with bytestream
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <math.h>
#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>
#include <gst/controller/gstcontroller.h>

#include "gstbpwsinc.h"

#define GST_CAT_DEFAULT gst_bpwsinc_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

static const GstElementDetails bpwsinc_details =
GST_ELEMENT_DETAILS ("Band-pass Windowed sinc filter",
    "Filter/Effect/Audio",
    "Band-pass Windowed sinc filter",
    "Thomas <thomas@apestaart.org>, "
    "Steven W. Smith, "
    "Dreamlab Technologies Ltd. <mathis.hofer@dreamlab.net>");

/* Filter signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_LENGTH,
  PROP_LOWER_FREQUENCY,
  PROP_UPPER_FREQUENCY
};

static GstStaticPadTemplate bpwsinc_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-float, "
        "rate = (int) [ 1, MAX ], "
        "channels = (int) [ 1, MAX ], "
        "endianness = (int) BYTE_ORDER, " "width = (int) 32")
    );

static GstStaticPadTemplate bpwsinc_src_template = GST_STATIC_PAD_TEMPLATE
    ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-float, "
        "rate = (int) [ 1, MAX ], "
        "channels = (int) [ 1, MAX ], "
        "endianness = (int) BYTE_ORDER, " "width = (int) 32")
    );

#define DEBUG_INIT(bla) \
  GST_DEBUG_CATEGORY_INIT (gst_bpwsinc_debug, "bpwsinc", 0, "Band-pass Windowed sinc filter plugin");

GST_BOILERPLATE_FULL (GstBPWSinc, gst_bpwsinc, GstBaseTransform,
    GST_TYPE_BASE_TRANSFORM, DEBUG_INIT);

static void bpwsinc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void bpwsinc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstFlowReturn bpwsinc_transform_ip (GstBaseTransform * base,
    GstBuffer * outbuf);
static gboolean bpwsinc_set_caps (GstBaseTransform * base, GstCaps * incaps,
    GstCaps * outcaps);

/* Element class */

static void
gst_bpwsinc_dispose (GObject * object)
{
  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_bpwsinc_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&bpwsinc_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&bpwsinc_sink_template));
  gst_element_class_set_details (element_class, &bpwsinc_details);
}

static void
gst_bpwsinc_class_init (GstBPWSincClass * klass)
{
  GObjectClass *gobject_class;
  GstBaseTransformClass *trans_class;

  gobject_class = (GObjectClass *) klass;
  trans_class = (GstBaseTransformClass *) klass;

  gobject_class->set_property = bpwsinc_set_property;
  gobject_class->get_property = bpwsinc_get_property;
  gobject_class->dispose = gst_bpwsinc_dispose;

  g_object_class_install_property (gobject_class, PROP_LOWER_FREQUENCY,
      g_param_spec_double ("lower-frequency", "Lower Frequency",
          "Cut-off lower frequency (relative to sample rate)",
          0.0, 0.5, 0, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_UPPER_FREQUENCY,
      g_param_spec_double ("upper-frequency", "Upper Frequency",
          "Cut-off upper frequency (relative to sample rate)",
          0.0, 0.5, 0, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_LENGTH,
      g_param_spec_int ("length", "Length",
          "N such that the filter length = 2N + 1",
          1, G_MAXINT, 1, G_PARAM_READWRITE));

  trans_class->transform_ip = GST_DEBUG_FUNCPTR (bpwsinc_transform_ip);
  trans_class->set_caps = GST_DEBUG_FUNCPTR (bpwsinc_set_caps);
}

static void
gst_bpwsinc_init (GstBPWSinc * this, GstBPWSincClass * g_class)
{
  this->wing_size = 50;
  this->lower_frequency = 0.25;
  this->upper_frequency = 0.3;
  this->kernel = NULL;
}


/* GstBaseTransform vmethod implementations */

/* get notified of caps and plug in the correct process function */
static gboolean
bpwsinc_set_caps (GstBaseTransform * base, GstCaps * incaps, GstCaps * outcaps)
{
  int i = 0;
  double sum = 0.0;
  int len = 0;
  double *kernel_lp, *kernel_hp;
  GstBPWSinc *this = GST_BPWSINC (base);

  GST_DEBUG_OBJECT (this,
      "set_caps: in %" GST_PTR_FORMAT " out %" GST_PTR_FORMAT, incaps, outcaps);

  len = this->wing_size;
  /* fill the lp kernel */
  GST_DEBUG ("bpwsinc: initializing LP kernel of length %d with cut-off %f",
      len * 2 + 1, this->lower_frequency);
  kernel_lp = (double *) g_malloc (sizeof (double) * (2 * len + 1));
  for (i = 0; i <= len * 2; ++i) {
    if (i == len)
      kernel_lp[i] = 2 * M_PI * this->lower_frequency;
    else
      kernel_lp[i] = sin (2 * M_PI * this->lower_frequency * (i - len))
          / (i - len);
    /* Blackman windowing */
    kernel_lp[i] *= (0.42 - 0.5 * cos (M_PI * i / len)
        + 0.08 * cos (2 * M_PI * i / len));
  }

  /* normalize for unity gain at DC
   * FIXME: sure this is not supposed to be quadratic ? */
  sum = 0.0;
  for (i = 0; i <= len * 2; ++i)
    sum += kernel_lp[i];
  for (i = 0; i <= len * 2; ++i)
    kernel_lp[i] /= sum;

  /* fill the hp kernel */
  GST_DEBUG ("bpwsinc: initializing HP kernel of length %d with cut-off %f",
      len * 2 + 1, this->upper_frequency);
  kernel_hp = (double *) g_malloc (sizeof (double) * (2 * len + 1));
  for (i = 0; i <= len * 2; ++i) {
    if (i == len)
      kernel_hp[i] = 2 * M_PI * this->upper_frequency;
    else
      kernel_hp[i] = sin (2 * M_PI * this->upper_frequency * (i - len))
          / (i - len);
    /* Blackman windowing */
    kernel_hp[i] *= (0.42 - 0.5 * cos (M_PI * i / len)
        + 0.08 * cos (2 * M_PI * i / len));
  }

  /* normalize for unity gain at DC
   * FIXME: sure this is not supposed to be quadratic ? */
  sum = 0.0;
  for (i = 0; i <= len * 2; ++i)
    sum += kernel_hp[i];
  for (i = 0; i <= len * 2; ++i)
    kernel_hp[i] /= sum;

  /* combine the two thiss */
  this->kernel = (double *) g_malloc (sizeof (double) * (2 * len + 1));

  for (i = 0; i <= len * 2; ++i)
    this->kernel[i] = kernel_lp[i] + kernel_hp[i];

  /* do spectral inversion to go from band reject to bandpass */
  for (i = 0; i <= len * 2; ++i)
    this->kernel[i] = -this->kernel[i];
  this->kernel[len] += 1;

  /* free the helper kernels */
  g_free (kernel_lp);
  g_free (kernel_hp);

  /* set up the residue memory space */
  this->residue = (gfloat *) g_malloc (sizeof (gfloat) * (len * 2 + 1));
  for (i = 0; i <= len * 2; ++i)
    this->residue[i] = 0.0;

  return TRUE;
}

static GstFlowReturn
bpwsinc_transform_ip (GstBaseTransform * base, GstBuffer * outbuf)
{
  GstBPWSinc *this = GST_BPWSINC (base);
  GstClockTime timestamp;

  gfloat *src;
  gfloat *input;
  int residue_samples;
  gint input_samples;
  gint total_samples;
  int i, j;

  /* don't process data in passthrough-mode */
  if (gst_base_transform_is_passthrough (base))
    return GST_FLOW_OK;

  /* FIXME: subdivide GST_BUFFER_SIZE into small chunks for smooth fades */
  timestamp = GST_BUFFER_TIMESTAMP (outbuf);

  if (GST_CLOCK_TIME_IS_VALID (timestamp))
    gst_object_sync_values (G_OBJECT (this), timestamp);

  /* FIXME: out of laziness, we copy the left-over bit from last buffer
   * together with the incoming buffer to a new buffer to make the loop
   * easy; this could be a lot more optimized though
   * to make amends we keep the incoming buffer around and write our
   * output samples there */

  src = (gfloat *) GST_BUFFER_DATA (outbuf);
  residue_samples = this->wing_size * 2 + 1;
  input_samples = GST_BUFFER_SIZE (outbuf) / sizeof (gfloat);
  total_samples = residue_samples + input_samples;

  input = (gfloat *) g_malloc (sizeof (gfloat) * total_samples);

  /* copy the left-over bit */
  memcpy (input, this->residue, sizeof (gfloat) * residue_samples);

  /* copy the new buffer */
  memcpy (&input[residue_samples], src, sizeof (gfloat) * input_samples);
  /* copy the tail of the current input buffer to the residue */
  memcpy (this->residue, &src[input_samples - residue_samples],
      sizeof (gfloat) * residue_samples);

  /* convolution */
  /* since we copied the previous set of samples we needed before the actual
   * input data, we need to add the filter length to our indices for input */
  for (i = 0; i < input_samples; ++i) {
    src[i] = 0.0;
    for (j = 0; j < residue_samples; ++j)
      src[i] += input[i - j + residue_samples] * this->kernel[j];
  }

  g_free (input);

  return GST_FLOW_OK;
}

static void
bpwsinc_set_property (GObject * object, guint prop_id, const GValue * value,
    GParamSpec * pspec)
{
  GstBPWSinc *this = GST_BPWSINC (object);

  g_return_if_fail (GST_IS_BPWSINC (this));

  switch (prop_id) {
    case PROP_LENGTH:
      this->wing_size = g_value_get_int (value);
      break;
    case PROP_LOWER_FREQUENCY:
      this->lower_frequency = g_value_get_double (value);
      break;
    case PROP_UPPER_FREQUENCY:
      this->upper_frequency = g_value_get_double (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
bpwsinc_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstBPWSinc *this = GST_BPWSINC (object);

  switch (prop_id) {
    case PROP_LENGTH:
      g_value_set_int (value, this->wing_size);
      break;
    case PROP_LOWER_FREQUENCY:
      g_value_set_double (value, this->lower_frequency);
      break;
    case PROP_UPPER_FREQUENCY:
      g_value_set_double (value, this->upper_frequency);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}
