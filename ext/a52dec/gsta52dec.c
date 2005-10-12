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

/* elementfactory information */
static GstElementDetails gst_a52dec_details = {
  "ATSC A/52 audio decoder",
  "Codec/Decoder/Audio",
  "Decodes ATSC A/52 encoded audio streams",
  "David I. Lehn <dlehn@users.sourceforge.net>",
};

#ifdef LIBA52_DOUBLE
#define SAMPLE_WIDTH 64
#else
#define SAMPLE_WIDTH 32
#endif

GST_DEBUG_CATEGORY_STATIC (a52dec_debug);
#define GST_CAT_DEFAULT (a52dec_debug)

/* A52Dec signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_DRC
};

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-ac3")
    );

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-float, "
        "endianness = (int) BYTE_ORDER, "
        "width = (int) " G_STRINGIFY (SAMPLE_WIDTH) ", "
        "rate = (int) [ 4000, 96000 ], "
        "channels = (int) [ 1, 6 ], " "buffer-frames = (int) 0")
    );

static void gst_a52dec_base_init (gpointer g_class);
static void gst_a52dec_class_init (GstA52DecClass * klass);
static void gst_a52dec_init (GstA52Dec * a52dec);

static void gst_a52dec_chain (GstPad * pad, GstData * data);
static GstStateChangeReturn gst_a52dec_change_state (GstElement * element,
    GstStateChange transition);

static void gst_a52dec_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_a52dec_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstElementClass *parent_class = NULL;

/* static guint gst_a52dec_signals[LAST_SIGNAL] = { 0 }; */

GType
gst_a52dec_get_type (void)
{
  static GType a52dec_type = 0;

  if (!a52dec_type) {
    static const GTypeInfo a52dec_info = {
      sizeof (GstA52DecClass),
      gst_a52dec_base_init,
      NULL, (GClassInitFunc) gst_a52dec_class_init,
      NULL,
      NULL,
      sizeof (GstA52Dec),
      0,
      (GInstanceInitFunc) gst_a52dec_init,
    };

    a52dec_type =
        g_type_register_static (GST_TYPE_ELEMENT, "GstA52Dec", &a52dec_info, 0);

    GST_DEBUG_CATEGORY_INIT (a52dec_debug, "a52dec", 0,
        "AC3/A52 software decoder");
  }
  return a52dec_type;
}

static void
gst_a52dec_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_factory));
  gst_element_class_set_details (element_class, &gst_a52dec_details);
}

static void
gst_a52dec_class_init (GstA52DecClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_DRC,
      g_param_spec_boolean ("drc", "Dynamic Range Compression",
          "Use Dynamic Range Compression", FALSE, G_PARAM_READWRITE));

  gobject_class->set_property = gst_a52dec_set_property;
  gobject_class->get_property = gst_a52dec_get_property;

  gstelement_class->change_state = gst_a52dec_change_state;
}

static void
gst_a52dec_init (GstA52Dec * a52dec)
{
  GST_OBJECT_FLAG_SET (GST_ELEMENT (a52dec), GST_ELEMENT_EVENT_AWARE);

  /* create the sink and src pads */
  a52dec->sinkpad =
      gst_pad_new_from_template (gst_element_get_pad_template (GST_ELEMENT
          (a52dec), "sink"), "sink");
  gst_pad_set_chain_function (a52dec->sinkpad, gst_a52dec_chain);
  gst_element_add_pad (GST_ELEMENT (a52dec), a52dec->sinkpad);

  a52dec->srcpad =
      gst_pad_new_from_template (gst_element_get_pad_template (GST_ELEMENT
          (a52dec), "src"), "src");
  gst_pad_use_explicit_caps (a52dec->srcpad);
  gst_element_add_pad (GST_ELEMENT (a52dec), a52dec->srcpad);

  a52dec->dynamic_range_compression = FALSE;
  a52dec->cache = NULL;
}

static int
gst_a52dec_channels (int flags, GstAudioChannelPosition ** _pos)
{
  int chans = 0;
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
      /*case A52_CHANNEL: */
    case A52_STEREO:
    case A52_DOLBY:
      if (pos) {
        pos[0 + chans] = GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT;
        pos[1 + chans] = GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT;
      }
      chans += 2;
      break;
    default:
      /* error */
      g_warning ("a52dec invalid flags %d", flags);
      g_free (pos);
      return 0;
  }

  return chans;
}

static int
gst_a52dec_push (GstA52Dec * a52dec,
    GstPad * srcpad, int flags, sample_t * samples, GstClockTime timestamp)
{
  GstBuffer *buf;
  int chans, n, c;

  flags &= (A52_CHANNEL_MASK | A52_LFE);
  chans = gst_a52dec_channels (flags, NULL);
  if (!chans) {
    return 1;
  }

  buf = gst_buffer_new ();
  GST_BUFFER_SIZE (buf) = 256 * chans * (SAMPLE_WIDTH / 8);
  GST_BUFFER_DATA (buf) = g_malloc (GST_BUFFER_SIZE (buf));
  for (n = 0; n < 256; n++) {
    for (c = 0; c < chans; c++) {
      ((sample_t *) GST_BUFFER_DATA (buf))[n * chans + c] =
          samples[c * 256 + n];
    }
  }
  GST_BUFFER_TIMESTAMP (buf) = timestamp;
  GST_BUFFER_DURATION (buf) = 256 * GST_SECOND / a52dec->sample_rate;

  GST_DEBUG_OBJECT (a52dec,
      "Pushing buffer with ts %" GST_TIME_FORMAT " duration %" GST_TIME_FORMAT,
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)),
      GST_TIME_ARGS (GST_BUFFER_DURATION (buf)));

  gst_pad_push (srcpad, GST_DATA (buf));

  return 0;
}

static gboolean
gst_a52dec_reneg (GstPad * pad)
{
  GstAudioChannelPosition *pos;
  GstA52Dec *a52dec = GST_A52DEC (gst_pad_get_parent (pad));
  gint channels = gst_a52dec_channels (a52dec->using_channels, &pos);
  GstCaps *caps;

  if (!channels)
    return FALSE;

  GST_INFO ("a52dec: reneg channels:%d rate:%d\n",
      channels, a52dec->sample_rate);

  caps = gst_caps_new_simple ("audio/x-raw-float",
      "endianness", G_TYPE_INT, G_BYTE_ORDER,
      "width", G_TYPE_INT, SAMPLE_WIDTH,
      "channels", G_TYPE_INT, channels,
      "rate", G_TYPE_INT, a52dec->sample_rate,
      "buffer-frames", G_TYPE_INT, 0, NULL);
  gst_audio_set_channel_positions (gst_caps_get_structure (caps, 0), pos);
  g_free (pos);

  return gst_pad_set_explicit_caps (pad, caps);
}

static void
gst_a52dec_handle_event (GstA52Dec * a52dec, GstEvent * event)
{
  GST_LOG ("Handling event of type %d timestamp %llu", GST_EVENT_TYPE (event),
      GST_EVENT_TIMESTAMP (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_DISCONTINUOUS:{
      gint64 val;

      if (!gst_event_discont_get_value (event, GST_FORMAT_TIME, &val)
          || !GST_CLOCK_TIME_IS_VALID (val)) {
        GST_WARNING ("No time discont value in event %p", event);
      } else {
        a52dec->time = val;
      }
    }
      /* fall-through */
    case GST_EVENT_FLUSH:
      if (a52dec->cache) {
        gst_buffer_unref (a52dec->cache);
        a52dec->cache = NULL;
      }
      break;
    default:
      break;
  }

  gst_pad_event_default (a52dec->sinkpad, event);
}

static void
gst_a52dec_update_streaminfo (GstA52Dec * a52dec)
{
  GstTagList *taglist;

  taglist = gst_tag_list_new ();

  gst_tag_list_add (taglist, GST_TAG_MERGE_APPEND,
      GST_TAG_BITRATE, (guint) a52dec->bit_rate, NULL);

  gst_element_found_tags_for_pad (GST_ELEMENT (a52dec),
      GST_PAD (a52dec->srcpad), a52dec->time, taglist);
}

static gboolean
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

  /* process */
  flags = a52dec->request_channels;     /* | A52_ADJUST_LEVEL; */
  a52dec->level = 1;
  if (a52_frame (a52dec->state, data, &flags, &a52dec->level, a52dec->bias)) {
    GST_WARNING ("a52_frame error");
    return TRUE;
  }
  channels = flags & (A52_CHANNEL_MASK | A52_LFE);
  if (a52dec->using_channels != channels) {
    need_reneg = TRUE;
    a52dec->using_channels = channels;
  }

  /* negotiate if required */
  if (need_reneg == TRUE) {
    GST_DEBUG ("a52dec reneg: sample_rate:%d stream_chans:%d using_chans:%d\n",
        a52dec->sample_rate, a52dec->stream_channels, a52dec->using_channels);
    if (!gst_a52dec_reneg (a52dec->srcpad)) {
      GST_ELEMENT_ERROR (a52dec, CORE, NEGOTIATION, (NULL), (NULL));
      return FALSE;
    }
  }

  if (a52dec->dynamic_range_compression == FALSE) {
    a52_dynrng (a52dec->state, NULL, NULL);
  }

  /* each frame consists of 6 blocks */
  for (i = 0; i < 6; i++) {
    if (a52_block (a52dec->state)) {
      GST_WARNING ("a52_block error %d", i);
    } else {
      /* push on */
      gst_a52dec_push (a52dec, a52dec->srcpad, a52dec->using_channels,
          a52dec->samples, a52dec->time);
    }
    a52dec->time += 256 * GST_SECOND / a52dec->sample_rate;
  }

  return TRUE;
}

static void
gst_a52dec_chain (GstPad * pad, GstData * _data)
{
  GstA52Dec *a52dec = GST_A52DEC (gst_pad_get_parent (pad));
  GstBuffer *buf;
  guint8 *data;
  guint size;
  gint length = 0, flags, sample_rate, bit_rate;

  /* event handling */
  if (GST_IS_EVENT (_data)) {
    gst_a52dec_handle_event (a52dec, GST_EVENT (_data));
    return;
  }

  /* merge with cache, if any. Also make sure timestamps match */
  buf = GST_BUFFER (_data);
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
      if (!gst_a52dec_handle_frame (a52dec, data,
              length, flags, sample_rate, bit_rate)) {
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
}

static GstStateChangeReturn
gst_a52dec_change_state (GstElement * element, GstStateChange transition)
{
  GstA52Dec *a52dec = GST_A52DEC (element);
  GstCPUFlags cpuflags;
  uint32_t a52_cpuflags = 0;

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      cpuflags = gst_cpu_get_flags ();
      if (cpuflags & GST_CPU_FLAG_MMX)
        a52_cpuflags |= MM_ACCEL_X86_MMX;
      if (cpuflags & GST_CPU_FLAG_3DNOW)
        a52_cpuflags |= MM_ACCEL_X86_3DNOW;
      if (cpuflags & GST_CPU_FLAG_MMXEXT)
        a52_cpuflags |= MM_ACCEL_X86_MMXEXT;

      a52dec->state = a52_init (a52_cpuflags);
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      a52dec->samples = a52_samples (a52dec->state);
      a52dec->bit_rate = -1;
      a52dec->sample_rate = -1;
      a52dec->stream_channels = A52_CHANNEL;
      a52dec->request_channels = A52_3F2R | A52_LFE;
      a52dec->using_channels = A52_CHANNEL;
      a52dec->level = 1;
      a52dec->bias = 0;
      a52dec->time = 0;
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      break;
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      a52dec->samples = NULL;
      if (a52dec->cache) {
        gst_buffer_unref (a52dec->cache);
        a52dec->cache = NULL;
      }
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      a52_free (a52dec->state);
      a52dec->state = NULL;
      break;
    default:
      break;

  }

  GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  return GST_STATE_CHANGE_SUCCESS;
}

static void
gst_a52dec_set_property (GObject * object, guint prop_id, const GValue * value,
    GParamSpec * pspec)
{
  GstA52Dec *src;

  g_return_if_fail (GST_IS_A52DEC (object));
  src = GST_A52DEC (object);

  switch (prop_id) {
    case ARG_DRC:
      src->dynamic_range_compression = g_value_get_boolean (value);
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
  GstA52Dec *src;

  g_return_if_fail (GST_IS_A52DEC (object));
  src = GST_A52DEC (object);

  switch (prop_id) {
    case ARG_DRC:
      g_value_set_boolean (value, src->dynamic_range_compression);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
plugin_init (GstPlugin * plugin)
{

  if (!gst_library_load ("gstaudio"))
    return FALSE;

  if (!gst_element_register (plugin, "a52dec", GST_RANK_PRIMARY,
          GST_TYPE_A52DEC))
    return FALSE;

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "a52dec",
    "Decodes ATSC A/52 encoded audio streams",
    plugin_init, VERSION, "GPL", GST_PACKAGE, GST_ORIGIN);
