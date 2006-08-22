/* -*- Mode: C; tab-width: 2; indent-tabs-mode: t; c-basic-offset: 2 -*- */
/* Copyright 2006 Tim-Philipp Müller <tim centricular net>
 * Copyright 2005 Jan Schmidt <thaytan@mad.scientist.com>
 * Copyright 2002,2003 Scott Wheeler <wheeler@kde.org> (portions from taglib)
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
#include <stdlib.h>
#include <gst/tag/tag.h>
#include <gst/base/gsttypefindhelper.h>

#ifdef HAVE_ZLIB
#include <zlib.h>
#endif

#include "id3tags.h"

GST_DEBUG_CATEGORY_EXTERN (id3demux_debug);
#define GST_CAT_DEFAULT (id3demux_debug)

static gboolean parse_comment_frame (ID3TagsWorking * work);
static GArray *parse_text_identification_frame (ID3TagsWorking * work);
static gchar *parse_user_text_identification_frame (ID3TagsWorking * work,
    const gchar ** tag_name);
static gchar *parse_unique_file_identifier (ID3TagsWorking * work,
    const gchar ** tag_name);
static gboolean parse_relative_volume_adjustment_two (ID3TagsWorking * work);
static gboolean id3v2_tag_to_taglist (ID3TagsWorking * work,
    const gchar * tag_name, const gchar * tag_str);
/* Parse a single string into an array of gchar* */
static void parse_split_strings (guint8 encoding, gchar * data, gint data_size,
    GArray ** out_fields);
static void free_tag_strings (GArray * fields);
static gboolean
id3v2_genre_fields_to_taglist (ID3TagsWorking * work, const gchar * tag_name,
    GArray * tag_fields);
static gboolean parse_picture_frame (ID3TagsWorking * work);

#define ID3V2_ENCODING_ISO8859 0x00
#define ID3V2_ENCODING_UTF16   0x01
#define ID3V2_ENCODING_UTF16BE 0x02
#define ID3V2_ENCODING_UTF8    0x03

extern guint read_synch_uint (guint8 * data, guint size);

gboolean
id3demux_id3v2_parse_frame (ID3TagsWorking * work)
{
  const gchar *tag_name;
  gboolean result = FALSE;
  gint i;
  guint8 *frame_data = work->hdr.frame_data;
  guint frame_data_size = work->cur_frame_size;
  gchar *tag_str = NULL;
  GArray *tag_fields = NULL;

  /* Check that the frame id is valid */
  for (i = 0; i < 5 && work->frame_id[i] != '\0'; i++) {
    if (!g_ascii_isalnum (work->frame_id[i])) {
      GST_DEBUG ("Encountered invalid frame_id");
      return FALSE;
    }
  }

  /* Can't handle encrypted frames right now */
  if (work->frame_flags & ID3V2_FRAME_FORMAT_ENCRYPTION) {
    GST_WARNING ("Encrypted frames are not supported");
    return FALSE;
  }

  if (work->frame_flags & ID3V2_FRAME_FORMAT_UNSYNCHRONISATION) {
    GST_WARNING ("ID3v2 frame with unsupported unsynchronisation applied. "
        "May fail badly");
  }

  tag_name = gst_tag_from_id3_tag (work->frame_id);
  if (tag_name == NULL &&
      strncmp (work->frame_id, "RVA2", 4) != 0 &&
      strncmp (work->frame_id, "TXXX", 4) != 0 &&
      strncmp (work->frame_id, "UFID", 4) != 0) {
    return FALSE;
  }

  if (work->frame_flags & (ID3V2_FRAME_FORMAT_COMPRESSION |
          ID3V2_FRAME_FORMAT_DATA_LENGTH_INDICATOR)) {
    if (work->hdr.frame_data_size <= 4)
      return FALSE;
    work->parse_size = read_synch_uint (frame_data, 4);
    frame_data += 4;
    frame_data_size -= 4;
    if (work->parse_size < frame_data_size) {
      GST_WARNING ("ID3v2 frame %s has invalid size %d.", tag_name,
          frame_data_size);
      return FALSE;
    }
  }

  work->parse_size = frame_data_size;

  if (work->frame_flags & ID3V2_FRAME_FORMAT_COMPRESSION) {
#ifdef HAVE_ZLIB
    uLongf destSize = work->parse_size;
    Bytef *dest, *src;

    work->parse_data = g_malloc (work->parse_size);
    g_return_val_if_fail (work->parse_data != NULL, FALSE);

    dest = (Bytef *) work->parse_data;
    src = (Bytef *) frame_data;

    if (uncompress (dest, &destSize, src, frame_data_size) != Z_OK) {
      g_free (work->parse_data);
      return FALSE;
    }
    if (destSize != work->parse_size) {
      GST_WARNING
          ("Decompressing ID3v2 frame %s did not produce expected size %d bytes (got %d)",
          tag_name, work->parse_data, destSize);
      return FALSE;
    }
#else
    GST_WARNING ("Compressed ID3v2 tag frame could not be decompressed"
        " because gstid3demux was compiled without zlib support");
    return FALSE;
#endif
  } else {
    work->parse_data = frame_data;
  }

  if (work->frame_id[0] == 'T') {
    if (strcmp (work->frame_id, "TXXX") != 0) {
      /* Text identification frame */
      tag_fields = parse_text_identification_frame (work);
    } else {
      /* Handle user text frame */
      tag_str = parse_user_text_identification_frame (work, &tag_name);
    }
  } else if (!strcmp (work->frame_id, "COMM")) {
    /* Comment */
    result = parse_comment_frame (work);
  } else if (!strcmp (work->frame_id, "APIC")) {
    /* Attached picture */
    result = parse_picture_frame (work);
  } else if (!strcmp (work->frame_id, "RVA2")) {
    /* Relative volume */
    result = parse_relative_volume_adjustment_two (work);
  } else if (!strcmp (work->frame_id, "UFID")) {
    /* Unique file identifier */
    tag_str = parse_unique_file_identifier (work, &tag_name);
  }

  if (work->frame_flags & ID3V2_FRAME_FORMAT_COMPRESSION)
    g_free (work->parse_data);

  if (tag_str != NULL) {
    /* g_print ("Tag %s value %s\n", tag_name, tag_str); */
    result = id3v2_tag_to_taglist (work, tag_name, tag_str);
    g_free (tag_str);
  }
  if (tag_fields != NULL) {
    if (strcmp (work->frame_id, "TCON") == 0) {
      /* Genre strings need special treatment */
      result |= id3v2_genre_fields_to_taglist (work, tag_name, tag_fields);
    } else {
      gint t;

      for (t = 0; t < tag_fields->len; t++) {
        tag_str = g_array_index (tag_fields, gchar *, t);
        if (tag_str != NULL && tag_str[0] != '\0')
          result |= id3v2_tag_to_taglist (work, tag_name, tag_str);
      }
    }
    free_tag_strings (tag_fields);
  }

  return result;
}

static gboolean
parse_comment_frame (ID3TagsWorking * work)
{
  guint dummy;
  guint8 encoding;
  gchar language[4];
  GArray *fields = NULL;
  gchar *description, *text;

  if (work->parse_size < 6)
    return FALSE;

  encoding = work->parse_data[0];
  language[0] = work->parse_data[1];
  language[1] = work->parse_data[2];
  language[2] = work->parse_data[3];
  language[3] = 0;

  parse_split_strings (encoding, (gchar *) work->parse_data + 4,
      work->parse_size - 4, &fields);

  if (fields == NULL || fields->len < 2) {
    GST_WARNING ("Failed to decode comment frame");
    goto fail;
  }
  description = g_array_index (fields, gchar *, 0);
  text = g_array_index (fields, gchar *, 1);

  if (!g_utf8_validate (text, -1, NULL)) {
    GST_WARNING ("Converted string is not valid utf-8");
    goto fail;
  }

  /* skip our own dummy descriptions (from id3v2mux) */
  if (strlen (description) > 0 && g_utf8_validate (description, -1, NULL) &&
      sscanf (description, "c%u", &dummy) != 1) {
    gchar *s;

    if (language[0] != '\0') {
      s = g_strdup_printf ("%s[%s]=%s", description, language, text);
    } else {
      s = g_strdup_printf ("%s=%s", description, text);
    }
    gst_tag_list_add (work->tags, GST_TAG_MERGE_APPEND,
        GST_TAG_EXTENDED_COMMENT, s, NULL);
    g_free (s);
  } else if (text != NULL && *text != '\0') {
    gst_tag_list_add (work->tags, GST_TAG_MERGE_APPEND,
        GST_TAG_COMMENT, text, NULL);
  } else {
    goto fail;
  }

  free_tag_strings (fields);
  return TRUE;

fail:
  {
    GST_WARNING ("failed to parse COMM frame");
    free_tag_strings (fields);
    return FALSE;
  }
}

static GArray *
parse_text_identification_frame (ID3TagsWorking * work)
{
  guchar encoding;
  GArray *fields = NULL;

  if (work->parse_size < 2)
    return NULL;

  encoding = work->parse_data[0];
  parse_split_strings (encoding, (gchar *) work->parse_data + 1,
      work->parse_size - 1, &fields);
  if (fields) {
    if (fields->len > 0) {
      GST_LOG ("Read %d fields from Text ID frame of size %d with encoding %d"
          ". First is '%s'", fields->len, work->parse_size - 1, encoding,
          g_array_index (fields, gchar *, 0));
    } else {
      GST_LOG ("Read 0 fields from Text ID frame of size %d with encoding %d",
          work->parse_size - 1, encoding);
    }
  }

  return fields;
}

static gchar *
parse_user_text_identification_frame (ID3TagsWorking * work,
    const gchar ** tag_name)
{
  gchar *ret;
  guchar encoding;
  GArray *fields = NULL;

  *tag_name = NULL;

  if (work->parse_size < 2)
    return NULL;

  encoding = work->parse_data[0];

  parse_split_strings (encoding, (gchar *) work->parse_data + 1,
      work->parse_size - 1, &fields);

  if (fields == NULL)
    return NULL;

  if (fields->len != 2) {
    GST_WARNING ("Expected 2 fields in TXXX frame, but got %d", fields->len);
    free_tag_strings (fields);
    return NULL;
  }

  *tag_name =
      gst_tag_from_id3_user_tag ("TXXX", g_array_index (fields, gchar *, 0));

  GST_LOG ("TXXX frame of size %d. Mapped descriptor '%s' to GStreamer tag %s",
      work->parse_size - 1, g_array_index (fields, gchar *, 0),
      GST_STR_NULL (*tag_name));

  if (*tag_name) {
    ret = g_strdup (g_array_index (fields, gchar *, 1));
    /* GST_LOG ("%s = %s", *tag_name, GST_STR_NULL (ret)); */
  } else {
    ret = NULL;
  }

  free_tag_strings (fields);
  return ret;
}

static gboolean
parse_id_string (ID3TagsWorking * work, gchar ** p_str, gint * p_len,
    gint * p_datalen)
{
  gint len, datalen;

  if (work->parse_size < 2)
    return FALSE;

  for (len = 0; len < work->parse_size - 1; ++len) {
    if (work->parse_data[len] == '\0')
      break;
  }

  datalen = work->parse_size - (len + 1);
  if (len == 0 || datalen <= 0)
    return FALSE;

  *p_str = g_strndup ((gchar *) work->parse_data, len);
  *p_len = len;
  *p_datalen = datalen;

  return TRUE;
}

static gchar *
parse_unique_file_identifier (ID3TagsWorking * work, const gchar ** tag_name)
{
  gint len, datalen;
  gchar *owner_id, *data, *ret = NULL;

  GST_LOG ("parsing UFID frame of size %d", work->parse_size);

  if (!parse_id_string (work, &owner_id, &len, &datalen))
    return NULL;

  data = (gchar *) work->parse_data + len + 1;
  GST_LOG ("UFID owner ID: %s (+ %d bytes of data)", owner_id, datalen);

  if (strcmp (owner_id, "http://musicbrainz.org") == 0 &&
      g_utf8_validate (data, datalen, NULL)) {
    *tag_name = GST_TAG_MUSICBRAINZ_TRACKID;
    ret = g_strndup (data, datalen);
  } else {
    GST_INFO ("Unknown UFID owner ID: %s", owner_id);
  }
  g_free (owner_id);

  return ret;
}

/* parse data and return length of the next string in the given encoding,
 * including the NUL terminator */
static gint
scan_encoded_string (guint8 encoding, gchar * data, gint data_size)
{
  gint i;

  switch (encoding) {
    case ID3V2_ENCODING_ISO8859:
    case ID3V2_ENCODING_UTF8:
      for (i = 0; i < data_size; ++i) {
        if (data[i] == '\0')
          return i + 1;
      }
      break;
    case ID3V2_ENCODING_UTF16:
    case ID3V2_ENCODING_UTF16BE:
      /* we don't care about BOMs here and treat them as part of the string */
      /* Find '\0\0' terminator */
      for (i = 0; i < data_size - 1; i += 2) {
        if (data[i] == '\0' && data[i + 1] == '\0')
          return i + 2;
      }
      break;
    default:
      break;
  }

  return 0;
}

static gboolean
parse_picture_frame (ID3TagsWorking * work)
{
  GstBuffer *image = NULL;
  GstCaps *image_caps = NULL;
  gboolean is_pic_uri = FALSE;
  guint8 txt_encoding, pic_type;
  gchar *mime_str = NULL;
  gint len, datalen;

  GST_LOG ("APIC frame");

  if (work->parse_size < 1 + 1 + 1 + 1 + 1)
    goto not_enough_data;

  txt_encoding = work->parse_data[0];
  ++work->parse_data;
  --work->parse_size;

  if (!parse_id_string (work, &mime_str, &len, &datalen)) {
    return FALSE;
  }

  is_pic_uri = (mime_str != NULL && strcmp (mime_str, "-->") == 0);

  if (mime_str && *mime_str && strchr (mime_str, '/') == NULL && !is_pic_uri) {
    gchar *tmp;

    tmp = g_strdup_printf ("image/%s", mime_str);
    g_free (mime_str);
    mime_str = tmp;
  }

  if (work->parse_size < (len + 1) + 1 + 1 + 1)
    goto not_enough_data;

  work->parse_data += (len + 1);
  work->parse_size -= (len + 1);

  pic_type = work->parse_data[0];
  ++work->parse_data;
  --work->parse_size;

  GST_LOG ("APIC frame mime type    : %s", GST_STR_NULL (mime_str));
  GST_LOG ("APIC frame picture type : 0x%02x", (guint) pic_type);

  if (work->parse_size < 1 + 1)
    goto not_enough_data;

  len = scan_encoded_string (txt_encoding, (gchar *) work->parse_data,
      work->parse_size);

  if (len < 1)
    goto error;

  /* just skip the description string ... */
  GST_LOG ("Skipping description string (%d bytes in original coding)", len);

  if (work->parse_size < len + 1)
    goto not_enough_data;

  work->parse_data += len;
  work->parse_size -= len;

  GST_DEBUG ("image data is %u bytes", work->parse_size);

  if (work->parse_size <= 0)
    goto not_enough_data;

  if (is_pic_uri) {
    gchar *uri;

    uri = g_strndup ((gchar *) work->parse_data, work->parse_size);
    GST_DEBUG ("image URI: %s", uri);

    image = gst_buffer_new ();
    GST_BUFFER_MALLOCDATA (image) = (guint8 *) uri;     /* take ownership */
    GST_BUFFER_DATA (image) = (guint8 *) uri;
    GST_BUFFER_SIZE (image) = work->parse_size;

    image_caps = gst_caps_new_simple ("text/uri-list", NULL);
  } else {
    image = gst_buffer_new_and_alloc (work->parse_size);
    memcpy (GST_BUFFER_DATA (image), work->parse_data, work->parse_size);

    /* if possible use GStreamer media type rather than declared type */
    image_caps = gst_type_find_helper_for_buffer (NULL, image, NULL);
    if (image_caps) {
      GST_DEBUG ("Found GStreamer media type: %" GST_PTR_FORMAT, image_caps);
    } else if (mime_str && *mime_str) {
      GST_DEBUG ("No GStreamer media type found, using declared type");
      image_caps = gst_caps_new_simple (mime_str, NULL);
    } else {
      GST_DEBUG ("Empty declared mime type, ignoring image frame");
      image = NULL;
      image_caps = NULL;
      goto error;
    }
  }

  if (image && image_caps) {
    /* FIXME: remove #ifdef once we depend on -base >= 0.10.9 */
#ifdef GST_TYPE_TAG_IMAGE_TYPE
    if (pic_type > 0x14)
      pic_type = GST_TAG_IMAGE_TYPE_UNDEFINED;
    gst_structure_set (gst_caps_get_structure (image_caps, 0),
        "image-type", GST_TYPE_TAG_IMAGE_TYPE,
        (GstTagImageType) pic_type, NULL);
#endif

    gst_buffer_set_caps (image, image_caps);
    gst_caps_unref (image_caps);
    if (pic_type == 0x01 || pic_type == 0x02) {
      /* file icon of some sort */
      gst_tag_list_add (work->tags, GST_TAG_MERGE_APPEND,
          GST_TAG_PREVIEW_IMAGE, image, NULL);
    } else {
      gst_tag_list_add (work->tags, GST_TAG_MERGE_APPEND,
          GST_TAG_IMAGE, image, NULL);
    }
    gst_buffer_unref (image);
  }

  g_free (mime_str);
  return TRUE;

not_enough_data:
  {
    GST_DEBUG ("not enough data, skipping APIC frame");
    /* fall through to error */
  }
error:
  {
    GST_DEBUG ("problem parsing APIC frame, skipping");
    g_free (mime_str);
    return FALSE;
  }
}

#define ID3V2_RVA2_CHANNEL_MASTER  1

static gboolean
parse_relative_volume_adjustment_two (ID3TagsWorking * work)
{
  const gchar *gain_tag_name = NULL;
  const gchar *peak_tag_name = NULL;
  gdouble gain_dB, peak_val;
  guint64 peak;
  guint8 *data, chan, peak_bits;
  gchar *id;
  gint len, datalen, i;

  if (!parse_id_string (work, &id, &len, &datalen))
    return FALSE;

  if (datalen < (1 + 2 + 1)) {
    GST_WARNING ("broken RVA2 frame, data size only %d bytes", datalen);
    g_free (id);
    return FALSE;
  }

  data = work->parse_data + len + 1;
  chan = GST_READ_UINT8 (data);
  gain_dB = (gdouble) ((gint16) GST_READ_UINT16_BE (data + 1)) / 512.0;
  /* The meaning of the peak value is not defined in the ID3v2 spec. However,
   * the first/only implementation of this seems to have been in XMMS, and
   * other libs (like mutagen) seem to follow that implementation as well:
   * see http://bugs.xmms.org/attachment.cgi?id=113&action=view */
  peak_bits = GST_READ_UINT8 (data + 1 + 2);
  if (peak_bits > 64) {
    GST_WARNING ("silly peak precision of %d bits, ignoring", (gint) peak_bits);
    peak_bits = 0;
  }
  data += 1 + 2 + 1;
  datalen -= 1 + 2 + 1;
  if (peak_bits == 16) {
    peak = GST_READ_UINT16_BE (data);
  } else {
    peak = 0;
    for (i = 0; i < (GST_ROUND_UP_8 (peak_bits) / 8) && datalen > 0; ++i) {
      peak = peak << 8;
      peak |= GST_READ_UINT8 (data);
      ++data;
      --datalen;
    }
  }

  peak = peak << (64 - GST_ROUND_UP_8 (peak_bits));
  peak_val =
      gst_guint64_to_gdouble (peak) / gst_util_guint64_to_gdouble (G_MAXINT64);
  GST_LOG ("RVA2 frame: id=%s, chan=%u, adj=%.2fdB, peak_bits=%u, peak=%.2f",
      id, chan, gain_dB, (guint) peak_bits, peak_val);

  if (chan == ID3V2_RVA2_CHANNEL_MASTER && strcmp (id, "track") == 0) {
    gain_tag_name = GST_TAG_TRACK_GAIN;
    peak_tag_name = GST_TAG_TRACK_PEAK;
  } else if (chan == ID3V2_RVA2_CHANNEL_MASTER && strcmp (id, "album") == 0) {
    gain_tag_name = GST_TAG_ALBUM_GAIN;
    peak_tag_name = GST_TAG_ALBUM_PEAK;
  } else {
    GST_INFO ("Unhandled RVA2 frame id '%s' for channel %d", id, chan);
  }

  if (gain_tag_name) {
    gst_tag_list_add (work->tags, GST_TAG_MERGE_APPEND,
        gain_tag_name, gain_dB, NULL);
  }
  if (peak_tag_name && peak_bits > 0) {
    gst_tag_list_add (work->tags, GST_TAG_MERGE_APPEND,
        peak_tag_name, peak_val, NULL);
  }

  g_free (id);

  return (gain_tag_name != NULL || peak_tag_name != NULL);
}

static gboolean
id3v2_tag_to_taglist (ID3TagsWorking * work, const gchar * tag_name,
    const gchar * tag_str)
{
  GType tag_type = gst_tag_get_type (tag_name);
  GstTagList *tag_list = work->tags;

  if (tag_str == NULL)
    return FALSE;

  switch (tag_type) {
    case G_TYPE_UINT:
    {
      gint current, total;

      if (sscanf (tag_str, "%d/%d", &current, &total) == 2) {
        if (total <= 0) {
          GST_WARNING ("Ignoring invalid value for total %d in tag %s",
              total, tag_name);
        } else {
          if (strcmp (tag_name, GST_TAG_TRACK_NUMBER) == 0) {
            gst_tag_list_add (tag_list, GST_TAG_MERGE_APPEND,
                GST_TAG_TRACK_COUNT, total, NULL);
          } else if (strcmp (tag_name, GST_TAG_ALBUM_VOLUME_NUMBER) == 0) {
            gst_tag_list_add (tag_list, GST_TAG_MERGE_APPEND,
                GST_TAG_ALBUM_VOLUME_COUNT, total, NULL);
          }
        }
      } else if (sscanf (tag_str, "%d", &current) != 1) {
        /* Not an integer in the string */
        GST_WARNING ("Tag string for tag %s does not contain an integer - "
            "ignoring", tag_name);
        break;
      }

      if (current <= 0) {
        GST_WARNING ("Ignoring invalid value %d in tag %s", current, tag_name);
      } else {
        gst_tag_list_add (tag_list, GST_TAG_MERGE_APPEND, tag_name, current,
            NULL);
      }
      break;
    }
    case G_TYPE_UINT64:
    {
      guint64 tmp;

      g_assert (strcmp (tag_name, GST_TAG_DURATION) == 0);
      tmp = strtoul (tag_str, NULL, 10);
      if (tmp == 0) {
        break;
      }
      gst_tag_list_add (tag_list, GST_TAG_MERGE_APPEND,
          GST_TAG_DURATION, tmp * 1000 * 1000, NULL);
      break;
    }
    case G_TYPE_STRING:{
      if (!strcmp (tag_name, GST_TAG_GENRE)) {
        if (work->prev_genre && !strcmp (tag_str, work->prev_genre))
          break;                /* Same as the last genre */
        g_free (work->prev_genre);
        work->prev_genre = g_strdup (tag_str);
      }
      gst_tag_list_add (tag_list, GST_TAG_MERGE_APPEND,
          tag_name, tag_str, NULL);
      break;
    }

    default:{
      gchar *tmp = NULL;
      GValue src = { 0, };
      GValue dest = { 0, };

      /* Ensure that any date string is complete */
      if (tag_type == GST_TYPE_DATE) {
        guint year = 1901, month = 1, day = 1;

        /* Dates can be yyyy-MM-dd, yyyy-MM or yyyy, but we need
         * the first type */
        if (sscanf (tag_str, "%04u-%02u-%02u", &year, &month, &day) == 0)
          break;

        tmp = g_strdup_printf ("%04u-%02u-%02u", year, month, day);
        tag_str = tmp;
      }

      /* handles anything else */
      g_value_init (&src, G_TYPE_STRING);
      g_value_set_string (&src, (const gchar *) tag_str);
      g_value_init (&dest, tag_type);

      if (g_value_transform (&src, &dest)) {
        gst_tag_list_add_values (tag_list, GST_TAG_MERGE_APPEND,
            tag_name, &dest, NULL);
      } else if (tag_type == G_TYPE_DOUBLE) {
        /* replaygain tags in TXXX frames ... */
        g_value_set_double (&dest, g_strtod (tag_str, NULL));
        gst_tag_list_add_values (tag_list, GST_TAG_MERGE_KEEP,
            tag_name, &dest, NULL);
        GST_LOG ("Converted string '%s' to double %f", tag_str,
            g_value_get_double (&dest));
      } else {
        GST_WARNING ("Failed to transform tag from string to type '%s'",
            g_type_name (tag_type));
      }

      g_value_unset (&src);
      g_value_unset (&dest);
      g_free (tmp);
      break;
    }
  }

  return TRUE;
}

/* Check that an array of characters contains only digits */
static gboolean
id3v2_are_digits (const gchar * chars, gint size)
{
  gint i;

  for (i = 0; i < size; i++) {
    if (!g_ascii_isdigit (chars[i]))
      return FALSE;
  }
  return TRUE;
}

static gboolean
id3v2_genre_string_to_taglist (ID3TagsWorking * work, const gchar * tag_name,
    const gchar * tag_str, gint len)
{
  g_return_val_if_fail (tag_str != NULL, FALSE);

  /* If it's a number, it might be a defined genre */
  if (id3v2_are_digits (tag_str, len)) {
    tag_str = gst_tag_id3_genre_get (strtol (tag_str, NULL, 10));
    return id3v2_tag_to_taglist (work, tag_name, tag_str);
  }
  /* Otherwise it might be "RX" or "CR" */
  if (len == 2) {
    if (g_ascii_strncasecmp ("rx", tag_str, len) == 0)
      return id3v2_tag_to_taglist (work, tag_name, "Remix");

    if (g_ascii_strncasecmp ("cr", tag_str, len) == 0)
      return id3v2_tag_to_taglist (work, tag_name, "Cover");
  }

  /* Otherwise it's a string */
  return id3v2_tag_to_taglist (work, tag_name, tag_str);
}

static gboolean
id3v2_genre_fields_to_taglist (ID3TagsWorking * work, const gchar * tag_name,
    GArray * tag_fields)
{
  gchar *tag_str = NULL;
  gboolean result = FALSE;
  gint i;

  for (i = 0; i < tag_fields->len; i++) {
    gint len;

    tag_str = g_array_index (tag_fields, gchar *, 0);
    if (tag_str == NULL)
      continue;

    len = strlen (tag_str);
    /* Only supposed to see '(n)' type numeric genre strings in ID3 <= 2.3.0
     * but apparently we see them in 2.4.0 sometimes too */
    if (TRUE || work->hdr.version <= 0x300) {   /* <= 2.3.0 */
      /* Check for genre numbers wrapped in parentheses, possibly
       * followed by a string */
      while (len >= 2) {
        gint pos;
        gboolean found = FALSE;

        /* Double parenthesis ends the numeric genres, but we need
         * to swallow the first one so we actually output '(' */
        if (tag_str[0] == '(' && tag_str[1] == '(') {
          tag_str++;
          len--;
          break;
        }

        /* If the first char is not a parenthesis, then stop
         * looking for parenthesised genre strings */
        if (tag_str[0] != '(')
          break;

        for (pos = 1; pos < len; pos++) {
          if (tag_str[pos] == ')') {
            gchar *tmp_str;

            tmp_str = g_strndup (tag_str + 1, pos - 1);
            result |=
                id3v2_genre_string_to_taglist (work, tag_name, tmp_str,
                pos - 1);
            g_free (tmp_str);
            tag_str += pos + 1;
            len -= pos + 1;
            found = TRUE;
            break;
          }

          /* If we encounter a non-digit while searching for a closing 
           * parenthesis, we should not try and interpret this as a 
           * numeric genre string */
          if (!g_ascii_isdigit (tag_str[pos]))
            break;
        }
        if (!found)
          break;                /* There was no closing parenthesis */
      }
    }

    if (len > 0 && tag_str != NULL)
      result |= id3v2_genre_string_to_taglist (work, tag_name, tag_str, len);
  }
  return result;
}

static const gchar utf16enc[] = "UTF-16";
static const gchar utf16leenc[] = "UTF-16LE";
static const gchar utf16beenc[] = "UTF-16BE";

static gboolean
find_utf16_bom (gchar * data, const gchar ** p_in_encoding)
{
  guint16 marker = (GST_READ_UINT8 (data) << 8) | GST_READ_UINT8 (data + 1);

  switch (marker) {
    case 0xFFFE:
      *p_in_encoding = utf16leenc;
      return TRUE;
    case 0xFEFF:
      *p_in_encoding = utf16beenc;
      return TRUE;
    default:
      break;
  }
  return FALSE;
}

static void
parse_insert_string_field (guint8 encoding, gchar * data, gint data_size,
    GArray * fields)
{
  gchar *field = NULL;

  switch (encoding) {
    case ID3V2_ENCODING_UTF16:
    case ID3V2_ENCODING_UTF16BE:
    {
      const gchar *in_encode;

      if (encoding == ID3V2_ENCODING_UTF16)
        in_encode = utf16enc;
      else
        in_encode = utf16beenc;

      /* Sometimes we see strings with multiple BOM markers at the start.
       * In that case, we assume the innermost one is correct. If that fails
       * to produce valid UTF-8, we try the other endianness anyway */
      while (data_size > 2 && find_utf16_bom (data, &in_encode)) {
        data += 2;              /* skip BOM */
        data_size -= 2;
      }

      field = g_convert (data, data_size, "UTF-8", in_encode, NULL, NULL, NULL);

      if (field == NULL || g_utf8_validate (field, -1, NULL) == FALSE) {
        /* As a fallback, try interpreting UTF-16 in the other endianness */
        if (in_encode == utf16beenc)
          field = g_convert (data, data_size, "UTF-8", utf16leenc,
              NULL, NULL, NULL);
      }
    }

      break;
    case ID3V2_ENCODING_ISO8859:
      if (g_utf8_validate (data, data_size, NULL))
        field = g_strndup (data, data_size);
      else
        field = g_convert (data, data_size, "UTF-8", "ISO-8859-1",
            NULL, NULL, NULL);
      break;
    default:
      field = g_strndup (data, data_size);
      break;
  }

  if (field) {
    if (g_utf8_validate (field, -1, NULL)) {
      g_array_append_val (fields, field);
      return;
    }

    GST_DEBUG ("%s was bad UTF-8 after conversion from encoding %d. Ignoring",
        field, encoding);
    g_free (field);
  }
}

static void
parse_split_strings (guint8 encoding, gchar * data, gint data_size,
    GArray ** out_fields)
{
  GArray *fields = g_array_new (FALSE, TRUE, sizeof (gchar *));
  gint text_pos;
  gint prev = 0;

  g_return_if_fail (out_fields != NULL);

  switch (encoding) {
    case ID3V2_ENCODING_ISO8859:
      for (text_pos = 0; text_pos < data_size; text_pos++) {
        if (data[text_pos] == 0) {
          parse_insert_string_field (encoding, data + prev,
              text_pos - prev + 1, fields);
          prev = text_pos + 1;
        }
      }
      if (data_size - prev > 0 && data[prev] != 0x00) {
        parse_insert_string_field (encoding, data + prev,
            data_size - prev, fields);
      }

      break;
    case ID3V2_ENCODING_UTF8:
      for (prev = 0, text_pos = 0; text_pos < data_size; text_pos++) {
        if (data[text_pos] == '\0') {
          parse_insert_string_field (encoding, data + prev,
              text_pos - prev + 1, fields);
          prev = text_pos + 1;
        }
      }
      if (data_size - prev > 0 && data[prev] != 0x00) {
        parse_insert_string_field (encoding, data + prev,
            data_size - prev, fields);
      }
      break;
    case ID3V2_ENCODING_UTF16:
    case ID3V2_ENCODING_UTF16BE:
    {
      /* Find '\0\0' terminator */
      for (text_pos = 0; text_pos < data_size - 1; text_pos += 2) {
        if (data[text_pos] == '\0' && data[text_pos + 1] == '\0') {
          /* found a delimiter */
          parse_insert_string_field (encoding, data + prev,
              text_pos - prev + 2, fields);
          text_pos++;           /* Advance to the 2nd NULL terminator */
          prev = text_pos + 1;
          break;
        }
      }
      if (data_size - prev > 1 &&
          (data[prev] != 0x00 || data[prev + 1] != 0x00)) {
        /* There were 2 or more non-null chars left, convert those too */
        parse_insert_string_field (encoding, data + prev,
            data_size - prev, fields);
      }
      break;
    }
  }
  if (fields->len > 0)
    *out_fields = fields;
  else
    g_array_free (fields, TRUE);
}

static void
free_tag_strings (GArray * fields)
{
  if (fields) {
    gint i;
    gchar *c;

    for (i = 0; i < fields->len; i++) {
      c = g_array_index (fields, gchar *, i);
      g_free (c);
    }
    g_array_free (fields, TRUE);
  }
}
