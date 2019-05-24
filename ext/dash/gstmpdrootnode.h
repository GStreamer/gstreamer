/* GStreamer
 *
 * Copyright (C) 2019 Collabora Ltd.
 *   Author: Stéphane Cerveau <scerveau@collabora.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library (COPYING); if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */
#ifndef __GSTMPDROOTNODE_H__
#define __GSTMPDROOTNODE_H__

#include <gst/gst.h>
#include "gstmpdhelper.h"

G_BEGIN_DECLS

#define GST_TYPE_MPD_ROOT_NODE gst_mpd_root_node_get_type ()
#define GST_MPD_ROOT_NODE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_MPD_ROOT_NODE, GstMPDRootNode))
#define GST_MPD_ROOT_NODE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_MPD_ROOT_NODE, GstMPDRootNodeClass))
#define GST_IS_MPD_ROOT_NODE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_MPD_ROOT_NODE))
#define GST_IS_MPD_ROOT_NODE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_MPD_ROOT_NODE))
#define GST_MPD_ROOT_NODE_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_MPD_ROOT_NODE, GstMPDRootNodeClass))

typedef struct _GstMPDRootNode                GstMPDRootNode;
typedef struct _GstMPDRootNodeClass           GstMPDRootNodeClass;


struct _GstMPDRootNode
{
  GstObject     parent_instance;
  gchar *default_namespace;
  gchar *namespace_xsi;
  gchar *namespace_ext;
  gchar *schemaLocation;
  gchar *id;
  gchar *profiles;
  GstMPDFileType type;
  GstDateTime *availabilityStartTime;
  GstDateTime *availabilityEndTime;
  guint64 mediaPresentationDuration;  /* [ms] */
  guint64 minimumUpdatePeriod;        /* [ms] */
  guint64 minBufferTime;              /* [ms] */
  guint64 timeShiftBufferDepth;       /* [ms] */
  guint64 suggestedPresentationDelay; /* [ms] */
  guint64 maxSegmentDuration;         /* [ms] */
  guint64 maxSubsegmentDuration;      /* [ms] */
  /* list of BaseURL nodes */
  GList *BaseURLs;
  /* list of Location nodes */
  GList *Locations;
  /* List of ProgramInformation nodes */
  GList *ProgramInfos;
  /* list of Periods nodes */
  GList *Periods;
  /* list of Metrics nodes */
  GList *Metrics;
  /* list of GstUTCTimingNode nodes */
  GList *UTCTimings;
};

struct _GstMPDRootNodeClass {
  GstObjectClass parent_class;
};


G_GNUC_INTERNAL GType gst_mpd_root_node_get_type (void);

GstMPDRootNode * gst_mpd_root_node_new (void);
void gst_mpd_root_node_free (GstMPDRootNode* self);

G_END_DECLS
#endif /* __GSTMPDROOTNODE_H__ */
