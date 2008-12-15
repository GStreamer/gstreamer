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
#include "mxfmetadata.h"

/* SMPTE 381M 8.1 */
#define MXF_TYPE_METADATA_MPEG_VIDEO_DESCRIPTOR \
  (mxf_metadata_mpeg_video_descriptor_get_type())
#define MXF_METADATA_MPEG_VIDEO_DESCRIPTOR(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),MXF_TYPE_METADATA_MPEG_VIDEO_DESCRIPTOR, MXFMetadataMPEGVideoDescriptor))
#define MXF_IS_METADATA_MPEG_VIDEO_DESCRIPTOR(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),MXF_TYPE_METADATA_MPEG_VIDEO_DESCRIPTOR))
typedef struct _MXFMetadataMPEGVideoDescriptor MXFMetadataMPEGVideoDescriptor;
typedef MXFMetadataBaseClass MXFMetadataMPEGVideoDescriptorClass;
GType mxf_metadata_mpeg_video_descriptor_get_type (void);

struct _MXFMetadataMPEGVideoDescriptor {
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
};

gboolean mxf_is_mpeg_essence_track (const MXFMetadataTrack *track);

GstCaps *
mxf_mpeg_create_caps (MXFMetadataGenericPackage *package, MXFMetadataTrack *track, GstTagList **tags, MXFEssenceElementHandler *handler, gpointer *mapping_data);

void mxf_mpeg_init (void);

#endif /* __MXF_MPEG_H__ */
