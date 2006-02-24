/*
 * gstskeltag.c - GStreamer annodex skeleton tags
 * Copyright (C) 2005 Alessandro Decina
 * 
 * Authors:
 *   Alessandro Decina <alessandro@nnva.org>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include "gstskeltag.h"

enum
{
  ARG_0,
  GST_SKEL_TAG_FISHEAD_MAJOR,
  GST_SKEL_TAG_FISHEAD_MINOR,
  GST_SKEL_TAG_FISHEAD_PRESTIME_N,
  GST_SKEL_TAG_FISHEAD_PRESTIME_D,
  GST_SKEL_TAG_FISHEAD_BASETIME_N,
  GST_SKEL_TAG_FISHEAD_BASETIME_D,
  GST_SKEL_TAG_FISHEAD_UTC,
  GST_SKEL_TAG_FISBONE_SERIALNO,
  GST_SKEL_TAG_FISBONE_GRANULERATE_N,
  GST_SKEL_TAG_FISBONE_GRANULERATE_D,
  GST_SKEL_TAG_FISBONE_START_GRANULE,
  GST_SKEL_TAG_FISBONE_PREROLL,
  GST_SKEL_TAG_FISBONE_GRANULESHIFT,
  GST_SKEL_TAG_FISBONE_HEADERS,
  GST_SKEL_TAG_FISBONE_CONTENT_TYPE,
  GST_SKEL_TAG_FISBONE_ENCODING,
};

/* GstSkelTagFishead prototypes */
G_DEFINE_TYPE (GstSkelTagFishead, gst_skel_tag_fishead, G_TYPE_OBJECT);
static void gst_skel_tag_fishead_finalize (GObject * object);
static void gst_skel_tag_fishead_set_property (GObject * object,
    guint property_id, const GValue * value, GParamSpec * pspec);
static void gst_skel_tag_fishead_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec);

/* GstSkelTagFisbone prototypes */
G_DEFINE_TYPE (GstSkelTagFisbone, gst_skel_tag_fisbone, G_TYPE_OBJECT);
static void gst_skel_tag_fisbone_finalize (GObject * object);
static void gst_skel_tag_fisbone_set_property (GObject * object,
    guint property_id, const GValue * value, GParamSpec * pspec);
static void gst_skel_tag_fisbone_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec);

/* GstSkelTagFishead */
static void
gst_skel_tag_fishead_class_init (GstSkelTagFisheadClass * fishead_class)
{
  GObjectClass *klass = G_OBJECT_CLASS (fishead_class);

  klass->set_property = gst_skel_tag_fishead_set_property;
  klass->get_property = gst_skel_tag_fishead_get_property;
  klass->finalize = gst_skel_tag_fishead_finalize;

  g_object_class_install_property (klass, GST_SKEL_TAG_FISHEAD_MAJOR,
      g_param_spec_int ("version-major",
          "Major version number",
          "Major number of the skeleton bitstream",
          0, 0, 0, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

  g_object_class_install_property (klass, GST_SKEL_TAG_FISHEAD_MINOR,
      g_param_spec_int ("version-minor",
          "Minor version number",
          "Minor number of the skeleton bitstream",
          0, 0, 0, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

  g_object_class_install_property (klass, GST_SKEL_TAG_FISHEAD_PRESTIME_N,
      g_param_spec_int64 ("presentation-time-numerator",
          "Presentation time numerator",
          "Stream presentation time numerator",
          0, 0, 0, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));


  g_object_class_install_property (klass, GST_SKEL_TAG_FISHEAD_PRESTIME_D,
      g_param_spec_int64 ("presentation-time-denominator",
          "Presentation time denominator",
          "Stream presentation time denominator",
          0, 0, 0, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

  g_object_class_install_property (klass, GST_SKEL_TAG_FISHEAD_BASETIME_N,
      g_param_spec_int64 ("base-time-numerator",
          "Basetime numerator",
          "Stream base time numerator",
          0, 0, 0, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

  g_object_class_install_property (klass, GST_SKEL_TAG_FISHEAD_BASETIME_D,
      g_param_spec_int64 ("base-time-denominator",
          "Base time denominator",
          "Stream base time denominator",
          0, 0, 0, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

  g_object_class_install_property (klass, GST_SKEL_TAG_FISHEAD_UTC,
      g_param_spec_string ("calendar-base-time",
          "Calendar base time",
          "Date and all-clock time (expressed as UTC in the format "
          "YYYYMMDDTHHMMSS.sssZ) associated with the base time",
          NULL, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
}

static void
gst_skel_tag_fishead_init (GstSkelTagFishead * fishead)
{
}

static void
gst_skel_tag_fishead_finalize (GObject * object)
{
  GObjectClass *parent_class =
      G_OBJECT_CLASS (gst_skel_tag_fishead_parent_class);
  GstSkelTagFishead *fishead = GST_SKEL_TAG_FISHEAD (object);

  g_free (fishead->utc);

  GST_CALL_PARENT (G_OBJECT_CLASS, finalize, (object));
}

static void
gst_skel_tag_fishead_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GstSkelTagFishead *fishead = GST_SKEL_TAG_FISHEAD (object);

  switch (property_id) {
    case GST_SKEL_TAG_FISHEAD_MAJOR:
      fishead->major = g_value_get_int (value);
      break;
    case GST_SKEL_TAG_FISHEAD_MINOR:
      fishead->minor = g_value_get_int (value);
      break;
    case GST_SKEL_TAG_FISHEAD_PRESTIME_N:
      fishead->prestime_n = g_value_get_int64 (value);
      break;
    case GST_SKEL_TAG_FISHEAD_PRESTIME_D:
      fishead->prestime_d = g_value_get_int64 (value);
      break;
    case GST_SKEL_TAG_FISHEAD_BASETIME_N:
      fishead->basetime_n = g_value_get_int64 (value);
      break;
    case GST_SKEL_TAG_FISHEAD_BASETIME_D:
      fishead->basetime_d = g_value_get_int64 (value);
      break;
    case GST_SKEL_TAG_FISHEAD_UTC:
      g_free (fishead->utc);
      fishead->utc = g_value_dup_string (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
gst_skel_tag_fishead_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GstSkelTagFishead *fishead = GST_SKEL_TAG_FISHEAD (object);

  switch (property_id) {
    case GST_SKEL_TAG_FISHEAD_MAJOR:
      g_value_set_int (value, fishead->major);
      break;
    case GST_SKEL_TAG_FISHEAD_MINOR:
      g_value_set_int (value, fishead->minor);
      break;
    case GST_SKEL_TAG_FISHEAD_BASETIME_N:
      g_value_set_int64 (value, fishead->basetime_n);
      break;
    case GST_SKEL_TAG_FISHEAD_BASETIME_D:
      g_value_set_int64 (value, fishead->basetime_d);
      break;
    case GST_SKEL_TAG_FISHEAD_PRESTIME_N:
      g_value_set_int64 (value, fishead->prestime_n);
      break;
    case GST_SKEL_TAG_FISHEAD_PRESTIME_D:
      g_value_set_int64 (value, fishead->prestime_d);
      break;
    case GST_SKEL_TAG_FISHEAD_UTC:
      g_value_set_string (value, fishead->utc);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

/* GstSkelTagFisbone code */

static void
gst_skel_tag_fisbone_class_init (GstSkelTagFisboneClass * fisbone_class)
{
  GObjectClass *klass = G_OBJECT_CLASS (fisbone_class);

  klass->set_property = gst_skel_tag_fisbone_set_property;
  klass->get_property = gst_skel_tag_fisbone_get_property;
  klass->finalize = gst_skel_tag_fisbone_finalize;

  g_object_class_install_property (klass, GST_SKEL_TAG_FISBONE_SERIALNO,
      g_param_spec_uint ("serial-number",
          "Serial number",
          "Serial number of the logical bitstream",
          0, 0, 0, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

  g_object_class_install_property (klass, GST_SKEL_TAG_FISBONE_GRANULERATE_N,
      g_param_spec_int64 ("granule-rate-numerator",
          "Granulerate numerator",
          "Granulerate numerator",
          0, 0, 0, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

  g_object_class_install_property (klass, GST_SKEL_TAG_FISBONE_GRANULERATE_D,
      g_param_spec_int64 ("granule-rate-denominator",
          "Granulerate denominator",
          "Granulerate denominator",
          0, 0, 0, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

  g_object_class_install_property (klass, GST_SKEL_TAG_FISBONE_START_GRANULE,
      g_param_spec_int64 ("granule-start",
          "Start granule",
          "The granule number with which this logical bitstream starts",
          0, 0, 0, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

  g_object_class_install_property (klass, GST_SKEL_TAG_FISBONE_PREROLL,
      g_param_spec_uint64 ("preroll",
          "The number of packets to preroll",
          "The number of packets to preroll to decode a packet correctly",
          0, 0, 0, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

  g_object_class_install_property (klass, GST_SKEL_TAG_FISBONE_GRANULESHIFT,
      g_param_spec_uint ("granule-shift",
          "Granuleshift",
          "The number of lower bits to use for partitioning a granule position",
          0, 0, 0, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

  g_object_class_install_property (klass, GST_SKEL_TAG_FISBONE_HEADERS,
      g_param_spec_boxed ("headers",
          "Message header fields",
          "RFC2822 header fields describing a logical bitstream",
          G_TYPE_VALUE_ARRAY, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

  g_object_class_install_property (klass, GST_SKEL_TAG_FISBONE_CONTENT_TYPE,
      g_param_spec_string ("content-type",
          "Content type",
          "Bitstream content type",
          NULL, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

  g_object_class_install_property (klass, GST_SKEL_TAG_FISBONE_ENCODING,
      g_param_spec_string ("encoding",
          "Encoding",
          "Bitstream encoding", NULL, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
}

static void
gst_skel_tag_fisbone_init (GstSkelTagFisbone * fisbone)
{
}

static void
gst_skel_tag_fisbone_finalize (GObject * object)
{
  GObjectClass *parent_class =
      G_OBJECT_CLASS (gst_skel_tag_fishead_parent_class);
  GstSkelTagFisbone *fisbone = GST_SKEL_TAG_FISBONE (object);

  g_free (fisbone->content_type);
  g_free (fisbone->encoding);
  g_value_array_free (fisbone->headers);

  GST_CALL_PARENT (G_OBJECT_CLASS, finalize, (object));
}

static void
gst_skel_tag_fisbone_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GstSkelTagFisbone *fisbone = GST_SKEL_TAG_FISBONE (object);

  switch (property_id) {
    case GST_SKEL_TAG_FISBONE_SERIALNO:
      fisbone->serialno = g_value_get_uint (value);
      break;
    case GST_SKEL_TAG_FISBONE_GRANULERATE_N:
      fisbone->granulerate_n = g_value_get_int64 (value);
      break;
    case GST_SKEL_TAG_FISBONE_GRANULERATE_D:
      fisbone->granulerate_d = g_value_get_int64 (value);
      break;
    case GST_SKEL_TAG_FISBONE_START_GRANULE:
      fisbone->start_granule = g_value_get_int64 (value);
      break;
    case GST_SKEL_TAG_FISBONE_PREROLL:
      fisbone->preroll = g_value_get_uint64 (value);
      break;
    case GST_SKEL_TAG_FISBONE_GRANULESHIFT:
      fisbone->granuleshift = g_value_get_uint (value);
      break;
    case GST_SKEL_TAG_FISBONE_HEADERS:
    {
      GValueArray *va;

      va = g_value_get_boxed (value);
      if (fisbone->headers)
        g_value_array_free (fisbone->headers);
      fisbone->headers = va != NULL ? g_value_array_copy (va) : NULL;
      break;
    }
    case GST_SKEL_TAG_FISBONE_CONTENT_TYPE:
      g_free (fisbone->content_type);
      fisbone->content_type = g_value_dup_string (value);
      break;
    case GST_SKEL_TAG_FISBONE_ENCODING:
      g_free (fisbone->encoding);
      fisbone->encoding = g_value_dup_string (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
gst_skel_tag_fisbone_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GstSkelTagFisbone *fisbone = GST_SKEL_TAG_FISBONE (object);

  switch (property_id) {
    case GST_SKEL_TAG_FISBONE_SERIALNO:
      g_value_set_uint (value, fisbone->serialno);
      break;
    case GST_SKEL_TAG_FISBONE_GRANULERATE_N:
      g_value_set_int64 (value, fisbone->granulerate_n);
      break;
    case GST_SKEL_TAG_FISBONE_GRANULERATE_D:
      g_value_set_int64 (value, fisbone->granulerate_d);
      break;
    case GST_SKEL_TAG_FISBONE_START_GRANULE:
      g_value_set_int64 (value, fisbone->start_granule);
      break;
    case GST_SKEL_TAG_FISBONE_GRANULESHIFT:
      g_value_set_uint (value, fisbone->granuleshift);
      break;
    case GST_SKEL_TAG_FISBONE_PREROLL:
      g_value_set_uint64 (value, fisbone->preroll);
      break;
    case GST_SKEL_TAG_FISBONE_HEADERS:
      g_value_set_boxed (value, fisbone->headers);
      break;
    case GST_SKEL_TAG_FISBONE_CONTENT_TYPE:
      g_value_set_string (value, fisbone->content_type);
      break;
    case GST_SKEL_TAG_FISBONE_ENCODING:
      g_value_set_string (value, fisbone->encoding);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}
