/* GStreamer
 * Copyright (C) 2010 Thiago Santos <thiago.sousa.santos@collabora.co.uk>
 *
 * gstexiftag.c: library for reading / modifying exif tags
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
 * SECTION:gsttagexif
 * @short_description: tag mappings and support functions for plugins
 *                     dealing with exif tags
 * @see_also: #GstTagList
 *
 * Contains utility function to parse #GstTagList<!-- -->s from exif
 * buffers and to create exif buffers from #GstTagList<!-- -->s
 *
 * Note that next IFD fields on the created exif buffers are set to 0.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <gst/gsttagsetter.h>
#include <gst/base/gstbytewriter.h>
#include "gsttageditingprivate.h"

#include <stdlib.h>
#include <string.h>

/* Some useful constants */
#define TIFF_LITTLE_ENDIAN  0x4949
#define TIFF_BIG_ENDIAN     0x4D4D
#define TIFF_HEADER_SIZE    8
#define EXIF_TAG_ENTRY_SIZE (2 + 2 + 4 + 4)

/* Exif tag types */
#define EXIF_TYPE_BYTE       1
#define EXIF_TYPE_ASCII      2
#define EXIF_TYPE_SHORT      3
#define EXIF_TYPE_LONG       4
#define EXIF_TYPE_RATIONAL   5
#define EXIF_TYPE_UNDEFINED  7
#define EXIF_TYPE_SLONG      9
#define EXIF_TYPE_SRATIONAL 10

typedef struct _GstExifTagMatch GstExifTagMatch;
typedef struct _GstExifWriter GstExifWriter;
typedef void (*GstExifSerializationFunc) (GstExifWriter * writer,
    const GstTagList * taglist, const GstExifTagMatch * tag_map,
    gint map_index);

/*
 * Function used to deserialize tags that don't follow the usual
 * deserialization conversions. Usually those that have 'Ref' complementary
 * tags.
 *
 * Those functions receive a exif tag data in the parameters, plus the taglist
 * and the reader and buffer if they need to get more information to build
 * its tags. There are lots of parameters, but this is needed to make it
 * versatile. Explanation of them follows:
 *
 * taglist: The #GstTagList to which the found tags should be added.
 * reader: The #GstByteReader pointing to the start of the next tag entry in
 * the ifd, useful for tags that use other complementary tags.
 * byte_order: To know how to parse the data.
 * buffer: Used for fetching data that is pointed by the tags offsets
 * base_offset: Value to be subtracted from the tags data offsets to align with
 * the buffer start
 * tag_map: The ifd tag map being used in this tag parsing
 * map_index: The index in the tag map of the found tag that triggered this
 * deserialization
 * tag_id: tagtype: count: offset: offset_as_data: values from the already parsed
 * tag
 */
typedef gint (*GstExifDeserializationFunc) (GstTagList * taglist,
    GstByteReader * reader, gint byte_order, const GstBuffer * buffer,
    guint32 base_offset, const GstExifTagMatch * tag_map, gint map_index,
    guint16 tag_id, guint16 tagtype, guint32 count, guint32 offset,
    const guint8 * offset_as_data);

struct _GstExifTagMatch
{
  const gchar *gst_tag;
  guint16 exif_tag;
  guint16 exif_type;

  /* for tags that need special handling */
  guint16 complementary_tag;
  GstExifSerializationFunc serialize;
  GstExifDeserializationFunc deserialize;
};

/*
 * Holds the info and variables necessary to write
 * the exif tags properly
 */
struct _GstExifWriter
{
  GstByteWriter tagwriter;
  GstByteWriter datawriter;

  gint byte_order;
  guint tags_total;
};

static void serialize_geo_coordinate (GstExifWriter * writer,
    const GstTagList * taglist, const GstExifTagMatch * tag_map,
    gint map_index);
static gint deserialize_geo_coordinate (GstTagList * taglist,
    GstByteReader * reader, gint byte_order, const GstBuffer * buffer,
    guint32 base_offset, const GstExifTagMatch * tag_map, gint map_index,
    guint16 tag_id, guint16 tagtype, guint32 count, guint32 offset,
    const guint8 * offset_as_data);

/* FIXME copyright tag has a weird "artist\0editor\0" format that is
 * not yet handled */

#define EXIF_GPS_IFD_TAG 0x8825

/* Should be kept in ascending id order */
static const GstExifTagMatch tag_map_ifd0[] = {
  {GST_TAG_DESCRIPTION, 0x10E, EXIF_TYPE_ASCII, 0, NULL, NULL},
  {GST_TAG_DEVICE_MANUFACTURER, 0x10F, EXIF_TYPE_ASCII, 0, NULL, NULL},
  {GST_TAG_DEVICE_MODEL, 0x110, EXIF_TYPE_ASCII, 0, NULL, NULL},
  {GST_TAG_ARTIST, 0x13B, EXIF_TYPE_ASCII, 0, NULL, NULL},
  {GST_TAG_COPYRIGHT, 0x8298, EXIF_TYPE_ASCII, 0, NULL, NULL},
  {NULL, EXIF_GPS_IFD_TAG, EXIF_TYPE_LONG, 0, NULL, NULL},
  {NULL, 0, 0, 0, NULL, NULL}
};

static const GstExifTagMatch tag_map_gps[] = {
  {GST_TAG_GEO_LOCATION_LATITUDE, 0x2, EXIF_TYPE_RATIONAL, 0x1,
      serialize_geo_coordinate, deserialize_geo_coordinate},
  {GST_TAG_GEO_LOCATION_LONGITUDE, 0x4, EXIF_TYPE_RATIONAL, 0x3,
      serialize_geo_coordinate, deserialize_geo_coordinate},
#if 0
  {GST_TAG_GEO_LOCATION_ELEVATION, 0x6, EXIF_TYPE_RATIONAL, 0x5, NULL, NULL},
  {GST_TAG_GEO_LOCATION_MOVEMENT_SPEED, 0xD, EXIF_TYPE_RATIONAL, 0xC, NULL,
      NULL},
  {GST_TAG_GEO_LOCATION_MOVEMENT_DIRECTION, 0xF, EXIF_TYPE_RATIONAL, 0xE, NULL,
      NULL},
  {GST_TAG_GEO_LOCATION_CAPTURE_DIRECTION, 0x11, EXIF_TYPE_RATIONAL, 0x10,
      NULL, NULL},
#endif
  {NULL, 0, 0, 0, NULL, NULL}
};

/* GstExifWriter functions */

static void
gst_exif_writer_init (GstExifWriter * writer, gint byte_order)
{
  gst_byte_writer_init (&writer->tagwriter);
  gst_byte_writer_init (&writer->datawriter);

  writer->byte_order = byte_order;
  writer->tags_total = 0;
}

static GstBuffer *
gst_exif_writer_reset_and_get_buffer (GstExifWriter * writer)
{
  GstBuffer *header;
  GstBuffer *data;

  header = gst_byte_writer_reset_and_get_buffer (&writer->tagwriter);
  data = gst_byte_writer_reset_and_get_buffer (&writer->datawriter);

  return gst_buffer_join (header, data);
}

/*
 * Given the exif tag with the passed id, returns the map index of the tag
 * corresponding to it. If use_complementary is true, then the complementary
 * are also used in the search.
 *
 * Returns -1 if not found
 */
static gint
exif_tag_map_find_reverse (guint16 exif_tag, const GstExifTagMatch * tag_map,
    gboolean use_complementary)
{
  gint i;

  for (i = 0; tag_map[i].exif_tag != 0; i++) {
    if (exif_tag == tag_map[i].exif_tag || (use_complementary &&
            exif_tag == tag_map[i].complementary_tag)) {
      return i;
    }
  }
  return -1;
}

static gboolean
gst_tag_list_has_ifd_tags (const GstTagList * taglist,
    const GstExifTagMatch * tag_map)
{
  gint i;

  for (i = 0; tag_map[i].exif_tag != 0; i++) {
    if (tag_map[i].gst_tag == NULL) {
      if (tag_map[i].exif_tag == EXIF_GPS_IFD_TAG &&
          gst_tag_list_has_ifd_tags (taglist, tag_map_gps))
        return TRUE;
    }

    if (gst_tag_list_get_value_index (taglist, tag_map[i].gst_tag, 0)) {
      return TRUE;
    }
  }
  return FALSE;
}

/*
 * Writes the tag entry.
 *
 * The tag entry is the tag id, the tag type,
 * the count and the offset.
 *
 * The offset is the on the amount of data writen so far, as one
 * can't predict the total bytes that the tag entries will take.
 * This means those fields requires being updated later.
 */
static void
gst_exif_writer_write_tag_header (GstExifWriter * writer,
    guint16 exif_tag, guint16 exif_type, guint32 count, guint32 offset,
    gboolean is_data)
{
  GST_DEBUG ("Writing tag entry: id %x, type %u, count %u, offset %u",
      exif_tag, exif_type, count, offset);

  if (writer->byte_order == G_LITTLE_ENDIAN) {
    gst_byte_writer_put_uint16_le (&writer->tagwriter, exif_tag);
    gst_byte_writer_put_uint16_le (&writer->tagwriter, exif_type);
    gst_byte_writer_put_uint32_le (&writer->tagwriter, count);
    gst_byte_writer_put_uint32_le (&writer->tagwriter, offset);
  } else if (writer->byte_order == G_BIG_ENDIAN) {
    gst_byte_writer_put_uint16_be (&writer->tagwriter, exif_tag);
    gst_byte_writer_put_uint16_be (&writer->tagwriter, exif_type);
    gst_byte_writer_put_uint32_be (&writer->tagwriter, count);
    if (is_data) {
      gst_byte_writer_put_uint32_le (&writer->tagwriter, offset);
    } else {
      gst_byte_writer_put_uint32_be (&writer->tagwriter, offset);
    }
  } else {
    g_assert_not_reached ();
  }

  writer->tags_total++;
}

static void
gst_exif_writer_write_rational_data (GstExifWriter * writer, guint32 frac_n,
    guint32 frac_d)
{
  if (writer->byte_order == G_LITTLE_ENDIAN) {
    gst_byte_writer_put_uint32_le (&writer->datawriter, frac_n);
    gst_byte_writer_put_uint32_le (&writer->datawriter, frac_d);
  } else {
    gst_byte_writer_put_uint32_be (&writer->datawriter, frac_n);
    gst_byte_writer_put_uint32_be (&writer->datawriter, frac_d);
  }
}

static void
write_exif_ascii_tag (GstExifWriter * writer, guint16 tag, const gchar * str)
{
  gint size;
  guint32 offset;

  size = strlen (str) + 1;

  if (size > 4) {
    /* we only use the data offset here, later we add up the
     * resulting tag headers offset and the base offset */
    offset = gst_byte_writer_get_size (&writer->datawriter);
    gst_exif_writer_write_tag_header (writer, tag, EXIF_TYPE_ASCII,
        size, offset, FALSE);
    gst_byte_writer_put_string (&writer->datawriter, str);
  } else {
    /* small enough to go in the offset */
    memcpy ((guint8 *) & offset, str, size);
    gst_exif_writer_write_tag_header (writer, tag, EXIF_TYPE_ASCII,
        size, offset, TRUE);
  }
}

static void
write_exif_ascii_tag_from_taglist (GstExifWriter * writer,
    const GstTagList * taglist, const gchar * gst_tag,
    const GstExifTagMatch * tag_map, gint map_index)
{
  gchar *str = NULL;
  gboolean cleanup = FALSE;
  const GValue *value;
  gint tag_size = gst_tag_list_get_tag_size (taglist, gst_tag);

  if (tag_size != 1) {
    /* FIXME support this by serializing them with a ','? */
    GST_WARNING ("Multiple string tags not supported yet");
    return;
  }

  value = gst_tag_list_get_value_index (taglist, gst_tag, 0);

  /* do some conversion if needed */
  switch (G_VALUE_TYPE (value)) {
    case G_TYPE_STRING:
      str = (gchar *) g_value_get_string (value);
      break;
    default:
      GST_WARNING ("Conversion from %s to ascii string not supported",
          G_VALUE_TYPE_NAME (value));
      break;
  }

  if (str == NULL)
    return;

  write_exif_ascii_tag (writer, tag_map[map_index].exif_tag, str);
  if (cleanup)
    g_free (str);
}

static void
write_exif_tag_from_taglist (GstExifWriter * writer, const GstTagList * taglist,
    const GstExifTagMatch * tag_map, gint map_index)
{
  GST_DEBUG ("Writing tag %s", tag_map[map_index].gst_tag);

  /* check for special handling */
  if (tag_map[map_index].serialize) {
    tag_map[map_index].serialize (writer, taglist, tag_map, map_index);
    return;
  }

  switch (tag_map[map_index].exif_type) {
    case EXIF_TYPE_ASCII:
      write_exif_ascii_tag_from_taglist (writer, taglist,
          tag_map[map_index].gst_tag, tag_map, map_index);
      break;
    default:
      GST_WARNING ("Unhandled tag type %d", tag_map[map_index].exif_type);
  }
}

static void
gst_exif_tag_rewrite_offsets (GstExifWriter * writer, guint32 base_offset)
{
  guint32 offset;

  GST_LOG ("Rewriting tag entries offsets");

  offset = gst_byte_writer_get_size (&writer->tagwriter);
  while (gst_byte_writer_get_pos (&writer->tagwriter) <
      gst_byte_writer_get_size (&writer->tagwriter)) {
    guint16 type;
    guint32 cur_offset;
    GstByteReader *reader;
    gint byte_size = 0;
    guint32 count = 0;
    guint16 tag_id;

    reader = (GstByteReader *) & writer->tagwriter;

    /* read the type */
    if (writer->byte_order == G_LITTLE_ENDIAN) {
      if (!gst_byte_reader_get_uint16_le (reader, &tag_id))
        break;
      if (!gst_byte_reader_get_uint16_le (reader, &type))
        break;
      if (!gst_byte_reader_get_uint32_le (reader, &count))
        break;
    } else if (writer->byte_order == G_BIG_ENDIAN) {
      if (!gst_byte_reader_get_uint16_be (reader, &tag_id))
        break;
      if (!gst_byte_reader_get_uint16_be (reader, &type))
        break;
      if (!gst_byte_reader_get_uint32_be (reader, &count))
        break;
    } else {
      GST_WARNING ("Unexpected endianness");
      break;
    }

    switch (type) {
      case EXIF_TYPE_BYTE:
      case EXIF_TYPE_ASCII:
      case EXIF_TYPE_UNDEFINED:
        byte_size = count;
        break;
      case EXIF_TYPE_SHORT:
        byte_size = count * 2;  /* 2 bytes */
        break;
      case EXIF_TYPE_LONG:
      case EXIF_TYPE_SLONG:
        byte_size = count * 4;  /* 4 bytes */
        break;
      case EXIF_TYPE_RATIONAL:
      case EXIF_TYPE_SRATIONAL:
        byte_size = count * 8;  /* 8 bytes */
        break;
      default:
        g_assert_not_reached ();
        break;
    }

    /* adjust the offset if needed */
    if (byte_size > 4 || tag_id == EXIF_GPS_IFD_TAG) {
      if (writer->byte_order == G_LITTLE_ENDIAN) {
        if (gst_byte_reader_peek_uint32_le (reader, &cur_offset)) {
          gst_byte_writer_put_uint32_le (&writer->tagwriter,
              cur_offset + offset + base_offset);
        }
      } else if (writer->byte_order == G_BIG_ENDIAN) {
        if (gst_byte_reader_peek_uint32_be (reader, &cur_offset)) {
          gst_byte_writer_put_uint32_be (&writer->tagwriter,
              cur_offset + offset + base_offset);
        }
      } else {
        GST_WARNING ("Unexpected endianness");
      }
      GST_DEBUG ("Rewriting tag offset from %u to (%u + %u + %u) %u",
          cur_offset, cur_offset, offset, base_offset,
          cur_offset + offset + base_offset);
    } else {
      gst_byte_reader_skip (reader, 4);
      GST_DEBUG ("No need to rewrite tag offset");
    }
  }
}

static void
parse_exif_ascii_tag (GstTagList * taglist, const GstBuffer * buffer,
    gint byte_order, const gchar * gst_tag, guint32 count, guint32 offset,
    const guint8 * offset_as_data, guint32 base_offset)
{
  gchar *str;
  guint32 real_offset;

  if (count > 4) {
    if (offset < base_offset) {
      GST_WARNING ("Offset is smaller (%u) than base offset (%u)", offset,
          base_offset);
      return;
    }

    real_offset = offset - base_offset;
    if (real_offset >= GST_BUFFER_SIZE (buffer)) {
      GST_WARNING ("Invalid offset %u for buffer of size %u, not adding tag %s",
          real_offset, GST_BUFFER_SIZE (buffer), gst_tag);
      return;
    }

    str = g_strndup ((gchar *) (GST_BUFFER_DATA (buffer) + real_offset), count);
  } else {
    str = g_strndup ((gchar *) offset_as_data, count);
  }
  gst_tag_list_add (taglist, GST_TAG_MERGE_REPLACE, gst_tag, str, NULL);
  g_free (str);
}

static GstBuffer *
write_exif_ifd (const GstTagList * taglist, gboolean byte_order,
    guint32 base_offset, const GstExifTagMatch * tag_map)
{
  GstExifWriter writer;
  gint i;

  GST_DEBUG ("Formatting taglist %p as exif buffer. Byte order: %d, "
      "base_offset: %u", taglist, byte_order, base_offset);

  g_assert (byte_order == G_LITTLE_ENDIAN || byte_order == G_BIG_ENDIAN);

  if (!gst_tag_list_has_ifd_tags (taglist, tag_map)) {
    GST_DEBUG ("No tags for this ifd");
    return NULL;
  }

  gst_exif_writer_init (&writer, byte_order);

  /* write tag number as 0 */
  gst_byte_writer_put_uint16_le (&writer.tagwriter, 0);

  /* write both tag headers and data
   * in ascending id order */

  for (i = 0; tag_map[i].exif_tag != 0; i++) {

    /* special cases have NULL gst tag */
    if (tag_map[i].gst_tag == NULL) {
      GstBuffer *inner_ifd = NULL;
      const GstExifTagMatch *inner_tag_map = NULL;

      GST_LOG ("Inner ifd tag: %x", tag_map[i].exif_tag);

      if (tag_map[i].exif_tag == EXIF_GPS_IFD_TAG) {
        inner_tag_map = tag_map_gps;
      }

      if (inner_tag_map) {
        /* The base offset for this inner ifd is the sum of:
         * - the current base offset
         * - the total tag data of current this ifd
         * - the total data of the current ifd
         * - its own tag entry length still to be writen (12)
         * - 4 bytes for the next ifd entry still to be writen
         */
        inner_ifd = write_exif_ifd (taglist, byte_order, base_offset +
            gst_byte_writer_get_size (&writer.tagwriter) +
            gst_byte_writer_get_size (&writer.datawriter) + 12 + 4,
            inner_tag_map);
      }

      if (inner_ifd) {
        GST_DEBUG ("Adding inner ifd: %x", tag_map[i].exif_tag);
        gst_exif_writer_write_tag_header (&writer, tag_map[i].exif_tag,
            EXIF_TYPE_LONG, 1,
            gst_byte_writer_get_size (&writer.datawriter), FALSE);
        gst_byte_writer_put_data (&writer.datawriter,
            GST_BUFFER_DATA (inner_ifd), GST_BUFFER_SIZE (inner_ifd));
        gst_buffer_unref (inner_ifd);
      }
      continue;
    }

    GST_LOG ("Checking tag %s", tag_map[i].gst_tag);
    if (gst_tag_list_get_value_index (taglist, tag_map[i].gst_tag, 0) == NULL)
      continue;

    write_exif_tag_from_taglist (&writer, taglist, tag_map, i);
  }

  /* FIXME */
  /* Add the next IFD offset, we just set it to 0 because
   * there is no easy way to predict what it is going to be.
   * The user might rewrite the value if needed */
  gst_byte_writer_put_uint32_le (&writer.tagwriter, 0);

  /* write the number of tags */
  gst_byte_writer_set_pos (&writer.tagwriter, 0);
  if (writer.byte_order == G_LITTLE_ENDIAN)
    gst_byte_writer_put_uint16_le (&writer.tagwriter, writer.tags_total);
  else
    gst_byte_writer_put_uint16_be (&writer.tagwriter, writer.tags_total);

  /* now that we know the tag headers size, we can add the offsets */
  gst_exif_tag_rewrite_offsets (&writer, base_offset);

  return gst_exif_writer_reset_and_get_buffer (&writer);
}

static gboolean
parse_exif_ifd (GstTagList * taglist, const GstBuffer * buffer, gint buf_offset,
    const GstExifTagMatch * tag_map, gint byte_order, guint32 base_offset)
{
  GstByteReader reader;
  guint16 entries = 0;
  guint16 i;

  g_return_val_if_fail (byte_order == G_LITTLE_ENDIAN
      || byte_order == G_BIG_ENDIAN, FALSE);

  gst_byte_reader_init_from_buffer (&reader, buffer);
  if (!gst_byte_reader_set_pos (&reader, buf_offset)) {
    GST_WARNING ("Buffer offset invalid when parsing exif ifd");
    return FALSE;
  }

  /* read the IFD entries number */
  if (byte_order == G_LITTLE_ENDIAN) {
    if (!gst_byte_reader_get_uint16_le (&reader, &entries))
      goto read_error;
  } else {
    if (!gst_byte_reader_get_uint16_be (&reader, &entries))
      goto read_error;
  }
  GST_DEBUG ("Read number of entries: %u", entries);

  /* iterate over the buffer and find the tags and stuff */
  for (i = 0; i < entries; i++) {
    guint16 tagid;
    guint16 tagtype;
    guint32 count;
    guint32 offset;
    const guint8 *offset_as_data;
    gint map_index;

    GST_LOG ("Reading entry: %u", i);

    /* read the fields */
    if (byte_order == G_LITTLE_ENDIAN) {
      if (!gst_byte_reader_get_uint16_le (&reader, &tagid) ||
          !gst_byte_reader_get_uint16_le (&reader, &tagtype) ||
          !gst_byte_reader_get_uint32_le (&reader, &count) ||
          !gst_byte_reader_get_data (&reader, 4, &offset_as_data)) {
        goto read_error;
      }
      offset = GST_READ_UINT32_LE (offset_as_data);
    } else {
      if (!gst_byte_reader_get_uint16_be (&reader, &tagid) ||
          !gst_byte_reader_get_uint16_be (&reader, &tagtype) ||
          !gst_byte_reader_get_uint32_be (&reader, &count) ||
          !gst_byte_reader_get_data (&reader, 4, &offset_as_data)) {
        goto read_error;
      }
      offset = GST_READ_UINT32_BE (offset_as_data);
    }

    GST_DEBUG ("Parsed tag: id 0x%x, type %u, count %u, offset %u (0x%x)",
        tagid, tagtype, count, offset, offset);

    map_index = exif_tag_map_find_reverse (tagid, tag_map, TRUE);
    if (map_index == -1) {
      GST_WARNING ("Unmapped exif tag: 0x%x", tagid);
      continue;
    }

    /* inner ifd tags handling */
    if (tagid == EXIF_GPS_IFD_TAG) {
      i += parse_exif_ifd (taglist, buffer, offset - base_offset,
          tag_map_gps, byte_order, base_offset);

      continue;
    }

    /* tags that need specialized deserialization */
    if (tag_map[map_index].deserialize) {
      tag_map[map_index].deserialize (taglist, &reader, byte_order, buffer,
          base_offset, tag_map, map_index, tagid, tagtype, count, offset,
          offset_as_data);
      continue;
    }

    switch (tagtype) {
      case EXIF_TYPE_ASCII:
        parse_exif_ascii_tag (taglist, buffer, byte_order,
            tag_map[map_index].gst_tag, count, offset, offset_as_data,
            base_offset);
        break;
      default:
        GST_WARNING ("Unhandled tag type: %u", tagtype);
        break;
    }
  }

  return TRUE;

read_error:
  {
    GST_WARNING ("Failed to parse the exif ifd");
    return FALSE;
  }
}

/**
 * gst_tag_list_to_exif_buffer:
 * @taglist: The taglist
 * @byte_order: byte order used in writing (G_LITTLE_ENDIAN or G_BIG_ENDIAN)
 * @base_offset: Offset from the tiff header first byte
 *
 * Formats the tags in taglist on exif format. The resulting buffer contains
 * the tags IFD and is followed by the data pointed by the tag entries.
 *
 * Returns: A GstBuffer containing the tag entries followed by the tag data
 *
 * Since: 0.10.30
 */
GstBuffer *
gst_tag_list_to_exif_buffer (const GstTagList * taglist, gint byte_order,
    guint32 base_offset)
{
  return write_exif_ifd (taglist, byte_order, base_offset, tag_map_ifd0);
}

/**
 * gst_tag_list_to_exif_buffer_with_tiff_header:
 * @taglist: The taglist
 *
 * Formats the tags in taglist into exif structure, a tiff header
 * is put in the beginning of the buffer.
 *
 * Returns: A GstBuffer containing the data
 *
 * Since: 0.10.30
 */
GstBuffer *
gst_tag_list_to_exif_buffer_with_tiff_header (const GstTagList * taglist)
{
  GstBuffer *ifd;
  GstByteWriter writer;
  guint size;

  ifd = gst_tag_list_to_exif_buffer (taglist, G_BYTE_ORDER, 8);
  if (ifd == NULL) {
    GST_WARNING ("Failed to create exif buffer");
    return NULL;
  }
  size = TIFF_HEADER_SIZE + GST_BUFFER_SIZE (ifd);

  /* TODO what is the correct endianness here? */
  gst_byte_writer_init_with_size (&writer, size, FALSE);
  /* TIFF header */
  if (G_BYTE_ORDER == G_LITTLE_ENDIAN) {
    gst_byte_writer_put_uint16_le (&writer, TIFF_LITTLE_ENDIAN);
    gst_byte_writer_put_uint16_le (&writer, 42);
    gst_byte_writer_put_uint32_le (&writer, 8);
  } else {
    gst_byte_writer_put_uint16_be (&writer, TIFF_BIG_ENDIAN);
    gst_byte_writer_put_uint16_be (&writer, 42);
    gst_byte_writer_put_uint32_be (&writer, 8);
  }
  if (!gst_byte_writer_put_data (&writer, GST_BUFFER_DATA (ifd),
          GST_BUFFER_SIZE (ifd))) {
    GST_WARNING ("Byte writer size mismatch");
    /* reaching here is a programming error because we should have a buffer
     * large enough */
    g_assert_not_reached ();
    gst_buffer_unref (ifd);
    gst_byte_writer_reset (&writer);
    return NULL;
  }
  gst_buffer_unref (ifd);
  return gst_byte_writer_reset_and_get_buffer (&writer);
}

/**
 * gst_tag_list_from_exif_buffer:
 * @buffer: The exif buffer
 * @byte_order: byte order of the data
 * @base_offset: Offset from the tiff header to this buffer
 *
 * Parses the IFD and IFD tags data contained in the buffer and puts it
 * on a taglist. The base_offset is used to subtract from the offset in
 * the tag entries and be able to get the offset relative to the buffer
 * start
 *
 * Returns: The parsed taglist
 *
 * Since: 0.10.30
 */
GstTagList *
gst_tag_list_from_exif_buffer (const GstBuffer * buffer, gint byte_order,
    guint32 base_offset)
{
  GstTagList *taglist = NULL;
  g_return_val_if_fail (byte_order == G_LITTLE_ENDIAN
      || byte_order == G_BIG_ENDIAN, NULL);

  taglist = gst_tag_list_new ();

  if (!parse_exif_ifd (taglist, buffer, 0, tag_map_ifd0, byte_order,
          base_offset))
    goto read_error;

  return taglist;

read_error:
  {
    if (taglist)
      gst_tag_list_free (taglist);
    GST_WARNING ("Failed to parse the exif buffer");
    return NULL;
  }
}

/**
 * gst_tag_list_from_exif_buffer_with_tiff_header:
 * @buffer: The exif buffer
 *
 * Parses the exif tags starting with a tiff header structure.
 *
 * Returns: The taglist
 *
 * Since: 0.10.30
 */
GstTagList *
gst_tag_list_from_exif_buffer_with_tiff_header (const GstBuffer * buffer)
{
  GstByteReader reader;
  guint16 fortytwo;
  guint16 endianness;
  guint32 offset;
  GstTagList *taglist = NULL;
  GstBuffer *subbuffer;

  GST_LOG ("Parsing exif tags with tiff header of size %u",
      GST_BUFFER_SIZE (buffer));

  gst_byte_reader_init_from_buffer (&reader, buffer);

  GST_LOG ("Parsing the tiff header");
  if (!gst_byte_reader_get_uint16_be (&reader, &endianness)) {
    goto byte_reader_fail;
  }

  if (endianness == TIFF_LITTLE_ENDIAN) {
    if (!gst_byte_reader_get_uint16_le (&reader, &fortytwo) ||
        !gst_byte_reader_get_uint32_le (&reader, &offset))
      goto byte_reader_fail;
  } else if (endianness == TIFF_BIG_ENDIAN) {
    if (!gst_byte_reader_get_uint16_be (&reader, &fortytwo) ||
        !gst_byte_reader_get_uint32_be (&reader, &offset))
      goto byte_reader_fail;
  } else {
    GST_WARNING ("Invalid endianness number %u", endianness);
    return NULL;
  }

  if (fortytwo != 42) {
    GST_WARNING ("Invalid magic number %u, should be 42", fortytwo);
    return NULL;
  }

  subbuffer = gst_buffer_new_and_alloc (GST_BUFFER_SIZE (buffer) -
      (TIFF_HEADER_SIZE - 2));
  memcpy (GST_BUFFER_DATA (subbuffer),
      GST_BUFFER_DATA (buffer) + TIFF_HEADER_SIZE,
      GST_BUFFER_SIZE (buffer) - TIFF_HEADER_SIZE);

  taglist = gst_tag_list_from_exif_buffer (subbuffer,
      endianness == TIFF_LITTLE_ENDIAN ? G_LITTLE_ENDIAN : G_BIG_ENDIAN, 8);

  gst_buffer_unref (subbuffer);
  return taglist;

byte_reader_fail:
  {
    GST_WARNING ("Failed to read values from buffer");
    return NULL;
  }
}

/* special serialization functions */
static void
serialize_geo_coordinate (GstExifWriter * writer, const GstTagList * taglist,
    const GstExifTagMatch * tag_map, gint map_index)
{
  gboolean latitude;
  gdouble value;
  gint degrees;
  gint minutes;
  gint seconds;
  guint32 offset;

  latitude = tag_map[map_index].exif_tag == 0x2;        /* exif tag for latitude */
  if (!gst_tag_list_get_double (taglist, tag_map[map_index].gst_tag, &value)) {
    GST_WARNING ("Failed to get double from tag list for tag: %s",
        tag_map[map_index].gst_tag);
    return;
  }

  /* first write the Latitude or Longitude Ref */
  if (latitude) {
    if (value >= 0) {
      write_exif_ascii_tag (writer, tag_map[map_index].complementary_tag, "N");
    } else {
      value *= -1;
      write_exif_ascii_tag (writer, tag_map[map_index].complementary_tag, "S");
    }
  } else {
    if (value >= 0) {
      write_exif_ascii_tag (writer, tag_map[map_index].complementary_tag, "E");
    } else {
      value *= -1;
      write_exif_ascii_tag (writer, tag_map[map_index].complementary_tag, "W");
    }
  }

  /* now write the degrees stuff */
  GST_LOG ("Converting geo location %lf to degrees", value);
  degrees = (gint) value;
  value -= degrees;
  minutes = (gint) (value * 60);
  value = (value * 60) - minutes;
  seconds = (gint) (value * 60);
  GST_LOG ("Converted geo location to %d.%d'%d'' degrees", degrees,
      minutes, seconds);

  offset = gst_byte_writer_get_size (&writer->datawriter);
  gst_exif_writer_write_tag_header (writer, tag_map[map_index].exif_tag,
      EXIF_TYPE_RATIONAL, 3, offset, FALSE);
  gst_exif_writer_write_rational_data (writer, degrees, 1);
  gst_exif_writer_write_rational_data (writer, minutes, 1);
  gst_exif_writer_write_rational_data (writer, seconds, 1);
}

static gint
deserialize_geo_coordinate (GstTagList * taglist,
    GstByteReader * reader, gint byte_order, const GstBuffer * buffer,
    guint32 base_offset, const GstExifTagMatch * tag_map, gint map_index,
    guint16 tag_id, guint16 tagtype, guint32 count, guint32 offset,
    const guint8 * offset_as_data)
{
  GstByteReader fractions_reader;
  gint multiplier;
  guint16 next_tag_id;
  guint16 next_tag_type;
  guint32 next_count;
  guint32 next_offset;
  gint ret = 0;
  /* for the conversion */
  guint32 degrees_n;
  guint32 degrees_d;
  guint32 minutes_n;
  guint32 minutes_d;
  guint32 seconds_n;
  guint32 seconds_d;
  gdouble degrees;
  gdouble minutes;
  gdouble seconds;

  GST_LOG ("Starting to parse %s tag in exif 0x%x", tag_map[map_index].gst_tag,
      tag_map[map_index].exif_tag);

  if (tag_map[map_index].complementary_tag != tag_id) {
    /* First should come the 'Ref' tags */
    GST_WARNING ("Tag %d is not the 'Ref' tag for latitude nor longitude",
        tag_id);
    return ret;
  }

  if (offset_as_data[0] == 'N' || offset_as_data[0] == 'E') {
    multiplier = 1;
  } else if (offset_as_data[0] == 'S' || offset_as_data[0] == 'W') {
    multiplier = -1;
  } else {
    GST_WARNING ("Invalid LatitudeRef or LongitudeRef %c", offset_as_data[0]);
    return ret;
  }

  /* now read the following tag that must be the latitude or longitude */
  if (byte_order == G_LITTLE_ENDIAN) {
    if (!gst_byte_reader_peek_uint16_le (reader, &next_tag_id))
      goto reader_fail;
  } else {
    if (!gst_byte_reader_peek_uint16_be (reader, &next_tag_id))
      goto reader_fail;
  }

  if (tag_map[map_index].exif_tag != next_tag_id) {
    GST_WARNING ("This is not a geo cordinate tag");
    return ret;
  }

  /* read the remaining tag entry data */
  gst_byte_reader_skip (reader, 2);
  if (byte_order == G_LITTLE_ENDIAN) {
    if (!gst_byte_reader_get_uint16_le (reader, &next_tag_type) ||
        !gst_byte_reader_get_uint32_le (reader, &next_count) ||
        !gst_byte_reader_get_uint32_le (reader, &next_offset)) {
      ret = -1;
      goto reader_fail;
    }
  } else {
    if (!gst_byte_reader_get_uint16_be (reader, &next_tag_type) ||
        !gst_byte_reader_get_uint32_be (reader, &next_count) ||
        !gst_byte_reader_get_uint32_be (reader, &next_offset)) {
      ret = -1;
      goto reader_fail;
    }
  }

  ret = 1;

  /* some checking */
  if (next_tag_type != EXIF_TYPE_RATIONAL) {
    GST_WARNING ("Invalid type %d for geo coordinate (latitude/longitude)",
        next_tag_type);
    return ret;
  }
  if (next_count != 3) {
    GST_WARNING ("Geo coordinate should use 3 fractions, we have %u",
        next_count);
    return ret;
  }

  /* now parse the fractions */
  gst_byte_reader_init_from_buffer (&fractions_reader, buffer);
  if (!gst_byte_reader_set_pos (&fractions_reader, next_offset - base_offset))
    goto reader_fail;

  if (byte_order == G_LITTLE_ENDIAN) {
    if (!gst_byte_reader_get_uint32_le (&fractions_reader, &degrees_n) ||
        !gst_byte_reader_get_uint32_le (&fractions_reader, &degrees_d) ||
        !gst_byte_reader_get_uint32_le (&fractions_reader, &minutes_n) ||
        !gst_byte_reader_get_uint32_le (&fractions_reader, &minutes_d) ||
        !gst_byte_reader_get_uint32_le (&fractions_reader, &seconds_n) ||
        !gst_byte_reader_get_uint32_le (&fractions_reader, &seconds_d))
      goto reader_fail;
  } else {
    if (!gst_byte_reader_get_uint32_be (&fractions_reader, &degrees_n) ||
        !gst_byte_reader_get_uint32_be (&fractions_reader, &degrees_d) ||
        !gst_byte_reader_get_uint32_be (&fractions_reader, &minutes_n) ||
        !gst_byte_reader_get_uint32_be (&fractions_reader, &minutes_d) ||
        !gst_byte_reader_get_uint32_be (&fractions_reader, &seconds_n) ||
        !gst_byte_reader_get_uint32_be (&fractions_reader, &seconds_d))
      goto reader_fail;
  }

  GST_DEBUG ("Read degrees fraction for tag %s: %u/%u %u/%u %u/%u",
      tag_map[map_index].gst_tag, degrees_n, degrees_d, minutes_n, minutes_d,
      seconds_n, seconds_d);

  gst_util_fraction_to_double (degrees_n, degrees_d, &degrees);
  gst_util_fraction_to_double (minutes_n, minutes_d, &minutes);
  gst_util_fraction_to_double (seconds_n, seconds_d, &seconds);

  minutes += seconds / 60;
  degrees += minutes / 60;
  degrees *= multiplier;

  GST_DEBUG ("Adding %s tag: %lf", tag_map[map_index].gst_tag, degrees);
  gst_tag_list_add (taglist, GST_TAG_MERGE_REPLACE, tag_map[map_index].gst_tag,
      degrees, NULL);

  return ret;

reader_fail:
  GST_WARNING ("Failed to read fields from buffer (too short?)");
  return ret;
}
