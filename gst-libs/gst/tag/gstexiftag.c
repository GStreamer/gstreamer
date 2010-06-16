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
typedef struct _GstExifReader GstExifReader;
typedef struct _GstExifTagData GstExifTagData;

typedef void (*GstExifSerializationFunc) (GstExifWriter * writer,
    const GstTagList * taglist, const GstExifTagMatch * exiftag);

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
 * exif_reader: The #GstExifReader with the reading parameter and taglist for
 * results.
 * reader: The #GstByteReader pointing to the start of the next tag entry in
 * the ifd, useful for tags that use other complementary tags.
 * the buffer start
 * exiftag: The #GstExifTagMatch that contains this tag info
 * tagdata: values from the already parsed tag
 */
typedef gint (*GstExifDeserializationFunc) (GstExifReader * exif_reader,
    GstByteReader * reader, const GstExifTagMatch * exiftag,
    GstExifTagData * tagdata);

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

struct _GstExifTagData
{
  guint16 tag;
  guint16 tag_type;
  guint32 count;
  guint32 offset;
  const guint8 *offset_as_data;
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

struct _GstExifReader
{
  GstTagList *taglist;
  const GstBuffer *buffer;
  guint32 base_offset;
  gint byte_order;
};

static void serialize_orientation (GstExifWriter * writer,
    const GstTagList * taglist, const GstExifTagMatch * exiftag);
static gint deserialize_orientation (GstExifReader * exif_reader,
    GstByteReader * reader, const GstExifTagMatch * exiftag,
    GstExifTagData * tagdata);

static void serialize_geo_coordinate (GstExifWriter * writer,
    const GstTagList * taglist, const GstExifTagMatch * exiftag);
static gint deserialize_geo_coordinate (GstExifReader * exif_reader,
    GstByteReader * reader, const GstExifTagMatch * exiftag,
    GstExifTagData * tagdata);

static void serialize_geo_direction (GstExifWriter * writer,
    const GstTagList * taglist, const GstExifTagMatch * exiftag);
static gint deserialize_geo_direction (GstExifReader * exif_reader,
    GstByteReader * reader, const GstExifTagMatch * exiftag,
    GstExifTagData * tagdata);

static void serialize_geo_elevation (GstExifWriter * writer,
    const GstTagList * taglist, const GstExifTagMatch * exiftag);
static gint deserialize_geo_elevation (GstExifReader * exif_reader,
    GstByteReader * reader, const GstExifTagMatch * exiftag,
    GstExifTagData * tagdata);

static void serialize_speed (GstExifWriter * writer,
    const GstTagList * taglist, const GstExifTagMatch * exiftag);
static gint deserialize_speed (GstExifReader * exif_reader,
    GstByteReader * reader, const GstExifTagMatch * exiftag,
    GstExifTagData * tagdata);


/* FIXME copyright tag has a weird "artist\0editor\0" format that is
 * not yet handled */

#define EXIF_GPS_IFD_TAG 0x8825

/* useful macros for speed tag */
#define METERS_PER_SECOND_TO_KILOMETERS_PER_HOUR (3.6)
#define KILOMETERS_PER_HOUR_TO_METERS_PER_SECOND (1/3.6)
#define MILES_PER_HOUR_TO_METERS_PER_SECOND (0.44704)
#define KNOTS_TO_METERS_PER_SECOND (0.514444)

/* Should be kept in ascending id order */
static const GstExifTagMatch tag_map_ifd0[] = {
  {GST_TAG_DESCRIPTION, 0x10E, EXIF_TYPE_ASCII, 0, NULL, NULL},
  {GST_TAG_DEVICE_MANUFACTURER, 0x10F, EXIF_TYPE_ASCII, 0, NULL, NULL},
  {GST_TAG_DEVICE_MODEL, 0x110, EXIF_TYPE_ASCII, 0, NULL, NULL},
  {GST_TAG_IMAGE_ORIENTATION, 0x112, EXIF_TYPE_SHORT, 0, serialize_orientation,
      deserialize_orientation},
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
  {GST_TAG_GEO_LOCATION_ELEVATION, 0x6, EXIF_TYPE_RATIONAL, 0x5,
      serialize_geo_elevation, deserialize_geo_elevation},
  {GST_TAG_GEO_LOCATION_MOVEMENT_SPEED, 0xD, EXIF_TYPE_RATIONAL, 0xC,
      serialize_speed, deserialize_speed},
  {GST_TAG_GEO_LOCATION_MOVEMENT_DIRECTION, 0xF, EXIF_TYPE_RATIONAL, 0xE,
      serialize_geo_direction, deserialize_geo_direction},
  {GST_TAG_GEO_LOCATION_CAPTURE_DIRECTION, 0x11, EXIF_TYPE_RATIONAL, 0x10,
      serialize_geo_direction, deserialize_geo_direction},
  {NULL, 0, 0, 0, NULL, NULL}
};

/* GstExifReader functions */
static void
gst_exif_reader_init (GstExifReader * reader, gint byte_order,
    const GstBuffer * buf, guint32 base_offset)
{
  reader->taglist = gst_tag_list_new ();
  reader->buffer = buf;
  reader->base_offset = base_offset;
  reader->byte_order = byte_order;
  if (reader->byte_order != G_LITTLE_ENDIAN &&
      reader->byte_order != G_BIG_ENDIAN) {
    GST_WARNING ("Unexpected byte order %d, using system default: %d",
        reader->byte_order, G_BYTE_ORDER);
    reader->byte_order = G_BYTE_ORDER;
  }
}

/* GstExifWriter functions */

static void
gst_exif_writer_init (GstExifWriter * writer, gint byte_order)
{
  gst_byte_writer_init (&writer->tagwriter);
  gst_byte_writer_init (&writer->datawriter);

  writer->byte_order = byte_order;
  writer->tags_total = 0;
  if (writer->byte_order != G_LITTLE_ENDIAN &&
      writer->byte_order != G_BIG_ENDIAN) {
    GST_WARNING ("Unexpected byte order %d, using system default: %d",
        writer->byte_order, G_BYTE_ORDER);
    writer->byte_order = G_BYTE_ORDER;
  }
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
      continue;
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
gst_exif_writer_write_rational_tag (GstExifWriter * writer,
    guint16 tag, guint32 frac_n, guint32 frac_d)
{
  guint32 offset = gst_byte_writer_get_size (&writer->datawriter);

  gst_exif_writer_write_tag_header (writer, tag, EXIF_TYPE_RATIONAL,
      1, offset, FALSE);

  gst_exif_writer_write_rational_data (writer, frac_n, frac_d);
}

static void
gst_exif_writer_write_rational_tag_from_double (GstExifWriter * writer,
    guint16 tag, gdouble value)
{
  gint frac_n;
  gint frac_d;

  gst_util_double_to_fraction (value, &frac_n, &frac_d);

  gst_exif_writer_write_rational_tag (writer, tag, frac_n, frac_d);
}

static void
gst_exif_writer_write_byte_tag (GstExifWriter * writer, guint16 tag,
    guint8 value)
{
  guint32 offset = 0;

  GST_WRITE_UINT8 ((guint8 *) & offset, value);
  gst_exif_writer_write_tag_header (writer, tag, EXIF_TYPE_BYTE,
      1, offset, TRUE);
}

static void
gst_exif_writer_write_short_tag (GstExifWriter * writer, guint16 tag,
    guint16 value)
{
  guint32 offset = 0;

  if (writer->byte_order == G_LITTLE_ENDIAN) {
    GST_WRITE_UINT16_LE ((guint8 *) & offset, value);
  } else {
    GST_WRITE_UINT16_BE ((guint8 *) & offset, value);
  }

  gst_exif_writer_write_tag_header (writer, tag, EXIF_TYPE_SHORT,
      1, offset, TRUE);
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
    const GstTagList * taglist, const GstExifTagMatch * exiftag)
{
  gchar *str = NULL;
  gboolean cleanup = FALSE;
  const GValue *value;
  gint tag_size = gst_tag_list_get_tag_size (taglist, exiftag->gst_tag);

  if (tag_size != 1) {
    /* FIXME support this by serializing them with a ','? */
    GST_WARNING ("Multiple string tags not supported yet");
    return;
  }

  value = gst_tag_list_get_value_index (taglist, exiftag->gst_tag, 0);

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

  write_exif_ascii_tag (writer, exiftag->exif_tag, str);
  if (cleanup)
    g_free (str);
}

static void
write_exif_tag_from_taglist (GstExifWriter * writer, const GstTagList * taglist,
    const GstExifTagMatch * exiftag)
{
  GST_DEBUG ("Writing tag %s", exiftag->gst_tag);

  /* check for special handling */
  if (exiftag->serialize) {
    exiftag->serialize (writer, taglist, exiftag);
    return;
  }

  switch (exiftag->exif_type) {
    case EXIF_TYPE_ASCII:
      write_exif_ascii_tag_from_taglist (writer, taglist, exiftag);
      break;
    default:
      GST_WARNING ("Unhandled tag type %d", exiftag->exif_type);
  }
}

static void
tagdata_copy (GstExifTagData * to, const GstExifTagData * from)
{
  to->tag = from->tag;
  to->tag_type = from->tag_type;
  to->count = from->count;
  to->offset = from->offset;
  to->offset_as_data = from->offset_as_data;
}

static void
gst_exif_tag_rewrite_offsets (GstExifWriter * writer, guint32 base_offset)
{
  guint32 offset;

  GST_LOG ("Rewriting tag entries offsets");

  offset = gst_byte_writer_get_size (&writer->tagwriter);
  while (gst_byte_writer_get_pos (&writer->tagwriter) <
      gst_byte_writer_get_size (&writer->tagwriter)) {
    guint16 type = 0;
    guint32 cur_offset = 0;
    GstByteReader *reader;
    gint byte_size = 0;
    guint32 count = 0;
    guint16 tag_id = 0;

    reader = (GstByteReader *) & writer->tagwriter;

    /* read the type */
    if (writer->byte_order == G_LITTLE_ENDIAN) {
      if (!gst_byte_reader_get_uint16_le (reader, &tag_id))
        break;
      if (!gst_byte_reader_get_uint16_le (reader, &type))
        break;
      if (!gst_byte_reader_get_uint32_le (reader, &count))
        break;
    } else {
      if (!gst_byte_reader_get_uint16_be (reader, &tag_id))
        break;
      if (!gst_byte_reader_get_uint16_be (reader, &type))
        break;
      if (!gst_byte_reader_get_uint32_be (reader, &count))
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
      } else {
        if (gst_byte_reader_peek_uint32_be (reader, &cur_offset)) {
          gst_byte_writer_put_uint32_be (&writer->tagwriter,
              cur_offset + offset + base_offset);
        }
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
parse_exif_ascii_tag (GstExifReader * reader, const gchar * gst_tag,
    guint32 count, guint32 offset, const guint8 * offset_as_data)
{
  gchar *str;
  guint32 real_offset;

  if (count > 4) {
    if (offset < reader->base_offset) {
      GST_WARNING ("Offset is smaller (%u) than base offset (%u)", offset,
          reader->base_offset);
      return;
    }

    real_offset = offset - reader->base_offset;
    if (real_offset >= GST_BUFFER_SIZE (reader->buffer)) {
      GST_WARNING ("Invalid offset %u for buffer of size %u, not adding tag %s",
          real_offset, GST_BUFFER_SIZE (reader->buffer), gst_tag);
      return;
    }

    str =
        g_strndup ((gchar *) (GST_BUFFER_DATA (reader->buffer) + real_offset),
        count);
  } else {
    str = g_strndup ((gchar *) offset_as_data, count);
  }
  gst_tag_list_add (reader->taglist, GST_TAG_MERGE_REPLACE, gst_tag, str, NULL);
  g_free (str);
}

static void
parse_exif_rational_tag (GstExifReader * exif_reader,
    const gchar * gst_tag, guint32 count, guint32 offset, gdouble multiplier)
{
  GstByteReader data_reader;
  guint32 real_offset;
  guint32 frac_n = 0;
  guint32 frac_d = 1;
  gdouble value;

  if (count > 1) {
    GST_WARNING ("Rationals with multiple entries are not supported");
  }
  if (offset < exif_reader->base_offset) {
    GST_WARNING ("Offset is smaller (%u) than base offset (%u)", offset,
        exif_reader->base_offset);
    return;
  }

  real_offset = offset - exif_reader->base_offset;
  if (real_offset >= GST_BUFFER_SIZE (exif_reader->buffer)) {
    GST_WARNING ("Invalid offset %u for buffer of size %u, not adding tag %s",
        real_offset, GST_BUFFER_SIZE (exif_reader->buffer), gst_tag);
    return;
  }

  gst_byte_reader_init_from_buffer (&data_reader, exif_reader->buffer);
  if (!gst_byte_reader_set_pos (&data_reader, real_offset))
    goto reader_fail;

  if (exif_reader->byte_order == G_LITTLE_ENDIAN) {
    if (!gst_byte_reader_get_uint32_le (&data_reader, &frac_n) ||
        !gst_byte_reader_get_uint32_le (&data_reader, &frac_d))
      goto reader_fail;
  } else {
    if (!gst_byte_reader_get_uint32_be (&data_reader, &frac_n) ||
        !gst_byte_reader_get_uint32_be (&data_reader, &frac_d))
      goto reader_fail;
  }

  GST_DEBUG ("Read fraction for tag %s: %u/%u", gst_tag, frac_n, frac_d);

  gst_util_fraction_to_double (frac_n, frac_d, &value);

  value *= multiplier;
  GST_DEBUG ("Adding %s tag: %lf", gst_tag, value);
  gst_tag_list_add (exif_reader->taglist, GST_TAG_MERGE_REPLACE, gst_tag, value,
      NULL);

  return;

reader_fail:
  GST_WARNING ("Failed to read from byte reader. (Buffer too short?)");
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

    write_exif_tag_from_taglist (&writer, taglist, &tag_map[i]);
  }

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
parse_exif_tag_header (GstByteReader * reader, gint byte_order,
    GstExifTagData * _tagdata)
{
  g_assert (_tagdata);

  /* read the fields */
  if (byte_order == G_LITTLE_ENDIAN) {
    if (!gst_byte_reader_get_uint16_le (reader, &_tagdata->tag) ||
        !gst_byte_reader_get_uint16_le (reader, &_tagdata->tag_type) ||
        !gst_byte_reader_get_uint32_le (reader, &_tagdata->count) ||
        !gst_byte_reader_get_data (reader, 4, &_tagdata->offset_as_data)) {
      return FALSE;
    }
    _tagdata->offset = GST_READ_UINT32_LE (_tagdata->offset_as_data);
  } else {
    if (!gst_byte_reader_get_uint16_be (reader, &_tagdata->tag) ||
        !gst_byte_reader_get_uint16_be (reader, &_tagdata->tag_type) ||
        !gst_byte_reader_get_uint32_be (reader, &_tagdata->count) ||
        !gst_byte_reader_get_data (reader, 4, &_tagdata->offset_as_data)) {
      return FALSE;
    }
    _tagdata->offset = GST_READ_UINT32_BE (_tagdata->offset_as_data);
  }

  return TRUE;
}

static gboolean
parse_exif_ifd (GstExifReader * exif_reader, gint buf_offset,
    const GstExifTagMatch * tag_map)
{
  GstByteReader reader;
  guint16 entries = 0;
  guint16 i;

  g_return_val_if_fail (exif_reader->byte_order == G_LITTLE_ENDIAN
      || exif_reader->byte_order == G_BIG_ENDIAN, FALSE);

  gst_byte_reader_init_from_buffer (&reader, exif_reader->buffer);
  if (!gst_byte_reader_set_pos (&reader, buf_offset)) {
    GST_WARNING ("Buffer offset invalid when parsing exif ifd");
    return FALSE;
  }

  /* read the IFD entries number */
  if (exif_reader->byte_order == G_LITTLE_ENDIAN) {
    if (!gst_byte_reader_get_uint16_le (&reader, &entries))
      goto read_error;
  } else {
    if (!gst_byte_reader_get_uint16_be (&reader, &entries))
      goto read_error;
  }
  GST_DEBUG ("Read number of entries: %u", entries);

  /* iterate over the buffer and find the tags and stuff */
  for (i = 0; i < entries; i++) {
    GstExifTagData tagdata;
    gint map_index;

    GST_LOG ("Reading entry: %u", i);

    if (!parse_exif_tag_header (&reader, exif_reader->byte_order, &tagdata))
      goto read_error;

    GST_DEBUG ("Parsed tag: id 0x%x, type %u, count %u, offset %u (0x%x)",
        tagdata.tag, tagdata.tag_type, tagdata.count, tagdata.offset,
        tagdata.offset);

    map_index = exif_tag_map_find_reverse (tagdata.tag, tag_map, TRUE);
    if (map_index == -1) {
      GST_WARNING ("Unmapped exif tag: 0x%x", tagdata.tag);
      continue;
    }

    /* inner ifd tags handling */
    if (tagdata.tag == EXIF_GPS_IFD_TAG) {
      i += parse_exif_ifd (exif_reader,
          tagdata.offset - exif_reader->base_offset, tag_map_gps);

      continue;
    }

    /* tags that need specialized deserialization */
    if (tag_map[map_index].deserialize) {
      i += tag_map[map_index].deserialize (exif_reader, &reader,
          &tag_map[map_index], &tagdata);
      continue;
    }

    switch (tagdata.tag_type) {
      case EXIF_TYPE_ASCII:
        parse_exif_ascii_tag (exif_reader, tag_map[map_index].gst_tag,
            tagdata.count, tagdata.offset, tagdata.offset_as_data);
        break;
      case EXIF_TYPE_RATIONAL:
        parse_exif_rational_tag (exif_reader, tag_map[map_index].gst_tag,
            tagdata.count, tagdata.offset, 1);
        break;
      default:
        GST_WARNING ("Unhandled tag type: %u", tagdata.tag_type);
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
  GstExifReader reader;
  g_return_val_if_fail (byte_order == G_LITTLE_ENDIAN
      || byte_order == G_BIG_ENDIAN, NULL);

  gst_exif_reader_init (&reader, byte_order, buffer, base_offset);

  if (!parse_exif_ifd (&reader, 0, tag_map_ifd0))
    goto read_error;

  return reader.taglist;

read_error:
  {
    if (reader.taglist)
      gst_tag_list_free (reader.taglist);
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
  guint16 fortytwo = 42;
  guint16 endianness = 0;
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
serialize_orientation (GstExifWriter * writer, const GstTagList * taglist,
    const GstExifTagMatch * exiftag)
{
  gchar *str = NULL;
  gint exif_value;

  if (!gst_tag_list_get_string_index (taglist, GST_TAG_IMAGE_ORIENTATION, 0,
          &str)) {
    GST_WARNING ("No image orientation tag present in taglist");
    return;
  }

  exif_value = gst_tag_image_orientation_to_exif_value (str);
  if (exif_value == -1) {
    GST_WARNING ("Invalid image orientation value: %s", str);
    g_free (str);
    return;
  }
  g_free (str);

  gst_exif_writer_write_short_tag (writer, exiftag->exif_tag, exif_value);
}

static gint
deserialize_orientation (GstExifReader * exif_reader,
    GstByteReader * reader, const GstExifTagMatch * exiftag,
    GstExifTagData * tagdata)
{
  gint ret = 1;
  const gchar *str = NULL;
  gint value;

  GST_LOG ("Starting to parse %s tag in exif 0x%x", exiftag->gst_tag,
      exiftag->exif_tag);

  /* validate tag */
  if (tagdata->tag_type != EXIF_TYPE_SHORT || tagdata->count != 1) {
    GST_WARNING ("Orientation tag has unexpected type/count");
    return ret;
  }

  if (exif_reader->byte_order == G_LITTLE_ENDIAN) {
    value = GST_READ_UINT16_LE (tagdata->offset_as_data);
  } else {
    value = GST_READ_UINT16_BE (tagdata->offset_as_data);
  }

  str = gst_tag_image_orientation_from_exif_value (value);
  if (str == NULL) {
    GST_WARNING ("Invalid value for exif orientation tag: %d", value);
    return ret;
  }
  gst_tag_list_add (exif_reader->taglist, GST_TAG_MERGE_REPLACE,
      exiftag->gst_tag, str, NULL);

  return ret;
}

static void
serialize_geo_coordinate (GstExifWriter * writer, const GstTagList * taglist,
    const GstExifTagMatch * exiftag)
{
  gboolean latitude;
  gdouble value;
  gint degrees;
  gint minutes;
  gint seconds;
  guint32 offset;

  latitude = exiftag->exif_tag == 0x2;  /* exif tag for latitude */
  if (!gst_tag_list_get_double (taglist, exiftag->gst_tag, &value)) {
    GST_WARNING ("Failed to get double from tag list for tag: %s",
        exiftag->gst_tag);
    return;
  }

  /* first write the Latitude or Longitude Ref */
  if (latitude) {
    if (value >= 0) {
      write_exif_ascii_tag (writer, exiftag->complementary_tag, "N");
    } else {
      value *= -1;
      write_exif_ascii_tag (writer, exiftag->complementary_tag, "S");
    }
  } else {
    if (value >= 0) {
      write_exif_ascii_tag (writer, exiftag->complementary_tag, "E");
    } else {
      value *= -1;
      write_exif_ascii_tag (writer, exiftag->complementary_tag, "W");
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
  gst_exif_writer_write_tag_header (writer, exiftag->exif_tag,
      EXIF_TYPE_RATIONAL, 3, offset, FALSE);
  gst_exif_writer_write_rational_data (writer, degrees, 1);
  gst_exif_writer_write_rational_data (writer, minutes, 1);
  gst_exif_writer_write_rational_data (writer, seconds, 1);
}

static gint
deserialize_geo_coordinate (GstExifReader * exif_reader,
    GstByteReader * reader, const GstExifTagMatch * exiftag,
    GstExifTagData * tagdata)
{
  GstByteReader fractions_reader;
  gint multiplier;
  GstExifTagData next_tagdata;
  gint ret = 0;
  /* for the conversion */
  guint32 degrees_n = 0;
  guint32 degrees_d = 1;
  guint32 minutes_n = 0;
  guint32 minutes_d = 1;
  guint32 seconds_n = 0;
  guint32 seconds_d = 1;
  gdouble degrees;
  gdouble minutes;
  gdouble seconds;

  GST_LOG ("Starting to parse %s tag in exif 0x%x", exiftag->gst_tag,
      exiftag->exif_tag);

  if (exiftag->complementary_tag != tagdata->tag) {
    /* First should come the 'Ref' tags */
    GST_WARNING ("Tag %d is not the 'Ref' tag for latitude nor longitude",
        tagdata->tag);
    return ret;
  }

  if (tagdata->offset_as_data[0] == 'N' || tagdata->offset_as_data[0] == 'E') {
    multiplier = 1;
  } else if (tagdata->offset_as_data[0] == 'S'
      || tagdata->offset_as_data[0] == 'W') {
    multiplier = -1;
  } else {
    GST_WARNING ("Invalid LatitudeRef or LongitudeRef %c",
        tagdata->offset_as_data[0]);
    return ret;
  }

  /* now read the following tag that must be the latitude or longitude */
  if (exif_reader->byte_order == G_LITTLE_ENDIAN) {
    if (!gst_byte_reader_peek_uint16_le (reader, &next_tagdata.tag))
      goto reader_fail;
  } else {
    if (!gst_byte_reader_peek_uint16_be (reader, &next_tagdata.tag))
      goto reader_fail;
  }

  if (exiftag->exif_tag != next_tagdata.tag) {
    GST_WARNING ("This is not a geo cordinate tag");
    return ret;
  }

  /* read the remaining tag entry data */
  if (!parse_exif_tag_header (reader, exif_reader->byte_order, &next_tagdata)) {
    ret = -1;
    goto reader_fail;
  }

  ret = 1;

  /* some checking */
  if (next_tagdata.tag_type != EXIF_TYPE_RATIONAL) {
    GST_WARNING ("Invalid type %d for geo coordinate (latitude/longitude)",
        next_tagdata.tag_type);
    return ret;
  }
  if (next_tagdata.count != 3) {
    GST_WARNING ("Geo coordinate should use 3 fractions, we have %u",
        next_tagdata.count);
    return ret;
  }

  /* now parse the fractions */
  gst_byte_reader_init_from_buffer (&fractions_reader, exif_reader->buffer);
  if (!gst_byte_reader_set_pos (&fractions_reader,
          next_tagdata.offset - exif_reader->base_offset))
    goto reader_fail;

  if (exif_reader->byte_order == G_LITTLE_ENDIAN) {
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
      exiftag->gst_tag, degrees_n, degrees_d, minutes_n, minutes_d,
      seconds_n, seconds_d);

  gst_util_fraction_to_double (degrees_n, degrees_d, &degrees);
  gst_util_fraction_to_double (minutes_n, minutes_d, &minutes);
  gst_util_fraction_to_double (seconds_n, seconds_d, &seconds);

  minutes += seconds / 60;
  degrees += minutes / 60;
  degrees *= multiplier;

  GST_DEBUG ("Adding %s tag: %lf", exiftag->gst_tag, degrees);
  gst_tag_list_add (exif_reader->taglist, GST_TAG_MERGE_REPLACE,
      exiftag->gst_tag, degrees, NULL);

  return ret;

reader_fail:
  GST_WARNING ("Failed to read fields from buffer (too short?)");
  return ret;
}


static void
serialize_geo_direction (GstExifWriter * writer, const GstTagList * taglist,
    const GstExifTagMatch * exiftag)
{
  gdouble value;

  if (!gst_tag_list_get_double (taglist, exiftag->gst_tag, &value)) {
    GST_WARNING ("Failed to get double from tag list for tag: %s",
        exiftag->gst_tag);
    return;
  }

  /* first write the direction ref */
  write_exif_ascii_tag (writer, exiftag->complementary_tag, "T");
  gst_exif_writer_write_rational_tag_from_double (writer,
      exiftag->exif_tag, value);
}

static gint
deserialize_geo_direction (GstExifReader * exif_reader,
    GstByteReader * reader, const GstExifTagMatch * exiftag,
    GstExifTagData * tagdata)
{
  GstExifTagData next_tagdata = { 0, };
  gint ret = 0;

  GST_LOG ("Starting to parse %s tag in exif 0x%x", exiftag->gst_tag,
      exiftag->exif_tag);

  if (exiftag->complementary_tag == tagdata->tag) {
    /* First should come the 'Ref' tags */
    if (tagdata->offset_as_data[0] == 'M') {
      GST_WARNING ("Magnetic direction is not supported");
      return ret;
    } else if (tagdata->offset_as_data[0] == 'T') {
      /* nop */
    } else {
      GST_WARNING ("Invalid Ref for direction or track %c",
          tagdata->offset_as_data[0]);
      return ret;
    }
  } else {
    GST_DEBUG ("No Direction Ref, using default=T");
    if (tagdata->tag == exiftag->exif_tag) {
      /* this is the main tag */
      tagdata_copy (&next_tagdata, tagdata);
    }
  }

  if (next_tagdata.tag == 0) {
    /* now read the following tag that must be the exif_tag */
    if (exif_reader->byte_order == G_LITTLE_ENDIAN) {
      if (!gst_byte_reader_peek_uint16_le (reader, &next_tagdata.tag))
        goto reader_fail;
    } else {
      if (!gst_byte_reader_peek_uint16_be (reader, &next_tagdata.tag))
        goto reader_fail;
    }

    if (exiftag->exif_tag != next_tagdata.tag) {
      GST_WARNING ("Unexpected tag");
      return ret;
    }

    /* read the remaining tag entry data */
    if (!parse_exif_tag_header (reader, exif_reader->byte_order, &next_tagdata)) {
      ret = -1;
      goto reader_fail;
    }
    ret = 1;
  }

  /* some checking */
  if (next_tagdata.tag_type != EXIF_TYPE_RATIONAL) {
    GST_WARNING ("Invalid type %d for 0x%x", next_tagdata.tag_type,
        next_tagdata.tag);
    return ret;
  }
  if (next_tagdata.count != 1) {
    GST_WARNING ("0x%x tag must have a single fraction, we have %u",
        next_tagdata.tag_type, next_tagdata.count);
    return ret;
  }

  parse_exif_rational_tag (exif_reader,
      exiftag->gst_tag, next_tagdata.count, next_tagdata.offset, 1);

  return ret;

reader_fail:
  GST_WARNING ("Failed to read fields from buffer (too short?)");
  return ret;
}


static void
serialize_geo_elevation (GstExifWriter * writer, const GstTagList * taglist,
    const GstExifTagMatch * exiftag)
{
  gdouble value;

  if (!gst_tag_list_get_double (taglist, exiftag->gst_tag, &value)) {
    GST_WARNING ("Failed to get double from tag list for tag: %s",
        exiftag->gst_tag);
    return;
  }

  /* first write the Ref */
  gst_exif_writer_write_byte_tag (writer,
      exiftag->complementary_tag, value >= 0 ? 0 : 1);

  if (value < 0)
    value *= -1;

  /* now the value */
  gst_exif_writer_write_rational_tag_from_double (writer,
      exiftag->exif_tag, value);
}

static gint
deserialize_geo_elevation (GstExifReader * exif_reader,
    GstByteReader * reader, const GstExifTagMatch * exiftag,
    GstExifTagData * tagdata)
{
  GstExifTagData next_tagdata = { 0, };
  gint multiplier = 1;
  gint ret = 0;

  GST_LOG ("Starting to parse %s tag in exif 0x%x", exiftag->gst_tag,
      exiftag->exif_tag);

  if (exiftag->complementary_tag == tagdata->tag) {
    if (tagdata->offset_as_data[0] == 0) {
      /* NOP */
    } else if (tagdata->offset_as_data[0] == 1) {
      multiplier = -1;
    } else {
      GST_WARNING ("Invalid GPSAltitudeRef %u", tagdata->offset_as_data[0]);
      return ret;
    }
  } else {
    GST_DEBUG ("No GPSAltitudeRef, using default=0");
    if (tagdata->tag == exiftag->exif_tag) {
      tagdata_copy (&next_tagdata, tagdata);
    }
  }

  /* now read the following tag that must be the exif_tag */
  if (next_tagdata.tag == 0) {
    if (exif_reader->byte_order == G_LITTLE_ENDIAN) {
      if (!gst_byte_reader_peek_uint16_le (reader, &next_tagdata.tag))
        goto reader_fail;
    } else {
      if (!gst_byte_reader_peek_uint16_be (reader, &next_tagdata.tag))
        goto reader_fail;
    }

    if (exiftag->exif_tag != next_tagdata.tag) {
      GST_WARNING ("Unexpected tag");
      return ret;
    }

    /* read the remaining tag entry data */
    if (!parse_exif_tag_header (reader, exif_reader->byte_order, &next_tagdata)) {
      ret = -1;
      goto reader_fail;
    }
    ret = 1;
  }

  /* some checking */
  if (next_tagdata.tag_type != EXIF_TYPE_RATIONAL) {
    GST_WARNING ("Invalid type %d for 0x%x", next_tagdata.tag_type,
        next_tagdata.tag);
    return ret;
  }
  if (next_tagdata.count != 1) {
    GST_WARNING ("0x%x tag must have a single fraction, we have %u",
        next_tagdata.tag_type, next_tagdata.count);
    return ret;
  }

  parse_exif_rational_tag (exif_reader,
      exiftag->gst_tag, next_tagdata.count, next_tagdata.offset, multiplier);

  return ret;

reader_fail:
  GST_WARNING ("Failed to read fields from buffer (too short?)");
  return ret;
}


static void
serialize_speed (GstExifWriter * writer, const GstTagList * taglist,
    const GstExifTagMatch * exiftag)
{
  gdouble value;

  if (!gst_tag_list_get_double (taglist, exiftag->gst_tag, &value)) {
    GST_WARNING ("Failed to get double from tag list for tag: %s",
        exiftag->gst_tag);
    return;
  }

  /* first write the Ref */
  write_exif_ascii_tag (writer, exiftag->complementary_tag, "K");

  /* now the value */
  gst_exif_writer_write_rational_tag_from_double (writer,
      exiftag->exif_tag, value * METERS_PER_SECOND_TO_KILOMETERS_PER_HOUR);
}

static gint
deserialize_speed (GstExifReader * exif_reader,
    GstByteReader * reader, const GstExifTagMatch * exiftag,
    GstExifTagData * tagdata)
{
  GstExifTagData next_tagdata = { 0, };
  gdouble multiplier = 1;
  gint ret = 0;

  GST_LOG ("Starting to parse %s tag in exif 0x%x", exiftag->gst_tag,
      exiftag->exif_tag);

  if (exiftag->complementary_tag == tagdata->tag) {
    if (tagdata->offset_as_data[0] == 'K') {
      multiplier = KILOMETERS_PER_HOUR_TO_METERS_PER_SECOND;
    } else if (tagdata->offset_as_data[0] == 'M') {
      multiplier = MILES_PER_HOUR_TO_METERS_PER_SECOND;
    } else if (tagdata->offset_as_data[0] == 'N') {
      multiplier = KNOTS_TO_METERS_PER_SECOND;
    } else {
      GST_WARNING ("Invalid GPSSpeedRed %c", tagdata->offset_as_data[0]);
      return ret;
    }
  } else {
    GST_DEBUG ("No GPSSpeedRef, using default=K");
    multiplier = KILOMETERS_PER_HOUR_TO_METERS_PER_SECOND;

    if (tagdata->tag == exiftag->exif_tag) {
      tagdata_copy (&next_tagdata, tagdata);
    }
  }

  /* now read the following tag that must be the exif_tag */
  if (next_tagdata.tag == 0) {
    if (exif_reader->byte_order == G_LITTLE_ENDIAN) {
      if (!gst_byte_reader_peek_uint16_le (reader, &next_tagdata.tag))
        goto reader_fail;
    } else {
      if (!gst_byte_reader_peek_uint16_be (reader, &next_tagdata.tag))
        goto reader_fail;
    }

    if (exiftag->exif_tag != next_tagdata.tag) {
      GST_WARNING ("Unexpected tag");
      return ret;
    }

    /* read the remaining tag entry data */
    if (!parse_exif_tag_header (reader, exif_reader->byte_order, &next_tagdata)) {
      ret = -1;
      goto reader_fail;
    }
    ret = 1;
  }


  /* some checking */
  if (next_tagdata.tag_type != EXIF_TYPE_RATIONAL) {
    GST_WARNING ("Invalid type %d for 0x%x", next_tagdata.tag_type,
        next_tagdata.tag);
    return ret;
  }
  if (next_tagdata.count != 1) {
    GST_WARNING ("0x%x tag must have a single fraction, we have %u",
        next_tagdata.tag_type, next_tagdata.count);
    return ret;
  }

  parse_exif_rational_tag (exif_reader,
      exiftag->gst_tag, next_tagdata.count, next_tagdata.offset, multiplier);

  return ret;

reader_fail:
  GST_WARNING ("Failed to read fields from buffer (too short?)");
  return ret;
}
