/*
 * Copyright (C) 2024 Piotr Brzeziński <piotr@centricular.com>
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
/**
 * SECTION:element-atenc
 * @title: atenc
 *
 * AudioToolbox based encoder.
 * ## Example launch line
 * |[
 * gst-launch-1.0 -v audiotestsrc ! atenc ! mp4mux ! filesink location=test.m4a
 * ]|
 * Encodes audio from audiotestsrc and writes it to a file.
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstatenc.h"

enum
{
  PROP_0,
  PROP_BITRATE,
  PROP_RATE_CONTROL,
  PROP_VBR_QUALITY,
};

#define DEFAULT_BITRATE       0
#define DEFAULT_RATE_CONTROL  GST_ATENC_RATE_CONTROL_CONSTANT
#define DEFAULT_VBR_QUALITY   65

#define ES_DESCRIPTOR_TAG          0x03
#define DECODER_CONFIG_DESC_TAG    0x04
#define DECODER_SPECIFIC_INFO_TAG  0x05

#define SAMPLE_RATES " 8000, " \
                    "11025, " \
                    "12000, " \
                    "16000, " \
                    "22050, " \
                    "24000, " \
                    "32000, " \
                    "44100, " \
                    "48000 "
/* Higher sample rates were failing when initializing the encoder.
 * Probably supported only in specific circumstances, hard to find documentation about that. */

/* *INDENT-OFF* */
static const GstATEncLayout aac_layouts[] = {
  {
    1, kAudioChannelLayoutTag_Mono, { GST_AUDIO_CHANNEL_POSITION_MONO }}, {
    2, kAudioChannelLayoutTag_Stereo, { 
      GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
      GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT }}, {
    /* C L R */
    3, kAudioChannelLayoutTag_AAC_3_0, {
      GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER,
      GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
      GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT }}, {
    /* C L R Cs */
    4, kAudioChannelLayoutTag_AAC_4_0, {
      GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER,
      GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
      GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
      GST_AUDIO_CHANNEL_POSITION_REAR_CENTER }}, {
    /* C L R Ls Rs */
    5, kAudioChannelLayoutTag_AAC_5_0, {
      GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER,
      GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
      GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
      GST_AUDIO_CHANNEL_POSITION_SURROUND_LEFT,
      GST_AUDIO_CHANNEL_POSITION_SURROUND_RIGHT }}, {
    /* C L R Ls Rs Lfe */
    6, kAudioChannelLayoutTag_AAC_5_1, {
      GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER,
      GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
      GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
      GST_AUDIO_CHANNEL_POSITION_SURROUND_LEFT,
      GST_AUDIO_CHANNEL_POSITION_SURROUND_RIGHT,
      GST_AUDIO_CHANNEL_POSITION_LFE1 }}, {
    /* C L R Ls Rs Cs */
    6, kAudioChannelLayoutTag_AAC_6_0, {
      GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER,
      GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
      GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
      GST_AUDIO_CHANNEL_POSITION_SURROUND_LEFT,
      GST_AUDIO_CHANNEL_POSITION_SURROUND_RIGHT,
      GST_AUDIO_CHANNEL_POSITION_REAR_CENTER }}, {
    /* C L R Ls Rs Cs Lfe */
    7, kAudioChannelLayoutTag_AAC_6_1, {
      GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER,
      GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
      GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
      GST_AUDIO_CHANNEL_POSITION_SURROUND_LEFT,
      GST_AUDIO_CHANNEL_POSITION_SURROUND_RIGHT,
      GST_AUDIO_CHANNEL_POSITION_REAR_CENTER,
      GST_AUDIO_CHANNEL_POSITION_LFE1 }}, {
    /* C L R Ls Rs Rls Rrs */
    7, kAudioChannelLayoutTag_AAC_7_0, {
      GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER,
      GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
      GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
      GST_AUDIO_CHANNEL_POSITION_SURROUND_LEFT,
      GST_AUDIO_CHANNEL_POSITION_SURROUND_RIGHT,
      GST_AUDIO_CHANNEL_POSITION_REAR_LEFT,
      GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT }}, {
    /* C Lc Rc L R Ls Rs Lfe */
    8, kAudioChannelLayoutTag_AAC_7_1, {
      GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER,
      GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT_OF_CENTER,
      GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT_OF_CENTER,
      GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
      GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
      GST_AUDIO_CHANNEL_POSITION_SURROUND_LEFT,
      GST_AUDIO_CHANNEL_POSITION_SURROUND_RIGHT,
      GST_AUDIO_CHANNEL_POSITION_LFE1 }}, {
    /* C L R Ls Rs Rls Rrs LFE */
    8, kAudioChannelLayoutTag_AAC_7_1_B, {
      GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER,
      GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
      GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
      GST_AUDIO_CHANNEL_POSITION_SURROUND_LEFT,
      GST_AUDIO_CHANNEL_POSITION_SURROUND_RIGHT,
      GST_AUDIO_CHANNEL_POSITION_REAR_LEFT,
      GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT,
      GST_AUDIO_CHANNEL_POSITION_LFE1 }}, {
    /* C L R Ls Rs LFE Vhl Vhr */
    8, kAudioChannelLayoutTag_AAC_7_1_C, {
      GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER,
      GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
      GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
      GST_AUDIO_CHANNEL_POSITION_SURROUND_LEFT,
      GST_AUDIO_CHANNEL_POSITION_SURROUND_RIGHT,
      GST_AUDIO_CHANNEL_POSITION_LFE1,
      GST_AUDIO_CHANNEL_POSITION_TOP_FRONT_LEFT,
      GST_AUDIO_CHANNEL_POSITION_TOP_FRONT_RIGHT }}, {
    /* Only used when iterating through all positions */
    0, kAudioChannelLayoutTag_Unknown, { 0 } }
};
/* *INDENT-ON* */

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw, "
        "format = (string) " GST_AUDIO_NE (S16) ", "
        "layout = (string) interleaved, "
        "rate = (int) { " SAMPLE_RATES " }, channels = (int) [ 1, 8 ]")
    );

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/mpeg, "
        "mpegversion = (int) 4, "
        "rate = (int) { " SAMPLE_RATES " }, "
        "channels = (int) [ 1, 8 ], "
        "stream-format = (string) raw, "
        "profile = (string) lc, framed = (boolean) true")
    );

GST_DEBUG_CATEGORY_STATIC (gst_atenc_debug);
#define GST_CAT_DEFAULT gst_atenc_debug

G_DEFINE_TYPE (GstATEnc, gst_atenc, GST_TYPE_AUDIO_ENCODER);
GST_ELEMENT_REGISTER_DEFINE (atenc, "atenc", GST_RANK_PRIMARY, GST_TYPE_ATENC);

#define GST_ATENC_RATE_CONTROL (gst_atenc_rate_control_get_type ())
static GType
gst_atenc_rate_control_get_type (void)
{
  static GType atenc_rate_control_type = 0;
  static const GEnumValue types[] = {
    {GST_ATENC_RATE_CONTROL_CONSTANT, "Constant bitrate", "cbr"},
    {GST_ATENC_RATE_CONTROL_LONG_TERM_AVERAGE, "Long-term-average bitrate",
        "lta"},
    {GST_ATENC_RATE_CONTROL_VARIABLE_CONSTRAINED,
        "Constrained variable bitrate", "cvbr"},
    {GST_ATENC_RATE_CONTROL_VARIABLE, "Variable bitrate", "vbr"},
    {0, NULL, NULL}
  };

  if (!atenc_rate_control_type)
    atenc_rate_control_type =
        g_enum_register_static ("GstATEncRateControl", types);

  return atenc_rate_control_type;
}

static void
gst_atenc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstATEnc *self = GST_ATENC (object);

  switch (prop_id) {
    case PROP_BITRATE:
      self->bitrate = g_value_get_uint (value);
      break;
    case PROP_RATE_CONTROL:
      self->rate_control = g_value_get_enum (value);
      break;
    case PROP_VBR_QUALITY:
      self->vbr_quality = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_atenc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstATEnc *self = GST_ATENC (object);

  switch (prop_id) {
    case PROP_BITRATE:
      g_value_set_uint (value, self->bitrate);
      break;
    case PROP_RATE_CONTROL:
      g_value_set_enum (value, self->rate_control);
      break;
    case PROP_VBR_QUALITY:
      g_value_set_uint (value, self->vbr_quality);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_atenc_start (GstAudioEncoder * enc)
{
  GstATEnc *self = GST_ATENC (enc);

  GST_DEBUG_OBJECT (self, "Starting encoder");

  self->input_queue = gst_queue_array_new (0);
  gst_queue_array_set_clear_func (self->input_queue,
      (GDestroyNotify) gst_buffer_unref);

  return TRUE;
}

static void
gst_atenc_flush (GstAudioEncoder * enc)
{
  GstATEnc *self = GST_ATENC (enc);

  GST_DEBUG_OBJECT (self, "Flushing encoder");
  AudioConverterReset (self->converter);

  gst_queue_array_clear (self->input_queue);
}

static gboolean
gst_atenc_stop (GstAudioEncoder * enc)
{
  GstATEnc *self = GST_ATENC (enc);

  GST_DEBUG_OBJECT (self, "Stopping encoder");

  gst_atenc_flush (enc);

  if (self->converter) {
    AudioConverterDispose (self->converter);
    self->converter = NULL;
  }

  gst_queue_array_free (self->input_queue);
  self->input_queue = NULL;

  if (self->used_buffer) {
    gst_audio_buffer_unmap (self->used_buffer);
    gst_buffer_unref (self->used_buffer->buffer);
    g_free (self->used_buffer);
    self->used_buffer = NULL;
  }

  return TRUE;
}

static GstCaps *
gst_atenc_get_caps (GstAudioEncoder * enc, GstCaps * filter)
{
  GstCaps *layout_caps, *ret, *caps = gst_caps_new_empty ();
  const GstATEncLayout *layout;
  guint64 channel_mask;

  for (layout = aac_layouts; layout->channels; layout++) {
    layout_caps =
        gst_caps_make_writable (gst_pad_get_pad_template_caps
        (GST_AUDIO_ENCODER_SINK_PAD (enc)));

    if (layout->channels == 1) {
      gst_caps_set_simple (layout_caps, "channels", G_TYPE_INT,
          layout->channels, NULL);
    } else {
      gst_audio_channel_positions_to_mask (layout->positions, layout->channels,
          FALSE, &channel_mask);
      gst_caps_set_simple (layout_caps, "channels", G_TYPE_INT,
          layout->channels, "channel-mask", GST_TYPE_BITMASK, channel_mask,
          NULL);
    }

    gst_caps_append (caps, layout_caps);
  }

  ret = gst_audio_encoder_proxy_getcaps (enc, caps, filter);
  gst_caps_unref (caps);

  return ret;
}

static OSStatus
gst_atenc_fill_buffer (AudioConverterRef converter, UInt32 * packets_amount,
    AudioBufferList * buffers, AudioStreamPacketDescription ** desc,
    void *user_data)
{
  GstATEnc *self = GST_ATENC (user_data);
  GstBuffer *buf;
  GstAudioBuffer *audio_buf;
  GstAudioInfo *audio_info;
  UInt32 wanted_samples = *packets_amount;

  /* We can now safely clean up the buffer that was previously passed to AT */
  if (self->used_buffer) {
    gst_audio_buffer_unmap (self->used_buffer);
    gst_buffer_unref (self->used_buffer->buffer);
    g_free (self->used_buffer);
    self->used_buffer = NULL;
  }

  /* See https://developer.apple.com/library/archive/qa/qa1317/_index.html
   * packets_amount indicates how much data is expected to be filled in.
   *
   * The way this is set up, we tell the base class how many samples AT will expect,
   * and it will provide us with that much. Only exception is at the end of stream,
   * where there might not be enough data. Thankfully, if we signal EOS, AT will encode
   * whatever it got as input, without needing to silence-pad to the expected amount.
   *
   * In case of less data than packets_amount => set that to the actual value and return noErr
   * No data currently available, but more is expected => packets_amount=0 and return 1
   * No data available and input got EOS => packets_amount=0 and return noErr
   */
  buf = gst_queue_array_pop_head (self->input_queue);
  if (!buf) {
    *packets_amount = 0;

    if (self->input_eos) {
      GST_DEBUG_OBJECT (self, "No more input data, returning noErr");
      return noErr;
    } else {
      GST_LOG_OBJECT (self, "No input buffer yet, waiting for more data");
      return 1;
    }
  }

  /* We can only unmap the audio_buffer in the next callback, but in the meantime 
   * the base class can invalidate the underlying buffer. Ref it manually to ensure
   * it lives long enough. */
  gst_buffer_ref (buf);
  audio_info = gst_audio_encoder_get_audio_info (GST_AUDIO_ENCODER (self));
  audio_buf = g_malloc0 (sizeof (GstAudioBuffer));
  gst_audio_buffer_map (audio_buf, audio_info, buf, GST_MAP_READ);

  /* Pushing this as a pointer instead of using the _struct() variants
   * because GstAudioBuffer contains self-references, so we'd get dangling pointers otherwise. */
  self->used_buffer = audio_buf;

  buffers->mNumberBuffers = 1;
  buffers->mBuffers[0].mNumberChannels = GST_AUDIO_INFO_CHANNELS (audio_info);
  buffers->mBuffers[0].mDataByteSize = GST_AUDIO_BUFFER_PLANE_SIZE (audio_buf);
  buffers->mBuffers[0].mData = GST_AUDIO_BUFFER_PLANE_DATA (audio_buf, 0);

  *packets_amount = audio_buf->n_samples;
  GST_LOG_OBJECT (self, "Wanted %d packets, filled %d", wanted_samples,
      *packets_amount);

  return noErr;
}

static GstFlowReturn
gst_atenc_handle_frame (GstAudioEncoder * enc, GstBuffer * buffer)
{
  GstATEnc *self = GST_ATENC (enc);
  OSStatus status;
  GstBuffer *outbuf;
  GstFlowReturn ret;
  GstMapInfo map_info;
  GstAudioInfo *audio_info;
  AudioBufferList out_bufs = { 0 };
  AudioStreamPacketDescription out_desc = { 0 };
  UInt32 out_packets;

  if (!buffer) {
    self->input_eos = TRUE;
    GST_DEBUG_OBJECT (self, "No input buffer, draining encoder");
  } else {
    self->input_eos = FALSE;
    gst_queue_array_push_tail (self->input_queue, buffer);
    GST_LOG ("Pushed buffer to queue");
  }

  outbuf =
      gst_audio_encoder_allocate_output_buffer (enc,
      self->max_output_buffer_size);
  if (!outbuf) {
    GST_ERROR_OBJECT (self, "Failed to allocate output buffer");
    return GST_FLOW_ERROR;
  }

  gst_buffer_map (outbuf, &map_info, GST_MAP_WRITE);

  audio_info = gst_audio_encoder_get_audio_info (enc);
  out_bufs.mNumberBuffers = 1;
  out_bufs.mBuffers[0].mNumberChannels = GST_AUDIO_INFO_CHANNELS (audio_info);
  out_bufs.mBuffers[0].mDataByteSize = self->max_output_buffer_size;
  out_bufs.mBuffers[0].mData = map_info.data;
  out_packets = 1;

  status =
      AudioConverterFillComplexBuffer (self->converter, gst_atenc_fill_buffer,
      self, &out_packets, &out_bufs, &out_desc);

  /* gst_atenc_fill_buffer will return 1 when it doesn't have enough data yet */
  if (status != noErr && status != 1) {
    GST_ERROR_OBJECT (self, "Failed to fill buffer: %d", status);
    return GST_FLOW_ERROR;
  }

  if (out_packets == 0) {
    GST_LOG_OBJECT (self, "No packets produced, more data needed or input EOS");
    gst_buffer_unmap (outbuf, &map_info);
    gst_buffer_unref (outbuf);
    return GST_FLOW_OK;
  }

  gst_buffer_unmap (outbuf, &map_info);

  /* On exit, mDataByteSize is set to the number of bytes written. */
  GST_LOG_OBJECT (self, "Output buffer size: %d", out_desc.mDataByteSize);
  g_assert (out_desc.mDataByteSize <= self->max_output_buffer_size);
  gst_buffer_set_size (outbuf, out_desc.mDataByteSize);
  ret = gst_audio_encoder_finish_frame (enc, outbuf, self->n_output_samples);

  return ret;
}

static void
gst_atenc_fill_input_layout (GstAudioInfo * info, AudioChannelLayout * layout)
{
  const GstAudioChannelPosition *input_positions =
      &GST_AUDIO_INFO_POSITION (info, 0);

  layout->mChannelLayoutTag = kAudioChannelLayoutTag_UseChannelDescriptions;
  layout->mNumberChannelDescriptions = GST_AUDIO_INFO_CHANNELS (info);
  for (int i = 0; i < GST_AUDIO_INFO_CHANNELS (info); i++) {
    layout->mChannelDescriptions[i].mChannelLabel =
        gst_audio_channel_position_to_core_audio (input_positions[i], i);
  }
}

static AudioChannelLayoutTag
gst_atenc_get_output_layout_tag (GstATEnc * self, GstAudioInfo * info)
{
  const GstAudioChannelPosition *input_positions =
      &GST_AUDIO_INFO_POSITION (info, 0);
  const GstATEncLayout *layout;
  gint input_channels = GST_AUDIO_INFO_CHANNELS (info);
  guint64 input_ch_mask;

  gst_audio_channel_positions_to_mask (input_positions, input_channels, FALSE,
      &input_ch_mask);

  /* Try to find a predefined output layout that matches the input channels.
   * Order doesn't matter - we set channel descriptions on input, so AT will reorder internally. */
  for (layout = aac_layouts; layout->channels; layout++) {
    const GstAudioChannelPosition *output_positions = layout->positions;
    guint64 layout_ch_mask;

    if (layout->channels != input_channels)
      continue;

    gst_audio_channel_positions_to_mask (output_positions, layout->channels,
        FALSE, &layout_ch_mask);
    if (input_ch_mask != layout_ch_mask)
      continue;

    return layout->aac_tag;
  }

  return kAudioChannelLayoutTag_Unknown;
}

static bool
_parse_descriptor (GstByteReader * br, guint8 * tag, gint * len)
{
  gint size_of_instance = 0;
  guint8 size_byte;
  gboolean has_next_byte;

  /* Descriptors are variable size, parse it according 
   * to the formula in sec. 14.3.3 of ISO/IEC 14496-1.
   * First 8 bits is the tag. */
  if (!gst_byte_reader_get_uint8 (br, tag))
    return FALSE;
  /* Following is one or more size_byte, in which bit 1 tells us if we should parse further,
   * and the remaining 7 bits are the actual (portion of the) size */
  do {
    if (!gst_byte_reader_get_uint8 (br, &size_byte))
      return FALSE;
    has_next_byte = size_byte & 0x80;
    size_of_instance = (size_of_instance << 7) | (size_byte & 0x7f);
    g_assert (size_of_instance >= 0);
  } while (has_next_byte && gst_byte_reader_get_remaining (br) > 0);

  if (len)
    *len = size_of_instance;

  return TRUE;
}

static void
gst_atenc_extract_audio_specific_config (guint8 * cookie_buf, guint cookie_size,
    guint8 ** asc, guint * asc_size)
{
  GstByteReader *br = gst_byte_reader_new (cookie_buf, cookie_size);
  gint len;
  guint8 tag, flags, flag_skip;

  /* Cookie data is a MPEG descriptor structure, we need to extract the AudioSpecificConfig.
   * Structures parsed below are described in ISO/IEC 14496-1 */
  while (gst_byte_reader_get_remaining (br) > 0) {
    if (!_parse_descriptor (br, &tag, NULL))
      break;
    if (tag == ES_DESCRIPTOR_TAG) {
      /* First, find the ES_Descriptor and parse flags that tell us how many bits to skip */
      if (!gst_byte_reader_skip (br, 2))
        break;
      if (!gst_byte_reader_get_uint8 (br, &flags))
        break;
      if (flags & 0x80)
        if (!gst_byte_reader_skip (br, 2))
          break;
      if (flags & 0x40) {
        if (!gst_byte_reader_get_uint8 (br, &flag_skip))
          break;
        if (!gst_byte_reader_skip (br, flag_skip))
          break;
      }
      if (flags & 0x20)
        if (!gst_byte_reader_skip (br, 2))
          break;
    } else if (tag == DECODER_CONFIG_DESC_TAG) {
      /* Then we get the DecoderConfigDescriptor and skip its first 13 bytes to get to DecoderSpecificInfo */
      if (!gst_byte_reader_skip (br, 13))
        break;
      if (!_parse_descriptor (br, &tag, &len))
        break;
      /* DecoderSpecificInfo is the AudioSpecificConfig in our case */
      if (tag == DECODER_SPECIFIC_INFO_TAG) {
        *asc_size = len;
        *asc = g_malloc0 (*asc_size);
        if (!gst_byte_reader_dup_data (br, *asc_size, asc)) {
          g_free (*asc);
          *asc = NULL;
        }
        break;
      }
    }
  }
}

static gboolean
gst_atenc_set_format (GstAudioEncoder * enc, GstAudioInfo * info)
{
  GstATEnc *self = GST_ATENC (enc);
  AudioStreamBasicDescription input_desc = { 0 };
  AudioStreamBasicDescription output_desc = { 0 };
  AudioChannelLayout *layout = NULL;
  AudioChannelLayoutTag output_layout_tag;
  GstCaps *src_caps;
  OSStatus status;
  gboolean ret;
  UInt32 prop_size, max_output_size;
  guint8 *cookie_data = NULL;
  guint8 *audio_config = NULL;
  guint32 audio_config_size = 0;
  GstBuffer *asc_buf;

  if (self->converter) {
    /* Drain any leftover data from encoder */
    gst_atenc_handle_frame (enc, NULL);
    AudioConverterDispose (self->converter);
    self->converter = NULL;
  }

  input_desc.mSampleRate = GST_AUDIO_INFO_RATE (info);
  input_desc.mFormatID = kAudioFormatLinearPCM;
  input_desc.mFormatFlags =
      kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked;
  input_desc.mFramesPerPacket = 1;
  input_desc.mBytesPerFrame = input_desc.mBytesPerPacket =
      GST_AUDIO_INFO_BPF (info);
  input_desc.mChannelsPerFrame = GST_AUDIO_INFO_CHANNELS (info);
  input_desc.mBitsPerChannel = GST_AUDIO_INFO_DEPTH (info);

  /* HE-AAC v1/v2 and LD to be added later.
   * For LD, AudioSpecificConfig parsing fails completely, might be due to faulty MPEG descriptor parsing.
   * For HE-AAC, channel configurations need testing (also sometimes fail to parse). */
  output_desc.mFormatID = kAudioFormatMPEG4AAC;
  output_desc.mSampleRate = GST_AUDIO_INFO_RATE (info);
  output_desc.mChannelsPerFrame = GST_AUDIO_INFO_CHANNELS (info);

  status = AudioConverterNew (&input_desc, &output_desc, &self->converter);
  if (status != noErr) {
    GST_ERROR_OBJECT (self, "Failed to create audio converter: %d", status);
    return FALSE;
  }

  /* Using the encoder-provided size results in kAudioCodecBadPropertySizeError, so let's calculate it manually... */
  prop_size =
      sizeof (AudioChannelLayout) +
      sizeof (AudioChannelDescription) * GST_AUDIO_INFO_CHANNELS (info);
  layout = g_malloc0 (prop_size);

  /* For input, AT expects per-channel descriptions to be used */
  gst_atenc_fill_input_layout (info, layout);
  status =
      AudioConverterSetProperty (self->converter,
      kAudioConverterInputChannelLayout, prop_size, layout);
  if (status != noErr) {
    GST_ERROR_OBJECT (self, "Failed to set input channel layout: %d", status);
    g_free (layout);
    return FALSE;
  }

  /* For output, instead of channel descriptions, we use an AAC tag indicating one of the predefined layouts */
  output_layout_tag = gst_atenc_get_output_layout_tag (self, info);
  if (output_layout_tag == kAudioChannelLayoutTag_Unknown) {
    GST_ERROR_OBJECT (self,
        "Failed to find a matching output channel layout tag");
    g_free (layout);
    return FALSE;
  }

  layout->mChannelLayoutTag = output_layout_tag;
  layout->mNumberChannelDescriptions = 0;

  status =
      AudioConverterSetProperty (self->converter,
      kAudioConverterOutputChannelLayout, prop_size, layout);
  g_free (layout);
  if (status != noErr) {
    GST_ERROR_OBJECT (self, "Failed to set output channel layout: %d", status);
    return FALSE;
  }

  /* TODO: Check if this works on iOS */
  status =
      AudioConverterSetProperty (self->converter,
      kAudioCodecPropertyBitRateControlMode, sizeof (UInt32),
      &self->rate_control);
  if (status != noErr) {
    GST_ERROR_OBJECT (self, "Failed to set bitrate control mode: %d", status);
    return FALSE;
  }

  if (self->rate_control == GST_ATENC_RATE_CONTROL_VARIABLE) {
    status =
        AudioConverterSetProperty (self->converter,
        kAudioCodecPropertySoundQualityForVBR, sizeof (UInt32),
        &self->vbr_quality);
    if (status != noErr) {
      GST_ERROR_OBJECT (self, "Failed to set VBR quality: %d", status);
      return FALSE;
    }
  }

  if (self->bitrate > 0
      && (self->rate_control == GST_ATENC_RATE_CONTROL_CONSTANT
          || self->rate_control == GST_ATENC_RATE_CONTROL_LONG_TERM_AVERAGE)) {
    /* Query the encoder for possible bitrate values and adjust if needed */
    AudioValueRange *bitrate_ranges;
    UInt32 actual_bitrate;

    status =
        AudioConverterGetPropertyInfo (self->converter,
        kAudioConverterApplicableEncodeBitRates, &prop_size, NULL);
    if (status != noErr) {
      GST_ERROR_OBJECT (self, "Failed to get possible bitrates size: %d",
          status);
      return FALSE;
    }

    bitrate_ranges = g_malloc (prop_size);
    status =
        AudioConverterGetProperty (self->converter,
        kAudioConverterApplicableEncodeBitRates, &prop_size, bitrate_ranges);
    if (status != noErr) {
      GST_ERROR_OBJECT (self, "Failed to get possible bitrates: %d", status);
      g_free (bitrate_ranges);
      return FALSE;
    }

    GST_LOG_OBJECT (self, "Allowed bitrate ranges:");
    for (int i = 0; i < prop_size / sizeof (AudioValueRange); i++) {
      AudioValueRange *range = &bitrate_ranges[i];
      GST_LOG_OBJECT (self, "%d: %f - %f",
          i + 1, range->mMinimum, range->mMaximum);
    }

    /* Returned ranges are ordered from lowest to highest values */
    for (int i = 0; i < prop_size / sizeof (AudioValueRange); i++) {
      AudioValueRange *range = &bitrate_ranges[i];
      if (self->bitrate == range->mMinimum && self->bitrate == range->mMaximum) {
        /* Often the min/max values are identical, so not that much of a range... */
        actual_bitrate = self->bitrate;
        break;
      } else if (self->bitrate < range->mMinimum) {
        actual_bitrate = range->mMinimum;
        break;
      } else if (self->bitrate > range->mMaximum) {
        /* We might find higher values still, so no break */
        actual_bitrate = range->mMaximum;
      }
    }

    if (actual_bitrate != self->bitrate) {
      GST_WARNING_OBJECT (self,
          "Requested bitrate %d not in the allowed range, using %d",
          self->bitrate, actual_bitrate);
      self->bitrate = actual_bitrate;
    }

    /* TODO: This could be changed at any time instead of just in set_format,
     * but from initial testing, changing the bitrate when encoding introduces
     * a very short pause in encoded sound. Needs investigation. */
    status =
        AudioConverterSetProperty (self->converter,
        kAudioConverterEncodeBitRate, sizeof (UInt32), &actual_bitrate);
    if (status != noErr) {
      GST_ERROR_OBJECT (self, "Failed to set bitrate: %d", status);
      g_free (bitrate_ranges);
      return FALSE;
    }
  }

  /* After creation, encoder fills input/output desc with more details */
  prop_size = sizeof (output_desc);
  status =
      AudioConverterGetProperty (self->converter,
      kAudioConverterCurrentOutputStreamDescription, &prop_size, &output_desc);
  if (status != noErr) {
    GST_ERROR_OBJECT (self, "Failed to get output format: %d", status);
    return FALSE;
  }
  self->n_output_samples = output_desc.mFramesPerPacket;
  GST_DEBUG_OBJECT (self, "samples per output packet: %d",
      self->n_output_samples);

  /* This isn't always set, so we might need to query manually */
  max_output_size = output_desc.mBytesPerPacket;
  if (max_output_size == 0) {
    prop_size = sizeof (max_output_size);
    status =
        AudioConverterGetProperty (self->converter,
        kAudioConverterPropertyMaximumOutputPacketSize, &prop_size,
        &max_output_size);
    if (status != noErr) {
      GST_ERROR_OBJECT (self, "Failed to get maximum output packet size: %d",
          status);
      return FALSE;
    }
  }
  self->max_output_buffer_size = max_output_size;
  GST_DEBUG_OBJECT (self, "maximum output buffer size: %d",
      self->max_output_buffer_size);

  /* For AAC, AT usually asks for 1024 samples per packet, base class needs to know */
  gst_audio_encoder_set_frame_max (enc, 1);
  gst_audio_encoder_set_frame_samples_min (enc, self->n_output_samples);
  gst_audio_encoder_set_frame_samples_max (enc, self->n_output_samples);
  gst_audio_encoder_set_drainable (enc, TRUE);

  /* FIXME: Handle lookahead according to kAudioConverterPrimeInfo.leadingFrames.
   * When passed directly to gst_audio_encoder_set_lookahead, causes
   * an audible skip in audio, and muxers such as mp4mux error out.
   * To be investigated. */

  status =
      AudioConverterGetPropertyInfo (self->converter,
      kAudioConverterCompressionMagicCookie, &prop_size, NULL);
  if (status != noErr) {
    GST_ERROR_OBJECT (self, "Failed to get magic cookie size: %d", status);
    return FALSE;
  }

  cookie_data = g_malloc (prop_size);
  status =
      AudioConverterGetProperty (self->converter,
      kAudioConverterCompressionMagicCookie, &prop_size, cookie_data);
  if (status != noErr) {
    GST_ERROR_OBJECT (self, "Failed to get magic cookie: %d", status);
    g_free (cookie_data);
    return FALSE;
  }

  /* Cookie contains a bunch of descriptors, gotta dig a bit to get the AudioSpecificConfig */
  gst_atenc_extract_audio_specific_config (cookie_data, prop_size,
      &audio_config, &audio_config_size);
  if (!audio_config) {
    GST_ERROR_OBJECT (self, "Failed to extract AudioSpecificConfig");
    g_free (cookie_data);
    return FALSE;
  }

  asc_buf = gst_buffer_new_wrapped (audio_config, audio_config_size);

  src_caps = gst_caps_new_simple ("audio/mpeg",
      "mpegversion", G_TYPE_INT, 4,
      "rate", G_TYPE_INT, GST_AUDIO_INFO_RATE (info),
      "channels", G_TYPE_INT, GST_AUDIO_INFO_CHANNELS (info),
      "stream-format", G_TYPE_STRING, "raw",
      "framed", G_TYPE_BOOLEAN, TRUE,
      "codec_data", GST_TYPE_BUFFER, asc_buf, NULL);

  gst_codec_utils_aac_caps_set_level_and_profile (src_caps, audio_config,
      audio_config_size);
  gst_buffer_unref (asc_buf);
  g_free (cookie_data);

  ret = gst_audio_encoder_set_output_format (enc, src_caps);
  GST_DEBUG ("output caps: %" GST_PTR_FORMAT, src_caps);
  gst_caps_unref (src_caps);

  return ret;
}

static void
gst_atenc_init (GstATEnc * self)
{
  self->bitrate = DEFAULT_BITRATE;
  self->rate_control = DEFAULT_RATE_CONTROL;
  self->vbr_quality = DEFAULT_VBR_QUALITY;
  self->input_eos = FALSE;
  self->used_buffer = NULL;
}

static void
gst_atenc_class_init (GstATEncClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstAudioEncoderClass *base_class = GST_AUDIO_ENCODER_CLASS (klass);

  object_class->set_property = GST_DEBUG_FUNCPTR (gst_atenc_set_property);
  object_class->get_property = GST_DEBUG_FUNCPTR (gst_atenc_get_property);

  base_class->start = GST_DEBUG_FUNCPTR (gst_atenc_start);
  base_class->stop = GST_DEBUG_FUNCPTR (gst_atenc_stop);
  base_class->getcaps = GST_DEBUG_FUNCPTR (gst_atenc_get_caps);
  base_class->set_format = GST_DEBUG_FUNCPTR (gst_atenc_set_format);
  base_class->handle_frame = GST_DEBUG_FUNCPTR (gst_atenc_handle_frame);
  base_class->flush = GST_DEBUG_FUNCPTR (gst_atenc_flush);

  /**
   * GstATEnc:bitrate:
   *
   * Target output bitrate in bps, for CBR and LTA rate control modes.
   *
   * Since: 1.26
   */
  g_object_class_install_property (object_class, PROP_BITRATE,
      g_param_spec_uint ("bitrate",
          "Bitrate",
          "target output bitrate in bps (for rate-control=cbr/lta) (0 - auto)",
          0, G_MAXUINT32, DEFAULT_BITRATE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstATEnc:rate-control:
   *
   * Rate control mode to be applied by the encoder.
   * CBR and LTA modes use the bitrate property, VBR uses the vbr-quality property.
   * Constrained VBR determines the bitrate/quality automatically based on the input signal.
   *
   * Since: 1.26
   */
  g_object_class_install_property (object_class, PROP_RATE_CONTROL,
      g_param_spec_enum ("rate-control",
          "Rate control",
          "Mode of output bitrate control to be applied",
          GST_ATENC_RATE_CONTROL,
          DEFAULT_RATE_CONTROL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstATEnc:vbr-quality:
   *
   * Sound quality setting for VBR encoding.
   *
   * Since: 1.26
   */
  g_object_class_install_property (object_class, PROP_VBR_QUALITY,
      g_param_spec_uint ("vbr-quality",
          "VBR quality",
          "Sound quality setting for VBR encoding (rate-control=vbr) (0-127)",
          0, 127, DEFAULT_VBR_QUALITY,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_add_static_pad_template (element_class, &sink_template);
  gst_element_class_add_static_pad_template (element_class, &src_template);

  gst_element_class_set_static_metadata (element_class,
      "AudioToolbox audio encoder", "Coder/Encoder/Audio/Converter",
      "AudioToolbox based audio encoder for macOS/iOS",
      "Piotr Brzeziński <piotr@centricular.com>");

  GST_DEBUG_CATEGORY_INIT (gst_atenc_debug, "atenc", 0,
      "AudioToolbox based audio encoder");
}
