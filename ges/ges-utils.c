/* GStreamer Editing Services
 * Copyright (C) 2010 Edward Hervey <edward.hervey@collabora.co.uk>
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

/**
 * SECTION:ges-utils
 * @short_description: Convenience methods
 *
 */

#include "ges-internal.h"
#include "ges-timeline.h"
#include "ges-track.h"
#include "ges-timeline-layer.h"
#include "ges.h"

/**
 * ges_timeline_new_audio_video:
 * 
 * Creates a new #GESTimeline containing a raw audio and a
 * raw video track.
 *
 * Returns: The newly created #GESTimeline.
 */

GESTimeline *
ges_timeline_new_audio_video (void)
{
  GESTrack *tracka, *trackv;
  GESTimeline *timeline;

  /* This is our main GESTimeline */
  timeline = ges_timeline_new ();

  tracka = ges_track_audio_raw_new ();
  trackv = ges_track_video_raw_new ();

  if (!ges_timeline_add_track (timeline, trackv) ||
      !ges_timeline_add_track (timeline, tracka)) {
    g_object_unref (timeline);
    timeline = NULL;
  }

  return timeline;
}

/* Internal utilities */
gint
element_start_compare (GESTimelineElement * a, GESTimelineElement * b)
{
  if (a->start == b->start) {
    if (a->priority < b->priority)
      return -1;
    if (a->priority > b->priority)
      return 1;
    if (a->duration < b->duration)
      return -1;
    if (a->duration > b->duration)
      return 1;
    return 0;
  }
  if (a->start < b->start)
    return -1;
  if (a->start > b->start)
    return 1;
  return 0;
}
