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
 * - Handle more of the metadata inside the descriptors
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
#define MXF_TYPE_METADATA_WAVE_AUDIO_ESSENCE_DESCRIPTOR \
  (mxf_metadata_wave_audio_essence_descriptor_get_type())
#define MXF_METADATA_WAVE_AUDIO_ESSENCE_DESCRIPTOR(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),MXF_TYPE_METADATA_WAVE_AUDIO_ESSENCE_DESCRIPTOR, MXFMetadataWaveAudioEssenceDescriptor))
#define MXF_IS_METADATA_WAVE_AUDIO_ESSENCE_DESCRIPTOR(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),MXF_TYPE_METADATA_WAVE_AUDIO_ESSENCE_DESCRIPTOR))
typedef struct _MXFMetadataWaveAudioEssenceDescriptor
    MXFMetadataWaveAudioEssenceDescriptor;
typedef MXFMetadataBaseClass MXFMetadataWaveAudioEssenceDescriptorClass;
GType mxf_metadata_wave_audio_essence_descriptor_get_type (void);

struct _MXFMetadataWaveAudioEssenceDescriptor
{
  MXFMetadataGenericSoundEssenceDescriptor parent;

  guint16 block_align;
  guint8 sequence_offset;

  guint32 avg_bps;

  MXFUL channel_assignment;

  guint32 peak_envelope_version;
  guint32 peak_envelope_format;
  guint32 points_per_peak_value;
  guint32 peak_envelope_block_size;
  guint32 peak_channels;
  guint32 peak_frames;
  gint64 peak_of_peaks_position;
  MXFTimestamp peak_envelope_timestamp;

  guint8 *peak_envelope_data;
  guint16 peak_envelope_data_length;
};

/* SMPTE 382M Annex 2 */
#define MXF_TYPE_METADATA_AES3_AUDIO_ESSENCE_DESCRIPTOR \
  (mxf_metadata_aes3_audio_essence_descriptor_get_type())
#define MXF_METADATA_AES3_AUDIO_ESSENCE_DESCRIPTOR(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),MXF_TYPE_METADATA_AES3_AUDIO_ESSENCE_DESCRIPTOR, MXFMetadataAES3AudioEssenceDescriptor))
#define MXF_IS_METADATA_AES3_AUDIO_ESSENCE_DESCRIPTOR(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),MXF_TYPE_METADATA_AES3_AUDIO_ESSENCE_DESCRIPTOR))
typedef struct _MXFMetadataAES3AudioEssenceDescriptor
    MXFMetadataAES3AudioEssenceDescriptor;
typedef MXFMetadataBaseClass MXFMetadataAES3AudioEssenceDescriptorClass;
GType mxf_metadata_aes3_audio_essence_descriptor_get_type (void);

struct _MXFMetadataAES3AudioEssenceDescriptor
{
  MXFMetadataWaveAudioEssenceDescriptor parent;

  guint8 emphasis;
  guint16 block_start_offset;
  guint8 auxiliary_bits_mode;

  guint32 n_channel_status_mode;
  guint8 *channel_status_mode;

  guint32 n_fixed_channel_status_data;
  guint8 **fixed_channel_status_data;

  guint32 n_user_data_mode;
  guint8 *user_data_mode;

  guint32 n_fixed_user_data;
  guint8 **fixed_user_data;

  guint32 linked_timecode_track_id;
  guint8 stream_number;
};

/* SMPTE 382M Annex 1 */
G_DEFINE_TYPE (MXFMetadataWaveAudioEssenceDescriptor,
    mxf_metadata_wave_audio_essence_descriptor,
    MXF_TYPE_METADATA_GENERIC_SOUND_ESSENCE_DESCRIPTOR);

static gboolean
mxf_metadata_wave_audio_essence_descriptor_handle_tag (MXFMetadataBase *
    metadata, MXFPrimerPack * primer, guint16 tag, const guint8 * tag_data,
    guint tag_size)
{
  MXFMetadataWaveAudioEssenceDescriptor *self =
      MXF_METADATA_WAVE_AUDIO_ESSENCE_DESCRIPTOR (metadata);
  gboolean ret = TRUE;
  gchar str[48];

  switch (tag) {
    case 0x3d0a:
      if (tag_size != 2)
        goto error;
      self->block_align = GST_READ_UINT16_BE (tag_data);
      GST_DEBUG ("  block align = %u", self->block_align);
      break;
    case 0x3d0b:
      if (tag_size != 1)
        goto error;
      self->sequence_offset = GST_READ_UINT8 (tag_data);
      GST_DEBUG ("  sequence offset = %u", self->sequence_offset);
      break;
    case 0x3d09:
      if (tag_size != 4)
        goto error;
      self->avg_bps = GST_READ_UINT32_BE (tag_data);
      GST_DEBUG ("  average bps = %u", self->avg_bps);
      break;
    case 0x3d32:
      if (tag_size != 16)
        goto error;
      memcpy (&self->channel_assignment, tag_data, 16);
      GST_DEBUG ("  channel assignment = %s",
          mxf_ul_to_string (&self->channel_assignment, str));
      break;
    case 0x3d29:
      if (tag_size != 4)
        goto error;
      self->peak_envelope_version = GST_READ_UINT32_BE (tag_data);
      GST_DEBUG ("  peak envelope version = %u", self->peak_envelope_version);
      break;
    case 0x3d2a:
      if (tag_size != 4)
        goto error;
      self->peak_envelope_format = GST_READ_UINT32_BE (tag_data);
      GST_DEBUG ("  peak envelope format = %u", self->peak_envelope_format);
      break;
    case 0x3d2b:
      if (tag_size != 4)
        goto error;
      self->points_per_peak_value = GST_READ_UINT32_BE (tag_data);
      GST_DEBUG ("  points per peak value = %u", self->points_per_peak_value);
      break;
    case 0x3d2c:
      if (tag_size != 4)
        goto error;
      self->peak_envelope_block_size = GST_READ_UINT32_BE (tag_data);
      GST_DEBUG ("  peak envelope block size = %u",
          self->peak_envelope_block_size);
      break;
    case 0x3d2d:
      if (tag_size != 4)
        goto error;
      self->peak_channels = GST_READ_UINT32_BE (tag_data);
      GST_DEBUG ("  peak channels = %u", self->peak_channels);
      break;
    case 0x3d2e:
      if (tag_size != 4)
        goto error;
      self->peak_frames = GST_READ_UINT32_BE (tag_data);
      GST_DEBUG ("  peak frames = %u", self->peak_frames);
      break;
    case 0x3d2f:
      if (tag_size != 8)
        goto error;
      self->peak_of_peaks_position = GST_READ_UINT64_BE (tag_data);
      GST_DEBUG ("  peak of peaks position = %" G_GINT64_FORMAT,
          self->peak_of_peaks_position);
      break;
    case 0x3d30:
      if (!mxf_timestamp_parse (&self->peak_envelope_timestamp,
              tag_data, tag_size))
        goto error;
      GST_DEBUG ("  peak envelope timestamp = %d/%u/%u %u:%u:%u.%u",
          self->peak_envelope_timestamp.year,
          self->peak_envelope_timestamp.month,
          self->peak_envelope_timestamp.day,
          self->peak_envelope_timestamp.hour,
          self->peak_envelope_timestamp.minute,
          self->peak_envelope_timestamp.second,
          (self->peak_envelope_timestamp.quarter_msecond * 1000) / 256);
      break;
    case 0x3d31:
      self->peak_envelope_data = g_memdup (tag_data, tag_size);
      self->peak_envelope_data_length = tag_size;
      GST_DEBUG ("  peak evelope data size = %u",
          self->peak_envelope_data_length);
      break;
    default:
      ret =
          MXF_METADATA_BASE_CLASS
          (mxf_metadata_wave_audio_essence_descriptor_parent_class)->handle_tag
          (metadata, primer, tag, tag_data, tag_size);
      break;
  }

  return ret;

error:

  GST_ERROR
      ("Invalid wave audio essence descriptor local tag 0x%04x of size %u", tag,
      tag_size);

  return FALSE;
}

static void
    mxf_metadata_wave_audio_essence_descriptor_init
    (MXFMetadataWaveAudioEssenceDescriptor * self)
{

}

static void
    mxf_metadata_wave_audio_essence_descriptor_class_init
    (MXFMetadataWaveAudioEssenceDescriptorClass * klass)
{
  MXFMetadataBaseClass *metadata_base_class = (MXFMetadataBaseClass *) klass;

  metadata_base_class->handle_tag =
      mxf_metadata_wave_audio_essence_descriptor_handle_tag;
}

/* SMPTE 382M Annex 2 */
G_DEFINE_TYPE (MXFMetadataAES3AudioEssenceDescriptor,
    mxf_metadata_aes3_audio_essence_descriptor,
    MXF_TYPE_METADATA_WAVE_AUDIO_ESSENCE_DESCRIPTOR);

static void
mxf_metadata_aes3_audio_essence_descriptor_finalize (GstMiniObject * object)
{
  MXFMetadataAES3AudioEssenceDescriptor *self =
      MXF_METADATA_AES3_AUDIO_ESSENCE_DESCRIPTOR (object);

  g_free (self->channel_status_mode);
  self->channel_status_mode = NULL;
  g_free (self->fixed_channel_status_data);
  self->fixed_channel_status_data = NULL;
  g_free (self->user_data_mode);
  self->user_data_mode = NULL;
  g_free (self->fixed_user_data);
  self->fixed_user_data = NULL;

  GST_MINI_OBJECT_CLASS
      (mxf_metadata_aes3_audio_essence_descriptor_parent_class)->finalize
      (object);
}

static gboolean
mxf_metadata_aes3_audio_essence_descriptor_handle_tag (MXFMetadataBase *
    metadata, MXFPrimerPack * primer, guint16 tag, const guint8 * tag_data,
    guint tag_size)
{
  MXFMetadataAES3AudioEssenceDescriptor *self =
      MXF_METADATA_AES3_AUDIO_ESSENCE_DESCRIPTOR (metadata);
  gboolean ret = TRUE;

  switch (tag) {
    case 0x3d0d:
      if (tag_size != 1)
        goto error;
      self->emphasis = GST_READ_UINT8 (tag_data);
      GST_DEBUG ("  emphasis = %u", self->emphasis);
      break;
    case 0x3d0f:
      if (tag_size != 2)
        goto error;
      self->block_start_offset = GST_READ_UINT16_BE (tag_data);
      GST_DEBUG ("  block start offset = %u", self->block_start_offset);
      break;
    case 0x3d08:
      if (tag_size != 1)
        goto error;
      self->auxiliary_bits_mode = GST_READ_UINT8 (tag_data);
      GST_DEBUG ("  auxiliary bits mode = %u", self->auxiliary_bits_mode);
      break;
    case 0x3d10:{
      guint32 len;
      guint i;

      if (tag_size < 8)
        goto error;
      len = GST_READ_UINT32_BE (tag_data);
      GST_DEBUG ("  number of channel status mode = %u", len);
      self->n_channel_status_mode = len;
      if (len == 0)
        return TRUE;

      if (GST_READ_UINT32_BE (tag_data + 4) != 1)
        goto error;

      tag_data += 8;
      tag_size -= 8;

      if (tag_size != len)
        goto error;

      self->channel_status_mode = g_new0 (guint8, len);

      for (i = 0; i < len; i++) {
        self->channel_status_mode[i] = GST_READ_UINT8 (tag_data);
        GST_DEBUG ("    channel status mode %u = %u", i,
            self->channel_status_mode[i]);
        tag_data++;
        tag_size--;
      }

      break;
    }
    case 0x3d11:{
      guint32 len;
      guint i;

      if (tag_size < 8)
        goto error;
      len = GST_READ_UINT32_BE (tag_data);
      GST_DEBUG ("  number of fixed channel status data = %u", len);
      self->n_fixed_channel_status_data = len;
      if (len == 0)
        return TRUE;

      if (GST_READ_UINT32_BE (tag_data + 4) != 24)
        goto error;

      tag_data += 8;
      tag_size -= 8;

      if (tag_size != len * 24)
        goto error;

      self->fixed_channel_status_data =
          g_malloc0 (len * sizeof (guint8 *) + len * 24);

      for (i = 0; i < len; i++) {
        self->fixed_channel_status_data[i] =
            ((guint8 *) self->fixed_channel_status_data) +
            len * sizeof (guint8 *) + i * 24;

        memcpy (self->fixed_channel_status_data[i], tag_data, 24);
        GST_DEBUG
            ("    fixed channel status data %u = 0x%02x.0x%02x.0x%02x.0x%02x.0x%02x.0x%02x.0x%02x.0x%02x.0x%02x.0x%02x.0x%02x.0x%02x.0x%02x.0x%02x.0x%02x.0x%02x.0x%02x.0x%02x.0x%02x.0x%02x.0x%02x.0x%02x.0x%02x.0x%02x",
            i, self->fixed_channel_status_data[i][0],
            self->fixed_channel_status_data[i][1],
            self->fixed_channel_status_data[i][2],
            self->fixed_channel_status_data[i][3],
            self->fixed_channel_status_data[i][4],
            self->fixed_channel_status_data[i][5],
            self->fixed_channel_status_data[i][6],
            self->fixed_channel_status_data[i][7],
            self->fixed_channel_status_data[i][8],
            self->fixed_channel_status_data[i][9],
            self->fixed_channel_status_data[i][10],
            self->fixed_channel_status_data[i][11],
            self->fixed_channel_status_data[i][12],
            self->fixed_channel_status_data[i][13],
            self->fixed_channel_status_data[i][14],
            self->fixed_channel_status_data[i][15],
            self->fixed_channel_status_data[i][16],
            self->fixed_channel_status_data[i][17],
            self->fixed_channel_status_data[i][18],
            self->fixed_channel_status_data[i][19],
            self->fixed_channel_status_data[i][20],
            self->fixed_channel_status_data[i][21],
            self->fixed_channel_status_data[i][22],
            self->fixed_channel_status_data[i][23]
            );
        tag_data += 24;
        tag_size -= 24;
      }

      break;
    }
    case 0x3d12:{
      guint32 len;
      guint i;

      if (tag_size < 8)
        goto error;
      len = GST_READ_UINT32_BE (tag_data);
      GST_DEBUG ("  number of user data mode = %u", len);
      self->n_user_data_mode = len;
      if (len == 0)
        return TRUE;

      if (GST_READ_UINT32_BE (tag_data + 4) != 1)
        goto error;

      tag_data += 8;
      tag_size -= 8;

      if (tag_size != len)
        goto error;

      self->user_data_mode = g_new0 (guint8, len);

      for (i = 0; i < len; i++) {
        self->user_data_mode[i] = GST_READ_UINT8 (tag_data);
        GST_DEBUG ("    user data mode %u = %u", i, self->user_data_mode[i]);
        tag_data++;
        tag_size--;
      }

      break;
    }
    case 0x3d13:{
      guint32 len;
      guint i;

      if (tag_size < 8)
        goto error;
      len = GST_READ_UINT32_BE (tag_data);
      GST_DEBUG ("  number of fixed user data = %u", len);
      self->n_fixed_user_data = len;
      if (len == 0)
        return TRUE;

      if (GST_READ_UINT32_BE (tag_data + 4) != 24)
        goto error;

      tag_data += 8;
      tag_size -= 8;

      if (tag_size != len * 24)
        goto error;

      self->fixed_user_data = g_malloc0 (len * sizeof (guint8 *) + len * 24);

      for (i = 0; i < len; i++) {
        self->fixed_user_data[i] =
            ((guint8 *) self->fixed_user_data) + len * sizeof (guint8 *) +
            i * 24;

        memcpy (self->fixed_user_data[i], tag_data, 24);
        GST_DEBUG
            ("    fixed user data %u = 0x%02x.0x%02x.0x%02x.0x%02x.0x%02x.0x%02x.0x%02x.0x%02x.0x%02x.0x%02x.0x%02x.0x%02x.0x%02x.0x%02x.0x%02x.0x%02x.0x%02x.0x%02x.0x%02x.0x%02x.0x%02x.0x%02x.0x%02x.0x%02x",
            i, self->fixed_user_data[i][0],
            self->fixed_user_data[i][1],
            self->fixed_user_data[i][2],
            self->fixed_user_data[i][3],
            self->fixed_user_data[i][4],
            self->fixed_user_data[i][5],
            self->fixed_user_data[i][6],
            self->fixed_user_data[i][7],
            self->fixed_user_data[i][8],
            self->fixed_user_data[i][9],
            self->fixed_user_data[i][10],
            self->fixed_user_data[i][11],
            self->fixed_user_data[i][12],
            self->fixed_user_data[i][13],
            self->fixed_user_data[i][14],
            self->fixed_user_data[i][15],
            self->fixed_user_data[i][16],
            self->fixed_user_data[i][17],
            self->fixed_user_data[i][18],
            self->fixed_user_data[i][19],
            self->fixed_user_data[i][20],
            self->fixed_user_data[i][21],
            self->fixed_user_data[i][22], self->fixed_user_data[i][23]
            );
        tag_data += 24;
        tag_size -= 24;
      }

      break;
    }
      /* TODO: linked timecode track / data_stream_number parsing, see
       * SMPTE 382M Annex 2 */
    default:
      ret =
          MXF_METADATA_BASE_CLASS
          (mxf_metadata_aes3_audio_essence_descriptor_parent_class)->handle_tag
          (metadata, primer, tag, tag_data, tag_size);
      break;
  }

  return ret;

error:

  GST_ERROR
      ("Invalid AES3 audio essence descriptor local tag 0x%04x of size %u", tag,
      tag_size);

  return FALSE;
}

static void
    mxf_metadata_aes3_audio_essence_descriptor_init
    (MXFMetadataAES3AudioEssenceDescriptor * self)
{

}

static void
    mxf_metadata_aes3_audio_essence_descriptor_class_init
    (MXFMetadataAES3AudioEssenceDescriptorClass * klass)
{
  MXFMetadataBaseClass *metadata_base_class = (MXFMetadataBaseClass *) klass;
  GstMiniObjectClass *miniobject_class = (GstMiniObjectClass *) klass;

  miniobject_class->finalize =
      mxf_metadata_aes3_audio_essence_descriptor_finalize;
  metadata_base_class->handle_tag =
      mxf_metadata_aes3_audio_essence_descriptor_handle_tag;
}

static gboolean
mxf_is_aes_bwf_essence_track (const MXFMetadataTimelineTrack * track)
{
  guint i;

  g_return_val_if_fail (track != NULL, FALSE);

  if (track->parent.descriptor == NULL) {
    GST_ERROR ("No descriptor for this track");
    return FALSE;
  }

  for (i = 0; i < track->parent.n_descriptor; i++) {
    MXFMetadataFileDescriptor *d = track->parent.descriptor[i];
    MXFUL *key;

    if (!d)
      continue;

    key = &d->essence_container;
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
    GstCaps * caps,
    MXFMetadataTimelineTrack * track,
    MXFMetadataStructuralComponent * component, gpointer mapping_data,
    GstBuffer ** outbuf)
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

static GstFlowReturn
mxf_aes3_handle_essence_element (const MXFUL * key, GstBuffer * buffer,
    GstCaps * caps, MXFMetadataTimelineTrack * track,
    MXFMetadataStructuralComponent * component, gpointer mapping_data,
    GstBuffer ** outbuf)
{
  *outbuf = buffer;

  /* SMPTE 382M Table 1: Check if this is some kind of Wave element */
  if (key->u[12] != 0x16 || (key->u[14] != 0x03 && key->u[14] != 0x04
          && key->u[14] != 0x0c)) {
    GST_ERROR ("Invalid AES3 essence element");
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
mxf_bwf_create_caps (MXFMetadataTimelineTrack * track,
    MXFMetadataGenericSoundEssenceDescriptor * descriptor, GstTagList ** tags,
    MXFEssenceElementHandleFunc * handler, gpointer * mapping_data)
{
  GstCaps *ret = NULL;
  MXFMetadataWaveAudioEssenceDescriptor *wa_descriptor = NULL;
  gchar str[48];
  gchar *codec_name = NULL;

  if (MXF_IS_METADATA_WAVE_AUDIO_ESSENCE_DESCRIPTOR (descriptor))
    wa_descriptor = (MXFMetadataWaveAudioEssenceDescriptor *) descriptor;

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
    if (wa_descriptor && wa_descriptor->block_align != 0)
      block_align = wa_descriptor->block_align;
    else
      block_align =
          (GST_ROUND_UP_8 (descriptor->quantization_bits) *
          descriptor->channel_count) / 8;

    ret = gst_caps_new_simple ("audio/x-raw-int",
        "signed", G_TYPE_BOOLEAN,
        (block_align != 1), "endianness", G_TYPE_INT, G_LITTLE_ENDIAN, "depth",
        G_TYPE_INT, (block_align / descriptor->channel_count) * 8, "width",
        G_TYPE_INT, (block_align / descriptor->channel_count) * 8, NULL);

    mxf_metadata_generic_sound_essence_descriptor_set_caps (descriptor, ret);

    codec_name =
        g_strdup_printf ("Uncompressed %u-bit little endian integer PCM audio",
        (block_align / descriptor->channel_count) * 8);
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

    if (wa_descriptor && wa_descriptor->block_align != 0)
      block_align = wa_descriptor->block_align;
    else
      block_align =
          (GST_ROUND_UP_8 (descriptor->quantization_bits) *
          descriptor->channel_count) / 8;

    ret = gst_caps_new_simple ("audio/x-raw-int",
        "signed", G_TYPE_BOOLEAN,
        (block_align != 1), "endianness", G_TYPE_INT, G_BIG_ENDIAN, "depth",
        G_TYPE_INT, (block_align / descriptor->channel_count) * 8, "width",
        G_TYPE_INT, (block_align / descriptor->channel_count) * 8, NULL);

    mxf_metadata_generic_sound_essence_descriptor_set_caps (descriptor, ret);

    codec_name =
        g_strdup_printf ("Uncompressed %u-bit big endian integer PCM audio",
        (block_align / descriptor->channel_count) * 8);
  } else if (mxf_ul_is_equal (&descriptor->sound_essence_compression,
          &mxf_sound_essence_compression_alaw)) {

    if (descriptor->audio_sampling_rate.n != 0 ||
        descriptor->audio_sampling_rate.d != 0 ||
        descriptor->channel_count != 0) {
      GST_ERROR ("Invalid descriptor");
      return NULL;
    }
    ret = gst_caps_new_simple ("audio/x-alaw", NULL);
    mxf_metadata_generic_sound_essence_descriptor_set_caps (descriptor, ret);

    codec_name = g_strdup ("A-law encoded audio");
  } else {
    GST_ERROR ("Unsupported sound essence compression: %s",
        mxf_ul_to_string (&descriptor->sound_essence_compression, str));
  }

  *handler = mxf_bwf_handle_essence_element;

  if (!*tags)
    *tags = gst_tag_list_new ();

  if (codec_name) {
    gst_tag_list_add (*tags, GST_TAG_MERGE_APPEND, GST_TAG_AUDIO_CODEC,
        codec_name, NULL);
    g_free (codec_name);
  }

  if (wa_descriptor && wa_descriptor->avg_bps)
    gst_tag_list_add (*tags, GST_TAG_MERGE_APPEND, GST_TAG_BITRATE,
        wa_descriptor->avg_bps * 8, NULL);

  return ret;
}

static GstCaps *
mxf_aes3_create_caps (MXFMetadataTimelineTrack * track,
    MXFMetadataGenericSoundEssenceDescriptor * descriptor, GstTagList ** tags,
    MXFEssenceElementHandleFunc * handler, gpointer * mapping_data)
{
  GstCaps *ret = NULL;
  MXFMetadataWaveAudioEssenceDescriptor *wa_descriptor = NULL;
  gchar *codec_name = NULL;
  guint block_align;

  if (MXF_IS_METADATA_WAVE_AUDIO_ESSENCE_DESCRIPTOR (descriptor))
    wa_descriptor = (MXFMetadataWaveAudioEssenceDescriptor *) descriptor;

  /* FIXME: set a channel layout */

  if (descriptor->channel_count == 0 ||
      descriptor->quantization_bits == 0 ||
      descriptor->audio_sampling_rate.n == 0 ||
      descriptor->audio_sampling_rate.d == 0) {
    GST_ERROR ("Invalid descriptor");
    return NULL;
  }
  if (wa_descriptor && wa_descriptor->block_align != 0)
    block_align = wa_descriptor->block_align;
  else
    block_align =
        (GST_ROUND_UP_8 (descriptor->quantization_bits) *
        descriptor->channel_count) / 8;

  ret = gst_caps_new_simple ("audio/x-raw-int",
      "signed", G_TYPE_BOOLEAN,
      (block_align != 1), "endianness", G_TYPE_INT, G_LITTLE_ENDIAN, "depth",
      G_TYPE_INT, (block_align / descriptor->channel_count) * 8, "width",
      G_TYPE_INT, (block_align / descriptor->channel_count) * 8, NULL);

  mxf_metadata_generic_sound_essence_descriptor_set_caps (descriptor, ret);

  codec_name =
      g_strdup_printf ("Uncompressed %u-bit AES3 audio",
      (block_align / descriptor->channel_count) * 8);

  if (!*tags)
    *tags = gst_tag_list_new ();

  gst_tag_list_add (*tags, GST_TAG_MERGE_APPEND, GST_TAG_AUDIO_CODEC,
      codec_name, GST_TAG_BITRATE, block_align * 8, NULL);
  g_free (codec_name);

  *handler = mxf_aes3_handle_essence_element;

  return ret;
}

static GstCaps *
mxf_aes_bwf_create_caps (MXFMetadataTimelineTrack * track, GstTagList ** tags,
    MXFEssenceElementHandleFunc * handler, gpointer * mapping_data)
{
  MXFMetadataGenericSoundEssenceDescriptor *s = NULL;
  gboolean bwf = FALSE;
  guint i;

  g_return_val_if_fail (track != NULL, NULL);

  if (track->parent.descriptor == NULL) {
    GST_ERROR ("No descriptor found for this track");
    return NULL;
  }

  for (i = 0; i < track->parent.n_descriptor; i++) {
    if (!track->parent.descriptor[i])
      continue;

    if (MXF_IS_METADATA_GENERIC_SOUND_ESSENCE_DESCRIPTOR (track->
            parent.descriptor[i])
        && (track->parent.descriptor[i]->essence_container.u[14] == 0x01
            || track->parent.descriptor[i]->essence_container.u[14] == 0x02
            || track->parent.descriptor[i]->essence_container.u[14] == 0x08)) {
      s = (MXFMetadataGenericSoundEssenceDescriptor *) track->
          parent.descriptor[i];
      bwf = TRUE;
      break;
    } else
        if (MXF_IS_METADATA_GENERIC_SOUND_ESSENCE_DESCRIPTOR (track->
            parent.descriptor[i])
        && (track->parent.descriptor[i]->essence_container.u[14] == 0x03
            || track->parent.descriptor[i]->essence_container.u[14] == 0x04
            || track->parent.descriptor[i]->essence_container.u[14] == 0x09)) {

      s = (MXFMetadataGenericSoundEssenceDescriptor *) track->
          parent.descriptor[i];
      bwf = FALSE;
      break;
    }
  }

  if (!s) {
    GST_ERROR ("No descriptor found for this track");
    return NULL;
  } else if (bwf) {
    return mxf_bwf_create_caps (track, s, tags, handler, mapping_data);
  } else {
    return mxf_aes3_create_caps (track, s, tags, handler, mapping_data);
  }

  return NULL;
}

static const MXFEssenceElementHandler mxf_aes_bwf_essence_handler = {
  mxf_is_aes_bwf_essence_track,
  mxf_aes_bwf_create_caps
};

void
mxf_aes_bwf_init (void)
{
  mxf_metadata_register (0x0148,
      MXF_TYPE_METADATA_WAVE_AUDIO_ESSENCE_DESCRIPTOR);
  mxf_metadata_register (0x0147,
      MXF_TYPE_METADATA_AES3_AUDIO_ESSENCE_DESCRIPTOR);

  mxf_essence_element_handler_register (&mxf_aes_bwf_essence_handler);
}
