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
#ifndef __GST_MPDCLIENT_H__
#define __GST_MPDCLIENT_H__

#include "gstmpdparser.h"
#include "downloadhelper.h"

G_BEGIN_DECLS

#define GST_TYPE_MPD_CLIENT gst_mpd_client2_get_type ()
G_DECLARE_FINAL_TYPE (GstMPDClient2, gst_mpd_client2, GST, MPD_CLIENT, GstObject)

struct _GstMPDClient2
{
  GstObject     parent_instance;
  GstMPDRootNode *mpd_root_node;              /* mpd root node */

  GList *periods;                             /* list of GstStreamPeriod */
  guint period_idx;                           /* index of current Period */

  GList *active_streams;                      /* list of GstActiveStream */

  guint update_failed_count;
  gchar *mpd_uri;                             /* manifest file URI */
  gchar *mpd_base_uri;                        /* base URI for resolving relative URIs.
                                               * this will be different for redirects */

  /* profiles */
  gboolean profile_isoff_ondemand;

  DownloadHelper *download_helper;
};

/* Basic initialization/deinitialization functions */

GstMPDClient2 *gst_mpd_client2_new (void);
GstMPDClient2 *gst_mpd_client2_new_static (void);

void gst_mpd_client2_active_streams_free (GstMPDClient2 * client);
void gst_mpd_client2_free (GstMPDClient2 * client);

/* main mpd parsing methods from xml data */
gboolean gst_mpd_client2_parse (GstMPDClient2 * client, const gchar * data, gint size);

/* xml generator */
gboolean gst_mpd_client2_get_xml_content (GstMPDClient2 * client, gchar ** data, gint * size);

void gst_mpd_client2_set_download_helper (GstMPDClient2 * client, DownloadHelper *dh);
void  gst_mpd_client2_check_profiles (GstMPDClient2 * client);
void gst_mpd_client2_fetch_on_load_external_resources (GstMPDClient2 * client);

/* Streaming management */
gboolean gst_mpd_client2_setup_media_presentation (GstMPDClient2 *client, GstClockTime time, gint period_index, const gchar *period_id);
gboolean gst_mpd_client2_setup_streaming (GstMPDClient2 * client, GstMPDAdaptationSetNode * adapt_set, gint64 max_bandwidth, gint max_video_width, gint max_video_height, gint max_video_framerate_n, gint max_video_framerate_d);
gboolean gst_mpd_client2_setup_representation (GstMPDClient2 *client, GstActiveStream *stream, GstMPDRepresentationNode *representation);

GstClockTime gst_mpd_client2_get_next_fragment_duration (GstMPDClient2 * client, GstActiveStream * stream);
GstClockTime gst_mpd_client2_get_media_presentation_duration (GstMPDClient2 *client);
GstClockTime gst_mpd_client2_get_maximum_segment_duration (GstMPDClient2 * client);
gboolean gst_mpd_client2_get_last_fragment_timestamp_end (GstMPDClient2 * client, guint stream_idx, GstClockTime * ts);
gboolean gst_mpd_client2_get_next_fragment_timestamp (GstMPDClient2 * client, guint stream_idx, GstClockTime * ts);
gboolean gst_mpd_client2_get_next_fragment (GstMPDClient2 *client, guint indexStream, GstMediaFragmentInfo * fragment);
gboolean gst_mpd_client2_get_next_header (GstMPDClient2 *client, gchar **uri, guint stream_idx, gint64 * range_start, gint64 * range_end);
gboolean gst_mpd_client2_get_next_header_index (GstMPDClient2 *client, gchar **uri, guint stream_idx, gint64 * range_start, gint64 * range_end);
gboolean gst_mpd_client2_is_live (GstMPDClient2 * client);
gboolean gst_mpd_client2_stream_seek (GstMPDClient2 * client, GstActiveStream * stream, gboolean forward, GstSeekFlags flags, GstClockTime ts, GstClockTime * final_ts);
gboolean gst_mpd_client2_seek_to_time (GstMPDClient2 * client, GDateTime * time);
GstClockTime gst_mpd_client2_get_stream_presentation_offset (GstMPDClient2 *client, guint stream_idx);
gchar** gst_mpd_client2_get_utc_timing_sources (GstMPDClient2 *client, guint methods, GstMPDUTCTimingType *selected_method);
GstClockTime gst_mpd_client2_get_period_start_time (GstMPDClient2 *client);

GstCaps *gst_mpd_client2_get_codec_caps (GstActiveStream *stream);

/* Period selection */
guint gst_mpd_client2_get_period_index_at_time (GstMPDClient2 * client, GstDateTime * time);
gboolean gst_mpd_client2_set_period_index (GstMPDClient2 *client, guint period_idx);
gboolean gst_mpd_client2_set_period_id (GstMPDClient2 *client, const gchar * period_id);
guint gst_mpd_client2_get_period_index (GstMPDClient2 *client);
const gchar *gst_mpd_client2_get_period_id (GstMPDClient2 *client);
gboolean gst_mpd_client2_has_next_period (GstMPDClient2 *client);
gboolean gst_mpd_client2_has_previous_period (GstMPDClient2 * client);

/* Representation selection */
gint gst_mpd_client2_get_rep_idx_with_max_bandwidth (GList *Representations, gint64 max_bandwidth, gint max_video_width, gint max_video_height, gint max_video_framerate_n, gint max_video_framerate_d);
gint gst_mpd_client2_get_rep_idx_with_min_bandwidth (GList * Representations);
GstMPDRepresentationNode* gst_mpd_client2_get_representation_with_id (GList * representations, gchar * rep_id);

GstDateTime *
gst_mpd_client2_get_availability_start_time (GstMPDClient2 * client);

/* URL management */
const gchar *gst_mpd_client2_get_baseURL (GstMPDClient2 *client, guint indexStream);
gchar *gst_mpd_client2_parse_baseURL (GstMPDClient2 * client, GstActiveStream * stream, gchar ** query);

/* Active stream */
guint gst_mpd_client2_get_nb_active_stream (GstMPDClient2 *client);
GstActiveStream *gst_mpd_client2_get_active_stream_by_index (GstMPDClient2 *client, guint stream_idx);
gboolean gst_mpd_client2_active_stream_contains_subtitles (GstActiveStream * stream);

/* AdaptationSet */
guint gst_mpd_client2_get_nb_adaptationSet (GstMPDClient2 *client);
GList * gst_mpd_client2_get_adaptation_sets (GstMPDClient2 * client);

/* Segment */
gboolean gst_mpd_client2_has_next_segment (GstMPDClient2 * client, GstActiveStream * stream, gboolean forward);
GstFlowReturn gst_mpd_client2_advance_segment (GstMPDClient2 * client, GstActiveStream * stream, gboolean forward);
void gst_mpd_client2_seek_to_first_segment (GstMPDClient2 * client);
GstDateTime *gst_mpd_client2_get_next_segment_availability_start_time (GstMPDClient2 * client, GstActiveStream * stream);

/* Get audio/video stream parameters (caps, width, height, rate, number of channels) */
GstCaps * gst_mpd_client2_get_stream_caps (GstActiveStream * stream);
gboolean gst_mpd_client2_get_bitstream_switching_flag (GstActiveStream * stream);
guint gst_mpd_client2_get_video_stream_width (GstActiveStream * stream);
guint gst_mpd_client2_get_video_stream_height (GstActiveStream * stream);
gboolean gst_mpd_client2_get_video_stream_framerate (GstActiveStream * stream, gint * fps_num, gint * fps_den);
guint gst_mpd_client2_get_audio_stream_rate (GstActiveStream * stream);
guint gst_mpd_client2_get_audio_stream_num_channels (GstActiveStream * stream);

/* Support multi language */
guint gst_mpd_client2_get_list_and_nb_of_audio_language (GstMPDClient2 *client, GList **lang);

GstClockTimeDiff gst_mpd_client2_calculate_time_difference (const GstDateTime * t1, const GstDateTime * t2);
GstDateTime *gst_mpd_client2_add_time_difference (GstDateTime * t1, GstClockTimeDiff diff);
gint64 gst_mpd_client2_parse_default_presentation_delay(GstMPDClient2 * client, const gchar * default_presentation_delay);

/* profiles */
gboolean gst_mpd_client2_has_isoff_ondemand_profile (GstMPDClient2 *client);

/* add/set node methods */
gboolean gst_mpd_client2_set_root_node (GstMPDClient2 * client,
                                       const gchar * property_name,
                                       ...);
gchar * gst_mpd_client2_set_period_node (GstMPDClient2 * client,
                                        gchar * period_id,
                                        const gchar * property_name,
                                        ...);
guint gst_mpd_client2_set_adaptation_set_node (GstMPDClient2 * client,
                                              gchar * period_id,
                                              guint adap_set_id,
                                              const gchar * property_name,
                                              ...);
gchar * gst_mpd_client2_set_representation_node (GstMPDClient2 * client,
                                                gchar * period_id,
                                                guint adap_set_id,
                                                gchar * rep_id,
                                                const gchar * property_name,
                                                ...);
gboolean gst_mpd_client2_set_segment_list (GstMPDClient2 * client,
                                          gchar * period_id,
                                          guint adap_set_id,
                                          gchar * rep_id,
                                          const gchar * property_name,
                                          ...);
gboolean gst_mpd_client2_set_segment_template (GstMPDClient2 * client,
                                              gchar * period_id,
                                              guint adap_set_id,
                                              gchar * rep_id,
                                              const gchar * property_name,
                                              ...);

/* create a new node */
gboolean gst_mpd_client2_add_baseurl_node (GstMPDClient2 * client,
                                          const gchar * property_name,
                                          ...);
gboolean gst_mpd_client2_add_segment_url (GstMPDClient2 * client,
                                         gchar * period_id,
                                         guint adap_set_id,
                                         gchar * rep_id,
                                         const gchar * property_name,
                                         ...);
G_END_DECLS

#endif /* __GST_MPDCLIENT_H__ */
