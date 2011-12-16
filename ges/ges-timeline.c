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
 *
 * To save/load a timeline, you can use the ges_timeline_load_from_uri() and
 * ges_timeline_save_to_uri() methods to use the default format. If you wish
 * to specify the format to save/load the timeline from, please consult the
 * documentation about #GESFormatter.
 */

#include "gesmarshal.h"
#include "ges-internal.h"
#include "ges-timeline.h"
#include "ges-track.h"
#include "ges-timeline-layer.h"
#include "ges.h"

static void track_duration_cb (GstElement * track,
    GParamSpec * arg G_GNUC_UNUSED, GESTimeline * timeline);

G_DEFINE_TYPE (GESTimeline, ges_timeline, GST_TYPE_BIN);

#define GES_TIMELINE_PENDINGOBJS_GET_LOCK(timeline) \
  (GES_TIMELINE(timeline)->priv->pendingobjects_lock)
#define GES_TIMELINE_PENDINGOBJS_LOCK(timeline) \
  (g_mutex_lock(GES_TIMELINE_PENDINGOBJS_GET_LOCK (timeline)))
#define GES_TIMELINE_PENDINGOBJS_UNLOCK(timeline) \
  (g_mutex_unlock(GES_TIMELINE_PENDINGOBJS_GET_LOCK (timeline)))

struct _GESTimelinePrivate
{
  GList *layers;                /* A list of GESTimelineLayer sorted by priority */
  GList *tracks;                /* A list of private track data */

  /* The duration of the timeline */
  gint64 duration;

  /* discoverer used for virgin sources */
  GstDiscoverer *discoverer;
  GList *pendingobjects;
  /* lock to avoid discovery of objects that will be removed */
  GMutex *pendingobjects_lock;

  /* Whether we are changing state asynchronously or not */
  gboolean async_pending;
};

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
  PROP_0,
  PROP_DURATION,
  PROP_LAST
};

static GParamSpec *properties[PROP_LAST];

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

static gint custom_find_track (TrackPrivate * tr_priv, GESTrack * track);
static GstStateChangeReturn
ges_timeline_change_state (GstElement * element, GstStateChange transition);
static void
discoverer_finished_cb (GstDiscoverer * discoverer, GESTimeline * timeline);
static void
discoverer_discovered_cb (GstDiscoverer * discoverer,
    GstDiscovererInfo * info, GError * err, GESTimeline * timeline);

static void
ges_timeline_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GESTimeline *timeline = GES_TIMELINE (object);

  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    case PROP_DURATION:
      g_value_set_uint64 (value, timeline->priv->duration);
      break;
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
  GESTimelinePrivate *priv = GES_TIMELINE (object)->priv;

  if (priv->discoverer) {
    gst_discoverer_stop (priv->discoverer);
    g_object_unref (priv->discoverer);
    priv->discoverer = NULL;
  }

  while (priv->layers) {
    GESTimelineLayer *layer = (GESTimelineLayer *) priv->layers->data;
    ges_timeline_remove_layer (GES_TIMELINE (object), layer);
  }

  /* FIXME: it should be possible to remove tracks before removing
   * layers, but at the moment this creates a problem because the track
   * objects aren't notified that their gnlobjects have been destroyed.
   */

  while (priv->tracks) {
    TrackPrivate *tr_priv = (TrackPrivate *) priv->tracks->data;
    ges_timeline_remove_track (GES_TIMELINE (object), tr_priv->track);
  }

  G_OBJECT_CLASS (ges_timeline_parent_class)->dispose (object);
}

static void
ges_timeline_finalize (GObject * object)
{
  GESTimeline *timeline = GES_TIMELINE (object);

  g_mutex_free (timeline->priv->pendingobjects_lock);

  G_OBJECT_CLASS (ges_timeline_parent_class)->finalize (object);
}

static void
ges_timeline_class_init (GESTimelineClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  g_type_class_add_private (klass, sizeof (GESTimelinePrivate));

  parent_class = g_type_class_peek_parent (klass);

  element_class->change_state = ges_timeline_change_state;

  object_class->get_property = ges_timeline_get_property;
  object_class->set_property = ges_timeline_set_property;
  object_class->dispose = ges_timeline_dispose;
  object_class->finalize = ges_timeline_finalize;

  /**
   * GESTimelineObject:duration
   *
   * Current duration (in nanoseconds) of the #GESTimeline
   *
   * Default value: 0
   */
  properties[PROP_DURATION] =
      g_param_spec_uint64 ("duration", "Duration",
      "The duration of the timeline", 0, G_MAXUINT64,
      GST_CLOCK_TIME_NONE, G_PARAM_READABLE);
  g_object_class_install_property (object_class, PROP_DURATION,
      properties[PROP_DURATION]);

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

  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GES_TYPE_TIMELINE, GESTimelinePrivate);

  self->priv->layers = NULL;
  self->priv->tracks = NULL;
  self->priv->duration = 0;

  self->priv->pendingobjects_lock = g_mutex_new ();
  /* New discoverer with a 15s timeout */
  self->priv->discoverer = gst_discoverer_new (15 * GST_SECOND, NULL);
  g_signal_connect (self->priv->discoverer, "finished",
      G_CALLBACK (discoverer_finished_cb), self);
  g_signal_connect (self->priv->discoverer, "discovered",
      G_CALLBACK (discoverer_discovered_cb), self);
  gst_discoverer_start (self->priv->discoverer);
}

static gint
sort_layers (gpointer a, gpointer b)
{
  GESTimelineLayer *layer_a, *layer_b;
  guint prio_a, prio_b;

  layer_a = GES_TIMELINE_LAYER (a);
  layer_b = GES_TIMELINE_LAYER (b);

  prio_a = ges_timeline_layer_get_priority (layer_a);
  prio_b = ges_timeline_layer_get_priority (layer_b);

  if ((gint) prio_a > (guint) prio_b)
    return 1;
  if ((guint) prio_a < (guint) prio_b)
    return -1;

  return 0;
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
 * ges_timeline_new_from_uri:
 * @uri: the URI to load from
 *
 * Creates a timeline from the given URI.
 *
 * Returns: A new timeline if the uri was loaded successfully, or NULL if the
 * uri could not be loaded
 */

GESTimeline *
ges_timeline_new_from_uri (const gchar * uri)
{
  GESTimeline *ret;

  /* FIXME : we should have a GError** argument so the user can know why
   * it wasn't able to load the uri
   */

  ret = ges_timeline_new ();

  if (!ges_timeline_load_from_uri (ret, uri)) {
    g_object_unref (ret);
    return NULL;
  }

  return ret;
}


/**
 * ges_timeline_load_from_uri:
 * @timeline: an empty #GESTimeline into which to load the formatter
 * @uri: The URI to load from
 *
 * Loads the contents of URI into the given timeline.
 *
 * Returns: TRUE if the timeline was loaded successfully, or FALSE if the uri
 * could not be loaded.
 */

gboolean
ges_timeline_load_from_uri (GESTimeline * timeline, const gchar * uri)
{
  GESFormatter *p = NULL;
  gboolean ret = FALSE;

  /* FIXME : we should have a GError** argument so the user can know why
   * it wasn't able to load the uri
   */

  if (!(p = ges_formatter_new_for_uri (uri))) {
    GST_ERROR ("unsupported uri '%s'", uri);
    goto fail;
  }

  if (!ges_formatter_load_from_uri (p, timeline, uri)) {
    GST_ERROR ("error deserializing formatter");
    goto fail;
  }

  ret = TRUE;

fail:
  if (p)
    g_object_unref (p);
  return ret;
}

/**
 * ges_timeline_save_to_uri:
 * @timeline: a #GESTimeline
 * @uri: The location to save to
 *
 * Saves the timeline to the given location
 *
 * Returns: TRUE if the timeline was successfully saved to the given location,
 * else FALSE.
 */

gboolean
ges_timeline_save_to_uri (GESTimeline * timeline, const gchar * uri)
{
  GESFormatter *p = NULL;
  gboolean ret = FALSE;

  /* FIXME : How will the user be able to chose the format he
   * wishes to store to ? */

  /* FIXME : How will we ensure a timeline loaded with a certain format
   * will be saved with the same one by default ? We need to make this
   * easy from an API perspective */

  /* FIXME : we should have a GError** argument so the user can know why
   * it wasn't able to save
   */

  if (!(p = ges_formatter_new_for_uri (uri))) {
    GST_ERROR ("unsupported uri '%s'", uri);
    goto fail;
  }

  if (!ges_formatter_save_to_uri (p, timeline, uri)) {
    GST_ERROR ("error serializing formatter");
    goto fail;
  }

  ret = TRUE;

fail:
  if (p)
    g_object_unref (p);
  return ret;
}

static void
add_object_to_track (GESTimelineObject * object, GESTrack * track)
{
  if (!ges_timeline_object_create_track_objects (object, track)) {
    GST_WARNING ("error creating track objects");
  }
}

static void
add_object_to_tracks (GESTimeline * timeline, GESTimelineObject * object)
{
  GList *tmp;

  for (tmp = timeline->priv->tracks; tmp; tmp = g_list_next (tmp)) {
    TrackPrivate *tr_priv = (TrackPrivate *) tmp->data;
    GESTrack *track = tr_priv->track;

    GST_LOG ("Trying with track %p", track);
    add_object_to_track (object, track);
  }
}


static void
do_async_start (GESTimeline * timeline)
{
  GstMessage *message;
  GList *tmp;

  timeline->priv->async_pending = TRUE;

  /* Freeze state of tracks */
  for (tmp = timeline->priv->tracks; tmp; tmp = tmp->next) {
    TrackPrivate *tr_priv = (TrackPrivate *) tmp->data;
    gst_element_set_locked_state ((GstElement *) tr_priv->track, TRUE);
  }

  message = gst_message_new_async_start (GST_OBJECT_CAST (timeline), FALSE);
  parent_class->handle_message (GST_BIN_CAST (timeline), message);
}

static void
do_async_done (GESTimeline * timeline)
{
  GstMessage *message;

  if (timeline->priv->async_pending) {
    GList *tmp;
    /* Unfreeze state of tracks */
    for (tmp = timeline->priv->tracks; tmp; tmp = tmp->next) {
      TrackPrivate *tr_priv = (TrackPrivate *) tmp->data;
      gst_element_set_locked_state ((GstElement *) tr_priv->track, FALSE);
      gst_element_sync_state_with_parent ((GstElement *) tr_priv->track);
    }

    GST_DEBUG_OBJECT (timeline, "Emitting async-done");
    message = gst_message_new_async_done (GST_OBJECT_CAST (timeline));
    parent_class->handle_message (GST_BIN_CAST (timeline), message);

    timeline->priv->async_pending = FALSE;
  }
}

static void
discoverer_finished_cb (GstDiscoverer * discoverer, GESTimeline * timeline)
{
  do_async_done (timeline);
}

static void
discoverer_discovered_cb (GstDiscoverer * discoverer,
    GstDiscovererInfo * info, GError * err, GESTimeline * timeline)
{
  GList *tmp;
  gboolean found = FALSE;
  gboolean is_image = FALSE;
  GESTimelineFileSource *tfs = NULL;
  GESTimelinePrivate *priv = timeline->priv;
  const gchar *uri = gst_discoverer_info_get_uri (info);

  GST_DEBUG ("Discovered uri %s", uri);

  GES_TIMELINE_PENDINGOBJS_LOCK (timeline);

  /* Find corresponding TimelineFileSource in the sources */
  for (tmp = priv->pendingobjects; tmp; tmp = tmp->next) {
    tfs = (GESTimelineFileSource *) tmp->data;

    if (!g_strcmp0 (ges_timeline_filesource_get_uri (tfs), uri)) {
      found = TRUE;
      break;
    }
  }

  if (found) {
    GList *stream_list;
    GESTrackType tfs_supportedformats;

    /* The timeline file source will be updated with discovered information
     * so it needs to not be finalized during this process */
    g_object_ref (tfs);

    /* Remove object from list */
    priv->pendingobjects = g_list_delete_link (priv->pendingobjects, tmp);
    GES_TIMELINE_PENDINGOBJS_UNLOCK (timeline);

    /* FIXME : Handle errors in discovery */
    stream_list = gst_discoverer_info_get_stream_list (info);

    tfs_supportedformats = ges_timeline_filesource_get_supported_formats (tfs);
    if (tfs_supportedformats != GES_TRACK_TYPE_UNKNOWN)
      goto check_image;

    /* Update timelinefilesource properties based on info */
    for (tmp = stream_list; tmp; tmp = tmp->next) {
      GstDiscovererStreamInfo *sinf = (GstDiscovererStreamInfo *) tmp->data;

      if (GST_IS_DISCOVERER_AUDIO_INFO (sinf)) {
        tfs_supportedformats |= GES_TRACK_TYPE_AUDIO;
        ges_timeline_filesource_set_supported_formats (tfs,
            tfs_supportedformats);
      } else if (GST_IS_DISCOVERER_VIDEO_INFO (sinf)) {
        tfs_supportedformats |= GES_TRACK_TYPE_VIDEO;
        ges_timeline_filesource_set_supported_formats (tfs,
            tfs_supportedformats);
        if (gst_discoverer_video_info_is_image ((GstDiscovererVideoInfo *)
                sinf)) {
          tfs_supportedformats |= GES_TRACK_TYPE_AUDIO;
          ges_timeline_filesource_set_supported_formats (tfs,
              tfs_supportedformats);
          is_image = TRUE;
        }
      }
    }

    if (stream_list)
      gst_discoverer_stream_info_list_free (stream_list);

  check_image:

    if (is_image) {
      /* don't set max-duration on still images */
      g_object_set (tfs, "is_image", (gboolean) TRUE, NULL);
    }

    else {
      g_object_set (tfs, "max-duration",
          gst_discoverer_info_get_duration (info), NULL);
    }

    /* Continue the processing on tfs */
    add_object_to_tracks (timeline, GES_TIMELINE_OBJECT (tfs));

    /* Remove the ref as the timeline file source is no longer needed here */
    g_object_unref (tfs);
  } else {
    GES_TIMELINE_PENDINGOBJS_UNLOCK (timeline);
  }
}

static GstStateChangeReturn
ges_timeline_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
  GESTimeline *timeline = GES_TIMELINE (element);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      GES_TIMELINE_PENDINGOBJS_LOCK (timeline);
      if (timeline->priv->pendingobjects) {
        GES_TIMELINE_PENDINGOBJS_UNLOCK (timeline);
        do_async_start (timeline);
        ret = GST_STATE_CHANGE_ASYNC;
      } else {
        GES_TIMELINE_PENDINGOBJS_UNLOCK (timeline);
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
  if (ges_timeline_object_is_moving_from_layer (object)) {
    GST_DEBUG ("TimelineObject %p is moving from a layer to another, not doing"
        " anything on it", object);
    return;
  }

  GST_DEBUG ("New TimelineObject %p added to layer %p", object, layer);

  if (GES_IS_TIMELINE_FILE_SOURCE (object)) {
    GESTimelineFileSource *tfs = GES_TIMELINE_FILE_SOURCE (object);
    GESTrackType tfs_supportedformats =
        ges_timeline_filesource_get_supported_formats (tfs);
    guint64 tfs_maxdur = ges_timeline_filesource_get_max_duration (tfs);
    const gchar *tfs_uri;

    /* Send the filesource to the discoverer if:
     * * it doesn't have specified supported formats
     * * OR it doesn't have a specified max-duration
     * * OR it doesn't have a valid duration  */

    if (tfs_supportedformats == GES_TRACK_TYPE_UNKNOWN ||
        tfs_maxdur == GST_CLOCK_TIME_NONE || object->duration == 0) {
      GST_LOG ("Incomplete TimelineFileSource, discovering it");
      tfs_uri = ges_timeline_filesource_get_uri (tfs);

      GES_TIMELINE_PENDINGOBJS_LOCK (timeline);
      timeline->priv->pendingobjects =
          g_list_append (timeline->priv->pendingobjects, object);
      GES_TIMELINE_PENDINGOBJS_UNLOCK (timeline);

      gst_discoverer_discover_uri_async (timeline->priv->discoverer, tfs_uri);
    } else
      add_object_to_tracks (timeline, object);
  } else {
    add_object_to_tracks (timeline, object);
  }

  GST_DEBUG ("done");
}

static void
layer_priority_changed_cb (GESTimelineLayer * layer,
    GParamSpec * arg G_GNUC_UNUSED, GESTimeline * timeline)
{
  timeline->priv->layers = g_list_sort (timeline->priv->layers, (GCompareFunc)
      sort_layers);
}

static void
layer_object_removed_cb (GESTimelineLayer * layer, GESTimelineObject * object,
    GESTimeline * timeline)
{
  GList *tmp, *trackobjects;

  if (ges_timeline_object_is_moving_from_layer (object)) {
    GST_DEBUG ("TimelineObject %p is moving from a layer to another, not doing"
        " anything on it", object);
    return;
  }

  GST_DEBUG ("TimelineObject %p removed from layer %p", object, layer);

  /* Go over the object's track objects and figure out which one belongs to
   * the list of tracks we control */

  trackobjects = ges_timeline_object_get_track_objects (object);
  for (tmp = trackobjects; tmp; tmp = tmp->next) {
    GESTrackObject *trobj = (GESTrackObject *) tmp->data;

    GST_DEBUG ("Trying to remove TrackObject %p", trobj);
    if (G_LIKELY (g_list_find_custom (timeline->priv->tracks,
                ges_track_object_get_track (trobj),
                (GCompareFunc) custom_find_track))) {
      GST_DEBUG ("Belongs to one of the tracks we control");
      ges_track_remove_object (ges_track_object_get_track (trobj), trobj);

      ges_timeline_object_release_track_object (object, trobj);
    }

    /* removing the reference added by _get_track_objects() */
    g_object_unref (trobj);
  }
  g_list_free (trackobjects);

  /* if the object is a timeline file source that has not yet been discovered,
   * it no longer needs to be discovered so remove it from the pendingobjects
   * list if it belongs to this layer */
  if (GES_IS_TIMELINE_FILE_SOURCE (object)) {
    GES_TIMELINE_PENDINGOBJS_LOCK (timeline);
    timeline->priv->pendingobjects =
        g_list_remove_all (timeline->priv->pendingobjects, object);
    GES_TIMELINE_PENDINGOBJS_UNLOCK (timeline);
  }

  GST_DEBUG ("Done");
}

/**
 * ges_timeline_append_layer:
 * @timeline: a #GESTimeline
 * @layer: the #GESTimelineLayer to add
 *
 * Convenience method to append @layer to @timeline which means that the
 * priority of @layer is changed to correspond to the last layer of @timeline.
 * The reference to the @layer will be stolen by @timeline.
 *
 * Returns: TRUE if the layer was properly added, else FALSE.
 */
gboolean
ges_timeline_append_layer (GESTimeline * timeline, GESTimelineLayer * layer)
{
  guint32 priority;
  GESTimelinePrivate *priv = timeline->priv;

  GST_DEBUG ("Appending layer to layer:%p, timeline:%p", timeline, layer);
  priority = g_list_length (priv->layers);

  ges_timeline_layer_set_priority (layer, priority);

  return ges_timeline_add_layer (timeline, layer);
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
  GList *objects, *tmp;
  GESTimelinePrivate *priv = timeline->priv;

  GST_DEBUG ("timeline:%p, layer:%p", timeline, layer);

  /* We can only add a layer that doesn't already belong to another timeline */
  if (G_UNLIKELY (layer->timeline)) {
    GST_WARNING ("Layer belongs to another timeline, can't add it");
    return FALSE;
  }

  /* Add to the list of layers, make sure we don't already control it */
  if (G_UNLIKELY (g_list_find (priv->layers, (gconstpointer) layer))) {
    GST_WARNING ("Layer is already controlled by this timeline");
    return FALSE;
  }

  g_object_ref_sink (layer);
  priv->layers = g_list_insert_sorted (priv->layers, layer,
      (GCompareFunc) sort_layers);

  /* Inform the layer that it belongs to a new timeline */
  ges_timeline_layer_set_timeline (layer, timeline);

  /* Connect to 'object-added'/'object-removed' signal from the new layer */
  g_signal_connect (layer, "object-added", G_CALLBACK (layer_object_added_cb),
      timeline);
  g_signal_connect (layer, "object-removed",
      G_CALLBACK (layer_object_removed_cb), timeline);
  g_signal_connect (layer, "notify::priority",
      G_CALLBACK (layer_priority_changed_cb), timeline);

  GST_DEBUG ("Done adding layer, emitting 'layer-added' signal");
  g_signal_emit (timeline, ges_timeline_signals[LAYER_ADDED], 0, layer);

  /* add any existing timeline objects to the timeline */
  objects = ges_timeline_layer_get_objects (layer);
  for (tmp = objects; tmp; tmp = tmp->next) {
    layer_object_added_cb (layer, tmp->data, timeline);
    g_object_unref (tmp->data);
    tmp->data = NULL;
  }
  g_list_free (objects);

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
  GList *layer_objects, *tmp;
  GESTimelinePrivate *priv = timeline->priv;

  GST_DEBUG ("timeline:%p, layer:%p", timeline, layer);

  if (G_UNLIKELY (!g_list_find (priv->layers, layer))) {
    GST_WARNING ("Layer doesn't belong to this timeline");
    return FALSE;
  }

  /* remove objects from any private data structures */

  layer_objects = ges_timeline_layer_get_objects (layer);
  for (tmp = layer_objects; tmp; tmp = tmp->next) {
    layer_object_removed_cb (layer, GES_TIMELINE_OBJECT (tmp->data), timeline);
    g_object_unref (G_OBJECT (tmp->data));
    tmp->data = NULL;
  }
  g_list_free (layer_objects);

  /* Disconnect signals */
  GST_DEBUG ("Disconnecting signal callbacks");
  g_signal_handlers_disconnect_by_func (layer, layer_object_added_cb, timeline);
  g_signal_handlers_disconnect_by_func (layer, layer_object_removed_cb,
      timeline);

  priv->layers = g_list_remove (priv->layers, layer);

  ges_timeline_layer_set_timeline (layer, NULL);

  g_signal_emit (timeline, ges_timeline_signals[LAYER_REMOVED], 0, layer);

  g_object_unref (layer);

  return TRUE;
}

static void
pad_added_cb (GESTrack * track, GstPad * pad, TrackPrivate * tr_priv)
{
  gchar *padname;


  GST_DEBUG ("track:%p, pad:%s:%s", track, GST_DEBUG_PAD_NAME (pad));

  if (G_UNLIKELY (tr_priv->pad)) {
    GST_WARNING ("We are already controlling a pad for this track");
    return;
  }

  /* Remember the pad */
  tr_priv->pad = pad;

  /* ghost it ! */
  GST_DEBUG ("Ghosting pad and adding it to ourself");
  padname = g_strdup_printf ("track_%p_src", track);
  tr_priv->ghostpad = gst_ghost_pad_new (padname, pad);
  g_free (padname);
  gst_pad_set_active (tr_priv->ghostpad, TRUE);
  gst_element_add_pad (GST_ELEMENT (tr_priv->timeline), tr_priv->ghostpad);
}

static void
pad_removed_cb (GESTrack * track, GstPad * pad, TrackPrivate * tr_priv)
{
  GST_DEBUG ("track:%p, pad:%s:%s", track, GST_DEBUG_PAD_NAME (pad));

  if (G_UNLIKELY (tr_priv->pad != pad)) {
    GST_WARNING ("Not the pad we're controlling");
    return;
  }

  if (G_UNLIKELY (tr_priv->ghostpad == NULL)) {
    GST_WARNING ("We don't have a ghostpad for this pad !");
    return;
  }

  GST_DEBUG ("Removing ghostpad");
  gst_pad_set_active (tr_priv->ghostpad, FALSE);
  gst_element_remove_pad (GST_ELEMENT (tr_priv->timeline), tr_priv->ghostpad);
  tr_priv->ghostpad = NULL;
  tr_priv->pad = NULL;
}

static gint
custom_find_track (TrackPrivate * tr_priv, GESTrack * track)
{
  if (tr_priv->track == track)
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

/* FIXME: create track objects for timeline objects which have already been
 * added to existing layers.
 */

gboolean
ges_timeline_add_track (GESTimeline * timeline, GESTrack * track)
{
  TrackPrivate *tr_priv;
  GESTimelinePrivate *priv = timeline->priv;
  GList *tmp;

  GST_DEBUG ("timeline:%p, track:%p", timeline, track);

  /* make sure we don't already control it */
  if (G_UNLIKELY (g_list_find_custom (priv->tracks, (gconstpointer) track,
              (GCompareFunc) custom_find_track))) {
    GST_WARNING ("Track is already controlled by this timeline");
    return FALSE;
  }

  /* Add the track to ourself (as a GstBin)
   * Reference is stolen ! */
  if (G_UNLIKELY (!gst_bin_add (GST_BIN (timeline), GST_ELEMENT (track)))) {
    GST_WARNING ("Couldn't add track to ourself (GST)");
    return FALSE;
  }

  tr_priv = g_new0 (TrackPrivate, 1);
  tr_priv->timeline = timeline;
  tr_priv->track = track;

  /* Add the track to the list of tracks we track */
  priv->tracks = g_list_append (priv->tracks, tr_priv);

  /* Listen to pad-added/-removed */
  g_signal_connect (track, "pad-added", (GCallback) pad_added_cb, tr_priv);
  g_signal_connect (track, "pad-removed", (GCallback) pad_removed_cb, tr_priv);

  /* Inform the track that it's currently being used by ourself */
  ges_track_set_timeline (track, timeline);

  GST_DEBUG ("Done adding track, emitting 'track-added' signal");

  /* emit 'track-added' */
  g_signal_emit (timeline, ges_timeline_signals[TRACK_ADDED], 0, track);

  /* ensure that each existing timeline object has the opportunity to create a
   * track object for this track*/

  for (tmp = priv->layers; tmp; tmp = tmp->next) {
    GList *objects, *obj;
    objects = ges_timeline_layer_get_objects (tmp->data);

    for (obj = objects; obj; obj = obj->next) {
      add_object_to_track (obj->data, track);
      g_object_unref (obj->data);
      obj->data = NULL;
    }
    g_list_free (objects);
  }

  /* We connect to the duration change notify, so we can update
   * our duration accordingly */
  g_signal_connect (G_OBJECT (track), "notify::duration",
      G_CALLBACK (track_duration_cb), timeline);
  track_duration_cb (GST_ELEMENT (track), NULL, timeline);

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

/* FIXME: release any track objects associated with this layer. currenly this
 * will not happen if you remove the track before removing *all*
 * timelineobjects which have a track object in this track.
 */

gboolean
ges_timeline_remove_track (GESTimeline * timeline, GESTrack * track)
{
  GList *tmp;
  TrackPrivate *tr_priv;
  GESTimelinePrivate *priv = timeline->priv;

  GST_DEBUG ("timeline:%p, track:%p", timeline, track);

  if (G_UNLIKELY (!(tmp =
              g_list_find_custom (priv->tracks, (gconstpointer) track,
                  (GCompareFunc) custom_find_track)))) {
    GST_WARNING ("Track doesn't belong to this timeline");
    return FALSE;
  }

  tr_priv = tmp->data;
  priv->tracks = g_list_remove (priv->tracks, tr_priv);

  ges_track_set_timeline (track, NULL);

  /* Remove ghost pad */
  if (tr_priv->ghostpad) {
    GST_DEBUG ("Removing ghostpad");
    gst_pad_set_active (tr_priv->ghostpad, FALSE);
    gst_ghost_pad_set_target ((GstGhostPad *) tr_priv->ghostpad, NULL);
    gst_element_remove_pad (GST_ELEMENT (timeline), tr_priv->ghostpad);
  }

  /* Remove pad-added/-removed handlers */
  g_signal_handlers_disconnect_by_func (track, pad_added_cb, tr_priv);
  g_signal_handlers_disconnect_by_func (track, pad_removed_cb, tr_priv);
  g_signal_handlers_disconnect_by_func (track, track_duration_cb,
      tr_priv->track);

  /* Signal track removal to all layers/objects */
  g_signal_emit (timeline, ges_timeline_signals[TRACK_REMOVED], 0, track);

  /* remove track from our bin */
  gst_object_ref (track);
  if (G_UNLIKELY (!gst_bin_remove (GST_BIN (timeline), GST_ELEMENT (track)))) {
    GST_WARNING ("Couldn't remove track to ourself (GST)");
    gst_object_unref (track);
    return FALSE;
  }

  /* set track state to NULL */

  gst_element_set_state (GST_ELEMENT (track), GST_STATE_NULL);

  gst_object_unref (track);

  g_free (tr_priv);

  return TRUE;
}

/**
 * ges_timeline_get_track_for_pad:
 * @timeline: The #GESTimeline
 * @pad: The #GstPad
 *
 * Search the #GESTrack corresponding to the given @timeline's @pad.
 *
 * Returns: (transfer none): The corresponding #GESTrack if it is found,
 * or %NULL if there is an error.
 */

GESTrack *
ges_timeline_get_track_for_pad (GESTimeline * timeline, GstPad * pad)
{
  GList *tmp;

  for (tmp = timeline->priv->tracks; tmp; tmp = g_list_next (tmp)) {
    TrackPrivate *tr_priv = (TrackPrivate *) tmp->data;
    if (pad == tr_priv->ghostpad)
      return tr_priv->track;
  }

  return NULL;
}

/**
 * ges_timeline_get_tracks:
 * @timeline: a #GESTimeline
 *
 * Returns the list of #GESTrack used by the Timeline.
 *
 * Returns: (transfer full) (element-type GESTrack): A list of #GESTrack.
 * The caller should unref each track once he is done with them.
 */
GList *
ges_timeline_get_tracks (GESTimeline * timeline)
{
  GList *tmp, *res = NULL;

  for (tmp = timeline->priv->tracks; tmp; tmp = g_list_next (tmp)) {
    TrackPrivate *tr_priv = (TrackPrivate *) tmp->data;
    res = g_list_append (res, g_object_ref (tr_priv->track));
  }

  return res;
}

/**
 * ges_timeline_get_layers:
 * @timeline: a #GESTimeline
 *
 * Get the list of #GESTimelineLayer present in the Timeline.
 *
 * Returns: (transfer full) (element-type GESTimelineLayer): the list of
 * #GESTimelineLayer present in the Timeline sorted by priority.
 * The caller should unref each Layer once he is done with them.
 */
GList *
ges_timeline_get_layers (GESTimeline * timeline)
{
  GList *tmp, *res = NULL;

  for (tmp = timeline->priv->layers; tmp; tmp = g_list_next (tmp)) {
    res = g_list_insert_sorted (res, g_object_ref (tmp->data),
        (GCompareFunc) sort_layers);
  }

  return res;
}

/**
 * ges_timeline_enable_update:
 * @timeline: a #GESTimeline
 * @enabled: TRUE if the timeline must be updated, FALSE otherwise.
 *
 * Calls the enable_update function of the tracks contained by the timeline.
 *
 * Returns: True if success, FALSE otherwise.
 */
gboolean
ges_timeline_enable_update (GESTimeline * timeline, gboolean enabled)
{
  GList *tmp, *tracks;
  gboolean res = TRUE;

  tracks = ges_timeline_get_tracks (timeline);

  for (tmp = tracks; tmp; tmp = tmp->next) {
    if (!ges_track_enable_update (tmp->data, enabled)) {
      res = FALSE;
    }
  }

  g_list_free (tracks);

  return res;
}

static void
track_duration_cb (GstElement * track,
    GParamSpec * arg G_GNUC_UNUSED, GESTimeline * timeline)
{
  guint64 duration, max_duration = 0;
  GList *tmp;

  for (tmp = timeline->priv->tracks; tmp; tmp = g_list_next (tmp)) {
    TrackPrivate *tr_priv = (TrackPrivate *) tmp->data;
    g_object_get (tr_priv->track, "duration", &duration, NULL);
    GST_DEBUG ("track duration : %" GST_TIME_FORMAT, GST_TIME_ARGS (duration));
    max_duration = MAX (duration, max_duration);
  }

  if (timeline->priv->duration != max_duration) {
    GST_DEBUG ("track duration : %" GST_TIME_FORMAT " current : %"
        GST_TIME_FORMAT, GST_TIME_ARGS (max_duration),
        GST_TIME_ARGS (timeline->priv->duration));

    timeline->priv->duration = max_duration;

#if GLIB_CHECK_VERSION(2,26,0)
    g_object_notify_by_pspec (G_OBJECT (timeline), properties[PROP_DURATION]);
#else
    g_object_notify (G_OBJECT (timeline), "duration");
#endif
  }
}
