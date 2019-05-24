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
#ifndef __GSTMPDADAPTATIONSETNODE_H__
#define __GSTMPDADAPTATIONSETNODE_H__

#include <gst/gst.h>
#include "gstmpdhelper.h"
#include "gstmpdsegmentlistnode.h"
#include "gstmpdsegmenttemplatenode.h"

G_BEGIN_DECLS

#define GST_TYPE_MPD_ADAPTATION_SET_NODE gst_mpd_adaptation_set_node_get_type ()
#define GST_MPD_ADAPTATION_SET_NODE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_MPD_ADAPTATION_SET_NODE, GstMPDAdaptationSetNode))
#define GST_MPD_ADAPTATION_SET_NODE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_MPD_ADAPTATION_SET_NODE, GstMPDAdaptationSetNodeClass))
#define GST_IS_MPD_ADAPTATION_SET_NODE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_MPD_ADAPTATION_SET_NODE))
#define GST_IS_MPD_ADAPTATION_SET_NODE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_MPD_ADAPTATION_SET_NODE))
#define GST_MPD_ADAPTATION_SET_NODE_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_MPD_ADAPTATION_SET_NODE, GstMPDAdaptationSetNodeClass))

typedef struct _GstMPDAdaptationSetNode                GstMPDAdaptationSetNode;
typedef struct _GstMPDAdaptationSetNodeClass           GstMPDAdaptationSetNodeClass;


struct _GstMPDAdaptationSetNode
{
  GstObject parent_instance;
  guint id;
  guint group;
  gchar *lang;                      /* LangVectorType RFC 5646 */
  gchar *contentType;
  GstXMLRatio *par;
  guint minBandwidth;
  guint maxBandwidth;
  guint minWidth;
  guint maxWidth;
  guint minHeight;
  guint maxHeight;
  GstXMLConditionalUintType *segmentAlignment;
  GstXMLConditionalUintType *subsegmentAlignment;
  GstMPDSAPType subsegmentStartsWithSAP;
  gboolean bitstreamSwitching;
  /* list of Accessibility DescriptorType nodes */
  GList *Accessibility;
  /* list of Role DescriptorType nodes */
  GList *Role;
  /* list of Rating DescriptorType nodes */
  GList *Rating;
  /* list of Viewpoint DescriptorType nodes */
  GList *Viewpoint;
  /* RepresentationBase extension */
  GstMPDRepresentationBaseType *RepresentationBase;
  /* SegmentBase node */
  GstMPDSegmentBaseType *SegmentBase;
  /* SegmentList node */
  GstMPDSegmentListNode *SegmentList;
  /* SegmentTemplate node */
  GstMPDSegmentTemplateNode *SegmentTemplate;
  /* list of BaseURL nodes */
  GList *BaseURLs;
  /* list of Representation nodes */
  GList *Representations;
  /* list of ContentComponent nodes */
  GList *ContentComponents;

  gchar *xlink_href;
  GstMPDXLinkActuate actuate;
};

struct _GstMPDAdaptationSetNodeClass {
  GstObjectClass parent_class;
};


G_GNUC_INTERNAL GType gst_mpd_adaptation_set_node_get_type (void);

GstMPDAdaptationSetNode * gst_mpd_adaptation_set_node_new (void);
void gst_mpd_adaptation_set_node_free (GstMPDAdaptationSetNode* self);

G_END_DECLS

#endif /* __GSTMPDADAPTATIONSETNODE_H__ */
