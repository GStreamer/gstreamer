/* GStreamer
 *
 * Copyright (C) 2014 Samsung Electronics. All rights reserved.
 *   Author: Thiago Santos <thiagoss@osg.samsung.com>
 *
 * Copyright (C) 2021-2022 Centricular Ltd
 *   Author: Edward Hervey <edward@centricular.com>
 *   Author: Jan Schmidt <jan@centricular.com>
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */
#ifndef _GST_ADAPTIVE_DEMUX_PRIVATE_H_
#define _GST_ADAPTIVE_DEMUX_PRIVATE_H_

#include <gst/gst.h>
#include <gst/base/gstadapter.h>
#include <gst/base/gstflowcombiner.h>

#include "gstadaptivedemux-types.h"
#include "gstadaptivedemux.h"
#include "gstadaptivedemuxutils.h"

G_BEGIN_DECLS

#define NUM_LOOKBACK_FRAGMENTS 3
#define MAX_DOWNLOAD_ERROR_COUNT 3

/* Internal, so not using GST_FLOW_CUSTOM_SUCCESS_N */
#define GST_ADAPTIVE_DEMUX_FLOW_SWITCH (GST_FLOW_CUSTOM_SUCCESS_2 + 2)

#define TRACKS_GET_LOCK(d) (&GST_ADAPTIVE_DEMUX_CAST(d)->priv->tracks_lock)
#define TRACKS_LOCK(d) g_mutex_lock (TRACKS_GET_LOCK (d))
#define TRACKS_UNLOCK(d) g_mutex_unlock (TRACKS_GET_LOCK (d))

#define BUFFERING_GET_LOCK(d) (&GST_ADAPTIVE_DEMUX_CAST(d)->priv->buffering_lock)
#define BUFFERING_LOCK(d) g_mutex_lock (BUFFERING_GET_LOCK (d))
#define BUFFERING_UNLOCK(d) g_mutex_unlock (BUFFERING_GET_LOCK (d))

#define GST_MANIFEST_GET_LOCK(d) (&(GST_ADAPTIVE_DEMUX_CAST(d)->priv->manifest_lock))
#define GST_MANIFEST_LOCK(d) G_STMT_START { \
    GST_TRACE("Locking manifest from thread %p", g_thread_self()); \
    g_rec_mutex_lock (GST_MANIFEST_GET_LOCK (d)); \
    GST_TRACE("Locked manifest from thread %p", g_thread_self()); \
 } G_STMT_END

#define GST_MANIFEST_UNLOCK(d) G_STMT_START { \
    GST_TRACE("Unlocking manifest from thread %p", g_thread_self()); \
    g_rec_mutex_unlock (GST_MANIFEST_GET_LOCK (d)); \
 } G_STMT_END

#define GST_ADAPTIVE_DEMUX_GET_SCHEDULER(d) (GST_ADAPTIVE_DEMUX_CAST(d)->priv->scheduler_task)

#define GST_ADAPTIVE_SCHEDULER_LOCK(d) gst_adaptive_demux_scheduler_lock(demux)
#define GST_ADAPTIVE_SCHEDULER_UNLOCK(d) G_STMT_START { \
    GST_TRACE("Unlocking scheduler from thread %p", g_thread_self()); \
    gst_adaptive_demux_loop_unlock_and_unpause (GST_ADAPTIVE_DEMUX_GET_SCHEDULER (d)); \
 } G_STMT_END

#define GST_ADAPTIVE_DEMUX_SEGMENT_GET_LOCK(d) (&GST_ADAPTIVE_DEMUX_CAST(d)->priv->segment_lock)
#define GST_ADAPTIVE_DEMUX_SEGMENT_LOCK(d) g_mutex_lock (GST_ADAPTIVE_DEMUX_SEGMENT_GET_LOCK (d))
#define GST_ADAPTIVE_DEMUX_SEGMENT_UNLOCK(d) g_mutex_unlock (GST_ADAPTIVE_DEMUX_SEGMENT_GET_LOCK (d))

struct _GstAdaptiveDemuxPrivate
{
  GstAdapter *input_adapter;    /* protected by manifest_lock */
  gint have_manifest;           /* MT safe */

  /* Adaptive scheduling and parsing task */
  GstAdaptiveDemuxLoop *scheduler_task;

  /* Callback / timer id for the next manifest update */
  guint manifest_updates_cb;
  gboolean manifest_updates_enabled;
  gboolean need_manual_manifest_update;

  /* Count of failed manifest updates */
  gint update_failed_count;

  guint32 segment_seqnum;       /* protected by manifest_lock */

  /* main lock used to protect adaptive demux and all its streams.
   * It serializes the adaptive demux public API.
   */
  GRecMutex manifest_lock;

  /* Duration, updated after manifest updates */
  GstClockTime duration;

  /* Set to TRUE if any stream is waiting on the manifest update */
  gboolean stream_waiting_for_manifest;

  /* Set to TRUE if streams can download fragment data. If FALSE,
   * they can load playlists / prepare for updata_fragment_info()
   */
  gboolean streams_can_download_fragments;

  /* Protects demux and stream segment information
   * Needed because seeks can update segment information
   * without needing to stop tasks when they just want to
   * update the segment boundaries */
  GMutex segment_lock;

  GstClockTime qos_earliest_time;

  /* Protects all tracks and period content */
  GMutex tracks_lock;
  /* Used to notify addition to a waiting (i.e. previously empty) track */
  GCond tracks_add;
  /* TRUE if we are buffering */
  gboolean is_buffering;
  /* TRUE if percent changed and message should be posted */
  gboolean percent_changed;
  gint percent;

  /* Serialises buffering message posting to avoid out-of-order
   * posting */
  GMutex buffering_lock;

  /* Atomic */
  guint32 requested_selection_seqnum;

  /* Lock protecting all the following fields */
  GRecMutex output_lock;
  /* Output task */
  GstTask *output_task;
  /* List of enabled OutputSlot */
  GList *outputs;
  /* flow combiner of output slots */
  GstFlowCombiner *flowcombiner;
  /* protected by output_lock */
  gboolean flushing;
  /* Current output selection seqnum */
  guint32 current_selection_seqnum;
  /* Current output position (in running time) */
  GstClockTime global_output_position;
  /* End of fields protected by output_lock */

  gint n_audio_streams, n_video_streams, n_subtitle_streams;

  /* Counter used for uniquely identifying periods */
  gint n_periods;

  /* Array of periods.
   *
   * Head is the period being outputted, or to be outputted first
   * Tail is where new streams get added */
  GQueue *periods;
};

static inline gboolean gst_adaptive_demux_scheduler_lock(GstAdaptiveDemux *d)
{
    GST_TRACE("Locking scheduler from thread %p", g_thread_self());
    if (!gst_adaptive_demux_loop_pause_and_lock (GST_ADAPTIVE_DEMUX_GET_SCHEDULER (d)))
      return FALSE;

    GST_TRACE("Locked scheduler from thread %p", g_thread_self());
    return TRUE;
}

void demux_update_buffering_locked (GstAdaptiveDemux * demux);
void demux_post_buffering_locked (GstAdaptiveDemux * demux);

GstFlowReturn gst_adaptive_demux_update_manifest (GstAdaptiveDemux *demux);

void gst_adaptive_demux2_stream_wants_manifest_update (GstAdaptiveDemux * demux);

void gst_adaptive_demux2_stream_parse_error (GstAdaptiveDemux2Stream *stream, GError * err);
GstClockTime gst_adaptive_demux2_stream_get_fragment_waiting_time (GstAdaptiveDemux2Stream * stream);
GstClockTime gst_adaptive_demux2_stream_get_presentation_offset (GstAdaptiveDemux2Stream * stream);
GstClockTime gst_adaptive_demux_get_period_start_time (GstAdaptiveDemux * demux);

gboolean gst_adaptive_demux_is_live (GstAdaptiveDemux * demux);

void gst_adaptive_demux2_stream_on_manifest_update (GstAdaptiveDemux2Stream * stream);
void gst_adaptive_demux2_stream_on_output_space_available (GstAdaptiveDemux2Stream *stream);
void gst_adaptive_demux2_stream_on_can_download_fragments(GstAdaptiveDemux2Stream *stream);

gboolean gst_adaptive_demux2_stream_has_next_fragment (GstAdaptiveDemux2Stream * stream);
GstFlowReturn gst_adaptive_demux2_stream_seek (GstAdaptiveDemux2Stream * stream,
    gboolean forward, GstSeekFlags flags,
    GstClockTimeDiff ts, GstClockTimeDiff * final_ts);
gboolean gst_adaptive_demux_get_live_seek_range (GstAdaptiveDemux * demux,
    gint64 * range_start, gint64 * range_stop);
gboolean gst_adaptive_demux2_stream_in_live_seek_range (GstAdaptiveDemux * demux,
    GstAdaptiveDemux2Stream * stream);
gboolean gst_adaptive_demux2_stream_is_selected_locked (GstAdaptiveDemux2Stream *stream);
gboolean gst_adaptive_demux2_stream_is_default_locked (GstAdaptiveDemux2Stream *stream);

gboolean gst_adaptive_demux_has_next_period (GstAdaptiveDemux * demux);
void gst_adaptive_demux_advance_period (GstAdaptiveDemux * demux);

GstFlowReturn gst_adaptive_demux2_stream_update_fragment_info (GstAdaptiveDemux2Stream * stream);
void gst_adaptive_demux2_stream_stop (GstAdaptiveDemux2Stream * stream);

gboolean gst_adaptive_demux_handle_lost_sync (GstAdaptiveDemux * demux);

typedef struct
{
  GstMiniObject *item;
  gsize size;
  /* running time of item : GST_CLOCK_STIME_NONE for non-timed data */
  GstClockTimeDiff runningtime;
  /* GST_CLOCK_STIME_NONE for non-timed data */
  GstClockTimeDiff runningtime_end;
  /* running time of item for buffering tracking: GST_CLOCK_STIME_NONE for non-timed data */
  GstClockTimeDiff runningtime_buffering;
} TrackQueueItem;

GstMiniObject * gst_adaptive_demux_track_dequeue_data_locked (GstAdaptiveDemux * demux, GstAdaptiveDemuxTrack * track, gboolean check_sticky_events);
void gst_adaptive_demux_track_flush (GstAdaptiveDemuxTrack * track);
void gst_adaptive_demux_track_drain_to (GstAdaptiveDemuxTrack * track, GstClockTime drain_running_time);
void gst_adaptive_demux_track_update_next_position (GstAdaptiveDemuxTrack * track);
void gst_adaptive_demux_track_update_level_locked (GstAdaptiveDemuxTrack * track);

/* Period functions */
GstAdaptiveDemuxPeriod * gst_adaptive_demux_period_new (GstAdaptiveDemux * demux);

GstAdaptiveDemuxPeriod * gst_adaptive_demux_period_ref (GstAdaptiveDemuxPeriod * period);
void                     gst_adaptive_demux_period_unref (GstAdaptiveDemuxPeriod * period);

gboolean                 gst_adaptive_demux_period_add_stream (GstAdaptiveDemuxPeriod * period,
							      GstAdaptiveDemux2Stream * stream);
gboolean                 gst_adaptive_demux_period_add_track (GstAdaptiveDemuxPeriod * period,
							      GstAdaptiveDemuxTrack * track);
gboolean                 gst_adaptive_demux_track_add_elements (GstAdaptiveDemuxTrack * track,
								guint period_num);

void                     gst_adaptive_demux_period_select_default_tracks (GstAdaptiveDemux * demux,
									  GstAdaptiveDemuxPeriod * period);
void                     gst_adaptive_demux_period_transfer_selection (GstAdaptiveDemux * demux,
								       GstAdaptiveDemuxPeriod * next_period,
								       GstAdaptiveDemuxPeriod * current_period);
void                     gst_adaptive_demux_period_stop_tasks (GstAdaptiveDemuxPeriod * period);
GstFlowReturn            gst_adaptive_demux_period_combine_stream_flows (GstAdaptiveDemuxPeriod * period);

gboolean                 gst_adaptive_demux_period_has_pending_tracks (GstAdaptiveDemuxPeriod * period);
void      gst_adaptive_demux_period_check_input_wakeup_locked (GstAdaptiveDemuxPeriod * period, GstClockTimeDiff current_output_position);

G_END_DECLS

#endif
