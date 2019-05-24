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
#ifndef __GSTMPDSUBREPRESENTATIONNODE_H__
#define __GSTMPDSUBREPRESENTATIONNODE_H__

#include <gst/gst.h>
#include "gstmpdhelper.h"

G_BEGIN_DECLS

#define GST_TYPE_MPD_SUB_REPRESENTATION_NODE gst_mpd_sub_representation_node_get_type ()
#define GST_MPD_SUB_REPRESENTATION_NODE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_MPD_SUB_REPRESENTATION_NODE, GstMPDSubRepresentationNode))
#define GST_MPD_SUB_REPRESENTATION_NODE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_MPD_SUB_REPRESENTATION_NODE, GstMPDSubRepresentationNodeClass))
#define GST_IS_MPD_SUB_REPRESENTATION_NODE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_MPD_SUB_REPRESENTATION_NODE))
#define GST_IS_MPD_SUB_REPRESENTATION_NODE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_MPD_SUB_REPRESENTATION_NODE))
#define GST_MPD_SUB_REPRESENTATION_NODE_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_MPD_SUB_REPRESENTATION_NODE, GstMPDSubRepresentationNodeClass))

typedef struct _GstMPDSubRepresentationNode                GstMPDSubRepresentationNode;
typedef struct _GstMPDSubRepresentationNodeClass           GstMPDSubRepresentationNodeClass;


struct _GstMPDSubRepresentationNode
{
  GstObject parent_instance;
  /* RepresentationBase extension */
  GstMPDRepresentationBaseType *RepresentationBase;
  guint level;
  guint *dependencyLevel;            /* UIntVectorType */
  guint size;                        /* size of "dependencyLevel" array */
  guint bandwidth;
  gchar **contentComponent;          /* StringVectorType */
};

struct _GstMPDSubRepresentationNodeClass {
  GstObjectClass parent_class;
};


G_GNUC_INTERNAL GType gst_mpd_sub_representation_node_get_type (void);

GstMPDSubRepresentationNode * gst_mpd_sub_representation_node_new (void);
void gst_mpd_sub_representation_node_free (GstMPDSubRepresentationNode* self);

G_END_DECLS

#endif /* __GSTMPDSUBREPRESENTATIONNODE_H__ */
