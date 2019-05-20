/*
 * DASH MPD parsing library
 *
 * gstmpdparser.h
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

#ifndef __GST_MPDPARSER_H__
#define __GST_MPDPARSER_H__

#include <gst/gst.h>
#include <gst/uridownloader/gsturidownloader.h>
#include <gst/base/gstadapter.h>
#include "gstmpdhelper.h"

G_BEGIN_DECLS

typedef struct _GstActiveStream           GstActiveStream;
typedef struct _GstStreamPeriod           GstStreamPeriod;
typedef struct _GstMediaFragmentInfo      GstMediaFragmentInfo;
typedef struct _GstMediaSegment           GstMediaSegment;
typedef struct _GstMPDNode                GstMPDNode;
typedef struct _GstPeriodNode             GstPeriodNode;
typedef struct _GstRepresentationBaseType GstRepresentationBaseType;
typedef struct _GstDescriptorType         GstDescriptorType;
typedef struct _GstContentComponentNode   GstContentComponentNode;
typedef struct _GstAdaptationSetNode      GstAdaptationSetNode;
typedef struct _GstRepresentationNode     GstRepresentationNode;
typedef struct _GstSubRepresentationNode  GstSubRepresentationNode;
typedef struct _GstSegmentListNode        GstSegmentListNode;
typedef struct _GstSegmentTemplateNode    GstSegmentTemplateNode;
typedef struct _GstSegmentURLNode         GstSegmentURLNode;
typedef struct _GstBaseURL                GstBaseURL;

typedef struct _GstSubsetNode             GstSubsetNode;
typedef struct _GstProgramInformationNode GstProgramInformationNode;
typedef struct _GstMetricsRangeNode       GstMetricsRangeNode;
typedef struct _GstMetricsNode            GstMetricsNode;
typedef struct _GstUTCTimingNode          GstUTCTimingNode;
typedef struct _GstSNode                  GstSNode;
typedef struct _GstSegmentTimelineNode    GstSegmentTimelineNode;
typedef struct _GstSegmentBaseType        GstSegmentBaseType;
typedef struct _GstURLType                GstURLType;
typedef struct _GstMultSegmentBaseType    GstMultSegmentBaseType;


#define GST_MPD_DURATION_NONE ((guint64)-1)

typedef enum
{
  GST_STREAM_UNKNOWN,
  GST_STREAM_VIDEO,           /* video stream (the main one) */
  GST_STREAM_AUDIO,           /* audio stream (optional) */
  GST_STREAM_APPLICATION      /* application stream (optional): for timed text/subtitles */
} GstStreamMimeType;


typedef enum
{
  GST_XLINK_ACTUATE_ON_REQUEST,
  GST_XLINK_ACTUATE_ON_LOAD
} GstXLinkActuate;

typedef enum
{
  GST_MPD_UTCTIMING_TYPE_UNKNOWN     = 0x00,
  GST_MPD_UTCTIMING_TYPE_NTP         = 0x01,
  GST_MPD_UTCTIMING_TYPE_SNTP        = 0x02,
  GST_MPD_UTCTIMING_TYPE_HTTP_HEAD   = 0x04,
  GST_MPD_UTCTIMING_TYPE_HTTP_XSDATE = 0x08,
  GST_MPD_UTCTIMING_TYPE_HTTP_ISO    = 0x10,
  GST_MPD_UTCTIMING_TYPE_HTTP_NTP    = 0x20,
  GST_MPD_UTCTIMING_TYPE_DIRECT      = 0x40
} GstMPDUTCTimingType;

struct _GstBaseURL
{
  gchar *baseURL;
  gchar *serviceLocation;
  gchar *byteRange;
};

struct _GstSNode
{
  guint64 t;
  guint64 d;
  gint r;
};

struct _GstSegmentTimelineNode
{
  /* list of S nodes */
  GQueue S;
};

struct _GstURLType
{
  gchar *sourceURL;
  GstXMLRange *range;
};

struct _GstSegmentBaseType
{
  guint timescale;
  guint64 presentationTimeOffset;
  GstXMLRange *indexRange;
  gboolean indexRangeExact;
  /* Initialization node */
  GstURLType *Initialization;
  /* RepresentationIndex node */
  GstURLType *RepresentationIndex;
};

struct _GstMultSegmentBaseType
{
  guint duration;                  /* in seconds */
  guint startNumber;
  /* SegmentBaseType extension */
  GstSegmentBaseType *SegBaseType;
  /* SegmentTimeline node */
  GstSegmentTimelineNode *SegmentTimeline;
  /* BitstreamSwitching node */
  GstURLType *BitstreamSwitching;
};

struct _GstSegmentListNode
{
  /* extension */
  GstMultSegmentBaseType *MultSegBaseType;
  /* list of SegmentURL nodes */
  GList *SegmentURL;

  gchar *xlink_href;
  GstXLinkActuate actuate;
};

struct _GstSegmentTemplateNode
{
  /* extension */
  GstMultSegmentBaseType *MultSegBaseType;
  gchar *media;
  gchar *index;
  gchar *initialization;
  gchar *bitstreamSwitching;
};

struct _GstSegmentURLNode
{
  gchar *media;
  GstXMLRange *mediaRange;
  gchar *index;
  GstXMLRange *indexRange;
};

struct _GstRepresentationBaseType
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
};

struct _GstSubRepresentationNode
{
  /* RepresentationBase extension */
  GstRepresentationBaseType *RepresentationBase;
  guint level;
  guint *dependencyLevel;            /* UIntVectorType */
  guint size;                        /* size of "dependencyLevel" array */
  guint bandwidth;
  gchar **contentComponent;          /* StringVectorType */
};

struct _GstRepresentationNode
{
  gchar *id;
  guint bandwidth;
  guint qualityRanking;
  gchar **dependencyId;              /* StringVectorType */
  gchar **mediaStreamStructureId;    /* StringVectorType */
  /* RepresentationBase extension */
  GstRepresentationBaseType *RepresentationBase;
  /* list of BaseURL nodes */
  GList *BaseURLs;
  /* list of SubRepresentation nodes */
  GList *SubRepresentations;
  /* SegmentBase node */
  GstSegmentBaseType *SegmentBase;
  /* SegmentTemplate node */
  GstSegmentTemplateNode *SegmentTemplate;
  /* SegmentList node */
  GstSegmentListNode *SegmentList;
};

struct _GstDescriptorType
{
  gchar *schemeIdUri;
  gchar *value;
};

struct _GstContentComponentNode
{
  guint id;
  gchar *lang;                      /* LangVectorType RFC 5646 */
  gchar *contentType;
  GstXMLRatio *par;
  /* list of Accessibility DescriptorType nodes */
  GList *Accessibility;
  /* list of Role DescriptorType nodes */
  GList *Role;
  /* list of Rating DescriptorType nodes */
  GList *Rating;
  /* list of Viewpoint DescriptorType nodes */
  GList *Viewpoint;
};

struct _GstAdaptationSetNode
{
  guint id;
  guint group;
  gchar *lang;                      /* LangVectorType RFC 5646 */
  gchar *contentType;
  GstXMLRatio *par;
  guint minBandwidth;
  guint maxBandwidth;
  guint minWidth;
  guint maxWidth;
  guint minHeight;
  guint maxHeight;
  GstXMLConditionalUintType *segmentAlignment;
  GstXMLConditionalUintType *subsegmentAlignment;
  GstMPDSAPType subsegmentStartsWithSAP;
  gboolean bitstreamSwitching;
  /* list of Accessibility DescriptorType nodes */
  GList *Accessibility;
  /* list of Role DescriptorType nodes */
  GList *Role;
  /* list of Rating DescriptorType nodes */
  GList *Rating;
  /* list of Viewpoint DescriptorType nodes */
  GList *Viewpoint;
  /* RepresentationBase extension */
  GstRepresentationBaseType *RepresentationBase;
  /* SegmentBase node */
  GstSegmentBaseType *SegmentBase;
  /* SegmentList node */
  GstSegmentListNode *SegmentList;
  /* SegmentTemplate node */
  GstSegmentTemplateNode *SegmentTemplate;
  /* list of BaseURL nodes */
  GList *BaseURLs;
  /* list of Representation nodes */
  GList *Representations;
  /* list of ContentComponent nodes */
  GList *ContentComponents;

  gchar *xlink_href;
  GstXLinkActuate actuate;
};

struct _GstSubsetNode
{
  guint *contains;                   /* UIntVectorType */
  guint size;                        /* size of the "contains" array */
};

struct _GstPeriodNode
{
  gchar *id;
  guint64 start;                     /* [ms] */
  guint64 duration;                  /* [ms] */
  gboolean bitstreamSwitching;
  /* SegmentBase node */
  GstSegmentBaseType *SegmentBase;
  /* SegmentList node */
  GstSegmentListNode *SegmentList;
  /* SegmentTemplate node */
  GstSegmentTemplateNode *SegmentTemplate;
  /* list of Adaptation Set nodes */
  GList *AdaptationSets;
  /* list of Representation nodes */
  GList *Subsets;
  /* list of BaseURL nodes */
  GList *BaseURLs;

  gchar *xlink_href;
  GstXLinkActuate actuate;
};

struct _GstProgramInformationNode
{
  gchar *lang;                      /* LangVectorType RFC 5646 */
  gchar *moreInformationURL;
  /* children nodes */
  gchar *Title;
  gchar *Source;
  gchar *Copyright;
};

struct _GstMetricsRangeNode
{
  guint64 starttime;                 /* [ms] */
  guint64 duration;                  /* [ms] */
};

struct _GstMetricsNode
{
  gchar *metrics;
  /* list of Metrics Range nodes */
  GList *MetricsRanges;
  /* list of Reporting nodes */
  GList *Reportings;
};

struct _GstUTCTimingNode {
  GstMPDUTCTimingType method;
  /* NULL terminated array of strings */
  gchar **urls;
};

struct _GstMPDNode
{
  gchar *default_namespace;
  gchar *namespace_xsi;
  gchar *namespace_ext;
  gchar *schemaLocation;
  gchar *id;
  gchar *profiles;
  GstMPDFileType type;
  GstDateTime *availabilityStartTime;
  GstDateTime *availabilityEndTime;
  guint64 mediaPresentationDuration;  /* [ms] */
  guint64 minimumUpdatePeriod;        /* [ms] */
  guint64 minBufferTime;              /* [ms] */
  guint64 timeShiftBufferDepth;       /* [ms] */
  guint64 suggestedPresentationDelay; /* [ms] */
  guint64 maxSegmentDuration;         /* [ms] */
  guint64 maxSubsegmentDuration;      /* [ms] */
  /* list of BaseURL nodes */
  GList *BaseURLs;
  /* list of Location nodes */
  GList *Locations;
  /* List of ProgramInformation nodes */
  GList *ProgramInfo;
  /* list of Periods nodes */
  GList *Periods;
  /* list of Metrics nodes */
  GList *Metrics;
  /* list of GstUTCTimingNode nodes */
  GList *UTCTiming;
};

/**
 * GstStreamPeriod:
 *
 * Stream period data structure
 */
struct _GstStreamPeriod
{
  GstPeriodNode *period;                      /* Stream period */
  guint number;                               /* Period number */
  GstClockTime start;                         /* Period start time */
  GstClockTime duration;                      /* Period duration */
};

/**
 * GstMediaSegment:
 *
 * Media segment data structure
 */
struct _GstMediaSegment
{
  GstSegmentURLNode *SegmentURL;              /* this is NULL when using a SegmentTemplate */
  guint number;                               /* segment number */
  gint repeat;                                /* number of extra repetitions (0 = played only once) */
  guint64 scale_start;                        /* start time in timescale units */
  guint64 scale_duration;                     /* duration in timescale units */
  GstClockTime start;                         /* segment start time */
  GstClockTime duration;                      /* segment duration */
};

struct _GstMediaFragmentInfo
{
  gchar *uri;
  gint64 range_start;
  gint64 range_end;

  gchar *index_uri;
  gint64 index_range_start;
  gint64 index_range_end;

  gboolean discontinuity;
  GstClockTime timestamp;
  GstClockTime duration;
};

/**
 * GstActiveStream:
 *
 * Active stream data structure
 */
struct _GstActiveStream
{
  GstStreamMimeType mimeType;                 /* video/audio/application */

  guint baseURL_idx;                          /* index of the baseURL used for last request */
  gchar *baseURL;                             /* active baseURL used for last request */
  gchar *queryURL;                            /* active queryURL used for last request */
  guint max_bandwidth;                        /* max bandwidth allowed for this mimeType */

  GstAdaptationSetNode *cur_adapt_set;        /* active adaptation set */
  gint representation_idx;                    /* index of current representation */
  GstRepresentationNode *cur_representation;  /* active representation */
  GstSegmentBaseType *cur_segment_base;       /* active segment base */
  GstSegmentListNode *cur_segment_list;       /* active segment list */
  GstSegmentTemplateNode *cur_seg_template;   /* active segment template */
  gint segment_index;                         /* index of next sequence chunk */
  guint segment_repeat_index;                 /* index of the repeat count of a segment */
  GPtrArray *segments;                        /* array of GstMediaSegment */
  GstClockTime presentationTimeOffset;        /* presentation time offset of the current segment */
};

/* MPD file parsing */
gboolean gst_mpdparser_get_mpd_node (GstMPDNode ** mpd_node, const gchar * data, gint size);
GstSegmentListNode * gst_mpdparser_get_external_segment_list (const gchar * data, gint size, GstSegmentListNode * parent);
GList * gst_mpdparser_get_external_periods (const gchar * data, gint size);
GList * gst_mpdparser_get_external_adaptation_sets (const gchar * data, gint size, GstPeriodNode* period);

/* navigation functions */
GstStreamMimeType gst_mpdparser_representation_get_mimetype (GstAdaptationSetNode * adapt_set, GstRepresentationNode * rep);

/* Memory management */
void gst_mpdparser_free_mpd_node (GstMPDNode * mpd_node);
void gst_mpdparser_free_period_node (GstPeriodNode * period_node);
void gst_mpdparser_free_adaptation_set_node (GstAdaptationSetNode * adaptation_set_node);
void gst_mpdparser_free_segment_list_node (GstSegmentListNode * segment_list_node);
void gst_mpdparser_free_stream_period (GstStreamPeriod * stream_period);
void gst_mpdparser_free_media_segment (GstMediaSegment * media_segment);
void gst_mpdparser_free_active_stream (GstActiveStream * active_stream);
void gst_mpdparser_free_base_url_node (GstBaseURL * base_url_node);

/* Active stream methods*/
void gst_mpdparser_init_active_stream_segments (GstActiveStream * stream);
gchar *gst_mpdparser_get_mediaURL (GstActiveStream * stream, GstSegmentURLNode * segmentURL);
const gchar *gst_mpdparser_get_initializationURL (GstActiveStream * stream, GstURLType * InitializationURL);

/*Helper methods */
gchar *gst_mpdparser_build_URL_from_template (const gchar * url_template, const gchar * id, guint number, guint bandwidth, guint64 time);
const gchar *gst_mpdparser_mimetype_to_caps (const gchar * mimeType);
GstUri *combine_urls (GstUri * base, GList * list, gchar ** query, guint idx);
int strncmp_ext (const char *s1, const char *s2);
G_END_DECLS

#endif /* __GST_MPDPARSER_H__ */

