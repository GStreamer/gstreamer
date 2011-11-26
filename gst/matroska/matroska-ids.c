/* GStreamer Matroska muxer/demuxer
 * (C) 2003 Ronald Bultje <rbultje@ronald.bitfreak.net>
 * (C) 2006 Tim-Philipp Müller <tim centricular net>
 *
 * matroska-ids.c: matroska track context utility functions
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "matroska-ids.h"

gboolean
gst_matroska_track_init_video_context (GstMatroskaTrackContext ** p_context)
{
  GstMatroskaTrackVideoContext *video_context;

  g_assert (p_context != NULL && *p_context != NULL);

  /* already set up? (track info might come before track type) */
  if ((*p_context)->type == GST_MATROSKA_TRACK_TYPE_VIDEO) {
    GST_LOG ("video context already set up");
    return TRUE;
  }

  /* it better not have been set up as some other track type ... */
  if ((*p_context)->type != 0) {
    g_return_val_if_reached (FALSE);
  }

  video_context = g_renew (GstMatroskaTrackVideoContext, *p_context, 1);
  *p_context = (GstMatroskaTrackContext *) video_context;

  /* defaults */
  (*p_context)->type = GST_MATROSKA_TRACK_TYPE_VIDEO;
  video_context->display_width = 0;
  video_context->display_height = 0;
  video_context->pixel_width = 0;
  video_context->pixel_height = 0;
  video_context->asr_mode = 0;
  video_context->fourcc = 0;
  video_context->default_fps = 0.0;
  video_context->earliest_time = GST_CLOCK_TIME_NONE;
  return TRUE;
}

gboolean
gst_matroska_track_init_audio_context (GstMatroskaTrackContext ** p_context)
{
  GstMatroskaTrackAudioContext *audio_context;

  g_assert (p_context != NULL && *p_context != NULL);

  /* already set up? (track info might come before track type) */
  if ((*p_context)->type == GST_MATROSKA_TRACK_TYPE_AUDIO)
    return TRUE;

  /* it better not have been set up as some other track type ... */
  if ((*p_context)->type != 0) {
    g_return_val_if_reached (FALSE);
  }

  audio_context = g_renew (GstMatroskaTrackAudioContext, *p_context, 1);
  *p_context = (GstMatroskaTrackContext *) audio_context;

  /* defaults */
  (*p_context)->type = GST_MATROSKA_TRACK_TYPE_AUDIO;
  audio_context->channels = 1;
  audio_context->samplerate = 8000;
  return TRUE;
}

gboolean
gst_matroska_track_init_subtitle_context (GstMatroskaTrackContext ** p_context)
{
  GstMatroskaTrackSubtitleContext *subtitle_context;

  g_assert (p_context != NULL && *p_context != NULL);

  /* already set up? (track info might come before track type) */
  if ((*p_context)->type == GST_MATROSKA_TRACK_TYPE_SUBTITLE)
    return TRUE;

  /* it better not have been set up as some other track type ... */
  if ((*p_context)->type != 0) {
    g_return_val_if_reached (FALSE);
  }

  subtitle_context = g_renew (GstMatroskaTrackSubtitleContext, *p_context, 1);
  *p_context = (GstMatroskaTrackContext *) subtitle_context;

  (*p_context)->type = GST_MATROSKA_TRACK_TYPE_SUBTITLE;
  subtitle_context->invalid_utf8 = FALSE;
  subtitle_context->seen_markup_tag = FALSE;
  return TRUE;
}

void
gst_matroska_register_tags (void)
{
  /* TODO: register other custom tags */
}
