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

/* SMPTE 382M Annex 1 */
#define MXF_METADATA_WAVE_AUDIO_ESSENCE_DESCRIPTOR 0x0148

/* SMPTE 382M Annex 1 */
typedef struct {
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
} MXFMetadataWaveAudioEssenceDescriptor;

gboolean mxf_metadata_wave_audio_essence_descriptor_parse (const MXFUL *key, MXFMetadataWaveAudioEssenceDescriptor *descriptor, const MXFPrimerPack *primer, guint16 type, const guint8 *data, gsize size);
void mxf_metadata_wave_audio_essence_descriptor_reset (MXFMetadataWaveAudioEssenceDescriptor *descriptor);

gboolean mxf_is_aes_bwf_essence_track (const MXFMetadataTrack *track);

GstCaps *
mxf_aes_bwf_create_caps (MXFMetadataGenericPackage *package, MXFMetadataTrack *track, GstTagList **tags, MXFEssenceElementHandler *handler, gpointer *mapping_data);

#endif /* __MXF_AES_BWF_H__ */
