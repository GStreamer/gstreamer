/*
 * DASH MPD parsing library
 *
 * gstmpdparser.c
 *
 * Copyright (C) 2012 STMicroelectronics
 *
 * Authors:
 *   Gianluca Gennari <gennarone@gmail.com>
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

#include <string.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include "gstmpdparser.h"
#include "gstdash_debug.h"

#define GST_CAT_DEFAULT gst_dash_demux_debug

/* Property parsing */
static gboolean gst_mpdparser_get_xml_prop_validated_string (xmlNode * a_node,
    const gchar * property_name, gchar ** property_value,
    gboolean (*validator) (const char *));
static gboolean gst_mpdparser_get_xml_prop_string (xmlNode * a_node,
    const gchar * property_name, gchar ** property_value);
static gboolean gst_mpdparser_get_xml_prop_string_stripped (xmlNode * a_node,
    const gchar * property_name, gchar ** property_value);
static gboolean gst_mpdparser_get_xml_ns_prop_string (xmlNode * a_node,
    const gchar * ns_name, const gchar * property_name,
    gchar ** property_value);
static gboolean gst_mpdparser_get_xml_prop_string_vector_type (xmlNode * a_node,
    const gchar * property_name, gchar *** property_value);
static gboolean gst_mpdparser_get_xml_prop_signed_integer (xmlNode * a_node,
    const gchar * property_name, gint default_val, gint * property_value);
static gboolean gst_mpdparser_get_xml_prop_unsigned_integer (xmlNode * a_node,
    const gchar * property_name, guint default_val, guint * property_value);
static gboolean gst_mpdparser_get_xml_prop_unsigned_integer_64 (xmlNode *
    a_node, const gchar * property_name, guint64 default_val,
    guint64 * property_value);
static gboolean gst_mpdparser_get_xml_prop_uint_vector_type (xmlNode * a_node,
    const gchar * property_name, guint ** property_value, guint * value_size);
static gboolean gst_mpdparser_get_xml_prop_double (xmlNode * a_node,
    const gchar * property_name, gdouble * property_value);
static gboolean gst_mpdparser_get_xml_prop_boolean (xmlNode * a_node,
    const gchar * property_name, gboolean default_val,
    gboolean * property_value);
static gboolean gst_mpdparser_get_xml_prop_type (xmlNode * a_node,
    const gchar * property_name, GstMPDFileType * property_value);
static gboolean gst_mpdparser_get_xml_prop_SAP_type (xmlNode * a_node,
    const gchar * property_name, GstSAPType * property_value);
static gboolean gst_mpdparser_get_xml_prop_range (xmlNode * a_node,
    const gchar * property_name, GstRange ** property_value);
static gboolean gst_mpdparser_get_xml_prop_ratio (xmlNode * a_node,
    const gchar * property_name, GstRatio ** property_value);
static gboolean gst_mpdparser_get_xml_prop_framerate (xmlNode * a_node,
    const gchar * property_name, GstFrameRate ** property_value);
static gboolean gst_mpdparser_get_xml_prop_cond_uint (xmlNode * a_node,
    const gchar * property_name, GstConditionalUintType ** property_value);
static gboolean gst_mpdparser_get_xml_prop_dateTime (xmlNode * a_node,
    const gchar * property_name, GstDateTime ** property_value);
static gboolean gst_mpdparser_get_xml_prop_duration (xmlNode * a_node,
    const gchar * property_name, guint64 default_value,
    guint64 * property_value);
static gboolean gst_mpdparser_get_xml_node_content (xmlNode * a_node,
    gchar ** content);
static gchar *gst_mpdparser_get_xml_node_namespace (xmlNode * a_node,
    const gchar * prefix);
static gboolean gst_mpdparser_get_xml_node_as_string (xmlNode * a_node,
    gchar ** content);

/* XML node parsing */
static void gst_mpdparser_parse_baseURL_node (GList ** list, xmlNode * a_node);
static void gst_mpdparser_parse_descriptor_type_node (GList ** list,
    xmlNode * a_node);
static void gst_mpdparser_parse_content_component_node (GList ** list,
    xmlNode * a_node);
static void gst_mpdparser_parse_location_node (GList ** list, xmlNode * a_node);
static void gst_mpdparser_parse_subrepresentation_node (GList ** list,
    xmlNode * a_node);
static void gst_mpdparser_parse_segment_url_node (GList ** list,
    xmlNode * a_node);
static void gst_mpdparser_parse_url_type_node (GstURLType ** pointer,
    xmlNode * a_node);
static void gst_mpdparser_parse_seg_base_type_ext (GstSegmentBaseType **
    pointer, xmlNode * a_node, GstSegmentBaseType * parent);
static void gst_mpdparser_parse_s_node (GQueue * queue, xmlNode * a_node);
static void gst_mpdparser_parse_segment_timeline_node (GstSegmentTimelineNode **
    pointer, xmlNode * a_node);
static gboolean
gst_mpdparser_parse_mult_seg_base_type_ext (GstMultSegmentBaseType ** pointer,
    xmlNode * a_node, GstMultSegmentBaseType * parent);
static gboolean gst_mpdparser_parse_segment_list_node (GstSegmentListNode **
    pointer, xmlNode * a_node, GstSegmentListNode * parent);
static void
gst_mpdparser_parse_representation_base_type (GstRepresentationBaseType **
    pointer, xmlNode * a_node);
static gboolean gst_mpdparser_parse_representation_node (GList ** list,
    xmlNode * a_node, GstAdaptationSetNode * parent,
    GstPeriodNode * period_node);
static gboolean gst_mpdparser_parse_adaptation_set_node (GList ** list,
    xmlNode * a_node, GstPeriodNode * parent);
static void gst_mpdparser_parse_subset_node (GList ** list, xmlNode * a_node);
static gboolean
gst_mpdparser_parse_segment_template_node (GstSegmentTemplateNode ** pointer,
    xmlNode * a_node, GstSegmentTemplateNode * parent);
static gboolean gst_mpdparser_parse_period_node (GList ** list,
    xmlNode * a_node);
static void gst_mpdparser_parse_program_info_node (GList ** list,
    xmlNode * a_node);
static void gst_mpdparser_parse_metrics_range_node (GList ** list,
    xmlNode * a_node);
static void gst_mpdparser_parse_metrics_node (GList ** list, xmlNode * a_node);
static gboolean gst_mpdparser_parse_root_node (GstMPDNode ** pointer,
    xmlNode * a_node);
static void gst_mpdparser_parse_utctiming_node (GList ** list,
    xmlNode * a_node);

/* Helper functions */
static guint convert_to_millisecs (guint decimals, gint pos);
static int strncmp_ext (const char *s1, const char *s2);
static GstStreamPeriod *gst_mpdparser_get_stream_period (GstMpdClient * client);
static GstSNode *gst_mpdparser_clone_s_node (GstSNode * pointer);
static GstSegmentTimelineNode
    * gst_mpdparser_clone_segment_timeline (GstSegmentTimelineNode * pointer);
static GstRange *gst_mpdparser_clone_range (GstRange * range);
static GstURLType *gst_mpdparser_clone_URL (GstURLType * url);
static gchar *gst_mpdparser_parse_baseURL (GstMpdClient * client,
    GstActiveStream * stream, gchar ** query);
static GstSegmentURLNode *gst_mpdparser_clone_segment_url (GstSegmentURLNode *
    seg_url);
static gchar *gst_mpdparser_get_mediaURL (GstActiveStream * stream,
    GstSegmentURLNode * segmentURL);
static const gchar *gst_mpdparser_get_initializationURL (GstActiveStream *
    stream, GstURLType * InitializationURL);
static gchar *gst_mpdparser_build_URL_from_template (const gchar * url_template,
    const gchar * id, guint number, guint bandwidth, guint64 time);
static gboolean gst_mpd_client_add_media_segment (GstActiveStream * stream,
    GstSegmentURLNode * url_node, guint number, gint repeat,
    guint64 scale_start, guint64 scale_duration, GstClockTime start,
    GstClockTime duration);
static const gchar *gst_mpdparser_mimetype_to_caps (const gchar * mimeType);
static GstClockTime gst_mpd_client_get_segment_duration (GstMpdClient * client,
    GstActiveStream * stream, guint64 * scale_duration);
static GstDateTime *gst_mpd_client_get_availability_start_time (GstMpdClient *
    client);

/* Representation */
static GstRepresentationNode *gst_mpdparser_get_lowest_representation (GList *
    Representations);
#if 0
static GstRepresentationNode *gst_mpdparser_get_highest_representation (GList *
    Representations);
static GstRepresentationNode
    * gst_mpdparser_get_representation_with_max_bandwidth (GList *
    Representations, gint max_bandwidth);
#endif
static GstSegmentBaseType *gst_mpdparser_get_segment_base (GstPeriodNode *
    Period, GstAdaptationSetNode * AdaptationSet,
    GstRepresentationNode * Representation);
static GstSegmentListNode *gst_mpdparser_get_segment_list (GstMpdClient *
    client, GstPeriodNode * Period, GstAdaptationSetNode * AdaptationSet,
    GstRepresentationNode * Representation);

/* Segments */
static guint gst_mpd_client_get_segments_counts (GstMpdClient * client,
    GstActiveStream * stream);

/* Memory management */
static GstSegmentTimelineNode *gst_mpdparser_segment_timeline_node_new (void);
static void gst_mpdparser_free_mpd_node (GstMPDNode * mpd_node);
static void gst_mpdparser_free_prog_info_node (GstProgramInformationNode *
    prog_info_node);
static void gst_mpdparser_free_metrics_node (GstMetricsNode * metrics_node);
static void gst_mpdparser_free_metrics_range_node (GstMetricsRangeNode *
    metrics_range_node);
static void gst_mpdparser_free_period_node (GstPeriodNode * period_node);
static void gst_mpdparser_free_subset_node (GstSubsetNode * subset_node);
static void gst_mpdparser_free_segment_template_node (GstSegmentTemplateNode *
    segment_template_node);
static void
gst_mpdparser_free_representation_base_type (GstRepresentationBaseType *
    representation_base);
static void gst_mpdparser_free_adaptation_set_node (GstAdaptationSetNode *
    adaptation_set_node);
static void gst_mpdparser_free_representation_node (GstRepresentationNode *
    representation_node);
static void gst_mpdparser_free_subrepresentation_node (GstSubRepresentationNode
    * subrep_node);
static void gst_mpdparser_free_s_node (GstSNode * s_node);
static void gst_mpdparser_free_segment_timeline_node (GstSegmentTimelineNode *
    seg_timeline);
static void gst_mpdparser_free_url_type_node (GstURLType * url_type_node);
static void gst_mpdparser_free_seg_base_type_ext (GstSegmentBaseType *
    seg_base_type);
static void gst_mpdparser_free_mult_seg_base_type_ext (GstMultSegmentBaseType *
    mult_seg_base_type);
static void gst_mpdparser_free_segment_list_node (GstSegmentListNode *
    segment_list_node);
static void gst_mpdparser_free_segment_url_node (GstSegmentURLNode *
    segment_url);
static void gst_mpdparser_free_base_url_node (GstBaseURL * base_url_node);
static void gst_mpdparser_free_descriptor_type_node (GstDescriptorType *
    descriptor_type);
static void gst_mpdparser_free_content_component_node (GstContentComponentNode *
    content_component_node);
static void gst_mpdparser_free_utctiming_node (GstUTCTimingNode * timing_type);
static void gst_mpdparser_free_stream_period (GstStreamPeriod * stream_period);
static void gst_mpdparser_free_media_segment (GstMediaSegment * media_segment);
static void gst_mpdparser_free_active_stream (GstActiveStream * active_stream);

static GstUri *combine_urls (GstUri * base, GList * list, gchar ** query,
    guint idx);

static GList *gst_mpd_client_fetch_external_period (GstMpdClient * client,
    GstPeriodNode * period_node);
static GList *gst_mpd_client_fetch_external_adaptation_set (GstMpdClient *
    client, GstPeriodNode * period, GstAdaptationSetNode * adapt_set);

struct GstMpdParserUtcTimingMethod
{
  const gchar *name;
  GstMPDUTCTimingType method;
};

static const struct GstMpdParserUtcTimingMethod
    gst_mpdparser_utc_timing_methods[] = {
  {"urn:mpeg:dash:utc:ntp:2014", GST_MPD_UTCTIMING_TYPE_NTP},
  {"urn:mpeg:dash:utc:sntp:2014", GST_MPD_UTCTIMING_TYPE_SNTP},
  {"urn:mpeg:dash:utc:http-head:2014", GST_MPD_UTCTIMING_TYPE_HTTP_HEAD},
  {"urn:mpeg:dash:utc:http-xsdate:2014", GST_MPD_UTCTIMING_TYPE_HTTP_XSDATE},
  {"urn:mpeg:dash:utc:http-iso:2014", GST_MPD_UTCTIMING_TYPE_HTTP_ISO},
  {"urn:mpeg:dash:utc:http-ntp:2014", GST_MPD_UTCTIMING_TYPE_HTTP_NTP},
  {"urn:mpeg:dash:utc:direct:2014", GST_MPD_UTCTIMING_TYPE_DIRECT},
  /*
   * Early working drafts used the :2012 namespace and this namespace is
   * used by some DASH packagers. To work-around these packagers, we also
   * accept the early draft scheme names.
   */
  {"urn:mpeg:dash:utc:ntp:2012", GST_MPD_UTCTIMING_TYPE_NTP},
  {"urn:mpeg:dash:utc:sntp:2012", GST_MPD_UTCTIMING_TYPE_SNTP},
  {"urn:mpeg:dash:utc:http-head:2012", GST_MPD_UTCTIMING_TYPE_HTTP_HEAD},
  {"urn:mpeg:dash:utc:http-xsdate:2012", GST_MPD_UTCTIMING_TYPE_HTTP_XSDATE},
  {"urn:mpeg:dash:utc:http-iso:2012", GST_MPD_UTCTIMING_TYPE_HTTP_ISO},
  {"urn:mpeg:dash:utc:http-ntp:2012", GST_MPD_UTCTIMING_TYPE_HTTP_NTP},
  {"urn:mpeg:dash:utc:direct:2012", GST_MPD_UTCTIMING_TYPE_DIRECT},
  {NULL, 0}
};

/* functions to parse node namespaces, content and properties */
static gboolean
gst_mpdparser_get_xml_prop_validated_string (xmlNode * a_node,
    const gchar * property_name, gchar ** property_value,
    gboolean (*validate) (const char *))
{
  xmlChar *prop_string;
  gboolean exists = FALSE;

  prop_string = xmlGetProp (a_node, (const xmlChar *) property_name);
  if (prop_string) {
    if (validate && !(*validate) ((const char *) prop_string)) {
      GST_WARNING ("Validation failure: %s", prop_string);
      xmlFree (prop_string);
      return FALSE;
    }
    *property_value = (gchar *) prop_string;
    exists = TRUE;
    GST_LOG (" - %s: %s", property_name, prop_string);
  }

  return exists;
}

static gboolean
gst_mpdparser_get_xml_ns_prop_string (xmlNode * a_node,
    const gchar * ns_name, const gchar * property_name, gchar ** property_value)
{
  xmlChar *prop_string;
  gboolean exists = FALSE;

  prop_string =
      xmlGetNsProp (a_node, (const xmlChar *) property_name,
      (const xmlChar *) ns_name);
  if (prop_string) {
    *property_value = (gchar *) prop_string;
    exists = TRUE;
    GST_LOG (" - %s:%s: %s", ns_name, property_name, prop_string);
  }

  return exists;
}

static gboolean
gst_mpdparser_get_xml_prop_string (xmlNode * a_node,
    const gchar * property_name, gchar ** property_value)
{
  return gst_mpdparser_get_xml_prop_validated_string (a_node, property_name,
      property_value, NULL);
}

static gboolean
gst_mpdparser_get_xml_prop_string_stripped (xmlNode * a_node,
    const gchar * property_name, gchar ** property_value)
{
  gboolean ret;
  ret =
      gst_mpdparser_get_xml_prop_string (a_node, property_name, property_value);
  if (ret)
    *property_value = g_strstrip (*property_value);
  return ret;
}

static gboolean
gst_mpdparser_validate_no_whitespace (const char *s)
{
  return !strpbrk (s, "\r\n\t ");
}

static gboolean
gst_mpdparser_get_xml_prop_string_no_whitespace (xmlNode * a_node,
    const gchar * property_name, gchar ** property_value)
{
  return gst_mpdparser_get_xml_prop_validated_string (a_node, property_name,
      property_value, gst_mpdparser_validate_no_whitespace);
}

static gboolean
gst_mpdparser_get_xml_prop_string_vector_type (xmlNode * a_node,
    const gchar * property_name, gchar *** property_value)
{
  xmlChar *prop_string;
  gchar **prop_string_vector = NULL;
  guint i = 0;
  gboolean exists = FALSE;

  prop_string = xmlGetProp (a_node, (const xmlChar *) property_name);
  if (prop_string) {
    prop_string_vector = g_strsplit ((gchar *) prop_string, " ", -1);
    if (prop_string_vector) {
      exists = TRUE;
      *property_value = prop_string_vector;
      GST_LOG (" - %s:", property_name);
      while (prop_string_vector[i]) {
        GST_LOG ("    %s", prop_string_vector[i]);
        i++;
      }
    } else {
      GST_WARNING ("Scan of string vector property failed!");
    }
    xmlFree (prop_string);
  }

  return exists;
}

static gboolean
gst_mpdparser_get_xml_prop_signed_integer (xmlNode * a_node,
    const gchar * property_name, gint default_val, gint * property_value)
{
  xmlChar *prop_string;
  gboolean exists = FALSE;

  *property_value = default_val;
  prop_string = xmlGetProp (a_node, (const xmlChar *) property_name);
  if (prop_string) {
    if (sscanf ((const gchar *) prop_string, "%d", property_value) == 1) {
      exists = TRUE;
      GST_LOG (" - %s: %d", property_name, *property_value);
    } else {
      GST_WARNING
          ("failed to parse signed integer property %s from xml string %s",
          property_name, prop_string);
    }
    xmlFree (prop_string);
  }

  return exists;
}

static gboolean
gst_mpdparser_get_xml_prop_unsigned_integer (xmlNode * a_node,
    const gchar * property_name, guint default_val, guint * property_value)
{
  xmlChar *prop_string;
  gboolean exists = FALSE;

  *property_value = default_val;
  prop_string = xmlGetProp (a_node, (const xmlChar *) property_name);
  if (prop_string) {
    if (sscanf ((gchar *) prop_string, "%u", property_value) == 1 &&
        strstr ((gchar *) prop_string, "-") == NULL) {
      exists = TRUE;
      GST_LOG (" - %s: %u", property_name, *property_value);
    } else {
      GST_WARNING
          ("failed to parse unsigned integer property %s from xml string %s",
          property_name, prop_string);
      /* sscanf might have written to *property_value. Restore to default */
      *property_value = default_val;
    }
    xmlFree (prop_string);
  }

  return exists;
}

static gboolean
gst_mpdparser_get_xml_prop_unsigned_integer_64 (xmlNode * a_node,
    const gchar * property_name, guint64 default_val, guint64 * property_value)
{
  xmlChar *prop_string;
  gboolean exists = FALSE;

  *property_value = default_val;
  prop_string = xmlGetProp (a_node, (const xmlChar *) property_name);
  if (prop_string) {
    if (sscanf ((gchar *) prop_string, "%" G_GUINT64_FORMAT,
            property_value) == 1 &&
        strstr ((gchar *) prop_string, "-") == NULL) {
      exists = TRUE;
      GST_LOG (" - %s: %" G_GUINT64_FORMAT, property_name, *property_value);
    } else {
      GST_WARNING
          ("failed to parse unsigned integer property %s from xml string %s",
          property_name, prop_string);
      /* sscanf might have written to *property_value. Restore to default */
      *property_value = default_val;
    }
    xmlFree (prop_string);
  }

  return exists;
}

static gboolean
gst_mpdparser_get_xml_prop_uint_vector_type (xmlNode * a_node,
    const gchar * property_name, guint ** property_value, guint * value_size)
{
  xmlChar *prop_string;
  gchar **str_vector;
  guint *prop_uint_vector = NULL, i;
  gboolean exists = FALSE;

  prop_string = xmlGetProp (a_node, (const xmlChar *) property_name);
  if (prop_string) {
    str_vector = g_strsplit ((gchar *) prop_string, " ", -1);
    if (str_vector) {
      *value_size = g_strv_length (str_vector);
      prop_uint_vector = g_malloc (*value_size * sizeof (guint));
      if (prop_uint_vector) {
        exists = TRUE;
        GST_LOG (" - %s:", property_name);
        for (i = 0; i < *value_size; i++) {
          if (sscanf ((gchar *) str_vector[i], "%u", &prop_uint_vector[i]) == 1
              && strstr (str_vector[i], "-") == NULL) {
            GST_LOG ("    %u", prop_uint_vector[i]);
          } else {
            GST_WARNING
                ("failed to parse uint vector type property %s from xml string %s",
                property_name, str_vector[i]);
            /* there is no special value to put in prop_uint_vector[i] to
             * signal it is invalid, so we just clean everything and return
             * FALSE
             */
            g_free (prop_uint_vector);
            prop_uint_vector = NULL;
            exists = FALSE;
            break;
          }
        }
        *property_value = prop_uint_vector;
      } else {
        GST_WARNING ("Array allocation failed!");
      }
    } else {
      GST_WARNING ("Scan of uint vector property failed!");
    }
    xmlFree (prop_string);
    g_strfreev (str_vector);
  }

  return exists;
}

static gboolean
gst_mpdparser_get_xml_prop_double (xmlNode * a_node,
    const gchar * property_name, gdouble * property_value)
{
  xmlChar *prop_string;
  gboolean exists = FALSE;

  prop_string = xmlGetProp (a_node, (const xmlChar *) property_name);
  if (prop_string) {
    if (sscanf ((gchar *) prop_string, "%lf", property_value) == 1) {
      exists = TRUE;
      GST_LOG (" - %s: %lf", property_name, *property_value);
    } else {
      GST_WARNING ("failed to parse double property %s from xml string %s",
          property_name, prop_string);
    }
    xmlFree (prop_string);
  }

  return exists;
}

static gboolean
gst_mpdparser_get_xml_prop_boolean (xmlNode * a_node,
    const gchar * property_name, gboolean default_val,
    gboolean * property_value)
{
  xmlChar *prop_string;
  gboolean exists = FALSE;

  *property_value = default_val;
  prop_string = xmlGetProp (a_node, (const xmlChar *) property_name);
  if (prop_string) {
    if (xmlStrcmp (prop_string, (xmlChar *) "false") == 0) {
      exists = TRUE;
      *property_value = FALSE;
      GST_LOG (" - %s: false", property_name);
    } else if (xmlStrcmp (prop_string, (xmlChar *) "true") == 0) {
      exists = TRUE;
      *property_value = TRUE;
      GST_LOG (" - %s: true", property_name);
    } else {
      GST_WARNING ("failed to parse boolean property %s from xml string %s",
          property_name, prop_string);
    }
    xmlFree (prop_string);
  }

  return exists;
}

static gboolean
gst_mpdparser_get_xml_prop_type (xmlNode * a_node,
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

static gboolean
gst_mpdparser_get_xml_prop_SAP_type (xmlNode * a_node,
    const gchar * property_name, GstSAPType * property_value)
{
  xmlChar *prop_string;
  guint prop_SAP_type = 0;
  gboolean exists = FALSE;

  prop_string = xmlGetProp (a_node, (const xmlChar *) property_name);
  if (prop_string) {
    if (sscanf ((gchar *) prop_string, "%u", &prop_SAP_type) == 1
        && prop_SAP_type <= 6) {
      exists = TRUE;
      *property_value = (GstSAPType) prop_SAP_type;
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

static gboolean
gst_mpdparser_get_xml_prop_range (xmlNode * a_node, const gchar * property_name,
    GstRange ** property_value)
{
  xmlChar *prop_string;
  guint64 first_byte_pos = 0, last_byte_pos = -1;
  guint len, pos;
  gchar *str;
  gboolean exists = FALSE;

  prop_string = xmlGetProp (a_node, (const xmlChar *) property_name);
  if (prop_string) {
    len = xmlStrlen (prop_string);
    str = (gchar *) prop_string;
    GST_TRACE ("range: %s, len %d", str, len);

    /* read "-" */
    pos = strcspn (str, "-");
    if (pos >= len) {
      GST_TRACE ("pos %d >= len %d", pos, len);
      goto error;
    }
    /* read first_byte_pos */
    if (pos != 0) {
      /* replace str[pos] with '\0' to allow sscanf to not be confused by
       * the minus sign (eg " -1" (observe the space before -) would otherwise
       * be interpreted as range -1 to 1)
       */
      str[pos] = 0;
      if (sscanf (str, "%" G_GUINT64_FORMAT, &first_byte_pos) != 1 ||
          strstr (str, "-") != NULL) {
        /* sscanf failed or it found a negative number */
        /* restore the '-' sign */
        str[pos] = '-';
        goto error;
      }
      /* restore the '-' sign */
      str[pos] = '-';
    }
    /* read last_byte_pos */
    if (pos < (len - 1)) {
      if (sscanf (str + pos + 1, "%" G_GUINT64_FORMAT, &last_byte_pos) != 1 ||
          strstr (str + pos + 1, "-") != NULL) {
        goto error;
      }
    }
    /* malloc return data structure */
    *property_value = g_slice_new0 (GstRange);
    exists = TRUE;
    (*property_value)->first_byte_pos = first_byte_pos;
    (*property_value)->last_byte_pos = last_byte_pos;
    xmlFree (prop_string);
    GST_LOG (" - %s: %" G_GUINT64_FORMAT "-%" G_GUINT64_FORMAT,
        property_name, first_byte_pos, last_byte_pos);
  }

  return exists;

error:
  GST_WARNING ("failed to parse property %s from xml string %s", property_name,
      prop_string);
  xmlFree (prop_string);
  return FALSE;
}

static gboolean
gst_mpdparser_get_xml_prop_ratio (xmlNode * a_node,
    const gchar * property_name, GstRatio ** property_value)
{
  xmlChar *prop_string;
  guint num = 0, den = 1;
  guint len, pos;
  gchar *str;
  gboolean exists = FALSE;

  prop_string = xmlGetProp (a_node, (const xmlChar *) property_name);
  if (prop_string) {
    len = xmlStrlen (prop_string);
    str = (gchar *) prop_string;
    GST_TRACE ("ratio: %s, len %d", str, len);

    /* read ":" */
    pos = strcspn (str, ":");
    if (pos >= len) {
      GST_TRACE ("pos %d >= len %d", pos, len);
      goto error;
    }
    /* search for negative sign */
    if (strstr (str, "-") != NULL) {
      goto error;
    }
    /* read num */
    if (pos != 0) {
      if (sscanf (str, "%u", &num) != 1) {
        goto error;
      }
    }
    /* read den */
    if (pos < (len - 1)) {
      if (sscanf (str + pos + 1, "%u", &den) != 1) {
        goto error;
      }
    }
    /* malloc return data structure */
    *property_value = g_slice_new0 (GstRatio);
    exists = TRUE;
    (*property_value)->num = num;
    (*property_value)->den = den;
    xmlFree (prop_string);
    GST_LOG (" - %s: %u:%u", property_name, num, den);
  }

  return exists;

error:
  GST_WARNING ("failed to parse property %s from xml string %s", property_name,
      prop_string);
  xmlFree (prop_string);
  return FALSE;
}

static gboolean
gst_mpdparser_get_xml_prop_framerate (xmlNode * a_node,
    const gchar * property_name, GstFrameRate ** property_value)
{
  xmlChar *prop_string;
  guint num = 0, den = 1;
  guint len, pos;
  gchar *str;
  gboolean exists = FALSE;

  prop_string = xmlGetProp (a_node, (const xmlChar *) property_name);
  if (prop_string) {
    len = xmlStrlen (prop_string);
    str = (gchar *) prop_string;
    GST_TRACE ("framerate: %s, len %d", str, len);

    /* search for negative sign */
    if (strstr (str, "-") != NULL) {
      goto error;
    }

    /* read "/" if available */
    pos = strcspn (str, "/");
    /* read num */
    if (pos != 0) {
      if (sscanf (str, "%u", &num) != 1) {
        goto error;
      }
    }
    /* read den (if available) */
    if (pos < (len - 1)) {
      if (sscanf (str + pos + 1, "%u", &den) != 1) {
        goto error;
      }
    }
    /* alloc return data structure */
    *property_value = g_slice_new0 (GstFrameRate);
    exists = TRUE;
    (*property_value)->num = num;
    (*property_value)->den = den;
    xmlFree (prop_string);
    if (den == 1)
      GST_LOG (" - %s: %u", property_name, num);
    else
      GST_LOG (" - %s: %u/%u", property_name, num, den);
  }

  return exists;

error:
  GST_WARNING ("failed to parse property %s from xml string %s", property_name,
      prop_string);
  xmlFree (prop_string);
  return FALSE;
}

static gboolean
gst_mpdparser_get_xml_prop_cond_uint (xmlNode * a_node,
    const gchar * property_name, GstConditionalUintType ** property_value)
{
  xmlChar *prop_string;
  gchar *str;
  gboolean flag;
  guint val;
  gboolean exists = FALSE;

  prop_string = xmlGetProp (a_node, (const xmlChar *) property_name);
  if (prop_string) {
    str = (gchar *) prop_string;
    GST_TRACE ("conditional uint: %s", str);

    if (strcmp (str, "false") == 0) {
      flag = FALSE;
      val = 0;
    } else if (strcmp (str, "true") == 0) {
      flag = TRUE;
      val = 0;
    } else {
      flag = TRUE;
      if (sscanf (str, "%u", &val) != 1 || strstr (str, "-") != NULL)
        goto error;
    }

    /* alloc return data structure */
    *property_value = g_slice_new0 (GstConditionalUintType);
    exists = TRUE;
    (*property_value)->flag = flag;
    (*property_value)->value = val;
    xmlFree (prop_string);
    GST_LOG (" - %s: flag=%s val=%u", property_name, flag ? "true" : "false",
        val);
  }

  return exists;

error:
  GST_WARNING ("failed to parse property %s from xml string %s", property_name,
      prop_string);
  xmlFree (prop_string);
  return FALSE;
}

/*
  DateTime Data Type

  The dateTime data type is used to specify a date and a time.

  The lexical form of xs:dateTime is YYYY-MM-DDThh:mm:ss[Z|(+|-)hh:mm]

    * YYYY indicates the year
    * MM indicates the month
    * DD indicates the day
    * T indicates the start of the required time section
    * hh indicates the hour
    * mm indicates the minute
    * ss indicates the second

  The time zone may be specified as Z (UTC) or (+|-)hh:mm
*/
static gboolean
gst_mpdparser_get_xml_prop_dateTime (xmlNode * a_node,
    const gchar * property_name, GstDateTime ** property_value)
{
  xmlChar *prop_string;
  gchar *str;
  gint ret, pos;
  gint year, month, day, hour, minute;
  gdouble second;
  gboolean exists = FALSE;
  gfloat tzoffset = 0.0;
  gint gmt_offset_hour = -99, gmt_offset_min = -99;

  prop_string = xmlGetProp (a_node, (const xmlChar *) property_name);
  if (prop_string) {
    str = (gchar *) prop_string;
    GST_TRACE ("dateTime: %s, len %d", str, xmlStrlen (prop_string));
    /* parse year */
    ret = sscanf (str, "%d", &year);
    if (ret != 1 || year <= 0)
      goto error;
    pos = strcspn (str, "-");
    str += (pos + 1);
    GST_TRACE (" - year %d", year);
    /* parse month */
    ret = sscanf (str, "%d", &month);
    if (ret != 1 || month <= 0)
      goto error;
    pos = strcspn (str, "-");
    str += (pos + 1);
    GST_TRACE (" - month %d", month);
    /* parse day */
    ret = sscanf (str, "%d", &day);
    if (ret != 1 || day <= 0)
      goto error;
    pos = strcspn (str, "T");
    str += (pos + 1);
    GST_TRACE (" - day %d", day);
    /* parse hour */
    ret = sscanf (str, "%d", &hour);
    if (ret != 1 || hour < 0)
      goto error;
    pos = strcspn (str, ":");
    str += (pos + 1);
    GST_TRACE (" - hour %d", hour);
    /* parse minute */
    ret = sscanf (str, "%d", &minute);
    if (ret != 1 || minute < 0)
      goto error;
    pos = strcspn (str, ":");
    str += (pos + 1);
    GST_TRACE (" - minute %d", minute);
    /* parse second */
    ret = sscanf (str, "%lf", &second);
    if (ret != 1 || second < 0)
      goto error;
    GST_TRACE (" - second %lf", second);

    GST_LOG (" - %s: %4d/%02d/%02d %02d:%02d:%09.6lf", property_name,
        year, month, day, hour, minute, second);

    if (strrchr (str, '+') || strrchr (str, '-')){
      /* reuse some code from gst-plugins-base/gst-libs/gst/tag/gstxmptag.c */
      gint gmt_offset = -1;
      gchar *plus_pos = NULL;
      gchar *neg_pos = NULL;
      gchar *pos = NULL;

      GST_LOG ("Checking for timezone information");

      /* check if there is timezone info */
      plus_pos = strrchr (str, '+');
      neg_pos = strrchr (str, '-');
      if (plus_pos)
        pos = plus_pos + 1;
      else if (neg_pos)
        pos = neg_pos + 1;

      if (pos && strlen (pos) >= 3) {
        gint ret_tz;
        if (pos[2] == ':')
          ret_tz = sscanf (pos, "%d:%d", &gmt_offset_hour, &gmt_offset_min);
        else
          ret_tz = sscanf (pos, "%02d%02d", &gmt_offset_hour, &gmt_offset_min);

        GST_DEBUG ("Parsing timezone: %s", pos);

        if (ret_tz == 2) {
          if (neg_pos != NULL && neg_pos + 1 == pos) {
            gmt_offset_hour *= -1;
            gmt_offset_min *= -1;
          }
          gmt_offset = gmt_offset_hour * 60 + gmt_offset_min;

          tzoffset = gmt_offset / 60.0;

          GST_LOG ("Timezone offset: %f (%d minutes)", tzoffset, gmt_offset);
        } else
          GST_WARNING ("Failed to parse timezone information");
      }
    }

    exists = TRUE;
    *property_value =
        gst_date_time_new (tzoffset, year, month, day, hour, minute, second);
    xmlFree (prop_string);
  }

  return exists;

error:
  GST_WARNING ("failed to parse property %s from xml string %s", property_name,
      prop_string);
  xmlFree (prop_string);
  return FALSE;
}


/*
  Duration Data Type

  The duration data type is used to specify a time interval.

  The time interval is specified in the following form "-PnYnMnDTnHnMnS" where:

    * - indicates the negative sign (optional)
    * P indicates the period (required)
    * nY indicates the number of years
    * nM indicates the number of months
    * nD indicates the number of days
    * T indicates the start of a time section (required if you are going to specify hours, minutes, or seconds)
    * nH indicates the number of hours
    * nM indicates the number of minutes
    * nS indicates the number of seconds
*/

/* this function computes decimals * 10 ^ (3 - pos) */
static guint
convert_to_millisecs (guint decimals, gint pos)
{
  guint num = 1, den = 1;
  gint i = 3 - pos;

  while (i < 0) {
    den *= 10;
    i++;
  }
  while (i > 0) {
    num *= 10;
    i--;
  }
  /* if i == 0 we have exactly 3 decimals and nothing to do */
  return decimals * num / den;
}

static gboolean
accumulate (guint64 * v, guint64 mul, guint64 add)
{
  guint64 tmp;

  if (*v > G_MAXUINT64 / mul)
    return FALSE;
  tmp = *v * mul;
  if (tmp > G_MAXUINT64 - add)
    return FALSE;
  *v = tmp + add;
  return TRUE;
}

static gboolean
gst_mpdparser_parse_duration (const char *str, guint64 * value)
{
  gint ret, len, pos, posT;
  gint years = -1, months = -1, days = -1, hours = -1, minutes = -1, seconds =
      -1, decimals = -1, read;
  gboolean have_ms = FALSE;
  guint64 tmp_value;

  len = strlen (str);
  GST_TRACE ("duration: %s, len %d", str, len);
  if (strspn (str, "PT0123456789., \tHMDSY") < len) {
    GST_WARNING ("Invalid character found: '%s'", str);
    goto error;
  }
  /* skip leading/trailing whitespace */
  while (g_ascii_isspace (str[0])) {
    str++;
    len--;
  }
  while (len > 0 && g_ascii_isspace (str[len - 1]))
    --len;

  /* read "P" for period */
  if (str[0] != 'P') {
    GST_WARNING ("P not found at the beginning of the string!");
    goto error;
  }
  str++;
  len--;

  /* read "T" for time (if present) */
  posT = strcspn (str, "T");
  len -= posT;
  if (posT > 0) {
    /* there is some room between P and T, so there must be a period section */
    /* read years, months, days */
    do {
      GST_TRACE ("parsing substring %s", str);
      pos = strcspn (str, "YMD");
      ret = sscanf (str, "%u", &read);
      if (ret != 1) {
        GST_WARNING ("can not read integer value from string %s!", str);
        goto error;
      }
      switch (str[pos]) {
        case 'Y':
          if (years != -1 || months != -1 || days != -1) {
            GST_WARNING ("year, month or day was already set");
            goto error;
          }
          years = read;
          break;
        case 'M':
          if (months != -1 || days != -1) {
            GST_WARNING ("month or day was already set");
            goto error;
          }
          months = read;
          if (months >= 12) {
            GST_WARNING ("Month out of range");
            goto error;
          }
          break;
        case 'D':
          if (days != -1) {
            GST_WARNING ("day was already set");
            goto error;
          }
          days = read;
          if (days >= 31) {
            GST_WARNING ("Day out of range");
            goto error;
          }
          break;
        default:
          GST_WARNING ("unexpected char %c!", str[pos]);
          goto error;
          break;
      }
      GST_TRACE ("read number %u type %c", read, str[pos]);
      str += (pos + 1);
      posT -= (pos + 1);
    } while (posT > 0);
  }

  if (years == -1)
    years = 0;
  if (months == -1)
    months = 0;
  if (days == -1)
    days = 0;

  GST_TRACE ("Y:M:D=%d:%d:%d", years, months, days);

  /* read "T" for time (if present) */
  /* here T is at pos == 0 */
  str++;
  len--;
  pos = 0;
  if (pos < len) {
    /* T found, there is a time section */
    /* read hours, minutes, seconds, hundredths of second */
    do {
      GST_TRACE ("parsing substring %s", str);
      pos = strcspn (str, "HMS,.");
      ret = sscanf (str, "%u", &read);
      if (ret != 1) {
        GST_WARNING ("can not read integer value from string %s!", str);
        goto error;
      }
      switch (str[pos]) {
        case 'H':
          if (hours != -1 || minutes != -1 || seconds != -1) {
            GST_WARNING ("hour, minute or second was already set");
            goto error;
          }
          hours = read;
          if (hours >= 24) {
            GST_WARNING ("Hour out of range");
            goto error;
          }
          break;
        case 'M':
          if (minutes != -1 || seconds != -1) {
            GST_WARNING ("minute or second was already set");
            goto error;
          }
          minutes = read;
          if (minutes >= 60) {
            GST_WARNING ("Minute out of range");
            goto error;
          }
          break;
        case 'S':
          if (have_ms) {
            /* we have read the decimal part of the seconds */
            decimals = convert_to_millisecs (read, pos);
            GST_TRACE ("decimal number %u (%d digits) -> %d ms", read, pos,
                decimals);
          } else {
            if (seconds != -1) {
              GST_WARNING ("second was already set");
              goto error;
            }
            /* no decimals */
            seconds = read;
          }
          break;
        case '.':
        case ',':
          /* we have read the integer part of a decimal number in seconds */
          if (seconds != -1) {
            GST_WARNING ("second was already set");
            goto error;
          }
          seconds = read;
          have_ms = TRUE;
          break;
        default:
          GST_WARNING ("unexpected char %c!", str[pos]);
          goto error;
          break;
      }
      GST_TRACE ("read number %u type %c", read, str[pos]);
      str += pos + 1;
      len -= (pos + 1);
    } while (len > 0);
  }

  if (hours == -1)
    hours = 0;
  if (minutes == -1)
    minutes = 0;
  if (seconds == -1)
    seconds = 0;
  if (decimals == -1)
    decimals = 0;
  GST_TRACE ("H:M:S.MS=%d:%d:%d.%03d", hours, minutes, seconds, decimals);

  tmp_value = 0;
  if (!accumulate (&tmp_value, 1, years)
      || !accumulate (&tmp_value, 365, months * 30)
      || !accumulate (&tmp_value, 1, days)
      || !accumulate (&tmp_value, 24, hours)
      || !accumulate (&tmp_value, 60, minutes)
      || !accumulate (&tmp_value, 60, seconds)
      || !accumulate (&tmp_value, 1000, decimals))
    goto error;

  /* ensure it can be converted from milliseconds to nanoseconds */
  if (tmp_value > G_MAXUINT64 / 1000000)
    goto error;

  *value = tmp_value;
  return TRUE;

error:
  return FALSE;
}

static gboolean
gst_mpdparser_get_xml_prop_duration (xmlNode * a_node,
    const gchar * property_name, guint64 default_value,
    guint64 * property_value)
{
  xmlChar *prop_string;
  gchar *str;
  gboolean exists = FALSE;

  *property_value = default_value;
  prop_string = xmlGetProp (a_node, (const xmlChar *) property_name);
  if (prop_string) {
    str = (gchar *) prop_string;
    if (!gst_mpdparser_parse_duration (str, property_value))
      goto error;
    GST_LOG (" - %s: %" G_GUINT64_FORMAT, property_name, *property_value);
    xmlFree (prop_string);
    exists = TRUE;
  }
  return exists;

error:
  xmlFree (prop_string);
  return FALSE;
}

static gboolean
gst_mpdparser_get_xml_node_content (xmlNode * a_node, gchar ** content)
{
  xmlChar *node_content = NULL;
  gboolean exists = FALSE;

  node_content = xmlNodeGetContent (a_node);
  if (node_content) {
    exists = TRUE;
    *content = (gchar *) node_content;
    GST_LOG (" - %s: %s", a_node->name, *content);
  }

  return exists;
}

static gboolean
gst_mpdparser_get_xml_node_as_string (xmlNode * a_node, gchar ** content)
{
  gboolean exists = FALSE;
  const char *txt_encoding;
  xmlOutputBufferPtr out_buf;

  txt_encoding = (const char *) a_node->doc->encoding;
  out_buf = xmlAllocOutputBuffer (NULL);
  g_assert (out_buf != NULL);
  xmlNodeDumpOutput (out_buf, a_node->doc, a_node, 0, 0, txt_encoding);
  xmlOutputBufferFlush (out_buf);
#ifdef LIBXML2_NEW_BUFFER
  if (xmlOutputBufferGetSize (out_buf) > 0) {
    *content =
        (gchar *) xmlStrndup (xmlOutputBufferGetContent (out_buf),
        xmlOutputBufferGetSize (out_buf));
    exists = TRUE;
  }
#else
  if (out_buf->conv && out_buf->conv->use > 0) {
    *content =
        (gchar *) xmlStrndup (out_buf->conv->content, out_buf->conv->use);
    exists = TRUE;
  } else if (out_buf->buffer && out_buf->buffer->use > 0) {
    *content =
        (gchar *) xmlStrndup (out_buf->buffer->content, out_buf->buffer->use);
    exists = TRUE;
  }
#endif // LIBXML2_NEW_BUFFER
  (void) xmlOutputBufferClose (out_buf);

  if (exists) {
    GST_LOG (" - %s: %s", a_node->name, *content);
  }
  return exists;
}

static gchar *
gst_mpdparser_get_xml_node_namespace (xmlNode * a_node, const gchar * prefix)
{
  xmlNs *curr_ns;
  gchar *namespace = NULL;

  if (prefix == NULL) {
    /* return the default namespace */
    if (a_node->ns) {
      namespace = xmlMemStrdup ((const gchar *) a_node->ns->href);
      if (namespace) {
        GST_LOG (" - default namespace: %s", namespace);
      }
    }
  } else {
    /* look for the specified prefix in the namespace list */
    for (curr_ns = a_node->ns; curr_ns; curr_ns = curr_ns->next) {
      if (xmlStrcmp (curr_ns->prefix, (xmlChar *) prefix) == 0) {
        namespace = xmlMemStrdup ((const gchar *) curr_ns->href);
        if (namespace) {
          GST_LOG (" - %s namespace: %s", curr_ns->prefix, curr_ns->href);
        }
      }
    }
  }

  return namespace;
}

static void
gst_mpdparser_parse_baseURL_node (GList ** list, xmlNode * a_node)
{
  GstBaseURL *new_base_url;

  new_base_url = g_slice_new0 (GstBaseURL);
  *list = g_list_append (*list, new_base_url);

  GST_LOG ("content of BaseURL node:");
  gst_mpdparser_get_xml_node_content (a_node, &new_base_url->baseURL);

  GST_LOG ("attributes of BaseURL node:");
  gst_mpdparser_get_xml_prop_string (a_node, "serviceLocation",
      &new_base_url->serviceLocation);
  gst_mpdparser_get_xml_prop_string (a_node, "byteRange",
      &new_base_url->byteRange);
}

static void
gst_mpdparser_parse_descriptor_type_node (GList ** list, xmlNode * a_node)
{
  GstDescriptorType *new_descriptor;

  new_descriptor = g_slice_new0 (GstDescriptorType);
  *list = g_list_append (*list, new_descriptor);

  GST_LOG ("attributes of %s node:", a_node->name);
  gst_mpdparser_get_xml_prop_string_stripped (a_node, "schemeIdUri",
      &new_descriptor->schemeIdUri);
  if (!gst_mpdparser_get_xml_prop_string (a_node, "value",
          &new_descriptor->value)) {
    /* if no value attribute, use XML string representation of the node */
    gst_mpdparser_get_xml_node_as_string (a_node, &new_descriptor->value);
  }
}

static void
gst_mpdparser_parse_content_component_node (GList ** list, xmlNode * a_node)
{
  xmlNode *cur_node;
  GstContentComponentNode *new_content_component;

  new_content_component = g_slice_new0 (GstContentComponentNode);
  *list = g_list_append (*list, new_content_component);

  GST_LOG ("attributes of ContentComponent node:");
  gst_mpdparser_get_xml_prop_unsigned_integer (a_node, "id", 0,
      &new_content_component->id);
  gst_mpdparser_get_xml_prop_string (a_node, "lang",
      &new_content_component->lang);
  gst_mpdparser_get_xml_prop_string (a_node, "contentType",
      &new_content_component->contentType);
  gst_mpdparser_get_xml_prop_ratio (a_node, "par", &new_content_component->par);

  /* explore children nodes */
  for (cur_node = a_node->children; cur_node; cur_node = cur_node->next) {
    if (cur_node->type == XML_ELEMENT_NODE) {
      if (xmlStrcmp (cur_node->name, (xmlChar *) "Accessibility") == 0) {
        gst_mpdparser_parse_descriptor_type_node
            (&new_content_component->Accessibility, cur_node);
      } else if (xmlStrcmp (cur_node->name, (xmlChar *) "Role") == 0) {
        gst_mpdparser_parse_descriptor_type_node (&new_content_component->Role,
            cur_node);
      } else if (xmlStrcmp (cur_node->name, (xmlChar *) "Rating") == 0) {
        gst_mpdparser_parse_descriptor_type_node
            (&new_content_component->Rating, cur_node);
      } else if (xmlStrcmp (cur_node->name, (xmlChar *) "Viewpoint") == 0) {
        gst_mpdparser_parse_descriptor_type_node
            (&new_content_component->Viewpoint, cur_node);
      }
    }
  }
}

static void
gst_mpdparser_parse_location_node (GList ** list, xmlNode * a_node)
{
  gchar *location = NULL;

  GST_LOG ("content of Location node:");
  if (gst_mpdparser_get_xml_node_content (a_node, &location))
    *list = g_list_append (*list, location);
}

static void
gst_mpdparser_parse_subrepresentation_node (GList ** list, xmlNode * a_node)
{
  GstSubRepresentationNode *new_subrep;

  new_subrep = g_slice_new0 (GstSubRepresentationNode);
  *list = g_list_append (*list, new_subrep);

  GST_LOG ("attributes of SubRepresentation node:");
  gst_mpdparser_get_xml_prop_unsigned_integer (a_node, "level", 0,
      &new_subrep->level);
  gst_mpdparser_get_xml_prop_uint_vector_type (a_node, "dependencyLevel",
      &new_subrep->dependencyLevel, &new_subrep->size);
  gst_mpdparser_get_xml_prop_unsigned_integer (a_node, "bandwidth", 0,
      &new_subrep->bandwidth);
  gst_mpdparser_get_xml_prop_string_vector_type (a_node,
      "contentComponent", &new_subrep->contentComponent);

  /* RepresentationBase extension */
  gst_mpdparser_parse_representation_base_type (&new_subrep->RepresentationBase,
      a_node);
}

static GstSegmentURLNode *
gst_mpdparser_clone_segment_url (GstSegmentURLNode * seg_url)
{
  GstSegmentURLNode *clone = NULL;

  if (seg_url) {
    clone = g_slice_new0 (GstSegmentURLNode);
    clone->media = xmlMemStrdup (seg_url->media);
    clone->mediaRange = gst_mpdparser_clone_range (seg_url->mediaRange);
    clone->index = xmlMemStrdup (seg_url->index);
    clone->indexRange = gst_mpdparser_clone_range (seg_url->indexRange);
  }

  return clone;
}

static void
gst_mpdparser_parse_segment_url_node (GList ** list, xmlNode * a_node)
{
  GstSegmentURLNode *new_segment_url;

  new_segment_url = g_slice_new0 (GstSegmentURLNode);
  *list = g_list_append (*list, new_segment_url);

  GST_LOG ("attributes of SegmentURL node:");
  gst_mpdparser_get_xml_prop_string (a_node, "media", &new_segment_url->media);
  gst_mpdparser_get_xml_prop_range (a_node, "mediaRange",
      &new_segment_url->mediaRange);
  gst_mpdparser_get_xml_prop_string (a_node, "index", &new_segment_url->index);
  gst_mpdparser_get_xml_prop_range (a_node, "indexRange",
      &new_segment_url->indexRange);
}

static void
gst_mpdparser_parse_url_type_node (GstURLType ** pointer, xmlNode * a_node)
{
  GstURLType *new_url_type;

  gst_mpdparser_free_url_type_node (*pointer);
  *pointer = new_url_type = g_slice_new0 (GstURLType);

  GST_LOG ("attributes of URLType node:");
  gst_mpdparser_get_xml_prop_string (a_node, "sourceURL",
      &new_url_type->sourceURL);
  gst_mpdparser_get_xml_prop_range (a_node, "range", &new_url_type->range);
}

static void
gst_mpdparser_parse_seg_base_type_ext (GstSegmentBaseType ** pointer,
    xmlNode * a_node, GstSegmentBaseType * parent)
{
  xmlNode *cur_node;
  GstSegmentBaseType *seg_base_type;
  guint intval;
  guint64 int64val;
  gboolean boolval;
  GstRange *rangeval;

  gst_mpdparser_free_seg_base_type_ext (*pointer);
  *pointer = seg_base_type = g_slice_new0 (GstSegmentBaseType);

  /* Initialize values that have defaults */
  seg_base_type->indexRangeExact = FALSE;
  seg_base_type->timescale = 1;

  /* Inherit attribute values from parent */
  if (parent) {
    seg_base_type->timescale = parent->timescale;
    seg_base_type->presentationTimeOffset = parent->presentationTimeOffset;
    seg_base_type->indexRange = gst_mpdparser_clone_range (parent->indexRange);
    seg_base_type->indexRangeExact = parent->indexRangeExact;
    seg_base_type->Initialization =
        gst_mpdparser_clone_URL (parent->Initialization);
    seg_base_type->RepresentationIndex =
        gst_mpdparser_clone_URL (parent->RepresentationIndex);
  }

  /* We must retrieve each value first to see if it exists.  If it does not
   * exist, we do not want to overwrite an inherited value */
  GST_LOG ("attributes of SegmentBaseType extension:");
  if (gst_mpdparser_get_xml_prop_unsigned_integer (a_node, "timescale", 1,
          &intval)) {
    seg_base_type->timescale = intval;
  }
  if (gst_mpdparser_get_xml_prop_unsigned_integer_64 (a_node,
          "presentationTimeOffset", 0, &int64val)) {
    seg_base_type->presentationTimeOffset = int64val;
  }
  if (gst_mpdparser_get_xml_prop_range (a_node, "indexRange", &rangeval)) {
    if (seg_base_type->indexRange) {
      g_slice_free (GstRange, seg_base_type->indexRange);
    }
    seg_base_type->indexRange = rangeval;
  }
  if (gst_mpdparser_get_xml_prop_boolean (a_node, "indexRangeExact",
          FALSE, &boolval)) {
    seg_base_type->indexRangeExact = boolval;
  }

  /* explore children nodes */
  for (cur_node = a_node->children; cur_node; cur_node = cur_node->next) {
    if (cur_node->type == XML_ELEMENT_NODE) {
      if (xmlStrcmp (cur_node->name, (xmlChar *) "Initialization") == 0 ||
          xmlStrcmp (cur_node->name, (xmlChar *) "Initialisation") == 0) {
        /* parse will free the previous pointer to create a new one */
        gst_mpdparser_parse_url_type_node (&seg_base_type->Initialization,
            cur_node);
      } else if (xmlStrcmp (cur_node->name,
              (xmlChar *) "RepresentationIndex") == 0) {
        /* parse will free the previous pointer to create a new one */
        gst_mpdparser_parse_url_type_node (&seg_base_type->RepresentationIndex,
            cur_node);
      }
    }
  }
}

static GstSNode *
gst_mpdparser_clone_s_node (GstSNode * pointer)
{
  GstSNode *clone = NULL;

  if (pointer) {
    clone = g_slice_new0 (GstSNode);
    clone->t = pointer->t;
    clone->d = pointer->d;
    clone->r = pointer->r;
  }

  return clone;
}

static void
gst_mpdparser_parse_s_node (GQueue * queue, xmlNode * a_node)
{
  GstSNode *new_s_node;

  new_s_node = g_slice_new0 (GstSNode);
  g_queue_push_tail (queue, new_s_node);

  GST_LOG ("attributes of S node:");
  gst_mpdparser_get_xml_prop_unsigned_integer_64 (a_node, "t", 0,
      &new_s_node->t);
  gst_mpdparser_get_xml_prop_unsigned_integer_64 (a_node, "d", 0,
      &new_s_node->d);
  gst_mpdparser_get_xml_prop_signed_integer (a_node, "r", 0, &new_s_node->r);
}

static GstSegmentTimelineNode *
gst_mpdparser_clone_segment_timeline (GstSegmentTimelineNode * pointer)
{
  GstSegmentTimelineNode *clone = NULL;

  if (pointer) {
    clone = gst_mpdparser_segment_timeline_node_new ();
    if (clone) {
      GList *list;
      for (list = g_queue_peek_head_link (&pointer->S); list;
          list = g_list_next (list)) {
        GstSNode *s_node;
        s_node = (GstSNode *) list->data;
        if (s_node) {
          g_queue_push_tail (&clone->S, gst_mpdparser_clone_s_node (s_node));
        }
      }
    } else {
      GST_WARNING ("Allocation of SegmentTimeline node failed!");
    }
  }

  return clone;
}

static void
gst_mpdparser_parse_segment_timeline_node (GstSegmentTimelineNode ** pointer,
    xmlNode * a_node)
{
  xmlNode *cur_node;
  GstSegmentTimelineNode *new_seg_timeline;

  gst_mpdparser_free_segment_timeline_node (*pointer);
  *pointer = new_seg_timeline = gst_mpdparser_segment_timeline_node_new ();
  if (new_seg_timeline == NULL) {
    GST_WARNING ("Allocation of SegmentTimeline node failed!");
    return;
  }

  /* explore children nodes */
  for (cur_node = a_node->children; cur_node; cur_node = cur_node->next) {
    if (cur_node->type == XML_ELEMENT_NODE) {
      if (xmlStrcmp (cur_node->name, (xmlChar *) "S") == 0) {
        gst_mpdparser_parse_s_node (&new_seg_timeline->S, cur_node);
      }
    }
  }
}

static gboolean
gst_mpdparser_parse_mult_seg_base_type_ext (GstMultSegmentBaseType ** pointer,
    xmlNode * a_node, GstMultSegmentBaseType * parent)
{
  xmlNode *cur_node;
  GstMultSegmentBaseType *mult_seg_base_type;
  guint intval;
  gboolean has_timeline = FALSE, has_duration = FALSE;

  gst_mpdparser_free_mult_seg_base_type_ext (*pointer);
  mult_seg_base_type = g_slice_new0 (GstMultSegmentBaseType);

  mult_seg_base_type->duration = 0;
  mult_seg_base_type->startNumber = 1;

  /* Inherit attribute values from parent */
  if (parent) {
    mult_seg_base_type->duration = parent->duration;
    mult_seg_base_type->startNumber = parent->startNumber;
    mult_seg_base_type->SegmentTimeline =
        gst_mpdparser_clone_segment_timeline (parent->SegmentTimeline);
    mult_seg_base_type->BitstreamSwitching =
        gst_mpdparser_clone_URL (parent->BitstreamSwitching);
  }

  GST_LOG ("attributes of MultipleSegmentBaseType extension:");
  if (gst_mpdparser_get_xml_prop_unsigned_integer (a_node, "duration", 0,
          &intval)) {
    mult_seg_base_type->duration = intval;
  }

  /* duration might be specified from parent */
  if (mult_seg_base_type->duration)
    has_duration = TRUE;

  if (gst_mpdparser_get_xml_prop_unsigned_integer (a_node, "startNumber", 1,
          &intval)) {
    mult_seg_base_type->startNumber = intval;
  }

  GST_LOG ("extension of MultipleSegmentBaseType extension:");
  gst_mpdparser_parse_seg_base_type_ext (&mult_seg_base_type->SegBaseType,
      a_node, (parent ? parent->SegBaseType : NULL));

  /* explore children nodes */
  for (cur_node = a_node->children; cur_node; cur_node = cur_node->next) {
    if (cur_node->type == XML_ELEMENT_NODE) {
      if (xmlStrcmp (cur_node->name, (xmlChar *) "SegmentTimeline") == 0) {
        /* parse frees the segmenttimeline if any */
        gst_mpdparser_parse_segment_timeline_node
            (&mult_seg_base_type->SegmentTimeline, cur_node);
      } else if (xmlStrcmp (cur_node->name,
              (xmlChar *) "BitstreamSwitching") == 0) {
        /* parse frees the old url before setting the new one */
        gst_mpdparser_parse_url_type_node
            (&mult_seg_base_type->BitstreamSwitching, cur_node);
      }
    }
  }

  has_timeline = mult_seg_base_type->SegmentTimeline != NULL;

  /* Checking duration and timeline only at Representation's child level */
  if (xmlStrcmp (a_node->parent->name, (xmlChar *) "Representation") == 0
      && !has_duration && !has_timeline) {
    GST_ERROR ("segment has neither duration nor timeline");
    goto error;
  }

  *pointer = mult_seg_base_type;
  return TRUE;

error:
  gst_mpdparser_free_mult_seg_base_type_ext (mult_seg_base_type);
  return FALSE;
}

static gboolean
gst_mpdparser_parse_segment_list_node (GstSegmentListNode ** pointer,
    xmlNode * a_node, GstSegmentListNode * parent)
{
  xmlNode *cur_node;
  GstSegmentListNode *new_segment_list;
  gchar *actuate;
  gboolean segment_urls_inherited_from_parent = FALSE;

  gst_mpdparser_free_segment_list_node (*pointer);
  new_segment_list = g_slice_new0 (GstSegmentListNode);

  /* Inherit attribute values from parent */
  if (parent) {
    GList *list;
    GstSegmentURLNode *seg_url;
    for (list = g_list_first (parent->SegmentURL); list;
        list = g_list_next (list)) {
      seg_url = (GstSegmentURLNode *) list->data;
      new_segment_list->SegmentURL =
          g_list_append (new_segment_list->SegmentURL,
          gst_mpdparser_clone_segment_url (seg_url));
      segment_urls_inherited_from_parent = TRUE;
    }
  }

  new_segment_list->actuate = GST_XLINK_ACTUATE_ON_REQUEST;
  if (gst_mpdparser_get_xml_ns_prop_string (a_node,
          "http://www.w3.org/1999/xlink", "href", &new_segment_list->xlink_href)
      && gst_mpdparser_get_xml_ns_prop_string (a_node,
          "http://www.w3.org/1999/xlink", "actuate", &actuate)) {
    if (strcmp (actuate, "onLoad") == 0)
      new_segment_list->actuate = GST_XLINK_ACTUATE_ON_LOAD;
    xmlFree (actuate);
  }

  GST_LOG ("extension of SegmentList node:");
  if (!gst_mpdparser_parse_mult_seg_base_type_ext
      (&new_segment_list->MultSegBaseType, a_node,
          (parent ? parent->MultSegBaseType : NULL)))
    goto error;

  /* explore children nodes */
  for (cur_node = a_node->children; cur_node; cur_node = cur_node->next) {
    if (cur_node->type == XML_ELEMENT_NODE) {
      if (xmlStrcmp (cur_node->name, (xmlChar *) "SegmentURL") == 0) {
        if (segment_urls_inherited_from_parent) {
          /*
           * SegmentBase, SegmentTemplate and SegmentList shall inherit
           * attributes and elements from the same element on a higher level.
           * If the same attribute or element is present on both levels,
           * the one on the lower level shall take precedence over the one
           * on the higher level.
           */

          /* Clear the list of inherited segment URLs */
          g_list_free_full (new_segment_list->SegmentURL,
              (GDestroyNotify) gst_mpdparser_free_segment_url_node);
          new_segment_list->SegmentURL = NULL;

          /* mark the fact that we cleared the list, so that it is not tried again */
          segment_urls_inherited_from_parent = FALSE;
        }
        gst_mpdparser_parse_segment_url_node (&new_segment_list->SegmentURL,
            cur_node);
      }
    }
  }

  *pointer = new_segment_list;
  return TRUE;

error:
  gst_mpdparser_free_segment_list_node (new_segment_list);
  return FALSE;
}

static void
gst_mpdparser_parse_content_protection_node (GList ** list, xmlNode * a_node)
{
  gchar *value = NULL;
  if (gst_mpdparser_get_xml_prop_string (a_node, "value", &value)) {
    if (!g_strcmp0 (value, "MSPR 2.0")) {
      xmlNode *cur_node;
      for (cur_node = a_node->children; cur_node; cur_node = cur_node->next) {
        if (cur_node->type == XML_ELEMENT_NODE) {
          if (xmlStrcmp (cur_node->name, (xmlChar *) "pro") == 0) {
            GstDescriptorType *new_descriptor;
            new_descriptor = g_slice_new0 (GstDescriptorType);
            *list = g_list_append (*list, new_descriptor);

            gst_mpdparser_get_xml_prop_string_stripped (a_node, "schemeIdUri",
                &new_descriptor->schemeIdUri);

            gst_mpdparser_get_xml_node_content (cur_node,
                &new_descriptor->value);
            goto beach;
          }
        }
      }
    } else {
      gst_mpdparser_parse_descriptor_type_node (list, a_node);
    }
  } else {
    gst_mpdparser_parse_descriptor_type_node (list, a_node);
  }
beach:
  if (value)
    g_free (value);
}

static void
gst_mpdparser_parse_representation_base_type (GstRepresentationBaseType **
    pointer, xmlNode * a_node)
{
  xmlNode *cur_node;
  GstRepresentationBaseType *representation_base;

  gst_mpdparser_free_representation_base_type (*pointer);
  *pointer = representation_base = g_slice_new0 (GstRepresentationBaseType);

  GST_LOG ("attributes of RepresentationBaseType extension:");
  gst_mpdparser_get_xml_prop_string (a_node, "profiles",
      &representation_base->profiles);
  gst_mpdparser_get_xml_prop_unsigned_integer (a_node, "width", 0,
      &representation_base->width);
  gst_mpdparser_get_xml_prop_unsigned_integer (a_node, "height", 0,
      &representation_base->height);
  gst_mpdparser_get_xml_prop_ratio (a_node, "sar", &representation_base->sar);
  gst_mpdparser_get_xml_prop_framerate (a_node, "frameRate",
      &representation_base->frameRate);
  gst_mpdparser_get_xml_prop_framerate (a_node, "minFrameRate",
      &representation_base->minFrameRate);
  gst_mpdparser_get_xml_prop_framerate (a_node, "maxFrameRate",
      &representation_base->maxFrameRate);
  gst_mpdparser_get_xml_prop_string (a_node, "audioSamplingRate",
      &representation_base->audioSamplingRate);
  gst_mpdparser_get_xml_prop_string (a_node, "mimeType",
      &representation_base->mimeType);
  gst_mpdparser_get_xml_prop_string (a_node, "segmentProfiles",
      &representation_base->segmentProfiles);
  gst_mpdparser_get_xml_prop_string (a_node, "codecs",
      &representation_base->codecs);
  gst_mpdparser_get_xml_prop_double (a_node, "maximumSAPPeriod",
      &representation_base->maximumSAPPeriod);
  gst_mpdparser_get_xml_prop_SAP_type (a_node, "startWithSAP",
      &representation_base->startWithSAP);
  gst_mpdparser_get_xml_prop_double (a_node, "maxPlayoutRate",
      &representation_base->maxPlayoutRate);
  gst_mpdparser_get_xml_prop_boolean (a_node, "codingDependency",
      FALSE, &representation_base->codingDependency);
  gst_mpdparser_get_xml_prop_string (a_node, "scanType",
      &representation_base->scanType);

  /* explore children nodes */
  for (cur_node = a_node->children; cur_node; cur_node = cur_node->next) {
    if (cur_node->type == XML_ELEMENT_NODE) {
      if (xmlStrcmp (cur_node->name, (xmlChar *) "FramePacking") == 0) {
        gst_mpdparser_parse_descriptor_type_node
            (&representation_base->FramePacking, cur_node);
      } else if (xmlStrcmp (cur_node->name,
              (xmlChar *) "AudioChannelConfiguration") == 0) {
        gst_mpdparser_parse_descriptor_type_node
            (&representation_base->AudioChannelConfiguration, cur_node);
      } else if (xmlStrcmp (cur_node->name,
              (xmlChar *) "ContentProtection") == 0) {
        gst_mpdparser_parse_content_protection_node
            (&representation_base->ContentProtection, cur_node);
      }
    }
  }
}

static gboolean
gst_mpdparser_parse_representation_node (GList ** list, xmlNode * a_node,
    GstAdaptationSetNode * parent, GstPeriodNode * period_node)
{
  xmlNode *cur_node;
  GstRepresentationNode *new_representation;

  new_representation = g_slice_new0 (GstRepresentationNode);

  GST_LOG ("attributes of Representation node:");
  if (!gst_mpdparser_get_xml_prop_string_no_whitespace (a_node, "id",
          &new_representation->id)) {
    GST_ERROR ("Cannot parse Representation id, invalid manifest");
    goto error;
  }
  if (!gst_mpdparser_get_xml_prop_unsigned_integer (a_node, "bandwidth", 0,
          &new_representation->bandwidth)) {
    GST_ERROR ("Cannot parse Representation bandwidth, invalid manifest");
    goto error;
  }
  gst_mpdparser_get_xml_prop_unsigned_integer (a_node, "qualityRanking", 0,
      &new_representation->qualityRanking);
  gst_mpdparser_get_xml_prop_string_vector_type (a_node, "dependencyId",
      &new_representation->dependencyId);
  gst_mpdparser_get_xml_prop_string_vector_type (a_node,
      "mediaStreamStructureId", &new_representation->mediaStreamStructureId);

  /* RepresentationBase extension */
  gst_mpdparser_parse_representation_base_type
      (&new_representation->RepresentationBase, a_node);

  /* explore children nodes */
  for (cur_node = a_node->children; cur_node; cur_node = cur_node->next) {
    if (cur_node->type == XML_ELEMENT_NODE) {
      if (xmlStrcmp (cur_node->name, (xmlChar *) "SegmentBase") == 0) {
        gst_mpdparser_parse_seg_base_type_ext (&new_representation->SegmentBase,
            cur_node, parent->SegmentBase ?
            parent->SegmentBase : period_node->SegmentBase);
      } else if (xmlStrcmp (cur_node->name, (xmlChar *) "SegmentTemplate") == 0) {
        if (!gst_mpdparser_parse_segment_template_node
            (&new_representation->SegmentTemplate, cur_node,
                parent->SegmentTemplate ?
                parent->SegmentTemplate : period_node->SegmentTemplate))
          goto error;
      } else if (xmlStrcmp (cur_node->name, (xmlChar *) "SegmentList") == 0) {
        if (!gst_mpdparser_parse_segment_list_node
            (&new_representation->SegmentList, cur_node,
                parent->SegmentList ? parent->
                SegmentList : period_node->SegmentList))
          goto error;
      } else if (xmlStrcmp (cur_node->name, (xmlChar *) "BaseURL") == 0) {
        gst_mpdparser_parse_baseURL_node (&new_representation->BaseURLs,
            cur_node);
      } else if (xmlStrcmp (cur_node->name,
              (xmlChar *) "SubRepresentation") == 0) {
        gst_mpdparser_parse_subrepresentation_node
            (&new_representation->SubRepresentations, cur_node);
      }
    }
  }

  /* some sanity checking */

  *list = g_list_append (*list, new_representation);
  return TRUE;

error:
  gst_mpdparser_free_representation_node (new_representation);
  return FALSE;
}

static gboolean
gst_mpdparser_parse_adaptation_set_node (GList ** list, xmlNode * a_node,
    GstPeriodNode * parent)
{
  xmlNode *cur_node;
  GstAdaptationSetNode *new_adap_set;
  gchar *actuate;

  new_adap_set = g_slice_new0 (GstAdaptationSetNode);

  GST_LOG ("attributes of AdaptationSet node:");

  new_adap_set->actuate = GST_XLINK_ACTUATE_ON_REQUEST;
  if (gst_mpdparser_get_xml_ns_prop_string (a_node,
          "http://www.w3.org/1999/xlink", "href", &new_adap_set->xlink_href)
      && gst_mpdparser_get_xml_ns_prop_string (a_node,
          "http://www.w3.org/1999/xlink", "actuate", &actuate)) {
    if (strcmp (actuate, "onLoad") == 0)
      new_adap_set->actuate = GST_XLINK_ACTUATE_ON_LOAD;
    xmlFree (actuate);
  }

  gst_mpdparser_get_xml_prop_unsigned_integer (a_node, "id", 0,
      &new_adap_set->id);
  gst_mpdparser_get_xml_prop_unsigned_integer (a_node, "group", 0,
      &new_adap_set->group);
  gst_mpdparser_get_xml_prop_string (a_node, "lang", &new_adap_set->lang);
  gst_mpdparser_get_xml_prop_string (a_node, "contentType",
      &new_adap_set->contentType);
  gst_mpdparser_get_xml_prop_ratio (a_node, "par", &new_adap_set->par);
  gst_mpdparser_get_xml_prop_unsigned_integer (a_node, "minBandwidth", 0,
      &new_adap_set->minBandwidth);
  gst_mpdparser_get_xml_prop_unsigned_integer (a_node, "maxBandwidth", 0,
      &new_adap_set->maxBandwidth);
  gst_mpdparser_get_xml_prop_unsigned_integer (a_node, "minWidth", 0,
      &new_adap_set->minWidth);
  gst_mpdparser_get_xml_prop_unsigned_integer (a_node, "maxWidth", 0,
      &new_adap_set->maxWidth);
  gst_mpdparser_get_xml_prop_unsigned_integer (a_node, "minHeight", 0,
      &new_adap_set->minHeight);
  gst_mpdparser_get_xml_prop_unsigned_integer (a_node, "maxHeight", 0,
      &new_adap_set->maxHeight);
  gst_mpdparser_get_xml_prop_cond_uint (a_node, "segmentAlignment",
      &new_adap_set->segmentAlignment);
  gst_mpdparser_get_xml_prop_boolean (a_node, "bitstreamSwitching",
      parent->bitstreamSwitching, &new_adap_set->bitstreamSwitching);
  if (parent->bitstreamSwitching && !new_adap_set->bitstreamSwitching) {
    /* according to the standard, if the Period's bitstreamSwitching attribute
     * is true, the AdaptationSet should not have the bitstreamSwitching
     * attribute set to false.
     * We should return a parsing error, but we are generous and ignore the
     * standard violation.
     */
    new_adap_set->bitstreamSwitching = parent->bitstreamSwitching;
  }
  gst_mpdparser_get_xml_prop_cond_uint (a_node, "subsegmentAlignment",
      &new_adap_set->subsegmentAlignment);
  gst_mpdparser_get_xml_prop_SAP_type (a_node, "subsegmentStartsWithSAP",
      &new_adap_set->subsegmentStartsWithSAP);

  /* RepresentationBase extension */
  gst_mpdparser_parse_representation_base_type
      (&new_adap_set->RepresentationBase, a_node);

  /* explore children nodes */
  for (cur_node = a_node->children; cur_node; cur_node = cur_node->next) {
    if (cur_node->type == XML_ELEMENT_NODE) {
      if (xmlStrcmp (cur_node->name, (xmlChar *) "Accessibility") == 0) {
        gst_mpdparser_parse_descriptor_type_node (&new_adap_set->Accessibility,
            cur_node);
      } else if (xmlStrcmp (cur_node->name, (xmlChar *) "Role") == 0) {
        gst_mpdparser_parse_descriptor_type_node (&new_adap_set->Role,
            cur_node);
      } else if (xmlStrcmp (cur_node->name, (xmlChar *) "Rating") == 0) {
        gst_mpdparser_parse_descriptor_type_node (&new_adap_set->Rating,
            cur_node);
      } else if (xmlStrcmp (cur_node->name, (xmlChar *) "Viewpoint") == 0) {
        gst_mpdparser_parse_descriptor_type_node (&new_adap_set->Viewpoint,
            cur_node);
      } else if (xmlStrcmp (cur_node->name, (xmlChar *) "BaseURL") == 0) {
        gst_mpdparser_parse_baseURL_node (&new_adap_set->BaseURLs, cur_node);
      } else if (xmlStrcmp (cur_node->name, (xmlChar *) "SegmentBase") == 0) {
        gst_mpdparser_parse_seg_base_type_ext (&new_adap_set->SegmentBase,
            cur_node, parent->SegmentBase);
      } else if (xmlStrcmp (cur_node->name, (xmlChar *) "SegmentList") == 0) {
        if (!gst_mpdparser_parse_segment_list_node (&new_adap_set->SegmentList,
                cur_node, parent->SegmentList))
          goto error;
      } else if (xmlStrcmp (cur_node->name,
              (xmlChar *) "ContentComponent") == 0) {
        gst_mpdparser_parse_content_component_node
            (&new_adap_set->ContentComponents, cur_node);
      } else if (xmlStrcmp (cur_node->name, (xmlChar *) "SegmentTemplate") == 0) {
        if (!gst_mpdparser_parse_segment_template_node
            (&new_adap_set->SegmentTemplate, cur_node, parent->SegmentTemplate))
          goto error;
      }
    }
  }

  /* We must parse Representation after everything else in the AdaptationSet
   * has been parsed because certain Representation child elements can inherit
   * attributes specified by the same element in the AdaptationSet
   */
  for (cur_node = a_node->children; cur_node; cur_node = cur_node->next) {
    if (cur_node->type == XML_ELEMENT_NODE) {
      if (xmlStrcmp (cur_node->name, (xmlChar *) "Representation") == 0) {
        if (!gst_mpdparser_parse_representation_node
            (&new_adap_set->Representations, cur_node, new_adap_set, parent))
          goto error;
      }
    }
  }

  *list = g_list_append (*list, new_adap_set);
  return TRUE;

error:
  gst_mpdparser_free_adaptation_set_node (new_adap_set);
  return FALSE;
}

static void
gst_mpdparser_parse_subset_node (GList ** list, xmlNode * a_node)
{
  GstSubsetNode *new_subset;

  new_subset = g_slice_new0 (GstSubsetNode);
  *list = g_list_append (*list, new_subset);

  GST_LOG ("attributes of Subset node:");
  gst_mpdparser_get_xml_prop_uint_vector_type (a_node, "contains",
      &new_subset->contains, &new_subset->size);
}

static gboolean
gst_mpdparser_parse_segment_template_node (GstSegmentTemplateNode ** pointer,
    xmlNode * a_node, GstSegmentTemplateNode * parent)
{
  GstSegmentTemplateNode *new_segment_template;
  gchar *strval;

  gst_mpdparser_free_segment_template_node (*pointer);
  new_segment_template = g_slice_new0 (GstSegmentTemplateNode);

  GST_LOG ("extension of SegmentTemplate node:");
  if (!gst_mpdparser_parse_mult_seg_base_type_ext
      (&new_segment_template->MultSegBaseType, a_node,
          (parent ? parent->MultSegBaseType : NULL)))
    goto error;

  /* Inherit attribute values from parent when the value isn't found */
  GST_LOG ("attributes of SegmentTemplate node:");
  if (gst_mpdparser_get_xml_prop_string (a_node, "media", &strval)) {
    new_segment_template->media = strval;
  } else if (parent) {
    new_segment_template->media = xmlMemStrdup (parent->media);
  }

  if (gst_mpdparser_get_xml_prop_string (a_node, "index", &strval)) {
    new_segment_template->index = strval;
  } else if (parent) {
    new_segment_template->index = xmlMemStrdup (parent->index);
  }

  if (gst_mpdparser_get_xml_prop_string (a_node, "initialization", &strval)) {
    new_segment_template->initialization = strval;
  } else if (parent) {
    new_segment_template->initialization =
        xmlMemStrdup (parent->initialization);
  }

  if (gst_mpdparser_get_xml_prop_string (a_node, "bitstreamSwitching", &strval)) {
    new_segment_template->bitstreamSwitching = strval;
  } else if (parent) {
    new_segment_template->bitstreamSwitching =
        xmlMemStrdup (parent->bitstreamSwitching);
  }

  *pointer = new_segment_template;
  return TRUE;

error:
  gst_mpdparser_free_segment_template_node (new_segment_template);
  return FALSE;
}

static gboolean
gst_mpdparser_parse_period_node (GList ** list, xmlNode * a_node)
{
  xmlNode *cur_node;
  GstPeriodNode *new_period;
  gchar *actuate;

  new_period = g_slice_new0 (GstPeriodNode);

  GST_LOG ("attributes of Period node:");

  new_period->actuate = GST_XLINK_ACTUATE_ON_REQUEST;
  if (gst_mpdparser_get_xml_ns_prop_string (a_node,
          "http://www.w3.org/1999/xlink", "href", &new_period->xlink_href)
      && gst_mpdparser_get_xml_ns_prop_string (a_node,
          "http://www.w3.org/1999/xlink", "actuate", &actuate)) {
    if (strcmp (actuate, "onLoad") == 0)
      new_period->actuate = GST_XLINK_ACTUATE_ON_LOAD;
    xmlFree (actuate);
  }

  gst_mpdparser_get_xml_prop_string (a_node, "id", &new_period->id);
  gst_mpdparser_get_xml_prop_duration (a_node, "start", GST_MPD_DURATION_NONE,
      &new_period->start);
  gst_mpdparser_get_xml_prop_duration (a_node, "duration",
      GST_MPD_DURATION_NONE, &new_period->duration);
  gst_mpdparser_get_xml_prop_boolean (a_node, "bitstreamSwitching", FALSE,
      &new_period->bitstreamSwitching);

  /* explore children nodes */
  for (cur_node = a_node->children; cur_node; cur_node = cur_node->next) {
    if (cur_node->type == XML_ELEMENT_NODE) {
      if (xmlStrcmp (cur_node->name, (xmlChar *) "SegmentBase") == 0) {
        gst_mpdparser_parse_seg_base_type_ext (&new_period->SegmentBase,
            cur_node, NULL);
      } else if (xmlStrcmp (cur_node->name, (xmlChar *) "SegmentList") == 0) {
        if (!gst_mpdparser_parse_segment_list_node (&new_period->SegmentList,
                cur_node, NULL))
          goto error;
      } else if (xmlStrcmp (cur_node->name, (xmlChar *) "SegmentTemplate") == 0) {
        if (!gst_mpdparser_parse_segment_template_node
            (&new_period->SegmentTemplate, cur_node, NULL))
          goto error;
      } else if (xmlStrcmp (cur_node->name, (xmlChar *) "Subset") == 0) {
        gst_mpdparser_parse_subset_node (&new_period->Subsets, cur_node);
      } else if (xmlStrcmp (cur_node->name, (xmlChar *) "BaseURL") == 0) {
        gst_mpdparser_parse_baseURL_node (&new_period->BaseURLs, cur_node);
      }
    }
  }

  /* We must parse AdaptationSet after everything else in the Period has been
   * parsed because certain AdaptationSet child elements can inherit attributes
   * specified by the same element in the Period
   */
  for (cur_node = a_node->children; cur_node; cur_node = cur_node->next) {
    if (cur_node->type == XML_ELEMENT_NODE) {
      if (xmlStrcmp (cur_node->name, (xmlChar *) "AdaptationSet") == 0) {
        if (!gst_mpdparser_parse_adaptation_set_node
            (&new_period->AdaptationSets, cur_node, new_period))
          goto error;
      }
    }
  }

  *list = g_list_append (*list, new_period);
  return TRUE;

error:
  gst_mpdparser_free_period_node (new_period);
  return FALSE;
}

static void
gst_mpdparser_parse_program_info_node (GList ** list, xmlNode * a_node)
{
  xmlNode *cur_node;
  GstProgramInformationNode *new_prog_info;

  new_prog_info = g_slice_new0 (GstProgramInformationNode);
  *list = g_list_append (*list, new_prog_info);

  GST_LOG ("attributes of ProgramInformation node:");
  gst_mpdparser_get_xml_prop_string (a_node, "lang", &new_prog_info->lang);
  gst_mpdparser_get_xml_prop_string (a_node, "moreInformationURL",
      &new_prog_info->moreInformationURL);

  /* explore children nodes */
  GST_LOG ("children of ProgramInformation node:");
  for (cur_node = a_node->children; cur_node; cur_node = cur_node->next) {
    if (cur_node->type == XML_ELEMENT_NODE) {
      if (xmlStrcmp (cur_node->name, (xmlChar *) "Title") == 0) {
        gst_mpdparser_get_xml_node_content (cur_node, &new_prog_info->Title);
      } else if (xmlStrcmp (cur_node->name, (xmlChar *) "Source") == 0) {
        gst_mpdparser_get_xml_node_content (cur_node, &new_prog_info->Source);
      } else if (xmlStrcmp (cur_node->name, (xmlChar *) "Copyright") == 0) {
        gst_mpdparser_get_xml_node_content (cur_node,
            &new_prog_info->Copyright);
      }
    }
  }
}

static void
gst_mpdparser_parse_metrics_range_node (GList ** list, xmlNode * a_node)
{
  GstMetricsRangeNode *new_metrics_range;

  new_metrics_range = g_slice_new0 (GstMetricsRangeNode);
  *list = g_list_append (*list, new_metrics_range);

  GST_LOG ("attributes of Metrics Range node:");
  gst_mpdparser_get_xml_prop_duration (a_node, "starttime",
      GST_MPD_DURATION_NONE, &new_metrics_range->starttime);
  gst_mpdparser_get_xml_prop_duration (a_node, "duration",
      GST_MPD_DURATION_NONE, &new_metrics_range->duration);
}

static void
gst_mpdparser_parse_metrics_node (GList ** list, xmlNode * a_node)
{
  xmlNode *cur_node;
  GstMetricsNode *new_metrics;

  new_metrics = g_slice_new0 (GstMetricsNode);
  *list = g_list_append (*list, new_metrics);

  GST_LOG ("attributes of Metrics node:");
  gst_mpdparser_get_xml_prop_string (a_node, "metrics", &new_metrics->metrics);

  /* explore children nodes */
  GST_LOG ("children of Metrics node:");
  for (cur_node = a_node->children; cur_node; cur_node = cur_node->next) {
    if (cur_node->type == XML_ELEMENT_NODE) {
      if (xmlStrcmp (cur_node->name, (xmlChar *) "Range") == 0) {
        gst_mpdparser_parse_metrics_range_node (&new_metrics->MetricsRanges,
            cur_node);
      } else if (xmlStrcmp (cur_node->name, (xmlChar *) "Reporting") == 0) {
        /* No reporting scheme is specified in this part of ISO/IEC 23009.
         * It is expected that external specifications may define formats
         * and delivery for the reporting data. */
        GST_LOG (" - Reporting node found (unknown structure)");
      }
    }
  }
}

/* The UTCTiming element is defined in
 * ISO/IEC 23009-1:2014/PDAM 1 "Information technology — Dynamic adaptive streaming over HTTP (DASH) — Part 1: Media presentation description and segment formats / Amendment 1: High Profile and Availability Time Synchronization"
 */
static void
gst_mpdparser_parse_utctiming_node (GList ** list, xmlNode * a_node)
{
  GstUTCTimingNode *new_timing;
  gchar *method = NULL;
  gchar *value = NULL;

  new_timing = g_slice_new0 (GstUTCTimingNode);

  GST_LOG ("attributes of UTCTiming node:");
  if (gst_mpdparser_get_xml_prop_string (a_node, "schemeIdUri", &method)) {
    int i;

    for (i = 0; gst_mpdparser_utc_timing_methods[i].name; ++i) {
      if (g_ascii_strncasecmp (gst_mpdparser_utc_timing_methods[i].name,
              method, strlen (gst_mpdparser_utc_timing_methods[i].name)) == 0) {
        new_timing->method = gst_mpdparser_utc_timing_methods[i].method;
        break;
      }
    }
    xmlFree (method);
  }

  if (gst_mpdparser_get_xml_prop_string (a_node, "value", &value)) {
    int max_tokens = 0;
    if (GST_MPD_UTCTIMING_TYPE_DIRECT == new_timing->method) {
      /* The GST_MPD_UTCTIMING_TYPE_DIRECT method is a special case
       * that is not a space separated list.
       */
      max_tokens = 1;
    }
    new_timing->urls = g_strsplit (value, " ", max_tokens);
    xmlFree (value);
  }

  /* append to list only if both method and urls were set */
  if (new_timing->method != 0 && new_timing->urls != NULL &&
      g_strv_length (new_timing->urls) != 0) {
    *list = g_list_append (*list, new_timing);
  } else {
    gst_mpdparser_free_utctiming_node (new_timing);
  }
}

static gboolean
gst_mpdparser_parse_root_node (GstMPDNode ** pointer, xmlNode * a_node)
{
  xmlNode *cur_node;
  GstMPDNode *new_mpd;

  gst_mpdparser_free_mpd_node (*pointer);
  *pointer = NULL;
  new_mpd = g_slice_new0 (GstMPDNode);

  GST_LOG ("namespaces of root MPD node:");
  new_mpd->default_namespace =
      gst_mpdparser_get_xml_node_namespace (a_node, NULL);
  new_mpd->namespace_xsi = gst_mpdparser_get_xml_node_namespace (a_node, "xsi");
  new_mpd->namespace_ext = gst_mpdparser_get_xml_node_namespace (a_node, "ext");

  GST_LOG ("attributes of root MPD node:");
  gst_mpdparser_get_xml_prop_string (a_node, "schemaLocation",
      &new_mpd->schemaLocation);
  gst_mpdparser_get_xml_prop_string (a_node, "id", &new_mpd->id);
  gst_mpdparser_get_xml_prop_string (a_node, "profiles", &new_mpd->profiles);
  gst_mpdparser_get_xml_prop_type (a_node, "type", &new_mpd->type);
  gst_mpdparser_get_xml_prop_dateTime (a_node, "availabilityStartTime",
      &new_mpd->availabilityStartTime);
  gst_mpdparser_get_xml_prop_dateTime (a_node, "availabilityEndTime",
      &new_mpd->availabilityEndTime);
  gst_mpdparser_get_xml_prop_duration (a_node, "mediaPresentationDuration",
      GST_MPD_DURATION_NONE, &new_mpd->mediaPresentationDuration);
  gst_mpdparser_get_xml_prop_duration (a_node, "minimumUpdatePeriod",
      GST_MPD_DURATION_NONE, &new_mpd->minimumUpdatePeriod);
  gst_mpdparser_get_xml_prop_duration (a_node, "minBufferTime",
      GST_MPD_DURATION_NONE, &new_mpd->minBufferTime);
  gst_mpdparser_get_xml_prop_duration (a_node, "timeShiftBufferDepth",
      GST_MPD_DURATION_NONE, &new_mpd->timeShiftBufferDepth);
  gst_mpdparser_get_xml_prop_duration (a_node, "suggestedPresentationDelay",
      GST_MPD_DURATION_NONE, &new_mpd->suggestedPresentationDelay);
  gst_mpdparser_get_xml_prop_duration (a_node, "maxSegmentDuration",
      GST_MPD_DURATION_NONE, &new_mpd->maxSegmentDuration);
  gst_mpdparser_get_xml_prop_duration (a_node, "maxSubsegmentDuration",
      GST_MPD_DURATION_NONE, &new_mpd->maxSubsegmentDuration);

  /* explore children Period nodes */
  for (cur_node = a_node->children; cur_node; cur_node = cur_node->next) {
    if (cur_node->type == XML_ELEMENT_NODE) {
      if (xmlStrcmp (cur_node->name, (xmlChar *) "Period") == 0) {
        if (!gst_mpdparser_parse_period_node (&new_mpd->Periods, cur_node))
          goto error;
      } else if (xmlStrcmp (cur_node->name,
              (xmlChar *) "ProgramInformation") == 0) {
        gst_mpdparser_parse_program_info_node (&new_mpd->ProgramInfo, cur_node);
      } else if (xmlStrcmp (cur_node->name, (xmlChar *) "BaseURL") == 0) {
        gst_mpdparser_parse_baseURL_node (&new_mpd->BaseURLs, cur_node);
      } else if (xmlStrcmp (cur_node->name, (xmlChar *) "Location") == 0) {
        gst_mpdparser_parse_location_node (&new_mpd->Locations, cur_node);
      } else if (xmlStrcmp (cur_node->name, (xmlChar *) "Metrics") == 0) {
        gst_mpdparser_parse_metrics_node (&new_mpd->Metrics, cur_node);
      } else if (xmlStrcmp (cur_node->name, (xmlChar *) "UTCTiming") == 0) {
        gst_mpdparser_parse_utctiming_node (&new_mpd->UTCTiming, cur_node);
      }
    }
  }

  *pointer = new_mpd;
  return TRUE;

error:
  gst_mpdparser_free_mpd_node (new_mpd);
  return FALSE;
}

/* comparison functions */
static int
strncmp_ext (const char *s1, const char *s2)
{
  if (s1 == NULL && s2 == NULL)
    return 0;
  if (s1 == NULL && s2 != NULL)
    return 1;
  if (s2 == NULL && s1 != NULL)
    return 1;
  return strncmp (s1, s2, strlen (s2));
}

/* navigation functions */
static GstStreamMimeType
gst_mpdparser_representation_get_mimetype (GstAdaptationSetNode * adapt_set,
    GstRepresentationNode * rep)
{
  gchar *mime = NULL;
  if (rep->RepresentationBase)
    mime = rep->RepresentationBase->mimeType;
  if (mime == NULL && adapt_set->RepresentationBase) {
    mime = adapt_set->RepresentationBase->mimeType;
  }

  if (strncmp_ext (mime, "audio") == 0)
    return GST_STREAM_AUDIO;
  if (strncmp_ext (mime, "video") == 0)
    return GST_STREAM_VIDEO;
  if (strncmp_ext (mime, "application") == 0 || strncmp_ext (mime, "text") == 0)
    return GST_STREAM_APPLICATION;

  return GST_STREAM_UNKNOWN;
}

static GstRepresentationNode *
gst_mpdparser_get_lowest_representation (GList * Representations)
{
  GList *list = NULL;
  GstRepresentationNode *rep = NULL;
  GstRepresentationNode *lowest = NULL;

  if (Representations == NULL)
    return NULL;

  for (list = g_list_first (Representations); list; list = g_list_next (list)) {
    rep = (GstRepresentationNode *) list->data;
    if (rep && (!lowest || rep->bandwidth < lowest->bandwidth)) {
      lowest = rep;
    }
  }

  return lowest;
}

#if 0
static GstRepresentationNode *
gst_mpdparser_get_highest_representation (GList * Representations)
{
  GList *list = NULL;

  if (Representations == NULL)
    return NULL;

  list = g_list_last (Representations);

  return list ? (GstRepresentationNode *) list->data : NULL;
}

static GstRepresentationNode *
gst_mpdparser_get_representation_with_max_bandwidth (GList * Representations,
    gint max_bandwidth)
{
  GList *list = NULL;
  GstRepresentationNode *representation, *best_rep = NULL;

  if (Representations == NULL)
    return NULL;

  if (max_bandwidth <= 0)       /* 0 => get highest representation available */
    return gst_mpdparser_get_highest_representation (Representations);

  for (list = g_list_first (Representations); list; list = g_list_next (list)) {
    representation = (GstRepresentationNode *) list->data;
    if (representation && representation->bandwidth <= max_bandwidth) {
      best_rep = representation;
    }
  }

  return best_rep;
}
#endif

static GstSegmentBaseType *
gst_mpdparser_get_segment_base (GstPeriodNode * Period,
    GstAdaptationSetNode * AdaptationSet,
    GstRepresentationNode * Representation)
{
  GstSegmentBaseType *SegmentBase = NULL;

  if (Representation && Representation->SegmentBase) {
    SegmentBase = Representation->SegmentBase;
  } else if (AdaptationSet && AdaptationSet->SegmentBase) {
    SegmentBase = AdaptationSet->SegmentBase;
  } else if (Period && Period->SegmentBase) {
    SegmentBase = Period->SegmentBase;
  }
  /* the SegmentBase element could be encoded also inside a SegmentList element */
  if (SegmentBase == NULL) {
    if (Representation && Representation->SegmentList
        && Representation->SegmentList->MultSegBaseType
        && Representation->SegmentList->MultSegBaseType->SegBaseType) {
      SegmentBase = Representation->SegmentList->MultSegBaseType->SegBaseType;
    } else if (AdaptationSet && AdaptationSet->SegmentList
        && AdaptationSet->SegmentList->MultSegBaseType
        && AdaptationSet->SegmentList->MultSegBaseType->SegBaseType) {
      SegmentBase = AdaptationSet->SegmentList->MultSegBaseType->SegBaseType;
    } else if (Period && Period->SegmentList
        && Period->SegmentList->MultSegBaseType
        && Period->SegmentList->MultSegBaseType->SegBaseType) {
      SegmentBase = Period->SegmentList->MultSegBaseType->SegBaseType;
    }
  }

  return SegmentBase;
}

gint
gst_mpdparser_get_rep_idx_with_min_bandwidth (GList * Representations)
{
  GList *list = NULL, *lowest = NULL;
  GstRepresentationNode *rep = NULL;
  gint lowest_bandwidth = -1;

  if (Representations == NULL)
    return -1;

  for (list = g_list_first (Representations); list; list = g_list_next (list)) {
    rep = (GstRepresentationNode *) list->data;
    if (rep && (!lowest || rep->bandwidth < lowest_bandwidth)) {
      lowest = list;
      lowest_bandwidth = rep->bandwidth;
    }
  }

  return lowest ? g_list_position (Representations, lowest) : -1;
}

gint
gst_mpdparser_get_rep_idx_with_max_bandwidth (GList * Representations,
    gint64 max_bandwidth, gint max_video_width, gint max_video_height, gint
    max_video_framerate_n, gint max_video_framerate_d)
{
  GList *list = NULL, *best = NULL;
  GstRepresentationNode *representation;
  gint best_bandwidth = 0;

  GST_DEBUG ("max_bandwidth = %" G_GINT64_FORMAT, max_bandwidth);

  if (Representations == NULL)
    return -1;

  if (max_bandwidth <= 0)       /* 0 => get lowest representation available */
    return gst_mpdparser_get_rep_idx_with_min_bandwidth (Representations);

  for (list = g_list_first (Representations); list; list = g_list_next (list)) {
    GstFrameRate *framerate = NULL;

    representation = (GstRepresentationNode *) list->data;

    /* FIXME: Really? */
    if (!representation)
      continue;

    framerate = representation->RepresentationBase->frameRate;
    if (!framerate)
      framerate = representation->RepresentationBase->maxFrameRate;

    if (framerate && max_video_framerate_n > 0) {
      if (gst_util_fraction_compare (framerate->num, framerate->den,
              max_video_framerate_n, max_video_framerate_d) > 0)
        continue;
    }

    if (max_video_width > 0
        && representation->RepresentationBase->width > max_video_width)
      continue;
    if (max_video_height > 0
        && representation->RepresentationBase->height > max_video_height)
      continue;

    if (representation->bandwidth <= max_bandwidth &&
        representation->bandwidth > best_bandwidth) {
      best = list;
      best_bandwidth = representation->bandwidth;
    }
  }

  return best ? g_list_position (Representations, best) : -1;
}

static GstSegmentListNode *
gst_mpd_client_fetch_external_segment_list (GstMpdClient * client,
    GstPeriodNode * Period,
    GstAdaptationSetNode * AdaptationSet,
    GstRepresentationNode * Representation,
    GstSegmentListNode * parent, GstSegmentListNode * segment_list)
{
  GstFragment *download;
  GstBuffer *segment_list_buffer;
  GstMapInfo map;
  GError *err = NULL;
  xmlDocPtr doc = NULL;
  GstUri *base_uri, *uri;
  gchar *query = NULL;
  gchar *uri_string;
  GstSegmentListNode *new_segment_list = NULL;

  /* ISO/IEC 23009-1:2014 5.5.3 4)
   * Remove nodes that resolve to nothing when resolving
   */
  if (strcmp (segment_list->xlink_href,
          "urn:mpeg:dash:resolve-to-zero:2013") == 0) {
    return NULL;
  }

  if (!client->downloader) {
    return NULL;
  }

  /* Build absolute URI */

  /* Get base URI at the MPD level */
  base_uri =
      gst_uri_from_string (client->
      mpd_base_uri ? client->mpd_base_uri : client->mpd_uri);

  /* combine a BaseURL at the MPD level with the current base url */
  base_uri = combine_urls (base_uri, client->mpd_node->BaseURLs, &query, 0);

  /* combine a BaseURL at the Period level with the current base url */
  base_uri = combine_urls (base_uri, Period->BaseURLs, &query, 0);

  if (AdaptationSet) {
    /* combine a BaseURL at the AdaptationSet level with the current base url */
    base_uri = combine_urls (base_uri, AdaptationSet->BaseURLs, &query, 0);

    if (Representation) {
      /* combine a BaseURL at the Representation level with the current base url */
      base_uri = combine_urls (base_uri, Representation->BaseURLs, &query, 0);
    }
  }

  uri = gst_uri_from_string_with_base (base_uri, segment_list->xlink_href);
  if (query)
    gst_uri_set_query_string (uri, query);
  g_free (query);
  uri_string = gst_uri_to_string (uri);
  gst_uri_unref (base_uri);
  gst_uri_unref (uri);

  download =
      gst_uri_downloader_fetch_uri (client->downloader,
      uri_string, client->mpd_uri, TRUE, FALSE, TRUE, &err);
  g_free (uri_string);

  if (!download) {
    GST_ERROR ("Failed to download external SegmentList node at '%s': %s",
        segment_list->xlink_href, err->message);
    g_clear_error (&err);
    return NULL;
  }

  segment_list_buffer = gst_fragment_get_buffer (download);
  g_object_unref (download);

  gst_buffer_map (segment_list_buffer, &map, GST_MAP_READ);

  doc =
      xmlReadMemory ((const gchar *) map.data, map.size, "noname.xml", NULL,
      XML_PARSE_NONET);

  gst_buffer_unmap (segment_list_buffer, &map);
  gst_buffer_unref (segment_list_buffer);

  /* NOTE: ISO/IEC 23009-1:2014 5.3.9.3.2 is saying that one or multiple SegmentList
   * in external xml is allowed, however, multiple SegmentList does not make sense
   * because Period/AdaptationSet/Representation allow only one SegmentList */
  if (doc) {
    xmlNode *root_element = xmlDocGetRootElement (doc);

    if (root_element->type != XML_ELEMENT_NODE ||
        xmlStrcmp (root_element->name, (xmlChar *) "SegmentList") != 0) {
      goto error;
    }

    gst_mpdparser_parse_segment_list_node (&new_segment_list, root_element,
        parent);
  } else {
    goto error;
  }

done:
  if (doc)
    xmlFreeDoc (doc);

  return new_segment_list;

error:
  GST_ERROR ("Failed to parse segment list node XML");
  goto done;
}

static GstSegmentListNode *
gst_mpdparser_get_segment_list (GstMpdClient * client, GstPeriodNode * Period,
    GstAdaptationSetNode * AdaptationSet,
    GstRepresentationNode * Representation)
{
  GstSegmentListNode **SegmentList;
  GstSegmentListNode *ParentSegmentList = NULL;

  if (Representation && Representation->SegmentList) {
    SegmentList = &Representation->SegmentList;
    ParentSegmentList = AdaptationSet->SegmentList;
  } else if (AdaptationSet && AdaptationSet->SegmentList) {
    SegmentList = &AdaptationSet->SegmentList;
    ParentSegmentList = Period->SegmentList;
    Representation = NULL;
  } else {
    Representation = NULL;
    AdaptationSet = NULL;
    SegmentList = &Period->SegmentList;
  }

  /* Resolve external segment list here. */
  if (*SegmentList && (*SegmentList)->xlink_href) {
    GstSegmentListNode *new_segment_list;

    /* TODO: Use SegmentList of parent if
     * - Parent has its own SegmentList
     * - Fail to get SegmentList from external xml
     */
    new_segment_list =
        gst_mpd_client_fetch_external_segment_list (client, Period,
        AdaptationSet, Representation, ParentSegmentList, *SegmentList);

    gst_mpdparser_free_segment_list_node (*SegmentList);
    *SegmentList = new_segment_list;
  }

  return *SegmentList;
}

/* memory management functions */
static void
gst_mpdparser_free_mpd_node (GstMPDNode * mpd_node)
{
  if (mpd_node) {
    if (mpd_node->default_namespace)
      xmlFree (mpd_node->default_namespace);
    if (mpd_node->namespace_xsi)
      xmlFree (mpd_node->namespace_xsi);
    if (mpd_node->namespace_ext)
      xmlFree (mpd_node->namespace_ext);
    if (mpd_node->schemaLocation)
      xmlFree (mpd_node->schemaLocation);
    if (mpd_node->id)
      xmlFree (mpd_node->id);
    if (mpd_node->profiles)
      xmlFree (mpd_node->profiles);
    if (mpd_node->availabilityStartTime)
      gst_date_time_unref (mpd_node->availabilityStartTime);
    if (mpd_node->availabilityEndTime)
      gst_date_time_unref (mpd_node->availabilityEndTime);
    g_list_free_full (mpd_node->ProgramInfo,
        (GDestroyNotify) gst_mpdparser_free_prog_info_node);
    g_list_free_full (mpd_node->BaseURLs,
        (GDestroyNotify) gst_mpdparser_free_base_url_node);
    g_list_free_full (mpd_node->Locations, (GDestroyNotify) xmlFree);
    g_list_free_full (mpd_node->Periods,
        (GDestroyNotify) gst_mpdparser_free_period_node);
    g_list_free_full (mpd_node->Metrics,
        (GDestroyNotify) gst_mpdparser_free_metrics_node);
    g_list_free_full (mpd_node->UTCTiming,
        (GDestroyNotify) gst_mpdparser_free_utctiming_node);
    g_slice_free (GstMPDNode, mpd_node);
  }
}

static void
gst_mpdparser_free_prog_info_node (GstProgramInformationNode * prog_info_node)
{
  if (prog_info_node) {
    if (prog_info_node->lang)
      xmlFree (prog_info_node->lang);
    if (prog_info_node->moreInformationURL)
      xmlFree (prog_info_node->moreInformationURL);
    if (prog_info_node->Title)
      xmlFree (prog_info_node->Title);
    if (prog_info_node->Source)
      xmlFree (prog_info_node->Source);
    if (prog_info_node->Copyright)
      xmlFree (prog_info_node->Copyright);
    g_slice_free (GstProgramInformationNode, prog_info_node);
  }
}

static void
gst_mpdparser_free_metrics_node (GstMetricsNode * metrics_node)
{
  if (metrics_node) {
    if (metrics_node->metrics)
      xmlFree (metrics_node->metrics);
    g_list_free_full (metrics_node->MetricsRanges,
        (GDestroyNotify) gst_mpdparser_free_metrics_range_node);
    g_slice_free (GstMetricsNode, metrics_node);
  }
}

static void
gst_mpdparser_free_metrics_range_node (GstMetricsRangeNode * metrics_range_node)
{
  if (metrics_range_node) {
    g_slice_free (GstMetricsRangeNode, metrics_range_node);
  }
}

static void
gst_mpdparser_free_period_node (GstPeriodNode * period_node)
{
  if (period_node) {
    if (period_node->id)
      xmlFree (period_node->id);
    gst_mpdparser_free_seg_base_type_ext (period_node->SegmentBase);
    gst_mpdparser_free_segment_list_node (period_node->SegmentList);
    gst_mpdparser_free_segment_template_node (period_node->SegmentTemplate);
    g_list_free_full (period_node->AdaptationSets,
        (GDestroyNotify) gst_mpdparser_free_adaptation_set_node);
    g_list_free_full (period_node->Subsets,
        (GDestroyNotify) gst_mpdparser_free_subset_node);
    g_list_free_full (period_node->BaseURLs,
        (GDestroyNotify) gst_mpdparser_free_base_url_node);
    if (period_node->xlink_href)
      xmlFree (period_node->xlink_href);
    g_slice_free (GstPeriodNode, period_node);
  }
}

static void
gst_mpdparser_free_subset_node (GstSubsetNode * subset_node)
{
  if (subset_node) {
    if (subset_node->contains)
      xmlFree (subset_node->contains);
    g_slice_free (GstSubsetNode, subset_node);
  }
}

static void
gst_mpdparser_free_segment_template_node (GstSegmentTemplateNode *
    segment_template_node)
{
  if (segment_template_node) {
    if (segment_template_node->media)
      xmlFree (segment_template_node->media);
    if (segment_template_node->index)
      xmlFree (segment_template_node->index);
    if (segment_template_node->initialization)
      xmlFree (segment_template_node->initialization);
    if (segment_template_node->bitstreamSwitching)
      xmlFree (segment_template_node->bitstreamSwitching);
    /* MultipleSegmentBaseType extension */
    gst_mpdparser_free_mult_seg_base_type_ext
        (segment_template_node->MultSegBaseType);
    g_slice_free (GstSegmentTemplateNode, segment_template_node);
  }
}

static void
gst_mpdparser_free_representation_base_type (GstRepresentationBaseType *
    representation_base)
{
  if (representation_base) {
    if (representation_base->profiles)
      xmlFree (representation_base->profiles);
    g_slice_free (GstRatio, representation_base->sar);
    g_slice_free (GstFrameRate, representation_base->frameRate);
    g_slice_free (GstFrameRate, representation_base->minFrameRate);
    g_slice_free (GstFrameRate, representation_base->maxFrameRate);
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
        (GDestroyNotify) gst_mpdparser_free_descriptor_type_node);
    g_list_free_full (representation_base->AudioChannelConfiguration,
        (GDestroyNotify) gst_mpdparser_free_descriptor_type_node);
    g_list_free_full (representation_base->ContentProtection,
        (GDestroyNotify) gst_mpdparser_free_descriptor_type_node);
    g_slice_free (GstRepresentationBaseType, representation_base);
  }
}

static void
gst_mpdparser_free_adaptation_set_node (GstAdaptationSetNode *
    adaptation_set_node)
{
  if (adaptation_set_node) {
    if (adaptation_set_node->lang)
      xmlFree (adaptation_set_node->lang);
    if (adaptation_set_node->contentType)
      xmlFree (adaptation_set_node->contentType);
    g_slice_free (GstRatio, adaptation_set_node->par);
    g_slice_free (GstConditionalUintType,
        adaptation_set_node->segmentAlignment);
    g_slice_free (GstConditionalUintType,
        adaptation_set_node->subsegmentAlignment);
    g_list_free_full (adaptation_set_node->Accessibility,
        (GDestroyNotify) gst_mpdparser_free_descriptor_type_node);
    g_list_free_full (adaptation_set_node->Role,
        (GDestroyNotify) gst_mpdparser_free_descriptor_type_node);
    g_list_free_full (adaptation_set_node->Rating,
        (GDestroyNotify) gst_mpdparser_free_descriptor_type_node);
    g_list_free_full (adaptation_set_node->Viewpoint,
        (GDestroyNotify) gst_mpdparser_free_descriptor_type_node);
    gst_mpdparser_free_representation_base_type
        (adaptation_set_node->RepresentationBase);
    gst_mpdparser_free_seg_base_type_ext (adaptation_set_node->SegmentBase);
    gst_mpdparser_free_segment_list_node (adaptation_set_node->SegmentList);
    gst_mpdparser_free_segment_template_node
        (adaptation_set_node->SegmentTemplate);
    g_list_free_full (adaptation_set_node->BaseURLs,
        (GDestroyNotify) gst_mpdparser_free_base_url_node);
    g_list_free_full (adaptation_set_node->Representations,
        (GDestroyNotify) gst_mpdparser_free_representation_node);
    g_list_free_full (adaptation_set_node->ContentComponents,
        (GDestroyNotify) gst_mpdparser_free_content_component_node);
    if (adaptation_set_node->xlink_href)
      xmlFree (adaptation_set_node->xlink_href);
    g_slice_free (GstAdaptationSetNode, adaptation_set_node);
  }
}

static void
gst_mpdparser_free_representation_node (GstRepresentationNode *
    representation_node)
{
  if (representation_node) {
    if (representation_node->id)
      xmlFree (representation_node->id);
    g_strfreev (representation_node->dependencyId);
    g_strfreev (representation_node->mediaStreamStructureId);
    gst_mpdparser_free_representation_base_type
        (representation_node->RepresentationBase);
    g_list_free_full (representation_node->SubRepresentations,
        (GDestroyNotify) gst_mpdparser_free_subrepresentation_node);
    gst_mpdparser_free_seg_base_type_ext (representation_node->SegmentBase);
    gst_mpdparser_free_segment_template_node
        (representation_node->SegmentTemplate);
    gst_mpdparser_free_segment_list_node (representation_node->SegmentList);
    g_list_free_full (representation_node->BaseURLs,
        (GDestroyNotify) gst_mpdparser_free_base_url_node);
    g_slice_free (GstRepresentationNode, representation_node);
  }
}

static void
gst_mpdparser_free_subrepresentation_node (GstSubRepresentationNode *
    subrep_node)
{
  if (subrep_node) {
    gst_mpdparser_free_representation_base_type
        (subrep_node->RepresentationBase);
    if (subrep_node->dependencyLevel)
      xmlFree (subrep_node->dependencyLevel);
    g_strfreev (subrep_node->contentComponent);
    g_slice_free (GstSubRepresentationNode, subrep_node);
  }
}

static void
gst_mpdparser_free_s_node (GstSNode * s_node)
{
  if (s_node) {
    g_slice_free (GstSNode, s_node);
  }
}

static GstSegmentTimelineNode *
gst_mpdparser_segment_timeline_node_new (void)
{
  GstSegmentTimelineNode *node = g_slice_new0 (GstSegmentTimelineNode);

  g_queue_init (&node->S);

  return node;
}

static void
gst_mpdparser_free_segment_timeline_node (GstSegmentTimelineNode * seg_timeline)
{
  if (seg_timeline) {
    g_queue_foreach (&seg_timeline->S, (GFunc) gst_mpdparser_free_s_node, NULL);
    g_queue_clear (&seg_timeline->S);
    g_slice_free (GstSegmentTimelineNode, seg_timeline);
  }
}

static void
gst_mpdparser_free_url_type_node (GstURLType * url_type_node)
{
  if (url_type_node) {
    if (url_type_node->sourceURL)
      xmlFree (url_type_node->sourceURL);
    g_slice_free (GstRange, url_type_node->range);
    g_slice_free (GstURLType, url_type_node);
  }
}

static void
gst_mpdparser_free_seg_base_type_ext (GstSegmentBaseType * seg_base_type)
{
  if (seg_base_type) {
    if (seg_base_type->indexRange)
      g_slice_free (GstRange, seg_base_type->indexRange);
    gst_mpdparser_free_url_type_node (seg_base_type->Initialization);
    gst_mpdparser_free_url_type_node (seg_base_type->RepresentationIndex);
    g_slice_free (GstSegmentBaseType, seg_base_type);
  }
}

static void
gst_mpdparser_free_mult_seg_base_type_ext (GstMultSegmentBaseType *
    mult_seg_base_type)
{
  if (mult_seg_base_type) {
    /* SegmentBaseType extension */
    gst_mpdparser_free_seg_base_type_ext (mult_seg_base_type->SegBaseType);
    gst_mpdparser_free_segment_timeline_node
        (mult_seg_base_type->SegmentTimeline);
    gst_mpdparser_free_url_type_node (mult_seg_base_type->BitstreamSwitching);
    g_slice_free (GstMultSegmentBaseType, mult_seg_base_type);
  }
}

static void
gst_mpdparser_free_segment_list_node (GstSegmentListNode * segment_list_node)
{
  if (segment_list_node) {
    g_list_free_full (segment_list_node->SegmentURL,
        (GDestroyNotify) gst_mpdparser_free_segment_url_node);
    /* MultipleSegmentBaseType extension */
    gst_mpdparser_free_mult_seg_base_type_ext
        (segment_list_node->MultSegBaseType);
    if (segment_list_node->xlink_href)
      xmlFree (segment_list_node->xlink_href);
    g_slice_free (GstSegmentListNode, segment_list_node);
  }
}

static void
gst_mpdparser_free_segment_url_node (GstSegmentURLNode * segment_url)
{
  if (segment_url) {
    if (segment_url->media)
      xmlFree (segment_url->media);
    g_slice_free (GstRange, segment_url->mediaRange);
    if (segment_url->index)
      xmlFree (segment_url->index);
    g_slice_free (GstRange, segment_url->indexRange);
    g_slice_free (GstSegmentURLNode, segment_url);
  }
}

static void
gst_mpdparser_free_base_url_node (GstBaseURL * base_url_node)
{
  if (base_url_node) {
    if (base_url_node->baseURL)
      xmlFree (base_url_node->baseURL);
    if (base_url_node->serviceLocation)
      xmlFree (base_url_node->serviceLocation);
    if (base_url_node->byteRange)
      xmlFree (base_url_node->byteRange);
    g_slice_free (GstBaseURL, base_url_node);
  }
}

static void
gst_mpdparser_free_descriptor_type_node (GstDescriptorType * descriptor_type)
{
  if (descriptor_type) {
    if (descriptor_type->schemeIdUri)
      xmlFree (descriptor_type->schemeIdUri);
    if (descriptor_type->value)
      xmlFree (descriptor_type->value);
    g_slice_free (GstDescriptorType, descriptor_type);
  }
}

static void
gst_mpdparser_free_content_component_node (GstContentComponentNode *
    content_component_node)
{
  if (content_component_node) {
    if (content_component_node->lang)
      xmlFree (content_component_node->lang);
    if (content_component_node->contentType)
      xmlFree (content_component_node->contentType);
    g_slice_free (GstRatio, content_component_node->par);
    g_list_free_full (content_component_node->Accessibility,
        (GDestroyNotify) gst_mpdparser_free_descriptor_type_node);
    g_list_free_full (content_component_node->Role,
        (GDestroyNotify) gst_mpdparser_free_descriptor_type_node);
    g_list_free_full (content_component_node->Rating,
        (GDestroyNotify) gst_mpdparser_free_descriptor_type_node);
    g_list_free_full (content_component_node->Viewpoint,
        (GDestroyNotify) gst_mpdparser_free_descriptor_type_node);
    g_slice_free (GstContentComponentNode, content_component_node);
  }
}

static void
gst_mpdparser_free_utctiming_node (GstUTCTimingNode * timing_type)
{
  if (timing_type) {
    if (timing_type->urls)
      g_strfreev (timing_type->urls);
    g_slice_free (GstUTCTimingNode, timing_type);
  }
}

static void
gst_mpdparser_free_stream_period (GstStreamPeriod * stream_period)
{
  if (stream_period) {
    g_slice_free (GstStreamPeriod, stream_period);
  }
}

static void
gst_mpdparser_free_media_segment (GstMediaSegment * media_segment)
{
  if (media_segment) {
    g_slice_free (GstMediaSegment, media_segment);
  }
}

static void
gst_mpdparser_init_active_stream_segments (GstActiveStream * stream)
{
  g_assert (stream->segments == NULL);
  stream->segments = g_ptr_array_new ();
  g_ptr_array_set_free_func (stream->segments,
      (GDestroyNotify) gst_mpdparser_free_media_segment);
}

static void
gst_mpdparser_free_active_stream (GstActiveStream * active_stream)
{
  if (active_stream) {
    g_free (active_stream->baseURL);
    active_stream->baseURL = NULL;
    g_free (active_stream->queryURL);
    active_stream->queryURL = NULL;
    if (active_stream->segments)
      g_ptr_array_unref (active_stream->segments);
    g_slice_free (GstActiveStream, active_stream);
  }
}

static gchar *
gst_mpdparser_get_mediaURL (GstActiveStream * stream,
    GstSegmentURLNode * segmentURL)
{
  const gchar *url_prefix;

  g_return_val_if_fail (stream != NULL, NULL);
  g_return_val_if_fail (segmentURL != NULL, NULL);

  url_prefix = segmentURL->media ? segmentURL->media : stream->baseURL;
  g_return_val_if_fail (url_prefix != NULL, NULL);

  return segmentURL->media;
}

static const gchar *
gst_mpdparser_get_initializationURL (GstActiveStream * stream,
    GstURLType * InitializationURL)
{
  const gchar *url_prefix;

  g_return_val_if_fail (stream != NULL, NULL);

  url_prefix = (InitializationURL
      && InitializationURL->sourceURL) ? InitializationURL->
      sourceURL : stream->baseURL;

  return url_prefix;
}

/* ISO/IEC 23009-1:2004 5.3.9.4.4 */
static gboolean
validate_format (const gchar * format)
{
  const gchar *p = format;

  /* Check if it starts with % */
  if (!p || p[0] != '%')
    return FALSE;
  p++;

  /* the spec mandates a format like %0[width]d */
  /* Following the %, we must have a 0 */
  if (p[0] != '0')
    return FALSE;

  /* Following the % must be a number starting with 0
   */
  while (g_ascii_isdigit (*p))
    p++;

  /* After any 0 and alphanumeric values, there must be a d.
   */
  if (p[0] != 'd')
    return FALSE;
  p++;

  /* And then potentially more characters without any
   * further %, even if the spec does not mention this
   */
  p = strchr (p, '%');
  if (p)
    return FALSE;

  return TRUE;
}

static gchar *
promote_format_to_uint64 (const gchar * format)
{
  const gchar *p = format;
  gchar *promoted_format;

  /* Must be called with a validated format! */
  g_return_val_if_fail (validate_format (format), NULL);

  /* it starts with % */
  p++;

  /* Following the % must be a 0, or any of d, x or u.
   * x and u are not part of the spec, but don't hurt us
   */
  if (p[0] == '0') {
    p++;

    while (g_ascii_isdigit (*p))
      p++;
  }

  /* After any 0 and alphanumeric values, there must be a d.
   * Otherwise validation would have failed
   */
  g_assert (p[0] == 'd');

  promoted_format =
      g_strdup_printf ("%.*s" G_GINT64_MODIFIER "%s", (gint) (p - format),
      format, p);

  return promoted_format;
}

static gboolean
gst_mpdparser_validate_rfc1738_url (const char *s)
{
  while (*s) {
    if (!strchr
        (";:@&=aAbBcCdDeEfFgGhHiIjJkKlLmMnNoOpPqQrRsStTuUvVwWxXyYzZ0123456789$-_.+!*'(),%/",
            *s))
      return FALSE;
    if (*s == '%') {
      /* g_ascii_isdigit returns FALSE for NUL, and || is a short circuiting
         operator, so this is safe for strings ending before two hex digits */
      if (!g_ascii_isxdigit (s[1]) || !g_ascii_isxdigit (s[2]))
        return FALSE;
      s += 2;
    }
    s++;
  }
  return TRUE;
}

static gchar *
gst_mpdparser_build_URL_from_template (const gchar * url_template,
    const gchar * id, guint number, guint bandwidth, guint64 time)
{
  static const gchar default_format[] = "%01d";
  gchar **tokens, *token, *ret;
  const gchar *format;
  gint i, num_tokens;

  g_return_val_if_fail (url_template != NULL, NULL);
  tokens = g_strsplit_set (url_template, "$", -1);
  if (!tokens) {
    GST_WARNING ("Scan of URL template failed!");
    return NULL;
  }
  num_tokens = g_strv_length (tokens);

  /*
   * each identifier is guarded by 2 $, which means that we must have an odd number of tokens
   * An even number of tokens means the string is not valid.
   */
  if ((num_tokens & 1) == 0) {
    GST_ERROR ("Invalid number of tokens (%d). url_template is '%s'",
        num_tokens, url_template);
    g_strfreev (tokens);
    return NULL;
  }

  for (i = 0; i < num_tokens; i++) {
    token = tokens[i];
    format = default_format;

    /* the tokens to replace must be provided between $ characters, eg $token$
     * For a string like token0$token1$token2$token3$token4, only the odd number
     * tokens (1,3,...) must be parsed.
     *
     * Skip even tokens
     */
    if ((i & 1) == 0)
      continue;

    if (!g_strcmp0 (token, "RepresentationID")) {
      if (!gst_mpdparser_validate_rfc1738_url (id))
        goto invalid_representation_id;

      tokens[i] = g_strdup_printf ("%s", id);
      g_free (token);
    } else if (!strncmp (token, "Number", 6)) {
      if (strlen (token) > 6) {
        format = token + 6;     /* format tag */
      }
      if (!validate_format (format))
        goto invalid_format;

      tokens[i] = g_strdup_printf (format, number);
      g_free (token);
    } else if (!strncmp (token, "Bandwidth", 9)) {
      if (strlen (token) > 9) {
        format = token + 9;     /* format tag */
      }
      if (!validate_format (format))
        goto invalid_format;

      tokens[i] = g_strdup_printf (format, bandwidth);
      g_free (token);
    } else if (!strncmp (token, "Time", 4)) {
      gchar *promoted_format;

      if (strlen (token) > 4) {
        format = token + 4;     /* format tag */
      }
      if (!validate_format (format))
        goto invalid_format;

      promoted_format = promote_format_to_uint64 (format);
      tokens[i] = g_strdup_printf (promoted_format, time);
      g_free (promoted_format);
      g_free (token);
    } else if (!g_strcmp0 (token, "")) {
      tokens[i] = g_strdup_printf ("%s", "$");
      g_free (token);
    } else {
      /* unexpected identifier found between $ signs
       *
       * "If the URL contains unescaped $ symbols which do not enclose a valid
       * identifier then the result of URL formation is undefined"
       */
      goto invalid_format;
    }
  }

  ret = g_strjoinv (NULL, tokens);

  g_strfreev (tokens);

  return ret;

invalid_format:
  {
    GST_ERROR ("Invalid format '%s' in '%s'", format, token);

    g_strfreev (tokens);

    return NULL;
  }
invalid_representation_id:
  {
    GST_ERROR
        ("Representation ID string '%s' has characters invalid in an RFC 1738 URL",
        id);

    g_strfreev (tokens);

    return NULL;
  }
}

guint
gst_mpd_client_get_period_index_at_time (GstMpdClient * client,
    GstDateTime * time)
{
  GList *iter;
  guint period_idx = G_MAXUINT;
  guint idx;
  gint64 time_offset;
  GstDateTime *avail_start =
      gst_mpd_client_get_availability_start_time (client);
  GstStreamPeriod *stream_period;

  if (avail_start == NULL)
    return 0;

  time_offset = gst_mpd_client_calculate_time_difference (avail_start, time);
  gst_date_time_unref (avail_start);

  if (time_offset < 0)
    return 0;

  if (!gst_mpd_client_setup_media_presentation (client, time_offset, -1, NULL))
    return 0;

  for (idx = 0, iter = client->periods; iter; idx++, iter = g_list_next (iter)) {
    stream_period = iter->data;
    if (stream_period->start <= time_offset
        && (!GST_CLOCK_TIME_IS_VALID (stream_period->duration)
            || stream_period->start + stream_period->duration > time_offset)) {
      period_idx = idx;
      break;
    }
  }

  return period_idx;
}

static GstStreamPeriod *
gst_mpdparser_get_stream_period (GstMpdClient * client)
{
  g_return_val_if_fail (client != NULL, NULL);
  g_return_val_if_fail (client->periods != NULL, NULL);

  return g_list_nth_data (client->periods, client->period_idx);
}

static GstRange *
gst_mpdparser_clone_range (GstRange * range)
{
  GstRange *clone = NULL;

  if (range) {
    clone = g_slice_new0 (GstRange);
    clone->first_byte_pos = range->first_byte_pos;
    clone->last_byte_pos = range->last_byte_pos;
  }

  return clone;
}

static GstURLType *
gst_mpdparser_clone_URL (GstURLType * url)
{

  GstURLType *clone = NULL;

  if (url) {
    clone = g_slice_new0 (GstURLType);
    if (url->sourceURL) {
      clone->sourceURL = xmlMemStrdup (url->sourceURL);
    }
    clone->range = gst_mpdparser_clone_range (url->range);
  }

  return clone;
}

/*
 * Combine a base url with the current stream base url from the list of
 * baseURLs. Takes ownership of base and returns a new base.
 */
static GstUri *
combine_urls (GstUri * base, GList * list, gchar ** query, guint idx)
{
  GstBaseURL *baseURL;
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

/* select a stream and extract the baseURL (if present) */
static gchar *
gst_mpdparser_parse_baseURL (GstMpdClient * client, GstActiveStream * stream,
    gchar ** query)
{
  GstStreamPeriod *stream_period;
  static const gchar empty[] = "";
  gchar *ret = NULL;
  GstUri *abs_url;

  g_return_val_if_fail (stream != NULL, g_strdup (empty));
  stream_period = gst_mpdparser_get_stream_period (client);
  g_return_val_if_fail (stream_period != NULL, g_strdup (empty));
  g_return_val_if_fail (stream_period->period != NULL, g_strdup (empty));

  /* NULLify query return before we start */
  if (query)
    *query = NULL;

  /* initialise base url */
  abs_url =
      gst_uri_from_string (client->
      mpd_base_uri ? client->mpd_base_uri : client->mpd_uri);

  /* combine a BaseURL at the MPD level with the current base url */
  abs_url =
      combine_urls (abs_url, client->mpd_node->BaseURLs, query,
      stream->baseURL_idx);

  /* combine a BaseURL at the Period level with the current base url */
  abs_url =
      combine_urls (abs_url, stream_period->period->BaseURLs, query,
      stream->baseURL_idx);

  GST_DEBUG ("Current adaptation set id %i (%s)", stream->cur_adapt_set->id,
      stream->cur_adapt_set->contentType);
  /* combine a BaseURL at the AdaptationSet level with the current base url */
  abs_url =
      combine_urls (abs_url, stream->cur_adapt_set->BaseURLs, query,
      stream->baseURL_idx);

  /* combine a BaseURL at the Representation level with the current base url */
  abs_url =
      combine_urls (abs_url, stream->cur_representation->BaseURLs, query,
      stream->baseURL_idx);

  ret = gst_uri_to_string (abs_url);
  gst_uri_unref (abs_url);

  return ret;
}

static GstClockTime
gst_mpd_client_get_segment_duration (GstMpdClient * client,
    GstActiveStream * stream, guint64 * scale_dur)
{
  GstStreamPeriod *stream_period;
  GstMultSegmentBaseType *base = NULL;
  GstClockTime duration = 0;

  g_return_val_if_fail (stream != NULL, GST_CLOCK_TIME_NONE);
  stream_period = gst_mpdparser_get_stream_period (client);
  g_return_val_if_fail (stream_period != NULL, GST_CLOCK_TIME_NONE);

  if (stream->cur_segment_list) {
    base = stream->cur_segment_list->MultSegBaseType;
  } else if (stream->cur_seg_template) {
    base = stream->cur_seg_template->MultSegBaseType;
  }

  if (base == NULL || base->SegBaseType == NULL) {
    /* this may happen when we have a single segment */
    duration = stream_period->duration;
    if (scale_dur)
      *scale_dur = duration;
  } else {
    /* duration is guint so this cannot overflow */
    duration = base->duration * GST_SECOND;
    if (scale_dur)
      *scale_dur = duration;
    duration /= base->SegBaseType->timescale;
  }

  return duration;
}

/*****************************/
/******* API functions *******/
/*****************************/

GstMpdClient *
gst_mpd_client_new (void)
{
  GstMpdClient *client;

  client = g_new0 (GstMpdClient, 1);

  return client;
}

void
gst_active_streams_free (GstMpdClient * client)
{
  if (client->active_streams) {
    g_list_foreach (client->active_streams,
        (GFunc) gst_mpdparser_free_active_stream, NULL);
    g_list_free (client->active_streams);
    client->active_streams = NULL;
  }
}

void
gst_mpd_client_free (GstMpdClient * client)
{
  g_return_if_fail (client != NULL);

  if (client->mpd_node)
    gst_mpdparser_free_mpd_node (client->mpd_node);

  if (client->periods) {
    g_list_free_full (client->periods,
        (GDestroyNotify) gst_mpdparser_free_stream_period);
  }

  gst_active_streams_free (client);

  g_free (client->mpd_uri);
  client->mpd_uri = NULL;
  g_free (client->mpd_base_uri);
  client->mpd_base_uri = NULL;

  if (client->downloader)
    gst_object_unref (client->downloader);
  client->downloader = NULL;

  g_free (client);
}

void
gst_mpd_client_set_uri_downloader (GstMpdClient * client,
    GstUriDownloader * downloader)
{
  if (client->downloader)
    gst_object_unref (client->downloader);
  client->downloader = gst_object_ref (downloader);
}

static void
gst_mpd_client_check_profiles (GstMpdClient * client)
{
  GST_DEBUG ("Profiles: %s",
      client->mpd_node->profiles ? client->mpd_node->profiles : "<none>");

  if (!client->mpd_node->profiles)
    return;

  if (g_strstr_len (client->mpd_node->profiles, -1,
          "urn:mpeg:dash:profile:isoff-on-demand:2011")) {
    client->profile_isoff_ondemand = TRUE;
    GST_DEBUG ("Found ISOFF on demand profile (2011)");
  }
}

static void
gst_mpd_client_fetch_on_load_external_resources (GstMpdClient * client)
{
  GList *l;

  for (l = client->mpd_node->Periods; l; /* explicitly advanced below */ ) {
    GstPeriodNode *period = l->data;
    GList *m;

    if (period->xlink_href && period->actuate == GST_XLINK_ACTUATE_ON_LOAD) {
      GList *new_periods, *prev, *next;

      new_periods = gst_mpd_client_fetch_external_period (client, period);

      prev = l->prev;
      client->mpd_node->Periods =
          g_list_delete_link (client->mpd_node->Periods, l);
      gst_mpdparser_free_period_node (period);
      period = NULL;

      /* Get new next node, we will insert before this */
      if (prev)
        next = prev->next;
      else
        next = client->mpd_node->Periods;

      while (new_periods) {
        client->mpd_node->Periods =
            g_list_insert_before (client->mpd_node->Periods, next,
            new_periods->data);
        new_periods = g_list_delete_link (new_periods, new_periods);
      }
      next = NULL;

      /* Update our iterator to the first new period if any, or the next */
      if (prev)
        l = prev->next;
      else
        l = client->mpd_node->Periods;

      continue;
    }

    if (period->SegmentList && period->SegmentList->xlink_href
        && period->SegmentList->actuate == GST_XLINK_ACTUATE_ON_LOAD) {
      GstSegmentListNode *new_segment_list;

      new_segment_list =
          gst_mpd_client_fetch_external_segment_list (client, period, NULL,
          NULL, NULL, period->SegmentList);

      gst_mpdparser_free_segment_list_node (period->SegmentList);
      period->SegmentList = new_segment_list;
    }

    for (m = period->AdaptationSets; m; /* explicitly advanced below */ ) {
      GstAdaptationSetNode *adapt_set = m->data;
      GList *n;

      if (adapt_set->xlink_href
          && adapt_set->actuate == GST_XLINK_ACTUATE_ON_LOAD) {
        GList *new_adapt_sets, *prev, *next;

        new_adapt_sets =
            gst_mpd_client_fetch_external_adaptation_set (client, period,
            adapt_set);

        prev = m->prev;
        period->AdaptationSets = g_list_delete_link (period->AdaptationSets, m);
        gst_mpdparser_free_adaptation_set_node (adapt_set);
        adapt_set = NULL;

        /* Get new next node, we will insert before this */
        if (prev)
          next = prev->next;
        else
          next = period->AdaptationSets;

        while (new_adapt_sets) {
          period->AdaptationSets =
              g_list_insert_before (period->AdaptationSets, next,
              new_adapt_sets->data);
          new_adapt_sets = g_list_delete_link (new_adapt_sets, new_adapt_sets);
        }
        next = NULL;

        /* Update our iterator to the first new adapt_set if any, or the next */
        if (prev)
          m = prev->next;
        else
          m = period->AdaptationSets;

        continue;
      }

      if (adapt_set->SegmentList && adapt_set->SegmentList->xlink_href
          && adapt_set->SegmentList->actuate == GST_XLINK_ACTUATE_ON_LOAD) {
        GstSegmentListNode *new_segment_list;

        new_segment_list =
            gst_mpd_client_fetch_external_segment_list (client, period,
            adapt_set, NULL, period->SegmentList, adapt_set->SegmentList);

        gst_mpdparser_free_segment_list_node (adapt_set->SegmentList);
        adapt_set->SegmentList = new_segment_list;
      }

      for (n = adapt_set->Representations; n; n = n->next) {
        GstRepresentationNode *representation = n->data;

        if (representation->SegmentList
            && representation->SegmentList->xlink_href
            && representation->SegmentList->actuate ==
            GST_XLINK_ACTUATE_ON_LOAD) {

          GstSegmentListNode *new_segment_list;

          new_segment_list =
              gst_mpd_client_fetch_external_segment_list (client, period,
              adapt_set, representation, adapt_set->SegmentList,
              representation->SegmentList);

          gst_mpdparser_free_segment_list_node (representation->SegmentList);
          representation->SegmentList = new_segment_list;

        }
      }

      m = m->next;
    }

    l = l->next;
  }
}

gboolean
gst_mpd_parse (GstMpdClient * client, const gchar * data, gint size)
{
  gboolean ret = FALSE;

  if (data) {
    xmlDocPtr doc;
    xmlNode *root_element = NULL;

    GST_DEBUG ("MPD file fully buffered, start parsing...");

    /* parse the complete MPD file into a tree (using the libxml2 default parser API) */

    /* this initialize the library and check potential ABI mismatches
     * between the version it was compiled for and the actual shared
     * library used
     */
    LIBXML_TEST_VERSION;

    /* parse "data" into a document (which is a libxml2 tree structure xmlDoc) */
    doc = xmlReadMemory (data, size, "noname.xml", NULL, XML_PARSE_NONET);
    if (doc == NULL) {
      GST_ERROR ("failed to parse the MPD file");
      ret = FALSE;
    } else {
      /* get the root element node */
      root_element = xmlDocGetRootElement (doc);

      if (root_element->type != XML_ELEMENT_NODE
          || xmlStrcmp (root_element->name, (xmlChar *) "MPD") != 0) {
        GST_ERROR
            ("can not find the root element MPD, failed to parse the MPD file");
        ret = FALSE;            /* used to return TRUE before, but this seems wrong */
      } else {
        /* now we can parse the MPD root node and all children nodes, recursively */
        ret = gst_mpdparser_parse_root_node (&client->mpd_node, root_element);
      }
      /* free the document */
      xmlFreeDoc (doc);
    }

    if (ret) {
      gst_mpd_client_check_profiles (client);
      gst_mpd_client_fetch_on_load_external_resources (client);
    }
  }

  return ret;
}

const gchar *
gst_mpdparser_get_baseURL (GstMpdClient * client, guint indexStream)
{
  GstActiveStream *stream;

  g_return_val_if_fail (client != NULL, NULL);
  g_return_val_if_fail (client->active_streams != NULL, NULL);
  stream = g_list_nth_data (client->active_streams, indexStream);
  g_return_val_if_fail (stream != NULL, NULL);

  return stream->baseURL;
}

static GstClockTime
gst_mpdparser_get_segment_end_time (GstMpdClient * client, GPtrArray * segments,
    const GstMediaSegment * segment, gint index)
{
  const GstStreamPeriod *stream_period;
  GstClockTime end;

  if (segment->repeat >= 0)
    return segment->start + (segment->repeat + 1) * segment->duration;

  if (index < segments->len - 1) {
    const GstMediaSegment *next_segment =
        g_ptr_array_index (segments, index + 1);
    end = next_segment->start;
  } else {
    stream_period = gst_mpdparser_get_stream_period (client);
    end = stream_period->start + stream_period->duration;
  }
  return end;
}

static gboolean
gst_mpd_client_add_media_segment (GstActiveStream * stream,
    GstSegmentURLNode * url_node, guint number, gint repeat,
    guint64 scale_start, guint64 scale_duration,
    GstClockTime start, GstClockTime duration)
{
  GstMediaSegment *media_segment;

  g_return_val_if_fail (stream->segments != NULL, FALSE);

  media_segment = g_slice_new0 (GstMediaSegment);

  media_segment->SegmentURL = url_node;
  media_segment->number = number;
  media_segment->scale_start = scale_start;
  media_segment->scale_duration = scale_duration;
  media_segment->start = start;
  media_segment->duration = duration;
  media_segment->repeat = repeat;

  g_ptr_array_add (stream->segments, media_segment);
  GST_LOG ("Added new segment: number %d, repeat %d, "
      "ts: %" GST_TIME_FORMAT ", dur: %"
      GST_TIME_FORMAT, number, repeat,
      GST_TIME_ARGS (start), GST_TIME_ARGS (duration));

  return TRUE;
}

static void
gst_mpd_client_stream_update_presentation_time_offset (GstMpdClient * client,
    GstActiveStream * stream)
{
  GstSegmentBaseType *segbase = NULL;

  /* Find the used segbase */
  if (stream->cur_segment_list) {
    segbase = stream->cur_segment_list->MultSegBaseType->SegBaseType;
  } else if (stream->cur_seg_template) {
    segbase = stream->cur_seg_template->MultSegBaseType->SegBaseType;
  } else if (stream->cur_segment_base) {
    segbase = stream->cur_segment_base;
  }

  if (segbase) {
    /* Avoid overflows */
    stream->presentationTimeOffset =
        gst_util_uint64_scale (segbase->presentationTimeOffset, GST_SECOND,
        segbase->timescale);
  } else {
    stream->presentationTimeOffset = 0;
  }

  GST_LOG ("Setting stream's presentation time offset to %" GST_TIME_FORMAT,
      GST_TIME_ARGS (stream->presentationTimeOffset));
}

gboolean
gst_mpd_client_setup_representation (GstMpdClient * client,
    GstActiveStream * stream, GstRepresentationNode * representation)
{
  GstStreamPeriod *stream_period;
  GList *rep_list;
  GstClockTime PeriodStart, PeriodEnd, start_time, duration;
  guint i;
  guint64 start;

  if (stream->cur_adapt_set == NULL) {
    GST_WARNING ("No valid AdaptationSet node in the MPD file, aborting...");
    return FALSE;
  }

  rep_list = stream->cur_adapt_set->Representations;
  stream->cur_representation = representation;
  stream->representation_idx = g_list_index (rep_list, representation);

  /* clean the old segment list, if any */
  if (stream->segments) {
    g_ptr_array_unref (stream->segments);
    stream->segments = NULL;
  }

  stream_period = gst_mpdparser_get_stream_period (client);
  g_return_val_if_fail (stream_period != NULL, FALSE);
  g_return_val_if_fail (stream_period->period != NULL, FALSE);

  PeriodStart = stream_period->start;
  if (GST_CLOCK_TIME_IS_VALID (stream_period->duration))
    PeriodEnd = stream_period->start + stream_period->duration;
  else
    PeriodEnd = GST_CLOCK_TIME_NONE;

  GST_LOG ("Building segment list for Period from %" GST_TIME_FORMAT " to %"
      GST_TIME_FORMAT, GST_TIME_ARGS (PeriodStart), GST_TIME_ARGS (PeriodEnd));

  if (representation->SegmentBase != NULL
      || representation->SegmentList != NULL) {
    GList *SegmentURL;

    /* We have a fixed list of segments for any of the cases here,
     * init the segments list */
    gst_mpdparser_init_active_stream_segments (stream);

    /* get the first segment_base of the selected representation */
    if ((stream->cur_segment_base =
            gst_mpdparser_get_segment_base (stream_period->period,
                stream->cur_adapt_set, representation)) == NULL) {
      GST_DEBUG ("No useful SegmentBase node for the current Representation");
    }

    /* get the first segment_list of the selected representation */
    if ((stream->cur_segment_list =
            gst_mpdparser_get_segment_list (client, stream_period->period,
                stream->cur_adapt_set, representation)) == NULL) {
      GST_DEBUG ("No useful SegmentList node for the current Representation");
      /* here we should have a single segment for each representation, whose URL is encoded in the baseURL element */
      if (!gst_mpd_client_add_media_segment (stream, NULL, 1, 0, 0,
              PeriodEnd - PeriodStart, 0, PeriodEnd - PeriodStart)) {
        return FALSE;
      }
    } else {
      /* build the list of GstMediaSegment nodes from the SegmentList node */
      SegmentURL = stream->cur_segment_list->SegmentURL;
      if (SegmentURL == NULL) {
        GST_WARNING
            ("No valid list of SegmentURL nodes in the MPD file, aborting...");
        return FALSE;
      }

      /* build segment list */
      i = stream->cur_segment_list->MultSegBaseType->startNumber;
      start = 0;
      start_time = 0;

      GST_LOG ("Building media segment list using a SegmentList node");
      if (stream->cur_segment_list->MultSegBaseType->SegmentTimeline) {
        GstSegmentTimelineNode *timeline;
        GstSNode *S;
        GList *list;

        timeline = stream->cur_segment_list->MultSegBaseType->SegmentTimeline;
        for (list = g_queue_peek_head_link (&timeline->S); list;
            list = g_list_next (list)) {
          guint timescale;

          S = (GstSNode *) list->data;
          GST_LOG ("Processing S node: d=%" G_GUINT64_FORMAT " r=%d t=%"
              G_GUINT64_FORMAT, S->d, S->r, S->t);
          timescale =
              stream->cur_segment_list->MultSegBaseType->SegBaseType->timescale;
          duration = gst_util_uint64_scale (S->d, GST_SECOND, timescale);

          if (S->t > 0) {
            start = S->t;
            start_time = gst_util_uint64_scale (S->t, GST_SECOND, timescale);
          }

          if (!SegmentURL) {
            GST_WARNING
                ("SegmentTimeline does not have a matching SegmentURL, aborting...");
            return FALSE;
          }

          if (!gst_mpd_client_add_media_segment (stream, SegmentURL->data, i,
                  S->r, start, S->d, start_time, duration)) {
            return FALSE;
          }
          i += S->r + 1;
          start_time += duration * (S->r + 1);
          start += S->d * (S->r + 1);
          SegmentURL = g_list_next (SegmentURL);
        }
      } else {
        guint64 scale_dur;

        duration =
            gst_mpd_client_get_segment_duration (client, stream, &scale_dur);
        if (!GST_CLOCK_TIME_IS_VALID (duration))
          return FALSE;

        while (SegmentURL) {
          if (!gst_mpd_client_add_media_segment (stream, SegmentURL->data, i,
                  0, start, scale_dur, start_time, duration)) {
            return FALSE;
          }
          i++;
          start += scale_dur;
          start_time += duration;
          SegmentURL = g_list_next (SegmentURL);
        }
      }
    }
  } else {
    if (representation->SegmentTemplate != NULL) {
      stream->cur_seg_template = representation->SegmentTemplate;
    } else if (stream->cur_adapt_set->SegmentTemplate != NULL) {
      stream->cur_seg_template = stream->cur_adapt_set->SegmentTemplate;
    } else if (stream_period->period->SegmentTemplate != NULL) {
      stream->cur_seg_template = stream_period->period->SegmentTemplate;
    }

    if (stream->cur_seg_template == NULL
        || stream->cur_seg_template->MultSegBaseType == NULL) {

      gst_mpdparser_init_active_stream_segments (stream);
      /* here we should have a single segment for each representation, whose URL is encoded in the baseURL element */
      if (!gst_mpd_client_add_media_segment (stream, NULL, 1, 0, 0,
              PeriodEnd - PeriodStart, 0, PeriodEnd - PeriodStart)) {
        return FALSE;
      }
    } else {
      GstMultSegmentBaseType *mult_seg =
          stream->cur_seg_template->MultSegBaseType;
      /* build segment list */
      i = mult_seg->startNumber;
      start = 0;
      start_time = 0;

      GST_LOG ("Building media segment list using this template: %s",
          stream->cur_seg_template->media);

      if (mult_seg->SegmentTimeline) {
        GstSegmentTimelineNode *timeline;
        GstSNode *S;
        GList *list;

        timeline = mult_seg->SegmentTimeline;
        gst_mpdparser_init_active_stream_segments (stream);
        for (list = g_queue_peek_head_link (&timeline->S); list;
            list = g_list_next (list)) {
          guint timescale;

          S = (GstSNode *) list->data;
          GST_LOG ("Processing S node: d=%" G_GUINT64_FORMAT " r=%u t=%"
              G_GUINT64_FORMAT, S->d, S->r, S->t);
          timescale = mult_seg->SegBaseType->timescale;
          duration = gst_util_uint64_scale (S->d, GST_SECOND, timescale);
          if (S->t > 0) {
            start = S->t;
            start_time = gst_util_uint64_scale (S->t, GST_SECOND, timescale);
          }

          if (!gst_mpd_client_add_media_segment (stream, NULL, i, S->r, start,
                  S->d, start_time, duration)) {
            return FALSE;
          }
          i += S->r + 1;
          start += S->d * (S->r + 1);
          start_time += duration * (S->r + 1);
        }
      } else {
        /* NOP - The segment is created on demand with the template, no need
         * to build a list */
      }
    }
  }

  /* clip duration of segments to stop at period end */
  if (stream->segments && stream->segments->len) {
    if (GST_CLOCK_TIME_IS_VALID (PeriodEnd)) {
      guint n;

      for (n = 0; n < stream->segments->len; ++n) {
        GstMediaSegment *media_segment =
            g_ptr_array_index (stream->segments, n);
        if (media_segment) {
          if (media_segment->start + media_segment->duration >
              PeriodEnd - PeriodStart) {
            GstClockTime stop = PeriodEnd - PeriodStart;
            if (n < stream->segments->len - 1) {
              GstMediaSegment *next_segment =
                  g_ptr_array_index (stream->segments, n + 1);
              if (next_segment && next_segment->start < PeriodEnd - PeriodStart)
                stop = next_segment->start;
            }
            media_segment->duration =
                media_segment->start > stop ? 0 : stop - media_segment->start;
            GST_LOG ("Fixed duration of segment %u: %" GST_TIME_FORMAT, n,
                GST_TIME_ARGS (media_segment->duration));

            /* If the segment was clipped entirely, we discard it and all
             * subsequent ones */
            if (media_segment->duration == 0) {
              GST_WARNING ("Discarding %u segments outside period",
                  stream->segments->len - n);
              /* _set_size should properly unref elements */
              g_ptr_array_set_size (stream->segments, n);
              break;
            }
          }
        }
      }
    }
#ifndef GST_DISABLE_GST_DEBUG
    if (stream->segments->len > 0) {
      GstMediaSegment *last_media_segment =
          g_ptr_array_index (stream->segments, stream->segments->len - 1);
      GST_LOG ("Built a list of %d segments", last_media_segment->number);
    } else {
      GST_LOG ("All media segments were clipped");
    }
#endif
  }

  g_free (stream->baseURL);
  g_free (stream->queryURL);
  stream->baseURL =
      gst_mpdparser_parse_baseURL (client, stream, &stream->queryURL);

  gst_mpd_client_stream_update_presentation_time_offset (client, stream);

  return TRUE;
}

#define CUSTOM_WRAPPER_START "<custom_wrapper>"
#define CUSTOM_WRAPPER_END "</custom_wrapper>"

static GList *
gst_mpd_client_fetch_external_period (GstMpdClient * client,
    GstPeriodNode * period_node)
{
  GstFragment *download;
  GstAdapter *adapter;
  GstBuffer *period_buffer;
  GError *err = NULL;
  xmlDocPtr doc = NULL;
  GstUri *base_uri, *uri;
  gchar *query = NULL;
  gchar *uri_string, *wrapper;
  GList *new_periods = NULL;
  const gchar *data;

  /* ISO/IEC 23009-1:2014 5.5.3 4)
   * Remove nodes that resolve to nothing when resolving
   */
  if (strcmp (period_node->xlink_href,
          "urn:mpeg:dash:resolve-to-zero:2013") == 0) {
    return NULL;
  }

  if (!client->downloader) {
    return NULL;
  }

  /* Build absolute URI */

  /* Get base URI at the MPD level */
  base_uri =
      gst_uri_from_string (client->
      mpd_base_uri ? client->mpd_base_uri : client->mpd_uri);

  /* combine a BaseURL at the MPD level with the current base url */
  base_uri = combine_urls (base_uri, client->mpd_node->BaseURLs, &query, 0);
  uri = gst_uri_from_string_with_base (base_uri, period_node->xlink_href);
  if (query)
    gst_uri_set_query_string (uri, query);
  g_free (query);
  uri_string = gst_uri_to_string (uri);
  gst_uri_unref (base_uri);
  gst_uri_unref (uri);

  download =
      gst_uri_downloader_fetch_uri (client->downloader,
      uri_string, client->mpd_uri, TRUE, FALSE, TRUE, &err);
  g_free (uri_string);

  if (!download) {
    GST_ERROR ("Failed to download external Period node at '%s': %s",
        period_node->xlink_href, err->message);
    g_clear_error (&err);
    return NULL;
  }

  period_buffer = gst_fragment_get_buffer (download);
  g_object_unref (download);

  /* external xml could have multiple period without root xmlNode.
   * To avoid xml parsing error caused by no root node, wrapping it with
   * custom root node */
  adapter = gst_adapter_new ();

  wrapper = g_new (gchar, strlen (CUSTOM_WRAPPER_START));
  memcpy (wrapper, CUSTOM_WRAPPER_START, strlen (CUSTOM_WRAPPER_START));
  gst_adapter_push (adapter,
      gst_buffer_new_wrapped (wrapper, strlen (CUSTOM_WRAPPER_START)));

  gst_adapter_push (adapter, period_buffer);

  wrapper = g_strdup (CUSTOM_WRAPPER_END);
  gst_adapter_push (adapter,
      gst_buffer_new_wrapped (wrapper, strlen (CUSTOM_WRAPPER_END) + 1));

  data = gst_adapter_map (adapter, gst_adapter_available (adapter));

  doc =
      xmlReadMemory (data, gst_adapter_available (adapter), "noname.xml", NULL,
      XML_PARSE_NONET);

  gst_adapter_unmap (adapter);
  gst_adapter_clear (adapter);
  gst_object_unref (adapter);

  if (doc) {
    xmlNode *root_element = xmlDocGetRootElement (doc);
    xmlNode *iter;

    if (root_element->type != XML_ELEMENT_NODE)
      goto error;

    for (iter = root_element->children; iter; iter = iter->next) {
      if (iter->type == XML_ELEMENT_NODE) {
        if (xmlStrcmp (iter->name, (xmlChar *) "Period") == 0) {
          gst_mpdparser_parse_period_node (&new_periods, iter);
        } else {
          goto error;
        }
      }
    }
  } else {
    goto error;
  }

done:
  if (doc)
    xmlFreeDoc (doc);

  return new_periods;

error:
  GST_ERROR ("Failed to parse period node XML");

  if (new_periods) {
    g_list_free_full (new_periods,
        (GDestroyNotify) gst_mpdparser_free_period_node);
    new_periods = NULL;
  }
  goto done;
}

gboolean
gst_mpd_client_setup_media_presentation (GstMpdClient * client,
    GstClockTime time, gint period_idx, const gchar * period_id)
{
  GstStreamPeriod *stream_period;
  GstClockTime start, duration;
  GList *list, *next;
  guint idx;
  gboolean ret = FALSE;

  g_return_val_if_fail (client != NULL, FALSE);
  g_return_val_if_fail (client->mpd_node != NULL, FALSE);

  /* Check if we set up the media presentation far enough already */
  for (list = client->periods; list; list = list->next) {
    GstStreamPeriod *stream_period = list->data;

    if ((time != GST_CLOCK_TIME_NONE
            && stream_period->duration != GST_CLOCK_TIME_NONE
            && stream_period->start + stream_period->duration >= time)
        || (time != GST_CLOCK_TIME_NONE && stream_period->start >= time))
      return TRUE;

    if (period_idx != -1 && stream_period->number >= period_idx)
      return TRUE;

    if (period_id != NULL && stream_period->period->id != NULL
        && strcmp (stream_period->period->id, period_id) == 0)
      return TRUE;

  }

  GST_DEBUG ("Building the list of Periods in the Media Presentation");
  /* clean the old period list, if any */
  /* TODO: In theory we could reuse the ones we have so far but that
   * seems more complicated than the overhead caused here
   */
  if (client->periods) {
    g_list_foreach (client->periods,
        (GFunc) gst_mpdparser_free_stream_period, NULL);
    g_list_free (client->periods);
    client->periods = NULL;
  }

  idx = 0;
  start = 0;
  duration = GST_CLOCK_TIME_NONE;

  if (client->mpd_node->mediaPresentationDuration <= 0 &&
      client->mpd_node->mediaPresentationDuration != -1) {
    /* Invalid MPD file: MPD duration is negative or zero */
    goto syntax_error;
  }

  for (list = client->mpd_node->Periods; list; /* explicitly advanced below */ ) {
    GstPeriodNode *period_node = list->data;
    GstPeriodNode *next_period_node = NULL;

    /* Download external period */
    if (period_node->xlink_href) {
      GList *new_periods;
      GList *prev;

      new_periods = gst_mpd_client_fetch_external_period (client, period_node);

      prev = list->prev;
      client->mpd_node->Periods =
          g_list_delete_link (client->mpd_node->Periods, list);
      gst_mpdparser_free_period_node (period_node);
      period_node = NULL;

      /* Get new next node, we will insert before this */
      if (prev)
        next = prev->next;
      else
        next = client->mpd_node->Periods;

      while (new_periods) {
        client->mpd_node->Periods =
            g_list_insert_before (client->mpd_node->Periods, next,
            new_periods->data);
        new_periods = g_list_delete_link (new_periods, new_periods);
      }
      next = NULL;

      /* Update our iterator to the first new period if any, or the next */
      if (prev)
        list = prev->next;
      else
        list = client->mpd_node->Periods;

      /* And try again */
      continue;
    }

    if (period_node->start != -1) {
      /* we have a regular period */
      /* start cannot be smaller than previous start */
      if (list != g_list_first (client->mpd_node->Periods)
          && start >= period_node->start * GST_MSECOND) {
        /* Invalid MPD file: duration would be negative or zero */
        goto syntax_error;
      }
      start = period_node->start * GST_MSECOND;
    } else if (duration != GST_CLOCK_TIME_NONE) {
      /* start time inferred from previous period, this is still a regular period */
      start += duration;
    } else if (idx == 0 && client->mpd_node->type == GST_MPD_FILE_TYPE_STATIC) {
      /* first period of a static MPD file, start time is 0 */
      start = 0;
    } else if (client->mpd_node->type == GST_MPD_FILE_TYPE_DYNAMIC) {
      /* this should be a live stream, let this pass */
    } else {
      /* this is an 'Early Available Period' */
      goto early;
    }

    /* compute duration.
       If there is a start time for the next period, or this is the last period
       and mediaPresentationDuration was set, those values will take precedence
       over a configured period duration in computing this period's duration

       ISO/IEC 23009-1:2014(E), chapter 5.3.2.1
       "The Period extends until the PeriodStart of the next Period, or until
       the end of the Media Presentation in the case of the last Period."
     */

    while ((next = g_list_next (list)) != NULL) {
      /* try to infer this period duration from the start time of the next period */
      next_period_node = next->data;

      if (next_period_node->xlink_href) {
        GList *new_periods;

        new_periods =
            gst_mpd_client_fetch_external_period (client, next_period_node);

        client->mpd_node->Periods =
            g_list_delete_link (client->mpd_node->Periods, next);
        gst_mpdparser_free_period_node (next_period_node);
        next_period_node = NULL;
        /* Get new next node, we will insert before this */
        next = g_list_next (list);
        while (new_periods) {
          client->mpd_node->Periods =
              g_list_insert_before (client->mpd_node->Periods, next,
              new_periods->data);
          new_periods = g_list_delete_link (new_periods, new_periods);
        }

        /* And try again, getting the next list element which is now our newly
         * inserted nodes. If any */
      } else {
        /* Got the next period and it doesn't have to be downloaded first */
        break;
      }
    }

    if (next_period_node) {
      if (next_period_node->start != -1) {
        if (start >= next_period_node->start * GST_MSECOND) {
          /* Invalid MPD file: duration would be negative or zero */
          goto syntax_error;
        }
        duration = next_period_node->start * GST_MSECOND - start;
      } else if (period_node->duration != -1) {
        if (period_node->duration <= 0) {
          /* Invalid MPD file: duration would be negative or zero */
          goto syntax_error;
        }
        duration = period_node->duration * GST_MSECOND;
      } else if (client->mpd_node->type == GST_MPD_FILE_TYPE_DYNAMIC) {
        /* might be a live file, ignore unspecified duration */
      } else {
        /* Invalid MPD file! */
        goto syntax_error;
      }
    } else if (client->mpd_node->mediaPresentationDuration != -1) {
      /* last Period of the Media Presentation */
      if (client->mpd_node->mediaPresentationDuration * GST_MSECOND <= start) {
        /* Invalid MPD file: duration would be negative or zero */
        goto syntax_error;
      }
      duration =
          client->mpd_node->mediaPresentationDuration * GST_MSECOND - start;
    } else if (period_node->duration != -1) {
      duration = period_node->duration * GST_MSECOND;
    } else if (client->mpd_node->type == GST_MPD_FILE_TYPE_DYNAMIC) {
      /* might be a live file, ignore unspecified duration */
    } else {
      /* Invalid MPD file! */
      goto syntax_error;
    }

    stream_period = g_slice_new0 (GstStreamPeriod);
    client->periods = g_list_append (client->periods, stream_period);
    stream_period->period = period_node;
    stream_period->number = idx++;
    stream_period->start = start;
    stream_period->duration = duration;
    ret = TRUE;
    GST_LOG (" - added Period %d start=%" GST_TIME_FORMAT " duration=%"
        GST_TIME_FORMAT, idx, GST_TIME_ARGS (start), GST_TIME_ARGS (duration));

    if ((time != GST_CLOCK_TIME_NONE
            && stream_period->duration != GST_CLOCK_TIME_NONE
            && stream_period->start + stream_period->duration >= time)
        || (time != GST_CLOCK_TIME_NONE && stream_period->start >= time))
      break;

    if (period_idx != -1 && stream_period->number >= period_idx)
      break;

    if (period_id != NULL && stream_period->period->id != NULL
        && strcmp (stream_period->period->id, period_id) == 0)
      break;

    list = list->next;
  }

  GST_DEBUG
      ("Found a total of %d valid Periods in the Media Presentation up to this point",
      idx);
  return ret;

early:
  GST_WARNING
      ("Found an Early Available Period, skipping the rest of the Media Presentation");
  return ret;

syntax_error:
  GST_WARNING
      ("Cannot get the duration of the Period %d, skipping the rest of the Media Presentation",
      idx);
  return ret;
}

static GList *
gst_mpd_client_fetch_external_adaptation_set (GstMpdClient * client,
    GstPeriodNode * period, GstAdaptationSetNode * adapt_set)
{
  GstFragment *download;
  GstBuffer *adapt_set_buffer;
  GstMapInfo map;
  GError *err = NULL;
  xmlDocPtr doc = NULL;
  GstUri *base_uri, *uri;
  gchar *query = NULL;
  gchar *uri_string;
  GList *new_adapt_sets = NULL;

  /* ISO/IEC 23009-1:2014 5.5.3 4)
   * Remove nodes that resolve to nothing when resolving
   */
  if (strcmp (adapt_set->xlink_href, "urn:mpeg:dash:resolve-to-zero:2013") == 0) {
    return NULL;
  }

  if (!client->downloader) {
    return NULL;
  }

  /* Build absolute URI */

  /* Get base URI at the MPD level */
  base_uri =
      gst_uri_from_string (client->
      mpd_base_uri ? client->mpd_base_uri : client->mpd_uri);

  /* combine a BaseURL at the MPD level with the current base url */
  base_uri = combine_urls (base_uri, client->mpd_node->BaseURLs, &query, 0);

  /* combine a BaseURL at the Period level with the current base url */
  base_uri = combine_urls (base_uri, period->BaseURLs, &query, 0);

  uri = gst_uri_from_string_with_base (base_uri, adapt_set->xlink_href);
  if (query)
    gst_uri_set_query_string (uri, query);
  g_free (query);
  uri_string = gst_uri_to_string (uri);
  gst_uri_unref (base_uri);
  gst_uri_unref (uri);

  download =
      gst_uri_downloader_fetch_uri (client->downloader,
      uri_string, client->mpd_uri, TRUE, FALSE, TRUE, &err);
  g_free (uri_string);

  if (!download) {
    GST_ERROR ("Failed to download external AdaptationSet node at '%s': %s",
        adapt_set->xlink_href, err->message);
    g_clear_error (&err);
    return NULL;
  }

  adapt_set_buffer = gst_fragment_get_buffer (download);
  g_object_unref (download);

  gst_buffer_map (adapt_set_buffer, &map, GST_MAP_READ);

  doc =
      xmlReadMemory ((const gchar *) map.data, map.size, "noname.xml", NULL,
      XML_PARSE_NONET);

  gst_buffer_unmap (adapt_set_buffer, &map);
  gst_buffer_unref (adapt_set_buffer);

  /* NOTE: ISO/IEC 23009-1:2014 5.3.3.2 is saying that exactly one AdaptationSet
   * in external xml is allowed */
  if (doc) {
    xmlNode *root_element = xmlDocGetRootElement (doc);

    if (root_element->type != XML_ELEMENT_NODE ||
        xmlStrcmp (root_element->name, (xmlChar *) "AdaptationSet") != 0) {
      goto error;
    }

    gst_mpdparser_parse_adaptation_set_node (&new_adapt_sets, root_element,
        period);
  } else {
    goto error;
  }

done:
  if (doc)
    xmlFreeDoc (doc);

  return new_adapt_sets;

error:
  GST_ERROR ("Failed to parse adaptation set node XML");
  goto done;
}

static GList *
gst_mpd_client_get_adaptation_sets_for_period (GstMpdClient * client,
    GstStreamPeriod * period)
{
  GList *list;

  g_return_val_if_fail (period != NULL, NULL);

  /* Resolve all external adaptation sets of this period. Every user of
   * the adaptation sets would need to know the content of all adaptation sets
   * to decide which one to use, so we have to resolve them all here
   */
  for (list = period->period->AdaptationSets; list;
      /* advanced explicitely below */ ) {
    GstAdaptationSetNode *adapt_set = (GstAdaptationSetNode *) list->data;
    GList *new_adapt_sets = NULL, *prev, *next;

    if (!adapt_set->xlink_href) {
      list = list->next;
      continue;
    }

    new_adapt_sets =
        gst_mpd_client_fetch_external_adaptation_set (client, period->period,
        adapt_set);

    prev = list->prev;
    period->period->AdaptationSets =
        g_list_delete_link (period->period->AdaptationSets, list);
    gst_mpdparser_free_adaptation_set_node (adapt_set);
    adapt_set = NULL;

    /* Get new next node, we will insert before this */
    if (prev)
      next = prev->next;
    else
      next = period->period->AdaptationSets;

    while (new_adapt_sets) {
      period->period->AdaptationSets =
          g_list_insert_before (period->period->AdaptationSets, next,
          new_adapt_sets->data);
      new_adapt_sets = g_list_delete_link (new_adapt_sets, new_adapt_sets);
    }

    /* Update our iterator to the first new adaptation set if any, or the next */
    if (prev)
      list = prev->next;
    else
      list = period->period->AdaptationSets;
  }

  return period->period->AdaptationSets;
}

GList *
gst_mpd_client_get_adaptation_sets (GstMpdClient * client)
{
  GstStreamPeriod *stream_period;

  stream_period = gst_mpdparser_get_stream_period (client);
  if (stream_period == NULL || stream_period->period == NULL) {
    GST_DEBUG ("No more Period nodes in the MPD file, terminating...");
    return NULL;
  }

  return gst_mpd_client_get_adaptation_sets_for_period (client, stream_period);
}

gboolean
gst_mpd_client_setup_streaming (GstMpdClient * client,
    GstAdaptationSetNode * adapt_set)
{
  GstRepresentationNode *representation;
  GList *rep_list = NULL;
  GstActiveStream *stream;

  rep_list = adapt_set->Representations;
  if (!rep_list) {
    GST_WARNING ("Can not retrieve any representation, aborting...");
    return FALSE;
  }

  stream = g_slice_new0 (GstActiveStream);
  gst_mpdparser_init_active_stream_segments (stream);

  stream->baseURL_idx = 0;
  stream->cur_adapt_set = adapt_set;

  GST_DEBUG ("0. Current stream %p", stream);

#if 0
  /* fast start */
  representation =
      gst_mpdparser_get_representation_with_max_bandwidth (rep_list,
      stream->max_bandwidth);

  if (!representation) {
    GST_WARNING
        ("Can not retrieve a representation with the requested bandwidth");
    representation = gst_mpdparser_get_lowest_representation (rep_list);
  }
#else
  /* slow start */
  representation = gst_mpdparser_get_lowest_representation (rep_list);
#endif

  if (!representation) {
    GST_WARNING ("No valid representation in the MPD file, aborting...");
    gst_mpdparser_free_active_stream (stream);
    return FALSE;
  }
  stream->mimeType =
      gst_mpdparser_representation_get_mimetype (adapt_set, representation);
  if (stream->mimeType == GST_STREAM_UNKNOWN) {
    GST_WARNING ("Unknown mime type in the representation, aborting...");
    gst_mpdparser_free_active_stream (stream);
    return FALSE;
  }

  client->active_streams = g_list_append (client->active_streams, stream);
  if (!gst_mpd_client_setup_representation (client, stream, representation)) {
    GST_WARNING ("Failed to setup the representation, aborting...");
    return FALSE;
  }

  GST_INFO ("Successfully setup the download pipeline for mimeType %d",
      stream->mimeType);

  return TRUE;
}

gboolean
gst_mpd_client_stream_seek (GstMpdClient * client, GstActiveStream * stream,
    gboolean forward, GstSeekFlags flags, GstClockTime ts,
    GstClockTime * final_ts)
{
  gint index = 0;
  gint repeat_index = 0;
  GstMediaSegment *selectedChunk = NULL;

  g_return_val_if_fail (stream != NULL, 0);

  if (stream->segments) {
    for (index = 0; index < stream->segments->len; index++) {
      gboolean in_segment = FALSE;
      GstMediaSegment *segment = g_ptr_array_index (stream->segments, index);
      GstClockTime end_time;

      GST_DEBUG ("Looking at fragment sequence chunk %d / %d", index,
          stream->segments->len);

      end_time =
          gst_mpdparser_get_segment_end_time (client, stream->segments,
          segment, index);

      /* avoid downloading another fragment just for 1ns in reverse mode */
      if (forward)
        in_segment = ts < end_time;
      else
        in_segment = ts <= end_time;

      if (in_segment) {
        GstClockTime chunk_time;

        selectedChunk = segment;
        repeat_index = (ts - segment->start) / segment->duration;

        chunk_time = segment->start + segment->duration * repeat_index;

        /* At the end of a segment in reverse mode, start from the previous fragment */
        if (!forward && repeat_index > 0
            && ((ts - segment->start) % segment->duration == 0))
          repeat_index--;

        if ((flags & GST_SEEK_FLAG_SNAP_NEAREST) == GST_SEEK_FLAG_SNAP_NEAREST) {
          if (repeat_index + 1 < segment->repeat) {
            if (ts - chunk_time > chunk_time + segment->duration - ts)
              repeat_index++;
          } else if (index + 1 < stream->segments->len) {
            GstMediaSegment *next_segment =
                g_ptr_array_index (stream->segments, index + 1);

            if (ts - chunk_time > next_segment->start - ts) {
              repeat_index = 0;
              selectedChunk = next_segment;
              index++;
            }
          }
        } else if (((forward && flags & GST_SEEK_FLAG_SNAP_AFTER) ||
                (!forward && flags & GST_SEEK_FLAG_SNAP_BEFORE)) &&
            ts != chunk_time) {

          if (repeat_index + 1 < segment->repeat) {
            repeat_index++;
          } else {
            repeat_index = 0;
            if (index + 1 >= stream->segments->len) {
              selectedChunk = NULL;
            } else {
              selectedChunk = g_ptr_array_index (stream->segments, ++index);
            }
          }
        }
        break;
      }
    }

    if (selectedChunk == NULL) {
      stream->segment_index = stream->segments->len;
      stream->segment_repeat_index = 0;
      GST_DEBUG ("Seek to after last segment");
      return FALSE;
    }

    if (final_ts)
      *final_ts = selectedChunk->start + selectedChunk->duration * repeat_index;
  } else {
    GstClockTime duration =
        gst_mpd_client_get_segment_duration (client, stream, NULL);
    GstStreamPeriod *stream_period = gst_mpdparser_get_stream_period (client);
    guint segments_count = gst_mpd_client_get_segments_counts (client, stream);
    GstClockTime index_time;

    g_return_val_if_fail (stream->cur_seg_template->
        MultSegBaseType->SegmentTimeline == NULL, FALSE);
    if (!GST_CLOCK_TIME_IS_VALID (duration)) {
      return FALSE;
    }

    if (ts > stream_period->start)
      ts -= stream_period->start;
    else
      ts = 0;

    index = ts / duration;

    /* At the end of a segment in reverse mode, start from the previous fragment */
    if (!forward && index > 0 && ts % duration == 0)
      index--;

    index_time = index * duration;

    if ((flags & GST_SEEK_FLAG_SNAP_NEAREST) == GST_SEEK_FLAG_SNAP_NEAREST) {
      if (ts - index_time > index_time + duration - ts)
        index++;
    } else if (((forward && flags & GST_SEEK_FLAG_SNAP_AFTER) ||
            (!forward && flags & GST_SEEK_FLAG_SNAP_BEFORE))
        && ts != index_time) {
      index++;
    }

    if (segments_count > 0 && index >= segments_count) {
      stream->segment_index = segments_count;
      stream->segment_repeat_index = 0;
      GST_DEBUG ("Seek to after last segment");
      return FALSE;
    }
    if (final_ts)
      *final_ts = index * duration;
  }

  stream->segment_repeat_index = repeat_index;
  stream->segment_index = index;

  return TRUE;
}

gint64
gst_mpd_client_calculate_time_difference (const GstDateTime * t1,
    const GstDateTime * t2)
{
  GDateTime *gdt1, *gdt2;
  GTimeSpan diff;

  g_assert (t1 != NULL && t2 != NULL);
  gdt1 = gst_date_time_to_g_date_time ((GstDateTime *) t1);
  gdt2 = gst_date_time_to_g_date_time ((GstDateTime *) t2);
  diff = g_date_time_difference (gdt2, gdt1);
  g_date_time_unref (gdt1);
  g_date_time_unref (gdt2);
  return diff * GST_USECOND;
}

GstDateTime *
gst_mpd_client_add_time_difference (GstDateTime * t1, gint64 usecs)
{
  GDateTime *gdt;
  GDateTime *gdt2;
  GstDateTime *rv;

  g_assert (t1 != NULL);
  gdt = gst_date_time_to_g_date_time (t1);
  g_assert (gdt != NULL);
  gdt2 = g_date_time_add (gdt, usecs);
  g_assert (gdt2 != NULL);
  g_date_time_unref (gdt);
  rv = gst_date_time_new_from_g_date_time (gdt2);

  /* Don't g_date_time_unref(gdt2) because gst_date_time_new_from_g_date_time takes
   * ownership of the GDateTime pointer.
   */

  return rv;
}

static GstDateTime *
gst_mpd_client_get_availability_start_time (GstMpdClient * client)
{
  GstDateTime *start_time;

  if (client == NULL)
    return (GstDateTime *) NULL;

  start_time = client->mpd_node->availabilityStartTime;
  if (start_time)
    gst_date_time_ref (start_time);
  return start_time;
}

gboolean
gst_mpd_client_get_last_fragment_timestamp_end (GstMpdClient * client,
    guint stream_idx, GstClockTime * ts)
{
  GstActiveStream *stream;
  gint segment_idx;
  GstMediaSegment *currentChunk;
  GstStreamPeriod *stream_period;

  GST_DEBUG ("Stream index: %i", stream_idx);
  stream = g_list_nth_data (client->active_streams, stream_idx);
  g_return_val_if_fail (stream != NULL, 0);

  if (!stream->segments) {
    stream_period = gst_mpdparser_get_stream_period (client);
    *ts = stream_period->start + stream_period->duration;
  } else {
    segment_idx = gst_mpd_client_get_segments_counts (client, stream) - 1;
    currentChunk = g_ptr_array_index (stream->segments, segment_idx);

    if (currentChunk->repeat >= 0) {
      *ts =
          currentChunk->start + (currentChunk->duration * (1 +
              currentChunk->repeat));
    } else {
      /* 5.3.9.6.1: negative repeat means repeat till the end of the
       * period, or the next update of the MPD (which I think is
       * implicit, as this will all get deleted/recreated), or the
       * start of the next segment, if any. */
      stream_period = gst_mpdparser_get_stream_period (client);
      *ts = stream_period->start + stream_period->duration;
    }
  }

  return TRUE;
}

gboolean
gst_mpd_client_get_next_fragment_timestamp (GstMpdClient * client,
    guint stream_idx, GstClockTime * ts)
{
  GstActiveStream *stream;
  GstMediaSegment *currentChunk;

  GST_DEBUG ("Stream index: %i", stream_idx);
  stream = g_list_nth_data (client->active_streams, stream_idx);
  g_return_val_if_fail (stream != NULL, 0);

  if (stream->segments) {
    GST_DEBUG ("Looking for fragment sequence chunk %d / %d",
        stream->segment_index, stream->segments->len);
    if (stream->segment_index >= stream->segments->len)
      return FALSE;
    currentChunk = g_ptr_array_index (stream->segments, stream->segment_index);

    *ts =
        currentChunk->start +
        (currentChunk->duration * stream->segment_repeat_index);
  } else {
    GstClockTime duration =
        gst_mpd_client_get_segment_duration (client, stream, NULL);
    guint segments_count = gst_mpd_client_get_segments_counts (client, stream);

    g_return_val_if_fail (stream->cur_seg_template->
        MultSegBaseType->SegmentTimeline == NULL, FALSE);
    if (!GST_CLOCK_TIME_IS_VALID (duration) || (segments_count > 0
            && stream->segment_index >= segments_count)) {
      return FALSE;
    }
    *ts = stream->segment_index * duration;
  }

  return TRUE;
}

GstClockTime
gst_mpd_parser_get_stream_presentation_offset (GstMpdClient * client,
    guint stream_idx)
{
  GstActiveStream *stream = NULL;

  g_return_val_if_fail (client != NULL, 0);
  g_return_val_if_fail (client->active_streams != NULL, 0);
  stream = g_list_nth_data (client->active_streams, stream_idx);
  g_return_val_if_fail (stream != NULL, 0);

  return stream->presentationTimeOffset;
}

GstClockTime
gst_mpd_parser_get_period_start_time (GstMpdClient * client)
{
  GstStreamPeriod *stream_period = NULL;

  g_return_val_if_fail (client != NULL, 0);
  stream_period = gst_mpdparser_get_stream_period (client);
  g_return_val_if_fail (stream_period != NULL, 0);

  return stream_period->start;
}

/**
 * gst_mpd_client_get_utc_timing_sources:
 * @client: #GstMpdClient to check for UTCTiming elements
 * @methods: A bit mask of #GstMPDUTCTimingType that specifies the methods
 *     to search for.
 * @selected_method: (nullable): The selected method
 * Returns: (transfer none): A NULL terminated array of URLs of servers
 *     that use @selected_method to provide a realtime clock.
 *
 * Searches the UTCTiming elements found in the manifest for an element
 * that uses one of the UTC timing methods specified in @selected_method.
 * If multiple UTCTiming elements are present that support one of the
 * methods specified in @selected_method, the first one is returned.
 *
 * Since: 1.6
 */
gchar **
gst_mpd_client_get_utc_timing_sources (GstMpdClient * client,
    guint methods, GstMPDUTCTimingType * selected_method)
{
  GList *list;

  g_return_val_if_fail (client != NULL, NULL);
  g_return_val_if_fail (client->mpd_node != NULL, NULL);
  for (list = g_list_first (client->mpd_node->UTCTiming); list;
      list = g_list_next (list)) {
    const GstUTCTimingNode *node = (const GstUTCTimingNode *) list->data;
    if (node->method & methods) {
      if (selected_method) {
        *selected_method = node->method;
      }
      return node->urls;
    }
  }
  return NULL;
}

gboolean
gst_mpd_client_get_next_fragment (GstMpdClient * client,
    guint indexStream, GstMediaFragmentInfo * fragment)
{
  GstActiveStream *stream = NULL;
  GstMediaSegment *currentChunk;
  gchar *mediaURL = NULL;
  gchar *indexURL = NULL;
  GstUri *base_url, *frag_url;

  /* select stream */
  g_return_val_if_fail (client != NULL, FALSE);
  g_return_val_if_fail (client->active_streams != NULL, FALSE);
  stream = g_list_nth_data (client->active_streams, indexStream);
  g_return_val_if_fail (stream != NULL, FALSE);
  g_return_val_if_fail (stream->cur_representation != NULL, FALSE);

  if (stream->segments) {
    GST_DEBUG ("Looking for fragment sequence chunk %d / %d",
        stream->segment_index, stream->segments->len);
    if (stream->segment_index >= stream->segments->len)
      return FALSE;
  } else {
    GstClockTime duration = gst_mpd_client_get_segment_duration (client,
        stream, NULL);
    guint segments_count = gst_mpd_client_get_segments_counts (client, stream);

    g_return_val_if_fail (stream->cur_seg_template->
        MultSegBaseType->SegmentTimeline == NULL, FALSE);
    if (!GST_CLOCK_TIME_IS_VALID (duration) || (segments_count > 0
            && stream->segment_index >= segments_count)) {
      return FALSE;
    }
    fragment->duration = duration;
  }

  /* FIXME rework discont checking */
  /* fragment->discontinuity = segment_idx != currentChunk.number; */
  fragment->range_start = 0;
  fragment->range_end = -1;
  fragment->index_uri = NULL;
  fragment->index_range_start = 0;
  fragment->index_range_end = -1;

  if (stream->segments) {
    currentChunk = g_ptr_array_index (stream->segments, stream->segment_index);

    GST_DEBUG ("currentChunk->SegmentURL = %p", currentChunk->SegmentURL);
    if (currentChunk->SegmentURL != NULL) {
      mediaURL =
          g_strdup (gst_mpdparser_get_mediaURL (stream,
              currentChunk->SegmentURL));
      indexURL = g_strdup (currentChunk->SegmentURL->index);
    } else if (stream->cur_seg_template != NULL) {
      mediaURL =
          gst_mpdparser_build_URL_from_template (stream->
          cur_seg_template->media, stream->cur_representation->id,
          currentChunk->number + stream->segment_repeat_index,
          stream->cur_representation->bandwidth,
          currentChunk->scale_start +
          stream->segment_repeat_index * currentChunk->scale_duration);
      if (stream->cur_seg_template->index) {
        indexURL =
            gst_mpdparser_build_URL_from_template (stream->
            cur_seg_template->index, stream->cur_representation->id,
            currentChunk->number + stream->segment_repeat_index,
            stream->cur_representation->bandwidth,
            currentChunk->scale_start +
            stream->segment_repeat_index * currentChunk->scale_duration);
      }
    }
    GST_DEBUG ("mediaURL = %s", mediaURL);
    GST_DEBUG ("indexURL = %s", indexURL);

    fragment->timestamp =
        currentChunk->start +
        stream->segment_repeat_index * currentChunk->duration;
    fragment->duration = currentChunk->duration;
    if (currentChunk->SegmentURL) {
      if (currentChunk->SegmentURL->mediaRange) {
        fragment->range_start =
            currentChunk->SegmentURL->mediaRange->first_byte_pos;
        fragment->range_end =
            currentChunk->SegmentURL->mediaRange->last_byte_pos;
      }
      if (currentChunk->SegmentURL->indexRange) {
        fragment->index_range_start =
            currentChunk->SegmentURL->indexRange->first_byte_pos;
        fragment->index_range_end =
            currentChunk->SegmentURL->indexRange->last_byte_pos;
      }
    }
  } else {
    if (stream->cur_seg_template != NULL) {
      mediaURL =
          gst_mpdparser_build_URL_from_template (stream->
          cur_seg_template->media, stream->cur_representation->id,
          stream->segment_index +
          stream->cur_seg_template->MultSegBaseType->startNumber,
          stream->cur_representation->bandwidth,
          stream->segment_index * fragment->duration);
      if (stream->cur_seg_template->index) {
        indexURL =
            gst_mpdparser_build_URL_from_template (stream->
            cur_seg_template->index, stream->cur_representation->id,
            stream->segment_index +
            stream->cur_seg_template->MultSegBaseType->startNumber,
            stream->cur_representation->bandwidth,
            stream->segment_index * fragment->duration);
      }
    } else {
      return FALSE;
    }

    GST_DEBUG ("mediaURL = %s", mediaURL);
    GST_DEBUG ("indexURL = %s", indexURL);

    fragment->timestamp = stream->segment_index * fragment->duration;
  }

  base_url = gst_uri_from_string (stream->baseURL);
  frag_url = gst_uri_from_string_with_base (base_url, mediaURL);
  g_free (mediaURL);
  if (stream->queryURL) {
    frag_url = gst_uri_make_writable (frag_url);
    gst_uri_set_query_string (frag_url, stream->queryURL);
  }
  fragment->uri = gst_uri_to_string (frag_url);
  gst_uri_unref (frag_url);

  if (indexURL != NULL) {
    frag_url = gst_uri_make_writable (gst_uri_from_string_with_base (base_url,
            indexURL));
    gst_uri_set_query_string (frag_url, stream->queryURL);
    fragment->index_uri = gst_uri_to_string (frag_url);
    gst_uri_unref (frag_url);
    g_free (indexURL);
  } else if (indexURL == NULL && (fragment->index_range_start
          || fragment->index_range_end != -1)) {
    /* index has no specific URL but has a range, we should only use this if
     * the media also has a range, otherwise we are serving some data twice
     * (in the media fragment and again in the index) */
    if (!(fragment->range_start || fragment->range_end != -1)) {
      GST_WARNING ("Ignoring index ranges because there isn't a media range "
          "and URIs would be the same");
      /* removing index information */
      fragment->index_range_start = 0;
      fragment->index_range_end = -1;
    }
  }

  gst_uri_unref (base_url);

  GST_DEBUG ("Loading chunk with URL %s", fragment->uri);

  return TRUE;
}

gboolean
gst_mpd_client_has_next_segment (GstMpdClient * client,
    GstActiveStream * stream, gboolean forward)
{
  if (forward) {
    guint segments_count = gst_mpd_client_get_segments_counts (client, stream);

    if (segments_count > 0 && stream->segments
        && stream->segment_index + 1 == segments_count) {
      GstMediaSegment *segment;

      segment = g_ptr_array_index (stream->segments, stream->segment_index);
      if (segment->repeat >= 0
          && stream->segment_repeat_index >= segment->repeat)
        return FALSE;
    } else if (segments_count > 0
        && stream->segment_index + 1 >= segments_count) {
      return FALSE;
    }
  } else {
    if (stream->segment_index < 0)
      return FALSE;
  }

  return TRUE;
}

GstFlowReturn
gst_mpd_client_advance_segment (GstMpdClient * client, GstActiveStream * stream,
    gboolean forward)
{
  GstMediaSegment *segment;
  GstFlowReturn ret = GST_FLOW_OK;
  guint segments_count = gst_mpd_client_get_segments_counts (client, stream);

  GST_DEBUG ("Advancing segment. Current: %d / %d r:%d", stream->segment_index,
      segments_count, stream->segment_repeat_index);

  /* handle special cases first */
  if (forward) {
    if (segments_count > 0 && stream->segment_index >= segments_count) {
      ret = GST_FLOW_EOS;
      goto done;
    }

    if (stream->segments == NULL) {
      if (stream->segment_index < 0) {
        stream->segment_index = 0;
      } else {
        stream->segment_index++;
        if (segments_count > 0 && stream->segment_index >= segments_count) {
          ret = GST_FLOW_EOS;
        }
      }
      goto done;
    }

    /* special case for when playback direction is reverted right at *
     * the end of the segment list */
    if (stream->segment_index < 0) {
      stream->segment_index = 0;
      goto done;
    }
  } else {
    if (stream->segments == NULL)
      stream->segment_index--;
    if (stream->segment_index < 0) {
      stream->segment_index = -1;
      ret = GST_FLOW_EOS;
      goto done;
    }
    if (stream->segments == NULL)
      goto done;

    /* special case for when playback direction is reverted right at *
     * the end of the segment list */
    if (stream->segment_index >= segments_count) {
      stream->segment_index = segments_count - 1;
      segment = g_ptr_array_index (stream->segments, stream->segment_index);
      if (segment->repeat >= 0) {
        stream->segment_repeat_index = segment->repeat;
      } else {
        GstClockTime start = segment->start;
        GstClockTime end =
            gst_mpdparser_get_segment_end_time (client, stream->segments,
            segment,
            stream->segment_index);
        stream->segment_repeat_index =
            (guint) (end - start) / segment->duration;
      }
      goto done;
    }
  }

  /* for the normal cases we can get the segment safely here */
  segment = g_ptr_array_index (stream->segments, stream->segment_index);
  if (forward) {
    if (segment->repeat >= 0 && stream->segment_repeat_index >= segment->repeat) {
      stream->segment_repeat_index = 0;
      stream->segment_index++;
      if (segments_count > 0 && stream->segment_index >= segments_count) {
        ret = GST_FLOW_EOS;
        goto done;
      }
    } else {
      stream->segment_repeat_index++;
    }
  } else {
    if (stream->segment_repeat_index == 0) {
      stream->segment_index--;
      if (stream->segment_index < 0) {
        ret = GST_FLOW_EOS;
        goto done;
      }

      segment = g_ptr_array_index (stream->segments, stream->segment_index);
      /* negative repeats only seem to make sense at the end of a list,
       * so this one will probably not be. Needs some sanity checking
       * when loading the XML data. */
      if (segment->repeat >= 0) {
        stream->segment_repeat_index = segment->repeat;
      } else {
        GstClockTime start = segment->start;
        GstClockTime end =
            gst_mpdparser_get_segment_end_time (client, stream->segments,
            segment,
            stream->segment_index);
        stream->segment_repeat_index =
            (guint) (end - start) / segment->duration;
      }
    } else {
      stream->segment_repeat_index--;
    }
  }

done:
  GST_DEBUG ("Advanced to segment: %d / %d r:%d (ret: %s)",
      stream->segment_index, segments_count,
      stream->segment_repeat_index, gst_flow_get_name (ret));
  return ret;
}

gboolean
gst_mpd_client_get_next_header (GstMpdClient * client, gchar ** uri,
    guint stream_idx, gint64 * range_start, gint64 * range_end)
{
  GstActiveStream *stream;
  GstStreamPeriod *stream_period;

  stream = gst_mpdparser_get_active_stream_by_index (client, stream_idx);
  g_return_val_if_fail (stream != NULL, FALSE);
  g_return_val_if_fail (stream->cur_representation != NULL, FALSE);
  stream_period = gst_mpdparser_get_stream_period (client);
  g_return_val_if_fail (stream_period != NULL, FALSE);
  g_return_val_if_fail (stream_period->period != NULL, FALSE);

  *range_start = 0;
  *range_end = -1;

  GST_DEBUG ("Looking for current representation header");
  *uri = NULL;
  if (stream->cur_segment_base) {
    if (stream->cur_segment_base->Initialization) {
      *uri =
          g_strdup (gst_mpdparser_get_initializationURL (stream,
              stream->cur_segment_base->Initialization));
      if (stream->cur_segment_base->Initialization->range) {
        *range_start =
            stream->cur_segment_base->Initialization->range->first_byte_pos;
        *range_end =
            stream->cur_segment_base->Initialization->range->last_byte_pos;
      }
    } else if (stream->cur_segment_base->indexRange) {
      *uri =
          g_strdup (gst_mpdparser_get_initializationURL (stream,
              stream->cur_segment_base->Initialization));
      *range_start = 0;
      *range_end = stream->cur_segment_base->indexRange->first_byte_pos - 1;
    }
  } else if (stream->cur_seg_template
      && stream->cur_seg_template->initialization) {
    *uri =
        gst_mpdparser_build_URL_from_template (stream->
        cur_seg_template->initialization, stream->cur_representation->id, 0,
        stream->cur_representation->bandwidth, 0);
  }

  return *uri == NULL ? FALSE : TRUE;
}

gboolean
gst_mpd_client_get_next_header_index (GstMpdClient * client, gchar ** uri,
    guint stream_idx, gint64 * range_start, gint64 * range_end)
{
  GstActiveStream *stream;
  GstStreamPeriod *stream_period;

  stream = gst_mpdparser_get_active_stream_by_index (client, stream_idx);
  g_return_val_if_fail (stream != NULL, FALSE);
  g_return_val_if_fail (stream->cur_representation != NULL, FALSE);
  stream_period = gst_mpdparser_get_stream_period (client);
  g_return_val_if_fail (stream_period != NULL, FALSE);
  g_return_val_if_fail (stream_period->period != NULL, FALSE);

  *range_start = 0;
  *range_end = -1;

  GST_DEBUG ("Looking for current representation index");
  *uri = NULL;
  if (stream->cur_segment_base && stream->cur_segment_base->indexRange) {
    *uri =
        g_strdup (gst_mpdparser_get_initializationURL (stream,
            stream->cur_segment_base->RepresentationIndex));
    *range_start = stream->cur_segment_base->indexRange->first_byte_pos;
    *range_end = stream->cur_segment_base->indexRange->last_byte_pos;
  } else if (stream->cur_seg_template && stream->cur_seg_template->index) {
    *uri =
        gst_mpdparser_build_URL_from_template (stream->cur_seg_template->index,
        stream->cur_representation->id, 0,
        stream->cur_representation->bandwidth, 0);
  }

  return *uri == NULL ? FALSE : TRUE;
}

GstClockTime
gst_mpd_client_get_next_fragment_duration (GstMpdClient * client,
    GstActiveStream * stream)
{
  GstMediaSegment *media_segment = NULL;
  gint seg_idx;

  g_return_val_if_fail (stream != NULL, 0);

  seg_idx = stream->segment_index;

  if (stream->segments) {
    if (seg_idx < stream->segments->len && seg_idx >= 0)
      media_segment = g_ptr_array_index (stream->segments, seg_idx);

    return media_segment == NULL ? 0 : media_segment->duration;
  } else {
    GstClockTime duration =
        gst_mpd_client_get_segment_duration (client, stream, NULL);
    guint segments_count = gst_mpd_client_get_segments_counts (client, stream);

    g_return_val_if_fail (stream->cur_seg_template->MultSegBaseType->
        SegmentTimeline == NULL, 0);

    if (!GST_CLOCK_TIME_IS_VALID (duration) || (segments_count > 0
            && seg_idx >= segments_count)) {
      return 0;
    }
    return duration;
  }
}

GstClockTime
gst_mpd_client_get_media_presentation_duration (GstMpdClient * client)
{
  GstClockTime duration;

  g_return_val_if_fail (client != NULL, GST_CLOCK_TIME_NONE);

  if (client->mpd_node->mediaPresentationDuration != -1) {
    duration = client->mpd_node->mediaPresentationDuration * GST_MSECOND;
  } else {
    /* We can only get the duration for on-demand streams */
    duration = GST_CLOCK_TIME_NONE;
  }

  return duration;
}

gboolean
gst_mpd_client_set_period_id (GstMpdClient * client, const gchar * period_id)
{
  GstStreamPeriod *next_stream_period;
  gboolean ret = FALSE;
  GList *iter;
  guint period_idx;

  g_return_val_if_fail (client != NULL, FALSE);
  g_return_val_if_fail (client->periods != NULL, FALSE);
  g_return_val_if_fail (period_id != NULL, FALSE);

  if (!gst_mpd_client_setup_media_presentation (client, GST_CLOCK_TIME_NONE, -1,
          period_id))
    return FALSE;

  for (period_idx = 0, iter = client->periods; iter;
      period_idx++, iter = g_list_next (iter)) {
    next_stream_period = iter->data;

    if (next_stream_period->period->id
        && strcmp (next_stream_period->period->id, period_id) == 0) {
      ret = TRUE;
      client->period_idx = period_idx;
      break;
    }
  }

  return ret;
}

gboolean
gst_mpd_client_set_period_index (GstMpdClient * client, guint period_idx)
{
  GstStreamPeriod *next_stream_period;
  gboolean ret = FALSE;

  g_return_val_if_fail (client != NULL, FALSE);
  g_return_val_if_fail (client->periods != NULL, FALSE);

  if (!gst_mpd_client_setup_media_presentation (client, -1, period_idx, NULL))
    return FALSE;

  next_stream_period = g_list_nth_data (client->periods, period_idx);
  if (next_stream_period != NULL) {
    client->period_idx = period_idx;
    ret = TRUE;
  }

  return ret;
}

guint
gst_mpd_client_get_period_index (GstMpdClient * client)
{
  guint period_idx;

  g_return_val_if_fail (client != NULL, 0);
  period_idx = client->period_idx;

  return period_idx;
}

const gchar *
gst_mpd_client_get_period_id (GstMpdClient * client)
{
  GstStreamPeriod *period;
  gchar *period_id = NULL;

  g_return_val_if_fail (client != NULL, 0);
  period = g_list_nth_data (client->periods, client->period_idx);
  if (period && period->period)
    period_id = period->period->id;

  return period_id;
}

gboolean
gst_mpd_client_has_previous_period (GstMpdClient * client)
{
  GList *next_stream_period;
  g_return_val_if_fail (client != NULL, FALSE);
  g_return_val_if_fail (client->periods != NULL, FALSE);

  if (!gst_mpd_client_setup_media_presentation (client, GST_CLOCK_TIME_NONE,
          client->period_idx - 1, NULL))
    return FALSE;

  next_stream_period =
      g_list_nth_data (client->periods, client->period_idx - 1);

  return next_stream_period != NULL;
}

gboolean
gst_mpd_client_has_next_period (GstMpdClient * client)
{
  GList *next_stream_period;
  g_return_val_if_fail (client != NULL, FALSE);
  g_return_val_if_fail (client->periods != NULL, FALSE);

  if (!gst_mpd_client_setup_media_presentation (client, GST_CLOCK_TIME_NONE,
          client->period_idx + 1, NULL))
    return FALSE;

  next_stream_period =
      g_list_nth_data (client->periods, client->period_idx + 1);
  return next_stream_period != NULL;
}

void
gst_mpd_client_seek_to_first_segment (GstMpdClient * client)
{
  GList *list;

  g_return_if_fail (client != NULL);
  g_return_if_fail (client->active_streams != NULL);

  for (list = g_list_first (client->active_streams); list;
      list = g_list_next (list)) {
    GstActiveStream *stream = (GstActiveStream *) list->data;
    if (stream) {
      stream->segment_index = 0;
      stream->segment_repeat_index = 0;
    }
  }
}

static guint
gst_mpd_client_get_segments_counts (GstMpdClient * client,
    GstActiveStream * stream)
{
  GstStreamPeriod *stream_period;

  g_return_val_if_fail (stream != NULL, 0);

  if (stream->segments)
    return stream->segments->len;
  g_return_val_if_fail (stream->cur_seg_template->MultSegBaseType->
      SegmentTimeline == NULL, 0);

  stream_period = gst_mpdparser_get_stream_period (client);
  if (stream_period->duration != -1)
    return gst_util_uint64_scale_ceil (stream_period->duration, 1,
        gst_mpd_client_get_segment_duration (client, stream, NULL));

  return 0;
}

gboolean
gst_mpd_client_is_live (GstMpdClient * client)
{
  g_return_val_if_fail (client != NULL, FALSE);
  g_return_val_if_fail (client->mpd_node != NULL, FALSE);

  return client->mpd_node->type == GST_MPD_FILE_TYPE_DYNAMIC;
}

guint
gst_mpdparser_get_nb_active_stream (GstMpdClient * client)
{
  g_return_val_if_fail (client != NULL, 0);

  return g_list_length (client->active_streams);
}

guint
gst_mpdparser_get_nb_adaptationSet (GstMpdClient * client)
{
  GstStreamPeriod *stream_period;

  stream_period = gst_mpdparser_get_stream_period (client);
  g_return_val_if_fail (stream_period != NULL, 0);
  g_return_val_if_fail (stream_period->period != NULL, 0);

  return g_list_length (stream_period->period->AdaptationSets);
}

GstActiveStream *
gst_mpdparser_get_active_stream_by_index (GstMpdClient * client,
    guint stream_idx)
{
  g_return_val_if_fail (client != NULL, NULL);
  g_return_val_if_fail (client->active_streams != NULL, NULL);

  return g_list_nth_data (client->active_streams, stream_idx);
}

gboolean
gst_mpd_client_active_stream_contains_subtitles (GstActiveStream * stream)
{
  const gchar *mimeType;
  const gchar *adapt_set_codecs;
  const gchar *rep_codecs;

  mimeType = stream->cur_representation->RepresentationBase->mimeType;
  if (!mimeType)
    mimeType = stream->cur_adapt_set->RepresentationBase->mimeType;

  if (g_strcmp0 (mimeType, "application/ttml+xml") == 0 ||
      g_strcmp0 (mimeType, "text/vtt") == 0)
    return TRUE;

  adapt_set_codecs = stream->cur_adapt_set->RepresentationBase->codecs;
  rep_codecs = stream->cur_representation->RepresentationBase->codecs;

  return (adapt_set_codecs && g_str_has_prefix (adapt_set_codecs, "stpp"))
      || (rep_codecs && g_str_has_prefix (rep_codecs, "stpp"));
}

static const gchar *
gst_mpdparser_mimetype_to_caps (const gchar * mimeType)
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

GstCaps *
gst_mpd_client_get_stream_caps (GstActiveStream * stream)
{
  const gchar *mimeType, *caps_string;
  GstCaps *ret = NULL;

  if (stream == NULL || stream->cur_adapt_set == NULL
      || stream->cur_representation == NULL)
    return NULL;

  mimeType = stream->cur_representation->RepresentationBase->mimeType;
  if (mimeType == NULL) {
    mimeType = stream->cur_adapt_set->RepresentationBase->mimeType;
  }

  caps_string = gst_mpdparser_mimetype_to_caps (mimeType);

  if ((g_strcmp0 (caps_string, "application/mp4") == 0)
      && gst_mpd_client_active_stream_contains_subtitles (stream))
    caps_string = "video/quicktime";

  if (caps_string)
    ret = gst_caps_from_string (caps_string);

  return ret;
}

gboolean
gst_mpd_client_get_bitstream_switching_flag (GstActiveStream * stream)
{
  if (stream == NULL || stream->cur_adapt_set == NULL)
    return FALSE;

  return stream->cur_adapt_set->bitstreamSwitching;
}

guint
gst_mpd_client_get_video_stream_width (GstActiveStream * stream)
{
  guint width;

  if (stream == NULL || stream->cur_adapt_set == NULL
      || stream->cur_representation == NULL)
    return 0;

  width = stream->cur_representation->RepresentationBase->width;
  if (width == 0) {
    width = stream->cur_adapt_set->RepresentationBase->width;
  }

  return width;
}

guint
gst_mpd_client_get_video_stream_height (GstActiveStream * stream)
{
  guint height;

  if (stream == NULL || stream->cur_adapt_set == NULL
      || stream->cur_representation == NULL)
    return 0;

  height = stream->cur_representation->RepresentationBase->height;
  if (height == 0) {
    height = stream->cur_adapt_set->RepresentationBase->height;
  }

  return height;
}

gboolean
gst_mpd_client_get_video_stream_framerate (GstActiveStream * stream,
    gint * fps_num, gint * fps_den)
{
  if (stream == NULL)
    return FALSE;

  if (stream->cur_adapt_set &&
      stream->cur_adapt_set->RepresentationBase->frameRate != NULL) {
    *fps_num = stream->cur_adapt_set->RepresentationBase->frameRate->num;
    *fps_den = stream->cur_adapt_set->RepresentationBase->frameRate->den;
    return TRUE;
  }

  if (stream->cur_adapt_set &&
      stream->cur_adapt_set->RepresentationBase->maxFrameRate != NULL) {
    *fps_num = stream->cur_adapt_set->RepresentationBase->maxFrameRate->num;
    *fps_den = stream->cur_adapt_set->RepresentationBase->maxFrameRate->den;
    return TRUE;
  }

  if (stream->cur_representation &&
      stream->cur_representation->RepresentationBase->frameRate != NULL) {
    *fps_num = stream->cur_representation->RepresentationBase->frameRate->num;
    *fps_den = stream->cur_representation->RepresentationBase->frameRate->den;
    return TRUE;
  }

  if (stream->cur_representation &&
      stream->cur_representation->RepresentationBase->maxFrameRate != NULL) {
    *fps_num =
        stream->cur_representation->RepresentationBase->maxFrameRate->num;
    *fps_den =
        stream->cur_representation->RepresentationBase->maxFrameRate->den;
    return TRUE;
  }

  return FALSE;
}

guint
gst_mpd_client_get_audio_stream_rate (GstActiveStream * stream)
{
  const gchar *rate;

  if (stream == NULL || stream->cur_adapt_set == NULL
      || stream->cur_representation == NULL)
    return 0;

  rate = stream->cur_representation->RepresentationBase->audioSamplingRate;
  if (rate == NULL) {
    rate = stream->cur_adapt_set->RepresentationBase->audioSamplingRate;
  }

  return rate ? atoi (rate) : 0;
}

guint
gst_mpd_client_get_audio_stream_num_channels (GstActiveStream * stream)
{
  if (stream == NULL || stream->cur_adapt_set == NULL
      || stream->cur_representation == NULL)
    return 0;
  /* TODO: here we have to parse the AudioChannelConfiguration descriptors */
  return 0;
}

guint
gst_mpdparser_get_list_and_nb_of_audio_language (GstMpdClient * client,
    GList ** lang)
{
  GstStreamPeriod *stream_period;
  GstAdaptationSetNode *adapt_set;
  GList *adaptation_sets, *list;
  const gchar *this_mimeType = "audio";
  gchar *mimeType = NULL;
  guint nb_adaptation_set = 0;

  stream_period = gst_mpdparser_get_stream_period (client);
  g_return_val_if_fail (stream_period != NULL, 0);
  g_return_val_if_fail (stream_period->period != NULL, 0);

  adaptation_sets =
      gst_mpd_client_get_adaptation_sets_for_period (client, stream_period);
  for (list = adaptation_sets; list; list = g_list_next (list)) {
    adapt_set = (GstAdaptationSetNode *) list->data;
    if (adapt_set && adapt_set->lang) {
      gchar *this_lang = adapt_set->lang;
      GstRepresentationNode *rep;
      rep =
          gst_mpdparser_get_lowest_representation (adapt_set->Representations);
      mimeType = NULL;
      if (rep->RepresentationBase)
        mimeType = rep->RepresentationBase->mimeType;
      if (!mimeType && adapt_set->RepresentationBase) {
        mimeType = adapt_set->RepresentationBase->mimeType;
      }

      if (strncmp_ext (mimeType, this_mimeType) == 0) {
        nb_adaptation_set++;
        *lang = g_list_append (*lang, this_lang);
      }
    }
  }

  return nb_adaptation_set;
}


GstDateTime *
gst_mpd_client_get_next_segment_availability_start_time (GstMpdClient * client,
    GstActiveStream * stream)
{
  GstDateTime *availability_start_time, *rv;
  gint seg_idx;
  GstStreamPeriod *stream_period;
  GstMediaSegment *segment;
  GstClockTime segmentEndTime;

  g_return_val_if_fail (client != NULL, NULL);
  g_return_val_if_fail (stream != NULL, NULL);

  stream_period = gst_mpdparser_get_stream_period (client);

  seg_idx = stream->segment_index;

  if (stream->segments) {
    segment = g_ptr_array_index (stream->segments, seg_idx);

    if (segment->repeat >= 0) {
      segmentEndTime = segment->start + (stream->segment_repeat_index + 1) *
          segment->duration;
    } else if (seg_idx < stream->segments->len - 1) {
      const GstMediaSegment *next_segment =
          g_ptr_array_index (stream->segments, seg_idx + 1);
      segmentEndTime = next_segment->start;
    } else {
      const GstStreamPeriod *stream_period;
      stream_period = gst_mpdparser_get_stream_period (client);
      segmentEndTime = stream_period->start + stream_period->duration;
    }
  } else {
    GstClockTime seg_duration;
    seg_duration = gst_mpd_client_get_segment_duration (client, stream, NULL);
    if (seg_duration == 0)
      return NULL;
    segmentEndTime = (1 + seg_idx) * seg_duration;
  }

  availability_start_time = gst_mpd_client_get_availability_start_time (client);
  if (availability_start_time == NULL) {
    GST_WARNING_OBJECT (client, "Failed to get availability_start_time");
    return NULL;
  }

  if (stream_period && stream_period->period) {
    GstDateTime *t =
        gst_mpd_client_add_time_difference (availability_start_time,
        stream_period->start / GST_USECOND);
    gst_date_time_unref (availability_start_time);
    availability_start_time = t;

    if (availability_start_time == NULL) {
      GST_WARNING_OBJECT (client, "Failed to offset availability_start_time");
      return NULL;
    }
  }

  rv = gst_mpd_client_add_time_difference (availability_start_time,
      segmentEndTime / GST_USECOND);
  gst_date_time_unref (availability_start_time);
  if (rv == NULL) {
    GST_WARNING_OBJECT (client, "Failed to offset availability_start_time");
    return NULL;
  }

  return rv;
}

gboolean
gst_mpd_client_seek_to_time (GstMpdClient * client, GDateTime * time)
{
  GDateTime *start;
  GTimeSpan ts_microseconds;
  GstClockTime ts;
  gboolean ret = TRUE;
  GList *stream;

  g_return_val_if_fail (gst_mpd_client_is_live (client), FALSE);
  g_return_val_if_fail (client->mpd_node->availabilityStartTime != NULL, FALSE);

  start =
      gst_date_time_to_g_date_time (client->mpd_node->availabilityStartTime);

  ts_microseconds = g_date_time_difference (time, start);
  g_date_time_unref (start);

  /* Clamp to availability start time, otherwise calculations wrap around */
  if (ts_microseconds < 0)
    ts_microseconds = 0;

  ts = ts_microseconds * GST_USECOND;
  for (stream = client->active_streams; stream; stream = g_list_next (stream)) {
    ret =
        ret & gst_mpd_client_stream_seek (client, stream->data, TRUE, 0, ts,
        NULL);
  }
  return ret;
}

void
gst_media_fragment_info_clear (GstMediaFragmentInfo * fragment)
{
  g_free (fragment->uri);
  g_free (fragment->index_uri);
}

gboolean
gst_mpd_client_has_isoff_ondemand_profile (GstMpdClient * client)
{
  return client->profile_isoff_ondemand;
}

/**
 * gst_mpd_client_parse_default_presentation_delay:
 * @client: #GstMpdClient that has a parsed manifest
 * @default_presentation_delay: A string that specifies a time period
 * in fragments (e.g. "5 f"), seconds ("12 s") or milliseconds
 * ("12000 ms")
 * Returns: the parsed string in milliseconds
 *
 * Since: 1.6
 */
gint64
gst_mpd_client_parse_default_presentation_delay (GstMpdClient * client,
    const gchar * default_presentation_delay)
{
  gint64 value;
  char *endptr = NULL;

  g_return_val_if_fail (client != NULL, 0);
  g_return_val_if_fail (default_presentation_delay != NULL, 0);
  value = strtol (default_presentation_delay, &endptr, 10);
  if (endptr == default_presentation_delay || value == 0) {
    return 0;
  }
  while (*endptr == ' ')
    endptr++;
  if (*endptr == 's' || *endptr == 'S') {
    value *= 1000;              /* convert to ms */
  } else if (*endptr == 'f' || *endptr == 'F') {
    gint64 segment_duration;
    g_assert (client->mpd_node != NULL);
    segment_duration = client->mpd_node->maxSegmentDuration;
    value *= segment_duration;
  } else if (*endptr != 'm' && *endptr != 'M') {
    GST_ERROR ("Unable to parse default presentation delay: %s",
        default_presentation_delay);
    value = 0;
  }
  return value;
}

GstClockTime
gst_mpd_client_get_maximum_segment_duration (GstMpdClient * client)
{
  GstClockTime ret = GST_CLOCK_TIME_NONE, dur;
  GList *stream;

  g_return_val_if_fail (client != NULL, GST_CLOCK_TIME_NONE);
  g_return_val_if_fail (client->mpd_node != NULL, GST_CLOCK_TIME_NONE);

  if (client->mpd_node->maxSegmentDuration != GST_MPD_DURATION_NONE) {
    return client->mpd_node->maxSegmentDuration * GST_MSECOND;
  }

  /* According to the DASH specification, if maxSegmentDuration is not present:
     "If not present, then the maximum Segment duration shall be the maximum
     duration of any Segment documented in this MPD"
   */
  for (stream = client->active_streams; stream; stream = g_list_next (stream)) {
    dur = gst_mpd_client_get_segment_duration (client, stream->data, NULL);
    if (dur != GST_CLOCK_TIME_NONE && (dur > ret || ret == GST_CLOCK_TIME_NONE)) {
      ret = dur;
    }
  }
  return ret;
}
