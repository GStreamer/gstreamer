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

/*
 * "audio/a52", "audio/x-a52" and "audio/ac3" should not be used (deprecated names)
 * Only use "audio/x-ac3" in new code.
 */
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

static void gst_a52dec_loop (GstElement * element);
static GstElementStateReturn gst_a52dec_change_state (GstElement * element);

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
  /* create the sink and src pads */
  a52dec->sinkpad =
      gst_pad_new_from_template (gst_element_get_pad_template (GST_ELEMENT
          (a52dec), "sink"), "sink");
  gst_element_add_pad (GST_ELEMENT (a52dec), a52dec->sinkpad);
  gst_element_set_loop_function ((GstElement *) a52dec, gst_a52dec_loop);

  a52dec->srcpad =
      gst_pad_new_from_template (gst_element_get_pad_template (GST_ELEMENT
          (a52dec), "src"), "src");
  gst_pad_use_explicit_caps (a52dec->srcpad);
  gst_element_add_pad (GST_ELEMENT (a52dec), a52dec->srcpad);

  GST_FLAG_SET (GST_ELEMENT (a52dec), GST_ELEMENT_EVENT_AWARE);
  a52dec->dynamic_range_compression = FALSE;
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
gst_a52dec_push (GstPad * srcpad, int flags, sample_t * samples,
    GstClockTime timestamp)
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

  gst_pad_push (srcpad, GST_DATA (buf));

  return 0;
}

/* END modified a52dec conversion code */

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
gst_a52dec_handle_event (GstA52Dec * a52dec)
{
  guint32 remaining;
  GstEvent *event;

  gst_bytestream_get_status (a52dec->bs, &remaining, &event);

  if (!event) {
    g_warning ("a52dec: no bytestream event");
    return;
  }

  GST_LOG ("Handling event of type %d timestamp %llu", GST_EVENT_TYPE (event),
      GST_EVENT_TIMESTAMP (event));
  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_DISCONTINUOUS:
    case GST_EVENT_FLUSH:
      gst_bytestream_flush_fast (a52dec->bs, remaining);
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
      GST_PAD (a52dec->srcpad), a52dec->current_ts, taglist);
}

static void
gst_a52dec_loop (GstElement * element)
{
  GstA52Dec *a52dec;
  guint8 *data;
  int i, length, flags, sample_rate, bit_rate;
  int channels;
  GstBuffer *buf;
  guint32 got_bytes;
  gboolean need_reneg;
  GstClockTime timestamp = 0;

  a52dec = GST_A52DEC (element);

  /* find and read header */
  do {
    gint skipped_bytes = 0;

    while (skipped_bytes < 3840) {
      got_bytes = gst_bytestream_peek_bytes (a52dec->bs, &data, 7);
      if (got_bytes < 7) {
        gst_a52dec_handle_event (a52dec);
        return;
      }
      length = a52_syncinfo (data, &flags, &sample_rate, &bit_rate);
      if (length == 0) {
        /* slide window to next 7 bytesa */
        gst_bytestream_flush_fast (a52dec->bs, 1);
        skipped_bytes++;
        GST_LOG ("Skipped");
      } else
        break;
    }
  }
  while (0);

  need_reneg = FALSE;

  if (a52dec->sample_rate != sample_rate) {
    need_reneg = TRUE;
    a52dec->sample_rate = sample_rate;
  }

  a52dec->stream_channels = flags & (A52_CHANNEL_MASK | A52_LFE);

  if (bit_rate != a52dec->bit_rate) {
    a52dec->bit_rate = bit_rate;
    gst_a52dec_update_streaminfo (a52dec);
  }

  /* read the header + rest of frame */
  got_bytes = gst_bytestream_read (a52dec->bs, &buf, length);
  if (got_bytes < length) {
    gst_a52dec_handle_event (a52dec);
    return;
  }
  data = GST_BUFFER_DATA (buf);
  timestamp = gst_bytestream_get_timestamp (a52dec->bs);
  if (GST_CLOCK_TIME_IS_VALID (timestamp)) {
    if (timestamp == a52dec->last_ts) {
      timestamp = a52dec->current_ts;
    } else {
      a52dec->last_ts = timestamp;
    }
  }

  /* process */
  flags = a52dec->request_channels;     /* | A52_ADJUST_LEVEL; */
  a52dec->level = 1;

  if (a52_frame (a52dec->state, data, &flags, &a52dec->level, a52dec->bias)) {
    GST_WARNING ("a52_frame error");
    goto end;
  }

  channels = flags & (A52_CHANNEL_MASK | A52_LFE);

  if (a52dec->using_channels != channels) {
    need_reneg = TRUE;
    a52dec->using_channels = channels;
  }

  if (need_reneg == TRUE) {
    GST_DEBUG ("a52dec reneg: sample_rate:%d stream_chans:%d using_chans:%d\n",
        a52dec->sample_rate, a52dec->stream_channels, a52dec->using_channels);
    if (!gst_a52dec_reneg (a52dec->srcpad))
      goto end;
  }

  if (a52dec->dynamic_range_compression == FALSE) {
    a52_dynrng (a52dec->state, NULL, NULL);
  }

  for (i = 0; i < 6; i++) {
    if (a52_block (a52dec->state)) {
      GST_WARNING ("a52_block error %d", i);
      continue;
    }
    /* push on */

    if (gst_a52dec_push (a52dec->srcpad, a52dec->using_channels,
            a52dec->samples, timestamp)) {
      GST_WARNING ("a52dec push error");
    } else {

      if (i % 2)
        timestamp += 256 * GST_SECOND / a52dec->sample_rate;
    }
  }

  a52dec->current_ts = timestamp;

end:
  gst_buffer_unref (buf);
}

static GstElementStateReturn
gst_a52dec_change_state (GstElement * element)
{
  GstA52Dec *a52dec = GST_A52DEC (element);
  GstCPUFlags cpuflags;
  uint32_t a52_cpuflags = 0;

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_NULL_TO_READY:
      a52dec->bs = gst_bytestream_new (a52dec->sinkpad);
      cpuflags = gst_cpu_get_flags ();
      if (cpuflags & GST_CPU_FLAG_MMX)
        a52_cpuflags |= MM_ACCEL_X86_MMX;
      if (cpuflags & GST_CPU_FLAG_3DNOW)
        a52_cpuflags |= MM_ACCEL_X86_3DNOW;
      if (cpuflags & GST_CPU_FLAG_MMXEXT)
        a52_cpuflags |= MM_ACCEL_X86_MMXEXT;

      a52dec->state = a52_init (a52_cpuflags);
      break;
    case GST_STATE_READY_TO_PAUSED:
      a52dec->samples = a52_samples (a52dec->state);
      a52dec->bit_rate = -1;
      a52dec->sample_rate = -1;
      a52dec->stream_channels = A52_CHANNEL;
      a52dec->request_channels = A52_3F2R | A52_LFE;
      a52dec->using_channels = A52_CHANNEL;
      a52dec->level = 1;
      a52dec->bias = 0;
      a52dec->last_ts = 0;
      a52dec->current_ts = 0;
      a52dec->last_timestamp = 0;
      a52dec->last_diff = 0;

      break;
    case GST_STATE_PAUSED_TO_PLAYING:
      break;
    case GST_STATE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_PAUSED_TO_READY:
      gst_bytestream_destroy (a52dec->bs);
      a52dec->bs = NULL;
      a52dec->samples = NULL;
      break;
    case GST_STATE_READY_TO_NULL:
      a52_free (a52dec->state);
      a52dec->state = NULL;
      break;
    default:
      break;

  }

  GST_ELEMENT_CLASS (parent_class)->change_state (element);

  return GST_STATE_SUCCESS;
}

static void
gst_a52dec_set_property (GObject * object, guint prop_id, const GValue * value,
    GParamSpec * pspec)
{
  GstA52Dec *src;

  /* it's not null if we got it, but it might not be ours */
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

  /* it's not null if we got it, but it might not be ours */
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

  if (!gst_library_load ("gstbytestream") || !gst_library_load ("gstaudio"))
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
