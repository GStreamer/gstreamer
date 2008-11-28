/* GStreamer
 * Copyright (C) 2008 Sebastian Dröge <sebastian.droege@collabora.co.uk>
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

/* Implementation of SMPTE 382M - Mapping AES3 and Broadcast Wave
 * Audio into the MXF Generic Container
 */

/* TODO:
 * - Handle the case were a track only references specific channels
 *   of the essence (ChannelID property)
 * - Add support for more codecs 
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <string.h>

#include "mxfaes-bwf.h"

GST_DEBUG_CATEGORY_EXTERN (mxf_debug);
#define GST_CAT_DEFAULT mxf_debug

/* SMPTE 382M Annex 1 */
gboolean
    mxf_metadata_wave_audio_essence_descriptor_handle_tag
    (MXFMetadataGenericDescriptor * d, const MXFPrimerPack * primer,
    guint16 tag, const guint8 * tag_data, guint16 tag_size)
{
  MXFMetadataWaveAudioEssenceDescriptor *descriptor =
      (MXFMetadataWaveAudioEssenceDescriptor *) d;
  gboolean ret = FALSE;
  gchar str[48];

  switch (tag) {
    case 0x3d0a:
      if (tag_size != 2)
        goto error;
      descriptor->block_align = GST_READ_UINT16_BE (tag_data);
      GST_DEBUG ("  block align = %u", descriptor->block_align);
      ret = TRUE;
      break;
    case 0x3d0b:
      if (tag_size != 1)
        goto error;
      descriptor->sequence_offset = GST_READ_UINT8 (tag_data);
      GST_DEBUG ("  sequence offset = %u", descriptor->sequence_offset);
      ret = TRUE;
      break;
    case 0x3d09:
      if (tag_size != 4)
        goto error;
      descriptor->avg_bps = GST_READ_UINT32_BE (tag_data);
      GST_DEBUG ("  average bps = %u", descriptor->avg_bps);
      ret = TRUE;
      break;
    case 0x3d32:
      if (tag_size != 16)
        goto error;
      memcpy (&descriptor->channel_assignment, tag_data, 16);
      GST_DEBUG ("  channel assignment = %s",
          mxf_ul_to_string (&descriptor->channel_assignment, str));
      ret = TRUE;
      break;
    case 0x3d29:
      if (tag_size != 4)
        goto error;
      descriptor->peak_envelope_version = GST_READ_UINT32_BE (tag_data);
      GST_DEBUG ("  peak envelope version = %u",
          descriptor->peak_envelope_version);
      ret = TRUE;
      break;
    case 0x3d2a:
      if (tag_size != 4)
        goto error;
      descriptor->peak_envelope_format = GST_READ_UINT32_BE (tag_data);
      GST_DEBUG ("  peak envelope format = %u",
          descriptor->peak_envelope_format);
      ret = TRUE;
      break;
    case 0x3d2b:
      if (tag_size != 4)
        goto error;
      descriptor->points_per_peak_value = GST_READ_UINT32_BE (tag_data);
      GST_DEBUG ("  points per peak value = %u",
          descriptor->points_per_peak_value);
      ret = TRUE;
      break;
    case 0x3d2c:
      if (tag_size != 4)
        goto error;
      descriptor->peak_envelope_block_size = GST_READ_UINT32_BE (tag_data);
      GST_DEBUG ("  peak envelope block size = %u",
          descriptor->peak_envelope_block_size);
      ret = TRUE;
      break;
    case 0x3d2d:
      if (tag_size != 4)
        goto error;
      descriptor->peak_channels = GST_READ_UINT32_BE (tag_data);
      GST_DEBUG ("  peak channels = %u", descriptor->peak_channels);
      ret = TRUE;
      break;
    case 0x3d2e:
      if (tag_size != 4)
        goto error;
      descriptor->peak_frames = GST_READ_UINT32_BE (tag_data);
      GST_DEBUG ("  peak frames = %u", descriptor->peak_frames);
      ret = TRUE;
      break;
    case 0x3d2f:
      if (tag_size != 8)
        goto error;
      descriptor->peak_of_peaks_position = GST_READ_UINT64_BE (tag_data);
      GST_DEBUG ("  peak of peaks position = %" G_GINT64_FORMAT,
          descriptor->peak_of_peaks_position);
      ret = TRUE;
      break;
    case 0x3d30:
      if (!mxf_timestamp_parse (&descriptor->peak_envelope_timestamp,
              tag_data, tag_size))
        goto error;
      GST_DEBUG ("  peak envelope timestamp = %d/%u/%u %u:%u:%u.%u",
          descriptor->peak_envelope_timestamp.year,
          descriptor->peak_envelope_timestamp.month,
          descriptor->peak_envelope_timestamp.day,
          descriptor->peak_envelope_timestamp.hour,
          descriptor->peak_envelope_timestamp.minute,
          descriptor->peak_envelope_timestamp.second,
          (descriptor->peak_envelope_timestamp.quarter_msecond * 1000) / 256);
      ret = TRUE;
      break;
    case 0x3d31:
      descriptor->peak_envelope_data = g_memdup (tag_data, tag_size);
      descriptor->peak_envelope_data_length = tag_size;
      GST_DEBUG ("  peak evelope data size = %u",
          descriptor->peak_envelope_data_length);
      ret = TRUE;
      break;
    default:
      ret =
          mxf_metadata_generic_sound_essence_descriptor_handle_tag (d, primer,
          tag, tag_data, tag_size);
      break;
  }

  return ret;

error:
  GST_ERROR ("Invalid wave audio essence descriptor tag 0x%04x of size %u", tag,
      tag_size);

  return TRUE;
}

void mxf_metadata_wave_audio_essence_descriptor_reset
    (MXFMetadataWaveAudioEssenceDescriptor * descriptor)
{
  g_return_if_fail (descriptor != NULL);

  mxf_metadata_generic_sound_essence_descriptor_reset (
      (MXFMetadataGenericSoundEssenceDescriptor *) descriptor);

  MXF_METADATA_DESCRIPTOR_CLEAR (descriptor,
      MXFMetadataWaveAudioEssenceDescriptor,
      MXFMetadataGenericSoundEssenceDescriptor);
}

gboolean
mxf_is_aes_bwf_essence_track (const MXFMetadataTrack * track)
{
  guint i;

  g_return_val_if_fail (track != NULL, FALSE);

  if (track->descriptor == NULL) {
    GST_ERROR ("No descriptor for this track");
    return FALSE;
  }

  for (i = 0; i < track->n_descriptor; i++) {
    MXFMetadataFileDescriptor *d = track->descriptor[i];
    MXFUL *key = &d->essence_container;
    /* SMPTE 382M 9 */
    if (mxf_is_generic_container_essence_container_label (key) &&
        key->u[12] == 0x02 &&
        key->u[13] == 0x06 &&
        (key->u[14] == 0x01 ||
            key->u[14] == 0x02 ||
            key->u[14] == 0x03 ||
            key->u[14] == 0x04 || key->u[14] == 0x08 || key->u[14] == 0x09))
      return TRUE;
  }


  return FALSE;
}

static GstFlowReturn
mxf_bwf_handle_essence_element (const MXFUL * key, GstBuffer * buffer,
    GstCaps * caps, MXFMetadataGenericPackage * package,
    MXFMetadataTrack * track, MXFMetadataStructuralComponent * component,
    gpointer mapping_data, GstBuffer ** outbuf)
{
  *outbuf = buffer;

  /* SMPTE 382M Table 1: Check if this is some kind of Wave element */
  if (key->u[12] != 0x16 || (key->u[14] != 0x01 && key->u[14] != 0x02
          && key->u[14] != 0x0b)) {
    GST_ERROR ("Invalid BWF essence element");
    return GST_FLOW_ERROR;
  }

  /* FIXME: check if the size is a multiply of the unit size, ... */
  return GST_FLOW_OK;
}

/* SMPTE RP224 */
static const MXFUL mxf_sound_essence_compression_uncompressed =
    { {0x06, 0x0E, 0x2B, 0x34, 0x04, 0x01, 0x01, 0x01, 0x04, 0x02, 0x02, 0x01,
    0x7F, 0x00, 0x00, 0x00}
};

static const MXFUL mxf_sound_essence_compression_aiff =
    { {0x06, 0x0E, 0x2B, 0x34, 0x04, 0x01, 0x01, 0x07, 0x04, 0x02, 0x02, 0x01,
    0x7E, 0x00, 0x00, 0x00}
};

static const MXFUL mxf_sound_essence_compression_alaw =
    { {0x06, 0x0E, 0x2B, 0x34, 0x04, 0x01, 0x01, 0x03, 0x04, 0x02, 0x02, 0x02,
    0x03, 0x01, 0x01, 0x00}
};

static GstCaps *
mxf_bwf_create_caps (MXFMetadataGenericPackage * package,
    MXFMetadataTrack * track,
    MXFMetadataGenericSoundEssenceDescriptor * descriptor, GstTagList ** tags,
    MXFEssenceElementHandler * handler, gpointer * mapping_data)
{
  GstCaps *ret = NULL;
  MXFMetadataWaveAudioEssenceDescriptor *wa_descriptor = NULL;
  gchar str[48];

  if (((MXFMetadataGenericDescriptor *) descriptor)->type ==
      MXF_METADATA_WAVE_AUDIO_ESSENCE_DESCRIPTOR)
    wa_descriptor = (MXFMetadataWaveAudioEssenceDescriptor *) descriptor;

  /* TODO: set caps for avg bitrate, audio codec, ...... */
  /* TODO: Handle width=!depth, needs shifting of samples */

  /* FIXME: set a channel layout */

  if (mxf_ul_is_zero (&descriptor->sound_essence_compression) ||
      mxf_ul_is_equal (&descriptor->sound_essence_compression,
          &mxf_sound_essence_compression_uncompressed)) {
    guint block_align;

    if (descriptor->channel_count == 0 ||
        descriptor->quantization_bits == 0 ||
        descriptor->audio_sampling_rate.n == 0 ||
        descriptor->audio_sampling_rate.d == 0) {
      GST_ERROR ("Invalid descriptor");
      return NULL;
    }
    if (wa_descriptor)
      block_align = wa_descriptor->block_align;
    else
      block_align = GST_ROUND_UP_8 (descriptor->quantization_bits) / 8;

    ret = gst_caps_new_simple ("audio/x-raw-int",
        "rate", G_TYPE_INT,
        (gint) (((gdouble) descriptor->audio_sampling_rate.n) /
            ((gdouble) descriptor->audio_sampling_rate.d) + 0.5), "channels",
        G_TYPE_INT, descriptor->channel_count, "signed", G_TYPE_BOOLEAN,
        (block_align != 1), "endianness", G_TYPE_INT, G_LITTLE_ENDIAN, "depth",
        G_TYPE_INT, (block_align / descriptor->channel_count) * 8, "width",
        G_TYPE_INT, (block_align / descriptor->channel_count) * 8, NULL);
  } else if (mxf_ul_is_equal (&descriptor->sound_essence_compression,
          &mxf_sound_essence_compression_aiff)) {
    guint block_align;

    if (descriptor->channel_count == 0 ||
        descriptor->quantization_bits == 0 ||
        descriptor->audio_sampling_rate.n == 0 ||
        descriptor->audio_sampling_rate.d == 0) {
      GST_ERROR ("Invalid descriptor");
      return NULL;
    }

    if (wa_descriptor)
      block_align = wa_descriptor->block_align;
    else
      block_align = GST_ROUND_UP_8 (descriptor->quantization_bits) / 8;

    ret = gst_caps_new_simple ("audio/x-raw-int",
        "rate", G_TYPE_INT,
        (gint) (((gdouble) descriptor->audio_sampling_rate.n) /
            ((gdouble) descriptor->audio_sampling_rate.d) + 0.5), "channels",
        G_TYPE_INT, descriptor->channel_count, "signed", G_TYPE_BOOLEAN,
        (block_align != 1), "endianness", G_TYPE_INT, G_BIG_ENDIAN, "depth",
        G_TYPE_INT, (block_align / descriptor->channel_count) * 8, "width",
        G_TYPE_INT, (block_align / descriptor->channel_count) * 8, NULL);
  } else if (mxf_ul_is_equal (&descriptor->sound_essence_compression,
          &mxf_sound_essence_compression_alaw)) {

    if (descriptor->audio_sampling_rate.n != 0 ||
        descriptor->audio_sampling_rate.d != 0 ||
        descriptor->channel_count != 0) {
      GST_ERROR ("Invalid descriptor");
      return NULL;
    }
    ret = gst_caps_new_simple ("audio/x-alaw", "rate", G_TYPE_INT,
        (gint) (((gdouble) descriptor->audio_sampling_rate.n) /
            ((gdouble) descriptor->audio_sampling_rate.d) + 0.5),
        "channels", G_TYPE_INT, descriptor->channel_count);
  } else {
    GST_ERROR ("Unsupported sound essence compression: %s",
        mxf_ul_to_string (&descriptor->sound_essence_compression, str));
  }

  *handler = mxf_bwf_handle_essence_element;

  return ret;
}

GstCaps *
mxf_aes_bwf_create_caps (MXFMetadataGenericPackage * package,
    MXFMetadataTrack * track, GstTagList ** tags,
    MXFEssenceElementHandler * handler, gpointer * mapping_data)
{
  MXFMetadataGenericSoundEssenceDescriptor *s = NULL;
  guint i;

  g_return_val_if_fail (package != NULL, NULL);
  g_return_val_if_fail (track != NULL, NULL);

  if (track->descriptor == NULL) {
    GST_ERROR ("No descriptor found for this track");
    return NULL;
  }

  /* TODO: handle AES3 audio */

  for (i = 0; i < track->n_descriptor; i++) {
    if ((track->descriptor[i]->parent.type ==
            MXF_METADATA_WAVE_AUDIO_ESSENCE_DESCRIPTOR
            || track->descriptor[i]->parent.type ==
            MXF_METADATA_GENERIC_SOUND_ESSENCE_DESCRIPTOR)
        && (track->descriptor[i]->essence_container.u[14] == 0x01
            || track->descriptor[i]->essence_container.u[14] == 0x02
            || track->descriptor[i]->essence_container.u[14] == 0x08)) {
      s = (MXFMetadataGenericSoundEssenceDescriptor *) track->descriptor[i];
      break;
    }
  }

  if (!s) {
    GST_ERROR ("No descriptor found for this track");
    return NULL;
  } else if (s) {
    return mxf_bwf_create_caps (package, track, s, tags, handler, mapping_data);
  }

  return NULL;
}
