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

#ifndef __MXF_DEMUX_H__
#define __MXF_DEMUX_H__

#include <gst/gst.h>
#include <gst/base/gstadapter.h>

#include "mxftypes.h"
#include "mxfparse.h"
#include "mxfmetadata.h"

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

struct _GstMXFDemux
{
  GstElement element;

  GstPad *sinkpad;
  GPtrArray *src;

  GstAdapter *adapter;

  GstSegment segment;

  GstEvent *new_seg_event;
  GstEvent *close_seg_event;

  guint64 offset;

  gboolean random_access;
  gboolean flushing;

  guint64 run_in;

  guint64 header_partition_pack_offset;
  guint64 footer_partition_pack_offset;

  /* MXF file state */
  MXFPartitionPack partition;
  MXFPrimerPack primer;

  GArray *partition_index;
  GArray *index_table;

  /* Structural metadata */
  gboolean update_metadata;
  gboolean pull_footer_metadata;

  gboolean metadata_resolved;
  MXFMetadataPreface *preface;
  GPtrArray *metadata;

  MXFUMID current_package_uid;
  MXFMetadataGenericPackage *current_package;
  gchar *current_package_string;
  gchar *requested_package_string;
};

struct _GstMXFDemuxClass
{
  GstElementClass parent_class;
};

GType gst_mxf_demux_get_type (void);

G_END_DECLS

#endif /* __MXF_DEMUX_H__ */
