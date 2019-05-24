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
#include "gstmpdcontentcomponentnode.h"
#include "gstmpdparser.h"


G_DEFINE_TYPE (GstMPDContentComponentNode, gst_mpd_content_component_node,
    GST_TYPE_OBJECT);

/* GObject VMethods */

static void
gst_mpd_content_component_node_finalize (GObject * object)
{
  GstMPDContentComponentNode *self = GST_MPD_CONTENT_COMPONENT_NODE (object);

  if (self->lang)
    xmlFree (self->lang);
  if (self->contentType)
    xmlFree (self->contentType);
  g_slice_free (GstXMLRatio, self->par);
  g_list_free_full (self->Accessibility,
      (GDestroyNotify) gst_mpd_helper_descriptor_type_free);
  g_list_free_full (self->Role,
      (GDestroyNotify) gst_mpd_helper_descriptor_type_free);
  g_list_free_full (self->Rating,
      (GDestroyNotify) gst_mpd_helper_descriptor_type_free);
  g_list_free_full (self->Viewpoint,
      (GDestroyNotify) gst_mpd_helper_descriptor_type_free);

  G_OBJECT_CLASS (gst_mpd_content_component_node_parent_class)->finalize
      (object);
}

static void
gst_mpd_content_component_node_class_init (GstMPDContentComponentNodeClass *
    klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  object_class->finalize = gst_mpd_content_component_node_finalize;
}

static void
gst_mpd_content_component_node_init (GstMPDContentComponentNode * self)
{
  self->id = 0;
  self->lang = NULL;
  self->contentType = NULL;
  self->par = 0;
  self->Accessibility = 0;
  self->Role = NULL;
  self->Rating = NULL;
  self->Viewpoint = NULL;
}

GstMPDContentComponentNode *
gst_mpd_content_component_node_new (void)
{
  return g_object_new (GST_TYPE_MPD_CONTENT_COMPONENT_NODE, NULL);
}

void
gst_mpd_content_component_node_free (GstMPDContentComponentNode * self)
{
  if (self)
    gst_object_unref (self);
}
