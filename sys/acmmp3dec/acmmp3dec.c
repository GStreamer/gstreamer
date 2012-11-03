/* GStreamer
 * Copyright (C) 2008 Pioneers of the Inevitable <songbird@songbirdnest.com>
 *
 * Authors: Michael Smith <msmith@songbirdnest.com>
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

#include <windows.h>
#include <mmreg.h>
#include <msacm.h>

#include <gst/gst.h>
#include <gst/riff/riff-media.h>

#define ACM_BUFFER_SIZE (64 * 1024)

#define GST_TYPE_ACM_MP3_DEC \
      (acmmp3dec_get_type())
#define GST_ACM_MP3_DEC(obj) \
      (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_ACM_MP3_DEC,ACMMP3Dec))

#define GST_CAT_DEFAULT acmmp3dec_debug
GST_DEBUG_CATEGORY_STATIC (acmmp3dec_debug);

static GstStaticPadTemplate acmmp3dec_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "depth = (int)16, "
        "width = (int)16, "
        "endianness = (int)" G_STRINGIFY (G_BYTE_ORDER) ", "
        "signed = (boolean)TRUE, "
        "channels = (int) [1,2], " "rate = (int)[1, MAX]")
    );

static GstStaticPadTemplate acmmp3dec_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/mpeg, "
        "mpegversion = (int)1, "
        "layer = (int)3, "
        "rate = (int){ 8000, 11025, 12000, 16000, 22050, 24000, "
        "              32000, 44100, 48000 }, "
        "channels = (int)[1,2], " "parsed = (boolean) true")
    );

typedef struct _ACMMP3DecClass
{
  GstElementClass parent_class;
} ACMMP3DecClass;

typedef struct _ACMMP3Dec
{
  GstElement parent;

  GstPad *sinkpad;
  GstPad *srcpad;

  gboolean is_setup;

  MPEGLAYER3WAVEFORMAT infmt;
  WAVEFORMATEX outfmt;

  HACMSTREAM stream;
  ACMSTREAMHEADER header;

  /* Next timestamp to push */
  GstClockTime timestamp;

  /* From received caps */
  int rate;
  int channels;

  GstCaps *output_caps;
} ACMMP3Dec;

GType acmmp3dec_get_type (void);

GST_BOILERPLATE (ACMMP3Dec, acmmp3dec, GstElement, GST_TYPE_ELEMENT);

static GstCaps *
acmmp3dec_caps_from_format (WAVEFORMATEX * fmt)
{
  return gst_riff_create_audio_caps (fmt->wFormatTag,
      NULL, (gst_riff_strf_auds *) fmt, NULL, NULL, NULL);
}

static gboolean
acmmp3dec_set_input_format (ACMMP3Dec * dec)
{
  dec->infmt.wfx.wFormatTag = WAVE_FORMAT_MPEGLAYER3;
  dec->infmt.wfx.nChannels = dec->channels;
  dec->infmt.wfx.nSamplesPerSec = dec->rate;
  dec->infmt.wfx.nAvgBytesPerSec = 8000;        /* Has to be set, but actual
                                                   value doesn't matter */
  dec->infmt.wfx.nBlockAlign = 1;
  dec->infmt.wfx.wBitsPerSample = 0;
  dec->infmt.wfx.cbSize = MPEGLAYER3_WFX_EXTRA_BYTES;
  dec->infmt.wID = MPEGLAYER3_ID_MPEG;
  dec->infmt.fdwFlags = MPEGLAYER3_FLAG_PADDING_OFF;
  dec->infmt.nBlockSize = 1;    /* Needs to be non-zero, actual
                                   value doesn't matter */
  dec->infmt.nFramesPerBlock = 1;
  dec->infmt.nCodecDelay = 0;

  return TRUE;
}

static gboolean
acmmp3dec_set_output_format (ACMMP3Dec * dec)
{
  dec->outfmt.wFormatTag = WAVE_FORMAT_PCM;
  dec->outfmt.nChannels = dec->channels;
  dec->outfmt.nSamplesPerSec = dec->rate;
  dec->outfmt.nAvgBytesPerSec = 2 * dec->channels * dec->rate;
  dec->outfmt.nBlockAlign = 2 * dec->channels;
  dec->outfmt.wBitsPerSample = 16;
  dec->outfmt.cbSize = 0;

  return TRUE;
}

static gboolean
acmmp3dec_setup (ACMMP3Dec * dec)
{
  MMRESULT res;
  int destBufferSize;

  acmmp3dec_set_input_format (dec);
  acmmp3dec_set_output_format (dec);

  res =
      acmStreamOpen (&dec->stream, NULL, (LPWAVEFORMATEX) & dec->infmt,
      &dec->outfmt, 0, 0, 0, 0);
  if (res) {
    GST_WARNING_OBJECT (dec, "Failed to open ACM stream: %d", res);
    return FALSE;
  }

  dec->header.cbStruct = sizeof (ACMSTREAMHEADER);
  dec->header.fdwStatus = 0;
  dec->header.dwUser = 0;

  dec->header.pbSrc = (BYTE *) g_malloc (ACM_BUFFER_SIZE);
  dec->header.cbSrcLength = ACM_BUFFER_SIZE;
  dec->header.cbSrcLengthUsed = 0;
  dec->header.dwSrcUser = 0;

  /* Ask what buffer size we need to use for our output */
  acmStreamSize (dec->stream, ACM_BUFFER_SIZE,
      (LPDWORD) & destBufferSize, ACM_STREAMSIZEF_SOURCE);

  dec->header.pbDst = (BYTE *) g_malloc (destBufferSize);
  dec->header.cbDstLength = destBufferSize;
  dec->header.cbDstLengthUsed = 0;
  dec->header.dwDstUser = 0;

  res = acmStreamPrepareHeader (dec->stream, &dec->header, 0);
  if (res) {
    GST_WARNING_OBJECT (dec, "Failed to prepare ACM stream: %x", res);
    return FALSE;
  }

  dec->output_caps = acmmp3dec_caps_from_format (&dec->outfmt);
  if (dec->output_caps) {
    gst_pad_set_caps (dec->srcpad, dec->output_caps);
  }

  dec->timestamp = GST_CLOCK_TIME_NONE;
  dec->is_setup = TRUE;
  return TRUE;
}

static void
acmmp3dec_teardown (ACMMP3Dec * dec)
{
  if (dec->output_caps) {
    gst_caps_unref (dec->output_caps);
    dec->output_caps = NULL;
  }

  if (dec->header.fdwStatus & ACMSTREAMHEADER_STATUSF_PREPARED)
    acmStreamUnprepareHeader (dec->stream, &dec->header, 0);
  if (dec->header.pbSrc)
    g_free (dec->header.pbSrc);
  if (dec->header.pbDst)
    g_free (dec->header.pbDst);
  memset (&dec->header, 0, sizeof (dec->header));

  if (dec->stream) {
    acmStreamClose (dec->stream, 0);
    dec->stream = 0;
  }

  dec->timestamp = GST_CLOCK_TIME_NONE;
  dec->is_setup = FALSE;
}

static gboolean
acmmp3dec_sink_setcaps (GstPad * pad, GstCaps * caps)
{
  ACMMP3Dec *dec = (ACMMP3Dec *) GST_PAD_PARENT (pad);
  GstStructure *structure;
  gboolean ret;

  structure = gst_caps_get_structure (caps, 0);
  gst_structure_get_int (structure, "channels", &dec->channels);
  gst_structure_get_int (structure, "rate", &dec->rate);

  if (dec->is_setup)
    acmmp3dec_teardown (dec);
  ret = acmmp3dec_setup (dec);

  return ret;
}

static GstFlowReturn
acmmp3dec_push_output (ACMMP3Dec * dec)
{
  GstFlowReturn ret = GST_FLOW_OK;

  if (dec->header.cbDstLengthUsed > 0) {
    GstBuffer *outbuf = gst_buffer_new_and_alloc (dec->header.cbDstLengthUsed);
    memcpy (GST_BUFFER_DATA (outbuf), dec->header.pbDst,
        dec->header.cbDstLengthUsed);

    if (dec->timestamp != GST_CLOCK_TIME_NONE)
      GST_BUFFER_TIMESTAMP (outbuf) = dec->timestamp;
    GST_BUFFER_DURATION (outbuf) =
        gst_util_uint64_scale_int (GST_BUFFER_SIZE (outbuf), GST_SECOND,
        dec->rate * dec->channels * 2);

    GST_DEBUG_OBJECT (dec, "decoded buffer has ts %d, duration %d",
        (int) (GST_BUFFER_TIMESTAMP (outbuf)),
        (int) (GST_BUFFER_DURATION (outbuf)));

    gst_buffer_set_caps (outbuf, dec->output_caps);

    if (dec->timestamp != GST_CLOCK_TIME_NONE)
      dec->timestamp += GST_BUFFER_DURATION (outbuf);

    GST_DEBUG_OBJECT (dec, "Pushing %lu byte decoded buffer",
        dec->header.cbDstLengthUsed);
    ret = gst_pad_push (dec->srcpad, outbuf);
  } else
    GST_DEBUG_OBJECT (dec, "Not pushing decoded buffer, no output");


  return ret;
}

static GstFlowReturn
acmmp3dec_chain (GstPad * pad, GstBuffer * buf)
{
  MMRESULT res;
  ACMMP3Dec *dec = (ACMMP3Dec *) GST_PAD_PARENT (pad);
  guchar *data = GST_BUFFER_DATA (buf);
  gint len = GST_BUFFER_SIZE (buf);
  GstFlowReturn ret = GST_FLOW_OK;

  if (len > ACM_BUFFER_SIZE) {
    GST_WARNING_OBJECT (dec, "Impossibly large mp3 frame!");
    ret = GST_FLOW_ERROR;
    goto done;
  }

  if (GST_BUFFER_TIMESTAMP (buf) != GST_CLOCK_TIME_NONE &&
      (dec->timestamp == GST_CLOCK_TIME_NONE ||
          GST_BUFFER_FLAG_IS_SET (buf, GST_BUFFER_FLAG_DISCONT))) {
    dec->timestamp = GST_BUFFER_TIMESTAMP (buf);
  }

  memcpy (dec->header.pbSrc, data, len);
  dec->header.cbSrcLength = len;

  /* Now we have a buffer ready to go */
  res = acmStreamConvert (dec->stream, &dec->header,
      ACM_STREAMCONVERTF_BLOCKALIGN);
  if (res) {
    GST_WARNING_OBJECT (dec, "Failed to decode data");
    ret = GST_FLOW_OK;          /* Maybe it was just a corrupt frame */
    goto done;
  }

  if (dec->header.cbSrcLengthUsed > 0) {
    if (dec->header.cbSrcLengthUsed != dec->header.cbSrcLength) {
      GST_WARNING_OBJECT (dec, "ACM decoder didn't consume all data!");
      /* We could handle this, but it shouldn't be possible, so don't try
       * for now */
      ret = GST_FLOW_ERROR;
      goto done;
    }

    /* Write out any data produced */
    acmmp3dec_push_output (dec);
  }

done:
  gst_buffer_unref (buf);

  return ret;
}

static GstFlowReturn
acmmp3dec_finish_stream (ACMMP3Dec * dec)
{
  MMRESULT res;
  GstFlowReturn ret = GST_FLOW_OK;

  dec->header.cbSrcLength = 0;

  /* Flush out any remaining data internal to the decoder */
  res = acmStreamConvert (dec->stream, &dec->header,
      ACM_STREAMCONVERTF_BLOCKALIGN | ACM_STREAMCONVERTF_END);
  if (res) {
    GST_WARNING_OBJECT (dec, "Failed to decode data");
    return ret;
  }

  ret = acmmp3dec_push_output (dec);
  return ret;
}

static gboolean
acmmp3dec_sink_event (GstPad * pad, GstEvent * event)
{
  ACMMP3Dec *dec = (ACMMP3Dec *) GST_PAD_PARENT (pad);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
      acmmp3dec_finish_stream (dec);
      break;
    case GST_EVENT_FLUSH_STOP:
      if (dec->stream)
        acmStreamReset (dec->stream, 0);
      dec->timestamp = GST_CLOCK_TIME_NONE;
      break;
    default:
      break;
  }


  return gst_pad_push_event (dec->srcpad, event);
}

static void
acmmp3dec_dispose (GObject * obj)
{
  ACMMP3Dec *dec = (ACMMP3Dec *) obj;

  if (dec->is_setup)
    acmmp3dec_teardown (dec);

  G_OBJECT_CLASS (parent_class)->dispose (obj);
}

static void
acmmp3dec_init (ACMMP3Dec * dec, ACMMP3DecClass * decclass)
{
  dec->sinkpad =
      gst_pad_new_from_static_template (&acmmp3dec_sink_template, "sink");
  gst_pad_set_setcaps_function (dec->sinkpad,
      GST_DEBUG_FUNCPTR (acmmp3dec_sink_setcaps));
  gst_pad_set_chain_function (dec->sinkpad,
      GST_DEBUG_FUNCPTR (acmmp3dec_chain));
  gst_pad_set_event_function (dec->sinkpad,
      GST_DEBUG_FUNCPTR (acmmp3dec_sink_event));
  gst_element_add_pad (GST_ELEMENT (dec), dec->sinkpad);

  dec->srcpad =
      gst_pad_new_from_static_template (&acmmp3dec_src_template, "src");
  gst_element_add_pad (GST_ELEMENT (dec), dec->srcpad);

}

static void
acmmp3dec_class_init (ACMMP3DecClass * klass)
{
  GObjectClass *gobjectclass = (GObjectClass *) klass;
  gobjectclass->dispose = acmmp3dec_dispose;
}

static void
acmmp3dec_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&acmmp3dec_sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&acmmp3dec_src_template));
  gst_element_class_set_static_metadata (element_class, "ACM MP3 decoder",
      "Codec/Decoder/Audio",
      "Decode MP3 using ACM decoder",
      "Pioneers of the Inevitable <songbird@songbirdnest.com");
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (acmmp3dec_debug, "acmmp3dec", 0, "ACM Decoders");

  GST_INFO ("Registering ACM MP3 decoder");
  if (!gst_element_register (plugin, "acmmp3dec", GST_RANK_PRIMARY,
          GST_TYPE_ACM_MP3_DEC)) {
    return FALSE;
  }

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    acmmp3dec,
    "ACM MP3 Decoder",
    plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
