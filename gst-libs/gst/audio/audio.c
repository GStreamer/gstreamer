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
 * gst_audio_info_copy:
 * @info: a #GstAudioInfo
 *
 * Copy a GstAudioInfo structure.
 *
 * Returns: a new #GstAudioInfo. free with gst_audio_info_free.
 */
GstAudioInfo *
gst_audio_info_copy (const GstAudioInfo * info)
{
  return g_slice_dup (GstAudioInfo, info);
}

/**
 * gst_audio_info_free:
 * @info: a #GstAudioInfo
 *
 * Free a GstAudioInfo structure previously allocated with gst_audio_info_new()
 * or gst_audio_info_copy().
 */
void
gst_audio_info_free (GstAudioInfo * info)
{
  g_slice_free (GstAudioInfo, info);
}

G_DEFINE_BOXED_TYPE (GstAudioInfo, gst_audio_info,
    (GBoxedCopyFunc) gst_audio_info_copy, (GBoxedFreeFunc) gst_audio_info_free);

/**
 * gst_audio_info_new:
 *
 * Allocate a new #GstAudioInfo that is also initialized with
 * gst_audio_info_init().
 *
 * Returns: a new #GstAudioInfo. free with gst_audio_info_free().
 */
GstAudioInfo *
gst_audio_info_new (void)
{
  GstAudioInfo *info;

  info = g_slice_new (GstAudioInfo);
  gst_audio_info_init (info);

  return info;
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

  info->finfo = &formats[GST_AUDIO_FORMAT_UNKNOWN];

  memset (&info->position, 0xff, sizeof (info->position));
}

static const GstAudioChannelPosition default_channel_order[64] = {
  GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
  GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
  GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER,
  GST_AUDIO_CHANNEL_POSITION_LFE1,
  GST_AUDIO_CHANNEL_POSITION_REAR_LEFT,
  GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT,
  GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT_OF_CENTER,
  GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT_OF_CENTER,
  GST_AUDIO_CHANNEL_POSITION_REAR_CENTER,
  GST_AUDIO_CHANNEL_POSITION_LFE2,
  GST_AUDIO_CHANNEL_POSITION_SIDE_LEFT,
  GST_AUDIO_CHANNEL_POSITION_SIDE_RIGHT,
  GST_AUDIO_CHANNEL_POSITION_TOP_FRONT_LEFT,
  GST_AUDIO_CHANNEL_POSITION_TOP_FRONT_RIGHT,
  GST_AUDIO_CHANNEL_POSITION_TOP_FRONT_CENTER,
  GST_AUDIO_CHANNEL_POSITION_TOP_CENTER,
  GST_AUDIO_CHANNEL_POSITION_TOP_REAR_LEFT,
  GST_AUDIO_CHANNEL_POSITION_TOP_REAR_RIGHT,
  GST_AUDIO_CHANNEL_POSITION_TOP_SIDE_LEFT,
  GST_AUDIO_CHANNEL_POSITION_TOP_SIDE_RIGHT,
  GST_AUDIO_CHANNEL_POSITION_TOP_REAR_CENTER,
  GST_AUDIO_CHANNEL_POSITION_BOTTOM_FRONT_CENTER,
  GST_AUDIO_CHANNEL_POSITION_BOTTOM_FRONT_LEFT,
  GST_AUDIO_CHANNEL_POSITION_BOTTOM_FRONT_RIGHT,
  GST_AUDIO_CHANNEL_POSITION_WIDE_LEFT,
  GST_AUDIO_CHANNEL_POSITION_WIDE_RIGHT,
  GST_AUDIO_CHANNEL_POSITION_SURROUND_LEFT,
  GST_AUDIO_CHANNEL_POSITION_SURROUND_RIGHT,
  GST_AUDIO_CHANNEL_POSITION_INVALID,
  GST_AUDIO_CHANNEL_POSITION_INVALID,
  GST_AUDIO_CHANNEL_POSITION_INVALID,
  GST_AUDIO_CHANNEL_POSITION_INVALID,
  GST_AUDIO_CHANNEL_POSITION_INVALID,
  GST_AUDIO_CHANNEL_POSITION_INVALID,
  GST_AUDIO_CHANNEL_POSITION_INVALID,
  GST_AUDIO_CHANNEL_POSITION_INVALID,
  GST_AUDIO_CHANNEL_POSITION_INVALID,
  GST_AUDIO_CHANNEL_POSITION_INVALID,
  GST_AUDIO_CHANNEL_POSITION_INVALID,
  GST_AUDIO_CHANNEL_POSITION_INVALID,
  GST_AUDIO_CHANNEL_POSITION_INVALID,
  GST_AUDIO_CHANNEL_POSITION_INVALID,
  GST_AUDIO_CHANNEL_POSITION_INVALID,
  GST_AUDIO_CHANNEL_POSITION_INVALID,
  GST_AUDIO_CHANNEL_POSITION_INVALID,
  GST_AUDIO_CHANNEL_POSITION_INVALID,
  GST_AUDIO_CHANNEL_POSITION_INVALID,
  GST_AUDIO_CHANNEL_POSITION_INVALID,
  GST_AUDIO_CHANNEL_POSITION_INVALID,
  GST_AUDIO_CHANNEL_POSITION_INVALID,
  GST_AUDIO_CHANNEL_POSITION_INVALID,
  GST_AUDIO_CHANNEL_POSITION_INVALID,
  GST_AUDIO_CHANNEL_POSITION_INVALID,
  GST_AUDIO_CHANNEL_POSITION_INVALID,
  GST_AUDIO_CHANNEL_POSITION_INVALID,
  GST_AUDIO_CHANNEL_POSITION_INVALID,
  GST_AUDIO_CHANNEL_POSITION_INVALID,
  GST_AUDIO_CHANNEL_POSITION_INVALID,
  GST_AUDIO_CHANNEL_POSITION_INVALID,
  GST_AUDIO_CHANNEL_POSITION_INVALID,
  GST_AUDIO_CHANNEL_POSITION_INVALID,
  GST_AUDIO_CHANNEL_POSITION_INVALID,
  GST_AUDIO_CHANNEL_POSITION_INVALID,
  GST_AUDIO_CHANNEL_POSITION_INVALID
};

static gboolean
check_valid_channel_positions (const GstAudioChannelPosition * position,
    gint channels, gboolean enforce_order, guint64 * channel_mask_out)
{
  gint i, j;
  guint64 channel_mask = 0;

  if (channels == 1 && position[0] == GST_AUDIO_CHANNEL_POSITION_MONO) {
    if (channel_mask_out)
      *channel_mask_out = 0;
    return TRUE;
  }

  if (channels > 0 && position[0] == GST_AUDIO_CHANNEL_POSITION_NONE) {
    if (channel_mask_out)
      *channel_mask_out = 0;
    return TRUE;
  }

  j = 0;
  for (i = 0; i < channels; i++) {
    while (j < G_N_ELEMENTS (default_channel_order)
        && default_channel_order[j] != position[i])
      j++;

    if (position[i] == GST_AUDIO_CHANNEL_POSITION_INVALID ||
        position[i] == GST_AUDIO_CHANNEL_POSITION_MONO ||
        position[i] == GST_AUDIO_CHANNEL_POSITION_NONE)
      return FALSE;

    /* Is this in valid channel order? */
    if (enforce_order && j == G_N_ELEMENTS (default_channel_order))
      return FALSE;
    j++;

    if ((channel_mask & (G_GUINT64_CONSTANT (1) << position[i])))
      return FALSE;

    channel_mask |= (G_GUINT64_CONSTANT (1) << position[i]);
  }

  if (channel_mask_out)
    *channel_mask_out = channel_mask;

  return TRUE;
}

/**
 * gst_audio_info_set_format:
 * @info: a #GstAudioInfo
 * @format: the format
 * @rate: the samplerate
 * @channels: the number of channels
 * @position: the channel positions
 *
 * Set the default info for the audio info of @format and @rate and @channels.
 */
void
gst_audio_info_set_format (GstAudioInfo * info, GstAudioFormat format,
    gint rate, gint channels, const GstAudioChannelPosition * position)
{
  const GstAudioFormatInfo *finfo;
  gint i;

  g_return_if_fail (info != NULL);
  g_return_if_fail (format != GST_AUDIO_FORMAT_UNKNOWN);

  finfo = &formats[format];

  info->flags = 0;
  info->layout = GST_AUDIO_LAYOUT_INTERLEAVED;
  info->finfo = finfo;
  info->rate = rate;
  info->channels = channels;
  info->bpf = (finfo->width * channels) / 8;

  memset (&info->position, 0xff, sizeof (info->position));

  if (!position && channels == 1) {
    info->position[0] = GST_AUDIO_CHANNEL_POSITION_MONO;
    return;
  } else if (!position && channels == 2) {
    info->position[0] = GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT;
    info->position[1] = GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT;
    return;
  } else {
    if (!position
        || !check_valid_channel_positions (position, channels, TRUE, NULL)) {
      if (position)
        g_warning ("Invalid channel positions");
    } else {
      memcpy (&info->position, position,
          info->channels * sizeof (info->position[0]));
      if (info->position[0] == GST_AUDIO_CHANNEL_POSITION_NONE)
        info->flags |= GST_AUDIO_FLAG_UNPOSITIONED;
      return;
    }
  }

  /* Otherwise a NONE layout */
  info->flags |= GST_AUDIO_FLAG_UNPOSITIONED;
  for (i = 0; i < MIN (64, channels); i++)
    info->position[i] = GST_AUDIO_CHANNEL_POSITION_NONE;
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
  guint64 channel_mask;
  gint i;
  GstAudioChannelPosition position[64];

  g_return_val_if_fail (info != NULL, FALSE);
  g_return_val_if_fail (caps != NULL, FALSE);
  g_return_val_if_fail (gst_caps_is_fixed (caps), FALSE);

  GST_DEBUG ("parsing caps %" GST_PTR_FORMAT, caps);

  info->flags = 0;

  str = gst_caps_get_structure (caps, 0);

  if (!gst_structure_has_name (str, "audio/x-raw"))
    goto wrong_name;

  if (!(s = gst_structure_get_string (str, "format")))
    goto no_format;

  format = gst_audio_format_from_string (s);
  if (format == GST_AUDIO_FORMAT_UNKNOWN)
    goto unknown_format;

  if (!(s = gst_structure_get_string (str, "layout")))
    goto no_layout;
  if (g_str_equal (s, "interleaved"))
    info->layout = GST_AUDIO_LAYOUT_INTERLEAVED;
  else if (g_str_equal (s, "non-interleaved"))
    info->layout = GST_AUDIO_LAYOUT_NON_INTERLEAVED;
  else
    goto unknown_layout;

  if (!gst_structure_get_int (str, "rate", &rate))
    goto no_rate;
  if (!gst_structure_get_int (str, "channels", &channels))
    goto no_channels;

  if (!gst_structure_get (str, "channel-mask", GST_TYPE_BITMASK, &channel_mask,
          NULL)) {
    if (channels == 1) {
      position[0] = GST_AUDIO_CHANNEL_POSITION_MONO;
    } else if (channels == 2) {
      position[0] = GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT;
      position[1] = GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT;
    } else {
      goto no_channel_mask;
    }
  } else if (channel_mask == 0) {
    info->flags |= GST_AUDIO_FLAG_UNPOSITIONED;
    for (i = 0; i < MIN (64, channels); i++)
      position[i] = GST_AUDIO_CHANNEL_POSITION_NONE;
  } else {
    if (!gst_audio_channel_positions_from_mask (channels, channel_mask,
            position))
      goto invalid_channel_mask;
  }

  gst_audio_info_set_format (info, format, rate, channels, position);

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
no_layout:
  {
    GST_ERROR ("no layout given");
    return FALSE;
  }
unknown_layout:
  {
    GST_ERROR ("unknown layout given");
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
no_channel_mask:
  {
    GST_ERROR ("no channel-mask property given");
    return FALSE;
  }
invalid_channel_mask:
  {
    GST_ERROR ("Invalid channel mask 0x%016" G_GINT64_MODIFIER
        "x for %d channels", channel_mask, channels);
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
gst_audio_info_to_caps (const GstAudioInfo * info)
{
  GstCaps *caps;
  const gchar *format;
  const gchar *layout;
  GstAudioFlags flags;

  g_return_val_if_fail (info != NULL, NULL);
  g_return_val_if_fail (info->finfo != NULL, NULL);
  g_return_val_if_fail (info->finfo->format != GST_AUDIO_FORMAT_UNKNOWN, NULL);

  format = gst_audio_format_to_string (info->finfo->format);
  g_return_val_if_fail (format != NULL, NULL);

  if (info->layout == GST_AUDIO_LAYOUT_INTERLEAVED)
    layout = "interleaved";
  else if (info->layout == GST_AUDIO_LAYOUT_NON_INTERLEAVED)
    layout = "non-interleaved";
  else
    g_return_val_if_reached (NULL);

  flags = info->flags;
  if ((flags & GST_AUDIO_FLAG_UNPOSITIONED) && info->channels > 1
      && info->position[0] != GST_AUDIO_CHANNEL_POSITION_NONE) {
    flags &= ~GST_AUDIO_FLAG_UNPOSITIONED;
    g_warning ("Unpositioned audio channel position flag set but "
        "channel positions present");
  } else if (!(flags & GST_AUDIO_FLAG_UNPOSITIONED) && info->channels > 1
      && info->position[0] == GST_AUDIO_CHANNEL_POSITION_NONE) {
    flags |= GST_AUDIO_FLAG_UNPOSITIONED;
    g_warning ("Unpositioned audio channel position flag not set "
        "but no channel positions present");
  }

  caps = gst_caps_new_simple ("audio/x-raw",
      "format", G_TYPE_STRING, format,
      "layout", G_TYPE_STRING, layout,
      "rate", G_TYPE_INT, info->rate,
      "channels", G_TYPE_INT, info->channels, NULL);

  if (info->channels > 1
      || info->position[0] != GST_AUDIO_CHANNEL_POSITION_MONO) {
    guint64 channel_mask = 0;

    if ((flags & GST_AUDIO_FLAG_UNPOSITIONED)) {
      channel_mask = 0;
    } else {
      if (!check_valid_channel_positions (info->position, info->channels,
              TRUE, &channel_mask))
        goto invalid_channel_positions;
    }

    if (info->channels == 1
        && info->position[0] == GST_AUDIO_CHANNEL_POSITION_MONO) {
      /* Default mono special case */
    } else {
      gst_caps_set_simple (caps, "channel-mask", GST_TYPE_BITMASK, channel_mask,
          NULL);
    }
  }

  return caps;

invalid_channel_positions:
  {
    GST_ERROR ("Invalid channel positions");
    gst_caps_unref (caps);
    return NULL;
  }
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
gst_audio_info_convert (const GstAudioInfo * info,
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
 * Clip the buffer to the given %GstSegment.
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
  gsize trim, size, osize;
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
  osize = size = gst_buffer_get_size (buffer);

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

  if (trim == 0 && size == osize) {
    /* nothing changed */
    ret = buffer;
  } else {
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
  }
  return ret;
}

/**
 * gst_audio_reorder_channels:
 * @data: The pointer to the memory.
 * @size: The size of the memory.
 * @format: The %GstAudioFormat of the buffer.
 * @channels: The number of channels.
 * @from: The channel positions in the buffer.
 * @to: The channel positions to convert to.
 *
 * Reorders @data from the channel positions @from to the channel
 * positions @to. @from and @to must contain the same number of
 * positions and the same positions, only in a different order.
 *
 * Returns: %TRUE if the reordering was possible.
 */
gboolean
gst_audio_reorder_channels (gpointer data, gsize size, GstAudioFormat format,
    gint channels, const GstAudioChannelPosition * from,
    const GstAudioChannelPosition * to)
{
  const GstAudioFormatInfo *info;
  gint i, j, n;
  gint reorder_map[64] = { 0, };
  guint8 *ptr;
  gint bpf, bps;
  guint8 tmp[64 * 8];

  info = gst_audio_format_get_info (format);

  g_return_val_if_fail (data != NULL, FALSE);
  g_return_val_if_fail (from != NULL, FALSE);
  g_return_val_if_fail (to != NULL, FALSE);
  g_return_val_if_fail (info != NULL && info->width > 0, FALSE);
  g_return_val_if_fail (info->width > 0, FALSE);
  g_return_val_if_fail (info->width <= 8 * 64, FALSE);
  g_return_val_if_fail (size % ((info->width * channels) / 8) == 0, FALSE);
  g_return_val_if_fail (channels > 0, FALSE);
  g_return_val_if_fail (channels <= 64, FALSE);

  if (size == 0)
    return TRUE;

  if (memcmp (from, to, channels * sizeof (from[0])) == 0)
    return TRUE;

  if (!gst_audio_get_channel_reorder_map (channels, from, to, reorder_map))
    return FALSE;

  bps = info->width / 8;
  bpf = bps * channels;
  ptr = data;

  n = size / bpf;
  for (i = 0; i < n; i++) {

    memcpy (tmp, ptr, bpf);
    for (j = 0; j < channels; j++)
      memcpy (ptr + reorder_map[j] * bps, tmp + j * bps, bps);

    ptr += bpf;
  }

  return TRUE;
}

/**
 * gst_audio_buffer_reorder_channels:
 * @buffer: The buffer to reorder.
 * @format: The %GstAudioFormat of the buffer.
 * @channels: The number of channels.
 * @from: The channel positions in the buffer.
 * @to: The channel positions to convert to.
 *
 * Reorders @buffer from the channel positions @from to the channel
 * positions @to. @from and @to must contain the same number of
 * positions and the same positions, only in a different order.
 * @buffer must be writable.
 *
 * Returns: %TRUE if the reordering was possible.
 */
gboolean
gst_audio_buffer_reorder_channels (GstBuffer * buffer,
    GstAudioFormat format, gint channels,
    const GstAudioChannelPosition * from, const GstAudioChannelPosition * to)
{
  GstMapInfo info;
  gboolean ret;

  g_return_val_if_fail (GST_IS_BUFFER (buffer), FALSE);
  g_return_val_if_fail (gst_buffer_is_writable (buffer), FALSE);

  gst_buffer_map (buffer, &info, GST_MAP_READWRITE);

  ret =
      gst_audio_reorder_channels (info.data, info.size, format, channels, from,
      to);

  gst_buffer_unmap (buffer, &info);

  return ret;
}

/**
 * gst_audio_check_valid_channel_positions:
 * @position: The %GstAudioChannelPositions to check.
 * @channels: The number of channels.
 * @force_order: Only consider the GStreamer channel order.
 *
 * Checks if @position contains valid channel positions for
 * @channels channels. If @force_order is %TRUE it additionally
 * checks if the channels are in the order required by GStreamer.
 *
 * Returns: %TRUE if the channel positions are valid.
 */
gboolean
gst_audio_check_valid_channel_positions (const GstAudioChannelPosition *
    position, gint channels, gboolean force_order)
{
  return check_valid_channel_positions (position, channels, force_order, NULL);
}

/**
 * gst_audio_channel_positions_to_mask:
 * @position: The %GstAudioChannelPositions
 * @channels: The number of channels.
 * @channel_mask: the output channel mask
 *
 * Convert the @position array of @channels channels to a bitmask.
 *
 * Returns: %TRUE if the channel positions are valid and could be converted.
 */
gboolean
gst_audio_channel_positions_to_mask (const GstAudioChannelPosition * position,
    gint channels, guint64 * channel_mask)
{
  return check_valid_channel_positions (position, channels, FALSE,
      channel_mask);
}

/**
 * gst_audio_channel_positions_from_mask:
 * @channels: The number of channels
 * @channel_mask: The input channel_mask
 * @position: The %GstAudioChannelPositions
 * @caps: a #GstCaps
 *
 * Convert the @channels present in @channel_mask to a @position array
 * (which should have at least @channels entries ensured by caller).
 * If @channel_mask is set to 0, it is considered as 'not present' for purpose
 * of conversion.
 *
 * Returns: %TRUE if channel and channel mask are valid and could be converted
 */
gboolean
gst_audio_channel_positions_from_mask (gint channels, guint64 channel_mask,
    GstAudioChannelPosition * position)
{
  g_return_val_if_fail (position != NULL, FALSE);
  g_return_val_if_fail (channels != 0, FALSE);

  GST_DEBUG ("converting %d channels for "
      " channel mask 0x%016" G_GINT64_MODIFIER "x", channels, channel_mask);

  if (!channel_mask) {
    if (channels == 1) {
      position[0] = GST_AUDIO_CHANNEL_POSITION_MONO;
    } else if (channels == 2) {
      position[0] = GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT;
      position[1] = GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT;
    } else {
      goto no_channel_mask;
    }
  } else {
    gint i, j;

    j = 0;
    for (i = 0; i < 64; i++) {
      if ((channel_mask & (G_GUINT64_CONSTANT (1) << i))) {
        if (j < channels)
          position[j] = default_channel_order[i];
        if (default_channel_order[i] == GST_AUDIO_CHANNEL_POSITION_INVALID)
          goto invalid_channel_mask;
        j++;
      }
    }

    if (j != channels)
      goto invalid_channel_mask;
  }

  return TRUE;

  /* ERROR */
no_channel_mask:
  {
    GST_ERROR ("no channel-mask property given");
    return FALSE;
  }
invalid_channel_mask:
  {
    GST_ERROR ("Invalid channel mask 0x%016" G_GINT64_MODIFIER
        "x for %d channels", channel_mask, channels);
    return FALSE;
  }
}


/**
 * gst_audio_get_channel_reorder_map:
 * @channels: The number of channels.
 * @from: The channel positions to reorder from.
 * @to: The channel positions to reorder to.
 * @reorder_map: Pointer to the reorder map.
 *
 * Returns a reorder map for @from to @to that can be used in
 * custom channel reordering code, e.g. to convert from or to the
 * GStreamer channel order. @from and @to must contain the same
 * number of positions and the same positions, only in a
 * different order.
 *
 * The resulting @reorder_map can be used for reordering by assigning
 * channel i of the input to channel reorder_map[i] of the output.
 *
 * Returns: %TRUE if the channel positions are valid and reordering
 * is possible.
 */
gboolean
gst_audio_get_channel_reorder_map (gint channels,
    const GstAudioChannelPosition * from, const GstAudioChannelPosition * to,
    gint * reorder_map)
{
  gint i, j;

  g_return_val_if_fail (reorder_map != NULL, FALSE);
  g_return_val_if_fail (channels > 0, FALSE);
  g_return_val_if_fail (from != NULL, FALSE);
  g_return_val_if_fail (to != NULL, FALSE);
  g_return_val_if_fail (check_valid_channel_positions (from, channels, FALSE,
          NULL), FALSE);
  g_return_val_if_fail (check_valid_channel_positions (to, channels, FALSE,
          NULL), FALSE);

  /* Build reorder map and check compatibility */
  for (i = 0; i < channels; i++) {
    if (from[i] == GST_AUDIO_CHANNEL_POSITION_NONE
        || to[i] == GST_AUDIO_CHANNEL_POSITION_NONE)
      return FALSE;
    if (from[i] == GST_AUDIO_CHANNEL_POSITION_INVALID
        || to[i] == GST_AUDIO_CHANNEL_POSITION_INVALID)
      return FALSE;
    if (from[i] == GST_AUDIO_CHANNEL_POSITION_MONO
        || to[i] == GST_AUDIO_CHANNEL_POSITION_MONO)
      return FALSE;

    for (j = 0; j < channels; j++) {
      if (from[i] == to[j]) {
        reorder_map[i] = j;
        break;
      }
    }

    /* Not all channels present in both */
    if (j == channels)
      return FALSE;
  }

  return TRUE;
}

/**
 * gst_audio_channel_positions_to_valid_order:
 * @position: The channel positions to reorder to.
 * @channels: The number of channels.
 *
 * Reorders the channel positions in @position from any order to
 * the GStreamer channel order.
 *
 * Returns: %TRUE if the channel positions are valid and reordering
 * was successful.
 */
gboolean
gst_audio_channel_positions_to_valid_order (GstAudioChannelPosition * position,
    gint channels)
{
  GstAudioChannelPosition tmp[64];
  guint64 channel_mask = 0;
  gint i, j;

  g_return_val_if_fail (channels > 0, FALSE);
  g_return_val_if_fail (position != NULL, FALSE);
  g_return_val_if_fail (check_valid_channel_positions (position, channels,
          FALSE, NULL), FALSE);

  if (channels == 1 && position[0] == GST_AUDIO_CHANNEL_POSITION_MONO)
    return TRUE;
  if (position[0] == GST_AUDIO_CHANNEL_POSITION_NONE)
    return TRUE;

  check_valid_channel_positions (position, channels, FALSE, &channel_mask);

  memset (tmp, 0xff, sizeof (tmp));
  j = 0;
  for (i = 0; i < 64; i++) {
    if ((channel_mask & (G_GUINT64_CONSTANT (1) << i))) {
      tmp[j] = i;
      j++;
    }
  }

  memcpy (position, tmp, sizeof (tmp[0]) * channels);

  return TRUE;
}
