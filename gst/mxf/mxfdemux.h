/* GStreamer
 * Copyright (C) 2008-2009 Sebastian Dröge <sebastian.droege@collabora.co.uk>
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifndef __MXF_DEMUX_H__
#define __MXF_DEMUX_H__

#include <gst/gst.h>
#include <gst/base/gstadapter.h>
#include <gst/base/gstflowcombiner.h>

#include "mxfessence.h"

G_BEGIN_DECLS

#define GST_TYPE_MXF_DEMUX \
  (gst_mxf_demux_get_type())
#define GST_MXF_DEMUX(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_MXF_DEMUX,GstMXFDemux))
#define GST_MXF_DEMUX_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_MXF_DEMUX,GstMXFDemuxClass))
#define GST_IS_MXF_DEMUX(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_MXF_DEMUX))
#define GST_IS_MXF_DEMUX_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_MXF_DEMUX))
typedef struct _GstMXFDemux GstMXFDemux;
typedef struct _GstMXFDemuxClass GstMXFDemuxClass;

#define GST_TYPE_MXF_DEMUX_PAD (gst_mxf_demux_pad_get_type())
#define GST_MXF_DEMUX_PAD(pad) (G_TYPE_CHECK_INSTANCE_CAST((pad),GST_TYPE_MXF_DEMUX_PAD,GstMXFDemuxPad))
#define GST_MXF_DEMUX_PAD_CAST(pad) ((GstMXFDemuxPad *) pad)
#define GST_IS_MXF_DEMUX_PAD(pad) (G_TYPE_CHECK_INSTANCE_TYPE((pad),GST_TYPE_MXF_DEMUX_PAD))
typedef struct _GstMXFDemuxPad GstMXFDemuxPad;
typedef struct _GstMXFDemuxPadClass GstMXFDemuxPadClass;

typedef struct
{
  MXFPartitionPack partition;
  MXFPrimerPack primer;
  gboolean parsed_metadata;
  guint64 essence_container_offset;
} GstMXFDemuxPartition;

typedef struct
{
  guint64 offset;
  gboolean keyframe;
} GstMXFDemuxIndex;

typedef struct
{
  guint32 body_sid;
  guint32 track_number;

  guint32 track_id;
  MXFUMID source_package_uid;

  gint64 position;
  gint64 duration;

  GArray *offsets;

  MXFMetadataSourcePackage *source_package;
  MXFMetadataTimelineTrack *source_track;

  gpointer mapping_data;
  const MXFEssenceElementHandler *handler;
  MXFEssenceElementHandleFunc handle_func;

  GstTagList *tags;

  GstCaps *caps;
} GstMXFDemuxEssenceTrack;

struct _GstMXFDemuxPad
{
  GstPad parent;

  guint32 track_id;
  gboolean need_segment;

  GstClockTime position;
  gdouble position_accumulated_error;
  gboolean eos, discont;

  GstTagList *tags;

  MXFMetadataGenericPackage *material_package;
  MXFMetadataTimelineTrack *material_track;

  guint current_component_index;
  MXFMetadataSourceClip *current_component;

  gint64 current_component_start;
  gint64 current_component_duration;

  GstMXFDemuxEssenceTrack *current_essence_track;
  gint64 current_essence_track_position;
};

struct _GstMXFDemuxPadClass
{
  GstPadClass parent;
};

struct _GstMXFDemux
{
  GstElement element;

  GstPad *sinkpad;
  GPtrArray *src;

  /* < private > */
  gboolean have_group_id;
  guint group_id;

  GstAdapter *adapter;

  GstFlowCombiner *flowcombiner;

  GstSegment segment;
  guint32 seqnum;

  GstEvent *close_seg_event;

  guint64 offset;

  gboolean random_access;
  gboolean flushing;

  guint64 run_in;

  guint64 header_partition_pack_offset;
  guint64 footer_partition_pack_offset;

  /* MXF file state */
  GList *partitions;
  GstMXFDemuxPartition *current_partition;

  GArray *essence_tracks;
  GList *pending_index_table_segments;

  GArray *random_index_pack;

  /* Metadata */
  GRWLock metadata_lock;
  gboolean update_metadata;
  gboolean pull_footer_metadata;

  gboolean metadata_resolved;
  MXFMetadataPreface *preface;
  GHashTable *metadata;

  MXFUMID current_package_uid;
  MXFMetadataGenericPackage *current_package;
  gchar *current_package_string;

  GstTagList *tags;

  /* Properties */
  gchar *requested_package_string;
  GstClockTime max_drift;
};

struct _GstMXFDemuxClass
{
  GstElementClass parent_class;
};

GType gst_mxf_demux_get_type (void);

G_END_DECLS

#endif /* __MXF_DEMUX_H__ */
