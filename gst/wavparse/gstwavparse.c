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

#include <gstwavparse.h>

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

/* elementfactory information */
static GstElementDetails gst_wavparse_details =
GST_ELEMENT_DETAILS (".wav demuxer",
    "Codec/Demuxer",
    "Parse a .wav file into raw audio",
    "Erik Walthinsen <omega@cse.ogi.edu>");

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
  PROP_0,
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
        g_type_register_static (GST_TYPE_ELEMENT, "GstWavParse", &wavparse_info,
        0);
  }
  return wavparse_type;
}


static void
gst_wavparse_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

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
}

static void
gst_wavparse_init (GstWavParse * wavparse)
{
  /* sink */
  wavparse->sinkpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&sink_template_factory), "sink");
  gst_element_add_pad (GST_ELEMENT (wavparse), wavparse->sinkpad);

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

  wavparse->state = GST_WAVPARSE_UNKNOWN;
  wavparse->bps = 0;
  wavparse->seek_pending = FALSE;
  wavparse->seek_offset = 0;
}

static void
gst_wavparse_destroy_sourcepad (GstWavParse * wavparse)
{
  gst_element_remove_pad (GST_ELEMENT (wavparse), wavparse->srcpad);
  wavparse->srcpad = NULL;
}

static void
gst_wavparse_create_sourcepad (GstWavParse * wavparse)
{
  if (wavparse->srcpad)
    gst_wavparse_destroy_sourcepad (wavparse);

  /* source */
  wavparse->srcpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&src_template_factory), "src");
  gst_pad_use_explicit_caps (wavparse->srcpad);
  /*gst_element_add_pad (GST_ELEMENT (wavparse), wavparse->srcpad); */
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
gst_wavparse_parse_info (GstWavParse * wavparse, int len)
{
  gst_riff_chunk *temp_chunk, chunk;
  GstByteStream *bs = wavparse->bs;
  guint8 *tempdata;
  guint32 got_bytes;
  char *name, *type;

  while (len > 0) {
    got_bytes =
        gst_bytestream_peek_bytes (bs, &tempdata, sizeof (gst_riff_chunk));
    temp_chunk = (gst_riff_chunk *) tempdata;

    chunk.id = GUINT32_FROM_LE (temp_chunk->id);
    chunk.size = GUINT32_FROM_LE (temp_chunk->size);

    gst_bytestream_flush (bs, sizeof (gst_riff_chunk));
    if (got_bytes != sizeof (gst_riff_chunk)) {
      return;
    }

    /* move our pointer on past the header */
    len -= sizeof (gst_riff_chunk);

    if (chunk.size == 0) {
      continue;
    }

    got_bytes = gst_bytestream_peek_bytes (bs, &tempdata, chunk.size);
    name = (char *) tempdata;
    if (got_bytes != chunk.size) {
      return;
    }

    /* move our pointer on past the data ... on an even boundary */
    gst_bytestream_flush (bs, (chunk.size + 1) & ~1);
    len -= ((chunk.size + 1) & ~1);

    /* We now have an info string in 'name' of type chunk.id
       - find type */
    switch (chunk.id) {
      case GST_RIFF_INFO_IARL:
        type = "Location";
        break;

      case GST_RIFF_INFO_IART:
        type = "Artist";
        break;

      case GST_RIFF_INFO_ICMS:
        type = "Commissioner";
        break;

      case GST_RIFF_INFO_ICMT:
        type = "Comment";
        break;

      case GST_RIFF_INFO_ICOP:
        type = "Copyright";
        break;

      case GST_RIFF_INFO_ICRD:
        type = "Creation Date";
        break;

      case GST_RIFF_INFO_IENG:
        type = "Engineer";
        break;

      case GST_RIFF_INFO_IGNR:
        type = "Genre";
        break;

      case GST_RIFF_INFO_IKEY:
        type = "Keywords";
        break;

      case GST_RIFF_INFO_INAM:
        type = "Title";         /* name */
        break;

      case GST_RIFF_INFO_IPRD:
        type = "Product";
        break;

      case GST_RIFF_INFO_ISBJ:
        type = "Subject";
        break;

      case GST_RIFF_INFO_ISFT:
        type = "Software";
        break;

      case GST_RIFF_INFO_ITCH:
        type = "Technician";
        break;

      default:
        g_print ("Unknown: %4.4s\n", (char *) &chunk.id);
        type = NULL;
        break;
    }

    if (type) {
      GstPropsEntry *entry;

      entry = gst_props_entry_new (type, G_TYPE_STRING (name));
      gst_props_add_entry (wavparse->metadata->properties, entry);
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

static void
gst_wavparse_parse_fmt (GstWavParse * wavparse, guint size)
{
  GstWavParseFormat *format;
  GstCaps *caps = NULL;
  guint8 *fmtdata;
  GstByteStream *bs = wavparse->bs;
  guint32 got_bytes;

  got_bytes =
      gst_bytestream_peek_bytes (bs, &fmtdata, sizeof (GstWavParseFormat));
  format = (GstWavParseFormat *) fmtdata;

  if (got_bytes == sizeof (GstWavParseFormat)) {
    gst_bytestream_flush (bs, size);
    wavparse->bps = GUINT16_FROM_LE (format->wBlockAlign);
    wavparse->rate = GUINT32_FROM_LE (format->dwSamplesPerSec);
    wavparse->channels = GUINT16_FROM_LE (format->wChannels);
    wavparse->width = GUINT16_FROM_LE (format->wBitsPerSample);
    wavparse->format = GINT16_FROM_LE (format->wFormatTag);

    gst_wavparse_create_sourcepad (wavparse);

    /* set the caps on the src pad */
    /* FIXME: handle all of the other formats as well */
    switch (wavparse->format) {
      case GST_RIFF_WAVE_FORMAT_ALAW:
      case GST_RIFF_WAVE_FORMAT_MULAW:{
        char *mime = (wavparse->format == GST_RIFF_WAVE_FORMAT_ALAW) ?
            "audio/x-alaw" : "audio/x-mulaw";
        if (wavparse->width != 8) {
          g_warning ("Ignoring invalid width %d", wavparse->width);
          return;
        }

        caps = gst_caps_new_simple (mime,
            "rate", G_TYPE_INT, wavparse->rate,
            "channels", G_TYPE_INT, wavparse->channels, NULL);
      }
        break;

      case GST_RIFF_WAVE_FORMAT_PCM:
        caps = gst_caps_new_simple ("audio/x-raw-int",
            "endianness", G_TYPE_INT, G_LITTLE_ENDIAN,
            "signed", G_TYPE_BOOLEAN, (wavparse->width > 8) ? TRUE : FALSE,
            "width", G_TYPE_INT, wavparse->width,
            "depth", G_TYPE_INT, wavparse->width,
            "rate", G_TYPE_INT, wavparse->rate,
            "channels", G_TYPE_INT, wavparse->channels, NULL);
        break;

      case GST_RIFF_WAVE_FORMAT_ADPCM:
        caps = gst_caps_new_simple ("audio/x-adpcm",
            "layout", G_TYPE_STRING, "microsoft",
            "block_align", G_TYPE_INT, wavparse->bps,
            "rate", G_TYPE_INT, wavparse->rate,
            "channels", G_TYPE_INT, wavparse->channels, NULL);
        break;

      case GST_RIFF_WAVE_FORMAT_MPEGL12:
      case GST_RIFF_WAVE_FORMAT_MPEGL3:{
        int layer = (wavparse->format == GST_RIFF_WAVE_FORMAT_MPEGL12) ? 2 : 3;

        caps = gst_caps_new_simple ("audio/mpeg",
            "mpegversion", G_TYPE_INT, 1,
            "layer", G_TYPE_INT, layer,
            "rate", G_TYPE_INT, wavparse->rate,
            "channels", G_TYPE_INT, wavparse->channels, NULL);
      }
        break;

      default:
        GST_ELEMENT_ERROR (wavparse, STREAM, NOT_IMPLEMENTED, (NULL),
            ("format %d not handled", wavparse->format));
        return;
    }

    if (caps) {
      gst_pad_set_explicit_caps (wavparse->srcpad, caps);
      gst_caps_free (caps);
    }
    gst_element_add_pad (GST_ELEMENT (wavparse), wavparse->srcpad);

    GST_DEBUG ("frequency %d, channels %d", wavparse->rate, wavparse->channels);
  }
}

static gboolean
gst_wavparse_handle_sink_event (GstWavParse * wavparse)
{
  guint32 remaining;
  GstEvent *event;
  GstEventType type;
  gboolean res = TRUE;

  gst_bytestream_get_status (wavparse->bs, &remaining, &event);

  type = event ? GST_EVENT_TYPE (event) : GST_EVENT_UNKNOWN;
  GST_DEBUG ("wavparse: event %p %d", event, type);

  switch (type) {
    case GST_EVENT_EOS:
      gst_bytestream_flush (wavparse->bs, remaining);
      gst_pad_event_default (wavparse->sinkpad, event);
      res = FALSE;
      goto done;

    case GST_EVENT_FLUSH:
      g_warning ("Wavparse: Flush event");
      break;

    default:
      g_warning ("Wavparse: Unhandled event %d", type);
      break;
  }

  gst_event_unref (event);

done:
  return res;
}

static void
gst_wavparse_loop (GstElement * element)
{
  GstWavParse *wavparse;
  gst_riff_riff chunk;
  guint32 flush = 0;
  guint32 got_bytes;
  GstByteStream *bs;

  wavparse = GST_WAVPARSE (element);

  bs = wavparse->bs;

  if (wavparse->seek_pending) {
    GST_DEBUG ("wavparse: seek pending to %" G_GINT64_FORMAT " %08llx",
        wavparse->seek_offset, (unsigned long long) wavparse->seek_offset);

    if (!gst_bytestream_seek (bs, wavparse->seek_offset, GST_SEEK_METHOD_SET)) {
      GST_INFO ("wavparse: Could not seek");
    }

    wavparse->seek_pending = FALSE;
  }

  if (wavparse->state == GST_WAVPARSE_DATA) {
    GstBuffer *buf;
    int desired;

    /* This seems to want the whole chunk,
       Will this screw up streaming?
       Does anyone care about streaming wavs?
       FIXME: Should we have a decent buffer size? */

#define MAX_BUFFER_SIZE 4096

    if (wavparse->dataleft > 0) {
      desired = MIN (wavparse->dataleft, MAX_BUFFER_SIZE);
      got_bytes = gst_bytestream_peek (bs, &buf, desired);

      if (got_bytes != desired) {
        /* EOS? */
        GstEvent *event;
        guint32 remaining;

        gst_bytestream_get_status (bs, &remaining, &event);
        if (event) {
          gst_pad_event_default (wavparse->sinkpad, event);
        } else {
          GST_ELEMENT_ERROR (element, RESOURCE, READ, (NULL), (NULL));
        }
        return;
      }

      wavparse->dataleft -= got_bytes;
      wavparse->byteoffset += got_bytes;

      gst_bytestream_flush (bs, got_bytes);

      gst_pad_push (wavparse->srcpad, GST_DATA (buf));
      return;
    } else {
      wavparse->state = GST_WAVPARSE_OTHER;
    }
  }

  do {
    gst_riff_riff *temp_chunk;
    guint8 *tempdata;
    guint32 skipsize;

    /* read first two dwords to get chunktype and size */
    while (TRUE) {
      got_bytes =
          gst_bytestream_peek_bytes (bs, &tempdata, sizeof (gst_riff_chunk));
      temp_chunk = (gst_riff_riff *) tempdata;

      if (got_bytes < sizeof (gst_riff_chunk)) {
        if (!gst_wavparse_handle_sink_event (wavparse)) {
          return;
        }
      } else {
        break;
      }
    }

    chunk.id = GUINT32_FROM_LE (temp_chunk->id);
    chunk.size = GUINT32_FROM_LE (temp_chunk->size);

    switch (chunk.id) {
      case GST_RIFF_TAG_RIFF:
      case GST_RIFF_TAG_LIST:
        /* Read complete list chunk */
        while (TRUE) {
          got_bytes =
              gst_bytestream_peek_bytes (bs, &tempdata, sizeof (gst_riff_list));
          temp_chunk = (gst_riff_riff *) tempdata;
          if (got_bytes < sizeof (gst_riff_list)) {
            if (!gst_wavparse_handle_sink_event (wavparse)) {
              return;
            }
          } else {
            break;
          }
        }

        chunk.type = GUINT32_FROM_LE (temp_chunk->type);
        skipsize = sizeof (gst_riff_list);
        break;

      case GST_RIFF_TAG_cue:
        skipsize = 0;
        break;

      default:
        skipsize = sizeof (gst_riff_chunk);
        break;
    }
    gst_bytestream_flush (bs, skipsize);
  } while (FALSE);

  /* need to flush an even number of bytes at the end */
  flush = (chunk.size + 1) & ~1;

  switch (wavparse->state) {
    case GST_WAVPARSE_START:
      if (chunk.id != GST_RIFF_TAG_RIFF && chunk.type != GST_RIFF_RIFF_WAVE) {
        GST_ELEMENT_ERROR (element, STREAM, WRONG_TYPE, (NULL),
            ("chunk.id %08x chunk.type %08x", chunk.id, chunk.type));
        return;
      }

      wavparse->state = GST_WAVPARSE_OTHER;
      /* We are not going to flush lists */
      flush = 0;
      break;

    case GST_WAVPARSE_OTHER:
      GST_DEBUG ("riff tag: %4.4s %08x", (char *) &chunk.id, chunk.size);

      switch (chunk.id) {
        case GST_RIFF_TAG_data:
          wavparse->state = GST_WAVPARSE_DATA;
          wavparse->dataleft = chunk.size;
          wavparse->byteoffset = 0;
          flush = 0;
          break;

        case GST_RIFF_TAG_fmt:
          gst_wavparse_parse_fmt (wavparse, chunk.size);
          flush = 0;
          break;

        case GST_RIFF_TAG_cue:
          //gst_wavparse_parse_cues (wavparse, chunk.size);
          break;

        case GST_RIFF_TAG_LIST:
          GST_DEBUG ("list type: %4.4s", (char *) &chunk.type);
          switch (chunk.type) {
            case GST_RIFF_LIST_INFO:
              //gst_wavparse_parse_info (wavparse, chunk.size - 4);
              //flush = 0;

              break;

            case GST_RIFF_LIST_adtl:
              //gst_wavparse_parse_adtl (wavparse, chunk.size - 4);
              //flush = 0;
              break;

            default:
              //flush = 0;
              break;
          }

        default:
          GST_DEBUG ("  *****  unknown chunkid %08x", chunk.id);
          //flush = 0;
          break;
      }
      break;

    case GST_WAVPARSE_DATA:
      /* Should have been handled up there ^^^^ */
      flush = 0;
      break;

    default:
      /* Unknown */
      g_warning ("Unknown state %d\n", wavparse->state);
      //GST_DEBUG ("  *****  unknown chunkid %08x", chunk.id);
      break;
  }

  if (flush > 0) {
    gboolean res;

    res = gst_bytestream_flush (bs, flush);
    if (!res) {
      guint32 remaining;
      GstEvent *event;

      gst_bytestream_get_status (bs, &remaining, &event);
      gst_event_unref (event);
    }
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
  gint bytes_per_sample;
  glong byterate;
  GstWavParse *wavparse;
  const GstFormat *formats;
  gboolean src_format_ok = FALSE;
  gboolean dest_format_ok = FALSE;

  wavparse = GST_WAVPARSE (gst_pad_get_parent (pad));

  bytes_per_sample = wavparse->channels * wavparse->width / 8;
  if (bytes_per_sample == 0) {
    GST_DEBUG ("bytes_per_sample 0, probably an mp3 - channels %d, width %d",
        wavparse->channels, wavparse->width);
    return FALSE;
  }
  byterate = (glong) (bytes_per_sample * wavparse->rate);
  if (byterate == 0) {
    g_warning ("byterate is 0, internal error\n");
    return FALSE;
  }
  GST_DEBUG ("bytes per sample: %d", bytes_per_sample);
  /* check if both src_format and sink_format are in the supported formats */
  formats = gst_pad_get_formats (pad);

  while (formats && *formats) {
    if (src_format == *formats) {
      src_format_ok = TRUE;
    }
    if (*dest_format == *formats) {
      dest_format_ok = TRUE;
    }
    formats++;
  }
  if (!src_format_ok || !dest_format_ok) {
    GST_DEBUG ("src or dest format not supported");
    return FALSE;
  }


  switch (src_format) {
    case GST_FORMAT_BYTES:
      if (*dest_format == GST_FORMAT_DEFAULT)
        *dest_value = src_value / bytes_per_sample;
      else if (*dest_format == GST_FORMAT_TIME)
        *dest_value = src_value * GST_SECOND / byterate;
      else {
        GST_DEBUG ("can't convert from bytes to other than units/time");
        return FALSE;
      }

      break;
    case GST_FORMAT_DEFAULT:
      if (*dest_format == GST_FORMAT_BYTES)
        *dest_value = src_value * bytes_per_sample;
      else if (*dest_format == GST_FORMAT_TIME)
        *dest_value = src_value * GST_SECOND / wavparse->rate;
      else {
        GST_DEBUG ("can't convert from units to other than bytes/time");
        return FALSE;
      }
      break;
    case GST_FORMAT_TIME:
      if (*dest_format == GST_FORMAT_BYTES)
        *dest_value = src_value * byterate / GST_SECOND;
      else if (*dest_format == GST_FORMAT_DEFAULT)
        *dest_value = src_value * wavparse->rate / GST_SECOND;
      else {
        GST_DEBUG ("can't convert from time to other than bytes/units");
        return FALSE;
      }

      *dest_value = *dest_value & ~(bytes_per_sample - 1);
      break;
    default:
      g_warning ("unhandled format for wavparse\n");
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
  GstFormat peer_format = GST_FORMAT_BYTES;
  gint64 peer_value;
  GstWavParse *wavparse;

  /* probe sink's peer pad, convert value, and that's it :) */
  /* FIXME: ideally we'd loop over possible formats of peer instead
   * of only using BYTE */

  /* only support byte, time and unit queries */
  wavparse = GST_WAVPARSE (gst_pad_get_parent (pad));
  if (!gst_pad_query (GST_PAD_PEER (wavparse->sinkpad), type,
          &peer_format, &peer_value)) {
    GST_DEBUG ("Could not query sink pad's peer");
    return FALSE;
  }
  if (!gst_pad_convert (wavparse->sinkpad, peer_format, peer_value,
          format, value)) {
    GST_DEBUG ("Could not convert sink pad's peer");
    return FALSE;
  }
  GST_DEBUG ("pad_query done, value %" G_GINT64_FORMAT "\n", *value);
  return TRUE;
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
#if 0
  GstWavParse *wavparse = GST_WAVPARSE (GST_PAD_PARENT (pad));
  gboolean res = FALSE;

  GST_DEBUG ("event %d", GST_EVENT_TYPE (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
    {
      gint64 byteoffset;
      GstFormat format;

      /* we can only seek when in the DATA state */
      if (wavparse->state != GST_WAVPARSE_DATA) {
        return FALSE;
      }

      format = GST_FORMAT_BYTES;

      /* bring format to bytes for the peer element, 
       * FIXME be smarter here */
      res = gst_pad_convert (pad,
          GST_EVENT_SEEK_FORMAT (event),
          GST_EVENT_SEEK_OFFSET (event), &format, &byteoffset);

      if (res) {
        /* ok, seek worked, update our state */
        wavparse->seek_offset = byteoffset;
        wavparse->seek_pending = TRUE;
        wavparse->need_discont = TRUE;
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
  GstWavParse *wavparse = GST_WAVPARSE (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_NULL_TO_READY:
      break;
    case GST_STATE_READY_TO_PAUSED:
      wavparse->bs = gst_bytestream_new (wavparse->sinkpad);
      wavparse->state = GST_WAVPARSE_START;
      break;
    case GST_STATE_PAUSED_TO_PLAYING:
      break;
    case GST_STATE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_PAUSED_TO_READY:
      gst_wavparse_destroy_sourcepad (wavparse);
      gst_bytestream_destroy (wavparse->bs);
      wavparse->state = GST_WAVPARSE_UNKNOWN;
      wavparse->bps = 0;
      wavparse->seek_pending = FALSE;
      wavparse->seek_offset = 0;
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
  if (!gst_library_load ("gstbytestream")) {
    return FALSE;
  }

  return gst_element_register (plugin, "wavparse", GST_RANK_SECONDARY,
      GST_TYPE_WAVPARSE);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "wavparse",
    "Parse a .wav file into raw audio",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE, GST_ORIGIN)
