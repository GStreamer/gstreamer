/* GStreamer CDXA sync strippper / VCD parser
 * Copyright (C) 2004 Ronald Bultje <rbultje@ronald.bitfreak.net>
 * Copyright (C) 2008 Tim-Philipp Müller <tim centricular net>
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
#include <config.h>
#endif

#include <string.h>

#include "gstvcdparse.h"

GST_DEBUG_CATEGORY_EXTERN (vcdparse_debug);
#define GST_CAT_DEFAULT vcdparse_debug

static gboolean gst_vcd_parse_sink_event (GstPad * pad, GstEvent * event);
static gboolean gst_vcd_parse_src_event (GstPad * pad, GstEvent * event);
static gboolean gst_vcd_parse_src_query (GstPad * pad, GstQuery * query);
static GstFlowReturn gst_vcd_parse_chain (GstPad * pad, GstBuffer * buf);
static GstStateChangeReturn gst_vcd_parse_change_state (GstElement * element,
    GstStateChange transition);

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-vcd")
    );

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/mpeg, systemstream = (boolean) TRUE")
    );

GST_BOILERPLATE (GstVcdParse, gst_vcd_parse, GstElement, GST_TYPE_ELEMENT);

static void
gst_vcd_parse_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_factory));

  gst_element_class_set_static_metadata (element_class, "(S)VCD stream parser",
      "Codec/Parser", "Strip (S)VCD stream from its sync headers",
      "Tim-Philipp Müller <tim centricular net>, "
      "Ronald Bultje <rbultje@ronald.bitfreak.net>");
}

static void
gst_vcd_parse_class_init (GstVcdParseClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  element_class->change_state = GST_DEBUG_FUNCPTR (gst_vcd_parse_change_state);
}

static void
gst_vcd_parse_init (GstVcdParse * vcd, GstVcdParseClass * klass)
{
  vcd->sinkpad = gst_pad_new_from_static_template (&sink_factory, "sink");
  gst_pad_set_chain_function (vcd->sinkpad,
      GST_DEBUG_FUNCPTR (gst_vcd_parse_chain));
  gst_pad_set_event_function (vcd->sinkpad,
      GST_DEBUG_FUNCPTR (gst_vcd_parse_sink_event));
  gst_element_add_pad (GST_ELEMENT (vcd), vcd->sinkpad);

  vcd->srcpad = gst_pad_new_from_static_template (&src_factory, "src");
  gst_pad_set_event_function (vcd->srcpad,
      GST_DEBUG_FUNCPTR (gst_vcd_parse_src_event));
  gst_pad_set_query_function (vcd->srcpad,
      GST_DEBUG_FUNCPTR (gst_vcd_parse_src_query));
  gst_pad_use_fixed_caps (vcd->srcpad);
  gst_pad_set_caps (vcd->srcpad,
      gst_static_pad_template_get_caps (&src_factory));
  gst_element_add_pad (GST_ELEMENT (vcd), vcd->srcpad);
}

/* These conversion functions assume there's no junk between sectors */

static gint64
gst_vcd_parse_get_out_offset (gint64 in_offset)
{
  gint64 out_offset, chunknum, rest;

  if (in_offset == -1)
    return -1;

  if (G_UNLIKELY (in_offset < -1)) {
    GST_WARNING ("unexpected/invalid in_offset %" G_GINT64_FORMAT, in_offset);
    return in_offset;
  }

  chunknum = in_offset / GST_CDXA_SECTOR_SIZE;
  rest = in_offset % GST_CDXA_SECTOR_SIZE;

  out_offset = chunknum * GST_CDXA_DATA_SIZE;
  if (rest > GST_CDXA_HEADER_SIZE) {
    if (rest >= GST_CDXA_HEADER_SIZE + GST_CDXA_DATA_SIZE)
      out_offset += GST_CDXA_DATA_SIZE;
    else
      out_offset += rest - GST_CDXA_HEADER_SIZE;
  }

  GST_LOG ("transformed in_offset %" G_GINT64_FORMAT " to out_offset %"
      G_GINT64_FORMAT, in_offset, out_offset);

  return out_offset;
}

static gint64
gst_vcd_parse_get_in_offset (gint64 out_offset)
{
  gint64 in_offset, chunknum, rest;

  if (out_offset == -1)
    return -1;

  if (G_UNLIKELY (out_offset < -1)) {
    GST_WARNING ("unexpected/invalid out_offset %" G_GINT64_FORMAT, out_offset);
    return out_offset;
  }

  chunknum = out_offset / GST_CDXA_DATA_SIZE;
  rest = out_offset % GST_CDXA_DATA_SIZE;

  in_offset = chunknum * GST_CDXA_SECTOR_SIZE;
  if (rest > 0)
    in_offset += GST_CDXA_HEADER_SIZE + rest;

  GST_LOG ("transformed out_offset %" G_GINT64_FORMAT " to in_offset %"
      G_GINT64_FORMAT, out_offset, in_offset);

  return in_offset;
}

static gboolean
gst_vcd_parse_src_query (GstPad * pad, GstQuery * query)
{
  GstVcdParse *vcd = GST_VCD_PARSE (gst_pad_get_parent (pad));
  gboolean res = FALSE;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_DURATION:{
      GstFormat format;
      gint64 dur;

      /* first try upstream */
      if (!gst_pad_query_default (pad, query))
        break;

      /* we can only handle BYTES */
      gst_query_parse_duration (query, &format, &dur);
      if (format != GST_FORMAT_BYTES)
        break;

      gst_query_set_duration (query, GST_FORMAT_BYTES,
          gst_vcd_parse_get_out_offset (dur));

      res = TRUE;
      break;
    }
    case GST_QUERY_POSITION:{
      GstFormat format;
      gint64 pos;

      /* first try upstream */
      if (!gst_pad_query_default (pad, query))
        break;

      /* we can only handle BYTES */
      gst_query_parse_position (query, &format, &pos);
      if (format != GST_FORMAT_BYTES)
        break;

      gst_query_set_position (query, GST_FORMAT_BYTES,
          gst_vcd_parse_get_out_offset (pos));

      res = TRUE;
      break;
    }
    default:
      res = gst_pad_query_default (pad, query);
      break;
  }

  gst_object_unref (vcd);
  return res;
}

static gboolean
gst_vcd_parse_sink_event (GstPad * pad, GstEvent * event)
{
  GstVcdParse *vcd = GST_VCD_PARSE (gst_pad_get_parent (pad));
  gboolean res;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_NEWSEGMENT:{
      GstFormat format;
      gboolean update;
      gdouble rate, applied_rate;
      gint64 start, stop, position;

      gst_event_parse_new_segment_full (event, &update, &rate, &applied_rate,
          &format, &start, &stop, &position);

      if (format == GST_FORMAT_BYTES) {
        gst_event_unref (event);
        event = gst_event_new_new_segment_full (update, rate, applied_rate,
            GST_FORMAT_BYTES, gst_vcd_parse_get_out_offset (start),
            gst_vcd_parse_get_out_offset (stop), position);
      } else {
        GST_WARNING_OBJECT (vcd, "newsegment event in non-byte format");
      }
      res = gst_pad_event_default (pad, event);
      break;
    }
    case GST_EVENT_FLUSH_START:
      gst_adapter_clear (vcd->adapter);
      /* fall through */
    default:
      res = gst_pad_event_default (pad, event);
      break;
  }

  gst_object_unref (vcd);
  return res;
}

static gboolean
gst_vcd_parse_src_event (GstPad * pad, GstEvent * event)
{
  GstVcdParse *vcd = GST_VCD_PARSE (gst_pad_get_parent (pad));
  gboolean res;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:{
      GstSeekType start_type, stop_type;
      GstSeekFlags flags;
      GstFormat format;
      gdouble rate;
      gint64 start, stop;

      gst_event_parse_seek (event, &rate, &format, &flags, &start_type,
          &start, &stop_type, &stop);

      if (format == GST_FORMAT_BYTES) {
        gst_event_unref (event);
        if (start_type != GST_SEEK_TYPE_NONE)
          start = gst_vcd_parse_get_in_offset (start);
        if (stop_type != GST_SEEK_TYPE_NONE)
          stop = gst_vcd_parse_get_in_offset (stop);
        event = gst_event_new_seek (rate, GST_FORMAT_BYTES, flags, start_type,
            start, stop_type, stop);
      } else {
        GST_WARNING_OBJECT (vcd, "seek event in non-byte format");
      }
      res = gst_pad_event_default (pad, event);
      break;
    }
    default:
      res = gst_pad_event_default (pad, event);
      break;
  }

  gst_object_unref (vcd);
  return res;
}

/* -1 = no sync (discard buffer),
 * otherwise offset indicates sync point in buffer */
static gint
gst_vcd_parse_sync (const guint8 * data, guint size)
{
  const guint8 sync_marker[12] = { 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00
  };
  guint off = 0;

  while (size >= 12) {
    if (memcmp (data, sync_marker, 12) == 0)
      return off;

    --size;
    ++data;
    ++off;
  }
  return -1;
}

static GstFlowReturn
gst_vcd_parse_chain (GstPad * pad, GstBuffer * buf)
{
  GstVcdParse *vcd = GST_VCD_PARSE (GST_PAD_PARENT (pad));
  GstFlowReturn flow = GST_FLOW_OK;

  gst_adapter_push (vcd->adapter, buf);
  buf = NULL;

  while (gst_adapter_available (vcd->adapter) >= GST_CDXA_SECTOR_SIZE) {
    const guint8 *data;
    guint8 header[4 + 8];
    gint sync_offset;

    /* find sync (we could peek any size though really) */
    data = gst_adapter_peek (vcd->adapter, GST_CDXA_SECTOR_SIZE);
    sync_offset = gst_vcd_parse_sync (data, GST_CDXA_SECTOR_SIZE);
    GST_LOG_OBJECT (vcd, "sync offset = %d", sync_offset);

    if (sync_offset < 0) {
      gst_adapter_flush (vcd->adapter, GST_CDXA_SECTOR_SIZE - 12);
      continue;                 /* try again */
    }

    gst_adapter_flush (vcd->adapter, sync_offset);

    if (gst_adapter_available (vcd->adapter) < GST_CDXA_SECTOR_SIZE) {
      GST_LOG_OBJECT (vcd, "not enough data in adapter, waiting for more");
      break;
    }

    GST_LOG_OBJECT (vcd, "have full sector");

    /* have one sector: a sector is 2352 bytes long and is composed of:
     *
     * +-------------------------------------------------------+
     * !  sync    !  header ! subheader ! data ...   ! edc     !
     * ! 12 bytes ! 4 bytes ! 8 bytes   ! 2324 bytes ! 4 bytes !
     * +-------------------------------------------------------+
     * 
     * We strip the data out of it and send it to the srcpad.
     * 
     * sync       : 00 FF FF FF FF FF FF FF FF FF FF 00
     * header     : hour minute second mode
     * sub-header : track channel sub_mode coding repeat (4 bytes)
     * edc        : checksum
     */

    /* Skip CDXA header and edc footer, only keep data in the middle */
    gst_adapter_copy (vcd->adapter, header, 12, sizeof (header));
    gst_adapter_flush (vcd->adapter, GST_CDXA_HEADER_SIZE);
    buf = gst_adapter_take_buffer (vcd->adapter, GST_CDXA_DATA_SIZE);
    gst_adapter_flush (vcd->adapter, 4);

    /* we could probably do something clever to keep track of buffer offsets */
    buf = gst_buffer_make_metadata_writable (buf);
    GST_BUFFER_OFFSET (buf) = GST_BUFFER_OFFSET_NONE;
    GST_BUFFER_TIMESTAMP (buf) = GST_CLOCK_TIME_NONE;
    gst_buffer_set_caps (buf, GST_PAD_CAPS (vcd->srcpad));

    flow = gst_pad_push (vcd->srcpad, buf);
    buf = NULL;

    if (G_UNLIKELY (flow != GST_FLOW_OK)) {
      GST_DEBUG_OBJECT (vcd, "flow: %s", gst_flow_get_name (flow));
      break;
    }
  }

  return flow;
}

static GstStateChangeReturn
gst_vcd_parse_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn res = GST_STATE_CHANGE_SUCCESS;
  GstVcdParse *vcd = GST_VCD_PARSE (element);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      vcd->adapter = gst_adapter_new ();
      break;
    default:
      break;
  }

  res = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
    case GST_STATE_CHANGE_READY_TO_NULL:
      if (vcd->adapter) {
        g_object_unref (vcd->adapter);
        vcd->adapter = NULL;
      }
      break;
    default:
      break;
  }

  return res;
}
