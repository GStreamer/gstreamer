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

#define SINT (GST_AUDIO_FORMAT_FLAG_INT | GST_AUDIO_FORMAT_FLAG_SIGNED)
#define UINT (GST_AUDIO_FORMAT_FLAG_INT)

#define MAKE_FORMAT(str,flags,end,width,depth,silent) \
  { GST_AUDIO_FORMAT_ ##str, G_STRINGIFY(str), flags, end, width, depth, silent }

#define SILENT_0       { 0, 0, 0, 0, 0, 0, 0, 0 }
#define SILENT_U8      { 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80 }
#define SILENT_U16_LE  { 0x00, 0x80,  0x00, 0x80,  0x00, 0x80,  0x00, 0x80 }
#define SILENT_U16_BE  { 0x80, 0x00,  0x80, 0x00,  0x80, 0x00,  0x80, 0x00 }
#define SILENT_U24_LE  { 0x00, 0x00, 0x80, 0x00,  0x00, 0x00, 0x80, 0x00 }
#define SILENT_U24_BE  { 0x00, 0x80, 0x00, 0x00,  0x00, 0x80, 0x00, 0x00 }
#define SILENT_U32_LE  { 0x00, 0x00, 0x00, 0x80,  0x00, 0x00, 0x00, 0x80 }
#define SILENT_U32_BE  { 0x80, 0x00, 0x00, 0x00,  0x80, 0x00, 0x00, 0x00 }
#define SILENT_U24_3LE { 0x00, 0x00, 0x80,  0x00, 0x00, 0x80 }
#define SILENT_U24_3BE { 0x80, 0x00, 0x00,  0x80, 0x00, 0x00 }
#define SILENT_U20_3LE { 0x00, 0x00, 0x08,  0x00, 0x00, 0x08 }
#define SILENT_U20_3BE { 0x08, 0x00, 0x00,  0x08, 0x00, 0x00 }
#define SILENT_U18_3LE { 0x00, 0x00, 0x02,  0x00, 0x00, 0x02 }
#define SILENT_U18_3BE { 0x02, 0x00, 0x00,  0x02, 0x00, 0x00 }

static GstAudioFormatInfo formats[] = {
  {GST_AUDIO_FORMAT_UNKNOWN, "UNKNOWN", 0, 0, 0, 0},
  /* 8 bit */
  MAKE_FORMAT (S8, SINT, 0, 8, 8, SILENT_0),
  MAKE_FORMAT (U8, UINT, 0, 8, 8, SILENT_U8),
  /* 16 bit */
  MAKE_FORMAT (S16_LE, SINT, G_LITTLE_ENDIAN, 16, 16, SILENT_0),
  MAKE_FORMAT (S16_BE, SINT, G_BIG_ENDIAN, 16, 16, SILENT_0),
  MAKE_FORMAT (U16_LE, UINT, G_LITTLE_ENDIAN, 16, 16, SILENT_U16_LE),
  MAKE_FORMAT (U16_BE, UINT, G_BIG_ENDIAN, 16, 16, SILENT_U16_BE),
  /* 24 bit in low 3 bytes of 32 bits */
  MAKE_FORMAT (S24_LE, SINT, G_LITTLE_ENDIAN, 32, 24, SILENT_0),
  MAKE_FORMAT (S24_BE, SINT, G_BIG_ENDIAN, 32, 24, SILENT_0),
  MAKE_FORMAT (U24_LE, UINT, G_LITTLE_ENDIAN, 32, 24, SILENT_U24_LE),
  MAKE_FORMAT (U24_BE, UINT, G_BIG_ENDIAN, 32, 24, SILENT_U24_BE),
  /* 32 bit */
  MAKE_FORMAT (S32_LE, SINT, G_LITTLE_ENDIAN, 32, 32, SILENT_0),
  MAKE_FORMAT (S32_BE, SINT, G_BIG_ENDIAN, 32, 32, SILENT_0),
  MAKE_FORMAT (U32_LE, UINT, G_LITTLE_ENDIAN, 32, 32, SILENT_U32_LE),
  MAKE_FORMAT (U32_BE, UINT, G_BIG_ENDIAN, 32, 32, SILENT_U32_BE),
  /* 24 bit in 3 bytes */
  MAKE_FORMAT (S24_3LE, SINT, G_LITTLE_ENDIAN, 24, 24, SILENT_0),
  MAKE_FORMAT (S24_3BE, SINT, G_BIG_ENDIAN, 24, 24, SILENT_0),
  MAKE_FORMAT (U24_3LE, UINT, G_LITTLE_ENDIAN, 24, 24, SILENT_U24_3LE),
  MAKE_FORMAT (U24_3BE, UINT, G_BIG_ENDIAN, 24, 24, SILENT_U24_3BE),
  /* 20 bit in 3 bytes */
  MAKE_FORMAT (S20_3LE, SINT, G_LITTLE_ENDIAN, 24, 20, SILENT_0),
  MAKE_FORMAT (S20_3BE, SINT, G_BIG_ENDIAN, 24, 20, SILENT_0),
  MAKE_FORMAT (U20_3LE, UINT, G_LITTLE_ENDIAN, 24, 20, SILENT_U20_3LE),
  MAKE_FORMAT (U20_3BE, UINT, G_BIG_ENDIAN, 24, 20, SILENT_U20_3BE),
  /* 18 bit in 3 bytes */
  MAKE_FORMAT (S18_3LE, SINT, G_LITTLE_ENDIAN, 24, 18, SILENT_0),
  MAKE_FORMAT (S18_3BE, SINT, G_BIG_ENDIAN, 24, 18, SILENT_0),
  MAKE_FORMAT (U18_3LE, UINT, G_LITTLE_ENDIAN, 24, 18, SILENT_U18_3LE),
  MAKE_FORMAT (U18_3BE, UINT, G_BIG_ENDIAN, 24, 18, SILENT_U18_3BE),
  /* float */
  MAKE_FORMAT (F32_LE, GST_AUDIO_FORMAT_FLAG_FLOAT, G_LITTLE_ENDIAN, 32, 32,
      SILENT_0),
  MAKE_FORMAT (F32_BE, GST_AUDIO_FORMAT_FLAG_FLOAT, G_BIG_ENDIAN, 32, 32,
      SILENT_0),
  MAKE_FORMAT (F64_LE, GST_AUDIO_FORMAT_FLAG_FLOAT, G_LITTLE_ENDIAN, 64, 64,
      SILENT_0),
  MAKE_FORMAT (F64_BE, GST_AUDIO_FORMAT_FLAG_FLOAT, G_BIG_ENDIAN, 64, 64,
      SILENT_0)
};

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
 * @lenfth: the length to fill
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
    info->flags |= GST_AUDIO_FLAG_UNPOSITIONED;
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
 * Returns: a new #GstCaps containing the info of @info.
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
 * gst_audio_frame_byte_size:
 * @pad: the #GstPad to get the caps from
 *
 * Calculate byte size of an audio frame.
 *
 * Returns: the byte size, or 0 if there was an error
 */
int
gst_audio_frame_byte_size (GstPad * pad)
{
  /* FIXME: this should be moved closer to the gstreamer core
   * and be implemented for every mime type IMO
   */

  int width = 0;
  int channels = 0;
  GstCaps *caps;
  GstStructure *structure;

  /* get caps of pad */
  caps = gst_pad_get_current_caps (pad);

  if (caps == NULL)
    goto no_caps;

  structure = gst_caps_get_structure (caps, 0);

  gst_structure_get_int (structure, "width", &width);
  gst_structure_get_int (structure, "channels", &channels);
  gst_caps_unref (caps);

  return (width / 8) * channels;

  /* ERRORS */
no_caps:
  {
    /* ERROR: could not get caps of pad */
    g_warning ("gstaudio: could not get caps of pad %s:%s\n",
        GST_DEBUG_PAD_NAME (pad));
    return 0;
  }
}

/**
 * gst_audio_frame_length:
 * @pad: the #GstPad to get the caps from
 * @buf: the #GstBuffer
 *
 * Calculate length of buffer in frames.
 *
 * Returns: 0 if there's an error, or the number of frames if everything's ok
 */
long
gst_audio_frame_length (GstPad * pad, GstBuffer * buf)
{
  /* FIXME: this should be moved closer to the gstreamer core
   * and be implemented for every mime type IMO
   */
  int frame_byte_size = 0;

  frame_byte_size = gst_audio_frame_byte_size (pad);
  if (frame_byte_size == 0)
    /* error */
    return 0;
  /* FIXME: this function assumes the buffer size to be a whole multiple
   *        of the frame byte size
   */
  return gst_buffer_get_size (buf) / frame_byte_size;
}

/**
 * gst_audio_duration_from_pad_buffer:
 * @pad: the #GstPad to get the caps from
 * @buf: the #GstBuffer
 *
 * Calculate length in nanoseconds of audio buffer @buf based on capabilities of
 * @pad.
 *
 * Returns: the length.
 */
GstClockTime
gst_audio_duration_from_pad_buffer (GstPad * pad, GstBuffer * buf)
{
  long bytes = 0;
  int width = 0;
  int channels = 0;
  int rate = 0;
  GstCaps *caps;
  GstStructure *structure;

  g_assert (GST_IS_BUFFER (buf));

  /* get caps of pad */
  caps = gst_pad_get_current_caps (pad);
  if (caps == NULL)
    goto no_caps;

  structure = gst_caps_get_structure (caps, 0);
  bytes = gst_buffer_get_size (buf);
  gst_structure_get_int (structure, "width", &width);
  gst_structure_get_int (structure, "channels", &channels);
  gst_structure_get_int (structure, "rate", &rate);
  gst_caps_unref (caps);

  g_assert (bytes != 0);
  g_assert (width != 0);
  g_assert (channels != 0);
  g_assert (rate != 0);

  return (bytes * 8 * GST_SECOND) / (rate * channels * width);

  /* ERRORS */
no_caps:
  {
    /* ERROR: could not get caps of pad */
    g_warning ("gstaudio: could not get caps of pad %s:%s\n",
        GST_DEBUG_PAD_NAME (pad));
    return GST_CLOCK_TIME_NONE;
  }
}

/**
 * gst_audio_is_buffer_framed:
 * @pad: the #GstPad to get the caps from
 * @buf: the #GstBuffer
 *
 * Check if the buffer size is a whole multiple of the frame size.
 *
 * Returns: %TRUE if buffer size is multiple.
 */
gboolean
gst_audio_is_buffer_framed (GstPad * pad, GstBuffer * buf)
{
  if (gst_buffer_get_size (buf) % gst_audio_frame_byte_size (pad) == 0)
    return TRUE;
  else
    return FALSE;
}

/**
 * gst_audio_buffer_clip:
 * @buffer: The buffer to clip.
 * @segment: Segment in %GST_FORMAT_TIME or %GST_FORMAT_DEFAULT to which the buffer should be clipped.
 * @rate: sample rate.
 * @frame_size: size of one audio frame in bytes.
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
    gint frame_size)
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
    duration = gst_util_uint64_scale (size / frame_size, GST_SECOND, rate);
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
    offset_end = offset + size / frame_size;
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
        trim += diff * frame_size;
        size -= diff * frame_size;
      }

      diff = stop - cstop;
      if (diff > 0) {
        /* duration is always valid if stop is valid */
        duration -= diff;

        diff = gst_util_uint64_scale (diff, rate, GST_SECOND);
        if (change_offset_end)
          offset_end -= diff;
        size -= diff * frame_size;
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

        trim += diff * frame_size;
        size -= diff * frame_size;
      }

      diff = stop - cstop;
      if (diff > 0) {
        offset_end = cstop;

        if (change_duration)
          duration -= gst_util_uint64_scale (diff, GST_SECOND, rate);

        size -= diff * frame_size;
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
