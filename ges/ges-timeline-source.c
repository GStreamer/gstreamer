/* GStreamer Editing Services
 * Copyright (C) 2009 Edward Hervey <edward.hervey@collabora.co.uk>
 *               2009 Nokia Corporation
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

/**
 * SECTION:ges-timeline-source
 * @short_description: Base Class for sources of a #GESTimelineLayer
 */

#include "ges-internal.h"
#include "ges-timeline-object.h"
#include "ges-timeline-source.h"
#include "ges-track-source.h"
#include "ges-track-text-overlay.h"

G_DEFINE_TYPE (GESTimelineSource, ges_timeline_source,
    GES_TYPE_TIMELINE_OBJECT);

static GESTrackObject
    * ges_timeline_source_create_track_object (GESTimelineObject * obj,
    GESTrack * track);

static gboolean
ges_timeline_source_create_track_objects (GESTimelineObject * obj,
    GESTrack * track);

static void
ges_timeline_source_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_timeline_source_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_timeline_source_dispose (GObject * object)
{
  G_OBJECT_CLASS (ges_timeline_source_parent_class)->dispose (object);
}

static void
ges_timeline_source_finalize (GObject * object)
{
  G_OBJECT_CLASS (ges_timeline_source_parent_class)->finalize (object);
}

static void
ges_timeline_source_class_init (GESTimelineSourceClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GESTimelineObjectClass *timobj_class = GES_TIMELINE_OBJECT_CLASS (klass);

  object_class->get_property = ges_timeline_source_get_property;
  object_class->set_property = ges_timeline_source_set_property;
  object_class->dispose = ges_timeline_source_dispose;
  object_class->finalize = ges_timeline_source_finalize;

  timobj_class->create_track_object = ges_timeline_source_create_track_object;

  timobj_class->create_track_objects = ges_timeline_source_create_track_objects;
}

static void
ges_timeline_source_init (GESTimelineSource * self)
{
}

GESTimelineSource *
ges_timeline_source_new (void)
{
  return g_object_new (GES_TYPE_TIMELINE_SOURCE, NULL);
}

static GESTrackObject *
ges_timeline_source_create_track_object (GESTimelineObject * obj,
    GESTrack * track)
{
  GST_DEBUG ("Creating a GESTrackSource");
  /* FIXME : Implement properly ! */
  return (GESTrackObject *) ges_track_source_new ();
}

static gboolean
ges_timeline_source_create_track_objects (GESTimelineObject * obj,
    GESTrack * track)
{
  GESTrackObject *primary, *overlay;
  gboolean success = FALSE;

  /* calls add_track_object() for us. we already own this reference */
  primary = ges_timeline_object_create_track_object (obj, track);
  if (!primary) {
    GST_WARNING ("couldn't create primary track object");
    return FALSE;
  }

  success = ges_track_add_object (track, primary);

  /* create priority space for the text overlay. do this regardless of
   * wthether we create an overlay so that track objects have a consistent
   * priority between tracks. */
  g_object_set (primary, "priority-offset", (guint) 1, NULL);

  if (track->type == GES_TRACK_TYPE_VIDEO) {
    overlay = (GESTrackObject *) ges_track_text_overlay_new ();
    /* will check for null */
    if (!ges_timeline_object_add_track_object (obj, overlay)) {
      GST_ERROR ("couldn't add textoverlay");
      return FALSE;
    }

    ges_track_text_overlay_set_text ((GESTrackTextOverlay *) overlay,
        "test overlays in timeline sources");
    success = ges_track_add_object (track, overlay);
  }

  return success;
}
