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

GST_DEBUG_CATEGORY_STATIC (dtsdec_debug);
#define GST_CAT_DEFAULT (dtsdec_debug)

enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_DRC
      /* FILL ME */
};

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-dts")
    );

#if defined(LIBDTS_FIXED)
#define DTS_CAPS "audio/x-raw-int, " \
    "endianness = (int) BYTE_ORDER, " \
    "signed = (boolean) true, " \
    "width = (int) 16, " \
    "depth = (int) 16"
#define SAMPLE_WIDTH 16
#elif defined(LIBDTS_DOUBLE)
#define DTS_CAPS "audio/x-raw-float, " \
    "endianness = (int) BYTE_ORDER, " \
    "width = (int) 64"
#define SAMPLE_WIDTH 64
#else
#define DTS_CAPS "audio/x-raw-float, " \
    "endianness = (int) BYTE_ORDER, " \
    "width = (int) 32"
#define SAMPLE_WIDTH 32
#endif

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (DTS_CAPS ", "
        "rate = (int) [ 4000, 96000 ], " "channels = (int) [ 1, 6 ]")
    );

static void gst_dtsdec_base_init (GstDtsDecClass * klass);
static void gst_dtsdec_class_init (GstDtsDecClass * klass);
static void gst_dtsdec_init (GstDtsDec * dtsdec);

static void gst_dtsdec_chain (GstPad * pad, GstData * data);
static GstStateChangeReturn gst_dtsdec_change_state (GstElement * element,
    GstStateChange transition);

static void gst_dtsdec_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_dtsdec_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstElementClass *parent_class = NULL;

/* static guint gst_dtsdec_signals[LAST_SIGNAL] = { 0 }; */

GType
gst_dtsdec_get_type (void)
{
  static GType dtsdec_type = 0;

  if (!dtsdec_type) {
    static const GTypeInfo dtsdec_info = {
      sizeof (GstDtsDecClass),
      (GBaseInitFunc) gst_dtsdec_base_init,
      NULL, (GClassInitFunc) gst_dtsdec_class_init,
      NULL,
      NULL,
      sizeof (GstDtsDec),
      0,
      (GInstanceInitFunc) gst_dtsdec_init,
    };

    dtsdec_type =
        g_type_register_static (GST_TYPE_ELEMENT, "GstDtsDec", &dtsdec_info, 0);

    GST_DEBUG_CATEGORY_INIT (dtsdec_debug, "dtsdec", 0, "DTS audio decoder");
  }
  return dtsdec_type;
}

static void
gst_dtsdec_base_init (GstDtsDecClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  static GstElementDetails gst_dtsdec_details = {
    "DTS audio decoder",
    "Codec/Decoder/Audio",
    "Decodes DTS audio streams",
    "Ronald Bultje <rbultje@ronald.bitfreak.net>"
  };

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_factory));
  gst_element_class_set_details (element_class, &gst_dtsdec_details);
}

static void
gst_dtsdec_class_init (GstDtsDecClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_DRC,
      g_param_spec_boolean ("drc", "Dynamic Range Compression",
          "Use Dynamic Range Compression", FALSE, G_PARAM_READWRITE));

  gobject_class->set_property = gst_dtsdec_set_property;
  gobject_class->get_property = gst_dtsdec_get_property;

  gstelement_class->change_state = gst_dtsdec_change_state;
}

static void
gst_dtsdec_init (GstDtsDec * dtsdec)
{
  GstElement *element = GST_ELEMENT (dtsdec);

  /* create the sink and src pads */
  dtsdec->sinkpad =
      gst_pad_new_from_template (gst_element_get_pad_template (GST_ELEMENT
          (dtsdec), "sink"), "sink");
  gst_pad_set_chain_function (dtsdec->sinkpad, gst_dtsdec_chain);
  gst_element_add_pad (element, dtsdec->sinkpad);

  dtsdec->srcpad =
      gst_pad_new_from_template (gst_element_get_pad_template (element,
          "src"), "src");
  gst_pad_use_explicit_caps (dtsdec->srcpad);
  gst_element_add_pad (element, dtsdec->srcpad);

  GST_FLAG_SET (element, GST_ELEMENT_EVENT_AWARE);
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
      /* error */
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

  if (!channels)
    return FALSE;

  GST_INFO ("dtsdec renegotiate, channels=%d, rate=%d",
      channels, dts->sample_rate);

  gst_caps_set_simple (caps,
      "channels", G_TYPE_INT, channels,
      "rate", G_TYPE_INT, (gint) dts->sample_rate, NULL);
  gst_audio_set_channel_positions (gst_caps_get_structure (caps, 0), pos);
  g_free (pos);

  return gst_pad_set_explicit_caps (dts->srcpad, caps);
}

static void
gst_dtsdec_handle_event (GstDtsDec * dts, GstEvent * event)
{
  if (!event) {
    GST_ELEMENT_ERROR (dts, RESOURCE, READ, (NULL), (NULL));
    return;
  }

  GST_LOG ("Handling event of type %d timestamp %llu", GST_EVENT_TYPE (event),
      GST_EVENT_TIMESTAMP (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_DISCONTINUOUS:
    {
      gint64 val;

      if (!gst_event_discont_get_value (event, GST_FORMAT_TIME, &val) ||
          !GST_CLOCK_TIME_IS_VALID (val)) {
        GST_WARNING ("No time discont value in event %p", event);
      } else {
        dts->current_ts = val;
      }
    }
      /* Fallthrough */
    case GST_EVENT_FLUSH:
      if (dts->cache) {
        gst_buffer_unref (dts->cache);
        dts->cache = NULL;
      }
      break;
    default:
      break;
  }

  gst_pad_event_default (dts->sinkpad, event);
}

static void
gst_dtsdec_update_streaminfo (GstDtsDec * dts)
{
  GstTagList *taglist;

  taglist = gst_tag_list_new ();

  gst_tag_list_add (taglist, GST_TAG_MERGE_APPEND,
      GST_TAG_BITRATE, (guint) dts->bit_rate, NULL);

  gst_element_found_tags_for_pad (GST_ELEMENT (dts),
      dts->srcpad, dts->current_ts, taglist);
}

static gboolean
gst_dtsdec_handle_frame (GstDtsDec * dts, guint8 * data,
    guint length, gint flags, gint sample_rate, gint bit_rate)
{
  gboolean need_renegotiation = FALSE;
  GstClockTime timestamp = 0;
  gint channels, num_blocks;
  GstBuffer *out;
  gint i, s, c, num_c;
  sample_t *samples;

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

  /* process */
  flags = dts->request_channels | DTS_ADJUST_LEVEL;
  dts->level = 1;

  if (dts_frame (dts->state, data, &flags, &dts->level, dts->bias)) {
    GST_WARNING ("dts_frame error");
    return FALSE;
  }

  channels = flags & (DTS_CHANNEL_MASK | DTS_LFE);

  if (dts->using_channels != channels) {
    need_renegotiation = TRUE;
    dts->using_channels = channels;
  }

  if (need_renegotiation == TRUE) {
    GST_DEBUG ("dtsdec: sample_rate:%d stream_chans:0x%x using_chans:0x%x",
        dts->sample_rate, dts->stream_channels, dts->using_channels);
    if (!gst_dtsdec_renegotiate (dts))
      return FALSE;
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
    out = gst_buffer_new_and_alloc ((SAMPLE_WIDTH / 8) * 256 * num_c);
    if (!out) {
      GST_ELEMENT_ERROR (dts, RESOURCE, FAILED, (NULL), ("Out of memory"));
      return FALSE;
    }

    GST_BUFFER_TIMESTAMP (out) = timestamp;
    GST_BUFFER_DURATION (out) = GST_SECOND * 256 / dts->sample_rate;

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
    gst_pad_push (dts->srcpad, GST_DATA (out));
    timestamp += GST_SECOND * 256 / dts->sample_rate;
  }

  dts->current_ts = timestamp;
  return TRUE;
}

static void
gst_dtsdec_chain (GstPad * pad, GstData * _data)
{
  GstDtsDec *dts;
  guint8 *data;
  gint64 size;
  GstBuffer *buf;
  gint length, flags, sample_rate, bit_rate, frame_length;

  g_return_if_fail (pad != NULL);
  g_return_if_fail (_data != NULL);

  dts = GST_DTSDEC (gst_pad_get_parent (pad));

  if (GST_IS_EVENT (_data)) {
    gst_dtsdec_handle_event (dts, GST_EVENT (_data));
    return;
  }

  /* merge with cache, if any. Also make sure timestamps match */
  buf = GST_BUFFER (_data);
  if (GST_BUFFER_TIMESTAMP_IS_VALID (buf)) {
    dts->current_ts = GST_BUFFER_TIMESTAMP (buf);
    GST_DEBUG_OBJECT (dts, "Received buffer with ts %" GST_TIME_FORMAT
        " duration %" GST_TIME_FORMAT,
        GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)),
        GST_TIME_ARGS (GST_BUFFER_DURATION (buf)));
  }

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
      if (!gst_dtsdec_handle_frame (dts, data,
              length, flags, sample_rate, bit_rate)) {
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
}

static GstStateChangeReturn
gst_dtsdec_change_state (GstElement * element, GstStateChange transition)
{
  GstDtsDec *dts = GST_DTSDEC (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:{
      GstCPUFlags cpuflags;
      uint32_t mm_accel = 0;

      cpuflags = gst_cpu_get_flags ();
      if (cpuflags & GST_CPU_FLAG_MMX)
        mm_accel |= MM_ACCEL_X86_MMX;
      if (cpuflags & GST_CPU_FLAG_3DNOW)
        mm_accel |= MM_ACCEL_X86_3DNOW;
      if (cpuflags & GST_CPU_FLAG_MMXEXT)
        mm_accel |= MM_ACCEL_X86_MMXEXT;

      dts->state = dts_init (mm_accel);
      break;
    }
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      dts->samples = dts_samples (dts->state);
      dts->bit_rate = -1;
      dts->sample_rate = -1;
      dts->stream_channels = 0;
      /* FIXME force stereo for now */
      dts->request_channels = DTS_STEREO;
      dts->using_channels = 0;
      dts->level = 1;
      dts->bias = 0;
      dts->current_ts = 0;
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      dts->samples = NULL;
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      dts_free (dts->state);
      dts->state = NULL;
      break;
    default:
      break;
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  return GST_STATE_CHANGE_SUCCESS;
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
  if (!gst_library_load ("gstbytestream") || !gst_library_load ("gstaudio"))
    return FALSE;

  if (!gst_element_register (plugin, "dtsdec", GST_RANK_PRIMARY,
          GST_TYPE_DTSDEC))
    return FALSE;

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "dtsdec",
    "Decodes DTS audio streams",
    plugin_init, VERSION, "GPL", GST_PACKAGE, GST_ORIGIN);
