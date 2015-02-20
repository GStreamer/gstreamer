/* GStreamer Editing Services
 * Copyright (C) 2009 Edward Hervey <edward.hervey@collabora.co.uk>
 *               2009 Nokia Corporation
 *               2012 Thibault Saunier <tsaunier@gnome.org>
 *               2012 Collabora Ltd.
 *                 Author: Sebastian Dröge <sebastian.droege@collabora.co.uk>
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

#include "ges-internal.h"
#include "ges-project.h"
#include "ges-container.h"
#include "ges-timeline.h"
#include "ges-track.h"
#include "ges-layer.h"
#include "ges-auto-transition.h"
#include "ges.h"

typedef struct _MoveContext MoveContext;

static GPtrArray *select_tracks_for_object_default (GESTimeline * timeline,
    GESClip * clip, GESTrackElement * tr_obj, gpointer user_data);
static inline void init_movecontext (MoveContext * mv_ctx, gboolean first_init);
static void ges_extractable_interface_init (GESExtractableInterface * iface);
static void ges_meta_container_interface_init
    (GESMetaContainerInterface * iface);

G_DEFINE_TYPE_WITH_CODE (GESTimeline, ges_timeline, GST_TYPE_BIN,
    G_IMPLEMENT_INTERFACE (GES_TYPE_EXTRACTABLE, ges_extractable_interface_init)
    G_IMPLEMENT_INTERFACE (GES_TYPE_META_CONTAINER,
        ges_meta_container_interface_init));


GST_DEBUG_CATEGORY_STATIC (ges_timeline_debug);
#undef GST_CAT_DEFAULT
#define GST_CAT_DEFAULT ges_timeline_debug

/* lock to protect dynamic callbacks, like pad-added */
#define DYN_LOCK(timeline) (&GES_TIMELINE (timeline)->priv->dyn_mutex)
#define LOCK_DYN(timeline) G_STMT_START {                       \
    GST_INFO_OBJECT (timeline, "Getting dynamic lock from %p", \
        g_thread_self());                                       \
    g_rec_mutex_lock (DYN_LOCK (timeline));                     \
    GST_INFO_OBJECT (timeline, "Got Dynamic lock from %p",     \
        g_thread_self());         \
  } G_STMT_END

#define UNLOCK_DYN(timeline) G_STMT_START {                         \
    GST_INFO_OBJECT (timeline, "Unlocking dynamic lock from %p", \
        g_thread_self());                                         \
    g_rec_mutex_unlock (DYN_LOCK (timeline));                     \
    GST_INFO_OBJECT (timeline, "Unlocked Dynamic lock from %p",  \
        g_thread_self());         \
  } G_STMT_END

typedef struct TrackObjIters
{
  GSequenceIter *iter_start;
  GSequenceIter *iter_end;
  GSequenceIter *iter_obj;
  GSequenceIter *iter_by_layer;

  GESLayer *layer;
  GESTrackElement *trackelement;
} TrackObjIters;

static void
_destroy_obj_iters (TrackObjIters * iters)
{
  g_slice_free (TrackObjIters, iters);
}

/*  The move context is used for the timeline editing modes functions in order to
 *  + Ripple / Roll /  Slide / Move / Trim
 *
 * The context aims at avoiding to recalculate values/objects on each call of the
 * editing functions.
 */
struct _MoveContext
{
  GESClip *clip;
  GESEdge edge;
  GESEditMode mode;

  /* Ripple and Roll Objects */
  GList *moving_trackelements;

  /* We use it as a set of Clip to move between layers */
  GHashTable *toplevel_containers;
  /* Min priority of the objects currently in toplevel_containers */
  guint min_move_layer;
  /* Max priority of the objects currently in toplevel_containers */
  guint max_layer_prio;

  /* Never trim so duration would becomes < 0 */
  guint64 max_trim_pos;

  /* fields to force/avoid new context */
  /* Set to %TRUE when the track is doing updates of track element
   * properties so we don't end up always needing new move context */
  gboolean ignore_needs_ctx;
  gboolean needs_move_ctx;

  /* Last snapping  properties */
  GESTrackElement *last_snaped1;
  GESTrackElement *last_snaped2;
  GstClockTime *last_snap_ts;
};

struct _GESTimelinePrivate
{
  /* The duration of the timeline */
  gint64 duration;

  /* The auto-transition of the timeline */
  gboolean auto_transition;

  /* Timeline edition modes and snapping management */
  guint64 snapping_distance;

  /* FIXME: Should we offer an API over those fields ?
   * FIXME: Should other classes than subclasses of Source also
   * be tracked? */

  /* Snapping fields */
  GHashTable *by_start;         /* {Source: start} */
  GHashTable *by_end;           /* {Source: end} */
  GHashTable *by_object;        /* {timecode: Source} */
  GHashTable *obj_iters;        /* {Source: TrackObjIters} */
  GSequence *starts_ends;       /* Sorted list of starts/ends */
  /* We keep 1 reference to our trackelement here */
  GSequence *tracksources;      /* Source-s sorted by start/priorities */

  GRecMutex dyn_mutex;
  GList *priv_tracks;
  /* FIXME: We should definitly offer an API over this,
   * probably through a ges_layer_get_track_elements () method */
  GHashTable *by_layer;         /* {layer: GSequence of TrackElement by start/priorities} */

  /* The set of auto_transitions we control, currently the key is
   * pointerToPreviousiTrackObjAdresspointerToNextTrackObjAdress as a string,
   * ... not really optimal but it works */
  GHashTable *auto_transitions;

  MoveContext movecontext;

  /* This variable is set to %TRUE when it makes sense to update the transitions,
   * and %FALSE otherwize */
  gboolean needs_transitions_update;

  /* While we are creating and adding the TrackElements for a clip, we need to
   * ignore the child-added signal */
  GESClip *ignore_track_element_added;
  GList *groups;

  guint group_id;

  GHashTable *all_elements;

  guint expected_async_done;
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
  SNAPING_STARTED,
  SNAPING_ENDED,
  SELECT_TRACKS_FOR_OBJECT,
  COMMITED,
  LAST_SIGNAL
};

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

  while (priv->groups)
    g_list_free_full (ges_container_ungroup (priv->groups->data, FALSE),
        gst_object_unref);

  g_hash_table_unref (priv->by_start);
  g_hash_table_unref (priv->by_end);
  g_hash_table_unref (priv->by_object);
  g_hash_table_unref (priv->by_layer);
  g_hash_table_unref (priv->obj_iters);
  g_sequence_free (priv->starts_ends);
  g_sequence_free (priv->tracksources);
  g_list_free (priv->movecontext.moving_trackelements);
  g_hash_table_unref (priv->movecontext.toplevel_containers);

  g_hash_table_unref (priv->auto_transitions);

  g_hash_table_unref (priv->all_elements);

  G_OBJECT_CLASS (ges_timeline_parent_class)->dispose (object);
}

static void
ges_timeline_finalize (GObject * object)
{
  GESTimeline *tl = GES_TIMELINE (object);

  g_rec_mutex_clear (&tl->priv->dyn_mutex);

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
            " not concidering async", gst_structure_get_string (mstructure,
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
            " not concidering async", gst_structure_get_string (mstructure,
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

  g_type_class_add_private (klass, sizeof (GESTimelinePrivate));

  GST_DEBUG_CATEGORY_INIT (ges_timeline_debug, "gestimeline",
      GST_DEBUG_FG_YELLOW, "ges timeline");

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
   * Will be emitted after the layer was added to the timeline.
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
   * GESTimeline::track-elements-snapping:
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
   * GESTimeline::snapping-end:
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
   * @clip: The #GESClip on which @track-element will land
   * @track-element: The #GESTrackElement for which to choose the tracks it should land into
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
   */
  ges_timeline_signals[COMMITED] =
      g_signal_new ("commited", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 0);
}

static void
ges_timeline_init (GESTimeline * self)
{
  GESTimelinePrivate *priv = self->priv;

  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GES_TYPE_TIMELINE, GESTimelinePrivate);

  priv = self->priv;
  self->layers = NULL;
  self->tracks = NULL;
  self->priv->duration = 0;
  self->priv->auto_transition = FALSE;
  priv->snapping_distance = 0;
  priv->expected_async_done = 0;

  /* Move context initialization */
  init_movecontext (&self->priv->movecontext, TRUE);
  priv->movecontext.ignore_needs_ctx = FALSE;

  priv->priv_tracks = NULL;
  priv->by_start = g_hash_table_new (g_direct_hash, g_direct_equal);
  priv->by_end = g_hash_table_new (g_direct_hash, g_direct_equal);
  priv->by_object = g_hash_table_new (g_direct_hash, g_direct_equal);
  priv->by_layer = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL,
      (GDestroyNotify) g_sequence_free);
  priv->obj_iters = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL,
      (GDestroyNotify) _destroy_obj_iters);
  priv->starts_ends = g_sequence_new (g_free);
  priv->tracksources = g_sequence_new (gst_object_unref);

  priv->auto_transitions =
      g_hash_table_new_full (g_str_hash, g_str_equal, NULL, gst_object_unref);
  priv->needs_transitions_update = TRUE;

  priv->all_elements =
      g_hash_table_new_full (g_str_hash, g_str_equal, g_free, gst_object_unref);

  priv->group_id = -1;

  g_signal_connect_after (self, "select-tracks-for-object",
      G_CALLBACK (select_tracks_for_object_default), NULL);

  g_rec_mutex_init (&priv->dyn_mutex);
}

/* Private methods */

static inline GESContainer *
get_toplevel_container (GESTrackElement * element)
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
timeline_update_duration (GESTimeline * timeline)
{
  GstClockTime *cduration;
  GSequenceIter *it = g_sequence_get_end_iter (timeline->priv->starts_ends);

  it = g_sequence_iter_prev (it);

  if (g_sequence_iter_is_end (it)) {
    timeline->priv->duration = 0;
    g_object_notify_by_pspec (G_OBJECT (timeline), properties[PROP_DURATION]);
    return;
  }

  cduration = g_sequence_get (it);

  if (cduration && timeline->priv->duration != *cduration) {
    GST_DEBUG ("track duration : %" GST_TIME_FORMAT " current : %"
        GST_TIME_FORMAT, GST_TIME_ARGS (*cduration),
        GST_TIME_ARGS (timeline->priv->duration));

    timeline->priv->duration = *cduration;

    g_object_notify_by_pspec (G_OBJECT (timeline), properties[PROP_DURATION]);
  }
}

static gint
find_layer_by_prio (GESLayer * a, gpointer pprio)
{
  gint prio = GPOINTER_TO_INT (pprio), lprio = ges_layer_get_priority (a);

  if (lprio < prio)
    return -1;
  if (lprio > prio)
    return 1;
  return 0;
}

static void
sort_track_elements (GESTimeline * timeline, TrackObjIters * iters)
{
  g_sequence_sort_changed (iters->iter_obj,
      (GCompareDataFunc) element_start_compare, NULL);
}

static gint
compare_uint64 (guint64 * a, guint64 * b, gpointer user_data)
{
  if (*a > *b)
    return 1;
  else if (*a == *b)
    return 0;
  else
    return -1;
}

static gint
custom_find_track (TrackPrivate * tr_priv, GESTrack * track)
{
  if (tr_priv->track == track)
    return 0;
  return -1;
}

static inline void
sort_starts_ends_end (GESTimeline * timeline, TrackObjIters * iters)
{
  GESTimelineElement *obj = GES_TIMELINE_ELEMENT (iters->trackelement);
  GESTimelinePrivate *priv = timeline->priv;
  guint64 *end = g_hash_table_lookup (priv->by_end, obj);

  *end = _START (obj) + _DURATION (obj);

  g_sequence_sort_changed (iters->iter_end, (GCompareDataFunc) compare_uint64,
      NULL);
  timeline_update_duration (timeline);
}

static inline void
sort_starts_ends_start (GESTimeline * timeline, TrackObjIters * iters)
{
  GESTimelineElement *obj = GES_TIMELINE_ELEMENT (iters->trackelement);
  GESTimelinePrivate *priv = timeline->priv;
  guint64 *start = g_hash_table_lookup (priv->by_start, obj);

  *start = _START (obj);

  g_sequence_sort_changed (iters->iter_start,
      (GCompareDataFunc) compare_uint64, NULL);
  timeline_update_duration (timeline);
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

  if (!g_hash_table_remove (priv->auto_transitions, auto_transition->key))
    GST_WARNING_OBJECT (timeline, "Could not remove auto_transition %"
        GST_PTR_FORMAT, auto_transition->key);
}

static GESAutoTransition *
create_transition (GESTimeline * timeline, GESTrackElement * previous,
    GESTrackElement * next, GESClip * transition,
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

  g_hash_table_insert (timeline->priv->auto_transitions,
      auto_transition->key, auto_transition);

  return auto_transition;
}

typedef GESAutoTransition *(*GetAutoTransitionFunc) (GESTimeline * timeline,
    GESLayer * layer, GESTrack * track, GESTrackElement * previous,
    GESTrackElement * next, GstClockTime transition_duration);

static GESAutoTransition *
_find_transition_from_auto_transitions (GESTimeline * timeline,
    GESLayer * layer, GESTrack * track, GESTrackElement * prev,
    GESTrackElement * next, GstClockTime transition_duration)
{
  GESAutoTransition *auto_transition;

  gchar *key = g_strdup_printf ("%p%p", prev, next);

  auto_transition = g_hash_table_lookup (timeline->priv->auto_transitions, key);
  g_free (key);

  return auto_transition;
}

static GESAutoTransition *
_create_auto_transition_from_transitions (GESTimeline * timeline,
    GESLayer * layer, GESTrack * track, GESTrackElement * prev,
    GESTrackElement * next, GstClockTime transition_duration)
{
  GSequenceIter *tmp_iter;
  GSequence *by_layer_sequence;

  GESTimelinePrivate *priv = timeline->priv;
  GESAutoTransition *auto_transition =
      _find_transition_from_auto_transitions (timeline, layer, track, prev,
      next, transition_duration);

  if (auto_transition)
    return auto_transition;


  /* Try to find a transition that perfectly fits with the one that
   * should be added at that place
   * optimize: Use g_sequence_search instead of going over all the
   * sequence */
  by_layer_sequence = g_hash_table_lookup (priv->by_layer, layer);
  for (tmp_iter = g_sequence_get_begin_iter (by_layer_sequence);
      tmp_iter && !g_sequence_iter_is_end (tmp_iter);
      tmp_iter = g_sequence_iter_next (tmp_iter)) {
    GESTrackElement *maybe_transition = g_sequence_get (tmp_iter);

    if (ges_track_element_get_track (maybe_transition) != track)
      continue;

    if (_START (maybe_transition) > _START (next))
      break;
    else if (_START (maybe_transition) != _START (next) ||
        _DURATION (maybe_transition) != transition_duration)
      continue;
    else if (GES_IS_TRANSITION (maybe_transition))
      /* Use that transition */
      /* TODO We should make sure that the transition contains only
       * TrackElement-s in @track and if it is not the case properly unlink the
       * object to use it */
      return create_transition (timeline, prev, next,
          GES_CLIP (GES_TIMELINE_ELEMENT_PARENT (maybe_transition)), layer,
          _START (next), transition_duration);
  }

  return NULL;
}

/* Create all transition that do not exist on @layer.
 * @get_auto_transition is called to check if a particular transition exists
 * if @ track is specified, we will create the transitions only for that particular
 * track */
static void
_create_transitions_on_layer (GESTimeline * timeline, GESLayer * layer,
    GESTrack * track, GESTrackElement * initiating_obj,
    GetAutoTransitionFunc get_auto_transition)
{
  guint32 layer_prio;
  GSequenceIter *iter;
  GESAutoTransition *transition;

  GESTrack *ctrack = track;
  GList *entered = NULL;        /* List of TrackElement for wich we walk through the
                                 * "start" but not the "end" in the starts_ends list */
  GESTimelinePrivate *priv = timeline->priv;

  if (!layer || !ges_layer_get_auto_transition (layer))
    return;

  layer_prio = ges_layer_get_priority (layer);
  for (iter = g_sequence_get_begin_iter (priv->starts_ends);
      iter && !g_sequence_iter_is_end (iter);
      iter = g_sequence_iter_next (iter)) {
    GList *tmp;
    guint *start_or_end = g_sequence_get (iter);
    GESTrackElement *next = g_hash_table_lookup (timeline->priv->by_object,
        start_or_end);
    GESTimelineElement *toplevel =
        ges_timeline_element_get_toplevel_parent (GES_TIMELINE_ELEMENT (next));

    /* Only object that are in that layer and track */
    if (_ges_track_element_get_layer_priority (next) != layer_prio ||
        (track && track != ges_track_element_get_track (next)))
      continue;

    if (track == NULL)
      ctrack = ges_track_element_get_track (next);

    if (start_or_end == g_hash_table_lookup (priv->by_end, next)) {
      if (initiating_obj == next) {
        /* We passed the objects that initiated the research
         * we are now done */
        g_list_free (entered);
        return;
      }
      entered = g_list_remove (entered, next);

      continue;
    }

    for (tmp = entered; tmp; tmp = tmp->next) {
      gint64 transition_duration;

      GESTrackElement *prev = tmp->data;

      if (ctrack != ges_track_element_get_track (prev) ||
          ges_timeline_element_get_toplevel_parent (GES_TIMELINE_ELEMENT (prev))
          == toplevel)
        continue;

      transition_duration = (_START (prev) + _DURATION (prev)) - _START (next);
      if (transition_duration > 0 && transition_duration < _DURATION (prev) &&
          transition_duration < _DURATION (next)) {
        transition =
            get_auto_transition (timeline, layer, ctrack, prev, next,
            transition_duration);
        if (!transition)
          create_transition (timeline, prev, next, NULL, layer,
              _START (next), transition_duration);
      }
    }

    /* And add that object to the entered list so that it we can possibly set
     * a transition on its end edge */
    entered = g_list_append (entered, next);
  }
}

/* @track_element must be a GESSource */
static void
create_transitions (GESTimeline * timeline, GESTrackElement * track_element)
{
  GESTrack *track;
  GList *layer_node;

  GESTimelinePrivate *priv = timeline->priv;

  if (!priv->needs_transitions_update)
    return;

  GST_DEBUG_OBJECT (timeline, "Creating transitions around %p", track_element);

  track = ges_track_element_get_track (track_element);
  layer_node = g_list_find_custom (timeline->layers,
      GINT_TO_POINTER (_ges_track_element_get_layer_priority (track_element)),
      (GCompareFunc) find_layer_by_prio);

  _create_transitions_on_layer (timeline,
      layer_node ? layer_node->data : NULL, track, track_element,
      _find_transition_from_auto_transitions);

  GST_DEBUG_OBJECT (timeline, "Done updating transitions");
}

/* Timeline edition functions */
static inline void
init_movecontext (MoveContext * mv_ctx, gboolean first_init)
{
  if (G_UNLIKELY (first_init))
    mv_ctx->toplevel_containers =
        g_hash_table_new (g_direct_hash, g_direct_equal);

  mv_ctx->moving_trackelements = NULL;
  mv_ctx->max_trim_pos = G_MAXUINT64;
  mv_ctx->min_move_layer = G_MAXUINT;
  mv_ctx->max_layer_prio = 0;
  mv_ctx->last_snaped1 = NULL;
  mv_ctx->last_snaped2 = NULL;
  mv_ctx->last_snap_ts = NULL;
}

static inline void
clean_movecontext (MoveContext * mv_ctx)
{
  g_list_free (mv_ctx->moving_trackelements);
  g_hash_table_remove_all (mv_ctx->toplevel_containers);
  init_movecontext (mv_ctx, FALSE);
}

static void
stop_tracking_track_element (GESTimeline * timeline,
    GESTrackElement * trackelement)
{
  guint64 *start, *end;
  TrackObjIters *iters;
  GESTimelinePrivate *priv = timeline->priv;

  iters = g_hash_table_lookup (priv->obj_iters, trackelement);
  if (G_LIKELY (iters->iter_by_layer)) {
    g_sequence_remove (iters->iter_by_layer);
  } else {
    GST_WARNING_OBJECT (timeline, "TrackElement %p was in no layer",
        trackelement);
  }

  if (GES_IS_SOURCE (trackelement)) {
    start = g_hash_table_lookup (priv->by_start, trackelement);
    end = g_hash_table_lookup (priv->by_end, trackelement);

    g_hash_table_remove (priv->by_start, trackelement);
    g_hash_table_remove (priv->by_end, trackelement);
    g_hash_table_remove (priv->by_object, end);
    g_hash_table_remove (priv->by_object, start);
    g_sequence_remove (iters->iter_start);
    g_sequence_remove (iters->iter_end);
    g_sequence_remove (iters->iter_obj);
    timeline_update_duration (timeline);
  }
  g_hash_table_remove (priv->obj_iters, trackelement);
}

static void
start_tracking_track_element (GESTimeline * timeline,
    GESTrackElement * trackelement)
{
  guint64 *pstart, *pend;
  GSequence *by_layer_sequence;
  TrackObjIters *iters;
  GESTimelinePrivate *priv = timeline->priv;

  guint layer_prio = _ges_track_element_get_layer_priority (trackelement);
  GList *layer_node = g_list_find_custom (timeline->layers,
      GINT_TO_POINTER (layer_prio), (GCompareFunc) find_layer_by_prio);
  GESLayer *layer = layer_node ? layer_node->data : NULL;

  iters = g_slice_new0 (TrackObjIters);

  /* We add all TrackElement to obj_iters as we always follow them
   * in the by_layer Sequences */
  g_hash_table_insert (priv->obj_iters, trackelement, iters);

  /* Track all objects by layer */
  if (G_UNLIKELY (layer == NULL)) {
    /* We handle the case where we have TrackElement that are in no layer by not
     * tracking them
     *
     * FIXME: Check if we should rather try to track them in some sort of
     * dummy layer, or even refuse TrackElements to be added in Tracks if
     * they land in no layer the timeline controls.
     */
    GST_ERROR_OBJECT (timeline, "Adding a TrackElement that lands in no layer "
        "we are controlling");
  } else {
    by_layer_sequence = g_hash_table_lookup (priv->by_layer, layer);
    iters->iter_by_layer =
        g_sequence_insert_sorted (by_layer_sequence, trackelement,
        (GCompareDataFunc) element_start_compare, NULL);
    iters->layer = layer;
  }

  if (GES_IS_SOURCE (trackelement)) {
    /* Track only sources for timeline edition and snapping */
    pstart = g_malloc (sizeof (guint64));
    pend = g_malloc (sizeof (guint64));
    *pstart = _START (trackelement);
    *pend = *pstart + _DURATION (trackelement);

    iters->iter_start = g_sequence_insert_sorted (priv->starts_ends, pstart,
        (GCompareDataFunc) compare_uint64, NULL);
    iters->iter_end = g_sequence_insert_sorted (priv->starts_ends, pend,
        (GCompareDataFunc) compare_uint64, NULL);
    iters->iter_obj =
        g_sequence_insert_sorted (priv->tracksources,
        gst_object_ref (trackelement), (GCompareDataFunc) element_start_compare,
        NULL);
    iters->trackelement = trackelement;

    g_hash_table_insert (priv->by_start, trackelement, pstart);
    g_hash_table_insert (priv->by_object, pstart, trackelement);
    g_hash_table_insert (priv->by_end, trackelement, pend);
    g_hash_table_insert (priv->by_object, pend, trackelement);

    timeline->priv->movecontext.needs_move_ctx = TRUE;

    timeline_update_duration (timeline);
    create_transitions (timeline, trackelement);
  }
}

static inline void
ges_timeline_emit_snappig (GESTimeline * timeline, GESTrackElement * obj1,
    guint64 * timecode)
{
  GESTrackElement *obj2;
  MoveContext *mv_ctx = &timeline->priv->movecontext;
  GstClockTime snap_time = timecode ? *timecode : 0;
  GstClockTime last_snap_ts = mv_ctx->last_snap_ts ?
      *mv_ctx->last_snap_ts : GST_CLOCK_TIME_NONE;

  GST_DEBUG_OBJECT (timeline, "Distance: %" GST_TIME_FORMAT " snapping at %"
      GST_TIME_FORMAT, GST_TIME_ARGS (timeline->priv->snapping_distance),
      GST_TIME_ARGS (snap_time));

  if (timecode == NULL) {
    if (mv_ctx->last_snaped1 != NULL && mv_ctx->last_snaped2 != NULL) {
      g_signal_emit (timeline, ges_timeline_signals[SNAPING_ENDED], 0,
          mv_ctx->last_snaped1, mv_ctx->last_snaped2, last_snap_ts);

      /* We then need to recalculate the moving context */
      timeline->priv->movecontext.needs_move_ctx = TRUE;
    }

    return;
  }

  obj2 = g_hash_table_lookup (timeline->priv->by_object, timecode);

  if (last_snap_ts != *timecode) {
    g_signal_emit (timeline, ges_timeline_signals[SNAPING_ENDED], 0,
        mv_ctx->last_snaped1, mv_ctx->last_snaped2, (last_snap_ts));

    /* We want the snap start signal to be emited anyway */
    mv_ctx->last_snap_ts = NULL;
  }

  if (mv_ctx->last_snap_ts == NULL) {

    mv_ctx->last_snaped1 = obj1;
    mv_ctx->last_snaped2 = obj2;
    mv_ctx->last_snap_ts = timecode;

    g_signal_emit (timeline, ges_timeline_signals[SNAPING_STARTED], 0,
        obj1, obj2, *timecode);

  }
}

static guint64 *
ges_timeline_snap_position (GESTimeline * timeline,
    GESTrackElement * trackelement, guint64 * current, guint64 timecode,
    gboolean emit)
{
  GESTimelinePrivate *priv = timeline->priv;
  GSequenceIter *iter, *prev_iter, *nxt_iter;
  GESTrackElement *tmp_trackelement;
  GESContainer *tmp_container, *container;

  GstClockTime *last_snap_ts = priv->movecontext.last_snap_ts;
  guint64 snap_distance = timeline->priv->snapping_distance;
  guint64 *prev_tc, *next_tc, *ret = NULL, off = G_MAXUINT64, off1 =
      G_MAXUINT64;

  /* Avoid useless calculations */
  if (snap_distance == 0)
    return NULL;

  /* If we can just resnap as last snap... do it */
  if (last_snap_ts) {
    off = timecode > *last_snap_ts ?
        timecode - *last_snap_ts : *last_snap_ts - timecode;
    if (off <= snap_distance) {
      ret = last_snap_ts;
      goto done;
    }
  }

  container = get_toplevel_container (trackelement);

  iter = g_sequence_search (priv->starts_ends, &timecode,
      (GCompareDataFunc) compare_uint64, NULL);

  /* Getting the next/previous  values, and use the closest one if any "respects"
   * the snap_distance value */
  nxt_iter = iter;
  while (!g_sequence_iter_is_end (nxt_iter)) {
    next_tc = g_sequence_get (iter);
    tmp_trackelement = g_hash_table_lookup (timeline->priv->by_object, next_tc);
    tmp_container = get_toplevel_container (tmp_trackelement);

    off = timecode > *next_tc ? timecode - *next_tc : *next_tc - timecode;
    if (next_tc != current && off <= snap_distance
        && container != tmp_container) {

      ret = next_tc;
      break;
    }

    nxt_iter = g_sequence_iter_next (nxt_iter);
  }

  if (ret == NULL)
    off = G_MAXUINT64;

  prev_iter = g_sequence_iter_prev (iter);
  while (!g_sequence_iter_is_begin (prev_iter)) {
    prev_tc = g_sequence_get (prev_iter);
    tmp_trackelement = g_hash_table_lookup (timeline->priv->by_object, prev_tc);
    tmp_container = get_toplevel_container (tmp_trackelement);

    off1 = timecode > *prev_tc ? timecode - *prev_tc : *prev_tc - timecode;
    if (prev_tc != current && off1 < off && off1 <= snap_distance &&
        container != tmp_container) {
      ret = prev_tc;

      break;
    }

    prev_iter = g_sequence_iter_prev (prev_iter);
  }

done:
  /* We emit the snapping signal only if we snapped with a different value
   * than the current one */
  if (emit) {
    GstClockTime snap_time = ret ? *ret : GST_CLOCK_TIME_NONE;

    ges_timeline_emit_snappig (timeline, trackelement, ret);

    GST_DEBUG_OBJECT (timeline, "Snaping at %" GST_TIME_FORMAT,
        GST_TIME_ARGS (snap_time));
  }

  return ret;
}

static inline GESContainer *
add_toplevel_container (MoveContext * mv_ctx, GESTrackElement * trackelement)
{
  guint layer_prio;
  GESContainer *toplevel = get_toplevel_container (trackelement);

  /* Avoid recalculating */
  if (!g_hash_table_lookup (mv_ctx->toplevel_containers, toplevel)) {
    if (GES_IS_CLIP (toplevel)) {

      layer_prio = ges_clip_get_layer_priority (GES_CLIP (toplevel));
      if (layer_prio == (guint32) - 1) {
        GST_WARNING_OBJECT (toplevel, "Not in any layer, can not move"
            " between layers");

        return toplevel;
      }
      mv_ctx->min_move_layer = MIN (mv_ctx->min_move_layer, layer_prio);
      mv_ctx->max_layer_prio = MAX (mv_ctx->max_layer_prio, layer_prio);
    } else if GES_IS_GROUP
      (toplevel) {
      mv_ctx->min_move_layer = MIN (mv_ctx->min_move_layer,
          _PRIORITY (toplevel));
      mv_ctx->max_layer_prio = MAX (mv_ctx->max_layer_prio,
          _PRIORITY (toplevel) + GES_CONTAINER_HEIGHT (toplevel));
    } else
      g_assert_not_reached ();

    g_hash_table_insert (mv_ctx->toplevel_containers, toplevel, toplevel);

  }

  return toplevel;
}

static gboolean
ges_move_context_set_objects (GESTimeline * timeline, GESTrackElement * obj,
    GESEdge edge)
{
  TrackObjIters *iters;
  GESTrackElement *tmptrackelement;
  guint64 start, end, tmpend;
  GSequenceIter *iter, *trackelement_iter;

  MoveContext *mv_ctx = &timeline->priv->movecontext;

  iters = g_hash_table_lookup (timeline->priv->obj_iters, obj);
  trackelement_iter = iters->iter_obj;
  switch (edge) {
    case GES_EDGE_START:
      /* set it properly in the context of "trimming" */
      mv_ctx->max_trim_pos = 0;
      start = _START (obj);

      if (g_sequence_iter_is_begin (trackelement_iter))
        break;

      /* Look for the objects */
      for (iter = g_sequence_iter_prev (trackelement_iter);
          iter && !g_sequence_iter_is_end (iter);
          iter = g_sequence_iter_prev (iter)) {

        tmptrackelement = GES_TRACK_ELEMENT (g_sequence_get (iter));
        tmpend = _START (tmptrackelement) + _DURATION (tmptrackelement);

        if (tmpend <= start) {
          mv_ctx->max_trim_pos =
              MAX (mv_ctx->max_trim_pos, _START (tmptrackelement));
          mv_ctx->moving_trackelements =
              g_list_prepend (mv_ctx->moving_trackelements, tmptrackelement);
        }

        if (g_sequence_iter_is_begin (iter))
          break;
      }
      break;

    case GES_EDGE_END:
    case GES_EDGE_NONE:        /* In this case only works for ripple */
      end = _START (obj) + _DURATION (obj);
      mv_ctx->max_trim_pos = G_MAXUINT64;

      /* Look for folowing objects */
      for (iter = g_sequence_iter_next (trackelement_iter);
          iter && !g_sequence_iter_is_end (iter);
          iter = g_sequence_iter_next (iter)) {
        tmptrackelement = GES_TRACK_ELEMENT (g_sequence_get (iter));

        if (_START (tmptrackelement) >= end) {
          tmpend = _START (tmptrackelement) + _DURATION (tmptrackelement);
          mv_ctx->max_trim_pos = MIN (mv_ctx->max_trim_pos, tmpend);
          mv_ctx->moving_trackelements =
              g_list_prepend (mv_ctx->moving_trackelements, tmptrackelement);
        }
      }
      break;
    default:
      GST_DEBUG ("Edge type %d no supported", edge);
      return FALSE;
  }

  return TRUE;
}

static gboolean
ges_timeline_set_moving_context (GESTimeline * timeline, GESTrackElement * obj,
    GESEditMode mode, GESEdge edge, GList * layers)
{
  /* A TrackElement that could initiate movement for other object */
  GESTrackElement *editor_trackelement = NULL;
  MoveContext *mv_ctx = &timeline->priv->movecontext;
  GESClip *clip = GES_CLIP (GES_TIMELINE_ELEMENT_PARENT (obj));

  /* Still in the same mv_ctx */
  if ((mv_ctx->clip == clip && mv_ctx->mode == mode &&
          mv_ctx->edge == edge && !mv_ctx->needs_move_ctx)) {

    GST_DEBUG ("Keeping the same moving mv_ctx");
    return TRUE;
  }

  GST_DEBUG_OBJECT (clip,
      "Changing context:\nold: obj: %p, mode: %d, edge: %d \n"
      "new: obj: %p, mode: %d, edge: %d ! Has changed %i", mv_ctx->clip,
      mv_ctx->mode, mv_ctx->edge, clip, mode, edge, mv_ctx->needs_move_ctx);

  clean_movecontext (mv_ctx);
  mv_ctx->edge = edge;
  mv_ctx->mode = mode;
  mv_ctx->clip = clip;
  mv_ctx->needs_move_ctx = FALSE;

  /* We try to find a Source inside the Clip so we can set the
   * moving context Else we just move the selected one only */
  if (GES_IS_SOURCE (obj) == FALSE) {
    GList *tmp;

    for (tmp = GES_CONTAINER_CHILDREN (clip); tmp; tmp = tmp->next) {
      if (GES_IS_SOURCE (tmp->data)) {
        editor_trackelement = tmp->data;
        break;
      }
    }
  } else {
    editor_trackelement = obj;
  }

  if (editor_trackelement) {
    switch (mode) {
      case GES_EDIT_MODE_RIPPLE:
      case GES_EDIT_MODE_ROLL:
        if (!(ges_move_context_set_objects (timeline, editor_trackelement,
                    edge)))
          return FALSE;
      default:
        break;
    }
    add_toplevel_container (&timeline->priv->movecontext, editor_trackelement);
  } else {
    /* We add the main object to the toplevel_containers set */
    add_toplevel_container (&timeline->priv->movecontext, obj);
  }


  return TRUE;
}

gboolean
ges_timeline_trim_object_simple (GESTimeline * timeline,
    GESTimelineElement * element, GList * layers, GESEdge edge,
    guint64 position, gboolean snapping)
{
  guint64 start, inpoint, duration, max_duration, *snapped, *cur;
  gboolean ret = TRUE;
  gint64 real_dur;
  GESTrackElement *track_element;

  /* We only work with GESSource-s */
  if (GES_IS_SOURCE (element) == FALSE)
    return FALSE;

  track_element = GES_TRACK_ELEMENT (element);
  GST_DEBUG_OBJECT (track_element, "Trimming to %" GST_TIME_FORMAT
      " %s snaping, edge %i", GST_TIME_ARGS (position),
      snapping ? "Is" : "Not", edge);

  start = _START (track_element);
  g_object_get (track_element, "max-duration", &max_duration, NULL);

  switch (edge) {
    case GES_EDGE_START:
    {
      GESTimelineElement *toplevel;
      GESChildrenControlMode old_mode;
      toplevel = ges_timeline_element_get_toplevel_parent (element);

      if (position < _START (toplevel) && _START (toplevel) < _START (element)) {
        GST_DEBUG_OBJECT (toplevel, "Not trimming %p as not at begining "
            "of the container", element);

        gst_object_unref (toplevel);
        return FALSE;
      }

      old_mode = GES_CONTAINER (toplevel)->children_control_mode;
      if (GES_IS_GROUP (toplevel) && old_mode == GES_CHILDREN_UPDATE) {
        GST_DEBUG_OBJECT (toplevel, "Setting children udpate mode to"
            " UPDDATE_ALL_VALUES so we can trim without moving the contained");
        /* The container will update its values itself according to new
         * values of the children */
        GES_CONTAINER (toplevel)->children_control_mode =
            GES_CHILDREN_UPDATE_ALL_VALUES;
      }

      inpoint = _INPOINT (track_element);
      duration = _DURATION (track_element);

      if (snapping) {
        cur = g_hash_table_lookup (timeline->priv->by_start, track_element);

        snapped = ges_timeline_snap_position (timeline, track_element, cur,
            position, TRUE);
        if (snapped)
          position = *snapped;
      }

      /* Calculate new values */
      position = MIN (position, start + duration);
      inpoint = inpoint + position > start ? inpoint + position - start : 0;

      real_dur = _END (element) - position;
      duration = CLAMP (real_dur, 0, max_duration > inpoint ?
          max_duration - inpoint : G_MAXUINT64);


      /* If we already are at max duration or duration == 0 do no useless work */
      if ((duration == _DURATION (track_element) &&
              _DURATION (track_element) == _MAXDURATION (track_element)) ||
          (duration == 0 && _DURATION (element) == 0)) {
        GST_DEBUG_OBJECT (track_element,
            "Duration already == max_duration, no triming");
        gst_object_unref (toplevel);
        return FALSE;
      }

      timeline->priv->needs_transitions_update = FALSE;
      _set_start0 (GES_TIMELINE_ELEMENT (track_element), position);
      _set_inpoint0 (GES_TIMELINE_ELEMENT (track_element), inpoint);
      timeline->priv->needs_transitions_update = TRUE;

      _set_duration0 (GES_TIMELINE_ELEMENT (track_element), duration);
      if (GES_IS_GROUP (toplevel))
        GES_CONTAINER (toplevel)->children_control_mode = old_mode;

      gst_object_unref (toplevel);
      break;
    }
    case GES_EDGE_END:
    {
      cur = g_hash_table_lookup (timeline->priv->by_end, track_element);
      snapped = ges_timeline_snap_position (timeline, track_element, cur,
          position, TRUE);
      if (snapped)
        position = *snapped;

      /* Calculate new values */
      real_dur = position - start;
      duration = MAX (0, real_dur);
      duration = MIN (duration, max_duration - _INPOINT (track_element));

      /* Not moving, avoid overhead */
      if (duration == _DURATION (track_element)) {
        GST_DEBUG_OBJECT (track_element, "No change in duration");
        return FALSE;
      }

      _set_duration0 (GES_TIMELINE_ELEMENT (track_element), duration);
      break;
    }
    default:
      GST_WARNING ("Can not trim with %i GESEdge", edge);
      return FALSE;
  }

  return ret;
}

gboolean
timeline_ripple_object (GESTimeline * timeline, GESTrackElement * obj,
    GList * layers, GESEdge edge, guint64 position)
{
  GList *tmp, *moved_clips = NULL;
  GESTrackElement *trackelement;
  GESContainer *container;
  guint64 duration, new_start, *snapped, *cur;
  gint64 offset;

  MoveContext *mv_ctx = &timeline->priv->movecontext;

  mv_ctx->ignore_needs_ctx = TRUE;

  if (!ges_timeline_set_moving_context (timeline, obj, GES_EDIT_MODE_RIPPLE,
          edge, layers))
    goto error;

  switch (edge) {
    case GES_EDGE_NONE:
      GST_DEBUG ("Simply rippling");

      /* We should be smart here to avoid recalculate transitions when possible */
      cur = g_hash_table_lookup (timeline->priv->by_end, obj);
      snapped = ges_timeline_snap_position (timeline, obj, cur, position, TRUE);
      if (snapped)
        position = *snapped;

      offset = position - _START (obj);

      for (tmp = mv_ctx->moving_trackelements; tmp; tmp = tmp->next) {
        trackelement = GES_TRACK_ELEMENT (tmp->data);
        new_start = _START (trackelement) + offset;

        container = add_toplevel_container (mv_ctx, trackelement);
        /* Make sure not to move 2 times the same Clip */
        if (g_list_find (moved_clips, container) == NULL) {
          _set_start0 (GES_TIMELINE_ELEMENT (trackelement), new_start);
          moved_clips = g_list_prepend (moved_clips, container);
        }

      }
      g_list_free (moved_clips);
      _set_start0 (GES_TIMELINE_ELEMENT (obj), position);

      break;
    case GES_EDGE_END:
      timeline->priv->needs_transitions_update = FALSE;
      GST_DEBUG ("Rippling end");

      cur = g_hash_table_lookup (timeline->priv->by_end, obj);
      snapped = ges_timeline_snap_position (timeline, obj, cur, position, TRUE);
      if (snapped)
        position = *snapped;

      duration = _DURATION (obj);

      if (!ges_timeline_trim_object_simple (timeline,
              GES_TIMELINE_ELEMENT (obj), NULL, GES_EDGE_END, position,
              FALSE)) {
        return FALSE;
      }

      offset = _DURATION (obj) - duration;
      for (tmp = mv_ctx->moving_trackelements; tmp; tmp = tmp->next) {
        trackelement = GES_TRACK_ELEMENT (tmp->data);
        new_start = _START (trackelement) + offset;

        container = add_toplevel_container (mv_ctx, trackelement);
        if (GES_IS_GROUP (container))
          container->children_control_mode = GES_CHILDREN_UPDATE_OFFSETS;
        /* Make sure not to move 2 times the same Clip */
        if (g_list_find (moved_clips, container) == NULL) {
          _set_start0 (GES_TIMELINE_ELEMENT (trackelement), new_start);
          moved_clips = g_list_prepend (moved_clips, container);
        }
        if (GES_IS_GROUP (container))
          container->children_control_mode = GES_CHILDREN_UPDATE;

      }

      g_list_free (moved_clips);
      timeline->priv->needs_transitions_update = TRUE;
      GST_DEBUG ("Done Rippling end");
      break;
    case GES_EDGE_START:
      GST_INFO ("Ripple start doesn't make sense, trimming instead");
      timeline->priv->movecontext.needs_move_ctx = TRUE;
      timeline_trim_object (timeline, obj, layers, edge, position);
      break;
    default:
      GST_DEBUG ("Can not ripple edge: %i", edge);

      break;
  }

  mv_ctx->ignore_needs_ctx = FALSE;

  return TRUE;

error:
  mv_ctx->ignore_needs_ctx = FALSE;

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

gboolean
timeline_trim_object (GESTimeline * timeline, GESTrackElement * object,
    GList * layers, GESEdge edge, guint64 position)
{
  gboolean ret = FALSE;
  MoveContext *mv_ctx = &timeline->priv->movecontext;

  mv_ctx->ignore_needs_ctx = TRUE;

  if (!ges_timeline_set_moving_context (timeline, object, GES_EDIT_MODE_TRIM,
          edge, layers))
    goto end;

  ret = ges_timeline_trim_object_simple (timeline,
      GES_TIMELINE_ELEMENT (object), layers, edge, position, TRUE);

end:
  mv_ctx->ignore_needs_ctx = FALSE;

  return ret;
}

gboolean
timeline_roll_object (GESTimeline * timeline, GESTrackElement * obj,
    GList * layers, GESEdge edge, guint64 position)
{
  MoveContext *mv_ctx = &timeline->priv->movecontext;
  guint64 start, duration, end, tmpstart, tmpduration, tmpend, *snapped, *cur;
  gboolean ret = TRUE;
  GList *tmp;

  mv_ctx->ignore_needs_ctx = TRUE;

  GST_DEBUG_OBJECT (obj, "Rolling object to %" GST_TIME_FORMAT,
      GST_TIME_ARGS (position));

  if (!ges_timeline_set_moving_context (timeline, obj, GES_EDIT_MODE_ROLL,
          edge, layers))
    goto error;

  start = _START (obj);
  duration = _DURATION (obj);
  end = start + duration;

  timeline->priv->needs_transitions_update = FALSE;
  switch (edge) {
    case GES_EDGE_START:

      /* Avoid negative durations */
      if (position < mv_ctx->max_trim_pos || position > end)
        goto error;

      cur = g_hash_table_lookup (timeline->priv->by_start, obj);
      snapped = ges_timeline_snap_position (timeline, obj, cur, position, TRUE);
      if (snapped)
        position = *snapped;

      ret &= ges_timeline_trim_object_simple (timeline,
          GES_TIMELINE_ELEMENT (obj), layers, GES_EDGE_START, position, FALSE);

      /* In the case we reached max_duration we just make sure to roll
       * everything to the real new position */
      position = _START (obj);

      /* Send back changes to the neighbourhood */
      for (tmp = mv_ctx->moving_trackelements; tmp; tmp = tmp->next) {
        GESTimelineElement *tmpelement = GES_TIMELINE_ELEMENT (tmp->data);

        tmpstart = _START (tmpelement);
        tmpduration = _DURATION (tmpelement);
        tmpend = tmpstart + tmpduration;

        /* Check that the object should be resized at this position
         * even if an error accurs, we keep doing our job */
        if (tmpend == start) {
          ret &= ges_timeline_trim_object_simple (timeline, tmpelement, NULL,
              GES_EDGE_END, position, FALSE);
          break;
        }
      }
      break;
    case GES_EDGE_END:

      /* Avoid negative durations */
      if (position > mv_ctx->max_trim_pos || position < start)
        goto error;

      end = _START (obj) + _DURATION (obj);

      cur = g_hash_table_lookup (timeline->priv->by_end, obj);
      snapped = ges_timeline_snap_position (timeline, obj, cur, position, TRUE);
      if (snapped)
        position = *snapped;

      ret &= ges_timeline_trim_object_simple (timeline,
          GES_TIMELINE_ELEMENT (obj), NULL, GES_EDGE_END, position, FALSE);

      if (ret == FALSE) {
        GST_DEBUG_OBJECT (timeline, "No triming, bailing out");
        goto done;
      }

      /* In the case we reached max_duration we just make sure to roll
       * everything to the real new position */
      position = _START (obj) + _DURATION (obj);

      /* Send back changes to the neighbourhood */
      for (tmp = mv_ctx->moving_trackelements; tmp; tmp = tmp->next) {
        GESTimelineElement *tmpelement = GES_TIMELINE_ELEMENT (tmp->data);

        tmpstart = _START (tmpelement);
        tmpduration = _DURATION (tmpelement);
        tmpend = tmpstart + tmpduration;

        /* Check that the object should be resized at this position
         * even if an error accure, we keep doing our job */
        if (end == tmpstart) {
          ret &= ges_timeline_trim_object_simple (timeline, tmpelement, NULL,
              GES_EDGE_START, position, FALSE);
        }
      }
      break;
    default:
      GST_DEBUG ("Edge type %i not handled here", edge);
      break;
  }

done:
  timeline->priv->needs_transitions_update = TRUE;
  mv_ctx->ignore_needs_ctx = FALSE;

  return ret;

error:
  GST_DEBUG_OBJECT (obj, "Could not roll edge %d to %" GST_TIME_FORMAT,
      edge, GST_TIME_ARGS (position));

  ret = FALSE;
  goto done;
}

gboolean
timeline_move_object (GESTimeline * timeline, GESTrackElement * object,
    GList * layers, GESEdge edge, guint64 position)
{
  if (!ges_timeline_set_moving_context (timeline, object, GES_EDIT_MODE_NORMAL,
          edge, layers)) {
    GST_DEBUG_OBJECT (object, "Could not move to %" GST_TIME_FORMAT,
        GST_TIME_ARGS (position));

    return FALSE;
  }

  return ges_timeline_move_object_simple (timeline,
      GES_TIMELINE_ELEMENT (object), layers, edge, position);
}

gboolean
ges_timeline_move_object_simple (GESTimeline * timeline,
    GESTimelineElement * element, GList * layers, GESEdge edge,
    guint64 position)
{
  guint64 *snap_end, *snap_st, *cur, off1, off2, end;
  GESTrackElement *track_element;

  /* We only work with GESSource-s and we check that we are not already moving
   * element ourself*/
  if (GES_IS_SOURCE (element) == FALSE ||
      g_list_find (timeline->priv->movecontext.moving_trackelements, element))
    return FALSE;

  track_element = GES_TRACK_ELEMENT (element);
  end = position + _DURATION (get_toplevel_container (track_element));
  cur = g_hash_table_lookup (timeline->priv->by_end, track_element);

  GST_DEBUG_OBJECT (timeline, "Moving %" GST_PTR_FORMAT "to %"
      GST_TIME_FORMAT " (end %" GST_TIME_FORMAT ")", element,
      GST_TIME_ARGS (position), GST_TIME_ARGS (end));

  snap_end = ges_timeline_snap_position (timeline, track_element, cur, end,
      FALSE);
  if (snap_end)
    off1 = end > *snap_end ? end - *snap_end : *snap_end - end;
  else
    off1 = G_MAXUINT64;

  cur = g_hash_table_lookup (timeline->priv->by_start, track_element);
  snap_st =
      ges_timeline_snap_position (timeline, track_element, cur, position,
      FALSE);
  if (snap_st)
    off2 = position > *snap_st ? position - *snap_st : *snap_st - position;
  else
    off2 = G_MAXUINT64;

  /* In the case we could snap on both sides, we snap on the end */
  if (snap_end && off1 <= off2) {
    position = position + *snap_end - end;
    ges_timeline_emit_snappig (timeline, track_element, snap_end);
  } else if (snap_st) {
    position = position + *snap_st - position;
    ges_timeline_emit_snappig (timeline, track_element, snap_st);
  } else
    ges_timeline_emit_snappig (timeline, track_element, NULL);


  _set_start0 (GES_TIMELINE_ELEMENT (track_element), position);

  return TRUE;
}

gboolean
timeline_context_to_layer (GESTimeline * timeline, gint offset)
{
  gboolean ret = TRUE;
  MoveContext *mv_ctx = &timeline->priv->movecontext;



  /* Layer's priority is always positive */
  if (offset != 0 && (offset > 0 || mv_ctx->min_move_layer >= -offset)) {
    GHashTableIter iter;
    GESContainer *key, *value;
    GESLayer *new_layer;
    guint prio;

    mv_ctx->ignore_needs_ctx = TRUE;

    GST_DEBUG ("Moving %d object, offset %d",
        g_hash_table_size (mv_ctx->toplevel_containers), offset);

    g_hash_table_iter_init (&iter, mv_ctx->toplevel_containers);
    while (g_hash_table_iter_next (&iter, (gpointer *) & key,
            (gpointer *) & value)) {

      if (GES_IS_CLIP (value)) {
        prio = ges_clip_get_layer_priority (GES_CLIP (value));

        /* We know that the layer exists as we created it */
        new_layer =
            GES_LAYER (g_list_nth_data (timeline->layers, prio + offset));

        if (new_layer == NULL) {
          do {
            new_layer = ges_timeline_append_layer (timeline);
          } while (ges_layer_get_priority (new_layer) < prio + offset);
        }

        ret &= ges_clip_move_to_layer (GES_CLIP (key), new_layer);
      } else if (GES_IS_GROUP (value)) {
        guint32 last_prio = _PRIORITY (value) + offset +
            GES_CONTAINER_HEIGHT (value) - 1;

        new_layer = GES_LAYER (g_list_nth_data (timeline->layers, last_prio));

        if (new_layer == NULL) {
          do {
            new_layer = ges_timeline_append_layer (timeline);
          } while (ges_layer_get_priority (new_layer) < last_prio);
        }

        _set_priority0 (GES_TIMELINE_ELEMENT (value),
            _PRIORITY (value) + offset);
      }
    }

    /* Readjust min_move_layer */
    mv_ctx->min_move_layer = mv_ctx->min_move_layer + offset;

    mv_ctx->ignore_needs_ctx = FALSE;
  }

  return ret;
}

void
timeline_add_group (GESTimeline * timeline, GESGroup * group)
{
  GST_DEBUG_OBJECT (timeline, "Adding group %" GST_PTR_FORMAT, group);

  timeline->priv->movecontext.needs_move_ctx = TRUE;
  timeline->priv->groups = g_list_prepend (timeline->priv->groups,
      gst_object_ref_sink (group));

  ges_timeline_element_set_timeline (GES_TIMELINE_ELEMENT (group), timeline);
}

void
timeline_remove_group (GESTimeline * timeline, GESGroup * group)
{
  GST_DEBUG_OBJECT (timeline, "Removing group %" GST_PTR_FORMAT, group);

  timeline->priv->groups = g_list_remove (timeline->priv->groups, group);

  timeline->priv->movecontext.needs_move_ctx = TRUE;
  ges_timeline_element_set_timeline (GES_TIMELINE_ELEMENT (group), NULL);
  gst_object_unref (group);
}

GList *
timeline_get_groups (GESTimeline * timeline)
{
  return timeline->priv->groups;
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
  _create_transitions_on_layer (timeline, layer, NULL, NULL,
      _create_auto_transition_from_transitions);

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
        " from clip",
        track_element, ges_track_type_name (ges_track_element_get_track_type
            (track_element)));

    if (tracks)
      g_ptr_array_unref (tracks);

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
        " of type %s, removing new one.", track,
        G_OBJECT_TYPE_NAME (track_element));
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
          " of type %s, removing new one.", track,
          G_OBJECT_TYPE_NAME (track_element));
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
    timeline->priv->movecontext.needs_move_ctx = TRUE;
    _create_transitions_on_layer (timeline, layer, NULL, NULL,
        _find_transition_from_auto_transitions);
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

    GST_DEBUG_OBJECT (timeline, "Trying to remove TrackElement %p",
        track_element);

    /* FIXME Check if we should actually check that we control the
     * track in the new management of TrackElement context */
    LOCK_DYN (timeline);
    if (G_LIKELY (g_list_find_custom (timeline->priv->priv_tracks, track,
                (GCompareFunc) custom_find_track) || track == NULL)) {
      GST_DEBUG ("Belongs to one of the tracks we control");

      ges_container_remove (GES_CONTAINER (clip),
          GES_TIMELINE_ELEMENT (track_element));
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
  GESTimelinePrivate *priv = timeline->priv;
  TrackObjIters *iters = g_hash_table_lookup (priv->obj_iters, child);

  if (G_LIKELY (iters->iter_by_layer))
    g_sequence_sort_changed (iters->iter_by_layer,
        (GCompareDataFunc) element_start_compare, NULL);

  if (GES_IS_SOURCE (child)) {
    sort_track_elements (timeline, iters);
    sort_starts_ends_start (timeline, iters);
    sort_starts_ends_end (timeline, iters);

    /* If the timeline is set to snap objects together, we
     * are sure that all movement of TrackElement-s are done within
     * the moving context, so we do not need to recalculate the
     * move context as often */
    if (timeline->priv->movecontext.ignore_needs_ctx &&
        timeline->priv->snapping_distance == 0)
      timeline->priv->movecontext.needs_move_ctx = TRUE;

    create_transitions (timeline, child);
  }
}

static void
trackelement_priority_changed_cb (GESTrackElement * child,
    GParamSpec * arg G_GNUC_UNUSED, GESTimeline * timeline)
{
  GESTimelinePrivate *priv = timeline->priv;

  GList *layer_node = g_list_find_custom (timeline->layers,
      GINT_TO_POINTER (_ges_track_element_get_layer_priority (child)),
      (GCompareFunc) find_layer_by_prio);
  GESLayer *layer = layer_node ? layer_node->data : NULL;
  TrackObjIters *iters = g_hash_table_lookup (priv->obj_iters,
      child);

  if (G_UNLIKELY (layer == NULL)) {
    GST_ERROR_OBJECT (timeline,
        "Changing a TrackElement prio, which would not "
        "land in no layer we are controlling");
    if (iters->iter_by_layer)
      g_sequence_remove (iters->iter_by_layer);
    iters->iter_by_layer = NULL;
    iters->layer = NULL;
  } else {
    /* If it moves from layer, properly change it */
    if (layer != iters->layer) {
      GSequence *by_layer_sequence =
          g_hash_table_lookup (priv->by_layer, layer);

      GST_DEBUG_OBJECT (child, "Moved from layer %" GST_PTR_FORMAT
          "(prio %d) to" " %" GST_PTR_FORMAT " (prio %d)", layer,
          ges_layer_get_priority (layer), iters->layer,
          ges_layer_get_priority (iters->layer));

      g_sequence_remove (iters->iter_by_layer);
      iters->iter_by_layer =
          g_sequence_insert_sorted (by_layer_sequence, child,
          (GCompareDataFunc) element_start_compare, NULL);
      iters->layer = layer;
    } else {
      g_sequence_sort_changed (iters->iter_by_layer,
          (GCompareDataFunc) element_start_compare, NULL);
    }
  }

  if (GES_IS_SOURCE (child))
    sort_track_elements (timeline, iters);
}

static void
trackelement_duration_changed_cb (GESTrackElement * child,
    GParamSpec * arg G_GNUC_UNUSED, GESTimeline * timeline)
{
  GESTimelinePrivate *priv = timeline->priv;
  TrackObjIters *iters = g_hash_table_lookup (priv->obj_iters, child);

  if (GES_IS_SOURCE (child)) {
    sort_starts_ends_end (timeline, iters);

    /* If the timeline is set to snap objects together, we
     * are sure that all movement of TrackElement-s are done within
     * the moving context, so we do not need to recalculate the
     * move context as often */
    if (timeline->priv->movecontext.ignore_needs_ctx &&
        timeline->priv->snapping_distance == 0) {
      timeline->priv->movecontext.needs_move_ctx = TRUE;
    }

    create_transitions (timeline, child);
  }
}

static void
track_element_added_cb (GESTrack * track, GESTrackElement * track_element,
    GESTimeline * timeline)
{
  /* Auto transition should be updated before we receive the signal */
  g_signal_connect_after (GES_TRACK_ELEMENT (track_element), "notify::start",
      G_CALLBACK (trackelement_start_changed_cb), timeline);
  g_signal_connect_after (GES_TRACK_ELEMENT (track_element),
      "notify::duration", G_CALLBACK (trackelement_duration_changed_cb),
      timeline);
  g_signal_connect_after (GES_TRACK_ELEMENT (track_element),
      "notify::priority", G_CALLBACK (trackelement_priority_changed_cb),
      timeline);

  start_tracking_track_element (timeline, track_element);
}

static void
track_element_removed_cb (GESTrack * track,
    GESTrackElement * track_element, GESTimeline * timeline)
{

  if (GES_IS_SOURCE (track_element)) {
    /* Make sure to reinitialise the moving context next time */
    timeline->priv->movecontext.needs_move_ctx = TRUE;
  }

  /* Disconnect all signal handlers */
  g_signal_handlers_disconnect_by_func (track_element,
      trackelement_start_changed_cb, timeline);
  g_signal_handlers_disconnect_by_func (track_element,
      trackelement_duration_changed_cb, timeline);
  g_signal_handlers_disconnect_by_func (track_element,
      trackelement_priority_changed_cb, timeline);

  stop_tracking_track_element (timeline, track_element);
}

static GstPadProbeReturn
_pad_probe_cb (GstPad * mixer_pad, GstPadProbeInfo * info,
    GESTimeline * timeline)
{
  GstEvent *event = GST_PAD_PROBE_INFO_EVENT (info);
  if (GST_EVENT_TYPE (event) == GST_EVENT_STREAM_START) {
    LOCK_DYN (timeline);
    if (timeline->priv->group_id == -1) {
      if (!gst_event_parse_group_id (event, &timeline->priv->group_id))
        timeline->priv->group_id = gst_util_group_id_next ();
    }

    info->data = gst_event_make_writable (event);
    gst_event_set_group_id (GST_PAD_PROBE_INFO_EVENT (info),
        timeline->priv->group_id);
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

  return TRUE;
}

gboolean
timeline_remove_element (GESTimeline * timeline, GESTimelineElement * element)
{
  return g_hash_table_remove (timeline->priv->all_elements, element->name);
}

void
timeline_fill_gaps (GESTimeline * timeline)
{
  GList *tmp;

  for (tmp = timeline->tracks; tmp; tmp = tmp->next) {
    track_resort_and_fill_gaps (tmp->data);
  }
}

/**** API *****/
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
 * Returns: A new timeline if the uri was loaded successfully, or NULL if the
 * uri could not be loaded
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
 * Returns: TRUE if the timeline was loaded successfully, or FALSE if the uri
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
 * Returns: TRUE if the timeline was successfully saved to the given location,
 * else FALSE.
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

  layer = ges_layer_new ();
  priority = g_list_length (timeline->layers);
  ges_layer_set_priority (layer, priority);

  ges_timeline_add_layer (timeline, layer);

  return layer;
}

/**
 * ges_timeline_add_layer:
 * @timeline: a #GESTimeline
 * @layer: the #GESLayer to add
 *
 * Add the layer to the timeline. The reference to the @layer will be stolen
 * by the @timeline.
 *
 * Returns: TRUE if the layer was properly added, else FALSE.
 */
gboolean
ges_timeline_add_layer (GESTimeline * timeline, GESLayer * layer)
{
  gboolean auto_transition;
  GList *objects, *tmp;

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

  g_hash_table_insert (timeline->priv->by_layer, layer, g_sequence_new (NULL));

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

  timeline->priv->movecontext.needs_move_ctx = TRUE;

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
 * Returns: TRUE if the layer was properly removed, else FALSE.
 */

gboolean
ges_timeline_remove_layer (GESTimeline * timeline, GESLayer * layer)
{
  GList *layer_objects, *tmp;

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

  g_hash_table_remove (timeline->priv->by_layer, layer);
  timeline->layers = g_list_remove (timeline->layers, layer);
  ges_layer_set_timeline (layer, NULL);

  g_signal_emit (timeline, ges_timeline_signals[LAYER_REMOVED], 0, layer);

  gst_object_unref (layer);
  timeline->priv->movecontext.needs_move_ctx = TRUE;

  return TRUE;
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

/* FIXME: create track elements for clips which have already been
 * added to existing layers.
 */

gboolean
ges_timeline_add_track (GESTimeline * timeline, GESTrack * track)
{
  TrackPrivate *tr_priv;
  GList *tmp;

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
 * Returns: TRUE if the @track was properly removed, else FALSE.
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
 * Returns: (transfer none): The corresponding #GESTrack if it is found,
 * or %NULL if there is an error.
 */

GESTrack *
ges_timeline_get_track_for_pad (GESTimeline * timeline, GstPad * pad)
{
  GList *tmp;

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
 * Returns: (transfer none): The corresponding #GstPad if it is found,
 * or %NULL if there is an error.
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

  for (tmp = timeline->layers; tmp; tmp = g_list_next (tmp)) {
    res = g_list_insert_sorted (res, gst_object_ref (tmp->data),
        (GCompareFunc) sort_layers);
  }

  return res;
}

/**
 * ges_timeline_commit:
 * @timeline: a #GESTimeline
 *
 * Commits all the pending changes of the clips contained in the
 * @timeline.
 *
 * When timing changes happen in a timeline, the changes are not
 * directly done inside NLE. This method needs to be called so any changes
 * on a clip contained in the timeline actually happen at the media
 * processing level.
 *
 * Returns: %TRUE if something as been commited %FALSE if nothing needed
 * to be commited
 */
gboolean
ges_timeline_commit (GESTimeline * timeline)
{
  GList *tmp;
  gboolean res = TRUE;

  GST_DEBUG_OBJECT (timeline, "commiting changes");

  for (tmp = timeline->layers; tmp; tmp = tmp->next) {
    _create_transitions_on_layer (timeline, GES_LAYER (tmp->data),
        NULL, NULL, _find_transition_from_auto_transitions);
  }

  for (tmp = timeline->tracks; tmp; tmp = tmp->next) {
    if (!ges_track_commit (GES_TRACK (tmp->data)))
      res = FALSE;
  }

  /* Make sure we reset the context */
  timeline->priv->movecontext.needs_move_ctx = TRUE;

  if (res)
    g_signal_emit (timeline, ges_timeline_signals[COMMITED], 0);

  return res;
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
  g_return_val_if_fail (GES_IS_TIMELINE (timeline), 0);

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

  timeline->priv->snapping_distance = snapping_distance;
}

/**
 * ges_timeline_get_element:
 * @timeline: a #GESTimeline
 *
 * Gets a #GESTimelineElement contained in the timeline
 *
 * Returns: (transfer full): The #GESTimelineElement or %NULL if
 * not found.
 */
GESTimelineElement *
ges_timeline_get_element (GESTimeline * timeline, const gchar * name)
{
  GESTimelineElement *ret;

  g_return_val_if_fail (GES_IS_TIMELINE (timeline), NULL);

  ret = g_hash_table_lookup (timeline->priv->all_elements, name);

  if (ret)
    return gst_object_ref (ret);

  return NULL;
}

/**
 * ges_timeline_is_empty:
 * @timeline: a #GESTimeline
 *
 * Check whether a #GESTimelineElement is empty or not
 *
 * Returns: %TRUE if the timeline is empty %FALSE otherwize
 */
gboolean
ges_timeline_is_empty (GESTimeline * timeline)
{
  GHashTableIter iter;
  gpointer key, value;

  g_return_val_if_fail (GES_IS_TIMELINE (timeline), FALSE);

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
 * Returns: A #GESLayer or %NULL if no layer with @priority was found
 *
 * Since 1.6
 */
GESLayer *
ges_timeline_get_layer (GESTimeline * timeline, guint priority)
{
  GList *tmp;
  GESLayer *layer = NULL;

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
