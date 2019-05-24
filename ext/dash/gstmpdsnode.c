/* GStreamer
 *
 * Copyright (C) 2019 Collabora Ltd.
 *   Author: Stéphane Cerveau <scerveau@collabora.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 *
 */
#include "gstmpdsnode.h"
#include "gstmpdparser.h"

G_DEFINE_TYPE (GstMPDSNode, gst_mpd_s_node, GST_TYPE_OBJECT);

/* GObject VMethods */

static void
gst_mpd_s_node_class_init (GstMPDSNodeClass * klass)
{
}

static void
gst_mpd_s_node_init (GstMPDSNode * self)
{
  self->t = 0;
  self->d = 0;
  self->r = 0;
}

GstMPDSNode *
gst_mpd_s_node_new (void)
{
  return g_object_new (GST_TYPE_MPD_S_NODE, NULL);
}

void
gst_mpd_s_node_free (GstMPDSNode * self)
{
  if (self)
    gst_object_unref (self);
}

GstMPDSNode *
gst_mpd_s_node_clone (GstMPDSNode * s_node)
{
  GstMPDSNode *clone = NULL;

  if (s_node) {
    clone = gst_mpd_s_node_new ();
    clone->t = s_node->t;
    clone->d = s_node->d;
    clone->r = s_node->r;
  }

  return clone;
}
