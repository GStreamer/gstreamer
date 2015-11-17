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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

/* FIXME: shouldn't all this GstKateDecoderBase stuff really be a base class? */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <string.h>
#ifdef HAVE_TIGER
#include <tiger/tiger.h>
#endif
#include <gst/tag/tag.h>
#include "gstkate.h"
#include "gstkateutil.h"

GST_DEBUG_CATEGORY_EXTERN (gst_kateutil_debug);
#define GST_CAT_DEFAULT gst_kateutil_debug

static void gst_kate_util_decoder_base_free_event_queue (GstKateDecoderBase *
    decoder);

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
    g_value_init (&value, GST_TYPE_BUFFER);
    buffer = gst_buffer_copy (buffer);
    GST_BUFFER_FLAG_SET (buffer, GST_BUFFER_FLAG_HEADER);
    gst_value_take_buffer (&value, buffer);
    gst_value_array_append_value (&array, &value);
    g_value_unset (&value);
    headers = headers->next;
  }

  gst_structure_take_value (structure, "streamheader", &array);

  GST_LOG_OBJECT (element, "here are the newly set caps: %" GST_PTR_FORMAT,
      caps);

  return caps;
}

void
gst_kate_util_install_decoder_base_properties (GObjectClass * gobject_class)
{
  g_object_class_install_property (gobject_class, ARG_DEC_BASE_LANGUAGE,
      g_param_spec_string ("language", "Language", "The language of the stream",
          "", G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, ARG_DEC_BASE_CATEGORY,
      g_param_spec_string ("category", "Category", "The category of the stream",
          "", G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class,
      ARG_DEC_BASE_ORIGINAL_CANVAS_WIDTH,
      g_param_spec_int ("original-canvas-width",
          "Original canvas width (0 is unspecified)",
          "The canvas width this stream was authored for", 0, G_MAXINT, 0,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class,
      ARG_DEC_BASE_ORIGINAL_CANVAS_HEIGHT,
      g_param_spec_int ("original-canvas-height", "Original canvas height",
          "The canvas height this stream was authored for (0 is unspecified)",
          0, G_MAXINT, 0, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
}

void
gst_kate_util_decode_base_init (GstKateDecoderBase * decoder,
    gboolean delay_events)
{
  if (G_UNLIKELY (!decoder))
    return;

  decoder->language = NULL;
  decoder->category = NULL;
  decoder->original_canvas_width = 0;
  decoder->original_canvas_height = 0;
  decoder->tags = NULL;
  decoder->tags_changed = FALSE;
  decoder->initialized = FALSE;
  decoder->delay_events = delay_events;
  decoder->event_queue = NULL;
}

static void
gst_kate_util_decode_base_reset (GstKateDecoderBase * decoder)
{
  g_free (decoder->language);
  decoder->language = NULL;
  g_free (decoder->category);
  decoder->category = NULL;
  if (decoder->tags) {
    gst_tag_list_unref (decoder->tags);
    decoder->tags = NULL;
  }
  decoder->tags_changed = FALSE;
  decoder->original_canvas_width = 0;
  decoder->original_canvas_height = 0;
  if (decoder->event_queue) {
    gst_kate_util_decoder_base_free_event_queue (decoder);
  }
  decoder->initialized = FALSE;
}

gboolean
gst_kate_util_decoder_base_queue_event (GstKateDecoderBase * decoder,
    GstEvent * event, gboolean (*handler) (GstPad *, GstObject *, GstEvent *),
    GstObject * parent, GstPad * pad)
{
  gboolean can_be_queued;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_START:
    case GST_EVENT_FLUSH_STOP:
    case GST_EVENT_EOS:
      can_be_queued = FALSE;
      break;
    case GST_EVENT_SEGMENT:
      gst_kate_util_decoder_base_segment_event (decoder, event);
      can_be_queued = TRUE;
      break;
    default:
      can_be_queued = TRUE;
      break;
  }

  if (GST_EVENT_IS_STICKY (event) && GST_EVENT_TYPE (event) < GST_EVENT_CAPS)
    can_be_queued = FALSE;

  if (decoder->delay_events && can_be_queued) {
    GstKateDecoderBaseQueuedEvent *item;
    GST_DEBUG_OBJECT (decoder, "We have to delay the event");
    item = g_slice_new (GstKateDecoderBaseQueuedEvent);
    if (item) {
      item->event = event;
      item->parent = parent;
      item->pad = pad;
      item->handler = handler;
      g_queue_push_tail (decoder->event_queue, item);
      return TRUE;
    } else {
      return FALSE;
    }
  } else {
    return FALSE;
  }
}

static void
gst_kate_util_decoder_base_free_event_queue (GstKateDecoderBase * decoder)
{
  while (decoder->event_queue->length) {
    GstKateDecoderBaseQueuedEvent *item = (GstKateDecoderBaseQueuedEvent *)
        g_queue_pop_head (decoder->event_queue);
    gst_event_unref (item->event);
    g_slice_free (GstKateDecoderBaseQueuedEvent, item);
  }
  g_queue_free (decoder->event_queue);
  decoder->event_queue = NULL;
}

static void
gst_kate_util_decoder_base_drain_event_queue (GstKateDecoderBase * decoder)
{
  decoder->delay_events = FALSE;

  if (decoder->event_queue->length == 0)
    return;

  GST_DEBUG_OBJECT (decoder, "We can now drain all events!");
  while (decoder->event_queue->length) {
    GstKateDecoderBaseQueuedEvent *item = (GstKateDecoderBaseQueuedEvent *)
        g_queue_pop_head (decoder->event_queue);
    (*item->handler) (item->pad, item->parent, item->event);
    g_slice_free (GstKateDecoderBaseQueuedEvent, item);
  }
}

void
gst_kate_util_decoder_base_add_tags (GstKateDecoderBase * decoder,
    GstTagList * tags, gboolean take_ownership_of_tags)
{
  if (!decoder->tags) {
    if (!take_ownership_of_tags)
      tags = gst_tag_list_ref (tags);
    decoder->tags = tags;
  } else {
    GstTagList *old = decoder->tags;
    decoder->tags = gst_tag_list_merge (old, tags, GST_TAG_MERGE_REPLACE);
    gst_tag_list_unref (old);
    if (take_ownership_of_tags)
      gst_tag_list_unref (tags);
  }
  decoder->tags_changed = TRUE;
}

GstEvent *
gst_kate_util_decoder_base_get_tag_event (GstKateDecoderBase * decoder)
{
  if (!decoder->tags)
    return NULL;
  decoder->tags_changed = FALSE;
  return gst_event_new_tag (gst_tag_list_ref (decoder->tags));
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

static inline gboolean
gst_kate_util_is_utf8_string (const char *value, size_t len)
{
  if (len == 0)
    return FALSE;
  if (memchr (value, 0, len - 1))
    return FALSE;
  if (value[len - 1])
    return FALSE;
  return (kate_text_validate (kate_utf8, value, len) >= 0);
}

GstFlowReturn
gst_kate_util_decoder_base_chain_kate_packet (GstKateDecoderBase * decoder,
    GstElement * element, GstPad * pad, GstBuffer * buf, GstPad * srcpad,
    GstPad * tagpad, GstCaps ** src_caps, const kate_event ** ev)
{
  kate_packet kp;
  int ret;
  GstFlowReturn rflow = GST_FLOW_OK;
  gboolean is_header;
  GstMapInfo info;
  gsize header_size;
  guint8 header[1];

  header_size = gst_buffer_extract (buf, 0, header, 1);

  GST_DEBUG_OBJECT (element,
      "got kate packet, %" G_GSIZE_FORMAT " bytes, type %02x",
      gst_buffer_get_size (buf), header_size == 0 ? -1 : header[0]);

  is_header = header_size > 0 && (header[0] & 0x80);

  if (!is_header && decoder->tags_changed) {
    /* after we've processed headers, send any tags before processing the data packet */
    GST_DEBUG_OBJECT (element, "Not a header, sending tags for pad %s:%s",
        GST_DEBUG_PAD_NAME (tagpad));
    gst_pad_push_event (tagpad,
        gst_kate_util_decoder_base_get_tag_event (decoder));
  }

  if (gst_buffer_map (buf, &info, GST_MAP_READ)) {
    kate_packet_wrap (&kp, info.size, info.data);
    ret = kate_high_decode_packetin (&decoder->k, &kp, ev);
    gst_buffer_unmap (buf, &info);
  } else {
    GST_ELEMENT_ERROR (element, STREAM, DECODE, (NULL),
        ("Failed to map buffer"));
    return GST_FLOW_ERROR;
  }

  if (G_UNLIKELY (ret < 0)) {
    GST_ELEMENT_ERROR (element, STREAM, DECODE, (NULL),
        ("Failed to decode Kate packet: %s",
            gst_kate_util_get_error_message (ret)));
    return GST_FLOW_ERROR;
  }

  if (G_UNLIKELY (ret > 0)) {
    GST_DEBUG_OBJECT (element,
        "kate_high_decode_packetin has received EOS packet");
  }

  /* headers may be interesting to retrieve information from */
  if (G_UNLIKELY (is_header)) {
    switch (header[0]) {
      case 0x80:               /* ID header */
        GST_INFO_OBJECT (element, "Parsed ID header: language %s, category %s",
            decoder->k.ki->language, decoder->k.ki->category);
        if (src_caps) {
          if (*src_caps) {
            gst_caps_unref (*src_caps);
            *src_caps = NULL;
          }
          if (strcmp (decoder->k.ki->category, "K-SPU") == 0 ||
              strcmp (decoder->k.ki->category, "spu-subtitles") == 0) {
            *src_caps = gst_caps_new_empty_simple ("subpicture/x-dvd");
          } else if (decoder->k.ki->text_markup_type == kate_markup_none) {
            *src_caps = gst_caps_new_simple ("text/x-raw", "format",
                G_TYPE_STRING, "utf8", NULL);
          } else {
            *src_caps = gst_caps_new_simple ("text/x-raw", "format",
                G_TYPE_STRING, "pango-markup", NULL);
          }
          GST_INFO_OBJECT (srcpad, "Setting caps: %" GST_PTR_FORMAT, *src_caps);
          if (!gst_pad_set_caps (srcpad, *src_caps)) {
            GST_ERROR_OBJECT (srcpad, "Failed to set caps %" GST_PTR_FORMAT,
                *src_caps);
          }
        }
        if (decoder->k.ki->language && *decoder->k.ki->language) {
          GstTagList *tags = gst_tag_list_new_empty ();
          gchar *lang_code;

          /* en_GB -> en */
          lang_code = g_ascii_strdown (decoder->k.ki->language, -1);
          g_strdelimit (lang_code, NULL, '\0');
          gst_tag_list_add (tags, GST_TAG_MERGE_APPEND, GST_TAG_LANGUAGE_CODE,
              lang_code, NULL);
          g_free (lang_code);
          /* TODO: category - where should it go ? */
          gst_kate_util_decoder_base_add_tags (decoder, tags, TRUE);
        }

        /* update properties */
        g_free (decoder->language);
        decoder->language = g_strdup (decoder->k.ki->language);
        g_free (decoder->category);
        decoder->category = g_strdup (decoder->k.ki->category);
        decoder->original_canvas_width = decoder->k.ki->original_canvas_width;
        decoder->original_canvas_height = decoder->k.ki->original_canvas_height;

        /* we can now send away any event we've delayed, as the src pad now has caps */
        gst_kate_util_decoder_base_drain_event_queue (decoder);

        break;

      case 0x81:               /* Vorbis comments header */
        GST_INFO_OBJECT (element, "Parsed comments header");
        {
          gchar *encoder = NULL;
          GstTagList *list = gst_tag_list_from_vorbiscomment_buffer (buf,
              (const guint8 *) "\201kate\0\0\0\0", 9, &encoder);
          if (!list) {
            GST_ERROR_OBJECT (element, "failed to decode comment header");
            list = gst_tag_list_new_empty ();
          }
          if (encoder) {
            gst_tag_list_add (list, GST_TAG_MERGE_REPLACE,
                GST_TAG_ENCODER, encoder, NULL);
            g_free (encoder);
          }
          gst_tag_list_add (list, GST_TAG_MERGE_REPLACE,
              GST_TAG_SUBTITLE_CODEC, "Kate", NULL);
          gst_tag_list_add (list, GST_TAG_MERGE_REPLACE,
              GST_TAG_ENCODER_VERSION, decoder->k.ki->bitstream_version_major,
              NULL);

          gst_kate_util_decoder_base_add_tags (decoder, list, TRUE);

          if (decoder->initialized) {
            gst_pad_push_event (tagpad,
                gst_event_new_tag (gst_tag_list_ref (decoder->tags)));
          }
        }
        break;

      default:
        break;
    }
  }
#if ((KATE_VERSION_MAJOR<<16)|(KATE_VERSION_MINOR<<8)|KATE_VERSION_PATCH) >= 0x000400
  else if (*ev && (*ev)->meta) {
    int count = kate_meta_query_count ((*ev)->meta);
    if (count > 0) {
      GstTagList *evtags = gst_tag_list_new_empty ();
      int idx;
      GST_DEBUG_OBJECT (decoder, "Kate event has %d attached metadata", count);
      for (idx = 0; idx < count; ++idx) {
        const char *tag, *value;
        size_t len;
        if (kate_meta_query ((*ev)->meta, idx, &tag, &value, &len) < 0) {
          GST_WARNING_OBJECT (decoder, "Failed to retrieve metadata %d", idx);
        } else {
          if (gst_kate_util_is_utf8_string (value, len)) {
            gchar *compound = g_strdup_printf ("%s=%s", tag, value);
            GST_DEBUG_OBJECT (decoder,
                "Metadata %d: %s=%s (%" G_GSIZE_FORMAT " bytes)", idx, tag,
                value, len);
            gst_tag_list_add (evtags, GST_TAG_MERGE_APPEND,
                GST_TAG_EXTENDED_COMMENT, compound, NULL);
            g_free (compound);
          } else {
            GST_INFO_OBJECT (decoder,
                "Metadata %d, (%s, %" G_GSIZE_FORMAT
                " bytes) is binary, ignored", idx, tag, len);
          }
        }
      }
      gst_kate_util_decoder_base_add_tags (decoder, evtags, TRUE);
      gst_pad_push_event (tagpad,
          gst_kate_util_decoder_base_get_tag_event (decoder));
    }
  }
#endif

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
        GST_WARNING_OBJECT (element, "failed to initialize kate state: %s",
            gst_kate_util_get_error_message (ret));
      }
      gst_segment_init (&decoder->kate_segment, GST_FORMAT_UNDEFINED);
      decoder->kate_flushing = FALSE;
      decoder->initialized = TRUE;
      decoder->event_queue = g_queue_new ();
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
      gst_segment_init (&decoder->kate_segment, GST_FORMAT_UNDEFINED);
      decoder->kate_flushing = TRUE;
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

void
gst_kate_util_decoder_base_set_flushing (GstKateDecoderBase * decoder,
    gboolean flushing)
{
  decoder->kate_flushing = flushing;
  gst_segment_init (&decoder->kate_segment, GST_FORMAT_UNDEFINED);
}

void
gst_kate_util_decoder_base_segment_event (GstKateDecoderBase * decoder,
    GstEvent * event)
{
  GstSegment seg;

  gst_event_copy_segment (event, &seg);

  GST_DEBUG_OBJECT (decoder, "kate pad segment: %" GST_SEGMENT_FORMAT, &seg);

  decoder->kate_segment = seg;
}

gboolean
gst_kate_util_decoder_base_update_segment (GstKateDecoderBase * decoder,
    GstElement * element, GstBuffer * buf)
{
  guint64 clip_start = 0, clip_stop = 0;
  gboolean in_seg;

  if (decoder->kate_flushing) {
    GST_LOG_OBJECT (element, "Kate pad flushing, buffer ignored");
    return FALSE;
  }

  if (G_LIKELY (GST_BUFFER_TIMESTAMP_IS_VALID (buf))) {
    GstClockTime stop;

    if (G_LIKELY (GST_BUFFER_DURATION_IS_VALID (buf)))
      stop = GST_BUFFER_TIMESTAMP (buf) + GST_BUFFER_DURATION (buf);
    else
      stop = GST_CLOCK_TIME_NONE;

    in_seg = gst_segment_clip (&decoder->kate_segment, GST_FORMAT_TIME,
        GST_BUFFER_TIMESTAMP (buf), stop, &clip_start, &clip_stop);
  } else {
    in_seg = TRUE;
  }

  if (in_seg) {
    if (GST_BUFFER_TIMESTAMP_IS_VALID (buf)) {
      decoder->kate_segment.position = clip_start;
    }
  } else {
    GST_INFO_OBJECT (element, "Kate buffer not in segment, ignored");
  }

  return in_seg;
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
    GstElement * element, GstPad * pad, GstObject * parent, GstQuery * query)
{
  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CONVERT:
    {
      GstFormat src_fmt, dest_fmt;
      gint64 src_val, dest_val;

      gst_query_parse_convert (query, &src_fmt, &src_val, &dest_fmt, &dest_val);
      if (!gst_kate_decoder_base_convert (decoder, element, pad, src_fmt,
              src_val, &dest_fmt, &dest_val)) {
        return gst_pad_query_default (pad, parent, query);
      }
      gst_query_set_convert (query, src_fmt, src_val, dest_fmt, dest_val);
      return TRUE;
    }
    default:
      return gst_pad_query_default (pad, parent, query);
  }
}

const char *
gst_kate_util_get_error_message (int ret)
{
  switch (ret) {
    case KATE_E_NOT_FOUND:
      return "value not found";
    case KATE_E_INVALID_PARAMETER:
      return "invalid parameter";
    case KATE_E_OUT_OF_MEMORY:
      return "out of memory";
    case KATE_E_BAD_GRANULE:
      return "bad granule";
    case KATE_E_INIT:
      return "initialization error";
    case KATE_E_BAD_PACKET:
      return "bad packet";
    case KATE_E_TEXT:
      return "invalid/truncated text";
    case KATE_E_LIMIT:
      return "a limit was exceeded";
    case KATE_E_VERSION:
      return "unsupported bitstream version";
    case KATE_E_NOT_KATE:
      return "not a kate bitstream";
    case KATE_E_BAD_TAG:
      return "bad tag";
    case KATE_E_IMPL:
      return "not implemented";

#ifdef HAVE_TIGER
    case TIGER_E_NOT_FOUND:
      return "value not found";
    case TIGER_E_INVALID_PARAMETER:
      return "invalid parameter";
    case TIGER_E_OUT_OF_MEMORY:
      return "out of memory";
    case TIGER_E_CAIRO_ERROR:
      return "Cairo error";
    case TIGER_E_BAD_SURFACE_TYPE:
      return "bad surface type";
#endif

    default:
      return "unknown error";
  }
}
