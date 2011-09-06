/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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
 * SECTION:gstaudio
 * @short_description: Support library for audio elements
 *
 * This library contains some helper functions for audio elements.
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <string.h>

#include "audio.h"
#include "audio-enumtypes.h"

#include <gst/gststructure.h>

#define SINT (GST_AUDIO_FORMAT_FLAG_INTEGER | GST_AUDIO_FORMAT_FLAG_SIGNED)
#define UINT (GST_AUDIO_FORMAT_FLAG_INTEGER)

#define MAKE_FORMAT(str,desc,flags,end,width,depth,silent) \
  { GST_AUDIO_FORMAT_ ##str, G_STRINGIFY(str), desc, flags, end, width, depth, silent }

#define SILENT_0         { 0, 0, 0, 0, 0, 0, 0, 0 }
#define SILENT_U8        { 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80 }
#define SILENT_U16LE     { 0x00, 0x80,  0x00, 0x80,  0x00, 0x80,  0x00, 0x80 }
#define SILENT_U16BE     { 0x80, 0x00,  0x80, 0x00,  0x80, 0x00,  0x80, 0x00 }
#define SILENT_U24_32LE  { 0x00, 0x00, 0x80, 0x00,  0x00, 0x00, 0x80, 0x00 }
#define SILENT_U24_32BE  { 0x00, 0x80, 0x00, 0x00,  0x00, 0x80, 0x00, 0x00 }
#define SILENT_U32LE     { 0x00, 0x00, 0x00, 0x80,  0x00, 0x00, 0x00, 0x80 }
#define SILENT_U32BE     { 0x80, 0x00, 0x00, 0x00,  0x80, 0x00, 0x00, 0x00 }
#define SILENT_U24LE     { 0x00, 0x00, 0x80,  0x00, 0x00, 0x80 }
#define SILENT_U24BE     { 0x80, 0x00, 0x00,  0x80, 0x00, 0x00 }
#define SILENT_U20LE     { 0x00, 0x00, 0x08,  0x00, 0x00, 0x08 }
#define SILENT_U20BE     { 0x08, 0x00, 0x00,  0x08, 0x00, 0x00 }
#define SILENT_U18LE     { 0x00, 0x00, 0x02,  0x00, 0x00, 0x02 }
#define SILENT_U18BE     { 0x02, 0x00, 0x00,  0x02, 0x00, 0x00 }

static GstAudioFormatInfo formats[] = {
  {GST_AUDIO_FORMAT_UNKNOWN, "UNKNOWN", 0, 0, 0, 0},
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
  MAKE_FORMAT (S32LE, "32-bit signed PCM audio", SINT, G_LITTLE_ENDIAN, 32, 32,
      SILENT_0),
  MAKE_FORMAT (S32BE, "32-bit signed PCM audio", SINT, G_BIG_ENDIAN, 32, 32,
      SILENT_0),
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
      GST_AUDIO_FORMAT_FLAG_FLOAT, G_LITTLE_ENDIAN, 32, 32,
      SILENT_0),
  MAKE_FORMAT (F32BE, "32-bit floating-point audio",
      GST_AUDIO_FORMAT_FLAG_FLOAT, G_BIG_ENDIAN, 32, 32,
      SILENT_0),
  MAKE_FORMAT (F64LE, "64-bit floating-point audio",
      GST_AUDIO_FORMAT_FLAG_FLOAT, G_LITTLE_ENDIAN, 64, 64,
      SILENT_0),
  MAKE_FORMAT (F64BE, "64-bit floating-point audio",
      GST_AUDIO_FORMAT_FLAG_FLOAT, G_BIG_ENDIAN, 64, 64,
      SILENT_0)
};

/**
 * gst_audio_format_build_int:
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
    if (sign && !GST_AUDIO_FORMAT_INFO_IS_SIGNED (finfo))
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
  g_return_val_if_fail (format != GST_AUDIO_FORMAT_UNKNOWN, NULL);
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


/**
 * gst_audio_info_init:
 * @info: a #GstAudioInfo
 *
 * Initialize @info with default values.
 */
void
gst_audio_info_init (GstAudioInfo * info)
{
  g_return_if_fail (info != NULL);

  memset (info, 0, sizeof (GstAudioInfo));
}

/**
 * gst_audio_info_set_format:
 * @info: a #GstAudioInfo
 * @format: the format
 * @rate: the samplerate
 * @channels: the number of channels
 *
 * Set the default info for the audio info of @format and @rate and @channels.
 */
void
gst_audio_info_set_format (GstAudioInfo * info, GstAudioFormat format,
    gint rate, gint channels)
{
  const GstAudioFormatInfo *finfo;

  g_return_if_fail (info != NULL);
  g_return_if_fail (format != GST_AUDIO_FORMAT_UNKNOWN);

  finfo = &formats[format];

  info->flags = 0;
  info->finfo = finfo;
  info->rate = rate;
  info->channels = channels;
  info->bpf = (finfo->width * channels) / 8;
}

/**
 * gst_audio_info_from_caps:
 * @info: a #GstAudioInfo
 * @caps: a #GstCaps
 *
 * Parse @caps and update @info.
 *
 * Returns: TRUE if @caps could be parsed
 */
gboolean
gst_audio_info_from_caps (GstAudioInfo * info, const GstCaps * caps)
{
  GstStructure *str;
  const gchar *s;
  GstAudioFormat format;
  gint rate, channels;
  const GValue *pos_val_arr, *pos_val_entry;
  gint i;

  g_return_val_if_fail (info != NULL, FALSE);
  g_return_val_if_fail (caps != NULL, FALSE);
  g_return_val_if_fail (gst_caps_is_fixed (caps), FALSE);

  GST_DEBUG ("parsing caps %" GST_PTR_FORMAT, caps);

  str = gst_caps_get_structure (caps, 0);

  if (!gst_structure_has_name (str, "audio/x-raw"))
    goto wrong_name;

  if (!(s = gst_structure_get_string (str, "format")))
    goto no_format;

  format = gst_audio_format_from_string (s);
  if (format == GST_AUDIO_FORMAT_UNKNOWN)
    goto unknown_format;

  if (!gst_structure_get_int (str, "rate", &rate))
    goto no_rate;
  if (!gst_structure_get_int (str, "channels", &channels))
    goto no_channels;

  gst_audio_info_set_format (info, format, rate, channels);

  pos_val_arr = gst_structure_get_value (str, "channel-positions");
  if (pos_val_arr) {
    guint max_pos = MAX (channels, 64);
    for (i = 0; i < max_pos; i++) {
      pos_val_entry = gst_value_array_get_value (pos_val_arr, i);
      info->position[i] = g_value_get_enum (pos_val_entry);
    }
  } else {
    info->flags |= GST_AUDIO_FLAG_DEFAULT_POSITIONS;
    /* FIXME, set default positions */
  }

  return TRUE;

  /* ERROR */
wrong_name:
  {
    GST_ERROR ("wrong name, expected audio/x-raw");
    return FALSE;
  }
no_format:
  {
    GST_ERROR ("no format given");
    return FALSE;
  }
unknown_format:
  {
    GST_ERROR ("unknown format given");
    return FALSE;
  }
no_rate:
  {
    GST_ERROR ("no rate property given");
    return FALSE;
  }
no_channels:
  {
    GST_ERROR ("no channels property given");
    return FALSE;
  }
}

/**
 * gst_audio_info_to_caps:
 * @info: a #GstAudioInfo
 *
 * Convert the values of @info into a #GstCaps.
 *
 * Returns: (transfer full): the new #GstCaps containing the
 *          info of @info.
 */
GstCaps *
gst_audio_info_to_caps (GstAudioInfo * info)
{
  GstCaps *caps;
  const gchar *format;

  g_return_val_if_fail (info != NULL, NULL);
  g_return_val_if_fail (info->finfo != NULL, NULL);
  g_return_val_if_fail (info->finfo->format != GST_AUDIO_FORMAT_UNKNOWN, NULL);

  format = gst_audio_format_to_string (info->finfo->format);
  g_return_val_if_fail (format != NULL, NULL);

  caps = gst_caps_new_simple ("audio/x-raw",
      "format", G_TYPE_STRING, format,
      "rate", G_TYPE_INT, info->rate,
      "channels", G_TYPE_INT, info->channels, NULL);

  if (info->channels > 2) {
    GValue pos_val_arr = { 0 }
    , pos_val_entry = {
    0};
    gint i, max_pos;
    GstStructure *str;

    /* build gvaluearray from positions */
    g_value_init (&pos_val_arr, GST_TYPE_ARRAY);
    g_value_init (&pos_val_entry, GST_TYPE_AUDIO_CHANNEL_POSITION);
    max_pos = MAX (info->channels, 64);
    for (i = 0; i < max_pos; i++) {
      g_value_set_enum (&pos_val_entry, info->position[i]);
      gst_value_array_append_value (&pos_val_arr, &pos_val_entry);
    }
    g_value_unset (&pos_val_entry);

    /* add to structure */
    str = gst_caps_get_structure (caps, 0);
    gst_structure_set_value (str, "channel-positions", &pos_val_arr);
    g_value_unset (&pos_val_arr);
  }

  return caps;
}

/**
 * gst_audio_format_convert:
 * @info: a #GstAudioInfo
 * @src_format: #GstFormat of the @src_value
 * @src_value: value to convert
 * @dest_format: #GstFormat of the @dest_value
 * @dest_value: pointer to destination value
 *
 * Converts among various #GstFormat types.  This function handles
 * GST_FORMAT_BYTES, GST_FORMAT_TIME, and GST_FORMAT_DEFAULT.  For
 * raw audio, GST_FORMAT_DEFAULT corresponds to audio frames.  This
 * function can be used to handle pad queries of the type GST_QUERY_CONVERT.
 *
 * Returns: TRUE if the conversion was successful.
 */
gboolean
gst_audio_info_convert (GstAudioInfo * info,
    GstFormat src_fmt, gint64 src_val, GstFormat dest_fmt, gint64 * dest_val)
{
  gboolean res = TRUE;
  gint bpf, rate;

  GST_DEBUG ("converting value %" G_GINT64_FORMAT " from %s (%d) to %s (%d)",
      src_val, gst_format_get_name (src_fmt), src_fmt,
      gst_format_get_name (dest_fmt), dest_fmt);

  if (src_fmt == dest_fmt || src_val == -1) {
    *dest_val = src_val;
    goto done;
  }

  /* get important info */
  bpf = GST_AUDIO_INFO_BPF (info);
  rate = GST_AUDIO_INFO_RATE (info);

  if (bpf == 0 || rate == 0) {
    GST_DEBUG ("no rate or bpf configured");
    res = FALSE;
    goto done;
  }

  switch (src_fmt) {
    case GST_FORMAT_BYTES:
      switch (dest_fmt) {
        case GST_FORMAT_TIME:
          *dest_val = GST_FRAMES_TO_CLOCK_TIME (src_val / bpf, rate);
          break;
        case GST_FORMAT_DEFAULT:
          *dest_val = src_val / bpf;
          break;
        default:
          res = FALSE;
          break;
      }
      break;
    case GST_FORMAT_DEFAULT:
      switch (dest_fmt) {
        case GST_FORMAT_TIME:
          *dest_val = GST_FRAMES_TO_CLOCK_TIME (src_val, rate);
          break;
        case GST_FORMAT_BYTES:
          *dest_val = src_val * bpf;
          break;
        default:
          res = FALSE;
          break;
      }
      break;
    case GST_FORMAT_TIME:
      switch (dest_fmt) {
        case GST_FORMAT_DEFAULT:
          *dest_val = GST_CLOCK_TIME_TO_FRAMES (src_val, rate);
          break;
        case GST_FORMAT_BYTES:
          *dest_val = GST_CLOCK_TIME_TO_FRAMES (src_val, rate);
          *dest_val *= bpf;
          break;
        default:
          res = FALSE;
          break;
      }
      break;
    default:
      res = FALSE;
      break;
  }
done:
  GST_DEBUG ("ret=%d result %" G_GINT64_FORMAT, res, *dest_val);

  return res;
}

/**
 * gst_audio_buffer_clip:
 * @buffer: The buffer to clip.
 * @segment: Segment in %GST_FORMAT_TIME or %GST_FORMAT_DEFAULT to which
 *           the buffer should be clipped.
 * @rate: sample rate.
 * @bpf: size of one audio frame in bytes. This is the size of one sample
 * * channels.
 *
 * Clip the the buffer to the given %GstSegment.
 *
 * After calling this function the caller does not own a reference to
 * @buffer anymore.
 *
 * Returns: %NULL if the buffer is completely outside the configured segment,
 * otherwise the clipped buffer is returned.
 *
 * If the buffer has no timestamp, it is assumed to be inside the segment and
 * is not clipped
 *
 * Since: 0.10.14
 */
GstBuffer *
gst_audio_buffer_clip (GstBuffer * buffer, GstSegment * segment, gint rate,
    gint bpf)
{
  GstBuffer *ret;
  GstClockTime timestamp = GST_CLOCK_TIME_NONE, duration = GST_CLOCK_TIME_NONE;
  guint64 offset = GST_BUFFER_OFFSET_NONE, offset_end = GST_BUFFER_OFFSET_NONE;
  gsize trim, size;
  gboolean change_duration = TRUE, change_offset = TRUE, change_offset_end =
      TRUE;

  g_return_val_if_fail (segment->format == GST_FORMAT_TIME ||
      segment->format == GST_FORMAT_DEFAULT, buffer);
  g_return_val_if_fail (GST_IS_BUFFER (buffer), NULL);

  if (!GST_BUFFER_TIMESTAMP_IS_VALID (buffer))
    /* No timestamp - assume the buffer is completely in the segment */
    return buffer;

  /* Get copies of the buffer metadata to change later. 
   * Calculate the missing values for the calculations,
   * they won't be changed later though. */

  trim = 0;
  size = gst_buffer_get_size (buffer);

  timestamp = GST_BUFFER_TIMESTAMP (buffer);
  GST_DEBUG ("timestamp %" GST_TIME_FORMAT, GST_TIME_ARGS (timestamp));
  if (GST_BUFFER_DURATION_IS_VALID (buffer)) {
    duration = GST_BUFFER_DURATION (buffer);
  } else {
    change_duration = FALSE;
    duration = gst_util_uint64_scale (size / bpf, GST_SECOND, rate);
  }

  if (GST_BUFFER_OFFSET_IS_VALID (buffer)) {
    offset = GST_BUFFER_OFFSET (buffer);
  } else {
    change_offset = FALSE;
    offset = 0;
  }

  if (GST_BUFFER_OFFSET_END_IS_VALID (buffer)) {
    offset_end = GST_BUFFER_OFFSET_END (buffer);
  } else {
    change_offset_end = FALSE;
    offset_end = offset + size / bpf;
  }

  if (segment->format == GST_FORMAT_TIME) {
    /* Handle clipping for GST_FORMAT_TIME */

    guint64 start, stop, cstart, cstop, diff;

    start = timestamp;
    stop = timestamp + duration;

    if (gst_segment_clip (segment, GST_FORMAT_TIME,
            start, stop, &cstart, &cstop)) {

      diff = cstart - start;
      if (diff > 0) {
        timestamp = cstart;

        if (change_duration)
          duration -= diff;

        diff = gst_util_uint64_scale (diff, rate, GST_SECOND);
        if (change_offset)
          offset += diff;
        trim += diff * bpf;
        size -= diff * bpf;
      }

      diff = stop - cstop;
      if (diff > 0) {
        /* duration is always valid if stop is valid */
        duration -= diff;

        diff = gst_util_uint64_scale (diff, rate, GST_SECOND);
        if (change_offset_end)
          offset_end -= diff;
        size -= diff * bpf;
      }
    } else {
      gst_buffer_unref (buffer);
      return NULL;
    }
  } else {
    /* Handle clipping for GST_FORMAT_DEFAULT */
    guint64 start, stop, cstart, cstop, diff;

    g_return_val_if_fail (GST_BUFFER_OFFSET_IS_VALID (buffer), buffer);

    start = offset;
    stop = offset_end;

    if (gst_segment_clip (segment, GST_FORMAT_DEFAULT,
            start, stop, &cstart, &cstop)) {

      diff = cstart - start;
      if (diff > 0) {
        offset = cstart;

        timestamp = gst_util_uint64_scale (cstart, GST_SECOND, rate);

        if (change_duration)
          duration -= gst_util_uint64_scale (diff, GST_SECOND, rate);

        trim += diff * bpf;
        size -= diff * bpf;
      }

      diff = stop - cstop;
      if (diff > 0) {
        offset_end = cstop;

        if (change_duration)
          duration -= gst_util_uint64_scale (diff, GST_SECOND, rate);

        size -= diff * bpf;
      }
    } else {
      gst_buffer_unref (buffer);
      return NULL;
    }
  }

  /* Get a writable buffer and apply all changes */
  GST_DEBUG ("trim %" G_GSIZE_FORMAT " size %" G_GSIZE_FORMAT, trim, size);
  ret = gst_buffer_copy_region (buffer, GST_BUFFER_COPY_ALL, trim, size);
  gst_buffer_unref (buffer);

  GST_DEBUG ("timestamp %" GST_TIME_FORMAT, GST_TIME_ARGS (timestamp));
  GST_BUFFER_TIMESTAMP (ret) = timestamp;

  if (change_duration)
    GST_BUFFER_DURATION (ret) = duration;
  if (change_offset)
    GST_BUFFER_OFFSET (ret) = offset;
  if (change_offset_end)
    GST_BUFFER_OFFSET_END (ret) = offset_end;

  return ret;
}
