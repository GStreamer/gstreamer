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

#include "gesmarshal.h"
#include "ges-timeline.h"
#include "ges-track.h"
#include "ges-timeline-layer.h"
#include "ges.h"

/**
 * GESTimelinePipeline
 *
 * Top-level container for pipelines
 * 
 * Contains a list of TimelineLayer which users should use to arrange the
 * various timeline objects.
 *
 */

G_DEFINE_TYPE (GESTimeline, ges_timeline, GST_TYPE_BIN);

#define GET_PRIVATE(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), GES_TYPE_TIMELINE, GESTimelinePrivate));

typedef struct _GESTimelinePrivate GESTimelinePrivate;

struct _GESTimelinePrivate
{

};

enum
{
  TRACK_ADDED,
  TRACK_REMOVED,
  LAYER_ADDED,
  LAYER_REMOVED,
  LAST_SIGNAL
};

static guint ges_timeline_signals[LAST_SIGNAL] = { 0 };

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

  /* Signals
   * 'track-added'
   * 'track-removed'
   * 'layer-added'
   * 'layer-removed'
   */

  ges_timeline_signals[TRACK_ADDED] =
      g_signal_new ("track-added", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_FIRST, G_STRUCT_OFFSET (GESTimelineClass, track_added), NULL,
      NULL, ges_marshal_VOID__OBJECT, G_TYPE_NONE, 1, GES_TYPE_TRACK);

  ges_timeline_signals[TRACK_REMOVED] =
      g_signal_new ("track-removed", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_FIRST, G_STRUCT_OFFSET (GESTimelineClass, track_removed),
      NULL, NULL, ges_marshal_VOID__OBJECT, G_TYPE_NONE, 1, GES_TYPE_TRACK);

  ges_timeline_signals[LAYER_ADDED] =
      g_signal_new ("layer-added", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_FIRST, G_STRUCT_OFFSET (GESTimelineClass, layer_added), NULL,
      NULL, ges_marshal_VOID__OBJECT, G_TYPE_NONE, 1, GES_TYPE_TIMELINE_LAYER);

  ges_timeline_signals[LAYER_REMOVED] =
      g_signal_new ("layer-removed", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_FIRST, G_STRUCT_OFFSET (GESTimelineClass, layer_removed),
      NULL, NULL, ges_marshal_VOID__OBJECT, G_TYPE_NONE, 1,
      GES_TYPE_TIMELINE_LAYER);
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

static void
layer_object_added_cb (GESTimelineLayer * layer, GESTimelineObject * object,
    GESTimeline * timeline)
{
  GList *tmp;

  GST_DEBUG ("New TimelineObject %p added to layer %p", object, layer);

  for (tmp = timeline->tracks; tmp; tmp = g_list_next (tmp)) {
    GESTrack *track = (GESTrack *) track;
    GESTrackObject *trobj;

    GST_LOG ("Trying with track %p", track);

    if (G_UNLIKELY (!(trobj =
                ges_timeline_object_create_track_object (object, track)))) {
      GST_WARNING ("Couldn't create TrackObject for TimelineObject");
      continue;
    }

    GST_LOG ("Got new TrackObject %p, adding it to track", trobj);
    ges_track_add_object (track, trobj);
  }

  GST_DEBUG ("done");
}


static void
layer_object_removed_cb (GESTimelineLayer * layer, GESTimelineObject * object,
    GESTimeline * timeline)
{
  /* FIXME : IMPLEMENT */
}


gboolean
ges_timeline_add_layer (GESTimeline * timeline, GESTimelineLayer * layer)
{
  GST_DEBUG ("timeline:%p, layer:%p", timeline, layer);

  /* We can only add a layer that doesn't already belong to another timeline */
  if (G_UNLIKELY (layer->timeline)) {
    GST_WARNING ("Layer belongs to another timeline, can't add it");
    return FALSE;
  }

  /* Add to the list of layers, make sure we don't already control it */
  if (G_UNLIKELY (g_list_find (timeline->layers, (gconstpointer) layer))) {
    GST_WARNING ("Layer is already controlled by this timeline");
    return FALSE;
  }

  /* Reference is taken */
  timeline->layers = g_list_append (timeline->layers, g_object_ref (layer));

  /* Inform the layer that it belongs to a new timeline */
  ges_timeline_layer_set_timeline (layer, timeline);

  /* FIXME : GO OVER THE LIST OF EXISTING TIMELINE OBJECTS IN THAT LAYER
   * AND ADD THEM !!! */

  /* Connect to 'object-added'/'object-removed' signal from the new layer */
  g_signal_connect (layer, "object-added", G_CALLBACK (layer_object_added_cb),
      timeline);
  g_signal_connect (layer, "object-removed",
      G_CALLBACK (layer_object_removed_cb), timeline);

  GST_DEBUG ("Done adding layer, emitting 'layer-added' signal");
  g_signal_emit (timeline, ges_timeline_signals[LAYER_ADDED], 0, layer);

  return TRUE;
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
  GST_DEBUG ("timeline:%p, track:%p", timeline, track);

  /* Add to the list of tracks, make sure we don't already control it */
  if (G_UNLIKELY (g_list_find (timeline->tracks, (gconstpointer) track))) {
    GST_WARNING ("Track is already controlled by this timeline");
    return FALSE;
  }

  /* Add the track to ourself (as a GstBin) 
   * Reference is taken ! */
  if (G_UNLIKELY (!gst_bin_add (GST_BIN (timeline), GST_ELEMENT (track)))) {
    GST_WARNING ("Couldn't add track to ourself (GST)");
    return FALSE;
  }

  /* Add the track to the list of tracks we track */
  timeline->tracks = g_list_append (timeline->tracks, track);

  /* Inform the track that it's currently being used by ourself */
  ges_track_set_timeline (track, timeline);

  GST_DEBUG ("Done adding track, emitting 'track-added' signal");

  /* emit 'track-added' */
  g_signal_emit (timeline, ges_timeline_signals[TRACK_ADDED], 0, track);

  return TRUE;
}

gboolean
ges_timeline_remove_track (GESTimeline * timeline, GESTrack * track)
{
  /* FIXME : IMPLEMENT */

  /* Signal track removal to all layers/objects */

  /* */
  return FALSE;
}
