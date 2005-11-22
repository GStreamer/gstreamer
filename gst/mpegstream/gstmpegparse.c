/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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


/*#define GST_DEBUG_ENABLED*/
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "gstmpegparse.h"
#include "gstmpegclock.h"

static GstFormat scr_format;

GST_DEBUG_CATEGORY_STATIC (gstmpegparse_debug);
#define GST_CAT_DEFAULT (gstmpegparse_debug)

GST_DEBUG_CATEGORY_EXTERN (GST_CAT_SEEK);

#define MP_INVALID_SCR ((guint64)(-1))
#define MP_MUX_RATE_MULT 50
#define MP_MIN_VALID_BSS 8192
#define MP_MAX_VALID_BSS 16384
/*
 * Hysteresis value to limit the
 * total predicted time skipping about
 */
#define MP_SCR_RATE_HYST 0.08

/* elementfactory information */
static GstElementDetails mpeg_parse_details = {
  "MPEG System Parser",
  "Codec/Parser",
  "Parses MPEG1 and MPEG2 System Streams",
  "Erik Walthinsen <omega@cse.ogi.edu>\n" "Wim Taymans <wim.taymans@chello.be>"
};

#define CLASS(o)	GST_MPEG_PARSE_CLASS (G_OBJECT_GET_CLASS (o))

#define DEFAULT_MAX_DISCONT	120000

/* GstMPEGParse signals and args */
enum
{
  SIGNAL_REACHED_OFFSET,
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_SYNC,
  ARG_MAX_DISCONT,
  ARG_DO_ADJUST,
  ARG_BYTE_OFFSET,
  ARG_TIME_OFFSET
      /* FILL ME */
};

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/mpeg, "
        "mpegversion = (int) [ 1, 2 ], " "systemstream = (boolean) TRUE")
    );

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/mpeg, "
        "mpegversion = (int) [ 1, 2 ], " "systemstream = (boolean) TRUE")
    );

static void gst_mpeg_parse_class_init (GstMPEGParseClass * klass);
static void gst_mpeg_parse_base_init (GstMPEGParseClass * klass);
static void gst_mpeg_parse_init (GstMPEGParse * mpeg_parse);
static GstStateChangeReturn gst_mpeg_parse_change_state (GstElement * element,
    GstStateChange transition);

static gboolean gst_mpeg_parse_set_clock (GstElement * element,
    GstClock * clock);

static gboolean gst_mpeg_parse_parse_packhead (GstMPEGParse * mpeg_parse,
    GstBuffer * buffer);

static void gst_mpeg_parse_reset (GstMPEGParse * mpeg_parse);
static GstFlowReturn gst_mpeg_parse_handle_discont (GstMPEGParse * mpeg_parse,
    GstEvent * event);

static GstFlowReturn gst_mpeg_parse_send_buffer (GstMPEGParse * mpeg_parse,
    GstBuffer * buffer, GstClockTime time);
static GstFlowReturn gst_mpeg_parse_process_event (GstMPEGParse * mpeg_parse,
    GstEvent * event, GstClockTime time);
static GstFlowReturn gst_mpeg_parse_send_discont (GstMPEGParse * mpeg_parse,
    GstClockTime time);
static gboolean gst_mpeg_parse_send_event (GstMPEGParse * mpeg_parse,
    GstEvent * event, GstClockTime time);

static void gst_mpeg_parse_pad_added (GstElement * element, GstPad * pad);

static gboolean gst_mpeg_parse_event (GstPad * pad, GstEvent * event);
static GstFlowReturn gst_mpeg_parse_chain (GstPad * pad, GstBuffer * buf);

static void gst_mpeg_parse_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_mpeg_parse_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_mpeg_parse_set_index (GstElement * element, GstIndex * index);
static GstIndex *gst_mpeg_parse_get_index (GstElement * element);

static GstElementClass *parent_class = NULL;

static guint gst_mpeg_parse_signals[LAST_SIGNAL] = { 0 };

GType
gst_mpeg_parse_get_type (void)
{
  static GType mpeg_parse_type = 0;

  if (!mpeg_parse_type) {
    static const GTypeInfo mpeg_parse_info = {
      sizeof (GstMPEGParseClass),
      (GBaseInitFunc) gst_mpeg_parse_base_init,
      NULL,
      (GClassInitFunc) gst_mpeg_parse_class_init,
      NULL,
      NULL,
      sizeof (GstMPEGParse),
      0,
      (GInstanceInitFunc) gst_mpeg_parse_init,
    };

    mpeg_parse_type =
        g_type_register_static (GST_TYPE_ELEMENT, "GstMPEGParse",
        &mpeg_parse_info, 0);

    GST_DEBUG_CATEGORY_INIT (gstmpegparse_debug, "mpegparse", 0,
        "MPEG parser element");
  }
  return mpeg_parse_type;
}

static void
gst_mpeg_parse_base_init (GstMPEGParseClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_set_details (element_class, &mpeg_parse_details);
}

static void
gst_mpeg_parse_class_init (GstMPEGParseClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gst_mpeg_parse_signals[SIGNAL_REACHED_OFFSET] =
      g_signal_new ("reached-offset", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_FIRST, G_STRUCT_OFFSET (GstMPEGParseClass, reached_offset),
      NULL, NULL, gst_marshal_VOID__VOID, G_TYPE_NONE, 0);

  gobject_class->get_property = gst_mpeg_parse_get_property;
  gobject_class->set_property = gst_mpeg_parse_set_property;

  gstelement_class->pad_added = gst_mpeg_parse_pad_added;
  gstelement_class->change_state = gst_mpeg_parse_change_state;
  gstelement_class->set_clock = gst_mpeg_parse_set_clock;
  gstelement_class->get_index = gst_mpeg_parse_get_index;
  gstelement_class->set_index = gst_mpeg_parse_set_index;

  klass->parse_packhead = gst_mpeg_parse_parse_packhead;
  klass->parse_syshead = NULL;
  klass->parse_packet = NULL;
  klass->parse_pes = NULL;
  klass->handle_discont = gst_mpeg_parse_handle_discont;
  klass->send_buffer = gst_mpeg_parse_send_buffer;
  klass->process_event = gst_mpeg_parse_process_event;
  klass->send_discont = gst_mpeg_parse_send_discont;
  klass->send_event = gst_mpeg_parse_send_event;

  /* FIXME: this is a hack.  We add the pad templates here instead
   * in the base_init function, since the derived class (mpegdemux)
   * uses different pads.  IMO, this is wrong. */
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&src_factory));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&sink_factory));

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_SYNC,
      g_param_spec_boolean ("sync", "Sync", "Synchronize on the stream SCR",
          FALSE, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_MAX_DISCONT,
      g_param_spec_int ("max_discont", "Max Discont",
          "The maximum allowed SCR discontinuity", 0, G_MAXINT,
          DEFAULT_MAX_DISCONT, G_PARAM_READWRITE));
  /* FIXME: Default is TRUE to make the behavior backwards compatible.
     It probably should be FALSE. */
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_DO_ADJUST,
      g_param_spec_boolean ("adjust", "adjust", "Adjust timestamps to "
          "smooth discontinuities", TRUE, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_BYTE_OFFSET,
      g_param_spec_uint64 ("byte-offset", "Byte Offset",
          "Emit reached-offset signal when the byte offset is reached.",
          0, G_MAXUINT64, G_MAXUINT64, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_TIME_OFFSET,
      g_param_spec_uint64 ("time-offset", "Time Offset",
          "Time offset in the stream.",
          0, G_MAXUINT64, G_MAXUINT64, G_PARAM_READABLE));
}

static void
gst_mpeg_parse_init (GstMPEGParse * mpeg_parse)
{
  GstElementClass *klass = GST_ELEMENT_GET_CLASS (mpeg_parse);
  GstPadTemplate *templ;

  templ = gst_element_class_get_pad_template (klass, "sink");
  mpeg_parse->sinkpad = gst_pad_new_from_template (templ, "sink");
  gst_element_add_pad (GST_ELEMENT (mpeg_parse), mpeg_parse->sinkpad);

  if ((templ = gst_element_class_get_pad_template (klass, "src"))) {
    mpeg_parse->srcpad = gst_pad_new_from_template (templ, "src");
    gst_element_add_pad (GST_ELEMENT (mpeg_parse), mpeg_parse->srcpad);
    gst_pad_set_event_function (mpeg_parse->srcpad,
        GST_DEBUG_FUNCPTR (gst_mpeg_parse_handle_src_event));
#if 0
    gst_pad_set_query_type_function (mpeg_parse->srcpad,
        GST_DEBUG_FUNCPTR (gst_mpeg_parse_get_src_query_types));
    gst_pad_set_query_function (mpeg_parse->srcpad,
        GST_DEBUG_FUNCPTR (gst_mpeg_parse_handle_src_query));
#endif
    gst_pad_use_fixed_caps (mpeg_parse->srcpad);
  }

  gst_pad_set_event_function (mpeg_parse->sinkpad,
      GST_DEBUG_FUNCPTR (gst_mpeg_parse_event));
  gst_pad_set_chain_function (mpeg_parse->sinkpad,
      GST_DEBUG_FUNCPTR (gst_mpeg_parse_chain));

  mpeg_parse->packetize = NULL;
  mpeg_parse->sync = FALSE;
  mpeg_parse->id = NULL;
  mpeg_parse->max_discont = DEFAULT_MAX_DISCONT;

  mpeg_parse->do_adjust = TRUE;
  mpeg_parse->use_adjust = TRUE;

  mpeg_parse->byte_offset = G_MAXUINT64;

  gst_mpeg_parse_reset (mpeg_parse);
}

static gboolean
gst_mpeg_parse_set_clock (GstElement * element, GstClock * clock)
{
  GstMPEGParse *parse = GST_MPEG_PARSE (element);

  parse->clock = clock;

  return TRUE;
}

#if 0
static void
gst_mpeg_parse_update_streaminfo (GstMPEGParse * mpeg_parse)
{
  GstProps *props;
  GstPropsEntry *entry;
  gboolean mpeg2 = GST_MPEG_PACKETIZE_IS_MPEG2 (mpeg_parse->packetize);
  GstCaps *caps;

  props = gst_props_empty_new ();

  entry = gst_props_entry_new ("mpegversion", G_TYPE_INT (mpeg2 ? 2 : 1));
  gst_props_add_entry (props, (GstPropsEntry *) entry);

  entry =
      gst_props_entry_new ("bitrate", G_TYPE_INT (mpeg_parse->mux_rate * 8));
  gst_props_add_entry (props, (GstPropsEntry *) entry);

  caps = gst_caps_new ("mpeg_streaminfo",
      "application/x-gst-streaminfo", props);

  gst_caps_replace_sink (&mpeg_parse->streaminfo, caps);
  g_object_notify (G_OBJECT (mpeg_parse), "streaminfo");
}
#endif

static void
gst_mpeg_parse_reset (GstMPEGParse * mpeg_parse)
{
  GST_DEBUG ("Resetting mpeg_parse");
  mpeg_parse->current_scr = 0;
  mpeg_parse->current_ts = 0;
  mpeg_parse->bytes_since_scr = 0;
  mpeg_parse->avg_bitrate_time = 0;
  mpeg_parse->avg_bitrate_bytes = 0;
  mpeg_parse->first_scr = MP_INVALID_SCR;
  mpeg_parse->first_scr_pos = 0;
  mpeg_parse->last_scr = MP_INVALID_SCR;
  mpeg_parse->last_scr_pos = 0;
  mpeg_parse->scr_rate = 0;

  mpeg_parse->adjust = 0;
  mpeg_parse->next_scr = 0;
  mpeg_parse->mux_rate = 0;

  mpeg_parse->discont_pending = FALSE;
  mpeg_parse->scr_pending = FALSE;
}

static gboolean
gst_mpeg_parse_handle_discont (GstMPEGParse * mpeg_parse, GstEvent * event)
{
  GstFormat format;
  gboolean ret = TRUE;
  gint64 time;

#if 0
  if (GST_EVENT_DISCONT_NEW_MEDIA (event)) {
    gst_mpeg_parse_reset (mpeg_parse);
  }
#endif

  gst_event_parse_new_segment (event, NULL, NULL, &format, &time, NULL, NULL);

  if (format == GST_FORMAT_TIME && (GST_CLOCK_TIME_IS_VALID (time))) {
    GST_DEBUG_OBJECT (mpeg_parse, "forwarding discontinuity, time: %0.3fs",
        (double) time / GST_SECOND);

    if (CLASS (mpeg_parse)->send_discont)
      ret = CLASS (mpeg_parse)->send_discont (mpeg_parse, time);
  } else {
    /* Use the next SCR to send a discontinuous event. */
    GST_DEBUG_OBJECT (mpeg_parse, "Using next SCR to send discont");
    mpeg_parse->discont_pending = TRUE;
    mpeg_parse->scr_pending = TRUE;
  }
  mpeg_parse->packetize->resync = TRUE;

  gst_event_unref (event);
  return ret;
}

static GstFlowReturn
gst_mpeg_parse_send_buffer (GstMPEGParse * mpeg_parse, GstBuffer * buffer,
    GstClockTime time)
{
  GstFlowReturn result = GST_FLOW_OK;

  if (!gst_caps_is_fixed (GST_PAD_CAPS (mpeg_parse->srcpad))) {
    gboolean mpeg2 = GST_MPEG_PACKETIZE_IS_MPEG2 (mpeg_parse->packetize);
    GstCaps *caps;

    caps = gst_caps_new_simple ("video/mpeg",
        "mpegversion", G_TYPE_INT, (mpeg2 ? 2 : 1),
        "systemstream", G_TYPE_BOOLEAN, TRUE,
        "parsed", G_TYPE_BOOLEAN, TRUE, NULL);

    if (!gst_pad_set_caps (mpeg_parse->srcpad, caps)) {
      GST_ELEMENT_ERROR (GST_ELEMENT (mpeg_parse),
          CORE, NEGOTIATION, (NULL), ("failed to set caps"));
      gst_caps_unref (caps);
      return GST_FLOW_ERROR;
    }
    gst_caps_unref (caps);
  }

  GST_BUFFER_TIMESTAMP (buffer) = time;
  GST_DEBUG ("current_scr %" G_GINT64_FORMAT, time);

  result = gst_pad_push (mpeg_parse->srcpad, buffer);

  return result;
}

static gboolean
gst_mpeg_parse_process_event (GstMPEGParse * mpeg_parse, GstEvent * event,
    GstClockTime time)
{
  return gst_pad_event_default (mpeg_parse->sinkpad, event);
}

static gboolean
gst_mpeg_parse_send_discont (GstMPEGParse * mpeg_parse, GstClockTime time)
{
  GstEvent *event;

  event = gst_event_new_new_segment (FALSE, 1.0, GST_FORMAT_TIME, time,
      GST_CLOCK_TIME_NONE, (gint64) 0);

  if (CLASS (mpeg_parse)->send_event)
    return CLASS (mpeg_parse)->send_event (mpeg_parse, event, time);

  return FALSE;
}

static gboolean
gst_mpeg_parse_send_event (GstMPEGParse * mpeg_parse, GstEvent * event,
    GstClockTime time)
{
  return gst_pad_push_event (mpeg_parse->srcpad, event);
}

static void
gst_mpeg_parse_pad_added (GstElement * element, GstPad * pad)
{
  GstMPEGParse *mpeg_parse;

  if (GST_PAD_IS_SINK (pad))
    return;

  mpeg_parse = GST_MPEG_PARSE (element);

  /* For each new added pad, send a discont so it knows about the current
   * time. This is required because MPEG allows any sort of order of
   * packets, including setting base time before defining streams or
   * even adding streams halfway a stream. */
  if (!mpeg_parse->scr_pending) {
    GstEvent *event = gst_event_new_new_segment (FALSE, 1.0,
        GST_FORMAT_TIME,
        (guint64) MPEGTIME_TO_GSTTIME (mpeg_parse->current_scr +
            mpeg_parse->adjust),
        GST_CLOCK_TIME_NONE,
        (gint64) 0);

    gst_pad_push_event (pad, event);
  }
}

static gboolean
gst_mpeg_parse_parse_packhead (GstMPEGParse * mpeg_parse, GstBuffer * buffer)
{
  guint8 *buf;
  guint64 prev_scr, scr, diff;
  guint32 scr1, scr2;
  guint32 new_rate;
  guint64 offset;

  buf = GST_BUFFER_DATA (buffer);
  buf += 4;

  scr1 = GUINT32_FROM_BE (*(guint32 *) buf);
  scr2 = GUINT32_FROM_BE (*(guint32 *) (buf + 4));

  if (GST_MPEG_PACKETIZE_IS_MPEG2 (mpeg_parse->packetize)) {
    guint32 scr_ext;

    /* :2=01 ! scr:3 ! marker:1==1 ! scr:15 ! marker:1==1 ! scr:15 */
    scr = ((guint64) scr1 & 0x38000000) << 3;
    scr |= ((guint64) scr1 & 0x03fff800) << 4;
    scr |= ((guint64) scr1 & 0x000003ff) << 5;
    scr |= ((guint64) scr2 & 0xf8000000) >> 27;

    scr_ext = (scr2 & 0x03fe0000) >> 17;

    scr = (scr * 300 + scr_ext % 300) / 300;

    GST_LOG_OBJECT (mpeg_parse, "%" G_GINT64_FORMAT " %d, %08x %08x %"
        G_GINT64_FORMAT " diff: %" G_GINT64_FORMAT,
        scr, scr_ext, scr1, scr2, mpeg_parse->bytes_since_scr,
        scr - mpeg_parse->current_scr);

    buf += 6;
    new_rate = (GUINT32_FROM_BE ((*(guint32 *) buf)) & 0xfffffc00) >> 10;
  } else {
    scr = ((guint64) scr1 & 0x0e000000) << 5;
    scr |= ((guint64) scr1 & 0x00fffe00) << 6;
    scr |= ((guint64) scr1 & 0x000000ff) << 7;
    scr |= ((guint64) scr2 & 0xfe000000) >> 25;

    buf += 5;
    /* we do this byte by byte because buf[3] might be outside of buf's
     * memory space */
    new_rate = ((gint32) buf[0] & 0x7f) << 15;
    new_rate |= ((gint32) buf[1]) << 7;
    new_rate |= buf[2] >> 1;
  }
  new_rate *= MP_MUX_RATE_MULT;

  prev_scr = mpeg_parse->current_scr;
  mpeg_parse->current_scr = scr;
  mpeg_parse->current_ts = MPEGTIME_TO_GSTTIME (mpeg_parse->current_scr +
      mpeg_parse->adjust);
  mpeg_parse->scr_pending = FALSE;

  if (mpeg_parse->next_scr == MP_INVALID_SCR) {
    mpeg_parse->next_scr = mpeg_parse->current_scr;
  }

  if ((mpeg_parse->first_scr == MP_INVALID_SCR) ||
      (mpeg_parse->current_scr < mpeg_parse->first_scr)) {
    mpeg_parse->first_scr = mpeg_parse->current_scr;
    mpeg_parse->first_scr_pos = gst_mpeg_packetize_tell (mpeg_parse->packetize);
  }

  if ((mpeg_parse->last_scr == MP_INVALID_SCR) ||
      (mpeg_parse->current_scr > mpeg_parse->last_scr)) {
    mpeg_parse->last_scr = mpeg_parse->current_scr;
    mpeg_parse->last_scr_pos = gst_mpeg_packetize_tell (mpeg_parse->packetize);
  }

  GST_LOG_OBJECT (mpeg_parse,
      "SCR is %" G_GUINT64_FORMAT
      " (%" G_GUINT64_FORMAT ") next: %"
      G_GINT64_FORMAT " (%" G_GINT64_FORMAT
      ") diff: %" G_GINT64_FORMAT " (%"
      G_GINT64_FORMAT ")",
      mpeg_parse->current_scr,
      MPEGTIME_TO_GSTTIME (mpeg_parse->current_scr),
      mpeg_parse->next_scr,
      MPEGTIME_TO_GSTTIME (mpeg_parse->next_scr),
      mpeg_parse->current_scr - mpeg_parse->next_scr,
      MPEGTIME_TO_GSTTIME (mpeg_parse->current_scr) -
      MPEGTIME_TO_GSTTIME (mpeg_parse->next_scr));

  /* watch out for integer overflows... */
  if (mpeg_parse->next_scr > scr) {
    diff = mpeg_parse->next_scr - scr;
  } else {
    diff = scr - mpeg_parse->next_scr;
  }

  if (diff > mpeg_parse->max_discont) {
    GST_DEBUG ("discontinuity detected; expected: %" G_GUINT64_FORMAT " got: %"
        G_GUINT64_FORMAT " adjusted:%" G_GINT64_FORMAT " adjust:%"
        G_GINT64_FORMAT, mpeg_parse->next_scr, mpeg_parse->current_scr,
        mpeg_parse->current_scr + mpeg_parse->adjust, mpeg_parse->adjust);

    if (mpeg_parse->do_adjust) {
      if (mpeg_parse->use_adjust) {
        mpeg_parse->adjust +=
            (gint64) mpeg_parse->next_scr - (gint64) mpeg_parse->current_scr;
        GST_DEBUG ("new adjust: %" G_GINT64_FORMAT, mpeg_parse->adjust);
      }
    } else {
      mpeg_parse->discont_pending = TRUE;
    }
  }

  mpeg_parse->current_ts = MPEGTIME_TO_GSTTIME (mpeg_parse->current_scr +
      mpeg_parse->adjust);
  offset = gst_mpeg_packetize_tell (mpeg_parse->packetize);
  if (offset > mpeg_parse->byte_offset) {
    /* we have reached the wanted offset so emit the signal. */
    g_signal_emit (G_OBJECT (mpeg_parse),
        gst_mpeg_parse_signals[SIGNAL_REACHED_OFFSET], 0);
  }

  if (mpeg_parse->index && GST_INDEX_IS_WRITABLE (mpeg_parse->index)) {
    /* update index if any */
    gst_index_add_association (mpeg_parse->index, mpeg_parse->index_id,
        GST_ASSOCIATION_FLAG_KEY_UNIT,
        GST_FORMAT_BYTES, GST_BUFFER_OFFSET (buffer),
        GST_FORMAT_TIME, MPEGTIME_TO_GSTTIME (mpeg_parse->current_scr), 0);
  }

  if ((mpeg_parse->current_scr > prev_scr) && (diff < mpeg_parse->max_discont)) {
    mpeg_parse->avg_bitrate_time +=
        MPEGTIME_TO_GSTTIME (mpeg_parse->current_scr - prev_scr);
    mpeg_parse->avg_bitrate_bytes += mpeg_parse->bytes_since_scr;
  }

  if (mpeg_parse->mux_rate != new_rate) {
    if (GST_MPEG_PACKETIZE_IS_MPEG2 (mpeg_parse->packetize)) {
      mpeg_parse->mux_rate = new_rate;
    } else if (mpeg_parse->avg_bitrate_bytes > MP_MIN_VALID_BSS) {
      mpeg_parse->mux_rate =
          GST_SECOND * mpeg_parse->avg_bitrate_bytes /
          mpeg_parse->avg_bitrate_time;
    }
    //gst_mpeg_parse_update_streaminfo (mpeg_parse);
    GST_DEBUG ("stream current is %1.3fMbs, calculated over %1.3fkB",
        (mpeg_parse->mux_rate * 8) / 1048576.0,
        mpeg_parse->bytes_since_scr / 1024.0);
  }

  if (mpeg_parse->avg_bitrate_bytes) {
    GST_DEBUG ("stream avg is %1.3fMbs, calculated over %1.3fkB",
        (float) (mpeg_parse->avg_bitrate_bytes) * 8 * GST_SECOND
        / mpeg_parse->avg_bitrate_time / 1048576.0,
        mpeg_parse->avg_bitrate_bytes / 1024.0);
  }

  if (mpeg_parse->avg_bitrate_bytes > MP_MAX_VALID_BSS) {
    mpeg_parse->avg_bitrate_bytes = 0;
    mpeg_parse->avg_bitrate_time = 0;
  }
  mpeg_parse->bytes_since_scr = 0;

  return TRUE;
}

static gboolean
gst_mpeg_parse_event (GstPad * pad, GstEvent * event)
{
  GstMPEGParse *mpeg_parse = GST_MPEG_PARSE (gst_pad_get_parent (pad));
  GstClockTime time;
  gboolean ret = FALSE;

  time = MPEGTIME_TO_GSTTIME (mpeg_parse->current_scr);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_NEWSEGMENT:
      if (CLASS (mpeg_parse)->handle_discont)
        ret = CLASS (mpeg_parse)->handle_discont (mpeg_parse, event);
      else
        gst_event_unref (event);
      break;
    default:
      if (CLASS (mpeg_parse)->process_event)
        ret = CLASS (mpeg_parse)->process_event (mpeg_parse, event, time);
      else
        gst_event_unref (event);
      break;
  }

  gst_object_unref (mpeg_parse);
  return ret;
}

static GstFlowReturn
gst_mpeg_parse_chain (GstPad * pad, GstBuffer * buffer)
{
  GstMPEGParse *mpeg_parse = GST_MPEG_PARSE (gst_pad_get_parent (pad));
  GstFlowReturn result = GST_FLOW_ERROR;
  guint id;
  gboolean mpeg2;
  GstClockTime time;
  guint64 size;

  if (!gst_mpeg_packetize_put (mpeg_parse->packetize, buffer)) {
    gst_buffer_unref (buffer);
    goto done;
  }

  while (1) {
    result = gst_mpeg_packetize_read (mpeg_parse->packetize, &buffer);
    if (result == GST_FLOW_RESEND) {
      /* there was not enough data in packetizer cache */
      result = GST_FLOW_OK;
      goto done;
    }
    if (result != GST_FLOW_OK)
      goto done;

    id = GST_MPEG_PACKETIZE_ID (mpeg_parse->packetize);
    mpeg2 = GST_MPEG_PACKETIZE_IS_MPEG2 (mpeg_parse->packetize);

    GST_LOG_OBJECT (mpeg_parse, "have chunk 0x%02X", id);

    switch (id) {
      case 0xb9:
        break;
      case 0xba:
        if (CLASS (mpeg_parse)->parse_packhead) {
          CLASS (mpeg_parse)->parse_packhead (mpeg_parse, buffer);
        }
        break;
      case 0xbb:
        if (CLASS (mpeg_parse)->parse_syshead) {
          CLASS (mpeg_parse)->parse_syshead (mpeg_parse, buffer);
        }
        break;
      default:
        if (mpeg2 && ((id < 0xBD) || (id > 0xFE))) {
          g_warning ("******** unknown id 0x%02X", id);
        } else {
          if (mpeg2) {
            if (CLASS (mpeg_parse)->parse_pes) {
              result = CLASS (mpeg_parse)->parse_pes (mpeg_parse, buffer);
            }
          } else {
            if (CLASS (mpeg_parse)->parse_packet) {
              result = CLASS (mpeg_parse)->parse_packet (mpeg_parse, buffer);
            }
          }
        }
    }

    time = MPEGTIME_TO_GSTTIME (mpeg_parse->current_scr);

    /* we're not sending data as long as no new SCR was found */
    if (mpeg_parse->discont_pending) {
      if (!mpeg_parse->scr_pending) {
#if 0
        if (mpeg_parse->clock && mpeg_parse->sync) {
          gst_element_set_time (GST_ELEMENT (mpeg_parse),
              MPEGTIME_TO_GSTTIME (mpeg_parse->current_scr));
        }
#endif
        if (CLASS (mpeg_parse)->send_discont) {
          CLASS (mpeg_parse)->send_discont (mpeg_parse,
              MPEGTIME_TO_GSTTIME (mpeg_parse->current_scr +
                  mpeg_parse->adjust));
        }
        mpeg_parse->discont_pending = FALSE;
      } else {
        GST_DEBUG ("waiting for SCR");
        gst_buffer_unref (buffer);
        result = GST_FLOW_OK;
        goto done;
      }
    }

    size = GST_BUFFER_SIZE (buffer);
    mpeg_parse->bytes_since_scr += size;

    if (!GST_PAD_CAPS (mpeg_parse->sinkpad)) {
      gboolean mpeg2 = GST_MPEG_PACKETIZE_IS_MPEG2 (mpeg_parse->packetize);

      if (!gst_pad_set_caps (mpeg_parse->sinkpad,
              gst_caps_new_simple ("video/mpeg",
                  "mpegversion", G_TYPE_INT, (mpeg2 ? 2 : 1),
                  "systemstream", G_TYPE_BOOLEAN, TRUE,
                  "parsed", G_TYPE_BOOLEAN, TRUE, NULL)) < 0) {
        GST_ELEMENT_ERROR (mpeg_parse, CORE, NEGOTIATION, (NULL), (NULL));
        gst_buffer_unref (buffer);
        result = GST_FLOW_ERROR;
        goto done;
      }
    }

    if (CLASS (mpeg_parse)->send_buffer)
      result = CLASS (mpeg_parse)->send_buffer (mpeg_parse, buffer, time);

#if 0
    if (mpeg_parse->clock && mpeg_parse->sync && !mpeg_parse->discont_pending) {
      GST_DEBUG ("syncing mpegparse");
      gst_element_wait (GST_ELEMENT (mpeg_parse), time);
    }
#endif

    if (mpeg_parse->current_scr != MP_INVALID_SCR) {
      guint64 scr, bss, br;

      scr = mpeg_parse->current_scr;
      bss = mpeg_parse->bytes_since_scr;
      if (mpeg_parse->scr_rate != 0)
        br = mpeg_parse->scr_rate;
      else
        br = mpeg_parse->mux_rate;

      if (br) {
        if (GST_MPEG_PACKETIZE_IS_MPEG2 (mpeg_parse->packetize)) {
          /* 
           * The mpeg spec says something like this, but that doesn't really work:
           *
           * mpeg_parse->next_scr = (scr * br + bss * CLOCK_FREQ) / (CLOCK_FREQ + br);
           */

          mpeg_parse->next_scr = scr + (bss * CLOCK_FREQ) / br;
        } else {
          /* we are interpolating the scr here */
          mpeg_parse->next_scr = scr + (bss * CLOCK_FREQ) / br;
        }
      } else {
        /* no bitrate known */
        mpeg_parse->next_scr = scr;
      }

      GST_LOG_OBJECT (mpeg_parse, "size: %" G_GINT64_FORMAT
          ", total since SCR: %" G_GINT64_FORMAT ", br: %" G_GINT64_FORMAT
          ", next SCR: %" G_GINT64_FORMAT, size, bss, br, mpeg_parse->next_scr);
    }

    if (result != GST_FLOW_OK && result != GST_FLOW_NOT_LINKED) {
      gst_buffer_unref (buffer);
      goto done;
    }
  }

done:
  gst_object_unref (mpeg_parse);

  if (result == GST_FLOW_NOT_LINKED)
    result = GST_FLOW_OK;

  return result;
}

#if 0
const GstFormat *
gst_mpeg_parse_get_src_formats (GstPad * pad)
{
  static const GstFormat formats[] = {
    GST_FORMAT_BYTES,
    GST_FORMAT_TIME,
    0
  };

  return formats;
}

/*
 * Return the bitrate to the nearest byte/sec
 */
static gboolean
gst_mpeg_parse_get_rate (GstMPEGParse * mpeg_parse, gint64 * rate)
{
  GstFormat time_format = GST_FORMAT_TIME;
  GstFormat bytes_format = GST_FORMAT_BYTES;
  gint64 total_time = 0;
  gint64 total_bytes = 0;

  /* If upstream knows the total time and the total bytes,
   * use those to compute an average byterate
   */
  if (gst_pad_query (GST_PAD_PEER (mpeg_parse->sinkpad),
          GST_QUERY_TOTAL, &time_format, &total_time)
      &&
      gst_pad_query (GST_PAD_PEER (mpeg_parse->sinkpad),
          GST_QUERY_TOTAL, &bytes_format, &total_bytes)
      && total_time != 0 && total_bytes != 0) {
    /* Use the funny calculation to avoid overflow of 64 bits */
    *rate =
        ((total_bytes * GST_USECOND) / total_time) * (GST_SECOND / GST_USECOND);

    if (*rate > 0)
      return TRUE;
  }
  *rate = 0;

  if ((mpeg_parse->first_scr != MP_INVALID_SCR) &&
      (mpeg_parse->last_scr != MP_INVALID_SCR) &&
      (mpeg_parse->last_scr_pos - mpeg_parse->first_scr_pos > MP_MIN_VALID_BSS)
      && (mpeg_parse->last_scr != mpeg_parse->first_scr)) {
    *rate =
        GST_SECOND * (mpeg_parse->last_scr_pos -
        mpeg_parse->first_scr_pos) / MPEGTIME_TO_GSTTIME (mpeg_parse->last_scr -
        mpeg_parse->first_scr);
  }

  if (*rate == 0 && mpeg_parse->avg_bitrate_time != 0
      && mpeg_parse->avg_bitrate_bytes > MP_MIN_VALID_BSS) {
    *rate =
        GST_SECOND * mpeg_parse->avg_bitrate_bytes /
        mpeg_parse->avg_bitrate_time;
  }

  if (*rate != 0) {
    /*
     * check if we need to update scr_rate
     */
    if ((mpeg_parse->scr_rate == 0) ||
        (((double) (ABS (mpeg_parse->scr_rate - *rate)) / mpeg_parse->scr_rate)
            >= MP_SCR_RATE_HYST)) {
      mpeg_parse->scr_rate = *rate;
      return TRUE;
    }
  }

  if (mpeg_parse->scr_rate != 0) {
    *rate = mpeg_parse->scr_rate;
    return TRUE;
  }

  if (mpeg_parse->mux_rate != 0) {
    *rate = mpeg_parse->mux_rate;
    return TRUE;
  }

  return FALSE;
}

gboolean
gst_mpeg_parse_convert_src (GstPad * pad, GstFormat src_format,
    gint64 src_value, GstFormat * dest_format, gint64 * dest_value)
{
  gboolean res = TRUE;
  GstMPEGParse *mpeg_parse = GST_MPEG_PARSE (gst_pad_get_parent (pad));
  gint64 rate;


  switch (src_format) {
    case GST_FORMAT_BYTES:
      switch (*dest_format) {
        case GST_FORMAT_DEFAULT:
          *dest_format = GST_FORMAT_TIME;
        case GST_FORMAT_TIME:
          if (!gst_mpeg_parse_get_rate (mpeg_parse, &rate))
            res = FALSE;
          else {
            *dest_value = GST_SECOND * src_value / rate;
          }
          break;
        default:
          res = FALSE;
      }
      break;
    case GST_FORMAT_TIME:
      switch (*dest_format) {
        case GST_FORMAT_DEFAULT:
          *dest_format = GST_FORMAT_BYTES;
        case GST_FORMAT_BYTES:
          if (!gst_mpeg_parse_get_rate (mpeg_parse, &rate))
            res = FALSE;
          else {
            *dest_value = src_value * rate / GST_SECOND;
          }
          break;
        default:
          res = FALSE;
      }
      break;
    default:
      res = FALSE;
      break;
  }

  return res;
}

#if 0
const GstQueryType *
gst_mpeg_parse_get_src_query_types (GstPad * pad)
{
  static const GstQueryType types[] = {
    GST_QUERY_TOTAL,
    GST_QUERY_POSITION,
    0
  };

  return types;
}
#endif

gboolean
gst_mpeg_parse_handle_src_query (GstPad * pad, GstQueryType type,
    GstFormat * format, gint64 * value)
{
  gboolean res = TRUE;
  GstMPEGParse *mpeg_parse = GST_MPEG_PARSE (gst_pad_get_parent (pad));
  GstFormat src_format = GST_FORMAT_UNDEFINED;
  gint64 src_value = 0;

  switch (type) {
    case GST_QUERY_TOTAL:
    {
      switch (*format) {
        case GST_FORMAT_DEFAULT:
          *format = GST_FORMAT_TIME;
          /* fallthrough */
        case GST_FORMAT_TIME:
          /*
           * Try asking upstream if it knows the time - a DVD might know
           */
          src_format = GST_FORMAT_TIME;
          if (gst_pad_query (GST_PAD_PEER (mpeg_parse->sinkpad),
                  GST_QUERY_TOTAL, &src_format, &src_value)) {
            res = TRUE;
            break;
          }
          /* Otherwise fallthrough */
        default:
          src_format = GST_FORMAT_BYTES;
          if (!gst_pad_query (GST_PAD_PEER (mpeg_parse->sinkpad),
                  GST_QUERY_TOTAL, &src_format, &src_value)) {
            res = FALSE;
          }
          break;
      }
      break;
    }
    case GST_QUERY_POSITION:
    {
      switch (*format) {
        case GST_FORMAT_DEFAULT:
          *format = GST_FORMAT_TIME;
          /* fallthrough */
        default:
          src_format = GST_FORMAT_TIME;
          if ((mpeg_parse->current_scr == MP_INVALID_SCR) ||
              (mpeg_parse->first_scr == MP_INVALID_SCR))
            res = FALSE;
          else {
            gint64 cur =
                (gint64) (mpeg_parse->current_scr) - mpeg_parse->first_scr;
            src_value = MPEGTIME_TO_GSTTIME (MAX (0, cur));
          }
          break;
      }
      break;
    }
    default:
      res = FALSE;
      break;
  }

  /* bring to requested format */
  if (res)
    res = gst_pad_convert (pad, src_format, src_value, format, value);

  return res;
}

const GstEventMask *
gst_mpeg_parse_get_src_event_masks (GstPad * pad)
{
  static const GstEventMask masks[] = {
    {GST_EVENT_SEEK, GST_SEEK_METHOD_SET | GST_SEEK_FLAG_FLUSH},
    {0,}
  };

  return masks;
}

static gboolean
index_seek (GstPad * pad, GstEvent * event, gint64 * offset, gint64 * scr)
{
  GstIndexEntry *entry;
  GstMPEGParse *mpeg_parse = GST_MPEG_PARSE (gst_pad_get_parent (pad));

  entry = gst_index_get_assoc_entry (mpeg_parse->index, mpeg_parse->index_id,
      GST_INDEX_LOOKUP_BEFORE, 0,
      GST_EVENT_SEEK_FORMAT (event), GST_EVENT_SEEK_OFFSET (event));
  if (!entry)
    return FALSE;

  if (gst_index_entry_assoc_map (entry, GST_FORMAT_BYTES, offset)) {
    gint64 time;

    if (gst_index_entry_assoc_map (entry, GST_FORMAT_TIME, &time)) {
      *scr = GSTTIME_TO_MPEGTIME (time);
    }
    GST_CAT_DEBUG (GST_CAT_SEEK, "%s:%s index %s %" G_GINT64_FORMAT
        " -> %" G_GINT64_FORMAT " bytes, scr=%" G_GINT64_FORMAT,
        GST_DEBUG_PAD_NAME (pad),
        gst_format_get_details (GST_EVENT_SEEK_FORMAT (event))->nick,
        GST_EVENT_SEEK_OFFSET (event), *offset, *scr);
    return TRUE;
  }
  return FALSE;
}

static gboolean
normal_seek (GstPad * pad, GstEvent * event, gint64 * offset, gint64 * scr)
{
  gboolean res;
  GstFormat format;
  gint64 time;

  /* bring offset to bytes */
  format = GST_FORMAT_BYTES;
  res = gst_pad_convert (pad,
      GST_EVENT_SEEK_FORMAT (event),
      GST_EVENT_SEEK_OFFSET (event), &format, offset);
  /* bring offset to time */
  format = GST_FORMAT_TIME;
  res &= gst_pad_convert (pad,
      GST_EVENT_SEEK_FORMAT (event),
      GST_EVENT_SEEK_OFFSET (event), &format, &time);

  /* convert to scr */
  *scr = GSTTIME_TO_MPEGTIME (time);

  return res;
}
#endif

gboolean
gst_mpeg_parse_handle_src_event (GstPad * pad, GstEvent * event)
{
  gboolean res = FALSE;

#if 0
  GstMPEGParse *mpeg_parse = GST_MPEG_PARSE (gst_pad_get_parent (pad));
#endif

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
    {
#if 0
      /* FIXME: port the seeking to gstreamer 0.9 */
      gint64 desired_offset;
      gint64 expected_scr = 0;

      /* first to to use the index if we have one */
      if (mpeg_parse->index)
        res = index_seek (pad, event, &desired_offset, &expected_scr);
      /* nothing found, try fuzzy seek */
      if (!res)
        res = normal_seek (pad, event, &desired_offset, &expected_scr);

      if (!res)
        break;

      GST_DEBUG ("from pad %s: sending seek to %" G_GINT64_FORMAT
          " expected SCR: %" G_GUINT64_FORMAT " (%" G_GUINT64_FORMAT ")",
          gst_object_get_name (GST_OBJECT (pad)), desired_offset, expected_scr,
          MPEGTIME_TO_GSTTIME (expected_scr));

      if (gst_bytestream_seek (mpeg_parse->packetize->bs, desired_offset,
              GST_SEEK_METHOD_SET)) {
        mpeg_parse->discont_pending = TRUE;
        mpeg_parse->scr_pending = TRUE;
        mpeg_parse->next_scr = expected_scr;
        mpeg_parse->current_scr = MP_INVALID_SCR;
        mpeg_parse->current_ts = GST_CLOCK_TIME_NONE;
        mpeg_parse->adjust = 0;
        res = TRUE;
      }
#endif
      break;
    }
    default:
      break;
  }
  gst_event_unref (event);
  return res;
}

static GstStateChangeReturn
gst_mpeg_parse_change_state (GstElement * element, GstStateChange transition)
{
  GstMPEGParse *mpeg_parse = GST_MPEG_PARSE (element);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      if (!mpeg_parse->packetize) {
        mpeg_parse->packetize =
            gst_mpeg_packetize_new (mpeg_parse->srcpad,
            GST_MPEG_PACKETIZE_SYSTEM);
      }
      /* initialize parser state */
      gst_mpeg_parse_reset (mpeg_parse);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      if (mpeg_parse->packetize) {
        gst_mpeg_packetize_destroy (mpeg_parse->packetize);
        mpeg_parse->packetize = NULL;
      }
      //gst_caps_replace (&mpeg_parse->streaminfo, NULL);
      break;
    default:
      break;
  }

  return GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
}

static void
gst_mpeg_parse_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstMPEGParse *mpeg_parse;

  mpeg_parse = GST_MPEG_PARSE (object);

  switch (prop_id) {
    case ARG_SYNC:
      g_value_set_boolean (value, mpeg_parse->sync);
      break;
    case ARG_MAX_DISCONT:
      g_value_set_int (value, mpeg_parse->max_discont);
      break;
    case ARG_DO_ADJUST:
      g_value_set_boolean (value, mpeg_parse->do_adjust);
      break;
    case ARG_BYTE_OFFSET:
      g_value_set_uint64 (value, mpeg_parse->byte_offset);
      break;
    case ARG_TIME_OFFSET:
      g_value_set_uint64 (value, mpeg_parse->current_ts);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_mpeg_parse_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstMPEGParse *mpeg_parse;

  mpeg_parse = GST_MPEG_PARSE (object);

  switch (prop_id) {
    case ARG_SYNC:
      mpeg_parse->sync = g_value_get_boolean (value);
      break;
    case ARG_MAX_DISCONT:
      mpeg_parse->max_discont = g_value_get_int (value);
      break;
    case ARG_DO_ADJUST:
      mpeg_parse->do_adjust = g_value_get_boolean (value);
      mpeg_parse->adjust = 0;
      break;
    case ARG_BYTE_OFFSET:
      mpeg_parse->byte_offset = g_value_get_uint64 (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_mpeg_parse_set_index (GstElement * element, GstIndex * index)
{
  GstMPEGParse *mpeg_parse;

  mpeg_parse = GST_MPEG_PARSE (element);

  mpeg_parse->index = index;

  gst_index_get_writer_id (index, GST_OBJECT (mpeg_parse->sinkpad),
      &mpeg_parse->index_id);
  gst_index_add_format (index, mpeg_parse->index_id, scr_format);
}

static GstIndex *
gst_mpeg_parse_get_index (GstElement * element)
{
  GstMPEGParse *mpeg_parse;

  mpeg_parse = GST_MPEG_PARSE (element);

  return mpeg_parse->index;
}

gboolean
gst_mpeg_parse_plugin_init (GstPlugin * plugin)
{
  scr_format =
      gst_format_register ("scr", "The MPEG system clock reference time");

  return gst_element_register (plugin, "mpegparse",
      GST_RANK_NONE, GST_TYPE_MPEG_PARSE);
}
