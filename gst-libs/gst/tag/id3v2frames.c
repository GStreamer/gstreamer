/* -*- Mode: C; tab-width: 2; indent-tabs-mode: t; c-basic-offset: 2 -*- */
/* Copyright 2005 Jan Schmidt <thaytan@mad.scientist.com>
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

#ifdef HAVE_ZLIB
#include <zlib.h>
#endif

#include "id3tags.h"

GST_DEBUG_CATEGORY_EXTERN (id3demux_debug);
#define GST_CAT_DEFAULT (id3demux_debug)

static gchar *parse_comment_frame (ID3TagsWorking * work);
static GArray *parse_text_identification_frame (ID3TagsWorking * work);
static gboolean id3v2_tag_to_taglist (ID3TagsWorking * work,
    const gchar * tag_name, const gchar * tag_str);
/* Parse a single string into an array of gchar* */
static void parse_split_strings (guint8 encoding, gchar * data, gint data_size,
    GArray ** out_fields);
static void free_tag_strings (GArray * fields);
static gboolean
id3v2_genre_fields_to_taglist (ID3TagsWorking * work, const gchar * tag_name,
    GArray * tag_fields);

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
  if (tag_name == NULL)
    return FALSE;

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
    }
  } else if (!strcmp (work->frame_id, "COMM")) {
    /* Comment */
    tag_str = parse_comment_frame (work);
  } else if (!strcmp (work->frame_id, "APIC")) {
    /* Attached picture */
  } else if (!strcmp (work->frame_id, "RVA2")) {
    /* Relative volume */
  } else if (!strcmp (work->frame_id, "UFID")) {
    /* Unique file identifier */
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

static gchar *
parse_comment_frame (ID3TagsWorking * work)
{
  guint8 encoding;
  gchar language[4];
  GArray *fields = NULL;
  gchar *out_str = NULL;
  gchar *description, *text;

  if (work->parse_size < 6)
    return NULL;

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
  } else {
    if (strlen (description) > 0 && g_utf8_validate (description, -1, NULL)) {
      out_str = g_strdup_printf ("Description: %s\nComment: %s",
          description, text);
    } else {
      out_str = g_strdup (text);
    }
  }

fail:
  free_tag_strings (fields);

  return out_str;
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
      GST_LOG ("Read %d fields from Text ID frame of size %d. First is '%s'",
          fields->len, work->parse_size - 1,
          g_array_index (fields, gchar *, 0));
    } else {
      GST_LOG ("Read %d fields from Text ID frame of size %d", fields->len,
          work->parse_size - 1);
    }
  }

  return fields;
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
      guint tmp;
      gchar *check;

      tmp = strtoul ((char *) tag_str, &check, 10);

      if (strcmp (tag_name, GST_TAG_TRACK_NUMBER) == 0) {
        if (*check == '/') {
          guint total;

          check++;
          total = strtoul (check, &check, 10);
          if (*check != '\0')
            break;

          gst_tag_list_add (tag_list, GST_TAG_MERGE_APPEND,
              GST_TAG_TRACK_COUNT, total, NULL);
        }
      } else if (strcmp (tag_name, GST_TAG_ALBUM_VOLUME_NUMBER) == 0) {
        if (*check == '/') {
          guint total;

          check++;
          total = strtoul (check, &check, 10);
          if (*check != '\0')
            break;

          gst_tag_list_add (tag_list, GST_TAG_MERGE_APPEND,
              GST_TAG_ALBUM_VOLUME_COUNT, total, NULL);
        }
      }

      if (*check != '\0')
        break;

      gst_tag_list_add (tag_list, GST_TAG_MERGE_APPEND, tag_name, tmp, NULL);
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
    if (work->hdr.version <= 0x300) {   /* <= 2.3.0 */
      /* Check for genre numbers wrapped in parentheses, possibly
       * followed by a string */
      while (len >= 2) {
        gint pos;
        gboolean found = FALSE;

        /* Double parenthesis ends the numeric genres */
        if (tag_str[0] == '(' && tag_str[1] == '(')
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

static void
parse_insert_string_field (const gchar * encoding, gchar * data, gint data_size,
    GArray * fields)
{
  gchar *field;

  field = g_convert (data, data_size, "UTF-8", encoding, NULL, NULL, NULL);
  if (field && !g_utf8_validate (field, -1, NULL)) {
    GST_DEBUG ("%s was bad UTF-8. Ignoring", field);
    g_free (field);
    field = NULL;
  }
  if (field)
    g_array_append_val (fields, field);
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
          parse_insert_string_field ("ISO-8859-1", data + prev,
              text_pos - prev + 1, fields);
          prev = text_pos + 1;
        }
      }
      if (data_size - prev > 0 && data[prev] != 0x00) {
        parse_insert_string_field ("ISO-8859-1", data + prev,
            data_size - prev, fields);
      }

      break;
    case ID3V2_ENCODING_UTF8:
      for (prev = 0, text_pos = 0; text_pos < data_size; text_pos++) {
        if (data[text_pos] == '\0') {
          parse_insert_string_field ("UTF-8", data + prev,
              text_pos - prev + 1, fields);
          prev = text_pos + 1;
        }
      }
      if (data_size - prev > 0 && data[prev] != 0x00) {
        parse_insert_string_field ("UTF-8", data + prev,
            data_size - prev, fields);
      }
      break;
    case ID3V2_ENCODING_UTF16:
    case ID3V2_ENCODING_UTF16BE:
    {
      const gchar *in_encode;

      if (encoding == ID3V2_ENCODING_UTF16)
        in_encode = "UTF-16";
      else
        in_encode = "UTF-16BE";

      /* Find '\0\0' terminator */
      for (text_pos = 0; text_pos < data_size - 1; text_pos += 2) {
        if (data[text_pos] == '\0' && data[text_pos + 1] == '\0') {
          /* found a delimiter */
          parse_insert_string_field (in_encode, data + prev,
              text_pos - prev + 2, fields);
          text_pos++;           /* Advance to the 2nd NULL terminator */
          prev = text_pos + 1;
          break;
        }
      }
      if (data_size - prev > 1 &&
          (data[prev] != 0x00 || data[prev + 1] != 0x00)) {
        /* There were 2 or more non-null chars left, convert those too */
        parse_insert_string_field (in_encode, data + prev,
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
