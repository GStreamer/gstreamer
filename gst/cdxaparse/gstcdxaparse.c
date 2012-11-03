/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 *               <2002> Wim Taymans <wim.taymans@chello.be>
 *               <2006> Tim-Philipp Müller <tim centricular net>
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
#include "config.h"
#endif

/* FIXME 0.11: suppress warnings for deprecated API such as GStaticRecMutex
 * with newer GLib versions (>= 2.31.0) */
#define GLIB_DISABLE_DEPRECATION_WARNINGS

#include <string.h>

#include "gstcdxaparse.h"
#include "gstvcdparse.h"

#include <gst/riff/riff-ids.h>
#include <gst/riff/riff-read.h>

GST_DEBUG_CATEGORY (vcdparse_debug);

GST_DEBUG_CATEGORY_STATIC (cdxaparse_debug);
#define GST_CAT_DEFAULT cdxaparse_debug

static gboolean gst_cdxa_parse_sink_activate (GstPad * sinkpad);
static void gst_cdxa_parse_loop (GstPad * sinkpad);
static gboolean gst_cdxa_parse_sink_activate_pull (GstPad * sinkpad,
    gboolean active);
static GstStateChangeReturn gst_cdxa_parse_change_state (GstElement * element,
    GstStateChange transition);
static gboolean gst_cdxa_parse_src_event (GstPad * srcpad, GstEvent * event);
static gboolean gst_cdxa_parse_src_query (GstPad * srcpad, GstQuery * query);

static GstStaticPadTemplate sink_template_factory =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-cdxa")
    );

static GstStaticPadTemplate src_template_factory =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/mpeg, " "systemstream = (boolean) TRUE")
    );

GST_BOILERPLATE (GstCDXAParse, gst_cdxa_parse, GstElement, GST_TYPE_ELEMENT);

static void
gst_cdxa_parse_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_static_metadata (element_class, "(S)VCD parser",
      "Codec/Parser",
      "Parse a .dat file from (S)VCD into raw MPEG-1",
      "Wim Taymans <wim.taymans@tvd.be>");

  /* register src pads */
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_template_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_template_factory));
}

static void
gst_cdxa_parse_class_init (GstCDXAParseClass * klass)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_cdxa_parse_change_state);
}

static void
gst_cdxa_parse_init (GstCDXAParse * cdxaparse, GstCDXAParseClass * klass)
{
  GstCaps *caps;

  cdxaparse->sinkpad =
      gst_pad_new_from_static_template (&sink_template_factory, "sink");
  gst_pad_set_activate_function (cdxaparse->sinkpad,
      GST_DEBUG_FUNCPTR (gst_cdxa_parse_sink_activate));
  gst_pad_set_activatepull_function (cdxaparse->sinkpad,
      GST_DEBUG_FUNCPTR (gst_cdxa_parse_sink_activate_pull));

  gst_element_add_pad (GST_ELEMENT (cdxaparse), cdxaparse->sinkpad);

  cdxaparse->srcpad =
      gst_pad_new_from_static_template (&src_template_factory, "src");

  gst_pad_set_event_function (cdxaparse->srcpad,
      GST_DEBUG_FUNCPTR (gst_cdxa_parse_src_event));
  gst_pad_set_query_function (cdxaparse->srcpad,
      GST_DEBUG_FUNCPTR (gst_cdxa_parse_src_query));

  caps = gst_caps_new_simple ("video/mpeg",
      "systemstream", G_TYPE_BOOLEAN, TRUE, NULL);
  gst_pad_use_fixed_caps (cdxaparse->srcpad);
  gst_pad_set_caps (cdxaparse->srcpad, caps);
  gst_caps_unref (caps);
  gst_element_add_pad (GST_ELEMENT (cdxaparse), cdxaparse->srcpad);


  cdxaparse->state = GST_CDXA_PARSE_START;
  cdxaparse->offset = 0;
  cdxaparse->datasize = 0;
  cdxaparse->datastart = -1;
}

#define HAVE_FOURCC(data,fourcc)  (GST_READ_UINT32_LE((data))==(fourcc))

static gboolean
gst_cdxa_parse_stream_init (GstCDXAParse * cdxa)
{
  GstFlowReturn flow_ret;
  GstBuffer *buf = NULL;
  guint8 *data;

  flow_ret = gst_pad_pull_range (cdxa->sinkpad, cdxa->offset, 12, &buf);
  if (flow_ret != GST_FLOW_OK)
    return flow_ret;

  if (GST_BUFFER_SIZE (buf) < 12)
    goto wrong_type;

  data = GST_BUFFER_DATA (buf);
  if (!HAVE_FOURCC (data, GST_RIFF_TAG_RIFF)) {
    GST_ERROR_OBJECT (cdxa, "Not a RIFF file");
    goto wrong_type;
  }

  if (!HAVE_FOURCC (data + 8, GST_RIFF_RIFF_CDXA)) {
    GST_ERROR_OBJECT (cdxa, "RIFF file does not have CDXA content");
    goto wrong_type;
  }

  cdxa->offset += 12;
  gst_buffer_unref (buf);

  return TRUE;

wrong_type:

  GST_ELEMENT_ERROR (cdxa, STREAM, WRONG_TYPE, (NULL), (NULL));
  gst_buffer_unref (buf);
  return FALSE;
}

static gboolean
gst_cdxa_parse_sink_activate (GstPad * sinkpad)
{
  GstCDXAParse *cdxa = GST_CDXA_PARSE (GST_PAD_PARENT (sinkpad));

  if (!gst_pad_check_pull_range (sinkpad) ||
      !gst_pad_activate_pull (sinkpad, TRUE)) {
    GST_DEBUG_OBJECT (cdxa, "No pull mode");
    return FALSE;
  }

  /* If we can activate pull_range upstream, then read the header
   * and see if it's really a RIFF CDXA file. */
  GST_DEBUG_OBJECT (cdxa, "Activated pull mode. Reading RIFF header");
  if (!gst_cdxa_parse_stream_init (cdxa))
    return FALSE;

  return TRUE;
}

static gboolean
gst_cdxa_parse_sink_activate_pull (GstPad * sinkpad, gboolean active)
{
  if (active) {
    /* if we have a scheduler we can start the task */
    gst_pad_start_task (sinkpad, (GstTaskFunction) gst_cdxa_parse_loop,
        sinkpad, NULL);
  } else {
    gst_pad_stop_task (sinkpad);
  }

  return TRUE;
}

/*
 * A sector is 2352 bytes long and is composed of:
 * 
 * !  sync    !  header ! subheader ! data ...   ! edc     !
 * ! 12 bytes ! 4 bytes ! 8 bytes   ! 2324 bytes ! 4 bytes !
 * !-------------------------------------------------------!
 * 
 * We strip the data out of it and send it to the srcpad.
 * 
 * sync : 00 FF FF FF FF FF FF FF FF FF FF 00
 * header : hour minute second mode
 * sub-header : track channel sub_mode coding repeat (4 bytes)
 * edc : checksum
 */

/* FIXME: use define from gstcdxastrip.h */
#define GST_CDXA_SECTOR_SIZE  	2352
#define GST_CDXA_DATA_SIZE  	2324
#define GST_CDXA_HEADER_SIZE	24

/* FIXME: use version from gstcdxastrip.c */
static GstBuffer *
gst_cdxa_parse_strip (GstBuffer * buf)
{
  GstBuffer *sub;

  g_assert (GST_BUFFER_SIZE (buf) >= GST_CDXA_SECTOR_SIZE);

  /* Skip CDXA headers, only keep data.
   * FIXME: check sync, resync, ... */
  sub = gst_buffer_create_sub (buf, GST_CDXA_HEADER_SIZE, GST_CDXA_DATA_SIZE);
  gst_buffer_unref (buf);

  return sub;
}

/* -1 = no sync (discard buffer),
 * otherwise offset indicates syncpoint in buffer.
 */

static gint
gst_cdxa_parse_sync (GstBuffer * buf)
{
  const guint8 sync_marker[12] = { 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00
  };
  guint8 *data;
  guint size;

  size = GST_BUFFER_SIZE (buf);
  data = GST_BUFFER_DATA (buf);

  while (size >= 12) {
    if (memcmp (data, sync_marker, 12) == 0) {
      return (gint) (data - GST_BUFFER_DATA (buf));
    }
    --size;
    ++data;
  }
  return -1;
}

static void
gst_cdxa_parse_loop (GstPad * sinkpad)
{
  GstFlowReturn flow_ret;
  GstCDXAParse *cdxa;
  GstBuffer *buf = NULL;
  gint sync_offset = -1;

  cdxa = GST_CDXA_PARSE (GST_PAD_PARENT (sinkpad));

  if (cdxa->datasize <= 0) {
    GstFormat format = GST_FORMAT_BYTES;
    GstPad *peer;

    if ((peer = gst_pad_get_peer (sinkpad))) {
      if (!gst_pad_query_duration (peer, &format, &cdxa->datasize)) {
        GST_DEBUG_OBJECT (cdxa, "Failed to query upstream size!");
        gst_object_unref (peer);
        goto pause;
      }
      gst_object_unref (peer);
    }
    GST_DEBUG_OBJECT (cdxa, "Upstream size: %" G_GINT64_FORMAT, cdxa->datasize);
  }

  do {
    guint req;

    req = 8 + GST_CDXA_SECTOR_SIZE;     /* riff chunk header = 8 bytes */

    buf = NULL;
    flow_ret = gst_pad_pull_range (cdxa->sinkpad, cdxa->offset, req, &buf);

    if (flow_ret != GST_FLOW_OK) {
      GST_DEBUG_OBJECT (cdxa, "Pull flow: %s", gst_flow_get_name (flow_ret));
      goto pause;
    }

    if (GST_BUFFER_SIZE (buf) < req) {
      GST_DEBUG_OBJECT (cdxa, "Short read, only got %u/%u bytes",
          GST_BUFFER_SIZE (buf), req);
      goto eos;
    }

    sync_offset = gst_cdxa_parse_sync (buf);
    gst_buffer_unref (buf);
    buf = NULL;

    if (sync_offset >= 0)
      break;

    cdxa->offset += req;
    cdxa->bytes_skipped += req;
  } while (1);

  cdxa->offset += sync_offset;
  cdxa->bytes_skipped += sync_offset;

  /* first sync frame? */
  if (cdxa->datastart < 0) {
    GST_LOG_OBJECT (cdxa, "datastart=0x%" G_GINT64_MODIFIER "x", cdxa->offset);
    cdxa->datastart = cdxa->offset;
    cdxa->bytes_skipped = 0;
    cdxa->bytes_sent = 0;
  }

  GST_DEBUG_OBJECT (cdxa, "pulling buffer at offset 0x%" G_GINT64_MODIFIER "x",
      cdxa->offset);

  buf = NULL;
  flow_ret = gst_pad_pull_range (cdxa->sinkpad, cdxa->offset,
      GST_CDXA_SECTOR_SIZE, &buf);

  if (flow_ret != GST_FLOW_OK) {
    GST_DEBUG_OBJECT (cdxa, "Flow: %s", gst_flow_get_name (flow_ret));
    goto pause;
  }

  if (GST_BUFFER_SIZE (buf) < GST_CDXA_SECTOR_SIZE) {
    GST_DEBUG_OBJECT (cdxa, "Short read, only got %u/%u bytes",
        GST_BUFFER_SIZE (buf), GST_CDXA_SECTOR_SIZE);
    goto eos;
  }

  buf = gst_cdxa_parse_strip (buf);

  GST_DEBUG_OBJECT (cdxa, "pushing buffer %p", buf);
  gst_buffer_set_caps (buf, GST_PAD_CAPS (cdxa->srcpad));

  cdxa->offset += GST_BUFFER_SIZE (buf);
  cdxa->bytes_sent += GST_BUFFER_SIZE (buf);

  flow_ret = gst_pad_push (cdxa->srcpad, buf);
  if (flow_ret != GST_FLOW_OK) {
    GST_DEBUG_OBJECT (cdxa, "Push flow: %s", gst_flow_get_name (flow_ret));
    goto pause;
  }

  return;

eos:
  {
    GST_DEBUG_OBJECT (cdxa, "Sending EOS");
    if (buf)
      gst_buffer_unref (buf);
    buf = NULL;
    gst_pad_push_event (cdxa->srcpad, gst_event_new_eos ());
    /* fallthrough */
  }
pause:
  {
    GST_DEBUG_OBJECT (cdxa, "Pausing");
    gst_pad_pause_task (cdxa->sinkpad);
    return;
  }
}

static gint64
gst_cdxa_parse_convert_src_to_sink_offset (GstCDXAParse * cdxa, gint64 src)
{
  gint64 sink;

  sink = src + cdxa->datastart;
  sink = gst_util_uint64_scale (sink, GST_CDXA_SECTOR_SIZE, GST_CDXA_DATA_SIZE);

  /* FIXME: take into account skipped bytes */

  GST_DEBUG_OBJECT (cdxa, "src offset=%" G_GINT64_FORMAT ", sink offset=%"
      G_GINT64_FORMAT, src, sink);

  return sink;
}

static gint64
gst_cdxa_parse_convert_sink_to_src_offset (GstCDXAParse * cdxa, gint64 sink)
{
  gint64 src;

  src = sink - cdxa->datastart;
  src = gst_util_uint64_scale (src, GST_CDXA_DATA_SIZE, GST_CDXA_SECTOR_SIZE);

  /* FIXME: take into account skipped bytes */

  GST_DEBUG_OBJECT (cdxa, "sink offset=%" G_GINT64_FORMAT ", src offset=%"
      G_GINT64_FORMAT, sink, src);

  return src;
}

static gboolean
gst_cdxa_parse_do_seek (GstCDXAParse * cdxa, GstEvent * event)
{
  GstSeekFlags flags;
  GstSeekType start_type;
  GstFormat format;
  gint64 start, off, upstream_size;

  gst_event_parse_seek (event, NULL, &format, &flags, &start_type, &start,
      NULL, NULL);

  if (format != GST_FORMAT_BYTES) {
    GST_DEBUG_OBJECT (cdxa, "Can only handle seek in BYTES format");
    return FALSE;
  }

  if (start_type != GST_SEEK_TYPE_SET) {
    GST_DEBUG_OBJECT (cdxa, "Can only handle seek from start (SEEK_TYPE_SET)");
    return FALSE;
  }

  GST_OBJECT_LOCK (cdxa);
  off = gst_cdxa_parse_convert_src_to_sink_offset (cdxa, start);
  upstream_size = cdxa->datasize;
  GST_OBJECT_UNLOCK (cdxa);

  if (off >= upstream_size) {
    GST_DEBUG_OBJECT (cdxa, "Invalid target offset %" G_GINT64_FORMAT ", file "
        "is only %" G_GINT64_FORMAT " bytes in size", off, upstream_size);
    return FALSE;
  }

  /* unlock upstream pull_range */
  gst_pad_push_event (cdxa->sinkpad, gst_event_new_flush_start ());

  /* make sure our loop function exits */
  gst_pad_push_event (cdxa->srcpad, gst_event_new_flush_start ());

  /* wait for streaming to finish */
  GST_PAD_STREAM_LOCK (cdxa->sinkpad);

  /* prepare for streaming again */
  gst_pad_push_event (cdxa->sinkpad, gst_event_new_flush_stop ());
  gst_pad_push_event (cdxa->srcpad, gst_event_new_flush_stop ());

  gst_pad_push_event (cdxa->srcpad,
      gst_event_new_new_segment (FALSE, 1.0, GST_FORMAT_BYTES,
          start, GST_CLOCK_TIME_NONE, 0));

  GST_OBJECT_LOCK (cdxa);
  cdxa->offset = off;
  GST_OBJECT_UNLOCK (cdxa);

  /* and restart */
  gst_pad_start_task (cdxa->sinkpad,
      (GstTaskFunction) gst_cdxa_parse_loop, cdxa->sinkpad, NULL);

  GST_PAD_STREAM_UNLOCK (cdxa->sinkpad);
  return TRUE;
}

static gboolean
gst_cdxa_parse_src_event (GstPad * srcpad, GstEvent * event)
{
  GstCDXAParse *cdxa = GST_CDXA_PARSE (gst_pad_get_parent (srcpad));
  gboolean res = FALSE;

  GST_DEBUG_OBJECT (cdxa, "Handling %s event", GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
      res = gst_cdxa_parse_do_seek (cdxa, event);
      break;
    default:
      res = gst_pad_event_default (srcpad, event);
      break;
  }

  gst_object_unref (cdxa);
  return res;
}

static gboolean
gst_cdxa_parse_src_query (GstPad * srcpad, GstQuery * query)
{
  GstCDXAParse *cdxa = GST_CDXA_PARSE (gst_pad_get_parent (srcpad));
  gboolean res = FALSE;

  GST_DEBUG_OBJECT (cdxa, "Handling %s query",
      gst_query_type_get_name (GST_QUERY_TYPE (query)));

  res = gst_pad_query_default (srcpad, query);

  if (res) {
    GstFormat format;
    gint64 val;

    switch (GST_QUERY_TYPE (query)) {
      case GST_QUERY_DURATION:
        gst_query_parse_duration (query, &format, &val);
        if (format == GST_FORMAT_BYTES) {
          val = gst_cdxa_parse_convert_sink_to_src_offset (cdxa, val);
          gst_query_set_duration (query, format, val);
        }
        break;
      case GST_QUERY_POSITION:
        gst_query_parse_position (query, &format, &val);
        if (format == GST_FORMAT_BYTES) {
          val = gst_cdxa_parse_convert_sink_to_src_offset (cdxa, val);
          gst_query_set_position (query, format, val);
        }
        break;
      default:
        break;
    }
  }

  gst_object_unref (cdxa);
  return res;
}

static GstStateChangeReturn
gst_cdxa_parse_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
  GstCDXAParse *cdxa = GST_CDXA_PARSE (element);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      cdxa->state = GST_CDXA_PARSE_START;
      break;
    default:
      break;
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      cdxa->state = GST_CDXA_PARSE_START;
      cdxa->datasize = 0;
      cdxa->datastart = -1;
      break;
    default:
      break;
  }

  return ret;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (cdxaparse_debug, "cdxaparse", 0, "CDXA Parser");
  GST_DEBUG_CATEGORY_INIT (vcdparse_debug, "vcdparse", 0, "VCD Parser");

  if (!gst_element_register (plugin, "cdxaparse", GST_RANK_PRIMARY,
          GST_TYPE_CDXA_PARSE))
    return FALSE;
  if (!gst_element_register (plugin, "vcdparse", GST_RANK_PRIMARY,
          GST_TYPE_VCD_PARSE))
    return FALSE;

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    cdxaparse,
    "Parse a .dat file (VCD) into raw mpeg1",
    plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
