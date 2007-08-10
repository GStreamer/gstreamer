/* -*- c-basic-offset: 2 -*-
 * 
 * GStreamer
 * Copyright (C) 1999-2001 Erik Walthinsen <omega@cse.ogi.edu>
 *               2006 Dreamlab Technologies Ltd. <mathis.hofer@dreamlab.net>
 *               2007 Sebastian Dröge <slomo@circular-chaos.org>
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
 *        - Maybe allow cascading the filter to get a better stopband attenuation.
 *          Can be done by convolving a filter kernel with itself
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
    "Dreamlab Technologies Ltd. <mathis.hofer@dreamlab.net>, "
    "Sebastian Dröge <slomo@circular-chaos.org>");

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
  PROP_UPPER_FREQUENCY,
  PROP_MODE,
  PROP_WINDOW
};

enum
{
  MODE_BAND_PASS = 0,
  MODE_BAND_REJECT
};

#define GST_TYPE_BPWSINC_MODE (gst_bpwsinc_mode_get_type ())
static GType
gst_bpwsinc_mode_get_type (void)
{
  static GType gtype = 0;

  if (gtype == 0) {
    static const GEnumValue values[] = {
      {MODE_BAND_PASS, "Band pass (default)",
          "band-pass"},
      {MODE_BAND_REJECT, "Band reject",
          "band-reject"},
      {0, NULL, NULL}
    };

    gtype = g_enum_register_static ("GstBPWSincMode", values);
  }
  return gtype;
}

enum
{
  WINDOW_HAMMING = 0,
  WINDOW_BLACKMAN
};

#define GST_TYPE_BPWSINC_WINDOW (gst_bpwsinc_window_get_type ())
static GType
gst_bpwsinc_window_get_type (void)
{
  static GType gtype = 0;

  if (gtype == 0) {
    static const GEnumValue values[] = {
      {WINDOW_HAMMING, "Hamming window (default)",
          "hamming"},
      {WINDOW_BLACKMAN, "Blackman window",
          "blackman"},
      {0, NULL, NULL}
    };

    gtype = g_enum_register_static ("GstBPWSincWindow", values);
  }
  return gtype;
}

#define ALLOWED_CAPS \
    "audio/x-raw-float, "                                             \
    " width = (int) { 32, 64 }, "                                     \
    " endianness = (int) BYTE_ORDER, "                                \
    " rate = (int) [ 1, MAX ], "                                      \
    " channels = (int) [ 1, MAX ] "

#define DEBUG_INIT(bla) \
  GST_DEBUG_CATEGORY_INIT (gst_bpwsinc_debug, "bpwsinc", 0, "Band-pass Windowed sinc filter plugin");

GST_BOILERPLATE_FULL (GstBPWSinc, gst_bpwsinc, GstAudioFilter,
    GST_TYPE_AUDIO_FILTER, DEBUG_INIT);

static void bpwsinc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void bpwsinc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstFlowReturn bpwsinc_transform (GstBaseTransform * base,
    GstBuffer * inbuf, GstBuffer * outbuf);
static gboolean bpwsinc_get_unit_size (GstBaseTransform * base, GstCaps * caps,
    guint * size);
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
          "Cut-off lower frequency (Hz)",
          0.0, G_MAXDOUBLE, 0, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_UPPER_FREQUENCY,
      g_param_spec_double ("upper-frequency", "Upper Frequency",
          "Cut-off upper frequency (Hz)",
          0.0, G_MAXDOUBLE, 0, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_LENGTH,
      g_param_spec_int ("length", "Length",
          "Filter kernel length, will be rounded to the next odd number",
          3, G_MAXINT, 101, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_MODE,
      g_param_spec_enum ("mode", "Mode",
          "Band pass or band reject mode", GST_TYPE_BPWSINC_MODE,
          MODE_BAND_PASS, G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE));

  g_object_class_install_property (gobject_class, PROP_WINDOW,
      g_param_spec_enum ("window", "Window",
          "Window function to use", GST_TYPE_BPWSINC_WINDOW,
          WINDOW_HAMMING, G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE));

  trans_class->transform = GST_DEBUG_FUNCPTR (bpwsinc_transform);
  trans_class->get_unit_size = GST_DEBUG_FUNCPTR (bpwsinc_get_unit_size);
  GST_AUDIO_FILTER_CLASS (klass)->setup = GST_DEBUG_FUNCPTR (bpwsinc_setup);
}

static void
gst_bpwsinc_init (GstBPWSinc * self, GstBPWSincClass * g_class)
{
  self->kernel_length = 101;
  self->lower_frequency = 0.0;
  self->upper_frequency = 0.0;
  self->mode = MODE_BAND_PASS;
  self->window = WINDOW_HAMMING;
  self->kernel = NULL;
  self->have_kernel = FALSE;
  self->residue = NULL;
}

static void
process_32 (GstBPWSinc * self, gfloat * src, gfloat * dst, guint input_samples)
{
  gint kernel_length = self->kernel_length;
  gint i, j, k, l;
  gint channels = GST_AUDIO_FILTER (self)->format.channels;

  /* convolution */
  for (i = 0; i < input_samples; i++) {
    dst[i] = 0.0;
    k = i % channels;
    l = i / channels;
    for (j = 0; j < kernel_length; j++)
      if (l < j)
        dst[i] +=
            self->residue[(kernel_length + l - j) * channels +
            k] * self->kernel[j];
      else
        dst[i] += src[(l - j) * channels + k] * self->kernel[j];
  }

  /* copy the tail of the current input buffer to the residue */
  for (i = 0; i < kernel_length * channels; i++)
    self->residue[i] = src[input_samples - kernel_length * channels + i];
}

static void
process_64 (GstBPWSinc * self, gdouble * src, gdouble * dst,
    guint input_samples)
{
  gint kernel_length = self->kernel_length;
  gint i, j, k, l;
  gint channels = GST_AUDIO_FILTER (self)->format.channels;

  /* convolution */
  for (i = 0; i < input_samples; i++) {
    dst[i] = 0.0;
    k = i % channels;
    l = i / channels;
    for (j = 0; j < kernel_length; j++)
      if (l < j)
        dst[i] +=
            self->residue[(kernel_length + l - j) * channels +
            k] * self->kernel[j];
      else
        dst[i] += src[(l - j) * channels + k] * self->kernel[j];
  }

  /* copy the tail of the current input buffer to the residue */
  for (i = 0; i < kernel_length * channels; i++)
    self->residue[i] = src[input_samples - kernel_length * channels + i];
}

static void
bpwsinc_build_kernel (GstBPWSinc * self)
{
  gint i = 0;
  gdouble sum = 0.0;
  gint len = 0;
  gdouble *kernel_lp, *kernel_hp;
  gdouble w;

  len = self->kernel_length;

  if (GST_AUDIO_FILTER (self)->format.rate == 0) {
    GST_DEBUG ("rate not set yet");
    return;
  }

  if (GST_AUDIO_FILTER (self)->format.channels == 0) {
    GST_DEBUG ("channels not set yet");
    return;
  }

  /* Clamp frequencies */
  self->lower_frequency =
      CLAMP (self->lower_frequency, 0.0,
      GST_AUDIO_FILTER (self)->format.rate / 2);
  self->upper_frequency =
      CLAMP (self->upper_frequency, 0.0,
      GST_AUDIO_FILTER (self)->format.rate / 2);
  if (self->lower_frequency > self->upper_frequency) {
    gint tmp = self->lower_frequency;

    self->lower_frequency = self->upper_frequency;
    self->upper_frequency = tmp;
  }

  /* fill the lp kernel */
  GST_DEBUG ("bpwsinc: initializing LP kernel of length %d with cut-off %f",
      len, self->lower_frequency);

  w = 2 * M_PI * (self->lower_frequency / GST_AUDIO_FILTER (self)->format.rate);
  kernel_lp = g_new (gdouble, len);
  for (i = 0; i < len; ++i) {
    if (i == len / 2)
      kernel_lp[i] = w;
    else
      kernel_lp[i] = sin (w * (i - len / 2))
          / (i - len / 2);
    /* Windowing */
    if (self->window == WINDOW_HAMMING)
      kernel_lp[i] *= (0.54 - 0.46 * cos (2 * M_PI * i / len));
    else
      kernel_lp[i] *=
          (0.42 - 0.5 * cos (2 * M_PI * i / len) +
          0.08 * cos (4 * M_PI * i / len));
  }

  /* normalize for unity gain at DC */
  sum = 0.0;
  for (i = 0; i < len; ++i)
    sum += kernel_lp[i];
  for (i = 0; i < len; ++i)
    kernel_lp[i] /= sum;

  /* fill the hp kernel */
  GST_DEBUG ("bpwsinc: initializing HP kernel of length %d with cut-off %f",
      len, self->upper_frequency);

  w = 2 * M_PI * (self->upper_frequency / GST_AUDIO_FILTER (self)->format.rate);
  kernel_hp = g_new (gdouble, len);
  for (i = 0; i < len; ++i) {
    if (i == len / 2)
      kernel_hp[i] = w;
    else
      kernel_hp[i] = sin (w * (i - len / 2))
          / (i - len / 2);
    /* Windowing */
    if (self->window == WINDOW_HAMMING)
      kernel_hp[i] *= (0.54 - 0.46 * cos (2 * M_PI * i / len));
    else
      kernel_hp[i] *=
          (0.42 - 0.5 * cos (2 * M_PI * i / len) +
          0.08 * cos (4 * M_PI * i / len));
  }

  /* normalize for unity gain at DC */
  sum = 0.0;
  for (i = 0; i < len; ++i)
    sum += kernel_hp[i];
  for (i = 0; i < len; ++i)
    kernel_hp[i] /= sum;

  /* do spectral inversion to go from lowpass to highpass */
  for (i = 0; i < len; ++i)
    kernel_hp[i] = -kernel_hp[i];
  kernel_hp[len / 2] += 1;

  /* combine the two kernels */
  if (self->kernel)
    g_free (self->kernel);
  self->kernel = g_new (gdouble, len);

  for (i = 0; i < len; ++i)
    self->kernel[i] = kernel_lp[i] + kernel_hp[i];

  /* free the helper kernels */
  g_free (kernel_lp);
  g_free (kernel_hp);

  /* do spectral inversion to go from bandreject to bandpass
   * if specified */
  if (self->mode == MODE_BAND_PASS) {
    for (i = 0; i < len; ++i)
      self->kernel[i] = -self->kernel[i];
    self->kernel[len / 2] += 1;
  }

  /* set up the residue memory space */
  if (self->residue)
    g_free (self->residue);

  self->residue = g_new0 (gdouble, len);
}

/* GstAudioFilter vmethod implementations */

/* get notified of caps and plug in the correct process function */
static gboolean
bpwsinc_setup (GstAudioFilter * base, GstRingBufferSpec * format)
{
  GstBPWSinc *self = GST_BPWSINC (base);

  gboolean ret = TRUE;

  if (format->width == 32)
    self->process = (GstBPWSincProcessFunc) process_32;
  else if (format->width == 64)
    self->process = (GstBPWSincProcessFunc) process_64;
  else
    ret = FALSE;

  self->have_kernel = FALSE;

  return TRUE;
}

/* GstBaseTransform vmethod implementations */

static gboolean
bpwsinc_get_unit_size (GstBaseTransform * base, GstCaps * caps, guint * size)
{
  gint width, channels;
  GstStructure *structure;
  gboolean ret;

  g_assert (size);

  structure = gst_caps_get_structure (caps, 0);
  ret = gst_structure_get_int (structure, "width", &width);
  ret &= gst_structure_get_int (structure, "channels", &channels);

  *size = width * channels / 8;

  return ret;
}

static GstFlowReturn
bpwsinc_transform (GstBaseTransform * base, GstBuffer * inbuf,
    GstBuffer * outbuf)
{
  GstBPWSinc *self = GST_BPWSINC (base);
  GstClockTime timestamp;
  gint input_samples =
      GST_BUFFER_SIZE (outbuf) / (GST_AUDIO_FILTER (self)->format.width / 8);

  /* don't process data in passthrough-mode */
  if (gst_base_transform_is_passthrough (base))
    return GST_FLOW_OK;

  /* FIXME: subdivide GST_BUFFER_SIZE into small chunks for smooth fades */
  timestamp = GST_BUFFER_TIMESTAMP (outbuf);

  if (GST_CLOCK_TIME_IS_VALID (timestamp))
    gst_object_sync_values (G_OBJECT (self), timestamp);

  if (!self->have_kernel)
    bpwsinc_build_kernel (self);

  self->process (self, GST_BUFFER_DATA (inbuf), GST_BUFFER_DATA (outbuf),
      input_samples);

  return GST_FLOW_OK;
}

static void
bpwsinc_set_property (GObject * object, guint prop_id, const GValue * value,
    GParamSpec * pspec)
{
  GstBPWSinc *self = GST_BPWSINC (object);

  g_return_if_fail (GST_IS_BPWSINC (self));

  switch (prop_id) {
    case PROP_LENGTH:{
      gint val;

      GST_BASE_TRANSFORM_LOCK (self);
      val = g_value_get_int (value);
      if (val % 2 == 0)
        val++;
      self->kernel_length = val;
      bpwsinc_build_kernel (self);
      GST_BASE_TRANSFORM_UNLOCK (self);
      break;
    }
    case PROP_LOWER_FREQUENCY:
      GST_BASE_TRANSFORM_LOCK (self);
      self->lower_frequency = g_value_get_double (value);
      bpwsinc_build_kernel (self);
      GST_BASE_TRANSFORM_UNLOCK (self);
      break;
    case PROP_UPPER_FREQUENCY:
      GST_BASE_TRANSFORM_LOCK (self);
      self->upper_frequency = g_value_get_double (value);
      bpwsinc_build_kernel (self);
      GST_BASE_TRANSFORM_UNLOCK (self);
      break;
    case PROP_MODE:
      GST_BASE_TRANSFORM_LOCK (self);
      self->mode = g_value_get_enum (value);
      bpwsinc_build_kernel (self);
      GST_BASE_TRANSFORM_UNLOCK (self);
      break;
    case PROP_WINDOW:
      GST_BASE_TRANSFORM_LOCK (self);
      self->window = g_value_get_enum (value);
      bpwsinc_build_kernel (self);
      GST_BASE_TRANSFORM_UNLOCK (self);
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
      g_value_set_int (value, self->kernel_length);
      break;
    case PROP_LOWER_FREQUENCY:
      g_value_set_double (value, self->lower_frequency);
      break;
    case PROP_UPPER_FREQUENCY:
      g_value_set_double (value, self->upper_frequency);
      break;
    case PROP_MODE:
      g_value_set_enum (value, self->mode);
      break;
    case PROP_WINDOW:
      g_value_set_enum (value, self->window);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}
