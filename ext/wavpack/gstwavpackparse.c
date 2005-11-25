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

static gboolean gst_wavepack_parse_sink_activate (GstPad * sinkpad);
static gboolean
gst_wavepack_parse_sink_activate_pull (GstPad * sinkpad, gboolean active);

static gboolean gst_wavpack_parse_sink_event (GstPad * pad, GstEvent * event);

static void gst_wavpack_parse_loop (GstElement * element);
static GstStateChangeReturn gst_wavpack_parse_change_state (GstElement *
    element, GstStateChange transition);

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
gst_wavpack_parse_src_query (GstPad * pad, GstQuery * query)
{
  GstWavpackParse *wavpackparse = GST_WAVPACK_PARSE (gst_pad_get_parent (pad));
  GstFormat format = GST_FORMAT_DEFAULT;
  gint64 value;
  gboolean ret = FALSE;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_POSITION:
      gst_query_parse_position (query, &format, &value);
      if (format == GST_FORMAT_TIME) {
        value = wavpackparse->timestamp;
        gst_query_set_duration (query, format, value);
        g_object_unref (wavpackparse);
        ret = TRUE;
        break;
      }
      break;
    case GST_QUERY_DURATION:
      gst_query_parse_duration (query, &format, &value);

      if (format == GST_FORMAT_TIME) {
        if (wavpackparse->total_samples == 0) {
          value = 0;
          gst_query_set_duration (query, format, value);
          g_object_unref (wavpackparse);
          ret = FALSE;
          break;
        }
        value = ((gdouble) wavpackparse->total_samples /
            (gdouble) wavpackparse->samplerate) * GST_SECOND;
        gst_query_set_duration (query, format, value);
        g_object_unref (wavpackparse);
        ret = TRUE;
        break;
      }
      break;
    default:
      g_object_unref (wavpackparse);
      ret = gst_pad_query_default (pad, query);
      break;
  }

  return ret;

}

static gboolean
gst_wavpack_parse_src_event (GstPad * pad, GstEvent * event)
{
  GstWavpackParse *wavpackparse;
  GstSeekType type;
  GstFormat format;
  gboolean need_flush;
  gint64 offset, dest;
  GstSeekFlags flags;
  gboolean ret = TRUE;

  wavpackparse = GST_WAVPACK_PARSE (gst_pad_get_parent (pad));

  if (GST_EVENT_TYPE (event) != GST_EVENT_SEEK) {
    GstPad *peer;

    if (!(peer = gst_pad_get_peer (wavpackparse->sinkpad))) {
      ret = FALSE;
      goto done;
    }
    ret = gst_pad_send_event (peer, event);
    gst_object_unref (peer);
    goto done;
  }

  gst_event_parse_seek (event, NULL, &format, &flags, &type, &offset, NULL,
      NULL);

  need_flush = flags & GST_SEEK_FLAG_FLUSH;



  if (offset < 0 || type != GST_SEEK_TYPE_SET) {
    ret = FALSE;
    goto done;
  }

  if (format == GST_FORMAT_TIME) {
    dest = offset * wavpackparse->samplerate / GST_SECOND;
  } else if (format == GST_FORMAT_DEFAULT) {
    dest = offset;
  } else {
    ret = FALSE;
    goto done;
  }

  wavpackparse->need_discont = TRUE;
  wavpackparse->need_flush = need_flush;

  wavpackparse->seek_pending = TRUE;
  wavpackparse->seek_offset = dest;

done:
  gst_event_unref (event);
  gst_object_unref (wavpackparse);
  return ret;
}

#define BUFSIZE 4096

static guint64
find_header (GstWavpackParse * wavpackparse, guint64 filepos,
    WavpackHeader * wphdr)
{
  guint64 pos = filepos;
  gint read = 0;
  GstBuffer *buf = NULL;

  while (TRUE) {
    guint8 *cur;

    if (GST_FLOW_OK != gst_pad_pull_range (wavpackparse->sinkpad,
            wavpackparse->flushed_bytes + filepos, BUFSIZE, &buf)) {
      wavpackparse->eos = TRUE;
      gst_pad_push_event (wavpackparse->srcpad, gst_event_new_eos ());
      return 0;
    }
    read = GST_BUFFER_SIZE (buf);

    if (read == 0) {
      wavpackparse->eos = TRUE;
      gst_pad_push_event (wavpackparse->srcpad, gst_event_new_eos ());
      gst_buffer_unref (buf);
      return 0;
    }

    cur = GST_BUFFER_DATA (buf);
    do {
      if (cur[0] == 'w' && cur[1] == 'v' && cur[2] == 'p' && cur[3] == 'k') {
        gst_wavpack_read_header (wphdr, cur);
        pos += (cur - GST_BUFFER_DATA (buf));
        gst_buffer_unref (buf);
        buf = NULL;
        return pos;
      }
      cur++;
    } while ((cur - GST_BUFFER_DATA (buf)) <
        (BUFSIZE - sizeof (WavpackHeader)));

    wavpackparse->flushed_bytes += BUFSIZE - sizeof (WavpackHeader);

    pos += BUFSIZE - sizeof (WavpackHeader);
  }

  /* never reached */

  if (buf) {
    gst_buffer_unref (buf);
    buf = NULL;
  }
  return wavpackparse->duration - wavpackparse->flushed_bytes;
}

/* find the position of sample in the input bytestream, adapted from the
 * original wavpack find_sample function */
static guint64
find_sample (GstWavpackParse * wavpackparse, guint32 sample)
{
  WavpackHeader wphdr;
  guint64 file_pos1 = 0;
  guint64 file_pos2 = wavpackparse->duration - wavpackparse->flushed_bytes;
  guint64 sample_pos1 = 0, sample_pos2 = wavpackparse->total_samples;
  double ratio = 0.96;
  int file_skip = 0;

  if (sample >= wavpackparse->total_samples) {
    return wavpackparse->duration - wavpackparse->flushed_bytes;
  }

  while (1) {
    double bytes_per_sample;
    guint64 seek_pos;

    bytes_per_sample = file_pos2 - file_pos1;
    bytes_per_sample /= sample_pos2 - sample_pos1;
    seek_pos = file_pos1 + (file_skip ? 32 : 0);
    seek_pos += (guint64) (bytes_per_sample * (sample - sample_pos1) * ratio);
    seek_pos = find_header (wavpackparse, seek_pos, &wphdr);

    if (seek_pos == wavpackparse->duration - wavpackparse->flushed_bytes
        || seek_pos >= file_pos2) {
      if (ratio > 0.0) {
        if ((ratio -= 0.24) < 0.0)
          ratio = 0.0;
      } else {
        return wavpackparse->duration - wavpackparse->flushed_bytes;
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
  GstBuffer *buf;
  gint num;
  WavpackHeader *header = g_malloc (sizeof (WavpackHeader));

  guint64 offset = find_sample (wavpackparse, wavpackparse->seek_offset);

  if (offset >= wavpackparse->duration - wavpackparse->flushed_bytes) {
    /* seek failed or went beyond the end, go EOS */
    wavpackparse->timestamp =
        ((gdouble) wavpackparse->total_samples /
        (gdouble) wavpackparse->samplerate) * GST_SECOND;
    return;
  }

  if (GST_FLOW_OK != gst_pad_pull_range (wavpackparse->sinkpad,
          wavpackparse->flushed_bytes + offset, sizeof (WavpackHeader), &buf)) {
    wavpackparse->eos = TRUE;
    gst_pad_push_event (wavpackparse->srcpad, gst_event_new_eos ());
    return;
  }
  num = GST_BUFFER_SIZE (buf);

  if (num != sizeof (WavpackHeader)) {
    wavpackparse->eos = TRUE;
    gst_pad_push_event (wavpackparse->srcpad, gst_event_new_eos ());
    return;
  }

  gst_wavpack_read_header (header, GST_BUFFER_DATA (buf));
  gst_buffer_unref (buf);

  if (wavpackparse->need_flush) {
    wavpackparse->need_flush = FALSE;
    gst_pad_push_event (wavpackparse->srcpad, gst_event_new_flush_start ());
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

  wavpackparse->duration = -1;
  wavpackparse->flushed_bytes = -1;
  wavpackparse->eos = FALSE;

  wavpackparse->sinkpad =
      gst_pad_new_from_template (gst_element_class_get_pad_template (klass,
          "sink"), "sink");

  gst_pad_set_event_function (wavpackparse->sinkpad,
      gst_wavpack_parse_sink_event);

  gst_element_add_pad (GST_ELEMENT (wavpackparse), wavpackparse->sinkpad);

  gst_pad_set_activate_function (wavpackparse->sinkpad,
      gst_wavepack_parse_sink_activate);

  gst_pad_set_activatepull_function (wavpackparse->sinkpad,
      gst_wavepack_parse_sink_activate_pull);

  wavpackparse->srcpad = NULL;

}

static gboolean
gst_wavpack_parse_sink_event (GstPad * pad, GstEvent * event)
{
  GstWavpackParse *wavpackparse = GST_WAVPACK_PARSE (gst_pad_get_parent (pad));
  gboolean res = TRUE;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
      wavpackparse->eos = TRUE;
      /* fall through */
    default:
      res = gst_pad_event_default (pad, event);
      gst_object_unref (wavpackparse);
      return res;
      break;
  }

}

static void
gst_wavpack_parse_loop (GstElement * element)
{
  GstWavpackParse *wavpackparse = GST_WAVPACK_PARSE (element);
  gint num;
  GstBuffer *buf;
  WavpackHeader *header = g_malloc (sizeof (WavpackHeader));

  GST_PAD_STREAM_LOCK (wavpackparse->sinkpad);

  if (wavpackparse->eos) {
    goto done;
  }

  if (wavpackparse->seek_pending) {
    gst_wavpack_parse_seek (wavpackparse);
    wavpackparse->need_discont = TRUE;
    wavpackparse->seek_pending = FALSE;
  }

  if (wavpackparse->need_discont) {
    if (GST_IS_PAD (wavpackparse->srcpad)) {
      gst_pad_push_event (wavpackparse->srcpad,
          gst_event_new_new_segment (FALSE, 1.0, GST_FORMAT_TIME,
              wavpackparse->timestamp, GST_CLOCK_TIME_NONE, 0));

      wavpackparse->need_discont = FALSE;
    }
  }

  if (GST_FLOW_OK != gst_pad_pull_range (wavpackparse->sinkpad,
          wavpackparse->flushed_bytes, sizeof (WavpackHeader), &buf)) {
    wavpackparse->eos = TRUE;
    gst_pad_push_event (wavpackparse->srcpad, gst_event_new_eos ());
    goto done;
  }
  num = GST_BUFFER_SIZE (buf);

  if (num != sizeof (WavpackHeader)) {
    gst_buffer_unref (buf);
    wavpackparse->eos = TRUE;
    gst_pad_push_event (wavpackparse->srcpad, gst_event_new_eos ());
    goto done;
  }

  gst_wavpack_read_header (header, GST_BUFFER_DATA (buf));
  gst_buffer_unref (buf);

  if (GST_FLOW_OK != gst_pad_pull_range (wavpackparse->sinkpad,
          wavpackparse->flushed_bytes, header->ckSize + 8, &buf)) {
    wavpackparse->eos = TRUE;
    gst_pad_push_event (wavpackparse->srcpad, gst_event_new_eos ());
    goto done;
  }
  num = GST_BUFFER_SIZE (buf);

  if (num != header->ckSize + 8) {
    gst_buffer_unref (buf);
    wavpackparse->eos = TRUE;
    gst_pad_push_event (wavpackparse->srcpad, gst_event_new_eos ());
    goto done;
  }

  if (!GST_IS_PAD (wavpackparse->srcpad)) {

    guchar *bufptr = GST_BUFFER_DATA (buf) + sizeof (WavpackHeader);
    GstCaps *caps = NULL;
    WavpackMetadata meta;

    while (read_metadata_buff (&meta, GST_BUFFER_DATA (buf), &bufptr)) {
      if (meta.id == ID_WVC_BITSTREAM) {
        caps = gst_caps_new_simple ("audio/x-wavpack-correction",
            "framed", G_TYPE_BOOLEAN, TRUE, NULL);
        if (GST_IS_PAD (wavpackparse->srcpad)) {
          gst_object_unref (wavpackparse->srcpad);
        }
        wavpackparse->srcpad =
            gst_pad_new_from_template (gst_element_class_get_pad_template
            (GST_ELEMENT_GET_CLASS (wavpackparse), "wvcsrc"), "wvcsrc");
      } else if (meta.id == ID_RIFF_HEADER) {
        WaveHeader *wheader = g_malloc (sizeof (WaveHeader));

        /* skip RiffChunkHeader and ChunkHeader */
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
        if (GST_IS_PAD (wavpackparse->srcpad)) {
          gst_object_unref (wavpackparse->srcpad);
        }
        wavpackparse->srcpad =
            gst_pad_new_from_template (gst_element_class_get_pad_template
            (GST_ELEMENT_GET_CLASS (wavpackparse), "src"), "src");
      }
    }

    if (!(caps && GST_IS_PAD (wavpackparse->srcpad))) {
      gst_buffer_unref (buf);
      goto done;
    }

    gst_pad_set_query_function (wavpackparse->srcpad,
        gst_wavpack_parse_src_query);
    gst_pad_set_event_function (wavpackparse->srcpad,
        gst_wavpack_parse_src_event);

    gst_pad_set_caps (wavpackparse->srcpad, caps);
    gst_pad_use_fixed_caps (wavpackparse->srcpad);

    gst_element_add_pad (GST_ELEMENT (wavpackparse), wavpackparse->srcpad);

  }

  if (wavpackparse->need_discont) {
    if (GST_IS_PAD (wavpackparse->srcpad)) {
      gst_pad_push_event (wavpackparse->srcpad,
          gst_event_new_new_segment (FALSE, 1.0, GST_FORMAT_TIME,
              wavpackparse->timestamp, GST_CLOCK_TIME_NONE, 0));

      wavpackparse->need_discont = FALSE;
    }
  }

  wavpackparse->flushed_bytes += header->ckSize + 8;

  wavpackparse->timestamp =
      ((gdouble) header->block_index / (gdouble) wavpackparse->samplerate) *
      GST_SECOND;
  GST_BUFFER_TIMESTAMP (buf) = wavpackparse->timestamp;
  GST_BUFFER_DURATION (buf) =
      ((gdouble) header->block_samples / (gdouble) wavpackparse->samplerate) *
      GST_SECOND;
  gst_buffer_set_caps (buf, GST_PAD_CAPS (wavpackparse->srcpad));

  if (GST_FLOW_OK != gst_pad_push (wavpackparse->srcpad, buf)) {
    gst_buffer_unref (buf);
  }

done:
  GST_PAD_STREAM_UNLOCK (wavpackparse->sinkpad);
  return;

}

static GstStateChangeReturn
gst_wavpack_parse_change_state (GstElement * element, GstStateChange transition)
{
  GstWavpackParse *wavpackparse = GST_WAVPACK_PARSE (element);
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      wavpackparse->flushed_bytes = 0;
      wavpackparse->need_discont = TRUE;
      wavpackparse->eos = FALSE;
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
    {
      GstQuery *query;
      GstFormat format = GST_FORMAT_BYTES;

      query = gst_query_new_duration (GST_FORMAT_BYTES);
      if (gst_pad_query (GST_PAD_PEER (wavpackparse->sinkpad), query)) {

        gst_query_parse_duration (query, &format,
            (gint64 *) & wavpackparse->duration);

        if (format != GST_FORMAT_BYTES) {
          wavpackparse->duration = -1;
          ret = GST_STATE_CHANGE_FAILURE;
        }

      } else {
        wavpackparse->duration = -1;
        ret = GST_STATE_CHANGE_FAILURE;
      }
      gst_query_unref (query);
    }
      break;
    default:
      break;
  }

  if (GST_ELEMENT_CLASS (parent)->change_state)
    ret = GST_ELEMENT_CLASS (parent)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      wavpackparse->seek_pending = FALSE;
      break;
    default:
      break;
  }

  return ret;
}


static gboolean
gst_wavepack_parse_sink_activate (GstPad * sinkpad)
{

  if (gst_pad_check_pull_range (sinkpad)) {
    return gst_pad_activate_pull (sinkpad, TRUE);
  } else {
    return FALSE;
  }
}

static gboolean
gst_wavepack_parse_sink_activate_pull (GstPad * sinkpad, gboolean active)
{

  gboolean result;

  if (active) {

    result = gst_pad_start_task (sinkpad,
        (GstTaskFunction) gst_wavpack_parse_loop, GST_PAD_PARENT (sinkpad));
  } else {
    result = gst_pad_stop_task (sinkpad);
  }

  return result;

  return TRUE;
}

gboolean
gst_wavpack_parse_plugin_init (GstPlugin * plugin)
{

  if (!gst_element_register (plugin, "wavpackparse",
          GST_RANK_PRIMARY, GST_TYPE_WAVPACK_PARSE)) {
    return FALSE;
  }

  GST_DEBUG_CATEGORY_INIT (gst_wavpack_parse_debug, "wavpackparse", 0,
      "wavpack file parser");

  return TRUE;
}
