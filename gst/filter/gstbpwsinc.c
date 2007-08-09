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
 * TODO:  - Implement the convolution in place, probably only makes sense
 *          when using FFT convolution as currently the convolution itself
 *          is probably the bottleneck
 *        - Implement a band reject mode (spectral inversion)
 *        - Allow choosing between different windows (blackman, hanning, ...)
 *        - Specify filter length instead of 2*N+1
 * FIXME: - Doesn't work at all with >1 channels
 *        - Is bandreject, not bandpass
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <math.h>
#include <gst/gst.h>
#include <gst/audio/gstaudiofilter.h>
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

#define ALLOWED_CAPS \
    "audio/x-raw-float,"                                              \
    " width = (int) 32, "                                             \
    " endianness = (int) BYTE_ORDER,"                                 \
    " rate = (int) [ 1, MAX ],"                                       \
    " channels = (int) [ 1, MAX ]"

#define DEBUG_INIT(bla) \
  GST_DEBUG_CATEGORY_INIT (gst_bpwsinc_debug, "bpwsinc", 0, "Band-pass Windowed sinc filter plugin");

GST_BOILERPLATE_FULL (GstBPWSinc, gst_bpwsinc, GstAudioFilter,
    GST_TYPE_AUDIO_FILTER, DEBUG_INIT);

static void bpwsinc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void bpwsinc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstFlowReturn bpwsinc_transform_ip (GstBaseTransform * base,
    GstBuffer * outbuf);
static gboolean bpwsinc_setup (GstAudioFilter * base,
    GstRingBufferSpec * format);

/* Element class */

static void
gst_bpwsinc_dispose (GObject * object)
{
  GstBPWSinc *self = GST_BPWSINC (object);

  if (self->residue) {
    g_free (self->residue);
    self->residue = NULL;
  }

  if (self->kernel) {
    g_free (self->kernel);
    self->kernel = NULL;
  }

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_bpwsinc_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);
  GstCaps *caps;

  gst_element_class_set_details (element_class, &bpwsinc_details);

  caps = gst_caps_from_string (ALLOWED_CAPS);
  gst_audio_filter_class_add_pad_templates (GST_AUDIO_FILTER_CLASS (g_class),
      caps);
  gst_caps_unref (caps);
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
  GST_AUDIO_FILTER_CLASS (klass)->setup = GST_DEBUG_FUNCPTR (bpwsinc_setup);
}

static void
gst_bpwsinc_init (GstBPWSinc * self, GstBPWSincClass * g_class)
{
  self->wing_size = 50;
  self->lower_frequency = 0.25;
  self->upper_frequency = 0.3;
  self->kernel = NULL;
  self->residue = NULL;
}


/* GstAudioFilter vmethod implementations */

/* get notified of caps and plug in the correct process function */
static gboolean
bpwsinc_setup (GstAudioFilter * base, GstRingBufferSpec * format)
{
  int i = 0;
  double sum = 0.0;
  int len = 0;
  double *kernel_lp, *kernel_hp;
  GstBPWSinc *self = GST_BPWSINC (base);

  len = self->wing_size;
  /* fill the lp kernel
   * FIXME: refactor to own function, this is not caps related
   */
  GST_DEBUG ("bpwsinc: initializing LP kernel of length %d with cut-off %f",
      len * 2 + 1, self->lower_frequency);
  kernel_lp = (double *) g_malloc (sizeof (double) * (2 * len + 1));
  for (i = 0; i <= len * 2; ++i) {
    if (i == len)
      kernel_lp[i] = 2 * M_PI * self->lower_frequency;
    else
      kernel_lp[i] = sin (2 * M_PI * self->lower_frequency * (i - len))
          / (i - len);
    /* Blackman windowing */
    kernel_lp[i] *= (0.42 - 0.5 * cos (M_PI * i / len)
        + 0.08 * cos (2 * M_PI * i / len));
  }

  /* normalize for unity gain at DC */
  sum = 0.0;
  for (i = 0; i <= len * 2; ++i)
    sum += kernel_lp[i];
  for (i = 0; i <= len * 2; ++i)
    kernel_lp[i] /= sum;

  /* fill the hp kernel */
  GST_DEBUG ("bpwsinc: initializing HP kernel of length %d with cut-off %f",
      len * 2 + 1, self->upper_frequency);
  kernel_hp = (double *) g_malloc (sizeof (double) * (2 * len + 1));
  for (i = 0; i <= len * 2; ++i) {
    if (i == len)
      kernel_hp[i] = 2 * M_PI * self->upper_frequency;
    else
      kernel_hp[i] = sin (2 * M_PI * self->upper_frequency * (i - len))
          / (i - len);
    /* Blackman windowing */
    kernel_hp[i] *= (0.42 - 0.5 * cos (M_PI * i / len)
        + 0.08 * cos (2 * M_PI * i / len));
  }

  /* normalize for unity gain at DC */
  sum = 0.0;
  for (i = 0; i <= len * 2; ++i)
    sum += kernel_hp[i];
  for (i = 0; i <= len * 2; ++i)
    kernel_hp[i] /= sum;

  /* combine the two kernels */
  if (self->kernel)
    g_free (self->kernel);
  self->kernel = (double *) g_malloc (sizeof (double) * (2 * len + 1));

  for (i = 0; i <= len * 2; ++i)
    self->kernel[i] = kernel_lp[i] + kernel_hp[i];

  /* do spectral inversion to go from band reject to bandpass */
  for (i = 0; i <= len * 2; ++i)
    self->kernel[i] = -self->kernel[i];
  self->kernel[len] += 1;

  /* free the helper kernels */
  g_free (kernel_lp);
  g_free (kernel_hp);

  /* set up the residue memory space */
  if (self->residue)
    g_free (self->residue);

  self->residue = (gfloat *) g_malloc (sizeof (gfloat) * (len * 2 + 1));
  for (i = 0; i <= len * 2; ++i)
    self->residue[i] = 0.0;

  return TRUE;
}

/* GstBaseTransform vmethod implementations */

static GstFlowReturn
bpwsinc_transform_ip (GstBaseTransform * base, GstBuffer * outbuf)
{
  GstBPWSinc *self = GST_BPWSINC (base);
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
    gst_object_sync_values (G_OBJECT (self), timestamp);

  /* FIXME: out of laziness, we copy the left-over bit from last buffer
   * together with the incoming buffer to a new buffer to make the loop
   * easy; self could be a lot more optimized though
   * to make amends we keep the incoming buffer around and write our
   * output samples there */

  src = (gfloat *) GST_BUFFER_DATA (outbuf);
  residue_samples = self->wing_size * 2 + 1;
  input_samples = GST_BUFFER_SIZE (outbuf) / sizeof (gfloat);
  total_samples = residue_samples + input_samples;

  input = (gfloat *) g_malloc (sizeof (gfloat) * total_samples);

  /* copy the left-over bit */
  memcpy (input, self->residue, sizeof (gfloat) * residue_samples);

  /* copy the new buffer */
  memcpy (&input[residue_samples], src, sizeof (gfloat) * input_samples);
  /* copy the tail of the current input buffer to the residue */
  memcpy (self->residue, &src[input_samples - residue_samples],
      sizeof (gfloat) * residue_samples);

  /* convolution */
  /* since we copied the previous set of samples we needed before the actual
   * input data, we need to add the filter length to our indices for input */
  for (i = 0; i < input_samples; ++i) {
    src[i] = 0.0;
    for (j = 0; j < residue_samples; ++j)
      src[i] += input[i - j + residue_samples] * self->kernel[j];
  }

  g_free (input);

  return GST_FLOW_OK;
}

static void
bpwsinc_set_property (GObject * object, guint prop_id, const GValue * value,
    GParamSpec * pspec)
{
  GstBPWSinc *self = GST_BPWSINC (object);

  g_return_if_fail (GST_IS_BPWSINC (self));

  switch (prop_id) {
    case PROP_LENGTH:
      self->wing_size = g_value_get_int (value);
      break;
    case PROP_LOWER_FREQUENCY:
      self->lower_frequency = g_value_get_double (value);
      break;
    case PROP_UPPER_FREQUENCY:
      self->upper_frequency = g_value_get_double (value);
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
  GstBPWSinc *self = GST_BPWSINC (object);

  switch (prop_id) {
    case PROP_LENGTH:
      g_value_set_int (value, self->wing_size);
      break;
    case PROP_LOWER_FREQUENCY:
      g_value_set_double (value, self->lower_frequency);
      break;
    case PROP_UPPER_FREQUENCY:
      g_value_set_double (value, self->upper_frequency);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}
