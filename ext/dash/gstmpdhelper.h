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
#ifndef __GST_MPDHELPER_H__
#define __GST_MPDHELPER_H__

#include "gstxmlhelper.h"
#include "gstmpdsegmenttimelinenode.h"

G_BEGIN_DECLS

typedef enum
{
  GST_MPD_FILE_TYPE_STATIC,
  GST_MPD_FILE_TYPE_DYNAMIC
} GstMPDFileType;

typedef enum
{
  GST_SAP_TYPE_0 = 0,
  GST_SAP_TYPE_1,
  GST_SAP_TYPE_2,
  GST_SAP_TYPE_3,
  GST_SAP_TYPE_4,
  GST_SAP_TYPE_5,
  GST_SAP_TYPE_6
} GstMPDSAPType;

typedef enum
{
  GST_MPD_XLINK_ACTUATE_ON_REQUEST,
  GST_MPD_XLINK_ACTUATE_ON_LOAD
} GstMPDXLinkActuate;

typedef struct _GstMPDURLType
{
  gchar *sourceURL;
  GstXMLRange *range;
} GstMPDURLType;

typedef struct _GstMPDDescriptorType
{
  gchar *schemeIdUri;
  gchar *value;
} GstMPDDescriptorType;

typedef struct _GstMPDSegmentBaseType
{
  guint timescale;
  guint64 presentationTimeOffset;
  GstXMLRange *indexRange;
  gboolean indexRangeExact;
  /* Initialization node */
  GstMPDURLType *Initialization;
  /* RepresentationIndex node */
  GstMPDURLType *RepresentationIndex;
} GstMPDSegmentBaseType;

typedef struct _GstMPDMultSegmentBaseType
{
  guint duration;                  /* in seconds */
  guint startNumber;
  /* SegmentBaseType extension */
  GstMPDSegmentBaseType *SegBaseType;
  /* SegmentTimeline node */
  GstMPDSegmentTimelineNode *SegmentTimeline;
  /* BitstreamSwitching node */
  GstMPDURLType *BitstreamSwitching;
} GstMPDMultSegmentBaseType;

typedef struct _GstMPDRepresentationBaseType
{
  gchar *profiles;
  guint width;
  guint height;
  GstXMLRatio *sar;
  GstXMLFrameRate *minFrameRate;
  GstXMLFrameRate *maxFrameRate;
  GstXMLFrameRate *frameRate;
  gchar *audioSamplingRate;
  gchar *mimeType;
  gchar *segmentProfiles;
  gchar *codecs;
  gdouble maximumSAPPeriod;
  GstMPDSAPType startWithSAP;
  gdouble maxPlayoutRate;
  gboolean codingDependency;
  gchar *scanType;
  /* list of FramePacking DescriptorType nodes */
  GList *FramePacking;
  /* list of AudioChannelConfiguration DescriptorType nodes */
  GList *AudioChannelConfiguration;
  /* list of ContentProtection DescriptorType nodes */
  GList *ContentProtection;
} GstMPDRepresentationBaseType;

gboolean gst_mpd_helper_get_mpd_type (xmlNode * a_node, const gchar * property_name, GstMPDFileType * property_value);
gboolean gst_mpd_helper_get_SAP_type (xmlNode * a_node, const gchar * property_name, GstMPDSAPType * property_value);

GstMPDURLType *gst_mpd_helper_URLType_clone (GstMPDURLType * url);
void gst_mpd_helper_url_type_node_free (GstMPDURLType * url_type_node);
void gst_mpd_helper_descriptor_type_free (GstMPDDescriptorType *
    descriptor_type);
void gst_mpd_helper_segment_base_type_free (GstMPDSegmentBaseType * seg_base_type);
void gst_mpd_helper_mult_seg_base_type_free (GstMPDMultSegmentBaseType *
    mult_seg_base_type);
void
gst_mpd_helper_representation_base_type_free (GstMPDRepresentationBaseType *
    representation_base);

const gchar * gst_mpd_helper_mimetype_to_caps (const gchar * mimeType);
GstUri *gst_mpd_helper_combine_urls (GstUri * base, GList * list, gchar ** query, guint idx);
int gst_mpd_helper_strncmp_ext (const char *s1, const char *s2);

G_END_DECLS
#endif /* __GST_MPDHELPER_H__ */
