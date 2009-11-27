/* -*- c-basic-offset: 2 -*-
 * 
 * GStreamer
 * Copyright (C) 1999-2001 Erik Walthinsen <omega@cse.ogi.edu>
 *               2006 Dreamlab Technologies Ltd. <mathis.hofer@dreamlab.net>
 *               2007-2009 Sebastian Dröge <sebastian.droege@collabora.co.uk>
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

/* FIXME: Remove this once we depend on gst-plugins-base 0.10.26 */
#ifndef GST_AUDIO_FILTER_CAST
#define GST_AUDIO_FILTER_CAST(obj) ((GstAudioFilter *) (obj))
#endif

#include "audiofxbasefirfilter.h"

#define GST_CAT_DEFAULT gst_audio_fx_base_fir_filter_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

#define ALLOWED_CAPS \
    "audio/x-raw-float, "                                             \
    " width = (int) { 32, 64 }, "                                     \
    " endianness = (int) BYTE_ORDER, "                                \
    " rate = (int) [ 1, MAX ], "                                      \
    " channels = (int) [ 1, MAX ]"

#define DEBUG_INIT(bla) \
  GST_DEBUG_CATEGORY_INIT (gst_audio_fx_base_fir_filter_debug, "audiofxbasefirfilter", 0, \
      "FIR filter base class");

GST_BOILERPLATE_FULL (GstAudioFXBaseFIRFilter, gst_audio_fx_base_fir_filter,
    GstAudioFilter, GST_TYPE_AUDIO_FILTER, DEBUG_INIT);

static GstFlowReturn gst_audio_fx_base_fir_filter_transform (GstBaseTransform *
    base, GstBuffer * inbuf, GstBuffer * outbuf);
static gboolean gst_audio_fx_base_fir_filter_start (GstBaseTransform * base);
static gboolean gst_audio_fx_base_fir_filter_stop (GstBaseTransform * base);
static gboolean gst_audio_fx_base_fir_filter_event (GstBaseTransform * base,
    GstEvent * event);
static gboolean gst_audio_fx_base_fir_filter_setup (GstAudioFilter * base,
    GstRingBufferSpec * format);

static gboolean gst_audio_fx_base_fir_filter_query (GstPad * pad,
    GstQuery * query);
static const GstQueryType *gst_audio_fx_base_fir_filter_query_type (GstPad *
    pad);

/* Element class */

static void
gst_audio_fx_base_fir_filter_dispose (GObject * object)
{
  GstAudioFXBaseFIRFilter *self = GST_AUDIO_FX_BASE_FIR_FILTER (object);

  if (self->buffer) {
    g_free (self->buffer);
    self->buffer = NULL;
    self->buffer_length = 0;
  }

  if (self->kernel) {
    g_free (self->kernel);
    self->kernel = NULL;
  }

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_audio_fx_base_fir_filter_base_init (gpointer g_class)
{
  GstCaps *caps;

  caps = gst_caps_from_string (ALLOWED_CAPS);
  gst_audio_filter_class_add_pad_templates (GST_AUDIO_FILTER_CLASS (g_class),
      caps);
  gst_caps_unref (caps);
}

static void
gst_audio_fx_base_fir_filter_class_init (GstAudioFXBaseFIRFilterClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstBaseTransformClass *trans_class = (GstBaseTransformClass *) klass;
  GstAudioFilterClass *filter_class = (GstAudioFilterClass *) klass;

  gobject_class->dispose = gst_audio_fx_base_fir_filter_dispose;

  trans_class->transform =
      GST_DEBUG_FUNCPTR (gst_audio_fx_base_fir_filter_transform);
  trans_class->start = GST_DEBUG_FUNCPTR (gst_audio_fx_base_fir_filter_start);
  trans_class->stop = GST_DEBUG_FUNCPTR (gst_audio_fx_base_fir_filter_stop);
  trans_class->event = GST_DEBUG_FUNCPTR (gst_audio_fx_base_fir_filter_event);
  filter_class->setup = GST_DEBUG_FUNCPTR (gst_audio_fx_base_fir_filter_setup);
}

static void
gst_audio_fx_base_fir_filter_init (GstAudioFXBaseFIRFilter * self,
    GstAudioFXBaseFIRFilterClass * g_class)
{
  self->kernel = NULL;
  self->buffer = NULL;
  self->buffer_length = 0;

  self->start_ts = GST_CLOCK_TIME_NONE;
  self->start_off = GST_BUFFER_OFFSET_NONE;
  self->nsamples_out = 0;
  self->nsamples_in = 0;

  gst_pad_set_query_function (GST_BASE_TRANSFORM (self)->srcpad,
      gst_audio_fx_base_fir_filter_query);
  gst_pad_set_query_type_function (GST_BASE_TRANSFORM (self)->srcpad,
      gst_audio_fx_base_fir_filter_query_type);
}

/* 
 * The code below calculates the linear convolution:
 *
 * y[t] = \sum_{u=0}^{M-1} x[t - u] * h[u]
 *
 * where y is the output, x is the input, M is the length
 * of the filter kernel and h is the filter kernel. For x
 * holds: x[t] == 0 \forall t < 0.
 *
 * The runtime complexity of this is O (M) per sample.
 *
 */
#define DEFINE_PROCESS_FUNC(width,ctype) \
static guint \
process_##width (GstAudioFXBaseFIRFilter * self, const g##ctype * src, g##ctype * dst, guint input_samples) \
{ \
  gint kernel_length = self->kernel_length; \
  gint i, j, k, l; \
  gint channels = GST_AUDIO_FILTER_CAST (self)->format.channels; \
  gint res_start; \
  gint from_input; \
  gint off; \
  gdouble *buffer = self->buffer; \
  gdouble *kernel = self->kernel; \
  guint buffer_length = self->buffer_length; \
  \
  if (!buffer) { \
    self->buffer_length = buffer_length = kernel_length * channels; \
    self->buffer = buffer = g_new0 (gdouble, self->buffer_length); \
  } \
  \
  /* convolution */ \
  for (i = 0; i < input_samples; i++) { \
    dst[i] = 0.0; \
    k = i % channels; \
    l = i / channels; \
    from_input = MIN (l, kernel_length-1); \
    off = l * channels + k; \
    for (j = 0; j <= from_input; j++) { \
      dst[i] += src[off] * kernel[j]; \
      off -= channels; \
    } \
    /* j == from_input && off == (l - j) * channels + k */ \
    off += kernel_length * channels; \
    for (; j < kernel_length; j++) { \
      dst[i] += buffer[off] * kernel[j]; \
      off -= channels; \
    } \
  } \
  \
  /* copy the tail of the current input buffer to the residue, while \
   * keeping parts of the residue if the input buffer is smaller than \
   * the kernel length */ \
  /* from now on take kernel length as length over all channels */ \
  kernel_length *= channels; \
  if (input_samples < kernel_length) \
    res_start = kernel_length - input_samples; \
  else \
    res_start = 0; \
  \
  for (i = 0; i < res_start; i++) \
    buffer[i] = buffer[i + input_samples]; \
  /* i == res_start */ \
  for (; i < kernel_length; i++) \
    buffer[i] = src[input_samples - kernel_length + i]; \
  \
  self->buffer_fill += kernel_length - res_start; \
  if (self->buffer_fill > kernel_length) \
    self->buffer_fill = kernel_length; \
  \
  return input_samples; \
}

DEFINE_PROCESS_FUNC (32, float);
DEFINE_PROCESS_FUNC (64, double);

#undef DEFINE_PROCESS_FUNC

void
gst_audio_fx_base_fir_filter_push_residue (GstAudioFXBaseFIRFilter * self)
{
  GstBuffer *outbuf;
  GstFlowReturn res;
  gint rate = GST_AUDIO_FILTER_CAST (self)->format.rate;
  gint channels = GST_AUDIO_FILTER_CAST (self)->format.channels;
  gint width = GST_AUDIO_FILTER_CAST (self)->format.width / 8;
  guint outsize, outsamples;
  gint64 diffsize, diffsamples;
  guint8 *in, *out;

  if (channels == 0 || rate == 0 || self->nsamples_in == 0) {
    self->buffer_fill = 0;
    g_free (self->buffer);
    self->buffer = NULL;
    return;
  }

  /* Calculate the number of samples and their memory size that
   * should be pushed from the residue */
  outsamples = self->nsamples_in - (self->nsamples_out - self->latency);
  if (outsamples == 0) {
    self->buffer_fill = 0;
    g_free (self->buffer);
    self->buffer = NULL;
    return;
  }
  outsize = outsamples * channels * width;

  /* Process the difference between latency and residue length samples
   * to start at the actual data instead of starting at the zeros before
   * when we only got one buffer smaller than latency */

  /* FIXME: still time domain convolution specific */
  diffsamples =
      ((gint64) self->latency) - ((gint64) self->buffer_fill) / channels;
  if (diffsamples > 0) {
    diffsize = diffsamples * channels * width;
    in = g_new0 (guint8, diffsize);
    out = g_new0 (guint8, diffsize);
    self->nsamples_out += self->process (self, in, out, diffsamples * channels);
    g_free (in);
    g_free (out);
  }

  res = gst_pad_alloc_buffer (GST_BASE_TRANSFORM_CAST (self)->srcpad,
      GST_BUFFER_OFFSET_NONE, outsize,
      GST_PAD_CAPS (GST_BASE_TRANSFORM_CAST (self)->srcpad), &outbuf);

  if (G_UNLIKELY (res != GST_FLOW_OK)) {
    GST_WARNING_OBJECT (self, "failed allocating buffer of %d bytes", outsize);
    self->buffer_fill = 0;
    return;
  }

  /* Convolve the residue with zeros to get the actual remaining data */
  in = g_new0 (guint8, outsize);
  self->nsamples_out +=
      self->process (self, in, GST_BUFFER_DATA (outbuf), outsamples * channels);
  g_free (in);

  /* FIXME: time domain convolution specific */

  /* Set timestamp, offset, etc from the values we
   * saved when processing the regular buffers */
  if (GST_CLOCK_TIME_IS_VALID (self->start_ts))
    GST_BUFFER_TIMESTAMP (outbuf) = self->start_ts;
  else
    GST_BUFFER_TIMESTAMP (outbuf) = 0;
  GST_BUFFER_TIMESTAMP (outbuf) +=
      gst_util_uint64_scale_int (self->nsamples_out - outsamples -
      self->latency, GST_SECOND, rate);

  GST_BUFFER_DURATION (outbuf) =
      gst_util_uint64_scale_int (outsamples, GST_SECOND, rate);

  if (self->start_off != GST_BUFFER_OFFSET_NONE) {
    GST_BUFFER_OFFSET (outbuf) =
        self->start_off + self->nsamples_out - outsamples - self->latency;
    GST_BUFFER_OFFSET_END (outbuf) = GST_BUFFER_OFFSET (outbuf) + outsamples;
  }

  GST_DEBUG_OBJECT (self, "Pushing residue buffer of size %d with timestamp: %"
      GST_TIME_FORMAT ", duration: %" GST_TIME_FORMAT ", offset: %"
      G_GUINT64_FORMAT ", offset_end: %" G_GUINT64_FORMAT ", nsamples_out: %d",
      GST_BUFFER_SIZE (outbuf), GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (outbuf)),
      GST_TIME_ARGS (GST_BUFFER_DURATION (outbuf)), GST_BUFFER_OFFSET (outbuf),
      GST_BUFFER_OFFSET_END (outbuf), outsamples);

  res = gst_pad_push (GST_BASE_TRANSFORM_CAST (self)->srcpad, outbuf);

  if (G_UNLIKELY (res != GST_FLOW_OK)) {
    GST_WARNING_OBJECT (self, "failed to push residue");
  }

  self->buffer_fill = 0;
}

/* GstAudioFilter vmethod implementations */

/* get notified of caps and plug in the correct process function */
static gboolean
gst_audio_fx_base_fir_filter_setup (GstAudioFilter * base,
    GstRingBufferSpec * format)
{
  GstAudioFXBaseFIRFilter *self = GST_AUDIO_FX_BASE_FIR_FILTER (base);
  gboolean ret = TRUE;

  if (self->buffer) {
    gst_audio_fx_base_fir_filter_push_residue (self);
    g_free (self->buffer);
    self->buffer = NULL;
    self->buffer_fill = 0;
    self->buffer_length = 0;
    self->start_ts = GST_CLOCK_TIME_NONE;
    self->start_off = GST_BUFFER_OFFSET_NONE;
    self->nsamples_out = 0;
    self->nsamples_in = 0;
  }

  if (format->width == 32)
    self->process = (GstAudioFXBaseFIRFilterProcessFunc) process_32;
  else if (format->width == 64)
    self->process = (GstAudioFXBaseFIRFilterProcessFunc) process_64;
  else
    ret = FALSE;

  return TRUE;
}

/* GstBaseTransform vmethod implementations */

static GstFlowReturn
gst_audio_fx_base_fir_filter_transform (GstBaseTransform * base,
    GstBuffer * inbuf, GstBuffer * outbuf)
{
  GstAudioFXBaseFIRFilter *self = GST_AUDIO_FX_BASE_FIR_FILTER (base);
  GstClockTime timestamp, expected_timestamp;
  gint channels = GST_AUDIO_FILTER_CAST (self)->format.channels;
  gint rate = GST_AUDIO_FILTER_CAST (self)->format.rate;
  gint width = GST_AUDIO_FILTER_CAST (self)->format.width / 8;
  guint input_samples = (GST_BUFFER_SIZE (inbuf) / width) / channels;
  guint output_samples = (GST_BUFFER_SIZE (outbuf) / width) / channels;
  guint generated_samples;
  guint64 output_offset;
  gint64 diff = 0;

  timestamp = GST_BUFFER_TIMESTAMP (outbuf);
  if (!GST_CLOCK_TIME_IS_VALID (timestamp)
      && !GST_CLOCK_TIME_IS_VALID (self->start_ts)) {
    GST_ERROR_OBJECT (self, "Invalid timestamp");
    return GST_FLOW_ERROR;
  }

  gst_object_sync_values (G_OBJECT (self), timestamp);

  g_return_val_if_fail (self->kernel != NULL, GST_FLOW_ERROR);
  g_return_val_if_fail (channels != 0, GST_FLOW_ERROR);

  if (GST_CLOCK_TIME_IS_VALID (self->start_ts))
    expected_timestamp =
        self->start_ts + gst_util_uint64_scale_int (self->nsamples_in,
        GST_SECOND, rate);
  else
    expected_timestamp = GST_CLOCK_TIME_NONE;

  /* Reset the residue if already existing on discont buffers */
  if (GST_BUFFER_IS_DISCONT (inbuf)
      || (GST_CLOCK_TIME_IS_VALID (expected_timestamp)
          && (ABS (GST_CLOCK_DIFF (timestamp,
                      expected_timestamp) > 5 * GST_MSECOND)))) {
    GST_DEBUG_OBJECT (self, "Discontinuity detected - flushing");
    if (GST_CLOCK_TIME_IS_VALID (expected_timestamp))
      gst_audio_fx_base_fir_filter_push_residue (self);
    self->buffer_fill = 0;
    g_free (self->buffer);
    self->buffer = NULL;
    expected_timestamp = self->start_ts = timestamp;
    self->start_off = GST_BUFFER_OFFSET (inbuf);
    self->nsamples_out = 0;
    self->nsamples_in = 0;
  } else if (!GST_CLOCK_TIME_IS_VALID (self->start_ts)) {
    expected_timestamp = self->start_ts = timestamp;
    self->start_off = GST_BUFFER_OFFSET (inbuf);
  }

  self->nsamples_in += input_samples;

  generated_samples =
      self->process (self, GST_BUFFER_DATA (inbuf), GST_BUFFER_DATA (outbuf),
      input_samples * channels);

  g_assert (generated_samples <= output_samples);
  self->nsamples_out += generated_samples;
  if (generated_samples == 0)
    return GST_BASE_TRANSFORM_FLOW_DROPPED;

  /* Calculate the number of samples we can push out now without outputting
   * latency zeros in the beginning */
  diff = ((gint64) self->nsamples_out) - ((gint64) self->latency);
  if (diff < 0) {
    return GST_BASE_TRANSFORM_FLOW_DROPPED;
  } else if (diff < generated_samples) {
    gint64 tmp = diff;
    diff = generated_samples - diff;
    generated_samples = tmp;
    GST_BUFFER_DATA (outbuf) += diff * width * channels;
  }
  GST_BUFFER_SIZE (outbuf) = generated_samples * width * channels;

  output_offset = self->nsamples_out - self->latency - generated_samples;
  GST_BUFFER_TIMESTAMP (outbuf) =
      self->start_ts + gst_util_uint64_scale_int (output_offset, GST_SECOND,
      rate);
  GST_BUFFER_DURATION (outbuf) =
      gst_util_uint64_scale_int (output_samples, GST_SECOND, rate);
  if (self->start_off != GST_BUFFER_OFFSET_NONE) {
    GST_BUFFER_OFFSET (outbuf) = self->start_off + output_offset;
    GST_BUFFER_OFFSET_END (outbuf) =
        GST_BUFFER_OFFSET (outbuf) + generated_samples;
  } else {
    GST_BUFFER_OFFSET (outbuf) = GST_BUFFER_OFFSET_NONE;
    GST_BUFFER_OFFSET_END (outbuf) = GST_BUFFER_OFFSET_NONE;
  }

  GST_DEBUG_OBJECT (self, "Pushing buffer of size %d with timestamp: %"
      GST_TIME_FORMAT ", duration: %" GST_TIME_FORMAT ", offset: %"
      G_GUINT64_FORMAT ", offset_end: %" G_GUINT64_FORMAT ", nsamples_out: %d",
      GST_BUFFER_SIZE (outbuf), GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (outbuf)),
      GST_TIME_ARGS (GST_BUFFER_DURATION (outbuf)), GST_BUFFER_OFFSET (outbuf),
      GST_BUFFER_OFFSET_END (outbuf), generated_samples);

  return GST_FLOW_OK;
}

static gboolean
gst_audio_fx_base_fir_filter_start (GstBaseTransform * base)
{
  GstAudioFXBaseFIRFilter *self = GST_AUDIO_FX_BASE_FIR_FILTER (base);

  self->buffer_fill = 0;
  g_free (self->buffer);
  self->buffer = NULL;
  self->start_ts = GST_CLOCK_TIME_NONE;
  self->start_off = GST_BUFFER_OFFSET_NONE;
  self->nsamples_out = 0;
  self->nsamples_in = 0;

  return TRUE;
}

static gboolean
gst_audio_fx_base_fir_filter_stop (GstBaseTransform * base)
{
  GstAudioFXBaseFIRFilter *self = GST_AUDIO_FX_BASE_FIR_FILTER (base);

  g_free (self->buffer);
  self->buffer = NULL;
  self->buffer_length = 0;

  return TRUE;
}

static gboolean
gst_audio_fx_base_fir_filter_query (GstPad * pad, GstQuery * query)
{
  GstAudioFXBaseFIRFilter *self =
      GST_AUDIO_FX_BASE_FIR_FILTER (gst_pad_get_parent (pad));
  gboolean res = TRUE;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_LATENCY:
    {
      GstClockTime min, max;
      gboolean live;
      guint64 latency;
      GstPad *peer;
      gint rate = GST_AUDIO_FILTER (self)->format.rate;

      if (rate == 0) {
        res = FALSE;
      } else if ((peer = gst_pad_get_peer (GST_BASE_TRANSFORM (self)->sinkpad))) {
        if ((res = gst_pad_query (peer, query))) {
          gst_query_parse_latency (query, &live, &min, &max);

          GST_DEBUG_OBJECT (self, "Peer latency: min %"
              GST_TIME_FORMAT " max %" GST_TIME_FORMAT,
              GST_TIME_ARGS (min), GST_TIME_ARGS (max));

          /* add our own latency */
          latency =
              gst_util_uint64_scale_round (self->latency, GST_SECOND, rate);

          GST_DEBUG_OBJECT (self, "Our latency: %"
              GST_TIME_FORMAT, GST_TIME_ARGS (latency));

          min += latency;
          if (max != GST_CLOCK_TIME_NONE)
            max += latency;

          GST_DEBUG_OBJECT (self, "Calculated total latency : min %"
              GST_TIME_FORMAT " max %" GST_TIME_FORMAT,
              GST_TIME_ARGS (min), GST_TIME_ARGS (max));

          gst_query_set_latency (query, live, min, max);
        }
        gst_object_unref (peer);
      }
      break;
    }
    default:
      res = gst_pad_query_default (pad, query);
      break;
  }
  gst_object_unref (self);
  return res;
}

static const GstQueryType *
gst_audio_fx_base_fir_filter_query_type (GstPad * pad)
{
  static const GstQueryType types[] = {
    GST_QUERY_LATENCY,
    0
  };

  return types;
}

static gboolean
gst_audio_fx_base_fir_filter_event (GstBaseTransform * base, GstEvent * event)
{
  GstAudioFXBaseFIRFilter *self = GST_AUDIO_FX_BASE_FIR_FILTER (base);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
      gst_audio_fx_base_fir_filter_push_residue (self);
      self->start_ts = GST_CLOCK_TIME_NONE;
      self->start_off = GST_BUFFER_OFFSET_NONE;
      self->nsamples_out = 0;
      self->nsamples_in = 0;
      break;
    default:
      break;
  }

  return GST_BASE_TRANSFORM_CLASS (parent_class)->event (base, event);
}

void
gst_audio_fx_base_fir_filter_set_kernel (GstAudioFXBaseFIRFilter * self,
    gdouble * kernel, guint kernel_length, guint64 latency)
{
  g_return_if_fail (kernel != NULL);
  g_return_if_fail (self != NULL);

  GST_BASE_TRANSFORM_LOCK (self);
  if (self->buffer) {
    gst_audio_fx_base_fir_filter_push_residue (self);
    self->start_ts = GST_CLOCK_TIME_NONE;
    self->start_off = GST_BUFFER_OFFSET_NONE;
    self->nsamples_out = 0;
    self->nsamples_in = 0;
    self->buffer_fill = 0;
  }

  g_free (self->kernel);
  g_free (self->buffer);
  self->buffer = NULL;
  self->buffer_fill = 0;
  self->buffer_length = 0;

  self->kernel = kernel;
  self->kernel_length = kernel_length;

  if (self->latency != latency) {
    self->latency = latency;
    gst_element_post_message (GST_ELEMENT (self),
        gst_message_new_latency (GST_OBJECT (self)));
  }

  GST_BASE_TRANSFORM_UNLOCK (self);
}
