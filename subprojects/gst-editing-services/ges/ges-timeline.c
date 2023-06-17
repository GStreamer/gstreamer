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
 * A timeline is composed of a set of #GESTrack-s and a set of
 * #GESLayer-s, which are added to the timeline using
 * ges_timeline_add_track() and ges_timeline_append_layer(), respectively.
 *
 * The contained tracks define the supported types of the timeline
 * and provide the media output. Essentially, each track provides an
 * additional source #GstPad.
 *
 * Most usage of a timeline will likely only need a single #GESAudioTrack
 * and/or a single #GESVideoTrack. You can create such a timeline with
 * ges_timeline_new_audio_video(). After this, you are unlikely to need to
 * work with the tracks directly.
 *
 * A timeline's layers contain #GESClip-s, which in turn control the
 * creation of #GESTrackElement-s, which are added to the timeline's
 * tracks. See #GESTimeline::select-tracks-for-object if you wish to have
 * more control over which track a clip's elements are added to.
 *
 * The layers are ordered, with higher priority layers having their
 * content prioritised in the tracks. This ordering can be changed using
 * ges_timeline_move_layer().
 *
 * ## Editing
 *
 * See #GESTimelineElement for the various ways the elements of a timeline
 * can be edited.
 *
 * If you change the timing or ordering of a timeline's
 * #GESTimelineElement-s, then these changes will not actually be taken
 * into account in the output of the timeline's tracks until the
 * ges_timeline_commit() method is called. This allows you to move its
 * elements around, say, in response to an end user's mouse dragging, with
 * little expense before finalising their effect on the produced data.
 *
 * ## Overlaps and Auto-Transitions
 *
 * There are certain restrictions placed on how #GESSource-s may overlap
 * in a #GESTrack that belongs to a timeline. These will be enforced by
 * GES, so the user will not need to keep track of them, but they should
 * be aware that certain edits will be refused as a result if the overlap
 * rules would be broken.
 *
 * Consider two #GESSource-s, `A` and `B`, with start times `startA` and
 * `startB`, and end times `endA` and `endB`, respectively. The start
 * time refers to their #GESTimelineElement:start, and the end time is
 * their #GESTimelineElement:start + #GESTimelineElement:duration. These
 * two sources *overlap* if:
 *
 * + they share the same #GESTrackElement:track (non %NULL), which belongs
 *   to the timeline;
 * + they share the same #GES_TIMELINE_ELEMENT_LAYER_PRIORITY; and
 * + `startA < endB` and `startB < endA `.
 *
 * Note that when `startA = endB` or `startB = endA` then the two sources
 * will *touch* at their edges, but are not considered overlapping.
 *
 * If, in addition, `startA < startB < endA`, then we can say that the
 * end of `A` overlaps the start of `B`.
 *
 * If, instead, `startA <= startB` and `endA >= endB`, then we can say
 * that `A` fully overlaps `B`.
 *
 * The overlap rules for a timeline are that:
 *
 * 1. One source cannot fully overlap another source.
 * 2. A source can only overlap the end of up to one other source at its
 *    start.
 * 3. A source can only overlap the start of up to one other source at its
 *    end.
 *
 * The last two rules combined essentially mean that at any given timeline
 * position, only up to two #GESSource-s may overlap at that position. So
 * triple or more overlaps are not allowed.
 *
 * If you switch on #GESTimeline:auto-transition, then at any moment when
 * the end of one source (the first source) overlaps the start of another
 * (the second source), a #GESTransitionClip will be automatically created
 * for the pair in the same layer and it will cover their overlap. If the
 * two elements are edited in a way such that the end of the first source
 * no longer overlaps the start of the second, the transition will be
 * automatically removed from the timeline. However, if the two sources
 * still overlap at the same edges after the edit, then the same
 * transition object will be kept, but with its timing and layer adjusted
 * accordingly.
 *
 * NOTE: if you know what you are doing and want to be in full control of the
 * timeline layout, you can disable the edit APIs with
 * #ges_timeline_disable_edit_apis.
 *
 * ## Saving
 *
 * To save/load a timeline, you can use the ges_timeline_load_from_uri()
 * and ges_timeline_save_to_uri() methods that use the default format.
 *
 * ## Playing
 *
 * A timeline is a #GstBin with a source #GstPad for each of its
 * tracks, which you can fetch with ges_timeline_get_pad_for_track(). You
 * will likely want to link these to some compatible sink #GstElement-s to
 * be able to play or capture the content of the timeline.
 *
 * You can use a #GESPipeline to easily preview/play the timeline's
 * content, or render it to a file.
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

  GESTrack *auto_transition_track;
  GESTrack *new_track;

  /* While we are creating and adding the TrackElements for a clip, we need to
   * ignore the child-added signal */
  gboolean track_elements_moving;
  /* whether any error occurred during track selection, including
   * programming or usage errors */
  gboolean has_any_track_selection_error;
  /* error set for non-programming/usage errors */
  GError *track_selection_error;
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
  gboolean commit_frozen;
  gboolean commit_delayed;

  GThread *valid_thread;
  gboolean disposed;

  GstStreamCollection *stream_collection;

  gboolean rendering_smartly;
  gboolean disable_edit_apis;
};

/* private structure to contain our track-related information */

typedef struct
{
  GESTimeline *timeline;
  GESTrack *track;
  GstPad *pad;                  /* Pad from the track */
  GstPad *ghostpad;
  gulong track_element_added_sigid;

  gulong probe_id;
  GstStream *stream;
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
  SELECT_ELEMENT_TRACK,
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

gboolean
ges_timeline_is_disposed (GESTimeline * timeline)
{
  return timeline->priv->disposed;
}

static void
ges_timeline_dispose (GObject * object)
{
  GESTimeline *tl = GES_TIMELINE (object);
  GESTimelinePrivate *priv = tl->priv;
  GList *tmp, *groups;

  priv->disposed = TRUE;
  while (tl->layers) {
    GESLayer *layer = (GESLayer *) tl->layers->data;
    ges_timeline_remove_layer (GES_TIMELINE (object), layer);
  }

  /* FIXME: it should be possible to remove tracks before removing
   * layers, but at the moment this creates a problem because the track
   * objects aren't notified that their nleobjects have been destroyed.
   */

  LOCK_DYN (tl);
  while (tl->tracks)
    ges_timeline_remove_track (GES_TIMELINE (object), tl->tracks->data);
  UNLOCK_DYN (tl);

  /* NOTE: the timeline should not contain empty groups */
  groups = g_list_copy_deep (priv->groups, (GCopyFunc) gst_object_ref, NULL);
  for (tmp = groups; tmp; tmp = tmp->next) {
    GList *elems = ges_container_ungroup (tmp->data, FALSE);

    g_list_free_full (elems, gst_object_unref);
  }
  g_list_free_full (groups, gst_object_unref);
  g_list_free_full (priv->groups, gst_object_unref);

  g_list_free_full (priv->auto_transitions, gst_object_unref);

  g_hash_table_unref (priv->all_elements);
  gst_object_unref (priv->stream_collection);

  gst_clear_object (&priv->auto_transition_track);
  gst_clear_object (&priv->new_track);
  g_clear_error (&priv->track_selection_error);
  priv->track_selection_error = NULL;

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

  if (GST_MESSAGE_TYPE (message) == GST_MESSAGE_ASYNC_START) {
    GST_INFO_OBJECT (timeline, "Dropping %" GST_PTR_FORMAT, message);
    return;
  }

  if (GST_MESSAGE_TYPE (message) == GST_MESSAGE_ASYNC_DONE) {
    GST_INFO_OBJECT (timeline, "Dropping %" GST_PTR_FORMAT, message);

    return;
  }

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
        LOCK_DYN (timeline);
        timeline->priv->expected_async_done = g_list_length (timeline->tracks);
        UNLOCK_DYN (timeline);
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

    if (amessage) {
      gst_message_unref (message);
      gst_element_post_message (GST_ELEMENT_CAST (bin), amessage);
      return;
    }
  }

forward:
  gst_element_post_message (GST_ELEMENT_CAST (bin), message);
}

static GstStateChangeReturn
ges_timeline_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn res;
  GESTimeline *timeline = GES_TIMELINE (element);

  res = GST_ELEMENT_CLASS (ges_timeline_parent_class)->change_state (element,
      transition);

  if (transition == GST_STATE_CHANGE_READY_TO_PAUSED)
    gst_element_post_message ((GstElement *) timeline,
        gst_message_new_stream_collection ((GstObject *) timeline,
            timeline->priv->stream_collection));
  return res;
}

static gboolean
ges_timeline_send_event (GstElement * element, GstEvent * event)
{
  GESTimeline *timeline = GES_TIMELINE (element);

  if (GST_EVENT_TYPE (event) == GST_EVENT_SELECT_STREAMS) {
    GList *stream_ids = NULL, *tmp, *to_remove =
        ges_timeline_get_tracks (timeline);

    gst_event_parse_select_streams (event, &stream_ids);
    for (tmp = stream_ids; tmp; tmp = tmp->next) {
      GList *trackit;
      gchar *stream_id = tmp->data;

      LOCK_DYN (timeline);
      for (trackit = timeline->priv->priv_tracks; trackit;
          trackit = trackit->next) {
        TrackPrivate *tr_priv = trackit->data;

        if (!g_strcmp0 (gst_stream_get_stream_id (tr_priv->stream), stream_id)) {
          to_remove = g_list_remove (to_remove, tr_priv->track);
        }
      }
      UNLOCK_DYN (timeline);
    }
    for (tmp = to_remove; tmp; tmp = tmp->next) {
      GST_INFO_OBJECT (timeline, "Removed unselected track: %" GST_PTR_FORMAT,
          tmp->data);
      ges_timeline_remove_track (timeline, tmp->data);
    }

    g_list_free_full (stream_ids, g_free);
    g_list_free (to_remove);

    return TRUE;
  }

  return GST_ELEMENT_CLASS (ges_timeline_parent_class)->send_event (element,
      event);
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
  GstElementClass *element_class = (GstElementClass *) klass;
  GstBinClass *bin_class = GST_BIN_CLASS (klass);

  GST_DEBUG_CATEGORY_INIT (ges_timeline_debug, "gestimeline",
      GST_DEBUG_FG_YELLOW, "ges timeline");
  timeline_tree_init_debug ();

  parent_class = g_type_class_peek_parent (klass);

  object_class->get_property = ges_timeline_get_property;
  object_class->set_property = ges_timeline_set_property;
  object_class->dispose = ges_timeline_dispose;
  object_class->finalize = ges_timeline_finalize;

  element_class->change_state = GST_DEBUG_FUNCPTR (ges_timeline_change_state);
  element_class->send_event = GST_DEBUG_FUNCPTR (ges_timeline_send_event);

  bin_class->handle_message = GST_DEBUG_FUNCPTR (ges_timeline_handle_message);

  /**
   * GESTimeline:duration:
   *
   * The current duration (in nanoseconds) of the timeline. A timeline
   * 'starts' at time 0, so this is the maximum end time of all of its
   * #GESTimelineElement-s.
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
   * Whether to automatically create a transition whenever two
   * #GESSource-s overlap in a track of the timeline. See
   * #GESLayer:auto-transition if you want this to only happen in some
   * layers.
   */
  g_object_class_install_property (object_class, PROP_AUTO_TRANSITION,
      g_param_spec_boolean ("auto-transition", "Auto-Transition",
          "whether the transitions are added", FALSE, G_PARAM_READWRITE));

  /**
   * GESTimeline:snapping-distance:
   *
   * The distance (in nanoseconds) at which a #GESTimelineElement being
   * moved within the timeline should snap one of its #GESSource-s with
   * another #GESSource-s edge. See #GESEditMode for which edges can
   * snap during an edit. 0 means no snapping.
   */
  properties[PROP_SNAPPING_DISTANCE] =
      g_param_spec_uint64 ("snapping-distance", "Snapping distance",
      "Distance from which moving an object will snap with neighbours", 0,
      G_MAXUINT64, 0, G_PARAM_READWRITE);
  g_object_class_install_property (object_class, PROP_SNAPPING_DISTANCE,
      properties[PROP_SNAPPING_DISTANCE]);

  /**
   * GESTimeline::track-added:
   * @timeline: The #GESTimeline
   * @track: The track that was added to @timeline
   *
   * Will be emitted after the track is added to the timeline.
   *
   * Note that this should not be emitted whilst a timeline is being
   * loaded from its #GESProject asset. You should connect to the
   * project's #GESProject::loaded signal if you want to know which
   * tracks were created for the timeline.
   */
  ges_timeline_signals[TRACK_ADDED] =
      g_signal_new ("track-added", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_FIRST, G_STRUCT_OFFSET (GESTimelineClass, track_added), NULL,
      NULL, NULL, G_TYPE_NONE, 1, GES_TYPE_TRACK);

  /**
   * GESTimeline::track-removed:
   * @timeline: The #GESTimeline
   * @track: The track that was removed from @timeline
   *
   * Will be emitted after the track is removed from the timeline.
   */
  ges_timeline_signals[TRACK_REMOVED] =
      g_signal_new ("track-removed", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_FIRST, G_STRUCT_OFFSET (GESTimelineClass, track_removed),
      NULL, NULL, NULL, G_TYPE_NONE, 1, GES_TYPE_TRACK);

  /**
   * GESTimeline::layer-added:
   * @timeline: The #GESTimeline
   * @layer: The layer that was added to @timeline
   *
   * Will be emitted after the layer is added to the timeline.
   *
   * Note that this should not be emitted whilst a timeline is being
   * loaded from its #GESProject asset. You should connect to the
   * project's #GESProject::loaded signal if you want to know which
   * layers were created for the timeline.
   */
  ges_timeline_signals[LAYER_ADDED] =
      g_signal_new ("layer-added", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_FIRST, G_STRUCT_OFFSET (GESTimelineClass, layer_added), NULL,
      NULL, NULL, G_TYPE_NONE, 1, GES_TYPE_LAYER);

  /**
   * GESTimeline::layer-removed:
   * @timeline: The #GESTimeline
   * @layer: The layer that was removed from @timeline
   *
   * Will be emitted after the layer is removed from the timeline.
   */
  ges_timeline_signals[LAYER_REMOVED] =
      g_signal_new ("layer-removed", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_FIRST, G_STRUCT_OFFSET (GESTimelineClass, layer_removed),
      NULL, NULL, NULL, G_TYPE_NONE, 1, GES_TYPE_LAYER);

  /**
   * GESTimeline::group-added
   * @timeline: The #GESTimeline
   * @group: The group that was added to @timeline
   *
   * Will be emitted after the group is added to to the timeline. This can
   * happen when grouping with `ges_container_group`, or by adding
   * containers to a newly created group.
   *
   * Note that this should not be emitted whilst a timeline is being
   * loaded from its #GESProject asset. You should connect to the
   * project's #GESProject::loaded signal if you want to know which groups
   * were created for the timeline.
   */
  ges_timeline_signals[GROUP_ADDED] =
      g_signal_new ("group-added", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_FIRST, G_STRUCT_OFFSET (GESTimelineClass, group_added), NULL,
      NULL, NULL, G_TYPE_NONE, 1, GES_TYPE_GROUP);

  /**
   * GESTimeline::group-removed
   * @timeline: The #GESTimeline
   * @group: The group that was removed from @timeline
   * @children: (element-type GESContainer) (transfer none): A list
   * of #GESContainer-s that _were_ the children of the removed @group
   *
   * Will be emitted after the group is removed from the timeline through
   * `ges_container_ungroup`. Note that @group will no longer contain its
   * former children, these are held in @children.
   *
   * Note that if a group is emptied, then it will no longer belong to the
   * timeline, but this signal will **not** be emitted in such a case.
   */
  ges_timeline_signals[GROUP_REMOVED] =
      g_signal_new ("group-removed", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_FIRST, G_STRUCT_OFFSET (GESTimelineClass, group_removed),
      NULL, NULL, NULL, G_TYPE_NONE, 2, GES_TYPE_GROUP, G_TYPE_PTR_ARRAY);

  /**
   * GESTimeline::snapping-started:
   * @timeline: The #GESTimeline
   * @obj1: The first element that is snapping
   * @obj2: The second element that is snapping
   * @position: The position where the two objects will snap to
   *
   * Will be emitted whenever an element's movement invokes a snapping
   * event during an edit (usually of one of its ancestors) because its
   * start or end point lies within the #GESTimeline:snapping-distance of
   * another element's start or end point.
   *
   * See #GESEditMode to see what can snap during an edit.
   *
   * Note that only up to one snapping-started signal will be emitted per
   * element edit within a timeline.
   */
  ges_timeline_signals[SNAPING_STARTED] =
      g_signal_new ("snapping-started", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
      G_TYPE_NONE, 3, GES_TYPE_TRACK_ELEMENT, GES_TYPE_TRACK_ELEMENT,
      G_TYPE_UINT64);

  /**
   * GESTimeline::snapping-ended:
   * @timeline: The #GESTimeline
   * @obj1: The first element that was snapping
   * @obj2: The second element that was snapping
   * @position: The position where the two objects were to be snapped to
   *
   * Will be emitted whenever a snapping event ends. After a snap event
   * has started (see #GESTimeline::snapping-started), it can later end
   * because either another timeline edit has occurred (which may or may
   * not have created a new snapping event), or because the timeline has
   * been committed.
   */
  ges_timeline_signals[SNAPING_ENDED] =
      g_signal_new ("snapping-ended", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
      G_TYPE_NONE, 3, GES_TYPE_TRACK_ELEMENT, GES_TYPE_TRACK_ELEMENT,
      G_TYPE_UINT64);

  /**
   * GESTimeline::select-tracks-for-object:
   * @timeline: The #GESTimeline
   * @clip: The clip that @track_element is being added to
   * @track_element: The element being added
   *
   * This will be emitted whenever the timeline needs to determine which
   * tracks a clip's children should be added to. The track element will
   * be added to each of the tracks given in the return. If a track
   * element is selected to go into multiple tracks, it will be copied
   * into the additional tracks, under the same clip. Note that the copy
   * will *not* keep its properties or state in sync with the original.
   *
   * Connect to this signal once if you wish to control which element
   * should be added to which track. Doing so will overwrite the default
   * behaviour, which adds @track_element to all tracks whose
   * #GESTrack:track-type includes the @track_element's
   * #GESTrackElement:track-type.
   *
   * Note that under the default track selection, if a clip would produce
   * multiple core children of the same #GESTrackType, it will choose
   * one of the core children arbitrarily to place in the corresponding
   * tracks, with a warning for the other core children that are not
   * placed in the track. For example, this would happen for a #GESUriClip
   * that points to a file that contains multiple audio streams. If you
   * wish to choose the stream, you could connect to this signal, and use,
   * say, ges_uri_source_asset_get_stream_info() to choose which core
   * source to add.
   *
   * When a clip is first added to a timeline, its core elements will
   * be created for the current tracks in the timeline if they have not
   * already been created. Then this will be emitted for each of these
   * core children to select which tracks, if any, they should be added
   * to. It will then be called for any non-core children in the clip.
   *
   * In addition, if a new track element is ever added to a clip in a
   * timeline (and it is not already part of a track) this will be emitted
   * to select which tracks the element should be added to.
   *
   * Finally, as a special case, if a track is added to the timeline
   * *after* it already contains clips, then it will request the creation
   * of the clips' core elements of the corresponding type, if they have
   * not already been created, and this signal will be emitted for each of
   * these newly created elements. In addition, this will also be released
   * for all other track elements in the timeline's clips that have not
   * yet been assigned a track. However, in this final case, the timeline
   * will only check whether the newly added track appears in the track
   * list. If it does appear, the track element will be added to the newly
   * added track. All other tracks in the returned track list are ignored.
   *
   * In this latter case, track elements that are already part of a track
   * will not be asked if they want to be copied into the new track. If
   * you wish to do this, you can use ges_clip_add_child_to_track().
   *
   * Note that the returned #GPtrArray should own a new reference to each
   * of its contained #GESTrack. The timeline will set the #GDestroyNotify
   * free function on the #GPtrArray to dereference the elements.
   *
   * Returns: (transfer full) (element-type GESTrack): An array of
   * #GESTrack-s that @track_element should be added to, or %NULL to
   * not add the element to any track.
   */
  ges_timeline_signals[SELECT_TRACKS_FOR_OBJECT] =
      g_signal_new ("select-tracks-for-object", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, _gst_array_accumulator, NULL, NULL,
      G_TYPE_PTR_ARRAY, 2, GES_TYPE_CLIP, GES_TYPE_TRACK_ELEMENT);

  /**
   * GESTimeline::select-element-track:
   * @timeline: The #GESTimeline
   * @clip: The clip that @track_element is being added to
   * @track_element: The element being added
   *
   * Simplified version of #GESTimeline::select-tracks-for-object which only
   * allows @track_element to be added to a single #GESTrack.
   *
   * Returns: (transfer full) (nullable): A track to put @track_element into, or %NULL if
   * it should be discarded.
   *
   * Since: 1.18
   */
  ges_timeline_signals[SELECT_ELEMENT_TRACK] =
      g_signal_new ("select-element-track", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
      GES_TYPE_TRACK, 2, GES_TYPE_CLIP, GES_TYPE_TRACK_ELEMENT);

  /**
   * GESTimeline::commited:
   * @timeline: The #GESTimeline
   *
   * This signal will be emitted once the changes initiated by
   * ges_timeline_commit() have been executed in the backend. Use
   * ges_timeline_commit_sync() if you do not want to have to connect
   * to this signal.
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

  priv->all_elements =
      g_hash_table_new_full (g_str_hash, g_str_equal, g_free, gst_object_unref);

  priv->stream_start_group_id = -1;
  priv->stream_collection = gst_stream_collection_new (NULL);

  g_signal_connect_after (self, "select-tracks-for-object",
      G_CALLBACK (select_tracks_for_object_default), NULL);

  g_rec_mutex_init (&priv->dyn_mutex);
  g_mutex_init (&priv->commited_lock);
  priv->valid_thread = g_thread_self ();
}

/* Private methods */

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

  if (prio_a > prio_b)
    return 1;
  if (prio_a < prio_b)
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
  GESAutoTransition *auto_transition;
  GESTrackElement *child;
  /* track should not be NULL */
  GESTrack *track = ges_track_element_get_track (next);

  if (transition == NULL) {
    GESAsset *asset;

    LOCK_DYN (timeline);
    timeline->priv->auto_transition_track = gst_object_ref (track);
    UNLOCK_DYN (timeline);

    asset = ges_asset_request (GES_TYPE_TRANSITION_CLIP, "crossfade", NULL);
    transition = ges_layer_add_asset (layer, asset, start, 0, duration,
        ges_track_element_get_track_type (next));
    gst_object_unref (asset);

    LOCK_DYN (timeline);
    /* should have been set to NULL, but clear just in case */
    gst_clear_object (&timeline->priv->auto_transition_track);
    UNLOCK_DYN (timeline);
  } else {
    GST_DEBUG_OBJECT (timeline,
        "Reusing already existing transition: %" GST_PTR_FORMAT, transition);
  }

  g_return_val_if_fail (transition, NULL);
  g_return_val_if_fail (g_list_length (GES_CONTAINER_CHILDREN (transition)) ==
      1, NULL);
  child = GES_CONTAINER_CHILDREN (transition)->data;
  if (ges_track_element_get_track (child) != track) {
    GST_ERROR_OBJECT (timeline, "The auto transition element %"
        GES_FORMAT " for elements %" GES_FORMAT " and %" GES_FORMAT
        " is not in the same track %" GST_PTR_FORMAT,
        GES_ARGS (child), GES_ARGS (previous), GES_ARGS (next), track);
    return NULL;
  }

  /* We know there is only 1 TrackElement */
  auto_transition = ges_auto_transition_new (child, previous, next);

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

GESAutoTransition *
ges_timeline_get_auto_transition_at_edge (GESTimeline * timeline,
    GESTrackElement * source, GESEdge edge)
{
  GList *tmp, *auto_transitions;
  GESAutoTransition *ret = NULL;

  LOCK_DYN (timeline);
  auto_transitions = g_list_copy_deep (timeline->priv->auto_transitions,
      (GCopyFunc) gst_object_ref, NULL);
  UNLOCK_DYN (timeline);

  for (tmp = auto_transitions; tmp; tmp = tmp->next) {
    GESAutoTransition *auto_trans = (GESAutoTransition *) tmp->data;

    /* We already have a transition linked to one of the elements we want to
     * find a transition for */
    if (edge == GES_EDGE_END && auto_trans->previous_source == source) {
      ret = gst_object_ref (auto_trans);
      break;
    } else if (edge == GES_EDGE_START && auto_trans->next_source == source) {
      ret = gst_object_ref (auto_trans);
      break;
    }
  }

  g_list_free_full (auto_transitions, gst_object_unref);

  return ret;
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
ges_timeline_emit_snapping (GESTimeline * timeline, GESTrackElement * elem1,
    GESTrackElement * elem2, GstClockTime snap_time)
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

  if (GST_CLOCK_TIME_IS_VALID (last_snap_ts))
    g_signal_emit (timeline, ges_timeline_signals[SNAPING_ENDED], 0,
        priv->last_snaped1, priv->last_snaped2, (last_snap_ts));

  priv->last_snaped1 = elem1;
  priv->last_snaped2 = elem2;
  timeline->priv->last_snap_ts = snap_time;
  g_signal_emit (timeline, ges_timeline_signals[SNAPING_STARTED], 0,
      elem1, elem2, snap_time);
}

/* Accept @self == NULL, making it use default framerate */
void
timeline_get_framerate (GESTimeline * self, gint * fps_n, gint * fps_d)
{
  GList *tmp;

  *fps_n = *fps_d = -1;
  if (!self)
    goto done;

  LOCK_DYN (self);
  for (tmp = self->tracks; tmp; tmp = tmp->next) {
    if (GES_IS_VIDEO_TRACK (tmp->data)) {
      GstCaps *restriction = ges_track_get_restriction_caps (tmp->data);
      gint i;

      if (!restriction)
        continue;

      for (i = 0; i < gst_caps_get_size (restriction); i++) {
        gint n, d;

        if (!gst_structure_get_fraction (gst_caps_get_structure (restriction,
                    i), "framerate", &n, &d))
          continue;

        if (*fps_n != -1 && *fps_d != -1 && !(n == *fps_n && d == *fps_d)) {
          GST_WARNING_OBJECT (self,
              "Various framerates specified, this is not supported"
              " First one will be used.");
          continue;
        }

        *fps_n = n;
        *fps_d = d;
      }
      gst_caps_unref (restriction);
    }
  }
  UNLOCK_DYN (self);

done:
  if (*fps_n == -1 && *fps_d == -1) {
    GST_INFO_OBJECT (self,
        "No framerate found, using default " G_STRINGIFY (FRAMERATE_N) "/ "
        G_STRINGIFY (FRAMERATE_D));
    *fps_n = DEFAULT_FRAMERATE_N;
    *fps_d = DEFAULT_FRAMERATE_D;
  }
}

void
ges_timeline_freeze_auto_transitions (GESTimeline * timeline, gboolean freeze)
{
  GList *tmp, *trans = g_list_copy (timeline->priv->auto_transitions);
  for (tmp = trans; tmp; tmp = tmp->next) {
    GESAutoTransition *auto_transition = tmp->data;
    auto_transition->frozen = freeze;
    if (freeze == FALSE) {
      GST_LOG_OBJECT (timeline, "Un-Freezing %" GES_FORMAT,
          GES_ARGS (auto_transition->transition_clip));
      ges_auto_transition_update (auto_transition);
    } else {
      GST_LOG_OBJECT (timeline, "Freezing %" GES_FORMAT,
          GES_ARGS (auto_transition->transition_clip));
    }
  }
  g_list_free (trans);
}

static gint
_edit_auto_transition (GESTimeline * timeline, GESTimelineElement * element,
    gint64 new_layer_priority, GESEditMode mode, GESEdge edge,
    GstClockTime position, GError ** error)
{
  GList *tmp;
  guint32 layer_prio = ges_timeline_element_get_layer_priority (element);
  GESLayer *layer = ges_timeline_get_layer (timeline, layer_prio);

  if (!ges_layer_get_auto_transition (layer)) {
    gst_object_unref (layer);
    return -1;
  }

  gst_object_unref (layer);
  for (tmp = timeline->priv->auto_transitions; tmp; tmp = tmp->next) {
    GESTimelineElement *replace;
    GESAutoTransition *auto_transition = tmp->data;

    if (GES_TIMELINE_ELEMENT (auto_transition->transition) == element ||
        GES_TIMELINE_ELEMENT (auto_transition->transition_clip) == element) {
      if (auto_transition->positioning) {
        GST_ERROR_OBJECT (element, "Trying to edit an auto-transition "
            "whilst it is being positioned");
        return FALSE;
      }
      if (new_layer_priority != layer_prio) {
        GST_WARNING_OBJECT (element, "Cannot edit an auto-transition to a "
            "new layer");
        return FALSE;
      }
      if (mode != GES_EDIT_MODE_TRIM) {
        GST_WARNING_OBJECT (element, "Cannot edit an auto-transition "
            "under the edit mode %i", mode);
        return FALSE;
      }

      if (edge == GES_EDGE_END)
        replace = GES_TIMELINE_ELEMENT (auto_transition->previous_source);
      else
        replace = GES_TIMELINE_ELEMENT (auto_transition->next_source);

      GST_INFO_OBJECT (element, "Trimming %" GES_FORMAT " in place  of "
          "trimming the corresponding auto-transition", GES_ARGS (replace));
      return ges_timeline_element_edit_full (replace, -1, mode, edge,
          position, error);
    }
  }

  return -1;
}

gboolean
ges_timeline_edit (GESTimeline * timeline, GESTimelineElement * element,
    gint64 new_layer_priority, GESEditMode mode, GESEdge edge,
    guint64 position, GError ** error)
{
  GstClockTimeDiff edge_diff = (edge == GES_EDGE_END ?
      GST_CLOCK_DIFF (position, element->start + element->duration) :
      GST_CLOCK_DIFF (position, element->start));
  gint64 prio_diff = (gint64) ges_timeline_element_get_layer_priority (element)
      - new_layer_priority;
  gint res = -1;

  if ((GES_IS_TRANSITION (element) || GES_IS_TRANSITION_CLIP (element)))
    res = _edit_auto_transition (timeline, element, new_layer_priority, mode,
        edge, position, error);

  if (res != -1)
    return res;

  switch (mode) {
    case GES_EDIT_MODE_RIPPLE:
      return timeline_tree_ripple (timeline->priv->tree, element, prio_diff,
          edge_diff, edge, timeline->priv->snapping_distance, error);
    case GES_EDIT_MODE_TRIM:
      return timeline_tree_trim (timeline->priv->tree, element, prio_diff,
          edge_diff, edge, timeline->priv->snapping_distance, error);
    case GES_EDIT_MODE_NORMAL:
      return timeline_tree_move (timeline->priv->tree, element, prio_diff,
          edge_diff, edge, timeline->priv->snapping_distance, error);
    case GES_EDIT_MODE_ROLL:
      if (prio_diff != 0) {
        GST_WARNING_OBJECT (element, "Cannot roll an element to a new layer");
        return FALSE;
      }
      return timeline_tree_roll (timeline->priv->tree, element,
          edge_diff, edge, timeline->priv->snapping_distance, error);
    case GES_EDIT_MODE_SLIDE:
      GST_ERROR_OBJECT (element, "Sliding not implemented.");
      return FALSE;
  }
  return FALSE;
}

void
timeline_add_group (GESTimeline * timeline, GESGroup * group)
{
  GST_DEBUG_OBJECT (timeline, "Adding group %" GST_PTR_FORMAT, group);

  timeline->priv->groups = g_list_prepend (timeline->priv->groups,
      gst_object_ref_sink (group));

  ges_timeline_element_set_timeline (GES_TIMELINE_ELEMENT (group), timeline);
}

/**
 * timeline_emit_group_added:
 * @timeline: The #GESTimeline
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
 * @timeline: The #GESTimeline
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

static GESTrackElement *
_core_in_track (GESTrack * track, GESClip * clip)
{
  GList *tmp;
  for (tmp = GES_CONTAINER_CHILDREN (clip); tmp; tmp = tmp->next) {
    GESTrackElement *el = tmp->data;
    if (ges_track_element_is_core (el)
        && ges_track_element_get_track (el) == track) {
      return tmp->data;
    }
  }
  return NULL;
}

static GPtrArray *
select_tracks_for_object_default (GESTimeline * timeline,
    GESClip * clip, GESTrackElement * tr_object, gpointer user_data)
{
  GPtrArray *result;
  GList *tmp;
  GESTrackElement *core;

  result = g_ptr_array_new ();

  LOCK_DYN (timeline);
  for (tmp = timeline->tracks; tmp; tmp = tmp->next) {
    GESTrack *track = GES_TRACK (tmp->data);

    if ((track->type & ges_track_element_get_track_type (tr_object))) {
      if (ges_track_element_is_core (tr_object)) {
        core = _core_in_track (track, clip);
        if (core) {
          GST_WARNING_OBJECT (timeline, "The clip '%s' contains multiple "
              "core elements of the same %s track type. The core child "
              "'%s' has already been chosen arbitrarily for the track %"
              GST_PTR_FORMAT ", which means that the other core child "
              "'%s' of the same type can not be added to the track. "
              "Consider connecting to "
              "GESTimeline::select-tracks-for-objects to be able to "
              "specify which core element should land in the track",
              GES_TIMELINE_ELEMENT_NAME (clip),
              ges_track_type_name (track->type),
              GES_TIMELINE_ELEMENT_NAME (core), track,
              GES_TIMELINE_ELEMENT_NAME (tr_object));
          continue;
        }
      }
      gst_object_ref (track);
      g_ptr_array_add (result, track);
    }
  }
  UNLOCK_DYN (timeline);

  return result;
}

static GPtrArray *
_get_selected_tracks (GESTimeline * timeline, GESClip * clip,
    GESTrackElement * track_element)
{
  guint i, j;
  GPtrArray *tracks = NULL;
  GESTrack *track = NULL;

  g_signal_emit (G_OBJECT (timeline),
      ges_timeline_signals[SELECT_ELEMENT_TRACK], 0, clip, track_element,
      &track);

  if (track) {
    tracks = g_ptr_array_new ();

    g_ptr_array_add (tracks, track);
  } else {
    g_signal_emit (G_OBJECT (timeline),
        ges_timeline_signals[SELECT_TRACKS_FOR_OBJECT], 0, clip, track_element,
        &tracks);
  }

  if (tracks == NULL)
    tracks = g_ptr_array_new ();

  g_ptr_array_set_free_func (tracks, gst_object_unref);

  /* make sure unique */
  for (i = 0; i < tracks->len;) {
    GESTrack *track = GES_TRACK (g_ptr_array_index (tracks, i));

    for (j = i + 1; j < tracks->len;) {
      if (track == g_ptr_array_index (tracks, j)) {
        GST_WARNING_OBJECT (timeline, "Found the track %" GST_PTR_FORMAT
            " more than once in the return for select-tracks-for-object "
            "signal for track element %" GES_FORMAT " in clip %"
            GES_FORMAT ". Ignoring the extra track", track,
            GES_ARGS (track_element), GES_ARGS (clip));
        g_ptr_array_remove_index (tracks, j);
        /* don't increase index since the next track is in its place */
        continue;
      }
      j++;
    }

    if (ges_track_get_timeline (track) != timeline) {
      GST_WARNING_OBJECT (timeline, "The track %" GST_PTR_FORMAT
          " found in the return for select-tracks-for-object belongs "
          "to a different timeline %" GST_PTR_FORMAT ". Ignoring this "
          "track", track, ges_track_get_timeline (track));
      g_ptr_array_remove_index (tracks, i);
      /* don't increase index since the next track is in its place */
      continue;
    }
    i++;
  }

  return tracks;
}

/* returns TRUE if track element was successfully added to all the
 * selected tracks */
static gboolean
_add_track_element_to_tracks (GESTimeline * timeline, GESClip * clip,
    GESTrackElement * track_element, GError ** error)
{
  guint i;
  gboolean ret = TRUE;
  GPtrArray *tracks = _get_selected_tracks (timeline, clip, track_element);

  for (i = 0; i < tracks->len; i++) {
    GESTrack *track = GES_TRACK (g_ptr_array_index (tracks, i));
    if (!ges_clip_add_child_to_track (clip, track_element, track, error)) {
      ret = FALSE;
      if (error)
        break;
    }
  }

  g_ptr_array_unref (tracks);

  return ret;
}

static gboolean
_try_add_track_element_to_track (GESTimeline * timeline, GESClip * clip,
    GESTrackElement * track_element, GESTrack * track, GError ** error)
{
  gboolean no_error = TRUE;
  GPtrArray *tracks = _get_selected_tracks (timeline, clip, track_element);

  /* if we are trying to add the element to a newly added track, then
   * we only check whether the track list contains the newly added track,
   * if it does we add the track element to the track, or add a copy if
   * the track element is already in a track */
  if (g_ptr_array_find (tracks, track, NULL)) {
    if (!ges_clip_add_child_to_track (clip, track_element, track, error))
      no_error = FALSE;
  }

  g_ptr_array_unref (tracks);
  return no_error;
}

/* accepts NULL */
void
ges_timeline_set_moving_track_elements (GESTimeline * timeline, gboolean moving)
{
  if (timeline) {
    LOCK_DYN (timeline);
    timeline->priv->track_elements_moving = moving;
    UNLOCK_DYN (timeline);
  }
}

void
ges_timeline_set_track_selection_error (GESTimeline * timeline,
    gboolean was_error, GError * error)
{
  GESTimelinePrivate *priv;

  LOCK_DYN (timeline);

  priv = timeline->priv;
  g_clear_error (&priv->track_selection_error);
  priv->track_selection_error = error;
  priv->has_any_track_selection_error = was_error;

  UNLOCK_DYN (timeline);
}

gboolean
ges_timeline_take_track_selection_error (GESTimeline * timeline,
    GError ** error)
{
  gboolean ret;
  GESTimelinePrivate *priv;

  LOCK_DYN (timeline);

  priv = timeline->priv;
  if (error) {
    if (*error) {
      GST_ERROR_OBJECT (timeline, "Error not handled %s", (*error)->message);
      g_error_free (*error);
    }
    *error = priv->track_selection_error;
  } else if (priv->track_selection_error) {
    GST_WARNING_OBJECT (timeline, "Got track selection error: %s",
        priv->track_selection_error->message);
    g_error_free (priv->track_selection_error);
  }
  priv->track_selection_error = NULL;
  ret = priv->has_any_track_selection_error;
  priv->has_any_track_selection_error = FALSE;

  UNLOCK_DYN (timeline);

  return ret;
}

static void
clip_track_element_added_cb (GESClip * clip,
    GESTrackElement * track_element, GESTimeline * timeline)
{
  GESTrack *auto_trans_track, *new_track;
  GError *error = NULL;
  gboolean success = FALSE;

  if (timeline->priv->track_elements_moving) {
    GST_DEBUG_OBJECT (timeline, "Ignoring element added: %" GES_FORMAT
        " in %" GES_FORMAT, GES_ARGS (track_element), GES_ARGS (clip));
    return;
  }

  if (ges_track_element_get_track (track_element) != NULL) {
    GST_DEBUG_OBJECT (timeline, "Not selecting tracks for %" GES_FORMAT
        " in %" GES_FORMAT " because it already part of the track %"
        GST_PTR_FORMAT, GES_ARGS (track_element), GES_ARGS (clip),
        ges_track_element_get_track (track_element));
    return;
  }

  LOCK_DYN (timeline);
  /* take ownership of auto_transition_track. For auto-transitions, this
   * should be used exactly once! */
  auto_trans_track = timeline->priv->auto_transition_track;
  timeline->priv->auto_transition_track = NULL;
  /* don't take ownership of new_track */
  new_track = timeline->priv->new_track;
  UNLOCK_DYN (timeline);

  if (auto_trans_track) {
    /* don't use track-selection */
    success = !!ges_clip_add_child_to_track (clip, track_element,
        auto_trans_track, &error);
    gst_object_unref (auto_trans_track);
  } else {
    if (new_track)
      success = _try_add_track_element_to_track (timeline, clip, track_element,
          new_track, &error);
    else
      success = _add_track_element_to_tracks (timeline, clip, track_element,
          &error);
  }

  if (error || !success) {
    if (!error)
      GST_WARNING_OBJECT (timeline, "Track selection failed for %" GES_FORMAT,
          GES_ARGS (track_element));
    ges_timeline_set_track_selection_error (timeline, TRUE, error);
  }
}

static void
clip_track_element_removed_cb (GESClip * clip,
    GESTrackElement * track_element, GESTimeline * timeline)
{
  GESTrack *track = ges_track_element_get_track (track_element);

  if (timeline->priv->track_elements_moving) {
    GST_DEBUG_OBJECT (timeline, "Ignoring element removed (%" GST_PTR_FORMAT
        " in %" GST_PTR_FORMAT, track_element, clip);

    return;
  }

  if (track) {
    /* if we have non-core elements in the same track, they should be
     * removed from them to preserve the rule that a non-core can only be
     * in the same track as a core element from the same clip */
    if (ges_track_element_is_core (track_element))
      ges_clip_empty_from_track (clip, track);
    ges_track_remove_element (track, track_element);
  }
}

static void
track_element_added_cb (GESTrack * track, GESTrackElement * element,
    GESTimeline * timeline)
{
  if (GES_IS_SOURCE (element))
    timeline_tree_create_transitions_for_track_element (timeline->priv->tree,
        element, ges_timeline_find_auto_transition);
}

/* returns TRUE if no errors in adding to tracks */
static gboolean
_add_clip_children_to_tracks (GESTimeline * timeline, GESClip * clip,
    gboolean add_core, GESTrack * new_track, GList * blacklist, GError ** error)
{
  GList *tmp, *children;
  gboolean no_errors = TRUE;

  /* list of children may change if some are copied into tracks */
  children = ges_container_get_children (GES_CONTAINER (clip), FALSE);
  for (tmp = children; tmp; tmp = tmp->next) {
    GESTrackElement *el = tmp->data;
    if (ges_track_element_is_core (el) != add_core)
      continue;
    if (g_list_find (blacklist, el))
      continue;
    if (ges_track_element_get_track (el) == NULL) {
      gboolean res;
      if (new_track)
        res = _try_add_track_element_to_track (timeline, clip, el, new_track,
            error);
      else
        res = _add_track_element_to_tracks (timeline, clip, el, error);
      if (!res) {
        no_errors = FALSE;
        if (error)
          goto done;
      }
    }
  }

done:
  g_list_free_full (children, gst_object_unref);

  return no_errors;
}

/* returns TRUE if no errors in adding to tracks */
static gboolean
add_object_to_tracks (GESTimeline * timeline, GESClip * clip,
    GESTrack * new_track, GError ** error)
{
  GList *tracks, *tmp, *list, *created, *just_added = NULL;
  gboolean no_errors = TRUE;

  GST_DEBUG_OBJECT (timeline, "Creating %" GST_PTR_FORMAT
      " trackelements and adding them to our tracks", clip);

  LOCK_DYN (timeline);
  tracks =
      g_list_copy_deep (timeline->tracks, (GCopyFunc) gst_object_ref, NULL);
  timeline->priv->new_track = new_track ? gst_object_ref (new_track) : NULL;
  UNLOCK_DYN (timeline);

  /* create core elements */
  for (tmp = tracks; tmp; tmp = tmp->next) {
    GESTrack *track = GES_TRACK (tmp->data);
    if (new_track && track != new_track)
      continue;

    list = ges_clip_create_track_elements (clip, track->type);
    /* just_added only used for pointer comparison, so safe to include
     * elements that may be destroyed because they fail to be added to
     * the clip */
    just_added = g_list_concat (just_added, list);

    for (created = list; created; created = created->next) {
      GESTimelineElement *el = created->data;

      gst_object_ref (el);

      /* make track selection be handled by clip_track_element_added_cb
       * This is needed for backward-compatibility: when adding a clip to
       * a layer, the track is set for the core elements of the clip
       * during the child-added signal emission, just before the user's
       * own connection.
       * NOTE: for the children that have not just been created, they
       * are already part of the clip and so child-added will not be
       * released. And when a child is selected for multiple tracks, their
       * copy will be added to the clip before the track is selected, so
       * the track will not be set in the child-added signal */
      ges_timeline_set_track_selection_error (timeline, FALSE, NULL);
      ges_clip_set_add_error (clip, NULL);
      if (!ges_container_add (GES_CONTAINER (clip), el)) {
        no_errors = FALSE;
        if (!error)
          GST_ERROR_OBJECT (clip, "Could not add the core element %s "
              "to the clip", el->name);
      }
      gst_object_unref (el);
      ges_clip_take_add_error (clip, error);

      if (error && !no_errors)
        goto done;

      if (ges_timeline_take_track_selection_error (timeline, error)) {
        no_errors = FALSE;
        if (error)
          goto done;
        /* else, carry on as much as we can */
      }
    }
  }

  /* set the tracks for the other children, with core elements first to
   * make sure the non-core can be placed above them in the track (a
   * non-core can not be in a track by itself) */
  /* include just_added as a blacklist to ensure we do not try the track
   * selection a second time when track selection returns no tracks */
  if (!_add_clip_children_to_tracks (timeline, clip, TRUE, new_track,
          just_added, error)) {
    no_errors = FALSE;
    if (error)
      goto done;
  }

  if (!_add_clip_children_to_tracks (timeline, clip, FALSE, new_track,
          just_added, error)) {
    no_errors = FALSE;
    if (error)
      goto done;
  }

done:
  g_list_free_full (tracks, gst_object_unref);

  LOCK_DYN (timeline);
  gst_clear_object (&timeline->priv->new_track);
  UNLOCK_DYN (timeline);

  g_list_free (just_added);

  return no_errors;
}

static void
layer_active_changed_cb (GESLayer * layer, gboolean active G_GNUC_UNUSED,
    GPtrArray * tracks G_GNUC_UNUSED, GESTimeline * timeline)
{
  timeline_tree_reset_layer_active (timeline->priv->tree, layer);
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

/* returns TRUE if selecting of tracks did not error */
gboolean
ges_timeline_add_clip (GESTimeline * timeline, GESClip * clip, GError ** error)
{
  GESProject *project;
  gboolean ret;

  ges_timeline_element_set_timeline (GES_TIMELINE_ELEMENT (clip), timeline);

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

  GST_DEBUG ("Making sure that the asset is in our project");
  project =
      GES_PROJECT (ges_extractable_get_asset (GES_EXTRACTABLE (timeline)));
  ges_project_add_asset (project,
      ges_extractable_get_asset (GES_EXTRACTABLE (clip)));

  if (ges_clip_is_moving_from_layer (clip)) {
    GST_DEBUG ("Clip %p moving from one layer to another, not creating "
        "TrackElement", clip);
    /* timeline-tree handles creation of auto-transitions */
    ret = TRUE;
  } else {
    ret = add_object_to_tracks (timeline, clip, NULL, error);
  }

  GST_DEBUG ("Done");

  return ret;
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

void
ges_timeline_remove_clip (GESTimeline * timeline, GESClip * clip)
{
  GList *tmp;

  if (ges_clip_is_moving_from_layer (clip)) {
    GST_DEBUG ("Clip %p is moving from a layer to another, not doing"
        " anything on it", clip);
    return;
  }

  GST_DEBUG_OBJECT (timeline, "Clip %" GES_FORMAT " removed from layer",
      GES_ARGS (clip));

  LOCK_DYN (timeline);
  for (tmp = timeline->tracks; tmp; tmp = tmp->next)
    ges_clip_empty_from_track (clip, tmp->data);
  UNLOCK_DYN (timeline);

  g_signal_handlers_disconnect_by_func (clip, clip_track_element_added_cb,
      timeline);
  g_signal_handlers_disconnect_by_func (clip, clip_track_element_removed_cb,
      timeline);

  ges_timeline_element_set_timeline (GES_TIMELINE_ELEMENT (clip), NULL);

  GST_DEBUG ("Done");
}

static gboolean
update_stream_object (TrackPrivate * tr_priv)
{
  gboolean res = FALSE;
  GstStreamType type = GST_STREAM_TYPE_UNKNOWN;
  gchar *stream_id;

  g_object_get (tr_priv->track, "id", &stream_id, NULL);
  if (tr_priv->track->type == GES_TRACK_TYPE_VIDEO)
    type = GST_STREAM_TYPE_VIDEO;
  if (tr_priv->track->type == GES_TRACK_TYPE_AUDIO)
    type = GST_STREAM_TYPE_AUDIO;

  if (!tr_priv->stream ||
      g_strcmp0 (stream_id, gst_stream_get_stream_id (tr_priv->stream))) {
    res = TRUE;
    gst_object_replace ((GstObject **) & tr_priv->stream,
        (GstObject *) gst_stream_new (stream_id,
            (GstCaps *) ges_track_get_caps (tr_priv->track),
            type, GST_STREAM_FLAG_NONE)
        );
  }

  g_free (stream_id);

  return res;
}

static GstPadProbeReturn
_pad_probe_cb (GstPad * mixer_pad, GstPadProbeInfo * info,
    TrackPrivate * tr_priv)
{
  GstEvent *event = GST_PAD_PROBE_INFO_EVENT (info);
  GESTimeline *timeline = tr_priv->timeline;

  if (GST_EVENT_TYPE (event) == GST_EVENT_STREAM_START) {
    LOCK_DYN (timeline);
    if (timeline->priv->stream_start_group_id == -1) {
      if (!gst_event_parse_group_id (event,
              &timeline->priv->stream_start_group_id))
        timeline->priv->stream_start_group_id = gst_util_group_id_next ();
    }

    gst_event_unref (event);
    event = info->data =
        gst_event_new_stream_start (gst_stream_get_stream_id (tr_priv->stream));
    gst_event_set_stream (event, tr_priv->stream);
    gst_event_set_group_id (event, timeline->priv->stream_start_group_id);
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
      (GstPadProbeCallback) _pad_probe_cb, tr_priv, NULL);

  UNLOCK_DYN (tr_priv->timeline);
}

gboolean
timeline_add_element (GESTimeline * timeline, GESTimelineElement * element)
{
  /* FIXME: handle NULL element->name */
  GESTimelineElement *same_name =
      g_hash_table_lookup (timeline->priv->all_elements,
      element->name);

  GST_DEBUG_OBJECT (timeline, "Adding element: %s", element->name);
  if (same_name) {
    GST_ERROR_OBJECT (timeline, "%s Already in the timeline %" GST_PTR_FORMAT,
        element->name, same_name);
    return FALSE;
  }

  /* FIXME: why is the hash table using the name of the element, rather than
   * the pointer to the element itself as the key? This makes it awkward
   * to change the name of an element after it has been added. See
   * ges_timeline_element_set_name. It means we have to remove and then
   * re-add the element. */
  g_hash_table_insert (timeline->priv->all_elements,
      ges_timeline_element_get_name (element), gst_object_ref (element));

  timeline_tree_track_element (timeline->priv->tree, element);
  if (GES_IS_SOURCE (element)) {
    ges_source_set_rendering_smartly (GES_SOURCE (element),
        timeline->priv->rendering_smartly);
  }

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

  LOCK_DYN (timeline);
  for (tmp = timeline->tracks; tmp; tmp = tmp->next) {
    track_resort_and_fill_gaps (tmp->data);
  }
  UNLOCK_DYN (timeline);
}

GNode *
timeline_get_tree (GESTimeline * timeline)
{
  return timeline->priv->tree;
}

void
ges_timeline_set_smart_rendering (GESTimeline * timeline,
    gboolean rendering_smartly)
{
  if (rendering_smartly) {
    GList *tmp;

    for (tmp = timeline->tracks; tmp; tmp = tmp->next) {
      if (ges_track_get_mixing (tmp->data)) {
        GST_INFO_OBJECT (timeline, "Smart rendering will not"
            " work as track %" GST_PTR_FORMAT " is doing mixing", tmp->data);
      } else {
        ges_track_set_smart_rendering (tmp->data, rendering_smartly);
      }
    }
  }
  timeline_tree_set_smart_rendering (timeline->priv->tree, rendering_smartly);
  timeline->priv->rendering_smartly = rendering_smartly;
}

gboolean
ges_timeline_get_smart_rendering (GESTimeline * timeline)
{
  return timeline->priv->rendering_smartly;
}

GstStreamCollection *
ges_timeline_get_stream_collection (GESTimeline * timeline)
{
  return gst_object_ref (timeline->priv->stream_collection);
}

/**** API *****/
/**
 * ges_timeline_new:
 *
 * Creates a new empty timeline.
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
 * @uri: The URI to load from
 * @error: An error to be set if loading fails, or
 * %NULL to ignore
 *
 * Creates a timeline from the given URI.
 *
 * Returns: (transfer floating): A new timeline if the uri was loaded
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
 * @timeline: An empty #GESTimeline into which to load the formatter
 * @uri: The URI to load from
 * @error: An error to be set if loading fails, or
 * %NULL to ignore
 *
 * Loads the contents of URI into the timeline.
 *
 * Returns: %TRUE if the timeline was loaded successfully from @uri.
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
 * @timeline: The #GESTimeline
 * @uri: The location to save to
 * @formatter_asset: (allow-none): The formatter asset to use, or %NULL
 * @overwrite: %TRUE to overwrite file if it exists
 * @error: An error to be set if saving fails, or
 * %NULL to ignore
 *
 * Saves the timeline to the given location. If @formatter_asset is %NULL,
 * the method will attempt to save in the same format the timeline was
 * loaded from, before defaulting to the formatter with highest rank.
 *
 * Returns: %TRUE if @timeline was successfully saved to @uri.
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
 * @timeline: The #GESTimeline
 *
 * Get the list of #GESGroup-s present in the timeline.
 *
 * Returns: (transfer none) (element-type GESGroup): The list of
 * groups that contain clips present in @timeline's layers.
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
 * @timeline: The #GESTimeline
 *
 * Append a newly created layer to the timeline. The layer will
 * be added at the lowest #GESLayer:priority (numerically, the highest).
 *
 * Returns: (transfer none): The newly created layer.
 */
GESLayer *
ges_timeline_append_layer (GESTimeline * timeline)
{
  GList *tmp;
  guint32 priority;
  GESLayer *layer;

  g_return_val_if_fail (GES_IS_TIMELINE (timeline), NULL);
  CHECK_THREAD (timeline);

  layer = ges_layer_new ();

  priority = 0;
  for (tmp = timeline->layers; tmp; tmp = tmp->next)
    priority = MAX (priority, ges_layer_get_priority (tmp->data) + 1);

  ges_layer_set_priority (layer, priority);

  ges_timeline_add_layer (timeline, layer);

  return layer;
}

/**
 * ges_timeline_add_layer:
 * @timeline: The #GESTimeline
 * @layer: (transfer floating): The layer to add
 *
 * Add a layer to the timeline.
 *
 * If the layer contains #GESClip-s, then this may trigger the creation of
 * their core track element children for the timeline's tracks, and the
 * placement of the clip's children in the tracks of the timeline using
 * #GESTimeline::select-tracks-for-object. Some errors may occur if this
 * would break one of the configuration rules of the timeline in one of
 * its tracks. In such cases, some track elements would fail to be added
 * to their tracks, but this method would still return %TRUE. As such, it
 * is advised that you only add clips to layers that already part of a
 * timeline. In such situations, ges_layer_add_clip() is able to fail if
 * adding the clip would cause such an error.
 *
 * Deprecated: 1.18: This method requires you to ensure the layer's
 * #GESLayer:priority will be unique to the timeline. Use
 * ges_timeline_append_layer() and ges_timeline_move_layer() instead.
 *
 * Returns: %TRUE if @layer was properly added.
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

  /* FIXME: ensure the layer->priority does not conflict with an existing
   * layer in the timeline. Currently can add several layers with equal
   * layer priorities */

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
  g_signal_connect (layer, "notify::priority",
      G_CALLBACK (layer_priority_changed_cb), timeline);
  g_signal_connect (layer, "notify::auto-transition",
      G_CALLBACK (layer_auto_transition_changed_cb), timeline);
  g_signal_connect_after (layer, "active-changed",
      G_CALLBACK (layer_active_changed_cb), timeline);

  GST_DEBUG ("Done adding layer, emitting 'layer-added' signal");
  g_signal_emit (timeline, ges_timeline_signals[LAYER_ADDED], 0, layer);

  /* add any existing clips to the timeline */
  objects = ges_layer_get_clips (layer);
  for (tmp = objects; tmp; tmp = tmp->next)
    ges_timeline_add_clip (timeline, tmp->data, NULL);
  g_list_free_full (objects, gst_object_unref);

  return TRUE;
}

/**
 * ges_timeline_remove_layer:
 * @timeline: The #GESTimeline
 * @layer: The layer to remove
 *
 * Removes a layer from the timeline.
 *
 * Returns: %TRUE if @layer was properly removed.
 */

gboolean
ges_timeline_remove_layer (GESTimeline * timeline, GESLayer * layer)
{
  GList *layer_objects, *tmp;

  g_return_val_if_fail (GES_IS_TIMELINE (timeline), FALSE);
  g_return_val_if_fail (GES_IS_LAYER (layer), FALSE);

  if (!timeline->priv->disposed)
    CHECK_THREAD (timeline);

  GST_DEBUG ("timeline:%p, layer:%p", timeline, layer);

  if (G_UNLIKELY (!g_list_find (timeline->layers, layer))) {
    GST_WARNING ("Layer doesn't belong to this timeline");
    return FALSE;
  }

  /* remove objects from any private data structures */

  layer_objects = ges_layer_get_clips (layer);
  for (tmp = layer_objects; tmp; tmp = tmp->next)
    ges_timeline_remove_clip (timeline, tmp->data);
  g_list_free_full (layer_objects, gst_object_unref);

  /* Disconnect signals */
  GST_DEBUG ("Disconnecting signal callbacks");
  g_signal_handlers_disconnect_by_func (layer, layer_priority_changed_cb,
      timeline);
  g_signal_handlers_disconnect_by_func (layer,
      layer_auto_transition_changed_cb, timeline);
  g_signal_handlers_disconnect_by_func (layer, layer_active_changed_cb,
      timeline);

  timeline->layers = g_list_remove (timeline->layers, layer);
  ges_layer_set_timeline (layer, NULL);
  /* FIXME: we should resync the layer priorities */

  g_signal_emit (timeline, ges_timeline_signals[LAYER_REMOVED], 0, layer);

  gst_object_unref (layer);

  return TRUE;
}

/**
 * ges_timeline_add_track:
 * @timeline: The #GESTimeline
 * @track: (transfer floating): The track to add
 *
 * Add a track to the timeline.
 *
 * If the timeline already contains clips, then this may trigger the
 * creation of their core track element children for the track, and the
 * placement of the clip's children in the track of the timeline using
 * #GESTimeline::select-tracks-for-object. Some errors may occur if this
 * would break one of the configuration rules for the timeline in the
 * track. In such cases, some track elements would fail to be added to the
 * track, but this method would still return %TRUE. As such, it is advised
 * that you avoid adding tracks to timelines that already contain clips.
 *
 * Returns: %TRUE if @track was properly added.
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
  LOCK_DYN (timeline);
  if (G_UNLIKELY (g_list_find (timeline->tracks, (gconstpointer) track))) {
    UNLOCK_DYN (timeline);
    GST_WARNING ("Track is already controlled by this timeline");
    gst_object_ref_sink (track);
    gst_object_unref (track);
    return FALSE;
  }

  /* Add the track to ourself (as a GstBin)
   * Reference is stolen ! */
  if (G_UNLIKELY (!gst_bin_add (GST_BIN (timeline), GST_ELEMENT (track)))) {
    UNLOCK_DYN (timeline);
    GST_WARNING ("Couldn't add track to ourself (GST)");
    return FALSE;
  }

  tr_priv = g_new0 (TrackPrivate, 1);
  tr_priv->timeline = timeline;
  tr_priv->track = track;
  tr_priv->track_element_added_sigid = g_signal_connect (track,
      "track-element-added", G_CALLBACK (track_element_added_cb), timeline);

  update_stream_object (tr_priv);
  gst_stream_collection_add_stream (timeline->priv->stream_collection,
      gst_object_ref (tr_priv->stream));

  /* Add the track to the list of tracks we track */
  timeline->priv->priv_tracks = g_list_append (timeline->priv->priv_tracks,
      tr_priv);
  timeline->tracks = g_list_append (timeline->tracks, track);

  /* Inform the track that it's currently being used by ourself */
  ges_track_set_timeline (track, timeline);

  GST_DEBUG ("Done adding track, emitting 'track-added' signal");

  _ghost_track_srcpad (tr_priv);
  UNLOCK_DYN (timeline);

  /* emit 'track-added' */
  g_signal_emit (timeline, ges_timeline_signals[TRACK_ADDED], 0, track);

  /* ensure that each existing clip has the opportunity to create a
   * track element for this track*/

  for (tmp = timeline->layers; tmp; tmp = tmp->next) {
    GList *objects, *obj;
    objects = ges_layer_get_clips (tmp->data);

    for (obj = objects; obj; obj = obj->next)
      add_object_to_tracks (timeline, obj->data, track, NULL);

    g_list_free_full (objects, gst_object_unref);
  }

  /* FIXME Check if we should rollback if we can't sync state */
  gst_element_sync_state_with_parent (GST_ELEMENT (track));
  g_object_set (track, "message-forward", TRUE, NULL);

  return TRUE;
}

/**
 * ges_timeline_remove_track:
 * @timeline: The #GESTimeline
 * @track: The track to remove
 *
 * Remove a track from the timeline.
 *
 * Returns: %TRUE if @track was properly removed.
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
  gst_object_unref (tr_priv->pad);
  priv->priv_tracks = g_list_remove (priv->priv_tracks, tr_priv);
  UNLOCK_DYN (timeline);

  /* empty track of all elements that belong to the timeline's clips */
  /* elements with no parent can stay in the track, but their timeline
   * will be set to NULL when the track's timeline is set to NULL */

  for (tmp = timeline->layers; tmp; tmp = tmp->next) {
    GList *clips, *clip;
    clips = ges_layer_get_clips (tmp->data);

    for (clip = clips; clip; clip = clip->next)
      ges_clip_empty_from_track (clip->data, track);

    g_list_free_full (clips, gst_object_unref);
  }

  timeline->tracks = g_list_remove (timeline->tracks, track);
  ges_track_set_timeline (track, NULL);

  /* Remove ghost pad */
  if (tr_priv->ghostpad) {
    GST_DEBUG ("Removing ghostpad");
    gst_pad_set_active (tr_priv->ghostpad, FALSE);
    gst_ghost_pad_set_target ((GstGhostPad *) tr_priv->ghostpad, NULL);
    gst_element_remove_pad (GST_ELEMENT (timeline), tr_priv->ghostpad);
  }

  /* Signal track removal to all layers/objects */
  g_signal_emit (timeline, ges_timeline_signals[TRACK_REMOVED], 0, track);

  /* remove track from our bin */
  gst_object_ref (track);
  if (G_UNLIKELY (!gst_bin_remove (GST_BIN (timeline), GST_ELEMENT (track)))) {
    GST_WARNING ("Couldn't remove track to ourself (GST)");
    gst_object_unref (track);
    return FALSE;
  }

  g_signal_handler_disconnect (track, tr_priv->track_element_added_sigid);

  /* set track state to NULL */
  gst_element_set_state (GST_ELEMENT (track), GST_STATE_NULL);

  gst_object_unref (track);

  g_free (tr_priv);

  return TRUE;
}

/**
 * ges_timeline_get_track_for_pad:
 * @timeline: The #GESTimeline
 * @pad: A pad
 *
 * Search for the #GESTrack corresponding to the given timeline's pad.
 *
 * Returns: (transfer none) (nullable): The track corresponding to @pad,
 * or %NULL if there is an error.
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
 * @track: A track
 *
 * Search for the #GstPad corresponding to the given timeline's track.
 * You can link to this pad to receive the output data of the given track.
 *
 * Returns: (transfer none) (nullable): The pad corresponding to @track,
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
 * @timeline: The #GESTimeline
 *
 * Get the list of #GESTrack-s used by the timeline.
 *
 * Returns: (transfer full) (element-type GESTrack): The list of tracks
 * used by @timeline.
 */
GList *
ges_timeline_get_tracks (GESTimeline * timeline)
{
  GList *res = NULL;
  g_return_val_if_fail (GES_IS_TIMELINE (timeline), NULL);

  LOCK_DYN (timeline);
  res = g_list_copy_deep (timeline->tracks, (GCopyFunc) gst_object_ref, NULL);
  UNLOCK_DYN (timeline);

  return res;
}

/**
 * ges_timeline_get_layers:
 * @timeline: The #GESTimeline
 *
 * Get the list of #GESLayer-s present in the timeline.
 *
 * Returns: (transfer full) (element-type GESLayer): The list of
 * layers present in @timeline sorted by priority.
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

  if (timeline->priv->commit_frozen) {
    GST_DEBUG_OBJECT (timeline, "commit locked");
    timeline->priv->commit_delayed = TRUE;
    return res;
  }

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
    GstStreamCollection *collection = gst_stream_collection_new (NULL);

    LOCK_DYN (timeline);
    for (tmp = timeline->tracks; tmp; tmp = tmp->next) {
      TrackPrivate *tr_priv =
          g_list_find_custom (timeline->priv->priv_tracks, tmp->data,
          (GCompareFunc) custom_find_track)->data;

      update_stream_object (tr_priv);
      gst_stream_collection_add_stream (collection,
          gst_object_ref (tr_priv->stream));
      g_signal_connect (tmp->data, "commited", G_CALLBACK (track_commited_cb),
          timeline);
      if (!ges_track_commit (GES_TRACK (tmp->data)))
        res = FALSE;
    }

    gst_object_unref (timeline->priv->stream_collection);
    timeline->priv->stream_collection = collection;
    UNLOCK_DYN (timeline);
  }

  return res;
}

/**
 * ges_timeline_commit:
 * @timeline: A #GESTimeline
 *
 * Commit all the pending changes of the clips contained in the
 * timeline.
 *
 * When changes happen in a timeline, they are not immediately executed
 * internally, in a way that effects the output data of the timeline. You
 * should call this method when you are done with a set of changes and you
 * want them to be executed.
 *
 * Any pending changes will be executed in the backend. The
 * #GESTimeline::commited signal will be emitted once this has completed.
 * You should not try to change the state of the timeline, seek it or add
 * tracks to it before receiving this signal. You can use
 * ges_timeline_commit_sync() if you do not want to perform other tasks in
 * the mean time.
 *
 * Note that all the pending changes will automatically be executed when
 * the timeline goes from #GST_STATE_READY to #GST_STATE_PAUSED, which is
 * usually triggered by a corresponding state changes in a containing
 * #GESPipeline.
 *
 * Returns: %TRUE if pending changes were committed, or %FALSE if nothing
 * needed to be committed.
 */
gboolean
ges_timeline_commit (GESTimeline * timeline)
{
  gboolean ret;
  GstStreamCollection *pcollection = timeline->priv->stream_collection;

  g_return_val_if_fail (GES_IS_TIMELINE (timeline), FALSE);

  LOCK_DYN (timeline);
  ret = ges_timeline_commit_unlocked (timeline);
  UNLOCK_DYN (timeline);

  if (pcollection != timeline->priv->stream_collection) {
    gst_element_post_message ((GstElement *) timeline,
        gst_message_new_stream_collection ((GstObject *) timeline,
            timeline->priv->stream_collection));
  }

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
 * @timeline: A #GESTimeline
 *
 * Commit all the pending changes of the clips contained in the
 * timeline and wait for the changes to complete.
 *
 * See ges_timeline_commit().
 *
 * Returns: %TRUE if pending changes were committed, or %FALSE if nothing
 * needed to be committed.
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
 * ges_timeline_freeze_commit:
 * @timeline: The #GESTimeline
 *
 * Freezes the timeline from being committed. This is usually needed while the
 * timeline is being rendered to ensure that not change to the timeline are
 * taken into account during that moment. Once the rendering is done, you
 * should call #ges_timeline_thaw_commit so that committing becomes possible
 * again and any call to `commit()` that happened during the rendering is
 * actually taken into account.
 *
 * Since: 1.20
 *
 */
void
ges_timeline_freeze_commit (GESTimeline * timeline)
{
  LOCK_DYN (timeline);
  timeline->priv->commit_frozen = TRUE;
  UNLOCK_DYN (timeline);
}

/**
 * ges_timeline_thaw_commit:
 * @timeline: The #GESTimeline
 *
 * Thaw the timeline so that comiting becomes possible
 * again and any call to `commit()` that happened during the rendering is
 * actually taken into account.
 *
 * Since: 1.20
 *
 */
void
ges_timeline_thaw_commit (GESTimeline * timeline)
{
  LOCK_DYN (timeline);
  timeline->priv->commit_frozen = FALSE;
  UNLOCK_DYN (timeline);
  if (timeline->priv->commit_delayed) {
    ges_timeline_commit (timeline);
    timeline->priv->commit_delayed = FALSE;
  }
}

/**
 * ges_timeline_get_duration:
 * @timeline: The #GESTimeline
 *
 * Get the current #GESTimeline:duration of the timeline
 *
 * Returns: The current duration of @timeline.
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
 * @timeline: The #GESTimeline
 *
 * Gets #GESTimeline:auto-transition for the timeline.
 *
 * Returns: The auto-transition of @self.
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
 * @timeline: The #GESTimeline
 * @auto_transition: Whether transitions should be automatically added
 * to @timeline's layers
 *
 * Sets #GESTimeline:auto-transition for the timeline. This will also set
 * the corresponding #GESLayer:auto-transition for all of the timeline's
 * layers to the same value. See ges_layer_set_auto_transition() if you
 * wish to set the layer's #GESLayer:auto-transition individually.
 */
void
ges_timeline_set_auto_transition (GESTimeline * timeline,
    gboolean auto_transition)
{
  GList *layers;
  GESLayer *layer;

  g_return_if_fail (GES_IS_TIMELINE (timeline));
  g_return_if_fail (!timeline->priv->disable_edit_apis);
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
 * @timeline: The #GESTimeline
 *
 * Gets the #GESTimeline:snapping-distance for the timeline.
 *
 * Returns: The snapping distance (in nanoseconds) of @timeline.
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
 * @timeline: The #GESTimeline
 * @snapping_distance: The snapping distance to use (in nanoseconds)
 *
 * Sets #GESTimeline:snapping-distance for the timeline. This new value
 * will only effect future snappings and will not be used to snap the
 * current element positions within the timeline.
 */
void
ges_timeline_set_snapping_distance (GESTimeline * timeline,
    GstClockTime snapping_distance)
{
  g_return_if_fail (GES_IS_TIMELINE (timeline));
  g_return_if_fail (GST_CLOCK_TIME_IS_VALID (snapping_distance));
  CHECK_THREAD (timeline);

  timeline->priv->snapping_distance = snapping_distance;
}

/**
 * ges_timeline_get_element:
 * @timeline: The #GESTimeline
 * @name: The name of the element to find
 *
 * Gets the element contained in the timeline with the given name.
 *
 * Returns: (transfer full) (nullable): The timeline element in @timeline
 * with the given @name, or %NULL if it was not found.
 */
GESTimelineElement *
ges_timeline_get_element (GESTimeline * timeline, const gchar * name)
{
  GESTimelineElement *ret;

  g_return_val_if_fail (GES_IS_TIMELINE (timeline), NULL);
  CHECK_THREAD (timeline);

  /* FIXME: handle NULL name */
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
 * @timeline: The #GESTimeline
 *
 * Check whether the timeline is empty or not.
 *
 * Returns: %TRUE if @timeline is empty.
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
 * @timeline: The #GESTimeline to retrieve a layer from
 * @priority: The priority/index of the layer to find
 *
 * Retrieve the layer whose index in the timeline matches the given
 * priority.
 *
 * Returns: (transfer full) (nullable): The layer with the given
 * @priority, or %NULL if none was found.
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

gboolean
ges_timeline_layer_priority_in_gap (GESTimeline * timeline, guint priority)
{
  GList *tmp;

  CHECK_THREAD (timeline);

  for (tmp = timeline->layers; tmp; tmp = tmp->next) {
    GESLayer *layer = GES_LAYER (tmp->data);
    guint tmp_priority = ges_layer_get_priority (layer);

    if (tmp_priority == priority)
      return FALSE;
    else if (tmp_priority > priority)
      return TRUE;
  }

  return FALSE;
}

/**
 * ges_timeline_paste_element:
 * @timeline: The #GESTimeline onto which @element should be pasted
 * @element: The element to paste
 * @position: The position in the timeline @element should be pasted to,
 * i.e. the #GESTimelineElement:start value for the pasted element.
 * @layer_priority: The layer into which the element should be pasted.
 * -1 means paste to the same layer from which @element has been copied from
 *
 * Paste an element inside the timeline. @element **must** be the return of
 * ges_timeline_element_copy() with `deep=TRUE`,
 * and it should not be changed before pasting. @element itself is not
 * placed in the timeline, instead a new element is created, alike to the
 * originally copied element. Note that the originally copied element must
 * also lie within @timeline, at both the point of copying and pasting.
 *
 * Pasting may fail if it would place the timeline in an unsupported
 * configuration.
 *
 * After calling this function @element should not be used. In particular,
 * @element can **not** be pasted again. Instead, you can copy the
 * returned element and paste that copy (although, this is only possible
 * if the paste was successful).
 *
 * See also ges_timeline_element_paste().
 *
 * Returns: (transfer full) (nullable): The newly created element, or
 * %NULL if pasting fails.
 */
GESTimelineElement *
ges_timeline_paste_element (GESTimeline * timeline,
    GESTimelineElement * element, GstClockTime position, gint layer_priority)
{
  GESTimelineElement *res, *copied_from;
  GESTimelineElementClass *element_class;

  g_return_val_if_fail (GES_IS_TIMELINE (timeline), FALSE);
  g_return_val_if_fail (GES_IS_TIMELINE_ELEMENT (element), FALSE);
  g_return_val_if_fail (GST_CLOCK_TIME_IS_VALID (position), FALSE);
  CHECK_THREAD (timeline);

  element_class = GES_TIMELINE_ELEMENT_GET_CLASS (element);
  /* steal ownership of the copied element */
  copied_from = ges_timeline_element_get_copied_from (element);

  if (!copied_from) {
    GST_ERROR_OBJECT (element, "Is not being 'deeply' copied!");

    return NULL;
  }

  if (!element_class->paste) {
    GST_ERROR_OBJECT (element, "No paste vmethod implemented");
    gst_object_unref (copied_from);
    return NULL;
  }

  /*
   * Currently the API only supports pasting onto the same layer from which
   * the @element has been copied from, i.e., @layer_priority needs to be -1.
   */
  if (layer_priority != -1) {
    GST_WARNING_OBJECT (timeline,
        "Only -1 value for layer priority is supported");
    gst_object_unref (copied_from);
    return NULL;
  }

  res = element_class->paste (element, copied_from, position);

  gst_object_unref (copied_from);

  return res ? g_object_ref_sink (res) : res;
}

/**
 * ges_timeline_move_layer:
 * @timeline: A #GESTimeline
 * @layer: A layer within @timeline, whose priority should be changed
 * @new_layer_priority: The new index for @layer
 *
 * Moves a layer within the timeline to the index given by
 * @new_layer_priority.
 * An index of 0 corresponds to the layer with the highest priority in a
 * timeline. If @new_layer_priority is greater than the number of layers
 * present in the timeline, it will become the lowest priority layer.
 *
 * Since: 1.16
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

/**
 * ges_timeline_get_frame_time:
 * @self: The self on which to retrieve the timestamp for @frame_number
 * @frame_number: The frame number to get the corresponding timestamp of in the
 *                timeline coordinates
 *
 * This method allows you to convert a timeline output frame number into a
 * timeline #GstClockTime. For example, this time could be used to seek to a
 * particular frame in the timeline's output, or as the edit position for
 * an element within the timeline.
 *
 * Returns: The timestamp corresponding to @frame_number in the output of @self.
 *
 * Since: 1.18
 */
GstClockTime
ges_timeline_get_frame_time (GESTimeline * self, GESFrameNumber frame_number)
{
  gint fps_n, fps_d;

  g_return_val_if_fail (GES_IS_TIMELINE (self), GST_CLOCK_TIME_NONE);
  g_return_val_if_fail (GES_FRAME_NUMBER_IS_VALID (frame_number),
      GST_CLOCK_TIME_NONE);

  timeline_get_framerate (self, &fps_n, &fps_d);

  return gst_util_uint64_scale_ceil (frame_number, fps_d * GST_SECOND, fps_n);
}

/**
 * ges_timeline_get_frame_at:
 * @self: A #GESTimeline
 * @timestamp: The timestamp to get the corresponding frame number of
 *
 * This method allows you to convert a timeline #GstClockTime into its
 * corresponding #GESFrameNumber in the timeline's output.
 *
 * Returns: The frame number @timestamp corresponds to.
 *
 * Since: 1.18
 */
GESFrameNumber
ges_timeline_get_frame_at (GESTimeline * self, GstClockTime timestamp)
{
  gint fps_n, fps_d;

  g_return_val_if_fail (GES_IS_TIMELINE (self), GES_FRAME_NUMBER_NONE);
  g_return_val_if_fail (GST_CLOCK_TIME_IS_VALID (timestamp),
      GES_FRAME_NUMBER_NONE);

  timeline_get_framerate (self, &fps_n, &fps_d);

  return gst_util_uint64_scale (timestamp, fps_n, fps_d * GST_SECOND);
}

/**
 * ges_timeline_disable_edit_apis:
 * @self: A #GESTimeline
 * @disable_edit_apis: %TRUE to disable all the edit APIs so the user is in full
 * control of ensuring timeline state validity %FALSE otherwise.
 *
 * WARNING: When using that mode, GES won't guarantee the coherence of the
 * timeline. You need to ensure that the rules described in the [Overlaps and
 * auto transitions](#overlaps-and-autotransitions) section are respected any time
 * the timeline is [commited](ges_timeline_commit) (otherwise playback will most
 * probably fail in different ways).
 *
 * When disabling editing APIs, GES won't be able to enforce the rules that
 * makes the timeline overall state to be valid but some feature won't be
 * usable:
 *   * #GESTimeline:snapping-distance
 *   * #GESTimeline:auto-transition
 *
 * Since: 1.22
 */
void
ges_timeline_disable_edit_apis (GESTimeline * self, gboolean disable_edit_apis)
{
  CHECK_THREAD (self);
  g_return_if_fail (GES_IS_TIMELINE (self));

  if (disable_edit_apis) {
    if (self->priv->snapping_distance > 0) {
      GST_INFO_OBJECT (self,
          "Disabling snapping as we are disabling edit APIs");

      ges_timeline_set_snapping_distance (self, 0);
    }

    if (self->priv->auto_transition || self->priv->auto_transitions) {
      GST_INFO_OBJECT (self,
          "Disabling auto transitions as we are disabling auto edit APIs");
      ges_timeline_set_auto_transition (self, FALSE);
    }
  }

  self->priv->disable_edit_apis = disable_edit_apis;
}

/**
 * ges_timeline_get_edit_apis_disabled:
 * @self: A #GESTimeline
 *
 * Returns: %TRUE if edit APIs are disabled, %FALSE otherwise.
 *
 * Since: 1.22
 */
gboolean
ges_timeline_get_edit_apis_disabled (GESTimeline * self)
{
  CHECK_THREAD (self);
  g_return_val_if_fail (GES_IS_TIMELINE (self), FALSE);

  return self->priv->disable_edit_apis;
}
