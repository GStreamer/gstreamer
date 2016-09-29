/*
 * Copyright (C) 2016 Sebastian Dröge <sebastian@centricular.com>
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

#include "gstfdkaacdec.h"

#include <gst/pbutils/pbutils.h>

#include <string.h>

/* TODO:
 * - LOAS / LATM support
 * - Error concealment
 */

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/mpeg, "
        "mpegversion = (int) 4, "
        "stream-format = (string) { adts, adif, raw }")
    );

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw, "
        "format = (string) " GST_AUDIO_NE (S16) ", "
        "layout = (string) interleaved, "
        "rate = (int) [8000, 96000], " "channels = (int) [1, 8]")
    );

GST_DEBUG_CATEGORY_STATIC (gst_fdkaacdec_debug);
#define GST_CAT_DEFAULT gst_fdkaacdec_debug

static gboolean gst_fdkaacdec_start (GstAudioDecoder * dec);
static gboolean gst_fdkaacdec_stop (GstAudioDecoder * dec);
static gboolean gst_fdkaacdec_set_format (GstAudioDecoder * dec,
    GstCaps * caps);
static GstFlowReturn gst_fdkaacdec_handle_frame (GstAudioDecoder * dec,
    GstBuffer * in_buf);
static void gst_fdkaacdec_flush (GstAudioDecoder * dec, gboolean hard);

G_DEFINE_TYPE (GstFdkAacDec, gst_fdkaacdec, GST_TYPE_AUDIO_DECODER);

static gboolean
gst_fdkaacdec_start (GstAudioDecoder * dec)
{
  GstFdkAacDec *self = GST_FDKAACDEC (dec);

  GST_DEBUG_OBJECT (self, "start");

  return TRUE;
}

static gboolean
gst_fdkaacdec_stop (GstAudioDecoder * dec)
{
  GstFdkAacDec *self = GST_FDKAACDEC (dec);

  GST_DEBUG_OBJECT (self, "stop");

  g_free (self->decode_buffer);
  self->decode_buffer = NULL;

  if (self->dec)
    aacDecoder_Close (self->dec);
  self->dec = NULL;

  return TRUE;
}

static gboolean
gst_fdkaacdec_set_format (GstAudioDecoder * dec, GstCaps * caps)
{
  GstFdkAacDec *self = GST_FDKAACDEC (dec);
  TRANSPORT_TYPE transport_format;
  GstStructure *s;
  const gchar *stream_format;
  AAC_DECODER_ERROR err;

  if (self->dec) {
    /* drain */
    gst_fdkaacdec_handle_frame (dec, NULL);
    aacDecoder_Close (self->dec);
    self->dec = NULL;
  }

  s = gst_caps_get_structure (caps, 0);
  stream_format = gst_structure_get_string (s, "stream-format");
  if (strcmp (stream_format, "raw") == 0) {
    transport_format = TT_MP4_RAW;
  } else if (strcmp (stream_format, "adif") == 0) {
    transport_format = TT_MP4_ADIF;
  } else if (strcmp (stream_format, "adts") == 0) {
    transport_format = TT_MP4_ADTS;
  } else {
    g_assert_not_reached ();
  }

  self->dec = aacDecoder_Open (transport_format, 1);
  if (!self->dec) {
    GST_ERROR_OBJECT (self, "Failed to open decoder");
    return FALSE;
  }

  if (transport_format == TT_MP4_RAW) {
    GstBuffer *codec_data = NULL;
    GstMapInfo map;
    guint8 *data;
    guint size;

    gst_structure_get (s, "codec_data", GST_TYPE_BUFFER, &codec_data, NULL);

    if (!codec_data) {
      GST_ERROR_OBJECT (self, "Raw AAC without codec_data not supported");
      return FALSE;
    }

    gst_buffer_map (codec_data, &map, GST_MAP_READ);
    data = map.data;
    size = map.size;

    if ((err = aacDecoder_ConfigRaw (self->dec, &data, &size)) != AAC_DEC_OK) {
      gst_buffer_unmap (codec_data, &map);
      gst_buffer_unref (codec_data);
      GST_ERROR_OBJECT (self, "Invalid codec_data: %d", err);
      return FALSE;
    }

    gst_buffer_unmap (codec_data, &map);
    gst_buffer_unref (codec_data);
  }

  if ((err =
          aacDecoder_SetParam (self->dec, AAC_PCM_OUTPUT_CHANNEL_MAPPING,
              0)) != AAC_DEC_OK) {
    GST_ERROR_OBJECT (self, "Failed to set output channel mapping: %d", err);
    return FALSE;
  }

  if ((err =
          aacDecoder_SetParam (self->dec, AAC_PCM_OUTPUT_INTERLEAVED,
              1)) != AAC_DEC_OK) {
    GST_ERROR_OBJECT (self, "Failed to set interleaved output: %d", err);
    return FALSE;
  }

  /* 8 channels * 2 bytes per sample * 2048 samples */
  if (!self->decode_buffer) {
    self->decode_buffer_size = 8 * 2048;
    self->decode_buffer = g_new (gint16, self->decode_buffer_size);
  }

  return TRUE;
}

static GstFlowReturn
gst_fdkaacdec_handle_frame (GstAudioDecoder * dec, GstBuffer * inbuf)
{
  GstFdkAacDec *self = GST_FDKAACDEC (dec);
  GstFlowReturn ret = GST_FLOW_OK;
  GstBuffer *outbuf;
  GstMapInfo imap;
  AAC_DECODER_ERROR err;
  guint size, valid;
  CStreamInfo *stream_info;
  GstAudioInfo info;
  guint flags = 0, i;
  GstAudioChannelPosition pos[64], gst_pos[64];
  gboolean need_reorder;

  if (inbuf) {
    gst_buffer_ref (inbuf);
    gst_buffer_map (inbuf, &imap, GST_MAP_READ);
    valid = size = imap.size;

    if ((err =
            aacDecoder_Fill (self->dec, (guint8 **) & imap.data, &size,
                &valid)) != AAC_DEC_OK) {
      GST_AUDIO_DECODER_ERROR (self, 1, STREAM, DECODE, (NULL),
          ("filling error: %d", err), ret);
      goto out;
    }

    if (GST_BUFFER_IS_DISCONT (inbuf))
      flags |= AACDEC_INTR;
  } else {
    flags |= AACDEC_FLUSH;
  }

  if ((err =
          aacDecoder_DecodeFrame (self->dec, self->decode_buffer,
              self->decode_buffer_size, flags)) != AAC_DEC_OK) {
    if (err == AAC_DEC_TRANSPORT_SYNC_ERROR) {
      ret = GST_FLOW_OK;
      outbuf = NULL;
      goto finish;
    }
    GST_AUDIO_DECODER_ERROR (self, 1, STREAM, DECODE, (NULL),
        ("decoding error: %d", err), ret);
    goto out;
  }

  stream_info = aacDecoder_GetStreamInfo (self->dec);
  if (!stream_info) {
    GST_AUDIO_DECODER_ERROR (self, 1, STREAM, DECODE, (NULL),
        ("failed to get stream info"), ret);
    goto out;
  }

  /* FIXME: Don't recalculate this on every buffer */
  if (stream_info->numChannels == 1) {
    pos[0] = GST_AUDIO_CHANNEL_POSITION_MONO;
  } else {
    gint n_front = 0, n_side = 0, n_back = 0, n_lfe = 0;

    /* FIXME: Can this be simplified somehow? */
    for (i = 0; i < stream_info->numChannels; i++) {
      if (stream_info->pChannelType[i] == ACT_FRONT) {
        n_front++;
      } else if (stream_info->pChannelType[i] == ACT_SIDE) {
        n_side++;
      } else if (stream_info->pChannelType[i] == ACT_BACK) {
        n_back++;
      } else if (stream_info->pChannelType[i] == ACT_LFE) {
        n_lfe++;
      } else {
        GST_ERROR_OBJECT (self, "Channel type %d not supported",
            stream_info->pChannelType[i]);
        ret = GST_FLOW_NOT_NEGOTIATED;
        goto out;
      }
    }

    for (i = 0; i < stream_info->numChannels; i++) {
      if (stream_info->pChannelType[i] == ACT_FRONT) {
        if (stream_info->pChannelIndices[i] == 0) {
          if (n_front & 1)
            pos[i] = GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER;
          else if (n_front > 2)
            pos[i] = GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT_OF_CENTER;
          else
            pos[i] = GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT;
        } else if (stream_info->pChannelIndices[i] == 1) {
          if ((n_front & 1) && n_front > 3)
            pos[i] = GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT_OF_CENTER;
          else if (n_front & 1)
            pos[i] = GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT;
          else if (n_front > 2)
            pos[i] = GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT_OF_CENTER;
          else
            pos[i] = GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT;
        } else if (stream_info->pChannelIndices[i] == 2) {
          if ((n_front & 1) && n_front > 3)
            pos[i] = GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT_OF_CENTER;
          else if (n_front & 1)
            pos[i] = GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT;
          else if (n_front > 2)
            pos[i] = GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT;
          else
            g_assert_not_reached ();
        } else if (stream_info->pChannelIndices[i] == 3) {
          if ((n_front & 1) && n_front > 3)
            pos[i] = GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT;
          else if (n_front & 1)
            g_assert_not_reached ();
          else if (n_front > 2)
            pos[i] = GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT;
          else
            g_assert_not_reached ();
        } else if (stream_info->pChannelIndices[i] == 4) {
          if ((n_front & 1) && n_front > 2)
            pos[i] = GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT;
          else if (n_front & 1)
            g_assert_not_reached ();
          else if (n_front > 2)
            g_assert_not_reached ();
          else
            g_assert_not_reached ();
        } else {
          GST_ERROR_OBJECT (self, "Front channel index %d not supported",
              stream_info->pChannelIndices[i]);
          ret = GST_FLOW_NOT_NEGOTIATED;
          goto out;
        }
      } else if (stream_info->pChannelType[i] == ACT_SIDE) {
        if (n_side & 1) {
          GST_ERROR_OBJECT (self, "Odd number of side channels not supported");
          ret = GST_FLOW_NOT_NEGOTIATED;
          goto out;
        } else if (stream_info->pChannelIndices[i] == 0) {
          pos[i] = GST_AUDIO_CHANNEL_POSITION_SIDE_LEFT;
        } else if (stream_info->pChannelIndices[i] == 1) {
          pos[i] = GST_AUDIO_CHANNEL_POSITION_SIDE_RIGHT;
        } else {
          GST_ERROR_OBJECT (self, "Side channel index %d not supported",
              stream_info->pChannelIndices[i]);
          ret = GST_FLOW_NOT_NEGOTIATED;
          goto out;
        }
      } else if (stream_info->pChannelType[i] == ACT_BACK) {
        if (stream_info->pChannelIndices[i] == 0) {
          if (n_back & 1)
            pos[i] = GST_AUDIO_CHANNEL_POSITION_REAR_CENTER;
          else
            pos[i] = GST_AUDIO_CHANNEL_POSITION_REAR_LEFT;
        } else if (stream_info->pChannelIndices[i] == 1) {
          if (n_back & 1)
            pos[i] = GST_AUDIO_CHANNEL_POSITION_REAR_LEFT;
          else
            pos[i] = GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT;
        } else if (stream_info->pChannelIndices[i] == 2) {
          if (n_back & 1)
            pos[i] = GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT;
          else
            g_assert_not_reached ();
        } else {
          GST_ERROR_OBJECT (self, "Side channel index %d not supported",
              stream_info->pChannelIndices[i]);
          ret = GST_FLOW_NOT_NEGOTIATED;
          goto out;
        }
      } else if (stream_info->pChannelType[i] == ACT_LFE) {
        if (stream_info->pChannelIndices[i] == 0) {
          pos[i] = GST_AUDIO_CHANNEL_POSITION_LFE1;
        } else {
          GST_ERROR_OBJECT (self, "LFE channel index %d not supported",
              stream_info->pChannelIndices[i]);
          ret = GST_FLOW_NOT_NEGOTIATED;
          goto out;
        }
      } else {
        GST_ERROR_OBJECT (self, "Channel type %d not supported",
            stream_info->pChannelType[i]);
        ret = GST_FLOW_NOT_NEGOTIATED;
        goto out;
      }
    }
  }

  memcpy (gst_pos, pos,
      sizeof (GstAudioChannelPosition) * stream_info->numChannels);
  if (!gst_audio_channel_positions_to_valid_order (gst_pos,
          stream_info->numChannels)) {
    ret = GST_FLOW_NOT_NEGOTIATED;
    goto out;
  }

  need_reorder =
      memcmp (pos, gst_pos,
      sizeof (GstAudioChannelPosition) * stream_info->numChannels) != 0;

  gst_audio_info_set_format (&info, GST_AUDIO_FORMAT_S16,
      stream_info->sampleRate, stream_info->numChannels, gst_pos);
  if (!gst_audio_decoder_set_output_format (dec, &info)) {
    GST_ERROR_OBJECT (self, "Failed to set output format");
    ret = GST_FLOW_NOT_NEGOTIATED;
    goto out;
  }

  outbuf =
      gst_audio_decoder_allocate_output_buffer (dec,
      stream_info->frameSize * GST_AUDIO_INFO_BPF (&info));
  gst_buffer_fill (outbuf, 0, self->decode_buffer,
      gst_buffer_get_size (outbuf));

  if (need_reorder) {
    gst_audio_buffer_reorder_channels (outbuf, GST_AUDIO_INFO_FORMAT (&info),
        GST_AUDIO_INFO_CHANNELS (&info), pos, gst_pos);
  }

finish:
  ret = gst_audio_decoder_finish_frame (dec, outbuf, 1);

out:

  if (inbuf) {
    gst_buffer_unmap (inbuf, &imap);
    gst_buffer_unref (inbuf);
  }

  return ret;
}

static void
gst_fdkaacdec_flush (GstAudioDecoder * dec, gboolean hard)
{
  GstFdkAacDec *self = GST_FDKAACDEC (dec);

  if (self->dec) {
    AAC_DECODER_ERROR err;
    if ((err =
            aacDecoder_DecodeFrame (self->dec, self->decode_buffer,
                self->decode_buffer_size, AACDEC_FLUSH)) != AAC_DEC_OK) {
      GST_ERROR_OBJECT (self, "flushing error: %d", err);
    }
  }
}

static void
gst_fdkaacdec_init (GstFdkAacDec * self)
{
  self->dec = NULL;

  gst_audio_decoder_set_drainable (GST_AUDIO_DECODER (self), TRUE);
  gst_audio_decoder_set_needs_format (GST_AUDIO_DECODER (self), TRUE);
}

static void
gst_fdkaacdec_class_init (GstFdkAacDecClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstAudioDecoderClass *base_class = GST_AUDIO_DECODER_CLASS (klass);

  base_class->start = GST_DEBUG_FUNCPTR (gst_fdkaacdec_start);
  base_class->stop = GST_DEBUG_FUNCPTR (gst_fdkaacdec_stop);
  base_class->set_format = GST_DEBUG_FUNCPTR (gst_fdkaacdec_set_format);
  base_class->handle_frame = GST_DEBUG_FUNCPTR (gst_fdkaacdec_handle_frame);
  base_class->flush = GST_DEBUG_FUNCPTR (gst_fdkaacdec_flush);

  gst_element_class_add_static_pad_template (element_class, &sink_template);
  gst_element_class_add_static_pad_template (element_class, &src_template);

  gst_element_class_set_static_metadata (element_class, "FDK AAC audio decoder",
      "Codec/Decoder/Audio", "FDK AAC audio decoder",
      "Sebastian Dröge <sebastian@centricular.com>");

  GST_DEBUG_CATEGORY_INIT (gst_fdkaacdec_debug, "fdkaacdec", 0,
      "fdkaac decoder");
}
