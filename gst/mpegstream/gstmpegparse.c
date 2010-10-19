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

#define CLASS(o)        GST_MPEG_PARSE_CLASS (G_OBJECT_GET_CLASS (o))

#define DEFAULT_MAX_SCR_GAP     120000

/* GstMPEGParse signals and args */
enum
{
  SIGNAL_REACHED_OFFSET,
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_MAX_SCR_GAP,
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

#define _do_init(bla) \
    GST_DEBUG_CATEGORY_INIT (gstmpegparse_debug, "mpegparse", 0, \
        "MPEG parser element");

GST_BOILERPLATE_FULL (GstMPEGParse, gst_mpeg_parse, GstElement,
    GST_TYPE_ELEMENT, _do_init);

static GstStateChangeReturn gst_mpeg_parse_change_state (GstElement * element,
    GstStateChange transition);

static gboolean gst_mpeg_parse_parse_packhead (GstMPEGParse * mpeg_parse,
    GstBuffer * buffer);

static void gst_mpeg_parse_reset (GstMPEGParse * mpeg_parse);

static GstClockTime gst_mpeg_parse_adjust_ts (GstMPEGParse * mpeg_parse,
    GstClockTime ts);

static GstFlowReturn gst_mpeg_parse_send_buffer (GstMPEGParse * mpeg_parse,
    GstBuffer * buffer, GstClockTime time);
static gboolean gst_mpeg_parse_process_event (GstMPEGParse * mpeg_parse,
    GstEvent * event);
static gboolean gst_mpeg_parse_send_event (GstMPEGParse * mpeg_parse,
    GstEvent * event);

static void gst_mpeg_parse_pad_added (GstElement * element, GstPad * pad);

static gboolean gst_mpeg_parse_event (GstPad * pad, GstEvent * event);
static GstFlowReturn gst_mpeg_parse_chain (GstPad * pad, GstBuffer * buf);

static void gst_mpeg_parse_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_mpeg_parse_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_mpeg_parse_set_index (GstElement * element, GstIndex * index);
static GstIndex *gst_mpeg_parse_get_index (GstElement * element);

static guint gst_mpeg_parse_signals[LAST_SIGNAL] = { 0 };

static void
gst_mpeg_parse_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_set_details_simple (element_class, "MPEG System Parser",
      "Codec/Parser",
      "Parses MPEG1 and MPEG2 System Streams",
      "Erik Walthinsen <omega@cse.ogi.edu>, Wim Taymans <wim.taymans@chello.be>");
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
  gstelement_class->get_index = gst_mpeg_parse_get_index;
  gstelement_class->set_index = gst_mpeg_parse_set_index;

  klass->parse_packhead = gst_mpeg_parse_parse_packhead;
  klass->parse_syshead = NULL;
  klass->parse_packet = NULL;
  klass->parse_pes = NULL;
  klass->adjust_ts = gst_mpeg_parse_adjust_ts;
  klass->send_buffer = gst_mpeg_parse_send_buffer;
  klass->process_event = gst_mpeg_parse_process_event;
  klass->send_event = gst_mpeg_parse_send_event;

  /* FIXME: this is a hack.  We add the pad templates here instead
   * in the base_init function, since the derived class (mpegdemux)
   * uses different pads.  IMO, this is wrong. */
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&src_factory));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&sink_factory));

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_MAX_SCR_GAP,
      g_param_spec_int ("max-scr-gap", "Max SCR gap",
          "Maximum allowed gap between expected and actual "
          "SCR values. -1 means never adjust.", -1, G_MAXINT,
          DEFAULT_MAX_SCR_GAP, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_BYTE_OFFSET,
      g_param_spec_uint64 ("byte-offset", "Byte Offset",
          "Emit reached-offset signal when the byte offset is reached.",
          0, G_MAXUINT64, G_MAXUINT64,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_TIME_OFFSET,
      g_param_spec_uint64 ("time-offset", "Time Offset",
          "Time offset in the stream.",
          0, G_MAXUINT64, G_MAXUINT64,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
}

static void
gst_mpeg_parse_init (GstMPEGParse * mpeg_parse, GstMPEGParseClass * klass)
{
  GstElementClass *gstelement_class;
  GstPadTemplate *templ;

  gstelement_class = GST_ELEMENT_GET_CLASS (mpeg_parse);

  mpeg_parse->packetize = NULL;

  mpeg_parse->max_scr_gap = DEFAULT_MAX_SCR_GAP;

  mpeg_parse->byte_offset = G_MAXUINT64;

  gst_mpeg_parse_reset (mpeg_parse);

  templ = gst_element_class_get_pad_template (gstelement_class, "sink");
  mpeg_parse->sinkpad = gst_pad_new_from_template (templ, "sink");
  gst_element_add_pad (GST_ELEMENT (mpeg_parse), mpeg_parse->sinkpad);

  if ((templ = gst_element_class_get_pad_template (gstelement_class, "src"))) {
    mpeg_parse->srcpad = gst_pad_new_from_template (templ, "src");
    gst_element_add_pad (GST_ELEMENT (mpeg_parse), mpeg_parse->srcpad);
    gst_pad_set_event_function (mpeg_parse->srcpad,
        GST_DEBUG_FUNCPTR (gst_mpeg_parse_handle_src_event));
    gst_pad_set_query_type_function (mpeg_parse->srcpad,
        GST_DEBUG_FUNCPTR (gst_mpeg_parse_get_src_query_types));
    gst_pad_set_query_function (mpeg_parse->srcpad,
        GST_DEBUG_FUNCPTR (gst_mpeg_parse_handle_src_query));
    gst_pad_use_fixed_caps (mpeg_parse->srcpad);
  }

  gst_pad_set_event_function (mpeg_parse->sinkpad,
      GST_DEBUG_FUNCPTR (gst_mpeg_parse_event));
  gst_pad_set_chain_function (mpeg_parse->sinkpad,
      GST_DEBUG_FUNCPTR (gst_mpeg_parse_chain));
}

#ifdef FIXME
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
  GST_DEBUG_OBJECT (mpeg_parse, "Resetting mpeg_parse");

  mpeg_parse->first_scr = MP_INVALID_SCR;
  mpeg_parse->first_scr_pos = 0;
  mpeg_parse->last_scr = MP_INVALID_SCR;
  mpeg_parse->last_scr_pos = 0;
  mpeg_parse->scr_rate = 0;

  mpeg_parse->avg_bitrate_time = 0;
  mpeg_parse->avg_bitrate_bytes = 0;

  mpeg_parse->mux_rate = 0;
  mpeg_parse->current_scr = MP_INVALID_SCR;
  mpeg_parse->next_scr = 0;
  mpeg_parse->bytes_since_scr = 0;

  mpeg_parse->current_ts = 0;

  mpeg_parse->do_adjust = TRUE;
  mpeg_parse->pending_newsegment = TRUE;
  mpeg_parse->adjust = 0;

  /* Initialize the current segment. */
  GST_DEBUG_OBJECT (mpeg_parse, "Resetting current segment");
  gst_segment_init (&mpeg_parse->current_segment, GST_FORMAT_TIME);
}

static GstClockTime
gst_mpeg_parse_adjust_ts (GstMPEGParse * mpeg_parse, GstClockTime ts)
{
  if (!GST_CLOCK_TIME_IS_VALID (ts)) {
    return GST_CLOCK_TIME_NONE;
  }

  if (mpeg_parse->do_adjust) {
    /* Close the SCR gaps. */
    return ts + MPEGTIME_TO_GSTTIME (mpeg_parse->adjust);
  } else {
    if (ts >= mpeg_parse->current_segment.start) {
      return ts;
    } else {
      /* The timestamp lies outside the current segment. Return an
         invalid timestamp instead. */
      return GST_CLOCK_TIME_NONE;
    }
  }
}

static GstFlowReturn
gst_mpeg_parse_send_buffer (GstMPEGParse * mpeg_parse, GstBuffer * buffer,
    GstClockTime time)
{
  GstFlowReturn result = GST_FLOW_OK;

  if (!GST_PAD_CAPS (mpeg_parse->srcpad)) {
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
      gst_buffer_unref (buffer);
      return GST_FLOW_ERROR;
    }
    gst_caps_unref (caps);
  }

  GST_BUFFER_TIMESTAMP (buffer) = time;
  GST_DEBUG_OBJECT (mpeg_parse, "current buffer time: %" GST_TIME_FORMAT,
      GST_TIME_ARGS (time));

  gst_buffer_set_caps (buffer, GST_PAD_CAPS (mpeg_parse->srcpad));
  result = gst_pad_push (mpeg_parse->srcpad, buffer);

  return result;
}

static gboolean
gst_mpeg_parse_process_event (GstMPEGParse * mpeg_parse, GstEvent * event)
{
  gboolean ret = FALSE;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_NEWSEGMENT:
    {
      gboolean update;
      gdouble rate;
      GstFormat format;
      gint64 start, stop, time;

      gst_event_parse_new_segment (event, &update, &rate, &format,
          &start, &stop, &time);

      if (format == GST_FORMAT_TIME && (GST_CLOCK_TIME_IS_VALID (time))) {
        /* We are receiving segments from upstream. Don't try to adjust
           SCR values. */
        mpeg_parse->do_adjust = FALSE;
        mpeg_parse->adjust = 0;

        if (!update && mpeg_parse->current_segment.stop != -1) {
          /* Close the current segment. */
          if (CLASS (mpeg_parse)->send_event) {
            CLASS (mpeg_parse)->send_event (mpeg_parse,
                gst_event_new_new_segment (TRUE,
                    mpeg_parse->current_segment.rate,
                    GST_FORMAT_TIME,
                    mpeg_parse->current_segment.start,
                    mpeg_parse->current_segment.stop,
                    mpeg_parse->current_segment.time));
          }
        }

        /* Update the current segment. */
        GST_DEBUG_OBJECT (mpeg_parse,
            "Updating current segment with newsegment");
        gst_segment_set_newsegment (&mpeg_parse->current_segment,
            update, rate, format, start, stop, time);

        if (!update) {
          /* Send a newsegment event for the new current segment. */
          if (CLASS (mpeg_parse)->send_event) {
            CLASS (mpeg_parse)->send_event (mpeg_parse,
                gst_event_new_new_segment (FALSE, rate, GST_FORMAT_TIME,
                    start, stop, time));
            mpeg_parse->pending_newsegment = FALSE;
          }
        }
      } else if (format != GST_FORMAT_TIME && !update) {
        GST_DEBUG_OBJECT (mpeg_parse,
            "Received non-time newsegment from stream");
        mpeg_parse->do_adjust = TRUE;
        mpeg_parse->adjust = 0;
        mpeg_parse->pending_newsegment = TRUE;
      }
      mpeg_parse->packetize->resync = TRUE;

      gst_event_unref (event);

      ret = TRUE;
      break;
    }
    case GST_EVENT_FLUSH_STOP:{
      /* Forward the event. */
      if (CLASS (mpeg_parse)->send_event) {
        ret = CLASS (mpeg_parse)->send_event (mpeg_parse, event);
      } else {
        gst_event_unref (event);
      }

      /* Reset the internal fields. */
      gst_mpeg_parse_reset (mpeg_parse);
      gst_mpeg_packetize_flush_cache (mpeg_parse->packetize);
      break;
    }
    case GST_EVENT_EOS:{
      GST_DEBUG_OBJECT (mpeg_parse, "EOS");

      if (CLASS (mpeg_parse)->send_event) {
        ret = CLASS (mpeg_parse)->send_event (mpeg_parse, event);
      } else {
        gst_event_unref (event);
      }
      if (!ret) {
        GST_ELEMENT_ERROR (mpeg_parse, STREAM, DEMUX, (NULL),
            ("Pushing EOS event didn't work on any of the source pads"));
      }
      break;
    }
    default:
      if (CLASS (mpeg_parse)->send_event) {
        ret = CLASS (mpeg_parse)->send_event (mpeg_parse, event);
      } else {
        gst_event_unref (event);
      }
      break;
  }

  return ret;
}

/* returns TRUE if pushing the event worked on at least one pad */
static gboolean
gst_mpeg_parse_send_event (GstMPEGParse * mpeg_parse, GstEvent * event)
{
  GstIterator *it;
  gpointer pad;
  gboolean ret = FALSE;

  /* Send event to all source pads of this element. */
  it = gst_element_iterate_src_pads (GST_ELEMENT (mpeg_parse));
  while (TRUE) {
    switch (gst_iterator_next (it, &pad)) {
      case GST_ITERATOR_OK:
        gst_event_ref (event);
        if (gst_pad_push_event (GST_PAD (pad), event))
          ret = TRUE;
        gst_object_unref (GST_OBJECT (pad));
        break;
      case GST_ITERATOR_RESYNC:
        gst_iterator_resync (it);
        break;
      case GST_ITERATOR_DONE:
        goto done;
      case GST_ITERATOR_ERROR:
        ret = FALSE;
        goto done;
    }
  }

done:
  gst_iterator_free (it);
  gst_event_unref (event);

  return ret;
}

static void
gst_mpeg_parse_pad_added (GstElement * element, GstPad * pad)
{
  GstMPEGParse *mpeg_parse;
  GstEvent *event;

  if (GST_PAD_IS_SINK (pad)) {
    return;
  }

  mpeg_parse = GST_MPEG_PARSE (element);

  /* For each new added pad, send a newsegment event so it knows about
   * the current time. This is required because MPEG allows any sort
   * of order of packets, and new substreams can be found at any
   * time. */
  event = gst_event_new_new_segment (FALSE,
      mpeg_parse->current_segment.rate,
      GST_FORMAT_TIME, mpeg_parse->current_segment.start,
      mpeg_parse->current_segment.stop, mpeg_parse->current_segment.start);

  gst_pad_push_event (pad, event);
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

  scr1 = GST_READ_UINT32_BE (buf);
  scr2 = GST_READ_UINT32_BE (buf + 4);

  /* Extract the SCR and rate values from the header. */
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
    new_rate = (GST_READ_UINT32_BE (buf) & 0xfffffc00) >> 10;
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

  /* Deal with SCR overflow */
  if (mpeg_parse->current_scr != MP_INVALID_SCR) {
    guint32 diff;

    diff = scr - mpeg_parse->current_scr;
    if (diff < 4 * CLOCK_FREQ)
      scr = mpeg_parse->current_scr + diff;
  }


  prev_scr = mpeg_parse->current_scr;
  mpeg_parse->current_scr = scr;

  if (mpeg_parse->do_adjust && mpeg_parse->pending_newsegment) {
    /* Open a new segment. */
    gst_segment_set_newsegment (&mpeg_parse->current_segment,
        FALSE, 1.0, GST_FORMAT_TIME, MPEGTIME_TO_GSTTIME (scr), -1,
        MPEGTIME_TO_GSTTIME (scr));
    CLASS (mpeg_parse)->send_event (mpeg_parse,
        gst_event_new_new_segment (FALSE, mpeg_parse->current_segment.rate,
            GST_FORMAT_TIME, mpeg_parse->current_segment.start, -1,
            mpeg_parse->current_segment.time));

    mpeg_parse->pending_newsegment = FALSE;

    /* The first SCR seen should not lead to timestamp adjustment. */
    mpeg_parse->next_scr = scr;
  }

  if (mpeg_parse->next_scr == MP_INVALID_SCR) {
    mpeg_parse->next_scr = mpeg_parse->current_scr;
  }

  /* Update the first SCR. */
  if ((mpeg_parse->first_scr == MP_INVALID_SCR) ||
      (mpeg_parse->current_scr < mpeg_parse->first_scr)) {
    mpeg_parse->first_scr = mpeg_parse->current_scr;
    mpeg_parse->first_scr_pos = gst_mpeg_packetize_tell (mpeg_parse->packetize);
  }

  /* Update the last SCR. */
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

  /* Watch out for integer overflows... */
  if (mpeg_parse->next_scr > scr) {
    diff = mpeg_parse->next_scr - scr;
  } else {
    diff = scr - mpeg_parse->next_scr;
  }

  if (mpeg_parse->do_adjust && diff > mpeg_parse->max_scr_gap) {
    /* SCR gap found, fix the adjust value. */
    GST_DEBUG_OBJECT (mpeg_parse, "SCR gap detected; expected: %"
        G_GUINT64_FORMAT " got: %" G_GUINT64_FORMAT,
        mpeg_parse->next_scr, mpeg_parse->current_scr);

    mpeg_parse->adjust +=
        (gint64) mpeg_parse->next_scr - (gint64) mpeg_parse->current_scr;
    GST_DEBUG_OBJECT (mpeg_parse, "new adjust: %" G_GINT64_FORMAT,
        mpeg_parse->adjust);
  }

  /* Update the current timestamp. */
  mpeg_parse->current_ts = CLASS (mpeg_parse)->adjust_ts (mpeg_parse,
      MPEGTIME_TO_GSTTIME (mpeg_parse->current_scr));

  /* Check for the reached offset signal. */
  offset = gst_mpeg_packetize_tell (mpeg_parse->packetize);
  if (offset > mpeg_parse->byte_offset) {
    /* we have reached the wanted offset so emit the signal. */
    g_signal_emit (G_OBJECT (mpeg_parse),
        gst_mpeg_parse_signals[SIGNAL_REACHED_OFFSET], 0);
  }

  /* Update index if any. */
  if (mpeg_parse->index && GST_INDEX_IS_WRITABLE (mpeg_parse->index)) {
    gst_index_add_association (mpeg_parse->index, mpeg_parse->index_id,
        GST_ASSOCIATION_FLAG_KEY_UNIT,
        GST_FORMAT_BYTES, GST_BUFFER_OFFSET (buffer),
        GST_FORMAT_TIME, MPEGTIME_TO_GSTTIME (mpeg_parse->current_scr), 0);
  }

  /* Update the calculated average bitrate. */
  if ((mpeg_parse->current_scr > prev_scr) && (diff < mpeg_parse->max_scr_gap)) {
    mpeg_parse->avg_bitrate_time +=
        MPEGTIME_TO_GSTTIME (mpeg_parse->current_scr - prev_scr);
    mpeg_parse->avg_bitrate_bytes += mpeg_parse->bytes_since_scr;
  }

  /* Update the bitrate. */
  if (mpeg_parse->mux_rate != new_rate) {
    if (GST_MPEG_PACKETIZE_IS_MPEG2 (mpeg_parse->packetize)) {
      mpeg_parse->mux_rate = new_rate;
    } else if (mpeg_parse->avg_bitrate_bytes > MP_MIN_VALID_BSS) {
      mpeg_parse->mux_rate =
          GST_SECOND * mpeg_parse->avg_bitrate_bytes /
          mpeg_parse->avg_bitrate_time;
    }
    //gst_mpeg_parse_update_streaminfo (mpeg_parse);
    GST_LOG_OBJECT (mpeg_parse,
        "stream current is %1.3fMbs, calculated over %1.3fkB",
        (mpeg_parse->mux_rate * 8) / 1048576.0,
        gst_guint64_to_gdouble (mpeg_parse->bytes_since_scr) / 1024.0);
  }

  if (mpeg_parse->avg_bitrate_bytes) {
    GST_LOG_OBJECT (mpeg_parse,
        "stream avg is %1.3fMbs, calculated over %1.3fkB",
        gst_guint64_to_gdouble (mpeg_parse->avg_bitrate_bytes) * 8 * GST_SECOND
        / gst_guint64_to_gdouble (mpeg_parse->avg_bitrate_time) / 1048576.0,
        gst_guint64_to_gdouble (mpeg_parse->avg_bitrate_bytes) / 1024.0);
  }

  /* Range-check the calculated average bitrate. */
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
  gboolean ret;
  GstMPEGParse *mpeg_parse = GST_MPEG_PARSE (gst_pad_get_parent (pad));

  ret = CLASS (mpeg_parse)->process_event (mpeg_parse, event);

  gst_object_unref (mpeg_parse);
  return ret;
}

static GstFlowReturn
gst_mpeg_parse_chain (GstPad * pad, GstBuffer * buffer)
{
  GstMPEGParse *mpeg_parse = GST_MPEG_PARSE (GST_PAD_PARENT (pad));
  GstFlowReturn result;
  guint id;
  gboolean mpeg2;
  GstClockTime time;
  guint64 size;

  if (GST_BUFFER_FLAG_IS_SET (buffer, GST_BUFFER_FLAG_DISCONT)) {
    GST_DEBUG_OBJECT (mpeg_parse, "buffer with DISCONT flag set");
    gst_mpeg_packetize_flush_cache (mpeg_parse->packetize);
  }

  gst_mpeg_packetize_put (mpeg_parse->packetize, buffer);
  buffer = NULL;

  do {
    result = gst_mpeg_packetize_read (mpeg_parse->packetize, &buffer);
    if (result == GST_FLOW_RESEND) {
      /* there was not enough data in packetizer cache */
      result = GST_FLOW_OK;
      break;
    }
    if (result != GST_FLOW_OK)
      break;

    id = GST_MPEG_PACKETIZE_ID (mpeg_parse->packetize);
    mpeg2 = GST_MPEG_PACKETIZE_IS_MPEG2 (mpeg_parse->packetize);

    GST_LOG_OBJECT (mpeg_parse, "have chunk 0x%02X", id);

    switch (id) {
      case ISO11172_END_START_CODE:
        break;
      case PACK_START_CODE:
        if (CLASS (mpeg_parse)->parse_packhead) {
          CLASS (mpeg_parse)->parse_packhead (mpeg_parse, buffer);
        }
        break;
      case SYS_HEADER_START_CODE:
        if (CLASS (mpeg_parse)->parse_syshead) {
          CLASS (mpeg_parse)->parse_syshead (mpeg_parse, buffer);
        }
        break;
      default:
        if (mpeg2 && ((id < 0xBD) || (id > 0xFE))) {
          GST_ELEMENT_WARNING (GST_ELEMENT (mpeg_parse),
              STREAM, DEMUX, (NULL), ("Unknown stream id 0x%02X", id));
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

    /* Don't send data as long as no new SCR is found. */
    if (mpeg_parse->current_scr == MP_INVALID_SCR) {
      GST_DEBUG_OBJECT (mpeg_parse, "waiting for SCR");
      gst_buffer_unref (buffer);
      result = GST_FLOW_OK;
      break;
    }

    /* Update the byte count. */
    size = GST_BUFFER_SIZE (buffer);
    mpeg_parse->bytes_since_scr += size;

    /* Make sure the output pad has proper capabilities. */
    if (!GST_PAD_CAPS (mpeg_parse->sinkpad)) {
      gboolean mpeg2 = GST_MPEG_PACKETIZE_IS_MPEG2 (mpeg_parse->packetize);
      GstCaps *caps = gst_caps_new_simple ("video/mpeg",
          "mpegversion", G_TYPE_INT, (mpeg2 ? 2 : 1),
          "systemstream", G_TYPE_BOOLEAN, TRUE,
          "parsed", G_TYPE_BOOLEAN, TRUE, NULL);
      if (!gst_pad_set_caps (mpeg_parse->sinkpad, caps) < 0) {
        GST_ELEMENT_ERROR (mpeg_parse, CORE, NEGOTIATION, (NULL), (NULL));
        gst_buffer_unref (buffer);
        result = GST_FLOW_ERROR;
        gst_caps_unref (caps);
        break;
      }
      gst_caps_unref (caps);
    }

    /* Send the buffer. */
    g_return_val_if_fail (mpeg_parse->current_scr != MP_INVALID_SCR,
        GST_FLOW_OK);
    time = CLASS (mpeg_parse)->adjust_ts (mpeg_parse,
        MPEGTIME_TO_GSTTIME (mpeg_parse->current_scr));

    if (CLASS (mpeg_parse)->send_buffer)
      result = CLASS (mpeg_parse)->send_buffer (mpeg_parse, buffer, time);
    else
      gst_buffer_unref (buffer);

    buffer = NULL;

    /* Calculate the expected next SCR. */
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
           * The mpeg spec says something like this, but that doesn't
           * really work:
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
  } while (result == GST_FLOW_OK);

  if (result != GST_FLOW_OK) {
    GST_DEBUG_OBJECT (mpeg_parse, "flow: %s", gst_flow_get_name (result));
  }

  return result;
}

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
  if (gst_pad_query_duration (GST_PAD_PEER (mpeg_parse->sinkpad),
          &time_format, &total_time)
      &&
      gst_pad_query_duration (GST_PAD_PEER (mpeg_parse->sinkpad),
          &bytes_format, &total_bytes)
      && total_time != 0 && total_bytes != 0) {
    /* Use the funny calculation to avoid overflow of 64 bits */
    *rate = ((total_bytes * GST_USECOND) / total_time) *
        (GST_SECOND / GST_USECOND);

    if (*rate > 0) {
      return TRUE;
    }
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
        ((gst_guint64_to_gdouble (ABS (mpeg_parse->scr_rate -
                        *rate)) / gst_guint64_to_gdouble (mpeg_parse->scr_rate))
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

static gboolean
gst_mpeg_parse_convert (GstMPEGParse * mpeg_parse, GstFormat src_format,
    gint64 src_value, GstFormat * dest_format, gint64 * dest_value)
{
  gboolean res = TRUE;
  gint64 rate;

  if (src_format == *dest_format) {
    *dest_value = src_value;
    return TRUE;
  }

  switch (src_format) {
    case GST_FORMAT_BYTES:
      switch (*dest_format) {
        case GST_FORMAT_DEFAULT:
          *dest_format = GST_FORMAT_TIME;
        case GST_FORMAT_TIME:
          if (!gst_mpeg_parse_get_rate (mpeg_parse, &rate)) {
            res = FALSE;
          } else {
            *dest_value = GST_SECOND * src_value / rate;
          }
          break;
        default:
          res = FALSE;
          break;
      }
      break;
    case GST_FORMAT_TIME:
      switch (*dest_format) {
        case GST_FORMAT_DEFAULT:
          *dest_format = GST_FORMAT_BYTES;
        case GST_FORMAT_BYTES:
          if (!gst_mpeg_parse_get_rate (mpeg_parse, &rate)) {
            res = FALSE;
          } else {
            *dest_value = src_value * rate / GST_SECOND;
          }
          break;
        case GST_FORMAT_TIME:
          break;
        default:
          res = FALSE;
          break;
      }
      break;
    default:
      res = FALSE;
      break;
  }

  return res;
}

const GstQueryType *
gst_mpeg_parse_get_src_query_types (GstPad * pad)
{
  static const GstQueryType types[] = {
    GST_QUERY_DURATION,
    GST_QUERY_POSITION,
    GST_QUERY_CONVERT,
    0
  };

  return types;
}

gboolean
gst_mpeg_parse_handle_src_query (GstPad * pad, GstQuery * query)
{
  gboolean res = TRUE;
  GstMPEGParse *mpeg_parse;
  GstFormat src_format = GST_FORMAT_UNDEFINED, format;
  gint64 src_value = 0, value = -1;

  mpeg_parse = GST_MPEG_PARSE (gst_pad_get_parent (pad));

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_DURATION:{
      gst_query_parse_duration (query, &format, NULL);

      switch (format) {
        case GST_FORMAT_DEFAULT:
          format = GST_FORMAT_TIME;
          /* fallthrough */
        case GST_FORMAT_TIME:
          /* Try asking upstream if it knows the time - a DVD might
             know */
          src_format = GST_FORMAT_TIME;
          if (gst_pad_query_peer_duration (mpeg_parse->sinkpad,
                  &src_format, &src_value)) {
            break;
          }
          /* Otherwise fallthrough */
        default:
          src_format = GST_FORMAT_BYTES;
          if (!gst_pad_query_peer_duration (mpeg_parse->sinkpad,
                  &src_format, &src_value)) {
            res = FALSE;
            goto done;
          }
          break;
      }

      /* Convert the value to the desired format. */
      if ((res = gst_mpeg_parse_convert (mpeg_parse, src_format, src_value,
                  &format, &value)) ||
          (res = gst_pad_query_peer_duration (mpeg_parse->sinkpad,
                  &format, &value))) {
        gst_query_set_duration (query, format, value);
      }
      break;
    }
    case GST_QUERY_POSITION:{
      gint64 cur;

      gst_query_parse_position (query, &format, NULL);

      switch (format) {
        case GST_FORMAT_DEFAULT:
          format = GST_FORMAT_TIME;
          /* Fallthrough. */
        default:
          src_format = GST_FORMAT_TIME;
          if ((mpeg_parse->current_scr == MP_INVALID_SCR) ||
              (mpeg_parse->first_scr == MP_INVALID_SCR)) {
            res = FALSE;
            goto done;
          }

          cur = (gint64) (mpeg_parse->current_scr) - mpeg_parse->first_scr;
          src_value = MPEGTIME_TO_GSTTIME (MAX (0, cur));
          break;
      }

      /* Convert the value to the desired format. */
      if ((res = gst_mpeg_parse_convert (mpeg_parse, src_format, src_value,
                  &format, &value)) ||
          (res = gst_pad_query_peer_position (mpeg_parse->sinkpad,
                  &format, &value))) {
        gst_query_set_position (query, format, value);
      }
      break;
    }
    case GST_QUERY_CONVERT:{
      gst_query_parse_convert (query, &src_format, &src_value, &format, NULL);

      if ((res = gst_mpeg_parse_convert (mpeg_parse, src_format, src_value,
                  &format, &value)) ||
          (res = gst_pad_query_peer_convert (mpeg_parse->sinkpad,
                  src_format, src_value, &format, &value))) {
        gst_query_set_convert (query, src_format, src_value, format, value);
      }
      break;
    }
    default:
      res = gst_pad_query_default (pad, query);
      break;
  }

done:
  gst_object_unref (mpeg_parse);
  return res;
}

#ifdef FIXME
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
#endif

static GstEvent *
normal_seek (GstMPEGParse * mpeg_parse, GstPad * pad, GstEvent * event)
{
  GstEvent *upstream = NULL;
  gint64 offset;
  GstFormat format, conv;
  gint64 cur, stop;
  gdouble rate;
  GstSeekType cur_type, stop_type;
  GstSeekFlags flags;
  gint64 start_position, end_position;

  gst_event_parse_seek (event, &rate, &format, &flags, &cur_type,
      &cur, &stop_type, &stop);

  offset = cur;
  if (offset != -1) {
    GST_LOG_OBJECT (mpeg_parse, "starting conversion of cur");
    /* Bring the format to time on srcpad. */
    conv = GST_FORMAT_TIME;
    if (!gst_pad_query_convert (pad, format, offset, &conv, &start_position)) {
      goto done;
    }
    /* And convert to bytes on sinkpad. */
    conv = GST_FORMAT_BYTES;
    if (!gst_pad_query_convert (mpeg_parse->sinkpad, GST_FORMAT_TIME,
            start_position, &conv, &start_position)) {
      goto done;
    }
    GST_INFO_OBJECT (mpeg_parse,
        "Finished conversion of cur, BYTES cur : %" G_GINT64_FORMAT,
        start_position);
  } else {
    start_position = -1;
  }

  offset = stop;
  if (offset != -1) {
    GST_INFO_OBJECT (mpeg_parse, "starting conversion of stop");
    /* Bring the format to time on srcpad. */
    conv = GST_FORMAT_TIME;
    if (!gst_pad_query_convert (pad, format, offset, &conv, &end_position)) {
      goto done;
    }
    /* And convert to bytes on sinkpad. */
    conv = GST_FORMAT_BYTES;
    if (!gst_pad_query_convert (mpeg_parse->sinkpad, GST_FORMAT_TIME,
            end_position, &conv, &end_position)) {
      goto done;
    }
    GST_INFO_OBJECT (mpeg_parse,
        "Finished conversion of stop, BYTES stop : %" G_GINT64_FORMAT,
        end_position);
  } else {
    end_position = -1;
  }

  upstream = gst_event_new_seek (rate, GST_FORMAT_BYTES, flags,
      cur_type, start_position, stop_type, end_position);

done:
  return upstream;
}

gboolean
gst_mpeg_parse_handle_src_event (GstPad * pad, GstEvent * event)
{
  gboolean res = TRUE;
  GstEvent *upstream = NULL;

  GstMPEGParse *mpeg_parse = GST_MPEG_PARSE (gst_pad_get_parent (pad));

  GST_LOG_OBJECT (mpeg_parse, "got %s event", GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
    {
#ifdef FIXME
      /* First try to use the index if we have one. */
      if (mpeg_parse->index) {
        res = index_seek (pad, event, &desired_offset, &expected_scr);
      }
#endif

      if (upstream == NULL) {
        /* Nothing found, try fuzzy seek. */
        upstream = normal_seek (mpeg_parse, pad, event);
      }

      if (!upstream) {
        gst_event_unref (event);
        res = FALSE;
        goto done;
      }

      res = gst_pad_event_default (pad, upstream);
      break;
    }
    case GST_EVENT_NAVIGATION:
      /* Forward navigation events unchanged. */
      res = gst_pad_push_event (mpeg_parse->sinkpad, event);
      break;
    default:
      res = gst_pad_event_default (pad, event);
      break;
  }

done:
  gst_object_unref (mpeg_parse);

  return res;
}

static GstStateChangeReturn
gst_mpeg_parse_change_state (GstElement * element, GstStateChange transition)
{
  GstMPEGParse *mpeg_parse = GST_MPEG_PARSE (element);
  GstStateChangeReturn result = GST_STATE_CHANGE_FAILURE;

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      if (!mpeg_parse->packetize) {
        mpeg_parse->packetize =
            gst_mpeg_packetize_new (GST_MPEG_PACKETIZE_SYSTEM);
      }

      /* Initialize parser state */
      gst_mpeg_parse_reset (mpeg_parse);
      break;
    default:
      break;
  }

  result = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
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

  return result;
}

static void
gst_mpeg_parse_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstMPEGParse *mpeg_parse;

  mpeg_parse = GST_MPEG_PARSE (object);

  switch (prop_id) {
    case ARG_MAX_SCR_GAP:
      g_value_set_int (value, mpeg_parse->max_scr_gap);
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
    case ARG_MAX_SCR_GAP:
      mpeg_parse->max_scr_gap = g_value_get_int (value);
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
