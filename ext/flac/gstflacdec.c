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

/**
 * SECTION:element-flacdec
 * @seealso: flacenc
 *
 * <refsect2>
 * <para>
 * flacdec decodes FLAC streams.
 * <ulink url="http://flac.sourceforge.net/">FLAC</ulink>
 * is a Free Lossless Audio Codec.
 * </para>
 * <title>Example launch line</title>
 * <para>
 * <programlisting>
 * gst-launch filesrc location=media/small/dark.441-16-s.flac ! flacdec ! audioconvert ! audioresample ! autoaudiosink
 * </programlisting>
 * </para>
 * </refsect2>
 */

/*
 * FIXME: this pipeline doesn't work, but we want to use it as example
 * gst-launch gnomevfssrc location=http://gstreamer.freedesktop.org/media/small/dark.441-16-s.flac ! flacdec ! autoaudiosink
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <string.h>

#include "gstflacdec.h"
#include <gst/gsttagsetter.h>

#include <gst/tag/tag.h>

#include "flac_compat.h"

GST_DEBUG_CATEGORY_STATIC (flacdec_debug);
#define GST_CAT_DEFAULT flacdec_debug

static GstPadTemplate *src_template, *sink_template;

static GstElementDetails flacdec_details =
GST_ELEMENT_DETAILS ("FLAC audio decoder",
    "Codec/Decoder/Audio",
    "Decodes FLAC lossless audio streams",
    "Wim Taymans <wim.taymans@chello.be>");


static void gst_flac_dec_finalize (GObject * object);

static void gst_flac_dec_loop (GstPad * pad);
static GstStateChangeReturn gst_flac_dec_change_state (GstElement * element,
    GstStateChange transition);
static const GstQueryType *gst_flac_dec_get_src_query_types (GstPad * pad);
static gboolean gst_flac_dec_src_query (GstPad * pad, GstQuery * query);
static gboolean gst_flac_dec_convert_src (GstPad * pad, GstFormat src_format,
    gint64 src_value, GstFormat * dest_format, gint64 * dest_value);
static gboolean gst_flac_dec_src_event (GstPad * pad, GstEvent * event);
static gboolean gst_flac_dec_sink_activate (GstPad * sinkpad);
static gboolean gst_flac_dec_sink_activate_pull (GstPad * sinkpad,
    gboolean active);
static void gst_flac_dec_send_newsegment (GstFlacDec * flacdec,
    gboolean update);

static FLAC__SeekableStreamDecoderReadStatus
gst_flac_dec_read (const FLAC__SeekableStreamDecoder * decoder,
    FLAC__byte buffer[], unsigned *bytes, void *client_data);
static FLAC__SeekableStreamDecoderSeekStatus
gst_flac_dec_seek (const FLAC__SeekableStreamDecoder * decoder,
    FLAC__uint64 position, void *client_data);
static FLAC__SeekableStreamDecoderTellStatus
gst_flac_dec_tell (const FLAC__SeekableStreamDecoder * decoder,
    FLAC__uint64 * position, void *client_data);
static FLAC__SeekableStreamDecoderLengthStatus
gst_flac_dec_length (const FLAC__SeekableStreamDecoder * decoder,
    FLAC__uint64 * length, void *client_data);
static FLAC__bool gst_flac_dec_eof (const FLAC__SeekableStreamDecoder * decoder,
    void *client_data);
static FLAC__StreamDecoderWriteStatus
gst_flac_dec_write (const FLAC__SeekableStreamDecoder * decoder,
    const FLAC__Frame * frame,
    const FLAC__int32 * const buffer[], void *client_data);
static void gst_flac_dec_metadata_callback (const FLAC__SeekableStreamDecoder *
    decoder, const FLAC__StreamMetadata * metadata, void *client_data);
static void gst_flac_dec_error_callback (const FLAC__SeekableStreamDecoder *
    decoder, FLAC__StreamDecoderErrorStatus status, void *client_data);

GST_BOILERPLATE (GstFlacDec, gst_flac_dec, GstElement, GST_TYPE_ELEMENT)
#define GST_FLAC_DEC_SRC_CAPS                             \
    "audio/x-raw-int, "                                   \
    "endianness = (int) " G_STRINGIFY (G_BYTE_ORDER) ", " \
    "signed = (boolean) true, "                           \
    "width = (int) { 8, 16, 32 }, "                       \
    "depth = (int) { 8, 12, 16, 20, 24, 32 }, "           \
    "rate = (int) [ 8000, 96000 ], "                      \
    "channels = (int) [ 1, 8 ]"
     static void gst_flac_dec_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);
  GstCaps *raw_caps, *flac_caps;

  raw_caps = gst_caps_from_string (GST_FLAC_DEC_SRC_CAPS);
  flac_caps = gst_caps_new_simple ("audio/x-flac", NULL);

  sink_template = gst_pad_template_new ("sink", GST_PAD_SINK,
      GST_PAD_ALWAYS, flac_caps);
  src_template = gst_pad_template_new ("src", GST_PAD_SRC,
      GST_PAD_ALWAYS, raw_caps);
  gst_element_class_add_pad_template (element_class, sink_template);
  gst_element_class_add_pad_template (element_class, src_template);
  gst_element_class_set_details (element_class, &flacdec_details);

  GST_DEBUG_CATEGORY_INIT (flacdec_debug, "flacdec", 0, "flac decoder");
}

static void
gst_flac_dec_class_init (GstFlacDecClass * klass)
{
  GstElementClass *gstelement_class;
  GObjectClass *gobject_class;

  gstelement_class = (GstElementClass *) klass;
  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = gst_flac_dec_finalize;

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_flac_dec_change_state);
}

static void
gst_flac_dec_init (GstFlacDec * flacdec, GstFlacDecClass * klass)
{
  flacdec->sinkpad = gst_pad_new_from_template (sink_template, "sink");
  gst_pad_set_activate_function (flacdec->sinkpad,
      GST_DEBUG_FUNCPTR (gst_flac_dec_sink_activate));
  gst_pad_set_activatepull_function (flacdec->sinkpad,
      GST_DEBUG_FUNCPTR (gst_flac_dec_sink_activate_pull));
  gst_element_add_pad (GST_ELEMENT (flacdec), flacdec->sinkpad);

  flacdec->srcpad = gst_pad_new_from_template (src_template, "src");
  gst_pad_set_query_type_function (flacdec->srcpad,
      GST_DEBUG_FUNCPTR (gst_flac_dec_get_src_query_types));
  gst_pad_set_query_function (flacdec->srcpad,
      GST_DEBUG_FUNCPTR (gst_flac_dec_src_query));
  gst_pad_set_event_function (flacdec->srcpad,
      GST_DEBUG_FUNCPTR (gst_flac_dec_src_event));
  gst_pad_use_fixed_caps (flacdec->srcpad);
  gst_element_add_pad (GST_ELEMENT (flacdec), flacdec->srcpad);

  flacdec->decoder = FLAC__seekable_stream_decoder_new ();
  flacdec->segment.last_stop = 0;
  flacdec->init = TRUE;

  FLAC__seekable_stream_decoder_set_read_callback (flacdec->decoder,
      gst_flac_dec_read);
  FLAC__seekable_stream_decoder_set_seek_callback (flacdec->decoder,
      gst_flac_dec_seek);
  FLAC__seekable_stream_decoder_set_tell_callback (flacdec->decoder,
      gst_flac_dec_tell);
  FLAC__seekable_stream_decoder_set_length_callback (flacdec->decoder,
      gst_flac_dec_length);
  FLAC__seekable_stream_decoder_set_eof_callback (flacdec->decoder,
      gst_flac_dec_eof);
#if FLAC_VERSION >= 0x010003
  FLAC__seekable_stream_decoder_set_write_callback (flacdec->decoder,
      gst_flac_dec_write);
#else
  FLAC__seekable_stream_decoder_set_write_callback (flacdec->decoder,
      (FLAC__StreamDecoderWriteStatus (*)
          (const FLAC__SeekableStreamDecoder * decoder,
              const FLAC__Frame * frame,
              const FLAC__int32 * buffer[], void *client_data))
      (gst_flac_dec_write));
#endif
  FLAC__seekable_stream_decoder_set_metadata_respond (flacdec->decoder,
      FLAC__METADATA_TYPE_VORBIS_COMMENT);
  FLAC__seekable_stream_decoder_set_metadata_callback (flacdec->decoder,
      gst_flac_dec_metadata_callback);
  FLAC__seekable_stream_decoder_set_error_callback (flacdec->decoder,
      gst_flac_dec_error_callback);
  FLAC__seekable_stream_decoder_set_client_data (flacdec->decoder, flacdec);
}

static void
gst_flac_dec_finalize (GObject * object)
{
  GstFlacDec *flacdec;

  flacdec = GST_FLAC_DEC (object);

  if (flacdec->decoder)
    FLAC__seekable_stream_decoder_delete (flacdec->decoder);
  flacdec->decoder = NULL;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}


static gboolean
gst_flac_dec_update_metadata (GstFlacDec * flacdec,
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
      gst_vorbis_tag_add (list, name, value);
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
gst_flac_dec_metadata_callback (const FLAC__SeekableStreamDecoder * decoder,
    const FLAC__StreamMetadata * metadata, void *client_data)
{
  GstFlacDec *flacdec;

  flacdec = GST_FLAC_DEC (client_data);

  switch (metadata->type) {
    case FLAC__METADATA_TYPE_STREAMINFO:
      gst_segment_set_duration (&flacdec->segment, GST_FORMAT_DEFAULT,
          metadata->data.stream_info.total_samples);
      if (flacdec->segment.stop == -1)
        flacdec->segment.stop = metadata->data.stream_info.total_samples;
      break;
    case FLAC__METADATA_TYPE_VORBIS_COMMENT:
      gst_flac_dec_update_metadata (flacdec, metadata);
      break;
    default:
      break;
  }
}

static void
gst_flac_dec_error_callback (const FLAC__SeekableStreamDecoder * decoder,
    FLAC__StreamDecoderErrorStatus status, void *client_data)
{
  GstFlacDec *flacdec;
  gchar *error;

  flacdec = GST_FLAC_DEC (client_data);

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
  flacdec->last_flow = GST_FLOW_ERROR;
}

static FLAC__SeekableStreamDecoderSeekStatus
gst_flac_dec_seek (const FLAC__SeekableStreamDecoder * decoder,
    FLAC__uint64 position, void *client_data)
{
  GstFlacDec *flacdec;

  flacdec = GST_FLAC_DEC (client_data);

  GST_DEBUG ("seek %" G_GINT64_FORMAT, position);
  flacdec->offset = position;

  return FLAC__SEEKABLE_STREAM_DECODER_SEEK_STATUS_OK;
}

static FLAC__SeekableStreamDecoderTellStatus
gst_flac_dec_tell (const FLAC__SeekableStreamDecoder * decoder,
    FLAC__uint64 * position, void *client_data)
{
  GstFlacDec *flacdec;

  flacdec = GST_FLAC_DEC (client_data);

  *position = flacdec->offset;

  GST_DEBUG ("tell %" G_GINT64_FORMAT, *position);

  return FLAC__SEEKABLE_STREAM_DECODER_TELL_STATUS_OK;
}

static FLAC__SeekableStreamDecoderLengthStatus
gst_flac_dec_length (const FLAC__SeekableStreamDecoder * decoder,
    FLAC__uint64 * length, void *client_data)
{
  GstFlacDec *flacdec;
  GstFormat fmt = GST_FORMAT_BYTES;
  gint64 len;
  GstPad *peer;

  flacdec = GST_FLAC_DEC (client_data);

  if (!(peer = gst_pad_get_peer (flacdec->sinkpad)))
    return FLAC__SEEKABLE_STREAM_DECODER_TELL_STATUS_ERROR;
  gst_pad_query_duration (peer, &fmt, &len);
  gst_object_unref (peer);
  if (fmt != GST_FORMAT_BYTES || len == -1)
    return FLAC__SEEKABLE_STREAM_DECODER_TELL_STATUS_ERROR;

  *length = len;

  GST_DEBUG ("length %" G_GINT64_FORMAT, *length);

  return FLAC__SEEKABLE_STREAM_DECODER_LENGTH_STATUS_OK;
}

static FLAC__bool
gst_flac_dec_eof (const FLAC__SeekableStreamDecoder * decoder,
    void *client_data)
{
  GstFlacDec *flacdec;
  GstFormat fmt;
  GstPad *peer;
  gboolean ret = FALSE;
  gint64 len;

  flacdec = GST_FLAC_DEC (client_data);

  if (!(peer = gst_pad_get_peer (flacdec->sinkpad))) {
    GST_WARNING_OBJECT (flacdec, "no peer pad, returning EOF");
    return TRUE;
  }

  fmt = GST_FORMAT_BYTES;
  if (gst_pad_query_duration (peer, &fmt, &len) && fmt == GST_FORMAT_BYTES &&
      len != -1 && flacdec->offset >= len) {
    GST_DEBUG ("offset=%" G_GINT64_FORMAT ", len=%" G_GINT64_FORMAT
        ", returning EOF", flacdec->offset, len);
    ret = TRUE;
  }

  gst_object_unref (peer);

  return ret;
}

static FLAC__SeekableStreamDecoderReadStatus
gst_flac_dec_read (const FLAC__SeekableStreamDecoder * decoder,
    FLAC__byte buffer[], unsigned *bytes, void *client_data)
{
  GstFlacDec *flacdec;
  GstBuffer *buf;

  flacdec = GST_FLAC_DEC (client_data);

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
gst_flac_dec_write (const FLAC__SeekableStreamDecoder * decoder,
    const FLAC__Frame * frame,
    const FLAC__int32 * const buffer[], void *client_data)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GstFlacDec *flacdec;
  GstBuffer *outbuf;
  guint depth = frame->header.bits_per_sample;
  guint width;
  guint channels = frame->header.channels;
  guint samples = frame->header.blocksize;
  guint j, i;

  flacdec = GST_FLAC_DEC (client_data);

  switch (depth) {
    case 8:
      width = 8;
      break;
    case 12:
    case 16:
      width = 16;
      break;
    case 20:
    case 24:
    case 32:
      width = 32;
      break;
    default:
      g_assert_not_reached ();
  }

  if (!GST_PAD_CAPS (flacdec->srcpad)) {
    GstCaps *caps;

    GST_DEBUG ("Negotiating %d Hz @ %d channels",
        frame->header.sample_rate, channels);

    caps = gst_caps_new_simple ("audio/x-raw-int",
        "endianness", G_TYPE_INT, G_BYTE_ORDER,
        "signed", G_TYPE_BOOLEAN, TRUE,
        "width", G_TYPE_INT, width,
        "depth", G_TYPE_INT, depth,
        "rate", G_TYPE_INT, frame->header.sample_rate,
        "channels", G_TYPE_INT, channels, NULL);

    if (!gst_pad_set_caps (flacdec->srcpad, caps)) {
      GST_ELEMENT_ERROR (flacdec, CORE, NEGOTIATION, (NULL),
          ("Failed to negotiate caps %" GST_PTR_FORMAT, caps));
      flacdec->last_flow = GST_FLOW_ERROR;
      gst_caps_unref (caps);
      return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
    }

    gst_caps_unref (caps);

    flacdec->depth = depth;
    flacdec->width = width;
    flacdec->channels = channels;
    flacdec->sample_rate = frame->header.sample_rate;
  }

  if (flacdec->need_newsegment) {
    gst_flac_dec_send_newsegment (flacdec, FALSE);
    flacdec->need_newsegment = FALSE;
  }

  ret = gst_pad_alloc_buffer_and_set_caps (flacdec->srcpad,
      flacdec->segment.last_stop, samples * channels * (width / 8),
      GST_PAD_CAPS (flacdec->srcpad), &outbuf);

  if (ret != GST_FLOW_OK) {
    GST_DEBUG ("gst_pad_alloc_buffer() returned %s", gst_flow_get_name (ret));
    goto done;
  }

  GST_BUFFER_TIMESTAMP (outbuf) =
      gst_util_uint64_scale_int (flacdec->segment.last_stop, GST_SECOND,
      frame->header.sample_rate);

  GST_BUFFER_DURATION (outbuf) =
      gst_util_uint64_scale_int (samples, GST_SECOND,
      frame->header.sample_rate);

  if (depth == 8) {
    gint8 *outbuffer = (gint8 *) GST_BUFFER_DATA (outbuf);

    for (i = 0; i < samples; i++) {
      for (j = 0; j < channels; j++) {
        *outbuffer++ = (gint8) buffer[j][i];
      }
    }
  } else if (depth == 12 || depth == 16) {
    gint16 *outbuffer = (gint16 *) GST_BUFFER_DATA (outbuf);

    for (i = 0; i < samples; i++) {
      for (j = 0; j < channels; j++) {
        *outbuffer++ = (gint16) buffer[j][i];
      }
    }
  } else if (depth == 20 || depth == 24 || depth == 32) {
    gint32 *outbuffer = (gint32 *) GST_BUFFER_DATA (outbuf);

    for (i = 0; i < samples; i++) {
      for (j = 0; j < channels; j++) {
        *outbuffer++ = (gint32) buffer[j][i];
      }
    }
  } else {
    g_assert_not_reached ();
  }

  if (!flacdec->seeking) {
    GST_DEBUG ("pushing %d samples at offset %" G_GINT64_FORMAT
        "(%" GST_TIME_FORMAT " + %" GST_TIME_FORMAT ")",
        samples, GST_BUFFER_OFFSET (outbuf),
        GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (outbuf)),
        GST_TIME_ARGS (GST_BUFFER_DURATION (outbuf)));

    ret = gst_pad_push (flacdec->srcpad, outbuf);
  } else {
    GST_DEBUG ("not pushing %d samples at offset %" G_GINT64_FORMAT
        " (in seek)", samples, GST_BUFFER_OFFSET (outbuf));
    gst_buffer_unref (outbuf);
    ret = GST_FLOW_OK;
  }

  if (ret != GST_FLOW_OK) {
    GST_DEBUG ("gst_pad_push() returned %s", gst_flow_get_name (ret));
  }

done:

  flacdec->segment.last_stop += samples;

  /* we act on the flow return value later in the loop function, as we don't
   * want to mess up the internal decoder state by returning ABORT when the
   * error is in fact non-fatal (like a pad in flushing mode) and we want
   * to continue later. So just pretend everything's dandy and act later. */
  flacdec->last_flow = ret;

  return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
}

static void
gst_flac_dec_loop (GstPad * sinkpad)
{
  GstFlacDec *flacdec;
  FLAC__SeekableStreamDecoderState s;

  flacdec = GST_FLAC_DEC (GST_OBJECT_PARENT (sinkpad));

  GST_DEBUG_OBJECT (flacdec, "entering loop");

  if (flacdec->init) {
    GST_DEBUG_OBJECT (flacdec, "initializing decoder");
    s = FLAC__seekable_stream_decoder_init (flacdec->decoder);
    if (s != FLAC__SEEKABLE_STREAM_DECODER_OK)
      goto analyze_state;
    /*    FLAC__seekable_stream_decoder_process_metadata (flacdec->decoder); */
    flacdec->init = FALSE;
  }

  flacdec->last_flow = GST_FLOW_OK;

  GST_DEBUG_OBJECT (flacdec, "processing single");
  FLAC__seekable_stream_decoder_process_single (flacdec->decoder);

analyze_state:

  GST_DEBUG_OBJECT (flacdec, "done processing, checking encoder state");
  s = FLAC__seekable_stream_decoder_get_state (flacdec->decoder);
  switch (s) {
    case FLAC__SEEKABLE_STREAM_DECODER_OK:
    case FLAC__SEEKABLE_STREAM_DECODER_SEEKING:{
      GST_DEBUG_OBJECT (flacdec, "everything ok");

      if (flacdec->last_flow != GST_FLOW_OK &&
          flacdec->last_flow != GST_FLOW_NOT_LINKED) {
        GST_DEBUG_OBJECT (flacdec, "last_flow return was %s, pausing",
            gst_flow_get_name (flacdec->last_flow));
        gst_pad_pause_task (sinkpad);
      }

/* FIXME: support segment seeks
      if (((flacdec->segment.flags & GST_SEEK_FLAG_SEGMENT) != 0) &&
          flacdec->segment.last_stop > 0 && flacdec->segment.stop != -1 &&
          flacdec->segment.last_stop >= flacdec->segment.stop) {
        GST_DEBUG_OBJECT (flacdec, "reached the end of the configured"
            " segment, posting SEGMENT_DONE message and pausing");
        gst_pad_pause_task (sinkpad);
        gst_element_post_message (GST_ELEMENT (flacdec),
            gst_message_new_segment_done (GST_OBJECT (flacdec),
                GST_FORMAT_DEFAULT, flacdec->segment.stop));
      }
*/
      return;
    }

    case FLAC__SEEKABLE_STREAM_DECODER_END_OF_STREAM:{
      GST_DEBUG_OBJECT (flacdec, "EOS, pushing downstream");
      FLAC__seekable_stream_decoder_reset (flacdec->decoder);

      gst_pad_push_event (flacdec->srcpad, gst_event_new_eos ());

      GST_DEBUG_OBJECT (flacdec, "pausing");
      gst_pad_pause_task (sinkpad);
      return;
    }

    case FLAC__SEEKABLE_STREAM_DECODER_MEMORY_ALLOCATION_ERROR:
    case FLAC__SEEKABLE_STREAM_DECODER_STREAM_DECODER_ERROR:
    case FLAC__SEEKABLE_STREAM_DECODER_READ_ERROR:
    case FLAC__SEEKABLE_STREAM_DECODER_SEEK_ERROR:
    case FLAC__SEEKABLE_STREAM_DECODER_ALREADY_INITIALIZED:
    case FLAC__SEEKABLE_STREAM_DECODER_INVALID_CALLBACK:
    case FLAC__SEEKABLE_STREAM_DECODER_UNINITIALIZED:
    default:{
      /* fixme: this error sucks -- should try to figure out when/if an more
         specific error was already sent via the callback */
      GST_ELEMENT_ERROR (flacdec, STREAM, DECODE, (NULL),
          ("%s", FLAC__SeekableStreamDecoderStateString[s]));

      gst_pad_push_event (flacdec->srcpad, gst_event_new_eos ());

      GST_DEBUG_OBJECT (flacdec, "pausing");
      gst_pad_pause_task (sinkpad);
      return;
    }
  }
}

static gboolean
gst_flac_dec_convert_src (GstPad * pad, GstFormat src_format, gint64 src_value,
    GstFormat * dest_format, gint64 * dest_value)
{
  GstFlacDec *flacdec = GST_FLAC_DEC (GST_PAD_PARENT (pad));
  gboolean res = TRUE;
  guint bytes_per_sample;
  guint scale = 1;

  if (flacdec->width == 0 || flacdec->channels == 0 ||
      flacdec->sample_rate == 0) {
    /* no frame decoded yet */
    GST_DEBUG_OBJECT (flacdec, "cannot convert: not set up yet");
    return FALSE;
  }

  bytes_per_sample = flacdec->channels * (flacdec->width / 8);

  switch (src_format) {
    case GST_FORMAT_BYTES:{
      switch (*dest_format) {
        case GST_FORMAT_DEFAULT:
          *dest_value =
              gst_util_uint64_scale_int (src_value, 1, bytes_per_sample);
          break;
        case GST_FORMAT_TIME:
        {
          gint byterate = bytes_per_sample * flacdec->sample_rate;

          *dest_value = gst_util_uint64_scale_int (src_value, GST_SECOND,
              byterate);
          break;
        }
        default:
          res = FALSE;
      }
      break;
    }
    case GST_FORMAT_DEFAULT:
      switch (*dest_format) {
        case GST_FORMAT_BYTES:
          *dest_value = src_value * bytes_per_sample;
          break;
        case GST_FORMAT_TIME:
          *dest_value = gst_util_uint64_scale_int (src_value, GST_SECOND,
              flacdec->sample_rate);
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
          *dest_value = gst_util_uint64_scale_int (src_value,
              scale * flacdec->sample_rate, GST_SECOND);
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
gst_flac_dec_get_src_query_types (GstPad * pad)
{
  static const GstQueryType types[] = {
    GST_QUERY_POSITION,
    GST_QUERY_DURATION,
    0,
  };

  return types;
}

static gboolean
gst_flac_dec_src_query (GstPad * pad, GstQuery * query)
{
  GstFlacDec *flacdec;
  gboolean res = TRUE;
  GstPad *peer;

  flacdec = GST_FLAC_DEC (gst_pad_get_parent (pad));
  peer = gst_pad_get_peer (flacdec->sinkpad);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_POSITION:{
      GstFormat fmt;
      gint64 pos;

      gst_query_parse_position (query, &fmt, NULL);

      if (fmt != GST_FORMAT_DEFAULT) {
        if (!gst_flac_dec_convert_src (flacdec->srcpad, GST_FORMAT_DEFAULT,
                flacdec->segment.last_stop, &fmt, &pos)) {
          GST_DEBUG_OBJECT (flacdec, "failed to convert position into %s "
              "format", gst_format_get_name (fmt));
          res = FALSE;
          goto done;
        }
      } else {
        pos = flacdec->segment.last_stop;
      }

      gst_query_set_position (query, fmt, pos);

      GST_DEBUG_OBJECT (flacdec, "returning position %" G_GUINT64_FORMAT
          " (format: %s)", pos, gst_format_get_name (fmt));

      res = TRUE;
      break;
    }

    case GST_QUERY_DURATION:{
      GstFormat fmt;
      gint64 len;

      gst_query_parse_duration (query, &fmt, NULL);

      /* try any demuxers before us first */
      if (fmt == GST_FORMAT_TIME && peer && gst_pad_query (peer, query)) {
        gst_query_parse_duration (query, NULL, &len);
        GST_DEBUG_OBJECT (flacdec, "peer returned duration %" GST_TIME_FORMAT,
            len);
        res = TRUE;
        goto done;
      }

      if (flacdec->segment.duration == 0 || flacdec->segment.duration == -1) {
        GST_DEBUG_OBJECT (flacdec, "duration not known yet");
        res = FALSE;
        goto done;
      }

      /* convert total number of samples to request format */
      if (fmt != GST_FORMAT_DEFAULT) {
        if (!gst_flac_dec_convert_src (flacdec->srcpad, GST_FORMAT_DEFAULT,
                flacdec->segment.duration, &fmt, &len)) {
          GST_DEBUG_OBJECT (flacdec, "failed to convert duration into %s "
              "format", gst_format_get_name (fmt));
          res = FALSE;
          goto done;
        }
      } else {
        len = flacdec->segment.duration;
      }

      gst_query_set_duration (query, fmt, len);

      GST_DEBUG_OBJECT (flacdec, "returning duration %" G_GUINT64_FORMAT
          " (format: %s)", len, gst_format_get_name (fmt));

      res = TRUE;
      break;
    }

    case GST_QUERY_CONVERT:{
      GstFormat src_fmt, dest_fmt;
      gint64 src_val, dest_val;

      gst_query_parse_convert (query, &src_fmt, &src_val, &dest_fmt, &dest_val);

      res = gst_flac_dec_convert_src (pad, src_fmt, src_val, &dest_fmt,
          &dest_val);

      if (res) {
        gst_query_set_convert (query, src_fmt, src_val, dest_fmt, dest_val);
      }

      break;
    }

    default:{
      res = gst_pad_query_default (pad, query);
      break;
    }
  }

done:

  if (peer)
    gst_object_unref (peer);

  gst_object_unref (flacdec);

  return res;
}

static void
gst_flac_dec_send_newsegment (GstFlacDec * flacdec, gboolean update)
{
  GstSegment *s = &flacdec->segment;
  GstFormat target_format = GST_FORMAT_TIME;
  gint64 stop_time = GST_CLOCK_TIME_NONE;
  gint64 start_time = 0;

  /* segment is in DEFAULT format, but we want to send a TIME newsegment */
  if (!gst_flac_dec_convert_src (flacdec->srcpad, GST_FORMAT_DEFAULT,
          s->start, &target_format, &start_time)) {
    GST_WARNING_OBJECT (flacdec, "failed to convert segment start %lld to TIME",
        s->start);
    return;
  }

  if (s->stop != -1 && !gst_flac_dec_convert_src (flacdec->srcpad,
          GST_FORMAT_DEFAULT, s->stop, &target_format, &stop_time)) {
    GST_WARNING_OBJECT (flacdec, "failed to convert segment stop to TIME");
    return;
  }

  GST_DEBUG_OBJECT (flacdec, "sending newsegment from %" GST_TIME_FORMAT
      " to %" GST_TIME_FORMAT, GST_TIME_ARGS (start_time),
      GST_TIME_ARGS (stop_time));

  gst_pad_push_event (flacdec->srcpad,
      gst_event_new_new_segment (update, s->rate, GST_FORMAT_TIME,
          start_time, stop_time, start_time));
}

static gboolean
gst_flac_dec_handle_seek_event (GstFlacDec * flacdec, GstEvent * event)
{
  FLAC__bool seek_ok;
  GstSeekFlags seek_flags;
  GstSeekType start_type;
  GstSeekType stop_type;
  GstSegment segment;
  GstFormat seek_format;
  gboolean only_update = FALSE;
  gboolean flush;
  gdouble rate;
  gint64 start;
  gint64 stop;

  gst_event_parse_seek (event, &rate, &seek_format, &seek_flags, &start_type,
      &start, &stop_type, &stop);

  if (seek_format != GST_FORMAT_DEFAULT && seek_format != GST_FORMAT_TIME) {
    GST_DEBUG ("seeking is only supported in TIME or DEFAULT format");
    return FALSE;
  }

  if (rate < 0.0) {
    GST_DEBUG ("only forward playback supported, rate %f not allowed", rate);
    return FALSE;
  }

  if (seek_format != GST_FORMAT_DEFAULT) {
    GstFormat target_format = GST_FORMAT_DEFAULT;

    if (start_type != GST_SEEK_TYPE_NONE &&
        !gst_flac_dec_convert_src (flacdec->srcpad, seek_format, start,
            &target_format, &start)) {
      GST_DEBUG ("failed to convert start to DEFAULT format");
      return FALSE;
    }

    if (stop_type != GST_SEEK_TYPE_NONE &&
        !gst_flac_dec_convert_src (flacdec->srcpad, seek_format, stop,
            &target_format, &stop)) {
      GST_DEBUG ("failed to convert stop to DEFAULT format");
      return FALSE;
    }
  }

  flush = ((seek_flags & GST_SEEK_FLAG_FLUSH) == GST_SEEK_FLAG_FLUSH);

  if (flush) {
    gst_pad_push_event (flacdec->srcpad, gst_event_new_flush_start ());
  } else {
    gst_pad_stop_task (flacdec->sinkpad);
  }

  GST_PAD_STREAM_LOCK (flacdec->sinkpad);

  /* operate on segment copy until we know the seek worked */
  segment = flacdec->segment;

  gst_segment_set_seek (&segment, rate, GST_FORMAT_DEFAULT,
      seek_flags, start_type, start, stop_type, stop, &only_update);

  GST_DEBUG ("configured segment: [%" G_GINT64_FORMAT "-%" G_GINT64_FORMAT
      "] = [%" GST_TIME_FORMAT "-%" GST_TIME_FORMAT "]",
      segment.start, segment.stop,
      GST_TIME_ARGS (segment.start * GST_SECOND / flacdec->sample_rate),
      GST_TIME_ARGS (segment.stop * GST_SECOND / flacdec->sample_rate));

  GST_DEBUG_OBJECT (flacdec, "performing seek to sample %" G_GINT64_FORMAT,
      segment.start);

  flacdec->seeking = TRUE;

  seek_ok = FLAC__seekable_stream_decoder_seek_absolute (flacdec->decoder,
      segment.start);

  flacdec->seeking = FALSE;

  /* FIXME: support segment seeks */
  if (flush) {
    gst_pad_push_event (flacdec->srcpad, gst_event_new_flush_stop ());
  }

  if (seek_ok) {
    flacdec->segment = segment;
    gst_flac_dec_send_newsegment (flacdec, FALSE);
    flacdec->segment.last_stop = segment.start;

    GST_DEBUG_OBJECT (flacdec, "seek successful");
  } else {
    GST_WARNING_OBJECT (flacdec, "seek failed");
  }

  gst_pad_start_task (flacdec->sinkpad,
      (GstTaskFunction) gst_flac_dec_loop, flacdec->sinkpad);

  GST_PAD_STREAM_UNLOCK (flacdec->sinkpad);

  return TRUE;
}

static gboolean
gst_flac_dec_src_event (GstPad * pad, GstEvent * event)
{
  gboolean res = TRUE;
  GstFlacDec *flacdec = GST_FLAC_DEC (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:{
      /* first, see if we're before a demuxer that
       * might handle the seek for us */
      gst_event_ref (event);
      res = gst_pad_event_default (pad, event);
      /* if not, try to handle it ourselves */
      if (!res) {
        res = gst_flac_dec_handle_seek_event (flacdec, event);
      }
      gst_event_unref (event);
      break;
    }
    default:
      res = gst_pad_event_default (pad, event);
      break;
  }

  gst_object_unref (flacdec);

  return res;
}

static gboolean
gst_flac_dec_sink_activate (GstPad * sinkpad)
{
  if (gst_pad_check_pull_range (sinkpad))
    return gst_pad_activate_pull (sinkpad, TRUE);

  return FALSE;
}

static gboolean
gst_flac_dec_sink_activate_pull (GstPad * sinkpad, gboolean active)
{
  if (active) {
    /* if we have a scheduler we can start the task */
    GST_FLAC_DEC (GST_OBJECT_PARENT (sinkpad))->offset = 0;
    return gst_pad_start_task (sinkpad, (GstTaskFunction) gst_flac_dec_loop,
        sinkpad);
  } else {
    return gst_pad_stop_task (sinkpad);
  }
}

static GstStateChangeReturn
gst_flac_dec_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
  GstFlacDec *flacdec = GST_FLAC_DEC (element);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      flacdec->segment.last_stop = 0;
      flacdec->need_newsegment = TRUE;
      flacdec->seeking = FALSE;
      flacdec->channels = 0;
      flacdec->depth = 0;
      flacdec->width = 0;
      flacdec->sample_rate = 0;
      if (flacdec->init == FALSE) {
        FLAC__seekable_stream_decoder_reset (flacdec->decoder);
      }
      gst_segment_init (&flacdec->segment, GST_FORMAT_DEFAULT);
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (ret == GST_STATE_CHANGE_FAILURE)
    return ret;

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_segment_init (&flacdec->segment, GST_FORMAT_UNDEFINED);
      break;
    default:
      break;
  }

  return ret;
}
