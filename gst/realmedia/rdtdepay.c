/* GStreamer
 * Copyright (C) <2006> Lutz Mueller <lutz at topfrose dot de>
 *               <2006> Wim Taymans <wim@fluendo.com>
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <string.h>

#include "gstrdtbuffer.h"
#include "rdtdepay.h"

GST_DEBUG_CATEGORY_STATIC (rdtdepay_debug);
#define GST_CAT_DEFAULT rdtdepay_debug

/* RDTDepay signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  PROP_0,
};

static GstStaticPadTemplate gst_rdt_depay_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/vnd.rn-realmedia")
    );

static GstStaticPadTemplate gst_rdt_depay_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rdt, "
        "media = (string) \"application\", "
        "clock-rate = (int) [1, MAX ], "
        "encoding-name = (string) \"X-REAL-RDT\""
        /* All optional parameters
         *
         * "config=" 
         */
    )
    );

#define gst_rdt_depay_parent_class parent_class
G_DEFINE_TYPE (GstRDTDepay, gst_rdt_depay, GST_TYPE_ELEMENT);

static void gst_rdt_depay_finalize (GObject * object);

static GstStateChangeReturn gst_rdt_depay_change_state (GstElement *
    element, GstStateChange transition);

static gboolean gst_rdt_depay_sink_event (GstPad * pad, GstObject * parent,
    GstEvent * event);
static GstFlowReturn gst_rdt_depay_chain (GstPad * pad, GstObject * parent,
    GstBuffer * buf);

static void
gst_rdt_depay_class_init (GstRDTDepayClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->finalize = gst_rdt_depay_finalize;

  gstelement_class->change_state = gst_rdt_depay_change_state;

  gst_element_class_add_static_pad_template (gstelement_class,
      &gst_rdt_depay_src_template);
  gst_element_class_add_static_pad_template (gstelement_class,
      &gst_rdt_depay_sink_template);

  gst_element_class_set_static_metadata (gstelement_class, "RDT packet parser",
      "Codec/Depayloader/Network",
      "Extracts RealMedia from RDT packets",
      "Lutz Mueller <lutz at topfrose dot de>, "
      "Wim Taymans <wim@fluendo.com>");

  GST_DEBUG_CATEGORY_INIT (rdtdepay_debug, "rdtdepay",
      0, "Depayloader for RDT RealMedia packets");
}

static void
gst_rdt_depay_init (GstRDTDepay * rdtdepay)
{
  rdtdepay->sinkpad =
      gst_pad_new_from_static_template (&gst_rdt_depay_sink_template, "sink");
  gst_pad_set_chain_function (rdtdepay->sinkpad, gst_rdt_depay_chain);
  gst_pad_set_event_function (rdtdepay->sinkpad, gst_rdt_depay_sink_event);
  gst_element_add_pad (GST_ELEMENT_CAST (rdtdepay), rdtdepay->sinkpad);

  rdtdepay->srcpad =
      gst_pad_new_from_static_template (&gst_rdt_depay_src_template, "src");
  gst_element_add_pad (GST_ELEMENT_CAST (rdtdepay), rdtdepay->srcpad);
}

static void
gst_rdt_depay_finalize (GObject * object)
{
  GstRDTDepay *rdtdepay;

  rdtdepay = GST_RDT_DEPAY (object);

  if (rdtdepay->header)
    gst_buffer_unref (rdtdepay->header);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_rdt_depay_setcaps (GstPad * pad, GstCaps * caps)
{
  GstStructure *structure;
  GstRDTDepay *rdtdepay;
  GstCaps *srccaps;
  gint clock_rate = 1000;       /* default */
  const GValue *value;
  GstBuffer *header;

  rdtdepay = GST_RDT_DEPAY (GST_PAD_PARENT (pad));

  structure = gst_caps_get_structure (caps, 0);

  if (gst_structure_has_field (structure, "clock-rate"))
    gst_structure_get_int (structure, "clock-rate", &clock_rate);

  /* config contains the RealMedia header as a buffer. */
  value = gst_structure_get_value (structure, "config");
  if (!value)
    goto no_header;

  header = gst_value_get_buffer (value);
  if (!header)
    goto no_header;

  /* get other values for newsegment */
  value = gst_structure_get_value (structure, "npt-start");
  if (value && G_VALUE_HOLDS_UINT64 (value))
    rdtdepay->npt_start = g_value_get_uint64 (value);
  else
    rdtdepay->npt_start = 0;
  GST_DEBUG_OBJECT (rdtdepay, "NPT start %" G_GUINT64_FORMAT,
      rdtdepay->npt_start);

  value = gst_structure_get_value (structure, "npt-stop");
  if (value && G_VALUE_HOLDS_UINT64 (value))
    rdtdepay->npt_stop = g_value_get_uint64 (value);
  else
    rdtdepay->npt_stop = -1;

  GST_DEBUG_OBJECT (rdtdepay, "NPT stop %" G_GUINT64_FORMAT,
      rdtdepay->npt_stop);

  value = gst_structure_get_value (structure, "play-speed");
  if (value && G_VALUE_HOLDS_DOUBLE (value))
    rdtdepay->play_speed = g_value_get_double (value);
  else
    rdtdepay->play_speed = 1.0;

  value = gst_structure_get_value (structure, "play-scale");
  if (value && G_VALUE_HOLDS_DOUBLE (value))
    rdtdepay->play_scale = g_value_get_double (value);
  else
    rdtdepay->play_scale = 1.0;

  /* caps seem good, configure element */
  rdtdepay->clock_rate = clock_rate;

  /* set caps on pad and on header */
  srccaps = gst_caps_new_empty_simple ("application/vnd.rn-realmedia");
  gst_pad_set_caps (rdtdepay->srcpad, srccaps);
  gst_caps_unref (srccaps);

  if (rdtdepay->header)
    gst_buffer_unref (rdtdepay->header);
  rdtdepay->header = gst_buffer_ref (header);

  return TRUE;

  /* ERRORS */
no_header:
  {
    GST_ERROR_OBJECT (rdtdepay, "no header found in caps, no 'config' field");
    return FALSE;
  }
}

static gboolean
gst_rdt_depay_sink_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstRDTDepay *depay;
  gboolean res = TRUE;

  depay = GST_RDT_DEPAY (parent);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:
    {
      GstCaps *caps;

      gst_event_parse_caps (event, &caps);
      res = gst_rdt_depay_setcaps (pad, caps);
      gst_event_unref (event);
      break;
    }
    case GST_EVENT_FLUSH_STOP:
      res = gst_pad_push_event (depay->srcpad, event);

      gst_segment_init (&depay->segment, GST_FORMAT_UNDEFINED);
      depay->need_newsegment = TRUE;
      depay->next_seqnum = -1;
      break;
    case GST_EVENT_SEGMENT:
    {
      gst_event_copy_segment (event, &depay->segment);
      /* don't pass the event downstream, we generate our own segment
       * including the NTP time and other things we receive in caps */
      gst_event_unref (event);
      break;
    }
    default:
      /* pass other events forward */
      res = gst_pad_push_event (depay->srcpad, event);
      break;
  }
  return res;
}

static GstEvent *
create_segment_event (GstRDTDepay * depay, gboolean update,
    GstClockTime position)
{
  GstSegment segment;

  gst_segment_init (&segment, GST_FORMAT_TIME);
  segment.rate = depay->play_speed;
  segment.applied_rate = depay->play_scale;
  segment.start = position;

  if (depay->npt_stop != -1)
    segment.stop = depay->npt_stop - depay->npt_start;
  else
    segment.stop = -1;

  segment.time = position + depay->npt_start;

  return gst_event_new_segment (&segment);
}

static GstFlowReturn
gst_rdt_depay_push (GstRDTDepay * rdtdepay, GstBuffer * buffer)
{
  GstFlowReturn ret;

  if (rdtdepay->need_newsegment) {
    GstEvent *event;

    event = create_segment_event (rdtdepay, FALSE, 0);
    gst_pad_push_event (rdtdepay->srcpad, event);

    rdtdepay->need_newsegment = FALSE;
  }

  if (rdtdepay->discont) {
    GST_BUFFER_FLAG_SET (buffer, GST_BUFFER_FLAG_DISCONT);
    rdtdepay->discont = FALSE;
  }
  ret = gst_pad_push (rdtdepay->srcpad, buffer);

  return ret;
}

static GstFlowReturn
gst_rdt_depay_handle_data (GstRDTDepay * rdtdepay, GstClockTime outtime,
    GstRDTPacket * packet)
{
  GstFlowReturn ret;
  GstBuffer *outbuf;
  GstMapInfo outmap;
  guint8 *data, *outdata;
  guint size;
  guint16 stream_id;
  guint32 timestamp;
  gint gap;
  guint16 seqnum;
  guint8 flags;
  guint16 outflags;

  /* get pointers to the packet data */
  data = gst_rdt_packet_data_map (packet, &size);

  outbuf = gst_buffer_new_and_alloc (12 + size);
  GST_BUFFER_TIMESTAMP (outbuf) = outtime;

  GST_DEBUG_OBJECT (rdtdepay, "have size %u", size);

  /* copy over some things */
  stream_id = gst_rdt_packet_data_get_stream_id (packet);
  timestamp = gst_rdt_packet_data_get_timestamp (packet);
  flags = gst_rdt_packet_data_get_flags (packet);

  seqnum = gst_rdt_packet_data_get_seq (packet);

  GST_DEBUG_OBJECT (rdtdepay, "stream_id %u, timestamp %u, seqnum %d, flags %d",
      stream_id, timestamp, seqnum, flags);

  if (rdtdepay->next_seqnum != -1) {
    gap = gst_rdt_buffer_compare_seqnum (seqnum, rdtdepay->next_seqnum);

    /* if we have no gap, all is fine */
    if (G_UNLIKELY (gap != 0)) {
      GST_LOG_OBJECT (rdtdepay, "got packet %u, expected %u, gap %d", seqnum,
          rdtdepay->next_seqnum, gap);
      if (gap < 0) {
        /* seqnum > next_seqnum, we are missing some packets, this is always a
         * DISCONT. */
        GST_LOG_OBJECT (rdtdepay, "%d missing packets", gap);
        rdtdepay->discont = TRUE;
      } else {
        /* seqnum < next_seqnum, we have seen this packet before or the sender
         * could be restarted. If the packet is not too old, we throw it away as
         * a duplicate, otherwise we mark discont and continue. 100 misordered
         * packets is a good threshold. See also RFC 4737. */
        if (gap < 100)
          goto dropping;

        GST_LOG_OBJECT (rdtdepay,
            "%d > 100, packet too old, sender likely restarted", gap);
        rdtdepay->discont = TRUE;
      }
    }
  }
  rdtdepay->next_seqnum = (seqnum + 1);
  if (rdtdepay->next_seqnum == 0xff00)
    rdtdepay->next_seqnum = 0;

  if ((flags & 1) == 0)
    outflags = 2;
  else
    outflags = 0;

  gst_buffer_map (outbuf, &outmap, GST_MAP_WRITE);
  outdata = outmap.data;
  GST_WRITE_UINT16_BE (outdata + 0, 0); /* version   */
  GST_WRITE_UINT16_BE (outdata + 2, size + 12); /* length    */
  GST_WRITE_UINT16_BE (outdata + 4, stream_id); /* stream    */
  GST_WRITE_UINT32_BE (outdata + 6, timestamp); /* timestamp */
  GST_WRITE_UINT16_BE (outdata + 10, outflags); /* flags     */
  memcpy (outdata + 12, data, size);
  gst_buffer_unmap (outbuf, &outmap);
  gst_buffer_resize (outbuf, 0, 12 + size);

  gst_rdt_packet_data_unmap (packet);

  GST_DEBUG_OBJECT (rdtdepay, "Pushing packet, outtime %" GST_TIME_FORMAT,
      GST_TIME_ARGS (outtime));

  ret = gst_rdt_depay_push (rdtdepay, outbuf);

  return ret;

  /* ERRORS */
dropping:
  {
    GST_WARNING_OBJECT (rdtdepay, "%d <= 100, dropping old packet", gap);
    return GST_FLOW_OK;
  }
}

static GstFlowReturn
gst_rdt_depay_chain (GstPad * pad, GstObject * parent, GstBuffer * buf)
{
  GstRDTDepay *rdtdepay;
  GstFlowReturn ret;
  GstClockTime timestamp;
  gboolean more;
  GstRDTPacket packet;

  rdtdepay = GST_RDT_DEPAY (parent);

  if (GST_BUFFER_IS_DISCONT (buf)) {
    GST_LOG_OBJECT (rdtdepay, "received discont");
    rdtdepay->discont = TRUE;
  }

  if (rdtdepay->header) {
    GstBuffer *out;

    out = rdtdepay->header;
    rdtdepay->header = NULL;

    /* push header data first */
    gst_rdt_depay_push (rdtdepay, out);
  }

  /* save timestamp */
  timestamp = GST_BUFFER_TIMESTAMP (buf);

  ret = GST_FLOW_OK;

  GST_LOG_OBJECT (rdtdepay, "received buffer timestamp %" GST_TIME_FORMAT,
      GST_TIME_ARGS (timestamp));

  /* data is in RDT format. */
  more = gst_rdt_buffer_get_first_packet (buf, &packet);
  while (more) {
    GstRDTType type;

    type = gst_rdt_packet_get_type (&packet);
    GST_DEBUG_OBJECT (rdtdepay, "Have packet of type %04x", type);

    if (GST_RDT_IS_DATA_TYPE (type)) {
      GST_DEBUG_OBJECT (rdtdepay, "We have a data packet");
      ret = gst_rdt_depay_handle_data (rdtdepay, timestamp, &packet);
    } else {
      switch (type) {
        default:
          GST_DEBUG_OBJECT (rdtdepay, "Ignoring packet");
          break;
      }
    }
    if (ret != GST_FLOW_OK)
      break;

    more = gst_rdt_packet_move_to_next (&packet);
  }

  gst_buffer_unref (buf);

  return ret;
}

static GstStateChangeReturn
gst_rdt_depay_change_state (GstElement * element, GstStateChange transition)
{
  GstRDTDepay *rdtdepay;
  GstStateChangeReturn ret;

  rdtdepay = GST_RDT_DEPAY (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      gst_segment_init (&rdtdepay->segment, GST_FORMAT_UNDEFINED);
      rdtdepay->next_seqnum = -1;
      rdtdepay->need_newsegment = TRUE;
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      if (rdtdepay->header)
        gst_buffer_unref (rdtdepay->header);
      rdtdepay->header = NULL;
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    default:
      break;
  }
  return ret;
}

gboolean
gst_rdt_depay_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "rdtdepay",
      GST_RANK_MARGINAL, GST_TYPE_RDT_DEPAY);
}
