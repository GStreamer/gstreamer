/* GStreamer
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstadaptivedemux.h"
#include "gstadaptivedemux-private.h"

GST_DEBUG_CATEGORY_EXTERN (adaptivedemux2_debug);
#define GST_CAT_DEFAULT adaptivedemux2_debug

GstAdaptiveDemuxPeriod *
gst_adaptive_demux_period_new (GstAdaptiveDemux * demux)
{
  GstAdaptiveDemuxPeriod *period;

  period = g_new0 (GstAdaptiveDemuxPeriod, 1);
  g_atomic_int_set (&period->ref_count, 1);

  period->demux = demux;
  period->period_num = demux->priv->n_periods++;

  g_queue_push_tail (demux->priv->periods, period);

  return period;
}

static void
_demux_period_free (GstAdaptiveDemuxPeriod * period)
{
  /* Disable and remove all streams and tracks. */
  g_list_free_full (period->streams, (GDestroyNotify) gst_object_unref);

  /* Theoretically all tracks should have gone by now */
  GST_DEBUG ("Disabling and removing all tracks");
  g_list_free_full (period->tracks,
      (GDestroyNotify) gst_adaptive_demux_track_unref);

  g_free (period);
}

GstAdaptiveDemuxPeriod *
gst_adaptive_demux_period_ref (GstAdaptiveDemuxPeriod * period)
{
  g_return_val_if_fail (period != NULL, NULL);

  GST_TRACE ("%p %d -> %d", period, period->ref_count, period->ref_count + 1);

  g_atomic_int_inc (&period->ref_count);

  return period;
}

void
gst_adaptive_demux_period_unref (GstAdaptiveDemuxPeriod * period)
{
  g_return_if_fail (period != NULL);

  GST_TRACE ("%p %d -> %d", period, period->ref_count, period->ref_count - 1);

  if (g_atomic_int_dec_and_test (&period->ref_count)) {
    _demux_period_free (period);
  }
}

static GstAdaptiveDemuxTrack *
default_track_for_stream_type_locked (GstAdaptiveDemuxPeriod * period,
    GstStreamType stream_type)
{
  GList *tmp;
  GstAdaptiveDemuxTrack *res = NULL, *select = NULL;

  for (tmp = period->tracks; tmp; tmp = tmp->next) {
    GstAdaptiveDemuxTrack *cand = tmp->data;

    if (cand->type == stream_type) {
      /* If selected, we're done */
      if (cand->selected)
        return cand;
      if (!select && cand->flags & GST_STREAM_FLAG_SELECT)
        res = select = cand;
      if (res == NULL)
        res = cand;
    }
  }

  return res;
}

/* called with TRACKS_LOCK taken */
void
gst_adaptive_demux_period_select_default_tracks (GstAdaptiveDemux * demux,
    GstAdaptiveDemuxPeriod * period)
{
  GstAdaptiveDemuxTrack *track;
  gboolean changed = FALSE;

  GST_DEBUG_OBJECT (demux, "Picking a default selection");

  /* Do initial selection (pick default for each type) */
  if ((track =
          default_track_for_stream_type_locked (period,
              GST_STREAM_TYPE_VIDEO))) {
    GST_DEBUG_OBJECT (demux, "Selecting default video track %s",
        track->stream_id);
    if (!track->selected) {
      changed = TRUE;
      track->selected = TRUE;
      gst_pad_set_active (track->sinkpad, TRUE);
    }
  }

  if ((track =
          default_track_for_stream_type_locked (period,
              GST_STREAM_TYPE_AUDIO))) {
    GST_DEBUG_OBJECT (demux, "Selecting default audio track %s",
        track->stream_id);
    if (!track->selected) {
      changed = TRUE;
      track->selected = TRUE;
      gst_pad_set_active (track->sinkpad, TRUE);
    }
  }

  if ((track =
          default_track_for_stream_type_locked (period,
              GST_STREAM_TYPE_TEXT))) {
    GST_DEBUG_OBJECT (demux, "Selecting default text track %s",
        track->stream_id);
    if (!track->selected) {
      changed = TRUE;
      track->selected = TRUE;
      gst_pad_set_active (track->sinkpad, TRUE);
    }
  }

  if (changed)
    g_atomic_int_set (&demux->priv->requested_selection_seqnum,
        gst_util_seqnum_next ());
}

static GstAdaptiveDemuxTrack *
gst_adaptive_demux_period_find_matching_track (GstAdaptiveDemuxPeriod * period,
    GstAdaptiveDemuxTrack * track)
{
  GList *iter;

  for (iter = period->tracks; iter; iter = iter->next) {
    GstAdaptiveDemuxTrack *cand = iter->data;

    if (!cand->selected && cand->type == track->type) {
      /* FIXME : Improve this a *lot* */
      if (!g_strcmp0 (cand->stream_id, track->stream_id))
        return cand;
    }
  }

  return NULL;
}

void
gst_adaptive_demux_period_transfer_selection (GstAdaptiveDemux * demux,
    GstAdaptiveDemuxPeriod * next_period,
    GstAdaptiveDemuxPeriod * current_period)
{
  GList *iter;

  for (iter = current_period->tracks; iter; iter = iter->next) {
    GstAdaptiveDemuxTrack *track = iter->data;
    if (track->selected) {
      GstAdaptiveDemuxTrack *new_track =
          gst_adaptive_demux_period_find_matching_track (next_period, track);
      if (new_track) {
        GST_DEBUG_OBJECT (demux, "Selecting replacement track %s",
            new_track->stream_id);
        new_track->selected = TRUE;
        gst_pad_set_active (new_track->sinkpad, TRUE);
      } else {
        GST_WARNING_OBJECT (demux, "Could not find replacement track for %s",
            track->stream_id);
        /* FIXME : Pick a default for that type ? Just continue as-is ? */
      }
    }
  }
}

/* called with TRACKS_LOCK taken */
gboolean
gst_adaptive_demux_period_add_track (GstAdaptiveDemuxPeriod * period,
    GstAdaptiveDemuxTrack * track)
{
  GST_LOG ("period %d track:%p", period->period_num, track);

  /* Actually create and add the elements to the demuxer */
  if (!gst_adaptive_demux_track_add_elements (track, period->period_num)) {
    GST_ERROR ("Failed to add track");
    return FALSE;
  }

  period->tracks =
      g_list_append (period->tracks, gst_adaptive_demux_track_ref (track));
  period->tracks_changed = TRUE;

  return TRUE;
}

/* must be called with manifest_lock taken */
GstFlowReturn
gst_adaptive_demux_period_combine_stream_flows (GstAdaptiveDemuxPeriod * period)
{
  gboolean all_notlinked = TRUE;
  gboolean all_eos = TRUE;
  GList *iter;

  for (iter = period->streams; iter; iter = g_list_next (iter)) {
    GstAdaptiveDemux2Stream *stream = iter->data;

    if (stream->last_ret != GST_FLOW_NOT_LINKED) {
      all_notlinked = FALSE;
      if (stream->last_ret != GST_FLOW_EOS)
        all_eos = FALSE;
    }

    if (stream->last_ret <= GST_FLOW_NOT_NEGOTIATED
        || stream->last_ret == GST_FLOW_FLUSHING) {
      return stream->last_ret;
    }
  }

  if (all_notlinked)
    return GST_FLOW_NOT_LINKED;

  if (all_eos)
    return GST_FLOW_EOS;

  return GST_FLOW_OK;
}

void
gst_adaptive_demux_period_stop_tasks (GstAdaptiveDemuxPeriod * period)
{
  GList *iter;

  for (iter = period->streams; iter; iter = g_list_next (iter)) {
    GstAdaptiveDemux2Stream *stream = iter->data;

    gst_adaptive_demux2_stream_stop (stream);

    stream->download_error_count = 0;
    stream->need_header = TRUE;
  }
}

gboolean
gst_adaptive_demux_period_has_pending_tracks (GstAdaptiveDemuxPeriod * period)
{
  GList *iter;

  for (iter = period->streams; iter; iter = g_list_next (iter)) {
    GstAdaptiveDemux2Stream *stream = iter->data;
    if (stream->pending_tracks)
      return TRUE;
  }
  return FALSE;
}
