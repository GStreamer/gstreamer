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

#include "gstflacdec.h"
#include <gst/gsttaginterface.h>

//#include <gst/tag/tag.h>

#include "flac_compat.h"

GST_DEBUG_CATEGORY_STATIC (flacdec_debug);
#define GST_CAT_DEFAULT flacdec_debug

static GstPadTemplate *src_template, *sink_template;

/* elementfactory information */
GstElementDetails flacdec_details = {
  "FLAC decoder",
  "Codec/Decoder/Audio",
  "Decodes FLAC lossless audio streams",
  "Wim Taymans <wim.taymans@chello.be>",
};

static void gst_flacdec_base_init (gpointer g_class);
static void gst_flacdec_class_init (FlacDecClass * klass);
static void gst_flacdec_init (FlacDec * flacdec);
static void gst_flacdec_finalize (GObject * object);

static void gst_flacdec_loop (GstPad * pad);
static GstStateChangeReturn gst_flacdec_change_state (GstElement * element,
    GstStateChange transition);
static const GstQueryType *gst_flacdec_get_src_query_types (GstPad * pad);
static gboolean gst_flacdec_src_query (GstPad * pad, GstQuery * query);
static gboolean gst_flacdec_convert_src (GstPad * pad, GstFormat src_format,
    gint64 src_value, GstFormat * dest_format, gint64 * dest_value);
static gboolean gst_flacdec_src_event (GstPad * pad, GstEvent * event);
static gboolean gst_flacdec_sink_activate (GstPad * sinkpad);
static gboolean gst_flacdec_sink_activate_pull (GstPad * sinkpad,
    gboolean active);

static FLAC__SeekableStreamDecoderReadStatus
gst_flacdec_read (const FLAC__SeekableStreamDecoder * decoder,
    FLAC__byte buffer[], unsigned *bytes, void *client_data);
static FLAC__SeekableStreamDecoderSeekStatus
gst_flacdec_seek (const FLAC__SeekableStreamDecoder * decoder,
    FLAC__uint64 position, void *client_data);
static FLAC__SeekableStreamDecoderTellStatus
gst_flacdec_tell (const FLAC__SeekableStreamDecoder * decoder,
    FLAC__uint64 * position, void *client_data);
static FLAC__SeekableStreamDecoderLengthStatus
gst_flacdec_length (const FLAC__SeekableStreamDecoder * decoder,
    FLAC__uint64 * length, void *client_data);
static FLAC__bool gst_flacdec_eof (const FLAC__SeekableStreamDecoder * decoder,
    void *client_data);
static FLAC__StreamDecoderWriteStatus
gst_flacdec_write (const FLAC__SeekableStreamDecoder * decoder,
    const FLAC__Frame * frame,
    const FLAC__int32 * const buffer[], void *client_data);
static void gst_flacdec_metadata_callback (const FLAC__SeekableStreamDecoder *
    decoder, const FLAC__StreamMetadata * metadata, void *client_data);
static void gst_flacdec_error_callback (const FLAC__SeekableStreamDecoder *
    decoder, FLAC__StreamDecoderErrorStatus status, void *client_data);

static GstElementClass *parent_class = NULL;

GType
flacdec_get_type (void)
{
  static GType flacdec_type = 0;

  if (!flacdec_type) {
    static const GTypeInfo flacdec_info = {
      sizeof (FlacDecClass),
      gst_flacdec_base_init,
      NULL,
      (GClassInitFunc) gst_flacdec_class_init,
      NULL,
      NULL,
      sizeof (FlacDec),
      0,
      (GInstanceInitFunc) gst_flacdec_init,
    };

    flacdec_type =
        g_type_register_static (GST_TYPE_ELEMENT, "FlacDec", &flacdec_info, 0);

    GST_DEBUG_CATEGORY_INIT (flacdec_debug, "flacdec", 0, "flac decoder");
  }
  return flacdec_type;
}

static GstCaps *
flac_caps_factory (void)
{
  return gst_caps_new_simple ("audio/x-flac", NULL);
  /* "rate",            GST_PROPS_INT_RANGE (11025, 48000),
   * "channels",        GST_PROPS_INT_RANGE (1, 6), */
}

static GstCaps *
raw_caps_factory (void)
{
  return gst_caps_from_string ("audio/x-raw-int,"
      "endianness = (int) " G_STRINGIFY (G_BYTE_ORDER) ", "
      "signed = (boolean) true, "
      "width = (int) { 8, 16, 32 }, "
      "depth = (int) { 8, 16, 24, 32 }, "
      "rate = (int) [ 11025, 48000 ], " "channels = (int) [ 1, 6 ]");
}

static void
gst_flacdec_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);
  GstCaps *raw_caps, *flac_caps;

  raw_caps = raw_caps_factory ();
  flac_caps = flac_caps_factory ();

  sink_template = gst_pad_template_new ("sink", GST_PAD_SINK,
      GST_PAD_ALWAYS, flac_caps);
  src_template = gst_pad_template_new ("src", GST_PAD_SRC,
      GST_PAD_ALWAYS, raw_caps);
  gst_element_class_add_pad_template (element_class, sink_template);
  gst_element_class_add_pad_template (element_class, src_template);
  gst_element_class_set_details (element_class, &flacdec_details);
}

static void
gst_flacdec_class_init (FlacDecClass * klass)
{
  GstElementClass *gstelement_class;
  GObjectClass *gobject_class;

  gstelement_class = (GstElementClass *) klass;
  gobject_class = (GObjectClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  gobject_class->finalize = gst_flacdec_finalize;

  gstelement_class->change_state = gst_flacdec_change_state;
}

static void
gst_flacdec_init (FlacDec * flacdec)
{
  flacdec->sinkpad = gst_pad_new_from_template (sink_template, "sink");
  gst_pad_set_activate_function (flacdec->sinkpad, gst_flacdec_sink_activate);
  gst_pad_set_activatepull_function (flacdec->sinkpad,
      gst_flacdec_sink_activate_pull);
  gst_element_add_pad (GST_ELEMENT (flacdec), flacdec->sinkpad);

  flacdec->srcpad = gst_pad_new_from_template (src_template, "src");
  gst_pad_set_query_type_function (flacdec->srcpad,
      gst_flacdec_get_src_query_types);
  gst_pad_set_query_function (flacdec->srcpad, gst_flacdec_src_query);
  gst_pad_set_event_function (flacdec->srcpad, gst_flacdec_src_event);
  gst_pad_use_fixed_caps (flacdec->srcpad);
  gst_element_add_pad (GST_ELEMENT (flacdec), flacdec->srcpad);

  flacdec->decoder = FLAC__seekable_stream_decoder_new ();
  flacdec->total_samples = 0;
  flacdec->init = TRUE;
  flacdec->eos = FALSE;
  flacdec->seek_pending = FALSE;

  FLAC__seekable_stream_decoder_set_read_callback (flacdec->decoder,
      gst_flacdec_read);
  FLAC__seekable_stream_decoder_set_seek_callback (flacdec->decoder,
      gst_flacdec_seek);
  FLAC__seekable_stream_decoder_set_tell_callback (flacdec->decoder,
      gst_flacdec_tell);
  FLAC__seekable_stream_decoder_set_length_callback (flacdec->decoder,
      gst_flacdec_length);
  FLAC__seekable_stream_decoder_set_eof_callback (flacdec->decoder,
      gst_flacdec_eof);
#if FLAC_VERSION >= 0x010003
  FLAC__seekable_stream_decoder_set_write_callback (flacdec->decoder,
      gst_flacdec_write);
#else
  FLAC__seekable_stream_decoder_set_write_callback (flacdec->decoder,
      (FLAC__StreamDecoderWriteStatus (*)
          (const FLAC__SeekableStreamDecoder * decoder,
              const FLAC__Frame * frame,
              const FLAC__int32 * buffer[], void *client_data))
      (gst_flacdec_write));
#endif
  FLAC__seekable_stream_decoder_set_metadata_respond (flacdec->decoder,
      FLAC__METADATA_TYPE_VORBIS_COMMENT);
  FLAC__seekable_stream_decoder_set_metadata_callback (flacdec->decoder,
      gst_flacdec_metadata_callback);
  FLAC__seekable_stream_decoder_set_error_callback (flacdec->decoder,
      gst_flacdec_error_callback);
  FLAC__seekable_stream_decoder_set_client_data (flacdec->decoder, flacdec);
}

static void
gst_flacdec_finalize (GObject * object)
{
  FlacDec *flacdec;

  flacdec = GST_FLACDEC (object);

  if (flacdec->decoder)
    FLAC__seekable_stream_decoder_delete (flacdec->decoder);
  flacdec->decoder = NULL;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}


static gboolean
gst_flacdec_update_metadata (FlacDec * flacdec,
    const FLAC__StreamMetadata * metadata)
{
  GstTagList *list;
  guint32 number_of_comments, cursor, str_len;
  gchar *p_value, *value, *name, *str_ptr;

  list = gst_tag_list_new ();
  if (list == NULL) {
    return FALSE;
  }

  number_of_comments = metadata->data.vorbis_comment.num_comments;
  value = NULL;
  GST_DEBUG ("%d tag(s) found", number_of_comments);
  for (cursor = 0; cursor < number_of_comments; cursor++) {
    str_ptr = (gchar *) metadata->data.vorbis_comment.comments[cursor].entry;
    str_len = metadata->data.vorbis_comment.comments[cursor].length;
    p_value = g_strstr_len (str_ptr, str_len, "=");
    if (p_value) {
      name = g_strndup (str_ptr, p_value - str_ptr);
      value = g_strndup (p_value + 1, str_ptr + str_len - p_value - 1);

      GST_DEBUG ("%s : %s", name, value);
      //gst_vorbis_tag_add (list, name, value);
      g_free (name);
      g_free (value);
    }
  }
  gst_tag_list_add (list, GST_TAG_MERGE_REPLACE,
      GST_TAG_AUDIO_CODEC, "FLAC", NULL);

  gst_element_found_tags_for_pad (GST_ELEMENT (flacdec), flacdec->srcpad, list);

  return TRUE;
}


static void
gst_flacdec_metadata_callback (const FLAC__SeekableStreamDecoder * decoder,
    const FLAC__StreamMetadata * metadata, void *client_data)
{
  FlacDec *flacdec;

  flacdec = GST_FLACDEC (client_data);

  switch (metadata->type) {
    case FLAC__METADATA_TYPE_STREAMINFO:
      flacdec->stream_samples = metadata->data.stream_info.total_samples;
      break;
    case FLAC__METADATA_TYPE_VORBIS_COMMENT:
      gst_flacdec_update_metadata (flacdec, metadata);
      break;
    default:
      break;
  }
}

static void
gst_flacdec_error_callback (const FLAC__SeekableStreamDecoder * decoder,
    FLAC__StreamDecoderErrorStatus status, void *client_data)
{
  FlacDec *flacdec;
  gchar *error;

  flacdec = GST_FLACDEC (client_data);

  switch (status) {
    case FLAC__STREAM_DECODER_ERROR_STATUS_LOST_SYNC:
      error = "lost sync";
      break;
    case FLAC__STREAM_DECODER_ERROR_STATUS_BAD_HEADER:
      error = "bad header";
      break;
    case FLAC__STREAM_DECODER_ERROR_STATUS_FRAME_CRC_MISMATCH:
      error = "CRC mismatch";
      break;
    default:
      error = "unknown error";
      break;
  }

  GST_ELEMENT_ERROR (flacdec, STREAM, DECODE, (NULL), (error));
}

static FLAC__SeekableStreamDecoderSeekStatus
gst_flacdec_seek (const FLAC__SeekableStreamDecoder * decoder,
    FLAC__uint64 position, void *client_data)
{
  FlacDec *flacdec;

  flacdec = GST_FLACDEC (client_data);

  GST_DEBUG ("seek %" G_GINT64_FORMAT, position);
  flacdec->offset = position;

  return FLAC__SEEKABLE_STREAM_DECODER_SEEK_STATUS_OK;
}

static FLAC__SeekableStreamDecoderTellStatus
gst_flacdec_tell (const FLAC__SeekableStreamDecoder * decoder,
    FLAC__uint64 * position, void *client_data)
{
  FlacDec *flacdec;

  flacdec = GST_FLACDEC (client_data);

  *position = flacdec->offset;

  GST_DEBUG ("tell %" G_GINT64_FORMAT, *position);

  return FLAC__SEEKABLE_STREAM_DECODER_TELL_STATUS_OK;
}

static FLAC__SeekableStreamDecoderLengthStatus
gst_flacdec_length (const FLAC__SeekableStreamDecoder * decoder,
    FLAC__uint64 * length, void *client_data)
{
  FlacDec *flacdec;
  GstFormat fmt = GST_FORMAT_BYTES;
  gint64 len;
  GstPad *peer;

  flacdec = GST_FLACDEC (client_data);

  if (!(peer = gst_pad_get_peer (flacdec->sinkpad)))
    return FLAC__SEEKABLE_STREAM_DECODER_TELL_STATUS_ERROR;
  gst_pad_query_position (peer, &fmt, NULL, &len);
  gst_object_unref (peer);
  if (fmt != GST_FORMAT_BYTES || len == -1)
    return FLAC__SEEKABLE_STREAM_DECODER_TELL_STATUS_ERROR;

  *length = len;

  GST_DEBUG ("length %" G_GINT64_FORMAT, *length);

  return FLAC__SEEKABLE_STREAM_DECODER_LENGTH_STATUS_OK;
}

static FLAC__bool
gst_flacdec_eof (const FLAC__SeekableStreamDecoder * decoder, void *client_data)
{
  FlacDec *flacdec;

  flacdec = GST_FLACDEC (client_data);
  GST_DEBUG ("eof %d", flacdec->eos);

  return flacdec->eos;
}

static FLAC__SeekableStreamDecoderReadStatus
gst_flacdec_read (const FLAC__SeekableStreamDecoder * decoder,
    FLAC__byte buffer[], unsigned *bytes, void *client_data)
{
  FlacDec *flacdec;
  GstBuffer *buf;

  flacdec = GST_FLACDEC (client_data);

  if (gst_pad_pull_range (flacdec->sinkpad, flacdec->offset, *bytes,
          &buf) != GST_FLOW_OK)
    return FLAC__SEEKABLE_STREAM_DECODER_READ_ERROR;

  GST_DEBUG ("Read %d bytes at %" G_GUINT64_FORMAT,
      GST_BUFFER_SIZE (buf), flacdec->offset);
  memcpy (buffer, GST_BUFFER_DATA (buf), GST_BUFFER_SIZE (buf));
  *bytes = GST_BUFFER_SIZE (buf);
  gst_buffer_unref (buf);
  flacdec->offset += *bytes;

  return FLAC__SEEKABLE_STREAM_DECODER_READ_STATUS_OK;
}

static FLAC__StreamDecoderWriteStatus
gst_flacdec_write (const FLAC__SeekableStreamDecoder * decoder,
    const FLAC__Frame * frame,
    const FLAC__int32 * const buffer[], void *client_data)
{
  FlacDec *flacdec;
  GstBuffer *outbuf;
  guint depth = frame->header.bits_per_sample;
  guint width = (depth == 24) ? 32 : depth;
  guint channels = frame->header.channels;
  guint samples = frame->header.blocksize;
  guint j, i;
  GstFlowReturn ret;

  flacdec = GST_FLACDEC (client_data);

  if (flacdec->need_discont) {
    gint64 time = 0;
    GstFormat format;
    GstEvent *newsegment;

    flacdec->need_discont = FALSE;

    if (flacdec->seek_pending) {
      flacdec->total_samples = flacdec->seek_value;
    }

    GST_DEBUG ("newsegment from %" G_GUINT64_FORMAT, flacdec->seek_value);

    format = GST_FORMAT_TIME;
    gst_flacdec_convert_src (flacdec->srcpad, GST_FORMAT_DEFAULT,
        flacdec->total_samples, &format, &time);
    newsegment = gst_event_new_newsegment (1.0, GST_FORMAT_TIME, time,
        GST_CLOCK_TIME_NONE, 0);

    if (!gst_pad_push_event (flacdec->srcpad, newsegment))
      return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
  }

  if (!GST_PAD_CAPS (flacdec->srcpad)) {
    GST_DEBUG ("Negotiating %d Hz @ %d channels",
        frame->header.sample_rate, channels);

    if (!gst_pad_set_caps (flacdec->srcpad,
            gst_caps_new_simple ("audio/x-raw-int",
                "endianness", G_TYPE_INT, G_BYTE_ORDER,
                "signed", G_TYPE_BOOLEAN, TRUE,
                "width", G_TYPE_INT, width,
                "depth", G_TYPE_INT, depth,
                "rate", G_TYPE_INT, frame->header.sample_rate,
                "channels", G_TYPE_INT, channels, NULL)))
      return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;

    flacdec->depth = depth;
    flacdec->width = width;
    flacdec->channels = channels;
    flacdec->frequency = frame->header.sample_rate;
  }

  gst_pad_alloc_buffer (flacdec->srcpad, flacdec->total_samples,
      samples * channels * ((width + 7) >> 3), GST_PAD_CAPS (flacdec->srcpad),
      &outbuf);
  GST_BUFFER_TIMESTAMP (outbuf) =
      flacdec->total_samples * GST_SECOND / frame->header.sample_rate;
  GST_BUFFER_DURATION (outbuf) =
      samples * GST_SECOND / frame->header.sample_rate;

  if (depth == 8) {
    guint8 *outbuffer = (guint8 *) GST_BUFFER_DATA (outbuf);

    for (i = 0; i < samples; i++) {
      for (j = 0; j < channels; j++) {
        *outbuffer++ = (guint8) buffer[j][i];
      }
    }
  } else if (depth == 16) {
    guint16 *outbuffer = (guint16 *) GST_BUFFER_DATA (outbuf);

    for (i = 0; i < samples; i++) {
      for (j = 0; j < channels; j++) {
        *outbuffer++ = (guint16) buffer[j][i];
      }
    }
  } else if (depth == 24 || depth == 32) {
    guint32 *outbuffer = (guint32 *) GST_BUFFER_DATA (outbuf);

    for (i = 0; i < samples; i++) {
      for (j = 0; j < channels; j++) {
        *outbuffer++ = (guint32) buffer[j][i];
      }
    }
  } else {
    g_warning ("flacdec: invalid depth %d found\n", depth);
    return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
  }
  GST_DEBUG ("Pushing %d samples, %" GST_TIME_FORMAT ":%" GST_TIME_FORMAT,
      samples, GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (outbuf)),
      GST_TIME_ARGS (GST_BUFFER_DURATION (outbuf)));

  ret = gst_pad_push (flacdec->srcpad, outbuf);
  if (ret != GST_FLOW_NOT_LINKED && ret != GST_FLOW_OK) {
    GST_DEBUG ("Invalid return code %d", (gint) ret);
    return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
  }
  flacdec->total_samples += samples;

  return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
}

static void
gst_flacdec_loop (GstPad * sinkpad)
{
  FlacDec *flacdec;
  gboolean res;
  FLAC__SeekableStreamDecoderState s;

  flacdec = GST_FLACDEC (GST_OBJECT_PARENT (sinkpad));

  GST_STREAM_LOCK (sinkpad);

  GST_DEBUG ("flacdec: entering loop");
  if (flacdec->init) {
    FLAC__StreamDecoderState res;

    GST_DEBUG ("flacdec: initializing decoder");
    res = FLAC__seekable_stream_decoder_init (flacdec->decoder);
    if (res != FLAC__SEEKABLE_STREAM_DECODER_OK) {
      GST_ELEMENT_ERROR (flacdec, LIBRARY, INIT, (NULL),
          (FLAC__SeekableStreamDecoderStateString[res]));
      goto end;
    }
    /*    FLAC__seekable_stream_decoder_process_metadata (flacdec->decoder); */
    flacdec->init = FALSE;
  }

  if (flacdec->seek_pending) {
    GST_DEBUG ("perform seek to sample %" G_GINT64_FORMAT, flacdec->seek_value);

    if (FLAC__seekable_stream_decoder_seek_absolute (flacdec->decoder,
            flacdec->seek_value)) {
      flacdec->total_samples = flacdec->seek_value;
      flacdec->need_discont = TRUE;
      GST_DEBUG ("seek done");
    } else {
      GST_DEBUG ("seek failed");
    }
    flacdec->seek_pending = FALSE;
  }

  GST_DEBUG ("flacdec: processing single");
  res = FLAC__seekable_stream_decoder_process_single (flacdec->decoder);
  if (!res)
    goto end;
  GST_DEBUG ("flacdec: checking for EOS");
  if ((s = FLAC__seekable_stream_decoder_get_state (flacdec->decoder)) ==
      FLAC__SEEKABLE_STREAM_DECODER_END_OF_STREAM) {
    GstEvent *event;

    GST_DEBUG ("flacdec: sending EOS event");
    FLAC__seekable_stream_decoder_reset (flacdec->decoder);

    event = gst_event_new_eos ();
    if (!gst_pad_push_event (flacdec->srcpad, event))
      goto end;
  } else if (s >= FLAC__SEEKABLE_STREAM_DECODER_MEMORY_ALLOCATION_ERROR &&
      s <= FLAC__SEEKABLE_STREAM_DECODER_INVALID_CALLBACK) {
    GST_DEBUG ("Error: %d", s);
    goto end;
  }
  GST_DEBUG ("flacdec: _loop end");
  GST_STREAM_UNLOCK (sinkpad);
  return;

end:
  GST_DEBUG ("pausing");
  gst_pad_pause_task (sinkpad);
  GST_STREAM_UNLOCK (sinkpad);
}

#if 0
static const GstFormat *
gst_flacdec_get_src_formats (GstPad * pad)
{
  static const GstFormat formats[] = {
    GST_FORMAT_DEFAULT,
    GST_FORMAT_BYTES,
    GST_FORMAT_TIME,
    0,
  };

  return formats;
}
#endif

static gboolean
gst_flacdec_convert_src (GstPad * pad, GstFormat src_format, gint64 src_value,
    GstFormat * dest_format, gint64 * dest_value)
{
  gboolean res = TRUE;
  FlacDec *flacdec = GST_FLACDEC (gst_pad_get_parent (pad));
  guint scale = 1;
  gint bytes_per_sample;

  bytes_per_sample = flacdec->channels * ((flacdec->width + 7) >> 3);

  switch (src_format) {
    case GST_FORMAT_BYTES:
      switch (*dest_format) {
        case GST_FORMAT_DEFAULT:
          if (bytes_per_sample == 0)
            return FALSE;
          *dest_value = src_value / bytes_per_sample;
          break;
        case GST_FORMAT_TIME:
        {
          gint byterate = bytes_per_sample * flacdec->frequency;

          if (byterate == 0)
            return FALSE;
          *dest_value = src_value * GST_SECOND / byterate;
          break;
        }
        default:
          res = FALSE;
      }
      break;
    case GST_FORMAT_DEFAULT:
      switch (*dest_format) {
        case GST_FORMAT_BYTES:
          *dest_value = src_value * bytes_per_sample;
          break;
        case GST_FORMAT_TIME:
          if (flacdec->frequency == 0)
            return FALSE;
          *dest_value = src_value * GST_SECOND / flacdec->frequency;
          break;
        default:
          res = FALSE;
      }
      break;
    case GST_FORMAT_TIME:
      switch (*dest_format) {
        case GST_FORMAT_BYTES:
          scale = bytes_per_sample;
        case GST_FORMAT_DEFAULT:
          *dest_value = src_value * scale * flacdec->frequency / GST_SECOND;
          break;
        default:
          res = FALSE;
      }
      break;
    default:
      res = FALSE;
  }
  return res;
}

static const GstQueryType *
gst_flacdec_get_src_query_types (GstPad * pad)
{
  static const GstQueryType types[] = {
    GST_QUERY_POSITION,
    0,
  };

  return types;
}

static gboolean
gst_flacdec_src_query (GstPad * pad, GstQuery * query)
{
  gboolean res = TRUE;
  FlacDec *flacdec = GST_FLACDEC (gst_pad_get_parent (pad));

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_POSITION:{
      gint64 len, pos;
      GstFormat fmt = GST_FORMAT_TIME;

      if (flacdec->stream_samples == 0)
        len = flacdec->total_samples;
      else
        len = flacdec->stream_samples;
      pos = flacdec->total_samples;

      if (gst_flacdec_convert_src (flacdec->srcpad,
              GST_FORMAT_DEFAULT, len, &fmt, &len) &&
          gst_flacdec_convert_src (flacdec->srcpad,
              GST_FORMAT_DEFAULT, pos, &fmt, &pos))
        gst_query_set_position (query, GST_FORMAT_TIME, pos, len);
      else
        res = FALSE;
      break;
    }
    default:
      res = FALSE;
      break;
  }

  return res;
}

static gboolean
gst_flacdec_src_event (GstPad * pad, GstEvent * event)
{
  gboolean res = TRUE;
  FlacDec *flacdec = GST_FLACDEC (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:{
      GstFormat format, seek_fmt;
      GstSeekType type;
      gint64 pos;

      gst_event_parse_seek (event, NULL, &seek_fmt, NULL, &type, &pos,
          NULL, NULL);

      format = GST_FORMAT_DEFAULT;

      if (type == GST_SEEK_TYPE_SET &&
          gst_flacdec_convert_src (flacdec->srcpad, seek_fmt, pos,
              &format, &pos)) {
        GST_DEBUG ("Initializing seek");
        g_print ("Grab seek lock\n");
        gst_pad_push_event (flacdec->srcpad, gst_event_new_flush_start ());
        GST_STREAM_LOCK (flacdec->sinkpad);
        g_print ("Got seek lock\n");
        gst_pad_push_event (flacdec->srcpad, gst_event_new_flush_stop ());
        GST_DEBUG ("Ready");
        flacdec->seek_pending = TRUE;
        flacdec->seek_value = pos;
        gst_pad_start_task (flacdec->sinkpad,
            (GstTaskFunction) gst_flacdec_loop, flacdec->sinkpad);
        GST_STREAM_UNLOCK (flacdec->sinkpad);
      } else
        res = FALSE;
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
gst_flacdec_sink_activate (GstPad * sinkpad)
{
  if (gst_pad_check_pull_range (sinkpad))
    return gst_pad_activate_pull (sinkpad, TRUE);

  return FALSE;
}

static gboolean
gst_flacdec_sink_activate_pull (GstPad * sinkpad, gboolean active)
{
  if (active) {
    /* if we have a scheduler we can start the task */
    GST_FLACDEC (GST_OBJECT_PARENT (sinkpad))->offset = 0;
    gst_pad_start_task (sinkpad, (GstTaskFunction) gst_flacdec_loop, sinkpad);
  } else {
    gst_pad_stop_task (sinkpad);
  }

  return TRUE;
}

static GstStateChangeReturn
gst_flacdec_change_state (GstElement * element, GstStateChange transition)
{
  FlacDec *flacdec = GST_FLACDEC (element);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      flacdec->seek_pending = FALSE;
      flacdec->total_samples = 0;
      flacdec->eos = FALSE;
      flacdec->need_discont = TRUE;
      if (flacdec->init == FALSE) {
        FLAC__seekable_stream_decoder_reset (flacdec->decoder);
      }
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      flacdec->eos = FALSE;
      break;
    default:
      break;
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  return GST_STATE_CHANGE_SUCCESS;
}
