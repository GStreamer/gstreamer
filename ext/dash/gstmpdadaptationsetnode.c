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
#include "gstmpdadaptationsetnode.h"
#include "gstmpdparser.h"

G_DEFINE_TYPE (GstMPDAdaptationSetNode, gst_mpd_adaptation_set_node,
    GST_TYPE_OBJECT);

/* GObject VMethods */

static void
gst_mpd_adaptation_set_node_finalize (GObject * object)
{
  GstMPDAdaptationSetNode *self = GST_MPD_ADAPTATION_SET_NODE (object);

  if (self->lang)
    xmlFree (self->lang);
  if (self->contentType)
    xmlFree (self->contentType);
  g_slice_free (GstXMLRatio, self->par);
  g_slice_free (GstXMLConditionalUintType, self->segmentAlignment);
  g_slice_free (GstXMLConditionalUintType, self->subsegmentAlignment);
  g_list_free_full (self->Accessibility,
      (GDestroyNotify) gst_mpd_helper_descriptor_type_free);
  g_list_free_full (self->Role,
      (GDestroyNotify) gst_mpd_helper_descriptor_type_free);
  g_list_free_full (self->Rating,
      (GDestroyNotify) gst_mpd_helper_descriptor_type_free);
  g_list_free_full (self->Viewpoint,
      (GDestroyNotify) gst_mpd_helper_descriptor_type_free);
  gst_mpd_helper_representation_base_type_free (self->RepresentationBase);
  gst_mpd_helper_segment_base_type_free (self->SegmentBase);
  gst_mpd_segment_list_node_free (self->SegmentList);
  gst_mpd_segment_template_node_free (self->SegmentTemplate);
  g_list_free_full (self->BaseURLs, (GDestroyNotify) gst_mpd_baseurl_node_free);
  g_list_free_full (self->Representations,
      (GDestroyNotify) gst_mpd_representation_node_free);
  g_list_free_full (self->ContentComponents,
      (GDestroyNotify) gst_mpd_content_component_node_free);
  if (self->xlink_href)
    xmlFree (self->xlink_href);

  G_OBJECT_CLASS (gst_mpd_adaptation_set_node_parent_class)->finalize (object);
}

static void
gst_mpd_adaptation_set_node_class_init (GstMPDAdaptationSetNodeClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  object_class->finalize = gst_mpd_adaptation_set_node_finalize;
}

static void
gst_mpd_adaptation_set_node_init (GstMPDAdaptationSetNode * self)
{
  self->id = 0;
  self->group = 0;
  self->lang = NULL;            /* LangVectorType RFC 5646 */
  self->contentType = NULL;
  self->par = 0;
  self->minBandwidth = 0;
  self->maxBandwidth = 0;
  self->minWidth = 0;
  self->maxWidth = 0;
  self->minHeight = 0;
  self->maxHeight = 0;
  self->segmentAlignment = NULL;
  self->subsegmentAlignment = NULL;
  self->subsegmentStartsWithSAP = GST_SAP_TYPE_0;
  self->bitstreamSwitching = FALSE;
  /* list of Accessibility DescriptorType nodes */
  self->Accessibility = NULL;
  /* list of Role DescriptorType nodes */
  self->Role = NULL;
  /* list of Rating DescriptorType nodes */
  self->Rating = NULL;
  /* list of Viewpoint DescriptorType nodes */
  self->Viewpoint = NULL;
  /* RepresentationBase extension */
  self->RepresentationBase = NULL;
  /* SegmentBase node */
  self->SegmentBase = NULL;
  /* SegmentList node */
  self->SegmentList = NULL;
  /* SegmentTemplate node */
  self->SegmentTemplate = NULL;
  /* list of BaseURL nodes */
  self->BaseURLs = NULL;
  /* list of Representation nodes */
  self->Representations = NULL;
  /* list of ContentComponent nodes */
  self->ContentComponents = NULL;

  self->xlink_href = NULL;
  self->actuate = GST_MPD_XLINK_ACTUATE_ON_REQUEST;
}

GstMPDAdaptationSetNode *
gst_mpd_adaptation_set_node_new (void)
{
  return g_object_new (GST_TYPE_MPD_ADAPTATION_SET_NODE, NULL);
}

void
gst_mpd_adaptation_set_node_free (GstMPDAdaptationSetNode * self)
{
  if (self)
    gst_object_unref (self);
}
