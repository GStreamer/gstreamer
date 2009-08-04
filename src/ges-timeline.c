/* GStreamer Editing Services
 * Copyright (C) 2009 Edward Hervey <bilboed@bilboed.com>
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

#include "ges-timeline.h"

/**
 * GESTimelinePipeline
 *
 * Top-level container for pipelines
 * 
 * Contains a list of TimelineLayer which users should use to arrange the
 * various timeline objects.
 *
 */

G_DEFINE_TYPE (GESTimeline, ges_timeline, GST_TYPE_BIN)
#define GET_PRIVATE(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), GES_TYPE_TIMELINE, GESTimelinePrivate))
     typedef struct _GESTimelinePrivate GESTimelinePrivate;

     struct _GESTimelinePrivate
     {
       GList *tracks;           /* TimelineTracks */
     };

     static void
         ges_timeline_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_timeline_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_timeline_dispose (GObject * object)
{
  G_OBJECT_CLASS (ges_timeline_parent_class)->dispose (object);
}

static void
ges_timeline_finalize (GObject * object)
{
  G_OBJECT_CLASS (ges_timeline_parent_class)->finalize (object);
}

static void
ges_timeline_class_init (GESTimelineClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  g_type_class_add_private (klass, sizeof (GESTimelinePrivate));

  object_class->get_property = ges_timeline_get_property;
  object_class->set_property = ges_timeline_set_property;
  object_class->dispose = ges_timeline_dispose;
  object_class->finalize = ges_timeline_finalize;
}

static void
ges_timeline_init (GESTimeline * self)
{
  self->layers = NULL;
  self->tracks = NULL;
}

GESTimeline *
ges_timeline_new (void)
{
  return g_object_new (GES_TYPE_TIMELINE, NULL);
}

GESTimeline *
ges_timeline_load_from_uri (gchar * uri)
{
  /* FIXME : IMPLEMENT */
  return NULL;
}

gboolean
ges_timeline_save (GESTimeline * timeline, gchar * uri)
{
  /* FIXME : IMPLEMENT */
  return FALSE;
}

gboolean
ges_timeline_add_layer (GESTimeline * timeline, GESTimelineLayer * layer)
{
  /* FIXME : IMPLEMENT */

  /* Add to the list of layers, make sure we don't already control it */

  /* Assign Tracks to it */

  return FALSE;
}

gboolean
ges_timeline_remove_layer (GESTimeline * timeline, GESTimelineLayer * layer)
{
  /* FIXME : IMPLEMENT */

  /* Unassign tracks from the given layer */
  return FALSE;
}

gboolean
ges_timeline_add_track (GESTimeline * timeline, GESTrack * track)
{
  /* FIXME : IMPLEMENT */

  /* Add to the list of tracks, make sure we don't already control it */


  return FALSE;
}

gboolean
ges_timeline_remove_track (GESTimeline * timeline, GESTrack * track)
{
  /* FIXME : IMPLEMENT */

  /* Signal track removal to all layers/objects */

  /* */
  return FALSE;
}
