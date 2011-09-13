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

#include "audio.h"
#include "audio-enumtypes.h"

#include <gst/gststructure.h>

#include <string.h>

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
  const GstCaps *caps = NULL;
  GstStructure *structure;

  /* get caps of pad */
  caps = GST_PAD_CAPS (pad);

  if (caps == NULL) {
    /* ERROR: could not get caps of pad */
    g_warning ("gstaudio: could not get caps of pad %s:%s\n",
        GST_DEBUG_PAD_NAME (pad));
    return 0;
  }

  structure = gst_caps_get_structure (caps, 0);

  gst_structure_get_int (structure, "width", &width);
  gst_structure_get_int (structure, "channels", &channels);
  return (width / 8) * channels;
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
  return GST_BUFFER_SIZE (buf) / frame_byte_size;
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

  GstClockTime length;

  const GstCaps *caps = NULL;
  GstStructure *structure;

  g_assert (GST_IS_BUFFER (buf));
  /* get caps of pad */
  caps = GST_PAD_CAPS (pad);
  if (caps == NULL) {
    /* ERROR: could not get caps of pad */
    g_warning ("gstaudio: could not get caps of pad %s:%s\n",
        GST_DEBUG_PAD_NAME (pad));
    length = GST_CLOCK_TIME_NONE;
  } else {
    structure = gst_caps_get_structure (caps, 0);
    bytes = GST_BUFFER_SIZE (buf);
    gst_structure_get_int (structure, "width", &width);
    gst_structure_get_int (structure, "channels", &channels);
    gst_structure_get_int (structure, "rate", &rate);

    g_assert (bytes != 0);
    g_assert (width != 0);
    g_assert (channels != 0);
    g_assert (rate != 0);
    length = (bytes * 8 * GST_SECOND) / (rate * channels * width);
  }
  return length;
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
  if (GST_BUFFER_SIZE (buf) % gst_audio_frame_byte_size (pad) == 0)
    return TRUE;
  else
    return FALSE;
}

/* _getcaps helper functions
 * sets structure fields to default for audio type
 * flag determines which structure fields to set to default
 * keep these functions in sync with the templates in audio.h
 */

/* private helper function
 * sets a list on the structure
 * pass in structure, fieldname for the list, type of the list values,
 * number of list values, and each of the values, terminating with NULL
 */
static void
_gst_audio_structure_set_list (GstStructure * structure,
    const gchar * fieldname, GType type, int number, ...)
{
  va_list varargs;
  GValue value = { 0 };
  GArray *array;
  int j;

  g_return_if_fail (structure != NULL);

  g_value_init (&value, GST_TYPE_LIST);
  array = g_value_peek_pointer (&value);

  va_start (varargs, number);

  for (j = 0; j < number; ++j) {
    int i;
    gboolean b;

    GValue list_value = { 0 };

    switch (type) {
      case G_TYPE_INT:
        i = va_arg (varargs, int);

        g_value_init (&list_value, G_TYPE_INT);
        g_value_set_int (&list_value, i);
        break;
      case G_TYPE_BOOLEAN:
        b = va_arg (varargs, gboolean);
        g_value_init (&list_value, G_TYPE_BOOLEAN);
        g_value_set_boolean (&list_value, b);
        break;
      default:
        g_warning
            ("_gst_audio_structure_set_list: LIST of given type not implemented.");
    }
    g_array_append_val (array, list_value);

  }
  gst_structure_set_value (structure, fieldname, &value);
  va_end (varargs);
}

/**
 * gst_audio_structure_set_int:
 * @structure: a #GstStructure
 * @flag: a set of #GstAudioFieldFlag
 *
 * Do not use anymore.
 *
 * Deprecated: use gst_structure_set()
 */
#ifndef GST_REMOVE_DEPRECATED
#ifdef GST_DISABLE_DEPRECATED
typedef enum
{
  GST_AUDIO_FIELD_RATE = (1 << 0),
  GST_AUDIO_FIELD_CHANNELS = (1 << 1),
  GST_AUDIO_FIELD_ENDIANNESS = (1 << 2),
  GST_AUDIO_FIELD_WIDTH = (1 << 3),
  GST_AUDIO_FIELD_DEPTH = (1 << 4),
  GST_AUDIO_FIELD_SIGNED = (1 << 5),
} GstAudioFieldFlag;
void
gst_audio_structure_set_int (GstStructure * structure, GstAudioFieldFlag flag);
#endif /* GST_DISABLE_DEPRECATED */

void
gst_audio_structure_set_int (GstStructure * structure, GstAudioFieldFlag flag)
{
  /* was added here:
   * http://webcvs.freedesktop.org/gstreamer/gst-plugins-base/gst-libs/gst/audio/audio.c?r1=1.16&r2=1.17
   * but it is not used
   */
  if (flag & GST_AUDIO_FIELD_RATE)
    gst_structure_set (structure, "rate", GST_TYPE_INT_RANGE, 1, G_MAXINT,
        NULL);
  if (flag & GST_AUDIO_FIELD_CHANNELS)
    gst_structure_set (structure, "channels", GST_TYPE_INT_RANGE, 1, G_MAXINT,
        NULL);
  if (flag & GST_AUDIO_FIELD_ENDIANNESS)
    _gst_audio_structure_set_list (structure, "endianness", G_TYPE_INT, 2,
        G_LITTLE_ENDIAN, G_BIG_ENDIAN, NULL);
  if (flag & GST_AUDIO_FIELD_WIDTH)
    _gst_audio_structure_set_list (structure, "width", G_TYPE_INT, 3, 8, 16, 32,
        NULL);
  if (flag & GST_AUDIO_FIELD_DEPTH)
    gst_structure_set (structure, "depth", GST_TYPE_INT_RANGE, 1, 32, NULL);
  if (flag & GST_AUDIO_FIELD_SIGNED)
    _gst_audio_structure_set_list (structure, "signed", G_TYPE_BOOLEAN, 2, TRUE,
        FALSE, NULL);
}
#endif /* GST_REMOVE_DEPRECATED */

#define SINT (GST_AUDIO_FORMAT_FLAG_INTEGER | GST_AUDIO_FORMAT_FLAG_SIGNED)
#define UINT (GST_AUDIO_FORMAT_FLAG_INTEGER)

#define MAKE_FORMAT(str,flags,end,width,depth,silent) \
  { GST_AUDIO_FORMAT_ ##str, G_STRINGIFY(str), flags, end, width, depth, silent }

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
  MAKE_FORMAT (S8, SINT, 0, 8, 8, SILENT_0),
  MAKE_FORMAT (U8, UINT, 0, 8, 8, SILENT_U8),
  /* 16 bit */
  MAKE_FORMAT (S16LE, SINT, G_LITTLE_ENDIAN, 16, 16, SILENT_0),
  MAKE_FORMAT (S16BE, SINT, G_BIG_ENDIAN, 16, 16, SILENT_0),
  MAKE_FORMAT (U16LE, UINT, G_LITTLE_ENDIAN, 16, 16, SILENT_U16LE),
  MAKE_FORMAT (U16BE, UINT, G_BIG_ENDIAN, 16, 16, SILENT_U16BE),
  /* 24 bit in low 3 bytes of 32 bits */
  MAKE_FORMAT (S24_32LE, SINT, G_LITTLE_ENDIAN, 32, 24, SILENT_0),
  MAKE_FORMAT (S24_32BE, SINT, G_BIG_ENDIAN, 32, 24, SILENT_0),
  MAKE_FORMAT (U24_32LE, UINT, G_LITTLE_ENDIAN, 32, 24, SILENT_U24_32LE),
  MAKE_FORMAT (U24_32BE, UINT, G_BIG_ENDIAN, 32, 24, SILENT_U24_32BE),
  /* 32 bit */
  MAKE_FORMAT (S32LE, SINT, G_LITTLE_ENDIAN, 32, 32, SILENT_0),
  MAKE_FORMAT (S32BE, SINT, G_BIG_ENDIAN, 32, 32, SILENT_0),
  MAKE_FORMAT (U32LE, UINT, G_LITTLE_ENDIAN, 32, 32, SILENT_U32LE),
  MAKE_FORMAT (U32BE, UINT, G_BIG_ENDIAN, 32, 32, SILENT_U32BE),
  /* 24 bit in 3 bytes */
  MAKE_FORMAT (S24LE, SINT, G_LITTLE_ENDIAN, 24, 24, SILENT_0),
  MAKE_FORMAT (S24BE, SINT, G_BIG_ENDIAN, 24, 24, SILENT_0),
  MAKE_FORMAT (U24LE, UINT, G_LITTLE_ENDIAN, 24, 24, SILENT_U24LE),
  MAKE_FORMAT (U24BE, UINT, G_BIG_ENDIAN, 24, 24, SILENT_U24BE),
  /* 20 bit in 3 bytes */
  MAKE_FORMAT (S20LE, SINT, G_LITTLE_ENDIAN, 24, 20, SILENT_0),
  MAKE_FORMAT (S20BE, SINT, G_BIG_ENDIAN, 24, 20, SILENT_0),
  MAKE_FORMAT (U20LE, UINT, G_LITTLE_ENDIAN, 24, 20, SILENT_U20LE),
  MAKE_FORMAT (U20BE, UINT, G_BIG_ENDIAN, 24, 20, SILENT_U20BE),
  /* 18 bit in 3 bytes */
  MAKE_FORMAT (S18LE, SINT, G_LITTLE_ENDIAN, 24, 18, SILENT_0),
  MAKE_FORMAT (S18BE, SINT, G_BIG_ENDIAN, 24, 18, SILENT_0),
  MAKE_FORMAT (U18LE, UINT, G_LITTLE_ENDIAN, 24, 18, SILENT_U18LE),
  MAKE_FORMAT (U18BE, UINT, G_BIG_ENDIAN, 24, 18, SILENT_U18BE),
  /* float */
  MAKE_FORMAT (F32LE, GST_AUDIO_FORMAT_FLAG_FLOAT, G_LITTLE_ENDIAN, 32, 32,
      SILENT_0),
  MAKE_FORMAT (F32BE, GST_AUDIO_FORMAT_FLAG_FLOAT, G_BIG_ENDIAN, 32, 32,
      SILENT_0),
  MAKE_FORMAT (F64LE, GST_AUDIO_FORMAT_FLAG_FLOAT, G_LITTLE_ENDIAN, 64, 64,
      SILENT_0),
  MAKE_FORMAT (F64BE, GST_AUDIO_FORMAT_FLAG_FLOAT, G_BIG_ENDIAN, 64, 64,
      SILENT_0)
};

static GstAudioFormat
gst_audio_format_from_caps_structure (const GstStructure * s)
{
  gint endianness, width, depth;
  guint i;

  if (gst_structure_has_name (s, "audio/x-raw-int")) {
    gboolean sign;

    if (!gst_structure_get_boolean (s, "signed", &sign))
      goto missing_field_signed;

    if (!gst_structure_get_int (s, "endianness", &endianness))
      goto missing_field_endianness;

    if (!gst_structure_get_int (s, "width", &width))
      goto missing_field_width;

    if (!gst_structure_get_int (s, "depth", &depth))
      goto missing_field_depth;

    for (i = 0; i < G_N_ELEMENTS (formats); i++) {
      if (GST_AUDIO_FORMAT_INFO_IS_INTEGER (&formats[i]) &&
          sign == GST_AUDIO_FORMAT_INFO_IS_SIGNED (&formats[i]) &&
          GST_AUDIO_FORMAT_INFO_ENDIANNESS (&formats[i]) == endianness &&
          GST_AUDIO_FORMAT_INFO_WIDTH (&formats[i]) == width &&
          GST_AUDIO_FORMAT_INFO_DEPTH (&formats[i]) == depth) {
        return GST_AUDIO_FORMAT_INFO_FORMAT (&formats[i]);
      }
    }
  } else if (gst_structure_has_name (s, "audio/x-raw-float")) {
    /* fallbacks are for backwards compatibility (is this needed at all?) */
    if (!gst_structure_get_int (s, "endianness", &endianness)) {
      GST_WARNING ("float audio caps without endianness %" GST_PTR_FORMAT, s);
      endianness = G_BYTE_ORDER;
    }

    if (!gst_structure_get_int (s, "width", &width)) {
      GST_WARNING ("float audio caps without width %" GST_PTR_FORMAT, s);
      width = 32;
    }

    for (i = 0; i < G_N_ELEMENTS (formats); i++) {
      if (GST_AUDIO_FORMAT_INFO_IS_FLOAT (&formats[i]) &&
          GST_AUDIO_FORMAT_INFO_ENDIANNESS (&formats[i]) == endianness &&
          GST_AUDIO_FORMAT_INFO_WIDTH (&formats[i]) == width) {
        return GST_AUDIO_FORMAT_INFO_FORMAT (&formats[i]);
      }
    }
  }

  /* no match */
  return GST_AUDIO_FORMAT_UNKNOWN;

missing_field_signed:
  {
    GST_ERROR ("missing 'signed' field in audio caps %" GST_PTR_FORMAT, s);
    return GST_AUDIO_FORMAT_UNKNOWN;
  }
missing_field_endianness:
  {
    GST_ERROR ("missing 'endianness' field in audio caps %" GST_PTR_FORMAT, s);
    return GST_AUDIO_FORMAT_UNKNOWN;
  }
missing_field_depth:
  {
    GST_ERROR ("missing 'depth' field in audio caps %" GST_PTR_FORMAT, s);
    return GST_AUDIO_FORMAT_UNKNOWN;
  }
missing_field_width:
  {
    GST_ERROR ("missing 'width' field in audio caps %" GST_PTR_FORMAT, s);
    return GST_AUDIO_FORMAT_UNKNOWN;
  }
}

/* FIXME: remove these if we don't actually go for deep alloc positions */
void
gst_audio_info_init (GstAudioInfo * info)
{
  memset (info, 0, sizeof (GstAudioInfo));
}

void
gst_audio_info_clear (GstAudioInfo * info)
{
  memset (info, 0, sizeof (GstAudioInfo));
}

GstAudioInfo *
gst_audio_info_copy (GstAudioInfo * info)
{
  return (GstAudioInfo *) g_slice_copy (sizeof (GstAudioInfo), info);
}

void
gst_audio_info_free (GstAudioInfo * info)
{
  g_slice_free (GstAudioInfo, info);
}

static void
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

/* from multichannel.c */
void priv_gst_audio_info_fill_default_channel_positions (GstAudioInfo * info);

/**
 * gst_audio_info_from_caps:
 * @info: a #GstAudioInfo
 * @caps: a #GstCaps
 *
 * Parse @caps and update @info.
 *
 * Returns: TRUE if @caps could be parsed
 *
 * Since: 0.10.36
 */
gboolean
gst_audio_info_from_caps (GstAudioInfo * info, const GstCaps * caps)
{
  GstStructure *str;
  GstAudioFormat format;
  gint rate, channels;
  const GValue *pos_val_arr, *pos_val_entry;
  gint i;

  g_return_val_if_fail (info != NULL, FALSE);
  g_return_val_if_fail (caps != NULL, FALSE);
  g_return_val_if_fail (gst_caps_is_fixed (caps), FALSE);

  GST_DEBUG ("parsing caps %" GST_PTR_FORMAT, caps);

  str = gst_caps_get_structure (caps, 0);

  format = gst_audio_format_from_caps_structure (str);
  if (format == GST_AUDIO_FORMAT_UNKNOWN)
    goto unknown_format;

  if (!gst_structure_get_int (str, "rate", &rate))
    goto no_rate;
  if (!gst_structure_get_int (str, "channels", &channels))
    goto no_channels;

  gst_audio_info_set_format (info, format, rate, channels);

  pos_val_arr = gst_structure_get_value (str, "channel-positions");
  if (pos_val_arr) {
    if (channels <= G_N_ELEMENTS (info->position)) {
      for (i = 0; i < channels; i++) {
        pos_val_entry = gst_value_array_get_value (pos_val_arr, i);
        info->position[i] = g_value_get_enum (pos_val_entry);
      }
    } else {
      /* for that many channels, the positions are always NONE */
      for (i = 0; i < G_N_ELEMENTS (info->position); i++)
        info->position[i] = GST_AUDIO_CHANNEL_POSITION_NONE;
      info->flags |= GST_AUDIO_FLAG_DEFAULT_POSITIONS;
    }
  } else {
    info->flags |= GST_AUDIO_FLAG_DEFAULT_POSITIONS;
    priv_gst_audio_info_fill_default_channel_positions (info);
  }

  return TRUE;

  /* ERROR */
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
 *
 * Since: 0.10.36
 */
GstCaps *
gst_audio_info_to_caps (GstAudioInfo * info)
{
  GstCaps *caps;

  g_return_val_if_fail (info != NULL, NULL);
  g_return_val_if_fail (info->finfo != NULL, NULL);
  g_return_val_if_fail (info->finfo->format != GST_AUDIO_FORMAT_UNKNOWN, NULL);

  if (GST_AUDIO_FORMAT_INFO_IS_INTEGER (info->finfo)) {
    caps = gst_caps_new_simple ("audio/x-raw-int",
        "width", G_TYPE_INT, GST_AUDIO_INFO_WIDTH (info),
        "depth", G_TYPE_INT, GST_AUDIO_INFO_DEPTH (info),
        "endianness", G_TYPE_INT,
        GST_AUDIO_FORMAT_INFO_ENDIANNESS (info->finfo), "signed",
        G_TYPE_BOOLEAN, GST_AUDIO_FORMAT_INFO_IS_SIGNED (info->finfo), "rate",
        G_TYPE_INT, GST_AUDIO_INFO_RATE (info), "channels", G_TYPE_INT,
        GST_AUDIO_INFO_CHANNELS (info), NULL);
  } else if (GST_AUDIO_FORMAT_INFO_IS_FLOAT (info->finfo)) {
    caps = gst_caps_new_simple ("audio/x-raw-float",
        "width", G_TYPE_INT, GST_AUDIO_INFO_WIDTH (info),
        "endianness", G_TYPE_INT,
        GST_AUDIO_FORMAT_INFO_ENDIANNESS (info->finfo), "rate", G_TYPE_INT,
        GST_AUDIO_INFO_RATE (info), "channels", G_TYPE_INT,
        GST_AUDIO_INFO_CHANNELS (info), NULL);
  } else {
    GST_ERROR ("unknown audio format, neither integer nor float");
    return NULL;
  }

  if (info->channels > 2) {
    GValue pos_val_arr = { 0 }
    , pos_val_entry = {
    0};
    GstStructure *str;
    gint i;

    /* build gvaluearray from positions */
    g_value_init (&pos_val_arr, GST_TYPE_ARRAY);
    g_value_init (&pos_val_entry, GST_TYPE_AUDIO_CHANNEL_POSITION);
    for (i = 0; i < info->channels; i++) {
      /* if we have many many channels, all positions are NONE */
      if (info->channels <= 64)
        g_value_set_enum (&pos_val_entry, info->position[i]);
      else
        g_value_set_enum (&pos_val_entry, GST_AUDIO_CHANNEL_POSITION_NONE);

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
 *
 * Since: 0.10.36
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
 * @segment: Segment in %GST_FORMAT_TIME or %GST_FORMAT_DEFAULT to which the buffer should be clipped.
 * @rate: sample rate.
 * @frame_size: size of one audio frame in bytes.
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
    gint frame_size)
{
  GstBuffer *ret;
  GstClockTime timestamp = GST_CLOCK_TIME_NONE, duration = GST_CLOCK_TIME_NONE;
  guint64 offset = GST_BUFFER_OFFSET_NONE, offset_end = GST_BUFFER_OFFSET_NONE;
  guint8 *data;
  guint size;

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

  data = GST_BUFFER_DATA (buffer);
  size = GST_BUFFER_SIZE (buffer);

  timestamp = GST_BUFFER_TIMESTAMP (buffer);
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

    gint64 start, stop, cstart, cstop, diff;

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
        data += diff * frame_size;
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
    gint64 start, stop, cstart, cstop, diff;

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

        data += diff * frame_size;
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

  /* Get a metadata writable buffer and apply all changes */
  ret = gst_buffer_make_metadata_writable (buffer);

  GST_BUFFER_TIMESTAMP (ret) = timestamp;
  GST_BUFFER_SIZE (ret) = size;
  GST_BUFFER_DATA (ret) = data;

  if (change_duration)
    GST_BUFFER_DURATION (ret) = duration;
  if (change_offset)
    GST_BUFFER_OFFSET (ret) = offset;
  if (change_offset_end)
    GST_BUFFER_OFFSET_END (ret) = offset_end;

  return ret;
}
