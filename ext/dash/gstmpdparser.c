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
static gboolean gst_mpdparser_get_xml_prop_string (xmlNode * a_node,
    const gchar * property_name, gchar ** property_value);
static gboolean gst_mpdparser_get_xml_prop_string_vector_type (xmlNode * a_node,
    const gchar * property_name, gchar *** property_value);
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
    const gchar * property_name, gint64 default_value, gint64 * property_value);
static gboolean gst_mpdparser_get_xml_node_content (xmlNode * a_node,
    gchar ** content);
static gchar *gst_mpdparser_get_xml_node_namespace (xmlNode * a_node,
    const gchar * prefix);

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
static void gst_mpdparser_parse_mult_seg_base_type_ext (GstMultSegmentBaseType
    ** pointer, xmlNode * a_node, GstMultSegmentBaseType * parent);
static void gst_mpdparser_parse_segment_list_node (GstSegmentListNode **
    pointer, xmlNode * a_node, GstSegmentListNode * parent);
static void
gst_mpdparser_parse_representation_base_type (GstRepresentationBaseType **
    pointer, xmlNode * a_node);
static void gst_mpdparser_parse_representation_node (GList ** list,
    xmlNode * a_node, GstAdaptationSetNode * parent);
static void gst_mpdparser_parse_adaptation_set_node (GList ** list,
    xmlNode * a_node, GstPeriodNode * parent);
static void gst_mpdparser_parse_subset_node (GList ** list, xmlNode * a_node);
static void gst_mpdparser_parse_segment_template_node (GstSegmentTemplateNode **
    pointer, xmlNode * a_node, GstSegmentTemplateNode * parent);
static void gst_mpdparser_parse_period_node (GList ** list, xmlNode * a_node);
static void gst_mpdparser_parse_program_info_node (GList ** list,
    xmlNode * a_node);
static void gst_mpdparser_parse_metrics_range_node (GList ** list,
    xmlNode * a_node);
static void gst_mpdparser_parse_metrics_node (GList ** list, xmlNode * a_node);
static void gst_mpdparser_parse_root_node (GstMPDNode ** pointer,
    xmlNode * a_node);

/* Helper functions */
static gint convert_to_millisecs (gint decimals, gint pos);
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
    GstSegmentURLNode * url_node, guint number, guint64 start,
    GstClockTime start_time, GstClockTime duration);
static const gchar *gst_mpdparser_mimetype_to_caps (const gchar * mimeType);
static GstClockTime gst_mpd_client_get_segment_duration (GstMpdClient * client,
    GstActiveStream * stream);
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
static GstSegmentListNode *gst_mpdparser_get_segment_list (GstPeriodNode *
    Period, GstAdaptationSetNode * AdaptationSet,
    GstRepresentationNode * Representation);

/* Segments */
static guint gst_mpd_client_get_segments_counts (GstActiveStream * stream);

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
static void gst_mpdparser_free_stream_period (GstStreamPeriod * stream_period);
static void gst_mpdparser_free_media_segment (GstMediaSegment * media_segment);
static void gst_mpdparser_free_active_stream (GstActiveStream * active_stream);

/* functions to parse node namespaces, content and properties */
static gboolean
gst_mpdparser_get_xml_prop_string (xmlNode * a_node,
    const gchar * property_name, gchar ** property_value)
{
  xmlChar *prop_string;
  gboolean exists = FALSE;

  prop_string = xmlGetProp (a_node, (const xmlChar *) property_name);
  if (prop_string) {
    *property_value = (gchar *) prop_string;
    exists = TRUE;
    GST_LOG (" - %s: %s", property_name, prop_string);
  }

  return exists;
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
gst_mpdparser_get_xml_prop_unsigned_integer (xmlNode * a_node,
    const gchar * property_name, guint default_val, guint * property_value)
{
  xmlChar *prop_string;
  gboolean exists = FALSE;

  *property_value = default_val;
  prop_string = xmlGetProp (a_node, (const xmlChar *) property_name);
  if (prop_string) {
    if (sscanf ((gchar *) prop_string, "%u", property_value)) {
      exists = TRUE;
      GST_LOG (" - %s: %u", property_name, *property_value);
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
gst_mpdparser_get_xml_prop_unsigned_integer_64 (xmlNode * a_node,
    const gchar * property_name, guint64 default_val, guint64 * property_value)
{
  xmlChar *prop_string;
  gboolean exists = FALSE;

  *property_value = default_val;
  prop_string = xmlGetProp (a_node, (const xmlChar *) property_name);
  if (prop_string) {
    if (sscanf ((gchar *) prop_string, "%" G_GUINT64_FORMAT, property_value)) {
      exists = TRUE;
      GST_LOG (" - %s: %" G_GUINT64_FORMAT, property_name, *property_value);
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
          if (sscanf ((gchar *) str_vector[i], "%u", &prop_uint_vector[i])) {
            GST_LOG ("    %u", prop_uint_vector[i]);
          } else {
            GST_WARNING
                ("failed to parse uint vector type property %s from xml string %s",
                property_name, str_vector[i]);
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
    if (sscanf ((gchar *) prop_string, "%lf", property_value)) {
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
    if (sscanf ((gchar *) prop_string, "%u", &prop_SAP_type)
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
  guint64 first_byte_pos = 0, last_byte_pos = 0;
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
      if (sscanf (str, "%" G_GUINT64_FORMAT, &first_byte_pos) != 1) {
        goto error;
      }
    }
    /* read last_byte_pos */
    if (pos < (len - 1)) {
      if (sscanf (str + pos + 1, "%" G_GUINT64_FORMAT, &last_byte_pos) != 1) {
        goto error;
      }
    }
    /* malloc return data structure */
    *property_value = g_slice_new0 (GstRange);
    if (*property_value == NULL) {
      GST_WARNING ("Allocation of GstRange failed!");
      goto error;
    }
    exists = TRUE;
    (*property_value)->first_byte_pos = first_byte_pos;
    (*property_value)->last_byte_pos = last_byte_pos;
    xmlFree (prop_string);
    GST_LOG (" - %s: %" G_GUINT64_FORMAT "-%" G_GUINT64_FORMAT,
        property_name, first_byte_pos, last_byte_pos);
  }

  return exists;

error:
  xmlFree (prop_string);
  GST_WARNING ("failed to parse property %s from xml string %s", property_name,
      prop_string);
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
    if (*property_value == NULL) {
      GST_WARNING ("Allocation of GstRatio failed!");
      goto error;
    }
    exists = TRUE;
    (*property_value)->num = num;
    (*property_value)->den = den;
    xmlFree (prop_string);
    GST_LOG (" - %s: %u:%u", property_name, num, den);
  }

  return exists;

error:
  xmlFree (prop_string);
  GST_WARNING ("failed to parse property %s from xml string %s", property_name,
      prop_string);
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
    if (*property_value == NULL) {
      GST_WARNING ("Allocation of GstFrameRate failed!");
      goto error;
    }
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
  xmlFree (prop_string);
  GST_WARNING ("failed to parse property %s from xml string %s", property_name,
      prop_string);
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
      if (sscanf (str, "%u", &val) != 1)
        goto error;
    }

    /* alloc return data structure */
    *property_value = g_slice_new0 (GstConditionalUintType);
    if (*property_value == NULL) {
      GST_WARNING ("Allocation of GstConditionalUintType failed!");
      goto error;
    }
    exists = TRUE;
    (*property_value)->flag = flag;
    (*property_value)->value = val;
    xmlFree (prop_string);
    GST_LOG (" - %s: flag=%s val=%u", property_name, flag ? "true" : "false",
        val);
  }

  return exists;

error:
  xmlFree (prop_string);
  GST_WARNING ("failed to parse property %s from xml string %s", property_name,
      prop_string);
  return FALSE;
}

/*
  DateTime Data Type

  The dateTime data type is used to specify a date and a time.

  The dateTime is specified in the following form "YYYY-MM-DDThh:mm:ss" where:

    * YYYY indicates the year
    * MM indicates the month
    * DD indicates the day
    * T indicates the start of the required time section
    * hh indicates the hour
    * mm indicates the minute
    * ss indicates the second

  Note: All components are required!
*/
static gboolean
gst_mpdparser_get_xml_prop_dateTime (xmlNode * a_node,
    const gchar * property_name, GstDateTime ** property_value)
{
  xmlChar *prop_string;
  gchar *str;
  gint ret, pos;
  gint year, month, day, hour, minute, second;
  gboolean exists = FALSE;

  prop_string = xmlGetProp (a_node, (const xmlChar *) property_name);
  if (prop_string) {
    str = (gchar *) prop_string;
    GST_TRACE ("dateTime: %s, len %d", str, xmlStrlen (prop_string));
    /* parse year */
    ret = sscanf (str, "%d", &year);
    if (ret != 1)
      goto error;
    pos = strcspn (str, "-");
    str += (pos + 1);
    GST_TRACE (" - year %d", year);
    /* parse month */
    ret = sscanf (str, "%d", &month);
    if (ret != 1)
      goto error;
    pos = strcspn (str, "-");
    str += (pos + 1);
    GST_TRACE (" - month %d", month);
    /* parse day */
    ret = sscanf (str, "%d", &day);
    if (ret != 1)
      goto error;
    pos = strcspn (str, "T");
    str += (pos + 1);
    GST_TRACE (" - day %d", day);
    /* parse hour */
    ret = sscanf (str, "%d", &hour);
    if (ret != 1)
      goto error;
    pos = strcspn (str, ":");
    str += (pos + 1);
    GST_TRACE (" - hour %d", hour);
    /* parse minute */
    ret = sscanf (str, "%d", &minute);
    if (ret != 1)
      goto error;
    pos = strcspn (str, ":");
    str += (pos + 1);
    GST_TRACE (" - minute %d", minute);
    /* parse second */
    ret = sscanf (str, "%d", &second);
    if (ret != 1)
      goto error;
    GST_TRACE (" - second %d", second);

    GST_LOG (" - %s: %4d/%02d/%02d %02d:%02d:%02d", property_name,
        year, month, day, hour, minute, second);

    exists = TRUE;
    *property_value =
        gst_date_time_new (0, year, month, day, hour, minute, second);
    xmlFree (prop_string);
  }

  return exists;

error:
  xmlFree (prop_string);
  GST_WARNING ("failed to parse property %s from xml string %s", property_name,
      prop_string);
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
static gint
convert_to_millisecs (gint decimals, gint pos)
{
  gint num = 1, den = 1, i = 3 - pos;

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
gst_mpdparser_get_xml_prop_duration (xmlNode * a_node,
    const gchar * property_name, gint64 default_value, gint64 * property_value)
{
  xmlChar *prop_string;
  gchar *str;
  gint ret, read, len, pos, posT;
  gint years = 0, months = 0, days = 0, hours = 0, minutes = 0, seconds =
      0, decimals = 0;
  gint sign = 1;
  gboolean have_ms = FALSE;
  gboolean exists = FALSE;

  prop_string = xmlGetProp (a_node, (const xmlChar *) property_name);
  if (prop_string) {
    len = xmlStrlen (prop_string);
    str = (gchar *) prop_string;
    GST_TRACE ("duration: %s, len %d", str, len);
    /* read "-" for sign, if present */
    pos = strcspn (str, "-");
    if (pos < len) {            /* found "-" */
      if (pos != 0) {
        GST_WARNING ("sign \"-\" non at the beginning of the string");
        goto error;
      }
      GST_TRACE ("found - sign at the beginning");
      sign = -1;
      str++;
      len--;
    }
    /* read "P" for period */
    pos = strcspn (str, "P");
    if (pos != 0) {
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
        ret = sscanf (str, "%d", &read);
        if (ret != 1) {
          GST_WARNING ("can not read integer value from string %s!", str);
          goto error;
        }
        switch (str[pos]) {
          case 'Y':
            years = read;
            break;
          case 'M':
            months = read;
            break;
          case 'D':
            days = read;
            break;
          default:
            GST_WARNING ("unexpected char %c!", str[pos]);
            goto error;
            break;
        }
        GST_TRACE ("read number %d type %c", read, str[pos]);
        str += (pos + 1);
        posT -= (pos + 1);
      } while (posT > 0);

      GST_TRACE ("Y:M:D=%d:%d:%d", years, months, days);
    }
    /* read "T" for time (if present) */
    /* here T is at pos == 0 */
    str++;
    len--;
    pos = 0;
    if (pos < len) {
      /* T found, there is a time section */
      /* read hours, minutes, seconds, cents of second */
      do {
        GST_TRACE ("parsing substring %s", str);
        pos = strcspn (str, "HMS,.");
        ret = sscanf (str, "%d", &read);
        if (ret != 1) {
          GST_WARNING ("can not read integer value from string %s!", str);
          goto error;
        }
        switch (str[pos]) {
          case 'H':
            hours = read;
            break;
          case 'M':
            minutes = read;
            break;
          case 'S':
            if (have_ms) {
              /* we have read the decimal part of the seconds */
              decimals = convert_to_millisecs (read, pos);
              GST_TRACE ("decimal number %d (%d digits) -> %d ms", read, pos,
                  decimals);
            } else {
              /* no decimals */
              seconds = read;
            }
            break;
          case '.':
          case ',':
            /* we have read the integer part of a decimal number in seconds */
            seconds = read;
            have_ms = TRUE;
            break;
          default:
            GST_WARNING ("unexpected char %c!", str[pos]);
            goto error;
            break;
        }
        GST_TRACE ("read number %d type %c", read, str[pos]);
        str += pos + 1;
        len -= (pos + 1);
      } while (len > 0);

      GST_TRACE ("H:M:S.MS=%d:%d:%d.%03d", hours, minutes, seconds, decimals);
    }

    xmlFree (prop_string);
    exists = TRUE;
    *property_value =
        sign * ((((((gint64) years * 365 + months * 30 + days) * 24 +
                    hours) * 60 + minutes) * 60 + seconds) * 1000 + decimals);
    GST_LOG (" - %s: %" G_GINT64_FORMAT, property_name, *property_value);
  }

  if (!exists) {
    *property_value = default_value;
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

static gchar *
gst_mpdparser_get_xml_node_namespace (xmlNode * a_node, const gchar * prefix)
{
  xmlNs *curr_ns;
  gchar *namespace = NULL;

  if (prefix == NULL) {
    /* return the default namespace */
    namespace = xmlMemStrdup ((const gchar *) a_node->ns->href);
    if (namespace) {
      GST_LOG (" - default namespace: %s", namespace);
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
  if (new_base_url == NULL) {
    GST_WARNING ("Allocation of BaseURL node failed!");
    return;
  }
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
  if (new_descriptor == NULL) {
    GST_WARNING ("Allocation of DescriptorType node failed!");
    return;
  }
  *list = g_list_append (*list, new_descriptor);

  GST_LOG ("attributes of %s node:", a_node->name);
  gst_mpdparser_get_xml_prop_string (a_node, "schemeIdUri",
      &new_descriptor->schemeIdUri);
  gst_mpdparser_get_xml_prop_string (a_node, "value", &new_descriptor->value);
}

static void
gst_mpdparser_parse_content_component_node (GList ** list, xmlNode * a_node)
{
  xmlNode *cur_node;
  GstContentComponentNode *new_content_component;

  new_content_component = g_slice_new0 (GstContentComponentNode);
  if (new_content_component == NULL) {
    GST_WARNING ("Allocation of ContentComponent node failed!");
    return;
  }
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
  if (new_subrep == NULL) {
    GST_WARNING ("Allocation of SubRepresentation node failed!");
    return;
  }
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
    if (clone) {
      clone->media = xmlMemStrdup (seg_url->media);
      clone->mediaRange = gst_mpdparser_clone_range (seg_url->mediaRange);
      clone->index = xmlMemStrdup (seg_url->index);
      clone->indexRange = gst_mpdparser_clone_range (seg_url->indexRange);
    } else {
      GST_WARNING ("Allocation of SegmentURL node failed!");
    }
  }

  return clone;
}

static void
gst_mpdparser_parse_segment_url_node (GList ** list, xmlNode * a_node)
{
  GstSegmentURLNode *new_segment_url;

  new_segment_url = g_slice_new0 (GstSegmentURLNode);
  if (new_segment_url == NULL) {
    GST_WARNING ("Allocation of SegmentURL node failed!");
    return;
  }
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
  if (new_url_type == NULL) {
    GST_WARNING ("Allocation of URLType node failed!");
    return;
  }

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
  gboolean boolval;
  GstRange *rangeval;

  gst_mpdparser_free_seg_base_type_ext (*pointer);
  *pointer = seg_base_type = g_slice_new0 (GstSegmentBaseType);
  if (seg_base_type == NULL) {
    GST_WARNING ("Allocation of SegmentBaseType node failed!");
    return;
  }

  /* Initialize values that have defaults */
  seg_base_type->indexRangeExact = FALSE;

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
  if (gst_mpdparser_get_xml_prop_unsigned_integer (a_node, "timescale", 0,
          &intval)) {
    seg_base_type->timescale = intval;
  }
  if (gst_mpdparser_get_xml_prop_unsigned_integer (a_node,
          "presentationTimeOffset", 0, &intval)) {
    seg_base_type->presentationTimeOffset = intval;
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
    if (clone) {
      clone->t = pointer->t;
      clone->d = pointer->d;
      clone->r = pointer->r;
    } else {
      GST_WARNING ("Allocation of S node failed!");
    }
  }

  return clone;
}

static void
gst_mpdparser_parse_s_node (GQueue * queue, xmlNode * a_node)
{
  GstSNode *new_s_node;

  new_s_node = g_slice_new0 (GstSNode);
  if (new_s_node == NULL) {
    GST_WARNING ("Allocation of S node failed!");
    return;
  }
  g_queue_push_tail (queue, new_s_node);

  GST_LOG ("attributes of S node:");
  gst_mpdparser_get_xml_prop_unsigned_integer_64 (a_node, "t", 0,
      &new_s_node->t);
  gst_mpdparser_get_xml_prop_unsigned_integer_64 (a_node, "d", 0,
      &new_s_node->d);
  gst_mpdparser_get_xml_prop_unsigned_integer (a_node, "r", 0, &new_s_node->r);
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

static void
gst_mpdparser_parse_mult_seg_base_type_ext (GstMultSegmentBaseType ** pointer,
    xmlNode * a_node, GstMultSegmentBaseType * parent)
{
  xmlNode *cur_node;
  GstMultSegmentBaseType *mult_seg_base_type;
  guint intval;

  gst_mpdparser_free_mult_seg_base_type_ext (*pointer);
  *pointer = mult_seg_base_type = g_slice_new0 (GstMultSegmentBaseType);
  if (mult_seg_base_type == NULL) {
    GST_WARNING ("Allocation of MultipleSegmentBaseType node failed!");
    return;
  }

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
}

static void
gst_mpdparser_parse_segment_list_node (GstSegmentListNode ** pointer,
    xmlNode * a_node, GstSegmentListNode * parent)
{
  xmlNode *cur_node;
  GstSegmentListNode *new_segment_list;

  gst_mpdparser_free_segment_list_node (*pointer);
  *pointer = new_segment_list = g_slice_new0 (GstSegmentListNode);
  if (new_segment_list == NULL) {
    GST_WARNING ("Allocation of SegmentList node failed!");
    return;
  }

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
    }
  }

  GST_LOG ("extension of SegmentList node:");
  gst_mpdparser_parse_mult_seg_base_type_ext
      (&new_segment_list->MultSegBaseType, a_node,
      (parent ? parent->MultSegBaseType : NULL));

  /* explore children nodes */
  for (cur_node = a_node->children; cur_node; cur_node = cur_node->next) {
    if (cur_node->type == XML_ELEMENT_NODE) {
      if (xmlStrcmp (cur_node->name, (xmlChar *) "SegmentURL") == 0) {
        gst_mpdparser_parse_segment_url_node (&new_segment_list->SegmentURL,
            cur_node);
      }
    }
  }
}

static void
gst_mpdparser_parse_representation_base_type (GstRepresentationBaseType **
    pointer, xmlNode * a_node)
{
  xmlNode *cur_node;
  GstRepresentationBaseType *representation_base;

  gst_mpdparser_free_representation_base_type (*pointer);
  *pointer = representation_base = g_slice_new0 (GstRepresentationBaseType);
  if (representation_base == NULL) {
    GST_WARNING ("Allocation of RepresentationBaseType node failed!");
    return;
  }

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
        gst_mpdparser_parse_descriptor_type_node
            (&representation_base->ContentProtection, cur_node);
      }
    }
  }
}

static void
gst_mpdparser_parse_representation_node (GList ** list, xmlNode * a_node,
    GstAdaptationSetNode * parent)
{
  xmlNode *cur_node;
  GstRepresentationNode *new_representation;

  new_representation = g_slice_new0 (GstRepresentationNode);
  if (new_representation == NULL) {
    GST_WARNING ("Allocation of Representation node failed!");
    return;
  }
  *list = g_list_append (*list, new_representation);

  GST_LOG ("attributes of Representation node:");
  gst_mpdparser_get_xml_prop_string (a_node, "id", &new_representation->id);
  gst_mpdparser_get_xml_prop_unsigned_integer (a_node, "bandwidth", 0,
      &new_representation->bandwidth);
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
            cur_node, parent->SegmentBase);
      } else if (xmlStrcmp (cur_node->name, (xmlChar *) "SegmentTemplate") == 0) {
        gst_mpdparser_parse_segment_template_node
            (&new_representation->SegmentTemplate, cur_node,
            parent->SegmentTemplate);
      } else if (xmlStrcmp (cur_node->name, (xmlChar *) "SegmentList") == 0) {
        gst_mpdparser_parse_segment_list_node (&new_representation->SegmentList,
            cur_node, parent->SegmentList);
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
}

static void
gst_mpdparser_parse_adaptation_set_node (GList ** list, xmlNode * a_node,
    GstPeriodNode * parent)
{
  xmlNode *cur_node;
  GstAdaptationSetNode *new_adap_set;

  new_adap_set = g_slice_new0 (GstAdaptationSetNode);
  if (new_adap_set == NULL) {
    GST_WARNING ("Allocation of AdaptationSet node failed!");
    return;
  }
  *list = g_list_append (*list, new_adap_set);

  GST_LOG ("attributes of AdaptationSet node:");
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
  gst_mpdparser_get_xml_prop_framerate (a_node, "minFrameRate",
      &new_adap_set->minFrameRate);
  gst_mpdparser_get_xml_prop_framerate (a_node, "maxFrameRate",
      &new_adap_set->maxFrameRate);
  gst_mpdparser_get_xml_prop_cond_uint (a_node, "segmentAlignment",
      &new_adap_set->segmentAlignment);
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
        gst_mpdparser_parse_segment_list_node (&new_adap_set->SegmentList,
            cur_node, parent->SegmentList);
      } else if (xmlStrcmp (cur_node->name,
              (xmlChar *) "ContentComponent") == 0) {
        gst_mpdparser_parse_content_component_node
            (&new_adap_set->ContentComponents, cur_node);
      } else if (xmlStrcmp (cur_node->name, (xmlChar *) "SegmentTemplate") == 0) {
        gst_mpdparser_parse_segment_template_node
            (&new_adap_set->SegmentTemplate, cur_node, parent->SegmentTemplate);
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
        gst_mpdparser_parse_representation_node (&new_adap_set->Representations,
            cur_node, new_adap_set);
      }
    }
  }
}

static void
gst_mpdparser_parse_subset_node (GList ** list, xmlNode * a_node)
{
  GstSubsetNode *new_subset;

  new_subset = g_slice_new0 (GstSubsetNode);
  if (new_subset == NULL) {
    GST_WARNING ("Allocation of Subset node failed!");
    return;
  }
  *list = g_list_append (*list, new_subset);

  GST_LOG ("attributes of Subset node:");
  gst_mpdparser_get_xml_prop_uint_vector_type (a_node, "contains",
      &new_subset->contains, &new_subset->size);
}

static void
gst_mpdparser_parse_segment_template_node (GstSegmentTemplateNode ** pointer,
    xmlNode * a_node, GstSegmentTemplateNode * parent)
{
  GstSegmentTemplateNode *new_segment_template;
  gchar *strval;

  gst_mpdparser_free_segment_template_node (*pointer);
  *pointer = new_segment_template = g_slice_new0 (GstSegmentTemplateNode);
  if (new_segment_template == NULL) {
    GST_WARNING ("Allocation of SegmentTemplate node failed!");
    return;
  }

  /* Inherit attribute values from parent */
  if (parent) {
    new_segment_template->media = xmlMemStrdup (parent->media);
    new_segment_template->index = xmlMemStrdup (parent->index);
    new_segment_template->initialization =
        xmlMemStrdup (parent->initialization);
    new_segment_template->bitstreamSwitching =
        xmlMemStrdup (parent->bitstreamSwitching);
  }

  GST_LOG ("extension of SegmentTemplate node:");
  gst_mpdparser_parse_mult_seg_base_type_ext
      (&new_segment_template->MultSegBaseType, a_node,
      (parent ? parent->MultSegBaseType : NULL));

  GST_LOG ("attributes of SegmentTemplate node:");
  if (gst_mpdparser_get_xml_prop_string (a_node, "media", &strval)) {
    new_segment_template->media = strval;
  }
  if (gst_mpdparser_get_xml_prop_string (a_node, "index", &strval)) {
    new_segment_template->index = strval;
  }
  if (gst_mpdparser_get_xml_prop_string (a_node, "initialization", &strval)) {
    new_segment_template->initialization = strval;
  }
  if (gst_mpdparser_get_xml_prop_string (a_node, "bitstreamSwitching", &strval)) {
    new_segment_template->bitstreamSwitching = strval;
  }
}

static void
gst_mpdparser_parse_period_node (GList ** list, xmlNode * a_node)
{
  xmlNode *cur_node;
  GstPeriodNode *new_period;

  new_period = g_slice_new0 (GstPeriodNode);
  if (new_period == NULL) {
    GST_WARNING ("Allocation of Period node failed!");
    return;
  }
  *list = g_list_append (*list, new_period);

  new_period->start = GST_CLOCK_TIME_NONE;

  GST_LOG ("attributes of Period node:");
  gst_mpdparser_get_xml_prop_string (a_node, "id", &new_period->id);
  gst_mpdparser_get_xml_prop_duration (a_node, "start", -1, &new_period->start);
  gst_mpdparser_get_xml_prop_duration (a_node, "duration", -1,
      &new_period->duration);
  gst_mpdparser_get_xml_prop_boolean (a_node, "bitstreamSwitching",
      FALSE, &new_period->bitstreamSwitching);

  /* explore children nodes */
  for (cur_node = a_node->children; cur_node; cur_node = cur_node->next) {
    if (cur_node->type == XML_ELEMENT_NODE) {
      if (xmlStrcmp (cur_node->name, (xmlChar *) "SegmentBase") == 0) {
        gst_mpdparser_parse_seg_base_type_ext (&new_period->SegmentBase,
            cur_node, NULL);
      } else if (xmlStrcmp (cur_node->name, (xmlChar *) "SegmentList") == 0) {
        gst_mpdparser_parse_segment_list_node (&new_period->SegmentList,
            cur_node, NULL);
      } else if (xmlStrcmp (cur_node->name, (xmlChar *) "SegmentTemplate") == 0) {
        gst_mpdparser_parse_segment_template_node (&new_period->SegmentTemplate,
            cur_node, NULL);
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
        gst_mpdparser_parse_adaptation_set_node (&new_period->AdaptationSets,
            cur_node, new_period);
      }
    }
  }
}

static void
gst_mpdparser_parse_program_info_node (GList ** list, xmlNode * a_node)
{
  xmlNode *cur_node;
  GstProgramInformationNode *new_prog_info;

  new_prog_info = g_slice_new0 (GstProgramInformationNode);
  if (new_prog_info == NULL) {
    GST_WARNING ("Allocation of ProgramInfo node failed!");
    return;
  }
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
  if (new_metrics_range == NULL) {
    GST_WARNING ("Allocation of Metrics Range node failed!");
    return;
  }
  *list = g_list_append (*list, new_metrics_range);

  GST_LOG ("attributes of Metrics Range node:");
  gst_mpdparser_get_xml_prop_duration (a_node, "starttime", -1,
      &new_metrics_range->starttime);
  gst_mpdparser_get_xml_prop_duration (a_node, "duration", -1,
      &new_metrics_range->duration);
}

static void
gst_mpdparser_parse_metrics_node (GList ** list, xmlNode * a_node)
{
  xmlNode *cur_node;
  GstMetricsNode *new_metrics;

  new_metrics = g_slice_new0 (GstMetricsNode);
  if (new_metrics == NULL) {
    GST_WARNING ("Allocation of Metrics node failed!");
    return;
  }
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

static void
gst_mpdparser_parse_root_node (GstMPDNode ** pointer, xmlNode * a_node)
{
  xmlNode *cur_node;
  GstMPDNode *new_mpd;

  gst_mpdparser_free_mpd_node (*pointer);
  *pointer = new_mpd = g_slice_new0 (GstMPDNode);
  if (new_mpd == NULL) {
    GST_WARNING ("Allocation of MPD node failed!");
    return;
  }

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
  gst_mpdparser_get_xml_prop_duration (a_node, "mediaPresentationDuration", -1,
      &new_mpd->mediaPresentationDuration);
  gst_mpdparser_get_xml_prop_duration (a_node, "minimumUpdatePeriod", -1,
      &new_mpd->minimumUpdatePeriod);
  gst_mpdparser_get_xml_prop_duration (a_node, "minBufferTime", -1,
      &new_mpd->minBufferTime);
  gst_mpdparser_get_xml_prop_duration (a_node, "timeShiftBufferDepth", -1,
      &new_mpd->timeShiftBufferDepth);
  gst_mpdparser_get_xml_prop_duration (a_node, "suggestedPresentationDelay", -1,
      &new_mpd->suggestedPresentationDelay);
  gst_mpdparser_get_xml_prop_duration (a_node, "maxSegmentDuration", -1,
      &new_mpd->maxSegmentDuration);
  gst_mpdparser_get_xml_prop_duration (a_node, "maxSubsegmentDuration", -1,
      &new_mpd->maxSubsegmentDuration);

  /* explore children Period nodes */
  for (cur_node = a_node->children; cur_node; cur_node = cur_node->next) {
    if (cur_node->type == XML_ELEMENT_NODE) {
      if (xmlStrcmp (cur_node->name, (xmlChar *) "Period") == 0) {
        gst_mpdparser_parse_period_node (&new_mpd->Periods, cur_node);
      } else if (xmlStrcmp (cur_node->name,
              (xmlChar *) "ProgramInformation") == 0) {
        gst_mpdparser_parse_program_info_node (&new_mpd->ProgramInfo, cur_node);
      } else if (xmlStrcmp (cur_node->name, (xmlChar *) "BaseURL") == 0) {
        gst_mpdparser_parse_baseURL_node (&new_mpd->BaseURLs, cur_node);
      } else if (xmlStrcmp (cur_node->name, (xmlChar *) "Location") == 0) {
        gst_mpdparser_parse_location_node (&new_mpd->Locations, cur_node);
      } else if (xmlStrcmp (cur_node->name, (xmlChar *) "Metrics") == 0) {
        gst_mpdparser_parse_metrics_node (&new_mpd->Metrics, cur_node);
      }
    }
  }
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

  if (strncmp_ext (mime, "audio"))
    return GST_STREAM_AUDIO;
  if (strncmp_ext (mime, "video"))
    return GST_STREAM_VIDEO;
  if (strncmp_ext (mime, "application"))
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

  if (Representation && Representation->SegmentBase
      && Representation->SegmentBase->Initialization) {
    SegmentBase = Representation->SegmentBase;
  } else if (AdaptationSet && AdaptationSet->SegmentBase
      && AdaptationSet->SegmentBase->Initialization) {
    SegmentBase = AdaptationSet->SegmentBase;
  } else if (Period && Period->SegmentBase
      && Period->SegmentBase->Initialization) {
    SegmentBase = Period->SegmentBase;
  }
  /* the SegmentBase element could be encoded also inside a SegmentList element */
  if (SegmentBase == NULL) {
    if (Representation && Representation->SegmentList
        && Representation->SegmentList->MultSegBaseType
        && Representation->SegmentList->MultSegBaseType->SegBaseType
        && Representation->SegmentList->MultSegBaseType->
        SegBaseType->Initialization) {
      SegmentBase = Representation->SegmentList->MultSegBaseType->SegBaseType;
    } else if (AdaptationSet && AdaptationSet->SegmentList
        && AdaptationSet->SegmentList->MultSegBaseType
        && AdaptationSet->SegmentList->MultSegBaseType->SegBaseType
        && AdaptationSet->SegmentList->MultSegBaseType->
        SegBaseType->Initialization) {
      SegmentBase = AdaptationSet->SegmentList->MultSegBaseType->SegBaseType;
    } else if (Period && Period->SegmentList
        && Period->SegmentList->MultSegBaseType
        && Period->SegmentList->MultSegBaseType->SegBaseType
        && Period->SegmentList->MultSegBaseType->SegBaseType->Initialization) {
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
    gint max_bandwidth)
{
  GList *list = NULL, *best = NULL;
  GstRepresentationNode *representation;
  gint best_bandwidth = 0;

  GST_DEBUG ("max_bandwidth = %i", max_bandwidth);

  if (Representations == NULL)
    return -1;

  if (max_bandwidth <= 0)       /* 0 => get lowest representation available */
    return 0;

  for (list = g_list_first (Representations); list; list = g_list_next (list)) {
    representation = (GstRepresentationNode *) list->data;
    if (representation && representation->bandwidth <= max_bandwidth &&
        representation->bandwidth > best_bandwidth) {
      best = list;
      best_bandwidth = representation->bandwidth;
    }
  }

  return best ? g_list_position (Representations, best) : -1;
}

static GstSegmentListNode *
gst_mpdparser_get_segment_list (GstPeriodNode * Period,
    GstAdaptationSetNode * AdaptationSet,
    GstRepresentationNode * Representation)
{
  GstSegmentListNode *SegmentList = NULL;

  if (Representation && Representation->SegmentList) {
    SegmentList = Representation->SegmentList;
  } else if (AdaptationSet && AdaptationSet->SegmentList) {
    SegmentList = AdaptationSet->SegmentList;
  } else {
    SegmentList = Period->SegmentList;
  }

  return SegmentList;
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
    g_slice_free (GstFrameRate, adaptation_set_node->minFrameRate);
    g_slice_free (GstFrameRate, adaptation_set_node->maxFrameRate);
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
  g_return_val_if_fail (InitializationURL != NULL, NULL);

  url_prefix = InitializationURL->sourceURL ? InitializationURL->sourceURL :
      stream->baseURL;

  return url_prefix;
}

static gchar *
gst_mpdparser_build_URL_from_template (const gchar * url_template,
    const gchar * id, guint number, guint bandwidth, guint64 time)
{
  static gchar default_format[] = "%01d";
  gchar **tokens, *token, *ret;
  const gchar *format;
  gint i, num_tokens;
  gboolean last_token_par = TRUE;       /* last token was a parameter */

  g_return_val_if_fail (url_template != NULL, NULL);
  tokens = g_strsplit_set (url_template, "$", -1);
  if (!tokens) {
    GST_WARNING ("Scan of URL template failed!");
    return NULL;
  }
  num_tokens = g_strv_length (tokens);

  for (i = 0; i < num_tokens; i++) {
    token = tokens[i];
    format = default_format;

    if (!g_strcmp0 (token, "RepresentationID")) {
      tokens[i] = g_strdup_printf ("%s", id);
      g_free (token);
      last_token_par = TRUE;
    } else if (!strncmp (token, "Number", 6)) {
      if (strlen (token) > 6) {
        format = token + 6;     /* format tag */
      }
      tokens[i] = g_strdup_printf (format, number);
      g_free (token);
      last_token_par = TRUE;
    } else if (!strncmp (token, "Bandwidth", 9)) {
      if (strlen (token) > 9) {
        format = token + 9;     /* format tag */
      }
      tokens[i] = g_strdup_printf (format, bandwidth);
      g_free (token);
      last_token_par = TRUE;
    } else if (!strncmp (token, "Time", 4)) {
      if (strlen (token) > 4) {
        format = token + 4;     /* format tag */
      } else {
        format = "%" G_GUINT64_FORMAT;
      }
      tokens[i] = g_strdup_printf (format, time);
      g_free (token);
      last_token_par = TRUE;
    } else if (!g_strcmp0 (token, "")) {
      if (!last_token_par) {
        tokens[i] = g_strdup_printf ("%s", "$");
        g_free (token);
        last_token_par = TRUE;
      }
    } else {
      last_token_par = FALSE;
    }
  }

  ret = g_strjoinv (NULL, tokens);
  g_strfreev (tokens);

  return ret;
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
    if (clone) {
      clone->first_byte_pos = range->first_byte_pos;
      clone->last_byte_pos = range->last_byte_pos;
    } else {
      GST_WARNING ("Allocation of GstRange failed!");
    }
  }

  return clone;
}

static GstURLType *
gst_mpdparser_clone_URL (GstURLType * url)
{

  GstURLType *clone = NULL;

  if (url) {
    clone = g_slice_new0 (GstURLType);
    if (clone) {
      if (url->sourceURL) {
        clone->sourceURL = xmlMemStrdup (url->sourceURL);
      }
      clone->range = gst_mpdparser_clone_range (url->range);
    } else {
      GST_WARNING ("Allocation of URLType node failed!");
    }
  }

  return clone;
}

/* select a stream and extract the baseURL (if present) */
static gchar *
gst_mpdparser_parse_baseURL (GstMpdClient * client, GstActiveStream * stream,
    gchar ** query)
{
  GstStreamPeriod *stream_period;
  GstBaseURL *baseURL;
  gchar *mpd_uri;
  GList *list;
  static gchar *baseURL_array[5];
  static gchar empty[] = "";
  gchar *ret = NULL;

  g_return_val_if_fail (stream != NULL, empty);
  stream_period = gst_mpdparser_get_stream_period (client);
  g_return_val_if_fail (stream_period != NULL, empty);
  g_return_val_if_fail (stream_period->period != NULL, empty);

  baseURL_array[0] = baseURL_array[1] = baseURL_array[2] = baseURL_array[3] =
      empty;
  baseURL_array[4] = NULL;

  /* FIXME: this simple implementation is not fully compliant with RFC 3986 */
  if ((list = client->mpd_node->BaseURLs) != NULL) {
    baseURL = g_list_nth_data (list, stream->baseURL_idx);
    if (!baseURL) {
      baseURL = list->data;
    }
    baseURL_array[0] = baseURL->baseURL;
  }
  if ((list = stream_period->period->BaseURLs) != NULL) {
    baseURL = g_list_nth_data (list, stream->baseURL_idx);
    if (!baseURL) {
      baseURL = list->data;
    }
    baseURL_array[1] = baseURL->baseURL;
  }
  GST_DEBUG ("Current adaptation set id %i (%s)", stream->cur_adapt_set->id,
      stream->cur_adapt_set->contentType);

  if ((list = stream->cur_adapt_set->BaseURLs) != NULL) {
    baseURL = g_list_nth_data (list, stream->baseURL_idx);
    if (!baseURL) {
      baseURL = list->data;
    }
    baseURL_array[2] = baseURL->baseURL;
  }

  if ((list = stream->cur_representation->BaseURLs) != NULL) {
    baseURL = g_list_nth_data (list, stream->baseURL_idx);
    if (!baseURL) {
      baseURL = list->data;
    }
    baseURL_array[3] = baseURL->baseURL;
  }

  ret = g_strjoinv (NULL, baseURL_array);

  /* get base URI from MPD file URI, if the "http" scheme is missing */
  mpd_uri = client->mpd_base_uri ? client->mpd_base_uri : client->mpd_uri;
  if (mpd_uri != NULL && strncmp (ret, "http://", 7) != 0) {
    gchar *last_sep, *tmp1, *tmp2;

    if (ret[0] == '?') {
      if (query)
        *query = g_strdup (ret);
      g_free (ret);
      ret = NULL;
    } else {
      if (query)
        *query = NULL;
    }

    last_sep = strrchr (mpd_uri, '/');
    if (last_sep) {
      tmp1 = g_strndup (mpd_uri, last_sep - mpd_uri + 1);
      if (ret) {
        tmp2 = ret;
        ret = g_strconcat (tmp1, tmp2, NULL);
        g_free (tmp1);
        g_free (tmp2);
      } else {
        ret = tmp1;
      }
      GST_INFO ("Got base URI from MPD file URI %s", ret);
    }
  }

  if (ret && *query == NULL) {
    gchar *params = strchr (ret, '?');
    if (params) {
      *query = g_strdup (params);
      params[0] = '\0';         /* can ignore the rest of the string */
    }
  }

  return ret;
}

static GstClockTime
gst_mpd_client_get_segment_duration (GstMpdClient * client,
    GstActiveStream * stream)
{
  GstStreamPeriod *stream_period;
  GstMultSegmentBaseType *base = NULL;
  GstClockTime duration = 0;
  guint timescale;

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
  } else {
    duration = base->duration * GST_SECOND;
    timescale = base->SegBaseType->timescale;

    if (timescale > 1)
      duration /= timescale;
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
  g_mutex_init (&client->lock);

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

  g_mutex_clear (&client->lock);

  g_free (client->mpd_uri);
  client->mpd_uri = NULL;
  g_free (client->mpd_base_uri);
  client->mpd_base_uri = NULL;

  g_free (client);
}

gboolean
gst_mpd_parse (GstMpdClient * client, const gchar * data, gint size)
{
  if (data) {
    xmlDocPtr doc;
    xmlNode *root_element = NULL;

    GST_DEBUG ("MPD file fully buffered, start parsing...");

    GST_MPD_CLIENT_LOCK (client);
    /* parse the complete MPD file into a tree (using the libxml2 default parser API) */

    /* this initialize the library and check potential ABI mismatches
     * between the version it was compiled for and the actual shared
     * library used
     */
    LIBXML_TEST_VERSION
        /* parse "data" into a document (which is a libxml2 tree structure xmlDoc) */
        doc = xmlReadMemory (data, size, "noname.xml", NULL, 0);
    if (doc == NULL) {
      GST_ERROR ("failed to parse the MPD file");
      GST_MPD_CLIENT_UNLOCK (client);
      return FALSE;
    } else {
      /* get the root element node */
      root_element = xmlDocGetRootElement (doc);

      if (root_element->type != XML_ELEMENT_NODE
          || xmlStrcmp (root_element->name, (xmlChar *) "MPD") != 0) {
        GST_ERROR
            ("can not find the root element MPD, failed to parse the MPD file");
      } else {
        /* now we can parse the MPD root node and all children nodes, recursively */
        gst_mpdparser_parse_root_node (&client->mpd_node, root_element);
      }
      /* free the document */
      xmlFreeDoc (doc);
    }
    GST_MPD_CLIENT_UNLOCK (client);

    return TRUE;
  }

  return FALSE;
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

gboolean
gst_mpdparser_get_chunk_by_index (GstMpdClient * client, guint indexStream,
    guint indexChunk, GstMediaSegment * segment)
{
  GstActiveStream *stream;

  /* select stream */
  g_return_val_if_fail (client != NULL, FALSE);
  g_return_val_if_fail (client->active_streams != NULL, FALSE);
  stream = g_list_nth_data (client->active_streams, indexStream);
  g_return_val_if_fail (stream != NULL, FALSE);

  if (stream->segments) {
    GstMediaSegment *list_segment;
    /* fixed list of segments */
    if (indexChunk >= stream->segments->len)
      return FALSE;

    list_segment = g_ptr_array_index (stream->segments, indexChunk);
    segment->SegmentURL = list_segment->SegmentURL;
    segment->number = list_segment->number;
    segment->start = list_segment->start;
    segment->start_time = list_segment->start_time;
    segment->duration = list_segment->duration;
  } else {
    GstClockTime duration;
    GstStreamPeriod *stream_period;

    g_return_val_if_fail (stream->cur_seg_template->
        MultSegBaseType->SegmentTimeline == NULL, FALSE);
    /* segment template generator */
    duration = gst_mpd_client_get_segment_duration (client, stream);
    if (!GST_CLOCK_TIME_IS_VALID (duration))
      return FALSE;

    stream_period = gst_mpdparser_get_stream_period (client);

    segment->number = indexChunk
        + stream->cur_seg_template->MultSegBaseType->startNumber;
    segment->start_time = duration * indexChunk;
    segment->duration = duration;
    segment->SegmentURL = NULL;

    if (segment->start_time > stream_period->start + stream_period->duration) {
      return FALSE;
    }
  }
  return TRUE;
}

static gboolean
gst_mpd_client_add_media_segment (GstActiveStream * stream,
    GstSegmentURLNode * url_node, guint number, guint64 start,
    GstClockTime start_time, GstClockTime duration)
{
  GstMediaSegment *media_segment;

  g_return_val_if_fail (stream->segments != NULL, FALSE);

  media_segment = g_slice_new0 (GstMediaSegment);
  if (media_segment == NULL) {
    GST_WARNING ("Allocation of GstMediaSegment struct failed!");
    return FALSE;
  }

  media_segment->SegmentURL = url_node;
  media_segment->number = number;
  media_segment->start = start;
  media_segment->start_time = start_time;
  media_segment->duration = duration;

  g_ptr_array_add (stream->segments, media_segment);

  return TRUE;
}

gboolean
gst_mpd_client_setup_representation (GstMpdClient * client,
    GstActiveStream * stream, GstRepresentationNode * representation)
{
  GstStreamPeriod *stream_period;
  GList *rep_list;
  GstClockTime PeriodStart, PeriodEnd, start_time, duration;
  GstMediaSegment *last_media_segment;
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
            gst_mpdparser_get_segment_list (stream_period->period,
                stream->cur_adapt_set, representation)) == NULL) {
      GST_DEBUG ("No useful SegmentList node for the current Representation");
      /* here we should have a single segment for each representation, whose URL is encoded in the baseURL element */
      if (!gst_mpd_client_add_media_segment (stream, NULL, 1, 0, PeriodStart,
              PeriodEnd)) {
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
      start_time = PeriodStart;

      GST_LOG ("Building media segment list using a SegmentList node");
      if (stream->cur_segment_list->MultSegBaseType->SegmentTimeline) {
        GstSegmentTimelineNode *timeline;
        GstSNode *S;
        GList *list;

        timeline = stream->cur_segment_list->MultSegBaseType->SegmentTimeline;
        for (list = g_queue_peek_head_link (&timeline->S); list;
            list = g_list_next (list)) {
          guint j, timescale;

          S = (GstSNode *) list->data;
          GST_LOG ("Processing S node: d=%" G_GUINT64_FORMAT " r=%d t=%"
              G_GUINT64_FORMAT, S->d, S->r, S->t);
          duration = S->d * GST_SECOND;
          timescale =
              stream->cur_segment_list->MultSegBaseType->SegBaseType->timescale;
          if (timescale > 1)
            duration /= timescale;
          if (S->t > 0) {
            start = S->t;
            start_time = S->t * GST_SECOND;
            if (timescale > 1)
              start_time /= timescale;
          }

          for (j = 0; j <= S->r && SegmentURL != NULL; j++) {
            if (!gst_mpd_client_add_media_segment (stream, SegmentURL->data, i,
                    start, start_time, duration)) {
              return FALSE;
            }
            i++;
            start += S->d;
            start_time += duration;
            SegmentURL = g_list_next (SegmentURL);
          }
        }
      } else {
        duration = gst_mpd_client_get_segment_duration (client, stream);
        if (!GST_CLOCK_TIME_IS_VALID (duration))
          return FALSE;

        while (SegmentURL) {
          if (!gst_mpd_client_add_media_segment (stream, SegmentURL->data, i, 0,
                  start_time, duration)) {
            return FALSE;
          }
          i++;
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
      if (!gst_mpd_client_add_media_segment (stream, NULL, 1, 0, 0, PeriodEnd)) {
        return FALSE;
      }
    } else {
      /* build segment list */
      i = stream->cur_seg_template->MultSegBaseType->startNumber;
      start = 0;
      start_time = PeriodStart;

      GST_LOG ("Building media segment list using this template: %s",
          stream->cur_seg_template->media);
      if (stream->cur_seg_template->MultSegBaseType->SegmentTimeline) {
        GstSegmentTimelineNode *timeline;
        GstSNode *S;
        GList *list;

        timeline = stream->cur_seg_template->MultSegBaseType->SegmentTimeline;
        gst_mpdparser_init_active_stream_segments (stream);
        for (list = g_queue_peek_head_link (&timeline->S); list;
            list = g_list_next (list)) {
          guint j, timescale;

          S = (GstSNode *) list->data;
          GST_LOG ("Processing S node: d=%" G_GUINT64_FORMAT " r=%u t=%"
              G_GUINT64_FORMAT, S->d, S->r, S->t);
          duration = S->d * GST_SECOND;
          timescale =
              stream->cur_seg_template->MultSegBaseType->SegBaseType->timescale;
          if (timescale > 1)
            duration /= timescale;
          if (S->t > 0) {
            start = S->t;
            start_time = S->t * GST_SECOND;
            if (timescale > 1)
              start_time /= timescale;
          }

          for (j = 0; j <= S->r; j++) {
            if (!gst_mpd_client_add_media_segment (stream, NULL, i, start,
                    start_time, duration)) {
              return FALSE;
            }
            i++;
            start += S->d;
            start_time += duration;
          }
        }
      } else {
        /* NOP - The segment is created on demand with the template, no need
         * to build a list */
      }
    }
  }

  /* check duration of last segment */
  last_media_segment = (stream->segments && stream->segments->len) ?
      g_ptr_array_index (stream->segments, stream->segments->len - 1) : NULL;

  if (last_media_segment && GST_CLOCK_TIME_IS_VALID (PeriodEnd)) {
    if (last_media_segment->start_time + last_media_segment->duration >
        PeriodEnd) {
      last_media_segment->duration = PeriodEnd - last_media_segment->start_time;
      GST_LOG ("Fixed duration of last segment: %" GST_TIME_FORMAT,
          GST_TIME_ARGS (last_media_segment->duration));
    }
    GST_LOG ("Built a list of %d segments", last_media_segment->number);
  }

  g_free (stream->baseURL);
  g_free (stream->queryURL);
  stream->baseURL =
      gst_mpdparser_parse_baseURL (client, stream, &stream->queryURL);

  return TRUE;
}

gboolean
gst_mpd_client_setup_media_presentation (GstMpdClient * client)
{
  GstStreamPeriod *stream_period;
  GstPeriodNode *period_node;
  GstClockTime start, duration;
  GList *list, *next;
  guint idx;
  gboolean ret = FALSE;

  g_return_val_if_fail (client != NULL, FALSE);
  g_return_val_if_fail (client->mpd_node != NULL, FALSE);

  GST_DEBUG ("Building the list of Periods in the Media Presentation");
  GST_MPD_CLIENT_LOCK (client);
  /* clean the old period list, if any */
  if (client->periods) {
    g_list_foreach (client->periods,
        (GFunc) gst_mpdparser_free_stream_period, NULL);
    g_list_free (client->periods);
    client->periods = NULL;
  }

  idx = 0;
  start = 0;
  duration = GST_CLOCK_TIME_NONE;
  for (list = g_list_first (client->mpd_node->Periods); list;
      list = g_list_next (list)) {
    period_node = (GstPeriodNode *) list->data;
    if (period_node->start != -1) {
      /* we have a regular period */
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

    if (period_node->duration != -1) {
      duration = period_node->duration * GST_MSECOND;
    } else if ((next = g_list_next (list)) != NULL) {
      /* try to infer this period duration from the start time of the next period */
      GstPeriodNode *next_period_node = next->data;
      if (next_period_node->start != -1) {
        duration = next_period_node->start * GST_MSECOND - start;
      } else if (client->mpd_node->type == GST_MPD_FILE_TYPE_DYNAMIC) {
        /* might be a live file, ignore unspecified duration */
      } else {
        /* Invalid MPD file! */
        goto syntax_error;
      }
    } else if (client->mpd_node->mediaPresentationDuration != -1) {
      /* last Period of the Media Presentation */
      duration =
          client->mpd_node->mediaPresentationDuration * GST_MSECOND - start;
    } else if (client->mpd_node->type == GST_MPD_FILE_TYPE_DYNAMIC) {
      /* might be a live file, ignore unspecified duration */
    } else {
      /* Invalid MPD file! */
      goto syntax_error;
    }

    stream_period = g_slice_new0 (GstStreamPeriod);
    if (stream_period == NULL) {
      goto no_mem;
    }
    client->periods = g_list_append (client->periods, stream_period);
    stream_period->period = period_node;
    stream_period->number = idx++;
    stream_period->start = start;
    stream_period->duration = duration;
    ret = TRUE;
    GST_LOG (" - added Period %d start=%" GST_TIME_FORMAT " duration=%"
        GST_TIME_FORMAT, idx, GST_TIME_ARGS (start), GST_TIME_ARGS (duration));
  }

  GST_MPD_CLIENT_UNLOCK (client);
  GST_DEBUG ("Found a total of %d valid Periods in the Media Presentation",
      idx);
  return ret;

early:
  GST_MPD_CLIENT_UNLOCK (client);
  GST_WARNING
      ("Found an Early Available Period, skipping the rest of the Media Presentation");
  return ret;

syntax_error:
  GST_MPD_CLIENT_UNLOCK (client);
  GST_WARNING
      ("Cannot get the duration of the Period %d, skipping the rest of the Media Presentation",
      idx);
  return ret;

no_mem:
  GST_MPD_CLIENT_UNLOCK (client);
  GST_WARNING ("Allocation of GstStreamPeriod struct failed!");
  return FALSE;
}

static GList *
gst_mpd_client_get_adaptation_sets_for_period (GstMpdClient * client,
    GstStreamPeriod * period)
{
  g_return_val_if_fail (period != NULL, NULL);
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
  if (stream == NULL) {
    GST_WARNING ("Allocation of active stream struct failed!");
    return FALSE;
  }
  gst_mpdparser_init_active_stream_segments (stream);

  stream->baseURL_idx = 0;
  stream->cur_adapt_set = adapt_set;

  GST_DEBUG ("0. Current stream %p", stream);

  /* retrive representation list */
  if (stream->cur_adapt_set != NULL)
    rep_list = stream->cur_adapt_set->Representations;

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
    g_slice_free (GstActiveStream, stream);
    return FALSE;
  }
  stream->mimeType =
      gst_mpdparser_representation_get_mimetype (adapt_set, representation);
  if (stream->mimeType == GST_STREAM_UNKNOWN) {
    g_slice_free (GstActiveStream, stream);
    return FALSE;
  }

  client->active_streams = g_list_append (client->active_streams, stream);
  if (!gst_mpd_client_setup_representation (client, stream, representation))
    return FALSE;

  GST_INFO ("Successfully setup the download pipeline for mimeType %d",
      stream->mimeType);

  return TRUE;
}

gboolean
gst_mpd_client_stream_seek (GstMpdClient * client, GstActiveStream * stream,
    GstClockTime ts)
{
  gint index = 0;
  GstMediaSegment *selectedChunk = NULL;

  g_return_val_if_fail (stream != NULL, 0);

  GST_MPD_CLIENT_LOCK (client);
  if (stream->segments) {
    for (index = 0; index < stream->segments->len; index++) {
      GstMediaSegment *segment = g_ptr_array_index (stream->segments, index);

      GST_DEBUG ("Looking at fragment sequence chunk %d", index);
      if (segment->start_time <= ts
          && ts < segment->start_time + segment->duration) {
        selectedChunk = segment;
        break;
      }
    }

    if (selectedChunk == NULL) {
      GST_MPD_CLIENT_UNLOCK (client);
      return FALSE;
    }
  } else {
    GstClockTime duration =
        gst_mpd_client_get_segment_duration (client, stream);
    g_return_val_if_fail (stream->cur_seg_template->MultSegBaseType->
        SegmentTimeline == NULL, FALSE);
    if (!GST_CLOCK_TIME_IS_VALID (duration)) {
      GST_MPD_CLIENT_UNLOCK (client);
      return FALSE;
    }
    index = ts / duration;
  }

  gst_mpd_client_set_segment_index (stream, index);

  GST_MPD_CLIENT_UNLOCK (client);

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

gint
gst_mpd_client_get_segment_index_at_time (GstMpdClient * client,
    GstActiveStream * stream, const GstDateTime * time)
{
  GstClockTime seg_duration;
  gint64 diff;
  GstDateTime *avail_start =
      gst_mpd_client_get_availability_start_time (client);
  GstStreamPeriod *stream_period = gst_mpdparser_get_stream_period (client);

  if (avail_start == NULL)
    return -1;

  if (stream_period && stream_period->period) {
    GstDateTime *t;

    t = gst_mpd_client_add_time_difference (avail_start,
        stream_period->period->start * 1000);
    gst_date_time_unref (avail_start);
    avail_start = t;
  }
  diff = gst_mpd_client_calculate_time_difference (avail_start, time);
  gst_date_time_unref (avail_start);

  if (diff < 0)
    return -2;
  if (diff > gst_mpd_client_get_media_presentation_duration (client))
    return -3;

  /* TODO: Assumes all fragments are roughly the same duration */
  seg_duration = gst_mpd_client_get_next_fragment_duration (client, stream);
  if (seg_duration == 0)
    return -1;
  return diff / seg_duration;
}

static GstDateTime *
gst_mpd_client_get_availability_start_time (GstMpdClient * client)
{
  GstDateTime *start_time;

  if (client == NULL)
    return (GstDateTime *) NULL;

  GST_MPD_CLIENT_LOCK (client);
  start_time = client->mpd_node->availabilityStartTime;
  gst_date_time_ref (start_time);
  GST_MPD_CLIENT_UNLOCK (client);
  return start_time;
}

gboolean
gst_mpd_client_get_last_fragment_timestamp (GstMpdClient * client,
    guint stream_idx, GstClockTime * ts)
{
  GstActiveStream *stream;
  gint segment_idx;
  GstMediaSegment currentChunk;

  GST_DEBUG ("Stream index: %i", stream_idx);
  stream = g_list_nth_data (client->active_streams, stream_idx);
  g_return_val_if_fail (stream != NULL, 0);

  GST_MPD_CLIENT_LOCK (client);
  segment_idx = gst_mpd_client_get_segments_counts (stream) - 1;
  GST_DEBUG ("Looking for fragment sequence chunk %d", segment_idx);

  if (!gst_mpdparser_get_chunk_by_index (client, stream_idx, segment_idx,
          &currentChunk)) {
    GST_MPD_CLIENT_UNLOCK (client);
    return FALSE;
  }

  *ts = currentChunk.start_time;
  GST_MPD_CLIENT_UNLOCK (client);

  return TRUE;
}

gboolean
gst_mpd_client_get_next_fragment_timestamp (GstMpdClient * client,
    guint stream_idx, GstClockTime * ts)
{
  GstActiveStream *stream;
  gint segment_idx;
  GstMediaSegment currentChunk;

  GST_DEBUG ("Stream index: %i", stream_idx);
  stream = g_list_nth_data (client->active_streams, stream_idx);
  g_return_val_if_fail (stream != NULL, 0);

  GST_MPD_CLIENT_LOCK (client);
  segment_idx = gst_mpd_client_get_segment_index (stream);
  GST_DEBUG ("Looking for fragment sequence chunk %d", segment_idx);

  if (!gst_mpdparser_get_chunk_by_index (client, stream_idx, segment_idx,
          &currentChunk)) {
    GST_MPD_CLIENT_UNLOCK (client);
    return FALSE;
  }

  *ts = currentChunk.start_time;
  GST_MPD_CLIENT_UNLOCK (client);

  return TRUE;
}

gboolean
gst_mpd_client_get_next_fragment (GstMpdClient * client,
    guint indexStream, GstMediaFragmentInfo * fragment, gboolean forward)
{
  GstActiveStream *stream = NULL;
  GstMediaSegment currentChunk;
  gchar *mediaURL = NULL;
  gchar *indexURL = NULL;
  guint segment_idx;

  /* select stream */
  g_return_val_if_fail (client != NULL, FALSE);
  g_return_val_if_fail (client->active_streams != NULL, FALSE);
  stream = g_list_nth_data (client->active_streams, indexStream);
  g_return_val_if_fail (stream != NULL, FALSE);
  g_return_val_if_fail (stream->cur_representation != NULL, FALSE);

  GST_MPD_CLIENT_LOCK (client);
  segment_idx = gst_mpd_client_get_segment_index (stream);
  GST_DEBUG ("Looking for fragment sequence chunk %d", segment_idx);

  if (!gst_mpdparser_get_chunk_by_index (client, indexStream, segment_idx,
          &currentChunk)) {
    GST_MPD_CLIENT_UNLOCK (client);
    return FALSE;
  }

  GST_DEBUG ("currentChunk->SegmentURL = %p", currentChunk.SegmentURL);
  if (currentChunk.SegmentURL != NULL) {
    mediaURL =
        g_strdup (gst_mpdparser_get_mediaURL (stream, currentChunk.SegmentURL));
    indexURL = currentChunk.SegmentURL->index;
  } else if (stream->cur_seg_template != NULL) {
    mediaURL =
        gst_mpdparser_build_URL_from_template (stream->cur_seg_template->media,
        stream->cur_representation->id, currentChunk.number,
        stream->cur_representation->bandwidth, currentChunk.start);
    if (stream->cur_seg_template->index) {
      indexURL =
          gst_mpdparser_build_URL_from_template (stream->cur_seg_template->
          index, stream->cur_representation->id, currentChunk.number,
          stream->cur_representation->bandwidth, currentChunk.start);
    }
  }
  GST_DEBUG ("mediaURL = %s", mediaURL);
  GST_DEBUG ("indexURL = %s", indexURL);

  fragment->timestamp = currentChunk.start_time;
  fragment->duration = currentChunk.duration;
  fragment->discontinuity = segment_idx != currentChunk.number;
  fragment->range_start = 0;
  fragment->range_end = -1;
  fragment->index_uri = NULL;
  fragment->index_range_start = 0;
  fragment->index_range_end = -1;
  if (currentChunk.SegmentURL) {
    if (currentChunk.SegmentURL->mediaRange) {
      fragment->range_start =
          currentChunk.SegmentURL->mediaRange->first_byte_pos;
      fragment->range_end = currentChunk.SegmentURL->mediaRange->last_byte_pos;
    }
    if (currentChunk.SegmentURL->indexRange) {
      fragment->index_range_start =
          currentChunk.SegmentURL->indexRange->first_byte_pos;
      fragment->index_range_end =
          currentChunk.SegmentURL->indexRange->last_byte_pos;
    }
  }

  if (mediaURL == NULL) {
    /* single segment with URL encoded in the baseURL syntax element */
    fragment->uri = g_strdup (stream->baseURL);
  } else if (strncmp (mediaURL, "http://", 7) != 0) {
    fragment->uri =
        g_strconcat (stream->baseURL, mediaURL, stream->queryURL, NULL);
    g_free (mediaURL);
  } else if (stream->queryURL) {
    fragment->uri = g_strconcat (mediaURL, stream->queryURL, NULL);
    g_free (mediaURL);
  } else {
    fragment->uri = mediaURL;
  }

  if (indexURL != NULL) {
    if (strncmp (indexURL, "http://", 7) != 0) {
      fragment->index_uri =
          g_strconcat (stream->baseURL, indexURL, stream->queryURL, NULL);
      g_free (indexURL);
    } else if (stream->queryURL) {
      fragment->index_uri = g_strconcat (indexURL, stream->queryURL, NULL);
      g_free (indexURL);
    } else {
      fragment->index_uri = indexURL;
    }
  } else if (fragment->index_range_start || fragment->index_range_end != -1) {
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

  gst_mpd_client_set_segment_index (stream,
      forward ? segment_idx + 1 : segment_idx - 1);
  GST_MPD_CLIENT_UNLOCK (client);

  GST_DEBUG ("Loading chunk with URL %s", fragment->uri);

  return TRUE;
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
  GST_MPD_CLIENT_LOCK (client);
  *uri = NULL;
  if (stream->cur_segment_base && stream->cur_segment_base->Initialization) {
    *uri =
        g_strdup (gst_mpdparser_get_initializationURL (stream,
            stream->cur_segment_base->Initialization));
    if (stream->cur_segment_base->Initialization->range) {
      *range_start =
          stream->cur_segment_base->Initialization->range->first_byte_pos;
      *range_end =
          stream->cur_segment_base->Initialization->range->last_byte_pos;
    }
  } else if (stream->cur_seg_template) {
    const gchar *initialization = NULL;
    if (stream->cur_seg_template->initialization) {
      initialization = stream->cur_seg_template->initialization;
    } else if (stream->cur_adapt_set->SegmentTemplate
        && stream->cur_adapt_set->SegmentTemplate->initialization) {
      initialization = stream->cur_adapt_set->SegmentTemplate->initialization;
    } else if (stream_period->period->SegmentTemplate
        && stream_period->period->SegmentTemplate->initialization) {
      initialization = stream_period->period->SegmentTemplate->initialization;
    }
    if (initialization) {
      *uri = gst_mpdparser_build_URL_from_template (initialization,
          stream->cur_representation->id, 0,
          stream->cur_representation->bandwidth, 0);
    }
  }
  GST_MPD_CLIENT_UNLOCK (client);

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
  GST_MPD_CLIENT_LOCK (client);
  *uri = NULL;
  if (stream->cur_segment_base && stream->cur_segment_base->indexRange) {
    *uri =
        g_strdup (gst_mpdparser_get_initializationURL (stream,
            stream->cur_segment_base->Initialization));
    *range_start = stream->cur_segment_base->indexRange->first_byte_pos;
    *range_end = stream->cur_segment_base->indexRange->last_byte_pos;
  } else if (stream->cur_seg_template) {
    const gchar *initialization = NULL;
    if (stream->cur_seg_template->index) {
      initialization = stream->cur_seg_template->index;
    } else if (stream->cur_adapt_set->SegmentTemplate
        && stream->cur_adapt_set->SegmentTemplate->index) {
      initialization = stream->cur_adapt_set->SegmentTemplate->index;
    } else if (stream_period->period->SegmentTemplate
        && stream_period->period->SegmentTemplate->index) {
      initialization = stream_period->period->SegmentTemplate->index;
    }
    if (initialization)
      *uri = gst_mpdparser_build_URL_from_template (initialization,
          stream->cur_representation->id, 0,
          stream->cur_representation->bandwidth, 0);
  }
  GST_MPD_CLIENT_UNLOCK (client);

  return *uri == NULL ? FALSE : TRUE;
}

GstClockTime
gst_mpd_client_get_next_fragment_duration (GstMpdClient * client,
    GstActiveStream * stream)
{
  GstMediaSegment *media_segment = NULL;
  guint seg_idx;

  g_return_val_if_fail (stream != NULL, 0);

  seg_idx = gst_mpd_client_get_segment_index (stream);

  if (stream->segments) {
    if (seg_idx < stream->segments->len)
      media_segment = g_ptr_array_index (stream->segments, seg_idx);

    return media_segment == NULL ? 0 : media_segment->duration;
  } else {
    GstClockTime duration =
        gst_mpd_client_get_segment_duration (client, stream);
    g_return_val_if_fail (stream->cur_seg_template->
        MultSegBaseType->SegmentTimeline == NULL, 0);

    if (GST_CLOCK_TIME_IS_VALID (duration))
      return duration;
    return 0;
  }
}

GstClockTime
gst_mpd_client_get_media_presentation_duration (GstMpdClient * client)
{
  GstClockTime duration;

  g_return_val_if_fail (client != NULL, GST_CLOCK_TIME_NONE);

  GST_MPD_CLIENT_LOCK (client);
  if (client->mpd_node->mediaPresentationDuration != -1) {
    duration = client->mpd_node->mediaPresentationDuration * GST_MSECOND;
  } else {
    /* We can only get the duration for on-demand streams */
    duration = GST_CLOCK_TIME_NONE;
  }
  GST_MPD_CLIENT_UNLOCK (client);

  return duration;
}

gboolean
gst_mpd_client_set_period_id (GstMpdClient * client, const gchar * period_id)
{
  GstStreamPeriod *next_stream_period;
  gboolean ret = FALSE;
  GList *iter;

  g_return_val_if_fail (client != NULL, FALSE);
  g_return_val_if_fail (client->periods != NULL, FALSE);
  g_return_val_if_fail (period_id != NULL, FALSE);

  GST_MPD_CLIENT_LOCK (client);
  for (iter = client->periods; iter; iter = g_list_next (iter)) {
    next_stream_period = iter->data;

    if (next_stream_period->period->id
        && strcmp (next_stream_period->period->id, period_id) == 0) {
      ret = TRUE;
      break;
    }
  }
  GST_MPD_CLIENT_UNLOCK (client);

  return ret;
}

gboolean
gst_mpd_client_set_period_index (GstMpdClient * client, guint period_idx)
{
  GstStreamPeriod *next_stream_period;
  gboolean ret = FALSE;

  g_return_val_if_fail (client != NULL, FALSE);
  g_return_val_if_fail (client->periods != NULL, FALSE);

  GST_MPD_CLIENT_LOCK (client);
  next_stream_period = g_list_nth_data (client->periods, period_idx);
  if (next_stream_period != NULL) {
    client->period_idx = period_idx;
    ret = TRUE;
  }
  GST_MPD_CLIENT_UNLOCK (client);

  return ret;
}

guint
gst_mpd_client_get_period_index (GstMpdClient * client)
{
  guint period_idx;

  g_return_val_if_fail (client != NULL, 0);
  GST_MPD_CLIENT_LOCK (client);
  period_idx = client->period_idx;
  GST_MPD_CLIENT_UNLOCK (client);

  return period_idx;
}

const gchar *
gst_mpd_client_get_period_id (GstMpdClient * client)
{
  GstStreamPeriod *period;
  gchar *period_id = NULL;

  g_return_val_if_fail (client != NULL, 0);
  GST_MPD_CLIENT_LOCK (client);
  period = g_list_nth_data (client->periods, client->period_idx);
  if (period && period->period)
    period_id = period->period->id;
  GST_MPD_CLIENT_UNLOCK (client);

  return period_id;
}

gboolean
gst_mpd_client_has_next_period (GstMpdClient * client)
{
  GList *next_stream_period;
  g_return_val_if_fail (client != NULL, FALSE);
  g_return_val_if_fail (client->periods != NULL, FALSE);

  GST_MPD_CLIENT_LOCK (client);
  next_stream_period =
      g_list_nth_data (client->periods, client->period_idx + 1);
  GST_MPD_CLIENT_UNLOCK (client);

  return next_stream_period != NULL;
}

void
gst_mpd_client_set_segment_index_for_all_streams (GstMpdClient * client,
    guint segment_idx)
{
  GList *list;

  g_return_if_fail (client != NULL);
  g_return_if_fail (client->active_streams != NULL);

  /* FIXME: support multiple streams with different segment duration */
  for (list = g_list_first (client->active_streams); list;
      list = g_list_next (list)) {
    GstActiveStream *stream = (GstActiveStream *) list->data;
    if (stream) {
      stream->segment_idx = segment_idx;
    }
  }
}

void
gst_mpd_client_set_segment_index (GstActiveStream * stream, guint segment_idx)
{
  g_return_if_fail (stream != NULL);

  stream->segment_idx = segment_idx;
}

guint
gst_mpd_client_get_segment_index (GstActiveStream * stream)
{
  g_return_val_if_fail (stream != NULL, 0);

  return stream->segment_idx;
}

static guint
gst_mpd_client_get_segments_counts (GstActiveStream * stream)
{
  g_return_val_if_fail (stream != NULL, 0);

  if (stream->segments)
    return stream->segments->len;
  g_return_val_if_fail (stream->cur_seg_template->
      MultSegBaseType->SegmentTimeline == NULL, 0);
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
  } else
    return mimeType;
}

const gchar *
gst_mpd_client_get_stream_mimeType (GstActiveStream * stream)
{
  const gchar *mimeType;

  if (stream == NULL || stream->cur_adapt_set == NULL
      || stream->cur_representation == NULL)
    return NULL;

  mimeType = stream->cur_representation->RepresentationBase->mimeType;
  if (mimeType == NULL) {
    mimeType = stream->cur_adapt_set->RepresentationBase->mimeType;
  }

  return gst_mpdparser_mimetype_to_caps (mimeType);
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
  GList *list;
  const gchar *this_mimeType = "audio";
  gchar *mimeType = NULL;
  guint nb_adapatation_set = 0;

  stream_period = gst_mpdparser_get_stream_period (client);
  g_return_val_if_fail (stream_period != NULL, 0);
  g_return_val_if_fail (stream_period->period != NULL, 0);

  for (list = g_list_first (stream_period->period->AdaptationSets); list;
      list = g_list_next (list)) {
    adapt_set = (GstAdaptationSetNode *) list->data;
    if (adapt_set) {
      gchar *this_lang = adapt_set->lang;
      GstRepresentationNode *rep;
      rep =
          gst_mpdparser_get_lowest_representation (adapt_set->Representations);
      if (rep->RepresentationBase)
        mimeType = rep->RepresentationBase->mimeType;
      if (!mimeType && adapt_set->RepresentationBase) {
        mimeType = adapt_set->RepresentationBase->mimeType;
      }

      if (strncmp_ext (mimeType, this_mimeType) == 0) {
        if (this_lang) {
          nb_adapatation_set++;
          *lang = g_list_append (*lang, this_lang);
        }
      }
    }
  }

  return nb_adapatation_set;
}


GstDateTime *
gst_mpd_client_get_next_segment_availability_end_time (GstMpdClient * client,
    GstActiveStream * stream)
{
  GstDateTime *availability_start_time, *rv;
  guint seg_idx;
  GstClockTime seg_duration;
  gint64 offset;
  GstStreamPeriod *stream_period;

  g_return_val_if_fail (client != NULL, NULL);
  g_return_val_if_fail (stream != NULL, NULL);

  stream_period = gst_mpdparser_get_stream_period (client);

  seg_idx = gst_mpd_client_get_segment_index (stream);
  seg_duration = gst_mpd_client_get_segment_duration (client, stream);
  if (seg_duration == 0)
    return NULL;
  availability_start_time = gst_mpd_client_get_availability_start_time (client);
  if (availability_start_time == NULL)
    return (GstDateTime *) NULL;

  if (stream_period && stream_period->period) {
    GstDateTime *t =
        gst_mpd_client_add_time_difference (availability_start_time,
        stream_period->period->start * 1000);
    gst_date_time_unref (availability_start_time);
    availability_start_time = t;
  }

  offset = (1 + seg_idx) * seg_duration;
  rv = gst_mpd_client_add_time_difference (availability_start_time,
      offset / GST_USECOND);
  gst_date_time_unref (availability_start_time);
  return rv;
}

gint
gst_mpd_client_check_time_position (GstMpdClient * client,
    GstActiveStream * stream, GstClockTime ts, gint64 * diff)
{
  GDateTime *now = g_date_time_new_now_utc ();
  GDateTime *start =
      gst_date_time_to_g_date_time (client->mpd_node->availabilityStartTime);
  GTimeSpan stream_now;
  GTimeSpan ts_microseconds;
  GstClockTime duration;

  g_return_val_if_fail (gst_mpd_client_is_live (client), 0);

  duration = gst_mpd_client_get_segment_duration (client, stream);
  stream_now = g_date_time_difference (now, start);
  g_date_time_unref (now);
  g_date_time_unref (start);

  /* sum duration to check if the segment is fully ready */
  ts_microseconds = (ts + duration) / GST_USECOND;

  /*
   * This functions checks if a given ts is in the 'available range' of
   * a DASH presentation. This only makes sense for live streams, which
   * are continuously adding new segments and removing old ones.
   *
   * Note: Both the client and the server should use UTC as a time reference.
   *
   * @ts is the time since the beginning of the stream and we need to find out
   * if it is currently available. The server should be hosting segments
   *
   * * ---------------- ... --- * ----------- * ---- ...
   * |
   * | past(unavailable) |      | available   | future(unavailable yet)
   * |
   * * ---------------- ... --- * ----------- * ---- ...
   * |                          |             |
   * availabilitStartTime       |             UTC now
   *                            UTC now - timeShiftBufferDepth
   *
   * This function should return 0 if @ts is in the 'available' area, 1 for
   * 'future' and '-1' for past and the corresponding distance to the
   * 'available' area is set to @diff
   *
   * TODO untested with live presentations with multiple periods as no
   * examples for it could be found/generated
   */

  if (ts_microseconds > stream_now) {
    *diff = ts_microseconds - stream_now;
    return 1;
  }
  if (client->mpd_node->timeShiftBufferDepth
      && ts_microseconds <
      stream_now - client->mpd_node->timeShiftBufferDepth) {
    *diff = ts_microseconds - stream_now;
    return -1;
  }

  *diff = 0;
  return 0;
}

gboolean
gst_mpd_client_seek_to_time (GstMpdClient * client, GDateTime * time)
{
  GDateTime *start =
      gst_date_time_to_g_date_time (client->mpd_node->availabilityStartTime);
  GTimeSpan ts_microseconds;
  GstClockTime ts;
  gboolean ret = TRUE;
  GList *stream;

  g_return_val_if_fail (gst_mpd_client_is_live (client), 0);

  ts_microseconds = g_date_time_difference (time, start);
  g_date_time_unref (start);

  ts = ts_microseconds * GST_USECOND;
  for (stream = client->active_streams; stream; stream = g_list_next (stream)) {
    ret = ret & gst_mpd_client_stream_seek (client, stream->data, ts);
  }
  return ret;
}

void
gst_media_fragment_info_clear (GstMediaFragmentInfo * fragment)
{
  g_free (fragment->uri);
  g_free (fragment->index_uri);
}
