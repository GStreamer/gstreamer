/* -*- Mode: C; tab-width: 2; indent-tabs-mode: t; c-basic-offset: 2 -*- */
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
#include <string.h>

#include "gstwavparse.h"
#include "gst/riff/riff-ids.h"
#include "gst/riff/riff-media.h"

#ifndef G_MAXUINT32
#define G_MAXUINT32 0xffffffff
#endif

GST_DEBUG_CATEGORY (wavparse_debug);
#define GST_CAT_DEFAULT (wavparse_debug)

static void gst_wavparse_base_init (gpointer g_class);
static void gst_wavparse_class_init (GstWavParseClass * klass);
static void gst_wavparse_init (GstWavParse * wavparse);

static gboolean gst_wavparse_sink_activate (GstPad * sinkpad);
static gboolean gst_wavparse_sink_activate_pull (GstPad * sinkpad,
    gboolean active);
static GstElementStateReturn gst_wavparse_change_state (GstElement * element);

static gboolean gst_wavparse_pad_query (GstPad * pad, GstQuery * query);
static const GstQueryType *gst_wavparse_get_query_types (GstPad * pad);
static gboolean gst_wavparse_pad_convert (GstPad * pad,
    GstFormat src_format,
    gint64 src_value, GstFormat * dest_format, gint64 * dest_value);

static void gst_wavparse_loop (GstPad * pad);
static gboolean gst_wavparse_srcpad_event (GstPad * pad, GstEvent * event);
static void gst_wavparse_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstStaticPadTemplate sink_template_factory =
GST_STATIC_PAD_TEMPLATE ("wavparse_sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-wav")
    );

static GstStaticPadTemplate src_template_factory =
    GST_STATIC_PAD_TEMPLATE ("wavparse_src",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,          /* FIXME: spider */
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "endianness = (int) little_endian, "
        "signed = (boolean) { true, false }, "
        "width = (int) { 8, 16, 24, 32 }, "
        "depth = (int) { 8, 16, 24, 32 }, "
        "rate = (int) [ 8000, 48000 ], "
        "channels = (int) [ 1, 2 ]; "
        "audio/mpeg, "
        "mpegversion = (int) 1, "
        "layer = (int) [ 1, 3 ], "
        "rate = (int) [ 8000, 48000 ], "
        "channels = (int) [ 1, 2 ]; "
        "audio/x-alaw, "
        "rate = (int) [ 8000, 48000 ], "
        "channels = (int) [ 1, 2 ]; "
        "audio/x-mulaw, "
        "rate = (int) [ 8000, 48000 ], " "channels = (int) [ 1, 2 ];"
        "audio/x-adpcm, "
        "layout = (string) microsoft, "
        "block_align = (int) [ 1, 8192 ], "
        "rate = (int) [ 8000, 48000 ], "
        "channels = (int) [ 1, 2 ]; "
        "audio/x-adpcm, "
        "layout = (string) dvi, "
        "block_align = (int) [ 1, 8192 ], "
        "rate = (int) [ 8000, 48000 ], " "channels = (int) [ 1, 2 ];"
        "audio/x-vnd.sony.atrac3")
    );

/* WavParse signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  PROP_0
};

static GstElementClass *parent_class = NULL;

/*static guint gst_wavparse_signals[LAST_SIGNAL] = { 0 }; */

GType
gst_wavparse_get_type (void)
{
  static GType wavparse_type = 0;

  if (!wavparse_type) {
    static const GTypeInfo wavparse_info = {
      sizeof (GstWavParseClass),
      gst_wavparse_base_init,
      NULL,
      (GClassInitFunc) gst_wavparse_class_init,
      NULL,
      NULL,
      sizeof (GstWavParse),
      0,
      (GInstanceInitFunc) gst_wavparse_init,
    };

    wavparse_type =
        g_type_register_static (GST_TYPE_ELEMENT, "GstWavParse",
        &wavparse_info, 0);
  }
  return wavparse_type;
}


static void
gst_wavparse_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);
  static GstElementDetails gst_wavparse_details =
      GST_ELEMENT_DETAILS (".wav demuxer",
      "Codec/Demuxer/Audio",
      "Parse a .wav file into raw audio",
      "Erik Walthinsen <omega@cse.ogi.edu>");

  gst_element_class_set_details (element_class, &gst_wavparse_details);

  /* register src pads */
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_template_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_template_factory));
}

static void
gst_wavparse_class_init (GstWavParseClass * klass)
{
  GstElementClass *gstelement_class;
  GObjectClass *object_class;

  gstelement_class = (GstElementClass *) klass;
  object_class = (GObjectClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  object_class->get_property = gst_wavparse_get_property;
  gstelement_class->change_state = gst_wavparse_change_state;

  GST_DEBUG_CATEGORY_INIT (wavparse_debug, "wavparse", 0, "WAV parser");
}

static void
gst_wavparse_reset (GstWavParse * wavparse)
{
  wavparse->state = GST_WAVPARSE_START;

  /* These will all be set correctly in the fmt chunk */
  wavparse->depth = 0;
  wavparse->rate = 0;
  wavparse->width = 0;
  wavparse->channels = 0;
  wavparse->blockalign = 0;
  wavparse->bps = 0;
  wavparse->dataleft = 0;
  wavparse->datasize = 0;
  wavparse->datastart = 0;

  if (wavparse->seek_event)
    gst_event_unref (wavparse->seek_event);
  wavparse->seek_event = NULL;
  wavparse->seek_pending = FALSE;
  wavparse->seek_offset = (guint64) - 1;
}

static void
gst_wavparse_init (GstWavParse * wavparse)
{
  /* sink */
  wavparse->sinkpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&sink_template_factory), "sink");
  gst_element_add_pad (GST_ELEMENT (wavparse), wavparse->sinkpad);
  wavparse->sinkpad = wavparse->sinkpad;

  gst_pad_set_activate_function (wavparse->sinkpad, gst_wavparse_sink_activate);
  gst_pad_set_activatepull_function (wavparse->sinkpad,
      gst_wavparse_sink_activate_pull);
  gst_wavparse_reset (wavparse);
}

static void
gst_wavparse_destroy_sourcepad (GstWavParse * wavparse)
{
  if (wavparse->srcpad) {
    gst_element_remove_pad (GST_ELEMENT (wavparse), wavparse->srcpad);
    wavparse->srcpad = NULL;
  }
}

static void
gst_wavparse_create_sourcepad (GstWavParse * wavparse)
{
  gst_wavparse_destroy_sourcepad (wavparse);

  /* source */
  wavparse->srcpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&src_template_factory), "src");
  gst_pad_use_fixed_caps (wavparse->srcpad);
  gst_pad_set_query_type_function (wavparse->srcpad,
      gst_wavparse_get_query_types);
  gst_pad_set_query_function (wavparse->srcpad, gst_wavparse_pad_query);
  gst_pad_set_event_function (wavparse->srcpad, gst_wavparse_srcpad_event);
}

static void
gst_wavparse_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstWavParse *wavparse;

  wavparse = GST_WAVPARSE (object);

  switch (prop_id) {
    default:
      break;
  }
}

#if 0
static void
gst_wavparse_parse_adtl (GstWavParse * wavparse, int len)
{
  guint32 got_bytes;
  GstByteStream *bs = wavparse->bs;
  gst_riff_chunk *temp_chunk, chunk;
  guint8 *tempdata;
  struct _gst_riff_labl labl, *temp_labl;
  struct _gst_riff_ltxt ltxt, *temp_ltxt;
  struct _gst_riff_note note, *temp_note;
  char *label_name;
  GstProps *props;
  GstPropsEntry *entry;
  GstCaps *new_caps;
  GList *caps = NULL;

  props = wavparse->metadata->properties;

  while (len > 0) {
    got_bytes =
        gst_bytestream_peek_bytes (bs, &tempdata, sizeof (gst_riff_chunk));
    if (got_bytes != sizeof (gst_riff_chunk)) {
      return;
    }
    temp_chunk = (gst_riff_chunk *) tempdata;

    chunk.id = GUINT32_FROM_LE (temp_chunk->id);
    chunk.size = GUINT32_FROM_LE (temp_chunk->size);

    if (chunk.size == 0) {
      gst_bytestream_flush (bs, sizeof (gst_riff_chunk));
      len -= sizeof (gst_riff_chunk);
      continue;
    }

    switch (chunk.id) {
      case GST_RIFF_adtl_labl:
        got_bytes =
            gst_bytestream_peek_bytes (bs, &tempdata,
            sizeof (struct _gst_riff_labl));
        if (got_bytes != sizeof (struct _gst_riff_labl)) {
          return;
        }

        temp_labl = (struct _gst_riff_labl *) tempdata;
        labl.id = GUINT32_FROM_LE (temp_labl->id);
        labl.size = GUINT32_FROM_LE (temp_labl->size);
        labl.identifier = GUINT32_FROM_LE (temp_labl->identifier);

        gst_bytestream_flush (bs, sizeof (struct _gst_riff_labl));
        len -= sizeof (struct _gst_riff_labl);

        got_bytes = gst_bytestream_peek_bytes (bs, &tempdata, labl.size - 4);
        if (got_bytes != labl.size - 4) {
          return;
        }

        label_name = (char *) tempdata;

        gst_bytestream_flush (bs, ((labl.size - 4) + 1) & ~1);
        len -= (((labl.size - 4) + 1) & ~1);

        new_caps = gst_caps_new ("label",
            "application/x-gst-metadata",
            gst_props_new ("identifier", G_TYPE_INT (labl.identifier),
                "name", G_TYPE_STRING (label_name), NULL));

        if (gst_props_get (props, "labels", &caps, NULL)) {
          caps = g_list_append (caps, new_caps);
        } else {
          caps = g_list_append (NULL, new_caps);

          entry = gst_props_entry_new ("labels", GST_PROPS_GLIST (caps));
          gst_props_add_entry (props, entry);
        }

        break;

      case GST_RIFF_adtl_ltxt:
        got_bytes =
            gst_bytestream_peek_bytes (bs, &tempdata,
            sizeof (struct _gst_riff_ltxt));
        if (got_bytes != sizeof (struct _gst_riff_ltxt)) {
          return;
        }

        temp_ltxt = (struct _gst_riff_ltxt *) tempdata;
        ltxt.id = GUINT32_FROM_LE (temp_ltxt->id);
        ltxt.size = GUINT32_FROM_LE (temp_ltxt->size);
        ltxt.identifier = GUINT32_FROM_LE (temp_ltxt->identifier);
        ltxt.length = GUINT32_FROM_LE (temp_ltxt->length);
        ltxt.purpose = GUINT32_FROM_LE (temp_ltxt->purpose);
        ltxt.country = GUINT16_FROM_LE (temp_ltxt->country);
        ltxt.language = GUINT16_FROM_LE (temp_ltxt->language);
        ltxt.dialect = GUINT16_FROM_LE (temp_ltxt->dialect);
        ltxt.codepage = GUINT16_FROM_LE (temp_ltxt->codepage);

        gst_bytestream_flush (bs, sizeof (struct _gst_riff_ltxt));
        len -= sizeof (struct _gst_riff_ltxt);

        if (ltxt.size - 20 > 0) {
          got_bytes = gst_bytestream_peek_bytes (bs, &tempdata, ltxt.size - 20);
          if (got_bytes != ltxt.size - 20) {
            return;
          }

          gst_bytestream_flush (bs, ((ltxt.size - 20) + 1) & ~1);
          len -= (((ltxt.size - 20) + 1) & ~1);

          label_name = (char *) tempdata;
        } else {
          label_name = "";
        }

        new_caps = gst_caps_new ("ltxt",
            "application/x-gst-metadata",
            gst_props_new ("identifier", G_TYPE_INT (ltxt.identifier),
                "name", G_TYPE_STRING (label_name),
                "length", G_TYPE_INT (ltxt.length), NULL));

        if (gst_props_get (props, "ltxts", &caps, NULL)) {
          caps = g_list_append (caps, new_caps);
        } else {
          caps = g_list_append (NULL, new_caps);

          entry = gst_props_entry_new ("ltxts", GST_PROPS_GLIST (caps));
          gst_props_add_entry (props, entry);
        }

        break;

      case GST_RIFF_adtl_note:
        got_bytes =
            gst_bytestream_peek_bytes (bs, &tempdata,
            sizeof (struct _gst_riff_note));
        if (got_bytes != sizeof (struct _gst_riff_note)) {
          return;
        }

        temp_note = (struct _gst_riff_note *) tempdata;
        note.id = GUINT32_FROM_LE (temp_note->id);
        note.size = GUINT32_FROM_LE (temp_note->size);
        note.identifier = GUINT32_FROM_LE (temp_note->identifier);

        gst_bytestream_flush (bs, sizeof (struct _gst_riff_note));
        len -= sizeof (struct _gst_riff_note);

        got_bytes = gst_bytestream_peek_bytes (bs, &tempdata, note.size - 4);
        if (got_bytes != note.size - 4) {
          return;
        }

        gst_bytestream_flush (bs, ((note.size - 4) + 1) & ~1);
        len -= (((note.size - 4) + 1) & ~1);

        label_name = (char *) tempdata;

        new_caps = gst_caps_new ("note",
            "application/x-gst-metadata",
            gst_props_new ("identifier", G_TYPE_INT (note.identifier),
                "name", G_TYPE_STRING (label_name), NULL));

        if (gst_props_get (props, "notes", &caps, NULL)) {
          caps = g_list_append (caps, new_caps);
        } else {
          caps = g_list_append (NULL, new_caps);

          entry = gst_props_entry_new ("notes", GST_PROPS_GLIST (caps));
          gst_props_add_entry (props, entry);
        }

        break;

      default:
        g_print ("Unknown chunk: " GST_FOURCC_FORMAT "\n",
            GST_FOURCC_ARGS (chunk.id));
        return;
    }
  }

  g_object_notify (G_OBJECT (wavparse), "metadata");
}
#endif

#if 0
static void
gst_wavparse_parse_cues (GstWavParse * wavparse, int len)
{
  guint32 got_bytes;
  GstByteStream *bs = wavparse->bs;
  struct _gst_riff_cue *temp_cue, cue;
  struct _gst_riff_cuepoints *points;
  guint8 *tempdata;
  int i;
  GList *cues = NULL;
  GstPropsEntry *entry;

  while (len > 0) {
    int required;

    got_bytes =
        gst_bytestream_peek_bytes (bs, &tempdata,
        sizeof (struct _gst_riff_cue));
    temp_cue = (struct _gst_riff_cue *) tempdata;

    /* fixup for our big endian friends */
    cue.id = GUINT32_FROM_LE (temp_cue->id);
    cue.size = GUINT32_FROM_LE (temp_cue->size);
    cue.cuepoints = GUINT32_FROM_LE (temp_cue->cuepoints);

    gst_bytestream_flush (bs, sizeof (struct _gst_riff_cue));
    if (got_bytes != sizeof (struct _gst_riff_cue)) {
      return;
    }

    len -= sizeof (struct _gst_riff_cue);

    /* -4 because cue.size contains the cuepoints size
       and we've already flushed that out of the system */
    required = cue.size - 4;
    got_bytes = gst_bytestream_peek_bytes (bs, &tempdata, required);
    gst_bytestream_flush (bs, ((required) + 1) & ~1);
    if (got_bytes != required) {
      return;
    }

    len -= (((cue.size - 4) + 1) & ~1);

    /* now we have an array of struct _gst_riff_cuepoints in tempdata */
    points = (struct _gst_riff_cuepoints *) tempdata;

    for (i = 0; i < cue.cuepoints; i++) {
      GstCaps *caps;

      caps = gst_caps_new ("cues",
          "application/x-gst-metadata",
          gst_props_new ("identifier", G_TYPE_INT (points[i].identifier),
              "position", G_TYPE_INT (points[i].offset), NULL));
      cues = g_list_append (cues, caps);
    }

    entry = gst_props_entry_new ("cues", GST_PROPS_GLIST (cues));
    gst_props_add_entry (wavparse->metadata->properties, entry);
  }

  g_object_notify (G_OBJECT (wavparse), "metadata");
}
#endif

static gboolean
gst_wavparse_parse_file_header (GstElement * element, GstBuffer * buf)
{
  guint32 doctype;

  if (!gst_riff_parse_file_header (element, buf, &doctype))
    return FALSE;

  if (doctype != GST_RIFF_RIFF_WAVE) {
    GST_ELEMENT_ERROR (element, STREAM, WRONG_TYPE, (NULL),
        ("File is not an WAVE file: " GST_FOURCC_FORMAT,
            GST_FOURCC_ARGS (doctype)));
    return FALSE;
  }

  return TRUE;
}

static GstFlowReturn
gst_wavparse_stream_init (GstWavParse * wav)
{
  GstFlowReturn res;
  GstBuffer *buf = NULL;

  if ((res = gst_pad_pull_range (wav->sinkpad,
              wav->offset, 12, &buf)) != GST_FLOW_OK)
    return res;
  else if (!gst_wavparse_parse_file_header (GST_ELEMENT (wav), buf))
    return GST_FLOW_ERROR;

  wav->offset += 12;

  return GST_FLOW_OK;
}

#if 0
/* Read 'fmt ' header */
static gboolean
gst_wavparse_fmt (GstWavParse * wav)
{
  gst_riff_strf_auds *header = NULL;
  GstCaps *caps;

  if (!gst_riff_read_strf_auds (wav, &header)) {
    g_warning ("Not fmt");
    return FALSE;
  }

  wav->format = header->format;
  wav->rate = header->rate;
  wav->channels = header->channels;
  if (wav->channels == 0) {
    GST_ELEMENT_ERROR (wav, STREAM, FAILED, (NULL),
        ("Stream claims to contain zero channels - invalid data"));
    g_free (header);
    return FALSE;
  }
  wav->blockalign = header->blockalign;
  wav->width = (header->blockalign * 8) / header->channels;
  wav->depth = header->size;
  wav->bps = header->av_bps;
  if (wav->bps <= 0) {
    GST_ELEMENT_ERROR (wav, STREAM, FAILED, (NULL),
        ("Stream claims to bitrate of <= zero - invalid data"));
    g_free (header);
    return FALSE;
  }

  /* Note: gst_riff_create_audio_caps might nedd to fix values in
   * the header header depending on the format, so call it first */
  caps = gst_riff_create_audio_caps (header->format, NULL, header, NULL);

  g_free (header);

  if (caps) {
    gst_wavparse_create_sourcepad (wav);
    gst_pad_use_fixed_caps (wav->srcpad);
    gst_pad_set_active (wav->srcpad, TRUE);
    gst_pad_set_caps (wav->srcpad, caps);
    gst_caps_free (caps);
    gst_element_add_pad (GST_ELEMENT (wav), wav->srcpad);
    gst_element_no_more_pads (GST_ELEMENT (wav));
    GST_DEBUG ("frequency %d, channels %d", wav->rate, wav->channels);
  } else {
    GST_ELEMENT_ERROR (wav, STREAM, TYPE_NOT_FOUND, (NULL), (NULL));
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_wavparse_other (GstWavParse * wav)
{
  guint32 tag, length;

  if (!gst_riff_peek_head (wav, &tag, &length, NULL)) {
    GST_WARNING_OBJECT (wav, "could not peek head");
    return FALSE;
  }
  GST_DEBUG_OBJECT (wav, "got tag (%08x) %4.4s, length %d", tag,
      (gchar *) & tag, length);

  switch (tag) {
    case GST_RIFF_TAG_LIST:
      if (!(tag = gst_riff_peek_list (wav))) {
        GST_WARNING_OBJECT (wav, "could not peek list");
        return FALSE;
      }

      switch (tag) {
        case GST_RIFF_LIST_INFO:
          if (!gst_riff_read_list (wav, &tag) || !gst_riff_read_info (wav)) {
            GST_WARNING_OBJECT (wav, "could not read list");
            return FALSE;
          }
          break;

        case GST_RIFF_LIST_adtl:
          if (!gst_riff_read_skip (wav)) {
            GST_WARNING_OBJECT (wav, "could not read skip");
            return FALSE;
          }
          break;

        default:
          GST_DEBUG_OBJECT (wav, "skipping tag (%08x) %4.4s", tag,
              (gchar *) & tag);
          if (!gst_riff_read_skip (wav)) {
            GST_WARNING_OBJECT (wav, "could not read skip");
            return FALSE;
          }
          break;
      }

      break;

    case GST_RIFF_TAG_data:
      if (!gst_bytestream_flush (wav->bs, 8)) {
        GST_WARNING_OBJECT (wav, "could not flush 8 bytes");
        return FALSE;
      }

      GST_DEBUG_OBJECT (wav, "switching to data mode");
      wav->state = GST_WAVPARSE_DATA;
      wav->datastart = gst_bytestream_tell (wav->bs);
      if (length == 0) {
        guint64 file_length;

        /* length is 0, data probably stretches to the end
         * of file */
        GST_DEBUG_OBJECT (wav, "length is 0 trying to find length");
        /* get length of file */
        file_length = gst_bytestream_length (wav->bs);
        if (file_length == -1) {
          GST_DEBUG_OBJECT (wav,
              "could not get file length, assuming data to eof");
          /* could not get length, assuming till eof */
          length = G_MAXUINT32;
        }
        if (file_length > G_MAXUINT32) {
          GST_DEBUG_OBJECT (wav, "file length %lld, clipping to 32 bits");
          /* could not get length, assuming till eof */
          length = G_MAXUINT32;
        } else {
          GST_DEBUG_OBJECT (wav, "file length %lld, datalength",
              file_length, length);
          /* substract offset of datastart from length */
          length = file_length - wav->datastart;
          GST_DEBUG_OBJECT (wav, "datalength %lld", length);
        }
      }
      wav->dataleft = wav->datasize = (guint64) length;
      break;

    case GST_RIFF_TAG_cue:
      if (!gst_riff_read_skip (wav)) {
        GST_WARNING_OBJECT (wav, "could not read skip");
        return FALSE;
      }
      break;

    default:
      GST_DEBUG_OBJECT (wav, "skipping tag (%08x) %4.4s", tag, (gchar *) & tag);
      if (!gst_riff_read_skip (wav))
        return FALSE;
      break;
  }

  return TRUE;
}
#endif

static gboolean
gst_wavparse_handle_seek (GstWavParse * wav)
{
  gst_pad_push_event (wav->srcpad, gst_event_new_flush_start ());

  GST_STREAM_LOCK (wav->sinkpad);

  wav->offset = wav->seek_offset + wav->datastart;
  wav->dataleft = wav->datasize - wav->seek_offset;

  /*
     FIXME : currently the seek/discont doesn't care about the stop value ! 
   */
  wav->seek_event = gst_event_new_newsegment (1.0,
      GST_FORMAT_TIME,
      GST_SECOND *
      wav->seek_offset / wav->bps, GST_SECOND * wav->datasize / wav->bps, 0);

  gst_pad_push_event (wav->srcpad, gst_event_new_flush_stop ());

  gst_pad_start_task (wav->sinkpad, (GstTaskFunction) gst_wavparse_loop,
      wav->sinkpad);

  GST_STREAM_UNLOCK (wav->sinkpad);

  return TRUE;

}

static GstFlowReturn
gst_wavparse_stream_headers (GstWavParse * wav)
{
  GstFlowReturn res;
  GstBuffer *buf, *extra;
  gst_riff_strf_auds *header = NULL;
  guint32 tag;
  gboolean gotdata = FALSE;
  GstCaps *caps;

  /* The header start with a 'fmt ' tag */
  if ((res = gst_riff_read_chunk (GST_ELEMENT (wav), wav->sinkpad,
              &wav->offset, &tag, &buf)) != GST_FLOW_OK)
    return res;
  else if (tag != GST_RIFF_TAG_fmt) {
    GST_ELEMENT_ERROR (wav, STREAM, DEMUX, (NULL),
        ("Invalid WAV header (no fmt at start): "
            GST_FOURCC_FORMAT, GST_FOURCC_ARGS (tag)));
    return GST_FLOW_ERROR;
  }
  if (!(gst_riff_parse_strf_auds (GST_ELEMENT (wav), buf, &header, &extra))) {
    GST_ELEMENT_ERROR (wav, STREAM, DEMUX, (NULL),
        ("Couldn't parse audio header"));
    gst_buffer_unref (buf);
    return GST_FLOW_ERROR;
  }

  /* Note: gst_riff_create_audio_caps might nedd to fix values in
   * the header header depending on the format, so call it first */
  caps =
      gst_riff_create_audio_caps (header->format, NULL, header, NULL,
      NULL, NULL);

  wav->format = header->format;
  wav->rate = header->rate;
  wav->channels = header->channels;
  if (wav->channels == 0) {
    GST_ELEMENT_ERROR (wav, STREAM, FAILED, (NULL),
        ("Stream claims to contain no channels - invalid data"));
    g_free (header);
    return GST_FLOW_ERROR;
  }
  wav->blockalign = header->blockalign;
  wav->width = (header->blockalign * 8) / header->channels;
  wav->depth = header->size;
  wav->bps = header->av_bps;
  if (wav->bps <= 0) {
    GST_ELEMENT_ERROR (wav, STREAM, FAILED, (NULL),
        ("Stream claims to have a bitrate of <= zero - invalid data"));
    g_free (header);
    return GST_FLOW_ERROR;
  }

  g_free (header);

  if (caps) {
    gst_wavparse_create_sourcepad (wav);
/*     gst_pad_use_fixed_caps (wav->srcpad); */
    gst_pad_set_active (wav->srcpad, TRUE);
    gst_pad_set_caps (wav->srcpad, caps);
/*     gst_caps_free (caps); */
    gst_element_add_pad (GST_ELEMENT (wav), wav->srcpad);
    gst_element_no_more_pads (GST_ELEMENT (wav));
    GST_DEBUG ("frequency %d, channels %d", wav->rate, wav->channels);
  } else {
    GST_ELEMENT_ERROR (wav, STREAM, TYPE_NOT_FOUND, (NULL),
        ("No caps found for format 0x%x, %d channels, %d Hz",
            wav->format, wav->channels, wav->rate));
    return GST_FLOW_ERROR;
  }


  /* loop headers until we get data */
  while (!gotdata) {
    guint size;
    guint32 tag;

    if ((res =
            gst_pad_pull_range (wav->sinkpad, wav->offset, 8,
                &buf)) != GST_FLOW_OK) {
      GST_ELEMENT_ERROR (wav, STREAM, DEMUX, (NULL),
          ("Couldn't read in header"));
      return GST_FLOW_ERROR;
    }

    /*
       wav is a st00pid format, we don't know for sure where data starts.
       So we have to go bit by bit until we find the 'data' header
     */
    tag = GST_READ_UINT32_LE (GST_BUFFER_DATA (buf));
    size = GST_READ_UINT32_LE (GST_BUFFER_DATA (buf) + 4);
    switch (tag) {
        /* TODO : Implement the various cases */
      case GST_RIFF_TAG_data:
        GST_DEBUG ("Got 'data' TAG, size : %d", size);
        gotdata = TRUE;
        wav->offset += 8;
        wav->datastart = wav->offset;
        wav->datasize = size;
        wav->dataleft = wav->datasize;
        break;
      default:
        GST_DEBUG ("Ignoring tag" GST_FOURCC_FORMAT, GST_FOURCC_ARGS (tag));
        wav->offset += 8 + ((size + 1) & ~1);
    }
    gst_buffer_unref (buf);
  }

  GST_DEBUG ("Finished parsing headers");
  /* Initial discont */
  wav->seek_event = gst_event_new_newsegment (1.0,
      GST_FORMAT_TIME,
      (gint64) 0, (gint64) GST_SECOND * wav->datasize / wav->bps, 0);

  return GST_FLOW_OK;
}

#define MAX_BUFFER_SIZE 4096

static GstFlowReturn
gst_wavparse_stream_data (GstWavParse * wav)
{
  GstBuffer *buf = NULL;
  GstFlowReturn res = GST_FLOW_OK;
  guint64 desired, obtained;

  GST_DEBUG ("stream data !!!");
  /* Get the next n bytes and output them */
  if (wav->dataleft == 0) {
    gst_pad_push_event (wav->srcpad, gst_event_new_eos ());
    return GST_FLOW_WRONG_STATE;
  }

  GST_DEBUG ("offset : %lld , dataleft : %lld", wav->offset, wav->dataleft);

  desired = MIN (wav->dataleft, MAX_BUFFER_SIZE);
  if (desired >= wav->blockalign && wav->blockalign > 0)
    desired -= (desired % wav->blockalign);
  GST_DEBUG ("Fetching %lld bytes of data from the sinkpad.", desired);
  if ((res = gst_pad_pull_range (wav->sinkpad, wav->offset,
              desired, &buf)) != GST_FLOW_OK) {
    GST_DEBUG ("Error getting %ldd bytes from the sinkpad!", desired);
    return res;
  }

  obtained = GST_BUFFER_SIZE (buf);
  GST_BUFFER_TIMESTAMP (buf) =
      GST_SECOND * (wav->datasize - wav->dataleft) / wav->bps;
  GST_BUFFER_DURATION (buf) = GST_SECOND * obtained / wav->bps;
  gst_buffer_set_caps (buf, GST_PAD_CAPS (wav->srcpad));

  GST_DEBUG ("Got buffer. timestamp:%lld , duration:%lld, size:%lld",
      GST_BUFFER_TIMESTAMP (buf),
      GST_BUFFER_DURATION (buf), GST_BUFFER_SIZE (buf));

  if ((res = gst_pad_push (wav->srcpad, buf)) != GST_FLOW_OK) {
    GST_DEBUG ("Error pushing on srcpad");
    return res;
  }
  if (obtained < wav->dataleft) {
    wav->dataleft -= obtained;
    wav->offset += obtained;
  } else {
    wav->dataleft = 0;
  }

  return res;
}

static void
gst_wavparse_loop (GstPad * pad)
{
  GstFlowReturn ret;
  GstWavParse *wav = GST_WAVPARSE (GST_PAD_PARENT (pad));

  switch (wav->state) {
    case GST_WAVPARSE_START:
      if ((ret = gst_wavparse_stream_init (wav)) != GST_FLOW_OK)
        goto pause;

      wav->state = GST_WAVPARSE_HEADER;
      /* fall-through */

    case GST_WAVPARSE_HEADER:
      if ((ret = gst_wavparse_stream_headers (wav)) != GST_FLOW_OK) {
        goto pause;
      }

      wav->state = GST_WAVPARSE_DATA;
      /* fall-through */
    case GST_WAVPARSE_DATA:
      if (wav->seek_event) {
        gst_pad_push_event (wav->srcpad, wav->seek_event);
        wav->seek_event = NULL;
      }
      if ((ret = gst_wavparse_stream_data (wav)) != GST_FLOW_OK)
        goto pause;
      break;
    default:
      g_assert_not_reached ();

  }

  return;

pause:
  GST_LOG_OBJECT (wav, "pausing task %d", ret);
  gst_pad_pause_task (wav->sinkpad);
  if (GST_FLOW_IS_FATAL (ret)) {
    /* for fatal errors we post an error message */
    GST_ELEMENT_ERROR (wav, STREAM, STOPPED,
        ("streaming stopped, reason %d", ret),
        ("streaming stopped, reason %d", ret));
    gst_pad_push_event (wav->srcpad, gst_event_new_eos ());
  }
}

#if 0
/* convert and query stuff */
static const GstFormat *
gst_wavparse_get_formats (GstPad * pad)
{
  static GstFormat formats[] = {
    GST_FORMAT_TIME,
    GST_FORMAT_BYTES,
    GST_FORMAT_DEFAULT,         /* a "frame", ie a set of samples per Hz */
    0
  };

  return formats;
}
#endif

static gboolean
gst_wavparse_pad_convert (GstPad * pad,
    GstFormat src_format, gint64 src_value,
    GstFormat * dest_format, gint64 * dest_value)
{
  guint bytes_per_sample, byterate;
  GstWavParse *wavparse;

  wavparse = GST_WAVPARSE (gst_pad_get_parent (pad));

  bytes_per_sample = wavparse->channels * wavparse->width / 8;
  if (bytes_per_sample == 0) {
    GST_DEBUG
        ("bytes_per_sample 0, probably an mp3 - channels %d, width %d",
        wavparse->channels, wavparse->width);
    return FALSE;
  }
  byterate = wavparse->bps;
  if (byterate == 0) {
    g_warning ("byterate is 0, internal error\n");
    return FALSE;
  }
  GST_DEBUG ("bytes per sample: %d", bytes_per_sample);

  switch (src_format) {
    case GST_FORMAT_BYTES:
      switch (*dest_format) {
        case GST_FORMAT_DEFAULT:
          *dest_value = src_value / bytes_per_sample;
          break;
        case GST_FORMAT_TIME:
          *dest_value = src_value * GST_SECOND / byterate;
          break;
        default:
          return FALSE;
      }
      *dest_value -= *dest_value % bytes_per_sample;
      break;

    case GST_FORMAT_DEFAULT:
      switch (*dest_format) {
        case GST_FORMAT_BYTES:
          *dest_value = src_value * bytes_per_sample;
          break;
        case GST_FORMAT_TIME:
          *dest_value = src_value * GST_SECOND / wavparse->rate;
          break;
        default:
          return FALSE;
      }
      break;

    case GST_FORMAT_TIME:
      switch (*dest_format) {
        case GST_FORMAT_BYTES:
          /* make sure we end up on a sample boundary */
          *dest_value =
              (src_value * wavparse->rate / GST_SECOND) * wavparse->blockalign;
          break;
        case GST_FORMAT_DEFAULT:
          *dest_value = src_value * wavparse->rate / GST_SECOND;
          break;
        default:
          return FALSE;
      }
      break;

    default:
      return FALSE;
  }

  return TRUE;
}

static const GstQueryType *
gst_wavparse_get_query_types (GstPad * pad)
{
  static const GstQueryType types[] = {
    GST_QUERY_POSITION,
    0
  };

  return types;
}

/* handle queries for location and length in requested format */
static gboolean
gst_wavparse_pad_query (GstPad * pad, GstQuery * query)
{
  gboolean res = TRUE;
  GstWavParse *wav = GST_WAVPARSE (GST_PAD_PARENT (pad));

  /* only if we know */
  if (wav->state != GST_WAVPARSE_DATA)
    return FALSE;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_POSITION:
    {
      gint64 curb, endb;
      gint64 cur, end;
      GstFormat format;
      gboolean res = TRUE;

      curb = wav->datasize - wav->dataleft;
      endb = wav->datasize;
      gst_query_parse_position (query, &format, NULL, NULL);

      switch (format) {
        case GST_FORMAT_TIME:
          res &=
              gst_wavparse_pad_convert (pad, GST_FORMAT_BYTES, curb,
              &format, &cur);
          res &=
              gst_wavparse_pad_convert (pad, GST_FORMAT_BYTES, endb,
              &format, &end);
          break;
        default:
          format = GST_FORMAT_BYTES;
          cur = curb;
          end = endb;
          break;
      }
      if (res)
        gst_query_set_position (query, format, cur, end);
      break;
    }
    case GST_QUERY_CONVERT:
    {
      gint64 srcvalue, dstvalue;
      GstFormat srcformat, dstformat;

      gst_query_parse_convert (query, &srcformat, &srcvalue,
          &dstformat, &dstvalue);
      res &=
          gst_wavparse_pad_convert (pad, srcformat, srcvalue,
          &dstformat, &dstvalue);
      if (res)
        gst_query_set_convert (query, srcformat, srcvalue, dstformat, dstvalue);
      break;
    }
    default:
      res = FALSE;
      break;
  }
  return res;
}

static gboolean
gst_wavparse_srcpad_event (GstPad * pad, GstEvent * event)
{
  GstWavParse *wavparse = GST_WAVPARSE (GST_PAD_PARENT (pad));
  gboolean res = TRUE;

  GST_DEBUG ("event %d", GST_EVENT_TYPE (event));

  /* TODO : we need to call handle_seek */

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
    {
      gint64 bseek_start, bseek_stop;
      GstFormat format;
      GstFormat dformat = GST_FORMAT_BYTES;
      gint64 cur, stop;

      gst_event_parse_seek (event, NULL, &format, NULL,
          NULL, &cur, NULL, &stop);

      GST_DEBUG ("seek format %d", format);

      /* find the corresponding bytestream position */
      if (format == GST_FORMAT_BYTES) {
        bseek_start = cur;
        bseek_stop = stop;
      } else {
        res &= gst_wavparse_pad_convert (pad, format,
            cur, &dformat, &bseek_start);
        res &= gst_wavparse_pad_convert (pad, format,
            stop, &dformat, &bseek_stop);
        if (!res)
          return res;
      }

      if (bseek_start > wavparse->datasize) {
        GST_WARNING ("seeking after end of data!");
        return FALSE;
      }

      wavparse->seek_offset = bseek_start;
      gst_wavparse_handle_seek (wavparse);
      break;
    }
    default:
      res = FALSE;
      break;
  }

  gst_event_unref (event);

  return res;
}

static gboolean
gst_wavparse_sink_activate (GstPad * sinkpad)
{
  if (gst_pad_check_pull_range (sinkpad))
    return gst_pad_activate_pull (sinkpad, TRUE);

  return FALSE;
};

static gboolean
gst_wavparse_sink_activate_pull (GstPad * sinkpad, gboolean active)
{
  if (active) {
    /* if we have a scheduler we can start the task */
    gst_pad_start_task (sinkpad, (GstTaskFunction) gst_wavparse_loop, sinkpad);
  } else {
    gst_pad_stop_task (sinkpad);
  }

  return TRUE;
};

static GstElementStateReturn
gst_wavparse_change_state (GstElement * element)
{
  GstWavParse *wav = GST_WAVPARSE (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_NULL_TO_READY:
      break;

    case GST_STATE_READY_TO_PAUSED:
      wav->state = GST_WAVPARSE_START;
      break;

    case GST_STATE_PAUSED_TO_PLAYING:
      break;

    case GST_STATE_PLAYING_TO_PAUSED:
      break;

    case GST_STATE_PAUSED_TO_READY:
      gst_wavparse_destroy_sourcepad (wav);
      gst_wavparse_reset (wav);
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
  gst_riff_init ();

  return gst_element_register (plugin, "wavparse", GST_RANK_PRIMARY,
      GST_TYPE_WAVPARSE);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "wavparse",
    "Parse a .wav file into raw audio",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE, GST_ORIGIN)
