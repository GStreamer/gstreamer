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
#include <gst/tag/tag.h>

#ifdef HAVE_ZLIB
#include <zlib.h>
#endif

#include "id3tags.h"

GST_DEBUG_CATEGORY_EXTERN (id3demux_debug);
#define GST_CAT_DEFAULT (id3demux_debug)

static gchar *parse_comment_frame (ID3TagsWorking * work);
static gchar *parse_text_identification_frame (ID3TagsWorking * work);
static gboolean id3v2_tag_to_taglist (ID3TagsWorking * work,
    const gchar * tag_name, gchar * tag_str);
static void parse_split_strings (ID3TagsWorking * work, guint8 encoding,
    gchar ** field1, gchar ** field2);

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
  } else
    work->parse_size = frame_data_size;

  if (work->frame_flags & ID3V2_FRAME_FORMAT_COMPRESSION) {
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
  } else {
    work->parse_data = work->hdr.frame_data;
  }

  if (work->frame_id[0] == 'T') {
    if (strcmp (work->frame_id, "TXXX") != 0) {
      /* Text identification frame */
      tag_str = parse_text_identification_frame (work);
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

  if (work->frame_flags & ID3V2_FRAME_FORMAT_COMPRESSION) {
    g_free (work->parse_data);
  }

  if (tag_str != NULL) {
    /* g_print ("Tag %s value %s\n", tag_name, tag_str); */
    result = id3v2_tag_to_taglist (work, tag_name, tag_str);
    g_free (tag_str);
  }

  return result;
}

static gchar *
parse_comment_frame (ID3TagsWorking * work)
{
  guint8 encoding;
  gchar language[4];
  gchar *description = NULL;
  gchar *text = NULL;
  gchar *out_str = NULL;

  if (work->parse_size < 6)
    return NULL;

  encoding = work->parse_data[0];
  language[0] = work->parse_data[1];
  language[1] = work->parse_data[2];
  language[2] = work->parse_data[3];
  language[3] = 0;

  parse_split_strings (work, encoding, &description, &text);

  if (text == NULL || description == NULL) {
    GST_ERROR ("Failed to decode comment frame");
    goto fail;
  }

  if (!g_utf8_validate (text, -1, NULL)) {
    GST_ERROR ("Converted string is not valid utf-8");
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
  g_free (description);
  g_free (text);

  return out_str;
}

static gchar *
parse_text_identification_frame (ID3TagsWorking * work)
{
  guchar encoding;
  gchar *text = NULL;

  if (work->parse_size < 2)
    return NULL;

  encoding = work->parse_data[0];

  switch (encoding) {
    case ID3V2_ENCODING_ISO8859:
      text = g_convert ((gchar *) (work->parse_data + 1),
          work->parse_size - 1, "UTF-8", "ISO-8859-1", NULL, NULL, NULL);
      break;
    case ID3V2_ENCODING_UTF8:
      text = g_strndup ((gchar *) (work->parse_data + 1), work->parse_size - 1);
      break;
    case ID3V2_ENCODING_UTF16:
      text = g_convert ((gchar *) (work->parse_data + 1),
          work->parse_size - 1, "UTF-8", "UTF-16", NULL, NULL, NULL);
      break;
    case ID3V2_ENCODING_UTF16BE:
      text = g_convert ((gchar *) (work->parse_data + 1),
          work->parse_size - 1, "UTF-8", "UTF-16BE", NULL, NULL, NULL);
      break;
  }

  if (text != NULL && !g_utf8_validate (text, -1, NULL)) {
    GST_ERROR ("Converted string is not valid utf-8");
    g_free (text);
    text = NULL;
  }

  return text;
}

static gboolean
id3v2_tag_to_taglist (ID3TagsWorking * work, const gchar * tag_name,
    gchar * tag_str)
{
  GType tag_type = gst_tag_get_type (tag_name);
  GstTagList *tag_list = work->tags;

  switch (tag_type) {
    case G_TYPE_UINT:
    {
      guint tmp;
      gchar *check;

      tmp = strtoul ((char *) tag_str, &check, 10);

      if (strcmp (tag_name, GST_TAG_DATE) == 0) {
        GDate *d;

        if (*check != '\0')
          break;
        if (tmp == 0)
          break;
        d = g_date_new_dmy (1, 1, tmp);
        tmp = g_date_get_julian (d);
        g_date_free (d);
      } else if (strcmp (tag_name, GST_TAG_TRACK_NUMBER) == 0) {
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
      tmp = strtoul ((char *) tag_str, NULL, 10);
      if (tmp == 0) {
        break;
      }
      gst_tag_list_add (tag_list, GST_TAG_MERGE_APPEND,
          GST_TAG_DURATION, tmp * 1000 * 1000, NULL);
      break;
    }
    case G_TYPE_STRING:{
      gst_tag_list_add (tag_list, GST_TAG_MERGE_APPEND,
          tag_name, (const gchar *) tag_str, NULL);
      break;
    }
      /* handles GST_TYPE_DATE and anything else */
    default:{
      GValue src = { 0, };
      GValue dest = { 0, };

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
      break;
    }
  }

  return TRUE;
}

static void
parse_split_strings (ID3TagsWorking * work, guint8 encoding,
    gchar ** field1, gchar ** field2)
{
  guint text_pos;

  *field1 = *field2 = NULL;

  switch (encoding) {
    case ID3V2_ENCODING_ISO8859:
      for (text_pos = 4; text_pos < work->parse_size - 1; text_pos++) {
        if (work->parse_data[text_pos] == 0) {
          *field1 = g_convert ((gchar *) (work->parse_data + 4),
              text_pos - 4, "UTF-8", "ISO-8859-1", NULL, NULL, NULL);
          *field2 = g_convert ((gchar *) (work->parse_data + text_pos + 5),
              work->parse_size - text_pos - 5,
              "UTF-8", "ISO-8859-1", NULL, NULL, NULL);
          break;
        }
      }
      break;
    case ID3V2_ENCODING_UTF8:
      *field1 = g_strndup ((gchar *) (work->parse_data + 4),
          work->parse_size - 4);
      text_pos = 4 + strlen (*field1) + 1;      /* Offset by one more for the null */
      if (text_pos < work->parse_size) {
        *field2 = g_strndup ((gchar *) (work->parse_data + text_pos),
            work->parse_size - text_pos);
      }
      break;
    case ID3V2_ENCODING_UTF16:
    case ID3V2_ENCODING_UTF16BE:
    {
      /* Find '\0\0' terminator */
      for (text_pos = 4; text_pos < work->parse_size - 2; text_pos++) {
        if (work->parse_data[text_pos] == 0 &&
            work->parse_data[text_pos + 1] == 0) {
          /* found our delimiter */
          if (encoding == ID3V2_ENCODING_UTF16) {
            *field1 = g_convert ((gchar *) (work->parse_data + 4),
                text_pos - 4, "UTF-8", "UTF-16", NULL, NULL, NULL);
            *field2 = g_convert ((gchar *) (work->parse_data + text_pos + 6),
                work->parse_size - text_pos - 6,
                "UTF-8", "UTF-16", NULL, NULL, NULL);
          } else {
            *field1 = g_convert ((gchar *) (work->parse_data + 4),
                text_pos - 4, "UTF-8", "UTF-16BE", NULL, NULL, NULL);
            *field2 = g_convert ((gchar *) (work->parse_data + text_pos + 6),
                work->parse_size - text_pos - 6,
                "UTF-8", "UTF-16BE", NULL, NULL, NULL);
          }
          break;
        }
      }
      break;
    }
  }
}
