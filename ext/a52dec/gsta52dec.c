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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/**
 * SECTION:element-a52dec
 *
 * Dolby Digital (AC-3) audio decoder.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch dvdreadsrc title=1 ! mpegpsdemux ! a52dec ! audioresample ! audioconvert ! alsasink
 * ]| Play audio track from a dvd.
 * |[
 * gst-launch filesrc location=abc.ac3 ! a52dec ! audioresample ! audioconvert ! alsasink
 * ]| Decode a stand alone file and play it.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include <stdlib.h>
#include "_stdint.h"

#include <gst/gst.h>
#include <gst/audio/multichannel.h>

#include <a52dec/a52.h>
#include <a52dec/mm_accel.h>
#include "gsta52dec.h"

#if HAVE_ORC
#include <orc/orc.h>
#endif

#ifdef LIBA52_DOUBLE
#define SAMPLE_WIDTH 64
#else
#define SAMPLE_WIDTH 32
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
    GST_STATIC_CAPS ("audio/x-raw-float, "
        "endianness = (int) " G_STRINGIFY (G_BYTE_ORDER) ", "
        "width = (int) " G_STRINGIFY (SAMPLE_WIDTH) ", "
        "rate = (int) [ 4000, 96000 ], " "channels = (int) [ 1, 6 ]")
    );

GST_BOILERPLATE (GstA52Dec, gst_a52dec, GstElement, GST_TYPE_ELEMENT);

static GstFlowReturn gst_a52dec_chain (GstPad * pad, GstBuffer * buffer);
static GstFlowReturn gst_a52dec_chain_raw (GstPad * pad, GstBuffer * buf);
static gboolean gst_a52dec_sink_setcaps (GstPad * pad, GstCaps * caps);
static gboolean gst_a52dec_sink_event (GstPad * pad, GstEvent * event);
static GstStateChangeReturn gst_a52dec_change_state (GstElement * element,
    GstStateChange transition);

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
gst_a52dec_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_static_pad_template (element_class, &sink_factory);
  gst_element_class_add_static_pad_template (element_class, &src_factory);
  gst_element_class_set_details_simple (element_class,
      "ATSC A/52 audio decoder", "Codec/Decoder/Audio",
      "Decodes ATSC A/52 encoded audio streams",
      "David I. Lehn <dlehn@users.sourceforge.net>");

  GST_DEBUG_CATEGORY_INIT (a52dec_debug, "a52dec", 0,
      "AC3/A52 software decoder");
}

static void
gst_a52dec_class_init (GstA52DecClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  guint cpuflags;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->set_property = gst_a52dec_set_property;
  gobject_class->get_property = gst_a52dec_get_property;

  gstelement_class->change_state = GST_DEBUG_FUNCPTR (gst_a52dec_change_state);

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

  /* If no CPU instruction based acceleration is available, end up using the
   * generic software djbfft based one when available in the used liba52 */
#ifdef MM_ACCEL_DJBFFT
  klass->a52_cpuflags = MM_ACCEL_DJBFFT;
#else
  klass->a52_cpuflags = 0;
#endif

#if HAVE_ORC
  cpuflags = orc_target_get_default_flags (orc_target_get_by_name ("mmx"));

  if (cpuflags & ORC_TARGET_MMX_MMX)
    klass->a52_cpuflags |= MM_ACCEL_X86_MMX;
  if (cpuflags & ORC_TARGET_MMX_3DNOW)
    klass->a52_cpuflags |= MM_ACCEL_X86_3DNOW;
  if (cpuflags & ORC_TARGET_MMX_MMXEXT)
    klass->a52_cpuflags |= MM_ACCEL_X86_MMXEXT;
#else
  cpuflags = 0;
#endif

  GST_LOG ("CPU flags: a52=%08x, liboil=%08x", klass->a52_cpuflags, cpuflags);
}

static void
gst_a52dec_init (GstA52Dec * a52dec, GstA52DecClass * g_class)
{
  /* create the sink and src pads */
  a52dec->sinkpad = gst_pad_new_from_static_template (&sink_factory, "sink");
  gst_pad_set_setcaps_function (a52dec->sinkpad,
      GST_DEBUG_FUNCPTR (gst_a52dec_sink_setcaps));
  gst_pad_set_chain_function (a52dec->sinkpad,
      GST_DEBUG_FUNCPTR (gst_a52dec_chain));
  gst_pad_set_event_function (a52dec->sinkpad,
      GST_DEBUG_FUNCPTR (gst_a52dec_sink_event));
  gst_element_add_pad (GST_ELEMENT (a52dec), a52dec->sinkpad);

  a52dec->srcpad = gst_pad_new_from_static_template (&src_factory, "src");
  gst_pad_use_fixed_caps (a52dec->srcpad);
  gst_element_add_pad (GST_ELEMENT (a52dec), a52dec->srcpad);

  a52dec->request_channels = A52_CHANNEL;
  a52dec->dynamic_range_compression = FALSE;

  a52dec->state = NULL;
  a52dec->samples = NULL;

  gst_segment_init (&a52dec->segment, GST_FORMAT_UNDEFINED);
}

static gint
gst_a52dec_channels (int flags, GstAudioChannelPosition ** _pos)
{
  gint chans = 0;
  GstAudioChannelPosition *pos = NULL;

  /* allocated just for safety. Number makes no sense */
  if (_pos) {
    pos = g_new (GstAudioChannelPosition, 6);
    *_pos = pos;
  }

  if (flags & A52_LFE) {
    chans += 1;
    if (pos) {
      pos[0] = GST_AUDIO_CHANNEL_POSITION_LFE;
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
        pos[0 + chans] = GST_AUDIO_CHANNEL_POSITION_FRONT_MONO;
      }
      chans += 1;
      break;
    default:
      /* error, caller should post error message */
      g_free (pos);
      return 0;
  }

  return chans;
}

static void
clear_queued (GstA52Dec * dec)
{
  g_list_foreach (dec->queued, (GFunc) gst_mini_object_unref, NULL);
  g_list_free (dec->queued);
  dec->queued = NULL;
}

static GstFlowReturn
flush_queued (GstA52Dec * dec)
{
  GstFlowReturn ret = GST_FLOW_OK;

  while (dec->queued) {
    GstBuffer *buf = GST_BUFFER_CAST (dec->queued->data);

    GST_LOG_OBJECT (dec, "pushing buffer %p, timestamp %"
        GST_TIME_FORMAT ", duration %" GST_TIME_FORMAT, buf,
        GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)),
        GST_TIME_ARGS (GST_BUFFER_DURATION (buf)));

    /* iterate ouput queue an push downstream */
    ret = gst_pad_push (dec->srcpad, buf);

    dec->queued = g_list_delete_link (dec->queued, dec->queued);
  }
  return ret;
}

static GstFlowReturn
gst_a52dec_drain (GstA52Dec * dec)
{
  GstFlowReturn ret = GST_FLOW_OK;

  if (dec->segment.rate < 0.0) {
    /* if we have some queued frames for reverse playback, flush
     * them now */
    ret = flush_queued (dec);
  }
  return ret;
}

static GstFlowReturn
gst_a52dec_push (GstA52Dec * a52dec,
    GstPad * srcpad, int flags, sample_t * samples, GstClockTime timestamp)
{
  GstBuffer *buf;
  int chans, n, c;
  GstFlowReturn result;

  flags &= (A52_CHANNEL_MASK | A52_LFE);
  chans = gst_a52dec_channels (flags, NULL);
  if (!chans) {
    GST_ELEMENT_ERROR (GST_ELEMENT (a52dec), STREAM, DECODE, (NULL),
        ("invalid channel flags: %d", flags));
    return GST_FLOW_ERROR;
  }

  result =
      gst_pad_alloc_buffer_and_set_caps (srcpad, 0,
      256 * chans * (SAMPLE_WIDTH / 8), GST_PAD_CAPS (srcpad), &buf);
  if (result != GST_FLOW_OK)
    return result;

  for (n = 0; n < 256; n++) {
    for (c = 0; c < chans; c++) {
      ((sample_t *) GST_BUFFER_DATA (buf))[n * chans + c] =
          samples[c * 256 + n];
    }
  }

  GST_BUFFER_TIMESTAMP (buf) = timestamp;
  GST_BUFFER_DURATION (buf) = 256 * GST_SECOND / a52dec->sample_rate;

  result = GST_FLOW_OK;
  if ((buf = gst_audio_buffer_clip (buf, &a52dec->segment,
              a52dec->sample_rate, (SAMPLE_WIDTH / 8) * chans))) {
    /* set discont when needed */
    if (a52dec->discont) {
      GST_LOG_OBJECT (a52dec, "marking DISCONT");
      GST_BUFFER_FLAG_SET (buf, GST_BUFFER_FLAG_DISCONT);
      a52dec->discont = FALSE;
    }

    if (a52dec->segment.rate > 0.0) {
      GST_DEBUG_OBJECT (a52dec,
          "Pushing buffer with ts %" GST_TIME_FORMAT " duration %"
          GST_TIME_FORMAT, GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)),
          GST_TIME_ARGS (GST_BUFFER_DURATION (buf)));

      result = gst_pad_push (srcpad, buf);
    } else {
      /* reverse playback, queue frame till later when we get a discont. */
      GST_DEBUG_OBJECT (a52dec, "queued frame");
      a52dec->queued = g_list_prepend (a52dec->queued, buf);
    }
  }
  return result;
}

static gboolean
gst_a52dec_reneg (GstA52Dec * a52dec, GstPad * pad)
{
  GstAudioChannelPosition *pos;
  gint channels = gst_a52dec_channels (a52dec->using_channels, &pos);
  GstCaps *caps = NULL;
  gboolean result = FALSE;

  if (!channels)
    goto done;

  GST_INFO_OBJECT (a52dec, "reneg channels:%d rate:%d",
      channels, a52dec->sample_rate);

  caps = gst_caps_new_simple ("audio/x-raw-float",
      "endianness", G_TYPE_INT, G_BYTE_ORDER,
      "width", G_TYPE_INT, SAMPLE_WIDTH,
      "channels", G_TYPE_INT, channels,
      "rate", G_TYPE_INT, a52dec->sample_rate, NULL);
  gst_audio_set_channel_positions (gst_caps_get_structure (caps, 0), pos);
  g_free (pos);

  if (!gst_pad_set_caps (pad, caps))
    goto done;

  result = TRUE;

done:
  if (caps)
    gst_caps_unref (caps);
  return result;
}

static gboolean
gst_a52dec_sink_event (GstPad * pad, GstEvent * event)
{
  GstA52Dec *a52dec = GST_A52DEC (gst_pad_get_parent (pad));
  gboolean ret = FALSE;

  GST_LOG ("Handling %s event", GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_NEWSEGMENT:
    {
      GstFormat fmt;
      gboolean update;
      gint64 start, end, pos;
      gdouble rate, arate;

      gst_event_parse_new_segment_full (event, &update, &rate, &arate, &fmt,
          &start, &end, &pos);

      /* drain queued buffers before activating the segment so that we can clip
       * against the old segment first */
      gst_a52dec_drain (a52dec);

      if (fmt != GST_FORMAT_TIME || !GST_CLOCK_TIME_IS_VALID (start)) {
        GST_WARNING ("No time in newsegment event %p (format is %s)",
            event, gst_format_get_name (fmt));
        gst_event_unref (event);
        a52dec->sent_segment = FALSE;
        /* set some dummy values, FIXME: do proper conversion */
        a52dec->time = start = pos = 0;
        fmt = GST_FORMAT_TIME;
        end = -1;
      } else {
        a52dec->time = start;
        a52dec->sent_segment = TRUE;
        GST_DEBUG_OBJECT (a52dec,
            "Pushing newseg rate %g, applied rate %g, format %d, start %"
            G_GINT64_FORMAT ", stop %" G_GINT64_FORMAT ", pos %"
            G_GINT64_FORMAT, rate, arate, fmt, start, end, pos);

        ret = gst_pad_push_event (a52dec->srcpad, event);
      }

      gst_segment_set_newsegment (&a52dec->segment, update, rate, fmt, start,
          end, pos);
      break;
    }
    case GST_EVENT_TAG:
      ret = gst_pad_push_event (a52dec->srcpad, event);
      break;
    case GST_EVENT_EOS:
      gst_a52dec_drain (a52dec);
      ret = gst_pad_push_event (a52dec->srcpad, event);
      break;
    case GST_EVENT_FLUSH_START:
      ret = gst_pad_push_event (a52dec->srcpad, event);
      break;
    case GST_EVENT_FLUSH_STOP:
      if (a52dec->cache) {
        gst_buffer_unref (a52dec->cache);
        a52dec->cache = NULL;
      }
      clear_queued (a52dec);
      gst_segment_init (&a52dec->segment, GST_FORMAT_UNDEFINED);
      ret = gst_pad_push_event (a52dec->srcpad, event);
      break;
    default:
      ret = gst_pad_push_event (a52dec->srcpad, event);
      break;
  }

  gst_object_unref (a52dec);
  return ret;
}

static void
gst_a52dec_update_streaminfo (GstA52Dec * a52dec)
{
  GstTagList *taglist;

  taglist = gst_tag_list_new ();

  gst_tag_list_add (taglist, GST_TAG_MERGE_APPEND,
      GST_TAG_AUDIO_CODEC, "Dolby Digital (AC-3)",
      GST_TAG_BITRATE, (guint) a52dec->bit_rate, NULL);

  gst_element_found_tags_for_pad (GST_ELEMENT (a52dec),
      GST_PAD (a52dec->srcpad), taglist);
}

static GstFlowReturn
gst_a52dec_handle_frame (GstA52Dec * a52dec, guint8 * data,
    guint length, gint flags, gint sample_rate, gint bit_rate)
{
  gint channels, i;
  gboolean need_reneg = FALSE;

  /* update stream information, renegotiate or re-streaminfo if needed */
  need_reneg = FALSE;
  if (a52dec->sample_rate != sample_rate) {
    need_reneg = TRUE;
    a52dec->sample_rate = sample_rate;
  }

  if (flags) {
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

    caps = gst_pad_get_allowed_caps (a52dec->srcpad);
    if (caps && gst_caps_get_size (caps) > 0) {
      GstCaps *copy = gst_caps_copy_nth (caps, 0);
      GstStructure *structure = gst_caps_get_structure (copy, 0);
      gint channels;
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
          flags ? gst_a52dec_channels (flags, NULL) : 6);
      if (gst_structure_get_int (structure, "channels", &channels)
          && channels <= 6)
        flags = a52_channels[channels - 1];
      else
        flags = a52_channels[5];

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
  if (a52_frame (a52dec->state, data, &flags, &a52dec->level, a52dec->bias)) {
    GST_WARNING ("a52_frame error");
    a52dec->discont = TRUE;
    return GST_FLOW_OK;
  }
  channels = flags & (A52_CHANNEL_MASK | A52_LFE);
  if (a52dec->using_channels != channels) {
    need_reneg = TRUE;
    a52dec->using_channels = channels;
  }

  /* negotiate if required */
  if (need_reneg) {
    GST_DEBUG ("a52dec reneg: sample_rate:%d stream_chans:%d using_chans:%d",
        a52dec->sample_rate, a52dec->stream_channels, a52dec->using_channels);
    if (!gst_a52dec_reneg (a52dec, a52dec->srcpad)) {
      GST_ELEMENT_ERROR (a52dec, CORE, NEGOTIATION, (NULL), (NULL));
      return GST_FLOW_ERROR;
    }
  }

  if (a52dec->dynamic_range_compression == FALSE) {
    a52_dynrng (a52dec->state, NULL, NULL);
  }

  /* each frame consists of 6 blocks */
  for (i = 0; i < 6; i++) {
    if (a52_block (a52dec->state)) {
      /* ignore errors but mark a discont */
      GST_WARNING ("a52_block error %d", i);
      a52dec->discont = TRUE;
    } else {
      GstFlowReturn ret;

      /* push on */
      ret = gst_a52dec_push (a52dec, a52dec->srcpad, a52dec->using_channels,
          a52dec->samples, a52dec->time);
      if (ret != GST_FLOW_OK)
        return ret;
    }
    a52dec->time += 256 * GST_SECOND / a52dec->sample_rate;
  }

  return GST_FLOW_OK;
}

static gboolean
gst_a52dec_sink_setcaps (GstPad * pad, GstCaps * caps)
{
  GstA52Dec *a52dec = GST_A52DEC (gst_pad_get_parent (pad));
  GstStructure *structure;

  structure = gst_caps_get_structure (caps, 0);

  if (structure && gst_structure_has_name (structure, "audio/x-private1-ac3"))
    a52dec->dvdmode = TRUE;
  else
    a52dec->dvdmode = FALSE;

  gst_object_unref (a52dec);

  return TRUE;
}

static GstFlowReturn
gst_a52dec_chain (GstPad * pad, GstBuffer * buf)
{
  GstA52Dec *a52dec = GST_A52DEC (GST_PAD_PARENT (pad));
  GstFlowReturn ret;
  gint first_access;

  if (GST_BUFFER_IS_DISCONT (buf)) {
    GST_LOG_OBJECT (a52dec, "received DISCONT");
    gst_a52dec_drain (a52dec);
    /* clear cache on discont and mark a discont in the element */
    if (a52dec->cache) {
      gst_buffer_unref (a52dec->cache);
      a52dec->cache = NULL;
    }
    a52dec->discont = TRUE;
  }

  if (a52dec->dvdmode) {
    gint size = GST_BUFFER_SIZE (buf);
    guchar *data = GST_BUFFER_DATA (buf);
    gint offset;
    gint len;
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
      GST_BUFFER_TIMESTAMP (subbuf) = GST_CLOCK_TIME_NONE;
      ret = gst_a52dec_chain_raw (pad, subbuf);
      if (ret != GST_FLOW_OK)
        goto done;

      offset += len;
      len = size - offset;

      if (len > 0) {
        subbuf = gst_buffer_create_sub (buf, offset, len);
        GST_BUFFER_TIMESTAMP (subbuf) = GST_BUFFER_TIMESTAMP (buf);

        ret = gst_a52dec_chain_raw (pad, subbuf);
      }
    } else {
      /* first_access = 0 or 1, so if there's a timestamp it applies to the first byte */
      subbuf = gst_buffer_create_sub (buf, offset, size - offset);
      GST_BUFFER_TIMESTAMP (subbuf) = GST_BUFFER_TIMESTAMP (buf);
      ret = gst_a52dec_chain_raw (pad, subbuf);
    }
  } else {
    gst_buffer_ref (buf);
    ret = gst_a52dec_chain_raw (pad, buf);
  }

done:
  gst_buffer_unref (buf);
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

static GstFlowReturn
gst_a52dec_chain_raw (GstPad * pad, GstBuffer * buf)
{
  GstA52Dec *a52dec;
  guint8 *data;
  guint size;
  gint length = 0, flags, sample_rate, bit_rate;
  GstFlowReturn result = GST_FLOW_OK;

  a52dec = GST_A52DEC (GST_PAD_PARENT (pad));

  if (!a52dec->sent_segment) {
    GstSegment segment;

    /* Create a basic segment. Usually, we'll get a new-segment sent by
     * another element that will know more information (a demuxer). If we're
     * just looking at a raw AC3 stream, we won't - so we need to send one
     * here, but we don't know much info, so just send a minimal TIME
     * new-segment event
     */
    gst_segment_init (&segment, GST_FORMAT_TIME);
    gst_pad_push_event (a52dec->srcpad, gst_event_new_new_segment (FALSE,
            segment.rate, segment.format, segment.start,
            segment.duration, segment.start));
    a52dec->sent_segment = TRUE;
  }

  /* merge with cache, if any. Also make sure timestamps match */
  if (GST_BUFFER_TIMESTAMP_IS_VALID (buf)) {
    a52dec->time = GST_BUFFER_TIMESTAMP (buf);
    GST_DEBUG_OBJECT (a52dec,
        "Received buffer with ts %" GST_TIME_FORMAT " duration %"
        GST_TIME_FORMAT, GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)),
        GST_TIME_ARGS (GST_BUFFER_DURATION (buf)));
  }

  if (a52dec->cache) {
    buf = gst_buffer_join (a52dec->cache, buf);
    a52dec->cache = NULL;
  }
  data = GST_BUFFER_DATA (buf);
  size = GST_BUFFER_SIZE (buf);

  /* find and read header */
  bit_rate = a52dec->bit_rate;
  sample_rate = a52dec->sample_rate;
  flags = 0;
  while (size >= 7) {
    length = a52_syncinfo (data, &flags, &sample_rate, &bit_rate);

    if (length == 0) {
      /* no sync */
      data++;
      size--;
    } else if (length <= size) {
      GST_DEBUG ("Sync: %d", length);

      if (flags != a52dec->prev_flags)
        a52dec->flag_update = TRUE;
      a52dec->prev_flags = flags;

      result = gst_a52dec_handle_frame (a52dec, data,
          length, flags, sample_rate, bit_rate);
      if (result != GST_FLOW_OK) {
        size = 0;
        break;
      }
      size -= length;
      data += length;
    } else {
      /* not enough data */
      GST_LOG ("Not enough data available");
      break;
    }
  }

  /* keep cache */
  if (length == 0) {
    GST_LOG ("No sync found");
  }

  if (size > 0) {
    a52dec->cache = gst_buffer_create_sub (buf,
        GST_BUFFER_SIZE (buf) - size, size);
  }

  gst_buffer_unref (buf);

  return result;
}

static GstStateChangeReturn
gst_a52dec_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
  GstA52Dec *a52dec = GST_A52DEC (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:{
      GstA52DecClass *klass;

      klass = GST_A52DEC_CLASS (G_OBJECT_GET_CLASS (a52dec));
      a52dec->state = a52_init (klass->a52_cpuflags);

      if (!a52dec->state) {
        GST_ELEMENT_ERROR (GST_ELEMENT (a52dec), STREAM, DECODE, (NULL),
            ("Failed to initialize a52 state"));
        ret = GST_STATE_CHANGE_FAILURE;
      }
      break;
    }
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      a52dec->samples = a52_samples (a52dec->state);
      a52dec->bit_rate = -1;
      a52dec->sample_rate = -1;
      a52dec->stream_channels = A52_CHANNEL;
      a52dec->using_channels = A52_CHANNEL;
      a52dec->level = 1;
      a52dec->bias = 0;
      a52dec->time = 0;
      a52dec->sent_segment = FALSE;
      a52dec->flag_update = TRUE;
      gst_segment_init (&a52dec->segment, GST_FORMAT_UNDEFINED);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      a52dec->samples = NULL;
      if (a52dec->cache) {
        gst_buffer_unref (a52dec->cache);
        a52dec->cache = NULL;
      }
      clear_queued (a52dec);
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      if (a52dec->state) {
        a52_free (a52dec->state);
        a52dec->state = NULL;
      }
      break;
    default:
      break;
  }

  return ret;
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

  /* ensure GstAudioChannelPosition type is registered */
  if (!gst_audio_channel_position_get_type ())
    return FALSE;

  if (!gst_element_register (plugin, "a52dec", GST_RANK_SECONDARY,
          GST_TYPE_A52DEC))
    return FALSE;

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "a52dec",
    "Decodes ATSC A/52 encoded audio streams",
    plugin_init, VERSION, "GPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN);
