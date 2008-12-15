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

#ifndef __MXF_AES_BWF_H__
#define __MXF_AES_BWF_H__

#include <gst/gst.h>

#include "mxfparse.h"
#include "mxfmetadata.h"

/* SMPTE 382M Annex 1 */
#define MXF_TYPE_METADATA_WAVE_AUDIO_ESSENCE_DESCRIPTOR \
  (mxf_metadata_wave_audio_essence_descriptor_get_type())
#define MXF_METADATA_WAVE_AUDIO_ESSENCE_DESCRIPTOR(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),MXF_TYPE_METADATA_WAVE_AUDIO_ESSENCE_DESCRIPTOR, MXFMetadataWaveAudioEssenceDescriptor))
#define MXF_IS_METADATA_WAVE_AUDIO_ESSENCE_DESCRIPTOR(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),MXF_TYPE_METADATA_WAVE_AUDIO_ESSENCE_DESCRIPTOR))
typedef struct _MXFMetadataWaveAudioEssenceDescriptor MXFMetadataWaveAudioEssenceDescriptor;
typedef MXFMetadataBaseClass MXFMetadataWaveAudioEssenceDescriptorClass;
GType mxf_metadata_wave_audio_essence_descriptor_get_type (void);

struct _MXFMetadataWaveAudioEssenceDescriptor {
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
typedef struct _MXFMetadataAES3AudioEssenceDescriptor MXFMetadataAES3AudioEssenceDescriptor;
typedef MXFMetadataBaseClass MXFMetadataAES3AudioEssenceDescriptorClass;
GType mxf_metadata_aes3_audio_essence_descriptor_get_type (void);

struct _MXFMetadataAES3AudioEssenceDescriptor {
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

gboolean mxf_is_aes_bwf_essence_track (const MXFMetadataTrack *track);

GstCaps *
mxf_aes_bwf_create_caps (MXFMetadataGenericPackage *package, MXFMetadataTrack *track, GstTagList **tags, MXFEssenceElementHandler *handler, gpointer *mapping_data);

void mxf_aes_bwf_init (void);

#endif /* __MXF_AES_BWF_H__ */
