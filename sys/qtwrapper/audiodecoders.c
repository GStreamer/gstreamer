/*
 * GStreamer QuickTime audio decoder codecs wrapper
 * Copyright <2006, 2007> Fluendo <gstreamer@fluendo.com>
 * Copyright <2006, 2007> Pioneers of the Inevitable <songbird@songbirdnest.com>
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <QuickTime/Movies.h>
#include <AudioToolbox/AudioToolbox.h>

#include <gst/base/gstadapter.h>
#include "qtwrapper.h"
#include "codecmapping.h"
#include "qtutils.h"

#define QTWRAPPER_ADEC_PARAMS_QDATA g_quark_from_static_string("qtwrapper-adec-params")

static GstStaticPadTemplate src_templ = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-float, "
        "endianness = (int) {" G_STRINGIFY (G_BYTE_ORDER) " }, "
        "signed = (boolean) { TRUE }, "
        "width = (int) 32, "
        "depth = (int) 32, " "rate = (int) 44100, " "channels = (int) 2")
    );

typedef struct _QTWrapperAudioDecoder QTWrapperAudioDecoder;
typedef struct _QTWrapperAudioDecoderClass QTWrapperAudioDecoderClass;

struct _QTWrapperAudioDecoder
{
  GstElement parent;

  GstPad *sinkpad;
  GstPad *srcpad;

  /* FIXME : all following should be protected by a mutex */
  AudioConverterRef aconv;
  AudioStreamBasicDescription indesc, outdesc;

  guint samplerate;
  guint channels;
  AudioBufferList *bufferlist;

  /* first time received after NEWSEGMENT */
  GstClockTime initial_time;
  /* offset in samples from the initial time */
  guint64 cur_offset;
  /* TRUE just after receiving a NEWSEGMENT */
  gboolean gotnewsegment;

  /* temporary output data */
  gpointer tmpdata;

  /* buffer previously used by the decoder */
  gpointer prevdata;

  GstAdapter *adapter;
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
qtwrapper_audio_decoder_base_init (QTWrapperAudioDecoderClass * klass)
{
  GstElementDetails details;
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  gchar *name = NULL;
  gchar *info = NULL;
  ComponentDescription desc;
  QTWrapperAudioDecoderParams *params;

  params = (QTWrapperAudioDecoderParams *)
      g_type_get_qdata (G_OBJECT_CLASS_TYPE (klass),
      QTWRAPPER_ADEC_PARAMS_QDATA);
  g_assert (params);

  get_name_info_from_component (params->component, &desc, &name, &info);

  /* Fill in details */
  details.longname = g_strdup_printf ("QTWrapper Audio Decoder : %s", name);
  details.klass = "Codec/Decoder/Audio";
  details.description = info;
  details.author = "Fluendo <gstreamer@fluendo.com>, "
      "Pioneers of the Inevitable <songbird@songbirdnest.com>";
  gst_element_class_set_details (element_class, &details);

  g_free (details.longname);
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
qtwrapper_audio_decoder_class_init (QTWrapperAudioDecoderClass * klass)
{
  /* FIXME : don't we need some vmethod implementations here ?? */
}

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

  qtwrapper->adapter = gst_adapter_new ();
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

}

static void
fill_indesc_mp3 (QTWrapperAudioDecoder * qtwrapper, guint32 fourcc, gint rate,
    gint channels)
{
  GST_LOG ("...");
  clear_AudioStreamBasicDescription (&qtwrapper->indesc);
  /* only the samplerate is needed apparently */
  qtwrapper->indesc.mSampleRate = rate;
  qtwrapper->indesc.mFormatID = kAudioFormatMPEGLayer3;
  qtwrapper->indesc.mChannelsPerFrame = channels;
}

static void
fill_indesc_aac (QTWrapperAudioDecoder * qtwrapper, guint32 fourcc, gint rate,
    gint channels)
{
  clear_AudioStreamBasicDescription (&qtwrapper->indesc);
  qtwrapper->indesc.mSampleRate = rate;
  qtwrapper->indesc.mFormatID = kAudioFormatMPEG4AAC;
  /* aac always has 1024 bytes per packet */
  qtwrapper->indesc.mBytesPerPacket = 1024;
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

static gpointer
make_samr_magic_cookie (GstBuffer * codec_data, gsize * len)
{
  gpointer res;

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

  /* padding 8 bytes */
  GST_WRITE_UINT32_BE (res + 40, 8);

#if DEBUG_DUMP
  gst_util_dump_mem (res, 48);
#endif

  return res;
}

static gboolean
open_decoder (QTWrapperAudioDecoder * qtwrapper, GstCaps * caps,
    GstCaps ** othercaps)
{
  gboolean ret = FALSE;
  QTWrapperAudioDecoderClass *oclass;
  gint channels = 2;
  gint rate = 44100;
  gint depth = 32;
  OSErr oserr;
  OSStatus status;
  GstStructure *s;
  gchar *tmp;
  const GValue *value;
  GstBuffer *codec_data = NULL;

  tmp = gst_caps_to_string (caps);
  GST_LOG_OBJECT (qtwrapper, "caps: %s", tmp);
  g_free (tmp);

  /* extract rate/channels information from the caps */
  s = gst_caps_get_structure (caps, 0);
  gst_structure_get_int (s, "rate", &rate);
  gst_structure_get_int (s, "channels", &channels);

  /* depth isn't compulsory */
  if (!(gst_structure_get_int (s, "depth", &depth)))
    gst_structure_get_int (s, "samplesize", &depth);

  /* get codec_data */
  if ((value = gst_structure_get_value (s, "codec_data"))) {
    codec_data = GST_BUFFER_CAST (gst_value_get_mini_object (value));
  }

  /* If the quicktime demuxer gives us a full esds atom, use that instead of the codec_data */
  if ((value = gst_structure_get_value (s, "quicktime_esds"))) {
    codec_data = GST_BUFFER_CAST (gst_value_get_mini_object (value));
  }
#if DEBUG_DUMP
  if (codec_data)
    gst_util_dump_mem (GST_BUFFER_DATA (codec_data),
        GST_BUFFER_SIZE (codec_data));
#endif


  GST_LOG ("rate:%d, channels:%d, depth:%d", rate, channels, depth);

  oclass = (QTWrapperAudioDecoderClass *) (G_OBJECT_GET_CLASS (qtwrapper));

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
    default:
      fill_indesc_generic (qtwrapper, oclass->componentSubType, rate, channels);
      break;
  }

#if DEBUG_DUMP
  gst_util_dump_mem (&qtwrapper->indesc, sizeof (AudioStreamBasicDescription));
#endif

  /* we're forcing output to stereo 44.1kHz */
  rate = 44100;
  channels = 2;

  qtwrapper->samplerate = rate;
  qtwrapper->channels = channels;

  /* Setup the output format description */
  qtwrapper->outdesc.mSampleRate = rate;
  qtwrapper->outdesc.mFormatID = kAudioFormatLinearPCM;
  qtwrapper->outdesc.mFormatFlags = kAudioFormatFlagIsFloat;
#if G_BYTE_ORDER == G_BIG_ENDIAN
  qtwrapper->outdesc.mFormatFlags |= kAudioFormatFlagIsBigEndian;
#endif
  qtwrapper->outdesc.mBytesPerPacket = channels * 4;    /* ?? */
  qtwrapper->outdesc.mFramesPerPacket = 1;
  qtwrapper->outdesc.mBytesPerFrame = channels * 4;     /* channels * bytes-per-samples */
  qtwrapper->outdesc.mChannelsPerFrame = channels;
  qtwrapper->outdesc.mBitsPerChannel = 32;

  /* Create an AudioConverter */
  status = AudioConverterNew (&qtwrapper->indesc,
      &qtwrapper->outdesc, &qtwrapper->aconv);
  if (status != noErr) {
    GST_WARNING_OBJECT (qtwrapper,
        "Error when calling AudioConverterNew() : %" GST_FOURCC_FORMAT,
        QT_FOURCC_ARGS (status));
    goto beach;
  }

  /* if we have codec_data, give it to the converter ! */
  if (codec_data) {
    gsize len;
    gpointer magiccookie;

    if (oclass->componentSubType == QT_MAKE_FOURCC_LE ('s', 'a', 'm', 'r')) {
      magiccookie = make_samr_magic_cookie (codec_data, &len);
    } else {
      len = GST_BUFFER_SIZE (codec_data);
      magiccookie = GST_BUFFER_DATA (codec_data);
    }
    GST_LOG_OBJECT (qtwrapper, "Setting magic cookie %p of size %"
        G_GSIZE_FORMAT, magiccookie, len);
    oserr = AudioConverterSetProperty (qtwrapper->aconv,
        kAudioConverterDecompressionMagicCookie, len, magiccookie);
    if (oserr != noErr) {
      GST_WARNING_OBJECT (qtwrapper, "Error setting extra codec data !");
      goto beach;
    }
  }

  /* Create output bufferlist */
  qtwrapper->bufferlist = AllocateAudioBufferList (channels,
      rate * channels * 4 / 20);

  /* Create output caps */
  *othercaps = gst_caps_new_simple ("audio/x-raw-float",
      "endianness", G_TYPE_INT, G_BYTE_ORDER,
      "signed", G_TYPE_BOOLEAN, TRUE,
      "width", G_TYPE_INT, 32,
      "depth", G_TYPE_INT, 32,
      "rate", G_TYPE_INT, rate, "channels", G_TYPE_INT, channels, NULL);

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
process_buffer_cb (AudioConverterRef inAudioConverter,
    UInt32 * ioNumberDataPackets,
    AudioBufferList * ioData,
    AudioStreamPacketDescription ** outDataPacketDescription,
    QTWrapperAudioDecoder * qtwrapper)
{
  gint len;
  AudioStreamPacketDescription aspd[200];

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
  if (qtwrapper->prevdata)
    g_free (qtwrapper->prevdata);

  len = gst_adapter_available (qtwrapper->adapter);

  if (len) {
    ioData->mBuffers[0].mData = gst_adapter_take (qtwrapper->adapter, len);
    qtwrapper->prevdata = ioData->mBuffers[0].mData;

    /* if we have a valid outDataPacketDescription, we need to fill it */
    if (outDataPacketDescription) {
      /* mStartOffset : the number of bytes from the start of the buffer to the
       * beginning of the packet. */
      aspd[0].mStartOffset = 0;
      aspd[1].mStartOffset = 0;
      /* mVariableFramesInPacket : the number of samples frames of data in the
       * packet. For formats with a constant number of frames per packet, this
       * field is set to 0. */
      aspd[0].mVariableFramesInPacket = 0;
      aspd[1].mVariableFramesInPacket = 0;
      /* mDataByteSize : The number of bytes in the packet. */
      aspd[0].mDataByteSize = len;
      aspd[1].mDataByteSize = 0;
      GST_LOG ("ASPD: mStartOffset:%lld, mVariableFramesInPacket:%u, "
          "mDataByteSize:%u", aspd[0].mStartOffset,
          (guint32) aspd[0].mVariableFramesInPacket,
          (guint32) aspd[0].mDataByteSize);
      *outDataPacketDescription = (AudioStreamPacketDescription *) & aspd;
    }

  } else {
    qtwrapper->prevdata = NULL;
  }

  ioData->mBuffers[0].mDataByteSize = len;

  GST_LOG_OBJECT (qtwrapper, "returning %d bytes at %p",
      len, ioData->mBuffers[0].mData);

  if (!len)
    return 42;
  return noErr;
}

static GstFlowReturn
qtwrapper_audio_decoder_chain (GstPad * pad, GstBuffer * buf)
{
  GstFlowReturn ret = GST_FLOW_OK;
  QTWrapperAudioDecoder *qtwrapper;

  qtwrapper = (QTWrapperAudioDecoder *) gst_pad_get_parent (pad);

  GST_LOG_OBJECT (qtwrapper,
      "buffer:%p , timestamp:%" GST_TIME_FORMAT " ,size:%d", buf,
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)), GST_BUFFER_SIZE (buf));

#if DEBUG_DUMP
  gst_util_dump_mem (GST_BUFFER_DATA (buf), GST_BUFFER_SIZE (buf));
#endif

  if (qtwrapper->gotnewsegment) {

    GST_DEBUG_OBJECT (qtwrapper, "AudioConverterReset()");

    AudioConverterReset (qtwrapper->aconv);

    /* some formats can give us a better initial time using the buffer
     * timestamp. */
    if (GST_CLOCK_TIME_IS_VALID (GST_BUFFER_TIMESTAMP (buf)))
      qtwrapper->initial_time = GST_BUFFER_TIMESTAMP (buf);

    qtwrapper->gotnewsegment = FALSE;
  }

  /* stack in adapter */
  gst_adapter_push (qtwrapper->adapter, buf);

  /* do we have enough to decode at least one frame ? */
  while (gst_adapter_available (qtwrapper->adapter)) {
    GstBuffer *outbuf;
    OSStatus status;
    guint32 outsamples = qtwrapper->bufferlist->mBuffers[0].mDataByteSize / 8;
    guint32 savedbytes = qtwrapper->bufferlist->mBuffers[0].mDataByteSize;
    guint32 realbytes;


    GST_LOG_OBJECT (qtwrapper, "Calling FillBuffer(outsamples:%d , outdata:%p)",
        outsamples, qtwrapper->bufferlist->mBuffers[0].mData);

    /* Ask AudioConverter to give us data ! */
    status = AudioConverterFillComplexBuffer (qtwrapper->aconv,
        (AudioConverterComplexInputDataProc) process_buffer_cb,
        qtwrapper, (UInt32 *) & outsamples, qtwrapper->bufferlist, NULL);

    if ((status != noErr) && (status != 42)) {
      if (status < 0)
        GST_WARNING_OBJECT (qtwrapper,
            "Error in AudioConverterFillComplexBuffer() : %d", (gint32) status);
      else
        GST_WARNING_OBJECT (qtwrapper,
            "Error in AudioConverterFillComplexBuffer() : %" GST_FOURCC_FORMAT,
            QT_FOURCC_ARGS (status));
      ret = GST_FLOW_ERROR;
      goto beach;
    }

    realbytes = qtwrapper->bufferlist->mBuffers[0].mDataByteSize;

    GST_LOG_OBJECT (qtwrapper, "We now have %d samples [%d bytes]",
        outsamples, realbytes);

    qtwrapper->bufferlist->mBuffers[0].mDataByteSize = savedbytes;

    if (!outsamples)
      break;

    /* 4. Create buffer and copy data in it */
    ret = gst_pad_alloc_buffer (qtwrapper->srcpad, qtwrapper->cur_offset,
        realbytes, GST_PAD_CAPS (qtwrapper->srcpad), &outbuf);
    if (ret != GST_FLOW_OK)
      goto beach;

    /* copy data from bufferlist to output buffer */
    g_memmove (GST_BUFFER_DATA (outbuf),
        qtwrapper->bufferlist->mBuffers[0].mData, realbytes);

    /* 5. calculate timestamp and duration */
    GST_BUFFER_TIMESTAMP (outbuf) =
        qtwrapper->initial_time + gst_util_uint64_scale_int (GST_SECOND,
        qtwrapper->cur_offset, qtwrapper->samplerate);
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
  }

beach:
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

      gst_adapter_clear (qtwrapper->adapter);

      GST_LOG ("initial_time is now %" GST_TIME_FORMAT, GST_TIME_ARGS (start));

      if (qtwrapper->aconv)
        qtwrapper->gotnewsegment = TRUE;

      /* FIXME : reset adapter */
      break;
    }
    default:
      break;
  }

  ret = gst_pad_push_event (qtwrapper->srcpad, event);

  gst_object_unref (qtwrapper);
  return TRUE;
}

gboolean
qtwrapper_audio_decoders_register (GstPlugin * plugin)
{
  gboolean res = TRUE;
  OSErr result;
  Component componentID = NULL;
  ComponentDescription desc = {
    'sdec', 0, 0, 0, 0
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

  /* Initialize quicktime environment */
  result = EnterMovies ();
  if (result != noErr) {
    GST_ERROR ("Error initializing QuickTime environment");
    res = FALSE;
    goto beach;
  }

  /* Find all ImageDecoders ! */
  GST_DEBUG ("There are %ld decompressors available", CountComponents (&desc));

  /* loop over ImageDecoders */
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

      GST_LOG (" name:%s", name);
      GST_LOG (" info:%s", info);

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

beach:
  return res;
}
