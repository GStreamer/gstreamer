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
#ifndef __GSTMPDUTCTIMINGNODE_H__
#define __GSTMPDUTCTIMINGNODE_H__

#include <gst/gst.h>
#include "gstxmlhelper.h"

G_BEGIN_DECLS

#define GST_TYPE_MPD_UTCTIMING_NODE gst_mpd_utctiming_node_get_type ()
#define GST_MPD_UTCTIMING_NODE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_MPD_UTCTIMING_NODE, GstMPDUTCTimingNode))
#define GST_MPD_UTCTIMING_NODE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_MPD_UTCTIMING_NODE, GstMPDUTCTimingNodeClass))
#define GST_IS_MPD_UTCTIMING_NODE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_MPD_UTCTIMING_NODE))
#define GST_IS_MPD_UTCTIMING_NODE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_MPD_UTCTIMING_NODE))
#define GST_MPD_UTCTIMING_NODE_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_MPD_UTCTIMING_NODE, GstMPDUTCTimingNodeClass))

typedef struct _GstMPDUTCTimingNode                GstMPDUTCTimingNode;
typedef struct _GstMPDUTCTimingNodeClass           GstMPDUTCTimingNodeClass;


typedef enum
{
  GST_MPD_UTCTIMING_TYPE_UNKNOWN     = 0x00,
  GST_MPD_UTCTIMING_TYPE_NTP         = 0x01,
  GST_MPD_UTCTIMING_TYPE_SNTP        = 0x02,
  GST_MPD_UTCTIMING_TYPE_HTTP_HEAD   = 0x04,
  GST_MPD_UTCTIMING_TYPE_HTTP_XSDATE = 0x08,
  GST_MPD_UTCTIMING_TYPE_HTTP_ISO    = 0x10,
  GST_MPD_UTCTIMING_TYPE_HTTP_NTP    = 0x20,
  GST_MPD_UTCTIMING_TYPE_DIRECT      = 0x40
} GstMPDUTCTimingType;

struct _GstMPDUTCTimingNode
{
  GstObject parent_instance;
  GstMPDUTCTimingType method;
  /* NULL terminated array of strings */
  gchar **urls;
  /* TODO add missing fields such as weight etc.*/
};

struct _GstMPDUTCTimingNodeClass {
  GstObjectClass parent_class;
};


G_GNUC_INTERNAL GType gst_mpd_utctiming_node_get_type (void);

GstMPDUTCTimingNode * gst_mpd_utctiming_node_new (void);
void gst_mpd_utctiming_node_free (GstMPDUTCTimingNode* self);

G_END_DECLS

#endif /* __GSTMPDUTCTIMINGNODE_H__ */
