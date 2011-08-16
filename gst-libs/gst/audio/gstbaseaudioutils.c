/* GStreamer
 * Copyright (C) 2011 Mark Nauwelaerts <mark.nauwelaerts@collabora.co.uk>.
 * Copyright (C) 2011 Nokia Corporation. All rights reserved.
 *   Contact: Stefan Kost <stefan.kost@nokia.com>
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

#include "gstbaseaudioutils.h"

#include <gst/gst.h>
#include <gst/audio/multichannel.h>


#define CHECK_VALUE(var, val) \
G_STMT_START { \
  if (!res) \
    goto fail; \
  if (var != val) \
    changed = TRUE; \
  var = val; \
} G_STMT_END

/**
 * gst_base_audio_parse_caps:
 * @caps: a #GstCaps
 * @state: a #GstAudioFormatInfo
 * @changed: whether @caps introduced a change in current @state
 *
 * Parses audio format as represented by @caps into a more concise form
 * as represented by @state, while checking if for changes to currently
 * defined audio format.
 *
 * Returns: TRUE if parsing succeeded, otherwise FALSE
 */
gboolean
gst_base_audio_parse_caps (GstCaps * caps, GstAudioFormatInfo * state,
    gboolean * _changed)
{
  gboolean res = TRUE, changed = FALSE;
  GstStructure *s;
  gboolean vb;
  gint vi;

  g_return_val_if_fail (caps != NULL, FALSE);
  g_return_val_if_fail (gst_caps_is_fixed (caps), FALSE);

  s = gst_caps_get_structure (caps, 0);
  if (gst_structure_has_name (s, "audio/x-raw-int"))
    state->is_int = TRUE;
  else if (gst_structure_has_name (s, "audio/x-raw-float"))
    state->is_int = FALSE;
  else
    goto fail;

  res = gst_structure_get_int (s, "rate", &vi);
  CHECK_VALUE (state->rate, vi);
  res &= gst_structure_get_int (s, "channels", &vi);
  CHECK_VALUE (state->channels, vi);
  res &= gst_structure_get_int (s, "width", &vi);
  CHECK_VALUE (state->width, vi);
  res &= (!state->is_int || gst_structure_get_int (s, "depth", &vi));
  CHECK_VALUE (state->depth, vi);
  res &= gst_structure_get_int (s, "endianness", &vi);
  CHECK_VALUE (state->endian, vi);
  res &= (!state->is_int || gst_structure_get_boolean (s, "signed", &vb));
  CHECK_VALUE (state->sign, vb);

  state->bpf = (state->width / 8) * state->channels;
  GST_LOG ("bpf: %d", state->bpf);
  if (!state->bpf)
    goto fail;

  g_free (state->channel_pos);
  state->channel_pos = gst_audio_get_channel_positions (s);

  if (_changed)
    *_changed = changed;

  return res;

  /* ERRORS */
fail:
  {
    /* there should not be caps out there that fail parsing ... */
    GST_WARNING ("failed to parse caps %" GST_PTR_FORMAT, caps);
    return res;
  }
}

/**
 * gst_base_audio_add_streamheader:
 * @caps: a #GstCaps
 * @buf: header buffers
 *
 * Adds given buffers to an array of buffers set as streamheader field
 * on the given @caps.  List of buffer arguments must be NULL-terminated.
 *
 * Returns: input caps with a streamheader field added, or NULL if some error
 */
GstCaps *
gst_base_audio_add_streamheader (GstCaps * caps, GstBuffer * buf, ...)
{
  GstStructure *structure = NULL;
  va_list va;
  GValue array = { 0 };
  GValue value = { 0 };

  g_return_val_if_fail (caps != NULL, NULL);
  g_return_val_if_fail (gst_caps_is_fixed (caps), NULL);

  caps = gst_caps_make_writable (caps);
  structure = gst_caps_get_structure (caps, 0);

  g_value_init (&array, GST_TYPE_ARRAY);

  va_start (va, buf);
  /* put buffers in a fixed list */
  while (buf) {
    g_assert (gst_buffer_is_metadata_writable (buf));

    /* mark buffer */
    GST_BUFFER_FLAG_SET (buf, GST_BUFFER_FLAG_IN_CAPS);

    g_value_init (&value, GST_TYPE_BUFFER);
    buf = gst_buffer_copy (buf);
    GST_BUFFER_FLAG_SET (buf, GST_BUFFER_FLAG_IN_CAPS);
    gst_value_set_buffer (&value, buf);
    gst_buffer_unref (buf);
    gst_value_array_append_value (&array, &value);
    g_value_unset (&value);

    buf = va_arg (va, GstBuffer *);
  }

  gst_structure_set_value (structure, "streamheader", &array);
  g_value_unset (&array);

  return caps;
}

/**
 * gst_base_audio_encoded_audio_convert:
 * @fmt: audio format of the encoded audio
 * @bytes: number of encoded bytes
 * @samples: number of encoded samples
 * @src_format: source format
 * @src_value: source value
 * @dest_format: destination format
 * @dest_value: destination format
 *
 * Helper function to convert @src_value in @src_format to @dest_value in
 * @dest_format for encoded audio data.  Conversion is possible between
 * BYTE and TIME format by using estimated bitrate based on
 * @samples and @bytes (and @fmt).
 */
gboolean
gst_base_audio_encoded_audio_convert (GstAudioFormatInfo * fmt,
    gint64 bytes, gint64 samples, GstFormat src_format,
    gint64 src_value, GstFormat * dest_format, gint64 * dest_value)
{
  gboolean res = FALSE;

  g_return_val_if_fail (dest_format != NULL, FALSE);
  g_return_val_if_fail (dest_value != NULL, FALSE);

  if (G_UNLIKELY (src_format == *dest_format || src_value == 0 ||
          src_value == -1)) {
    if (dest_value)
      *dest_value = src_value;
    return TRUE;
  }

  if (samples == 0 || bytes == 0 || fmt->rate == 0) {
    GST_DEBUG ("not enough metadata yet to convert");
    goto exit;
  }

  bytes *= fmt->rate;

  switch (src_format) {
    case GST_FORMAT_BYTES:
      switch (*dest_format) {
        case GST_FORMAT_TIME:
          *dest_value = gst_util_uint64_scale (src_value,
              GST_SECOND * samples, bytes);
          res = TRUE;
          break;
        default:
          res = FALSE;
      }
      break;
    case GST_FORMAT_TIME:
      switch (*dest_format) {
        case GST_FORMAT_BYTES:
          *dest_value = gst_util_uint64_scale (src_value, bytes,
              samples * GST_SECOND);
          res = TRUE;
          break;
        default:
          res = FALSE;
      }
      break;
    default:
      res = FALSE;
  }

exit:
  return res;
}

/**
 * gst_base_audio_raw_audio_convert:
 * @fmt: audio format of the encoded audio
 * @src_format: source format
 * @src_value: source value
 * @dest_format: destination format
 * @dest_value: destination format
 *
 * Helper function to convert @src_value in @src_format to @dest_value in
 * @dest_format for encoded audio data.  Conversion is possible between
 * BYTE, DEFAULT and TIME format based on audio characteristics provided
 * by @fmt.
 */
gboolean
gst_base_audio_raw_audio_convert (GstAudioFormatInfo * fmt,
    GstFormat src_format, gint64 src_value,
    GstFormat * dest_format, gint64 * dest_value)
{
  gboolean res = FALSE;
  guint scale = 1;
  gint bytes_per_sample, rate, byterate;

  g_return_val_if_fail (dest_format != NULL, FALSE);
  g_return_val_if_fail (dest_value != NULL, FALSE);

  if (G_UNLIKELY (src_format == *dest_format || src_value == 0 ||
          src_value == -1)) {
    if (dest_value)
      *dest_value = src_value;
    return TRUE;
  }

  bytes_per_sample = fmt->bpf;
  rate = fmt->rate;
  byterate = bytes_per_sample * rate;

  if (G_UNLIKELY (bytes_per_sample == 0 || rate == 0)) {
    GST_DEBUG ("not enough metadata yet to convert");
    goto exit;
  }

  switch (src_format) {
    case GST_FORMAT_BYTES:
      switch (*dest_format) {
        case GST_FORMAT_DEFAULT:
          *dest_value = src_value / bytes_per_sample;
          res = TRUE;
          break;
        case GST_FORMAT_TIME:
          *dest_value =
              gst_util_uint64_scale_int (src_value, GST_SECOND, byterate);
          res = TRUE;
          break;
        default:
          res = FALSE;
      }
      break;
    case GST_FORMAT_DEFAULT:
      switch (*dest_format) {
        case GST_FORMAT_BYTES:
          *dest_value = src_value * bytes_per_sample;
          res = TRUE;
          break;
        case GST_FORMAT_TIME:
          *dest_value = gst_util_uint64_scale_int (src_value, GST_SECOND, rate);
          res = TRUE;
          break;
        default:
          res = FALSE;
      }
      break;
    case GST_FORMAT_TIME:
      switch (*dest_format) {
        case GST_FORMAT_BYTES:
          scale = bytes_per_sample;
          /* fallthrough */
        case GST_FORMAT_DEFAULT:
          *dest_value = gst_util_uint64_scale_int (src_value,
              scale * rate, GST_SECOND);
          res = TRUE;
          break;
        default:
          res = FALSE;
      }
      break;
    default:
      res = FALSE;
  }

exit:
  return res;
}
