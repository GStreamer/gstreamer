/* GStreamer DTS decoder plugin based on libdtsdec
 * Copyright (C) 2004 Ronald Bultje <rbultje@ronald.bitfreak.net>
 * Copyright (C) 2009 Jan Schmidt <thaytan@noraisin.net>
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

/**
 * SECTION:element-dtsdec
 *
 * Digital Theatre System (DTS) audio decoder
 * 
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch dvdreadsrc title=1 ! mpegpsdemux ! dtsdec ! audioresample ! audioconvert ! alsasink
 * ]| Play a DTS audio track from a dvd.
 * |[
 * gst-launch filesrc location=abc.dts ! dtsdec ! audioresample ! audioconvert ! alsasink
 * ]| Decode a standalone file and play it.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include "_stdint.h"
#include <stdlib.h>

#include <gst/gst.h>
#include <gst/audio/multichannel.h>

#ifndef DTS_OLD
#include <dca.h>
#else
#include <dts.h>

typedef struct dts_state_s dca_state_t;
#define DCA_MONO DTS_MONO
#define DCA_CHANNEL DTS_CHANNEL
#define DCA_STEREO DTS_STEREO
#define DCA_STEREO_SUMDIFF DTS_STEREO_SUMDIFF
#define DCA_STEREO_TOTAL DTS_STEREO_TOTAL
#define DCA_3F DTS_3F
#define DCA_2F1R DTS_2F1R
#define DCA_3F1R DTS_3F1R
#define DCA_2F2R DTS_2F2R
#define DCA_3F2R DTS_3F2R
#define DCA_4F2R DTS_4F2R
#define DCA_DOLBY DTS_DOLBY
#define DCA_CHANNEL_MAX DTS_CHANNEL_MAX
#define DCA_CHANNEL_BITS DTS_CHANNEL_BITS
#define DCA_CHANNEL_MASK DTS_CHANNEL_MASK
#define DCA_LFE DTS_LFE
#define DCA_ADJUST_LEVEL DTS_ADJUST_LEVEL

#define dca_init dts_init
#define dca_syncinfo dts_syncinfo
#define dca_frame dts_frame
#define dca_dynrng dts_dynrng
#define dca_blocks_num dts_blocks_num
#define dca_block dts_block
#define dca_samples dts_samples
#define dca_free dts_free
#endif

#include "gstdtsdec.h"

#if HAVE_ORC
#include <orc/orc.h>
#endif

#if defined(LIBDTS_FIXED) || defined(LIBDCA_FIXED)
#define SAMPLE_WIDTH 16
#elif defined (LIBDTS_DOUBLE) || defined(LIBDCA_DOUBLE)
#define SAMPLE_WIDTH 64
#else
#define SAMPLE_WIDTH 32
#endif

GST_DEBUG_CATEGORY_STATIC (dtsdec_debug);
#define GST_CAT_DEFAULT (dtsdec_debug)

enum
{
  ARG_0,
  ARG_DRC
};

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-dts; audio/x-private1-dts")
    );

#if defined(LIBDTS_FIXED) || defined(LIBDCA_FIXED)
#define DTS_CAPS "audio/x-raw-int, " \
    "endianness = (int) " G_STRINGIFY (G_BYTE_ORDER) ", " \
    "signed = (boolean) true, " \
    "width = (int) " G_STRINGIFY (SAMPLE_WIDTH) ", " \
    "depth = (int) 16"
#else
#define DTS_CAPS "audio/x-raw-float, " \
    "endianness = (int) " G_STRINGIFY (G_BYTE_ORDER) ", " \
    "width = (int) " G_STRINGIFY (SAMPLE_WIDTH)
#endif

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (DTS_CAPS ", "
        "rate = (int) [ 4000, 96000 ], " "channels = (int) [ 1, 6 ]")
    );

GST_BOILERPLATE (GstDtsDec, gst_dtsdec, GstAudioDecoder,
    GST_TYPE_AUDIO_DECODER);

static gboolean gst_dtsdec_start (GstAudioDecoder * dec);
static gboolean gst_dtsdec_stop (GstAudioDecoder * dec);
static gboolean gst_dtsdec_set_format (GstAudioDecoder * bdec, GstCaps * caps);
static gboolean gst_dtsdec_parse (GstAudioDecoder * dec, GstAdapter * adapter,
    gint * offset, gint * length);
static GstFlowReturn gst_dtsdec_handle_frame (GstAudioDecoder * dec,
    GstBuffer * buffer);
static GstFlowReturn gst_dtsdec_pre_push (GstAudioDecoder * bdec,
    GstBuffer ** buffer);

static GstFlowReturn gst_dtsdec_chain (GstPad * pad, GstBuffer * buf);

static void gst_dtsdec_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_dtsdec_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);


static void
gst_dtsdec_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_static_pad_template (element_class, &sink_factory);
  gst_element_class_add_static_pad_template (element_class, &src_factory);
  gst_element_class_set_details_simple (element_class, "DTS audio decoder",
      "Codec/Decoder/Audio",
      "Decodes DTS audio streams",
      "Jan Schmidt <thaytan@noraisin.net>, "
      "Ronald Bultje <rbultje@ronald.bitfreak.net>");

  GST_DEBUG_CATEGORY_INIT (dtsdec_debug, "dtsdec", 0, "DTS/DCA audio decoder");
}

static void
gst_dtsdec_class_init (GstDtsDecClass * klass)
{
  GObjectClass *gobject_class;
  GstAudioDecoderClass *gstbase_class;
  guint cpuflags;

  gobject_class = (GObjectClass *) klass;
  gstbase_class = (GstAudioDecoderClass *) klass;

  gobject_class->set_property = gst_dtsdec_set_property;
  gobject_class->get_property = gst_dtsdec_get_property;

  gstbase_class->start = GST_DEBUG_FUNCPTR (gst_dtsdec_start);
  gstbase_class->stop = GST_DEBUG_FUNCPTR (gst_dtsdec_stop);
  gstbase_class->set_format = GST_DEBUG_FUNCPTR (gst_dtsdec_set_format);
  gstbase_class->parse = GST_DEBUG_FUNCPTR (gst_dtsdec_parse);
  gstbase_class->handle_frame = GST_DEBUG_FUNCPTR (gst_dtsdec_handle_frame);
  gstbase_class->pre_push = GST_DEBUG_FUNCPTR (gst_dtsdec_pre_push);

  /**
   * GstDtsDec::drc
   *
   * Set to true to apply the recommended DTS dynamic range compression
   * to the audio stream. Dynamic range compression makes loud sounds
   * softer and soft sounds louder, so you can more easily listen
   * to the stream without disturbing other people.
   */
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_DRC,
      g_param_spec_boolean ("drc", "Dynamic Range Compression",
          "Use Dynamic Range Compression", FALSE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  klass->dts_cpuflags = 0;

#if HAVE_ORC
  cpuflags = orc_target_get_default_flags (orc_target_get_by_name ("mmx"));
  if (cpuflags & ORC_TARGET_MMX_MMX)
    klass->dts_cpuflags |= MM_ACCEL_X86_MMX;
  if (cpuflags & ORC_TARGET_MMX_3DNOW)
    klass->dts_cpuflags |= MM_ACCEL_X86_3DNOW;
  if (cpuflags & ORC_TARGET_MMX_MMXEXT)
    klass->dts_cpuflags |= MM_ACCEL_X86_MMXEXT;
#else
  cpuflags = 0;
  klass->dts_cpuflags = 0;
#endif

  GST_LOG ("CPU flags: dts=%08x, liboil=%08x", klass->dts_cpuflags, cpuflags);
}

static void
gst_dtsdec_init (GstDtsDec * dtsdec, GstDtsDecClass * g_class)
{
  dtsdec->request_channels = DCA_CHANNEL;
  dtsdec->dynamic_range_compression = FALSE;

  /* retrieve and intercept base class chain.
   * Quite HACKish, but that's dvd specs for you,
   * since one buffer needs to be split into 2 frames */
  dtsdec->base_chain = GST_PAD_CHAINFUNC (GST_AUDIO_DECODER_SINK_PAD (dtsdec));
  gst_pad_set_chain_function (GST_AUDIO_DECODER_SINK_PAD (dtsdec),
      GST_DEBUG_FUNCPTR (gst_dtsdec_chain));
}

static gboolean
gst_dtsdec_start (GstAudioDecoder * dec)
{
  GstDtsDec *dts = GST_DTSDEC (dec);
  GstDtsDecClass *klass;

  GST_DEBUG_OBJECT (dec, "start");

  klass = GST_DTSDEC_CLASS (G_OBJECT_GET_CLASS (dts));
  dts->state = dca_init (klass->dts_cpuflags);
  dts->samples = dca_samples (dts->state);
  dts->bit_rate = -1;
  dts->sample_rate = -1;
  dts->stream_channels = DCA_CHANNEL;
  dts->using_channels = DCA_CHANNEL;
  dts->level = 1;
  dts->bias = 0;
  dts->flag_update = TRUE;

  /* call upon legacy upstream byte support (e.g. seeking) */
  gst_audio_decoder_set_byte_time (dec, TRUE);

  return TRUE;
}

static gboolean
gst_dtsdec_stop (GstAudioDecoder * dec)
{
  GstDtsDec *dts = GST_DTSDEC (dec);

  GST_DEBUG_OBJECT (dec, "stop");

  dts->samples = NULL;
  if (dts->state) {
    dca_free (dts->state);
    dts->state = NULL;
  }
  if (dts->pending_tags) {
    gst_tag_list_free (dts->pending_tags);
    dts->pending_tags = NULL;
  }

  return TRUE;
}

static GstFlowReturn
gst_dtsdec_parse (GstAudioDecoder * bdec, GstAdapter * adapter,
    gint * _offset, gint * len)
{
  GstDtsDec *dts;
  guint8 *data;
  gint av, size;
  gint length = 0, flags, sample_rate, bit_rate, frame_length;
  GstFlowReturn result = GST_FLOW_UNEXPECTED;

  dts = GST_DTSDEC (bdec);

  size = av = gst_adapter_available (adapter);
  data = (guint8 *) gst_adapter_peek (adapter, av);

  /* find and read header */
  bit_rate = dts->bit_rate;
  sample_rate = dts->sample_rate;
  flags = 0;
  while (av >= 7) {
    length = dca_syncinfo (dts->state, data, &flags,
        &sample_rate, &bit_rate, &frame_length);

    if (length == 0) {
      /* shift window to re-find sync */
      data++;
      size--;
    } else if (length <= size) {
      GST_LOG_OBJECT (dts, "Sync: frame size %d", length);
      result = GST_FLOW_OK;
      break;
    } else {
      GST_LOG_OBJECT (dts, "Not enough data available (needed %d had %d)",
          length, size);
      break;
    }
  }

  *_offset = av - size;
  *len = length;

  return result;
}

static gint
gst_dtsdec_channels (uint32_t flags, GstAudioChannelPosition ** pos)
{
  gint chans = 0;
  GstAudioChannelPosition *tpos = NULL;

  if (pos) {
    /* Allocate the maximum, for ease */
    tpos = *pos = g_new (GstAudioChannelPosition, 7);
    if (!tpos)
      return 0;
  }

  switch (flags & DCA_CHANNEL_MASK) {
    case DCA_MONO:
      chans = 1;
      if (tpos)
        tpos[0] = GST_AUDIO_CHANNEL_POSITION_FRONT_MONO;
      break;
      /* case DCA_CHANNEL: */
    case DCA_STEREO:
    case DCA_STEREO_SUMDIFF:
    case DCA_STEREO_TOTAL:
    case DCA_DOLBY:
      chans = 2;
      if (tpos) {
        tpos[0] = GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT;
        tpos[1] = GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT;
      }
      break;
    case DCA_3F:
      chans = 3;
      if (tpos) {
        tpos[0] = GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER;
        tpos[1] = GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT;
        tpos[2] = GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT;
      }
      break;
    case DCA_2F1R:
      chans = 3;
      if (tpos) {
        tpos[0] = GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT;
        tpos[1] = GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT;
        tpos[2] = GST_AUDIO_CHANNEL_POSITION_REAR_CENTER;
      }
      break;
    case DCA_3F1R:
      chans = 4;
      if (tpos) {
        tpos[0] = GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER;
        tpos[1] = GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT;
        tpos[2] = GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT;
        tpos[3] = GST_AUDIO_CHANNEL_POSITION_REAR_CENTER;
      }
      break;
    case DCA_2F2R:
      chans = 4;
      if (tpos) {
        tpos[0] = GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT;
        tpos[1] = GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT;
        tpos[2] = GST_AUDIO_CHANNEL_POSITION_REAR_LEFT;
        tpos[3] = GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT;
      }
      break;
    case DCA_3F2R:
      chans = 5;
      if (tpos) {
        tpos[0] = GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER;
        tpos[1] = GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT;
        tpos[2] = GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT;
        tpos[3] = GST_AUDIO_CHANNEL_POSITION_REAR_LEFT;
        tpos[4] = GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT;
      }
      break;
    case DCA_4F2R:
      chans = 6;
      if (tpos) {
        tpos[0] = GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT_OF_CENTER;
        tpos[1] = GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT_OF_CENTER;
        tpos[2] = GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT;
        tpos[3] = GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT;
        tpos[4] = GST_AUDIO_CHANNEL_POSITION_REAR_LEFT;
        tpos[5] = GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT;
      }
      break;
    default:
      g_warning ("dtsdec: invalid flags 0x%x", flags);
      return 0;
  }
  if (flags & DCA_LFE) {
    if (tpos) {
      tpos[chans] = GST_AUDIO_CHANNEL_POSITION_LFE;
    }
    chans += 1;
  }

  return chans;
}

static gboolean
gst_dtsdec_renegotiate (GstDtsDec * dts)
{
  GstAudioChannelPosition *pos;
  GstCaps *caps = gst_caps_from_string (DTS_CAPS);
  gint channels = gst_dtsdec_channels (dts->using_channels, &pos);
  gboolean result = FALSE;

  if (!channels)
    goto done;

  GST_INFO ("dtsdec renegotiate, channels=%d, rate=%d",
      channels, dts->sample_rate);

  gst_caps_set_simple (caps,
      "channels", G_TYPE_INT, channels,
      "rate", G_TYPE_INT, (gint) dts->sample_rate, NULL);
  gst_audio_set_channel_positions (gst_caps_get_structure (caps, 0), pos);
  g_free (pos);

  if (!gst_pad_set_caps (GST_AUDIO_DECODER_SRC_PAD (dts), caps))
    goto done;

  result = TRUE;

done:
  if (caps) {
    gst_caps_unref (caps);
  }
  return result;
}

static void
gst_dtsdec_update_streaminfo (GstDtsDec * dts)
{
  GstTagList *taglist;

  if (dts->bit_rate > 3) {
    taglist = gst_tag_list_new ();
    /* 1 => open bitrate, 2 => variable bitrate, 3 => lossless */
    gst_tag_list_add (taglist, GST_TAG_MERGE_APPEND, GST_TAG_BITRATE,
        (guint) dts->bit_rate, NULL);

    if (dts->pending_tags) {
      gst_tag_list_free (dts->pending_tags);
      dts->pending_tags = NULL;
    }

    dts->pending_tags = taglist;
  }
}

static GstFlowReturn
gst_dtsdec_pre_push (GstAudioDecoder * bdec, GstBuffer ** buffer)
{
  GstDtsDec *dts = GST_DTSDEC (bdec);

  if (G_UNLIKELY (dts->pending_tags)) {
    gst_element_found_tags_for_pad (GST_ELEMENT (dts),
        GST_AUDIO_DECODER_SRC_PAD (dts), dts->pending_tags);
    dts->pending_tags = NULL;
  }

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_dtsdec_handle_frame (GstAudioDecoder * bdec, GstBuffer * buffer)
{
  GstDtsDec *dts;
  gint channels, i, num_blocks;
  gboolean need_renegotiation = FALSE;
  guint8 *data;
  gint size, chans;
  gint length = 0, flags, sample_rate, bit_rate, frame_length;
  GstFlowReturn result = GST_FLOW_OK;
  GstBuffer *outbuf;

  dts = GST_DTSDEC (bdec);

  /* no fancy draining */
  if (G_UNLIKELY (!buffer))
    return GST_FLOW_OK;

  /* parsed stuff already, so this should work out fine */
  data = GST_BUFFER_DATA (buffer);
  size = GST_BUFFER_SIZE (buffer);
  g_assert (size >= 7);

  bit_rate = dts->bit_rate;
  sample_rate = dts->sample_rate;
  flags = 0;
  length = dca_syncinfo (dts->state, data, &flags, &sample_rate, &bit_rate,
      &frame_length);
  g_assert (length == size);

  if (flags != dts->prev_flags) {
    dts->prev_flags = flags;
    dts->flag_update = TRUE;
  }

  /* go over stream properties, renegotiate or update streaminfo if needed */
  if (dts->sample_rate != sample_rate) {
    need_renegotiation = TRUE;
    dts->sample_rate = sample_rate;
  }

  if (flags) {
    dts->stream_channels = flags & (DCA_CHANNEL_MASK | DCA_LFE);
  }

  if (bit_rate != dts->bit_rate) {
    dts->bit_rate = bit_rate;
    gst_dtsdec_update_streaminfo (dts);
  }

  /* If we haven't had an explicit number of channels chosen through properties
   * at this point, choose what to downmix to now, based on what the peer will 
   * accept - this allows a52dec to do downmixing in preference to a 
   * downstream element such as audioconvert.
   * FIXME: Add the property back in for forcing output channels.
   */
  if (dts->request_channels != DCA_CHANNEL) {
    flags = dts->request_channels;
  } else if (dts->flag_update) {
    GstCaps *caps;

    dts->flag_update = FALSE;

    caps = gst_pad_get_allowed_caps (GST_AUDIO_DECODER_SRC_PAD (dts));
    if (caps && gst_caps_get_size (caps) > 0) {
      GstCaps *copy = gst_caps_copy_nth (caps, 0);
      GstStructure *structure = gst_caps_get_structure (copy, 0);
      gint channels;
      const int dts_channels[6] = {
        DCA_MONO,
        DCA_STEREO,
        DCA_STEREO | DCA_LFE,
        DCA_2F2R,
        DCA_2F2R | DCA_LFE,
        DCA_3F2R | DCA_LFE,
      };

      /* Prefer the original number of channels, but fixate to something 
       * preferred (first in the caps) downstream if possible.
       */
      gst_structure_fixate_field_nearest_int (structure, "channels",
          flags ? gst_dtsdec_channels (flags, NULL) : 6);
      gst_structure_get_int (structure, "channels", &channels);
      if (channels <= 6)
        flags = dts_channels[channels - 1];
      else
        flags = dts_channels[5];

      gst_caps_unref (copy);
    } else if (flags) {
      flags = dts->stream_channels;
    } else {
      flags = DCA_3F2R | DCA_LFE;
    }

    if (caps)
      gst_caps_unref (caps);
  } else {
    flags = dts->using_channels;
  }

  /* process */
  flags |= DCA_ADJUST_LEVEL;
  dts->level = 1;
  if (dca_frame (dts->state, data, &flags, &dts->level, dts->bias)) {
    GST_AUDIO_DECODER_ERROR (dts, 1, STREAM, DECODE, (NULL),
        ("dts_frame error"), result);
    goto exit;
  }

  channels = flags & (DCA_CHANNEL_MASK | DCA_LFE);
  if (dts->using_channels != channels) {
    need_renegotiation = TRUE;
    dts->using_channels = channels;
  }

  /* negotiate if required */
  if (need_renegotiation) {
    GST_DEBUG_OBJECT (dts,
        "dtsdec: sample_rate:%d stream_chans:0x%x using_chans:0x%x",
        dts->sample_rate, dts->stream_channels, dts->using_channels);
    if (!gst_dtsdec_renegotiate (dts))
      goto failed_negotiation;
  }

  if (dts->dynamic_range_compression == FALSE) {
    dca_dynrng (dts->state, NULL, NULL);
  }

  flags &= (DCA_CHANNEL_MASK | DCA_LFE);
  chans = gst_dtsdec_channels (flags, NULL);
  if (!chans)
    goto invalid_flags;

  /* handle decoded data, one block is 256 samples */
  num_blocks = dca_blocks_num (dts->state);
  result =
      gst_pad_alloc_buffer_and_set_caps (GST_AUDIO_DECODER_SRC_PAD (dts), 0,
      256 * chans * (SAMPLE_WIDTH / 8) * num_blocks,
      GST_PAD_CAPS (GST_AUDIO_DECODER_SRC_PAD (dts)), &outbuf);
  if (result != GST_FLOW_OK)
    goto exit;

  data = GST_BUFFER_DATA (outbuf);
  for (i = 0; i < num_blocks; i++) {
    if (dca_block (dts->state)) {
      /* also marks discont */
      GST_AUDIO_DECODER_ERROR (dts, 1, STREAM, DECODE, (NULL),
          ("error decoding block %d", i), result);
      if (result != GST_FLOW_OK)
        goto exit;
    } else {
      gint n, c;

      for (n = 0; n < 256; n++) {
        for (c = 0; c < chans; c++) {
          ((sample_t *) data)[n * chans + c] = dts->samples[c * 256 + n];
        }
      }
    }
    data += 256 * chans * (SAMPLE_WIDTH / 8);
  }

  result = gst_audio_decoder_finish_frame (bdec, outbuf, 1);

exit:
  return result;

  /* ERRORS */
failed_negotiation:
  {
    GST_ELEMENT_ERROR (dts, CORE, NEGOTIATION, (NULL), (NULL));
    return GST_FLOW_ERROR;
  }
invalid_flags:
  {
    GST_ELEMENT_ERROR (GST_ELEMENT (dts), STREAM, DECODE, (NULL),
        ("Invalid channel flags: %d", flags));
    return GST_FLOW_ERROR;
  }
}

static gboolean
gst_dtsdec_set_format (GstAudioDecoder * bdec, GstCaps * caps)
{
  GstDtsDec *dts = GST_DTSDEC (bdec);
  GstStructure *structure;

  structure = gst_caps_get_structure (caps, 0);

  if (structure && gst_structure_has_name (structure, "audio/x-private1-dts"))
    dts->dvdmode = TRUE;
  else
    dts->dvdmode = FALSE;

  return TRUE;
}

static GstFlowReturn
gst_dtsdec_chain (GstPad * pad, GstBuffer * buf)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GstDtsDec *dts = GST_DTSDEC (GST_PAD_PARENT (pad));
  gint first_access;

  if (dts->dvdmode) {
    gint size = GST_BUFFER_SIZE (buf);
    guint8 *data = GST_BUFFER_DATA (buf);
    gint offset, len;
    GstBuffer *subbuf;

    if (size < 2)
      goto not_enough_data;

    first_access = (data[0] << 8) | data[1];

    /* Skip the first_access header */
    offset = 2;

    if (first_access > 1) {
      /* Length of data before first_access */
      len = first_access - 1;

      if (len <= 0 || offset + len > size)
        goto bad_first_access_parameter;

      subbuf = gst_buffer_create_sub (buf, offset, len);
      gst_buffer_copy_metadata (subbuf, buf, GST_BUFFER_COPY_ALL);
      GST_BUFFER_TIMESTAMP (subbuf) = GST_CLOCK_TIME_NONE;
      ret = dts->base_chain (pad, subbuf);
      if (ret != GST_FLOW_OK) {
        gst_buffer_unref (buf);
        goto done;
      }

      offset += len;
      len = size - offset;

      if (len > 0) {
        subbuf = gst_buffer_create_sub (buf, offset, len);
        gst_buffer_copy_metadata (subbuf, buf, GST_BUFFER_COPY_ALL);
        GST_BUFFER_TIMESTAMP (subbuf) = GST_BUFFER_TIMESTAMP (buf);

        ret = dts->base_chain (pad, subbuf);
      }
      gst_buffer_unref (buf);
    } else {
      /* first_access = 0 or 1, so if there's a timestamp it applies to the first byte */
      subbuf = gst_buffer_create_sub (buf, offset, size - offset);
      gst_buffer_copy_metadata (subbuf, buf, GST_BUFFER_COPY_ALL);
      GST_BUFFER_TIMESTAMP (subbuf) = GST_BUFFER_TIMESTAMP (buf);
      ret = dts->base_chain (pad, subbuf);
      gst_buffer_unref (buf);
    }
  } else {
    ret = dts->base_chain (pad, buf);
  }

done:
  return ret;

/* ERRORS */
not_enough_data:
  {
    GST_ELEMENT_ERROR (GST_ELEMENT (dts), STREAM, DECODE, (NULL),
        ("Insufficient data in buffer. Can't determine first_acess"));
    gst_buffer_unref (buf);
    return GST_FLOW_ERROR;
  }
bad_first_access_parameter:
  {
    GST_ELEMENT_ERROR (GST_ELEMENT (dts), STREAM, DECODE, (NULL),
        ("Bad first_access parameter (%d) in buffer", first_access));
    gst_buffer_unref (buf);
    return GST_FLOW_ERROR;
  }
}

static void
gst_dtsdec_set_property (GObject * object, guint prop_id, const GValue * value,
    GParamSpec * pspec)
{
  GstDtsDec *dts = GST_DTSDEC (object);

  switch (prop_id) {
    case ARG_DRC:
      dts->dynamic_range_compression = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_dtsdec_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstDtsDec *dts = GST_DTSDEC (object);

  switch (prop_id) {
    case ARG_DRC:
      g_value_set_boolean (value, dts->dynamic_range_compression);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
plugin_init (GstPlugin * plugin)
{
#if HAVE_ORC
  orc_init ();
#endif

  if (!gst_element_register (plugin, "dtsdec", GST_RANK_PRIMARY,
          GST_TYPE_DTSDEC))
    return FALSE;

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "dtsdec",
    "Decodes DTS audio streams",
    plugin_init, VERSION, "GPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN);
