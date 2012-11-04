/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * This file:
 * Copyright (C) 2005 Luca Ognibene <luogni@tin.it>
 * Copyright (C) 2006 Martin Zlomek <martin.zlomek@itonis.tv>
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <libavcodec/avcodec.h>

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>
#include <gst/video/video.h>

#include "gstffmpeg.h"
#include "gstffmpegcodecmap.h"

typedef struct _GstFFMpegAudioResample
{
  GstBaseTransform element;

  GstPad *sinkpad, *srcpad;

  gint in_rate, out_rate;
  gint in_channels, out_channels;

  ReSampleContext *res;
} GstFFMpegAudioResample;

typedef struct _GstFFMpegAudioResampleClass
{
  GstBaseTransformClass parent_class;
} GstFFMpegAudioResampleClass;

#define GST_TYPE_FFMPEGAUDIORESAMPLE \
	(gst_ffmpegaudioresample_get_type())
#define GST_FFMPEGAUDIORESAMPLE(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_FFMPEGAUDIORESAMPLE,GstFFMpegAudioResample))
#define GST_FFMPEGAUDIORESAMPLE_CLASS(klass) \
	(G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_FFMPEGAUDIORESAMPLE,GstFFMpegAudioResampleClass))
#define GST_IS_FFMPEGAUDIORESAMPLE(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_FFMPEGAUDIORESAMPLE))
#define GST_IS_FFMPEGAUDIORESAMPLE_CLASS(klass) \
	(G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_FFMPEGAUDIORESAMPLE))

GType gst_ffmpegaudioresample_get_type (void);

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS
    ("audio/x-raw,"
        "format = (string) " GST_AUDIO_NE (S16) ","
        "channels = (int) { 1 , 2 }, rate = (int) [1, MAX ]")
    );

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS
    ("audio/x-raw,"
        "format = (string) " GST_AUDIO_NE (S16) ","
        "channels = (int) { 1 , 2 }, rate = (int) [1, MAX ]")
    );

GST_BOILERPLATE (GstFFMpegAudioResample, gst_ffmpegaudioresample,
    GstBaseTransform, GST_TYPE_BASE_TRANSFORM);

static void gst_ffmpegaudioresample_finalize (GObject * object);

static GstCaps *gst_ffmpegaudioresample_transform_caps (GstBaseTransform *
    trans, GstPadDirection direction, GstCaps * caps);
static gboolean gst_ffmpegaudioresample_transform_size (GstBaseTransform *
    trans, GstPadDirection direction, GstCaps * caps, gsize size,
    GstCaps * othercaps, gsize * othersize);
static gboolean gst_ffmpegaudioresample_get_unit_size (GstBaseTransform * trans,
    GstCaps * caps, gsize * size);
static gboolean gst_ffmpegaudioresample_set_caps (GstBaseTransform * trans,
    GstCaps * incaps, GstCaps * outcaps);
static GstFlowReturn gst_ffmpegaudioresample_transform (GstBaseTransform *
    trans, GstBuffer * inbuf, GstBuffer * outbuf);

static void
gst_ffmpegaudioresample_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_static_pad_template (element_class, &src_factory);
  gst_element_class_add_static_pad_template (element_class, &sink_factory);
  gst_element_class_set_static_metadata (element_class,
      "libav Audio resampling element", "Filter/Converter/Audio",
      "Converts audio from one samplerate to another",
      "Edward Hervey <bilboed@bilboed.com>");
}

static void
gst_ffmpegaudioresample_class_init (GstFFMpegAudioResampleClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstBaseTransformClass *trans_class = GST_BASE_TRANSFORM_CLASS (klass);

  gobject_class->finalize = gst_ffmpegaudioresample_finalize;

  trans_class->transform_caps =
      GST_DEBUG_FUNCPTR (gst_ffmpegaudioresample_transform_caps);
  trans_class->get_unit_size =
      GST_DEBUG_FUNCPTR (gst_ffmpegaudioresample_get_unit_size);
  trans_class->set_caps = GST_DEBUG_FUNCPTR (gst_ffmpegaudioresample_set_caps);
  trans_class->transform =
      GST_DEBUG_FUNCPTR (gst_ffmpegaudioresample_transform);
  trans_class->transform_size =
      GST_DEBUG_FUNCPTR (gst_ffmpegaudioresample_transform_size);

  trans_class->passthrough_on_same_caps = TRUE;
}

static void
gst_ffmpegaudioresample_init (GstFFMpegAudioResample * resample,
    GstFFMpegAudioResampleClass * klass)
{
  GstBaseTransform *trans = GST_BASE_TRANSFORM (resample);

  gst_pad_set_bufferalloc_function (trans->sinkpad, NULL);

  resample->res = NULL;
}

static void
gst_ffmpegaudioresample_finalize (GObject * object)
{
  GstFFMpegAudioResample *resample = GST_FFMPEGAUDIORESAMPLE (object);

  if (resample->res != NULL)
    audio_resample_close (resample->res);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static GstCaps *
gst_ffmpegaudioresample_transform_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps)
{
  GstCaps *retcaps;
  GstStructure *struc;

  retcaps = gst_caps_copy (caps);
  struc = gst_caps_get_structure (retcaps, 0);
  gst_structure_set (struc, "rate", GST_TYPE_INT_RANGE, 1, G_MAXINT, NULL);

  GST_LOG_OBJECT (trans, "returning caps %" GST_PTR_FORMAT, retcaps);

  return retcaps;
}

static gboolean
gst_ffmpegaudioresample_transform_size (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, gsize size, GstCaps * othercaps,
    gsize * othersize)
{
  gint inrate, outrate;
  gint inchanns, outchanns;
  GstStructure *ins, *outs;
  gboolean ret;
  guint64 conv;

  ins = gst_caps_get_structure (caps, 0);
  outs = gst_caps_get_structure (othercaps, 0);

  /* Get input/output sample rate and channels */
  ret = gst_structure_get_int (ins, "rate", &inrate);
  ret &= gst_structure_get_int (ins, "channels", &inchanns);
  ret &= gst_structure_get_int (outs, "rate", &outrate);
  ret &= gst_structure_get_int (outs, "channels", &outchanns);

  if (!ret)
    return FALSE;

  conv = gst_util_uint64_scale (size, outrate * outchanns, inrate * inchanns);
  /* Adding padding to the output buffer size, since audio_resample's internal
   * methods might write a bit further. */
  *othersize = (guint) conv + 64;

  GST_DEBUG_OBJECT (trans, "Transformed size from %d to %d", size, *othersize);

  return TRUE;
}

static gboolean
gst_ffmpegaudioresample_get_unit_size (GstBaseTransform * trans, GstCaps * caps,
    gsize * size)
{
  gint channels;
  GstStructure *structure;
  gboolean ret;

  g_assert (size);

  structure = gst_caps_get_structure (caps, 0);
  ret = gst_structure_get_int (structure, "channels", &channels);
  g_return_val_if_fail (ret, FALSE);

  *size = 2 * channels;

  return TRUE;
}

static gboolean
gst_ffmpegaudioresample_set_caps (GstBaseTransform * trans, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstFFMpegAudioResample *resample = GST_FFMPEGAUDIORESAMPLE (trans);
  GstStructure *instructure = gst_caps_get_structure (incaps, 0);
  GstStructure *outstructure = gst_caps_get_structure (outcaps, 0);

  GST_LOG_OBJECT (resample, "incaps:%" GST_PTR_FORMAT, incaps);

  GST_LOG_OBJECT (resample, "outcaps:%" GST_PTR_FORMAT, outcaps);

  if (!gst_structure_get_int (instructure, "channels", &resample->in_channels))
    return FALSE;
  if (!gst_structure_get_int (instructure, "rate", &resample->in_rate))
    return FALSE;

  if (!gst_structure_get_int (outstructure, "channels",
          &resample->out_channels))
    return FALSE;
  if (!gst_structure_get_int (outstructure, "rate", &resample->out_rate))
    return FALSE;

  /* FIXME : Allow configuring the various resampling properties */
#define TAPS 16
  resample->res =
      av_audio_resample_init (resample->out_channels, resample->in_channels,
      resample->out_rate, resample->in_rate,
      AV_SAMPLE_FMT_S16, AV_SAMPLE_FMT_S16, TAPS, 10, 0, 0.8);
  if (resample->res == NULL)
    return FALSE;

  return TRUE;
}

static GstFlowReturn
gst_ffmpegaudioresample_transform (GstBaseTransform * trans, GstBuffer * inbuf,
    GstBuffer * outbuf)
{
  GstFFMpegAudioResample *resample = GST_FFMPEGAUDIORESAMPLE (trans);
  gint nbsamples;
  gint ret;
  guint8 *indata, *outdata;
  gsize insize, outsize;

  gst_buffer_copy_into (outbuf, inbuf, GST_BUFFER_COPY_TIMESTAMPS, 0, -1);

  indata = gst_buffer_map (inbuf, &insize, NULL, GST_MAP_READ);
  nbsamples = insize / (2 * resample->in_channels);

  GST_LOG_OBJECT (resample, "input buffer duration:%" GST_TIME_FORMAT,
      GST_TIME_ARGS (GST_BUFFER_DURATION (inbuf)));

  outdata = gst_buffer_map (outbuf, &outsize, NULL, GST_MAP_WRITE);
  GST_DEBUG_OBJECT (resample,
      "audio_resample(ctx, output:%p [size:%d], input:%p [size:%d], nbsamples:%d",
      outdata, outsize, indata, insize, nbsamples);

  ret =
      audio_resample (resample->res, (short *) outdata, (short *) indata,
      nbsamples);

  GST_DEBUG_OBJECT (resample, "audio_resample returned %d", ret);

  GST_BUFFER_DURATION (outbuf) = gst_util_uint64_scale (ret, GST_SECOND,
      resample->out_rate);

  outsize = ret * 2 * resample->out_channels;
  gst_buffer_unmap (outbuf, outdata, outsize);
  gst_buffer_unmap (inbuf, indata, insize);

  GST_LOG_OBJECT (resample, "Output buffer duration:%" GST_TIME_FORMAT,
      GST_TIME_ARGS (GST_BUFFER_DURATION (outbuf)));

  return GST_FLOW_OK;
}

gboolean
gst_ffmpegaudioresample_register (GstPlugin * plugin)
{
  return gst_element_register (plugin, "avaudioresample",
      GST_RANK_NONE, GST_TYPE_FFMPEGAUDIORESAMPLE);
}
