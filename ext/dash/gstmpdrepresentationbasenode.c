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
#include "gstmpdrepresentationbasenode.h"
#include "gstmpdparser.h"

G_DEFINE_TYPE (GstMPDRepresentationBaseNode, gst_mpd_representation_base_node,
    GST_TYPE_MPD_NODE);

/* GObject VMethods */

static void
gst_mpd_representation_base_node_finalize (GObject * object)
{
  GstMPDRepresentationBaseNode *self =
      GST_MPD_REPRESENTATION_BASE_NODE (object);

  if (self->profiles)
    xmlFree (self->profiles);
  g_slice_free (GstXMLRatio, self->sar);
  g_slice_free (GstXMLFrameRate, self->frameRate);
  g_slice_free (GstXMLFrameRate, self->minFrameRate);
  g_slice_free (GstXMLFrameRate, self->maxFrameRate);
  if (self->audioSamplingRate)
    xmlFree (self->audioSamplingRate);
  if (self->mimeType)
    xmlFree (self->mimeType);
  if (self->segmentProfiles)
    xmlFree (self->segmentProfiles);
  if (self->codecs)
    xmlFree (self->codecs);
  if (self->scanType)
    xmlFree (self->scanType);
  g_list_free_full (self->FramePacking,
      (GDestroyNotify) gst_mpd_descriptor_type_node_free);
  g_list_free_full (self->AudioChannelConfiguration,
      (GDestroyNotify) gst_mpd_descriptor_type_node_free);
  g_list_free_full (self->ContentProtection,
      (GDestroyNotify) gst_mpd_descriptor_type_node_free);

  G_OBJECT_CLASS (gst_mpd_representation_base_node_parent_class)->finalize
      (object);
}

/* Base class */

static void
gst_mpd_representation_base_get_xml_node (GstMPDNode * node,
    xmlNodePtr representation_base_node)
{
  GstMPDRepresentationBaseNode *self = GST_MPD_REPRESENTATION_BASE_NODE (node);

  if (self->profiles)
    gst_xml_helper_set_prop_string (representation_base_node, "profiles",
        self->profiles);
  if (self->width)
    gst_xml_helper_set_prop_uint (representation_base_node, "width",
        self->width);
  if (self->height)
    gst_xml_helper_set_prop_uint (representation_base_node, "height",
        self->height);

  gst_xml_helper_set_prop_ratio (representation_base_node, "sar", self->sar);
  gst_xml_helper_set_prop_framerate (representation_base_node, "minFrameRate",
      self->minFrameRate);
  gst_xml_helper_set_prop_framerate (representation_base_node, "maxFrameRate",
      self->maxFrameRate);
  gst_xml_helper_set_prop_framerate (representation_base_node, "frameRate",
      self->frameRate);

  gst_xml_helper_set_prop_string (representation_base_node,
      "audioSamplingRate", self->audioSamplingRate);
  gst_xml_helper_set_prop_string (representation_base_node, "mimeType",
      self->mimeType);
  gst_xml_helper_set_prop_string (representation_base_node, "segmentProfiles",
      self->segmentProfiles);
  gst_xml_helper_set_prop_string (representation_base_node, "codecs",
      self->codecs);
  if (self->maximumSAPPeriod)
    gst_xml_helper_set_prop_double (representation_base_node,
        "maximumSAPPeriod", self->maximumSAPPeriod);
  if (self->startWithSAP)
    gst_xml_helper_set_prop_int (representation_base_node, "startWithSAP",
        self->startWithSAP);
  if (self->maxPlayoutRate)
    gst_xml_helper_set_prop_double (representation_base_node, "maxPlayoutRate",
        self->maxPlayoutRate);
  if (self->codingDependency)
    gst_xml_helper_set_prop_boolean (representation_base_node,
        "codingDependency", self->codingDependency);

  gst_xml_helper_set_prop_string (representation_base_node, "scanType",
      self->scanType);

  g_list_foreach (self->FramePacking,
      gst_mpd_node_get_list_item, representation_base_node);
  g_list_foreach (self->AudioChannelConfiguration,
      gst_mpd_node_get_list_item, representation_base_node);
  g_list_foreach (self->ContentProtection,
      gst_mpd_node_get_list_item, representation_base_node);
}

static void
gst_mpd_representation_base_node_class_init (GstMPDRepresentationBaseNodeClass *
    klass)
{
  GObjectClass *object_class;

  object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = gst_mpd_representation_base_node_finalize;
}

static void
gst_mpd_representation_base_node_init (GstMPDRepresentationBaseNode * self)
{
  self->profiles = NULL;
  self->width = 0;
  self->height = 0;
  self->sar = NULL;
  self->minFrameRate = NULL;
  self->maxFrameRate = NULL;
  self->frameRate = NULL;
  self->audioSamplingRate = NULL;
  self->mimeType = NULL;
  self->segmentProfiles = NULL;
  self->codecs = NULL;
  self->maximumSAPPeriod = 0;
  self->startWithSAP = GST_SAP_TYPE_0;
  self->maxPlayoutRate = 0.0;
  self->codingDependency = FALSE;
  self->scanType = NULL;
  self->FramePacking = NULL;
  self->AudioChannelConfiguration = NULL;
  self->ContentProtection = NULL;
}

void
gst_mpd_representation_base_node_get_list_item (gpointer data,
    gpointer user_data)
{
  GstMPDNode *node = (GstMPDNode *) data;
  xmlNodePtr parent_xml_node = (xmlNodePtr) user_data;
  xmlNodePtr new_xml_node = gst_mpd_node_get_xml_pointer (node);

  gst_mpd_representation_base_get_xml_node (node, new_xml_node);
  xmlAddChild (parent_xml_node, new_xml_node);
}
