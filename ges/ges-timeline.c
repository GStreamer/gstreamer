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
 * SECTION:ges-timeline
 * @short_description: Multimedia timeline
 *
 * #GESTimeline is the central object for any multimedia timeline.
 * 
 * Contains a list of #GESTimelineLayer which users should use to arrange the
 * various timeline objects through time.
 *
 * The output type is determined by the #GESTrack that are set on
 * the #GESTimeline.
 */

#include "gesmarshal.h"
#include "ges-internal.h"
#include "ges-timeline.h"
#include "ges-track.h"
#include "ges-timeline-layer.h"
#include "ges.h"


G_DEFINE_TYPE (GESTimeline, ges_timeline, GST_TYPE_BIN);

/* private structure to contain our track-related information */

typedef struct
{
  GESTimeline *timeline;
  GESTrack *track;
  GstPad *pad;                  /* Pad from the track */
  GstPad *ghostpad;
} TrackPrivate;

enum
{
  TRACK_ADDED,
  TRACK_REMOVED,
  LAYER_ADDED,
  LAYER_REMOVED,
  LAST_SIGNAL
};

static GstBinClass *parent_class;

static guint ges_timeline_signals[LAST_SIGNAL] = { 0 };

gint custom_find_track (TrackPrivate * priv, GESTrack * track);
static GstStateChangeReturn
ges_timeline_change_state (GstElement * element, GstStateChange transition);
static void
discoverer_ready_cb (GstDiscoverer * discoverer, GESTimeline * timeline);
static void
discoverer_discovered_cb (GstDiscoverer * discoverer,
    GstDiscovererInformation * info, GError * err, GESTimeline * timeline);

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
  GESTimeline *timeline = GES_TIMELINE (object);

  if (timeline->discoverer) {
    gst_discoverer_stop (timeline->discoverer);
    g_object_unref (timeline->discoverer);
    timeline->discoverer = NULL;
  }
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
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  parent_class = g_type_class_peek_parent (klass);

  element_class->change_state = ges_timeline_change_state;

  object_class->get_property = ges_timeline_get_property;
  object_class->set_property = ges_timeline_set_property;
  object_class->dispose = ges_timeline_dispose;
  object_class->finalize = ges_timeline_finalize;

  /**
   * GESTimeline::track-added
   * @timeline: the #GESTimeline
   * @track: the #GESTrack that was added to the timeline
   *
   * Will be emitted after the track was added to the timeline.
   */
  ges_timeline_signals[TRACK_ADDED] =
      g_signal_new ("track-added", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_FIRST, G_STRUCT_OFFSET (GESTimelineClass, track_added), NULL,
      NULL, ges_marshal_VOID__OBJECT, G_TYPE_NONE, 1, GES_TYPE_TRACK);

  /**
   * GESTimeline::track-removed
   * @timeline: the #GESTimeline
   * @track: the #GESTrack that was removed from the timeline
   *
   * Will be emitted after the track was removed from the timeline.
   */
  ges_timeline_signals[TRACK_REMOVED] =
      g_signal_new ("track-removed", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_FIRST, G_STRUCT_OFFSET (GESTimelineClass, track_removed),
      NULL, NULL, ges_marshal_VOID__OBJECT, G_TYPE_NONE, 1, GES_TYPE_TRACK);

  /**
   * GESTimeline::layer-added
   * @timeline: the #GESTimeline
   * @layer: the #GESTimelineLayer that was added to the timeline
   *
   * Will be emitted after the layer was added to the timeline.
   */
  ges_timeline_signals[LAYER_ADDED] =
      g_signal_new ("layer-added", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_FIRST, G_STRUCT_OFFSET (GESTimelineClass, layer_added), NULL,
      NULL, ges_marshal_VOID__OBJECT, G_TYPE_NONE, 1, GES_TYPE_TIMELINE_LAYER);

  /**
   * GESTimeline::layer-removed
   * @timeline: the #GESTimeline
   * @layer: the #GESTimelineLayer that was removed from the timeline
   *
   * Will be emitted after the layer was removed from the timeline.
   */
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

  /* New discoverer with a 15s timeout */
  self->discoverer = gst_discoverer_new (15 * GST_SECOND);
  g_signal_connect (self->discoverer, "ready", G_CALLBACK (discoverer_ready_cb),
      self);
  g_signal_connect (self->discoverer, "discovered",
      G_CALLBACK (discoverer_discovered_cb), self);
  gst_discoverer_start (self->discoverer);
}

/**
 * ges_timeline_new:
 *
 * Creates a new empty #GESTimeline.
 *
 * Returns: The new timeline.
 */

GESTimeline *
ges_timeline_new (void)
{
  return g_object_new (GES_TYPE_TIMELINE, NULL);
}

/**
 * ges_timeline_load_from_uri:
 * @uri: The URI to load from
 *
 * Creates a timeline from the contents of given uri.
 *
 * NOT_IMPLEMENTED !
 *
 * Returns: A new #GESTimeline if loading was successful, else NULL.
 */

GESTimeline *
ges_timeline_load_from_uri (gchar * uri)
{
  /* FIXME : IMPLEMENT */
  return NULL;
}

/**
 * ges_timeline_save:
 * @timeline: a #GESTimeline
 * @uri: The location to save to
 *
 * Saves the timeline to the given location
 *
 * NOT_IMPLEMENTED !
 *
 * Returns: TRUE if the timeline was successfully saved to the given location,
 * else FALSE.
 */

gboolean
ges_timeline_save (GESTimeline * timeline, gchar * uri)
{
  /* FIXME : IMPLEMENT */
  return FALSE;
}

static void
add_object_to_tracks (GESTimeline * timeline, GESTimelineObject * object)
{
  GList *tmp;

  for (tmp = timeline->tracks; tmp; tmp = g_list_next (tmp)) {
    TrackPrivate *priv = (TrackPrivate *) tmp->data;
    GESTrack *track = priv->track;
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
}

static void
do_async_start (GESTimeline * timeline)
{
  GstMessage *message;
  GList *tmp;

  timeline->async_pending = TRUE;

  /* Freeze state of tracks */
  for (tmp = timeline->tracks; tmp; tmp = tmp->next) {
    TrackPrivate *priv = (TrackPrivate *) tmp->data;
    gst_element_set_locked_state ((GstElement *) priv->track, TRUE);
  }

  message = gst_message_new_async_start (GST_OBJECT_CAST (timeline), FALSE);
  parent_class->handle_message (GST_BIN_CAST (timeline), message);
}

static void
do_async_done (GESTimeline * timeline)
{
  GstMessage *message;

  if (timeline->async_pending) {
    GList *tmp;
    /* Unfreeze state of tracks */
    for (tmp = timeline->tracks; tmp; tmp = tmp->next) {
      TrackPrivate *priv = (TrackPrivate *) tmp->data;
      gst_element_set_locked_state ((GstElement *) priv->track, FALSE);
      gst_element_sync_state_with_parent ((GstElement *) priv->track);
    }

    GST_DEBUG_OBJECT (timeline, "Emitting async-done");
    message = gst_message_new_async_done (GST_OBJECT_CAST (timeline));
    parent_class->handle_message (GST_BIN_CAST (timeline), message);

    timeline->async_pending = FALSE;
  }
}

static void
discoverer_ready_cb (GstDiscoverer * discoverer, GESTimeline * timeline)
{
  do_async_done (timeline);
}

static void
discoverer_discovered_cb (GstDiscoverer * discoverer,
    GstDiscovererInformation * info, GError * err, GESTimeline * timeline)
{
  GList *tmp;
  gboolean found = FALSE;
  GESTimelineFileSource *tfs = NULL;

  GST_DEBUG ("Discovered uri %s", info->uri);

  /* Find corresponding TimelineFileSource in the sources */
  for (tmp = timeline->pendingobjects; tmp; tmp = tmp->next) {
    tfs = (GESTimelineFileSource *) tmp->data;

    if (!g_strcmp0 (tfs->uri, info->uri)) {
      found = TRUE;
      break;
    }
  }

  if (found) {
    /* Remove object from list */
    timeline->pendingobjects =
        g_list_delete_link (timeline->pendingobjects, tmp);

    /* FIXME : Handle errors in discovery */

    /* Update timelinefilesource properties based on info */
    for (tmp = info->stream_list; tmp; tmp = tmp->next) {
      GstStreamInformation *sinf = (GstStreamInformation *) tmp->data;

      if (sinf->streamtype == GST_STREAM_AUDIO)
        tfs->supportedformats |= GES_TRACK_TYPE_AUDIO;
      else if ((sinf->streamtype == GST_STREAM_VIDEO) ||
          (sinf->streamtype == GST_STREAM_IMAGE))
        tfs->supportedformats |= GES_TRACK_TYPE_VIDEO;
    }

    g_object_set (tfs, "max-duration", info->duration, NULL);

    /* Continue the processing on tfs */
    add_object_to_tracks (timeline, GES_TIMELINE_OBJECT (tfs));
  }
}

static GstStateChangeReturn
ges_timeline_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
  GESTimeline *timeline = GES_TIMELINE (element);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      if (timeline->pendingobjects) {
        do_async_start (timeline);
        ret = GST_STATE_CHANGE_ASYNC;
      }
      break;
    default:
      break;
  }

  {
    GstStateChangeReturn bret;

    bret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
    if (G_UNLIKELY (bret == GST_STATE_CHANGE_NO_PREROLL)) {
      do_async_done (timeline);
      ret = bret;
    }
  }

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      do_async_done (timeline);
      break;
    default:
      break;
  }

  return ret;

}

static void
layer_object_added_cb (GESTimelineLayer * layer, GESTimelineObject * object,
    GESTimeline * timeline)
{
  GST_DEBUG ("New TimelineObject %p added to layer %p", object, layer);

  if (GES_IS_TIMELINE_FILE_SOURCE (object)) {
    GESTimelineFileSource *tfs = GES_TIMELINE_FILE_SOURCE (object);

    /* Send the filesource to the discoverer if:
     * * it doesn't have specified supported formats
     * * OR it doesn't have a specified max-duration
     * * OR it doesn't have a valid duration  */

    if (tfs->supportedformats == GES_TRACK_TYPE_UNKNOWN ||
        tfs->maxduration == GST_CLOCK_TIME_NONE || object->duration == 0) {
      GST_LOG ("Incomplete TimelineFileSource, discovering it");
      timeline->pendingobjects =
          g_list_append (timeline->pendingobjects, object);
      gst_discoverer_append_uri (timeline->discoverer,
          GES_TIMELINE_FILE_SOURCE (object)->uri);
    } else
      add_object_to_tracks (timeline, object);
  } else {
    add_object_to_tracks (timeline, object);
  }

  GST_DEBUG ("done");
}


static void
layer_object_removed_cb (GESTimelineLayer * layer, GESTimelineObject * object,
    GESTimeline * timeline)
{
  GList *tmp, *next;

  GST_DEBUG ("TimelineObject %p removed from layer %p", object, layer);

  /* Go over the object's track objects and figure out which one belongs to
   * the list of tracks we control */

  for (tmp = object->trackobjects; tmp; tmp = next) {
    GESTrackObject *trobj = (GESTrackObject *) tmp->data;

    next = g_list_next (tmp);

    GST_DEBUG ("Trying to remove TrackObject %p", trobj);
    if (G_LIKELY (g_list_find_custom (timeline->tracks,
                (gconstpointer) trobj->track,
                (GCompareFunc) custom_find_track))) {
      GST_DEBUG ("Belongs to one of the tracks we control");
      ges_track_remove_object (trobj->track, trobj);

      ges_timeline_object_release_track_object (object, trobj);
    }
  }

  GST_DEBUG ("Done");
}

/**
 * ges_timeline_add_layer:
 * @timeline: a #GESTimeline
 * @layer: the #GESTimelineLayer to add
 *
 * Add the layer to the timeline. The reference to the @layer will be stolen
 * by the @timeline.
 *
 * Returns: TRUE if the layer was properly added, else FALSE.
 */
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

  /* Reference is stolen */
  timeline->layers = g_list_append (timeline->layers, layer);

  /* Inform the layer that it belongs to a new timeline */
  ges_timeline_layer_set_timeline (layer, timeline);

  /* FIXME : GO OVER THE LIST OF ALREADY EXISTING TIMELINE OBJECTS IN THAT
   * LAYER AND ADD THEM !!! */

  /* Connect to 'object-added'/'object-removed' signal from the new layer */
  g_signal_connect (layer, "object-added", G_CALLBACK (layer_object_added_cb),
      timeline);
  g_signal_connect (layer, "object-removed",
      G_CALLBACK (layer_object_removed_cb), timeline);

  GST_DEBUG ("Done adding layer, emitting 'layer-added' signal");
  g_signal_emit (timeline, ges_timeline_signals[LAYER_ADDED], 0, layer);

  return TRUE;
}

/**
 * ges_timeline_remove_layer:
 * @timeline: a #GESTimeline
 * @layer: the #GESTimelineLayer to remove
 *
 * Removes the layer from the timeline. The reference that the @timeline holds on
 * the layer will be dropped. If you wish to use the @layer after calling this
 * method, you need to take a reference before calling.
 *
 * Returns: TRUE if the layer was properly removed, else FALSE.
 */

gboolean
ges_timeline_remove_layer (GESTimeline * timeline, GESTimelineLayer * layer)
{
  GST_DEBUG ("timeline:%p, layer:%p", timeline, layer);

  if (G_UNLIKELY (!g_list_find (timeline->layers, layer))) {
    GST_WARNING ("Layer doesn't belong to this timeline");
    return FALSE;
  }

  /* Disconnect signals */
  GST_DEBUG ("Disconnecting signal callbacks");
  g_signal_handlers_disconnect_by_func (layer, layer_object_added_cb, timeline);
  g_signal_handlers_disconnect_by_func (layer, layer_object_removed_cb,
      timeline);

  timeline->layers = g_list_remove (timeline->layers, layer);

  ges_timeline_layer_set_timeline (layer, NULL);

  g_signal_emit (timeline, ges_timeline_signals[LAYER_REMOVED], 0, layer);

  g_object_unref (layer);

  return TRUE;
}

static void
pad_added_cb (GESTrack * track, GstPad * pad, TrackPrivate * priv)
{
  gchar *padname;

  GST_DEBUG ("track:%p, pad:%s:%s", track, GST_DEBUG_PAD_NAME (pad));

  if (G_UNLIKELY (priv->pad)) {
    GST_WARNING ("We are already controlling a pad for this track");
    return;
  }

  /* Remember the pad */
  priv->pad = pad;

  /* ghost it ! */
  GST_DEBUG ("Ghosting pad and adding it to ourself");
  padname = g_strdup_printf ("track_%p_src", track);
  priv->ghostpad = gst_ghost_pad_new (padname, pad);
  g_free (padname);
  gst_pad_set_active (priv->ghostpad, TRUE);
  gst_element_add_pad (GST_ELEMENT (priv->timeline), priv->ghostpad);
}

static void
pad_removed_cb (GESTrack * track, GstPad * pad, TrackPrivate * priv)
{
  GST_DEBUG ("track:%p, pad:%s:%s", track, GST_DEBUG_PAD_NAME (pad));

  if (G_UNLIKELY (priv->pad != pad)) {
    GST_WARNING ("Not the pad we're controlling");
    return;
  }

  if (G_UNLIKELY (priv->ghostpad == NULL)) {
    GST_WARNING ("We don't have a ghostpad for this pad !");
    return;
  }

  GST_DEBUG ("Removing ghostpad");
  gst_pad_set_active (priv->ghostpad, FALSE);
  gst_element_remove_pad (GST_ELEMENT (priv->timeline), priv->ghostpad);
  priv->ghostpad = NULL;
  priv->pad = NULL;
}

gint
custom_find_track (TrackPrivate * priv, GESTrack * track)
{
  if (priv->track == track)
    return 0;
  return -1;
}

/**
 * ges_timeline_add_track:
 * @timeline: a #GESTimeline
 * @track: the #GESTrack to add
 *
 * Add a track to the timeline. The reference to the track will be stolen by the
 * pipeline.
 *
 * Returns: TRUE if the track was properly added, else FALSE.
 */

gboolean
ges_timeline_add_track (GESTimeline * timeline, GESTrack * track)
{
  GList *tmp;
  TrackPrivate *priv;

  GST_DEBUG ("timeline:%p, track:%p", timeline, track);

  /* make sure we don't already control it */
  if (G_UNLIKELY ((tmp =
              g_list_find_custom (timeline->tracks, (gconstpointer) track,
                  (GCompareFunc) custom_find_track)))) {
    GST_WARNING ("Track is already controlled by this timeline");
    return FALSE;
  }

  /* Add the track to ourself (as a GstBin) 
   * Reference is stolen ! */
  if (G_UNLIKELY (!gst_bin_add (GST_BIN (timeline), GST_ELEMENT (track)))) {
    GST_WARNING ("Couldn't add track to ourself (GST)");
    return FALSE;
  }

  priv = g_new0 (TrackPrivate, 1);
  priv->timeline = timeline;
  priv->track = track;

  /* Add the track to the list of tracks we track */
  timeline->tracks = g_list_append (timeline->tracks, priv);

  /* Listen to pad-added/-removed */
  g_signal_connect (track, "pad-added", (GCallback) pad_added_cb, priv);
  g_signal_connect (track, "pad-removed", (GCallback) pad_removed_cb, priv);

  /* Inform the track that it's currently being used by ourself */
  ges_track_set_timeline (track, timeline);

  GST_DEBUG ("Done adding track, emitting 'track-added' signal");

  /* emit 'track-added' */
  g_signal_emit (timeline, ges_timeline_signals[TRACK_ADDED], 0, track);

  return TRUE;
}

/**
 * ges_timeline_remove_track:
 * @timeline: a #GESTimeline
 * @track: the #GESTrack to remove
 *
 * Remove the @track from the @timeline. The reference stolen when adding the
 * @track will be removed. If you wish to use the @track after calling this
 * function you must ensure that you have a reference to it.
 *
 * Returns: TRUE if the @track was properly removed, else FALSE.
 */
gboolean
ges_timeline_remove_track (GESTimeline * timeline, GESTrack * track)
{
  GList *tmp;
  TrackPrivate *priv;

  GST_DEBUG ("timeline:%p, track:%p", timeline, track);

  if (G_UNLIKELY (!(tmp =
              g_list_find_custom (timeline->tracks, (gconstpointer) track,
                  (GCompareFunc) custom_find_track)))) {
    GST_WARNING ("Track doesn't belong to this timeline");
    return FALSE;
  }

  priv = tmp->data;
  timeline->tracks = g_list_remove (timeline->tracks, priv);

  ges_track_set_timeline (track, NULL);

  /* Remove ghost pad */
  if (priv->ghostpad) {
    GST_DEBUG ("Removing ghostpad");
    gst_pad_set_active (priv->ghostpad, FALSE);
    gst_ghost_pad_set_target ((GstGhostPad *) priv->ghostpad, NULL);
    gst_element_remove_pad (GST_ELEMENT (timeline), priv->ghostpad);
  }

  /* Remove pad-added/-removed handlers */
  g_signal_handlers_disconnect_by_func (track, pad_added_cb, priv);
  g_signal_handlers_disconnect_by_func (track, pad_removed_cb, priv);

  /* Signal track removal to all layers/objects */
  g_signal_emit (timeline, ges_timeline_signals[TRACK_REMOVED], 0, track);

  /* remove track from our bin */
  if (G_UNLIKELY (!gst_bin_remove (GST_BIN (timeline), GST_ELEMENT (track)))) {
    GST_WARNING ("Couldn't remove track to ourself (GST)");
    return FALSE;
  }

  g_free (priv);

  return TRUE;
}

/**
 * ges_timeline_get_track_for_pad:
 * @timeline: The #GESTimeline
 * @pad: The #GstPad
 *
 * Search the #GESTrack corresponding to the given @timeline's @pad.
 *
 * Returns: The corresponding #GESTrack if it is found, or #NULL if there is
 * an error.
 */

GESTrack *
ges_timeline_get_track_for_pad (GESTimeline * timeline, GstPad * pad)
{
  GList *tmp;

  for (tmp = timeline->tracks; tmp; tmp = g_list_next (tmp)) {
    TrackPrivate *priv = (TrackPrivate *) tmp->data;
    if (pad == priv->ghostpad)
      return priv->track;
  }

  return NULL;
}

/**
 * ges_timeline_get_tracks:
 * @timeline: a #GESTimeline
 *
 * Returns the list of #GESTrack used by the Timeline.
 *
 * Returns: A list of #GESTrack. The caller should unref each track
 * once he is done with them. */
GList *
ges_timeline_get_tracks (GESTimeline * timeline)
{
  GList *tmp, *res = NULL;

  for (tmp = timeline->tracks; tmp; tmp = g_list_next (tmp)) {
    TrackPrivate *priv = (TrackPrivate *) tmp->data;
    res = g_list_append (res, g_object_ref (priv->track));
  }

  return res;
}
