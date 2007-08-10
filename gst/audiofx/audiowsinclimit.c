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
 *          is probably the bottleneck.
 *        - Maybe allow cascading the filter to get a better stopband attenuation.
 *          Can be done by convolving a filter kernel with itself.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <math.h>
#include <gst/gst.h>
#include <gst/audio/gstaudiofilter.h>
#include <gst/controller/gstcontroller.h>

#include "gstlpwsinc.h"

#define GST_CAT_DEFAULT gst_lpwsinc_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

static const GstElementDetails lpwsinc_details = GST_ELEMENT_DETAILS ("LPWSinc",
    "Filter/Effect/Audio",
    "Low-pass and High-pass Windowed sinc filter",
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
  PROP_FREQUENCY,
  PROP_MODE,
  PROP_WINDOW
};

enum
{
  MODE_LOW_PASS = 0,
  MODE_HIGH_PASS
};

#define GST_TYPE_LPWSINC_MODE (gst_lpwsinc_mode_get_type ())
static GType
gst_lpwsinc_mode_get_type (void)
{
  static GType gtype = 0;

  if (gtype == 0) {
    static const GEnumValue values[] = {
      {MODE_LOW_PASS, "Low pass (default)",
          "low-pass"},
      {MODE_HIGH_PASS, "High pass",
          "high-pass"},
      {0, NULL, NULL}
    };

    gtype = g_enum_register_static ("GstLPWSincMode", values);
  }
  return gtype;
}

enum
{
  WINDOW_HAMMING = 0,
  WINDOW_BLACKMAN
};

#define GST_TYPE_LPWSINC_WINDOW (gst_lpwsinc_window_get_type ())
static GType
gst_lpwsinc_window_get_type (void)
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

    gtype = g_enum_register_static ("GstLPWSincWindow", values);
  }
  return gtype;
}


#define ALLOWED_CAPS \
    "audio/x-raw-float,"                                              \
    " width = (int) { 32, 64 }, "                                     \
    " endianness = (int) BYTE_ORDER,"                                 \
    " rate = (int) [ 1, MAX ],"                                       \
    " channels = (int) [ 1, MAX ]"

#define DEBUG_INIT(bla) \
  GST_DEBUG_CATEGORY_INIT (gst_lpwsinc_debug, "lpwsinc", 0, "Low-pass and High-pass Windowed sinc filter plugin");

GST_BOILERPLATE_FULL (GstLPWSinc, gst_lpwsinc, GstAudioFilter,
    GST_TYPE_AUDIO_FILTER, DEBUG_INIT);

static void lpwsinc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void lpwsinc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstFlowReturn lpwsinc_transform (GstBaseTransform * base,
    GstBuffer * inbuf, GstBuffer * outbuf);
static gboolean lpwsinc_get_unit_size (GstBaseTransform * base, GstCaps * caps,
    guint * size);
static gboolean lpwsinc_setup (GstAudioFilter * base,
    GstRingBufferSpec * format);

/* Element class */

static void
gst_lpwsinc_dispose (GObject * object)
{
  GstLPWSinc *self = GST_LPWSINC (object);

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
gst_lpwsinc_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);
  GstCaps *caps;

  gst_element_class_set_details (element_class, &lpwsinc_details);

  caps = gst_caps_from_string (ALLOWED_CAPS);
  gst_audio_filter_class_add_pad_templates (GST_AUDIO_FILTER_CLASS (g_class),
      caps);
  gst_caps_unref (caps);
}

static void
gst_lpwsinc_class_init (GstLPWSincClass * klass)
{
  GObjectClass *gobject_class;
  GstBaseTransformClass *trans_class;

  gobject_class = (GObjectClass *) klass;
  trans_class = (GstBaseTransformClass *) klass;

  gobject_class->set_property = lpwsinc_set_property;
  gobject_class->get_property = lpwsinc_get_property;
  gobject_class->dispose = gst_lpwsinc_dispose;

  g_object_class_install_property (gobject_class, PROP_FREQUENCY,
      g_param_spec_double ("frequency", "Frequency",
          "Cut-off Frequency", 0.0, G_MAXDOUBLE, 0.0,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE));

  g_object_class_install_property (gobject_class, PROP_LENGTH,
      g_param_spec_int ("length", "Length",
          "Filter kernel length, will be rounded to the next odd number",
          3, G_MAXINT, 101, G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE));

  g_object_class_install_property (gobject_class, PROP_MODE,
      g_param_spec_enum ("mode", "Mode",
          "Low pass or high pass mode", GST_TYPE_LPWSINC_MODE,
          MODE_LOW_PASS, G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE));

  g_object_class_install_property (gobject_class, PROP_WINDOW,
      g_param_spec_enum ("window", "Window",
          "Window function to use", GST_TYPE_LPWSINC_WINDOW,
          WINDOW_HAMMING, G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE));

  trans_class->transform = GST_DEBUG_FUNCPTR (lpwsinc_transform);
  trans_class->get_unit_size = GST_DEBUG_FUNCPTR (lpwsinc_get_unit_size);
  GST_AUDIO_FILTER_CLASS (klass)->setup = GST_DEBUG_FUNCPTR (lpwsinc_setup);
}

static void
gst_lpwsinc_init (GstLPWSinc * self, GstLPWSincClass * g_class)
{
  self->mode = MODE_LOW_PASS;
  self->window = WINDOW_HAMMING;
  self->kernel_length = 101;
  self->frequency = 0.0;
  self->kernel = NULL;
  self->residue = NULL;

  self->have_kernel = FALSE;
}

static void
process_32 (GstLPWSinc * self, gfloat * src, gfloat * dst, guint input_samples)
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
process_64 (GstLPWSinc * self, gdouble * src, gdouble * dst,
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
lpwsinc_build_kernel (GstLPWSinc * self)
{
  gint i = 0;
  gdouble sum = 0.0;
  gint len = 0;
  gdouble w;

  /* fill the kernel */
  len = self->kernel_length;
  GST_DEBUG ("lpwsinc: initializing filter kernel of length %d", len);

  if (GST_AUDIO_FILTER (self)->format.rate == 0) {
    GST_DEBUG ("rate not set yet");
    return;
  }

  if (GST_AUDIO_FILTER (self)->format.channels == 0) {
    GST_DEBUG ("channels not set yet");
    return;
  }

  /* Clamp cutoff frequency between 0 and the nyquist frequency */
  self->frequency =
      CLAMP (self->frequency, 0.0, GST_AUDIO_FILTER (self)->format.rate / 2);

  w = 2 * M_PI * (self->frequency / GST_AUDIO_FILTER (self)->format.rate);

  if (self->kernel)
    g_free (self->kernel);
  self->kernel = g_new (gdouble, len);

  for (i = 0; i < len; ++i) {
    if (i == len / 2)
      self->kernel[i] = w;
    else
      self->kernel[i] = sin (w * (i - len / 2)) / (i - len / 2);
    /* windowing */
    if (self->window == WINDOW_HAMMING)
      self->kernel[i] *= (0.54 - 0.46 * cos (2 * M_PI * i / len));
    else
      self->kernel[i] *=
          (0.42 - 0.5 * cos (2 * M_PI * i / len) +
          0.08 * cos (4 * M_PI * i / len));
  }

  /* normalize for unity gain at DC */
  for (i = 0; i < len; ++i)
    sum += self->kernel[i];
  for (i = 0; i < len; ++i)
    self->kernel[i] /= sum;

  /* convert to highpass if specified */
  if (self->mode == MODE_HIGH_PASS) {
    for (i = 0; i < len; ++i)
      self->kernel[i] = -self->kernel[i];
    self->kernel[len / 2] += 1.0;
  }

  /* set up the residue memory space */
  if (self->residue)
    g_free (self->residue);
  self->residue =
      g_new0 (gdouble, len * GST_AUDIO_FILTER (self)->format.channels);

  self->have_kernel = TRUE;
}

/* GstAudioFilter vmethod implementations */

/* get notified of caps and plug in the correct process function */
static gboolean
lpwsinc_setup (GstAudioFilter * base, GstRingBufferSpec * format)
{
  GstLPWSinc *self = GST_LPWSINC (base);

  gboolean ret = TRUE;

  if (format->width == 32)
    self->process = (GstLPWSincProcessFunc) process_32;
  else if (format->width == 64)
    self->process = (GstLPWSincProcessFunc) process_64;
  else
    ret = FALSE;

  self->have_kernel = FALSE;

  return TRUE;
}

/* GstBaseTransform vmethod implementations */

static gboolean
lpwsinc_get_unit_size (GstBaseTransform * base, GstCaps * caps, guint * size)
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
lpwsinc_transform (GstBaseTransform * base, GstBuffer * inbuf,
    GstBuffer * outbuf)
{
  GstLPWSinc *self = GST_LPWSINC (base);
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
    lpwsinc_build_kernel (self);

  self->process (self, GST_BUFFER_DATA (inbuf), GST_BUFFER_DATA (outbuf),
      input_samples);

  return GST_FLOW_OK;
}

static void
lpwsinc_set_property (GObject * object, guint prop_id, const GValue * value,
    GParamSpec * pspec)
{
  GstLPWSinc *self = GST_LPWSINC (object);

  g_return_if_fail (GST_IS_LPWSINC (self));

  switch (prop_id) {
    case PROP_LENGTH:{
      gint val;

      GST_BASE_TRANSFORM_LOCK (self);
      val = g_value_get_int (value);
      if (val % 2 == 0)
        val++;
      self->kernel_length = val;
      lpwsinc_build_kernel (self);
      GST_BASE_TRANSFORM_UNLOCK (self);
      break;
    }
    case PROP_FREQUENCY:
      GST_BASE_TRANSFORM_LOCK (self);
      self->frequency = g_value_get_double (value);
      lpwsinc_build_kernel (self);
      GST_BASE_TRANSFORM_UNLOCK (self);
      break;
    case PROP_MODE:
      GST_BASE_TRANSFORM_LOCK (self);
      self->mode = g_value_get_enum (value);
      lpwsinc_build_kernel (self);
      GST_BASE_TRANSFORM_UNLOCK (self);
      break;
    case PROP_WINDOW:
      GST_BASE_TRANSFORM_LOCK (self);
      self->window = g_value_get_enum (value);
      lpwsinc_build_kernel (self);
      GST_BASE_TRANSFORM_UNLOCK (self);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
lpwsinc_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstLPWSinc *self = GST_LPWSINC (object);

  switch (prop_id) {
    case PROP_LENGTH:
      g_value_set_int (value, self->kernel_length);
      break;
    case PROP_FREQUENCY:
      g_value_set_double (value, self->frequency);
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
