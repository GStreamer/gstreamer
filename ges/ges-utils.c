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

#include <string.h>

#include "ges-internal.h"
#include "ges-timeline.h"
#include "ges-track.h"
#include "ges-layer.h"
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

  tracka = GES_TRACK (ges_audio_track_new ());
  trackv = GES_TRACK (ges_video_track_new ());

  if (!ges_timeline_add_track (timeline, trackv) ||
      !ges_timeline_add_track (timeline, tracka)) {
    gst_object_unref (timeline);
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
  } else if (a->start < b->start)
    return -1;

  return 1;
}

gint
element_end_compare (GESTimelineElement * a, GESTimelineElement * b)
{
  if (GES_TIMELINE_ELEMENT_END (a) == GES_TIMELINE_ELEMENT_END (b)) {
    if (a->priority < b->priority)
      return -1;
    if (a->priority > b->priority)
      return 1;
    if (a->duration < b->duration)
      return -1;
    if (a->duration > b->duration)
      return 1;
    return 0;
  } else if (GES_TIMELINE_ELEMENT_END (a) < (GES_TIMELINE_ELEMENT_END (b)))
    return -1;

  return 1;
}

gboolean
pspec_equal (gconstpointer key_spec_1, gconstpointer key_spec_2)
{
  const GParamSpec *key1 = key_spec_1;
  const GParamSpec *key2 = key_spec_2;

  return (key1->owner_type == key2->owner_type &&
      strcmp (key1->name, key2->name) == 0);
}

guint
pspec_hash (gconstpointer key_spec)
{
  const GParamSpec *key = key_spec;
  const gchar *p;
  guint h = key->owner_type;

  for (p = key->name; *p; p++)
    h = (h << 5) - h + *p;

  return h;
}
