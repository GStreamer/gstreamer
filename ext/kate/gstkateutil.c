/* GStreamer
 * Copyright (C) 2008 Vincent Penquerc'h <ogg.k.ogg.k@googlemail.com>
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

/* FIXME: shouldn't all this GstKateDecoderBase stuff really be a base class? */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <gst/tag/tag.h>
#include "gstkate.h"
#include "gstkateutil.h"

GstCaps *
gst_kate_util_set_header_on_caps (GstElement * element, GstCaps * caps,
    GList * headers)
{
  GstStructure *structure;
  GValue array = { 0 };

  GST_LOG_OBJECT (element, "caps: %" GST_PTR_FORMAT, caps);

  if (G_UNLIKELY (!caps))
    return NULL;
  if (G_UNLIKELY (!headers))
    return NULL;

  caps = gst_caps_make_writable (caps);
  structure = gst_caps_get_structure (caps, 0);

  g_value_init (&array, GST_TYPE_ARRAY);

  while (headers) {
    GValue value = { 0 };
    GstBuffer *buffer = headers->data;
    g_assert (buffer);
    GST_BUFFER_FLAG_SET (buffer, GST_BUFFER_FLAG_IN_CAPS);
    g_value_init (&value, GST_TYPE_BUFFER);
    /* as in theoraenc, we need to copy to avoid circular references */
    buffer = gst_buffer_copy (buffer);
    gst_value_set_buffer (&value, buffer);
    gst_buffer_unref (buffer);
    gst_value_array_append_value (&array, &value);
    g_value_unset (&value);
    headers = headers->next;
  }

  gst_structure_set_value (structure, "streamheader", &array);
  g_value_unset (&array);
  GST_LOG_OBJECT (element, "here are the newly set caps: %" GST_PTR_FORMAT,
      caps);

  return caps;
}

void
gst_kate_util_install_decoder_base_properties (GObjectClass * gobject_class)
{
  g_object_class_install_property (gobject_class, ARG_DEC_BASE_LANGUAGE,
      g_param_spec_string ("language", "Language", "The language of the stream",
          "", G_PARAM_READABLE));

  g_object_class_install_property (gobject_class, ARG_DEC_BASE_CATEGORY,
      g_param_spec_string ("category", "Category", "The category of the stream",
          "", G_PARAM_READABLE));

  g_object_class_install_property (gobject_class,
      ARG_DEC_BASE_ORIGINAL_CANVAS_WIDTH,
      g_param_spec_int ("original-canvas-width",
          "Original canvas width (0 is unspecified)",
          "The canvas width this stream was authored for", 0, G_MAXINT, 0,
          G_PARAM_READABLE));

  g_object_class_install_property (gobject_class,
      ARG_DEC_BASE_ORIGINAL_CANVAS_HEIGHT,
      g_param_spec_int ("original-canvas-height", "Original canvas height",
          "The canvas height this stream was authored for (0 is unspecified)",
          0, G_MAXINT, 0, G_PARAM_READABLE));
}

void
gst_kate_util_decode_base_init (GstKateDecoderBase * decoder)
{
  if (G_UNLIKELY (!decoder))
    return;

  decoder->language = NULL;
  decoder->category = NULL;
  decoder->original_canvas_width = 0;
  decoder->original_canvas_height = 0;
  decoder->tags = NULL;
  decoder->initialized = FALSE;
}

static void
gst_kate_util_decode_base_reset (GstKateDecoderBase * decoder)
{
  g_free (decoder->language);
  decoder->language = NULL;
  g_free (decoder->category);
  decoder->category = NULL;
  if (decoder->tags) {
    gst_tag_list_free (decoder->tags);
    decoder->tags = NULL;
  }
  decoder->original_canvas_width = 0;
  decoder->original_canvas_height = 0;
  decoder->initialized = FALSE;
}

gboolean
gst_kate_util_decoder_base_get_property (GstKateDecoderBase * decoder,
    GObject * object, guint prop_id, GValue * value, GParamSpec * pspec)
{
  gboolean res = TRUE;
  switch (prop_id) {
    case ARG_DEC_BASE_LANGUAGE:
      g_value_set_string (value, decoder->language);
      break;
    case ARG_DEC_BASE_CATEGORY:
      g_value_set_string (value, decoder->category);
      break;
    case ARG_DEC_BASE_ORIGINAL_CANVAS_WIDTH:
      g_value_set_int (value, decoder->original_canvas_width);
      break;
    case ARG_DEC_BASE_ORIGINAL_CANVAS_HEIGHT:
      g_value_set_int (value, decoder->original_canvas_height);
      break;
    default:
      res = FALSE;
      break;
  }
  return res;
}

GstFlowReturn
gst_kate_util_decoder_base_chain_kate_packet (GstKateDecoderBase * decoder,
    GstElement * element, GstPad * pad, GstBuffer * buf, GstPad * srcpad,
    const kate_event ** ev)
{
  kate_packet kp;
  int ret;
  GstFlowReturn rflow = GST_FLOW_OK;

  GST_DEBUG_OBJECT (element, "got kate packet, %u bytes, type %02x",
      GST_BUFFER_SIZE (buf),
      GST_BUFFER_SIZE (buf) == 0 ? -1 : GST_BUFFER_DATA (buf)[0]);
  kate_packet_wrap (&kp, GST_BUFFER_SIZE (buf), GST_BUFFER_DATA (buf));
  ret = kate_high_decode_packetin (&decoder->k, &kp, ev);
  if (G_UNLIKELY (ret < 0)) {
    GST_ELEMENT_ERROR (element, STREAM, DECODE, (NULL),
        ("Failed to decode Kate packet: %d", ret));
    return GST_FLOW_ERROR;
  } else if (G_UNLIKELY (ret > 0)) {
    GST_DEBUG_OBJECT (element,
        "kate_high_decode_packetin has received EOS packet");
    return GST_FLOW_OK;
  }

  /* headers may be interesting to retrieve information from */
  if (G_LIKELY (GST_BUFFER_SIZE (buf) > 0))
    switch (GST_BUFFER_DATA (buf)[0]) {
        GstCaps *caps;

      case 0x80:               /* ID header */
        GST_INFO_OBJECT (element, "Parsed ID header: language %s, category %s",
            decoder->k.ki->language, decoder->k.ki->category);
        caps = gst_caps_new_simple ("text/x-pango-markup", NULL);
        gst_pad_set_caps (srcpad, caps);
        gst_caps_unref (caps);
        if (decoder->k.ki->language && *decoder->k.ki->language) {
          GstTagList *old = decoder->tags, *tags = gst_tag_list_new ();
          if (tags) {
            gchar *lang_code;

            /* en_GB -> en */
            lang_code = g_ascii_strdown (decoder->k.ki->language, -1);
            g_strdelimit (lang_code, NULL, '\0');
            gst_tag_list_add (tags, GST_TAG_MERGE_APPEND, GST_TAG_LANGUAGE_CODE,
                lang_code, NULL);
            g_free (lang_code);
            /* TODO: category - where should it go ? */
            decoder->tags =
                gst_tag_list_merge (decoder->tags, tags, GST_TAG_MERGE_REPLACE);
            gst_tag_list_free (tags);
            if (old)
              gst_tag_list_free (old);
          }
        }

        /* update properties */
        if (decoder->language)
          g_free (decoder->language);
        decoder->language = g_strdup (decoder->k.ki->language);
        if (decoder->category)
          g_free (decoder->category);
        decoder->category = g_strdup (decoder->k.ki->category);
        decoder->original_canvas_width = decoder->k.ki->original_canvas_width;
        decoder->original_canvas_height = decoder->k.ki->original_canvas_height;

        break;

      case 0x81:               /* Vorbis comments header */
        GST_INFO_OBJECT (element, "Parsed comments header");
        {
          gchar *encoder = NULL;
          GstTagList *old = decoder->tags, *list =
              gst_tag_list_from_vorbiscomment_buffer (buf,
              (const guint8 *) "\201kate\0\0\0\0", 9, &encoder);
          if (list) {
            decoder->tags =
                gst_tag_list_merge (decoder->tags, list, GST_TAG_MERGE_REPLACE);
            gst_tag_list_free (list);
          }

          if (!decoder->tags) {
            GST_ERROR_OBJECT (element, "failed to decode comment header");
            decoder->tags = gst_tag_list_new ();
          }
          if (encoder) {
            gst_tag_list_add (decoder->tags, GST_TAG_MERGE_REPLACE,
                GST_TAG_ENCODER, encoder, NULL);
            g_free (encoder);
          }
          gst_tag_list_add (decoder->tags, GST_TAG_MERGE_REPLACE,
              GST_TAG_SUBTITLE_CODEC, "Kate", NULL);
          gst_tag_list_add (decoder->tags, GST_TAG_MERGE_REPLACE,
              GST_TAG_ENCODER_VERSION, decoder->k.ki->bitstream_version_major,
              NULL);

          if (old)
            gst_tag_list_free (old);

          if (decoder->initialized) {
            gst_element_found_tags_for_pad (element, srcpad, decoder->tags);
            decoder->tags = NULL;
          } else {
            /* Only push them as messages for the time being. *
             * They will be pushed on the pad once the decoder is initialized */
            gst_element_post_message (element,
                gst_message_new_tag (GST_OBJECT (element),
                    gst_tag_list_copy (decoder->tags)));
          }
        }
        break;

      default:
        break;
    }

  return rflow;
}

GstStateChangeReturn
gst_kate_decoder_base_change_state (GstKateDecoderBase * decoder,
    GstElement * element, GstElementClass * parent_class,
    GstStateChange transition)
{
  GstStateChangeReturn res;
  int ret;

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      GST_DEBUG_OBJECT (element, "READY -> PAUSED, initializing kate state");
      ret = kate_high_decode_init (&decoder->k);
      if (ret < 0) {
        GST_WARNING_OBJECT (element, "failed to initialize kate state: %d",
            ret);
      }
      decoder->initialized = TRUE;
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      break;
    default:
      break;
  }

  res = parent_class->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      GST_DEBUG_OBJECT (element, "PAUSED -> READY, clearing kate state");
      if (decoder->initialized) {
        kate_high_decode_clear (&decoder->k);
        decoder->initialized = FALSE;
      }
      gst_kate_util_decode_base_reset (decoder);
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      gst_kate_util_decode_base_reset (decoder);
      break;
    default:
      break;
  }

  return res;
}

static GstClockTime
gst_kate_util_granule_time (kate_state * k, gint64 granulepos)
{
  if (G_UNLIKELY (granulepos == -1))
    return -1;

  return kate_granule_time (k->ki, granulepos) * GST_SECOND;
}

/*
conversions on the sink:
  - default is granules at num/den rate (subject to the granule shift)
  - default -> time is possible
  - bytes do not mean anything, packets can be any number of bytes, and we
    have no way to know the number of bytes emitted without decoding
conversions on the source:
  - nothing
*/

gboolean
gst_kate_decoder_base_convert (GstKateDecoderBase * decoder,
    GstElement * element, GstPad * pad, GstFormat src_fmt, gint64 src_val,
    GstFormat * dest_fmt, gint64 * dest_val)
{
  gboolean res = FALSE;

  if (src_fmt == *dest_fmt) {
    *dest_val = src_val;
    return TRUE;
  }

  if (!decoder->initialized) {
    GST_WARNING_OBJECT (element, "not initialized yet");
    return FALSE;
  }

  if (src_fmt == GST_FORMAT_BYTES || *dest_fmt == GST_FORMAT_BYTES) {
    GST_WARNING_OBJECT (element, "unsupported format");
    return FALSE;
  }

  switch (src_fmt) {
    case GST_FORMAT_DEFAULT:
      switch (*dest_fmt) {
        case GST_FORMAT_TIME:
          *dest_val = gst_kate_util_granule_time (&decoder->k, src_val);
          res = TRUE;
          break;
        default:
          res = FALSE;
          break;
      }
      break;
    default:
      res = FALSE;
      break;
  }

  if (!res) {
    GST_WARNING_OBJECT (element, "unsupported format");
  }

  return res;
}

gboolean
gst_kate_decoder_base_sink_query (GstKateDecoderBase * decoder,
    GstElement * element, GstPad * pad, GstQuery * query)
{
  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CONVERT:
    {
      GstFormat src_fmt, dest_fmt;
      gint64 src_val, dest_val;

      gst_query_parse_convert (query, &src_fmt, &src_val, &dest_fmt, &dest_val);
      if (!gst_kate_decoder_base_convert (decoder, element, pad, src_fmt,
              src_val, &dest_fmt, &dest_val)) {
        return gst_pad_query_default (pad, query);
      }
      gst_query_set_convert (query, src_fmt, src_val, dest_fmt, dest_val);
      return TRUE;
    }
    default:
      return gst_pad_query_default (pad, query);
  }
}
