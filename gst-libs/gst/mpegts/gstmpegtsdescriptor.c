/*
 * gstmpegtsdescriptor.c - 
 * Copyright (C) 2013 Edward Hervey
 * 
 * Authors:
 *   Edward Hervey <edward@collabora.com>
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
#include <stdlib.h>
#include <string.h>

#include "mpegts.h"
#include "gstmpegts-private.h"

/**
 * SECTION:gstmpegtsdescriptor
 * @title: Base MPEG-TS descriptors
 * @short_description: Descriptors for ITU H.222.0 | ISO/IEC 13818-1 
 * @include: gst/mpegts/mpegts.h
 *
 * These are the base descriptor types and methods.
 *
 * For more details, refer to the ITU H.222.0 or ISO/IEC 13818-1 specifications
 * and other specifications mentionned in the documentation.
 */

/* FIXME : Move this to proper file once we have a C file for ATSC/ISDB descriptors */
/**
 * SECTION:gst-atsc-descriptor
 * @title: ATSC variants of MPEG-TS descriptors
 * @short_description: Descriptors for the various ATSC specifications
 * @include: gst/mpegts/mpegts.h
 *
 */

/**
 * SECTION:gst-isdb-descriptor
 * @title: ISDB variants of MPEG-TS descriptors
 * @short_description: Descriptors for the various ISDB specifications
 * @include: gst/mpegts/mpegts.h
 *
 */


/*
 * TODO
 *
 * * Add common validation code for data presence and minimum/maximum expected
 *   size.
 * * Add parsing methods for the following descriptors that were previously 
 *   handled in mpegtsbase:
 *   * GST_MTS_DESC_DVB_DATA_BROADCAST_ID
 *   * GST_MTS_DESC_DVB_DATA_BROADCAST
 *   * GST_MTS_DESC_DVB_CAROUSEL_IDENTIFIER
 *   * GST_MTS_DESC_DVB_STREAM_IDENTIFIER
 *   * GST_MTS_DESC_DVB_EXTENDED_EVENT
 *   * GST_MTS_DESC_DVB_COMPONENT
 *   * GST_MTS_DESC_DVB_TERRESTRIAL_DELIVERY_SYSTEM
 *   * GST_MTS_DESC_DVB_FREQUENCY_LIST
 */

#define MAX_KNOWN_ICONV 25

static GIConv __iconvs[MAX_KNOWN_ICONV];

/* All these conversions will be to UTF8 */
typedef enum
{
  _ICONV_UNKNOWN = -1,
  _ICONV_ISO8859_1,
  _ICONV_ISO8859_2,
  _ICONV_ISO8859_3,
  _ICONV_ISO8859_4,
  _ICONV_ISO8859_5,
  _ICONV_ISO8859_6,
  _ICONV_ISO8859_7,
  _ICONV_ISO8859_8,
  _ICONV_ISO8859_9,
  _ICONV_ISO8859_10,
  _ICONV_ISO8859_11,
  _ICONV_ISO8859_12,
  _ICONV_ISO8859_13,
  _ICONV_ISO8859_14,
  _ICONV_ISO8859_15,
  _ICONV_ISO10646_UC2,
  _ICONV_EUC_KR,
  _ICONV_GB2312,
  _ICONV_UTF_16BE,
  _ICONV_ISO10646_UTF8,
  _ICONV_ISO6937,
  /* Insert more here if needed */
  _ICONV_MAX
} LocalIconvCode;

static const gchar *iconvtablename[] = {
  "iso-8859-1",
  "iso-8859-2",
  "iso-8859-3",
  "iso-8859-4",
  "iso-8859-5",
  "iso-8859-6",
  "iso-8859-7",
  "iso-8859-8",
  "iso-8859-9",
  "iso-8859-10",
  "iso-8859-11",
  "iso-8859-12",
  "iso-8859-13",
  "iso-8859-14",
  "iso-8859-15",
  "ISO-10646/UCS2",
  "EUC-KR",
  "GB2312",
  "UTF-16BE",
  "ISO-10646/UTF8",
  "iso6937"
      /* Insert more here if needed */
};

void
__initialize_descriptors (void)
{
  guint i;

  /* Initialize converters */
  /* FIXME : How/when should we close them ??? */
  for (i = 0; i < MAX_KNOWN_ICONV; i++)
    __iconvs[i] = ((GIConv) - 1);
}

/*
 * @text: The text you want to get the encoding from
 * @start_text: Location where the beginning of the actual text is stored
 * @is_multibyte: Location where information whether it's a multibyte encoding
 * or not is stored
 * @returns: GIconv for conversion or NULL
 */
static LocalIconvCode
get_encoding (const gchar * text, guint * start_text, gboolean * is_multibyte)
{
  LocalIconvCode encoding;
  guint8 firstbyte;

  *is_multibyte = FALSE;
  *start_text = 0;

  firstbyte = (guint8) text[0];

  /* A wrong value */
  g_return_val_if_fail (firstbyte != 0x00, _ICONV_UNKNOWN);

  if (firstbyte <= 0x0B) {
    /* 0x01 => iso 8859-5 */
    encoding = firstbyte + _ICONV_ISO8859_4;
    *start_text = 1;
    goto beach;
  }

  /* ETSI EN 300 468, "Selection of character table" */
  switch (firstbyte) {
    case 0x0C:
    case 0x0D:
    case 0x0E:
    case 0x0F:
      /* RESERVED */
      encoding = _ICONV_UNKNOWN;
      break;
    case 0x10:
    {
      guint16 table;

      table = GST_READ_UINT16_BE (text + 1);

      if (table < 17)
        encoding = _ICONV_UNKNOWN + table;
      else
        encoding = _ICONV_UNKNOWN;;
      *start_text = 3;
      break;
    }
    case 0x11:
      encoding = _ICONV_ISO10646_UC2;
      *start_text = 1;
      *is_multibyte = TRUE;
      break;
    case 0x12:
      /*  EUC-KR implements KSX1001 */
      encoding = _ICONV_EUC_KR;
      *start_text = 1;
      *is_multibyte = TRUE;
      break;
    case 0x13:
      encoding = _ICONV_GB2312;
      *start_text = 1;
      break;
    case 0x14:
      encoding = _ICONV_UTF_16BE;
      *start_text = 1;
      *is_multibyte = TRUE;
      break;
    case 0x15:
      /* TODO : Where does this come from ?? */
      encoding = _ICONV_ISO10646_UTF8;
      *start_text = 1;
      break;
    case 0x16:
    case 0x17:
    case 0x18:
    case 0x19:
    case 0x1A:
    case 0x1B:
    case 0x1C:
    case 0x1D:
    case 0x1E:
    case 0x1F:
      /* RESERVED */
      encoding = _ICONV_UNKNOWN;
      break;
    default:
      encoding = _ICONV_ISO6937;
      break;
  }

beach:
  GST_DEBUG
      ("Found encoding %d, first byte is 0x%02x, start_text: %u, is_multibyte: %d",
      encoding, firstbyte, *start_text, *is_multibyte);

  return encoding;
}

/*
 * @text: The text to convert. It may include pango markup (<b> and </b>)
 * @length: The length of the string -1 if it's nul-terminated
 * @start: Where to start converting in the text
 * @encoding: The encoding of text
 * @is_multibyte: Whether the encoding is a multibyte encoding
 * @error: The location to store the error, or NULL to ignore errors
 * @returns: UTF-8 encoded string
 *
 * Convert text to UTF-8.
 */
static gchar *
convert_to_utf8 (const gchar * text, gint length, guint start,
    GIConv giconv, gboolean is_multibyte, GError ** error)
{
  gchar *new_text;
  gchar *tmp, *pos;
  gint i;

  text += start;

  pos = tmp = g_malloc (length * 2);

  if (is_multibyte) {
    if (length == -1) {
      while (*text != '\0') {
        guint16 code = GST_READ_UINT16_BE (text);

        switch (code) {
          case 0xE086:         /* emphasis on */
          case 0xE087:         /* emphasis off */
            /* skip it */
            break;
          case 0xE08A:{
            pos[0] = 0x00;      /* 0x00 0x0A is a new line */
            pos[1] = 0x0A;
            pos += 2;
            break;
          }
          default:
            pos[0] = text[0];
            pos[1] = text[1];
            pos += 2;
            break;
        }

        text += 2;
      }
    } else {
      for (i = 0; i < length; i += 2) {
        guint16 code = GST_READ_UINT16_BE (text);

        switch (code) {
          case 0xE086:         /* emphasis on */
          case 0xE087:         /* emphasis off */
            /* skip it */
            break;
          case 0xE08A:{
            pos[0] = 0x00;      /* 0x00 0x0A is a new line */
            pos[1] = 0x0A;
            pos += 2;
            break;
          }
          default:
            pos[0] = text[0];
            pos[1] = text[1];
            pos += 2;
            break;
        }

        text += 2;
      }
    }
  } else {
    if (length == -1) {
      while (*text != '\0') {
        guint8 code = (guint8) (*text);

        switch (code) {
          case 0x86:           /* emphasis on */
          case 0x87:           /* emphasis off */
            /* skip it */
            break;
          case 0x8A:
            *pos = '\n';
            pos += 1;
            break;
          default:
            *pos = *text;
            pos += 1;
            break;
        }

        text++;
      }
    } else {
      for (i = 0; i < length; i++) {
        guint8 code = (guint8) (*text);

        switch (code) {
          case 0x86:           /* emphasis on */
          case 0x87:           /* emphasis off */
            /* skip it */
            break;
          case 0x8A:
            *pos = '\n';
            pos += 1;
            break;
          default:
            *pos = *text;
            pos += 1;
            break;
        }

        text++;
      }
    }
  }

  if (pos > tmp) {
    gsize bread = 0;
    new_text =
        g_convert_with_iconv (tmp, pos - tmp, giconv, &bread, NULL, error);
    GST_DEBUG ("Converted to : %s", new_text);
  } else {
    new_text = g_strdup ("");
  }

  g_free (tmp);

  return new_text;
}

gchar *
get_encoding_and_convert (const gchar * text, guint length)
{
  GError *error = NULL;
  gchar *converted_str;
  guint start_text = 0;
  gboolean is_multibyte;
  LocalIconvCode encoding;
  GIConv giconv = (GIConv) - 1;

  g_return_val_if_fail (text != NULL, NULL);

  if (text == NULL || length == 0)
    return g_strdup ("");

  encoding = get_encoding (text, &start_text, &is_multibyte);

  if (encoding > _ICONV_UNKNOWN && encoding < _ICONV_MAX) {
    GST_DEBUG ("Encoding %s", iconvtablename[encoding]);
    if (__iconvs[encoding] == ((GIConv) - 1))
      __iconvs[encoding] = g_iconv_open ("utf-8", iconvtablename[encoding]);
    giconv = __iconvs[encoding];
  } else {
    GST_FIXME ("Could not detect encoding. Returning NULL string");
    converted_str = NULL;
    goto beach;
  }

  converted_str = convert_to_utf8 (text, length - start_text, start_text,
      giconv, is_multibyte, &error);
  if (error != NULL) {
    GST_WARNING ("Could not convert string: %s", error->message);
    if (converted_str)
      g_free (converted_str);
    g_error_free (error);
    error = NULL;

    if (encoding >= _ICONV_ISO8859_2 && encoding <= _ICONV_ISO8859_15) {
      /* Sometimes using the standard 8859-1 set fixes issues */
      GST_DEBUG ("Encoding %s", iconvtablename[_ICONV_ISO8859_1]);
      if (__iconvs[_ICONV_ISO8859_1] == (GIConv) - 1)
        __iconvs[_ICONV_ISO8859_1] =
            g_iconv_open ("utf-8", iconvtablename[_ICONV_ISO8859_1]);
      giconv = __iconvs[_ICONV_ISO8859_1];

      GST_INFO ("Trying encoding ISO 8859-1");
      converted_str = convert_to_utf8 (text, length, 1, giconv, FALSE, &error);
      if (error != NULL) {
        GST_WARNING
            ("Could not convert string while assuming encoding ISO 8859-1: %s",
            error->message);
        g_error_free (error);
        goto failed;
      }
    } else if (encoding == _ICONV_ISO6937) {

      /* The first part of ISO 6937 is identical to ISO 8859-9, but
       * they differ in the second part. Some channels don't
       * provide the first byte that indicates ISO 8859-9 encoding.
       * If decoding from ISO 6937 failed, we try ISO 8859-9 here.
       */
      if (__iconvs[_ICONV_ISO8859_9] == (GIConv) - 1)
        __iconvs[_ICONV_ISO8859_9] =
            g_iconv_open ("utf-8", iconvtablename[_ICONV_ISO8859_9]);
      giconv = __iconvs[_ICONV_ISO8859_9];

      GST_INFO ("Trying encoding ISO 8859-9");
      converted_str = convert_to_utf8 (text, length, 0, giconv, FALSE, &error);
      if (error != NULL) {
        GST_WARNING
            ("Could not convert string while assuming encoding ISO 8859-9: %s",
            error->message);
        g_error_free (error);
        goto failed;
      }
    } else
      goto failed;
  }

beach:
  return converted_str;

failed:
  {
    text += start_text;
    return g_strndup (text, length - start_text);
  }
}

static GstMpegTsDescriptor *
_copy_descriptor (GstMpegTsDescriptor * desc)
{
  GstMpegTsDescriptor *copy;

  copy = g_slice_dup (GstMpegTsDescriptor, desc);
  copy->data = g_memdup (desc->data, desc->length + 2);

  return copy;
}

static void
_free_descriptor (GstMpegTsDescriptor * desc)
{
  g_free ((gpointer) desc->data);
  g_slice_free (GstMpegTsDescriptor, desc);
}

G_DEFINE_BOXED_TYPE (GstMpegTsDescriptor, gst_mpegts_descriptor,
    (GBoxedCopyFunc) _copy_descriptor, (GBoxedFreeFunc) _free_descriptor);

/**
 * gst_mpegts_parse_descriptors:
 * @buffer: (transfer none): descriptors to parse
 * @buf_len: Size of @buffer
 *
 * Parses the descriptors present in @buffer and returns them as an
 * array.
 *
 * Note: The data provided in @buffer will not be copied.
 *
 * Returns: (transfer full) (element-type GstMpegTsDescriptor): an
 * array of the parsed descriptors or %NULL if there was an error.
 * Release with #g_array_unref when done with it.
 */
GPtrArray *
gst_mpegts_parse_descriptors (guint8 * buffer, gsize buf_len)
{
  GPtrArray *res;
  guint8 length;
  guint8 *data;
  guint i, nb_desc = 0;

  /* fast-path */
  if (buf_len == 0)
    return g_ptr_array_new ();

  data = buffer;

  GST_MEMDUMP ("Full descriptor array", buffer, buf_len);

  while (data - buffer < buf_len) {
    data++;                     /* skip tag */
    length = *data++;

    if (data - buffer > buf_len) {
      GST_WARNING ("invalid descriptor length %d now at %d max %"
          G_GSIZE_FORMAT, length, (gint) (data - buffer), buf_len);
      return NULL;
    }

    data += length;
    nb_desc++;
  }

  GST_DEBUG ("Saw %d descriptors, read %" G_GSIZE_FORMAT " bytes",
      nb_desc, data - buffer);

  if (data - buffer != buf_len) {
    GST_WARNING ("descriptors size %d expected %" G_GSIZE_FORMAT,
        (gint) (data - buffer), buf_len);
    return NULL;
  }

  res = g_ptr_array_new_full (nb_desc + 1, (GDestroyNotify) _free_descriptor);

  data = buffer;

  for (i = 0; i < nb_desc; i++) {
    GstMpegTsDescriptor *desc = g_slice_new0 (GstMpegTsDescriptor);

    desc->data = data;
    desc->tag = *data++;
    desc->length = *data++;
    /* Copy the data now that we known the size */
    desc->data = g_memdup (desc->data, desc->length + 2);
    GST_LOG ("descriptor 0x%02x length:%d", desc->tag, desc->length);
    GST_MEMDUMP ("descriptor", desc->data + 2, desc->length);
    /* extended descriptors */
    if (G_UNLIKELY (desc->tag == 0x7f))
      desc->tag_extension = *data;

    data += desc->length;

    /* Set the descriptor in the array */
    g_ptr_array_index (res, i) = desc;
  }

  res->len = nb_desc;

  return res;
}

/**
 * gst_mpegts_find_descriptor:
 * @descriptors: (element-type GstMpegTsDescriptor) (transfer none): an array
 * of #GstMpegTsDescriptor
 * @tag: the tag to look for
 *
 * Finds the first descriptor of type @tag in the array.
 *
 * Note: To look for descriptors that can be present more than once in an
 * array of descriptors, iterate the #GArray manually.
 *
 * Returns: (transfer none): the first descriptor matchin @tag, else %NULL.
 */
const GstMpegTsDescriptor *
gst_mpegts_find_descriptor (GPtrArray * descriptors, guint8 tag)
{
  guint i, nb_desc;

  g_return_val_if_fail (descriptors != NULL, NULL);

  nb_desc = descriptors->len;
  for (i = 0; i < nb_desc; i++) {
    GstMpegTsDescriptor *desc = g_ptr_array_index (descriptors, i);
    if (desc->tag == tag)
      return (const GstMpegTsDescriptor *) desc;
  }
  return NULL;
}


/* GST_MTS_DESC_ISO_639_LANGUAGE (0x0A) */
/**
 * gst_mpegts_descriptor_parse_iso_639_language:
 * @descriptor: a %GST_MTS_DESC_ISO_639_LANGUAGE #GstMpegTsDescriptor
 * @res: (out) (transfer none): the #GstMpegTsISO639LanguageDescriptor to fill
 *
 * Extracts the iso 639-2 language information from @descriptor.
 *
 * Note: Use #gst_tag_get_language_code if you want to get the the
 * ISO 639-1 language code from the returned ISO 639-2 one.
 *
 * Returns: %TRUE if parsing succeeded, else %FALSE.
 */
gboolean
gst_mpegts_descriptor_parse_iso_639_language (const GstMpegTsDescriptor *
    descriptor, GstMpegTsISO639LanguageDescriptor * res)
{
  guint i;
  guint8 *data;

  g_return_val_if_fail (descriptor != NULL && descriptor->data != NULL, FALSE);
  g_return_val_if_fail (res != NULL, FALSE);
  g_return_val_if_fail (descriptor->tag == 0x0A, FALSE);

  data = (guint8 *) descriptor->data + 2;
  /* Each language is 3 + 1 bytes */
  res->nb_language = descriptor->length / 4;
  for (i = 0; i < res->nb_language; i++) {
    memcpy (res->language[i], data, 3);
    res->audio_type[i] = data[3];
    data += 4;
  }
  return TRUE;

}

/**
 * gst_mpegts_descriptor_parse_logical_channel:
 * @descriptor: a %GST_MTS_DESC_DTG_LOGICAL_CHANNEL #GstMpegTsDescriptor
 * @res: (out) (transfer none): the #GstMpegTsLogicalChannelDescriptor to fill
 *
 * Extracts the logical channels from @descriptor.
 *
 * Returns: %TRUE if parsing succeeded, else %FALSE.
 */
gboolean
gst_mpegts_descriptor_parse_logical_channel (const GstMpegTsDescriptor *
    descriptor, GstMpegTsLogicalChannelDescriptor * res)
{
  guint i;
  guint8 *data;

  g_return_val_if_fail (descriptor != NULL && descriptor->data != NULL, FALSE);
  g_return_val_if_fail (descriptor->tag == 0x83, FALSE);

  data = (guint8 *) descriptor->data;
  res->nb_channels = descriptor->length / 4;

  for (i = 0; i < res->nb_channels; i++) {
    res->channels[i].service_id = GST_READ_UINT16_BE (data);
    data += 2;
    res->channels[i].visible_service = *data >> 7;
    res->channels[i].logical_channel_number =
        GST_READ_UINT16_BE (data) & 0x03ff;
    data += 2;
  }

  return TRUE;
}
