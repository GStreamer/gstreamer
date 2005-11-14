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
#include <gst/gst-i18n-plugin.h>

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
static GstStateChangeReturn gst_wavparse_change_state (GstElement * element,
    GstStateChange transition);

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

  wavparse->segment_rate = 1.0;
  wavparse->segment_start = -1;
  wavparse->segment_stop = -1;
}

static void
gst_wavparse_init (GstWavParse * wavparse)
{
  /* sink */
  wavparse->sinkpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&sink_template_factory), "sink");
  gst_element_add_pad (GST_ELEMENT (wavparse), wavparse->sinkpad);
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

  if (doctype != GST_RIFF_RIFF_WAVE)
    goto not_wav;

  return TRUE;

not_wav:
  {
    GST_ELEMENT_ERROR (element, STREAM, WRONG_TYPE, (NULL),
        ("File is not an WAVE file: " GST_FOURCC_FORMAT,
            GST_FOURCC_ARGS (doctype)));
    return FALSE;
  }
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
gst_wavparse_handle_seek (GstWavParse * wav, gboolean update)
{
  GstClockTime start_time, stop_time;
  gboolean flush;

  flush = wav->segment_flags & GST_SEEK_FLAG_FLUSH;

  if (flush)
    gst_pad_push_event (wav->srcpad, gst_event_new_flush_start ());
  else
    gst_pad_pause_task (wav->sinkpad);

  GST_STREAM_LOCK (wav->sinkpad);

  if (update) {
    wav->offset = wav->segment_start + wav->datastart;
    wav->dataleft = wav->segment_stop - wav->segment_start;
    start_time = GST_SECOND * wav->segment_start / wav->bps;
  } else {
    start_time = (wav->offset - wav->datastart) * GST_SECOND / wav->bps;
  }
  stop_time = GST_SECOND * wav->segment_stop / wav->bps;

  GST_DEBUG ("seek: offset %" G_GUINT64_FORMAT ", len %" G_GUINT64_FORMAT
      ", segment %" GST_TIME_FORMAT " -- %" GST_TIME_FORMAT,
      wav->offset, wav->dataleft, GST_TIME_ARGS (start_time),
      GST_TIME_ARGS (stop_time));

  wav->seek_event = gst_event_new_newsegment (!update, wav->segment_rate,
      GST_FORMAT_TIME, start_time, stop_time, start_time);

  if (flush)
    gst_pad_push_event (wav->srcpad, gst_event_new_flush_stop ());

  if (wav->segment_flags & GST_SEEK_FLAG_SEGMENT) {
    gst_element_post_message (GST_ELEMENT (wav),
        gst_message_new_segment_start (GST_OBJECT (wav), GST_FORMAT_TIME,
            start_time));
  }

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

  else if (tag != GST_RIFF_TAG_fmt)
    goto invalid_wav;

  if (!(gst_riff_parse_strf_auds (GST_ELEMENT (wav), buf, &header, &extra)))
    goto parse_header_error;

  /* Note: gst_riff_create_audio_caps might nedd to fix values in
   * the header header depending on the format, so call it first */
  caps =
      gst_riff_create_audio_caps (header->format, NULL, header, NULL,
      NULL, NULL);

  wav->format = header->format;
  wav->rate = header->rate;
  wav->channels = header->channels;

  if (wav->channels == 0)
    goto no_channels;

  wav->blockalign = header->blockalign;
  wav->width = (header->blockalign * 8) / header->channels;
  wav->depth = header->size;
  wav->bps = header->av_bps;

  if (wav->bps <= 0)
    goto no_bitrate;

  wav->bytes_per_sample = wav->channels * wav->width / 8;
  if (wav->bytes_per_sample <= 0)
    goto no_bytes_per_sample;

  g_free (header);

  if (!caps)
    goto unknown_format;

  gst_wavparse_create_sourcepad (wav);
  gst_pad_set_active (wav->srcpad, TRUE);
  gst_pad_set_caps (wav->srcpad, caps);
  gst_caps_unref (caps);
  caps = NULL;

  gst_element_add_pad (GST_ELEMENT (wav), wav->srcpad);
  gst_element_no_more_pads (GST_ELEMENT (wav));

  GST_DEBUG ("frequency %d, channels %d", wav->rate, wav->channels);

  /* loop headers until we get data */
  while (!gotdata) {
    guint size;
    guint32 tag;

    if ((res =
            gst_pad_pull_range (wav->sinkpad, wav->offset, 8,
                &buf)) != GST_FLOW_OK)
      goto header_read_error;

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

  wav->segment_start = 0;
  /* FIXME, can overflow */
  wav->segment_stop = (gint64) GST_SECOND *wav->datasize / wav->bps;

  /* Initial discont */
  wav->seek_event = gst_event_new_newsegment (FALSE, 1.0,
      GST_FORMAT_TIME,
      wav->segment_start, wav->segment_stop, wav->segment_start);

  return GST_FLOW_OK;

  /* ERROR */
invalid_wav:
  {
    GST_ELEMENT_ERROR (wav, STREAM, DEMUX, (NULL),
        ("Invalid WAV header (no fmt at start): "
            GST_FOURCC_FORMAT, GST_FOURCC_ARGS (tag)));
    return GST_FLOW_ERROR;
  }
parse_header_error:
  {
    GST_ELEMENT_ERROR (wav, STREAM, DEMUX, (NULL),
        ("Couldn't parse audio header"));
    gst_buffer_unref (buf);
    return GST_FLOW_ERROR;
  }
no_channels:
  {
    GST_ELEMENT_ERROR (wav, STREAM, FAILED, (NULL),
        ("Stream claims to contain no channels - invalid data"));
    g_free (header);
    return GST_FLOW_ERROR;
  }
no_bitrate:
  {
    GST_ELEMENT_ERROR (wav, STREAM, FAILED, (NULL),
        ("Stream claims to have a bitrate of <= zero - invalid data"));
    g_free (header);
    return GST_FLOW_ERROR;
  }
no_bytes_per_sample:
  {
    GST_ELEMENT_ERROR (wav, STREAM, FAILED, (NULL),
        ("could not caluclate bytes per sample - invalid data"));
    g_free (header);
    return GST_FLOW_ERROR;
  }
unknown_format:
  {
    GST_ELEMENT_ERROR (wav, STREAM, TYPE_NOT_FOUND, (NULL),
        ("No caps found for format 0x%x, %d channels, %d Hz",
            wav->format, wav->channels, wav->rate));
    return GST_FLOW_ERROR;
  }
header_read_error:
  {
    GST_ELEMENT_ERROR (wav, STREAM, DEMUX, (NULL), ("Couldn't read in header"));
    return GST_FLOW_ERROR;
  }
}

#define MAX_BUFFER_SIZE 4096

static GstFlowReturn
gst_wavparse_stream_data (GstWavParse * wav)
{
  GstBuffer *buf = NULL;
  GstFlowReturn res = GST_FLOW_OK;
  guint64 desired, obtained;

  GST_DEBUG ("offset : %lld , dataleft : %lld", wav->offset, wav->dataleft);

  /* Get the next n bytes and output them */
  if (wav->dataleft == 0)
    goto found_eos;

  desired = MIN (wav->dataleft, MAX_BUFFER_SIZE * ABS (wav->segment_rate));
  if (desired >= wav->blockalign && wav->blockalign > 0)
    desired -= (desired % wav->blockalign);

  GST_DEBUG ("Fetching %lld bytes of data from the sinkpad.", desired);

  if ((res = gst_pad_pull_range (wav->sinkpad, wav->offset,
              desired, &buf)) != GST_FLOW_OK)
    goto pull_error;

  obtained = GST_BUFFER_SIZE (buf);
  GST_BUFFER_OFFSET (buf) =
      (wav->offset - wav->datastart) / wav->bytes_per_sample;
  GST_BUFFER_TIMESTAMP (buf) =
      GST_SECOND * (wav->offset - wav->datastart) / wav->bps;
  GST_BUFFER_DURATION (buf) = 1 + GST_SECOND * obtained / wav->bps;
  gst_buffer_set_caps (buf, GST_PAD_CAPS (wav->srcpad));

  GST_DEBUG ("Got buffer. timestamp:%" GST_TIME_FORMAT " , duration:%"
      GST_TIME_FORMAT ", size:%u",
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)),
      GST_TIME_ARGS (GST_BUFFER_DURATION (buf)), GST_BUFFER_SIZE (buf));

  if ((res = gst_pad_push (wav->srcpad, buf)) != GST_FLOW_OK)
    goto push_error;

  if (obtained < wav->dataleft) {
    wav->dataleft -= obtained;
    wav->offset += obtained;
  } else {
    wav->dataleft = 0;
  }
  return res;

  /* ERROR */
found_eos:
  {
    GST_DEBUG ("found EOS");
    if (wav->segment_flags & GST_SEEK_FLAG_SEGMENT) {
      GstClockTime stop_time;

      stop_time = GST_SECOND * wav->segment_stop / wav->bps;

      gst_element_post_message (GST_ELEMENT (wav),
          gst_message_new_segment_done (GST_OBJECT (wav), GST_FORMAT_TIME,
              stop_time));
    } else {
      gst_pad_push_event (wav->srcpad, gst_event_new_eos ());
    }
    return GST_FLOW_WRONG_STATE;
  }
pull_error:
  {
    GST_DEBUG ("Error getting %ldd bytes from the sinkpad!", desired);
    return res;
  }
push_error:
  {
    GST_DEBUG ("Error pushing on srcpad");
    return res;
  }
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
      if ((ret = gst_wavparse_stream_headers (wav)) != GST_FLOW_OK)
        goto pause;

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
    GST_ELEMENT_ERROR (wav, STREAM, FAILED,
        (_("Internal data stream error.")),
        ("streaming stopped, reason %s", gst_flow_get_name (ret)));
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
  GstWavParse *wavparse;
  gboolean res = TRUE;

  wavparse = GST_WAVPARSE (gst_pad_get_parent (pad));

  if (wavparse->bytes_per_sample == 0)
    goto no_bytes_per_sample;

  if (wavparse->bps == 0)
    goto no_bps;

  switch (src_format) {
    case GST_FORMAT_BYTES:
      switch (*dest_format) {
        case GST_FORMAT_DEFAULT:
          *dest_value = src_value / wavparse->bytes_per_sample;
          break;
        case GST_FORMAT_TIME:
          *dest_value = src_value * GST_SECOND / wavparse->bps;
          break;
        default:
          res = FALSE;
          goto done;
      }
      *dest_value -= *dest_value % wavparse->bytes_per_sample;
      break;

    case GST_FORMAT_DEFAULT:
      switch (*dest_format) {
        case GST_FORMAT_BYTES:
          *dest_value = src_value * wavparse->bytes_per_sample;
          break;
        case GST_FORMAT_TIME:
          *dest_value = src_value * GST_SECOND / wavparse->rate;
          break;
        default:
          res = FALSE;
          goto done;
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
          res = FALSE;
          goto done;
      }
      break;

    default:
      res = FALSE;
      goto done;
  }

done:
  gst_object_unref (wavparse);

  return res;

  /* ERRORS */
no_bytes_per_sample:
  {
    GST_DEBUG
        ("bytes_per_sample 0, probably an mp3 - channels %d, width %d",
        wavparse->channels, wavparse->width);
    res = FALSE;
    goto done;
  }
no_bps:
  {
    g_warning ("bps is 0, internal error\n");
    res = FALSE;
    goto done;
  }
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
      gint64 curb;
      gint64 cur;
      GstFormat format;
      gboolean res = TRUE;

      curb = wav->offset - wav->datastart;
      gst_query_parse_position (query, &format, NULL);

      switch (format) {
        case GST_FORMAT_TIME:
          res &=
              gst_wavparse_pad_convert (pad, GST_FORMAT_BYTES, curb,
              &format, &cur);
          break;
        default:
          format = GST_FORMAT_BYTES;
          cur = curb;
          break;
      }
      if (res)
        gst_query_set_position (query, format, cur);
      break;
    }
    case GST_QUERY_DURATION:
    {
      gint64 endb;
      gint64 end;
      GstFormat format;
      gboolean res = TRUE;

      endb = wav->datasize;
      gst_query_parse_duration (query, &format, NULL);

      switch (format) {
        case GST_FORMAT_TIME:
          res &=
              gst_wavparse_pad_convert (pad, GST_FORMAT_BYTES, endb,
              &format, &end);
          break;
        default:
          format = GST_FORMAT_BYTES;
          end = endb;
          break;
      }
      if (res)
        gst_query_set_duration (query, format, end);
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

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
    {
      gdouble rate;
      GstFormat format;
      GstSeekFlags flags;
      GstSeekType start_type, stop_type;
      gint64 start, stop;
      GstFormat bformat = GST_FORMAT_BYTES;
      gint64 bstart, bstop;
      gboolean update_start = TRUE;
      gboolean update_stop = TRUE;

      gst_event_parse_seek (event, &rate, &format, &flags,
          &start_type, &start, &stop_type, &stop);

      GST_DEBUG ("seek format %d", format);

      /* find the corresponding byte position */
      if (format != GST_FORMAT_BYTES) {
        res &= gst_wavparse_pad_convert (pad, format, start, &bformat, &bstart);
        res &= gst_wavparse_pad_convert (pad, format, stop, &bformat, &bstop);
        if (!res)
          goto done;
      }

      switch (start_type) {
        case GST_SEEK_TYPE_CUR:
          bstart = wavparse->segment_start + bstart;
          break;
        case GST_SEEK_TYPE_END:
          bstart = wavparse->datasize + bstart;
          break;
        case GST_SEEK_TYPE_NONE:
          bstart = wavparse->segment_start;
          update_start = FALSE;
          break;
        case GST_SEEK_TYPE_SET:
          break;
      }
      bstart = CLAMP (bstart, 0, wavparse->datasize);

      switch (stop_type) {
        case GST_SEEK_TYPE_CUR:
          bstop = wavparse->segment_stop + bstop;
          break;
        case GST_SEEK_TYPE_END:
          bstop = wavparse->datasize + bstop;
          break;
        case GST_SEEK_TYPE_NONE:
          bstop = wavparse->segment_stop;
          update_stop = FALSE;
          break;
        case GST_SEEK_TYPE_SET:
          break;
      }
      bstop = CLAMP (bstop, 0, wavparse->datasize);

      /* now store the values */
      wavparse->segment_rate = rate;
      wavparse->segment_flags = flags;
      wavparse->segment_start = bstart;
      wavparse->segment_stop = bstop;

      gst_wavparse_handle_seek (wavparse, update_stop || update_start);
      break;
    }
    default:
      res = FALSE;
      break;
  }

done:
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

static GstStateChangeReturn
gst_wavparse_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret;
  GstWavParse *wav = GST_WAVPARSE (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      wav->state = GST_WAVPARSE_START;
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_wavparse_destroy_sourcepad (wav);
      gst_wavparse_reset (wav);
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    default:
      break;
  }
  return ret;
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
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
