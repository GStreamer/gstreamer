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
 * @short_description: Convenience library for using MPEG-TS descriptors
 *
 * For more details, refer to the ITU H.222.0 or ISO/IEC 13818-1 specifications
 * and other specifications mentionned in the documentation.
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

static gchar *
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
  }

  if (giconv == ((GIConv) - 1)) {
    GST_WARNING ("Could not detect encoding");
    converted_str = g_strndup (text, length);
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
 * Returns: (transfer full) (element-type GstMpegTSDescriptor): an
 * array of the parsed descriptors or %NULL if there was an error.
 * Release with #g_array_unref when done with it.
 */
GArray *
gst_mpegts_parse_descriptors (guint8 * buffer, gsize buf_len)
{
  GArray *res;
  guint8 length;
  guint8 *data;
  guint i, nb_desc = 0;

  /* fast-path */
  if (buf_len == 0)
    return g_array_new (FALSE, FALSE, sizeof (GstMpegTSDescriptor));

  data = buffer;

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

  res = g_array_sized_new (FALSE, FALSE, sizeof (GstMpegTSDescriptor), nb_desc);

  data = buffer;

  for (i = 0; i < nb_desc; i++) {
    GstMpegTSDescriptor *desc = &g_array_index (res, GstMpegTSDescriptor, i);

    desc->descriptor_data = data;
    desc->descriptor_tag = *data++;
    desc->descriptor_length = *data++;
    GST_LOG ("descriptor 0x%02x length:%d", desc->descriptor_tag,
        desc->descriptor_length);
    GST_MEMDUMP ("descriptor", desc->descriptor_data + 2,
        desc->descriptor_length);
    /* Adjust for extended descriptors */
    if (G_UNLIKELY (desc->descriptor_tag == 0x7f)) {
      desc->descriptor_tag_extension = *data++;
      desc->descriptor_length -= 1;
    }
    data += desc->descriptor_length;
  }

  res->len = nb_desc;

  return res;
}

/**
 * gst_mpegts_find_descriptor:
 * @descriptors: (element-type GstMpegTSDescriptor) (transfer none): an array
 * of #GstMpegTSDescriptor
 * @tag: the tag to look for
 *
 * Finds the first descriptor of type @tag in the array.
 *
 * Note: To look for descriptors that can be present more than once in an
 * array of descriptors, iterate the #GArray manually.
 *
 * Returns: (transfer none): the first descriptor matchin @tag, else %NULL.
 */
const GstMpegTSDescriptor *
gst_mpegts_find_descriptor (GArray * descriptors, guint8 tag)
{
  guint i, nb_desc;

  g_return_val_if_fail (descriptors != NULL, NULL);

  nb_desc = descriptors->len;
  for (i = 0; i < nb_desc; i++) {
    GstMpegTSDescriptor *desc =
        &g_array_index (descriptors, GstMpegTSDescriptor, i);
    if (desc->descriptor_tag == tag)
      return (const GstMpegTSDescriptor *) desc;
  }
  return NULL;
}

static GstMpegTSDescriptor *
_copy_descriptor (GstMpegTSDescriptor * desc)
{
  GstMpegTSDescriptor *copy;

  copy = g_new0 (GstMpegTSDescriptor, 1);
  copy->descriptor_tag = desc->descriptor_tag;
  copy->descriptor_tag_extension = desc->descriptor_tag_extension;
  copy->descriptor_length = desc->descriptor_length;
  copy->descriptor_data =
      g_memdup (desc->descriptor_data, desc->descriptor_length);

  return copy;
}

/* This freefunc will only ever be used with descriptors returned by the 
 * above function. That is why we free the descriptor data (unlike the
 * descriptors created in _parse_descriptors()) */
static void
_free_descriptor (GstMpegTSDescriptor * desc)
{
  g_free ((gpointer) desc->descriptor_data);
  g_free (desc);
}

G_DEFINE_BOXED_TYPE (GstMpegTSDescriptor, gst_mpegts_descriptor,
    (GBoxedCopyFunc) _copy_descriptor, (GBoxedFreeFunc) _free_descriptor);

/* GST_MTS_DESC_ISO_639_LANGUAGE (0x0A) */
/**
 * gst_mpegts_descriptor_parse_iso_639_language:
 * @descriptor: a %GST_MTS_DESC_ISO_639_LANGUAGE #GstMpegTSDescriptor
 * @res: (out) (transfer none): the #GstMpegTSISO639LanguageDescriptor to fill
 *
 * Extracts the iso 639-2 language information from @descriptor.
 *
 * Note: Use #gst_tag_get_language_code if you want to get the the
 * ISO 639-1 language code from the returned ISO 639-2 one.
 *
 * Returns: %TRUE if parsing succeeded, else %FALSE.
 */
gboolean
gst_mpegts_descriptor_parse_iso_639_language (const GstMpegTSDescriptor *
    descriptor, GstMpegTSISO639LanguageDescriptor * res)
{
  guint i;
  guint8 *data;

  g_return_val_if_fail (descriptor != NULL
      && descriptor->descriptor_data != NULL, FALSE);
  g_return_val_if_fail (res != NULL, FALSE);
  g_return_val_if_fail (descriptor->descriptor_tag == 0x0A, FALSE);

  data = (guint8 *) descriptor->descriptor_data + 2;
  /* Each language is 3 + 1 bytes */
  res->nb_language = descriptor->descriptor_length / 4;
  for (i = 0; i < res->nb_language; i++) {
    memcpy (res->language[i], data, 3);
    res->audio_type[i] = data[3];
    data += 4;
  }
  return TRUE;

}


/* GST_MTS_DESC_DVB_NETWORK_NAME (0x40) */
/**
 * gst_mpegts_descriptor_parse_dvb_network_name:
 * @descriptor: a %GST_MTS_DESC_DVB_NETWORK_NAME #GstMpegTSDescriptor
 * @name: (out) (transfer full): the extracted name
 *
 * Parses out the dvb network name from the @descriptor:
 *
 * Returns: %TRUE if the parsing happened correctly, else %FALSE.
 */
gboolean
gst_mpegts_descriptor_parse_dvb_network_name (const GstMpegTSDescriptor *
    descriptor, gchar ** name)
{
  g_return_val_if_fail (descriptor != NULL
      && descriptor->descriptor_data != NULL, FALSE);
  g_return_val_if_fail (descriptor->descriptor_tag == 0x40, FALSE);

  *name = get_encoding_and_convert ((gchar *) descriptor->descriptor_data + 2,
      descriptor->descriptor_data[1]);
  return TRUE;
}

/* GST_MTS_DESC_DVB_SATELLITE_DELIVERY_SYSTEM (0x43) */
/**
 * gst_mpegts_descriptor_parse_satellite_delivery_system:
 * @descriptor: a %GST_MTS_DESC_DVB_SATELLITE_DELIVERY_SYSTEM #GstMpegTSDescriptor
 * @res: (out) (transfer none): the #GstMpegTSSatelliteDeliverySystemDescriptor to fill
 *
 * Extracts the satellite delivery system information from @descriptor.
 *
 * Returns: %TRUE if parsing succeeded, else %FALSE.
 */
gboolean
gst_mpegts_descriptor_parse_satellite_delivery_system (const GstMpegTSDescriptor
    * descriptor, GstMpegTSSatelliteDeliverySystemDescriptor * res)
{
  guint8 *data;
  guint8 tmp;

  g_return_val_if_fail (descriptor != NULL
      && descriptor->descriptor_data != NULL, FALSE);
  g_return_val_if_fail (res != NULL, FALSE);
  g_return_val_if_fail (descriptor->descriptor_tag == 0x43, FALSE);

  data = (guint8 *) descriptor->descriptor_data + 2;

#define BCD_UN(a) ((a) & 0x0f)
#define BCD_DEC(a) (((a) >> 4) & 0x0f)
#define BCD(a) (BCD_UN(a) + 10 * BCD_DEC(a))
#define BCD_16(a) (BCD(a[1]) + 100 * BCD(a[0]))
#define BCD_28(a) (BCD_DEC(a[3]) + 10 * BCD(a[2]) + 1000 * BCD(a[1]) + 100000 * BCD(a[0]))
#define BCD_32(a) (BCD(a[3]) + 100 * BCD(a[2]) + 10000 * BCD(a[1]) + 1000000 * BCD(a[0]))

  /* BCD coded frequency in GHz (decimal point occurs after the 3rd character)
   * So direct BCD gives us units of (GHz / 100 000) = 10 kHz*/
  res->frequency = BCD_32 (data) * 10;
  data += 4;
  /* BCD codec position in degrees (float pointer after the 3rd character) */
  res->orbital_position = (BCD_16 (data)) / 10.0;
  data += 2;

  tmp = *data;
  res->west_east = (tmp & 0x80) == 0x80;
  res->polarization = (tmp >> 7) & 0x03;
  res->modulation_system = (tmp & 0x04) == 0x04;
  if (res->modulation_system)
    res->roll_off = (tmp >> 3 & 0x03);
  else
    res->roll_off = GST_MPEGTS_ROLLOFF_AUTO;
  switch (tmp & 0x03) {
    case 0x00:
      res->modulation_type = GST_MPEGTS_MODULATION_QAM_AUTO;
      break;
    case 0x01:
      res->modulation_type = GST_MPEGTS_MODULATION_QPSK;
      break;
    case 0x10:
      res->modulation_type = GST_MPEGTS_MODULATION_PSK_8;
      break;
    case 0x11:
      res->modulation_type = GST_MPEGTS_MODULATION_QAM_16;
      break;
    default:
      break;
  }
  res->modulation_type = tmp & 0x03;
  data += 1;
  /* symbol_rate is in Msymbols/ (decimal point occurs after 3rd character) */
  /* So direct BCD gives us units of (Msymbol / 10 000) = 100 sym/s */
  res->symbol_rate = BCD_28 (data) * 100;
  data += 3;
  /* fec_inner */
  res->fec_inner = *data >> 4;


  return TRUE;
}


/* GST_MTS_DESC_DVB_CABLE_DELIVERY_SYSTEM (0x44) */
/**
 * gst_mpegts_descriptor_parse_cable_delivery_system:
 * @descriptor: a %GST_MTS_DESC_DVB_CABLE_DELIVERY_SYSTEM #GstMpegTSDescriptor
 * @res: (out) (transfer none): the #GstMpegTSCableDeliverySystemDescriptor to fill
 *
 * Extracts the cable delivery system information from @descriptor.
 *
 * Returns: %TRUE if parsing succeeded, else %FALSE.
 */
gboolean
gst_mpegts_descriptor_parse_cable_delivery_system (const GstMpegTSDescriptor *
    descriptor, GstMpegTSCableDeliverySystemDescriptor * res)
{
  guint8 *data;

  g_return_val_if_fail (descriptor != NULL
      && descriptor->descriptor_data != NULL, FALSE);
  g_return_val_if_fail (res != NULL, FALSE);
  g_return_val_if_fail (descriptor->descriptor_tag == 0x44, FALSE);

  data = (guint8 *) descriptor->descriptor_data + 2;
  /* BCD in MHz, decimal place after the fourth character */
  res->frequency = BCD_32 (data) * 100;
  data += 5;
  /* fec_out (4bits) */
  res->outer_fec = *data++ & 0x0f;
  switch (*data) {
    case 0x00:
      res->modulation = GST_MPEGTS_MODULATION_NONE;
      break;
    case 0x01:
      res->modulation = GST_MPEGTS_MODULATION_QAM_16;
      break;
    case 0x02:
      res->modulation = GST_MPEGTS_MODULATION_QAM_32;
      break;
    case 0x03:
      res->modulation = GST_MPEGTS_MODULATION_QAM_64;
      break;
    case 0x04:
      res->modulation = GST_MPEGTS_MODULATION_QAM_128;
      break;
    case 0x05:
      res->modulation = GST_MPEGTS_MODULATION_QAM_256;
      break;
    default:
      GST_WARNING ("Unsupported cable modulation type: 0x%02x", *data);
      res->modulation = GST_MPEGTS_MODULATION_NONE;
      break;
  }

  data += 1;
  /* symbol_rate is in Msymbols/ (decimal point occurs after 3rd character) */
  /* So direct BCD gives us units of (Msymbol / 10 000) = 100 sym/s */
  res->symbol_rate = BCD_28 (data) * 100;
  data += 3;
  /* fec_inner */
  res->fec_inner = *data & 0x0f;

  return TRUE;
}

/* GST_MTS_DESC_DVB_SERVICE (0x48) */
/**
 * gst_mpegts_descriptor_parse_dvb_service:
 * @descriptor: a %GST_MTS_DESC_DVB_SERVICE #GstMpegTSDescriptor
 * @service_type: (out) (allow-none): the service type
 * @service_name: (out) (transfer full) (allow-none): the service name
 * @provider_name: (out) (transfer full) (allow-none): the provider name
 *
 * Extracts the dvb service information from @descriptor.
 *
 * Returns: %TRUE if parsing succeeded, else %FALSE.
 */
gboolean
gst_mpegts_descriptor_parse_dvb_service (const GstMpegTSDescriptor *
    descriptor, GstMpegTSDVBServiceType * service_type, gchar ** service_name,
    gchar ** provider_name)
{
  guint8 *data;

  g_return_val_if_fail (descriptor != NULL
      && descriptor->descriptor_data != NULL, FALSE);
  g_return_val_if_fail (descriptor->descriptor_tag == 0x48, FALSE);

  data = (guint8 *) descriptor->descriptor_data + 2;

  if (service_type)
    *service_type = *data;
  data += 1;
  if (*provider_name)
    *provider_name = get_encoding_and_convert ((const gchar *) data + 1, *data);
  data += *data + 1;
  if (*service_name)
    *service_name = get_encoding_and_convert ((const gchar *) data + 1, *data);

  return TRUE;
}

/* GST_MTS_DESC_DVB_SHORT_EVENT (0x4D) */
/**
 * gst_mpegts_descriptor_parse_dvb_short_event:
 * @descriptor: a %GST_MTS_DESC_DVB_SHORT_EVENT #GstMpegTSDescriptor
 * @language_code: (out) (transfer full) (allow-none): the language code
 * @event_name: (out) (transfer full) (allow-none): the event name
 * @text: (out) (transfer full) (allow-none): the event text
 *
 * Extracts the DVB short event information from @descriptor.
 *
 * Returns: %TRUE if parsing succeeded, else %FALSE.
 */
gboolean
gst_mpegts_descriptor_parse_dvb_short_event (const GstMpegTSDescriptor *
    descriptor, gchar ** language_code, gchar ** event_name, gchar ** text)
{
  guint8 *data;

  g_return_val_if_fail (descriptor != NULL
      && descriptor->descriptor_data != NULL, FALSE);
  g_return_val_if_fail (descriptor->descriptor_tag == 0x4D, FALSE);

  data = (guint8 *) descriptor->descriptor_data + 2;

  if (*language_code) {
    *language_code = g_malloc0 (4);
    memcpy (*language_code, data, 3);
  }
  data += 3;
  if (*event_name)
    *event_name = get_encoding_and_convert ((const gchar *) data + 1, *data);
  data += *data + 1;
  if (*text)
    *text = get_encoding_and_convert ((const gchar *) data + 1, *data);
  return TRUE;
}

/**
 * gst_mpegts_descriptor_parse_logical_channel:
 * @descriptor: a %GST_MTS_DESC_DTG_LOGICAL_CHANNEL #GstMpegTSDescriptor
 * @res: (out) (transfer none): the #GstMpegTSLogicalChannelDescriptor to fill
 *
 * Extracts the logical channels from @descriptor.
 *
 * Returns: %TRUE if parsing succeeded, else %FALSE.
 */
gboolean
gst_mpegts_descriptor_parse_logical_channel (const GstMpegTSDescriptor *
    descriptor, GstMpegTSLogicalChannelDescriptor * res)
{
  guint i;
  guint8 *data;

  g_return_val_if_fail (descriptor != NULL
      && descriptor->descriptor_data != NULL, FALSE);
  g_return_val_if_fail (descriptor->descriptor_tag == 0x83, FALSE);

  data = (guint8 *) descriptor->descriptor_data;
  res->nb_channels = descriptor->descriptor_length / 4;

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
