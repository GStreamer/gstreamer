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

#include "gstmpdhelper.h"
#include "gstmpdbaseurlnode.h"

gboolean
gst_mpd_helper_get_mpd_type (xmlNode * a_node,
    const gchar * property_name, GstMPDFileType * property_value)
{
  xmlChar *prop_string;
  gboolean exists = FALSE;

  *property_value = GST_MPD_FILE_TYPE_STATIC;   /* default */
  prop_string = xmlGetProp (a_node, (const xmlChar *) property_name);
  if (prop_string) {
    if (xmlStrcmp (prop_string, (xmlChar *) "OnDemand") == 0
        || xmlStrcmp (prop_string, (xmlChar *) "static") == 0) {
      exists = TRUE;
      *property_value = GST_MPD_FILE_TYPE_STATIC;
      GST_LOG (" - %s: static", property_name);
    } else if (xmlStrcmp (prop_string, (xmlChar *) "Live") == 0
        || xmlStrcmp (prop_string, (xmlChar *) "dynamic") == 0) {
      exists = TRUE;
      *property_value = GST_MPD_FILE_TYPE_DYNAMIC;
      GST_LOG (" - %s: dynamic", property_name);
    } else {
      GST_WARNING ("failed to parse MPD type property %s from xml string %s",
          property_name, prop_string);
    }
    xmlFree (prop_string);
  }

  return exists;
}

gboolean
gst_mpd_helper_get_SAP_type (xmlNode * a_node,
    const gchar * property_name, GstMPDSAPType * property_value)
{
  xmlChar *prop_string;
  guint prop_SAP_type = 0;
  gboolean exists = FALSE;

  prop_string = xmlGetProp (a_node, (const xmlChar *) property_name);
  if (prop_string) {
    if (sscanf ((gchar *) prop_string, "%u", &prop_SAP_type) == 1
        && prop_SAP_type <= 6) {
      exists = TRUE;
      *property_value = (GstMPDSAPType) prop_SAP_type;
      GST_LOG (" - %s: %u", property_name, prop_SAP_type);
    } else {
      GST_WARNING
          ("failed to parse unsigned integer property %s from xml string %s",
          property_name, prop_string);
    }
    xmlFree (prop_string);
  }

  return exists;
}

GstMPDURLType *
gst_mpd_helper_URLType_clone (GstMPDURLType * url)
{

  GstMPDURLType *clone = NULL;

  if (url) {
    clone = g_slice_new0 (GstMPDURLType);
    if (url->sourceURL) {
      clone->sourceURL = xmlMemStrdup (url->sourceURL);
    }
    clone->range = gst_xml_helper_clone_range (url->range);
  }

  return clone;
}

void
gst_mpd_helper_url_type_node_free (GstMPDURLType * url_type_node)
{
  if (url_type_node) {
    if (url_type_node->sourceURL)
      xmlFree (url_type_node->sourceURL);
    g_slice_free (GstXMLRange, url_type_node->range);
    g_slice_free (GstMPDURLType, url_type_node);
  }
}

void
gst_mpd_helper_descriptor_type_free (GstMPDDescriptorType * descriptor_type)
{
  if (descriptor_type) {
    if (descriptor_type->schemeIdUri)
      xmlFree (descriptor_type->schemeIdUri);
    if (descriptor_type->value)
      xmlFree (descriptor_type->value);
    g_slice_free (GstMPDDescriptorType, descriptor_type);
  }
}

void
gst_mpd_helper_segment_base_type_free (GstMPDSegmentBaseType * seg_base_type)
{
  if (seg_base_type) {
    if (seg_base_type->indexRange)
      g_slice_free (GstXMLRange, seg_base_type->indexRange);
    gst_mpd_helper_url_type_node_free (seg_base_type->Initialization);
    gst_mpd_helper_url_type_node_free (seg_base_type->RepresentationIndex);
    g_slice_free (GstMPDSegmentBaseType, seg_base_type);
  }
}

void
gst_mpd_helper_mult_seg_base_type_free (GstMPDMultSegmentBaseType *
    mult_seg_base_type)
{
  if (mult_seg_base_type) {
    /* SegmentBaseType extension */
    gst_mpd_helper_segment_base_type_free (mult_seg_base_type->SegBaseType);
    gst_mpd_segment_timeline_node_free (mult_seg_base_type->SegmentTimeline);
    gst_mpd_helper_url_type_node_free (mult_seg_base_type->BitstreamSwitching);
    g_slice_free (GstMPDMultSegmentBaseType, mult_seg_base_type);
  }
}

void
gst_mpd_helper_representation_base_type_free (GstMPDRepresentationBaseType *
    representation_base)
{
  if (representation_base) {
    if (representation_base->profiles)
      xmlFree (representation_base->profiles);
    g_slice_free (GstXMLRatio, representation_base->sar);
    g_slice_free (GstXMLFrameRate, representation_base->frameRate);
    g_slice_free (GstXMLFrameRate, representation_base->minFrameRate);
    g_slice_free (GstXMLFrameRate, representation_base->maxFrameRate);
    if (representation_base->audioSamplingRate)
      xmlFree (representation_base->audioSamplingRate);
    if (representation_base->mimeType)
      xmlFree (representation_base->mimeType);
    if (representation_base->segmentProfiles)
      xmlFree (representation_base->segmentProfiles);
    if (representation_base->codecs)
      xmlFree (representation_base->codecs);
    if (representation_base->scanType)
      xmlFree (representation_base->scanType);
    g_list_free_full (representation_base->FramePacking,
        (GDestroyNotify) gst_mpd_helper_descriptor_type_free);
    g_list_free_full (representation_base->AudioChannelConfiguration,
        (GDestroyNotify) gst_mpd_helper_descriptor_type_free);
    g_list_free_full (representation_base->ContentProtection,
        (GDestroyNotify) gst_mpd_helper_descriptor_type_free);
    g_slice_free (GstMPDRepresentationBaseType, representation_base);
  }
}

const gchar *
gst_mpd_helper_mimetype_to_caps (const gchar * mimeType)
{
  if (mimeType == NULL)
    return NULL;
  if (strcmp (mimeType, "video/mp2t") == 0) {
    return "video/mpegts, systemstream=(bool) true";
  } else if (strcmp (mimeType, "video/mp4") == 0) {
    return "video/quicktime";
  } else if (strcmp (mimeType, "audio/mp4") == 0) {
    return "audio/x-m4a";
  } else if (strcmp (mimeType, "text/vtt") == 0) {
    return "application/x-subtitle-vtt";
  } else
    return mimeType;
}

/*
 * Combine a base url with the current stream base url from the list of
 * baseURLs. Takes ownership of base and returns a new base.
 */
GstUri *
gst_mpd_helper_combine_urls (GstUri * base, GList * list, gchar ** query,
    guint idx)
{
  GstMPDBaseURLNode *baseURL;
  GstUri *ret = base;

  if (list != NULL) {
    baseURL = g_list_nth_data (list, idx);
    if (!baseURL) {
      baseURL = list->data;
    }

    ret = gst_uri_from_string_with_base (base, baseURL->baseURL);
    gst_uri_unref (base);

    if (ret && query) {
      g_free (*query);
      *query = gst_uri_get_query_string (ret);
      if (*query) {
        ret = gst_uri_make_writable (ret);
        gst_uri_set_query_table (ret, NULL);
      }
    }
  }

  return ret;
}

/* comparison functions */
int
gst_mpd_helper_strncmp_ext (const char *s1, const char *s2)
{
  if (s1 == NULL && s2 == NULL)
    return 0;
  if (s1 == NULL && s2 != NULL)
    return 1;
  if (s2 == NULL && s1 != NULL)
    return 1;
  return strncmp (s1, s2, strlen (s2));
}
