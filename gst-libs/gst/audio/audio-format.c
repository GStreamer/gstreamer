/* GStreamer
 * Copyright (C) <2012> Wim Taymans <wim.taymans@gmail.com>
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
#  include "config.h"
#endif

#include <string.h>

#include "audio-format.h"

#define SINT (GST_AUDIO_FORMAT_FLAG_INTEGER | GST_AUDIO_FORMAT_FLAG_SIGNED)
#define SINT_PACK (SINT | GST_AUDIO_FORMAT_FLAG_UNPACK)
#define UINT (GST_AUDIO_FORMAT_FLAG_INTEGER)
#define FLOAT (GST_AUDIO_FORMAT_FLAG_FLOAT)
#define FLOAT_PACK (FLOAT | GST_AUDIO_FORMAT_FLAG_UNPACK)

#define MAKE_FORMAT(str,desc,flags,end,width,depth,silent) \
  { GST_AUDIO_FORMAT_ ##str, G_STRINGIFY(str), desc, flags, end, width, depth, silent }

#define SILENT_0         { 0, 0, 0, 0, 0, 0, 0, 0 }
#define SILENT_U8        { 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80 }
#define SILENT_U16LE     { 0x00, 0x80, 0x00, 0x80, 0x00, 0x80, 0x00, 0x80 }
#define SILENT_U16BE     { 0x80, 0x00, 0x80, 0x00, 0x80, 0x00, 0x80, 0x00 }
#define SILENT_U24_32LE  { 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x80, 0x00 }
#define SILENT_U24_32BE  { 0x00, 0x80, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00 }
#define SILENT_U32LE     { 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x80 }
#define SILENT_U32BE     { 0x80, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00 }
#define SILENT_U24LE     { 0x00, 0x00, 0x80, 0x00, 0x00, 0x80 }
#define SILENT_U24BE     { 0x80, 0x00, 0x00, 0x80, 0x00, 0x00 }
#define SILENT_U20LE     { 0x00, 0x00, 0x08, 0x00, 0x00, 0x08 }
#define SILENT_U20BE     { 0x08, 0x00, 0x00, 0x08, 0x00, 0x00 }
#define SILENT_U18LE     { 0x00, 0x00, 0x02, 0x00, 0x00, 0x02 }
#define SILENT_U18BE     { 0x02, 0x00, 0x00, 0x02, 0x00, 0x00 }

static GstAudioFormatInfo formats[] = {
  {GST_AUDIO_FORMAT_UNKNOWN, "UNKNOWN", "Unknown audio", 0, 0, 0, 0},
  {GST_AUDIO_FORMAT_ENCODED, "ENCODED", "Encoded audio",
      GST_AUDIO_FORMAT_FLAG_COMPLEX, 0, 0, 0},
  /* 8 bit */
  MAKE_FORMAT (S8, "8-bit signed PCM audio", SINT, 0, 8, 8, SILENT_0),
  MAKE_FORMAT (U8, "8-bit unsigned PCM audio", UINT, 0, 8, 8, SILENT_U8),
  /* 16 bit */
  MAKE_FORMAT (S16LE, "16-bit signed PCM audio", SINT, G_LITTLE_ENDIAN, 16, 16,
      SILENT_0),
  MAKE_FORMAT (S16BE, "16-bit signed PCM audio", SINT, G_BIG_ENDIAN, 16, 16,
      SILENT_0),
  MAKE_FORMAT (U16LE, "16-bit unsigned PCM audio", UINT, G_LITTLE_ENDIAN, 16,
      16, SILENT_U16LE),
  MAKE_FORMAT (U16BE, "16-bit unsigned PCM audio", UINT, G_BIG_ENDIAN, 16, 16,
      SILENT_U16BE),
  /* 24 bit in low 3 bytes of 32 bits */
  MAKE_FORMAT (S24_32LE, "24-bit signed PCM audio", SINT, G_LITTLE_ENDIAN, 32,
      24, SILENT_0),
  MAKE_FORMAT (S24_32BE, "24-bit signed PCM audio", SINT, G_BIG_ENDIAN, 32, 24,
      SILENT_0),
  MAKE_FORMAT (U24_32LE, "24-bit unsigned PCM audio", UINT, G_LITTLE_ENDIAN, 32,
      24, SILENT_U24_32LE),
  MAKE_FORMAT (U24_32BE, "24-bit unsigned PCM audio", UINT, G_BIG_ENDIAN, 32,
      24, SILENT_U24_32BE),
  /* 32 bit */
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
  MAKE_FORMAT (S32LE, "32-bit signed PCM audio", SINT_PACK, G_LITTLE_ENDIAN, 32,
      32,
      SILENT_0),
  MAKE_FORMAT (S32BE, "32-bit signed PCM audio", SINT, G_BIG_ENDIAN, 32, 32,
      SILENT_0),
#else
  MAKE_FORMAT (S32LE, "32-bit signed PCM audio", SINT, G_LITTLE_ENDIAN, 32, 32,
      SILENT_0),
  MAKE_FORMAT (S32BE, "32-bit signed PCM audio", SINT_PACK, G_BIG_ENDIAN, 32,
      32,
      SILENT_0),
#endif
  MAKE_FORMAT (U32LE, "32-bit unsigned PCM audio", UINT, G_LITTLE_ENDIAN, 32,
      32, SILENT_U32LE),
  MAKE_FORMAT (U32BE, "32-bit unsigned PCM audio", UINT, G_BIG_ENDIAN, 32, 32,
      SILENT_U32BE),
  /* 24 bit in 3 bytes */
  MAKE_FORMAT (S24LE, "24-bit signed PCM audio", SINT, G_LITTLE_ENDIAN, 24, 24,
      SILENT_0),
  MAKE_FORMAT (S24BE, "24-bit signed PCM audio", SINT, G_BIG_ENDIAN, 24, 24,
      SILENT_0),
  MAKE_FORMAT (U24LE, "24-bit unsigned PCM audio", UINT, G_LITTLE_ENDIAN, 24,
      24, SILENT_U24LE),
  MAKE_FORMAT (U24BE, "24-bit unsigned PCM audio", UINT, G_BIG_ENDIAN, 24, 24,
      SILENT_U24BE),
  /* 20 bit in 3 bytes */
  MAKE_FORMAT (S20LE, "20-bit signed PCM audio", SINT, G_LITTLE_ENDIAN, 24, 20,
      SILENT_0),
  MAKE_FORMAT (S20BE, "20-bit signed PCM audio", SINT, G_BIG_ENDIAN, 24, 20,
      SILENT_0),
  MAKE_FORMAT (U20LE, "20-bit unsigned PCM audio", UINT, G_LITTLE_ENDIAN, 24,
      20, SILENT_U20LE),
  MAKE_FORMAT (U20BE, "20-bit unsigned PCM audio", UINT, G_BIG_ENDIAN, 24, 20,
      SILENT_U20BE),
  /* 18 bit in 3 bytes */
  MAKE_FORMAT (S18LE, "18-bit signed PCM audio", SINT, G_LITTLE_ENDIAN, 24, 18,
      SILENT_0),
  MAKE_FORMAT (S18BE, "18-bit signed PCM audio", SINT, G_BIG_ENDIAN, 24, 18,
      SILENT_0),
  MAKE_FORMAT (U18LE, "18-bit unsigned PCM audio", UINT, G_LITTLE_ENDIAN, 24,
      18, SILENT_U18LE),
  MAKE_FORMAT (U18BE, "18-bit unsigned PCM audio", UINT, G_BIG_ENDIAN, 24, 18,
      SILENT_U18BE),
  /* float */
  MAKE_FORMAT (F32LE, "32-bit floating-point audio",
      GST_AUDIO_FORMAT_FLAG_FLOAT, G_LITTLE_ENDIAN, 32, 32, SILENT_0),
  MAKE_FORMAT (F32BE, "32-bit floating-point audio",
      GST_AUDIO_FORMAT_FLAG_FLOAT, G_BIG_ENDIAN, 32, 32, SILENT_0),
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
  MAKE_FORMAT (F64LE, "64-bit floating-point audio",
      FLOAT_PACK, G_LITTLE_ENDIAN, 64, 64, SILENT_0),
  MAKE_FORMAT (F64BE, "64-bit floating-point audio",
      FLOAT, G_BIG_ENDIAN, 64, 64, SILENT_0)
#else
  MAKE_FORMAT (F64LE, "64-bit floating-point audio",
      FLOAT, G_LITTLE_ENDIAN, 64, 64, SILENT_0),
  MAKE_FORMAT (F64BE, "64-bit floating-point audio",
      FLOAT_PACK, G_BIG_ENDIAN, 64, 64, SILENT_0)
#endif
};

G_DEFINE_POINTER_TYPE (GstAudioFormatInfo, gst_audio_format_info);

/**
 * gst_audio_format_build_integer:
 * @sign: signed or unsigned format
 * @endianness: G_LITTLE_ENDIAN or G_BIG_ENDIAN
 * @width: amount of bits used per sample
 * @depth: amount of used bits in @width
 *
 * Construct a #GstAudioFormat with given parameters.
 *
 * Returns: a #GstAudioFormat or GST_AUDIO_FORMAT_UNKNOWN when no audio format
 * exists with the given parameters.
 */
GstAudioFormat
gst_audio_format_build_integer (gboolean sign, gint endianness,
    gint width, gint depth)
{
  gint i, e;

  for (i = 0; i < G_N_ELEMENTS (formats); i++) {
    GstAudioFormatInfo *finfo = &formats[i];

    /* must be int */
    if (!GST_AUDIO_FORMAT_INFO_IS_INTEGER (finfo))
      continue;

    /* width and depth must match */
    if (width != GST_AUDIO_FORMAT_INFO_WIDTH (finfo))
      continue;
    if (depth != GST_AUDIO_FORMAT_INFO_DEPTH (finfo))
      continue;

    /* if there is endianness, it must match */
    e = GST_AUDIO_FORMAT_INFO_ENDIANNESS (finfo);
    if (e && e != endianness)
      continue;

    /* check sign */
    if ((sign && !GST_AUDIO_FORMAT_INFO_IS_SIGNED (finfo)) ||
        (!sign && GST_AUDIO_FORMAT_INFO_IS_SIGNED (finfo)))
      continue;

    return GST_AUDIO_FORMAT_INFO_FORMAT (finfo);
  }
  return GST_AUDIO_FORMAT_UNKNOWN;
}

/**
 * gst_audio_format_from_string:
 * @format: a format string
 *
 * Convert the @format string to its #GstAudioFormat.
 *
 * Returns: the #GstAudioFormat for @format or GST_AUDIO_FORMAT_UNKNOWN when the
 * string is not a known format.
 */
GstAudioFormat
gst_audio_format_from_string (const gchar * format)
{
  guint i;

  for (i = 0; i < G_N_ELEMENTS (formats); i++) {
    if (strcmp (GST_AUDIO_FORMAT_INFO_NAME (&formats[i]), format) == 0)
      return GST_AUDIO_FORMAT_INFO_FORMAT (&formats[i]);
  }
  return GST_AUDIO_FORMAT_UNKNOWN;
}

const gchar *
gst_audio_format_to_string (GstAudioFormat format)
{
  g_return_val_if_fail (format != GST_AUDIO_FORMAT_UNKNOWN, NULL);

  if (format >= G_N_ELEMENTS (formats))
    return NULL;

  return GST_AUDIO_FORMAT_INFO_NAME (&formats[format]);
}

/**
 * gst_audio_format_get_info:
 * @format: a #GstAudioFormat
 *
 * Get the #GstAudioFormatInfo for @format
 *
 * Returns: The #GstAudioFormatInfo for @format.
 */
const GstAudioFormatInfo *
gst_audio_format_get_info (GstAudioFormat format)
{
  g_return_val_if_fail (format < G_N_ELEMENTS (formats), NULL);

  return &formats[format];
}

/**
 * gst_audio_format_fill_silence:
 * @info: a #GstAudioFormatInfo
 * @dest: a destination to fill
 * @length: the length to fill
 *
 * Fill @length bytes in @dest with silence samples for @info.
 */
void
gst_audio_format_fill_silence (const GstAudioFormatInfo * info,
    gpointer dest, gsize length)
{
  guint8 *dptr = dest;

  g_return_if_fail (info != NULL);
  g_return_if_fail (dest != NULL);

  if (info->flags & GST_AUDIO_FORMAT_FLAG_FLOAT ||
      info->flags & GST_AUDIO_FORMAT_FLAG_SIGNED) {
    /* float or signed always 0 */
    memset (dest, 0, length);
  } else {
    gint i, j, bps = info->width >> 3;

    switch (bps) {
      case 1:
        memset (dest, info->silence[0], length);
        break;
      default:
        for (i = 0; i < length; i += bps) {
          for (j = 0; j < bps; j++)
            *dptr++ = info->silence[j];
        }
        break;
    }
  }
}
