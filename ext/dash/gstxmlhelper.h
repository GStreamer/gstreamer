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
#ifndef __GST_XMLHELPER_H__
#define __GST_XMLHELPER_H__

#include <libxml/parser.h>
#include <libxml/tree.h>
#include <gst/gst.h>

G_BEGIN_DECLS

typedef struct _GstXMLRange                  GstXMLRange;
typedef struct _GstXMLRatio                  GstXMLRatio;
typedef struct _GstXMLFrameRate              GstXMLFrameRate;
typedef struct _GstXMLConditionalUintType    GstXMLConditionalUintType;

struct _GstXMLRange
{
  guint64 first_byte_pos;
  guint64 last_byte_pos;
};

struct _GstXMLRatio
{
  guint num;
  guint den;
};

struct _GstXMLFrameRate
{
  guint num;
  guint den;
};

struct _GstXMLConditionalUintType
{
  gboolean flag;
  guint value;
};

GstXMLRange *gst_xml_helper_clone_range (GstXMLRange * range);

/* XML property get method */
gboolean gst_xml_helper_get_prop_validated_string (xmlNode * a_node,
    const gchar * property_name, gchar ** property_value,
    gboolean (*validator) (const char *));
gboolean gst_xml_helper_get_prop_string (xmlNode * a_node,
    const gchar * property_name, gchar ** property_value);
gboolean gst_xml_helper_get_prop_string_stripped (xmlNode * a_node,
    const gchar * property_name, gchar ** property_value);
gboolean gst_xml_helper_get_ns_prop_string (xmlNode * a_node,
    const gchar * ns_name, const gchar * property_name,
    gchar ** property_value);
gboolean gst_xml_helper_get_prop_string_vector_type (xmlNode * a_node,
    const gchar * property_name, gchar *** property_value);
gboolean gst_xml_helper_get_prop_signed_integer (xmlNode * a_node,
    const gchar * property_name, gint default_val, gint * property_value);
gboolean gst_xml_helper_get_prop_unsigned_integer (xmlNode * a_node,
    const gchar * property_name, guint default_val, guint * property_value);
gboolean gst_xml_helper_get_prop_unsigned_integer_64 (xmlNode *
    a_node, const gchar * property_name, guint64 default_val,
    guint64 * property_value);
gboolean gst_xml_helper_get_prop_uint_vector_type (xmlNode * a_node,
    const gchar * property_name, guint ** property_value, guint * value_size);
gboolean gst_xml_helper_get_prop_double (xmlNode * a_node,
    const gchar * property_name, gdouble * property_value);
gboolean gst_xml_helper_get_prop_boolean (xmlNode * a_node,
    const gchar * property_name, gboolean default_val,
    gboolean * property_value);
gboolean gst_xml_helper_get_prop_range (xmlNode * a_node,
    const gchar * property_name, GstXMLRange ** property_value);
gboolean gst_xml_helper_get_prop_ratio (xmlNode * a_node,
    const gchar * property_name, GstXMLRatio ** property_value);
gboolean gst_xml_helper_get_prop_framerate (xmlNode * a_node,
    const gchar * property_name, GstXMLFrameRate ** property_value);
gboolean gst_xml_helper_get_prop_cond_uint (xmlNode * a_node,
    const gchar * property_name, GstXMLConditionalUintType ** property_value);
gboolean gst_xml_helper_get_prop_dateTime (xmlNode * a_node,
    const gchar * property_name, GstDateTime ** property_value);
gboolean gst_xml_helper_get_prop_duration (xmlNode * a_node,
    const gchar * property_name, guint64 default_value,
    guint64 * property_value);
gboolean gst_xml_helper_get_prop_string_no_whitespace (xmlNode * a_node,
    const gchar * property_name, gchar ** property_value);

/* XML node get method */
gboolean gst_xml_helper_get_node_content (xmlNode * a_node,
    gchar ** content);
gchar *gst_xml_helper_get_node_namespace (xmlNode * a_node,
    const gchar * prefix);
gboolean gst_xml_helper_get_node_as_string (xmlNode * a_node,
    gchar ** content);

G_END_DECLS
#endif /* __GST_XMLHELPER_H__ */
