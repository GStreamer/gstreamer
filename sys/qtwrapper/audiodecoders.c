/*
 * GStreamer QuickTime audio decoder codecs wrapper
 * Copyright <2006, 2007> Fluendo <gstreamer@fluendo.com>
 * Copyright <2006, 2007, 2008> Pioneers of the Inevitable 
 *                              <songbird@songbirdnest.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Alternatively, the contents of this file may be used under the
 * GNU Lesser General Public License Version 2.1 (the "LGPL"), in
 * which case the following provisions apply instead of the ones
 * mentioned above:
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

#include <string.h>

#include <gst/gst.h>
#include "qtwrapper.h"
#include "codecmapping.h"
#include "qtutils.h"

#ifdef G_OS_WIN32
#include <QuickTimeComponents.h>
#else
#include <QuickTime/QuickTimeComponents.h>
#endif

#define QTWRAPPER_ADEC_PARAMS_QDATA g_quark_from_static_string("qtwrapper-adec-params")

#define NO_MORE_INPUT_DATA 42

static GstStaticPadTemplate src_templ = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-float, "
        "endianness = (int) {" G_STRINGIFY (G_BYTE_ORDER) " }, "
        "signed = (boolean) { TRUE }, "
        "width = (int) 32, "
        "depth = (int) 32, " "rate = (int) [1, MAX], "
        "channels = (int) [1, MAX]")
    );

typedef struct _QTWrapperAudioDecoder QTWrapperAudioDecoder;
typedef struct _QTWrapperAudioDecoderClass QTWrapperAudioDecoderClass;

struct _QTWrapperAudioDecoder
{
  GstElement parent;

  GstPad *sinkpad;
  GstPad *srcpad;

  /* FIXME : all following should be protected by a mutex */
  ComponentInstance adec;       /* The Audio Decoder component */
  AudioStreamBasicDescription indesc, outdesc;

  guint samplerate;
  guint channels;

  AudioBufferList *bufferlist;
  AudioStreamPacketDescription aspd[1];

  /* first time received after NEWSEGMENT */
  GstClockTime initial_time;
  /* offset in samples from the initial time */
  guint64 cur_offset;
  /* TRUE just after receiving a NEWSEGMENT */
  gboolean gotnewsegment;

  /* Data for StdAudio callbacks */
  GstBuffer *input_buffer;
};

struct _QTWrapperAudioDecoderClass
{
  GstElementClass parent_class;

  /* fourcc of the format */
  guint32 componentSubType;

  GstPadTemplate *sinktempl;
};

typedef struct _QTWrapperAudioDecoderParams QTWrapperAudioDecoderParams;

struct _QTWrapperAudioDecoderParams
{
  Component component;
  GstCaps *sinkcaps;
};

static gboolean qtwrapper_audio_decoder_sink_setcaps (GstPad * pad,
    GstCaps * caps);
static GstFlowReturn qtwrapper_audio_decoder_chain (GstPad * pad,
    GstBuffer * buf);
static gboolean qtwrapper_audio_decoder_sink_event (GstPad * pad,
    GstEvent * event);

static void
qtwrapper_audio_decoder_init (QTWrapperAudioDecoder * qtwrapper)
{
  QTWrapperAudioDecoderClass *oclass;

  oclass = (QTWrapperAudioDecoderClass *) (G_OBJECT_GET_CLASS (qtwrapper));

  /* Sink pad */
  qtwrapper->sinkpad = gst_pad_new_from_template (oclass->sinktempl, "sink");
  gst_pad_set_setcaps_function (qtwrapper->sinkpad,
      GST_DEBUG_FUNCPTR (qtwrapper_audio_decoder_sink_setcaps));
  gst_pad_set_chain_function (qtwrapper->sinkpad,
      GST_DEBUG_FUNCPTR (qtwrapper_audio_decoder_chain));
  gst_pad_set_event_function (qtwrapper->sinkpad,
      GST_DEBUG_FUNCPTR (qtwrapper_audio_decoder_sink_event));
  gst_element_add_pad (GST_ELEMENT (qtwrapper), qtwrapper->sinkpad);

  /* Source pad */
  qtwrapper->srcpad = gst_pad_new_from_static_template (&src_templ, "src");
  gst_element_add_pad (GST_ELEMENT (qtwrapper), qtwrapper->srcpad);
}

static void
clear_AudioStreamBasicDescription (AudioStreamBasicDescription * desc)
{
  desc->mSampleRate = 0;
  desc->mFormatID = 0;
  desc->mFormatFlags = 0;
  desc->mBytesPerPacket = 0;
  desc->mFramesPerPacket = 0;
  desc->mBytesPerFrame = 0;
  desc->mChannelsPerFrame = 0;
  desc->mBitsPerChannel = 0;
  desc->mReserved = 0;
}

static void
fill_indesc_mp3 (QTWrapperAudioDecoder * qtwrapper, guint32 fourcc, gint rate,
    gint channels)
{
  GST_INFO_OBJECT (qtwrapper, "Filling input description for MP3 data");
  clear_AudioStreamBasicDescription (&qtwrapper->indesc);
  /* only the samplerate is needed apparently */
  qtwrapper->indesc.mSampleRate = (double) rate;
  qtwrapper->indesc.mFormatID = kAudioFormatMPEGLayer3;
  qtwrapper->indesc.mChannelsPerFrame = channels;
}

static void
fill_indesc_aac (QTWrapperAudioDecoder * qtwrapper, guint32 fourcc, gint rate,
    gint channels)
{
  clear_AudioStreamBasicDescription (&qtwrapper->indesc);
  qtwrapper->indesc.mSampleRate = (double) rate;
  qtwrapper->indesc.mFormatID = kAudioFormatMPEG4AAC;
  /* aac always has 1024 frames per packet */
  qtwrapper->indesc.mFramesPerPacket = 1024;
  qtwrapper->indesc.mChannelsPerFrame = channels;
}

static void
fill_indesc_samr (QTWrapperAudioDecoder * qtwrapper, guint32 fourcc,
    gint channels)
{
  clear_AudioStreamBasicDescription (&qtwrapper->indesc);
  qtwrapper->indesc.mSampleRate = 8000;
  qtwrapper->indesc.mFormatID = fourcc;
  qtwrapper->indesc.mChannelsPerFrame = 1;
  qtwrapper->indesc.mFramesPerPacket = 160;
}

static void
fill_indesc_generic (QTWrapperAudioDecoder * qtwrapper, guint32 fourcc,
    gint rate, gint channels)
{
  clear_AudioStreamBasicDescription (&qtwrapper->indesc);
  qtwrapper->indesc.mSampleRate = rate;
  qtwrapper->indesc.mFormatID = fourcc;
  qtwrapper->indesc.mChannelsPerFrame = channels;
}

static void
fill_indesc_alac (QTWrapperAudioDecoder * qtwrapper, guint32 fourcc,
    gint rate, gint channels)
{
  clear_AudioStreamBasicDescription (&qtwrapper->indesc);
  qtwrapper->indesc.mSampleRate = rate;
  qtwrapper->indesc.mFormatID = fourcc;
  qtwrapper->indesc.mChannelsPerFrame = channels;

  // This has to be set, but the particular value doesn't seem to matter much
  qtwrapper->indesc.mFramesPerPacket = 4096;
}

static gpointer
make_alac_magic_cookie (GstBuffer * codec_data, gsize * len)
{
  guint8 *res;

  if (GST_BUFFER_SIZE (codec_data) < 4)
    return NULL;

  *len = 20 + GST_BUFFER_SIZE (codec_data);
  res = g_malloc0 (*len);

  /* 12 first bytes are 'frma' (format) atom with 'alac' value */
  GST_WRITE_UINT32_BE (res, 0xc);       /* Atom length: 12 bytes */
  GST_WRITE_UINT32_LE (res + 4, QT_MAKE_FOURCC_BE ('f', 'r', 'm', 'a'));
  GST_WRITE_UINT32_LE (res + 8, QT_MAKE_FOURCC_BE ('a', 'l', 'a', 'c'));

  /* Write the codec_data, but with the first four bytes reversed (different
     endianness). This is the 'alac' atom. */
  GST_WRITE_UINT32_BE (res + 12,
      GST_READ_UINT32_LE (GST_BUFFER_DATA (codec_data)));
  memcpy (res + 16, GST_BUFFER_DATA (codec_data) + 4,
      GST_BUFFER_SIZE (codec_data) - 4);

  /* Terminator atom */
  GST_WRITE_UINT32_BE (res + 12 + GST_BUFFER_SIZE (codec_data), 8);
  GST_WRITE_UINT32_BE (res + 12 + GST_BUFFER_SIZE (codec_data) + 4, 0);

  return res;
}

static gpointer
make_samr_magic_cookie (GstBuffer * codec_data, gsize * len)
{
  guint8 *res;

  *len = 48;
  res = g_malloc0 (0x30);

  /* 12 first bytes are 'frma' (format) atom with 'samr' value */
  GST_WRITE_UINT32_BE (res, 0xc);
  GST_WRITE_UINT32_LE (res + 4, QT_MAKE_FOURCC_BE ('f', 'r', 'm', 'a'));
  GST_WRITE_UINT32_LE (res + 8, QT_MAKE_FOURCC_BE ('s', 'a', 'm', 'r'));

  /* 10 bytes for 'enda' atom with 0 */
  GST_WRITE_UINT32_BE (res + 12, 10);
  GST_WRITE_UINT32_LE (res + 16, QT_MAKE_FOURCC_BE ('e', 'n', 'd', 'a'));

  /* 17(+1) bytes for the codec_data contents */
  GST_WRITE_UINT32_BE (res + 22, 18);
  memcpy (res + 26, GST_BUFFER_DATA (codec_data) + 4, 17);

  /* yes... we need to replace 'damr' by 'samr'. Blame Apple ! */
  GST_WRITE_UINT8 (res + 26, 's');

  /* Terminator atom */
  GST_WRITE_UINT32_BE (res + 40, 8);

#if DEBUG_DUMP
  gst_util_dump_mem (res, 48);
#endif

  return res;
}

static int
write_len (guint8 * buf, int val)
{
  /* This is some sort of variable-length coding, but the quicktime
   * file(s) I have here all just use a 4-byte version, so we'll do that.
   * Return the number of bytes written;
   */
  buf[0] = ((val >> 21) & 0x7f) | 0x80;
  buf[1] = ((val >> 14) & 0x7f) | 0x80;
  buf[2] = ((val >> 7) & 0x7f) | 0x80;
  buf[3] = ((val >> 0) & 0x7f);

  return 4;
}

static void
aac_parse_codec_data (GstBuffer * codec_data, gint * channels)
{
  guint8 *data = GST_BUFFER_DATA (codec_data);
  guint codec_channels;

  if (GST_BUFFER_SIZE (codec_data) < 2) {
    GST_WARNING ("Cannot parse codec_data for channel count");
    return;
  }

  codec_channels = (data[1] & 0x7f) >> 3;

  if (*channels != codec_channels) {
    GST_INFO ("Overwriting channels %d with %d", *channels, codec_channels);
    *channels = (gint) codec_channels;
  } else {
    GST_INFO ("Retaining channel count %d", codec_channels);
  }
}

/* The AAC decoder requires the entire mpeg4 audio elementary stream 
 * descriptor, which is the body (except the 4-byte version field) of
 * the quicktime 'esds' atom. However, qtdemux only passes through the 
 * (two byte, normally) payload, so we need to reconstruct the ESD */

/* TODO: Get the AAC spec, and verify this implementation */
static gpointer
make_aac_magic_cookie (GstBuffer * codec_data, gsize * len)
{
  guint8 *cookie;
  int offset = 0;
  int decoder_specific_len = GST_BUFFER_SIZE (codec_data);
  int config_len = 13 + 5 + decoder_specific_len;
  int es_len = 3 + 5 + config_len + 5 + 1;
  int total_len = es_len + 5;

  cookie = g_malloc0 (total_len);
  *len = total_len;

  /* Structured something like this:
   * [ES Descriptor
   *  [Config Descriptor
   *   [Specific Descriptor]]
   *  [Unknown]]
   */

  QT_WRITE_UINT8 (cookie + offset, 0x03);
  offset += 1;                  /* ES Descriptor tag */
  offset += write_len (cookie + offset, es_len);
  QT_WRITE_UINT16 (cookie + offset, 0);
  offset += 2;                  /* Track ID */
  QT_WRITE_UINT8 (cookie + offset, 0);
  offset += 1;                  /* Flags */

  QT_WRITE_UINT8 (cookie + offset, 0x04);
  offset += 1;                  /* Config Descriptor tag */
  offset += write_len (cookie + offset, config_len);

  /* TODO: Fix these up */
  QT_WRITE_UINT8 (cookie + offset, 0x40);
  offset += 1;                  /* object_type_id */
  QT_WRITE_UINT8 (cookie + offset, 0x15);
  offset += 1;                  /* stream_type */
  QT_WRITE_UINT24 (cookie + offset, 0x1800);
  offset += 3;                  /* buffer_size_db */
  QT_WRITE_UINT32 (cookie + offset, 128000);
  offset += 4;                  /* max_bitrate */
  QT_WRITE_UINT32 (cookie + offset, 128000);
  offset += 4;                  /* avg_bitrate */

  QT_WRITE_UINT8 (cookie + offset, 0x05);
  offset += 1;                  /* Specific Descriptor tag */
  offset += write_len (cookie + offset, decoder_specific_len);
  memcpy (cookie + offset, GST_BUFFER_DATA (codec_data), decoder_specific_len);
  offset += decoder_specific_len;

  /* TODO: What is this? 'SL descriptor' apparently, but what does that mean? */
  QT_WRITE_UINT8 (cookie + offset, 0x06);
  offset += 1;                  /* SL Descriptor tag */
  offset += write_len (cookie + offset, 1);
  QT_WRITE_UINT8 (cookie + offset, 2);
  offset += 1;

  return cookie;
}

static void
close_decoder (QTWrapperAudioDecoder * qtwrapper)
{
  if (qtwrapper->adec) {
    CloseComponent (qtwrapper->adec);
    qtwrapper->adec = NULL;
  }

  if (qtwrapper->bufferlist) {
    DestroyAudioBufferList (qtwrapper->bufferlist);
    qtwrapper->bufferlist = NULL;
  }
}

static gboolean
open_decoder (QTWrapperAudioDecoder * qtwrapper, GstCaps * caps,
    GstCaps ** othercaps)
{
  gboolean ret = FALSE;
  QTWrapperAudioDecoderClass *oclass;

  /* TODO: these will be used as the output rate/channels for formats that
   * don't supply these in the caps. This isn't very nice!
   */
  gint channels = 2;
  gint rate = 44100;

  OSStatus status;
  GstStructure *s;
  gchar *tmp;
  const GValue *value;
  GstBuffer *codec_data = NULL;
  gboolean have_esds = FALSE;

  /* Clean up any existing decoder */
  close_decoder (qtwrapper);

  tmp = gst_caps_to_string (caps);
  GST_LOG_OBJECT (qtwrapper, "caps: %s", tmp);
  g_free (tmp);

  /* extract rate/channels information from the caps */
  s = gst_caps_get_structure (caps, 0);
  gst_structure_get_int (s, "rate", &rate);
  gst_structure_get_int (s, "channels", &channels);

  /* get codec_data */
  if ((value = gst_structure_get_value (s, "codec_data"))) {
    codec_data = GST_BUFFER_CAST (gst_value_get_mini_object (value));
  }

  oclass = (QTWrapperAudioDecoderClass *) (G_OBJECT_GET_CLASS (qtwrapper));

  if (codec_data
      && oclass->componentSubType == QT_MAKE_FOURCC_LE ('m', 'p', '4', 'a')) {
    /* QuickTime/iTunes creates AAC files with the wrong channel count in the header,
       so parse that out of the codec data if we can.
     */
    aac_parse_codec_data (codec_data, &channels);
  }

  /* If the quicktime demuxer gives us a full esds atom, use that instead of 
   * the codec_data */
  if ((value = gst_structure_get_value (s, "quicktime_esds"))) {
    have_esds = TRUE;
    codec_data = GST_BUFFER_CAST (gst_value_get_mini_object (value));
  }
#if DEBUG_DUMP
  if (codec_data)
    gst_util_dump_mem (GST_BUFFER_DATA (codec_data),
        GST_BUFFER_SIZE (codec_data));
#endif


  GST_INFO_OBJECT (qtwrapper, "rate:%d, channels:%d", rate, channels);

  GST_INFO_OBJECT (qtwrapper, "componentSubType is %" GST_FOURCC_FORMAT,
      QT_FOURCC_ARGS (oclass->componentSubType));

  /* Setup the input format description, some format require special handling */
  switch (oclass->componentSubType) {
    case QT_MAKE_FOURCC_LE ('.', 'm', 'p', '3'):
      fill_indesc_mp3 (qtwrapper, oclass->componentSubType, rate, channels);
      break;
    case QT_MAKE_FOURCC_LE ('m', 'p', '4', 'a'):
      fill_indesc_aac (qtwrapper, oclass->componentSubType, rate, channels);
      break;
    case QT_MAKE_FOURCC_LE ('s', 'a', 'm', 'r'):
      fill_indesc_samr (qtwrapper, oclass->componentSubType, channels);
      rate = 8000;
      break;
    case QT_MAKE_FOURCC_LE ('a', 'l', 'a', 'c'):
      fill_indesc_alac (qtwrapper, oclass->componentSubType, rate, channels);
      break;
    default:
      fill_indesc_generic (qtwrapper, oclass->componentSubType, rate, channels);
      break;
  }

#if DEBUG_DUMP
  gst_util_dump_mem ((gpointer) & qtwrapper->indesc,
      sizeof (AudioStreamBasicDescription));
#endif

  qtwrapper->samplerate = rate;
  qtwrapper->channels = channels;

  /* Create an instance of SCAudio */
  status = OpenADefaultComponent (StandardCompressionType,
      StandardCompressionSubTypeAudio, &qtwrapper->adec);
  if (status) {
    GST_WARNING_OBJECT (qtwrapper,
        "Error instantiating SCAudio component: %ld", status);
    qtwrapper->adec = NULL;
    goto beach;
  }

  /* This is necessary to make setting the InputBasicDescription succeed;
     without it SCAudio only accepts PCM as input. Presumably a bug in
     QuickTime. Thanks to Arek for figuring this one out!
   */
  {
    QTAtomContainer audiosettings = NULL;

    SCGetSettingsAsAtomContainer (qtwrapper->adec, &audiosettings);
    SCSetSettingsFromAtomContainer (qtwrapper->adec, audiosettings);

    /* TODO: Figure out if disposing of the QTAtomContainer is needed here */
  }

  /* Set the input description info on the SCAudio instance */
  status = QTSetComponentProperty (qtwrapper->adec, kQTPropertyClass_SCAudio,
      kQTSCAudioPropertyID_InputBasicDescription,
      sizeof (qtwrapper->indesc), &qtwrapper->indesc);
  if (status) {
    GST_WARNING_OBJECT (qtwrapper,
        "Error setting input description on SCAudio: %ld", status);

    GST_ELEMENT_ERROR (qtwrapper, STREAM, NOT_IMPLEMENTED,
        ("A QuickTime error occurred trying to decode this stream"),
        ("QuickTime returned error status %lx", status));
    goto beach;
  }

  /* TODO: we can select a channel layout here, figure out if we want to */

  /* if we have codec_data, give it to the converter ! */
  if (codec_data) {
    gsize len = 0;
    gpointer magiccookie;

    switch (oclass->componentSubType) {
        /* Some decoders want the 'magic cookie' in a different format from how
         * gstreamer represents it. So, convert...
         */
      case QT_MAKE_FOURCC_LE ('s', 'a', 'm', 'r'):
        magiccookie = make_samr_magic_cookie (codec_data, &len);
        break;
      case QT_MAKE_FOURCC_LE ('a', 'l', 'a', 'c'):
        magiccookie = make_alac_magic_cookie (codec_data, &len);
        break;
      case QT_MAKE_FOURCC_LE ('m', 'p', '4', 'a'):
        if (!have_esds) {
          magiccookie = make_aac_magic_cookie (codec_data, &len);
          break;
        }
        /* Else: fallthrough */
      default:
        len = GST_BUFFER_SIZE (codec_data);
        magiccookie = GST_BUFFER_DATA (codec_data);
        break;
    }

    if (magiccookie) {
      GST_LOG_OBJECT (qtwrapper, "Setting magic cookie %p of size %"
          G_GSIZE_FORMAT, magiccookie, len);

#if DEBUG_DUMP
      gst_util_dump_mem (magiccookie, len);
#endif

      status =
          QTSetComponentProperty (qtwrapper->adec, kQTPropertyClass_SCAudio,
          kQTSCAudioPropertyID_InputMagicCookie, len, magiccookie);
      if (status) {
        GST_WARNING_OBJECT (qtwrapper, "Error setting extra codec data: %ld",
            status);
        goto beach;
      }

      g_free (magiccookie);
    }
  }

  /* Set output to be interleaved raw PCM */
  {
    OSType outputFormat = kAudioFormatLinearPCM;
    SCAudioFormatFlagsRestrictions restrictions = { 0 };

    /* Set the mask in order to set this flag to zero */
    restrictions.formatFlagsMask =
        kAudioFormatFlagIsFloat | kAudioFormatFlagIsBigEndian;
    restrictions.formatFlagsValues = kAudioFormatFlagIsFloat;

    status = QTSetComponentProperty (qtwrapper->adec, kQTPropertyClass_SCAudio,
        kQTSCAudioPropertyID_ClientRestrictedLPCMFlags,
        sizeof (restrictions), &restrictions);
    if (status) {
      GST_WARNING_OBJECT (qtwrapper, "Error setting PCM to interleaved: %ld",
          status);
      goto beach;
    }

    status = QTSetComponentProperty (qtwrapper->adec, kQTPropertyClass_SCAudio,
        kQTSCAudioPropertyID_ClientRestrictedCompressionFormatList,
        sizeof (outputFormat), &outputFormat);
    if (status) {
      GST_WARNING_OBJECT (qtwrapper, "Error setting output to PCM: %ld",
          status);
      goto beach;
    }
  }

  qtwrapper->outdesc.mSampleRate = 0;   /* Use recommended; we read this out later */
  qtwrapper->outdesc.mFormatID = kAudioFormatLinearPCM;
  qtwrapper->outdesc.mFormatFlags = kAudioFormatFlagIsFloat;
  qtwrapper->outdesc.mBytesPerPacket = 0;
  qtwrapper->outdesc.mFramesPerPacket = 0;
  qtwrapper->outdesc.mBytesPerFrame = 4 * channels;
  qtwrapper->outdesc.mChannelsPerFrame = channels;
  qtwrapper->outdesc.mBitsPerChannel = 32;
  qtwrapper->outdesc.mReserved = 0;

  status = QTSetComponentProperty (qtwrapper->adec, kQTPropertyClass_SCAudio,
      kQTSCAudioPropertyID_BasicDescription,
      sizeof (qtwrapper->outdesc), &qtwrapper->outdesc);
  if (status) {
    GST_WARNING_OBJECT (qtwrapper, "Error setting output description: %ld",
        status);
    goto beach;
  }

  status = QTGetComponentProperty (qtwrapper->adec, kQTPropertyClass_SCAudio,
      kQTSCAudioPropertyID_BasicDescription,
      sizeof (qtwrapper->outdesc), &qtwrapper->outdesc, NULL);

  if (status) {
    GST_WARNING_OBJECT (qtwrapper,
        "Failed to get output audio description: %ld", status);
    ret = FALSE;
    goto beach;
  }

  if (qtwrapper->outdesc.mFormatID != kAudioFormatLinearPCM     /*||
                                                                   (qtwrapper->outdesc.mFormatFlags & kAudioFormatFlagIsFloat) !=
                                                                   kAudioFormatFlagIsFloat */ ) {
    GST_WARNING_OBJECT (qtwrapper, "Output is not floating point PCM");
    ret = FALSE;
    goto beach;
  }

  qtwrapper->samplerate = (int) qtwrapper->outdesc.mSampleRate;
  qtwrapper->channels = qtwrapper->outdesc.mChannelsPerFrame;
  GST_DEBUG_OBJECT (qtwrapper, "Output is %d Hz, %d channels",
      qtwrapper->samplerate, qtwrapper->channels);

  /* Create output bufferlist, big enough for 200ms of audio */
  GST_DEBUG_OBJECT (qtwrapper, "Allocating bufferlist for %d channels",
      channels);
  qtwrapper->bufferlist =
      AllocateAudioBufferList (channels,
      qtwrapper->samplerate / 5 * qtwrapper->channels * 4);

  /* Create output caps matching the format the component is giving us */
  *othercaps = gst_caps_new_simple ("audio/x-raw-float",
      "endianness", G_TYPE_INT, G_BYTE_ORDER,
      "signed", G_TYPE_BOOLEAN, TRUE,
      "width", G_TYPE_INT, 32,
      "depth", G_TYPE_INT, 32,
      "rate", G_TYPE_INT, qtwrapper->samplerate, "channels", G_TYPE_INT,
      qtwrapper->channels, NULL);

  ret = TRUE;

beach:
  return ret;
}

static gboolean
qtwrapper_audio_decoder_sink_setcaps (GstPad * pad, GstCaps * caps)
{
  QTWrapperAudioDecoder *qtwrapper;
  gboolean ret = FALSE;
  GstCaps *othercaps = NULL;

  qtwrapper = (QTWrapperAudioDecoder *) gst_pad_get_parent (pad);

  GST_LOG_OBJECT (qtwrapper, "caps:%" GST_PTR_FORMAT, caps);

  /* 1. open decoder */
  if (!(open_decoder (qtwrapper, caps, &othercaps)))
    goto beach;

  /* 2. set caps downstream */
  ret = gst_pad_set_caps (qtwrapper->srcpad, othercaps);

beach:
  if (othercaps)
    gst_caps_unref (othercaps);
  gst_object_unref (qtwrapper);
  return ret;
}

static OSStatus
process_buffer_cb (ComponentInstance inAudioConverter,
    UInt32 * ioNumberDataPackets,
    AudioBufferList * ioData,
    AudioStreamPacketDescription ** outDataPacketDescription,
    QTWrapperAudioDecoder * qtwrapper)
{
  GST_LOG_OBJECT (qtwrapper,
      "ioNumberDataPackets:%lu, iodata:%p, outDataPacketDescription:%p",
      *ioNumberDataPackets, ioData, outDataPacketDescription);
  if (outDataPacketDescription)
    GST_LOG ("*outDataPacketDescription:%p", *outDataPacketDescription);

  GST_LOG ("mNumberBuffers : %u", (guint32) ioData->mNumberBuffers);
  GST_LOG ("mData:%p , mDataByteSize:%u",
      ioData->mBuffers[0].mData, (guint32) ioData->mBuffers[0].mDataByteSize);

  ioData->mBuffers[0].mData = NULL;
  ioData->mBuffers[0].mDataByteSize = 0;

  *ioNumberDataPackets = 1;

  if (qtwrapper->input_buffer && GST_BUFFER_SIZE (qtwrapper->input_buffer)) {
    ioData->mBuffers[0].mData = GST_BUFFER_DATA (qtwrapper->input_buffer);
    ioData->mBuffers[0].mDataByteSize =
        GST_BUFFER_SIZE (qtwrapper->input_buffer);

    /* if we have a valid outDataPacketDescription, we need to fill it */
    if (outDataPacketDescription) {
      qtwrapper->aspd[0].mStartOffset = 0;
      qtwrapper->aspd[0].mVariableFramesInPacket = 0;
      qtwrapper->aspd[0].mDataByteSize =
          GST_BUFFER_SIZE (qtwrapper->input_buffer);
      *outDataPacketDescription = qtwrapper->aspd;
    }

    GST_LOG_OBJECT (qtwrapper, "returning %d bytes at %p",
        GST_BUFFER_SIZE (qtwrapper->input_buffer), ioData->mBuffers[0].mData);

    qtwrapper->input_buffer = 0;
    return noErr;
  }

  GST_LOG_OBJECT (qtwrapper,
      "No remaining input data, returning NO_MORE_INPUT_DATA");

  return NO_MORE_INPUT_DATA;
}

static GstFlowReturn
qtwrapper_audio_decoder_chain (GstPad * pad, GstBuffer * buf)
{
  GstFlowReturn ret = GST_FLOW_OK;
  QTWrapperAudioDecoder *qtwrapper;
  GstBuffer *outbuf;
  OSStatus status;
  guint32 outsamples;
  guint32 savedbytes;
  guint32 realbytes;

  qtwrapper = (QTWrapperAudioDecoder *) gst_pad_get_parent (pad);

  if (!qtwrapper->adec) {
    GST_WARNING_OBJECT (qtwrapper, "QTWrapper not initialised");
    goto beach;
  }

  GST_LOG_OBJECT (qtwrapper,
      "buffer:%p , timestamp:%" GST_TIME_FORMAT " ,size:%d", buf,
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)), GST_BUFFER_SIZE (buf));

#if DEBUG_DUMP
  gst_util_dump_mem (GST_BUFFER_DATA (buf), GST_BUFFER_SIZE (buf));
#endif

  if (qtwrapper->gotnewsegment) {

    GST_DEBUG_OBJECT (qtwrapper, "SCAudioReset()");

    SCAudioReset (qtwrapper->adec);

    /* some formats can give us a better initial time using the buffer
     * timestamp. */
    if (GST_CLOCK_TIME_IS_VALID (GST_BUFFER_TIMESTAMP (buf)))
      qtwrapper->initial_time = GST_BUFFER_TIMESTAMP (buf);

    qtwrapper->gotnewsegment = FALSE;
  }

  outsamples = qtwrapper->bufferlist->mBuffers[0].mDataByteSize / 8;
  savedbytes = qtwrapper->bufferlist->mBuffers[0].mDataByteSize;

  qtwrapper->input_buffer = buf;

  do {
    GST_LOG_OBJECT (qtwrapper,
        "Calling SCAudioFillBuffer(outsamples:%d , outdata:%p)", outsamples,
        qtwrapper->bufferlist->mBuffers[0].mData);

    /* Ask SCAudio to give us data ! */
    status = SCAudioFillBuffer (qtwrapper->adec,
        (SCAudioInputDataProc) process_buffer_cb,
        qtwrapper, (UInt32 *) & outsamples, qtwrapper->bufferlist, NULL);

    if ((status != noErr) && (status != NO_MORE_INPUT_DATA)) {
      if (status < 0)
        GST_WARNING_OBJECT (qtwrapper,
            "Error in SCAudioFillBuffer() : %d", (gint32) status);
      else
        GST_WARNING_OBJECT (qtwrapper,
            "Error in SCAudioFillBuffer() : %" GST_FOURCC_FORMAT,
            QT_FOURCC_ARGS (status));
      ret = GST_FLOW_ERROR;
      goto beach;
    }

    realbytes = qtwrapper->bufferlist->mBuffers[0].mDataByteSize;

    GST_LOG_OBJECT (qtwrapper, "We now have %d samples [%d bytes]",
        outsamples, realbytes);

    qtwrapper->bufferlist->mBuffers[0].mDataByteSize = savedbytes;

    if (!outsamples)
      goto beach;

    /* 4. Create buffer and copy data in it */
    ret = gst_pad_alloc_buffer (qtwrapper->srcpad, qtwrapper->cur_offset,
        realbytes, GST_PAD_CAPS (qtwrapper->srcpad), &outbuf);
    if (ret != GST_FLOW_OK)
      goto beach;

    /* copy data from bufferlist to output buffer */
    memmove (GST_BUFFER_DATA (outbuf),
        qtwrapper->bufferlist->mBuffers[0].mData, realbytes);

    /* 5. calculate timestamp and duration */
    GST_BUFFER_TIMESTAMP (outbuf) =
        qtwrapper->initial_time + gst_util_uint64_scale_int (GST_SECOND,
        (gint) qtwrapper->cur_offset, qtwrapper->samplerate);
    GST_BUFFER_SIZE (outbuf) = realbytes;
    GST_BUFFER_DURATION (outbuf) =
        gst_util_uint64_scale_int (GST_SECOND,
        realbytes / (qtwrapper->channels * 4), qtwrapper->samplerate);

    GST_LOG_OBJECT (qtwrapper,
        "timestamp:%" GST_TIME_FORMAT ", duration:%" GST_TIME_FORMAT
        "offset:%lld, offset_end:%lld",
        GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (outbuf)),
        GST_TIME_ARGS (GST_BUFFER_DURATION (outbuf)),
        GST_BUFFER_OFFSET (outbuf), GST_BUFFER_OFFSET_END (outbuf));

    qtwrapper->cur_offset += outsamples;

    /* 6. push buffer downstream */

    ret = gst_pad_push (qtwrapper->srcpad, outbuf);
    if (ret != GST_FLOW_OK)
      goto beach;

    GST_DEBUG_OBJECT (qtwrapper,
        "Read %d bytes, could have read up to %d bytes", realbytes, savedbytes);
  } while (status != NO_MORE_INPUT_DATA);

beach:
  gst_buffer_unref (buf);
  gst_object_unref (qtwrapper);
  return ret;
}

static gboolean
qtwrapper_audio_decoder_sink_event (GstPad * pad, GstEvent * event)
{
  QTWrapperAudioDecoder *qtwrapper;
  gboolean ret = FALSE;

  qtwrapper = (QTWrapperAudioDecoder *) gst_pad_get_parent (pad);

  GST_LOG_OBJECT (qtwrapper, "event:%s", GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
      /* TODO: Flush events should reset the decoder component */
    case GST_EVENT_NEWSEGMENT:{
      gint64 start, stop, position;
      gboolean update;
      gdouble rate;
      GstFormat format;

      GST_LOG ("We've got a newsegment");
      gst_event_parse_new_segment (event, &update, &rate, &format, &start,
          &stop, &position);

      /* if the format isn't time, we need to create a new time newsegment */
      /* FIXME : This is really bad, we should convert the values properly to time */
      if (format != GST_FORMAT_TIME) {
        GstEvent *newevent;

        GST_WARNING_OBJECT (qtwrapper,
            "Original event wasn't in GST_FORMAT_TIME, creating new fake one.");

        start = 0;

        newevent =
            gst_event_new_new_segment (update, rate, GST_FORMAT_TIME, start,
            GST_CLOCK_TIME_NONE, start);
        gst_event_unref (event);
        event = newevent;
      }

      qtwrapper->initial_time = start;
      qtwrapper->cur_offset = 0;

      GST_LOG ("initial_time is now %" GST_TIME_FORMAT, GST_TIME_ARGS (start));

      if (qtwrapper->adec)
        qtwrapper->gotnewsegment = TRUE;

      break;
    }
    default:
      break;
  }

  ret = gst_pad_push_event (qtwrapper->srcpad, event);

  gst_object_unref (qtwrapper);
  return TRUE;
}

static void
qtwrapper_audio_decoder_base_init (QTWrapperAudioDecoderClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  gchar *name = NULL;
  gchar *info = NULL;
  char *longname, *description;
  ComponentDescription desc;
  QTWrapperAudioDecoderParams *params;

  params = (QTWrapperAudioDecoderParams *)
      g_type_get_qdata (G_OBJECT_CLASS_TYPE (klass),
      QTWRAPPER_ADEC_PARAMS_QDATA);
  g_assert (params);

  get_name_info_from_component (params->component, &desc, &name, &info);

  /* Fill in details */
  longname =
      g_strdup_printf ("QTWrapper SCAudio Audio Decoder : %s",
      GST_STR_NULL (name));
  description =
      g_strdup_printf ("QTWrapper SCAudio wrapper for decoder: %s",
      GST_STR_NULL (info));
  gst_element_class_set_metadata (element_class,
      longname, "Codec/Decoder/Audio", description,
      "Fluendo <gstreamer@fluendo.com>, "
      "Pioneers of the Inevitable <songbird@songbirdnest.com>");

  g_free (longname);
  g_free (description);
  g_free (name);
  g_free (info);

  /* Add pad templates */
  klass->sinktempl = gst_pad_template_new ("sink", GST_PAD_SINK,
      GST_PAD_ALWAYS, params->sinkcaps);
  gst_element_class_add_pad_template (element_class, klass->sinktempl);
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_templ));

  /* Store class-global values */
  klass->componentSubType = desc.componentSubType;
}

static void
qtwrapper_audio_decoder_dispose (GObject * object)
{
  QTWrapperAudioDecoder *qtwrapper = (QTWrapperAudioDecoder *) object;
  QTWrapperAudioDecoderClass *oclass =
      (QTWrapperAudioDecoderClass *) (G_OBJECT_GET_CLASS (qtwrapper));
  GObjectClass *parent_class = g_type_class_peek_parent (oclass);

  close_decoder (qtwrapper);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
qtwrapper_audio_decoder_class_init (QTWrapperAudioDecoderClass * klass)
{
  GObjectClass *object_class;

  object_class = (GObjectClass *) klass;

  object_class->dispose = qtwrapper_audio_decoder_dispose;
}

gboolean
qtwrapper_audio_decoders_register (GstPlugin * plugin)
{
  gboolean res = TRUE;
  Component componentID = NULL;

  ComponentDescription desc = {
    kSoundDecompressor, 0, 0, 0, 0
  };

  GTypeInfo typeinfo = {
    sizeof (QTWrapperAudioDecoderClass),
    (GBaseInitFunc) qtwrapper_audio_decoder_base_init,
    NULL,
    (GClassInitFunc) qtwrapper_audio_decoder_class_init,
    NULL,
    NULL,
    sizeof (QTWrapperAudioDecoder),
    0,
    (GInstanceInitFunc) qtwrapper_audio_decoder_init,
  };

  /* Find all SoundDecompressors ! */
  GST_DEBUG ("There are %ld decompressors available", CountComponents (&desc));

  /* loop over SoundDecompressors */
  do {
    componentID = FindNextComponent (componentID, &desc);

    GST_LOG ("componentID : %p", componentID);

    if (componentID) {
      ComponentDescription thisdesc;
      gchar *name = NULL, *info = NULL;
      GstCaps *caps = NULL;
      gchar *type_name = NULL;
      GType type;
      QTWrapperAudioDecoderParams *params = NULL;

      if (!(get_name_info_from_component (componentID, &thisdesc, &name,
                  &info)))
        goto next;

      GST_LOG (" name:%s", GST_STR_NULL (name));
      GST_LOG (" info:%s", GST_STR_NULL (info));

      GST_LOG (" type:%" GST_FOURCC_FORMAT,
          QT_FOURCC_ARGS (thisdesc.componentType));
      GST_LOG (" subtype:%" GST_FOURCC_FORMAT,
          QT_FOURCC_ARGS (thisdesc.componentSubType));
      GST_LOG (" manufacturer:%" GST_FOURCC_FORMAT,
          QT_FOURCC_ARGS (thisdesc.componentManufacturer));

      if (!(caps =
              fourcc_to_caps (QT_READ_UINT32 (&thisdesc.componentSubType))))
        goto next;

      type_name = g_strdup_printf ("qtwrapperaudiodec_%" GST_FOURCC_FORMAT,
          QT_FOURCC_ARGS (thisdesc.componentSubType));
      g_strdelimit (type_name, " .", '_');

      if (g_type_from_name (type_name)) {
        GST_WARNING ("We already have a registered plugin for %s", type_name);
        goto next;
      }

      params = g_new0 (QTWrapperAudioDecoderParams, 1);
      params->component = componentID;
      params->sinkcaps = gst_caps_ref (caps);

      type = g_type_register_static (GST_TYPE_ELEMENT, type_name, &typeinfo, 0);
      /* Store params in type qdata */
      g_type_set_qdata (type, QTWRAPPER_ADEC_PARAMS_QDATA, (gpointer) params);

      /* register type */
      if (!gst_element_register (plugin, type_name, GST_RANK_MARGINAL, type)) {
        g_warning ("Failed to register %s", type_name);;
        g_type_set_qdata (type, QTWRAPPER_ADEC_PARAMS_QDATA, NULL);
        g_free (params);
        res = FALSE;
        goto next;
      }

    next:
      if (name)
        g_free (name);
      if (info)
        g_free (info);
      if (type_name)
        g_free (type_name);
      if (caps)
        gst_caps_unref (caps);
    }

  } while (componentID && res);

  return res;
}
