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

static void gst_cdxaparse_base_init (gpointer g_class);
static void gst_cdxaparse_class_init (GstCDXAParseClass * klass);
static void gst_cdxaparse_init (GstCDXAParse * cdxaparse);

static GstElementStateReturn gst_cdxaparse_change_state (GstElement * element);

static void gst_cdxaparse_loop (GstElement * element);

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

GType
gst_cdxaparse_get_type (void)
{
  static GType cdxaparse_type = 0;

  if (!cdxaparse_type) {
    static const GTypeInfo cdxaparse_info = {
      sizeof (GstCDXAParseClass),
      gst_cdxaparse_base_init,
      NULL,
      (GClassInitFunc) gst_cdxaparse_class_init,
      NULL,
      NULL,
      sizeof (GstCDXAParse),
      0,
      (GInstanceInitFunc) gst_cdxaparse_init,
    };

    cdxaparse_type =
        g_type_register_static (GST_TYPE_RIFF_READ, "GstCDXAParse",
        &cdxaparse_info, 0);
  }
  return cdxaparse_type;
}


static void
gst_cdxaparse_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);
  static GstElementDetails gst_cdxaparse_details =
      GST_ELEMENT_DETAILS (".dat parser",
      "Codec/Parser",
      "Parse a .dat file (VCD) into raw mpeg1",
      "Wim Taymans <wim.taymans@tvd.be>");

  gst_element_class_set_details (element_class, &gst_cdxaparse_details);

  /* register src pads */
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_template_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_template_factory));
}

static void
gst_cdxaparse_class_init (GstCDXAParseClass * klass)
{
  GstElementClass *gstelement_class;
  GObjectClass *object_class;

  gstelement_class = (GstElementClass *) klass;
  object_class = (GObjectClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_RIFF_READ);

  gstelement_class->change_state = gst_cdxaparse_change_state;
}

static void
gst_cdxaparse_init (GstCDXAParse * cdxaparse)
{
  /* sink */
  cdxaparse->sinkpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&sink_template_factory), "sink");
  gst_element_add_pad (GST_ELEMENT (cdxaparse), cdxaparse->sinkpad);
  GST_RIFF_READ (cdxaparse)->sinkpad = cdxaparse->sinkpad;


  gst_element_set_loop_function (GST_ELEMENT (cdxaparse), gst_cdxaparse_loop);

  cdxaparse->state = GST_CDXAPARSE_START;

  cdxaparse->srcpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&src_template_factory), "src");
  gst_element_add_pad (GST_ELEMENT (cdxaparse), cdxaparse->srcpad);

  cdxaparse->seek_pending = FALSE;
  cdxaparse->seek_offset = 0;
}

static gboolean
gst_cdxaparse_stream_init (GstCDXAParse * cdxa)
{
  GstRiffRead *riff = GST_RIFF_READ (cdxa);
  guint32 doctype;

  if (!gst_riff_read_header (riff, &doctype))
    return FALSE;

  if (doctype != GST_RIFF_RIFF_CDXA) {
    GST_ELEMENT_ERROR (cdxa, STREAM, WRONG_TYPE, (NULL), (NULL));
    return FALSE;
  }

  return TRUE;
}

/* Read 'fmt ' header */
static gboolean
gst_cdxaparse_fmt (GstCDXAParse * cdxa)
{
  GstRiffRead *riff = GST_RIFF_READ (cdxa);
  gst_riff_strf_auds *header;

  if (!gst_riff_read_strf_auds (riff, &header)) {
    g_warning ("Not fmt");
    return FALSE;
  }

  /* As we don't know what is in this fmt field, we do nothing */

  return TRUE;
}

static gboolean
gst_cdxaparse_other (GstCDXAParse * cdxa)
{
  GstRiffRead *riff = GST_RIFF_READ (cdxa);
  guint32 tag, length;

  if (!gst_riff_peek_head (riff, &tag, &length, NULL)) {
    return FALSE;
  }

  switch (tag) {
    case GST_RIFF_TAG_data:
      if (!gst_bytestream_flush (riff->bs, 8))
        return FALSE;

      cdxa->state = GST_CDXAPARSE_DATA;
      cdxa->dataleft = cdxa->datasize = (guint64) length;
      cdxa->datastart = gst_bytestream_tell (riff->bs);
      break;

    default:
      if (!gst_riff_read_skip (riff))
        return FALSE;
      break;
  }

  return TRUE;
}

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

static void
gst_cdxaparse_loop (GstElement * element)
{
  GstCDXAParse *cdxa = GST_CDXAPARSE (element);
  GstRiffRead *riff = GST_RIFF_READ (cdxa);

  if (cdxa->state == GST_CDXAPARSE_DATA) {

    if (cdxa->dataleft > 0) {
      guint32 got_bytes, desired;
      GstBuffer *buf = NULL;
      GstBuffer *outbuf = NULL;

      desired = GST_CDXA_SECTOR_SIZE;

      if (!(buf = gst_riff_read_element_data (riff, desired, &got_bytes)))
        return;

      /* Skip CDXA headers, only keep data */
      outbuf =
          gst_buffer_create_sub (buf, GST_CDXA_HEADER_SIZE, GST_CDXA_DATA_SIZE);

      gst_buffer_unref (buf);
      gst_pad_push (cdxa->srcpad, GST_DATA (outbuf));

      cdxa->byteoffset += got_bytes;
      if (got_bytes < cdxa->dataleft) {
        cdxa->dataleft -= got_bytes;
        return;
      } else {
        cdxa->dataleft = 0;
        cdxa->state = GST_CDXAPARSE_OTHER;
      }
    } else {
      cdxa->state = GST_CDXAPARSE_OTHER;
    }
  }

  switch (cdxa->state) {
    case GST_CDXAPARSE_START:
      if (!gst_cdxaparse_stream_init (cdxa)) {
        return;
      }

      cdxa->state = GST_CDXAPARSE_FMT;
      /* fall-through */

    case GST_CDXAPARSE_FMT:
      if (!gst_cdxaparse_fmt (cdxa)) {
        return;
      }

      cdxa->state = GST_CDXAPARSE_OTHER;
      /* fall-through */

    case GST_CDXAPARSE_OTHER:
      if (!gst_cdxaparse_other (cdxa)) {
        return;
      }

      break;

    case GST_CDXAPARSE_DATA:

    default:
      g_assert_not_reached ();
  }
}

static GstElementStateReturn
gst_cdxaparse_change_state (GstElement * element)
{
  GstCDXAParse *cdxa = GST_CDXAPARSE (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_NULL_TO_READY:
      break;

    case GST_STATE_READY_TO_PAUSED:
      cdxa->state = GST_CDXAPARSE_START;
      break;

    case GST_STATE_PAUSED_TO_PLAYING:
      break;

    case GST_STATE_PLAYING_TO_PAUSED:
      break;

    case GST_STATE_PAUSED_TO_READY:
      cdxa->state = GST_CDXAPARSE_START;

      cdxa->seek_pending = FALSE;
      cdxa->seek_offset = 0;
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

  return gst_element_register (plugin, "cdxaparse", GST_RANK_PRIMARY,
      GST_TYPE_CDXAPARSE);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "cdxaparse",
    "Parse a .dat file (VCD) into raw mpeg1",
    plugin_init, VERSION, "LGPL", GST_PACKAGE, GST_ORIGIN)
