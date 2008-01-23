/* GStreamer DTS decoder plugin based on libdtsdec
 * Copyright (C) 2004 Ronald Bultje <rbultje@ronald.bitfreak.net>
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

/* TODO: - Port to libdca API instead of relying on the compat header.
 *         libdca is the successor of libdts:
 *         http://www.videolan.org/developers/libdca.html
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include "_stdint.h"
#include <stdlib.h>

#include <gst/gst.h>
#include <gst/audio/multichannel.h>

#include <dts.h>

#include "gstdtsdec.h"

#include <liboil/liboil.h>
#include <liboil/liboilcpu.h>
#include <liboil/liboilfunction.h>

GST_DEBUG_CATEGORY_STATIC (dtsdec_debug);
#define GST_CAT_DEFAULT (dtsdec_debug)

static const GstElementDetails gst_dtsdec_details =
GST_ELEMENT_DETAILS ("DTS audio decoder",
    "Codec/Decoder/Audio",
    "Decodes DTS audio streams",
    "Ronald Bultje <rbultje@ronald.bitfreak.net>");

enum
{
  ARG_0,
  ARG_DRC
};
static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-dts;" "audio/x-private1-dts")
    );

#if defined(LIBDTS_FIXED) || defined(LIBDCA_FIXED)
#define DTS_CAPS "audio/x-raw-int, " \
    "endianness = (int) " G_STRINGIFY (G_BYTE_ORDER) ", " \
    "signed = (boolean) true, " \
    "width = (int) 16, " \
    "depth = (int) 16"
#define SAMPLE_WIDTH 16
#elif defined(LIBDTS_DOUBLE) || defined(LIBDCA_DOUBLE)
#define DTS_CAPS "audio/x-raw-float, " \
    "endianness = (int) " G_STRINGIFY (G_BYTE_ORDER) ", " \
    "width = (int) 64"
#define SAMPLE_WIDTH 64
#else
#define DTS_CAPS "audio/x-raw-float, " \
    "endianness = (int) " G_STRINGIFY (G_BYTE_ORDER) ", " \
    "width = (int) 32"
#define SAMPLE_WIDTH 32
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
  gst_element_class_set_details (element_class, &gst_dtsdec_details);

  GST_DEBUG_CATEGORY_INIT (dtsdec_debug, "dtsdec", 0, "DTS audio decoder");
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

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_DRC,
      g_param_spec_boolean ("drc", "Dynamic Range Compression",
          "Use Dynamic Range Compression", FALSE, G_PARAM_READWRITE));

  oil_init ();

  klass->dts_cpuflags = 0;
  cpuflags = oil_cpu_get_flags ();
  if (cpuflags & OIL_IMPL_FLAG_MMX)
    klass->dts_cpuflags |= MM_ACCEL_X86_MMX;
  if (cpuflags & OIL_IMPL_FLAG_3DNOW)
    klass->dts_cpuflags |= MM_ACCEL_X86_3DNOW;
  if (cpuflags & OIL_IMPL_FLAG_MMXEXT)
    klass->dts_cpuflags |= MM_ACCEL_X86_MMXEXT;

  GST_LOG ("CPU flags: dts=%08x, liboil=%08x", klass->dts_cpuflags, cpuflags);
}

static void
gst_dtsdec_init (GstDtsDec * dtsdec, GstDtsDecClass * g_class)
{
  dtsdec->sinkpad = gst_pad_new_from_static_template (&sink_factory, "sink");
  gst_pad_set_setcaps_function (dtsdec->sinkpad,
      GST_DEBUG_FUNCPTR (gst_dtsdec_sink_setcaps));
  gst_pad_set_chain_function (dtsdec->sinkpad,
      GST_DEBUG_FUNCPTR (gst_dtsdec_chain));
  gst_pad_set_event_function (dtsdec->sinkpad,
      GST_DEBUG_FUNCPTR (gst_dtsdec_sink_event));
  gst_element_add_pad (GST_ELEMENT (dtsdec), dtsdec->sinkpad);

  dtsdec->srcpad = gst_pad_new_from_static_template (&src_factory, "src");
  gst_pad_use_fixed_caps (dtsdec->srcpad);
  gst_element_add_pad (GST_ELEMENT (dtsdec), dtsdec->srcpad);

  dtsdec->dynamic_range_compression = FALSE;
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

  switch (flags & DTS_CHANNEL_MASK) {
    case DTS_MONO:
      chans = 1;
      if (tpos)
        tpos[0] = GST_AUDIO_CHANNEL_POSITION_FRONT_MONO;
      break;
      /* case DTS_CHANNEL: */
    case DTS_STEREO:
    case DTS_STEREO_SUMDIFF:
    case DTS_STEREO_TOTAL:
    case DTS_DOLBY:
      chans = 2;
      if (tpos) {
        tpos[0] = GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT;
        tpos[1] = GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT;
      }
      break;
    case DTS_3F:
      chans = 3;
      if (tpos) {
        tpos[0] = GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER;
        tpos[1] = GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT;
        tpos[2] = GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT;
      }
      break;
    case DTS_2F1R:
      chans = 3;
      if (tpos) {
        tpos[0] = GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT;
        tpos[1] = GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT;
        tpos[2] = GST_AUDIO_CHANNEL_POSITION_REAR_CENTER;
      }
      break;
    case DTS_3F1R:
      chans = 4;
      if (tpos) {
        tpos[0] = GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER;
        tpos[1] = GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT;
        tpos[2] = GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT;
        tpos[3] = GST_AUDIO_CHANNEL_POSITION_REAR_CENTER;
      }
      break;
    case DTS_2F2R:
      chans = 4;
      if (tpos) {
        tpos[0] = GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT;
        tpos[1] = GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT;
        tpos[2] = GST_AUDIO_CHANNEL_POSITION_REAR_LEFT;
        tpos[3] = GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT;
      }
      break;
    case DTS_3F2R:
      chans = 5;
      if (tpos) {
        tpos[0] = GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER;
        tpos[1] = GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT;
        tpos[2] = GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT;
        tpos[3] = GST_AUDIO_CHANNEL_POSITION_REAR_LEFT;
        tpos[4] = GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT;
      }
      break;
    case DTS_4F2R:
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
  if (flags & DTS_LFE) {
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
      gint64 val;

      gst_event_parse_new_segment (event, NULL, NULL, &format, &val, NULL,
          NULL);
      if (format != GST_FORMAT_TIME || !GST_CLOCK_TIME_IS_VALID (val)) {
        GST_WARNING ("No time in newsegment event %p", event);
      } else {
        dtsdec->current_ts = val;
      }

      if (dtsdec->cache) {
        gst_buffer_unref (dtsdec->cache);
        dtsdec->cache = NULL;
      }
      ret = gst_pad_event_default (pad, event);
      break;
    }
    case GST_EVENT_FLUSH_STOP:
      if (dtsdec->cache) {
        gst_buffer_unref (dtsdec->cache);
        dtsdec->cache = NULL;
      }
      ret = gst_pad_event_default (pad, event);
      break;
    default:
      ret = gst_pad_event_default (pad, event);
      break;
  }

  gst_object_unref (dtsdec);
  return ret;
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

static void
gst_dtsdec_update_streaminfo (GstDtsDec * dts)
{
  GstTagList *taglist;

  taglist = gst_tag_list_new ();

  gst_tag_list_add (taglist, GST_TAG_MERGE_APPEND,
      GST_TAG_BITRATE, (guint) dts->bit_rate, NULL);

  gst_element_found_tags_for_pad (GST_ELEMENT (dts), dts->srcpad, taglist);
}

static GstFlowReturn
gst_dtsdec_handle_frame (GstDtsDec * dts, guint8 * data,
    guint length, gint flags, gint sample_rate, gint bit_rate)
{
  gboolean need_renegotiation = FALSE;
  gint channels, num_blocks;
  GstBuffer *out;
  gint i, s, c, num_c;
  sample_t *samples;
  GstFlowReturn result = GST_FLOW_OK;

  /* go over stream properties, update caps/streaminfo if needed */
  if (dts->sample_rate != sample_rate) {
    need_renegotiation = TRUE;
    dts->sample_rate = sample_rate;
  }

  dts->stream_channels = flags;

  if (bit_rate != dts->bit_rate) {
    dts->bit_rate = bit_rate;
    gst_dtsdec_update_streaminfo (dts);
  }

  if (dts->request_channels == DTS_CHANNEL) {
    GstCaps *caps;

    caps = gst_pad_get_allowed_caps (dts->srcpad);
    if (caps && gst_caps_get_size (caps) > 0) {
      GstCaps *copy = gst_caps_copy_nth (caps, 0);
      GstStructure *structure = gst_caps_get_structure (copy, 0);
      gint channels;
      const int dts_channels[6] = {
        DTS_MONO,
        DTS_STEREO,
        DTS_STEREO | DTS_LFE,
        DTS_2F2R,
        DTS_2F2R | DTS_LFE,
        DTS_3F2R | DTS_LFE,
      };

      /* Prefer the original number of channels, but fixate to something 
       * preferred (first in the caps) downstream if possible.
       */
      gst_structure_fixate_field_nearest_int (structure, "channels",
          flags ? gst_dtsdec_channels (flags, NULL) : 6);
      gst_structure_get_int (structure, "channels", &channels);
      if (channels <= 6)
        dts->request_channels = dts_channels[channels - 1];
      else
        dts->request_channels = dts_channels[5];

      gst_caps_unref (copy);
    } else if (flags) {
      dts->request_channels = dts->stream_channels;
    } else {
      dts->request_channels = DTS_3F2R | DTS_LFE;
    }

    if (caps)
      gst_caps_unref (caps);
  }

  /* process */
  flags = dts->request_channels | DTS_ADJUST_LEVEL;
  dts->level = 1;

  if (dts_frame (dts->state, data, &flags, &dts->level, dts->bias)) {
    GST_WARNING ("dts_frame error");
    return GST_FLOW_OK;
  }

  channels = flags & (DTS_CHANNEL_MASK | DTS_LFE);

  if (dts->using_channels != channels) {
    need_renegotiation = TRUE;
    dts->using_channels = channels;
  }

  if (need_renegotiation == TRUE) {
    GST_DEBUG ("dtsdec: sample_rate:%d stream_chans:0x%x using_chans:0x%x",
        dts->sample_rate, dts->stream_channels, dts->using_channels);
    if (!gst_dtsdec_renegotiate (dts)) {
      GST_ELEMENT_ERROR (dts, CORE, NEGOTIATION, (NULL), (NULL));
      return GST_FLOW_ERROR;
    }
  }

  if (dts->dynamic_range_compression == FALSE) {
    dts_dynrng (dts->state, NULL, NULL);
  }

  /* handle decoded data, one block is 256 samples */
  num_blocks = dts_blocks_num (dts->state);
  for (i = 0; i < num_blocks; i++) {
    if (dts_block (dts->state)) {
      GST_WARNING ("dts_block error %d", i);
      continue;
    }

    samples = dts_samples (dts->state);
    num_c = gst_dtsdec_channels (dts->using_channels, NULL);

    result = gst_pad_alloc_buffer_and_set_caps (dts->srcpad, 0,
        (SAMPLE_WIDTH / 8) * 256 * num_c, GST_PAD_CAPS (dts->srcpad), &out);

    if (result != GST_FLOW_OK)
      break;

    GST_BUFFER_TIMESTAMP (out) = dts->current_ts;
    GST_BUFFER_DURATION (out) = GST_SECOND * 256 / dts->sample_rate;
    dts->current_ts += GST_BUFFER_DURATION (out);

    /* libdts returns buffers in 256-sample-blocks per channel,
     * we want interleaved. And we need to copy anyway... */
    data = GST_BUFFER_DATA (out);
    for (s = 0; s < 256; s++) {
      for (c = 0; c < num_c; c++) {
        *(sample_t *) data = samples[s + c * 256];
        data += (SAMPLE_WIDTH / 8);
      }
    }

    /* push on */
    result = gst_pad_push (dts->srcpad, out);

    if (result != GST_FLOW_OK)
      break;
  }

  return result;
}

static GstFlowReturn
gst_dtsdec_chain_raw (GstPad * pad, GstBuffer * buf)
{
  GstDtsDec *dts;
  guint8 *data;
  gint size;
  gint length, flags, sample_rate, bit_rate, frame_length;
  GstFlowReturn result = GST_FLOW_OK;

  dts = GST_DTSDEC (GST_PAD_PARENT (pad));

  if (dts->cache) {
    buf = gst_buffer_join (dts->cache, buf);
    dts->cache = NULL;
  }

  data = GST_BUFFER_DATA (buf);
  size = GST_BUFFER_SIZE (buf);
  length = 0;
  while (size >= 7) {
    length = dts_syncinfo (dts->state, data, &flags,
        &sample_rate, &bit_rate, &frame_length);
    if (length == 0) {
      /* shift window to re-find sync */
      data++;
      size--;
    } else if (length <= size) {
      GST_DEBUG ("Sync: frame size %d", length);
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


static GstFlowReturn
gst_dtsdec_chain (GstPad * pad, GstBuffer * buf)
{
  GstFlowReturn res = GST_FLOW_OK;
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
      GST_BUFFER_TIMESTAMP (subbuf) = GST_CLOCK_TIME_NONE;
      res = gst_dtsdec_chain_raw (pad, subbuf);
      if (res != GST_FLOW_OK)
        goto done;

      offset += len;
      len = size - offset;

      if (len > 0) {
        subbuf = gst_buffer_create_sub (buf, offset, len);
        GST_BUFFER_TIMESTAMP (subbuf) = GST_BUFFER_TIMESTAMP (buf);

        res = gst_dtsdec_chain_raw (pad, subbuf);
      }
    } else {
      /* first_access = 0 or 1, so if there's a timestamp it applies
       * to the first byte */
      subbuf = gst_buffer_create_sub (buf, offset, size - offset);
      GST_BUFFER_TIMESTAMP (subbuf) = GST_BUFFER_TIMESTAMP (buf);
      res = gst_dtsdec_chain_raw (pad, subbuf);
    }
  } else {
    res = gst_dtsdec_chain_raw (pad, buf);
  }

done:
  return res;

/* ERRORS */
not_enough_data:
  {
    GST_ELEMENT_ERROR (GST_ELEMENT (dts), STREAM, DECODE, (NULL),
        ("Insufficient data in buffer. Can't determine first_acess"));
    return GST_FLOW_ERROR;
  }
bad_first_access_parameter:
  {
    GST_ELEMENT_ERROR (GST_ELEMENT (dts), STREAM, DECODE, (NULL),
        ("Bad first_access parameter (%d) in buffer", first_access));
    return GST_FLOW_ERROR;
  }

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
      dts->state = dts_init (klass->dts_cpuflags);
      break;
    }
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      dts->samples = dts_samples (dts->state);
      dts->bit_rate = -1;
      dts->sample_rate = -1;
      dts->stream_channels = 0;
      /* FIXME force stereo for now */
      dts->request_channels = DTS_CHANNEL;
      dts->using_channels = 0;
      dts->level = 1;
      dts->bias = 0;
      dts->current_ts = 0;
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
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      dts_free (dts->state);
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
