/* -*- mOde: C; tab-width: 2; indent-tabs-mode: t; c-basic-offset: 2 -*- */
/* GStreamer .wav encoder
 * Copyright (C) <2002> Iain Holmes <iain@prettypeople.org>
 * Copyright (C) <2006> Tim-Philipp Müller <tim centricular net>
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
 * 
 */
/**
 * SECTION:element-wavenc
 *
 * Format a audio stream into the wav format.
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include "gstwavenc.h"

#include <gst/riff/riff-media.h>

GST_DEBUG_CATEGORY_STATIC (wavenc_debug);
#define GST_CAT_DEFAULT wavenc_debug

struct riff_struct
{
  guint8 id[4];                 /* RIFF */
  guint32 len;
  guint8 wav_id[4];             /* WAVE */
};

struct chunk_struct
{
  guint8 id[4];
  guint32 len;
};

struct common_struct
{
  guint16 wFormatTag;
  guint16 wChannels;
  guint32 dwSamplesPerSec;
  guint32 dwAvgBytesPerSec;
  guint16 wBlockAlign;
  guint16 wBitsPerSample;       /* Only for PCM */
};

struct wave_header
{
  struct riff_struct riff;
  struct chunk_struct format;
  struct common_struct common;
  struct chunk_struct data;
};

/* FIXME: mono doesn't produce correct files it seems, at least mplayer xruns */
/* Max. of two channels, more channels need WAVFORMATEX with
 * channel layout, which we do not support yet */
#define SINK_CAPS \
    "audio/x-raw-int, "                  \
    "rate = (int) [ 1, MAX ], "          \
    "channels = (int) [ 1, 2 ], "        \
    "endianness = (int) LITTLE_ENDIAN, " \
    "width = (int) 32, "                 \
    "depth = (int) 32, "                 \
    "signed = (boolean) true"            \
    "; "                                 \
    "audio/x-raw-int, "                  \
    "rate = (int) [ 1, MAX ], "          \
    "channels = (int) [ 1, 2 ], "        \
    "endianness = (int) LITTLE_ENDIAN, " \
    "width = (int) 24, "                 \
    "depth = (int) 24, "                 \
    "signed = (boolean) true"            \
    "; "                                 \
    "audio/x-raw-int, "                  \
    "rate = (int) [ 1, MAX ], "          \
    "channels = (int) [ 1, 2 ], "        \
    "endianness = (int) LITTLE_ENDIAN, " \
    "width = (int) 16, "                 \
    "depth = (int) 16, "                 \
    "signed = (boolean) true"            \
    "; "                                 \
    "audio/x-raw-int, "                  \
    "rate = (int) [ 1, MAX ], "          \
    "channels = (int) [ 1, 2 ], "        \
    "width = (int) 8, "                  \
    "depth = (int) 8, "                  \
    "signed = (boolean) false"           \
    "; "                                 \
    "audio/x-raw-float, "                \
    "rate = (int) [ 1, MAX ], "          \
    "channels = (int) [ 1, 2 ], "        \
    "endianness = (int) LITTLE_ENDIAN, " \
    "width = (int) { 32, 64 }; "         \
    "audio/x-alaw, "                     \
    "rate = (int) [ 8000, 192000 ], "    \
    "channels = (int) [ 1, 2 ], "        \
    "width = (int) 8, "                  \
    "depth = (int) 8, "                  \
    "signed = (boolean) false; "         \
    "audio/x-mulaw, "                    \
    "rate = (int) [ 8000, 192000 ], "    \
    "channels = (int) [ 1, 2 ], "        \
    "width = (int) 8, "                  \
    "depth = (int) 8, "                  \
    "signed = (boolean) false"


static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (SINK_CAPS)
    );

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-wav")
    );

GST_BOILERPLATE (GstWavEnc, gst_wavenc, GstElement, GST_TYPE_ELEMENT);

static GstFlowReturn gst_wavenc_chain (GstPad * pad, GstBuffer * buf);
static gboolean gst_wavenc_event (GstPad * pad, GstEvent * event);
static GstStateChangeReturn gst_wavenc_change_state (GstElement * element,
    GstStateChange transition);
static gboolean gst_wavenc_sink_setcaps (GstPad * pad, GstCaps * caps);

static void
gst_wavenc_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details_simple (element_class, "WAV audio muxer",
      "Codec/Muxer/Audio",
      "Encode raw audio into WAV", "Iain Holmes <iain@prettypeople.org>");

  gst_element_class_add_static_pad_template (element_class, &src_factory);
  gst_element_class_add_static_pad_template (element_class, &sink_factory);

  GST_DEBUG_CATEGORY_INIT (wavenc_debug, "wavenc", 0, "WAV encoder element");
}

static void
gst_wavenc_class_init (GstWavEncClass * klass)
{
  GstElementClass *element_class;

  element_class = (GstElementClass *) klass;

  element_class->change_state = GST_DEBUG_FUNCPTR (gst_wavenc_change_state);
}

static void
gst_wavenc_init (GstWavEnc * wavenc, GstWavEncClass * klass)
{
  wavenc->sinkpad = gst_pad_new_from_static_template (&sink_factory, "sink");
  gst_pad_set_chain_function (wavenc->sinkpad,
      GST_DEBUG_FUNCPTR (gst_wavenc_chain));
  gst_pad_set_event_function (wavenc->sinkpad,
      GST_DEBUG_FUNCPTR (gst_wavenc_event));
  gst_pad_set_setcaps_function (wavenc->sinkpad,
      GST_DEBUG_FUNCPTR (gst_wavenc_sink_setcaps));
  gst_pad_use_fixed_caps (wavenc->sinkpad);
  gst_element_add_pad (GST_ELEMENT (wavenc), wavenc->sinkpad);

  wavenc->srcpad = gst_pad_new_from_static_template (&src_factory, "src");
  gst_pad_use_fixed_caps (wavenc->srcpad);
  gst_pad_set_caps (wavenc->srcpad,
      gst_static_pad_template_get_caps (&src_factory));
  gst_element_add_pad (GST_ELEMENT (wavenc), wavenc->srcpad);
}

#define WAV_HEADER_LEN 44

static GstBuffer *
gst_wavenc_create_header_buf (GstWavEnc * wavenc, guint audio_data_size)
{
  struct wave_header wave;
  GstBuffer *buf;
  guint8 *header;

  buf = gst_buffer_new_and_alloc (WAV_HEADER_LEN);
  header = GST_BUFFER_DATA (buf);
  memset (header, 0, WAV_HEADER_LEN);

  wave.common.wChannels = wavenc->channels;
  wave.common.wBitsPerSample = wavenc->width;
  wave.common.dwSamplesPerSec = wavenc->rate;

  /* Fill out our wav-header with some information */
  memcpy (wave.riff.id, "RIFF", 4);
  wave.riff.len = audio_data_size + WAV_HEADER_LEN - 8;
  memcpy (wave.riff.wav_id, "WAVE", 4);

  memcpy (wave.format.id, "fmt ", 4);
  wave.format.len = 16;

  wave.common.wFormatTag = wavenc->format;
  wave.common.wBlockAlign = (wavenc->width / 8) * wave.common.wChannels;
  wave.common.dwAvgBytesPerSec =
      wave.common.wBlockAlign * wave.common.dwSamplesPerSec;

  memcpy (wave.data.id, "data", 4);
  wave.data.len = audio_data_size;

  memcpy (header, (char *) wave.riff.id, 4);
  GST_WRITE_UINT32_LE (header + 4, wave.riff.len);
  memcpy (header + 8, (char *) wave.riff.wav_id, 4);
  memcpy (header + 12, (char *) wave.format.id, 4);
  GST_WRITE_UINT32_LE (header + 16, wave.format.len);
  GST_WRITE_UINT16_LE (header + 20, wave.common.wFormatTag);
  GST_WRITE_UINT16_LE (header + 22, wave.common.wChannels);
  GST_WRITE_UINT32_LE (header + 24, wave.common.dwSamplesPerSec);
  GST_WRITE_UINT32_LE (header + 28, wave.common.dwAvgBytesPerSec);
  GST_WRITE_UINT16_LE (header + 32, wave.common.wBlockAlign);
  GST_WRITE_UINT16_LE (header + 34, wave.common.wBitsPerSample);
  memcpy (header + 36, (char *) wave.data.id, 4);
  GST_WRITE_UINT32_LE (header + 40, wave.data.len);

  gst_buffer_set_caps (buf, GST_PAD_CAPS (wavenc->srcpad));

  return buf;
}

static GstFlowReturn
gst_wavenc_push_header (GstWavEnc * wavenc, guint audio_data_size)
{
  GstFlowReturn ret;
  GstBuffer *outbuf;

  /* seek to beginning of file */
  gst_pad_push_event (wavenc->srcpad,
      gst_event_new_new_segment (FALSE, 1.0, GST_FORMAT_BYTES, 0, -1, 0));

  GST_DEBUG_OBJECT (wavenc, "writing header with datasize=%u", audio_data_size);

  outbuf = gst_wavenc_create_header_buf (wavenc, audio_data_size);
  GST_BUFFER_OFFSET (outbuf) = 0;

  ret = gst_pad_push (wavenc->srcpad, outbuf);

  if (ret != GST_FLOW_OK) {
    GST_WARNING_OBJECT (wavenc, "push header failed: flow = %s",
        gst_flow_get_name (ret));
  }

  return ret;
}

static gboolean
gst_wavenc_sink_setcaps (GstPad * pad, GstCaps * caps)
{
  GstWavEnc *wavenc;
  GstStructure *structure;
  const gchar *name;
  gint chans, rate, width;

  wavenc = GST_WAVENC (gst_pad_get_parent (pad));

  if (wavenc->sent_header && !gst_caps_can_intersect (caps, GST_PAD_CAPS (pad))) {
    GST_WARNING_OBJECT (wavenc, "cannot change format in middle of stream");
    goto fail;
  }

  GST_DEBUG_OBJECT (wavenc, "got caps: %" GST_PTR_FORMAT, caps);

  structure = gst_caps_get_structure (caps, 0);
  name = gst_structure_get_name (structure);

  if (!gst_structure_get_int (structure, "channels", &chans) ||
      !gst_structure_get_int (structure, "rate", &rate)) {
    GST_WARNING_OBJECT (wavenc, "caps incomplete");
    goto fail;
  }

  if (strcmp (name, "audio/x-raw-int") == 0) {
    if (!gst_structure_get_int (structure, "width", &width)) {
      GST_WARNING_OBJECT (wavenc, "caps incomplete");
      goto fail;
    }
    wavenc->format = GST_RIFF_WAVE_FORMAT_PCM;
    wavenc->width = width;
  } else if (strcmp (name, "audio/x-raw-float") == 0) {
    if (!gst_structure_get_int (structure, "width", &width)) {
      GST_WARNING_OBJECT (wavenc, "caps incomplete");
      goto fail;
    }
    wavenc->format = GST_RIFF_WAVE_FORMAT_IEEE_FLOAT;
    wavenc->width = width;
  } else if (strcmp (name, "audio/x-alaw") == 0) {
    wavenc->format = GST_RIFF_WAVE_FORMAT_ALAW;
    wavenc->width = 8;
  } else if (strcmp (name, "audio/x-mulaw") == 0) {
    wavenc->format = GST_RIFF_WAVE_FORMAT_MULAW;
    wavenc->width = 8;
  } else {
    GST_WARNING_OBJECT (wavenc, "Unsupported format %s", name);
    goto fail;
  }

  wavenc->channels = chans;
  wavenc->rate = rate;

  GST_LOG_OBJECT (wavenc,
      "accepted caps: format=0x%04x chans=%u width=%u rate=%u",
      wavenc->format, wavenc->channels, wavenc->width, wavenc->rate);

  gst_object_unref (wavenc);
  return TRUE;

fail:
  gst_object_unref (wavenc);
  return FALSE;
}

#if 0
static struct _maps
{
  const guint32 id;
  const gchar *name;
} maps[] = {
  {
  GST_RIFF_INFO_IARL, "Location"}, {
  GST_RIFF_INFO_IART, "Artist"}, {
  GST_RIFF_INFO_ICMS, "Commissioner"}, {
  GST_RIFF_INFO_ICMT, "Comment"}, {
  GST_RIFF_INFO_ICOP, "Copyright"}, {
  GST_RIFF_INFO_ICRD, "Creation Date"}, {
  GST_RIFF_INFO_IENG, "Engineer"}, {
  GST_RIFF_INFO_IGNR, "Genre"}, {
  GST_RIFF_INFO_IKEY, "Keywords"}, {
  GST_RIFF_INFO_INAM, "Title"}, {
  GST_RIFF_INFO_IPRD, "Product"}, {
  GST_RIFF_INFO_ISBJ, "Subject"}, {
  GST_RIFF_INFO_ISFT, "Software"}, {
  GST_RIFF_INFO_ITCH, "Technician"}
};

static guint32
get_id_from_name (const char *name)
{
  int i;

  for (i = 0; i < G_N_ELEMENTS (maps); i++) {
    if (strcasecmp (maps[i].name, name) == 0) {
      return maps[i].id;
    }
  }

  return 0;
}

static void
write_metadata (GstWavEnc * wavenc)
{
  GString *info_str;
  GList *props;
  int total = 4;
  gboolean need_to_write = FALSE;

  info_str = g_string_new ("LIST    INFO");

  for (props = wavenc->metadata->properties->properties; props;
      props = props->next) {
    GstPropsEntry *entry = props->data;
    const char *name;
    guint32 id;

    name = gst_props_entry_get_name (entry);
    id = get_id_from_name (name);
    if (id != 0) {
      const char *text;
      char *tmp;
      int len, req, i;

      need_to_write = TRUE;     /* We've got at least one entry */

      gst_props_entry_get_string (entry, &text);
      len = strlen (text) + 1;  /* The length in the file includes the \0 */

      tmp = g_strdup_printf ("%" GST_FOURCC_FORMAT "%d%s", GST_FOURCC_ARGS (id),
          GUINT32_TO_LE (len), text);
      g_string_append (info_str, tmp);
      g_free (tmp);

      /* Check that we end on an even boundary */
      req = ((len + 8) + 1) & ~1;
      for (i = 0; i < req - len; i++) {
        g_string_append_printf (info_str, "%c", 0);
      }

      total += req;
    }
  }

  if (need_to_write) {
    GstBuffer *buf;

    /* Now we've got all the strings together, we can write our length in */
    info_str->str[4] = GUINT32_TO_LE (total);

    buf = gst_buffer_new ();
    gst_buffer_set_data (buf, info_str->str, info_str->len);

    gst_pad_push (wavenc->srcpad, GST_DATA (buf));
    g_string_free (info_str, FALSE);
  }
}

static void
write_cues (GstWavEnc * wavenc)
{
  GString *cue_string, *point_string;
  GstBuffer *buf;
  GList *cue_list, *c;
  int num_cues, total = 4;

  if (gst_props_get (wavenc->metadata->properties,
          "cues", &cue_list, NULL) == FALSE) {
    /* No cues, move along please, nothing to see here */
    return;
  }

  /* Space for 'cue ', chunk size and number of cuepoints */
  cue_string = g_string_new ("cue         ");
#define CUEPOINT_SIZE 24
  point_string = g_string_sized_new (CUEPOINT_SIZE);

  for (c = cue_list, num_cues = 0; c; c = c->next, num_cues++) {
    GstCaps *cue_caps = c->data;
    guint32 pos;

    gst_props_get (cue_caps->properties, "position", &pos, NULL);

    point_string->str[0] = GUINT32_TO_LE (num_cues + 1);
    point_string->str[4] = GUINT32_TO_LE (0);
    /* Fixme: There is probably a macro for this */
    point_string->str[8] = 'd';
    point_string->str[9] = 'a';
    point_string->str[10] = 't';
    point_string->str[11] = 'a';
    point_string->str[12] = GUINT32_TO_LE (0);
    point_string->str[16] = GUINT32_TO_LE (0);
    point_string->str[20] = GUINT32_TO_LE (pos);

    total += CUEPOINT_SIZE;
  }

  /* Set the length and chunk size */
  cue_string->str[4] = GUINT32_TO_LE (total);
  cue_string->str[8] = GUINT32_TO_LE (num_cues);
  /* Stick the cue points on the end */
  g_string_append (cue_string, point_string->str);
  g_string_free (point_string, TRUE);

  buf = gst_buffer_new ();
  gst_buffer_set_data (buf, cue_string->str, cue_string->len);

  gst_pad_push (wavenc->srcpad, GST_DATA (buf));
  g_string_free (cue_string, FALSE);
}

static void
write_labels (GstWavEnc * wavenc)
{
  GstBuffer *buf;
  GString *info_str;
  int total = 4;
  GList *caps;

  info_str = g_string_new ("LIST    adtl");
  if (gst_props_get (wavenc->metadata->properties, "ltxts", &caps, NULL)) {
    GList *p;
    int i;

    for (p = caps, i = 1; p; p = p->next, i++) {
      GstCaps *ltxt_caps = p->data;
      GString *ltxt;
      char *label = NULL;
      int len, req, j;

      gst_props_get (ltxt_caps->properties, "name", &label, NULL);
      len = strlen (label);

#define LTXT_SIZE 28
      ltxt = g_string_new ("ltxt                        ");
      ltxt->str[8] = GUINT32_TO_LE (i); /* Identifier */
      ltxt->str[12] = GUINT32_TO_LE (0);        /* Sample Length */
      ltxt->str[16] = GUINT32_TO_LE (0);        /* FIXME: Don't save the purpose yet */
      ltxt->str[20] = GUINT16_TO_LE (0);        /* Country */
      ltxt->str[22] = GUINT16_TO_LE (0);        /* Language */
      ltxt->str[24] = GUINT16_TO_LE (0);        /* Dialect */
      ltxt->str[26] = GUINT16_TO_LE (0);        /* Code Page */
      g_string_append (ltxt, label);
      g_free (label);

      len += LTXT_SIZE;

      ltxt->str[4] = GUINT32_TO_LE (len);

      /* Check that we end on an even boundary */
      req = ((len + 8) + 1) & ~1;
      for (j = 0; j < req - len; j++) {
        g_string_append_printf (ltxt, "%c", 0);
      }

      total += req;

      g_string_append (info_str, ltxt->str);
      g_string_free (ltxt, TRUE);
    }
  }

  if (gst_props_get (wavenc->metadata->properties, "labels", &caps, NULL)) {
    GList *p;
    int i;

    for (p = caps, i = 1; p; p = p->next, i++) {
      GstCaps *labl_caps = p->data;
      GString *labl;
      char *label = NULL;
      int len, req, j;

      gst_props_get (labl_caps->properties, "name", &label, NULL);
      len = strlen (label);

#define LABL_SIZE 4
      labl = g_string_new ("labl        ");
      labl->str[8] = GUINT32_TO_LE (i);
      g_string_append (labl, label);
      g_free (label);

      len += LABL_SIZE;

      labl->str[4] = GUINT32_TO_LE (len);

      /* Check our size */
      req = ((len + 8) + 1) & ~1;
      for (j = 0; j < req - len; j++) {
        g_string_append_printf (labl, "%c", 0);
      }

      total += req;

      g_string_append (info_str, labl->str);
      g_string_free (labl, TRUE);
    }
  }

  if (gst_props_get (wavenc->metadata->properties, "notes", &caps, NULL)) {
    GList *p;
    int i;

    for (p = caps, i = 1; p; p = p->next, i++) {
      GstCaps *note_caps = p->data;
      GString *note;
      char *label = NULL;
      int len, req, j;

      gst_props_get (note_caps->properties, "name", &label, NULL);
      len = strlen (label);

#define NOTE_SIZE 4
      note = g_string_new ("note        ");
      note->str[8] = GUINT32_TO_LE (i);
      g_string_append (note, label);
      g_free (label);

      len += NOTE_SIZE;

      note->str[4] = GUINT32_TO_LE (len);

      /* Size check */
      req = ((len + 8) + 1) & ~1;
      for (j = 0; j < req - len; j++) {
        g_string_append_printf (note, "%c", 0);
      }

      total += req;

      g_string_append (info_str, note->str);
      g_string_free (note, TRUE);
    }
  }

  info_str->str[4] = GUINT32_TO_LE (total);

  buf = gst_buffer_new ();
  gst_buffer_set_data (buf, info_str->str, info_str->len);

  gst_pad_push (wavenc->srcpad, GST_DATA (buf));
  g_string_free (info_str, FALSE);
}
#endif

static gboolean
gst_wavenc_event (GstPad * pad, GstEvent * event)
{
  gboolean res = TRUE;
  GstWavEnc *wavenc;

  wavenc = GST_WAVENC (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:{
      GST_DEBUG_OBJECT (wavenc, "got EOS");
#if 0
      /* Write our metadata if we have any */
      if (wavenc->metadata) {
        write_metadata (wavenc);
        write_cues (wavenc);
        write_labels (wavenc);
      }
#endif
      /* write header with correct length values */
      gst_wavenc_push_header (wavenc, wavenc->length);

      /* we're done with this file */
      wavenc->finished_properly = TRUE;

      /* and forward the EOS event */
      res = gst_pad_event_default (pad, event);
      break;
    }
    case GST_EVENT_NEWSEGMENT:
      /* Just drop it, it's probably in TIME format
       * anyway. We'll send our own newsegment event */
      gst_event_unref (event);
      break;
    default:
      res = gst_pad_event_default (pad, event);
      break;
  }

  gst_object_unref (wavenc);
  return res;
}

static GstFlowReturn
gst_wavenc_chain (GstPad * pad, GstBuffer * buf)
{
  GstWavEnc *wavenc = GST_WAVENC (GST_PAD_PARENT (pad));
  GstFlowReturn flow = GST_FLOW_OK;

  g_return_val_if_fail (wavenc->channels > 0, GST_FLOW_WRONG_STATE);

  if (!wavenc->sent_header) {
    /* use bogus size initially, we'll write the real
     * header when we get EOS and know the exact length */
    flow = gst_wavenc_push_header (wavenc, 0x7FFF0000);

    /* starting a file, means we have to finish it properly */
    wavenc->finished_properly = FALSE;

    if (flow != GST_FLOW_OK)
      return flow;

    GST_DEBUG_OBJECT (wavenc, "wrote dummy header");
    wavenc->sent_header = TRUE;
  }

  GST_LOG_OBJECT (wavenc, "pushing %u bytes raw audio, ts=%" GST_TIME_FORMAT,
      GST_BUFFER_SIZE (buf), GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)));

  buf = gst_buffer_make_metadata_writable (buf);

  gst_buffer_set_caps (buf, GST_PAD_CAPS (wavenc->srcpad));
  GST_BUFFER_OFFSET (buf) = WAV_HEADER_LEN + wavenc->length;
  GST_BUFFER_OFFSET_END (buf) = GST_BUFFER_OFFSET_NONE;

  wavenc->length += GST_BUFFER_SIZE (buf);

  flow = gst_pad_push (wavenc->srcpad, buf);

  return flow;
}

static GstStateChangeReturn
gst_wavenc_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
  GstWavEnc *wavenc = GST_WAVENC (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      wavenc->format = 0;
      wavenc->channels = 0;
      wavenc->width = 0;
      wavenc->rate = 0;
      wavenc->length = 0;
      wavenc->sent_header = FALSE;
      /* its true because we haven't writen anything */
      wavenc->finished_properly = TRUE;
      break;
    default:
      break;
  }

  ret = parent_class->change_state (element, transition);
  if (ret != GST_STATE_CHANGE_SUCCESS)
    return ret;

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      if (!wavenc->finished_properly) {
        GST_ELEMENT_WARNING (wavenc, STREAM, MUX,
            ("Wav stream not finished properly"),
            ("Wav stream not finished properly, no EOS received "
                "before shutdown"));
      }
      break;
    default:
      break;
  }

  return ret;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "wavenc", GST_RANK_PRIMARY,
      GST_TYPE_WAVENC);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "wavenc",
    "Encode raw audio into WAV",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
