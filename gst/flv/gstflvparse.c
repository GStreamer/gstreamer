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

#include <gst/base/gstbytereader.h>

#include <string.h>

GST_DEBUG_CATEGORY_EXTERN (flvdemux_debug);
#define GST_CAT_DEFAULT flvdemux_debug

static gchar *
FLV_GET_STRING (GstByteReader * reader)
{
  guint16 string_size = 0;
  gchar *string = NULL;
  const guint8 *str = NULL;

  g_return_val_if_fail (reader != NULL, NULL);

  if (G_UNLIKELY (!gst_byte_reader_get_uint16_be (reader, &string_size)))
    return NULL;

  if (G_UNLIKELY (string_size > gst_byte_reader_get_remaining (reader)))
    return NULL;

  string = g_try_malloc0 (string_size + 1);
  if (G_UNLIKELY (!string)) {
    return NULL;
  }

  if (G_UNLIKELY (!gst_byte_reader_get_data (reader, string_size, &str))) {
    g_free (string);
    return NULL;
  }

  memcpy (string, str, string_size);
  if (!g_utf8_validate (string, string_size, NULL)) {
    g_free (string);
    return NULL;
  }

  return string;
}

static const GstQueryType *
gst_flv_demux_query_types (GstPad * pad)
{
  static const GstQueryType query_types[] = {
    GST_QUERY_DURATION,
    GST_QUERY_POSITION,
    GST_QUERY_SEEKING,
    0
  };

  return query_types;
}

static void
parse_flv_date_string (GDate * date, const gchar * s)
{
  g_date_set_parse (date, s);
  if (g_date_valid (date))
    return;

  /* "Fri Oct 15 15:13:16 2004" needs to be parsed */
  {
    static const gchar *months[] = {
      "Jan", "Feb", "Mar", "Apr",
      "May", "Jun", "Jul", "Aug",
      "Sep", "Oct", "Nov", "Dec"
    };
    gchar **tokens = g_strsplit (s, " ", -1);
    guint64 d;
    gchar *endptr;
    gint i;

    if (g_strv_length (tokens) != 5)
      goto out;

    if (strlen (tokens[1]) != 3)
      goto out;
    for (i = 0; i < 12; i++) {
      if (!strcmp (tokens[1], months[i])) {
        break;
      }
    }
    if (i == 12)
      goto out;
    g_date_set_month (date, i + 1);

    d = g_ascii_strtoull (tokens[2], &endptr, 10);
    if (d == 0 && *endptr != '\0')
      goto out;

    g_date_set_day (date, d);

    d = g_ascii_strtoull (tokens[4], &endptr, 10);
    if (d == 0 && *endptr != '\0')
      goto out;

    g_date_set_year (date, d);

  out:
    if (tokens)
      g_strfreev (tokens);
  }
}

static gboolean
gst_flv_parse_metadata_item (GstFLVDemux * demux, GstByteReader * reader,
    gboolean * end_marker)
{
  gchar *tag_name = NULL;
  guint8 tag_type = 0;

  /* Initialize the end_marker flag to FALSE */
  *end_marker = FALSE;

  /* Name of the tag */
  tag_name = FLV_GET_STRING (reader);
  if (G_UNLIKELY (!tag_name)) {
    GST_WARNING_OBJECT (demux, "failed reading tag name");
    return FALSE;
  }

  /* What kind of object is that */
  if (!gst_byte_reader_get_uint8 (reader, &tag_type))
    goto error;

  GST_DEBUG_OBJECT (demux, "tag name %s, tag type %d", tag_name, tag_type);

  switch (tag_type) {
    case 0:                    // Double
    {                           /* Use a union to read the uint64 and then as a double */
      gdouble d = 0;

      if (!gst_byte_reader_get_float64_be (reader, &d))
        goto error;

      GST_DEBUG_OBJECT (demux, "%s => (double) %f", tag_name, d);

      if (!strcmp (tag_name, "duration")) {
        demux->duration = d * GST_SECOND;

        gst_tag_list_add (demux->taglist, GST_TAG_MERGE_REPLACE,
            GST_TAG_DURATION, demux->duration, NULL);
      } else if (!strcmp (tag_name, "AspectRatioX")) {
        demux->par_x = d;
        demux->got_par = TRUE;
      } else if (!strcmp (tag_name, "AspectRatioY")) {
        demux->par_y = d;
        demux->got_par = TRUE;
      } else {
        GST_INFO_OBJECT (demux, "Tag \'%s\' not handled", tag_name);
      }

      break;
    }
    case 1:                    // Boolean
    {
      guint8 b = 0;

      if (!gst_byte_reader_get_uint8 (reader, &b))
        goto error;

      GST_DEBUG_OBJECT (demux, "%s => (boolean) %d", tag_name, b);

      GST_INFO_OBJECT (demux, "Tag \'%s\' not handled", tag_name);

      break;
    }
    case 2:                    // String
    {
      gchar *s = NULL;

      s = FLV_GET_STRING (reader);
      if (s == NULL)
        goto error;

      GST_DEBUG_OBJECT (demux, "%s => (string) %s", tag_name, s);

      if (!strcmp (tag_name, "creationdate")) {
        GDate *date = g_date_new ();

        parse_flv_date_string (date, s);
        if (!g_date_valid (date)) {
          GST_DEBUG_OBJECT (demux, "Failed to parse string as date");
        } else {
          gst_tag_list_add (demux->taglist, GST_TAG_MERGE_REPLACE,
              GST_TAG_DATE, date, NULL);
        }
        g_date_free (date);
      } else if (!strcmp (tag_name, "creator")) {
        gst_tag_list_add (demux->taglist, GST_TAG_MERGE_REPLACE,
            GST_TAG_ARTIST, s, NULL);
      } else if (!strcmp (tag_name, "title")) {
        gst_tag_list_add (demux->taglist, GST_TAG_MERGE_REPLACE,
            GST_TAG_TITLE, s, NULL);
      } else if (!strcmp (tag_name, "metadatacreator")) {
        gst_tag_list_add (demux->taglist, GST_TAG_MERGE_REPLACE,
            GST_TAG_ENCODER, s, NULL);
      } else {
        GST_INFO_OBJECT (demux, "Tag \'%s\' not handled", tag_name);
      }

      g_free (s);

      break;
    }
    case 3:                    // Object
    {
      gboolean end_of_object_marker = FALSE;

      while (!end_of_object_marker) {
        gboolean ok =
            gst_flv_parse_metadata_item (demux, reader, &end_of_object_marker);

        if (G_UNLIKELY (!ok)) {
          GST_WARNING_OBJECT (demux, "failed reading a tag, skipping");
          goto error;
        }
      }

      break;
    }
    case 8:                    // ECMA array
    {
      guint32 nb_elems = 0;
      gboolean end_of_object_marker = FALSE;

      if (!gst_byte_reader_get_uint32_be (reader, &nb_elems))
        goto error;

      GST_DEBUG_OBJECT (demux, "there are approx. %d elements in the array",
          nb_elems);

      while (!end_of_object_marker) {
        gboolean ok =
            gst_flv_parse_metadata_item (demux, reader, &end_of_object_marker);

        if (G_UNLIKELY (!ok)) {
          GST_WARNING_OBJECT (demux, "failed reading a tag, skipping");
          goto error;
        }
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
      guint32 nb_elems = 0;

      if (!gst_byte_reader_get_uint32_be (reader, &nb_elems))
        goto error;

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
        guint8 elem_type = 0;

        if (!gst_byte_reader_get_uint8 (reader, &elem_type))
          goto error;

        switch (elem_type) {
          case 0:
          {
            gdouble d;

            if (!gst_byte_reader_get_float64_be (reader, &d))
              goto error;

            GST_DEBUG_OBJECT (demux, "element is a double %f", d);

            if (!strcmp (tag_name, "times") && demux->times) {
              g_array_append_val (demux->times, d);
            } else if (!strcmp (tag_name, "filepositions") &&
                demux->filepositions) {
              g_array_append_val (demux->filepositions, d);
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
      gdouble d = 0;
      gint16 i = 0;

      if (!gst_byte_reader_get_float64_be (reader, &d))
        goto error;

      if (!gst_byte_reader_get_int16_be (reader, &i))
        goto error;

      GST_DEBUG_OBJECT (demux,
          "%s => (date as a double) %f, timezone offset %d", tag_name, d, i);

      GST_INFO_OBJECT (demux, "Tag \'%s\' not handled", tag_name);

      break;
    }
    default:
      GST_WARNING_OBJECT (demux, "unsupported tag type %d", tag_type);
  }

  g_free (tag_name);

  return TRUE;

error:
  g_free (tag_name);

  return FALSE;
}

GstFlowReturn
gst_flv_parse_tag_script (GstFLVDemux * demux, GstBuffer * buffer)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GstByteReader reader = GST_BYTE_READER_INIT_FROM_BUFFER (buffer);
  guint8 type = 0;

  g_return_val_if_fail (GST_BUFFER_SIZE (buffer) >= 7, GST_FLOW_ERROR);

  gst_byte_reader_skip (&reader, 7);

  GST_LOG_OBJECT (demux, "parsing a script tag");

  if (!gst_byte_reader_get_uint8 (&reader, &type))
    return GST_FLOW_OK;

  /* Must be string */
  if (type == 2) {
    gchar *function_name;
    guint i;

    function_name = FLV_GET_STRING (&reader);

    GST_LOG_OBJECT (demux, "function name is %s", GST_STR_NULL (function_name));

    if (function_name != NULL && strcmp (function_name, "onMetaData") == 0) {
      guint32 nb_elems = 0;
      gboolean end_marker = FALSE;

      GST_DEBUG_OBJECT (demux, "we have a metadata script object");

      /* Next type must be a ECMA array */
      if (!gst_byte_reader_get_uint8 (&reader, &type) || type != 8) {
        g_free (function_name);
        return GST_FLOW_OK;
      }

      if (!gst_byte_reader_get_uint32_be (&reader, &nb_elems)) {
        g_free (function_name);
        return GST_FLOW_OK;
      }

      GST_DEBUG_OBJECT (demux, "there are approx. %d elements in the array",
          nb_elems);

      while (nb_elems-- && !end_marker) {
        gboolean ok = gst_flv_parse_metadata_item (demux, &reader, &end_marker);

        if (G_UNLIKELY (!ok)) {
          GST_WARNING_OBJECT (demux, "failed reading a tag, skipping");
          break;
        }
      }

      demux->push_tags = TRUE;
    }

    g_free (function_name);

    if (demux->index && demux->times && demux->filepositions
        && !demux->random_access) {
      guint num;

      /* If an index was found and we're in push mode, insert associations */
      num = MIN (demux->times->len, demux->filepositions->len);
      for (i = 0; i < num; i++) {
        GstIndexAssociation associations[2];
        guint64 time, fileposition;

        time = g_array_index (demux->times, gdouble, i) * GST_SECOND;
        fileposition = g_array_index (demux->filepositions, gdouble, i);
        GST_LOG_OBJECT (demux, "adding association %" GST_TIME_FORMAT "-> %"
            G_GUINT64_FORMAT, GST_TIME_ARGS (time), fileposition);
        associations[0].format = GST_FORMAT_TIME;
        associations[0].value = time;
        associations[1].format = GST_FORMAT_BYTES;
        associations[1].value = fileposition;

        gst_index_add_associationv (demux->index, demux->index_id,
            GST_ASSOCIATION_FLAG_KEY_UNIT, 2, associations);
      }
      demux->indexed = TRUE;
    }
  }

  return ret;
}

static gboolean
gst_flv_parse_audio_negotiate (GstFLVDemux * demux, guint32 codec_tag,
    guint32 rate, guint32 channels, guint32 width)
{
  GstCaps *caps = NULL;
  gchar *codec_name = NULL;
  gboolean ret = FALSE;

  switch (codec_tag) {
    case 1:
      caps = gst_caps_new_simple ("audio/x-adpcm", "layout", G_TYPE_STRING,
          "swf", NULL);
      codec_name = "Shockwave ADPCM";
      break;
    case 2:
    case 14:
      caps = gst_caps_new_simple ("audio/mpeg",
          "mpegversion", G_TYPE_INT, 1, "layer", G_TYPE_INT, 3,
          "parsed", G_TYPE_BOOLEAN, TRUE, NULL);
      codec_name = "MPEG 1 Audio, Layer 3 (MP3)";
      break;
    case 0:
    case 3:
      /* Assuming little endian for 0 (aka endianness of the
       * system on which the file was created) as most people
       * are probably using little endian machines */
      caps = gst_caps_new_simple ("audio/x-raw-int",
          "endianness", G_TYPE_INT, G_LITTLE_ENDIAN,
          "signed", G_TYPE_BOOLEAN, (width == 8) ? FALSE : TRUE,
          "width", G_TYPE_INT, width, "depth", G_TYPE_INT, width, NULL);
      codec_name = "Raw Audio";
      break;
    case 4:
    case 5:
    case 6:
      caps = gst_caps_new_simple ("audio/x-nellymoser", NULL);
      codec_name = "Nellymoser ASAO";
      break;
    case 10:
      caps = gst_caps_new_simple ("audio/mpeg",
          "mpegversion", G_TYPE_INT, 4, "framed", G_TYPE_BOOLEAN, TRUE, NULL);
      codec_name = "AAC";
      break;
    case 7:
      caps = gst_caps_new_simple ("audio/x-alaw", NULL);
      codec_name = "A-Law";
      break;
    case 8:
      caps = gst_caps_new_simple ("audio/x-mulaw", NULL);
      codec_name = "Mu-Law";
      break;
    case 11:
      caps = gst_caps_new_simple ("audio/x-speex", NULL);
      codec_name = "Speex";
      break;
    default:
      GST_WARNING_OBJECT (demux, "unsupported audio codec tag %u", codec_tag);
  }

  if (G_UNLIKELY (!caps)) {
    GST_WARNING_OBJECT (demux, "failed creating caps for audio pad");
    goto beach;
  }

  gst_caps_set_simple (caps,
      "rate", G_TYPE_INT, rate, "channels", G_TYPE_INT, channels, NULL);

  if (demux->audio_codec_data) {
    gst_caps_set_simple (caps, "codec_data", GST_TYPE_BUFFER,
        demux->audio_codec_data, NULL);
  }

  ret = gst_pad_set_caps (demux->audio_pad, caps);

  if (G_LIKELY (ret)) {
    /* Store the caps we have set */
    demux->audio_codec_tag = codec_tag;
    demux->rate = rate;
    demux->channels = channels;
    demux->width = width;

    if (codec_name) {
      if (demux->taglist == NULL)
        demux->taglist = gst_tag_list_new ();
      gst_tag_list_add (demux->taglist, GST_TAG_MERGE_REPLACE,
          GST_TAG_AUDIO_CODEC, codec_name, NULL);
    }

    GST_DEBUG_OBJECT (demux->audio_pad, "successfully negotiated caps %"
        GST_PTR_FORMAT, caps);
  } else {
    GST_WARNING_OBJECT (demux->audio_pad, "failed negotiating caps %"
        GST_PTR_FORMAT, caps);
  }

  gst_caps_unref (caps);

beach:
  return ret;
}

GstFlowReturn
gst_flv_parse_tag_audio (GstFLVDemux * demux, GstBuffer * buffer)
{
  GstFlowReturn ret = GST_FLOW_OK;
  guint32 pts = 0, codec_tag = 0, rate = 5512, width = 8, channels = 1;
  guint32 codec_data = 0, pts_ext = 0;
  guint8 flags = 0;
  guint8 *data = GST_BUFFER_DATA (buffer);
  GstBuffer *outbuf;

  GST_LOG_OBJECT (demux, "parsing an audio tag");

  if (demux->no_more_pads && !demux->audio_pad) {
    GST_WARNING_OBJECT (demux,
        "Signaled no-more-pads already but had no audio pad -- ignoring");
    goto beach;
  }

  g_return_val_if_fail (GST_BUFFER_SIZE (buffer) == demux->tag_size,
      GST_FLOW_ERROR);

  GST_LOG_OBJECT (demux, "pts bytes %02X %02X %02X %02X", data[0], data[1],
      data[2], data[3]);

  /* Grab information about audio tag */
  pts = GST_READ_UINT24_BE (data);
  /* read the pts extension to 32 bits integer */
  pts_ext = GST_READ_UINT8 (data + 3);
  /* Combine them */
  pts |= pts_ext << 24;

  /* Error out on tags with too small headers */
  if (GST_BUFFER_SIZE (buffer) < 11) {
    GST_ERROR_OBJECT (demux, "Too small tag size (%d)",
        GST_BUFFER_SIZE (buffer));
    return GST_FLOW_ERROR;
  }

  /* Silently skip buffers with no data */
  if (GST_BUFFER_SIZE (buffer) == 11)
    return GST_FLOW_OK;

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
  if (codec_tag == 10) {        /* AAC has an extra byte for packet type */
    codec_data = 2;
  } else {
    codec_data = 1;
  }

  /* codec tags with special rates */
  if (codec_tag == 5 || codec_tag == 14)
    rate = 8000;
  else if (codec_tag == 4)
    rate = 16000;

  GST_LOG_OBJECT (demux, "audio tag with %d channels, %dHz sampling rate, "
      "%d bits width, codec tag %u (flags %02X)", channels, rate, width,
      codec_tag, flags);

  /* If we don't have our audio pad created, then create it. */
  if (G_UNLIKELY (!demux->audio_pad)) {

    demux->audio_pad =
        gst_pad_new_from_template (gst_element_class_get_pad_template
        (GST_ELEMENT_GET_CLASS (demux), "audio"), "audio");
    if (G_UNLIKELY (!demux->audio_pad)) {
      GST_WARNING_OBJECT (demux, "failed creating audio pad");
      ret = GST_FLOW_ERROR;
      goto beach;
    }

    /* Negotiate caps */
    if (!gst_flv_parse_audio_negotiate (demux, codec_tag, rate, channels,
            width)) {
      gst_object_unref (demux->audio_pad);
      demux->audio_pad = NULL;
      ret = GST_FLOW_ERROR;
      goto beach;
    }

    GST_DEBUG_OBJECT (demux, "created audio pad with caps %" GST_PTR_FORMAT,
        GST_PAD_CAPS (demux->audio_pad));

    /* Set functions on the pad */
    gst_pad_set_query_type_function (demux->audio_pad,
        GST_DEBUG_FUNCPTR (gst_flv_demux_query_types));
    gst_pad_set_query_function (demux->audio_pad,
        GST_DEBUG_FUNCPTR (gst_flv_demux_query));
    gst_pad_set_event_function (demux->audio_pad,
        GST_DEBUG_FUNCPTR (gst_flv_demux_src_event));

    gst_pad_use_fixed_caps (demux->audio_pad);

    /* Make it active */
    gst_pad_set_active (demux->audio_pad, TRUE);

    /* We need to set caps before adding */
    gst_element_add_pad (GST_ELEMENT (demux),
        gst_object_ref (demux->audio_pad));

    /* We only emit no more pads when we have audio and video. Indeed we can
     * not trust the FLV header to tell us if there will be only audio or 
     * only video and we would just break discovery of some files */
    if (demux->audio_pad && demux->video_pad) {
      GST_DEBUG_OBJECT (demux, "emitting no more pads");
      gst_element_no_more_pads (GST_ELEMENT (demux));
      demux->no_more_pads = TRUE;
    }
  }

  /* Check if caps have changed */
  if (G_UNLIKELY (rate != demux->rate || channels != demux->channels ||
          codec_tag != demux->audio_codec_tag || width != demux->width)) {
    GST_DEBUG_OBJECT (demux, "audio settings have changed, changing caps");

    /* Negotiate caps */
    if (!gst_flv_parse_audio_negotiate (demux, codec_tag, rate, channels,
            width)) {
      ret = GST_FLOW_ERROR;
      goto beach;
    }
  }

  /* Push taglist if present */
  if ((demux->has_audio && !demux->audio_pad) &&
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
  outbuf =
      gst_buffer_create_sub (buffer, 7 + codec_data,
      demux->tag_data_size - codec_data);

  if (demux->audio_codec_tag == 10) {
    guint8 aac_packet_type = GST_READ_UINT8 (data + 8);

    switch (aac_packet_type) {
      case 0:
      {
        /* AudioSpecificConfic data */
        GST_LOG_OBJECT (demux, "got an AAC codec data packet");
        if (demux->audio_codec_data) {
          gst_buffer_unref (demux->audio_codec_data);
        }
        demux->audio_codec_data = outbuf;
        /* Use that buffer data in the caps */
        gst_flv_parse_audio_negotiate (demux, codec_tag, rate, channels, width);
        goto beach;
        break;
      }
      case 1:
        /* AAC raw packet */
        GST_LOG_OBJECT (demux, "got a raw AAC audio packet");
        break;
      default:
        GST_WARNING_OBJECT (demux, "invalid AAC packet type %u",
            aac_packet_type);
    }
  }

  /* Fill buffer with data */
  GST_BUFFER_TIMESTAMP (outbuf) = pts * GST_MSECOND;
  GST_BUFFER_DURATION (outbuf) = GST_CLOCK_TIME_NONE;
  GST_BUFFER_OFFSET (outbuf) = demux->audio_offset++;
  GST_BUFFER_OFFSET_END (outbuf) = demux->audio_offset;
  gst_buffer_set_caps (outbuf, GST_PAD_CAPS (demux->audio_pad));

  if (demux->duration == GST_CLOCK_TIME_NONE ||
      demux->duration < GST_BUFFER_TIMESTAMP (outbuf))
    demux->duration = GST_BUFFER_TIMESTAMP (outbuf);

  /* Only add audio frames to the index if we have no video, if we don't have
   * random access and if the index is not yet complete */
  if (!demux->has_video && demux->index && !demux->random_access
      && !demux->indexed) {
    GST_LOG_OBJECT (demux,
        "adding association %" GST_TIME_FORMAT "-> %" G_GUINT64_FORMAT,
        GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (outbuf)), demux->cur_tag_offset);
    gst_index_add_association (demux->index, demux->index_id,
        GST_ASSOCIATION_FLAG_KEY_UNIT, GST_FORMAT_TIME,
        GST_BUFFER_TIMESTAMP (outbuf), GST_FORMAT_BYTES, demux->cur_tag_offset,
        NULL);
  }

  if (G_UNLIKELY (demux->audio_need_discont)) {
    GST_BUFFER_FLAG_SET (outbuf, GST_BUFFER_FLAG_DISCONT);
    demux->audio_need_discont = FALSE;
  }

  gst_segment_set_last_stop (&demux->segment, GST_FORMAT_TIME,
      GST_BUFFER_TIMESTAMP (outbuf));

  /* Do we need a newsegment event ? */
  if (G_UNLIKELY (demux->audio_need_segment)) {
    if (demux->close_seg_event)
      gst_pad_push_event (demux->audio_pad,
          gst_event_ref (demux->close_seg_event));

    if (!demux->new_seg_event) {
      GST_DEBUG_OBJECT (demux, "pushing newsegment from %"
          GST_TIME_FORMAT " to %" GST_TIME_FORMAT,
          GST_TIME_ARGS (demux->segment.last_stop),
          GST_TIME_ARGS (demux->segment.stop));
      demux->new_seg_event =
          gst_event_new_new_segment (FALSE, demux->segment.rate,
          demux->segment.format, demux->segment.last_stop,
          demux->segment.stop, demux->segment.last_stop);
    } else {
      GST_DEBUG_OBJECT (demux, "pushing pre-generated newsegment event");
    }

    gst_pad_push_event (demux->audio_pad, gst_event_ref (demux->new_seg_event));

    demux->audio_need_segment = FALSE;
  }

  GST_LOG_OBJECT (demux, "pushing %d bytes buffer at pts %" GST_TIME_FORMAT
      " with duration %" GST_TIME_FORMAT ", offset %" G_GUINT64_FORMAT,
      GST_BUFFER_SIZE (outbuf), GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (outbuf)),
      GST_TIME_ARGS (GST_BUFFER_DURATION (outbuf)), GST_BUFFER_OFFSET (outbuf));

  if (!GST_CLOCK_TIME_IS_VALID (demux->audio_start)) {
    demux->audio_start = GST_BUFFER_TIMESTAMP (outbuf);
  }

  if (G_UNLIKELY (!demux->no_more_pads
          && (GST_CLOCK_DIFF (demux->audio_start,
                  GST_BUFFER_TIMESTAMP (outbuf)) > 6 * GST_SECOND))) {
    GST_DEBUG_OBJECT (demux,
        "Signalling no-more-pads because no video stream was found"
        " after 6 seconds of audio");
    gst_element_no_more_pads (GST_ELEMENT_CAST (demux));
    demux->no_more_pads = TRUE;
  }

  /* Push downstream */
  ret = gst_pad_push (demux->audio_pad, outbuf);
  if (G_UNLIKELY (ret != GST_FLOW_OK)) {
    GST_WARNING_OBJECT (demux, "failed pushing a %" G_GUINT64_FORMAT
        " bytes audio buffer: %s", demux->tag_data_size,
        gst_flow_get_name (ret));
    if (ret == GST_FLOW_NOT_LINKED) {
      demux->audio_linked = FALSE;
    }
    goto beach;
  }

  demux->audio_linked = TRUE;

beach:
  return ret;
}

static gboolean
gst_flv_parse_video_negotiate (GstFLVDemux * demux, guint32 codec_tag)
{
  gboolean ret = FALSE;
  GstCaps *caps = NULL;
  gchar *codec_name = NULL;

  /* Generate caps for that pad */
  switch (codec_tag) {
    case 2:
      caps = gst_caps_new_simple ("video/x-flash-video", NULL);
      codec_name = "Sorenson Video";
      break;
    case 3:
      caps = gst_caps_new_simple ("video/x-flash-screen", NULL);
      codec_name = "Flash Screen Video";
      break;
    case 4:
      caps = gst_caps_new_simple ("video/x-vp6-flash", NULL);
      codec_name = "On2 VP6 Video";
      break;
    case 5:
      caps = gst_caps_new_simple ("video/x-vp6-alpha", NULL);
      codec_name = "On2 VP6 Video with alpha channel";
      break;
    case 7:
      caps = gst_caps_new_simple ("video/x-h264", NULL);
      codec_name = "H.264/AVC Video";
      break;
    default:
      GST_WARNING_OBJECT (demux, "unsupported video codec tag %u", codec_tag);
  }

  if (G_UNLIKELY (!caps)) {
    GST_WARNING_OBJECT (demux, "failed creating caps for video pad");
    goto beach;
  }

  gst_caps_set_simple (caps, "pixel-aspect-ratio", GST_TYPE_FRACTION,
      demux->par_x, demux->par_y, NULL);

  if (demux->video_codec_data) {
    gst_caps_set_simple (caps, "codec_data", GST_TYPE_BUFFER,
        demux->video_codec_data, NULL);
  }

  ret = gst_pad_set_caps (demux->video_pad, caps);

  if (G_LIKELY (ret)) {
    /* Store the caps we have set */
    demux->video_codec_tag = codec_tag;

    if (codec_name) {
      if (demux->taglist == NULL)
        demux->taglist = gst_tag_list_new ();
      gst_tag_list_add (demux->taglist, GST_TAG_MERGE_REPLACE,
          GST_TAG_VIDEO_CODEC, codec_name, NULL);
    }

    GST_DEBUG_OBJECT (demux->video_pad, "successfully negotiated caps %"
        GST_PTR_FORMAT, caps);
  } else {
    GST_WARNING_OBJECT (demux->video_pad, "failed negotiating caps %"
        GST_PTR_FORMAT, caps);
  }

  gst_caps_unref (caps);

beach:
  return ret;
}

GstFlowReturn
gst_flv_parse_tag_video (GstFLVDemux * demux, GstBuffer * buffer)
{
  GstFlowReturn ret = GST_FLOW_OK;
  guint32 pts = 0, codec_data = 1, pts_ext = 0;
  gboolean keyframe = FALSE;
  guint8 flags = 0, codec_tag = 0;
  guint8 *data = GST_BUFFER_DATA (buffer);
  GstBuffer *outbuf;

  g_return_val_if_fail (GST_BUFFER_SIZE (buffer) == demux->tag_size,
      GST_FLOW_ERROR);

  GST_LOG_OBJECT (demux, "parsing a video tag");


  GST_LOG_OBJECT (demux, "pts bytes %02X %02X %02X %02X", data[0], data[1],
      data[2], data[3]);

  if (demux->no_more_pads && !demux->video_pad) {
    GST_WARNING_OBJECT (demux,
        "Signaled no-more-pads already but had no audio pad -- ignoring");
    goto beach;
  }

  /* Grab information about video tag */
  pts = GST_READ_UINT24_BE (data);
  /* read the pts extension to 32 bits integer */
  pts_ext = GST_READ_UINT8 (data + 3);
  /* Combine them */
  pts |= pts_ext << 24;

  if (GST_BUFFER_SIZE (buffer) < 12) {
    GST_ERROR_OBJECT (demux, "Too small tag size");
    return GST_FLOW_ERROR;
  }

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
  } else if (codec_tag == 7) {
    codec_data = 5;
  }

  GST_LOG_OBJECT (demux, "video tag with codec tag %u, keyframe (%d) "
      "(flags %02X)", codec_tag, keyframe, flags);

  /* If we don't have our video pad created, then create it. */
  if (G_UNLIKELY (!demux->video_pad)) {
    demux->video_pad =
        gst_pad_new_from_template (gst_element_class_get_pad_template
        (GST_ELEMENT_GET_CLASS (demux), "video"), "video");
    if (G_UNLIKELY (!demux->video_pad)) {
      GST_WARNING_OBJECT (demux, "failed creating video pad");
      ret = GST_FLOW_ERROR;
      goto beach;
    }

    if (!gst_flv_parse_video_negotiate (demux, codec_tag)) {
      gst_object_unref (demux->video_pad);
      demux->video_pad = NULL;
      ret = GST_FLOW_ERROR;
      goto beach;
    }

    /* When we ve set pixel-aspect-ratio we use that boolean to detect a 
     * metadata tag that would come later and trigger a caps change */
    demux->got_par = FALSE;

    GST_DEBUG_OBJECT (demux, "created video pad with caps %" GST_PTR_FORMAT,
        GST_PAD_CAPS (demux->video_pad));

    /* Set functions on the pad */
    gst_pad_set_query_type_function (demux->video_pad,
        GST_DEBUG_FUNCPTR (gst_flv_demux_query_types));
    gst_pad_set_query_function (demux->video_pad,
        GST_DEBUG_FUNCPTR (gst_flv_demux_query));
    gst_pad_set_event_function (demux->video_pad,
        GST_DEBUG_FUNCPTR (gst_flv_demux_src_event));

    gst_pad_use_fixed_caps (demux->video_pad);

    /* Make it active */
    gst_pad_set_active (demux->video_pad, TRUE);

    /* We need to set caps before adding */
    gst_element_add_pad (GST_ELEMENT (demux),
        gst_object_ref (demux->video_pad));

    /* We only emit no more pads when we have audio and video. Indeed we can
     * not trust the FLV header to tell us if there will be only audio or 
     * only video and we would just break discovery of some files */
    if (demux->audio_pad && demux->video_pad) {
      GST_DEBUG_OBJECT (demux, "emitting no more pads");
      gst_element_no_more_pads (GST_ELEMENT (demux));
      demux->no_more_pads = TRUE;
    }
  }

  /* Check if caps have changed */
  if (G_UNLIKELY (codec_tag != demux->video_codec_tag || demux->got_par)) {

    GST_DEBUG_OBJECT (demux, "video settings have changed, changing caps");

    if (!gst_flv_parse_video_negotiate (demux, codec_tag)) {
      ret = GST_FLOW_ERROR;
      goto beach;
    }

    /* When we ve set pixel-aspect-ratio we use that boolean to detect a 
     * metadata tag that would come later and trigger a caps change */
    demux->got_par = FALSE;
  }

  /* Push taglist if present */
  if ((demux->has_audio && !demux->audio_pad) &&
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
  outbuf =
      gst_buffer_create_sub (buffer, 7 + codec_data,
      demux->tag_data_size - codec_data);

  if (demux->video_codec_tag == 7) {
    guint8 avc_packet_type = GST_READ_UINT8 (data + 8);

    switch (avc_packet_type) {
      case 0:
      {
        /* AVCDecoderConfigurationRecord data */
        GST_LOG_OBJECT (demux, "got an H.264 codec data packet");
        if (demux->video_codec_data) {
          gst_buffer_unref (demux->video_codec_data);
        }
        demux->video_codec_data = outbuf;
        /* Use that buffer data in the caps */
        gst_flv_parse_video_negotiate (demux, codec_tag);
        goto beach;
        break;
      }
      case 1:
        /* H.264 NALU packet */
        GST_LOG_OBJECT (demux, "got a H.264 NALU video packet");
        break;
      default:
        GST_WARNING_OBJECT (demux, "invalid video packet type %u",
            avc_packet_type);
    }
  }

  /* Fill buffer with data */
  GST_BUFFER_TIMESTAMP (outbuf) = pts * GST_MSECOND;
  GST_BUFFER_DURATION (outbuf) = GST_CLOCK_TIME_NONE;
  GST_BUFFER_OFFSET (outbuf) = demux->video_offset++;
  GST_BUFFER_OFFSET_END (outbuf) = demux->video_offset;
  gst_buffer_set_caps (outbuf, GST_PAD_CAPS (demux->video_pad));

  if (demux->duration == GST_CLOCK_TIME_NONE ||
      demux->duration < GST_BUFFER_TIMESTAMP (outbuf))
    demux->duration = GST_BUFFER_TIMESTAMP (outbuf);

  if (!keyframe)
    GST_BUFFER_FLAG_SET (outbuf, GST_BUFFER_FLAG_DELTA_UNIT);

  if (!demux->indexed && demux->index && !demux->random_access) {
    GST_LOG_OBJECT (demux, "adding association %" GST_TIME_FORMAT "-> %"
        G_GUINT64_FORMAT, GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (outbuf)),
        demux->cur_tag_offset);
    gst_index_add_association (demux->index, demux->index_id,
        keyframe ? GST_ASSOCIATION_FLAG_KEY_UNIT : GST_ASSOCIATION_FLAG_NONE,
        GST_FORMAT_TIME, GST_BUFFER_TIMESTAMP (outbuf),
        GST_FORMAT_BYTES, demux->cur_tag_offset, NULL);
  }

  if (G_UNLIKELY (demux->video_need_discont)) {
    GST_BUFFER_FLAG_SET (outbuf, GST_BUFFER_FLAG_DISCONT);
    demux->video_need_discont = FALSE;
  }

  gst_segment_set_last_stop (&demux->segment, GST_FORMAT_TIME,
      GST_BUFFER_TIMESTAMP (outbuf));

  /* Do we need a newsegment event ? */
  if (G_UNLIKELY (demux->video_need_segment)) {
    if (demux->close_seg_event)
      gst_pad_push_event (demux->video_pad,
          gst_event_ref (demux->close_seg_event));

    if (!demux->new_seg_event) {
      GST_DEBUG_OBJECT (demux, "pushing newsegment from %"
          GST_TIME_FORMAT " to %" GST_TIME_FORMAT,
          GST_TIME_ARGS (demux->segment.last_stop),
          GST_TIME_ARGS (demux->segment.stop));
      demux->new_seg_event =
          gst_event_new_new_segment (FALSE, demux->segment.rate,
          demux->segment.format, demux->segment.last_stop,
          demux->segment.stop, demux->segment.last_stop);
    } else {
      GST_DEBUG_OBJECT (demux, "pushing pre-generated newsegment event");
    }

    gst_pad_push_event (demux->video_pad, gst_event_ref (demux->new_seg_event));

    demux->video_need_segment = FALSE;
  }

  GST_LOG_OBJECT (demux, "pushing %d bytes buffer at pts %" GST_TIME_FORMAT
      " with duration %" GST_TIME_FORMAT ", offset %" G_GUINT64_FORMAT
      ", keyframe (%d)", GST_BUFFER_SIZE (outbuf),
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (outbuf)),
      GST_TIME_ARGS (GST_BUFFER_DURATION (outbuf)), GST_BUFFER_OFFSET (outbuf),
      keyframe);

  if (!GST_CLOCK_TIME_IS_VALID (demux->video_start)) {
    demux->video_start = GST_BUFFER_TIMESTAMP (outbuf);
  }

  if (G_UNLIKELY (!demux->no_more_pads
          && (GST_CLOCK_DIFF (demux->video_start,
                  GST_BUFFER_TIMESTAMP (outbuf)) > 6 * GST_SECOND))) {
    GST_DEBUG_OBJECT (demux,
        "Signalling no-more-pads because no audio stream was found"
        " after 6 seconds of video");
    gst_element_no_more_pads (GST_ELEMENT_CAST (demux));
    demux->no_more_pads = TRUE;
  }

  /* Push downstream */
  ret = gst_pad_push (demux->video_pad, outbuf);

  if (G_UNLIKELY (ret != GST_FLOW_OK)) {
    GST_WARNING_OBJECT (demux, "failed pushing a %" G_GUINT64_FORMAT
        " bytes video buffer: %s", demux->tag_data_size,
        gst_flow_get_name (ret));
    if (ret == GST_FLOW_NOT_LINKED) {
      demux->video_linked = FALSE;
    }
    goto beach;
  }

  demux->video_linked = TRUE;

beach:
  return ret;
}

GstClockTime
gst_flv_parse_tag_timestamp (GstFLVDemux * demux, GstBuffer * buffer,
    size_t * tag_size)
{
  guint32 pts = 0, pts_ext = 0;
  guint32 tag_data_size;
  guint8 type;
  gboolean keyframe = TRUE;
  GstClockTime ret;
  guint8 *data = GST_BUFFER_DATA (buffer);

  g_return_val_if_fail (GST_BUFFER_SIZE (buffer) >= 12, GST_CLOCK_TIME_NONE);

  type = data[0];

  if (type != 9 && type != 8 && type != 18) {
    GST_WARNING_OBJECT (demux, "Unsupported tag type %u", data[0]);
    return GST_CLOCK_TIME_NONE;
  }

  if (type == 9)
    demux->has_video = TRUE;
  else if (type == 8)
    demux->has_audio = TRUE;

  tag_data_size = GST_READ_UINT24_BE (data + 1);

  if (GST_BUFFER_SIZE (buffer) >= tag_data_size + 11 + 4) {
    if (GST_READ_UINT32_BE (data + tag_data_size + 11) != tag_data_size + 11) {
      GST_WARNING_OBJECT (demux, "Invalid tag size");
      return GST_CLOCK_TIME_NONE;
    }
  }

  if (tag_size)
    *tag_size = tag_data_size + 11 + 4;

  data += 4;

  GST_LOG_OBJECT (demux, "pts bytes %02X %02X %02X %02X", data[0], data[1],
      data[2], data[3]);

  /* Grab timestamp of tag tag */
  pts = GST_READ_UINT24_BE (data);
  /* read the pts extension to 32 bits integer */
  pts_ext = GST_READ_UINT8 (data + 3);
  /* Combine them */
  pts |= pts_ext << 24;

  if (type == 9) {
    data += 7;

    keyframe = ((data[0] >> 4) == 1);
  }

  ret = pts * GST_MSECOND;

  if (demux->index && !demux->indexed && (type == 9 || (type == 8
              && !demux->has_video))) {
    GstIndexAssociation associations[2];

    GST_LOG_OBJECT (demux,
        "adding association %" GST_TIME_FORMAT "-> %" G_GUINT64_FORMAT,
        GST_TIME_ARGS (ret), demux->offset);

    associations[0].format = GST_FORMAT_TIME;
    associations[0].value = ret;
    associations[1].format = GST_FORMAT_BYTES;
    associations[1].value = demux->offset;

    gst_index_add_associationv (demux->index, demux->index_id,
        (keyframe) ? GST_ASSOCIATION_FLAG_KEY_UNIT : GST_ASSOCIATION_FLAG_NONE,
        2, associations);
  }

  if (demux->duration == GST_CLOCK_TIME_NONE || demux->duration < ret)
    demux->duration = ret;

  return ret;
}

GstFlowReturn
gst_flv_parse_tag_type (GstFLVDemux * demux, GstBuffer * buffer)
{
  GstFlowReturn ret = GST_FLOW_OK;
  guint8 tag_type = 0;
  guint8 *data = GST_BUFFER_DATA (buffer);

  g_return_val_if_fail (GST_BUFFER_SIZE (buffer) >= 4, GST_FLOW_ERROR);

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
  demux->tag_data_size = GST_READ_UINT24_BE (data + 1);
  demux->tag_size = demux->tag_data_size + 11;

  GST_LOG_OBJECT (demux, "tag data size is %" G_GUINT64_FORMAT,
      demux->tag_data_size);

  return ret;
}

GstFlowReturn
gst_flv_parse_header (GstFLVDemux * demux, GstBuffer * buffer)
{
  GstFlowReturn ret = GST_FLOW_OK;
  guint8 *data = GST_BUFFER_DATA (buffer);

  g_return_val_if_fail (GST_BUFFER_SIZE (buffer) >= 9, GST_FLOW_ERROR);

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
