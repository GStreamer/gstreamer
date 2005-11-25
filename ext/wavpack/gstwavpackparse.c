/* GStreamer wavpack plugin
 * (c) 2005 Arwed v. Merkatz <v.merkatz@gmx.net>
 *
 * gstwavpackparse.c: wavpack file parser
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

#include <gst/gst.h>

#include <math.h>
#include <string.h>

#include <wavpack/wavpack.h>
#include "gstwavpackparse.h"
#include "gstwavpackcommon.h"

GST_DEBUG_CATEGORY_STATIC (gst_wavpack_parse_debug);
#define GST_CAT_DEFAULT gst_wavpack_parse_debug

/* Filter signals and args */
enum
{
  LAST_SIGNAL
};

enum
{
  ARG_0
};

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-wavpack, "
        "framed = (boolean) false; "
        "audio/x-wavpack-correction, " "framed = (boolean) false")
    );

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS ("audio/x-wavpack, "
        "width = (int) { 8, 16, 24 }, "
        "channels = (int) { 1, 2 }, "
        "rate = (int) [ 8000, 96000 ], " "framed = (boolean) true")
    );

static GstStaticPadTemplate wvc_src_factory = GST_STATIC_PAD_TEMPLATE ("wvcsrc",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS ("audio/x-wavpack-correction, " "framed = (boolean) true")
    );

static void gst_wavpack_parse_class_init (GstWavpackParseClass * klass);
static void gst_wavpack_parse_base_init (GstWavpackParseClass * klass);
static void gst_wavpack_parse_init (GstWavpackParse * wavpackparse);

static void gst_wavpack_parse_handle_event (GstElement * element);
static void gst_wavpack_parse_loop (GstElement * element);
static GstElementStateReturn gst_wavpack_parse_change_state (GstElement *
    element);

static GstElementClass *parent = NULL;

GType
gst_wavpack_parse_get_type (void)
{
  static GType plugin_type = 0;

  if (!plugin_type) {
    static const GTypeInfo plugin_info = {
      sizeof (GstWavpackParseClass),
      (GBaseInitFunc) gst_wavpack_parse_base_init,
      NULL,
      (GClassInitFunc) gst_wavpack_parse_class_init,
      NULL,
      NULL,
      sizeof (GstWavpackParse),
      0,
      (GInstanceInitFunc) gst_wavpack_parse_init,
    };
    plugin_type = g_type_register_static (GST_TYPE_ELEMENT,
        "GstWavpackParse", &plugin_info, 0);
  }
  return plugin_type;
}

static void
gst_wavpack_parse_base_init (GstWavpackParseClass * klass)
{
  static GstElementDetails plugin_details = {
    "Wavpack file parser",
    "Codec/Demuxer/Audio",
    "Parses Wavpack files",
    "Arwed v. Merkatz <v.merkatz@gmx.net>"
  };
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&wvc_src_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_factory));
  gst_element_class_set_details (element_class, &plugin_details);
}

static void
gst_wavpack_parse_dispose (GObject * object)
{
  G_OBJECT_CLASS (parent)->dispose (object);
}

static void
gst_wavpack_parse_class_init (GstWavpackParseClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent = g_type_class_ref (GST_TYPE_ELEMENT);

  gobject_class->dispose = gst_wavpack_parse_dispose;
  gstelement_class->change_state = gst_wavpack_parse_change_state;
}

static gboolean
gst_wavpack_parse_src_query (GstPad * pad, GstQueryType type,
    GstFormat * format, gint64 * value)
{
  GstWavpackParse *wavpackparse = GST_WAVPACK_PARSE (gst_pad_get_parent (pad));

  if ((type == GST_QUERY_TOTAL) && (*format == GST_FORMAT_TIME)) {
    if (wavpackparse->total_samples == 0) {
      *value = 0;
      return FALSE;
    }
    *value =
        ((gdouble) wavpackparse->total_samples /
        (gdouble) wavpackparse->samplerate) * GST_SECOND;
    return TRUE;
  } else if ((type == GST_QUERY_POSITION) && (*format == GST_FORMAT_TIME)) {
    *value = wavpackparse->timestamp;
    return TRUE;
  } else {
    return gst_pad_query_default (pad, type, format, value);
  }
  return FALSE;
}

static gboolean
gst_wavpack_parse_src_event (GstPad * pad, GstEvent * event)
{
  GstWavpackParse *wavpackparse;
  GstSeekType method;
  GstFormat format;
  gboolean need_flush;
  gint64 offset, dest;

  wavpackparse = GST_WAVPACK_PARSE (gst_pad_get_parent (pad));

  if (GST_EVENT_TYPE (event) != GST_EVENT_SEEK) {
    return gst_pad_send_event (GST_PAD_PEER (wavpackparse->sinkpad), event);
  }

  format = GST_EVENT_SEEK_FORMAT (event);
  offset = GST_EVENT_SEEK_OFFSET (event);
  method = GST_EVENT_SEEK_METHOD (event);
  need_flush = GST_EVENT_SEEK_FLAGS (event) & GST_SEEK_FLAG_FLUSH;

  gst_data_unref (GST_DATA (event));
  event = NULL;

  if (offset < 0 || method != GST_SEEK_METHOD_SET)
    return FALSE;

  if (format == GST_FORMAT_TIME) {
    dest = offset * wavpackparse->samplerate / GST_SECOND;
  } else if (format == GST_FORMAT_DEFAULT) {
    dest = offset;
  } else {
    return FALSE;
  }

  wavpackparse->need_discont = TRUE;
  wavpackparse->need_flush = need_flush;

  wavpackparse->seek_pending = TRUE;
  wavpackparse->seek_offset = dest;

  return TRUE;
}

#define BUFSIZE 4096

static guint64
find_header (GstByteStream * bs, guint64 filepos, WavpackHeader * wphdr)
{
  guint64 pos = filepos;

  gst_bytestream_seek (bs, filepos, GST_SEEK_METHOD_SET);

  while (1) {
    guint8 *data, *cur;
    gint read;

    read = gst_bytestream_peek_bytes (bs, &data, BUFSIZE);
    while (read != BUFSIZE) {
      guint remaining;
      GstEvent *event = NULL;

      gst_bytestream_get_status (bs, &remaining, &event);
      if (GST_EVENT_TYPE (event) == GST_EVENT_EOS) {
        gst_event_unref (event);
        return gst_bytestream_length (bs);
      }
      gst_event_unref (event);
      read = gst_bytestream_peek_bytes (bs, &data, BUFSIZE);
      continue;
    }
    cur = data;
    do {
      if (cur[0] == 'w' && cur[1] == 'v' && cur[2] == 'p' && cur[3] == 'k') {
        gst_wavpack_read_header (wphdr, cur);
        return pos + (cur - data);
      }
      cur++;
    } while ((cur - data) < (BUFSIZE - sizeof (WavpackHeader)));
    gst_bytestream_flush_fast (bs, BUFSIZE - sizeof (WavpackHeader));
    pos += BUFSIZE - sizeof (WavpackHeader);
  }

  /* never reached */
  return gst_bytestream_length (bs);
}

/* find the position of sample in the input bytestream, adapted from the
 * original wavpack find_sample function */
static guint64
find_sample (GstWavpackParse * wavpackparse, guint32 sample)
{
  WavpackHeader wphdr;
  guint64 file_pos1 = 0, file_pos2 = gst_bytestream_length (wavpackparse->bs);
  guint64 sample_pos1 = 0, sample_pos2 = wavpackparse->total_samples;
  double ratio = 0.96;
  int file_skip = 0;

  if (sample >= wavpackparse->total_samples) {
    return gst_bytestream_length (wavpackparse->bs);
  }

  while (1) {
    double bytes_per_sample;
    guint64 seek_pos;

    bytes_per_sample = file_pos2 - file_pos1;
    bytes_per_sample /= sample_pos2 - sample_pos1;
    seek_pos = file_pos1 + (file_skip ? 32 : 0);
    seek_pos += (guint64) (bytes_per_sample * (sample - sample_pos1) * ratio);
    seek_pos = find_header (wavpackparse->bs, seek_pos, &wphdr);

    if (seek_pos == gst_bytestream_length (wavpackparse->bs)
        || seek_pos >= file_pos2) {
      if (ratio > 0.0) {
        if ((ratio -= 0.24) < 0.0)
          ratio = 0.0;
      } else {
        return gst_bytestream_length (wavpackparse->bs);
      }
    } else if (wphdr.block_index > sample) {
      sample_pos2 = wphdr.block_index;
      file_pos2 = seek_pos;
    } else if (wphdr.block_index + wphdr.block_samples <= sample) {
      if (seek_pos == file_pos1)
        file_skip = 1;
      else {
        sample_pos1 = wphdr.block_index;
        file_pos1 = seek_pos;
      }
    } else {
      return seek_pos;
    }
  }
}


static void
gst_wavpack_parse_seek (GstWavpackParse * wavpackparse)
{
  guint8 *data;
  gint num;
  guint remaining;
  WavpackHeader *header = g_malloc (sizeof (WavpackHeader));

  guint64 offset = find_sample (wavpackparse, wavpackparse->seek_offset);

  gst_bytestream_seek (wavpackparse->bs, offset, GST_SEEK_METHOD_SET);

  if (offset == gst_bytestream_length (wavpackparse->bs)) {
    /* seek failed or went beyond the end, go EOS */
    wavpackparse->timestamp =
        ((gdouble) wavpackparse->total_samples /
        (gdouble) wavpackparse->samplerate) * GST_SECOND;
    return;
  }

  num =
      gst_bytestream_peek_bytes (wavpackparse->bs, &data,
      sizeof (WavpackHeader));
  while (num != sizeof (WavpackHeader)) {
    GstEvent *event = NULL;

    gst_bytestream_get_status (wavpackparse->bs, &remaining, &event);
    if (!event) {
      return;
    }
    gst_event_unref (event);
    num =
        gst_bytestream_peek_bytes (wavpackparse->bs, &data,
        sizeof (WavpackHeader));
    continue;
  }
  gst_wavpack_read_header (header, data);

  if (wavpackparse->need_flush) {
    GstEvent *flush = gst_event_new (GST_EVENT_FLUSH);

    gst_pad_push (wavpackparse->srcpad, GST_DATA (flush));
    wavpackparse->need_flush = FALSE;
  }

  wavpackparse->need_discont = TRUE;
  wavpackparse->timestamp =
      ((gdouble) header->block_index / (gdouble) wavpackparse->samplerate) *
      GST_SECOND;
}

static void
gst_wavpack_parse_init (GstWavpackParse * wavpackparse)
{
  GstElementClass *klass = GST_ELEMENT_GET_CLASS (wavpackparse);

  wavpackparse->sinkpad =
      gst_pad_new_from_template (gst_element_class_get_pad_template (klass,
          "sink"), "sink");

  gst_element_add_pad (GST_ELEMENT (wavpackparse), wavpackparse->sinkpad);
  gst_element_set_loop_function (GST_ELEMENT (wavpackparse),
      gst_wavpack_parse_loop);

  GST_FLAG_SET (wavpackparse, GST_ELEMENT_EVENT_AWARE);
}

static void
gst_wavpack_parse_handle_event (GstElement * element)
{
  GstWavpackParse *wavpackparse = GST_WAVPACK_PARSE (element);
  GstEvent *event = NULL;
  guint remaining;

  gst_bytestream_get_status (wavpackparse->bs, &remaining, &event);
  if (event) {
    if (GST_EVENT_TYPE (event) == GST_EVENT_EOS) {
      if (GST_IS_PAD (wavpackparse->srcpad)
          && gst_pad_is_active (wavpackparse->srcpad))
        gst_pad_push (wavpackparse->srcpad, GST_DATA (event));
      gst_element_set_eos (element);
    } else if (GST_EVENT_TYPE (event) == GST_EVENT_DISCONTINUOUS) {
      if (GST_IS_PAD (wavpackparse->srcpad)
          && gst_pad_is_active (wavpackparse->srcpad))
        gst_pad_event_default (wavpackparse->srcpad, event);
    } else {
      gst_event_unref (event);
    }
  } else {
    GST_ELEMENT_ERROR (element, STREAM, DEMUX, ("couldn't read wavpack header"),
        (NULL));
  }
}

static void
gst_wavpack_parse_loop (GstElement * element)
{
  GstWavpackParse *wavpackparse = GST_WAVPACK_PARSE (element);
  guint8 *data;
  gint num;
  GstBuffer *buf;
  WavpackHeader *header = g_malloc (sizeof (WavpackHeader));

  if (wavpackparse->seek_pending) {
    gst_wavpack_parse_seek (wavpackparse);
    wavpackparse->need_discont = TRUE;
    wavpackparse->seek_pending = FALSE;
  }

  if (wavpackparse->need_discont) {
    if (GST_IS_PAD (wavpackparse->srcpad)
        && gst_pad_is_active (wavpackparse->srcpad))
      gst_pad_push (wavpackparse->srcpad,
          GST_DATA (gst_event_new_discontinuous (FALSE, GST_FORMAT_TIME,
                  wavpackparse->timestamp, GST_FORMAT_UNDEFINED)));
    wavpackparse->need_discont = FALSE;
  }

  num =
      gst_bytestream_peek_bytes (wavpackparse->bs, &data,
      sizeof (WavpackHeader));
  if (num != sizeof (WavpackHeader)) {
    gst_wavpack_parse_handle_event (element);
    return;
  }
  gst_wavpack_read_header (header, data);

  num = gst_bytestream_peek_bytes (wavpackparse->bs, &data, header->ckSize + 8);
  if (num != header->ckSize + 8) {
    gst_wavpack_parse_handle_event (element);
    return;
  }

  if (!GST_IS_PAD (wavpackparse->srcpad)) {
    guchar *bufptr = data + sizeof (WavpackHeader);
    GstCaps *caps = NULL;
    WavpackMetadata meta;

    while (read_metadata_buff (&meta, data, &bufptr)) {
      if (meta.id == ID_WVC_BITSTREAM) {
        caps = gst_caps_new_simple ("audio/x-wavpack-correction",
            "framed", G_TYPE_BOOLEAN, TRUE, NULL);
        wavpackparse->srcpad =
            gst_pad_new_from_template (gst_element_class_get_pad_template
            (GST_ELEMENT_GET_CLASS (wavpackparse), "wvcsrc"), "wvcsrc");
      } else if (meta.id == ID_RIFF_HEADER) {
        WaveHeader *wheader = g_malloc (sizeof (WaveHeader));

        // skip RiffChunkHeader and ChunkHeader
        g_memmove (wheader, meta.data + 20, sizeof (WaveHeader));
        little_endian_to_native (wheader, WaveHeaderFormat);
        wavpackparse->samplerate = wheader->SampleRate;
        wavpackparse->channels = wheader->NumChannels;
        wavpackparse->total_samples = header->total_samples;
        caps = gst_caps_new_simple ("audio/x-wavpack",
            "width", G_TYPE_INT, wheader->BitsPerSample,
            "channels", G_TYPE_INT, wavpackparse->channels,
            "rate", G_TYPE_INT, wavpackparse->samplerate,
            "framed", G_TYPE_BOOLEAN, TRUE, NULL);
        wavpackparse->srcpad =
            gst_pad_new_from_template (gst_element_class_get_pad_template
            (GST_ELEMENT_GET_CLASS (wavpackparse), "src"), "src");
      }
    }
    gst_pad_use_explicit_caps (wavpackparse->srcpad);
    gst_pad_set_query_function (wavpackparse->srcpad,
        gst_wavpack_parse_src_query);
    gst_pad_set_event_function (wavpackparse->srcpad,
        gst_wavpack_parse_src_event);
    gst_element_add_pad (GST_ELEMENT (wavpackparse), wavpackparse->srcpad);

    gst_pad_set_explicit_caps (wavpackparse->srcpad, caps);
  }

  buf = gst_buffer_new_and_alloc (header->ckSize + 8);
  memcpy (GST_BUFFER_DATA (buf), data, header->ckSize + 8);
  gst_bytestream_flush_fast (wavpackparse->bs, header->ckSize + 8);
  wavpackparse->timestamp =
      ((gdouble) header->block_index / (gdouble) wavpackparse->samplerate) *
      GST_SECOND;
  GST_BUFFER_TIMESTAMP (buf) = wavpackparse->timestamp;
  GST_BUFFER_DURATION (buf) =
      ((gdouble) header->block_samples / (gdouble) wavpackparse->samplerate) *
      GST_SECOND;
  gst_pad_push (wavpackparse->srcpad, GST_DATA (buf));
}

static GstElementStateReturn
gst_wavpack_parse_change_state (GstElement * element)
{
  GstWavpackParse *wavpackparse = GST_WAVPACK_PARSE (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_READY_TO_PAUSED:
      wavpackparse->need_discont = TRUE;
      wavpackparse->bs = gst_bytestream_new (wavpackparse->sinkpad);
      break;
    case GST_STATE_PAUSED_TO_READY:
      gst_bytestream_destroy (wavpackparse->bs);
      wavpackparse->seek_pending = FALSE;
      break;
    default:
      break;
  }

  if (GST_ELEMENT_CLASS (parent)->change_state)
    return GST_ELEMENT_CLASS (parent)->change_state (element);

  return GST_STATE_SUCCESS;
}

gboolean
gst_wavpack_parse_plugin_init (GstPlugin * plugin)
{
  if (!gst_library_load ("gstbytestream"))
    return FALSE;

  if (!gst_element_register (plugin, "wavpackparse",
          GST_RANK_PRIMARY, GST_TYPE_WAVPACK_PARSE)) {
    return FALSE;
  }

  GST_DEBUG_CATEGORY_INIT (gst_wavpack_parse_debug, "wavpackparse", 0,
      "wavpack file parser");

  return TRUE;
}
