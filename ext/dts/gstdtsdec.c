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

GST_BOILERPLATE (GstDtsDec, gst_dtsdec, GstElement, GST_TYPE_ELEMENT);

static gboolean gst_dtsdec_sink_setcaps (GstPad * pad, GstCaps * caps);
static gboolean gst_dtsdec_sink_event (GstPad * pad, GstEvent * event);
static GstFlowReturn gst_dtsdec_chain (GstPad * pad, GstBuffer * buf);
static GstFlowReturn gst_dtsdec_chain_raw (GstPad * pad, GstBuffer * buf);
static GstStateChangeReturn gst_dtsdec_change_state (GstElement * element,
    GstStateChange transition);

static void gst_dtsdec_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_dtsdec_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);


static void
gst_dtsdec_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_factory));
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
  GstElementClass *gstelement_class;
  guint cpuflags;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->set_property = gst_dtsdec_set_property;
  gobject_class->get_property = gst_dtsdec_get_property;

  gstelement_class->change_state = gst_dtsdec_change_state;

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
  /* create the sink and src pads */
  dtsdec->sinkpad = gst_pad_new_from_static_template (&sink_factory, "sink");
  gst_pad_set_setcaps_function (dtsdec->sinkpad,
      GST_DEBUG_FUNCPTR (gst_dtsdec_sink_setcaps));
  gst_pad_set_chain_function (dtsdec->sinkpad,
      GST_DEBUG_FUNCPTR (gst_dtsdec_chain));
  gst_pad_set_event_function (dtsdec->sinkpad,
      GST_DEBUG_FUNCPTR (gst_dtsdec_sink_event));
  gst_element_add_pad (GST_ELEMENT (dtsdec), dtsdec->sinkpad);

  dtsdec->srcpad = gst_pad_new_from_static_template (&src_factory, "src");
  gst_element_add_pad (GST_ELEMENT (dtsdec), dtsdec->srcpad);

  dtsdec->request_channels = DCA_CHANNEL;
  dtsdec->dynamic_range_compression = FALSE;

  gst_segment_init (&dtsdec->segment, GST_FORMAT_UNDEFINED);
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

static void
clear_queued (GstDtsDec * dec)
{
  g_list_foreach (dec->queued, (GFunc) gst_mini_object_unref, NULL);
  g_list_free (dec->queued);
  dec->queued = NULL;
}

static GstFlowReturn
flush_queued (GstDtsDec * dec)
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
gst_dtsdec_drain (GstDtsDec * dec)
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
gst_dtsdec_push (GstDtsDec * dtsdec,
    GstPad * srcpad, int flags, sample_t * samples, GstClockTime timestamp)
{
  GstBuffer *buf;
  int chans, n, c;
  GstFlowReturn result;

  flags &= (DCA_CHANNEL_MASK | DCA_LFE);
  chans = gst_dtsdec_channels (flags, NULL);
  if (!chans) {
    GST_ELEMENT_ERROR (GST_ELEMENT (dtsdec), STREAM, DECODE, (NULL),
        ("Invalid channel flags: %d", flags));
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
  GST_BUFFER_DURATION (buf) = 256 * GST_SECOND / dtsdec->sample_rate;

  result = GST_FLOW_OK;
  if ((buf = gst_audio_buffer_clip (buf, &dtsdec->segment,
              dtsdec->sample_rate, (SAMPLE_WIDTH / 8) * chans))) {
    /* set discont when needed */
    if (dtsdec->discont) {
      GST_LOG_OBJECT (dtsdec, "marking DISCONT");
      GST_BUFFER_FLAG_SET (buf, GST_BUFFER_FLAG_DISCONT);
      dtsdec->discont = FALSE;
    }

    if (dtsdec->segment.rate > 0.0) {
      GST_DEBUG_OBJECT (dtsdec,
          "Pushing buffer with ts %" GST_TIME_FORMAT " duration %"
          GST_TIME_FORMAT, GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)),
          GST_TIME_ARGS (GST_BUFFER_DURATION (buf)));

      result = gst_pad_push (srcpad, buf);
    } else {
      /* reverse playback, queue frame till later when we get a discont. */
      GST_DEBUG_OBJECT (dtsdec, "queued frame");
      dtsdec->queued = g_list_prepend (dtsdec->queued, buf);
    }
  }
  return result;
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

  if (!gst_pad_set_caps (dts->srcpad, caps))
    goto done;

  result = TRUE;

done:
  if (caps) {
    gst_caps_unref (caps);
  }
  return result;
}

static gboolean
gst_dtsdec_sink_event (GstPad * pad, GstEvent * event)
{
  GstDtsDec *dtsdec = GST_DTSDEC (gst_pad_get_parent (pad));
  gboolean ret = FALSE;

  GST_LOG_OBJECT (dtsdec, "%s event", GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_NEWSEGMENT:{
      GstFormat format;
      gboolean update;
      gint64 start, end, pos;
      gdouble rate;

      gst_event_parse_new_segment (event, &update, &rate, &format, &start, &end,
          &pos);

      /* drain queued buffers before activating the segment so that we can clip
       * against the old segment first */
      gst_dtsdec_drain (dtsdec);

      if (format != GST_FORMAT_TIME || !GST_CLOCK_TIME_IS_VALID (start)) {
        GST_WARNING ("No time in newsegment event %p (format is %s)",
            event, gst_format_get_name (format));
        gst_event_unref (event);
        dtsdec->sent_segment = FALSE;
        /* set some dummy values, FIXME: do proper conversion */
        dtsdec->time = start = pos = 0;
        format = GST_FORMAT_TIME;
        end = -1;
      } else {
        dtsdec->time = start;
        dtsdec->sent_segment = TRUE;
        ret = gst_pad_push_event (dtsdec->srcpad, event);
      }

      gst_segment_set_newsegment (&dtsdec->segment, update, rate, format, start,
          end, pos);
      break;
    }
    case GST_EVENT_TAG:
      ret = gst_pad_push_event (dtsdec->srcpad, event);
      break;
    case GST_EVENT_EOS:
      gst_dtsdec_drain (dtsdec);
      ret = gst_pad_push_event (dtsdec->srcpad, event);
      break;
    case GST_EVENT_FLUSH_START:
      ret = gst_pad_push_event (dtsdec->srcpad, event);
      break;
    case GST_EVENT_FLUSH_STOP:
      if (dtsdec->cache) {
        gst_buffer_unref (dtsdec->cache);
        dtsdec->cache = NULL;
      }
      clear_queued (dtsdec);
      gst_segment_init (&dtsdec->segment, GST_FORMAT_UNDEFINED);
      ret = gst_pad_push_event (dtsdec->srcpad, event);
      break;
    default:
      ret = gst_pad_push_event (dtsdec->srcpad, event);
      break;
  }

  gst_object_unref (dtsdec);
  return ret;
}

static void
gst_dtsdec_update_streaminfo (GstDtsDec * dts)
{
  GstTagList *taglist;

  taglist = gst_tag_list_new ();

  gst_tag_list_add (taglist, GST_TAG_MERGE_APPEND,
      GST_TAG_AUDIO_CODEC, "DTS DCA", NULL);

  if (dts->bit_rate > 3) {
    /* 1 => open bitrate, 2 => variable bitrate, 3 => lossless */
    gst_tag_list_add (taglist, GST_TAG_MERGE_APPEND, GST_TAG_BITRATE,
        (guint) dts->bit_rate, NULL);
  }

  gst_element_found_tags_for_pad (GST_ELEMENT (dts), dts->srcpad, taglist);
}

static GstFlowReturn
gst_dtsdec_handle_frame (GstDtsDec * dts, guint8 * data,
    guint length, gint flags, gint sample_rate, gint bit_rate)
{
  gint channels, i, num_blocks;
  gboolean need_renegotiation = FALSE;

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

    caps = gst_pad_get_allowed_caps (dts->srcpad);
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
    GST_WARNING_OBJECT (dts, "dts_frame error");
    dts->discont = TRUE;
    return GST_FLOW_OK;
  }
  channels = flags & (DCA_CHANNEL_MASK | DCA_LFE);
  if (dts->using_channels != channels) {
    need_renegotiation = TRUE;
    dts->using_channels = channels;
  }

  /* negotiate if required */
  if (need_renegotiation) {
    GST_DEBUG ("dtsdec: sample_rate:%d stream_chans:0x%x using_chans:0x%x",
        dts->sample_rate, dts->stream_channels, dts->using_channels);
    if (!gst_dtsdec_renegotiate (dts)) {
      GST_ELEMENT_ERROR (dts, CORE, NEGOTIATION, (NULL), (NULL));
      return GST_FLOW_ERROR;
    }
  }

  if (dts->dynamic_range_compression == FALSE) {
    dca_dynrng (dts->state, NULL, NULL);
  }

  /* handle decoded data, one block is 256 samples */
  num_blocks = dca_blocks_num (dts->state);
  for (i = 0; i < num_blocks; i++) {
    if (dca_block (dts->state)) {
      /* Ignore errors, but mark a discont */
      GST_WARNING_OBJECT (dts, "dts_block error %d", i);
      dts->discont = TRUE;
    } else {
      GstFlowReturn ret;

      /* push on */
      ret = gst_dtsdec_push (dts, dts->srcpad, dts->using_channels,
          dts->samples, dts->time);
      if (ret != GST_FLOW_OK)
        return ret;
    }
    dts->time += GST_SECOND * 256 / dts->sample_rate;
  }

  return GST_FLOW_OK;
}

static gboolean
gst_dtsdec_sink_setcaps (GstPad * pad, GstCaps * caps)
{
  GstDtsDec *dts = GST_DTSDEC (gst_pad_get_parent (pad));
  GstStructure *structure;

  structure = gst_caps_get_structure (caps, 0);

  if (structure && gst_structure_has_name (structure, "audio/x-private1-dts"))
    dts->dvdmode = TRUE;
  else
    dts->dvdmode = FALSE;

  gst_object_unref (dts);

  return TRUE;
}

static GstFlowReturn
gst_dtsdec_chain (GstPad * pad, GstBuffer * buf)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GstDtsDec *dts = GST_DTSDEC (GST_PAD_PARENT (pad));
  gint first_access;

  if (GST_BUFFER_IS_DISCONT (buf)) {
    GST_LOG_OBJECT (dts, "received DISCONT");
    gst_dtsdec_drain (dts);
    /* clear cache on discont and mark a discont in the element */
    if (dts->cache) {
      gst_buffer_unref (dts->cache);
      dts->cache = NULL;
    }
    dts->discont = TRUE;
  }

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
      GST_BUFFER_TIMESTAMP (subbuf) = GST_CLOCK_TIME_NONE;
      ret = gst_dtsdec_chain_raw (pad, subbuf);
      if (ret != GST_FLOW_OK)
        goto done;

      offset += len;
      len = size - offset;

      if (len > 0) {
        subbuf = gst_buffer_create_sub (buf, offset, len);
        GST_BUFFER_TIMESTAMP (subbuf) = GST_BUFFER_TIMESTAMP (buf);

        ret = gst_dtsdec_chain_raw (pad, subbuf);
      }
    } else {
      /* first_access = 0 or 1, so if there's a timestamp it applies to the first byte */
      subbuf = gst_buffer_create_sub (buf, offset, size - offset);
      GST_BUFFER_TIMESTAMP (subbuf) = GST_BUFFER_TIMESTAMP (buf);
      ret = gst_dtsdec_chain_raw (pad, subbuf);
    }
  } else {
    gst_buffer_ref (buf);
    ret = gst_dtsdec_chain_raw (pad, buf);
  }

done:
  gst_buffer_unref (buf);
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

static GstFlowReturn
gst_dtsdec_chain_raw (GstPad * pad, GstBuffer * buf)
{
  GstDtsDec *dts;
  guint8 *data;
  gint size;
  gint length = 0, flags, sample_rate, bit_rate, frame_length;
  GstFlowReturn result = GST_FLOW_OK;

  dts = GST_DTSDEC (GST_PAD_PARENT (pad));

  if (!dts->sent_segment) {
    GstSegment segment;

    /* Create a basic segment. Usually, we'll get a new-segment sent by 
     * another element that will know more information (a demuxer). If we're
     * just looking at a raw AC3 stream, we won't - so we need to send one
     * here, but we don't know much info, so just send a minimal TIME 
     * new-segment event
     */
    gst_segment_init (&segment, GST_FORMAT_TIME);
    gst_pad_push_event (dts->srcpad, gst_event_new_new_segment (FALSE,
            segment.rate, segment.format, segment.start,
            segment.duration, segment.start));
    dts->sent_segment = TRUE;
  }

  /* merge with cache, if any. Also make sure timestamps match */
  if (GST_BUFFER_TIMESTAMP_IS_VALID (buf)) {
    dts->time = GST_BUFFER_TIMESTAMP (buf);
    GST_DEBUG_OBJECT (dts,
        "Received buffer with ts %" GST_TIME_FORMAT " duration %"
        GST_TIME_FORMAT, GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)),
        GST_TIME_ARGS (GST_BUFFER_DURATION (buf)));
  }

  if (dts->cache) {
    buf = gst_buffer_join (dts->cache, buf);
    dts->cache = NULL;
  }
  data = GST_BUFFER_DATA (buf);
  size = GST_BUFFER_SIZE (buf);

  /* find and read header */
  bit_rate = dts->bit_rate;
  sample_rate = dts->sample_rate;
  flags = 0;
  while (size >= 7) {
    length = dca_syncinfo (dts->state, data, &flags,
        &sample_rate, &bit_rate, &frame_length);

    if (length == 0) {
      /* shift window to re-find sync */
      data++;
      size--;
    } else if (length <= size) {
      GST_DEBUG ("Sync: frame size %d", length);

      if (flags != dts->prev_flags)
        dts->flag_update = TRUE;
      dts->prev_flags = flags;

      result = gst_dtsdec_handle_frame (dts, data, length,
          flags, sample_rate, bit_rate);
      if (result != GST_FLOW_OK) {
        size = 0;
        break;
      }
      size -= length;
      data += length;
    } else {
      GST_LOG ("Not enough data available (needed %d had %d)", length, size);
      break;
    }
  }

  /* keep cache */
  if (length == 0) {
    GST_LOG ("No sync found");
  }

  if (size > 0) {
    dts->cache = gst_buffer_create_sub (buf,
        GST_BUFFER_SIZE (buf) - size, size);
  }

  gst_buffer_unref (buf);

  return result;
}

static GstStateChangeReturn
gst_dtsdec_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
  GstDtsDec *dts = GST_DTSDEC (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:{
      GstDtsDecClass *klass;

      klass = GST_DTSDEC_CLASS (G_OBJECT_GET_CLASS (dts));
      dts->state = dca_init (klass->dts_cpuflags);
      break;
    }
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      dts->samples = dca_samples (dts->state);
      dts->bit_rate = -1;
      dts->sample_rate = -1;
      dts->stream_channels = DCA_CHANNEL;
      dts->using_channels = DCA_CHANNEL;
      dts->level = 1;
      dts->bias = 0;
      dts->time = 0;
      dts->sent_segment = FALSE;
      dts->flag_update = TRUE;
      gst_segment_init (&dts->segment, GST_FORMAT_UNDEFINED);
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
      dts->samples = NULL;
      if (dts->cache) {
        gst_buffer_unref (dts->cache);
        dts->cache = NULL;
      }
      clear_queued (dts);
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      dca_free (dts->state);
      dts->state = NULL;
      break;
    default:
      break;
  }

  return ret;
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
