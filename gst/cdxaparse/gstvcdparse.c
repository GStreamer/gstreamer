/* GStreamer CDXA sync strippper
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
#include <config.h>
#endif

#include <string.h>
#include <gst/gst.h>
#include "gstcdxastrip.h"

static void gst_cdxastrip_base_init (GstCDXAStripClass * klass);
static void gst_cdxastrip_class_init (GstCDXAStripClass * klass);
static void gst_cdxastrip_init (GstCDXAStrip * cdxastrip);

static const GstEventMask *gst_cdxastrip_get_event_mask (GstPad * pad);
static gboolean gst_cdxastrip_handle_src_event (GstPad * pad, GstEvent * event);
static const GstFormat *gst_cdxastrip_get_src_formats (GstPad * pad);
static const GstQueryType *gst_cdxastrip_get_src_query_types (GstPad * pad);
static gboolean gst_cdxastrip_handle_src_query (GstPad * pad,
    GstQueryType type, GstFormat * format, gint64 * value);

static void gst_cdxastrip_chain (GstPad * pad, GstData * data);
static GstStateChangeReturn gst_cdxastrip_change_state (GstElement * element,
    GstStateChange transition);

static GstStaticPadTemplate sink_template_factory =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-vcd")
    );

static GstStaticPadTemplate src_template_factory =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/mpeg, " "systemstream = (boolean) TRUE")
    );

static GstElementClass *parent_class = NULL;

GType
gst_cdxastrip_get_type (void)
{
  static GType cdxastrip_type = 0;

  if (!cdxastrip_type) {
    static const GTypeInfo cdxastrip_info = {
      sizeof (GstCDXAStripClass),
      (GBaseInitFunc) gst_cdxastrip_base_init,
      NULL,
      (GClassInitFunc) gst_cdxastrip_class_init,
      NULL,
      NULL,
      sizeof (GstCDXAStrip),
      0,
      (GInstanceInitFunc) gst_cdxastrip_init,
    };

    cdxastrip_type =
        g_type_register_static (GST_TYPE_ELEMENT, "GstCDXAStrip",
        &cdxastrip_info, 0);
  }

  return cdxastrip_type;
}

static void
gst_cdxastrip_base_init (GstCDXAStripClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  static GstElementDetails gst_cdxastrip_details =
      GST_ELEMENT_DETAILS ("vcd parser",
      "Codec/Parser",
      "Strip (S)VCD stream from its syncheaders",
      "Ronald Bultje <rbultje@ronald.bitfreak.net>");

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_template_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_template_factory));

  gst_element_class_set_details (element_class, &gst_cdxastrip_details);
}

static void
gst_cdxastrip_class_init (GstCDXAStripClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  element_class->change_state = gst_cdxastrip_change_state;
}

static void
gst_cdxastrip_init (GstCDXAStrip * cdxastrip)
{
  GST_OBJECT_FLAG_SET (cdxastrip, GST_ELEMENT_EVENT_AWARE);

  cdxastrip->sinkpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&sink_template_factory), "sink");
  gst_pad_set_chain_function (cdxastrip->sinkpad, gst_cdxastrip_chain);
  gst_element_add_pad (GST_ELEMENT (cdxastrip), cdxastrip->sinkpad);

  cdxastrip->srcpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&src_template_factory), "src");
  gst_pad_set_formats_function (cdxastrip->srcpad,
      gst_cdxastrip_get_src_formats);
  gst_pad_set_event_mask_function (cdxastrip->srcpad,
      gst_cdxastrip_get_event_mask);
  gst_pad_set_event_function (cdxastrip->srcpad,
      gst_cdxastrip_handle_src_event);
  gst_pad_set_query_type_function (cdxastrip->srcpad,
      gst_cdxastrip_get_src_query_types);
  gst_pad_set_query_function (cdxastrip->srcpad,
      gst_cdxastrip_handle_src_query);
  gst_element_add_pad (GST_ELEMENT (cdxastrip), cdxastrip->srcpad);
}

/*
 * Stuff.
 */

static const GstFormat *
gst_cdxastrip_get_src_formats (GstPad * pad)
{
  static const GstFormat formats[] = {
    GST_FORMAT_BYTES,
    0
  };

  return formats;
}

static const GstQueryType *
gst_cdxastrip_get_src_query_types (GstPad * pad)
{
  static const GstQueryType types[] = {
    GST_QUERY_TOTAL,
    GST_QUERY_POSITION,
    0
  };

  return types;
}

static gboolean
gst_cdxastrip_handle_src_query (GstPad * pad,
    GstQueryType type, GstFormat * format, gint64 * value)
{
  GstCDXAStrip *cdxa = GST_CDXASTRIP (gst_pad_get_parent (pad));

  if (!gst_pad_query (GST_PAD_PEER (cdxa->sinkpad), type, format, value))
    return FALSE;

  if (*format != GST_FORMAT_BYTES)
    return TRUE;

  switch (type) {
    case GST_QUERY_TOTAL:
    case GST_QUERY_POSITION:{
      gint num, rest;

      num = *value / GST_CDXA_SECTOR_SIZE;
      rest = *value % GST_CDXA_SECTOR_SIZE;

      *value = num * GST_CDXA_DATA_SIZE;
      if (rest > GST_CDXA_HEADER_SIZE) {
        if (rest >= GST_CDXA_HEADER_SIZE + GST_CDXA_DATA_SIZE)
          *value += GST_CDXA_DATA_SIZE;
        else
          *value += rest - GST_CDXA_HEADER_SIZE;
      }
      break;
    }
    default:
      break;
  }

  return TRUE;
}

static const GstEventMask *
gst_cdxastrip_get_event_mask (GstPad * pad)
{
  static const GstEventMask masks[] = {
    {GST_EVENT_SEEK, GST_SEEK_METHOD_SET | GST_SEEK_FLAG_KEY_UNIT},
    {0,}
  };

  return masks;
}

static gboolean
gst_cdxastrip_handle_src_event (GstPad * pad, GstEvent * event)
{
  GstCDXAStrip *cdxa = GST_CDXASTRIP (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
      switch (GST_EVENT_SEEK_FORMAT (event)) {
        case GST_FORMAT_BYTES:{
          GstEvent *new;
          gint64 off;
          gint num, rest;

          off = GST_EVENT_SEEK_OFFSET (event);
          num = off / GST_CDXA_DATA_SIZE;
          rest = off % GST_CDXA_DATA_SIZE;
          off = num * GST_CDXA_SECTOR_SIZE;
          if (rest > 0)
            off += rest + GST_CDXA_HEADER_SIZE;
          new = gst_event_new_seek (GST_EVENT_SEEK_TYPE (event), off);
          gst_event_unref (event);
          event = new;
        }
        default:
          break;
      }
      break;
    default:
      break;
  }

  return gst_pad_send_event (GST_PAD_PEER (cdxa->sinkpad), event);
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

GstBuffer *
gst_cdxastrip_strip (GstBuffer * buf)
{
  GstBuffer *sub;

  g_assert (GST_BUFFER_SIZE (buf) >= GST_CDXA_SECTOR_SIZE);

  /* Skip CDXA headers, only keep data.
   * FIXME: check sync, resync, ... */
  sub = gst_buffer_create_sub (buf, GST_CDXA_HEADER_SIZE, GST_CDXA_DATA_SIZE);
  gst_buffer_unref (buf);

  return sub;
}

/*
 * -1 = no sync (discard buffer),
 * otherwise offset indicates syncpoint in buffer.
 */

gint
gst_cdxastrip_sync (GstBuffer * buf)
{
  guint size, off = 0;
  guint8 *data;

  for (size = GST_BUFFER_SIZE (buf), data = GST_BUFFER_DATA (buf);
      size >= 12; size--, data++, off++) {
    /* we could do a checksum check as well, but who cares... */
    if (!memcmp (data, "\000\377\377\377\377\377\377\377\377\377\377\000", 12))
      return off;
  }

  return -1;
}

/*
 * Do stuff.
 */

static void
gst_cdxastrip_handle_event (GstCDXAStrip * cdxa, GstEvent * event)
{
  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_DISCONTINUOUS:{
      gint64 new_off, off;

      if (gst_event_discont_get_value (event, GST_FORMAT_BYTES, &new_off)) {
        GstEvent *new;
        gint chunknum, rest;

        chunknum = new_off / GST_CDXA_SECTOR_SIZE;
        rest = new_off % GST_CDXA_SECTOR_SIZE;
        off = chunknum * GST_CDXA_DATA_SIZE;
        if (rest > GST_CDXA_HEADER_SIZE) {
          if (rest >= GST_CDXA_HEADER_SIZE + GST_CDXA_DATA_SIZE)
            off += GST_CDXA_DATA_SIZE;
          else
            off += rest - GST_CDXA_HEADER_SIZE;
        }
        new = gst_event_new_discontinuous (GST_EVENT_DISCONT_NEW_MEDIA (event),
            GST_FORMAT_BYTES, new_off, GST_FORMAT_UNDEFINED);
        gst_event_unref (event);
        event = new;
      }
      gst_pad_event_default (cdxa->sinkpad, event);
      break;
    }
    case GST_EVENT_FLUSH:
      if (cdxa->cache) {
        gst_buffer_unref (cdxa->cache);
        cdxa->cache = NULL;
      }
      /* fall-through */
    default:
      gst_pad_event_default (cdxa->sinkpad, event);
      break;
  }
}

static void
gst_cdxastrip_chain (GstPad * pad, GstData * data)
{
  GstCDXAStrip *cdxa = GST_CDXASTRIP (gst_pad_get_parent (pad));
  GstBuffer *buf, *sub;
  gint sync;

  if (GST_IS_EVENT (data)) {
    gst_cdxastrip_handle_event (cdxa, GST_EVENT (data));
    return;
  }

  buf = GST_BUFFER (data);
  if (cdxa->cache) {
    buf = gst_buffer_join (cdxa->cache, buf);
  }
  cdxa->cache = NULL;

  while (buf && GST_BUFFER_SIZE (buf) >= GST_CDXA_SECTOR_SIZE) {
    /* sync */
    sync = gst_cdxastrip_sync (buf);
    if (sync < 0) {
      gst_buffer_unref (buf);
      return;
    }
    sub = gst_buffer_create_sub (buf, sync, GST_BUFFER_SIZE (buf) - sync);
    gst_buffer_unref (buf);
    buf = sub;
    if (GST_BUFFER_SIZE (buf) < GST_CDXA_SECTOR_SIZE)
      break;

    /* one chunk */
    sub = gst_cdxastrip_strip (gst_buffer_ref (buf));
    gst_pad_push (cdxa->srcpad, GST_DATA (sub));

    /* cache */
    if (GST_BUFFER_SIZE (buf) != GST_CDXA_SECTOR_SIZE) {
      sub = gst_buffer_create_sub (buf, GST_CDXA_SECTOR_SIZE,
          GST_BUFFER_SIZE (buf) - GST_CDXA_SECTOR_SIZE);
    } else {
      sub = NULL;
    }
    gst_buffer_unref (buf);
    buf = sub;
  }

  cdxa->cache = buf;
}

static GstStateChangeReturn
gst_cdxastrip_change_state (GstElement * element, GstStateChange transition)
{
  GstCDXAStrip *cdxa = GST_CDXASTRIP (element);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      if (cdxa->cache) {
        gst_buffer_unref (cdxa->cache);
        cdxa->cache = NULL;
      }
      break;
    default:
      break;
  }

  if (parent_class->change_state)
    return parent_class->change_state (element, transition);

  return GST_STATE_CHANGE_SUCCESS;
}
