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

typedef struct _MoveContext MoveContext;

static inline void init_movecontext (MoveContext * mv_ctx);

G_DEFINE_TYPE (GESTimeline, ges_timeline, GST_TYPE_BIN);

#define GES_TIMELINE_PENDINGOBJS_GET_LOCK(timeline) \
  (GES_TIMELINE(timeline)->priv->pendingobjects_lock)
#define GES_TIMELINE_PENDINGOBJS_LOCK(timeline) \
  (g_mutex_lock(GES_TIMELINE_PENDINGOBJS_GET_LOCK (timeline)))
#define GES_TIMELINE_PENDINGOBJS_UNLOCK(timeline) \
  (g_mutex_unlock(GES_TIMELINE_PENDINGOBJS_GET_LOCK (timeline)))

/**
 *  The move context is used for the timeline editing modes functions in order to
 *  + Ripple / Roll /  Slide / Move / Trim
 *
 * The context aims at avoiding to recalculate values/objects on each call of the
 * editing functions.
 */
struct _MoveContext
{
  GESTimelineObject *obj;
  GESEdge edge;
  GESEditMode mode;

  /* Ripple and Roll Objects */
  GList *moving_tckobjs;

  /* We use it as a set of TimelineObject to move between layers */
  GHashTable *moving_tlobjs;
  /* Min priority of the objects currently in moving_tlobjs */
  guint min_move_layer;
  /* Max priority of the objects currently in moving_tlobjs */
  guint max_layer_prio;

  /* Never trim so duration would becomes < 0 */
  guint64 max_trim_pos;

  /* fields to force/avoid new context */
  /* Set to %TRUE when the track is doing updates of track objects
   * properties so we don't end up always needing new move context */
  gboolean ignore_needs_ctx;
  gboolean needs_move_ctx;

  /* Last snapping  properties */
  GESTrackObject *last_snaped1;
  GESTrackObject *last_snaped2;
  GstClockTime last_snap_ts;
};

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

  /* Timeline edition modes and snapping management */
  guint64 snapping_distance;

  /* FIXME: Should we offer an API over those fields ?
   * FIXME: Should other classes than subclasses of TrackSource also
   * be tracked? */

  /* Snapping fields */
  GHashTable *by_start;         /* {TrackSource: start} */
  GHashTable *by_end;           /* {TrackSource: end} */
  GHashTable *by_object;        /* {timecode: TrackSource} */
  GSequence *starts_ends;       /* Sorted list of starts/ends */
  /* We keep 1 reference to our trackobject here */
  GSequence *tracksources;      /* TrackSource-s sorted by start/priorities */

  MoveContext movecontext;
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
  PROP_SNAPPING_DISTANCE,
  PROP_LAST
};

static GParamSpec *properties[PROP_LAST];

enum
{
  TRACK_ADDED,
  TRACK_REMOVED,
  LAYER_ADDED,
  LAYER_REMOVED,
  DISCOVERY_ERROR,
  SNAPING_STARTED,
  SNAPING_ENDED,
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

/* GObject Standard vmethods*/
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
    case PROP_SNAPPING_DISTANCE:
      g_value_set_uint64 (value, timeline->priv->snapping_distance);
      break;
  }
}

static void
ges_timeline_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GESTimeline *timeline = GES_TIMELINE (object);

  switch (property_id) {
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

  g_hash_table_unref (priv->by_start);
  g_hash_table_unref (priv->by_end);
  g_hash_table_unref (priv->by_object);
  g_sequence_free (priv->starts_ends);
  g_sequence_free (priv->tracksources);

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
   * GESTimeline:duration
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
   * GESTimeline:snapping-distance
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

  /**
   * GESTimeline::discovery-error:
   * @timeline: the #GESTimeline
   * @formatter: the #GESFormatter
   * @source: The #GESTimelineFileSource that could not be discovered properly
   * @error: (type GLib.Error): #GError, which will be non-NULL if an error
   *                            occurred during discovery
   */
  ges_timeline_signals[DISCOVERY_ERROR] =
      g_signal_new ("discovery-error", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_FIRST, 0, NULL, NULL, gst_marshal_VOID__OBJECT_BOXED,
      G_TYPE_NONE, 2, GES_TYPE_TIMELINE_FILE_SOURCE, GST_TYPE_G_ERROR);

  /**
   * GESTimeline::track-objects-snapping:
   * @timeline: the #GESTimeline
   * @obj1: the first #GESTrackObject that was snapping.
   * @obj2: the second #GESTrackObject that was snapping.
   * @position: the position where the two objects finally snapping.
   *
   * Will be emitted when the 2 #GESTrackObject first snapped
   *
   * Since: 0.10.XX
   */
  ges_timeline_signals[SNAPING_STARTED] =
      g_signal_new ("snapping-started", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
      G_TYPE_NONE, 3, GES_TYPE_TRACK_OBJECT, GES_TYPE_TRACK_OBJECT,
      G_TYPE_UINT64);

  /**
   * GESTimeline::snapping-end:
   * @timeline: the #GESTimeline
   * @obj1: the first #GESTrackObject that was snapping.
   * @obj2: the second #GESTrackObject that was snapping.
   * @position: the position where the two objects finally snapping.
   *
   * Will be emitted when the 2 #GESTrackObject ended to snap
   *
   * Since: 0.10.XX
   */
  ges_timeline_signals[SNAPING_ENDED] =
      g_signal_new ("snapping-ended", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
      G_TYPE_NONE, 3, GES_TYPE_TRACK_OBJECT, GES_TYPE_TRACK_OBJECT,
      G_TYPE_UINT64);
}

static void
ges_timeline_init (GESTimeline * self)
{
  GESTimelinePrivate *priv;

  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GES_TYPE_TIMELINE, GESTimelinePrivate);

  priv = self->priv;
  priv->layers = NULL;
  priv->tracks = NULL;
  priv->duration = 0;
  priv->snapping_distance = 0;

  /* Move context initialization */
  init_movecontext (&self->priv->movecontext);
  priv->movecontext.ignore_needs_ctx = FALSE;

  priv->by_start = g_hash_table_new (g_direct_hash, g_direct_equal);
  priv->by_end = g_hash_table_new (g_direct_hash, g_direct_equal);
  priv->by_object = g_hash_table_new (g_direct_hash, g_direct_equal);
  priv->starts_ends = g_sequence_new (g_free);
  priv->tracksources = g_sequence_new (g_object_unref);

  priv->pendingobjects_lock = g_mutex_new ();
  /* New discoverer with a 15s timeout */
  priv->discoverer = gst_discoverer_new (15 * GST_SECOND, NULL);
  g_signal_connect (priv->discoverer, "finished",
      G_CALLBACK (discoverer_finished_cb), self);
  g_signal_connect (priv->discoverer, "discovered",
      G_CALLBACK (discoverer_discovered_cb), self);
  gst_discoverer_start (priv->discoverer);
}

/* Private methods */

/* Sorting utils*/
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

static gint
objects_start_compare (GESTrackObject * a, GESTrackObject * b)
{
  if (a->start == b->start) {
    if (a->priority < b->priority)
      return -1;
    if (a->priority > b->priority)
      return 1;
    return 0;
  }
  if (a->start < b->start)
    return -1;
  if (a->start > b->start)
    return 1;
  return 0;
}

static inline void
sort_track_objects (GESTimeline * timeline)
{
  g_sequence_sort (timeline->priv->tracksources,
      (GCompareDataFunc) objects_start_compare, NULL);
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

/* Look for the pointer passed as @value */
static GSequenceIter *
lookup_pointer_uint (GSequence * seq, guint64 * value)
{
  GSequenceIter *iter, *tmpiter;
  guint64 *found, *tmpval;

  iter = g_sequence_lookup (seq, value,
      (GCompareDataFunc) compare_uint64, NULL);

  found = g_sequence_get (iter);

  /* We have the same pointer so we are all fine */
  if (found == value)
    return iter;

  if (!g_sequence_iter_is_end (iter)) {
    /* Looking frontward for the good pointer */
    tmpiter = iter;
    while ((tmpiter = g_sequence_iter_next (tmpiter))) {
      if (g_sequence_iter_is_end (tmpiter))
        break;

      tmpval = g_sequence_get (tmpiter);
      if (tmpval == value)
        return tmpiter;
      else if (*tmpval != *value)
        break;
    }
  }

  if (!g_sequence_iter_is_begin (iter)) {
    /* Looking backward for the good pointer */
    tmpiter = iter;
    while ((tmpiter = g_sequence_iter_prev (tmpiter))) {
      tmpval = g_sequence_get (tmpiter);
      if (tmpval == value)
        return tmpiter;
      else if (*tmpval != *value || g_sequence_iter_is_begin (tmpiter))
        break;
    }
  }

  GST_ERROR ("Missing timecode %p %" GST_TIME_FORMAT
      " this should never happen", value, GST_TIME_ARGS (*value));

  return NULL;
}

static inline void
sort_starts_ends_end (GESTimeline * timeline, GESTrackObject * obj)
{
  GSequenceIter *iter;

  GESTimelinePrivate *priv = timeline->priv;
  guint64 *end = g_hash_table_lookup (priv->by_end, obj);

  iter = lookup_pointer_uint (priv->starts_ends, end);
  *end = obj->start + obj->duration;

  g_sequence_sort_changed (iter, (GCompareDataFunc) compare_uint64, NULL);
}

static inline void
sort_starts_ends_start (GESTimeline * timeline, GESTrackObject * obj)
{
  GSequenceIter *iter;

  GESTimelinePrivate *priv = timeline->priv;
  guint64 *start = g_hash_table_lookup (priv->by_start, obj);

  iter = lookup_pointer_uint (priv->starts_ends, start);
  *start = obj->start;

  g_sequence_sort_changed (iter, (GCompareDataFunc) compare_uint64, NULL);
}

static inline void
resort_all_starts_ends (GESTimeline * timeline)
{
  GSequenceIter *iter;
  GESTrackObject *tckobj;
  guint64 *start, *end;

  GESTimelinePrivate *priv = timeline->priv;

  for (iter = g_sequence_get_begin_iter (priv->tracksources);
      !g_sequence_iter_is_end (iter); iter = g_sequence_iter_next (iter)) {
    tckobj = GES_TRACK_OBJECT (g_sequence_get (iter));

    start = g_hash_table_lookup (priv->by_start, tckobj);
    end = g_hash_table_lookup (priv->by_end, tckobj);

    *start = tckobj->start;
    *end = tckobj->start + tckobj->duration;
  }

  g_sequence_sort (priv->starts_ends, (GCompareDataFunc) compare_uint64, NULL);
}

static inline void
sort_all (GESTimeline * timeline)
{
  sort_track_objects (timeline);
  resort_all_starts_ends (timeline);
}

/* Timeline edition functions */
static inline void
init_movecontext (MoveContext * mv_ctx)
{
  mv_ctx->moving_tckobjs = NULL;
  mv_ctx->moving_tlobjs = g_hash_table_new (g_direct_hash, g_direct_equal);
  mv_ctx->max_trim_pos = G_MAXUINT64;
  mv_ctx->min_move_layer = G_MAXUINT;
  mv_ctx->max_layer_prio = 0;
  mv_ctx->last_snaped1 = NULL;
  mv_ctx->last_snaped2 = NULL;
  mv_ctx->last_snap_ts = GST_CLOCK_TIME_NONE;
}

static inline void
clean_movecontext (MoveContext * mv_ctx)
{
  g_list_free (mv_ctx->moving_tckobjs);
  g_hash_table_unref (mv_ctx->moving_tlobjs);
  init_movecontext (mv_ctx);
}

static void
stop_tracking_for_snapping (GESTimeline * timeline, GESTrackObject * tckobj)
{
  guint64 *start, *end;
  GESTimelinePrivate *priv = timeline->priv;
  GSequenceIter *iter_start, *iter_end, *tckobj_iter;


  start = g_hash_table_lookup (priv->by_start, tckobj);
  end = g_hash_table_lookup (priv->by_end, tckobj);

  iter_start = lookup_pointer_uint (priv->starts_ends, start);
  iter_end = lookup_pointer_uint (priv->starts_ends, end);
  tckobj_iter = g_sequence_lookup (timeline->priv->tracksources, tckobj,
      (GCompareDataFunc) objects_start_compare, NULL);

  g_hash_table_remove (priv->by_start, tckobj);
  g_hash_table_remove (priv->by_end, tckobj);
  g_hash_table_remove (priv->by_object, end);
  g_hash_table_remove (priv->by_object, start);

  g_sequence_remove (iter_start);
  g_sequence_remove (iter_end);
  g_sequence_remove (tckobj_iter);

}

static void
start_tracking_track_obj (GESTimeline * timeline, GESTrackObject * tckobj)
{
  guint64 *pstart, *pend;
  GESTimelinePrivate *priv = timeline->priv;

  pstart = g_malloc (sizeof (guint64));
  pend = g_malloc (sizeof (guint64));
  *pstart = tckobj->start;
  *pend = *pstart + tckobj->duration;

  g_sequence_insert_sorted (priv->starts_ends, pstart,
      (GCompareDataFunc) compare_uint64, NULL);
  g_sequence_insert_sorted (priv->starts_ends, pend,
      (GCompareDataFunc) compare_uint64, NULL);
  g_sequence_insert_sorted (priv->tracksources, g_object_ref (tckobj),
      (GCompareDataFunc) objects_start_compare, NULL);

  g_hash_table_insert (priv->by_start, tckobj, pstart);
  g_hash_table_insert (priv->by_object, pstart, tckobj);
  g_hash_table_insert (priv->by_end, tckobj, pend);
  g_hash_table_insert (priv->by_object, pend, tckobj);

  timeline->priv->movecontext.needs_move_ctx = TRUE;
}

static inline void
ges_timeline_emit_snappig (GESTimeline * timeline, GESTrackObject * obj1,
    guint64 * timecode)
{
  GESTrackObject *obj2;
  MoveContext *mv_ctx = &timeline->priv->movecontext;

  if (timecode == NULL) {
    if (mv_ctx->last_snaped1 != NULL && mv_ctx->last_snaped2 != NULL) {
      g_signal_emit (timeline, ges_timeline_signals[SNAPING_ENDED], 0,
          mv_ctx->last_snaped1, mv_ctx->last_snaped2, mv_ctx->last_snap_ts);

      /* We then need to recalculate the moving context */
      timeline->priv->movecontext.needs_move_ctx = TRUE;
    }

    return;
  }

  obj2 = g_hash_table_lookup (timeline->priv->by_object, timecode);

  if (mv_ctx->last_snap_ts != *timecode) {
    g_signal_emit (timeline, ges_timeline_signals[SNAPING_ENDED], 0,
        mv_ctx->last_snaped1, mv_ctx->last_snaped2, mv_ctx->last_snap_ts);

    /* We want the snap start signal to be emited anyway */
    mv_ctx->last_snap_ts = GST_CLOCK_TIME_NONE;
  }

  if (GST_CLOCK_TIME_IS_VALID (mv_ctx->last_snap_ts) == FALSE) {

    mv_ctx->last_snaped1 = obj1;
    mv_ctx->last_snaped2 = obj2;
    mv_ctx->last_snap_ts = *timecode;

    g_signal_emit (timeline, ges_timeline_signals[SNAPING_STARTED], 0,
        obj1, obj2, *timecode);

  }
}

static guint64 *
ges_timeline_snap_position (GESTimeline * timeline, GESTrackObject * trackobj,
    guint64 * current, guint64 timecode, gboolean emit)
{
  GESTimelinePrivate *priv = timeline->priv;
  GSequenceIter *iter, *prev_iter, *nxt_iter;
  GESTrackObject *tmp_tckobj;
  GESTimelineObject *tmp_tlobj, *tlobj;

  guint64 snap_distance = timeline->priv->snapping_distance;

  guint64 *prev_tc, *next_tc, *ret = NULL, off = G_MAXUINT64, off1 =
      G_MAXUINT64;

  /* Avoid useless calculations */
  if (snap_distance == 0)
    return NULL;

  tlobj = ges_track_object_get_timeline_object (trackobj);

  iter = g_sequence_search (priv->starts_ends, &timecode,
      (GCompareDataFunc) compare_uint64, NULL);

  /* Getting the next/previous  values, and use the closest one if any "respects"
   * the snap_distance value */
  nxt_iter = iter;
  while (!g_sequence_iter_is_end (nxt_iter)) {
    next_tc = g_sequence_get (iter);
    tmp_tckobj = g_hash_table_lookup (timeline->priv->by_object, next_tc);
    tmp_tlobj = ges_track_object_get_timeline_object (tmp_tckobj);

    off = timecode > *next_tc ? timecode - *next_tc : *next_tc - timecode;
    if (next_tc != current && off <= snap_distance && tlobj != tmp_tlobj) {

      ret = next_tc;
      break;
    }

    nxt_iter = g_sequence_iter_next (nxt_iter);
  }

  prev_iter = g_sequence_iter_prev (iter);
  while (!g_sequence_iter_is_begin (prev_iter)) {
    prev_tc = g_sequence_get (prev_iter);
    tmp_tckobj = g_hash_table_lookup (timeline->priv->by_object, prev_tc);
    tmp_tlobj = ges_track_object_get_timeline_object (tmp_tckobj);

    off1 = timecode > *prev_tc ? timecode - *prev_tc : *prev_tc - timecode;
    if (prev_tc != current && off1 < off && off1 <= snap_distance &&
        tlobj != tmp_tlobj) {
      ret = prev_tc;

      break;
    }

    prev_iter = g_sequence_iter_prev (prev_iter);
  }

  /* We emit the snapping signal only if we snapped with a different value
   * than the current one */
  if (emit) {
    GstClockTime snap_time = ret ? *ret : GST_CLOCK_TIME_NONE;

    ges_timeline_emit_snappig (timeline, trackobj, ret);

    GST_DEBUG_OBJECT (timeline, "Snaping at %" GST_TIME_FORMAT,
        GST_TIME_ARGS (snap_time));
  }

  return ret;
}

static inline GESTimelineObject *
add_moving_timeline_object (MoveContext * mv_ctx, GESTrackObject * tckobj)
{
  GESTimelineObject *tlobj;
  GESTimelineLayer *layer;
  guint layer_prio;

  tlobj = ges_track_object_get_timeline_object (tckobj);

  /* Avoid recalculating */
  if (!g_hash_table_lookup (mv_ctx->moving_tlobjs, tlobj)) {
    layer = ges_timeline_object_get_layer (tlobj);
    if (layer == NULL) {
      GST_WARNING_OBJECT (tlobj, "Not in any layer, can not move"
          " between layers");

    } else {

      g_hash_table_insert (mv_ctx->moving_tlobjs, tlobj, tlobj);

      layer_prio = ges_timeline_layer_get_priority (layer);
      mv_ctx->min_move_layer = MIN (mv_ctx->min_move_layer, layer_prio);
      mv_ctx->max_layer_prio = MAX (mv_ctx->max_layer_prio, layer_prio);

      g_object_unref (layer);
    }
  }

  return tlobj;
}

static gboolean
ges_move_context_set_objects (GESTimeline * timeline, GESTrackObject * obj,
    GESEdge edge)
{
  GSequenceIter *iter, *tckobj_iter;
  guint64 start, end, tmpend;
  GESTrackObject *tmptckobj;

  MoveContext *mv_ctx = &timeline->priv->movecontext;

  tckobj_iter = g_sequence_lookup (timeline->priv->tracksources, obj,
      (GCompareDataFunc) objects_start_compare, NULL);

  switch (edge) {
    case GES_EDGE_START:
      /* set it properly int the context of "trimming" */
      mv_ctx->max_trim_pos = 0;
      start = obj->start;

      if (g_sequence_iter_is_begin (tckobj_iter))
        break;

      /* Look for the objects */
      for (iter = g_sequence_iter_prev (tckobj_iter);
          iter && !g_sequence_iter_is_end (iter);
          iter = g_sequence_iter_prev (iter)) {

        tmptckobj = GES_TRACK_OBJECT (g_sequence_get (iter));
        tmpend = tmptckobj->start + tmptckobj->duration;

        if (tmpend <= start) {
          mv_ctx->max_trim_pos = MAX (mv_ctx->max_trim_pos, tmptckobj->start);
          mv_ctx->moving_tckobjs =
              g_list_prepend (mv_ctx->moving_tckobjs, tmptckobj);
        }

        if (g_sequence_iter_is_begin (iter))
          break;
      }
      break;

    case GES_EDGE_END:
    case GES_EDGE_NONE:        /* In this case only works for ripple */
      end = ges_track_object_get_start (obj) +
          ges_track_object_get_duration (obj);

      mv_ctx->max_trim_pos = G_MAXUINT64;

      /* Look for folowing objects */
      for (iter = g_sequence_iter_next (tckobj_iter);
          iter && !g_sequence_iter_is_end (iter);
          iter = g_sequence_iter_next (iter)) {
        tmptckobj = GES_TRACK_OBJECT (g_sequence_get (iter));

        if (tmptckobj->start >= end) {
          tmpend = tmptckobj->start + tmptckobj->duration;
          mv_ctx->max_trim_pos = MIN (mv_ctx->max_trim_pos, tmpend);
          mv_ctx->moving_tckobjs =
              g_list_prepend (mv_ctx->moving_tckobjs, tmptckobj);
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
ges_timeline_set_moving_context (GESTimeline * timeline, GESTrackObject * obj,
    GESEditMode mode, GESEdge edge, GList * layers)
{
  MoveContext *mv_ctx = &timeline->priv->movecontext;
  GESTimelineObject *tlobj = ges_track_object_get_timeline_object (obj);

  /* Still in the same mv_ctx */
  if ((mv_ctx->obj == tlobj && mv_ctx->mode == mode &&
          mv_ctx->edge == edge && !mv_ctx->needs_move_ctx)) {

    GST_DEBUG ("Keeping the same moving mv_ctx");
    return TRUE;
  }

  GST_DEBUG_OBJECT (tlobj,
      "Changing context:\nold: obj: %p, mode: %d, edge: %d \n"
      "new: obj: %p, mode: %d, edge: %d ! Has changed %i", mv_ctx->obj,
      mv_ctx->mode, mv_ctx->edge, tlobj, mode, edge, mv_ctx->needs_move_ctx);

  clean_movecontext (mv_ctx);
  mv_ctx->edge = edge;
  mv_ctx->mode = mode;
  mv_ctx->obj = tlobj;
  mv_ctx->needs_move_ctx = FALSE;

  switch (mode) {
    case GES_EDIT_MODE_RIPPLE:
    case GES_EDIT_MODE_ROLL:
      if (!(ges_move_context_set_objects (timeline, obj, edge)))
        return FALSE;
    default:
      break;
  }

  /* And we add the main object to the moving_tlobjs set */
  add_moving_timeline_object (&timeline->priv->movecontext, obj);


  return TRUE;
}

gboolean
ges_timeline_trim_object_simple (GESTimeline * timeline, GESTrackObject * obj,
    GList * layers, GESEdge edge, guint64 position, gboolean snapping)
{
  guint64 nstart, start, inpoint, duration, max_duration, *snapped, *cur;
  gboolean ret = TRUE;
  gint64 real_dur;

  GST_DEBUG_OBJECT (obj, "Trimming to %" GST_TIME_FORMAT " %s snaping, edge %i",
      GST_TIME_ARGS (position), snapping ? "Is" : "Not", edge);

  start = ges_track_object_get_start (obj);
  g_object_get (obj, "max-duration", &max_duration, NULL);

  switch (edge) {
    case GES_EDGE_START:
      inpoint = obj->inpoint;
      duration = obj->duration;

      if (snapping) {
        cur = g_hash_table_lookup (timeline->priv->by_start, obj);

        snapped = ges_timeline_snap_position (timeline, obj, cur, position,
            TRUE);
        if (snapped)
          position = *snapped;
      }

      nstart = position;

      /* Calculate new values */
      position = MAX (start > inpoint ? start - inpoint : 0, position);
      position = MIN (position, start + duration);
      inpoint = MAX (0, inpoint + position - start);

      real_dur = start + duration - nstart;
      /* FIXME: Why CLAMP (0, real_dur, max_duration) doesn't work? */
      duration = MAX (0, real_dur);
      duration = MIN (duration, max_duration - obj->inpoint);

      ges_track_object_set_start (obj, nstart);
      ges_track_object_set_duration (obj, duration);
      ges_track_object_set_inpoint (obj, inpoint);
      break;
    case GES_EDGE_END:
    {
      cur = g_hash_table_lookup (timeline->priv->by_end, obj);
      snapped = ges_timeline_snap_position (timeline, obj, cur, position, TRUE);
      if (snapped)
        position = *snapped;

      /* Calculate new values */
      real_dur = position - start;
      duration = MAX (0, real_dur);
      duration = MIN (duration, max_duration - obj->inpoint);

      ges_track_object_set_duration (obj, duration);
      break;
    }
    default:
      GST_WARNING ("Can not trim with %i GESEdge", edge);
      return FALSE;
  }

  return ret;
}

gboolean
timeline_ripple_object (GESTimeline * timeline, GESTrackObject * obj,
    GList * layers, GESEdge edge, guint64 position)
{
  GList *tmp, *moved_tlobjs = NULL;
  GESTrackObject *tckobj;
  GESTimelineObject *tlobj;
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

      cur = g_hash_table_lookup (timeline->priv->by_end, obj);
      snapped = ges_timeline_snap_position (timeline, obj, cur, position, TRUE);
      if (snapped)
        position = *snapped;

      offset = position - obj->start;

      for (tmp = mv_ctx->moving_tckobjs; tmp; tmp = tmp->next) {
        tckobj = GES_TRACK_OBJECT (tmp->data);
        new_start = tckobj->start + offset;

        tlobj = add_moving_timeline_object (mv_ctx, tckobj);

        if (ges_track_object_is_locked (tckobj) == TRUE) {

          /* Make sure not to move 2 times the same TimelineObject */
          if (g_list_find (moved_tlobjs, tlobj) == NULL) {
            ges_track_object_set_start (tckobj, new_start);
            moved_tlobjs = g_list_prepend (moved_tlobjs, tlobj);
          }

        } else {
          ges_track_object_set_start (tckobj, new_start);
        }
      }
      g_list_free (moved_tlobjs);
      ges_track_object_set_start (obj, position);

      break;
    case GES_EDGE_END:
      GST_DEBUG ("Rippling end");

      cur = g_hash_table_lookup (timeline->priv->by_end, obj);
      snapped = ges_timeline_snap_position (timeline, obj, cur, position, TRUE);
      if (snapped)
        position = *snapped;

      duration = obj->duration;

      ges_track_object_set_duration (obj, position - obj->start);

      offset = obj->duration - duration;
      for (tmp = mv_ctx->moving_tckobjs; tmp; tmp = tmp->next) {
        tckobj = GES_TRACK_OBJECT (tmp->data);
        new_start = tckobj->start + offset;

        tlobj = add_moving_timeline_object (mv_ctx, tckobj);

        if (ges_track_object_is_locked (tckobj) == TRUE) {

          /* Make sure not to move 2 times the same TimelineObject */
          if (g_list_find (moved_tlobjs, tlobj) == NULL) {
            ges_track_object_set_start (tckobj, new_start);
            moved_tlobjs = g_list_prepend (moved_tlobjs, tlobj);
          }

        } else {
          ges_track_object_set_start (tckobj, new_start);
        }
      }

      g_list_free (moved_tlobjs);
      GST_DEBUG ("Done Rippling end");
      break;
    case GES_EDGE_START:
      GST_WARNING ("Ripple start doesn't exist!");

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
timeline_slide_object (GESTimeline * timeline, GESTrackObject * obj,
    GList * layers, GESEdge edge, guint64 position)
{

  /* FIXME implement me! */
  GST_WARNING ("Slide mode editing not implemented yet");

  return FALSE;
}

gboolean
timeline_trim_object (GESTimeline * timeline, GESTrackObject * object,
    GList * layers, GESEdge edge, guint64 position)
{
  gboolean ret = FALSE;
  MoveContext *mv_ctx = &timeline->priv->movecontext;

  mv_ctx->ignore_needs_ctx = TRUE;

  if (!ges_timeline_set_moving_context (timeline, object, GES_EDIT_MODE_TRIM,
          edge, layers))
    goto end;

  ret =
      ges_timeline_trim_object_simple (timeline, object, layers, edge, position,
      TRUE);

end:
  mv_ctx->ignore_needs_ctx = FALSE;

  return ret;
}

gboolean
timeline_roll_object (GESTimeline * timeline, GESTrackObject * obj,
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

  start = ges_track_object_get_start (obj);
  duration = ges_track_object_get_duration (obj);
  end = start + duration;

  switch (edge) {
    case GES_EDGE_START:

      /* Avoid negative durations */
      if (position < mv_ctx->max_trim_pos || position > end)
        goto error;

      cur = g_hash_table_lookup (timeline->priv->by_start, obj);
      snapped = ges_timeline_snap_position (timeline, obj, cur, position, TRUE);
      if (snapped)
        position = *snapped;

      ret &=
          ges_timeline_trim_object_simple (timeline, obj, layers,
          GES_EDGE_START, position, FALSE);

      /* In the case we reached max_duration we just make sure to roll
       * everything to the real new position */
      position = obj->start;

      /* Send back changes to the neighbourhood */
      for (tmp = mv_ctx->moving_tckobjs; tmp; tmp = tmp->next) {
        GESTrackObject *tmptckobj = GES_TRACK_OBJECT (tmp->data);

        tmpstart = ges_track_object_get_start (tmptckobj);
        tmpduration = ges_track_object_get_duration (tmptckobj);
        tmpend = tmpstart + tmpduration;

        /* Check that the object should be resized at this position
         * even if an error accurs, we keep doing our job */
        if (tmpend == start) {
          ret &= ges_timeline_trim_object_simple (timeline, tmptckobj, NULL,
              GES_EDGE_END, position, FALSE);
          break;
        }
      }
      break;
    case GES_EDGE_END:

      /* Avoid negative durations */
      if (position > mv_ctx->max_trim_pos || position < start)
        goto error;

      end = obj->start + obj->duration;

      cur = g_hash_table_lookup (timeline->priv->by_end, obj);
      snapped = ges_timeline_snap_position (timeline, obj, cur, position, TRUE);
      if (snapped)
        position = *snapped;

      ret &= ges_timeline_trim_object_simple (timeline, obj, NULL, GES_EDGE_END,
          position, FALSE);

      /* In the case we reached max_duration we just make sure to roll
       * everything to the real new position */
      position = obj->start + obj->duration;

      /* Send back changes to the neighbourhood */
      for (tmp = mv_ctx->moving_tckobjs; tmp; tmp = tmp->next) {
        GESTrackObject *tmptckobj = GES_TRACK_OBJECT (tmp->data);

        tmpstart = ges_track_object_get_start (tmptckobj);
        tmpduration = ges_track_object_get_duration (tmptckobj);
        tmpend = tmpstart + tmpduration;

        /* Check that the object should be resized at this position
         * even if an error accure, we keep doing our job */
        if (end == tmpstart) {
          ret &= ges_timeline_trim_object_simple (timeline, tmptckobj, NULL,
              GES_EDGE_START, position, FALSE);
        }
      }
      break;
    default:
      GST_DEBUG ("Edge type %i not handled here", edge);
      break;
  }

  mv_ctx->ignore_needs_ctx = FALSE;

  return ret;

error:
  mv_ctx->ignore_needs_ctx = FALSE;

  GST_DEBUG_OBJECT (obj, "Could not roll edge %d to %" GST_TIME_FORMAT,
      edge, GST_TIME_ARGS (position));

  return FALSE;
}

gboolean
timeline_move_object (GESTimeline * timeline, GESTrackObject * object,
    GList * layers, GESEdge edge, guint64 position)
{
  if (!ges_timeline_set_moving_context (timeline, object, GES_EDIT_MODE_RIPPLE,
          edge, layers)) {
    GST_DEBUG_OBJECT (object, "Could not move to %" GST_TIME_FORMAT,
        GST_TIME_ARGS (position));

    return FALSE;
  }

  return ges_timeline_move_object_simple (timeline, object, layers, edge,
      position);
}

gboolean
ges_timeline_move_object_simple (GESTimeline * timeline,
    GESTrackObject * object, GList * layers, GESEdge edge, guint64 position)
{
  guint64 *snap_end, *snap_st, *cur, off1, off2, end;

  GST_DEBUG_OBJECT (timeline, "Moving to %" GST_TIME_FORMAT,
      GST_TIME_ARGS (position));

  end = position + object->duration;
  cur = g_hash_table_lookup (timeline->priv->by_end, object);
  snap_end = ges_timeline_snap_position (timeline, object, cur, end, FALSE);
  if (snap_end)
    off1 = end > *snap_end ? end - *snap_end : *snap_end - end;
  else
    off1 = G_MAXUINT64;

  cur = g_hash_table_lookup (timeline->priv->by_start, object);
  snap_st = ges_timeline_snap_position (timeline, object, cur, position, FALSE);
  if (snap_st)
    off2 = position > *snap_st ? position - *snap_st : *snap_st - position;
  else
    off2 = G_MAXUINT64;

  /* In the case we could snap on both sides, we snap on the end */
  if (snap_end && off1 <= off2) {
    position = position + *snap_end - end;
    ges_timeline_emit_snappig (timeline, object, snap_end);
    GST_DEBUG_OBJECT (timeline, "Real snap at %" GST_TIME_FORMAT,
        GST_TIME_ARGS (position));
  } else if (snap_st) {
    position = position + *snap_st - position;
    ges_timeline_emit_snappig (timeline, object, snap_st);
    GST_DEBUG_OBJECT (timeline, "Real snap at %" GST_TIME_FORMAT,
        GST_TIME_ARGS (position));
  } else
    ges_timeline_emit_snappig (timeline, object, NULL);


  ges_track_object_set_start (object, position);

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
    GESTimelineObject *key, *value;
    GESTimelineLayer *new_layer, *layer;
    guint prio;

    mv_ctx->ignore_needs_ctx = TRUE;

    GST_DEBUG ("Moving %d object, offset %d",
        g_hash_table_size (mv_ctx->moving_tlobjs), offset);

    g_hash_table_iter_init (&iter, mv_ctx->moving_tlobjs);
    while (g_hash_table_iter_next (&iter, (gpointer *) & key,
            (gpointer *) & value)) {
      layer = ges_timeline_object_get_layer (value);
      prio = ges_timeline_layer_get_priority (layer);

      /* We know that the layer exists as we created it */
      new_layer = GES_TIMELINE_LAYER (g_list_nth_data (timeline->priv->layers,
              prio + offset));

      if (new_layer == NULL) {
        do {
          new_layer = ges_timeline_append_layer (timeline);
        } while (ges_timeline_layer_get_priority (new_layer) < prio + offset);
      }

      ret &= ges_timeline_object_move_to_layer (key, new_layer);

      g_object_unref (layer);
    }

    /* Readjust min_move_layer */
    mv_ctx->min_move_layer = mv_ctx->min_move_layer + offset;

    mv_ctx->ignore_needs_ctx = FALSE;
  }

  return ret;
}

static void
add_object_to_track (GESTimelineObject * object, GESTrack * track)
{
  if (!ges_timeline_object_create_track_objects (object, track)) {
    if ((track->type & ges_timeline_object_get_supported_formats (object))) {
      GST_WARNING ("Error creating track objects");
    }
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

/* Callbacks  */
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
  GList *stream_list;
  GESTrackType tfs_supportedformats;

  gboolean found = FALSE;
  gboolean is_image = FALSE;
  GESTimelineFileSource *tfs = NULL;
  GESTimelinePrivate *priv = timeline->priv;
  const gchar *uri = gst_discoverer_info_get_uri (info);

  GES_TIMELINE_PENDINGOBJS_LOCK (timeline);

  /* Find corresponding TimelineFileSource in the sources */
  for (tmp = priv->pendingobjects; tmp; tmp = tmp->next) {
    tfs = (GESTimelineFileSource *) tmp->data;

    if (!g_strcmp0 (ges_timeline_filesource_get_uri (tfs), uri)) {
      found = TRUE;
      break;
    }
  }

  if (!found) {
    GST_WARNING ("Discovered %s, that seems not to be in the list of sources"
        "to discover", uri);
    GES_TIMELINE_PENDINGOBJS_UNLOCK (timeline);
    return;
  }

  if (err) {
    GError *propagate_error = NULL;

    priv->pendingobjects = g_list_delete_link (priv->pendingobjects, tmp);
    GES_TIMELINE_PENDINGOBJS_UNLOCK (timeline);
    GST_WARNING ("Error while discovering %s: %s", uri, err->message);

    g_propagate_error (&propagate_error, err);
    g_signal_emit (timeline, ges_timeline_signals[DISCOVERY_ERROR], 0, tfs,
        propagate_error);

    return;
  }

  /* Everything went fine... let's do our job! */
  GST_DEBUG ("Discovered uri %s", uri);

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
      ges_timeline_filesource_set_supported_formats (tfs, tfs_supportedformats);
    } else if (GST_IS_DISCOVERER_VIDEO_INFO (sinf)) {
      tfs_supportedformats |= GES_TRACK_TYPE_VIDEO;
      ges_timeline_filesource_set_supported_formats (tfs, tfs_supportedformats);
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

  /* Continue the processing on tfs */
  add_object_to_tracks (timeline, GES_TIMELINE_OBJECT (tfs));

  if (!is_image) {
    g_object_set (tfs, "max-duration",
        gst_discoverer_info_get_duration (info), NULL);
  }

  /* Remove the ref as the timeline file source is no longer needed here */
  g_object_unref (tfs);
}

static void
layer_object_added_cb (GESTimelineLayer * layer, GESTimelineObject * object,
    GESTimeline * timeline)
{
  if (ges_timeline_object_is_moving_from_layer (object)) {
    GST_DEBUG ("TimelineObject %p is moving from a layer to another, not doing"
        " anything on it", object);
    if (!timeline->priv->movecontext.ignore_needs_ctx)
      timeline->priv->movecontext.needs_move_ctx = TRUE;
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
  GList *trackobjects, *tmp;

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

static void
trackobj_start_changed_cb (GESTrackObject * child,
    GParamSpec * arg G_GNUC_UNUSED, GESTimeline * timeline)
{
  sort_track_objects (timeline);
  sort_starts_ends_start (timeline, child);
  sort_starts_ends_end (timeline, child);

  if (!timeline->priv->movecontext.ignore_needs_ctx)
    timeline->priv->movecontext.needs_move_ctx = TRUE;
}

static void
trackobj_duration_changed_cb (GESTrackObject * child,
    GParamSpec * arg G_GNUC_UNUSED, GESTimeline * timeline)
{
  sort_starts_ends_end (timeline, child);

  if (!timeline->priv->movecontext.ignore_needs_ctx)
    timeline->priv->movecontext.needs_move_ctx = TRUE;
}

static void
trackobj_inpoint_changed_cb (GESTrackObject * child,
    GParamSpec * arg G_GNUC_UNUSED, GESTimeline * timeline)
{
  if (!timeline->priv->movecontext.ignore_needs_ctx)
    timeline->priv->movecontext.needs_move_ctx = TRUE;
}

static void
track_object_added_cb (GESTrack * track, GESTrackObject * object,
    GESTimeline * timeline)
{
  /* We only work with sources */
  if (GES_IS_TRACK_SOURCE (object)) {
    start_tracking_track_obj (timeline, object);

    g_signal_connect (GES_TRACK_OBJECT (object), "notify::start",
        G_CALLBACK (trackobj_start_changed_cb), timeline);
    g_signal_connect (GES_TRACK_OBJECT (object), "notify::duration",
        G_CALLBACK (trackobj_duration_changed_cb), timeline);
    g_signal_connect (GES_TRACK_OBJECT (object), "notify::in-point",
        G_CALLBACK (trackobj_inpoint_changed_cb), timeline);
  }
}

static void
track_object_removed_cb (GESTrack * track, GESTrackObject * object,
    GESTimeline * timeline)
{
  /* We only work with sources */
  if (GES_IS_TRACK_SOURCE (object)) {
    g_signal_handlers_disconnect_by_func (object, trackobj_start_changed_cb,
        NULL);
    g_signal_handlers_disconnect_by_func (object, trackobj_duration_changed_cb,
        NULL);
    g_signal_handlers_disconnect_by_func (object, trackobj_inpoint_changed_cb,
        NULL);

    /* Make sure to reinitialise the moving context next time */
    timeline->priv->movecontext.needs_move_ctx = TRUE;
    stop_tracking_for_snapping (timeline, object);
  }
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

    GST_DEBUG_OBJECT (track, "track duration : %" GST_TIME_FORMAT,
        GST_TIME_ARGS (duration));
    max_duration = MAX (duration, max_duration);
  }

  if (timeline->priv->duration != max_duration) {
    GST_DEBUG ("track duration : %" GST_TIME_FORMAT " current : %"
        GST_TIME_FORMAT, GST_TIME_ARGS (max_duration),
        GST_TIME_ARGS (timeline->priv->duration));

    timeline->priv->duration = max_duration;

    g_object_notify_by_pspec (G_OBJECT (timeline), properties[PROP_DURATION]);
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

/**
 * ges_timeline_append_layer:
 * @timeline: a #GESTimeline
 *
 * Append a newly created #GESTimelineLayer to @timeline
 * Note that you do not own any reference to the returned layer.
 *
 * Returns: (transfer none): The newly created #GESTimelineLayer, or the last (empty)
 * #GESTimelineLayer of @timeline.
 */
GESTimelineLayer *
ges_timeline_append_layer (GESTimeline * timeline)
{
  guint32 priority;
  GESTimelinePrivate *priv = timeline->priv;
  GESTimelineLayer *layer;

  layer = ges_timeline_layer_new ();
  priority = g_list_length (priv->layers);
  ges_timeline_layer_set_priority (layer, priority);

  ges_timeline_add_layer (timeline, layer);

  return layer;
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
  gboolean no_more;
  GList *tmp;

  GST_DEBUG ("track:%p, pad:%s:%s", track, GST_DEBUG_PAD_NAME (pad));

  if (G_UNLIKELY (tr_priv->pad)) {
    GST_WARNING ("We are already controlling a pad for this track");
    return;
  }

  /* Remember the pad */
  GST_OBJECT_LOCK (track);
  tr_priv->pad = pad;

  no_more = TRUE;
  for (tmp = tr_priv->timeline->priv->tracks; tmp; tmp = g_list_next (tmp)) {
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

  /* We connect to the duration change notify, so we can update
   * our duration accordingly */
  g_signal_connect (G_OBJECT (track), "notify::duration",
      G_CALLBACK (track_duration_cb), timeline);

  /* We connect to the object for the timeline editing mode management */
  g_signal_connect (G_OBJECT (track), "track-object-added",
      G_CALLBACK (track_object_added_cb), timeline);
  g_signal_connect (G_OBJECT (track), "track-object-removed",
      G_CALLBACK (track_object_removed_cb), timeline);

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
  g_signal_handlers_disconnect_by_func (track, track_object_added_cb, timeline);
  g_signal_handlers_disconnect_by_func (track, track_object_removed_cb,
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
 * ges_timeline_is_updating:
 * @timeline: a #GESTimeline
 *
 * Get whether the timeline is updated for every change happening within or not.
 *
 * Returns: %TRUE if @timeline is updating on every changes, else %FALSE.
 */
gboolean
ges_timeline_is_updating (GESTimeline * timeline)
{
  GList *tmp;

  g_return_val_if_fail (GES_IS_TIMELINE (timeline), FALSE);

  for (tmp = timeline->priv->tracks; tmp; tmp = tmp->next) {
    if (!ges_track_is_updating (((TrackPrivate *) tmp->data)->track))
      return FALSE;
  }

  return TRUE;
}

/**
 * ges_timeline_enable_update:
 * @timeline: a #GESTimeline
 * @enabled: Whether the timeline should update on every change or not.
 *
 * Control whether the timeline is updated for every change happening within.
 *
 * Users will want to use this method with %FALSE before doing lots of changes,
 * and then call again with %TRUE for the changes to take effect in one go.
 *
 * Returns: %TRUE if the update status could be changed, else %FALSE.
 */
gboolean
ges_timeline_enable_update (GESTimeline * timeline, gboolean enabled)
{
  GList *tmp;
  gboolean res = TRUE;

  GST_DEBUG_OBJECT (timeline, "%s updates", enabled ? "Enabling" : "Disabling");

  for (tmp = timeline->priv->tracks; tmp; tmp = tmp->next) {
    if (!ges_track_enable_update (((TrackPrivate *) tmp->data)->track, enabled))
      res = FALSE;
  }

  /* Make sure we reset the context */
  timeline->priv->movecontext.needs_move_ctx = TRUE;

  return res;
}

/**
 * ges_timeline_get_duration
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
