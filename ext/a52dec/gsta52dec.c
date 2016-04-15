/* GStreamer
 * Copyright (C) <2001> David I. Lehn <dlehn@users.sourceforge.net>
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
 * SECTION:element-a52dec
 *
 * Dolby Digital (AC-3) audio decoder.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch-1.0 dvdreadsrc title=1 ! mpegpsdemux ! a52dec ! audioconvert ! audioresample ! autoaudiosink
 * ]| Play audio part of a dvd title.
 * |[
 * gst-launch-1.0 filesrc location=abc.ac3 ! ac3parse ! a52dec ! audioconvert ! audioresample ! autoaudiosink
 * ]| Decode and play a stand alone AC-3 file.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include <stdlib.h>
#include "_stdint.h"

#include <gst/gst.h>

#include <a52dec/a52.h>
#if !defined(A52_ACCEL_DETECT)
#  include <a52dec/mm_accel.h>
#endif
#include "gsta52dec.h"

#if HAVE_ORC
#include <orc/orc.h>
#endif

#ifdef LIBA52_DOUBLE
#define SAMPLE_WIDTH 64
#define SAMPLE_FORMAT GST_AUDIO_NE(F64)
#define SAMPLE_TYPE GST_AUDIO_FORMAT_F64
#else
#define SAMPLE_WIDTH 32
#define SAMPLE_FORMAT GST_AUDIO_NE(F32)
#define SAMPLE_TYPE GST_AUDIO_FORMAT_F32
#endif

GST_DEBUG_CATEGORY_STATIC (a52dec_debug);
#define GST_CAT_DEFAULT (a52dec_debug)

/* A52Dec args */
enum
{
  ARG_0,
  ARG_DRC,
  ARG_MODE,
  ARG_LFE,
};

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-ac3; audio/ac3; audio/x-private1-ac3")
    );

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw, "
        "format = (string) " SAMPLE_FORMAT ", "
        "layout = (string) interleaved, "
        "rate = (int) [ 4000, 96000 ], " "channels = (int) [ 1, 6 ]")
    );

#define gst_a52dec_parent_class parent_class
G_DEFINE_TYPE (GstA52Dec, gst_a52dec, GST_TYPE_AUDIO_DECODER);

static gboolean gst_a52dec_start (GstAudioDecoder * dec);
static gboolean gst_a52dec_stop (GstAudioDecoder * dec);
static gboolean gst_a52dec_set_format (GstAudioDecoder * bdec, GstCaps * caps);
static GstFlowReturn gst_a52dec_parse (GstAudioDecoder * dec,
    GstAdapter * adapter, gint * offset, gint * length);
static GstFlowReturn gst_a52dec_handle_frame (GstAudioDecoder * dec,
    GstBuffer * buffer);

static GstFlowReturn gst_a52dec_chain (GstPad * pad, GstObject * parent,
    GstBuffer * buffer);
static void gst_a52dec_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_a52dec_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

#define GST_TYPE_A52DEC_MODE (gst_a52dec_mode_get_type())
static GType
gst_a52dec_mode_get_type (void)
{
  static GType a52dec_mode_type = 0;
  static const GEnumValue a52dec_modes[] = {
    {A52_MONO, "Mono", "mono"},
    {A52_STEREO, "Stereo", "stereo"},
    {A52_3F, "3 Front", "3f"},
    {A52_2F1R, "2 Front, 1 Rear", "2f1r"},
    {A52_3F1R, "3 Front, 1 Rear", "3f1r"},
    {A52_2F2R, "2 Front, 2 Rear", "2f2r"},
    {A52_3F2R, "3 Front, 2 Rear", "3f2r"},
    {A52_DOLBY, "Dolby", "dolby"},
    {0, NULL, NULL},
  };

  if (!a52dec_mode_type) {
    a52dec_mode_type = g_enum_register_static ("GstA52DecMode", a52dec_modes);
  }
  return a52dec_mode_type;
}

static void
gst_a52dec_class_init (GstA52DecClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstAudioDecoderClass *gstbase_class;
  guint cpuflags = 0;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbase_class = (GstAudioDecoderClass *) klass;

  gobject_class->set_property = gst_a52dec_set_property;
  gobject_class->get_property = gst_a52dec_get_property;

  gstbase_class->start = GST_DEBUG_FUNCPTR (gst_a52dec_start);
  gstbase_class->stop = GST_DEBUG_FUNCPTR (gst_a52dec_stop);
  gstbase_class->set_format = GST_DEBUG_FUNCPTR (gst_a52dec_set_format);
  gstbase_class->parse = GST_DEBUG_FUNCPTR (gst_a52dec_parse);
  gstbase_class->handle_frame = GST_DEBUG_FUNCPTR (gst_a52dec_handle_frame);

  /**
   * GstA52Dec::drc
   *
   * Set to true to apply the recommended Dolby Digital dynamic range compression
   * to the audio stream. Dynamic range compression makes loud sounds
   * softer and soft sounds louder, so you can more easily listen
   * to the stream without disturbing other people.
   */
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_DRC,
      g_param_spec_boolean ("drc", "Dynamic Range Compression",
          "Use Dynamic Range Compression", FALSE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  /**
   * GstA52Dec::mode
   *
   * Force a particular output channel configuration from the decoder. By default,
   * the channel downmix (if any) is chosen automatically based on the downstream
   * capabilities of the pipeline.
   */
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_MODE,
      g_param_spec_enum ("mode", "Decoder Mode", "Decoding Mode (default 3f2r)",
          GST_TYPE_A52DEC_MODE, A52_3F2R,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  /**
   * GstA52Dec::lfe
   *
   * Whether to output the LFE (Low Frequency Emitter) channel of the audio stream.
   */
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_LFE,
      g_param_spec_boolean ("lfe", "LFE", "LFE", TRUE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_add_static_pad_template (gstelement_class, &sink_factory);
  gst_element_class_add_static_pad_template (gstelement_class, &src_factory);
  gst_element_class_set_static_metadata (gstelement_class,
      "ATSC A/52 audio decoder", "Codec/Decoder/Audio",
      "Decodes ATSC A/52 encoded audio streams",
      "David I. Lehn <dlehn@users.sourceforge.net>");

  GST_DEBUG_CATEGORY_INIT (a52dec_debug, "a52dec", 0,
      "AC3/A52 software decoder");

  /* If no CPU instruction based acceleration is available, end up using the
   * generic software djbfft based one when available in the used liba52 */
#ifdef MM_ACCEL_DJBFFT
  klass->a52_cpuflags = MM_ACCEL_DJBFFT;
#elif defined(A52_ACCEL_DETECT)
  klass->a52_cpuflags = A52_ACCEL_DETECT;
#else
  klass->a52_cpuflags = 0;
#endif

#if HAVE_ORC && !defined(A52_ACCEL_DETECT)
  cpuflags = orc_target_get_default_flags (orc_target_get_by_name ("mmx"));
  if (cpuflags & ORC_TARGET_MMX_MMX)
    klass->a52_cpuflags |= MM_ACCEL_X86_MMX;
  if (cpuflags & ORC_TARGET_MMX_3DNOW)
    klass->a52_cpuflags |= MM_ACCEL_X86_3DNOW;
  if (cpuflags & ORC_TARGET_MMX_MMXEXT)
    klass->a52_cpuflags |= MM_ACCEL_X86_MMXEXT;
#endif

  GST_LOG ("CPU flags: a52=%08x, orc=%08x", klass->a52_cpuflags, cpuflags);
}

static void
gst_a52dec_init (GstA52Dec * a52dec)
{
  a52dec->request_channels = A52_CHANNEL;
  a52dec->dynamic_range_compression = FALSE;

  a52dec->state = NULL;
  a52dec->samples = NULL;

  gst_audio_decoder_set_use_default_pad_acceptcaps (GST_AUDIO_DECODER_CAST
      (a52dec), TRUE);
  GST_PAD_SET_ACCEPT_TEMPLATE (GST_AUDIO_DECODER_SINK_PAD (a52dec));

  /* retrieve and intercept base class chain.
   * Quite HACKish, but that's dvd specs/caps for you,
   * since one buffer needs to be split into 2 frames */
  a52dec->base_chain = GST_PAD_CHAINFUNC (GST_AUDIO_DECODER_SINK_PAD (a52dec));
  gst_pad_set_chain_function (GST_AUDIO_DECODER_SINK_PAD (a52dec),
      GST_DEBUG_FUNCPTR (gst_a52dec_chain));
}

static gboolean
gst_a52dec_start (GstAudioDecoder * dec)
{
  GstA52Dec *a52dec = GST_A52DEC (dec);
  GstA52DecClass *klass;
  static GMutex init_mutex;

  GST_DEBUG_OBJECT (dec, "start");

  klass = GST_A52DEC_CLASS (G_OBJECT_GET_CLASS (a52dec));
  g_mutex_lock (&init_mutex);
#if defined(A52_ACCEL_DETECT)
  a52dec->state = a52_init ();
  /* This line is just to avoid being accused of not using klass */
  a52_accel (klass->a52_cpuflags & A52_ACCEL_DETECT);
#else
  a52dec->state = a52_init (klass->a52_cpuflags);
#endif
  g_mutex_unlock (&init_mutex);

  if (!a52dec->state) {
    GST_ELEMENT_ERROR (GST_ELEMENT (a52dec), LIBRARY, INIT, (NULL),
        ("failed to initialize a52 state"));
    return FALSE;
  }

  a52dec->samples = a52_samples (a52dec->state);
  a52dec->bit_rate = -1;
  a52dec->sample_rate = -1;
  a52dec->stream_channels = A52_CHANNEL;
  a52dec->using_channels = A52_CHANNEL;
  a52dec->level = 1;
  a52dec->bias = 0;
  a52dec->flag_update = TRUE;

  /* call upon legacy upstream byte support (e.g. seeking) */
  gst_audio_decoder_set_estimate_rate (dec, TRUE);

  return TRUE;
}

static gboolean
gst_a52dec_stop (GstAudioDecoder * dec)
{
  GstA52Dec *a52dec = GST_A52DEC (dec);

  GST_DEBUG_OBJECT (dec, "stop");

  a52dec->samples = NULL;
  if (a52dec->state) {
    a52_free (a52dec->state);
    a52dec->state = NULL;
  }

  return TRUE;
}

static GstFlowReturn
gst_a52dec_parse (GstAudioDecoder * bdec, GstAdapter * adapter,
    gint * _offset, gint * len)
{
  GstA52Dec *a52dec;
  const guint8 *data;
  gint av, size;
  gint length = 0, flags, sample_rate, bit_rate;
  GstFlowReturn result = GST_FLOW_EOS;

  a52dec = GST_A52DEC (bdec);

  size = av = gst_adapter_available (adapter);
  data = (const guint8 *) gst_adapter_map (adapter, av);

  /* find and read header */
  bit_rate = a52dec->bit_rate;
  sample_rate = a52dec->sample_rate;
  flags = 0;
  while (size >= 7) {
    length = a52_syncinfo ((guint8 *) data, &flags, &sample_rate, &bit_rate);

    if (length == 0) {
      /* shift window to re-find sync */
      data++;
      size--;
    } else if (length <= size) {
      GST_LOG_OBJECT (a52dec, "Sync: frame size %d", length);
      result = GST_FLOW_OK;
      break;
    } else {
      GST_LOG_OBJECT (a52dec, "Not enough data available (needed %d had %d)",
          length, size);
      break;
    }
  }
  gst_adapter_unmap (adapter);

  *_offset = av - size;
  *len = length;

  return result;
}

static gint
gst_a52dec_channels (int flags, GstAudioChannelPosition * pos)
{
  gint chans = 0;

  if (flags & A52_LFE) {
    chans += 1;
    if (pos) {
      pos[0] = GST_AUDIO_CHANNEL_POSITION_LFE1;
    }
  }
  flags &= A52_CHANNEL_MASK;
  switch (flags) {
    case A52_3F2R:
      if (pos) {
        pos[0 + chans] = GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT;
        pos[1 + chans] = GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER;
        pos[2 + chans] = GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT;
        pos[3 + chans] = GST_AUDIO_CHANNEL_POSITION_REAR_LEFT;
        pos[4 + chans] = GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT;
      }
      chans += 5;
      break;
    case A52_2F2R:
      if (pos) {
        pos[0 + chans] = GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT;
        pos[1 + chans] = GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT;
        pos[2 + chans] = GST_AUDIO_CHANNEL_POSITION_REAR_LEFT;
        pos[3 + chans] = GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT;
      }
      chans += 4;
      break;
    case A52_3F1R:
      if (pos) {
        pos[0 + chans] = GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT;
        pos[1 + chans] = GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER;
        pos[2 + chans] = GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT;
        pos[3 + chans] = GST_AUDIO_CHANNEL_POSITION_REAR_CENTER;
      }
      chans += 4;
      break;
    case A52_2F1R:
      if (pos) {
        pos[0 + chans] = GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT;
        pos[1 + chans] = GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT;
        pos[2 + chans] = GST_AUDIO_CHANNEL_POSITION_REAR_CENTER;
      }
      chans += 3;
      break;
    case A52_3F:
      if (pos) {
        pos[0 + chans] = GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT;
        pos[1 + chans] = GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER;
        pos[2 + chans] = GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT;
      }
      chans += 3;
      break;
    case A52_CHANNEL:          /* Dual mono. Should really be handled as 2 src pads */
    case A52_STEREO:
    case A52_DOLBY:
      if (pos) {
        pos[0 + chans] = GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT;
        pos[1 + chans] = GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT;
      }
      chans += 2;
      break;
    case A52_MONO:
      if (pos) {
        pos[0 + chans] = GST_AUDIO_CHANNEL_POSITION_MONO;
      }
      chans += 1;
      break;
    default:
      /* error, caller should post error message */
      return 0;
  }

  return chans;
}

static gboolean
gst_a52dec_reneg (GstA52Dec * a52dec)
{
  gint channels;
  gboolean result = FALSE;
  GstAudioChannelPosition from[6], to[6];
  GstAudioInfo info;

  channels = gst_a52dec_channels (a52dec->using_channels, from);

  if (!channels)
    goto done;

  GST_INFO_OBJECT (a52dec, "reneg channels:%d rate:%d",
      channels, a52dec->sample_rate);

  memcpy (to, from, sizeof (GstAudioChannelPosition) * channels);
  gst_audio_channel_positions_to_valid_order (to, channels);
  gst_audio_get_channel_reorder_map (channels, from, to,
      a52dec->channel_reorder_map);

  gst_audio_info_init (&info);
  gst_audio_info_set_format (&info,
      SAMPLE_TYPE, a52dec->sample_rate, channels, (channels > 1 ? to : NULL));

  if (!gst_audio_decoder_set_output_format (GST_AUDIO_DECODER (a52dec), &info))
    goto done;

  result = TRUE;

done:
  return result;
}

static void
gst_a52dec_update_streaminfo (GstA52Dec * a52dec)
{
  GstTagList *taglist;

  taglist = gst_tag_list_new_empty ();
  gst_tag_list_add (taglist, GST_TAG_MERGE_APPEND, GST_TAG_BITRATE,
      (guint) a52dec->bit_rate, NULL);

  gst_audio_decoder_merge_tags (GST_AUDIO_DECODER (a52dec), taglist,
      GST_TAG_MERGE_REPLACE);
  gst_tag_list_unref (taglist);
}

static GstFlowReturn
gst_a52dec_handle_frame (GstAudioDecoder * bdec, GstBuffer * buffer)
{
  GstA52Dec *a52dec;
  gint channels, i;
  gboolean need_reneg = FALSE;
  gint chans;
  gint length = 0, flags, sample_rate, bit_rate;
  GstMapInfo map;
  GstFlowReturn result = GST_FLOW_OK;
  GstBuffer *outbuf;
  const gint num_blocks = 6;

  a52dec = GST_A52DEC (bdec);

  /* no fancy draining */
  if (G_UNLIKELY (!buffer))
    return GST_FLOW_OK;

  /* parsed stuff already, so this should work out fine */
  gst_buffer_map (buffer, &map, GST_MAP_READ);
  g_assert (map.size >= 7);

  /* re-obtain some sync header info,
   * should be same as during _parse and could also be cached there,
   * but anyway ... */
  bit_rate = a52dec->bit_rate;
  sample_rate = a52dec->sample_rate;
  flags = 0;
  length = a52_syncinfo (map.data, &flags, &sample_rate, &bit_rate);
  g_assert (length == map.size);

  /* update stream information, renegotiate or re-streaminfo if needed */
  need_reneg = FALSE;
  if (a52dec->sample_rate != sample_rate) {
    GST_DEBUG_OBJECT (a52dec, "sample rate changed");
    need_reneg = TRUE;
    a52dec->sample_rate = sample_rate;
  }

  if (flags) {
    if (a52dec->stream_channels != (flags & (A52_CHANNEL_MASK | A52_LFE))) {
      GST_DEBUG_OBJECT (a52dec, "stream channel flags changed, marking update");
      a52dec->flag_update = TRUE;
    }
    a52dec->stream_channels = flags & (A52_CHANNEL_MASK | A52_LFE);
  }

  if (bit_rate != a52dec->bit_rate) {
    a52dec->bit_rate = bit_rate;
    gst_a52dec_update_streaminfo (a52dec);
  }

  /* If we haven't had an explicit number of channels chosen through properties
   * at this point, choose what to downmix to now, based on what the peer will
   * accept - this allows a52dec to do downmixing in preference to a
   * downstream element such as audioconvert.
   */
  if (a52dec->request_channels != A52_CHANNEL) {
    flags = a52dec->request_channels;
  } else if (a52dec->flag_update) {
    GstCaps *caps;

    a52dec->flag_update = FALSE;

    caps = gst_pad_get_allowed_caps (GST_AUDIO_DECODER_SRC_PAD (a52dec));
    if (caps && gst_caps_get_size (caps) > 0) {
      GstCaps *copy = gst_caps_copy_nth (caps, 0);
      GstStructure *structure = gst_caps_get_structure (copy, 0);
      gint orig_channels = flags ? gst_a52dec_channels (flags, NULL) : 6;
      gint fixed_channels = 0;
      const int a52_channels[6] = {
        A52_MONO,
        A52_STEREO,
        A52_STEREO | A52_LFE,
        A52_2F2R,
        A52_2F2R | A52_LFE,
        A52_3F2R | A52_LFE,
      };

      /* Prefer the original number of channels, but fixate to something
       * preferred (first in the caps) downstream if possible.
       */
      gst_structure_fixate_field_nearest_int (structure, "channels",
          orig_channels);

      if (gst_structure_get_int (structure, "channels", &fixed_channels)
          && fixed_channels <= 6) {
        if (fixed_channels < orig_channels)
          flags = a52_channels[fixed_channels - 1];
      } else {
        flags = a52_channels[5];
      }

      gst_caps_unref (copy);
    } else if (flags)
      flags = a52dec->stream_channels;
    else
      flags = A52_3F2R | A52_LFE;

    if (caps)
      gst_caps_unref (caps);
  } else {
    flags = a52dec->using_channels;
  }

  /* process */
  flags |= A52_ADJUST_LEVEL;
  a52dec->level = 1;
  if (a52_frame (a52dec->state, map.data, &flags, &a52dec->level, a52dec->bias)) {
    gst_buffer_unmap (buffer, &map);
    GST_AUDIO_DECODER_ERROR (a52dec, 1, STREAM, DECODE, (NULL),
        ("a52_frame error"), result);
    goto exit;
  }
  gst_buffer_unmap (buffer, &map);

  channels = flags & (A52_CHANNEL_MASK | A52_LFE);
  if (a52dec->using_channels != channels) {
    need_reneg = TRUE;
    a52dec->using_channels = channels;
  }

  /* negotiate if required */
  if (need_reneg) {
    GST_DEBUG_OBJECT (a52dec,
        "a52dec reneg: sample_rate:%d stream_chans:%d using_chans:%d",
        a52dec->sample_rate, a52dec->stream_channels, a52dec->using_channels);
    if (!gst_a52dec_reneg (a52dec))
      goto failed_negotiation;
  }

  if (a52dec->dynamic_range_compression == FALSE) {
    a52_dynrng (a52dec->state, NULL, NULL);
  }

  flags &= (A52_CHANNEL_MASK | A52_LFE);
  chans = gst_a52dec_channels (flags, NULL);
  if (!chans)
    goto invalid_flags;

  /* handle decoded data;
   * each frame has 6 blocks, one block is 256 samples, ea */
  outbuf =
      gst_buffer_new_and_alloc (256 * chans * (SAMPLE_WIDTH / 8) * num_blocks);

  gst_buffer_map (outbuf, &map, GST_MAP_WRITE);
  {
    guint8 *ptr = map.data;
    for (i = 0; i < num_blocks; i++) {
      if (a52_block (a52dec->state)) {
        /* also marks discont */
        GST_AUDIO_DECODER_ERROR (a52dec, 1, STREAM, DECODE, (NULL),
            ("error decoding block %d", i), result);
        if (result != GST_FLOW_OK) {
          gst_buffer_unmap (outbuf, &map);
          goto exit;
        }
      } else {
        gint n, c;
        gint *reorder_map = a52dec->channel_reorder_map;

        for (n = 0; n < 256; n++) {
          for (c = 0; c < chans; c++) {
            ((sample_t *) ptr)[n * chans + reorder_map[c]] =
                a52dec->samples[c * 256 + n];
          }
        }
      }
      ptr += 256 * chans * (SAMPLE_WIDTH / 8);
    }
  }
  gst_buffer_unmap (outbuf, &map);

  result = gst_audio_decoder_finish_frame (bdec, outbuf, 1);

exit:
  return result;

  /* ERRORS */
failed_negotiation:
  {
    GST_ELEMENT_ERROR (a52dec, CORE, NEGOTIATION, (NULL), (NULL));
    return GST_FLOW_ERROR;
  }
invalid_flags:
  {
    GST_ELEMENT_ERROR (GST_ELEMENT (a52dec), STREAM, DECODE, (NULL),
        ("Invalid channel flags: %d", flags));
    return GST_FLOW_ERROR;
  }
}

static gboolean
gst_a52dec_set_format (GstAudioDecoder * bdec, GstCaps * caps)
{
  GstA52Dec *a52dec = GST_A52DEC (bdec);
  GstStructure *structure;

  structure = gst_caps_get_structure (caps, 0);

  if (structure && gst_structure_has_name (structure, "audio/x-private1-ac3"))
    a52dec->dvdmode = TRUE;
  else
    a52dec->dvdmode = FALSE;

  return TRUE;
}

static GstFlowReturn
gst_a52dec_chain (GstPad * pad, GstObject * parent, GstBuffer * buf)
{
  GstA52Dec *a52dec = GST_A52DEC (parent);
  GstFlowReturn ret = GST_FLOW_OK;
  gint first_access;

  if (a52dec->dvdmode) {
    gsize size;
    guint8 data[2];
    gint offset;
    gint len;
    GstBuffer *subbuf;

    size = gst_buffer_get_size (buf);
    if (size < 2)
      goto not_enough_data;

    gst_buffer_extract (buf, 0, data, 2);
    first_access = (data[0] << 8) | data[1];

    /* Skip the first_access header */
    offset = 2;

    if (first_access > 1) {
      /* Length of data before first_access */
      len = first_access - 1;

      if (len <= 0 || offset + len > size)
        goto bad_first_access_parameter;

      subbuf = gst_buffer_copy_region (buf, GST_BUFFER_COPY_ALL, offset, len);
      GST_BUFFER_TIMESTAMP (subbuf) = GST_CLOCK_TIME_NONE;
      ret = a52dec->base_chain (pad, parent, subbuf);
      if (ret != GST_FLOW_OK) {
        gst_buffer_unref (buf);
        goto done;
      }

      offset += len;
      len = size - offset;

      if (len > 0) {
        subbuf = gst_buffer_copy_region (buf, GST_BUFFER_COPY_ALL, offset, len);
        GST_BUFFER_TIMESTAMP (subbuf) = GST_BUFFER_TIMESTAMP (buf);

        ret = a52dec->base_chain (pad, parent, subbuf);
      }
      gst_buffer_unref (buf);
    } else {
      /* first_access = 0 or 1, so if there's a timestamp it applies to the first byte */
      subbuf =
          gst_buffer_copy_region (buf, GST_BUFFER_COPY_ALL, offset,
          size - offset);
      GST_BUFFER_TIMESTAMP (subbuf) = GST_BUFFER_TIMESTAMP (buf);
      gst_buffer_unref (buf);
      ret = a52dec->base_chain (pad, parent, subbuf);
    }
  } else {
    ret = a52dec->base_chain (pad, parent, buf);
  }

done:
  return ret;

/* ERRORS */
not_enough_data:
  {
    GST_ELEMENT_ERROR (GST_ELEMENT (a52dec), STREAM, DECODE, (NULL),
        ("Insufficient data in buffer. Can't determine first_acess"));
    gst_buffer_unref (buf);
    return GST_FLOW_ERROR;
  }
bad_first_access_parameter:
  {
    GST_ELEMENT_ERROR (GST_ELEMENT (a52dec), STREAM, DECODE, (NULL),
        ("Bad first_access parameter (%d) in buffer", first_access));
    gst_buffer_unref (buf);
    return GST_FLOW_ERROR;
  }
}

static void
gst_a52dec_set_property (GObject * object, guint prop_id, const GValue * value,
    GParamSpec * pspec)
{
  GstA52Dec *src = GST_A52DEC (object);

  switch (prop_id) {
    case ARG_DRC:
      GST_OBJECT_LOCK (src);
      src->dynamic_range_compression = g_value_get_boolean (value);
      GST_OBJECT_UNLOCK (src);
      break;
    case ARG_MODE:
      GST_OBJECT_LOCK (src);
      src->request_channels &= ~A52_CHANNEL_MASK;
      src->request_channels |= g_value_get_enum (value);
      GST_OBJECT_UNLOCK (src);
      break;
    case ARG_LFE:
      GST_OBJECT_LOCK (src);
      src->request_channels &= ~A52_LFE;
      src->request_channels |= g_value_get_boolean (value) ? A52_LFE : 0;
      GST_OBJECT_UNLOCK (src);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_a52dec_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstA52Dec *src = GST_A52DEC (object);

  switch (prop_id) {
    case ARG_DRC:
      GST_OBJECT_LOCK (src);
      g_value_set_boolean (value, src->dynamic_range_compression);
      GST_OBJECT_UNLOCK (src);
      break;
    case ARG_MODE:
      GST_OBJECT_LOCK (src);
      g_value_set_enum (value, src->request_channels & A52_CHANNEL_MASK);
      GST_OBJECT_UNLOCK (src);
      break;
    case ARG_LFE:
      GST_OBJECT_LOCK (src);
      g_value_set_boolean (value, src->request_channels & A52_LFE);
      GST_OBJECT_UNLOCK (src);
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

  if (!gst_element_register (plugin, "a52dec", GST_RANK_SECONDARY,
          GST_TYPE_A52DEC))
    return FALSE;

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    a52dec,
    "Decodes ATSC A/52 encoded audio streams",
    plugin_init, VERSION, "GPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN);
