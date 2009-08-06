/* GStreamer
 * Copyright (C) 2009 Igalia S.L.
 * Author: Iago Toral <itoral@igalia.com>
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

#include "gstbaseaudiodecoder.h"

#include <string.h>

GST_DEBUG_CATEGORY_EXTERN (baseaudio_debug);
#define GST_CAT_DEFAULT baseaudio_debug

static void gst_base_audio_decoder_finalize (GObject * object);

static gboolean gst_base_audio_decoder_src_event (GstPad * pad,
    GstEvent * event);
static GstFlowReturn gst_base_audio_decoder_chain (GstPad * pad,
    GstBuffer * buf);
static gboolean gst_base_audio_decoder_sink_query (GstPad * pad,
    GstQuery * query);
static gboolean gst_base_audio_decoder_sink_convert (GstPad * pad,
    GstFormat src_format, gint64 src_value, GstFormat * dest_format,
    gint64 * dest_value);
static const GstQueryType *gst_base_audio_decoder_get_query_types (GstPad *
    pad);
static gboolean gst_base_audio_decoder_src_query (GstPad * pad,
    GstQuery * query);
static gboolean gst_base_audio_decoder_src_convert (GstPad * pad,
    GstFormat src_format, gint64 src_value, GstFormat * dest_format,
    gint64 * dest_value);
static void gst_base_audio_decoder_reset (GstBaseAudioDecoder *
    base_audio_decoder);

GST_BOILERPLATE (GstBaseAudioDecoder, gst_base_audio_decoder,
    GstBaseAudioCodec, GST_TYPE_BASE_AUDIO_CODEC);

static void
gst_base_audio_decoder_base_init (gpointer g_class)
{

}

static void
gst_base_audio_decoder_class_init (GstBaseAudioDecoderClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->finalize = gst_base_audio_decoder_finalize;

  parent_class = g_type_class_peek_parent (klass);
}

static void
gst_base_audio_decoder_init (GstBaseAudioDecoder * base_audio_decoder,
    GstBaseAudioDecoderClass * klass)
{
  GstPad *pad;

  GST_DEBUG ("gst_base_audio_decoder_init");

  pad = GST_BASE_AUDIO_CODEC_SINK_PAD (base_audio_decoder);

  gst_pad_set_chain_function (pad, gst_base_audio_decoder_chain);
  gst_pad_set_query_function (pad, gst_base_audio_decoder_sink_query);

  pad = GST_BASE_AUDIO_CODEC_SRC_PAD (base_audio_decoder);

  gst_pad_set_event_function (pad, gst_base_audio_decoder_src_event);
  gst_pad_set_query_type_function (pad, gst_base_audio_decoder_get_query_types);
  gst_pad_set_query_function (pad, gst_base_audio_decoder_src_query);
}

static void
gst_base_audio_decoder_finalize (GObject * object)
{
  GstBaseAudioDecoder *base_audio_decoder;

  g_return_if_fail (GST_IS_BASE_AUDIO_DECODER (object));

  GST_DEBUG_OBJECT (object, "finalize");

  base_audio_decoder = GST_BASE_AUDIO_DECODER (object);

  gst_base_audio_decoder_reset (base_audio_decoder);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

/* FIXME: implement */
static gboolean
gst_base_audio_decoder_src_convert (GstPad * pad, GstFormat src_format,
    gint64 src_value, GstFormat * dest_format, gint64 * dest_value)
{
  return TRUE;
}

#ifndef GST_DISABLE_INDEX
static gboolean
gst_base_audio_decoder_index_seek (GstBaseAudioDecoder *base_audio_decoder,
    GstIndex *index, GstPad * pad, GstEvent * event)
{
  GstIndexEntry *entry;
  gdouble rate;
  GstFormat format;
  GstSeekFlags flags;
  GstSeekType cur_type, stop_type;
  gint64 cur, stop;
  gint index_id;
  GstBaseAudioCodec *base_audio_codec;

  base_audio_codec = GST_BASE_AUDIO_CODEC (base_audio_decoder);

  gst_event_parse_seek (event, &rate, &format, &flags, &cur_type, &cur, 
      &stop_type, &stop);

  gst_index_get_writer_id (index, GST_OBJECT (base_audio_decoder), &index_id);
  entry = gst_index_get_assoc_entry (index, index_id,
      GST_INDEX_LOOKUP_BEFORE, GST_ASSOCIATION_FLAG_KEY_UNIT, format, cur);

  if (entry && gst_pad_is_linked (base_audio_codec->sinkpad)) {
    const GstFormat *peer_formats, *try_formats;

    /* since we know the exact byteoffset of the frame, 
       make sure to seek on bytes first */
    const GstFormat try_all_formats[] = {
      GST_FORMAT_BYTES,
      GST_FORMAT_TIME,
      0
    };

    try_formats = try_all_formats;

#if 0 
    /* FIXE ME */
    peer_formats = 
        gst_pad_get_formats (GST_PAD_PEER (base_audio_codec->sinkpad));
#else
    peer_formats = try_all_formats;
#endif

    while (gst_formats_contains (peer_formats, *try_formats)) {
      gint64 value;

      if (gst_index_entry_assoc_map (entry, *try_formats, &value)) {
        GstEvent *seek_event;

        GST_DEBUG_OBJECT (base_audio_decoder,
	    "index %s %" G_GINT64_FORMAT
	    " -> %s %" G_GINT64_FORMAT,
	    gst_format_get_details (format)->nick,
	    cur, 
	    gst_format_get_details (*try_formats)->nick, 
	    value);
	
        seek_event = gst_event_new_seek (rate, *try_formats, flags, 
	    cur_type, value, stop_type, stop);
	
        if (gst_pad_push_event (base_audio_codec->sinkpad, seek_event)) {
          return TRUE;
        }
      }

      try_formats++;
    }
  }

  return FALSE;
}
#endif

static gboolean
gst_base_audio_decoder_normal_seek (GstBaseAudioDecoder *base_audio_decoder,
    GstPad *pad, GstEvent *event)
{
  gdouble rate;
  GstFormat format, conv;
  GstSeekFlags flags;
  GstSeekType cur_type, stop_type;
  gint64 cur, stop;
  gint64 time_cur, bytes_cur;
  gint64 time_stop, bytes_stop;
  gboolean res;
  GstEvent *peer_event;
  GstBaseAudioDecoderClass *base_audio_decoder_class;
  GstBaseAudioCodec *base_audio_codec;

  base_audio_codec = GST_BASE_AUDIO_CODEC (base_audio_decoder);
  base_audio_decoder_class =
      GST_BASE_AUDIO_DECODER_GET_CLASS (base_audio_decoder);

  gst_event_parse_seek (event, &rate, &format, &flags, &cur_type, &cur,
      &stop_type, &stop); 

  res = FALSE;

  /* Try to seek in time */
  conv = GST_FORMAT_TIME;
  if (!gst_base_audio_decoder_src_convert (pad, format, cur, &conv, &time_cur))
    goto convert_failed;
  if (!gst_base_audio_decoder_src_convert (pad, format, stop, &conv, &time_stop))
    goto convert_failed;
  
  GST_DEBUG ("seek to time %" GST_TIME_FORMAT "-%" GST_TIME_FORMAT,
	     GST_TIME_ARGS (time_cur), GST_TIME_ARGS (time_stop));
  
  peer_event = gst_event_new_seek (rate, GST_FORMAT_TIME, flags,
				   cur_type, time_cur, stop_type, time_stop);
  
  res = gst_pad_push_event (base_audio_codec->sinkpad, peer_event);

  /* Try seek in bytes if seek in time failed */
  if (!res) {
    conv = GST_FORMAT_BYTES;
    if (!gst_base_audio_decoder_sink_convert (pad, GST_FORMAT_TIME, time_cur,
        &conv, &bytes_cur))
      goto convert_failed;
    if (!gst_base_audio_decoder_sink_convert (pad, GST_FORMAT_TIME, time_stop,
        &conv, &bytes_stop))
      goto convert_failed;
    
    peer_event =
        gst_event_new_seek (rate, GST_FORMAT_BYTES, flags, cur_type, bytes_cur,
            stop_type, bytes_stop);
    
    res = gst_pad_push_event (base_audio_codec->sinkpad, peer_event);
  }

  return res;

  /* ERRORS */
 convert_failed:
  {
    GST_DEBUG_OBJECT (base_audio_decoder, "failed to convert format %u", format);
    return FALSE;
  }
}

static gboolean
gst_base_audio_decoder_seek (GstBaseAudioDecoder *base_audio_decoder,
    GstPad *pad, GstEvent *event)
{
  gboolean res;

#ifndef GST_DISABLE_INDEX
  GstIndex *index = gst_element_get_index (GST_ELEMENT (base_audio_decoder));
  if (index) {
    res = gst_base_audio_decoder_index_seek (base_audio_decoder, 
        index, pad, event);
    gst_object_unref (index);
  } else
#endif
    res = gst_base_audio_decoder_normal_seek (base_audio_decoder, pad, event);

  return res;
}

static gboolean
gst_base_audio_decoder_src_event (GstPad * pad, GstEvent * event)
{
  gboolean res;
  GstBaseAudioDecoder *base_audio_decoder;
  GstBaseAudioCodec *base_audio_codec;
  GstBaseAudioDecoderClass *base_audio_decoder_class;
  
  base_audio_decoder = GST_BASE_AUDIO_DECODER (GST_PAD_PARENT (pad));
  base_audio_codec = GST_BASE_AUDIO_CODEC (base_audio_decoder);
  base_audio_decoder_class =
      GST_BASE_AUDIO_DECODER_GET_CLASS (base_audio_decoder);

  switch (GST_EVENT_TYPE (event)) {
  case GST_EVENT_SEEK:{
    gst_event_ref (event);
    res = gst_pad_push_event (base_audio_codec->sinkpad, event);
    if (!res) {
      res = gst_base_audio_decoder_seek (base_audio_decoder, pad, event);
    }
    gst_event_unref (event);
    break;
  }
  default:
    res = gst_pad_push_event (base_audio_codec->sinkpad, event);
    break;
  }
  return res;
}

static const GstQueryType *
gst_base_audio_decoder_get_query_types (GstPad * pad)
{
  static const GstQueryType query_types[] = {
    GST_QUERY_CONVERT,
    0
  };

  return query_types;
}

static gboolean
gst_base_audio_decoder_src_query (GstPad * pad, GstQuery * query)
{
  GstBaseAudioDecoder *enc;
  gboolean res;

  enc = GST_BASE_AUDIO_DECODER (gst_pad_get_parent (pad));

  switch GST_QUERY_TYPE (query) {
    case GST_QUERY_CONVERT:
    {
      GstFormat src_fmt, dest_fmt;
      gint64 src_val, dest_val;

      gst_query_parse_convert (query, &src_fmt, &src_val, &dest_fmt, &dest_val);
      res =
          gst_base_audio_decoder_src_convert (pad, src_fmt, src_val, &dest_fmt,
          &dest_val);
      if (!res)
        goto error;
      gst_query_set_convert (query, src_fmt, src_val, dest_fmt, dest_val);
      break;
    }
    default:
      res = gst_pad_query_default (pad, query);
    }
  gst_object_unref (enc);
  return res;

error:
  GST_DEBUG_OBJECT (enc, "query failed");
  gst_object_unref (enc);
  return res;
}

/* FIXME: implement */
static gboolean
gst_base_audio_decoder_sink_convert (GstPad * pad, GstFormat src_format,
    gint64 src_value, GstFormat * dest_format, gint64 * dest_value)
{
  return TRUE;
}

/* FIXME: implement */ 
static gboolean
gst_base_audio_decoder_sink_query (GstPad * pad, GstQuery * query)
{
  return TRUE;
}

static void
gst_base_audio_decoder_reset (GstBaseAudioDecoder * base_audio_decoder)
{
  GstBaseAudioCodecClass *base_audio_codec_class;
  GstBaseAudioCodec *base_audio_codec;

  base_audio_codec = GST_BASE_AUDIO_CODEC (base_audio_decoder);
  base_audio_codec_class = GST_BASE_AUDIO_CODEC_GET_CLASS (base_audio_codec);

  GST_DEBUG ("reset");

  if (base_audio_codec_class->reset) {
    base_audio_codec_class->reset (base_audio_codec);
  }
}

static GstFlowReturn
gst_base_audio_decoder_chain (GstPad * pad, GstBuffer * buf)
{
  GstBaseAudioDecoder *base_audio_decoder;
  GstBaseAudioCodec *base_audio_codec;
  GstBaseAudioDecoderClass *base_audio_decoder_class;
  GstBaseAudioCodecClass *base_audio_codec_class;
  GstFlowReturn ret;

  GST_DEBUG ("chain %lld", GST_BUFFER_TIMESTAMP (buf));

  base_audio_decoder = GST_BASE_AUDIO_DECODER (gst_pad_get_parent (pad));
  base_audio_codec = GST_BASE_AUDIO_CODEC (base_audio_decoder);
  base_audio_decoder_class = GST_BASE_AUDIO_DECODER_GET_CLASS (base_audio_decoder);
  base_audio_codec_class = GST_BASE_AUDIO_CODEC_GET_CLASS (base_audio_decoder);

  GST_DEBUG_OBJECT (base_audio_decoder, "chain");

  if (G_UNLIKELY (GST_BUFFER_FLAG_IS_SET (buf, GST_BUFFER_FLAG_DISCONT))) {
    GST_DEBUG_OBJECT (base_audio_decoder, "received DISCONT buffer");
    if (base_audio_codec->started) {
      gst_base_audio_decoder_reset (base_audio_decoder);
    }
  }

  if (!base_audio_codec->started) {
    base_audio_codec_class->start (base_audio_codec);
    base_audio_codec->started = TRUE;
  }

  base_audio_decoder->offset += GST_BUFFER_SIZE (buf);

  gst_adapter_push (base_audio_codec->input_adapter, buf);

  do {
    ret = base_audio_decoder_class->parse_data (base_audio_decoder);
  } while (ret == GST_FLOW_OK);

  if (ret == GST_BASE_AUDIO_DECODER_FLOW_NEED_DATA) {
    gst_object_unref (base_audio_decoder);
    return GST_FLOW_OK;
  }

  gst_object_unref (base_audio_decoder);
  return ret;
}
