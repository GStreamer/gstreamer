/* GStreamer Opus Encoder
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) <2008> Sebastian Dröge <sebastian.droege@collabora.co.uk>
 * Copyright (C) <2011> Vincent Penquerc'h <vincent.penquerch@collabora.co.uk>
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
#include "config.h"
#endif
#include <gst/tag/tag.h>
#include <gst/base/gstbytewriter.h>
#include "gstopusheader.h"

static GstBuffer *
gst_opus_enc_create_id_buffer (gint nchannels, gint n_stereo_streams,
    gint sample_rate, guint8 channel_mapping_family,
    const guint8 * channel_mapping)
{
  GstBuffer *buffer;
  GstByteWriter bw;

  g_return_val_if_fail (nchannels > 0 && nchannels < 256, NULL);
  g_return_val_if_fail (n_stereo_streams >= 0, NULL);
  g_return_val_if_fail (n_stereo_streams <= nchannels - n_stereo_streams, NULL);

  gst_byte_writer_init (&bw);

  /* See http://wiki.xiph.org/OggOpus */
  gst_byte_writer_put_data (&bw, (const guint8 *) "OpusHead", 8);
  gst_byte_writer_put_uint8 (&bw, 0);   /* version number */
  gst_byte_writer_put_uint8 (&bw, nchannels);
  gst_byte_writer_put_uint16_le (&bw, 0);       /* pre-skip */
  gst_byte_writer_put_uint32_le (&bw, sample_rate);
  gst_byte_writer_put_uint16_le (&bw, 0);       /* output gain */
  gst_byte_writer_put_uint8 (&bw, channel_mapping_family);
  if (channel_mapping_family > 0) {
    gst_byte_writer_put_uint8 (&bw, nchannels - n_stereo_streams);
    gst_byte_writer_put_uint8 (&bw, n_stereo_streams);
    gst_byte_writer_put_data (&bw, channel_mapping, nchannels);
  }

  buffer = gst_byte_writer_reset_and_get_buffer (&bw);

  GST_BUFFER_OFFSET (buffer) = 0;
  GST_BUFFER_OFFSET_END (buffer) = 0;

  return buffer;
}

static GstBuffer *
gst_opus_enc_create_metadata_buffer (const GstTagList * tags)
{
  GstTagList *empty_tags = NULL;
  GstBuffer *comments = NULL;

  GST_DEBUG ("tags = %" GST_PTR_FORMAT, tags);

  if (tags == NULL) {
    /* FIXME: better fix chain of callers to not write metadata at all,
     * if there is none */
    empty_tags = gst_tag_list_new ();
    tags = empty_tags;
  }
  comments =
      gst_tag_list_to_vorbiscomment_buffer (tags, (const guint8 *) "OpusTags",
      8, "Encoded with GStreamer Opusenc");

  GST_BUFFER_OFFSET (comments) = 0;
  GST_BUFFER_OFFSET_END (comments) = 0;

  if (empty_tags)
    gst_tag_list_free (empty_tags);

  return comments;
}

/*
 * (really really) FIXME: move into core (dixit tpm)
 */
/**
 * _gst_caps_set_buffer_array:
 * @caps: a #GstCaps
 * @field: field in caps to set
 * @buf: header buffers
 *
 * Adds given buffers to an array of buffers set as the given @field
 * on the given @caps.  List of buffer arguments must be NULL-terminated.
 *
 * Returns: input caps with a streamheader field added, or NULL if some error
 */
static GstCaps *
_gst_caps_set_buffer_array (GstCaps * caps, const gchar * field,
    GstBuffer * buf, ...)
{
  GstStructure *structure = NULL;
  va_list va;
  GValue array = { 0 };
  GValue value = { 0 };

  g_return_val_if_fail (caps != NULL, NULL);
  g_return_val_if_fail (gst_caps_is_fixed (caps), NULL);
  g_return_val_if_fail (field != NULL, NULL);

  caps = gst_caps_make_writable (caps);
  structure = gst_caps_get_structure (caps, 0);

  g_value_init (&array, GST_TYPE_ARRAY);

  va_start (va, buf);
  /* put buffers in a fixed list */
  while (buf) {
    g_assert (gst_buffer_is_writable (buf));

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

  gst_structure_set_value (structure, field, &array);
  g_value_unset (&array);

  return caps;
}

void
gst_opus_header_create_caps_from_headers (GstCaps ** caps, GSList ** headers,
    GstBuffer * buf1, GstBuffer * buf2)
{
  int n_streams, family;
  gboolean multistream;

  g_return_if_fail (caps);
  g_return_if_fail (headers && !*headers);
  g_return_if_fail (GST_BUFFER_SIZE (buf1) >= 19);

  /* work out the number of streams */
  family = GST_BUFFER_DATA (buf1)[18];
  if (family == 0) {
    n_streams = 1;
  } else {
    /* only included in the header for family > 0 */
    g_return_if_fail (GST_BUFFER_SIZE (buf1) >= 20);
    n_streams = GST_BUFFER_DATA (buf1)[19];
  }

  /* mark and put on caps */
  multistream = n_streams > 1;
  *caps = gst_caps_new_simple ("audio/x-opus",
      "multistream", G_TYPE_BOOLEAN, multistream, NULL);
  *caps = _gst_caps_set_buffer_array (*caps, "streamheader", buf1, buf2, NULL);

  *headers = g_slist_prepend (*headers, buf2);
  *headers = g_slist_prepend (*headers, buf1);
}

void
gst_opus_header_create_caps (GstCaps ** caps, GSList ** headers, gint nchannels,
    gint n_stereo_streams, gint sample_rate, guint8 channel_mapping_family,
    const guint8 * channel_mapping, const GstTagList * tags)
{
  GstBuffer *buf1, *buf2;

  g_return_if_fail (caps);
  g_return_if_fail (headers && !*headers);
  g_return_if_fail (nchannels > 0);
  g_return_if_fail (sample_rate >= 0);  /* 0 -> unset */
  g_return_if_fail (channel_mapping_family == 0 || channel_mapping);

  /* Opus streams in Ogg begin with two headers; the initial header (with
     most of the codec setup parameters) which is mandated by the Ogg
     bitstream spec.  The second header holds any comment fields. */

  /* create header buffers */
  buf1 =
      gst_opus_enc_create_id_buffer (nchannels, n_stereo_streams, sample_rate,
      channel_mapping_family, channel_mapping);
  buf2 = gst_opus_enc_create_metadata_buffer (tags);

  gst_opus_header_create_caps_from_headers (caps, headers, buf1, buf2);
}

gboolean
gst_opus_header_is_header (GstBuffer * buf, const char *magic, guint magic_size)
{
  return (GST_BUFFER_SIZE (buf) >= magic_size
      && !memcmp (magic, GST_BUFFER_DATA (buf), magic_size));
}

gboolean
gst_opus_header_is_id_header (GstBuffer * buf)
{
  gsize size = GST_BUFFER_SIZE (buf);
  const guint8 *data = GST_BUFFER_DATA (buf);
  guint8 channels, channel_mapping_family, n_streams, n_stereo_streams;

  if (size < 19)
    return FALSE;
  if (!gst_opus_header_is_header (buf, "OpusHead", 8))
    return FALSE;
  channels = data[9];
  if (channels == 0)
    return FALSE;
  channel_mapping_family = data[18];
  if (channel_mapping_family == 0) {
    if (channels > 2)
      return FALSE;
  } else {
    channels = data[9];
    if (size < 21 + channels)
      return FALSE;
    n_streams = data[19];
    n_stereo_streams = data[20];
    if (n_streams == 0)
      return FALSE;
    if (n_stereo_streams > n_streams)
      return FALSE;
    if (n_streams + n_stereo_streams > 255)
      return FALSE;
  }
  return TRUE;
}

gboolean
gst_opus_header_is_comment_header (GstBuffer * buf)
{
  return gst_opus_header_is_header (buf, "OpusTags", 8);
}
