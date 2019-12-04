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
#include "gstmpdrootnode.h"
#include "gstmpdparser.h"

G_DEFINE_TYPE (GstMPDRootNode, gst_mpd_root_node, GST_TYPE_MPD_NODE);

/* GObject VMethods */

static void
gst_mpd_root_node_finalize (GObject * object)
{
  GstMPDRootNode *self = GST_MPD_ROOT_NODE (object);

  g_free (self->default_namespace);
  g_free (self->namespace_xsi);
  g_free (self->namespace_ext);
  g_free (self->schemaLocation);
  g_free (self->id);
  g_free (self->profiles);

  if (self->availabilityStartTime)
    gst_date_time_unref (self->availabilityStartTime);
  if (self->availabilityEndTime)
    gst_date_time_unref (self->availabilityEndTime);

  g_list_free_full (self->ProgramInfos,
      (GDestroyNotify) gst_mpd_program_information_node_free);
  g_list_free_full (self->BaseURLs, (GDestroyNotify) gst_mpd_baseurl_node_free);
  g_list_free_full (self->Locations,
      (GDestroyNotify) gst_mpd_location_node_free);
  g_list_free_full (self->Periods, (GDestroyNotify) gst_mpd_period_node_free);
  g_list_free_full (self->Metrics, (GDestroyNotify) gst_mpd_metrics_node_free);
  g_list_free_full (self->UTCTimings,
      (GDestroyNotify) gst_mpd_utctiming_node_free);


  G_OBJECT_CLASS (gst_mpd_root_node_parent_class)->finalize (object);
}

/* Base class */

static xmlNodePtr
gst_mpd_root_get_xml_node (GstMPDNode * node)
{
  xmlNodePtr root_xml_node;
  GstMPDRootNode *self = GST_MPD_ROOT_NODE (node);

  root_xml_node = xmlNewNode (NULL, (xmlChar *) "MPD");

  gst_xml_helper_set_prop_string (root_xml_node, "xmlns",
      self->default_namespace);
  gst_xml_helper_set_prop_string (root_xml_node, "profiles", self->profiles);
  gst_xml_helper_set_prop_string (root_xml_node, "schemaLocation",
      self->schemaLocation);
  gst_xml_helper_set_prop_string (root_xml_node, "xmlns:xsi",
      self->namespace_xsi);
  gst_xml_helper_set_prop_string (root_xml_node, "xmlns:ext",
      self->namespace_ext);
  gst_xml_helper_set_prop_string (root_xml_node, "id", self->id);

  if (self->type == GST_MPD_FILE_TYPE_STATIC)
    gst_xml_helper_set_prop_string (root_xml_node, "type", (gchar *) "static");
  else
    gst_xml_helper_set_prop_string (root_xml_node, "type", (gchar *) "dynamic");


  gst_xml_helper_set_prop_date_time (root_xml_node, "availabilityStartTime",
      self->availabilityStartTime);

  gst_xml_helper_set_prop_date_time (root_xml_node, "availabilityEndTime",
      self->availabilityEndTime);

  if (self->mediaPresentationDuration)
    gst_xml_helper_set_prop_duration (root_xml_node,
        "mediaPresentationDuration", self->mediaPresentationDuration);
  if (self->minimumUpdatePeriod)
    gst_xml_helper_set_prop_duration (root_xml_node, "minimumUpdatePeriod",
        self->minimumUpdatePeriod);
  if (self->minimumUpdatePeriod)
    gst_xml_helper_set_prop_duration (root_xml_node, "minBufferTime",
        self->minBufferTime);
  if (self->timeShiftBufferDepth)
    gst_xml_helper_set_prop_duration (root_xml_node, "timeShiftBufferDepth",
        self->timeShiftBufferDepth);
  if (self->suggestedPresentationDelay)
    gst_xml_helper_set_prop_duration (root_xml_node,
        "suggestedPresentationDelay", self->suggestedPresentationDelay);
  if (self->maxSegmentDuration)
    gst_xml_helper_set_prop_duration (root_xml_node, "maxSegmentDuration",
        self->maxSegmentDuration);
  if (self->maxSubsegmentDuration)
    gst_xml_helper_set_prop_duration (root_xml_node, "maxSubsegmentDuration",
        self->maxSubsegmentDuration);

  g_list_foreach (self->BaseURLs, gst_mpd_node_get_list_item, root_xml_node);
  g_list_foreach (self->Locations, gst_mpd_node_get_list_item, root_xml_node);
  g_list_foreach (self->ProgramInfos, gst_mpd_node_get_list_item,
      root_xml_node);
  g_list_foreach (self->Periods, gst_mpd_node_get_list_item, root_xml_node);
  g_list_foreach (self->Metrics, gst_mpd_node_get_list_item, root_xml_node);
  g_list_foreach (self->UTCTimings, gst_mpd_node_get_list_item, root_xml_node);

  return root_xml_node;
}

static gboolean
gst_mpd_root_get_xml_buffer (GstMPDNode * node, gchar ** doc_content,
    gint * doc_size)
{
  xmlDocPtr doc;
  xmlNodePtr root_xml_node;
  xmlChar *xmlbody;

  doc = xmlNewDoc ((xmlChar *) "1.0");
  root_xml_node = gst_mpd_root_get_xml_node (node);
  xmlDocSetRootElement (doc, root_xml_node);

  xmlDocDumpMemory (doc, &xmlbody, doc_size);
  *doc_content = g_strndup ((gchar *) xmlbody, *doc_size);
  xmlFree (xmlbody);

  xmlFreeDoc (doc);
  return TRUE;
}

static void
gst_mpd_root_node_class_init (GstMPDRootNodeClass * klass)
{
  GObjectClass *object_class;
  GstMPDNodeClass *m_klass;

  object_class = G_OBJECT_CLASS (klass);
  m_klass = GST_MPD_NODE_CLASS (klass);

  object_class->finalize = gst_mpd_root_node_finalize;

  m_klass->get_xml_buffer = gst_mpd_root_get_xml_buffer;
  m_klass->get_xml_node = gst_mpd_root_get_xml_node;
}

static void
gst_mpd_root_node_init (GstMPDRootNode * self)
{
  self->default_namespace = NULL;
  self->namespace_xsi = NULL;
  self->namespace_ext = NULL;
  self->schemaLocation = NULL;
  self->id = NULL;
  self->profiles = NULL;
  self->type = GST_MPD_FILE_TYPE_STATIC;
  self->availabilityStartTime = NULL;
  self->availabilityEndTime = NULL;
  self->mediaPresentationDuration = 0;  /* [ms] */
  self->minimumUpdatePeriod = 0;        /* [ms] */
  self->minBufferTime = 2000;   /* [ms] */
  self->timeShiftBufferDepth = 0;       /* [ms] */
  self->suggestedPresentationDelay = 0; /* [ms] */
  self->maxSegmentDuration = 0; /* [ms] */
  self->maxSubsegmentDuration = 0;      /* [ms] */
  /* list of BaseURL nodes */
  self->BaseURLs = NULL;
  /* list of Location nodes */
  self->Locations = NULL;
  /* List of ProgramInformation nodes */
  self->ProgramInfos = NULL;
  /* list of Periods nodes */
  self->Periods = NULL;
  /* list of Metrics nodes */
  self->Metrics = NULL;
  /* list of GstUTCTimingNode nodes */
  self->UTCTimings = NULL;
}

GstMPDRootNode *
gst_mpd_root_node_new (void)
{
  return g_object_new (GST_TYPE_MPD_ROOT_NODE, NULL);
}

void
gst_mpd_root_node_free (GstMPDRootNode * self)
{
  if (self)
    gst_object_unref (self);
}
