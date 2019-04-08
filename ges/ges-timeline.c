/* GStreamer Editing Services
 * Copyright (C) 2009 Edward Hervey <edward.hervey@collabora.co.uk>
 *               2009 Nokia Corporation
 *               2012 Thibault Saunier <tsaunier@gnome.org>
 *               2012 Collabora Ltd.
 *                 Author: Sebastian Dröge <sebastian.droege@collabora.co.uk>
 *               2019 Igalia S.L
 *                 Author: Thibault Saunier <tsaunier@igalia.com>
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
 * SECTION:gestimeline
 * @title: GESTimeline
 * @short_description: Multimedia timeline
 *
 * #GESTimeline is the central object for any multimedia timeline.
 *
 * Contains a list of #GESLayer which users should use to arrange the
 * various clips through time.
 *
 * The output type is determined by the #GESTrack that are set on
 * the #GESTimeline.
 *
 * To save/load a timeline, you can use the ges_timeline_load_from_uri() and
 * ges_timeline_save_to_uri() methods to use the default format. If you wish
 *
 * Note that any change you make in the timeline will not actually be taken
 * into account until you call the #ges_timeline_commit method.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "ges-internal.h"
#include "ges-project.h"
#include "ges-container.h"
#include "ges-timeline.h"
#include "ges-timeline-tree.h"
#include "ges-track.h"
#include "ges-layer.h"
#include "ges-auto-transition.h"
#include "ges.h"


static GPtrArray *select_tracks_for_object_default (GESTimeline * timeline,
    GESClip * clip, GESTrackElement * tr_obj, gpointer user_data);
static void ges_extractable_interface_init (GESExtractableInterface * iface);
static void ges_meta_container_interface_init
    (GESMetaContainerInterface * iface);

GST_DEBUG_CATEGORY_STATIC (ges_timeline_debug);
#undef GST_CAT_DEFAULT
#define GST_CAT_DEFAULT ges_timeline_debug

/* lock to protect dynamic callbacks, like pad-added */
#define DYN_LOCK(timeline) (&GES_TIMELINE (timeline)->priv->dyn_mutex)
#define LOCK_DYN(timeline) G_STMT_START {                       \
    GST_LOG_OBJECT (timeline, "Getting dynamic lock from %p", \
        g_thread_self());                                       \
    g_rec_mutex_lock (DYN_LOCK (timeline));                     \
    GST_LOG_OBJECT (timeline, "Got Dynamic lock from %p",     \
        g_thread_self());         \
  } G_STMT_END

#define UNLOCK_DYN(timeline) G_STMT_START {                         \
    GST_LOG_OBJECT (timeline, "Unlocking dynamic lock from %p", \
        g_thread_self());                                         \
    g_rec_mutex_unlock (DYN_LOCK (timeline));                     \
    GST_LOG_OBJECT (timeline, "Unlocked Dynamic lock from %p",  \
        g_thread_self());         \
  } G_STMT_END

#define CHECK_THREAD(timeline) g_assert(timeline->priv->valid_thread == g_thread_self())

struct _GESTimelinePrivate
{
  GNode *tree;

  /* The duration of the timeline */
  gint64 duration;

  /* The auto-transition of the timeline */
  gboolean auto_transition;

  /* Timeline edition modes and snapping management */
  guint64 snapping_distance;

  GRecMutex dyn_mutex;
  GList *priv_tracks;

  /* Avoid sorting layers when we are actually resyncing them ourself */
  gboolean resyncing_layers;
  GList *auto_transitions;

  /* Last snapping  properties */
  GstClockTime last_snap_ts;
  GESTrackElement *last_snaped1;
  GESTrackElement *last_snaped2;

  /* This variable is set to %TRUE when it makes sense to update the transitions,
   * and %FALSE otherwize */
  gboolean needs_transitions_update;

  /* While we are creating and adding the TrackElements for a clip, we need to
   * ignore the child-added signal */
  GESClip *ignore_track_element_added;
  GList *groups;

  guint stream_start_group_id;

  GHashTable *all_elements;

  /* With GST_OBJECT_LOCK */
  guint expected_async_done;
  /* With GST_OBJECT_LOCK */
  guint expected_commited;

  /* For ges_timeline_commit_sync */
  GMutex commited_lock;
  GCond commited_cond;

  GThread *valid_thread;
};

/* private structure to contain our track-related information */

typedef struct
{
  GESTimeline *timeline;
  GESTrack *track;
  GstPad *pad;                  /* Pad from the track */
  GstPad *ghostpad;

  gulong probe_id;
} TrackPrivate;

enum
{
  PROP_0,
  PROP_DURATION,
  PROP_AUTO_TRANSITION,
  PROP_SNAPPING_DISTANCE,
  PROP_UPDATE,
  PROP_LAST
};

static GParamSpec *properties[PROP_LAST];

enum
{
  TRACK_ADDED,
  TRACK_REMOVED,
  LAYER_ADDED,
  LAYER_REMOVED,
  GROUP_ADDED,
  GROUP_REMOVED,
  SNAPING_STARTED,
  SNAPING_ENDED,
  SELECT_TRACKS_FOR_OBJECT,
  COMMITED,
  LAST_SIGNAL
};

G_DEFINE_TYPE_WITH_CODE (GESTimeline, ges_timeline, GST_TYPE_BIN,
    G_ADD_PRIVATE (GESTimeline)
    G_IMPLEMENT_INTERFACE (GES_TYPE_EXTRACTABLE, ges_extractable_interface_init)
    G_IMPLEMENT_INTERFACE (GES_TYPE_META_CONTAINER,
        ges_meta_container_interface_init));

static GstBinClass *parent_class;

static guint ges_timeline_signals[LAST_SIGNAL] = { 0 };

static gint custom_find_track (TrackPrivate * tr_priv, GESTrack * track);

static guint nb_assets = 0;

/* GESExtractable implementation */
static gchar *
extractable_check_id (GType type, const gchar * id)
{
  gchar *res;

  if (id == NULL)
    res = g_strdup_printf ("%s-%i", "project", nb_assets);
  else
    res = g_strdup (id);

  nb_assets++;

  return res;
}

static gchar *
extractable_get_id (GESExtractable * self)
{
  GESAsset *asset;

  if (!(asset = ges_extractable_get_asset (self)))
    return NULL;

  return g_strdup (ges_asset_get_id (asset));
}

static void
ges_extractable_interface_init (GESExtractableInterface * iface)
{
  iface->asset_type = GES_TYPE_PROJECT;
  iface->check_id = (GESExtractableCheckId) extractable_check_id;
  iface->get_id = extractable_get_id;
}

static void
ges_meta_container_interface_init (GESMetaContainerInterface * iface)
{
}

/* GObject Standard vmethods*/
static void
ges_timeline_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GESTimeline *timeline = GES_TIMELINE (object);

  switch (property_id) {
    case PROP_DURATION:
      g_value_set_uint64 (value, timeline->priv->duration);
      break;
    case PROP_AUTO_TRANSITION:
      g_value_set_boolean (value, timeline->priv->auto_transition);
      break;
    case PROP_SNAPPING_DISTANCE:
      g_value_set_uint64 (value, timeline->priv->snapping_distance);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_timeline_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GESTimeline *timeline = GES_TIMELINE (object);

  switch (property_id) {
    case PROP_AUTO_TRANSITION:
      ges_timeline_set_auto_transition (timeline, g_value_get_boolean (value));
      break;
    case PROP_SNAPPING_DISTANCE:
      timeline->priv->snapping_distance = g_value_get_uint64 (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_timeline_dispose (GObject * object)
{
  GESTimeline *tl = GES_TIMELINE (object);
  GESTimelinePrivate *priv = tl->priv;
  GList *tmp, *groups;

  while (tl->layers) {
    GESLayer *layer = (GESLayer *) tl->layers->data;
    ges_timeline_remove_layer (GES_TIMELINE (object), layer);
  }

  /* FIXME: it should be possible to remove tracks before removing
   * layers, but at the moment this creates a problem because the track
   * objects aren't notified that their nleobjects have been destroyed.
   */

  while (tl->tracks)
    ges_timeline_remove_track (GES_TIMELINE (object), tl->tracks->data);

  groups = g_list_copy (priv->groups);
  for (tmp = groups; tmp; tmp = tmp->next) {
    GList *elems = ges_container_ungroup (tmp->data, FALSE);

    g_list_free_full (elems, gst_object_unref);
  }
  g_list_free (priv->groups);
  g_list_free (groups);

  g_list_free_full (priv->auto_transitions, gst_object_unref);

  g_hash_table_unref (priv->all_elements);

  G_OBJECT_CLASS (ges_timeline_parent_class)->dispose (object);
}

static void
ges_timeline_finalize (GObject * object)
{
  GESTimeline *tl = GES_TIMELINE (object);

  g_rec_mutex_clear (&tl->priv->dyn_mutex);
  g_node_destroy (tl->priv->tree);

  G_OBJECT_CLASS (ges_timeline_parent_class)->finalize (object);
}



static void
ges_timeline_handle_message (GstBin * bin, GstMessage * message)
{
  GESTimeline *timeline = GES_TIMELINE (bin);

  if (GST_MESSAGE_TYPE (message) == GST_MESSAGE_ELEMENT) {
    GstMessage *amessage = NULL;
    const GstStructure *mstructure = gst_message_get_structure (message);

    if (gst_structure_has_name (mstructure, "NleCompositionStartUpdate")) {
      if (g_strcmp0 (gst_structure_get_string (mstructure, "reason"), "Seek")) {
        GST_INFO_OBJECT (timeline,
            "A composition is starting an update because of %s"
            " not considering async", gst_structure_get_string (mstructure,
                "reason"));

        goto forward;
      }

      GST_OBJECT_LOCK (timeline);
      if (timeline->priv->expected_async_done == 0) {
        amessage = gst_message_new_async_start (GST_OBJECT_CAST (bin));
        timeline->priv->expected_async_done = g_list_length (timeline->tracks);
        GST_INFO_OBJECT (timeline, "Posting ASYNC_START %s",
            gst_structure_get_string (mstructure, "reason"));
      }
      GST_OBJECT_UNLOCK (timeline);

    } else if (gst_structure_has_name (mstructure, "NleCompositionUpdateDone")) {
      if (g_strcmp0 (gst_structure_get_string (mstructure, "reason"), "Seek")) {
        GST_INFO_OBJECT (timeline,
            "A composition is done updating because of %s"
            " not considering async", gst_structure_get_string (mstructure,
                "reason"));

        goto forward;
      }

      GST_OBJECT_LOCK (timeline);
      timeline->priv->expected_async_done -= 1;
      if (timeline->priv->expected_async_done == 0) {
        amessage = gst_message_new_async_done (GST_OBJECT_CAST (bin),
            GST_CLOCK_TIME_NONE);
        GST_INFO_OBJECT (timeline, "Posting ASYNC_DONE %s",
            gst_structure_get_string (mstructure, "reason"));
      }
      GST_OBJECT_UNLOCK (timeline);
    }

    if (amessage)
      gst_element_post_message (GST_ELEMENT_CAST (bin), amessage);
  }

forward:
  gst_element_post_message (GST_ELEMENT_CAST (bin), message);
}

/* we collect the first result */
static gboolean
_gst_array_accumulator (GSignalInvocationHint * ihint,
    GValue * return_accu, const GValue * handler_return, gpointer dummy)
{
  gpointer array;

  array = g_value_get_boxed (handler_return);
  if (!(ihint->run_type & G_SIGNAL_RUN_CLEANUP))
    g_value_set_boxed (return_accu, array);

  return FALSE;
}

static void
ges_timeline_class_init (GESTimelineClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GstBinClass *bin_class = GST_BIN_CLASS (klass);

  GST_DEBUG_CATEGORY_INIT (ges_timeline_debug, "gestimeline",
      GST_DEBUG_FG_YELLOW, "ges timeline");
  timeline_tree_init_debug ();

  parent_class = g_type_class_peek_parent (klass);

  object_class->get_property = ges_timeline_get_property;
  object_class->set_property = ges_timeline_set_property;
  object_class->dispose = ges_timeline_dispose;
  object_class->finalize = ges_timeline_finalize;

  bin_class->handle_message = GST_DEBUG_FUNCPTR (ges_timeline_handle_message);

  /**
   * GESTimeline:duration:
   *
   * Current duration (in nanoseconds) of the #GESTimeline
   */
  properties[PROP_DURATION] =
      g_param_spec_uint64 ("duration", "Duration",
      "The duration of the timeline", 0, G_MAXUINT64,
      GST_CLOCK_TIME_NONE, G_PARAM_READABLE);
  g_object_class_install_property (object_class, PROP_DURATION,
      properties[PROP_DURATION]);

  /**
   * GESTimeline:auto-transition:
   *
   * Sets whether transitions are added automagically when clips overlap.
   */
  g_object_class_install_property (object_class, PROP_AUTO_TRANSITION,
      g_param_spec_boolean ("auto-transition", "Auto-Transition",
          "whether the transitions are added", FALSE, G_PARAM_READWRITE));

  /**
   * GESTimeline:snapping-distance:
   *
   * Distance (in nanoseconds) from which a moving object will snap
   * with it neighboors. 0 means no snapping.
   */
  properties[PROP_SNAPPING_DISTANCE] =
      g_param_spec_uint64 ("snapping-distance", "Snapping distance",
      "Distance from which moving an object will snap with neighboors", 0,
      G_MAXUINT64, 0, G_PARAM_READWRITE);
  g_object_class_install_property (object_class, PROP_SNAPPING_DISTANCE,
      properties[PROP_SNAPPING_DISTANCE]);

  /**
   * GESTimeline::track-added:
   * @timeline: the #GESTimeline
   * @track: the #GESTrack that was added to the timeline
   *
   * Will be emitted after the track was added to the timeline.
   */
  ges_timeline_signals[TRACK_ADDED] =
      g_signal_new ("track-added", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_FIRST, G_STRUCT_OFFSET (GESTimelineClass, track_added), NULL,
      NULL, g_cclosure_marshal_generic, G_TYPE_NONE, 1, GES_TYPE_TRACK);

  /**
   * GESTimeline::track-removed:
   * @timeline: the #GESTimeline
   * @track: the #GESTrack that was removed from the timeline
   *
   * Will be emitted after the track was removed from the timeline.
   */
  ges_timeline_signals[TRACK_REMOVED] =
      g_signal_new ("track-removed", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_FIRST, G_STRUCT_OFFSET (GESTimelineClass, track_removed),
      NULL, NULL, g_cclosure_marshal_generic, G_TYPE_NONE, 1, GES_TYPE_TRACK);

  /**
   * GESTimeline::layer-added:
   * @timeline: the #GESTimeline
   * @layer: the #GESLayer that was added to the timeline
   *
   * Will be emitted after a new layer is added to the timeline.
   */
  ges_timeline_signals[LAYER_ADDED] =
      g_signal_new ("layer-added", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_FIRST, G_STRUCT_OFFSET (GESTimelineClass, layer_added), NULL,
      NULL, g_cclosure_marshal_generic, G_TYPE_NONE, 1, GES_TYPE_LAYER);

  /**
   * GESTimeline::layer-removed:
   * @timeline: the #GESTimeline
   * @layer: the #GESLayer that was removed from the timeline
   *
   * Will be emitted after the layer was removed from the timeline.
   */
  ges_timeline_signals[LAYER_REMOVED] =
      g_signal_new ("layer-removed", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_FIRST, G_STRUCT_OFFSET (GESTimelineClass, layer_removed),
      NULL, NULL, g_cclosure_marshal_generic, G_TYPE_NONE, 1, GES_TYPE_LAYER);

  /**
   * GESTimeline::group-added
   * @timeline: the #GESTimeline
   * @group: the #GESGroup
   *
   * Will be emitted after a new group is added to to the timeline.
   */
  ges_timeline_signals[GROUP_ADDED] =
      g_signal_new ("group-added", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_FIRST, G_STRUCT_OFFSET (GESTimelineClass, group_added), NULL,
      NULL, g_cclosure_marshal_generic, G_TYPE_NONE, 1, GES_TYPE_GROUP);

  /**
   * GESTimeline::group-removed
   * @timeline: the #GESTimeline
   * @group: the #GESGroup
   * @children: (element-type GES.Container) (transfer container): a list of #GESContainer
   *
   * Will be emitted after a group has been removed from the timeline.
   */
  ges_timeline_signals[GROUP_REMOVED] =
      g_signal_new ("group-removed", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_FIRST, G_STRUCT_OFFSET (GESTimelineClass, group_removed),
      NULL, NULL, g_cclosure_marshal_generic, G_TYPE_NONE, 2, GES_TYPE_GROUP,
      G_TYPE_PTR_ARRAY);

  /**
   * GESTimeline::snapping-started:
   * @timeline: the #GESTimeline
   * @obj1: the first #GESTrackElement that was snapping.
   * @obj2: the second #GESTrackElement that was snapping.
   * @position: the position where the two objects finally snapping.
   *
   * Will be emitted when the 2 #GESTrackElement first snapped
   */
  ges_timeline_signals[SNAPING_STARTED] =
      g_signal_new ("snapping-started", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
      G_TYPE_NONE, 3, GES_TYPE_TRACK_ELEMENT, GES_TYPE_TRACK_ELEMENT,
      G_TYPE_UINT64);

  /**
   * GESTimeline::snapping-ended:
   * @timeline: the #GESTimeline
   * @obj1: the first #GESTrackElement that was snapping.
   * @obj2: the second #GESTrackElement that was snapping.
   * @position: the position where the two objects finally snapping.
   *
   * Will be emitted when the 2 #GESTrackElement ended to snap
   */
  ges_timeline_signals[SNAPING_ENDED] =
      g_signal_new ("snapping-ended", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
      G_TYPE_NONE, 3, GES_TYPE_TRACK_ELEMENT, GES_TYPE_TRACK_ELEMENT,
      G_TYPE_UINT64);

  /**
   * GESTimeline::select-tracks-for-object:
   * @timeline: the #GESTimeline
   * @clip: The #GESClip on which @track_element will land
   * @track_element: The #GESTrackElement for which to choose the tracks it should land into
   *
   * Returns: (transfer full) (element-type GESTrack): a #GPtrArray of #GESTrack-s where that object should be added
   */
  ges_timeline_signals[SELECT_TRACKS_FOR_OBJECT] =
      g_signal_new ("select-tracks-for-object", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, _gst_array_accumulator, NULL, NULL,
      G_TYPE_PTR_ARRAY, 2, GES_TYPE_CLIP, GES_TYPE_TRACK_ELEMENT);

  /**
   * GESTimeline::commited:
   * @timeline: the #GESTimeline
   *
   * This signal will be emitted once the changes initiated by #ges_timeline_commit
   * have been executed in the backend. Use #ges_timeline_commit_sync if you
   * don't need to do anything in the meantime.
   */
  ges_timeline_signals[COMMITED] =
      g_signal_new ("commited", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 0);
}

static void
ges_timeline_init (GESTimeline * self)
{
  GESTimelinePrivate *priv = self->priv;

  self->priv = ges_timeline_get_instance_private (self);
  self->priv->tree = g_node_new (self);

  priv = self->priv;
  self->layers = NULL;
  self->tracks = NULL;
  self->priv->duration = 0;
  self->priv->auto_transition = FALSE;
  priv->snapping_distance = 0;
  priv->expected_async_done = 0;
  priv->expected_commited = 0;

  self->priv->last_snap_ts = GST_CLOCK_TIME_NONE;

  priv->priv_tracks = NULL;
  priv->needs_transitions_update = TRUE;

  priv->all_elements =
      g_hash_table_new_full (g_str_hash, g_str_equal, g_free, gst_object_unref);

  priv->stream_start_group_id = -1;

  g_signal_connect_after (self, "select-tracks-for-object",
      G_CALLBACK (select_tracks_for_object_default), NULL);

  g_rec_mutex_init (&priv->dyn_mutex);
  g_mutex_init (&priv->commited_lock);
  priv->valid_thread = g_thread_self ();
}

/* Private methods */

static inline GESContainer *
get_toplevel_container (gpointer element)
{
  GESTimelineElement *ret =
      ges_timeline_element_get_toplevel_parent ((GESTimelineElement
          *) (element));

  /*  We own a ref to the elements ourself */
  gst_object_unref (ret);
  return (GESContainer *) ret;
}

/* Sorting utils*/
static gint
sort_layers (gpointer a, gpointer b)
{
  GESLayer *layer_a, *layer_b;
  guint prio_a, prio_b;

  layer_a = GES_LAYER (a);
  layer_b = GES_LAYER (b);

  prio_a = ges_layer_get_priority (layer_a);
  prio_b = ges_layer_get_priority (layer_b);

  if ((gint) prio_a > (guint) prio_b)
    return 1;
  if ((guint) prio_a < (guint) prio_b)
    return -1;

  return 0;
}

static void
_resync_layers (GESTimeline * timeline)
{
  GList *tmp;
  gint i = 0;

  timeline->priv->resyncing_layers = TRUE;
  for (tmp = timeline->layers; tmp; tmp = tmp->next) {
    layer_set_priority (tmp->data, i, TRUE);
    i++;
  }
  timeline->priv->resyncing_layers = FALSE;
}

void
timeline_update_duration (GESTimeline * timeline)
{
  GstClockTime duration = timeline_tree_get_duration (timeline->priv->tree);

  if (timeline->priv->duration != duration) {
    GST_DEBUG ("track duration : %" GST_TIME_FORMAT " current : %"
        GST_TIME_FORMAT, GST_TIME_ARGS (duration),
        GST_TIME_ARGS (timeline->priv->duration));

    timeline->priv->duration = duration;

    g_object_notify_by_pspec (G_OBJECT (timeline), properties[PROP_DURATION]);
  }
}

static gint
custom_find_track (TrackPrivate * tr_priv, GESTrack * track)
{
  if (tr_priv->track == track)
    return 0;
  return -1;
}

static void
_destroy_auto_transition_cb (GESAutoTransition * auto_transition,
    GESTimeline * timeline)
{
  GESTimelinePrivate *priv = timeline->priv;
  GESClip *transition = auto_transition->transition_clip;
  GESLayer *layer = ges_clip_get_layer (transition);

  ges_layer_remove_clip (layer, transition);
  g_signal_handlers_disconnect_by_func (auto_transition,
      _destroy_auto_transition_cb, timeline);

  priv->auto_transitions =
      g_list_remove (priv->auto_transitions, auto_transition);
  gst_object_unref (auto_transition);
}

GESAutoTransition *
ges_timeline_create_transition (GESTimeline * timeline,
    GESTrackElement * previous, GESTrackElement * next, GESClip * transition,
    GESLayer * layer, guint64 start, guint64 duration)
{
  GESAsset *asset;
  GESAutoTransition *auto_transition;

  if (transition == NULL) {
    /* TODO make it possible to specify a Transition asset in the API */
    asset = ges_asset_request (GES_TYPE_TRANSITION_CLIP, "crossfade", NULL);
    transition =
        ges_layer_add_asset (layer, asset, start, 0, duration,
        ges_track_element_get_track_type (next));
    g_object_unref (asset);
  } else {
    GST_DEBUG_OBJECT (timeline,
        "Reusing already existing transition: %" GST_PTR_FORMAT, transition);
  }

  /* We know there is only 1 TrackElement */
  auto_transition =
      ges_auto_transition_new (GES_CONTAINER_CHILDREN (transition)->data,
      previous, next);

  g_signal_connect (auto_transition, "destroy-me",
      G_CALLBACK (_destroy_auto_transition_cb), timeline);

  timeline->priv->auto_transitions =
      g_list_prepend (timeline->priv->auto_transitions, auto_transition);

  return auto_transition;
}

GESAutoTransition *
ges_timeline_find_auto_transition (GESTimeline * timeline,
    GESTrackElement * prev, GESTrackElement * next,
    GstClockTime transition_duration)
{
  GList *tmp;

  for (tmp = timeline->priv->auto_transitions; tmp; tmp = tmp->next) {
    GESAutoTransition *auto_trans = (GESAutoTransition *) tmp->data;

    /* We already have a transition linked to one of the elements we want to
     * find a transition for */
    if (auto_trans->previous_source == prev || auto_trans->next_source == next) {
      if (auto_trans->previous_source != prev
          || auto_trans->next_source != next) {
        GST_ERROR_OBJECT (timeline, "Failed creating auto transition, "
            " trying to have 3 clips overlapping, rolling back");
      }

      return auto_trans;
    }
  }

  return NULL;
}

static GESAutoTransition *
_create_auto_transition_from_transitions (GESTimeline * timeline,
    GESTrackElement * prev, GESTrackElement * next,
    GstClockTime transition_duration)
{
  GList *tmp, *elements;
  GESLayer *layer;
  guint32 layer_prio = GES_TIMELINE_ELEMENT_LAYER_PRIORITY (prev);
  GESTrack *track;
  GESAutoTransition *auto_transition =
      ges_timeline_find_auto_transition (timeline, prev, next,
      transition_duration);

  if (auto_transition)
    return auto_transition;

  layer = ges_timeline_get_layer (timeline, layer_prio);
  track = ges_track_element_get_track (prev);
  elements = ges_track_get_elements (track);
  for (tmp = elements; tmp; tmp = tmp->next) {
    GESTrackElement *maybe_transition = tmp->data;

    if (ges_timeline_element_get_layer_priority (tmp->data) != layer_prio)
      continue;

    if (_START (maybe_transition) > _START (next))
      break;
    else if (_START (maybe_transition) != _START (next) ||
        _DURATION (maybe_transition) != transition_duration)
      continue;
    else if (GES_IS_TRANSITION (maybe_transition)) {
      /* Use that transition */
      /* TODO We should make sure that the transition contains only
       * TrackElement-s in @track and if it is not the case properly unlink the
       * object to use it */
      auto_transition = ges_timeline_create_transition (timeline, prev, next,
          GES_CLIP (GES_TIMELINE_ELEMENT_PARENT (maybe_transition)), layer,
          _START (next), transition_duration);

      break;
    }
  }
  gst_object_unref (layer);
  g_list_free_full (elements, gst_object_unref);

  return auto_transition;
}

void
ges_timeline_emit_snapping (GESTimeline * timeline, GESTimelineElement * elem1,
    GESTimelineElement * elem2, GstClockTime snap_time)
{
  GESTimelinePrivate *priv = timeline->priv;
  GstClockTime last_snap_ts = timeline->priv->last_snap_ts;

  if (!GST_CLOCK_TIME_IS_VALID (snap_time)) {
    if (priv->last_snaped1 != NULL && priv->last_snaped2 != NULL) {
      g_signal_emit (timeline, ges_timeline_signals[SNAPING_ENDED], 0,
          priv->last_snaped1, priv->last_snaped2, last_snap_ts);
      priv->last_snaped1 = NULL;
      priv->last_snaped2 = NULL;
      priv->last_snap_ts = GST_CLOCK_TIME_NONE;
    }

    return;
  }

  g_assert (elem1 != elem2);
  if (GES_IS_CLIP (elem1)) {
    g_assert (GES_CONTAINER_CHILDREN (elem1));
    elem1 = GES_CONTAINER_CHILDREN (elem1)->data;
  }

  if (GES_IS_CLIP (elem2)) {
    g_assert (GES_CONTAINER_CHILDREN (elem2));
    elem2 = GES_CONTAINER_CHILDREN (elem2)->data;
  }

  if (last_snap_ts != snap_time) {
    g_signal_emit (timeline, ges_timeline_signals[SNAPING_ENDED], 0,
        priv->last_snaped1, priv->last_snaped2, (last_snap_ts));

    /* We want the snap start signal to be emited anyway */
    timeline->priv->last_snap_ts = GST_CLOCK_TIME_NONE;
  }

  if (!GST_CLOCK_TIME_IS_VALID (timeline->priv->last_snap_ts)) {
    priv->last_snaped1 = (GESTrackElement *) elem1;
    priv->last_snaped2 = (GESTrackElement *) elem2;
    timeline->priv->last_snap_ts = snap_time;

    g_signal_emit (timeline, ges_timeline_signals[SNAPING_STARTED], 0,
        elem1, elem2, snap_time);
  }

}

gboolean
ges_timeline_trim_object_simple (GESTimeline * timeline,
    GESTimelineElement * element, guint32 new_layer_priority,
    GList * layers, GESEdge edge, guint64 position, gboolean snapping)
{

  return timeline_trim_object (timeline, element, new_layer_priority, layers,
      edge, position);
}

gboolean
timeline_ripple_object (GESTimeline * timeline, GESTimelineElement * obj,
    gint new_layer_priority, GList * layers, GESEdge edge, guint64 position)
{
  gboolean res = TRUE;
  guint64 new_duration;
  GstClockTimeDiff diff;

  switch (edge) {
    case GES_EDGE_NONE:
      GST_DEBUG ("Simply rippling");
      diff = GST_CLOCK_DIFF (position, _START (obj));

      timeline->priv->needs_transitions_update = FALSE;
      res = timeline_tree_ripple (timeline->priv->tree,
          (gint64) GES_TIMELINE_ELEMENT_LAYER_PRIORITY (obj) -
          (gint64) new_layer_priority, diff, obj,
          GES_EDGE_NONE, timeline->priv->snapping_distance);
      timeline->priv->needs_transitions_update = TRUE;

      break;
    case GES_EDGE_END:
      GST_DEBUG ("Rippling end");

      timeline->priv->needs_transitions_update = FALSE;
      new_duration =
          CLAMP (position - obj->start, 0, obj->maxduration - obj->inpoint);
      res =
          timeline_tree_ripple (timeline->priv->tree,
          (gint64) GES_TIMELINE_ELEMENT_LAYER_PRIORITY (obj) -
          (gint64) new_layer_priority,
          _DURATION (obj) - new_duration, obj,
          GES_EDGE_END, timeline->priv->snapping_distance);
      timeline->priv->needs_transitions_update = TRUE;

      GST_DEBUG ("Done Rippling end");
      break;
    case GES_EDGE_START:
      GST_INFO ("Ripple start doesn't make sense, trimming instead");
      if (!timeline_trim_object (timeline, obj, -1, layers, edge, position))
        goto error;
      break;
    default:
      GST_DEBUG ("Can not ripple edge: %i", edge);

      break;
  }

  return res;

error:

  return FALSE;
}

gboolean
timeline_slide_object (GESTimeline * timeline, GESTrackElement * obj,
    GList * layers, GESEdge edge, guint64 position)
{

  /* FIXME implement me! */
  GST_FIXME_OBJECT (timeline, "Slide mode editing not implemented yet");

  return FALSE;
}

static gboolean
_trim_transition (GESTimeline * timeline, GESTimelineElement * element,
    GESEdge edge, GstClockTime position)
{
  GList *tmp;
  GESLayer *layer = ges_timeline_get_layer (timeline,
      GES_TIMELINE_ELEMENT_LAYER_PRIORITY (element));

  if (!ges_layer_get_auto_transition (layer))
    goto fail;

  gst_object_unref (layer);
  for (tmp = timeline->priv->auto_transitions; tmp; tmp = tmp->next) {
    GESAutoTransition *auto_transition = tmp->data;

    if (GES_TIMELINE_ELEMENT (auto_transition->transition) == element ||
        GES_TIMELINE_ELEMENT (auto_transition->transition_clip) == element) {
      /* Trimming an auto transition means trimming its neighboors */
      if (!auto_transition->positioning) {
        if (edge == GES_EDGE_END) {
          ges_container_edit (GES_CONTAINER (auto_transition->previous_clip),
              NULL, -1, GES_EDIT_MODE_TRIM, GES_EDGE_END, position);
        } else {
          ges_container_edit (GES_CONTAINER (auto_transition->next_clip),
              NULL, -1, GES_EDIT_MODE_TRIM, GES_EDGE_START, position);
        }

        return TRUE;
      }

      return FALSE;
    }
  }

  return FALSE;

fail:
  gst_object_unref (layer);
  return FALSE;
}


gboolean
timeline_trim_object (GESTimeline * timeline, GESTimelineElement * object,
    guint32 new_layer_priority, GList * layers, GESEdge edge, guint64 position)
{
  if ((GES_IS_TRANSITION (object) || GES_IS_TRANSITION_CLIP (object)) &&
      !ELEMENT_FLAG_IS_SET (object, GES_TIMELINE_ELEMENT_SET_SIMPLE)) {
    return _trim_transition (timeline, object, edge, position);
  }

  return timeline_tree_trim (timeline->priv->tree,
      GES_TIMELINE_ELEMENT (object), new_layer_priority > 0 ? (gint64)
      ges_timeline_element_get_layer_priority (GES_TIMELINE_ELEMENT (object)) -
      new_layer_priority : 0, edge == GES_EDGE_END ? GST_CLOCK_DIFF (position,
          _START (object) + _DURATION (object)) : GST_CLOCK_DIFF (position,
          GES_TIMELINE_ELEMENT_START (object)), edge,
      timeline->priv->snapping_distance);
}

gboolean
timeline_roll_object (GESTimeline * timeline, GESTimelineElement * element,
    GList * layers, GESEdge edge, guint64 position)
{
  return timeline_tree_roll (timeline->priv->tree,
      element,
      (edge == GES_EDGE_END) ?
      GST_CLOCK_DIFF (position, _END (element)) :
      GST_CLOCK_DIFF (position, _START (element)),
      edge, timeline->priv->snapping_distance);
}

gboolean
timeline_move_object (GESTimeline * timeline, GESTimelineElement * object,
    guint32 new_layer_priority, GList * layers, GESEdge edge, guint64 position)
{
  gboolean ret = FALSE;
  GstClockTimeDiff offset = edge == GES_EDGE_END ?
      GST_CLOCK_DIFF (position, _START (object) + _DURATION (object)) :
      GST_CLOCK_DIFF (position, GES_TIMELINE_ELEMENT_START (object));

  ret = timeline_tree_move (timeline->priv->tree,
      GES_TIMELINE_ELEMENT (object), new_layer_priority < 0 ? 0 : (gint64)
      ges_timeline_element_get_layer_priority (GES_TIMELINE_ELEMENT (object)) -
      new_layer_priority, offset, edge, timeline->priv->snapping_distance);

  return ret;
}

gboolean
ges_timeline_move_object_simple (GESTimeline * timeline,
    GESTimelineElement * element, GList * layers, GESEdge edge,
    guint64 position)
{
  return timeline_move_object (timeline, element,
      ges_timeline_element_get_layer_priority (element), NULL, edge, position);
}

void
timeline_add_group (GESTimeline * timeline, GESGroup * group)
{
  GST_DEBUG_OBJECT (timeline, "Adding group %" GST_PTR_FORMAT, group);

  timeline->priv->groups = g_list_prepend (timeline->priv->groups,
      gst_object_ref_sink (group));

  ges_timeline_element_set_timeline (GES_TIMELINE_ELEMENT (group), timeline);
}

void
timeline_update_transition (GESTimeline * timeline)
{
  GList *tmp, *auto_transs;

  auto_transs = g_list_copy (timeline->priv->auto_transitions);
  for (tmp = auto_transs; tmp; tmp = tmp->next)
    ges_auto_transition_update (tmp->data);
  g_list_free (auto_transs);
}

/**
 * timeline_emit_group_added:
 * @timeline: a #GESTimeline
 * @group: group that was added
 *
 * Emit group-added signal.
 */
void
timeline_emit_group_added (GESTimeline * timeline, GESGroup * group)
{
  g_signal_emit (timeline, ges_timeline_signals[GROUP_ADDED], 0, group);
}

/**
 * timeline_emit_group_removed:
 * @timeline: a #GESTimeline
 * @group: group that was removed
 *
 * Emit group-removed signal.
 */
void
timeline_emit_group_removed (GESTimeline * timeline, GESGroup * group,
    GPtrArray * array)
{
  g_signal_emit (timeline, ges_timeline_signals[GROUP_REMOVED], 0, group,
      array);
}

void
timeline_remove_group (GESTimeline * timeline, GESGroup * group)
{
  GST_DEBUG_OBJECT (timeline, "Removing group %" GST_PTR_FORMAT, group);

  timeline->priv->groups = g_list_remove (timeline->priv->groups, group);

  ges_timeline_element_set_timeline (GES_TIMELINE_ELEMENT (group), NULL);
  gst_object_unref (group);
}

static GPtrArray *
select_tracks_for_object_default (GESTimeline * timeline,
    GESClip * clip, GESTrackElement * tr_object, gpointer user_data)
{
  GPtrArray *result;
  GList *tmp;

  result = g_ptr_array_new ();

  for (tmp = timeline->tracks; tmp; tmp = tmp->next) {
    GESTrack *track = GES_TRACK (tmp->data);

    if ((track->type & ges_track_element_get_track_type (tr_object))) {
      gst_object_ref (track);
      g_ptr_array_add (result, track);
    }
  }

  return result;
}

static void
add_object_to_tracks (GESTimeline * timeline, GESClip * clip, GESTrack * track)
{
  gint i;
  GList *tmp, *list;
  GESTrackType types, visited_type = GES_TRACK_TYPE_UNKNOWN;

  GST_DEBUG_OBJECT (timeline, "Creating %" GST_PTR_FORMAT
      " trackelements and adding them to our tracks", clip);

  types = ges_clip_get_supported_formats (clip);
  if (track) {
    if ((types & track->type) == 0)
      return;
    types = track->type;
  }

  for (i = 0, tmp = timeline->tracks; tmp; tmp = tmp->next, i++) {
    GESTrack *track = GES_TRACK (tmp->data);

    if (((track->type & types) == 0 || (track->type & visited_type)))
      continue;

    list = ges_clip_create_track_elements (clip, track->type);
    g_list_free (list);
  }
}

static void
layer_auto_transition_changed_cb (GESLayer * layer,
    GParamSpec * arg G_GNUC_UNUSED, GESTimeline * timeline)
{
  GList *tmp, *clips;

  timeline_tree_create_transitions (timeline->priv->tree,
      _create_auto_transition_from_transitions);
  clips = ges_layer_get_clips (layer);
  for (tmp = clips; tmp; tmp = tmp->next) {
    if (GES_IS_TRANSITION_CLIP (tmp->data)) {
      GList *tmpautotrans;
      gboolean found = FALSE;

      for (tmpautotrans = timeline->priv->auto_transitions; tmpautotrans;
          tmpautotrans = tmpautotrans->next) {
        if (GES_AUTO_TRANSITION (tmpautotrans->data)->transition_clip ==
            tmp->data) {
          found = TRUE;
          break;
        }
      }

      if (!found) {
        GST_ERROR_OBJECT (timeline,
            "Transition %s could not be wrapped into an auto transition"
            " REMOVING it", GES_TIMELINE_ELEMENT_NAME (tmp->data));

        ges_layer_remove_clip (layer, tmp->data);
      }
    }
  }
  g_list_free_full (clips, gst_object_unref);
}

static void
clip_track_element_added_cb (GESClip * clip,
    GESTrackElement * track_element, GESTimeline * timeline)
{
  guint i;
  GESTrack *track;
  gboolean is_source;
  GPtrArray *tracks = NULL;
  GESTrackElement *existing_src = NULL;

  if (timeline->priv->ignore_track_element_added == clip) {
    GST_DEBUG_OBJECT (timeline, "Ignoring element added (%" GST_PTR_FORMAT
        " in %" GST_PTR_FORMAT, track_element, clip);

    return;
  }

  if (ges_track_element_get_track (track_element)) {
    GST_WARNING_OBJECT (track_element, "Already in a track");

    return;
  }

  g_signal_emit (G_OBJECT (timeline),
      ges_timeline_signals[SELECT_TRACKS_FOR_OBJECT], 0, clip, track_element,
      &tracks);

  if (!tracks || tracks->len == 0) {
    GST_WARNING_OBJECT (timeline, "Got no Track to add %p (type %s), removing"
        " from clip (stopping 'child-added' signal emission).",
        track_element, ges_track_type_name (ges_track_element_get_track_type
            (track_element)));

    if (tracks)
      g_ptr_array_unref (tracks);

    g_signal_stop_emission_by_name (clip, "child-added");
    ges_container_remove (GES_CONTAINER (clip),
        GES_TIMELINE_ELEMENT (track_element));

    return;
  }

  /* We add the current element to the first track */
  track = g_ptr_array_index (tracks, 0);

  is_source = g_type_is_a (G_OBJECT_TYPE (track_element), GES_TYPE_SOURCE);
  if (is_source)
    existing_src = ges_clip_find_track_element (clip, track, GES_TYPE_SOURCE);

  if (existing_src == NULL) {
    if (!ges_track_add_element (track, track_element)) {
      GST_WARNING_OBJECT (clip, "Failed to add track element to track");
      ges_container_remove (GES_CONTAINER (clip),
          GES_TIMELINE_ELEMENT (track_element));
      g_ptr_array_unref (tracks);
      return;
    }
  } else {
    GST_INFO_OBJECT (clip, "Already had a Source Element in %" GST_PTR_FORMAT
        " of type %s, removing new one. (stopping 'child-added' emission)",
        track, G_OBJECT_TYPE_NAME (track_element));
    g_signal_stop_emission_by_name (clip, "child-added");
    ges_container_remove (GES_CONTAINER (clip),
        GES_TIMELINE_ELEMENT (track_element));
  }
  gst_object_unref (track);
  g_clear_object (&existing_src);

  /* And create copies to add to other tracks */
  timeline->priv->ignore_track_element_added = clip;
  for (i = 1; i < tracks->len; i++) {
    GESTrack *track;
    GESTrackElement *track_element_copy;

    track = g_ptr_array_index (tracks, i);
    if (is_source)
      existing_src = ges_clip_find_track_element (clip, track, GES_TYPE_SOURCE);
    if (existing_src == NULL) {
      ges_container_remove (GES_CONTAINER (clip),
          GES_TIMELINE_ELEMENT (track_element));
      gst_object_unref (track);
      g_ptr_array_unref (tracks);
      continue;
    } else {
      GST_INFO_OBJECT (clip, "Already had a Source Element in %" GST_PTR_FORMAT
          " of type %s, removing new one. (stopping 'child-added' emission)",
          track, G_OBJECT_TYPE_NAME (track_element));
      g_signal_stop_emission_by_name (clip, "child-added");
      ges_container_remove (GES_CONTAINER (clip),
          GES_TIMELINE_ELEMENT (track_element));
    }
    g_clear_object (&existing_src);

    track_element_copy =
        GES_TRACK_ELEMENT (ges_timeline_element_copy (GES_TIMELINE_ELEMENT
            (track_element), TRUE));

    GST_LOG_OBJECT (timeline, "Trying to add %p to track %p",
        track_element_copy, track);

    if (!ges_container_add (GES_CONTAINER (clip),
            GES_TIMELINE_ELEMENT (track_element_copy))) {
      GST_WARNING_OBJECT (clip, "Failed to add track element to clip");
      gst_object_unref (track_element_copy);
      g_ptr_array_unref (tracks);
      return;
    }

    if (!ges_track_add_element (track, track_element_copy)) {
      GST_WARNING_OBJECT (clip, "Failed to add track element to track");
      ges_container_remove (GES_CONTAINER (clip),
          GES_TIMELINE_ELEMENT (track_element_copy));
      gst_object_unref (track_element_copy);
      g_ptr_array_unref (tracks);
      return;
    }

    gst_object_unref (track);
  }
  timeline->priv->ignore_track_element_added = NULL;
  g_ptr_array_unref (tracks);
  if (GES_IS_SOURCE (track_element))
    timeline_tree_create_transitions (timeline->priv->tree,
        ges_timeline_find_auto_transition);
}

static void
clip_track_element_removed_cb (GESClip * clip,
    GESTrackElement * track_element, GESTimeline * timeline)
{
  GESTrack *track = ges_track_element_get_track (track_element);

  if (track)
    ges_track_remove_element (track, track_element);
}

static void
layer_object_added_cb (GESLayer * layer, GESClip * clip, GESTimeline * timeline)
{
  GESProject *project;

  /* We make sure not to be connected twice */
  g_signal_handlers_disconnect_by_func (clip, clip_track_element_added_cb,
      timeline);
  g_signal_handlers_disconnect_by_func (clip, clip_track_element_removed_cb,
      timeline);

  /* And we connect to the object */
  g_signal_connect (clip, "child-added",
      G_CALLBACK (clip_track_element_added_cb), timeline);
  g_signal_connect (clip, "child-removed",
      G_CALLBACK (clip_track_element_removed_cb), timeline);

  if (ges_clip_is_moving_from_layer (clip)) {
    GST_DEBUG ("Clip %p moving from one layer to another, not creating "
        "TrackElement", clip);
    timeline_tree_create_transitions (timeline->priv->tree,
        ges_timeline_find_auto_transition);
    return;
  }

  add_object_to_tracks (timeline, clip, NULL);

  GST_DEBUG ("Making sure that the asset is in our project");
  project =
      GES_PROJECT (ges_extractable_get_asset (GES_EXTRACTABLE (timeline)));
  ges_project_add_asset (project,
      ges_extractable_get_asset (GES_EXTRACTABLE (clip)));

  GST_DEBUG ("Done");
}

static void
layer_priority_changed_cb (GESLayer * layer,
    GParamSpec * arg G_GNUC_UNUSED, GESTimeline * timeline)
{
  if (timeline->priv->resyncing_layers)
    return;

  timeline->layers = g_list_sort (timeline->layers, (GCompareFunc)
      sort_layers);
}

static void
layer_object_removed_cb (GESLayer * layer, GESClip * clip,
    GESTimeline * timeline)
{
  GList *trackelements, *tmp;

  if (ges_clip_is_moving_from_layer (clip)) {
    GST_DEBUG ("Clip %p is moving from a layer to another, not doing"
        " anything on it", clip);
    return;
  }

  GST_DEBUG ("Clip %p removed from layer %p", clip, layer);

  /* Go over the clip's track element and figure out which one belongs to
   * the list of tracks we control */

  trackelements = ges_container_get_children (GES_CONTAINER (clip), FALSE);
  for (tmp = trackelements; tmp; tmp = tmp->next) {
    GESTrackElement *track_element = (GESTrackElement *) tmp->data;
    GESTrack *track = ges_track_element_get_track (track_element);

    if (!track)
      continue;

    GST_DEBUG_OBJECT (timeline, "Trying to remove TrackElement %p",
        track_element);

    /* FIXME Check if we should actually check that we control the
     * track in the new management of TrackElement context */
    LOCK_DYN (timeline);
    if (G_LIKELY (g_list_find_custom (timeline->priv->priv_tracks, track,
                (GCompareFunc) custom_find_track) || track == NULL)) {
      GST_DEBUG ("Belongs to one of the tracks we control");

      ges_track_remove_element (track, track_element);
    }
    UNLOCK_DYN (timeline);
  }
  g_signal_handlers_disconnect_by_func (clip, clip_track_element_added_cb,
      timeline);
  g_signal_handlers_disconnect_by_func (clip, clip_track_element_removed_cb,
      timeline);

  g_list_free_full (trackelements, gst_object_unref);

  GST_DEBUG ("Done");
}

static void
trackelement_start_changed_cb (GESTrackElement * child,
    GParamSpec * arg G_GNUC_UNUSED, GESTimeline * timeline)
{
  timeline_update_duration (timeline);
}

static void
track_element_added_cb (GESTrack * track, GESTrackElement * track_element,
    GESTimeline * timeline)
{
  /* Auto transition should be updated before we receive the signal */
  g_signal_connect_after (GES_TRACK_ELEMENT (track_element), "notify::start",
      G_CALLBACK (trackelement_start_changed_cb), timeline);
}

static void
track_element_removed_cb (GESTrack * track,
    GESTrackElement * track_element, GESTimeline * timeline)
{
  /* Disconnect all signal handlers */
  g_signal_handlers_disconnect_by_func (track_element,
      trackelement_start_changed_cb, timeline);
}

static GstPadProbeReturn
_pad_probe_cb (GstPad * mixer_pad, GstPadProbeInfo * info,
    GESTimeline * timeline)
{
  GstEvent *event = GST_PAD_PROBE_INFO_EVENT (info);
  if (GST_EVENT_TYPE (event) == GST_EVENT_STREAM_START) {
    LOCK_DYN (timeline);
    if (timeline->priv->stream_start_group_id == -1) {
      if (!gst_event_parse_group_id (event,
              &timeline->priv->stream_start_group_id))
        timeline->priv->stream_start_group_id = gst_util_group_id_next ();
    }

    info->data = gst_event_make_writable (event);
    gst_event_set_group_id (GST_PAD_PROBE_INFO_EVENT (info),
        timeline->priv->stream_start_group_id);
    UNLOCK_DYN (timeline);

    return GST_PAD_PROBE_REMOVE;
  }

  return GST_PAD_PROBE_OK;
}

static void
_ghost_track_srcpad (TrackPrivate * tr_priv)
{
  GstPad *pad;
  gchar *padname;
  gboolean no_more;
  GList *tmp;
  GESTrack *track = tr_priv->track;

  pad = gst_element_get_static_pad (GST_ELEMENT (track), "src");

  GST_DEBUG ("track:%p, pad:%s:%s", track, GST_DEBUG_PAD_NAME (pad));

  /* Remember the pad */
  LOCK_DYN (tr_priv->timeline);
  GST_OBJECT_LOCK (track);
  tr_priv->pad = pad;

  no_more = TRUE;
  for (tmp = tr_priv->timeline->priv->priv_tracks; tmp; tmp = g_list_next (tmp)) {
    TrackPrivate *tr_priv = (TrackPrivate *) tmp->data;

    if (!tr_priv->pad) {
      GST_LOG ("Found track without pad %p", tr_priv->track);
      no_more = FALSE;
    }
  }
  GST_OBJECT_UNLOCK (track);

  /* ghost it ! */
  GST_DEBUG ("Ghosting pad and adding it to ourself");
  padname = g_strdup_printf ("track_%p_src", track);
  tr_priv->ghostpad = gst_ghost_pad_new (padname, pad);
  g_free (padname);
  gst_pad_set_active (tr_priv->ghostpad, TRUE);
  gst_element_add_pad (GST_ELEMENT (tr_priv->timeline), tr_priv->ghostpad);

  if (no_more) {
    GST_DEBUG ("Signaling no-more-pads");
    gst_element_no_more_pads (GST_ELEMENT (tr_priv->timeline));
  }

  tr_priv->probe_id = gst_pad_add_probe (pad,
      GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM,
      (GstPadProbeCallback) _pad_probe_cb, tr_priv->timeline, NULL);

  UNLOCK_DYN (tr_priv->timeline);
}

gboolean
timeline_add_element (GESTimeline * timeline, GESTimelineElement * element)
{
  GESTimelineElement *same_name =
      g_hash_table_lookup (timeline->priv->all_elements,
      element->name);

  GST_DEBUG_OBJECT (timeline, "Adding element: %s", element->name);
  if (same_name) {
    GST_ERROR_OBJECT (timeline, "%s Already in the timeline %" GST_PTR_FORMAT,
        element->name, same_name);
    return FALSE;
  }

  g_hash_table_insert (timeline->priv->all_elements,
      ges_timeline_element_get_name (element), gst_object_ref (element));

  timeline_tree_track_element (timeline->priv->tree, element);

  return TRUE;
}

gboolean
timeline_remove_element (GESTimeline * timeline, GESTimelineElement * element)
{
  if (g_hash_table_remove (timeline->priv->all_elements, element->name)) {
    timeline_tree_stop_tracking_element (timeline->priv->tree, element);

    return TRUE;
  }

  return FALSE;
}

void
timeline_fill_gaps (GESTimeline * timeline)
{
  GList *tmp;

  for (tmp = timeline->tracks; tmp; tmp = tmp->next) {
    track_resort_and_fill_gaps (tmp->data);
  }
}

GNode *
timeline_get_tree (GESTimeline * timeline)
{
  return timeline->priv->tree;
}

/**** API *****/
/**
 * ges_timeline_new:
 *
 * Creates a new empty #GESTimeline.
 *
 * Returns: (transfer floating): The new timeline.
 */

GESTimeline *
ges_timeline_new (void)
{
  GESProject *project = ges_project_new (NULL);
  GESExtractable *timeline = g_object_new (GES_TYPE_TIMELINE, NULL);

  ges_extractable_set_asset (timeline, GES_ASSET (project));
  gst_object_unref (project);

  return GES_TIMELINE (timeline);
}

/**
 * ges_timeline_new_from_uri:
 * @uri: the URI to load from
 * @error: (out) (allow-none): An error to be set in case something wrong happens or %NULL
 *
 * Creates a timeline from the given URI.
 *
 * Returns: (transfer floating) (nullable): A new timeline if the uri was loaded
 * successfully, or %NULL if the uri could not be loaded.
 */
GESTimeline *
ges_timeline_new_from_uri (const gchar * uri, GError ** error)
{
  GESTimeline *ret;
  GESProject *project = ges_project_new (uri);

  ret = GES_TIMELINE (ges_asset_extract (GES_ASSET (project), error));
  gst_object_unref (project);

  return ret;
}

/**
 * ges_timeline_load_from_uri:
 * @timeline: an empty #GESTimeline into which to load the formatter
 * @uri: The URI to load from
 * @error: (out) (allow-none): An error to be set in case something wrong happens or %NULL
 *
 * Loads the contents of URI into the given timeline.
 *
 * Returns: %TRUE if the timeline was loaded successfully, or %FALSE if the uri
 * could not be loaded.
 */
gboolean
ges_timeline_load_from_uri (GESTimeline * timeline, const gchar * uri,
    GError ** error)
{
  GESProject *project;
  gboolean ret = FALSE;

  g_return_val_if_fail (GES_IS_TIMELINE (timeline), FALSE);
  g_return_val_if_fail ((ges_extractable_get_asset (GES_EXTRACTABLE
              (timeline)) == NULL), FALSE);

  project = ges_project_new (uri);
  ret = ges_project_load (project, timeline, error);
  gst_object_unref (project);

  return ret;
}

/**
 * ges_timeline_save_to_uri:
 * @timeline: a #GESTimeline
 * @uri: The location to save to
 * @formatter_asset: (allow-none): The formatter asset to use or %NULL. If %NULL,
 * will try to save in the same format as the one from which the timeline as been loaded
 * or default to the formatter with highest rank
 * @overwrite: %TRUE to overwrite file if it exists
 * @error: (out) (allow-none): An error to be set in case something wrong happens or %NULL
 *
 * Saves the timeline to the given location
 *
 * Returns: %TRUE if the timeline was successfully saved to the given location,
 * else %FALSE.
 */
gboolean
ges_timeline_save_to_uri (GESTimeline * timeline, const gchar * uri,
    GESAsset * formatter_asset, gboolean overwrite, GError ** error)
{
  GESProject *project;

  gboolean ret, created_proj = FALSE;

  g_return_val_if_fail (GES_IS_TIMELINE (timeline), FALSE);
  project =
      GES_PROJECT (ges_extractable_get_asset (GES_EXTRACTABLE (timeline)));

  if (project == NULL) {
    project = ges_project_new (NULL);
    created_proj = TRUE;
  }

  ret = ges_project_save (project, timeline, uri, formatter_asset, overwrite,
      error);

  if (created_proj)
    gst_object_unref (project);

  return ret;
}

/**
 * ges_timeline_get_groups:
 * @timeline: a #GESTimeline
 *
 * Get the list of #GESGroup present in the Timeline.
 *
 * Returns: (transfer none) (element-type GESGroup): the list of
 * #GESGroup that contain clips present in the timeline's layers.
 * Must not be changed.
 */
GList *
ges_timeline_get_groups (GESTimeline * timeline)
{
  g_return_val_if_fail (GES_IS_TIMELINE (timeline), NULL);
  CHECK_THREAD (timeline);

  return timeline->priv->groups;
}

/**
 * ges_timeline_append_layer:
 * @timeline: a #GESTimeline
 *
 * Append a newly created #GESLayer to @timeline
 * Note that you do not own any reference to the returned layer.
 *
 * Returns: (transfer none): The newly created #GESLayer, or the last (empty)
 * #GESLayer of @timeline.
 */
GESLayer *
ges_timeline_append_layer (GESTimeline * timeline)
{
  guint32 priority;
  GESLayer *layer;

  g_return_val_if_fail (GES_IS_TIMELINE (timeline), NULL);
  CHECK_THREAD (timeline);

  layer = ges_layer_new ();
  priority = g_list_length (timeline->layers);
  ges_layer_set_priority (layer, priority);

  ges_timeline_add_layer (timeline, layer);

  return layer;
}

/**
 * ges_timeline_add_layer:
 * @timeline: a #GESTimeline
 * @layer: (transfer floating): the #GESLayer to add
 *
 * Add the layer to the timeline. The reference to the @layer will be stolen
 * by the @timeline.
 *
 * Returns: %TRUE if the layer was properly added, else %FALSE.
 */
gboolean
ges_timeline_add_layer (GESTimeline * timeline, GESLayer * layer)
{
  gboolean auto_transition;
  GList *objects, *tmp;

  g_return_val_if_fail (GES_IS_TIMELINE (timeline), FALSE);
  g_return_val_if_fail (GES_IS_LAYER (layer), FALSE);
  CHECK_THREAD (timeline);

  GST_DEBUG ("timeline:%p, layer:%p", timeline, layer);

  /* We can only add a layer that doesn't already belong to another timeline */
  if (G_UNLIKELY (layer->timeline)) {
    GST_WARNING ("Layer belongs to another timeline, can't add it");
    gst_object_ref_sink (layer);
    gst_object_unref (layer);
    return FALSE;
  }

  /* Add to the list of layers, make sure we don't already control it */
  if (G_UNLIKELY (g_list_find (timeline->layers, (gconstpointer) layer))) {
    GST_WARNING ("Layer is already controlled by this timeline");
    gst_object_ref_sink (layer);
    gst_object_unref (layer);
    return FALSE;
  }

  auto_transition = ges_layer_get_auto_transition (layer);

  /* If the user doesn't explicitely set layer auto_transition, then set our */
  if (!auto_transition) {
    auto_transition = ges_timeline_get_auto_transition (timeline);
    ges_layer_set_auto_transition (layer, auto_transition);
  }

  gst_object_ref_sink (layer);
  timeline->layers = g_list_insert_sorted (timeline->layers, layer,
      (GCompareFunc) sort_layers);

  /* Inform the layer that it belongs to a new timeline */
  ges_layer_set_timeline (layer, timeline);

  /* Connect to 'clip-added'/'clip-removed' signal from the new layer */
  g_signal_connect_after (layer, "clip-added",
      G_CALLBACK (layer_object_added_cb), timeline);
  g_signal_connect_after (layer, "clip-removed",
      G_CALLBACK (layer_object_removed_cb), timeline);
  g_signal_connect (layer, "notify::priority",
      G_CALLBACK (layer_priority_changed_cb), timeline);
  g_signal_connect (layer, "notify::auto-transition",
      G_CALLBACK (layer_auto_transition_changed_cb), timeline);

  GST_DEBUG ("Done adding layer, emitting 'layer-added' signal");
  g_signal_emit (timeline, ges_timeline_signals[LAYER_ADDED], 0, layer);

  /* add any existing clips to the timeline */
  objects = ges_layer_get_clips (layer);
  for (tmp = objects; tmp; tmp = tmp->next) {
    layer_object_added_cb (layer, tmp->data, timeline);
    gst_object_unref (tmp->data);
    tmp->data = NULL;
  }
  g_list_free (objects);

  return TRUE;
}

/**
 * ges_timeline_remove_layer:
 * @timeline: a #GESTimeline
 * @layer: the #GESLayer to remove
 *
 * Removes the layer from the timeline. The reference that the @timeline holds on
 * the layer will be dropped. If you wish to use the @layer after calling this
 * method, you need to take a reference before calling.
 *
 * Returns: %TRUE if the layer was properly removed, else %FALSE.
 */

gboolean
ges_timeline_remove_layer (GESTimeline * timeline, GESLayer * layer)
{
  GList *layer_objects, *tmp;

  g_return_val_if_fail (GES_IS_TIMELINE (timeline), FALSE);
  g_return_val_if_fail (GES_IS_LAYER (layer), FALSE);
  CHECK_THREAD (timeline);

  GST_DEBUG ("timeline:%p, layer:%p", timeline, layer);

  if (G_UNLIKELY (!g_list_find (timeline->layers, layer))) {
    GST_WARNING ("Layer doesn't belong to this timeline");
    return FALSE;
  }

  /* remove objects from any private data structures */

  layer_objects = ges_layer_get_clips (layer);
  for (tmp = layer_objects; tmp; tmp = tmp->next) {
    layer_object_removed_cb (layer, GES_CLIP (tmp->data), timeline);
    gst_object_unref (G_OBJECT (tmp->data));
    tmp->data = NULL;
  }
  g_list_free (layer_objects);

  /* Disconnect signals */
  GST_DEBUG ("Disconnecting signal callbacks");
  g_signal_handlers_disconnect_by_func (layer, layer_object_added_cb, timeline);
  g_signal_handlers_disconnect_by_func (layer, layer_object_removed_cb,
      timeline);
  g_signal_handlers_disconnect_by_func (layer, layer_priority_changed_cb,
      timeline);
  g_signal_handlers_disconnect_by_func (layer,
      layer_auto_transition_changed_cb, timeline);

  timeline->layers = g_list_remove (timeline->layers, layer);
  ges_layer_set_timeline (layer, NULL);

  g_signal_emit (timeline, ges_timeline_signals[LAYER_REMOVED], 0, layer);

  gst_object_unref (layer);

  return TRUE;
}

/**
 * ges_timeline_add_track:
 * @timeline: a #GESTimeline
 * @track: (transfer full): the #GESTrack to add
 *
 * Add a track to the timeline. The reference to the track will be stolen by the
 * pipeline.
 *
 * Returns: %TRUE if the track was properly added, else %FALSE.
 */

/* FIXME: create track elements for clips which have already been
 * added to existing layers.
 */

gboolean
ges_timeline_add_track (GESTimeline * timeline, GESTrack * track)
{
  TrackPrivate *tr_priv;
  GList *tmp;

  g_return_val_if_fail (GES_IS_TIMELINE (timeline), FALSE);
  g_return_val_if_fail (GES_IS_TRACK (track), FALSE);
  CHECK_THREAD (timeline);

  GST_DEBUG ("timeline:%p, track:%p", timeline, track);

  /* make sure we don't already control it */
  if (G_UNLIKELY (g_list_find (timeline->tracks, (gconstpointer) track))) {
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
  LOCK_DYN (timeline);
  timeline->priv->priv_tracks = g_list_append (timeline->priv->priv_tracks,
      tr_priv);
  UNLOCK_DYN (timeline);
  timeline->tracks = g_list_append (timeline->tracks, track);

  /* Inform the track that it's currently being used by ourself */
  ges_track_set_timeline (track, timeline);

  GST_DEBUG ("Done adding track, emitting 'track-added' signal");

  _ghost_track_srcpad (tr_priv);

  /* emit 'track-added' */
  g_signal_emit (timeline, ges_timeline_signals[TRACK_ADDED], 0, track);

  /* ensure that each existing clip has the opportunity to create a
   * track element for this track*/

  /* We connect to the object for the timeline editing mode management */
  g_signal_connect (G_OBJECT (track), "track-element-added",
      G_CALLBACK (track_element_added_cb), timeline);
  g_signal_connect (G_OBJECT (track), "track-element-removed",
      G_CALLBACK (track_element_removed_cb), timeline);

  for (tmp = timeline->layers; tmp; tmp = tmp->next) {
    GList *objects, *obj;
    objects = ges_layer_get_clips (tmp->data);

    for (obj = objects; obj; obj = obj->next) {
      GESClip *clip = obj->data;

      add_object_to_tracks (timeline, clip, track);
      gst_object_unref (clip);
    }
    g_list_free (objects);
  }

  /* FIXME Check if we should rollback if we can't sync state */
  gst_element_sync_state_with_parent (GST_ELEMENT (track));
  g_object_set (track, "message-forward", TRUE, NULL);

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
 * Returns: %TRUE if the @track was properly removed, else %FALSE.
 */

/* FIXME: release any track elements associated with this layer. currenly this
 * will not happen if you remove the track before removing *all*
 * clips which have a track element in this track.
 */

gboolean
ges_timeline_remove_track (GESTimeline * timeline, GESTrack * track)
{
  GList *tmp;
  TrackPrivate *tr_priv;
  GESTimelinePrivate *priv;

  g_return_val_if_fail (GES_IS_TRACK (track), FALSE);
  g_return_val_if_fail (GES_IS_TIMELINE (timeline), FALSE);
  CHECK_THREAD (timeline);

  GST_DEBUG ("timeline:%p, track:%p", timeline, track);

  priv = timeline->priv;
  LOCK_DYN (timeline);
  if (G_UNLIKELY (!(tmp = g_list_find_custom (priv->priv_tracks,
                  track, (GCompareFunc) custom_find_track)))) {
    GST_WARNING ("Track doesn't belong to this timeline");
    UNLOCK_DYN (timeline);
    return FALSE;
  }

  tr_priv = tmp->data;
  gst_object_unref (tr_priv->pad);
  priv->priv_tracks = g_list_remove (priv->priv_tracks, tr_priv);
  UNLOCK_DYN (timeline);
  timeline->tracks = g_list_remove (timeline->tracks, track);

  ges_track_set_timeline (track, NULL);

  /* Remove ghost pad */
  if (tr_priv->ghostpad) {
    GST_DEBUG ("Removing ghostpad");
    gst_pad_set_active (tr_priv->ghostpad, FALSE);
    gst_ghost_pad_set_target ((GstGhostPad *) tr_priv->ghostpad, NULL);
    gst_element_remove_pad (GST_ELEMENT (timeline), tr_priv->ghostpad);
  }

  /* Remove pad-added/-removed handlers */
  g_signal_handlers_disconnect_by_func (track, track_element_added_cb,
      timeline);
  g_signal_handlers_disconnect_by_func (track, track_element_removed_cb,
      timeline);

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
 * Returns: (transfer none) (nullable): The corresponding #GESTrack if it is
 * found, or %NULL if there is an error.
 */

GESTrack *
ges_timeline_get_track_for_pad (GESTimeline * timeline, GstPad * pad)
{
  GList *tmp;

  g_return_val_if_fail (GES_IS_TIMELINE (timeline), NULL);

  LOCK_DYN (timeline);
  for (tmp = timeline->priv->priv_tracks; tmp; tmp = g_list_next (tmp)) {
    TrackPrivate *tr_priv = (TrackPrivate *) tmp->data;
    if (pad == tr_priv->ghostpad) {
      UNLOCK_DYN (timeline);
      return tr_priv->track;
    }
  }
  UNLOCK_DYN (timeline);

  return NULL;
}

/**
 * ges_timeline_get_pad_for_track:
 * @timeline: The #GESTimeline
 * @track: The #GESTrack
 *
 * Search the #GstPad corresponding to the given @timeline's @track.
 *
 * Returns: (transfer none) (nullable): The corresponding #GstPad if it is
 * found, or %NULL if there is an error.
 */

GstPad *
ges_timeline_get_pad_for_track (GESTimeline * timeline, GESTrack * track)
{
  GList *tmp;

  LOCK_DYN (timeline);
  for (tmp = timeline->priv->priv_tracks; tmp; tmp = g_list_next (tmp)) {
    TrackPrivate *tr_priv = (TrackPrivate *) tmp->data;

    if (track == tr_priv->track) {
      if (tr_priv->ghostpad)
        gst_object_ref (tr_priv->ghostpad);

      UNLOCK_DYN (timeline);
      return tr_priv->ghostpad;
    }
  }
  UNLOCK_DYN (timeline);

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
  g_return_val_if_fail (GES_IS_TIMELINE (timeline), NULL);
  CHECK_THREAD (timeline);

  return g_list_copy_deep (timeline->tracks, (GCopyFunc) gst_object_ref, NULL);
}

/**
 * ges_timeline_get_layers:
 * @timeline: a #GESTimeline
 *
 * Get the list of #GESLayer present in the Timeline.
 *
 * Returns: (transfer full) (element-type GESLayer): the list of
 * #GESLayer present in the Timeline sorted by priority.
 * The caller should unref each Layer once he is done with them.
 */
GList *
ges_timeline_get_layers (GESTimeline * timeline)
{
  GList *tmp, *res = NULL;

  g_return_val_if_fail (GES_IS_TIMELINE (timeline), NULL);
  CHECK_THREAD (timeline);

  for (tmp = timeline->layers; tmp; tmp = g_list_next (tmp)) {
    res = g_list_insert_sorted (res, gst_object_ref (tmp->data),
        (GCompareFunc) sort_layers);
  }

  return res;
}

static void
track_commited_cb (GESTrack * track, GESTimeline * timeline)
{
  gboolean emit_commited = FALSE;
  GST_OBJECT_LOCK (timeline);
  timeline->priv->expected_commited -= 1;
  if (timeline->priv->expected_commited == 0)
    emit_commited = TRUE;
  g_signal_handlers_disconnect_by_func (track, track_commited_cb, timeline);
  GST_OBJECT_UNLOCK (timeline);

  if (emit_commited) {
    g_signal_emit (timeline, ges_timeline_signals[COMMITED], 0);
  }
}

/* Must be called with the timeline's DYN_LOCK */
static gboolean
ges_timeline_commit_unlocked (GESTimeline * timeline)
{
  GList *tmp;
  gboolean res = TRUE;

  GST_DEBUG_OBJECT (timeline, "commiting changes");

  timeline_tree_create_transitions (timeline->priv->tree,
      ges_timeline_find_auto_transition);
  for (tmp = timeline->layers; tmp; tmp = tmp->next) {
    GESLayer *layer = tmp->data;

    /* Ensure clip priorities are correct after an edit */
    ges_layer_resync_priorities (layer);
  }

  timeline->priv->expected_commited =
      g_list_length (timeline->priv->priv_tracks);

  if (timeline->priv->expected_commited == 0) {
    g_signal_emit (timeline, ges_timeline_signals[COMMITED], 0);
  } else {
    for (tmp = timeline->tracks; tmp; tmp = tmp->next) {
      g_signal_connect (tmp->data, "commited", G_CALLBACK (track_commited_cb),
          timeline);
      if (!ges_track_commit (GES_TRACK (tmp->data)))
        res = FALSE;
    }
  }

  return res;
}

/**
 * ges_timeline_commit:
 * @timeline: a #GESTimeline
 *
 * Commit all the pending changes of the clips contained in the
 * @timeline.
 *
 * When changes happen in a timeline, they are not
 * directly executed in the non-linear engine. Call this method once you are
 * done with a set of changes and want it to be executed.
 *
 * The #GESTimeline::commited signal will be emitted when the (possibly updated)
 * #GstPipeline is ready to output data again, except if the state of the
 * timeline was #GST_STATE_READY or #GST_STATE_NULL.
 *
 * Note that all the pending changes will automatically be executed when the
 * timeline goes from #GST_STATE_READY to #GST_STATE_PAUSED, which usually is
 * triggered by corresponding state changes in a containing #GESPipeline.
 *
 * You should not try to change the state of the timeline, seek it or add
 * tracks to it during a commit operation, that is between a call to this
 * function and after receiving the #GESTimeline::commited signal.
 *
 * See #ges_timeline_commit_sync if you don't want to bother with waiting
 * for the signal.
 *
 * Returns: %TRUE if pending changes were commited or %FALSE if nothing needed
 * to be commited
 */
gboolean
ges_timeline_commit (GESTimeline * timeline)
{
  gboolean ret;

  g_return_val_if_fail (GES_IS_TIMELINE (timeline), FALSE);

  LOCK_DYN (timeline);
  ret = ges_timeline_commit_unlocked (timeline);
  UNLOCK_DYN (timeline);

  ges_timeline_emit_snapping (timeline, NULL, NULL, GST_CLOCK_TIME_NONE);
  return ret;
}

static void
commited_cb (GESTimeline * timeline)
{
  g_mutex_lock (&timeline->priv->commited_lock);
  g_cond_signal (&timeline->priv->commited_cond);
  g_mutex_unlock (&timeline->priv->commited_lock);
}

/**
 * ges_timeline_commit_sync:
 * @timeline: a #GESTimeline
 *
 * Commit all the pending changes of the #GESClips contained in the
 * @timeline.
 *
 * Will return once the update is complete, that is when the
 * (possibly updated) #GstPipeline is ready to output data again, or if the
 * state of the timeline was #GST_STATE_READY or #GST_STATE_NULL.
 *
 * This function will wait for any pending state change of the timeline by
 * calling #gst_element_get_state with a #GST_CLOCK_TIME_NONE timeout, you
 * should not try to change the state from another thread before this function
 * has returned.
 *
 * See #ges_timeline_commit for more information.
 *
 * Returns: %TRUE if pending changes were commited or %FALSE if nothing needed
 * to be commited
 */
gboolean
ges_timeline_commit_sync (GESTimeline * timeline)
{
  gboolean ret;
  gboolean wait_for_signal;

  g_return_val_if_fail (GES_IS_TIMELINE (timeline), FALSE);

  /* Let's make sure our state is stable */
  gst_element_get_state (GST_ELEMENT (timeline), NULL, NULL,
      GST_CLOCK_TIME_NONE);

  /* Let's make sure no track gets added between now and the actual commiting */
  LOCK_DYN (timeline);
  wait_for_signal = g_list_length (timeline->priv->priv_tracks) > 0
      && GST_STATE (timeline) >= GST_STATE_PAUSED;

  if (!wait_for_signal) {
    ret = ges_timeline_commit_unlocked (timeline);
  } else {
    gulong handler_id =
        g_signal_connect (timeline, "commited", (GCallback) commited_cb, NULL);

    g_mutex_lock (&timeline->priv->commited_lock);

    ret = ges_timeline_commit_unlocked (timeline);
    g_cond_wait (&timeline->priv->commited_cond,
        &timeline->priv->commited_lock);
    g_mutex_unlock (&timeline->priv->commited_lock);
    g_signal_handler_disconnect (timeline, handler_id);
  }

  UNLOCK_DYN (timeline);

  return ret;
}

/**
 * ges_timeline_get_duration:
 * @timeline: a #GESTimeline
 *
 * Get the current duration of @timeline
 *
 * Returns: The current duration of @timeline
 */
GstClockTime
ges_timeline_get_duration (GESTimeline * timeline)
{
  g_return_val_if_fail (GES_IS_TIMELINE (timeline), GST_CLOCK_TIME_NONE);
  CHECK_THREAD (timeline);

  return timeline->priv->duration;
}

/**
 * ges_timeline_get_auto_transition:
 * @timeline: a #GESTimeline
 *
 * Gets whether transitions are automatically added when objects
 * overlap or not.
 *
 * Returns: %TRUE if transitions are automatically added, else %FALSE.
 */
gboolean
ges_timeline_get_auto_transition (GESTimeline * timeline)
{
  g_return_val_if_fail (GES_IS_TIMELINE (timeline), FALSE);
  CHECK_THREAD (timeline);

  return timeline->priv->auto_transition;
}

/**
 * ges_timeline_set_auto_transition:
 * @timeline: a #GESLayer
 * @auto_transition: whether the auto_transition is active
 *
 * Sets the layer to the given @auto_transition. See the documentation of the
 * property auto_transition for more information.
 */
void
ges_timeline_set_auto_transition (GESTimeline * timeline,
    gboolean auto_transition)
{
  GList *layers;
  GESLayer *layer;

  g_return_if_fail (GES_IS_TIMELINE (timeline));
  CHECK_THREAD (timeline);

  timeline->priv->auto_transition = auto_transition;
  g_object_notify (G_OBJECT (timeline), "auto-transition");

  layers = timeline->layers;
  for (; layers; layers = layers->next) {
    layer = layers->data;
    ges_layer_set_auto_transition (layer, auto_transition);
  }
}

/**
 * ges_timeline_get_snapping_distance:
 * @timeline: a #GESTimeline
 *
 * Gets the configured snapping distance of the timeline. See
 * the documentation of the property snapping_distance for more
 * information.
 *
 * Returns: The @snapping_distance property of the timeline
 */
GstClockTime
ges_timeline_get_snapping_distance (GESTimeline * timeline)
{
  g_return_val_if_fail (GES_IS_TIMELINE (timeline), GST_CLOCK_TIME_NONE);
  CHECK_THREAD (timeline);

  return timeline->priv->snapping_distance;

}

/**
 * ges_timeline_set_snapping_distance:
 * @timeline: a #GESLayer
 * @snapping_distance: whether the snapping_distance is active
 *
 * Sets the @snapping_distance of the timeline. See the documentation of the
 * property snapping_distance for more information.
 */
void
ges_timeline_set_snapping_distance (GESTimeline * timeline,
    GstClockTime snapping_distance)
{
  g_return_if_fail (GES_IS_TIMELINE (timeline));
  CHECK_THREAD (timeline);

  timeline->priv->snapping_distance = snapping_distance;
}

/**
 * ges_timeline_get_element:
 * @timeline: a #GESTimeline
 *
 * Gets a #GESTimelineElement contained in the timeline
 *
 * Returns: (transfer full) (nullable): The #GESTimelineElement or %NULL if
 * not found.
 */
GESTimelineElement *
ges_timeline_get_element (GESTimeline * timeline, const gchar * name)
{
  GESTimelineElement *ret;

  g_return_val_if_fail (GES_IS_TIMELINE (timeline), NULL);
  CHECK_THREAD (timeline);

  ret = g_hash_table_lookup (timeline->priv->all_elements, name);

  if (ret)
    return gst_object_ref (ret);

#ifndef GST_DISABLE_GST_DEBUG
  {
    GList *element_names, *tmp;
    element_names = g_hash_table_get_keys (timeline->priv->all_elements);

    GST_INFO_OBJECT (timeline, "Does not contain element %s", name);

    for (tmp = element_names; tmp; tmp = tmp->next) {
      GST_DEBUG_OBJECT (timeline, "Containes: %s", (gchar *) tmp->data);
    }
    g_list_free (element_names);
  }
#endif

  return NULL;
}

/**
 * ges_timeline_is_empty:
 * @timeline: a #GESTimeline
 *
 * Check whether a #GESTimeline is empty or not
 *
 * Returns: %TRUE if the timeline is empty %FALSE otherwize
 */
gboolean
ges_timeline_is_empty (GESTimeline * timeline)
{
  GHashTableIter iter;
  gpointer key, value;

  g_return_val_if_fail (GES_IS_TIMELINE (timeline), FALSE);
  CHECK_THREAD (timeline);

  if (g_hash_table_size (timeline->priv->all_elements) == 0)
    return TRUE;

  g_hash_table_iter_init (&iter, timeline->priv->all_elements);
  while (g_hash_table_iter_next (&iter, &key, &value)) {
    if (GES_IS_SOURCE (value) &&
        ges_track_element_is_active (GES_TRACK_ELEMENT (value)))
      return FALSE;
  }

  return TRUE;
}

/**
 * ges_timeline_get_layer:
 * @timeline: The #GESTimeline to retrive a layer from
 * @priority: The priority of the layer to find
 *
 * Retrieve the layer with @priority as a priority
 *
 * Returns: (transfer full) (nullable): A #GESLayer or %NULL if no layer with
 * @priority was found
 *
 * Since 1.6
 */
GESLayer *
ges_timeline_get_layer (GESTimeline * timeline, guint priority)
{
  GList *tmp;
  GESLayer *layer = NULL;

  g_return_val_if_fail (GES_IS_TIMELINE (timeline), NULL);
  CHECK_THREAD (timeline);

  for (tmp = timeline->layers; tmp; tmp = tmp->next) {
    GESLayer *tmp_layer = GES_LAYER (tmp->data);
    guint tmp_priority;

    g_object_get (tmp_layer, "priority", &tmp_priority, NULL);
    if (tmp_priority == priority) {
      layer = gst_object_ref (tmp_layer);
      break;
    }
  }

  return layer;
}

/**
 * ges_timeline_paste_element:
 * @timeline: The #GESTimeline onto which the #GESTimelineElement should be pasted
 * @element: The #GESTimelineElement to paste
 * @position: The position in the timeline the element should
 * be pasted to, meaning it will become the start of @element
 * @layer_priority: The #GESLayer to which the element should be pasted to.
 * -1 means paste to the same layer from which the @element has been copied from.
 *
 * Paste @element inside the timeline. @element must have been
 * created using ges_timeline_element_copy with deep=TRUE set,
 * i.e. it must be a deep copy, otherwise it will fail.
 *
 * Returns: (transfer none): Shallow copy of the @element pasted
 */
GESTimelineElement *
ges_timeline_paste_element (GESTimeline * timeline,
    GESTimelineElement * element, GstClockTime position, gint layer_priority)
{
  GESTimelineElement *res, *copied_from;
  GESTimelineElementClass *element_class;

  g_return_val_if_fail (GES_IS_TIMELINE (timeline), FALSE);
  g_return_val_if_fail (GES_IS_TIMELINE_ELEMENT (element), FALSE);
  CHECK_THREAD (timeline);

  element_class = GES_TIMELINE_ELEMENT_GET_CLASS (element);
  copied_from = ges_timeline_element_get_copied_from (element);

  if (!copied_from) {
    GST_ERROR_OBJECT (element, "Is not being 'deeply' copied!");

    return NULL;
  }

  if (!element_class->paste) {
    GST_ERROR_OBJECT (element, "No paste vmethod implemented");

    return NULL;
  }

  /*
   * Currently the API only supports pasting onto the same layer from which
   * the @element has been copied from, i.e., @layer_priority needs to be -1.
   */
  if (layer_priority != -1) {
    GST_WARNING_OBJECT (timeline,
        "Only -1 value for layer priority is supported");
  }

  res = element_class->paste (element, copied_from, position);

  g_clear_object (&copied_from);

  return g_object_ref (res);
}

/**
 * ges_timeline_move_layer:
 * @timeline: The timeline in which @layer must be
 * @layer: The layer to move at @new_layer_priority
 * @new_layer_priority: The index at which @layer should land
 *
 * Moves @layer at @new_layer_priority meaning that @layer
 * we land at that position in the stack of layers inside
 * the timeline. If @new_layer_priority is superior than the number
 * of layers present in the time, it will move to the end of the
 * stack of layers.
 */
gboolean
ges_timeline_move_layer (GESTimeline * timeline, GESLayer * layer,
    guint new_layer_priority)
{
  gint current_priority;

  g_return_val_if_fail (GES_IS_TIMELINE (timeline), FALSE);
  g_return_val_if_fail (GES_IS_LAYER (layer), FALSE);
  g_return_val_if_fail (ges_layer_get_timeline (layer) == timeline, FALSE);
  CHECK_THREAD (timeline);

  current_priority = ges_layer_get_priority (layer);

  if (new_layer_priority == current_priority) {
    GST_DEBUG_OBJECT (timeline,
        "Nothing to do for %" GST_PTR_FORMAT ", same priorities", layer);

    return TRUE;
  }

  timeline->layers = g_list_remove (timeline->layers, layer);
  timeline->layers = g_list_insert (timeline->layers, layer,
      (gint) new_layer_priority);

  _resync_layers (timeline);

  return TRUE;
}
