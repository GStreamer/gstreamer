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

G_BEGIN_DECLS

typedef struct _GstMpdClient              GstMpdClient;
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
typedef struct _GstRange                  GstRange;
typedef struct _GstRatio                  GstRatio;
typedef struct _GstFrameRate              GstFrameRate;
typedef struct _GstConditionalUintType    GstConditionalUintType;
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

#define GST_MPD_CLIENT_LOCK(c) g_mutex_lock (&c->lock);
#define GST_MPD_CLIENT_UNLOCK(c) g_mutex_unlock (&c->lock);

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
} GstSAPType;

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

struct _GstRange
{
  guint64 first_byte_pos;
  guint64 last_byte_pos;
};

struct _GstRatio
{
  guint num;
  guint den;
};

struct _GstFrameRate
{
  guint num;
  guint den;
};

struct _GstConditionalUintType
{
  gboolean flag;
  guint value;
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
  GstRange *range;
};

struct _GstSegmentBaseType
{
  guint timescale;
  guint64 presentationTimeOffset;
  GstRange *indexRange;
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
  GstRange *mediaRange;
  gchar *index;
  GstRange *indexRange;
};

struct _GstRepresentationBaseType
{
  gchar *profiles;
  guint width;
  guint height;
  GstRatio *sar;
  GstFrameRate *minFrameRate;
  GstFrameRate *maxFrameRate;
  GstFrameRate *frameRate;
  gchar *audioSamplingRate;
  gchar *mimeType;
  gchar *segmentProfiles;
  gchar *codecs;
  gdouble maximumSAPPeriod;
  GstSAPType startWithSAP;
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
  GstRatio *par;
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
  GstRatio *par;
  guint minBandwidth;
  guint maxBandwidth;
  guint minWidth;
  guint maxWidth;
  guint minHeight;
  guint maxHeight;
  GstConditionalUintType *segmentAlignment;
  GstConditionalUintType *subsegmentAlignment;
  GstSAPType subsegmentStartsWithSAP;
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

struct _GstMpdClient
{
  GstMPDNode *mpd_node;                       /* active MPD manifest file */

  GList *periods;                             /* list of GstStreamPeriod */
  guint period_idx;                           /* index of current Period */

  GList *active_streams;                      /* list of GstActiveStream */

  guint update_failed_count;
  gchar *mpd_uri;                             /* manifest file URI */
  gchar *mpd_base_uri;                        /* base URI for resolving relative URIs.
                                               * this will be different for redirects */

  /* profiles */
  gboolean profile_isoff_ondemand;

  GstUriDownloader * downloader;
};

/* Basic initialization/deinitialization functions */
GstMpdClient *gst_mpd_client_new (void);
void gst_active_streams_free (GstMpdClient * client);
void gst_mpd_client_free (GstMpdClient * client);
void gst_media_fragment_info_clear (GstMediaFragmentInfo * fragment);

void gst_mpd_client_set_uri_downloader (GstMpdClient * client, GstUriDownloader * download);

/* MPD file parsing */
gboolean gst_mpd_parse (GstMpdClient *client, const gchar *data, gint size);

/* Streaming management */
gboolean gst_mpd_client_setup_media_presentation (GstMpdClient *client, GstClockTime time, gint period_index, const gchar *period_id);
gboolean gst_mpd_client_setup_streaming (GstMpdClient * client, GstAdaptationSetNode * adapt_set);
gboolean gst_mpd_client_setup_representation (GstMpdClient *client, GstActiveStream *stream, GstRepresentationNode *representation);
GstClockTime gst_mpd_client_get_next_fragment_duration (GstMpdClient * client, GstActiveStream * stream);
GstClockTime gst_mpd_client_get_media_presentation_duration (GstMpdClient *client);
GstClockTime gst_mpd_client_get_maximum_segment_duration (GstMpdClient * client);
gboolean gst_mpd_client_get_last_fragment_timestamp_end (GstMpdClient * client, guint stream_idx, GstClockTime * ts);
gboolean gst_mpd_client_get_next_fragment_timestamp (GstMpdClient * client, guint stream_idx, GstClockTime * ts);
gboolean gst_mpd_client_get_next_fragment (GstMpdClient *client, guint indexStream, GstMediaFragmentInfo * fragment);
gboolean gst_mpd_client_get_next_header (GstMpdClient *client, gchar **uri, guint stream_idx, gint64 * range_start, gint64 * range_end);
gboolean gst_mpd_client_get_next_header_index (GstMpdClient *client, gchar **uri, guint stream_idx, gint64 * range_start, gint64 * range_end);
gboolean gst_mpd_client_is_live (GstMpdClient * client);
gboolean gst_mpd_client_stream_seek (GstMpdClient * client, GstActiveStream * stream, gboolean forward, GstSeekFlags flags, GstClockTime ts, GstClockTime * final_ts);
gboolean gst_mpd_client_seek_to_time (GstMpdClient * client, GDateTime * time);
GstClockTime gst_mpd_parser_get_stream_presentation_offset (GstMpdClient *client, guint stream_idx);
gchar** gst_mpd_client_get_utc_timing_sources (GstMpdClient *client, guint methods, GstMPDUTCTimingType *selected_method);
GstClockTime gst_mpd_parser_get_period_start_time (GstMpdClient *client);

/* Period selection */
guint gst_mpd_client_get_period_index_at_time (GstMpdClient * client, GstDateTime * time);
gboolean gst_mpd_client_set_period_index (GstMpdClient *client, guint period_idx);
gboolean gst_mpd_client_set_period_id (GstMpdClient *client, const gchar * period_id);
guint gst_mpd_client_get_period_index (GstMpdClient *client);
const gchar *gst_mpd_client_get_period_id (GstMpdClient *client);
gboolean gst_mpd_client_has_next_period (GstMpdClient *client);
gboolean gst_mpd_client_has_previous_period (GstMpdClient * client);

/* Representation selection */
gint gst_mpdparser_get_rep_idx_with_max_bandwidth (GList *Representations, gint max_bandwidth, gint max_video_width, gint max_video_height, gint max_video_framerate_n, gint max_video_framerate_d);
gint gst_mpdparser_get_rep_idx_with_min_bandwidth (GList * Representations);

/* URL management */
const gchar *gst_mpdparser_get_baseURL (GstMpdClient *client, guint indexStream);

/* Active stream */
guint gst_mpdparser_get_nb_active_stream (GstMpdClient *client);
GstActiveStream *gst_mpdparser_get_active_stream_by_index (GstMpdClient *client, guint stream_idx);
gboolean gst_mpd_client_active_stream_contains_subtitles (GstActiveStream * stream);

/* AdaptationSet */
guint gst_mpdparser_get_nb_adaptationSet (GstMpdClient *client);
GList * gst_mpd_client_get_adaptation_sets (GstMpdClient * client);

/* Segment */
gboolean gst_mpd_client_has_next_segment (GstMpdClient * client, GstActiveStream * stream, gboolean forward);
GstFlowReturn gst_mpd_client_advance_segment (GstMpdClient * client, GstActiveStream * stream, gboolean forward);
void gst_mpd_client_seek_to_first_segment (GstMpdClient * client);
GstDateTime *gst_mpd_client_get_next_segment_availability_start_time (GstMpdClient * client, GstActiveStream * stream);

/* Get audio/video stream parameters (caps, width, height, rate, number of channels) */
GstCaps * gst_mpd_client_get_stream_caps (GstActiveStream * stream);
gboolean gst_mpd_client_get_bitstream_switching_flag (GstActiveStream * stream);
guint gst_mpd_client_get_video_stream_width (GstActiveStream * stream);
guint gst_mpd_client_get_video_stream_height (GstActiveStream * stream);
gboolean gst_mpd_client_get_video_stream_framerate (GstActiveStream * stream, gint * fps_num, gint * fps_den);
guint gst_mpd_client_get_audio_stream_rate (GstActiveStream * stream);
guint gst_mpd_client_get_audio_stream_num_channels (GstActiveStream * stream);

/* Support multi language */
guint gst_mpdparser_get_list_and_nb_of_audio_language (GstMpdClient *client, GList **lang);

gint64 gst_mpd_client_calculate_time_difference (const GstDateTime * t1, const GstDateTime * t2);
GstDateTime *gst_mpd_client_add_time_difference (GstDateTime * t1, gint64 usecs);
gint64 gst_mpd_client_parse_default_presentation_delay(GstMpdClient * client, const gchar * default_presentation_delay);

/* profiles */
gboolean gst_mpd_client_has_isoff_ondemand_profile (GstMpdClient *client);

G_END_DECLS

#endif /* __GST_MPDPARSER_H__ */

