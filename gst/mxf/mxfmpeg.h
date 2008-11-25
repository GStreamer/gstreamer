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

/* Implementation of SMPTE 381M - Mapping MPEG streams into the MXF
 * Generic Container
 */

#ifndef __MXF_MPEG_H__
#define __MXF_MPEG_H__

#include <gst/gst.h>

#include "mxfparse.h"

/* SMPTE 381M 8.1.1 */
#define MXF_METADATA_MPEG_VIDEO_DESCRIPTOR 0x0151

/* SMPTE 381M 8.1 */
typedef struct {
  MXFMetadataCDCIPictureEssenceDescriptor parent;

  gboolean single_sequence;
  gboolean const_b_frames;
  guint8 coded_content_type;
  gboolean low_delay;

  gboolean closed_gop;
  gboolean identical_gop;
  guint16 max_gop;

  guint16 b_picture_count;
  guint32 bitrate;
  guint8 profile_and_level;
} MXFMetadataMPEGVideoDescriptor;

gboolean mxf_metadata_mpeg_video_descriptor_handle_tag (MXFMetadataGenericDescriptor *descriptor, const MXFPrimerPack *primer, guint16 tag, const guint8 *tag_data, guint16 tag_size);
void mxf_metadata_mpeg_video_descriptor_reset (MXFMetadataMPEGVideoDescriptor *descriptor);

gboolean mxf_is_mpeg_video_essence_track (const MXFMetadataTrack *track);

GstCaps *
mxf_mpeg_video_create_caps (MXFMetadataGenericPackage *package, MXFMetadataTrack *track, GstTagList **tags, MXFEssenceElementHandler *handler, gpointer *mapping_data);

#endif /* __MXF_MPEG_H__ */
