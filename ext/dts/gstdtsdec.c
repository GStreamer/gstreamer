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

static void gst_dtsdec_loop (GstElement * element);
static GstElementStateReturn gst_dtsdec_change_state (GstElement * element);

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
  gst_element_add_pad (element, dtsdec->sinkpad);
  gst_element_set_loop_function (element, gst_dtsdec_loop);

  dtsdec->srcpad =
      gst_pad_new_from_template (gst_element_get_pad_template (element,
          "src"), "src");
  gst_pad_use_explicit_caps (dtsdec->srcpad);
  gst_element_add_pad (element, dtsdec->srcpad);

  GST_FLAG_SET (element, GST_ELEMENT_EVENT_AWARE);
  dtsdec->dynamic_range_compression = FALSE;
}

static gint
gst_dtsdec_channels (uint32_t flags)
{
  gint chans = 0;

  switch (flags & DTS_CHANNEL_MASK) {
    case DTS_MONO:
      chans = 1;
      break;
    case DTS_CHANNEL:
    case DTS_STEREO:
    case DTS_STEREO_SUMDIFF:
    case DTS_STEREO_TOTAL:
    case DTS_DOLBY:
      chans = 2;
      break;
    case DTS_3F:
    case DTS_2F1R:
      chans = 3;
      break;
    case DTS_3F1R:
    case DTS_2F2R:
      chans = 4;
      break;
    case DTS_3F2R:
      chans = 5;
      break;
    case DTS_4F2R:
      chans = 6;
      break;
    default:
      /* error */
      g_warning ("dtsdec: invalid flags 0x%x", flags);
      return 0;
  }
  if (flags & DTS_LFE)
    chans += 1;

  return chans;
}

static gboolean
gst_dtsdec_renegotiate (GstDtsDec * dts)
{
  GstCaps *caps = gst_caps_from_string (DTS_CAPS);
  gint channels = gst_dtsdec_channels (dts->using_channels);

  GST_INFO ("dtsdec renegotiate, channels=%d, rate=%d",
      channels, dts->sample_rate);

  gst_caps_set_simple (caps,
      "channels", G_TYPE_INT, channels,
      "rate", G_TYPE_INT, (gint) dts->sample_rate, NULL);

  return gst_pad_set_explicit_caps (dts->srcpad, caps);
}

static void
gst_dtsdec_handle_event (GstDtsDec * dts)
{
  guint32 remaining;
  GstEvent *event;

  gst_bytestream_get_status (dts->bs, &remaining, &event);

  if (!event) {
    GST_ELEMENT_ERROR (dts, RESOURCE, READ, (NULL), (NULL));
    return;
  }

  GST_LOG ("Handling event of type %d timestamp %llu", GST_EVENT_TYPE (event),
      GST_EVENT_TIMESTAMP (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_DISCONTINUOUS:
    case GST_EVENT_FLUSH:
      gst_bytestream_flush_fast (dts->bs, remaining);
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

static void
gst_dtsdec_loop (GstElement * element)
{
  GstDtsDec *dts = GST_DTSDEC (element);
  guint8 *data;
  GstBuffer *buf, *out;
  sample_t *samples;
  gint i, length, flags, sample_rate, bit_rate, frame_length, s, c, num_c;
  gint channels, skipped = 0, num_blocks;
  guint32 got_bytes;
  gboolean need_renegotiation = FALSE;
  GstClockTime timestamp = 0;

  /* find sync. Don't know what 3840 is based on...  */
#define MAX_SKIP 3840
  while (skipped < MAX_SKIP) {
    got_bytes = gst_bytestream_peek_bytes (dts->bs, &data, 7);
    if (got_bytes < 7) {
      gst_dtsdec_handle_event (dts);
      return;
    }
    length = dts_syncinfo (dts->state, data, &flags,
        &sample_rate, &bit_rate, &frame_length);
    if (length == 0) {
      /* shift window to re-find sync */
      gst_bytestream_flush_fast (dts->bs, 1);
      skipped++;
      GST_LOG ("Skipped");
    } else
      break;
  }

  if (skipped >= MAX_SKIP) {
    GST_ELEMENT_ERROR (dts, RESOURCE, SYNC, (NULL), (NULL));
    return;
  }

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

  /* read the header + rest of frame */
  got_bytes = gst_bytestream_read (dts->bs, &buf, length);
  if (got_bytes < length) {
    gst_dtsdec_handle_event (dts);
    return;
  }

  data = GST_BUFFER_DATA (buf);
  timestamp = gst_bytestream_get_timestamp (dts->bs);
  if (GST_CLOCK_TIME_IS_VALID (timestamp)) {
    if (timestamp == dts->last_ts) {
      timestamp = dts->current_ts;
    } else {
      dts->last_ts = timestamp;
    }
  }

  /* process */
  flags = dts->request_channels | DTS_ADJUST_LEVEL;
  dts->level = 1;

  if (dts_frame (dts->state, data, &flags, &dts->level, dts->bias)) {
    GST_WARNING ("dts_frame error");
    goto end;
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
      goto end;
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
    num_c = gst_dtsdec_channels (dts->using_channels);
    out = gst_buffer_new_and_alloc ((SAMPLE_WIDTH / 8) * 256 * num_c);
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

end:
  gst_buffer_unref (buf);
}

static GstElementStateReturn
gst_dtsdec_change_state (GstElement * element)
{
  GstDtsDec *dts = GST_DTSDEC (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_NULL_TO_READY:{
      GstCPUFlags cpuflags;
      uint32_t mm_accel = 0;

      dts->bs = gst_bytestream_new (dts->sinkpad);
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
    case GST_STATE_READY_TO_PAUSED:
      dts->samples = dts_samples (dts->state);
      dts->bit_rate = -1;
      dts->sample_rate = -1;
      dts->stream_channels = 0;
      /* FIXME force stereo for now */
      dts->request_channels = DTS_STEREO;
      dts->using_channels = 0;
      dts->level = 1;
      dts->bias = 0;
      dts->last_ts = 0;
      dts->current_ts = 0;
      break;
    case GST_STATE_PAUSED_TO_READY:
      dts->samples = NULL;
      break;
    case GST_STATE_READY_TO_NULL:
      gst_bytestream_destroy (dts->bs);
      dts->bs = NULL;
      dts_free (dts->state);
      dts->state = NULL;
      break;
    default:
      break;
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);

  return GST_STATE_SUCCESS;
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
  if (!gst_library_load ("gstbytestream"))
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
