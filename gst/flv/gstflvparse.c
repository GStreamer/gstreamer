/* GStreamer
 * Copyright (C) <2007> Julien Moutte <julien@moutte.net>
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

#include "gstflvparse.h"

#include <string.h>

GST_DEBUG_CATEGORY_EXTERN (flvdemux_debug);
#define GST_CAT_DEFAULT flvdemux_debug

static guint32
FLV_GET_BEUI24 (const guint8 * data, size_t data_size)
{
  guint32 ret = 0;

  g_return_val_if_fail (data != NULL, 0);
  g_return_val_if_fail (data_size >= 3, 0);

  ret = GST_READ_UINT16_BE (data) << 8;
  ret |= GST_READ_UINT8 (data + 2);

  return ret;
}

static gchar *
FLV_GET_STRING (const guint8 * data, size_t data_size)
{
  guint32 string_size = 0;
  gchar *string = NULL;

  g_return_val_if_fail (data != NULL, NULL);
  g_return_val_if_fail (data_size >= 2, NULL);

  string_size = GST_READ_UINT16_BE (data);
  if (G_UNLIKELY (string_size > data_size)) {
    return NULL;
  }

  string = g_try_malloc0 (string_size + 1);
  if (G_UNLIKELY (!string)) {
    return NULL;
  }

  memcpy (string, data + 2, string_size);

  return string;
}

static const GstQueryType *
gst_flv_demux_query_types (GstPad * pad)
{
  static const GstQueryType query_types[] = {
    GST_QUERY_DURATION,
    0
  };

  return query_types;
}

static size_t
gst_flv_parse_metadata_item (GstFLVDemux * demux, const guint8 * data,
    size_t data_size, gboolean * end_marker)
{
  gchar *tag_name = NULL;
  guint8 tag_type = 0;
  size_t offset = 0;

  /* Initialize the end_marker flag to FALSE */
  *end_marker = FALSE;

  /* Name of the tag */
  tag_name = FLV_GET_STRING (data, data_size);
  if (G_UNLIKELY (!tag_name)) {
    GST_WARNING_OBJECT (demux, "failed reading tag name");
    goto beach;
  }

  offset += strlen (tag_name) + 2;

  /* What kind of object is that */
  tag_type = GST_READ_UINT8 (data + offset);

  offset++;

  GST_DEBUG_OBJECT (demux, "tag name %s, tag type %d", tag_name, tag_type);

  switch (tag_type) {
    case 0:                    // Double
    {                           /* Use a union to read the uint64 and then as a double */
      union
      {
        guint64 value_uint64;
        gdouble value_double;
      } value_union;

      value_union.value_uint64 = GST_READ_UINT64_BE (data + offset);

      offset += 8;

      GST_DEBUG_OBJECT (demux, "%s => (double) %f", tag_name,
          value_union.value_double);

      if (!strcmp (tag_name, "duration")) {
        demux->duration = value_union.value_double * GST_SECOND;

        gst_tag_list_add (demux->taglist, GST_TAG_MERGE_REPLACE,
            GST_TAG_DURATION, demux->duration, NULL);
      } else {
        if (tag_name) {
          if (!strcmp (tag_name, "AspectRatioX")) {
            demux->par_x = value_union.value_double;
            demux->got_par = TRUE;
          } else if (!strcmp (tag_name, "AspectRatioY")) {
            demux->par_y = value_union.value_double;
            demux->got_par = TRUE;
          }
          if (!gst_tag_exists (tag_name)) {
            gst_tag_register (tag_name, GST_TAG_FLAG_META, G_TYPE_DOUBLE,
                tag_name, tag_name, gst_tag_merge_use_first);
          }

          if (gst_tag_get_type (tag_name) == G_TYPE_DOUBLE) {
            gst_tag_list_add (demux->taglist, GST_TAG_MERGE_REPLACE,
                tag_name, value_union.value_double, NULL);
          } else {
            GST_WARNING_OBJECT (demux, "tag %s already registered with a "
                "different type", tag_name);
          }
        }
      }

      break;
    }
    case 1:                    // Boolean
    {
      gboolean value = GST_READ_UINT8 (data + offset);

      offset++;

      GST_DEBUG_OBJECT (demux, "%s => (boolean) %d", tag_name, value);

      if (tag_name) {
        if (!gst_tag_exists (tag_name)) {
          gst_tag_register (tag_name, GST_TAG_FLAG_META, G_TYPE_BOOLEAN,
              tag_name, tag_name, gst_tag_merge_use_first);
        }

        if (gst_tag_get_type (tag_name) == G_TYPE_BOOLEAN) {
          gst_tag_list_add (demux->taglist, GST_TAG_MERGE_REPLACE,
              tag_name, value, NULL);
        } else {
          GST_WARNING_OBJECT (demux, "tag %s already registered with a "
              "different type", tag_name);
        }
      }

      break;
    }
    case 2:                    // String
    {
      gchar *value = NULL;

      value = FLV_GET_STRING (data + offset, data_size - offset);

      offset += strlen (value) + 2;

      GST_DEBUG_OBJECT (demux, "%s => (string) %s", tag_name, value);

      if (tag_name) {
        if (!gst_tag_exists (tag_name)) {
          gst_tag_register (tag_name, GST_TAG_FLAG_META, G_TYPE_STRING,
              tag_name, tag_name, gst_tag_merge_strings_with_comma);
        }

        if (gst_tag_get_type (tag_name) == G_TYPE_STRING) {
          gst_tag_list_add (demux->taglist, GST_TAG_MERGE_REPLACE,
              tag_name, value, NULL);
        } else {
          GST_WARNING_OBJECT (demux, "tag %s already registered with a "
              "different type", tag_name);
        }
      }

      g_free (value);

      break;
    }
    case 3:                    // Object
    {
      gboolean end_of_object_marker = FALSE;

      while (!end_of_object_marker && offset < data_size) {
        size_t read = gst_flv_parse_metadata_item (demux, data + offset,
            data_size - offset, &end_of_object_marker);

        if (G_UNLIKELY (!read)) {
          GST_WARNING_OBJECT (demux, "failed reading a tag, skipping");
          break;
        }

        offset += read;
      }

      break;
    }
    case 9:                    // End marker
    {
      GST_DEBUG_OBJECT (demux, "end marker ?");
      if (tag_name[0] == '\0') {

        GST_DEBUG_OBJECT (demux, "end marker detected");

        *end_marker = TRUE;
      }

      break;
    }
    case 10:                   // Array
    {
      guint32 nb_elems = GST_READ_UINT32_BE (data + offset);

      offset += 4;

      GST_DEBUG_OBJECT (demux, "array has %d elements", nb_elems);

      if (!strcmp (tag_name, "times")) {
        if (demux->times) {
          g_array_free (demux->times, TRUE);
        }
        demux->times = g_array_new (FALSE, TRUE, sizeof (gdouble));
      } else if (!strcmp (tag_name, "filepositions")) {
        if (demux->filepositions) {
          g_array_free (demux->filepositions, TRUE);
        }
        demux->filepositions = g_array_new (FALSE, TRUE, sizeof (gdouble));
      }

      while (nb_elems--) {
        guint8 elem_type = GST_READ_UINT8 (data + offset);

        offset++;

        switch (elem_type) {
          case 0:
          {
            union
            {
              guint64 value_uint64;
              gdouble value_double;
            } value_union;

            value_union.value_uint64 = GST_READ_UINT64_BE (data + offset);

            offset += 8;

            GST_DEBUG_OBJECT (demux, "element is a double %f",
                value_union.value_double);

            if (!strcmp (tag_name, "times") && demux->times) {
              g_array_append_val (demux->times, value_union.value_double);
            } else if (!strcmp (tag_name, "filepositions") &&
                demux->filepositions) {
              g_array_append_val (demux->filepositions,
                  value_union.value_double);
            }
            break;
          }
          default:
            GST_WARNING_OBJECT (demux, "unsupported array element type %d",
                elem_type);
        }
      }

      break;
    }
    case 11:                   // Date
    {
      union
      {
        guint64 value_uint64;
        gdouble value_double;
      } value_union;

      value_union.value_uint64 = GST_READ_UINT64_BE (data + offset);

      offset += 8;

      /* There are 2 additional bytes */
      offset += 2;

      GST_DEBUG_OBJECT (demux, "%s => (date as a double) %f", tag_name,
          value_union.value_double);

      break;
    }
    default:
      GST_WARNING_OBJECT (demux, "unsupported tag type %d", tag_type);
  }

  g_free (tag_name);

beach:
  return offset;
}

GstFlowReturn
gst_flv_parse_tag_script (GstFLVDemux * demux, const guint8 * data,
    size_t data_size)
{
  GstFlowReturn ret = GST_FLOW_OK;
  size_t offset = 7;

  GST_LOG_OBJECT (demux, "parsing a script tag");

  if (GST_READ_UINT8 (data + offset++) == 2) {
    guint i;
    gchar *function_name = FLV_GET_STRING (data + offset, data_size - offset);

    GST_LOG_OBJECT (demux, "function name is %s", function_name);

    if (!strcmp (function_name, "onMetaData")) {
      guint32 nb_elems = 0;
      gboolean end_marker = FALSE;

      GST_DEBUG_OBJECT (demux, "we have a metadata script object");

      /* Jump over the onMetaData string and the array indicator */
      offset += 13;

      nb_elems = GST_READ_UINT32_BE (data + offset);

      /* Jump over the number of elements */
      offset += 4;

      GST_DEBUG_OBJECT (demux, "there are %d elements in the array", nb_elems);

      while (nb_elems-- && !end_marker) {
        size_t read = gst_flv_parse_metadata_item (demux, data + offset,
            data_size - offset, &end_marker);

        if (G_UNLIKELY (!read)) {
          GST_WARNING_OBJECT (demux, "failed reading a tag, skipping");
          break;
        }
        offset += read;
      }

      demux->push_tags = TRUE;
    }

    g_free (function_name);

    if (demux->index && demux->times && demux->filepositions) {
      /* If an index was found, insert associations */
      for (i = 0; i < MIN (demux->times->len, demux->filepositions->len); i++) {
        guint64 time, fileposition;

        time = g_array_index (demux->times, gdouble, i) * GST_SECOND;
        fileposition = g_array_index (demux->filepositions, gdouble, i);
        GST_LOG_OBJECT (demux, "adding association %" GST_TIME_FORMAT "-> %"
            G_GUINT64_FORMAT, GST_TIME_ARGS (time), fileposition);
        gst_index_add_association (demux->index, demux->index_id,
            GST_ASSOCIATION_FLAG_KEY_UNIT, GST_FORMAT_TIME, time,
            GST_FORMAT_BYTES, fileposition, NULL);
      }
    }
  }


  return ret;
}

GstFlowReturn
gst_flv_parse_tag_audio (GstFLVDemux * demux, const guint8 * data,
    size_t data_size)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GstBuffer *buffer = NULL;
  guint32 pts = 0, codec_tag = 0, rate = 5512, width = 8, channels = 1;
  guint32 codec_data = 0, pts_ext = 0;
  guint8 flags = 0;

  GST_LOG_OBJECT (demux, "parsing an audio tag");

  GST_LOG_OBJECT (demux, "pts bytes %02X %02X %02X %02X", data[0], data[1],
      data[2], data[3]);

  /* Grab information about audio tag */
  pts = FLV_GET_BEUI24 (data, data_size);
  /* read the pts extension to 32 bits integer */
  pts_ext = GST_READ_UINT8 (data + 3);
  /* Combine them */
  pts |= pts_ext << 24;
  /* Skip the stream id and go directly to the flags */
  flags = GST_READ_UINT8 (data + 7);

  /* Channels */
  if (flags & 0x01) {
    channels = 2;
  }
  /* Width */
  if (flags & 0x02) {
    width = 16;
  }
  /* Sampling rate */
  if ((flags & 0x0C) == 0x0C) {
    rate = 44100;
  } else if ((flags & 0x0C) == 0x08) {
    rate = 22050;
  } else if ((flags & 0x0C) == 0x04) {
    rate = 11025;
  }
  /* Codec tag */
  codec_tag = flags >> 4;
  codec_data = 1;

  GST_LOG_OBJECT (demux, "audio tag with %d channels, %dHz sampling rate, "
      "%d bits width, codec tag %u (flags %02X)", channels, rate, width,
      codec_tag, flags);

  /* If we don't have our audio pad created, then create it. */
  if (G_UNLIKELY (!demux->audio_pad)) {
    GstCaps *caps = NULL;

    demux->audio_pad = gst_pad_new ("audio", GST_PAD_SRC);
    if (G_UNLIKELY (!demux->audio_pad)) {
      GST_WARNING_OBJECT (demux, "failed creating audio pad");
      ret = GST_FLOW_ERROR;
      goto beach;
    }

    /* Make it active */
    gst_pad_set_active (demux->audio_pad, TRUE);

    switch (codec_tag) {
      case 2:
        caps = gst_caps_new_simple ("audio/mpeg",
            "mpegversion", G_TYPE_INT, 1, "layer", G_TYPE_INT, 3, NULL);
        break;
      case 0:
      case 3:
        caps = gst_caps_new_simple ("audio/x-raw-int",
            "endianness", G_TYPE_INT, G_BYTE_ORDER,
            "signed", G_TYPE_BOOLEAN, TRUE,
            "width", G_TYPE_INT, width, "depth", G_TYPE_INT, width, NULL);
        break;
      case 6:
        caps = gst_caps_new_simple ("audio/x-nellymoser", NULL);
        break;
      default:
        GST_WARNING_OBJECT (demux, "unsupported audio codec tag %u", codec_tag);
    }

    if (G_UNLIKELY (!caps)) {
      GST_WARNING_OBJECT (demux, "failed creating caps for audio pad");
      ret = GST_FLOW_ERROR;
      gst_object_unref (demux->audio_pad);
      demux->audio_pad = NULL;
      goto beach;
    }

    gst_caps_set_simple (caps,
        "rate", G_TYPE_INT, rate, "channels", G_TYPE_INT, channels, NULL);

    gst_pad_set_caps (demux->audio_pad, caps);

    GST_DEBUG_OBJECT (demux, "created audio pad with caps %" GST_PTR_FORMAT,
        caps);

    gst_caps_unref (caps);

    /* Store the caps we have set */
    demux->audio_codec_tag = codec_tag;
    demux->rate = rate;
    demux->channels = channels;
    demux->width = width;

    /* Set functions on the pad */
    gst_pad_set_query_type_function (demux->audio_pad,
        GST_DEBUG_FUNCPTR (gst_flv_demux_query_types));
    gst_pad_set_query_function (demux->audio_pad,
        GST_DEBUG_FUNCPTR (gst_flv_demux_query));
    gst_pad_set_event_function (demux->audio_pad,
        GST_DEBUG_FUNCPTR (gst_flv_demux_src_event));

    /* We need to set caps before adding */
    gst_element_add_pad (GST_ELEMENT (demux),
        gst_object_ref (demux->audio_pad));

    /* We only emit no more pads when we have audio and video. Indeed we can
     * not trust the FLV header to tell us if there will be only audio or 
     * only video and we would just break discovery of some files */
    if (demux->audio_pad && demux->video_pad) {
      GST_DEBUG_OBJECT (demux, "emitting no more pads");
      gst_element_no_more_pads (GST_ELEMENT (demux));
    }
  }

  /* Check if caps have changed */
  if (G_UNLIKELY (rate != demux->rate || channels != demux->channels ||
          codec_tag != demux->audio_codec_tag || width != demux->width)) {
    GstCaps *caps = NULL;

    GST_DEBUG_OBJECT (demux, "audio settings have changed, changing caps");

    switch (codec_tag) {
      case 2:
        caps = gst_caps_new_simple ("audio/mpeg",
            "mpegversion", G_TYPE_INT, 1, "layer", G_TYPE_INT, 3, NULL);
        break;
      case 0:
      case 3:
        caps = gst_caps_new_simple ("audio/x-raw-int", NULL);
        break;
      case 6:
        caps = gst_caps_new_simple ("audio/x-nellymoser", NULL);
        break;
      default:
        GST_WARNING_OBJECT (demux, "unsupported audio codec tag %u", codec_tag);
    }

    if (G_UNLIKELY (!caps)) {
      GST_WARNING_OBJECT (demux, "failed creating caps for audio pad");
      ret = GST_FLOW_ERROR;
      goto beach;
    }

    gst_caps_set_simple (caps,
        "rate", G_TYPE_INT, rate,
        "channels", G_TYPE_INT, channels, "width", G_TYPE_INT, width, NULL);

    gst_pad_set_caps (demux->audio_pad, caps);

    gst_caps_unref (caps);

    /* Store the caps we have set */
    demux->audio_codec_tag = codec_tag;
    demux->rate = rate;
    demux->channels = channels;
    demux->width = width;
  }

  /* Push taglist if present */
  if ((demux->has_audio && !demux->audio_pad) ||
      (demux->has_video && !demux->video_pad)) {
    GST_DEBUG_OBJECT (demux, "we are still waiting for a stream to come up "
        "before we can push tags");
  } else {
    if (demux->taglist && demux->push_tags) {
      GST_DEBUG_OBJECT (demux, "pushing tags out");
      gst_element_found_tags (GST_ELEMENT (demux), demux->taglist);
      demux->taglist = gst_tag_list_new ();
      demux->push_tags = FALSE;
    }
  }

  /* Check if we have anything to push */
  if (demux->tag_data_size <= codec_data) {
    GST_LOG_OBJECT (demux, "Nothing left in this tag, returning");
    goto beach;
  }

  /* Create buffer from pad */
  ret = gst_pad_alloc_buffer (demux->audio_pad, GST_BUFFER_OFFSET_NONE,
      demux->tag_data_size - codec_data, GST_PAD_CAPS (demux->audio_pad),
      &buffer);
  if (G_UNLIKELY (ret != GST_FLOW_OK)) {
    GST_WARNING_OBJECT (demux, "failed allocating a %" G_GUINT64_FORMAT
        " bytes buffer: %s", demux->tag_data_size, gst_flow_get_name (ret));
    if (ret == GST_FLOW_NOT_LINKED) {
      demux->audio_linked = FALSE;
    }
    goto beach;
  }

  demux->audio_linked = TRUE;

  /* Fill buffer with data */
  GST_BUFFER_TIMESTAMP (buffer) = pts * GST_MSECOND;
  GST_BUFFER_DURATION (buffer) = GST_CLOCK_TIME_NONE;
  GST_BUFFER_OFFSET (buffer) = demux->audio_offset++;
  GST_BUFFER_OFFSET_END (buffer) = demux->audio_offset;

  if (G_UNLIKELY (demux->audio_need_discont)) {
    GST_BUFFER_FLAG_SET (buffer, GST_BUFFER_FLAG_DISCONT);
    demux->audio_need_discont = FALSE;
  }

  gst_segment_set_last_stop (demux->segment, GST_FORMAT_TIME,
      GST_BUFFER_TIMESTAMP (buffer));

  /* Do we need a newsegment event ? */
  if (G_UNLIKELY (demux->audio_need_segment)) {
    if (!demux->new_seg_event) {
      GST_DEBUG_OBJECT (demux, "pushing newsegment from %"
          GST_TIME_FORMAT " to %" GST_TIME_FORMAT,
          GST_TIME_ARGS (demux->segment->last_stop),
          GST_TIME_ARGS (demux->segment->stop));
      demux->new_seg_event =
          gst_event_new_new_segment (FALSE, demux->segment->rate,
          demux->segment->format, demux->segment->last_stop,
          demux->segment->stop, demux->segment->last_stop);
    } else {
      GST_DEBUG_OBJECT (demux, "pushing pre-generated newsegment event");
    }

    gst_pad_push_event (demux->audio_pad, gst_event_ref (demux->new_seg_event));

    demux->audio_need_segment = FALSE;
  }

  memcpy (GST_BUFFER_DATA (buffer), data + 7 + codec_data,
      demux->tag_data_size - codec_data);

  GST_LOG_OBJECT (demux, "pushing %d bytes buffer at pts %" GST_TIME_FORMAT
      " with duration %" GST_TIME_FORMAT ", offset %" G_GUINT64_FORMAT,
      GST_BUFFER_SIZE (buffer), GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buffer)),
      GST_TIME_ARGS (GST_BUFFER_DURATION (buffer)), GST_BUFFER_OFFSET (buffer));

  /* Push downstream */
  ret = gst_pad_push (demux->audio_pad, buffer);

beach:
  return ret;
}

GstFlowReturn
gst_flv_parse_tag_video (GstFLVDemux * demux, const guint8 * data,
    size_t data_size)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GstBuffer *buffer = NULL;
  guint32 pts = 0, codec_data = 1, pts_ext = 0;
  gboolean keyframe = FALSE;
  guint8 flags = 0, codec_tag = 0;

  GST_LOG_OBJECT (demux, "parsing a video tag");

  GST_LOG_OBJECT (demux, "pts bytes %02X %02X %02X %02X", data[0], data[1],
      data[2], data[3]);

  /* Grab information about video tag */
  pts = FLV_GET_BEUI24 (data, data_size);
  /* read the pts extension to 32 bits integer */
  pts_ext = GST_READ_UINT8 (data + 3);
  /* Combine them */
  pts |= pts_ext << 24;
  /* Skip the stream id and go directly to the flags */
  flags = GST_READ_UINT8 (data + 7);

  /* Keyframe */
  if ((flags >> 4) == 1) {
    keyframe = TRUE;
  }
  /* Codec tag */
  codec_tag = flags & 0x0F;
  if (codec_tag == 4 || codec_tag == 5) {
    codec_data = 2;
  }

  GST_LOG_OBJECT (demux, "video tag with codec tag %u, keyframe (%d) "
      "(flags %02X)", codec_tag, keyframe, flags);

  /* If we don't have our video pad created, then create it. */
  if (G_UNLIKELY (!demux->video_pad)) {
    GstCaps *caps = NULL;

    demux->video_pad = gst_pad_new ("video", GST_PAD_SRC);
    if (G_UNLIKELY (!demux->video_pad)) {
      GST_WARNING_OBJECT (demux, "failed creating video pad");
      ret = GST_FLOW_ERROR;
      goto beach;
    }
    /* Make it active */
    gst_pad_set_active (demux->video_pad, TRUE);

    /* Generate caps for that pad */
    switch (codec_tag) {
      case 2:
        caps = gst_caps_new_simple ("video/x-flash-video", NULL);
        break;
      case 3:
        caps = gst_caps_new_simple ("video/x-flash-video", NULL);
        break;
      case 4:
        caps = gst_caps_new_simple ("video/x-vp6-flash", NULL);
        break;
      case 5:
        caps = gst_caps_new_simple ("video/x-vp6-flash", NULL);
        break;
      default:
        GST_WARNING_OBJECT (demux, "unsupported video codec tag %d", codec_tag);
    }

    if (G_UNLIKELY (!caps)) {
      GST_WARNING_OBJECT (demux, "failed creating caps for video pad");
      gst_object_unref (demux->video_pad);
      demux->video_pad = NULL;
      ret = GST_FLOW_ERROR;
      goto beach;
    }

    gst_caps_set_simple (caps, "pixel-aspect-ratio", GST_TYPE_FRACTION,
        demux->par_x, demux->par_y, NULL);

    /* When we ve set pixel-aspect-ratio we use that boolean to detect a 
     * metadata tag that would come later and trigger a caps change */
    demux->got_par = FALSE;

    gst_pad_set_caps (demux->video_pad, caps);

    GST_DEBUG_OBJECT (demux, "created video pad with caps %" GST_PTR_FORMAT,
        caps);

    gst_caps_unref (caps);

    /* Store the caps we have set */
    demux->video_codec_tag = codec_tag;

    /* Set functions on the pad */
    gst_pad_set_query_type_function (demux->video_pad,
        GST_DEBUG_FUNCPTR (gst_flv_demux_query_types));
    gst_pad_set_query_function (demux->video_pad,
        GST_DEBUG_FUNCPTR (gst_flv_demux_query));
    gst_pad_set_event_function (demux->video_pad,
        GST_DEBUG_FUNCPTR (gst_flv_demux_src_event));

    /* We need to set caps before adding */
    gst_element_add_pad (GST_ELEMENT (demux),
        gst_object_ref (demux->video_pad));

    /* We only emit no more pads when we have audio and video. Indeed we can
     * not trust the FLV header to tell us if there will be only audio or 
     * only video and we would just break discovery of some files */
    if (demux->audio_pad && demux->video_pad) {
      GST_DEBUG_OBJECT (demux, "emitting no more pads");
      gst_element_no_more_pads (GST_ELEMENT (demux));
    }
  }

  /* Check if caps have changed */
  if (G_UNLIKELY (codec_tag != demux->video_codec_tag || demux->got_par)) {
    GstCaps *caps = NULL;

    GST_DEBUG_OBJECT (demux, "video settings have changed, changing caps");

    /* Generate caps for that pad */
    switch (codec_tag) {
      case 2:
        caps = gst_caps_new_simple ("video/x-flash-video", NULL);
        break;
      case 3:
        caps = gst_caps_new_simple ("video/x-flash-video", NULL);
        break;
      case 4:
        caps = gst_caps_new_simple ("video/x-vp6", NULL);
        break;
      case 5:
        caps = gst_caps_new_simple ("video/x-vp6", NULL);
        break;
      default:
        GST_WARNING_OBJECT (demux, "unsupported video codec tag %d", codec_tag);
    }

    if (G_UNLIKELY (!caps)) {
      GST_WARNING_OBJECT (demux, "failed creating caps for video pad");
      ret = GST_FLOW_ERROR;
      goto beach;
    }

    gst_caps_set_simple (caps, "pixel-aspect-ratio", GST_TYPE_FRACTION,
        demux->par_x, demux->par_y, NULL);

    /* When we ve set pixel-aspect-ratio we use that boolean to detect a 
     * metadata tag that would come later and trigger a caps change */
    demux->got_par = FALSE;

    gst_pad_set_caps (demux->video_pad, caps);

    gst_caps_unref (caps);

    /* Store the caps we have set */
    demux->video_codec_tag = codec_tag;
  }

  /* Push taglist if present */
  if ((demux->has_audio && !demux->audio_pad) ||
      (demux->has_video && !demux->video_pad)) {
    GST_DEBUG_OBJECT (demux, "we are still waiting for a stream to come up "
        "before we can push tags");
  } else {
    if (demux->taglist && demux->push_tags) {
      GST_DEBUG_OBJECT (demux, "pushing tags out");
      gst_element_found_tags (GST_ELEMENT (demux), demux->taglist);
      demux->taglist = gst_tag_list_new ();
      demux->push_tags = FALSE;
    }
  }

  /* Check if we have anything to push */
  if (demux->tag_data_size <= codec_data) {
    GST_LOG_OBJECT (demux, "Nothing left in this tag, returning");
    goto beach;
  }

  /* Create buffer from pad */
  ret = gst_pad_alloc_buffer (demux->video_pad, GST_BUFFER_OFFSET_NONE,
      demux->tag_data_size - codec_data, GST_PAD_CAPS (demux->video_pad),
      &buffer);
  if (G_UNLIKELY (ret != GST_FLOW_OK)) {
    GST_WARNING_OBJECT (demux, "failed allocating a %" G_GUINT64_FORMAT
        " bytes buffer: %s", demux->tag_data_size, gst_flow_get_name (ret));
    if (ret == GST_FLOW_NOT_LINKED) {
      demux->video_linked = FALSE;
    }
    goto beach;
  }

  demux->video_linked = TRUE;

  /* Fill buffer with data */
  GST_BUFFER_TIMESTAMP (buffer) = pts * GST_MSECOND;
  GST_BUFFER_DURATION (buffer) = GST_CLOCK_TIME_NONE;
  GST_BUFFER_OFFSET (buffer) = demux->video_offset++;
  GST_BUFFER_OFFSET_END (buffer) = demux->video_offset;

  if (!keyframe) {
    GST_BUFFER_FLAG_SET (buffer, GST_BUFFER_FLAG_DELTA_UNIT);
  } else {
    if (demux->index) {
      GST_LOG_OBJECT (demux, "adding association %" GST_TIME_FORMAT "-> %"
          G_GUINT64_FORMAT, GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buffer)),
          demux->cur_tag_offset);
      gst_index_add_association (demux->index, demux->index_id,
          GST_ASSOCIATION_FLAG_KEY_UNIT,
          GST_FORMAT_TIME, GST_BUFFER_TIMESTAMP (buffer),
          GST_FORMAT_BYTES, demux->cur_tag_offset, NULL);
    }
  }

  if (G_UNLIKELY (demux->video_need_discont)) {
    GST_BUFFER_FLAG_SET (buffer, GST_BUFFER_FLAG_DISCONT);
    demux->video_need_discont = FALSE;
  }

  gst_segment_set_last_stop (demux->segment, GST_FORMAT_TIME,
      GST_BUFFER_TIMESTAMP (buffer));

  /* Do we need a newsegment event ? */
  if (G_UNLIKELY (demux->video_need_segment)) {
    if (!demux->new_seg_event) {
      GST_DEBUG_OBJECT (demux, "pushing newsegment from %"
          GST_TIME_FORMAT " to %" GST_TIME_FORMAT,
          GST_TIME_ARGS (demux->segment->last_stop),
          GST_TIME_ARGS (demux->segment->stop));
      demux->new_seg_event =
          gst_event_new_new_segment (FALSE, demux->segment->rate,
          demux->segment->format, demux->segment->last_stop,
          demux->segment->stop, demux->segment->last_stop);
    } else {
      GST_DEBUG_OBJECT (demux, "pushing pre-generated newsegment event");
    }

    gst_pad_push_event (demux->video_pad, gst_event_ref (demux->new_seg_event));

    demux->video_need_segment = FALSE;
  }

  /* FIXME: safety checks */
  memcpy (GST_BUFFER_DATA (buffer), data + 7 + codec_data,
      demux->tag_data_size - codec_data);

  GST_LOG_OBJECT (demux, "pushing %d bytes buffer at pts %" GST_TIME_FORMAT
      " with duration %" GST_TIME_FORMAT ", offset %" G_GUINT64_FORMAT
      ", keyframe (%d)", GST_BUFFER_SIZE (buffer),
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buffer)),
      GST_TIME_ARGS (GST_BUFFER_DURATION (buffer)), GST_BUFFER_OFFSET (buffer),
      keyframe);

  /* Push downstream */
  ret = gst_pad_push (demux->video_pad, buffer);

beach:
  return ret;
}

GstFlowReturn
gst_flv_parse_tag_type (GstFLVDemux * demux, const guint8 * data,
    size_t data_size)
{
  GstFlowReturn ret = GST_FLOW_OK;
  guint8 tag_type = 0;

  tag_type = data[0];

  switch (tag_type) {
    case 9:
      demux->state = FLV_STATE_TAG_VIDEO;
      demux->has_video = TRUE;
      break;
    case 8:
      demux->state = FLV_STATE_TAG_AUDIO;
      demux->has_audio = TRUE;
      break;
    case 18:
      demux->state = FLV_STATE_TAG_SCRIPT;
      break;
    default:
      GST_WARNING_OBJECT (demux, "unsupported tag type %u", tag_type);
  }

  /* Tag size is 1 byte of type + 3 bytes of size + 7 bytes + tag data size +
   * 4 bytes of previous tag size */
  demux->tag_data_size = FLV_GET_BEUI24 (data + 1, data_size - 1);
  demux->tag_size = demux->tag_data_size + 11;

  GST_LOG_OBJECT (demux, "tag data size is %" G_GUINT64_FORMAT,
      demux->tag_data_size);

  return ret;
}

GstFlowReturn
gst_flv_parse_header (GstFLVDemux * demux, const guint8 * data,
    size_t data_size)
{
  GstFlowReturn ret = GST_FLOW_OK;

  /* Check for the FLV tag */
  if (data[0] == 'F' && data[1] == 'L' && data[2] == 'V') {
    GST_DEBUG_OBJECT (demux, "FLV header detected");
  } else {
    if (G_UNLIKELY (demux->strict)) {
      GST_WARNING_OBJECT (demux, "invalid header tag detected");
      ret = GST_FLOW_UNEXPECTED;
      goto beach;
    }
  }

  /* Jump over the 4 first bytes */
  data += 4;

  /* Now look at audio/video flags */
  {
    guint8 flags = data[0];

    demux->has_video = demux->has_audio = FALSE;

    if (flags & 1) {
      GST_DEBUG_OBJECT (demux, "there is a video stream");
      demux->has_video = TRUE;
    }
    if (flags & 4) {
      GST_DEBUG_OBJECT (demux, "there is an audio stream");
      demux->has_audio = TRUE;
    }
  }

  /* We don't care about the rest */
  demux->need_header = FALSE;

beach:
  return ret;
}
