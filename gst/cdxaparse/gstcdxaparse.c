/* -*- Mode: C; tab-width: 2; indent-tabs-mode: t; c-basic-offset: 2 -*- */
/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 *               <2002> Wim Taymans <wim.taymans@chello.be>
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

#include "gstcdxaparse.h"
#include "gst/riff/riff-ids.h"
#include "gst/riff/riff-media.h"

static void gst_cdxa_parse_base_init (gpointer g_class);
static void gst_cdxa_parse_class_init (GstCDXAParseClass * klass);
static void gst_cdxa_parse_init (GstCDXAParse * cdxa_parse);

static GstElementStateReturn gst_cdxa_parse_change_state (GstElement * element);

static void gst_cdxa_parse_loop (GstElement * element);

/* elementfactory information */
static GstElementDetails gst_cdxa_parse_details =
GST_ELEMENT_DETAILS (".dat parser",
    "Codec/Parser",
    "Parse a .dat file (VCD) into raw mpeg1",
    "Wim Taymans <wim.taymans@tvd.be>");

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

/* CDXAParse signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0,
  /* FILL ME */
};

static GstElementClass *parent_class = NULL;

/*static guint gst_cdxa_parse_signals[LAST_SIGNAL] = { 0 }; */

GType
gst_cdxa_parse_get_type (void)
{
  static GType cdxa_parse_type = 0;

  if (!cdxa_parse_type) {
    static const GTypeInfo cdxa_parse_info = {
      sizeof (GstCDXAParseClass),
      gst_cdxa_parse_base_init,
      NULL,
      (GClassInitFunc) gst_cdxa_parse_class_init,
      NULL,
      NULL,
      sizeof (GstCDXAParse),
      0,
      (GInstanceInitFunc) gst_cdxa_parse_init,
    };

    cdxa_parse_type =
        g_type_register_static (GST_TYPE_RIFF_READ, "GstCDXAParse",
        &cdxa_parse_info, 0);
  }
  return cdxa_parse_type;
}


static void
gst_cdxa_parse_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details (element_class, &gst_cdxa_parse_details);

  /* register src pads */
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_template_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_template_factory));
}

static void
gst_cdxa_parse_class_init (GstCDXAParseClass * klass)
{
  GstElementClass *gstelement_class;
  GObjectClass *object_class;

  gstelement_class = (GstElementClass *) klass;
  object_class = (GObjectClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_RIFF_READ);

  gstelement_class->change_state = gst_cdxa_parse_change_state;
}

static void
gst_cdxa_parse_init (GstCDXAParse * cdxa_parse)
{
  /* sink */
  cdxa_parse->sinkpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&sink_template_factory), "sink");
  gst_element_add_pad (GST_ELEMENT (cdxa_parse), cdxa_parse->sinkpad);
  GST_RIFF_READ (cdxa_parse)->sinkpad = cdxa_parse->sinkpad;


  gst_element_set_loop_function (GST_ELEMENT (cdxa_parse), gst_cdxa_parse_loop);


  cdxa_parse->srcpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&src_template_factory), "src");
  gst_element_add_pad (GST_ELEMENT (cdxa_parse), cdxa_parse->srcpad);

  cdxa_parse->state = GST_CDXA_PARSE_START;

  cdxa_parse->seek_pending = FALSE;
  cdxa_parse->seek_offset = 0;
}

static gboolean
gst_cdxa_parse_stream_init (GstCDXAParse * cdxa_parse)
{
  GstRiffRead *riff = GST_RIFF_READ (cdxa_parse);
  guint32 doctype;

  if (!gst_riff_read_header (riff, &doctype))
    return FALSE;

  if (doctype != GST_RIFF_RIFF_CDXA) {
    GST_ELEMENT_ERROR (cdxa_parse, STREAM, WRONG_TYPE, (NULL), (NULL));
    return FALSE;
  }

  return TRUE;
}

/* Read 'fmt ' header */
static gboolean
gst_cdxa_parse_fmt (GstCDXAParse * cdxa_parse)
{
  GstRiffRead *riff = GST_RIFF_READ (cdxa_parse);
  gst_riff_strf_auds *header;

  if (!gst_riff_read_strf_auds (riff, &header)) {
    g_warning ("Not fmt");
    return FALSE;
  }

  /* As we don't know what is in this fmt field, we do nothing */

  return TRUE;
}

static gboolean
gst_cdxa_parse_other (GstCDXAParse * cdxa_parse)
{
  GstRiffRead *riff = GST_RIFF_READ (cdxa_parse);
  guint32 tag, length;

  /* Fixme, need to handle a seek...can you seek in cdxa? */

  if (!gst_riff_peek_head (riff, &tag, &length, NULL)) {
    return FALSE;
  }

  switch (tag) {
    case GST_RIFF_TAG_data:
      gst_bytestream_flush (riff->bs, 8);

      cdxa_parse->state = GST_CDXA_PARSE_DATA;
      cdxa_parse->dataleft = (guint64) length;
      break;

    default:
      gst_riff_read_skip (riff);
      break;
  }

  return TRUE;
}

#define MAX_BUFFER_SIZE 4096

static void
gst_cdxa_parse_loop (GstElement * element)
{
  GstCDXAParse *cdxa_parse = GST_CDXA_PARSE (element);
  GstRiffRead *riff = GST_RIFF_READ (cdxa_parse);

  if (cdxa_parse->state == GST_CDXA_PARSE_DATA) {
    if (cdxa_parse->dataleft > 0) {
      guint32 got_bytes, desired;
      GstBuffer *buf, *outbuf;

      desired = GST_CDXA_SECTOR_SIZE;

      buf = gst_riff_read_element_data (riff, desired, &got_bytes);

/*

A sector is 2352 bytes long and is composed of:

!  sync    !  header ! subheader ! data ...   ! edc     !
! 12 bytes ! 4 bytes ! 8 bytes   ! 2324 bytes ! 4 bytes !
!-------------------------------------------------------!

We parse the data out of it and send it to the srcpad.

sync : 00 FF FF FF FF FF FF FF FF FF FF 00
header : hour minute second mode
sub-header : track channel sub_mode coding repeat (4 bytes)
edc : checksum

*/

/*
      if (got_bytes < GST_CDXA_SECTOR_SIZE) {
        gst_cdxa_parse_handle_event (cdxa_parse);
        return;
      }
*/

      /* Extract time from CDXA header */
/*      printf( "%02u:%02u:%02u\n", (unsigned char) *(GST_BUFFER_DATA(buf)+12), (unsigned char) *(GST_BUFFER_DATA(buf)+13), (unsigned char) *(GST_BUFFER_DATA(buf)+14) );*/

      /* Jump CDXA headers, only keep data */
      outbuf = gst_buffer_create_sub (buf, 24, GST_CDXA_DATA_SIZE);
      gst_buffer_unref (buf);

      gst_pad_push (cdxa_parse->srcpad, GST_DATA (outbuf));

      cdxa_parse->byteoffset += got_bytes;
      if (got_bytes < cdxa_parse->dataleft) {
        cdxa_parse->dataleft -= got_bytes;
        return;
      } else {
        cdxa_parse->dataleft = 0;
        cdxa_parse->state = GST_CDXA_PARSE_OTHER;
      }
    } else {
      cdxa_parse->state = GST_CDXA_PARSE_OTHER;
    }
  }

  switch (cdxa_parse->state) {
    case GST_CDXA_PARSE_START:
      if (!gst_cdxa_parse_stream_init (cdxa_parse)) {
        return;
      }

      cdxa_parse->state = GST_CDXA_PARSE_FMT;
      /* fall-through */

    case GST_CDXA_PARSE_FMT:
      if (!gst_cdxa_parse_fmt (cdxa_parse)) {
        return;
      }

      cdxa_parse->state = GST_CDXA_PARSE_OTHER;
      /* fall-through */

    case GST_CDXA_PARSE_OTHER:
      if (!gst_cdxa_parse_other (cdxa_parse)) {
        return;
      }

      break;

    case GST_CDXA_PARSE_DATA:

    default:
      g_assert_not_reached ();
  }
}

static GstElementStateReturn
gst_cdxa_parse_change_state (GstElement * element)
{
  GstCDXAParse *cdxa_parse = GST_CDXA_PARSE (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_NULL_TO_READY:
      break;

    case GST_STATE_READY_TO_PAUSED:
      cdxa_parse->state = GST_CDXA_PARSE_START;
      break;

    case GST_STATE_PAUSED_TO_PLAYING:
      break;

    case GST_STATE_PLAYING_TO_PAUSED:
      break;

    case GST_STATE_PAUSED_TO_READY:
      cdxa_parse->state = GST_CDXA_PARSE_START;


      cdxa_parse->seek_pending = FALSE;
      cdxa_parse->seek_offset = 0;
      break;

    case GST_STATE_READY_TO_NULL:
      break;
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);

  return GST_STATE_SUCCESS;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  if (!gst_library_load ("riff")) {
    return FALSE;
  }

  return gst_element_register (plugin, "cdxaparse", GST_RANK_SECONDARY,
      GST_TYPE_CDXA_PARSE);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "cdxaparse",
    "Parse a .dat file (VCD) into raw mpeg1",
    plugin_init, VERSION, "LGPL", GST_PACKAGE, GST_ORIGIN)
