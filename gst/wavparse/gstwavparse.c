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

static void gst_wavparse_base_init (gpointer g_class);
static void gst_wavparse_class_init (GstWavParseClass * klass);
static void gst_wavparse_init (GstWavParse * wavparse);

static GstElementStateReturn gst_wavparse_change_state (GstElement * element);

static const GstFormat *gst_wavparse_get_formats (GstPad * pad);
static const GstQueryType *gst_wavparse_get_query_types (GstPad * pad);
static gboolean gst_wavparse_pad_query (GstPad * pad,
    GstQueryType type, GstFormat * format, gint64 * value);
static gboolean gst_wavparse_pad_convert (GstPad * pad,
    GstFormat src_format,
    gint64 src_value, GstFormat * dest_format, gint64 * dest_value);

static void gst_wavparse_loop (GstElement * element);
static const GstEventMask *gst_wavparse_get_event_masks (GstPad * pad);
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
        "width = (int) { 8, 16 }, "
        "depth = (int) { 8, 16 }, "
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
        "rate = (int) [ 8000, 48000 ], " "channels = (int) [ 1, 2 ]")
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
        g_type_register_static (GST_TYPE_RIFF_READ, "GstWavParse",
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

  parent_class = g_type_class_ref (GST_TYPE_RIFF_READ);

  object_class->get_property = gst_wavparse_get_property;
  gstelement_class->change_state = gst_wavparse_change_state;
}

static void
gst_wavparse_init (GstWavParse * wavparse)
{
  /* sink */
  wavparse->sinkpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&sink_template_factory), "sink");
  gst_element_add_pad (GST_ELEMENT (wavparse), wavparse->sinkpad);
  GST_RIFF_READ (wavparse)->sinkpad = wavparse->sinkpad;

  gst_pad_set_formats_function (wavparse->sinkpad, gst_wavparse_get_formats);
  gst_pad_set_convert_function (wavparse->sinkpad, gst_wavparse_pad_convert);
  gst_pad_set_query_type_function (wavparse->sinkpad,
      gst_wavparse_get_query_types);
  gst_pad_set_query_function (wavparse->sinkpad, gst_wavparse_pad_query);

#if 0
  /* source */
  wavparse->srcpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&src_template_factory), "src");
  gst_pad_use_explicit_caps (wavparse->srcpad);
  gst_element_add_pad (GST_ELEMENT (wavparse), wavparse->srcpad);
  gst_pad_set_formats_function (wavparse->srcpad, gst_wavparse_get_formats);
  gst_pad_set_convert_function (wavparse->srcpad, gst_wavparse_pad_convert);
  gst_pad_set_query_type_function (wavparse->srcpad,
      gst_wavparse_get_query_types);
  gst_pad_set_query_function (wavparse->srcpad, gst_wavparse_pad_query);
  gst_pad_set_event_function (wavparse->srcpad, gst_wavparse_srcpad_event);
  gst_pad_set_event_mask_function (wavparse->srcpad,
      gst_wavparse_get_event_masks);
#endif

  gst_element_set_loop_function (GST_ELEMENT (wavparse), gst_wavparse_loop);

  wavparse->state = GST_WAVPARSE_START;

  /* These will all be set correctly in the fmt chunk */
  wavparse->depth = 0;
  wavparse->rate = 0;
  wavparse->width = 0;
  wavparse->channels = 0;

  wavparse->seek_pending = FALSE;
  wavparse->seek_offset = 0;
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
  gst_pad_use_explicit_caps (wavparse->srcpad);
  gst_pad_set_formats_function (wavparse->srcpad, gst_wavparse_get_formats);
  gst_pad_set_convert_function (wavparse->srcpad, gst_wavparse_pad_convert);
  gst_pad_set_query_type_function (wavparse->srcpad,
      gst_wavparse_get_query_types);
  gst_pad_set_query_function (wavparse->srcpad, gst_wavparse_pad_query);
  gst_pad_set_event_function (wavparse->srcpad, gst_wavparse_srcpad_event);
  gst_pad_set_event_mask_function (wavparse->srcpad,
      gst_wavparse_get_event_masks);
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
gst_wavparse_stream_init (GstWavParse * wav)
{
  GstRiffRead *riff = GST_RIFF_READ (wav);
  guint32 doctype;

  if (!gst_riff_read_header (riff, &doctype))
    return FALSE;

  if (doctype != GST_RIFF_RIFF_WAVE) {
    GST_ELEMENT_ERROR (wav, STREAM, WRONG_TYPE, (NULL), (NULL));
    return FALSE;
  }

  return TRUE;
}

/* Read 'fmt ' header */
static gboolean
gst_wavparse_fmt (GstWavParse * wav)
{
  GstRiffRead *riff = GST_RIFF_READ (wav);
  gst_riff_strf_auds *header;
  GstCaps *caps;

  if (!gst_riff_read_strf_auds (riff, &header)) {
    g_warning ("Not fmt");
    return FALSE;
  }

  gst_wavparse_create_sourcepad (wav);

  wav->format = header->format;
  wav->rate = header->rate;
  wav->channels = header->channels;
  wav->width = (header->blockalign * 8) / header->channels;
  wav->depth = header->size;
  wav->bps = header->av_bps;

  gst_element_add_pad (GST_ELEMENT (wav), wav->srcpad);

  caps = gst_riff_create_audio_caps (header->format, NULL, header, NULL);

  if (caps) {
    gst_pad_set_explicit_caps (wav->srcpad, caps);
    gst_caps_free (caps);
  }

  GST_DEBUG ("frequency %d, channels %d", wav->rate, wav->channels);

  return TRUE;
}

static gboolean
gst_wavparse_other (GstWavParse * wav)
{
  GstRiffRead *riff = GST_RIFF_READ (wav);
  guint32 tag, length;

  if (!gst_riff_peek_head (riff, &tag, &length, NULL)) {
    return FALSE;
  }

  switch (tag) {
    case GST_RIFF_TAG_LIST:
      if (!(tag = gst_riff_peek_list (riff))) {
        return FALSE;
      }

      switch (tag) {
        case GST_RIFF_LIST_INFO:
          if (!gst_riff_read_list (riff, &tag) || !gst_riff_read_info (riff))
            return FALSE;
          break;

        case GST_RIFF_LIST_adtl:
          if (!gst_riff_read_skip (riff))
            return FALSE;
          break;

        default:
          if (!gst_riff_read_skip (riff))
            return FALSE;
          break;
      }

      break;

    case GST_RIFF_TAG_data:
      if (!gst_bytestream_flush (riff->bs, 8))
        return FALSE;

      wav->state = GST_WAVPARSE_DATA;
      wav->dataleft = wav->datasize = (guint64) length;
      wav->datastart = gst_bytestream_tell (riff->bs);
      break;

    case GST_RIFF_TAG_cue:
      if (!gst_riff_read_skip (riff))
        return FALSE;
      break;

    default:
      if (!gst_riff_read_skip (riff))
        return FALSE;
      break;
  }

  return TRUE;
}

static gboolean
gst_wavparse_handle_seek (GstWavParse * wav)
{
#if 1
  GstRiffRead *riff = GST_RIFF_READ (wav);
  GstEvent *event = NULL;
  guint32 remaining;
  guint8 *data;

  if (!gst_bytestream_seek (riff->bs, wav->seek_offset + wav->datastart,
          GST_SEEK_METHOD_SET))
    return FALSE;

  /* wait for discont */
  while (!event) {
    if (gst_bytestream_peek_bytes (riff->bs, &data, 1)) {
      GST_WARNING ("Unexpected data after seek - this means seek failed");
      return FALSE;
    }

    /* get the discont event and return */
    gst_bytestream_get_status (riff->bs, &remaining, &event);
    if (!event) {
      GST_WARNING ("No discontinuity event after seek - seek failed");
      return FALSE;
    } else if (GST_EVENT_TYPE (event) != GST_EVENT_DISCONTINUOUS) {
      GstEventType type = GST_EVENT_TYPE (event);

      gst_pad_event_default (riff->sinkpad, event);
      if (type == GST_EVENT_EOS)
        return FALSE;

      event = NULL;
    }
  }

  wav->dataleft = wav->datasize - wav->seek_offset;

  gst_event_unref (event);
  event = gst_event_new_discontinuous (FALSE,
      GST_FORMAT_BYTES, wav->seek_offset,
      GST_FORMAT_TIME, GST_SECOND * wav->seek_offset / wav->bps,
      GST_FORMAT_UNDEFINED);
  gst_pad_event_default (wav->sinkpad, event);

  return TRUE;
#else
  return FALSE;
#endif
}

#define MAX_BUFFER_SIZE 4096

static void
gst_wavparse_loop (GstElement * element)
{
  GstWavParse *wav = GST_WAVPARSE (element);
  GstRiffRead *riff = GST_RIFF_READ (wav);

  if (wav->state == GST_WAVPARSE_DATA) {
    /* seek handling */
    if (wav->seek_pending) {
      gst_wavparse_handle_seek (wav);
      wav->seek_pending = FALSE;
    }

    if (wav->dataleft > 0) {
      guint32 got_bytes, desired;
      GstBuffer *buf = NULL;

      desired = MIN (wav->dataleft, MAX_BUFFER_SIZE);
      if (!(buf = gst_riff_read_element_data (riff, desired, &got_bytes)))
        return;
      GST_BUFFER_TIMESTAMP (buf) = GST_SECOND *
          (wav->datasize - wav->dataleft) / wav->bps;
      GST_BUFFER_DURATION (buf) = GST_SECOND * got_bytes / wav->bps;

      gst_pad_push (wav->srcpad, GST_DATA (buf));

      wav->byteoffset += got_bytes;
      if (got_bytes < wav->dataleft) {
        wav->dataleft -= got_bytes;
        return;
      } else {
        wav->dataleft = 0;
        wav->state = GST_WAVPARSE_OTHER;
      }
    } else {
      wav->state = GST_WAVPARSE_OTHER;
    }
  }

  switch (wav->state) {
    case GST_WAVPARSE_START:
      if (!gst_wavparse_stream_init (wav)) {
        return;
      }

      wav->state = GST_WAVPARSE_FMT;
      /* fall-through */

    case GST_WAVPARSE_FMT:
      if (!gst_wavparse_fmt (wav)) {
        return;
      }

      wav->state = GST_WAVPARSE_OTHER;
      /* fall-through */

    case GST_WAVPARSE_OTHER:
      if (!gst_wavparse_other (wav)) {
        return;
      }

      break;

    case GST_WAVPARSE_DATA:

    default:
      g_assert_not_reached ();
  }
}

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
    GST_DEBUG ("bytes_per_sample 0, probably an mp3 - channels %d, width %d",
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
          *dest_value = src_value * byterate / GST_SECOND;
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
    GST_QUERY_TOTAL,
    GST_QUERY_POSITION,
    0
  };

  return types;
}

/* handle queries for location and length in requested format */
static gboolean
gst_wavparse_pad_query (GstPad * pad, GstQueryType type,
    GstFormat * format, gint64 * value)
{
  gint64 bytevalue;
  GstWavParse *wav = GST_WAVPARSE (gst_pad_get_parent (pad));

  /* only if we know */
  if (wav->state != GST_WAVPARSE_DATA)
    return FALSE;

  switch (type) {
    case GST_QUERY_POSITION:
      bytevalue = wav->datasize - wav->dataleft;
      break;
    case GST_QUERY_TOTAL:
      bytevalue = wav->datasize;
      break;
    default:
      return FALSE;
  }

  if (*format == GST_FORMAT_BYTES) {
    *value = bytevalue;
    return TRUE;
  }

  return gst_pad_convert (wav->sinkpad, bytevalue,
      GST_FORMAT_BYTES, format, value);
}

static const GstEventMask *
gst_wavparse_get_event_masks (GstPad * pad)
{
  static const GstEventMask gst_wavparse_src_event_masks[] = {
    {GST_EVENT_SEEK, GST_SEEK_METHOD_SET | GST_SEEK_FLAG_FLUSH},
    {0,}
  };

  return gst_wavparse_src_event_masks;
}

static gboolean
gst_wavparse_srcpad_event (GstPad * pad, GstEvent * event)
{
#if 1
  GstWavParse *wavparse = GST_WAVPARSE (GST_PAD_PARENT (pad));
  gboolean res = FALSE;

  GST_DEBUG ("event %d", GST_EVENT_TYPE (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
    {
      gint64 byteoffset;
      GstFormat format;

      /* bring format to bytes for the peer element, 
       * FIXME be smarter here */
      format = GST_FORMAT_BYTES;
      res = gst_pad_convert (pad,
          GST_EVENT_SEEK_FORMAT (event),
          GST_EVENT_SEEK_OFFSET (event), &format, &byteoffset);

      if (res) {
        /* ok, seek worked, update our state */
        wavparse->seek_offset = byteoffset;
        wavparse->seek_pending = TRUE;
      }
      break;
    }
    default:
      break;
  }

  gst_event_unref (event);

  return res;
#else
  return FALSE;
#endif
}

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
      wav->state = GST_WAVPARSE_START;

      wav->width = 0;
      wav->depth = 0;
      wav->rate = 0;
      wav->channels = 0;

      wav->seek_pending = FALSE;
      wav->seek_offset = 0;
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

  return gst_element_register (plugin, "wavparse", GST_RANK_PRIMARY,
      GST_TYPE_WAVPARSE);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "wavparse",
    "Parse a .wav file into raw audio",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE, GST_ORIGIN)
