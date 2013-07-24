/* GStreamer
 * Copyright (C) 2013 Alessandro Decina <alessandro.d@gmail.com>
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
 * Free Software Foundation, Inc., 51 Franklin Street, Suite 500,
 * Boston, MA 02110-1335, USA.
 */
/**
 * SECTION:element-gstatdec
 *
 * AudioToolbox based decoder.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v filesrc location=file.mov ! qtdemux ! queue ! aacparse ! atdec ! autoaudiosink
 * ]|
 * Decode aac audio from a mov file
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/audio/gstaudiodecoder.h>
#include "atdec.h"

GST_DEBUG_CATEGORY_STATIC (gst_atdec_debug_category);
#define GST_CAT_DEFAULT gst_atdec_debug_category

static void gst_atdec_set_property (GObject * object,
    guint property_id, const GValue * value, GParamSpec * pspec);
static void gst_atdec_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec);
static void gst_atdec_finalize (GObject * object);

static gboolean gst_atdec_start (GstAudioDecoder * decoder);
static gboolean gst_atdec_stop (GstAudioDecoder * decoder);
static gboolean gst_atdec_set_format (GstAudioDecoder * decoder,
    GstCaps * caps);
static GstFlowReturn gst_atdec_handle_frame (GstAudioDecoder * decoder,
    GstBuffer * buffer);
static void gst_atdec_flush (GstAudioDecoder * decoder, gboolean hard);
static void gst_atdec_buffer_emptied (void *user_data,
    AudioQueueRef queue, AudioQueueBufferRef buffer);

enum
{
  PROP_0
};

static GstStaticPadTemplate gst_atdec_src_template =
    GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_AUDIO_CAPS_MAKE ("S16LE") ", layout=interleaved;"
        GST_AUDIO_CAPS_MAKE ("F32LE") ", layout=interleaved")
    );

static GstStaticPadTemplate gst_atdec_sink_template =
    GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/mpeg, mpegversion=4, framed=true, channels=[1,max];"
        "audio/mpeg, mpegversion=1, layer=3")
    );

G_DEFINE_TYPE_WITH_CODE (GstATDec, gst_atdec, GST_TYPE_AUDIO_DECODER,
    GST_DEBUG_CATEGORY_INIT (gst_atdec_debug_category, "atdec", 0,
        "debug category for atdec element"));

static GstStaticCaps aac_caps = GST_STATIC_CAPS ("audio/mpeg, mpegversion=4");
static GstStaticCaps mp3_caps =
GST_STATIC_CAPS ("audio/mpeg, mpegversion=1, layer=3");
static GstStaticCaps raw_caps = GST_STATIC_CAPS ("audio/x-raw");

static void
gst_atdec_class_init (GstATDecClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstAudioDecoderClass *audio_decoder_class = GST_AUDIO_DECODER_CLASS (klass);

  gst_element_class_add_pad_template (GST_ELEMENT_CLASS (klass),
      gst_static_pad_template_get (&gst_atdec_src_template));
  gst_element_class_add_pad_template (GST_ELEMENT_CLASS (klass),
      gst_static_pad_template_get (&gst_atdec_sink_template));

  gst_element_class_set_static_metadata (GST_ELEMENT_CLASS (klass),
      "AudioToolbox based audio decoder",
      "Codec/Decoder/Audio",
      "AudioToolbox based audio decoder",
      "Alessandro Decina <alessandro.d@gmail.com>");

  gobject_class->set_property = gst_atdec_set_property;
  gobject_class->get_property = gst_atdec_get_property;
  gobject_class->finalize = gst_atdec_finalize;
  audio_decoder_class->start = GST_DEBUG_FUNCPTR (gst_atdec_start);
  audio_decoder_class->stop = GST_DEBUG_FUNCPTR (gst_atdec_stop);
  audio_decoder_class->set_format = GST_DEBUG_FUNCPTR (gst_atdec_set_format);
  audio_decoder_class->handle_frame =
      GST_DEBUG_FUNCPTR (gst_atdec_handle_frame);
  audio_decoder_class->flush = GST_DEBUG_FUNCPTR (gst_atdec_flush);
}

static void
gst_atdec_init (GstATDec * atdec)
{
  gst_audio_decoder_set_needs_format (GST_AUDIO_DECODER (atdec), TRUE);
  atdec->queue = NULL;
}

void
gst_atdec_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GstATDec *atdec = GST_ATDEC (object);

  GST_DEBUG_OBJECT (atdec, "set_property");

  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

void
gst_atdec_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GstATDec *atdec = GST_ATDEC (object);

  GST_DEBUG_OBJECT (atdec, "get_property");

  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
gst_atdec_destroy_queue (GstATDec * atdec, gboolean drain)
{
  AudioQueueStop (atdec->queue, drain);
  AudioQueueDispose (atdec->queue, true);
  atdec->queue = NULL;
}

void
gst_atdec_finalize (GObject * object)
{
  GstATDec *atdec = GST_ATDEC (object);

  GST_DEBUG_OBJECT (atdec, "finalize");

  if (atdec->queue)
    gst_atdec_destroy_queue (atdec, FALSE);

  G_OBJECT_CLASS (gst_atdec_parent_class)->finalize (object);
}

static gboolean
gst_atdec_start (GstAudioDecoder * decoder)
{
  GstATDec *atdec = GST_ATDEC (decoder);

  GST_DEBUG_OBJECT (atdec, "start");

  return TRUE;
}

static gboolean
gst_atdec_stop (GstAudioDecoder * decoder)
{
  GstATDec *atdec = GST_ATDEC (decoder);

  gst_atdec_destroy_queue (atdec, FALSE);

  return TRUE;
}

static gboolean
can_intersect_static_caps (GstCaps * caps, GstStaticCaps * caps1)
{
  GstCaps *tmp;
  gboolean ret;

  tmp = gst_static_caps_get (caps1);
  ret = gst_caps_can_intersect (caps, tmp);
  gst_caps_unref (tmp);

  return ret;
}

static gboolean
gst_caps_to_at_format (GstCaps * caps, AudioStreamBasicDescription * format)
{
  int channels = 0;
  int rate = 0;
  GstStructure *structure;

  structure = gst_caps_get_structure (caps, 0);
  gst_structure_get_int (structure, "rate", &rate);
  gst_structure_get_int (structure, "channels", &channels);
  format->mSampleRate = rate;
  format->mChannelsPerFrame = channels;

  if (can_intersect_static_caps (caps, &aac_caps))
    format->mFormatID = kAudioFormatMPEG4AAC;
  else if (can_intersect_static_caps (caps, &mp3_caps))
    format->mFormatID = kAudioFormatMPEGLayer3;
  else if (can_intersect_static_caps (caps, &raw_caps)) {
    GstAudioFormat audio_format;
    const char *audio_format_str;

    format->mFormatID = kAudioFormatLinearPCM;
    format->mFramesPerPacket = 1;

    audio_format_str = gst_structure_get_string (structure, "format");
    if (!audio_format_str)
      audio_format_str = "S16LE";

    audio_format = gst_audio_format_from_string (audio_format_str);
    switch (audio_format) {
      case GST_AUDIO_FORMAT_S16LE:
        format->mFormatFlags =
            kLinearPCMFormatFlagIsPacked | kLinearPCMFormatFlagIsSignedInteger;
        format->mBitsPerChannel = 16;
        format->mBytesPerPacket = format->mBytesPerFrame = 2 * channels;
        break;
      case GST_AUDIO_FORMAT_F32LE:
        format->mFormatFlags =
            kLinearPCMFormatFlagIsPacked | kLinearPCMFormatFlagIsFloat;
        format->mBitsPerChannel = 32;
        format->mBytesPerPacket = format->mBytesPerFrame = 4 * channels;
        break;
      default:
        g_warn_if_reached ();
        break;
    }
  }

  return TRUE;
}

static gboolean
gst_atdec_set_format (GstAudioDecoder * decoder, GstCaps * caps)
{
  OSStatus status;
  AudioStreamBasicDescription input_format = { 0 };
  AudioStreamBasicDescription output_format = { 0 };
  GstAudioInfo output_info = { 0 };
  AudioChannelLayout output_layout = { 0 };
  GstCaps *output_caps;
  GstATDec *atdec = GST_ATDEC (decoder);

  GST_DEBUG_OBJECT (atdec, "set_format");

  if (atdec->queue)
    gst_atdec_destroy_queue (atdec, TRUE);

  /* configure input_format from caps */
  gst_caps_to_at_format (caps, &input_format);

  /* negotiate output caps */
  output_caps = gst_pad_get_allowed_caps (GST_AUDIO_DECODER_SRC_PAD (atdec));
  if (!output_caps)
    output_caps =
        gst_pad_get_pad_template_caps (GST_AUDIO_DECODER_SRC_PAD (atdec));
  output_caps = gst_caps_fixate (output_caps);

  gst_caps_set_simple (output_caps,
      "rate", G_TYPE_INT, (int) input_format.mSampleRate,
      "channels", G_TYPE_INT, input_format.mChannelsPerFrame, NULL);

  /* configure output_format from caps */
  gst_caps_to_at_format (output_caps, &output_format);

  /* set the format we want to negotiate downstream */
  gst_audio_info_from_caps (&output_info, output_caps);
  gst_audio_info_set_format (&output_info,
      output_format.mFormatFlags & kLinearPCMFormatFlagIsSignedInteger ?
      GST_AUDIO_FORMAT_S16LE : GST_AUDIO_FORMAT_F32LE,
      output_format.mSampleRate, output_format.mChannelsPerFrame, NULL);
  gst_audio_decoder_set_output_format (decoder, &output_info);
  gst_caps_unref (output_caps);

  status = AudioQueueNewOutput (&input_format, gst_atdec_buffer_emptied,
      atdec, NULL, NULL, 0, &atdec->queue);
  if (status)
    goto create_queue_error;

  /* FIXME: figure out how to map this properly */
  if (output_format.mChannelsPerFrame == 1)
    output_layout.mChannelLayoutTag = kAudioChannelLayoutTag_Mono;
  else
    output_layout.mChannelLayoutTag = kAudioChannelLayoutTag_Stereo;

  status = AudioQueueSetOfflineRenderFormat (atdec->queue,
      &output_format, &output_layout);
  if (status)
    goto set_format_error;

  status = AudioQueueStart (atdec->queue, NULL);
  if (status)
    goto start_error;

  return TRUE;

create_queue_error:
  GST_ELEMENT_ERROR (atdec, STREAM, FORMAT, (NULL),
      ("AudioQueueNewOutput returned error: %d", status));
  return FALSE;

set_format_error:
  GST_ELEMENT_ERROR (atdec, STREAM, FORMAT, (NULL),
      ("AudioQueueSetOfflineRenderFormat returned error: %d", status));
  gst_atdec_destroy_queue (atdec, FALSE);
  return FALSE;

start_error:
  GST_ELEMENT_ERROR (atdec, STREAM, FORMAT, (NULL),
      ("AudioQueueStart returned error: %d", status));
  gst_atdec_destroy_queue (atdec, FALSE);
  return FALSE;
}

static void
gst_atdec_buffer_emptied (void *user_data, AudioQueueRef queue,
    AudioQueueBufferRef buffer)
{
  AudioQueueFreeBuffer (queue, buffer);
}

static GstFlowReturn
gst_atdec_handle_frame (GstAudioDecoder * decoder, GstBuffer * buffer)
{
  AudioTimeStamp timestamp = { 0 };
  AudioStreamPacketDescription packet;
  AudioQueueBufferRef input_buffer, output_buffer;
  GstBuffer *out;
  GstMapInfo info;
  GstAudioInfo *audio_info;
  int size, out_frames;
  GstFlowReturn flow_ret = GST_FLOW_OK;
  GstATDec *atdec = GST_ATDEC (decoder);

  if (buffer == NULL)
    return GST_FLOW_OK;

  audio_info = gst_audio_decoder_get_audio_info (decoder);

  /* copy the input buffer into an AudioQueueBuffer */
  size = gst_buffer_get_size (buffer);
  AudioQueueAllocateBuffer (atdec->queue, size, &input_buffer);
  gst_buffer_extract (buffer, 0, input_buffer->mAudioData, size);
  input_buffer->mAudioDataByteSize = size;

  /* assume framed input */
  packet.mStartOffset = 0;
  packet.mVariableFramesInPacket = 1;
  packet.mDataByteSize = size;

  /* enqueue the buffer. It will get free'd once the gst_atdec_buffer_emptied
   * callback is called
   */
  AudioQueueEnqueueBuffer (atdec->queue, input_buffer, 1, &packet);

  /* figure out how many frames we need to pull out of the queue */
  out_frames = GST_CLOCK_TIME_TO_FRAMES (GST_BUFFER_DURATION (buffer),
      audio_info->rate);
  size = out_frames * audio_info->bpf;
  AudioQueueAllocateBuffer (atdec->queue, size, &output_buffer);

  /* pull the frames */
  AudioQueueOfflineRender (atdec->queue, &timestamp, output_buffer, out_frames);
  if (output_buffer->mAudioDataByteSize) {
    out =
        gst_audio_decoder_allocate_output_buffer (decoder,
        output_buffer->mAudioDataByteSize);

    gst_buffer_map (out, &info, GST_MAP_WRITE);
    memcpy (info.data, output_buffer->mAudioData,
        output_buffer->mAudioDataByteSize);
    gst_buffer_unmap (out, &info);

    flow_ret = gst_audio_decoder_finish_frame (decoder, out, 1);
  }

  AudioQueueFreeBuffer (atdec->queue, output_buffer);

  return flow_ret;
}

static void
gst_atdec_flush (GstAudioDecoder * decoder, gboolean hard)
{
  GstATDec *atdec = GST_ATDEC (decoder);

  AudioQueueFlush (atdec->queue);
}
