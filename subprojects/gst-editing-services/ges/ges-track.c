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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

/**
 * SECTION:gestrack
 * @title: GESTrack
 * @short_description: The output source of a #GESTimeline
 *
 * A #GESTrack acts an output source for a #GESTimeline. Each one
 * essentially provides an additional #GstPad for the timeline, with
 * #GESTrack:restriction-caps capabilities. Internally, a track
 * wraps an #nlecomposition filtered by a #capsfilter.
 *
 * A track will contain a number of #GESTrackElement-s, and its role is
 * to select and activate these elements according to their timings when
 * the timeline in played. For example, a track would activate a
 * #GESSource when its #GESTimelineElement:start is reached by outputting
 * its data for its #GESTimelineElement:duration. Similarly, a
 * #GESOperation would be activated by applying its effect to the source
 * data, starting from its #GESTimelineElement:start time and lasting for
 * its #GESTimelineElement:duration.
 *
 * For most users, it will usually be sufficient to add newly created
 * tracks to a timeline, but never directly add an element to a track.
 * Whenever a #GESClip is added to a timeline, the clip adds its
 * elements to the timeline's tracks and assumes responsibility for
 * updating them.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "ges-internal.h"
#include "ges-track.h"
#include "ges-track-element.h"
#include "ges-meta-container.h"
#include "ges-video-track.h"
#include "ges-audio-track.h"

#define CHECK_THREAD(track) g_assert(track->priv->valid_thread == g_thread_self())

static GstStaticPadTemplate ges_track_src_pad_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

/* Structure that represents gaps and keep knowledge
 * of the gaps filled in the track */
typedef struct
{
  GstElement *nleobj;

  GstClockTime start;
  GstClockTime duration;
  GESTrack *track;
} Gap;

struct _GESTrackPrivate
{
  /*< private > */
  GESTimeline *timeline;
  GSequence *trackelements_by_start;
  GHashTable *trackelements_iter;
  GList *gaps;
  gboolean last_gap_disabled;

  guint64 duration;

  GstCaps *caps;
  GstCaps *restriction_caps;

  GstElement *composition;      /* The composition associated with this track */
  GstPad *srcpad;               /* The source GhostPad */

  gboolean updating;

  gboolean mixing;
  GstElement *mixing_operation;
  GstElement *capsfilter;

  /* Virtual method to create GstElement that fill gaps */
  GESCreateElementForGapFunc create_element_for_gaps;

  GThread *valid_thread;
};

enum
{
  ARG_0,
  ARG_CAPS,
  ARG_RESTRICTION_CAPS,
  ARG_TYPE,
  ARG_DURATION,
  ARG_MIXING,
  ARG_ID,
  ARG_LAST,
  TRACK_ELEMENT_ADDED,
  TRACK_ELEMENT_REMOVED,
  COMMITED,
  LAST_SIGNAL
};

static guint ges_track_signals[LAST_SIGNAL] = { 0 };

static GParamSpec *properties[ARG_LAST];

G_DEFINE_TYPE_WITH_CODE (GESTrack, ges_track, GST_TYPE_BIN,
    G_ADD_PRIVATE (GESTrack)
    G_IMPLEMENT_INTERFACE (GES_TYPE_META_CONTAINER, NULL));


static void composition_duration_cb (GstElement * composition, GParamSpec * arg
    G_GNUC_UNUSED, GESTrack * obj);
static void ges_track_set_caps (GESTrack * track, const GstCaps * caps);

/* Private methods/functions/callbacks */
static void
add_trackelement_to_list_foreach (GESTrackElement * trackelement, GList ** list)
{
  gst_object_ref (trackelement);
  *list = g_list_prepend (*list, trackelement);
}

static Gap *
gap_new (GESTrack * track, GstClockTime start, GstClockTime duration)
{
  GstElement *nlesrc, *elem;

  Gap *new_gap;

  nlesrc = gst_element_factory_make ("nlesource", NULL);
  elem = track->priv->create_element_for_gaps (track);
  if (G_UNLIKELY (gst_bin_add (GST_BIN (nlesrc), elem) == FALSE)) {
    GST_WARNING_OBJECT (track, "Could not create gap filler");

    if (nlesrc)
      gst_object_unref (nlesrc);

    if (elem)
      gst_object_unref (elem);

    return NULL;
  }

  if (G_UNLIKELY (ges_nle_composition_add_object (track->priv->composition,
              nlesrc) == FALSE)) {
    GST_WARNING_OBJECT (track, "Could not add gap to the composition");

    if (nlesrc)
      gst_object_unref (nlesrc);

    if (elem)
      gst_object_unref (elem);

    return NULL;
  }

  new_gap = g_new (Gap, 1);
  new_gap->start = start;
  new_gap->duration = duration;
  new_gap->track = track;
  new_gap->nleobj = nlesrc;


  g_object_set (nlesrc, "start", new_gap->start, "duration", new_gap->duration,
      "priority", 1, NULL);

  GST_DEBUG_OBJECT (track,
      "Created gap with start %" GST_TIME_FORMAT " duration %" GST_TIME_FORMAT,
      GST_TIME_ARGS (new_gap->start), GST_TIME_ARGS (new_gap->duration));


  return new_gap;
}

static void
free_gap (Gap * gap)
{
  GESTrack *track = gap->track;

  GST_DEBUG_OBJECT (track, "Removed gap with start %" GST_TIME_FORMAT
      " duration %" GST_TIME_FORMAT, GST_TIME_ARGS (gap->start),
      GST_TIME_ARGS (gap->duration));
  ges_nle_composition_remove_object (track->priv->composition, gap->nleobj);

  g_free (gap);
}

static inline void
update_gaps (GESTrack * track)
{
  Gap *gap;
  GList *gaps;
  GSequenceIter *it;

  GESTrackElement *trackelement;
  GstClockTime start, end, duration = 0, timeline_duration = 0;

  GESTrackPrivate *priv = track->priv;

  if (priv->create_element_for_gaps == NULL) {
    GST_INFO ("Not filling the gaps as no create_element_for_gaps vmethod"
        " provided");
    return;
  }

  gaps = priv->gaps;
  priv->gaps = NULL;

  /* 1- And recalculate gaps */
  for (it = g_sequence_get_begin_iter (priv->trackelements_by_start);
      g_sequence_iter_is_end (it) == FALSE; it = g_sequence_iter_next (it)) {
    trackelement = g_sequence_get (it);

    if (!ges_track_element_is_active (trackelement))
      continue;

    if (priv->timeline) {
      guint32 layer_prio = GES_TIMELINE_ELEMENT_LAYER_PRIORITY (trackelement);

      if (layer_prio != GES_TIMELINE_ELEMENT_NO_LAYER_PRIORITY) {
        GESLayer *layer = g_list_nth_data (priv->timeline->layers, layer_prio);

        if (!ges_layer_get_active_for_track (layer, track))
          continue;
      }
    }

    start = _START (trackelement);
    end = start + _DURATION (trackelement);

    if (start > duration) {
      /* 2- Fill gap */
      gap = gap_new (track, duration, start - duration);

      if (G_LIKELY (gap != NULL))
        priv->gaps = g_list_prepend (priv->gaps, gap);
    }

    duration = MAX (duration, end);
  }

  /* 3- Add a gap at the end of the timeline if needed */
  if (priv->timeline) {
    g_object_get (priv->timeline, "duration", &timeline_duration, NULL);

    if (duration < timeline_duration) {
      gap = gap_new (track, duration, timeline_duration - duration);

      if (G_LIKELY (gap != NULL)) {
        priv->gaps = g_list_prepend (priv->gaps, gap);
      }

      /* FIXME: here the duration is set to the duration of the timeline,
       * but elsewhere it is set to the duration of the composition. Are
       * these always the same? */
      priv->duration = timeline_duration;
    }
  }

  if (!track->priv->last_gap_disabled) {
    GST_DEBUG_OBJECT (track, "Adding a one second gap at the end");
    gap = gap_new (track, timeline_duration, 1);
    priv->gaps = g_list_prepend (priv->gaps, gap);
  }

  /* 4- Remove old gaps */
  g_list_free_full (gaps, (GDestroyNotify) free_gap);
}

void
track_disable_last_gap (GESTrack * track, gboolean disabled)
{
  track->priv->last_gap_disabled = disabled;
  update_gaps (track);
}

void
track_resort_and_fill_gaps (GESTrack * track)
{
  g_sequence_sort (track->priv->trackelements_by_start,
      (GCompareDataFunc) element_start_compare, NULL);

  if (track->priv->updating == TRUE) {
    update_gaps (track);
  }
}

static gboolean
update_field (GQuark field_id, const GValue * value, GstStructure * original)
{
  gst_structure_id_set_value (original, field_id, value);
  return TRUE;
}

/* callbacks */
static void
_ghost_nlecomposition_srcpad (GESTrack * track)
{
  GstPad *capsfilter_sink;
  GstPad *capsfilter_src;
  GESTrackPrivate *priv = track->priv;
  GstPad *pad = gst_element_get_static_pad (priv->composition, "src");

  capsfilter_sink = gst_element_get_static_pad (priv->capsfilter, "sink");

  GST_DEBUG ("track:%p, pad %s:%s", track, GST_DEBUG_PAD_NAME (pad));

  gst_pad_link (pad, capsfilter_sink);
  gst_object_unref (capsfilter_sink);
  gst_object_unref (pad);

  capsfilter_src = gst_element_get_static_pad (priv->capsfilter, "src");
  /* ghost the pad */
  priv->srcpad = gst_ghost_pad_new ("src", capsfilter_src);
  gst_object_unref (capsfilter_src);
  gst_pad_set_active (priv->srcpad, TRUE);
  gst_element_add_pad (GST_ELEMENT (track), priv->srcpad);

  GST_DEBUG ("done");
}

static void
composition_duration_cb (GstElement * composition,
    GParamSpec * arg G_GNUC_UNUSED, GESTrack * track)
{
  guint64 duration;

  g_object_get (composition, "duration", &duration, NULL);

  if (track->priv->duration != duration) {
    GST_DEBUG_OBJECT (track,
        "composition duration : %" GST_TIME_FORMAT " current : %"
        GST_TIME_FORMAT, GST_TIME_ARGS (duration),
        GST_TIME_ARGS (track->priv->duration));

    /* FIXME: here the duration is set to the duration of the composition,
     * but elsewhere it is set to the duration of the timeline. Are these
     * always the same? */
    track->priv->duration = duration;

    g_object_notify_by_pspec (G_OBJECT (track), properties[ARG_DURATION]);
  }
}

static void
composition_commited_cb (GstElement * composition, gboolean changed,
    GESTrack * self)
{
  g_signal_emit (self, ges_track_signals[COMMITED], 0);
}

/* Internal */
GstElement *
ges_track_get_composition (GESTrack * track)
{
  return track->priv->composition;
}

void
ges_track_set_smart_rendering (GESTrack * track, gboolean rendering_smartly)
{
  GESTrackPrivate *priv = track->priv;

  g_object_set (priv->capsfilter, "caps",
      rendering_smartly ? NULL : priv->restriction_caps, NULL);
}

/* FIXME: Find out how to avoid doing this "hack" using the GDestroyNotify
 * function pointer in the trackelements_by_start GSequence
 *
 * Remove @object from @track, but keeps it in the sequence this is needed
 * when finalizing as we can not change a GSequence at the same time we are
 * accessing it
 */
static gboolean
remove_object_internal (GESTrack * track, GESTrackElement * object,
    gboolean emit, GError ** error)
{
  GESTrackPrivate *priv;
  GstElement *nleobject;

  GST_DEBUG_OBJECT (track, "object:%p", object);

  priv = track->priv;

  if (G_UNLIKELY (ges_track_element_get_track (object) != track)) {
    GST_WARNING_OBJECT (track, "Object belongs to another track");
    return FALSE;
  }

  if (!ges_track_element_set_track (object, NULL, error)) {
    GST_INFO_OBJECT (track, "Failed to unset the track for %" GES_FORMAT,
        GES_ARGS (object));
    return FALSE;
  }
  ges_timeline_element_set_timeline (GES_TIMELINE_ELEMENT (object), NULL);

  if ((nleobject = ges_track_element_get_nleobject (object))) {
    GST_DEBUG ("Removing NleObject '%s' from composition '%s'",
        GST_ELEMENT_NAME (nleobject), GST_ELEMENT_NAME (priv->composition));

    if (!ges_nle_composition_remove_object (priv->composition, nleobject)) {
      GST_WARNING_OBJECT (track, "Failed to remove nleobject from composition");
      return FALSE;
    }
  }

  if (emit)
    g_signal_emit (track, ges_track_signals[TRACK_ELEMENT_REMOVED], 0,
        GES_TRACK_ELEMENT (object));

  gst_object_unref (object);

  return TRUE;
}

static void
dispose_trackelements_foreach (GESTrackElement * trackelement, GESTrack * track)
{
  remove_object_internal (track, trackelement, TRUE, NULL);
}

/* GstElement virtual methods */

static GstStateChangeReturn
ges_track_change_state (GstElement * element, GstStateChange transition)
{
  GESTrack *track = GES_TRACK (element);

  if (transition == GST_STATE_CHANGE_READY_TO_PAUSED &&
      track->priv->valid_thread == g_thread_self ())
    track_resort_and_fill_gaps (GES_TRACK (element));

  return GST_ELEMENT_CLASS (ges_track_parent_class)->change_state (element,
      transition);
}

void
ges_track_select_subtimeline_streams (GESTrack * track,
    GstStreamCollection * collection, GstElement * subtimeline)
{
  GList *selected_streams = NULL;

  for (gint i = 0; i < gst_stream_collection_get_size (collection); i++) {
    GstStream *stream = gst_stream_collection_get_stream (collection, i);
    GstStreamType stype = gst_stream_get_stream_type (stream);

    if ((track->type == GES_TRACK_TYPE_VIDEO && stype == GST_STREAM_TYPE_VIDEO)
        || (track->type == GES_TRACK_TYPE_AUDIO
            && stype == GST_STREAM_TYPE_AUDIO)
        || (stype == GST_STREAM_TYPE_UNKNOWN)) {

      selected_streams =
          g_list_append (selected_streams,
          g_strdup (gst_stream_get_stream_id (stream)));
    }
  }

  if (selected_streams) {
    gst_element_send_event (subtimeline,
        gst_event_new_select_streams (selected_streams));

    g_list_free_full (selected_streams, g_free);
  }
}

static void
ges_track_handle_message (GstBin * bin, GstMessage * message)
{
  GESTrack *track = GES_TRACK (bin);

  if (GST_MESSAGE_TYPE (message) == GST_MESSAGE_STREAM_COLLECTION) {
    GstStreamCollection *collection;

    gst_message_parse_stream_collection (message, &collection);
    if (GES_IS_TIMELINE (GST_MESSAGE_SRC (message))) {
      ges_track_select_subtimeline_streams (track, collection,
          GST_ELEMENT (GST_MESSAGE_SRC (message)));
    }
  }

  gst_element_post_message (GST_ELEMENT_CAST (bin), message);
}

/* GObject virtual methods */
static void
ges_track_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GESTrack *track = GES_TRACK (object);

  switch (property_id) {
    case ARG_CAPS:
      gst_value_set_caps (value, track->priv->caps);
      break;
    case ARG_TYPE:
      g_value_set_flags (value, track->type);
      break;
    case ARG_DURATION:
      g_value_set_uint64 (value, track->priv->duration);
      break;
    case ARG_RESTRICTION_CAPS:
      gst_value_set_caps (value, track->priv->restriction_caps);
      break;
    case ARG_MIXING:
      g_value_set_boolean (value, track->priv->mixing);
      break;
    case ARG_ID:
      g_object_get_property (G_OBJECT (track->priv->composition), "id", value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_track_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GESTrack *track = GES_TRACK (object);

  switch (property_id) {
    case ARG_CAPS:
      ges_track_set_caps (track, gst_value_get_caps (value));
      break;
    case ARG_TYPE:
      track->type = g_value_get_flags (value);
      break;
    case ARG_RESTRICTION_CAPS:
      ges_track_set_restriction_caps (track, gst_value_get_caps (value));
      break;
    case ARG_MIXING:
      ges_track_set_mixing (track, g_value_get_boolean (value));
      break;
    case ARG_ID:
      g_object_set_property (G_OBJECT (track->priv->composition), "id", value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_track_dispose (GObject * object)
{
  GESTrack *track = (GESTrack *) object;
  GESTrackPrivate *priv = track->priv;

  /* Remove all TrackElements and drop our reference */
  g_hash_table_unref (priv->trackelements_iter);
  g_sequence_foreach (track->priv->trackelements_by_start,
      (GFunc) dispose_trackelements_foreach, track);
  g_sequence_free (priv->trackelements_by_start);
  g_list_free_full (priv->gaps, (GDestroyNotify) free_gap);
  ges_nle_object_commit (track->priv->composition, TRUE);

  gst_clear_object (&track->priv->mixing_operation);
  if (priv->composition) {
    gst_element_remove_pad (GST_ELEMENT (track), priv->srcpad);
    gst_bin_remove (GST_BIN (object), priv->composition);
    priv->composition = NULL;
  }

  if (priv->caps) {
    gst_caps_unref (priv->caps);
    priv->caps = NULL;
  }

  if (priv->restriction_caps) {
    gst_caps_unref (priv->restriction_caps);
    priv->restriction_caps = NULL;
  }

  G_OBJECT_CLASS (ges_track_parent_class)->dispose (object);
}

static void
ges_track_finalize (GObject * object)
{
  G_OBJECT_CLASS (ges_track_parent_class)->finalize (object);
}

static void
ges_track_constructed (GObject * object)
{
  GESTrack *self = GES_TRACK (object);
  gchar *componame = NULL;
  gchar *capsfiltername = NULL;

  if (self->type == GES_TRACK_TYPE_VIDEO) {
    componame =
        g_strdup_printf ("video_%s", GST_OBJECT_NAME (self->priv->composition));
    capsfiltername =
        g_strdup_printf ("video_restriction_%s",
        GST_OBJECT_NAME (self->priv->capsfilter));
  } else if (self->type == GES_TRACK_TYPE_AUDIO) {
    componame =
        g_strdup_printf ("audio_%s", GST_OBJECT_NAME (self->priv->composition));
    capsfiltername =
        g_strdup_printf ("audio_restriction_%s",
        GST_OBJECT_NAME (self->priv->capsfilter));
  }

  if (componame) {
    gst_object_set_name (GST_OBJECT (self->priv->composition), componame);
    gst_object_set_name (GST_OBJECT (self->priv->capsfilter), capsfiltername);

    g_free (componame);
    g_free (capsfiltername);
  }

  if (!gst_bin_add (GST_BIN (self), self->priv->composition))
    GST_ERROR ("Couldn't add composition to bin !");

  if (!gst_bin_add (GST_BIN (self), self->priv->capsfilter))
    GST_ERROR ("Couldn't add capsfilter to bin !");

  _ghost_nlecomposition_srcpad (self);
  if (GES_TRACK_GET_CLASS (self)->get_mixing_element) {
    GstElement *nleobject;
    GstElement *mixer = GES_TRACK_GET_CLASS (self)->get_mixing_element (self);

    if (mixer == NULL) {
      GST_WARNING_OBJECT (self, "Got no element fron get_mixing_element");

      return;
    }

    nleobject = gst_element_factory_make ("nleoperation", "mixing-operation");
    if (!gst_bin_add (GST_BIN (nleobject), mixer)) {
      GST_WARNING_OBJECT (self, "Could not add the mixer to our composition");
      gst_object_unref (mixer);
      gst_object_unref (nleobject);

      return;
    }
    g_object_set (nleobject, "expandable", TRUE, NULL);

    if (self->priv->mixing) {
      if (!ges_nle_composition_add_object (self->priv->composition, nleobject)) {
        GST_WARNING_OBJECT (self, "Could not add the mixer to our composition");
        gst_object_unref (nleobject);

        return;
      }
    }

    self->priv->mixing_operation = gst_object_ref (nleobject);

  } else {
    GST_INFO_OBJECT (self, "No way to create a main mixer");
  }
}

static void
ges_track_class_init (GESTrackClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GstElementClass *gstelement_class = (GstElementClass *) klass;
  GstBinClass *bin_class = GST_BIN_CLASS (klass);

  gstelement_class->change_state = GST_DEBUG_FUNCPTR (ges_track_change_state);

  bin_class->handle_message = GST_DEBUG_FUNCPTR (ges_track_handle_message);

  object_class->get_property = ges_track_get_property;
  object_class->set_property = ges_track_set_property;
  object_class->dispose = ges_track_dispose;
  object_class->finalize = ges_track_finalize;
  object_class->constructed = ges_track_constructed;

  /**
   * GESTrack:caps:
   *
   * The capabilities used to choose the output of the #GESTrack's
   * elements. Internally, this is used to select output streams when
   * several may be available, by determining whether its #GstPad is
   * compatible (see #NleObject:caps for #nlecomposition). As such,
   * this is used as a weaker indication of the desired output type of the
   * track, **before** the #GESTrack:restriction-caps is applied.
   * Therefore, this should be set to a *generic* superset of the
   * #GESTrack:restriction-caps, such as "video/x-raw(ANY)". In addition,
   * it should match with the track's #GESTrack:track-type.
   *
   * Note that when you set this property, the #GstCapsFeatures of all its
   * #GstStructure-s will be automatically set to #GST_CAPS_FEATURES_ANY.
   *
   * Once a track has been added to a #GESTimeline, you should not change
   * this.
   *
   * Default value: #GST_CAPS_ANY.
   */
  properties[ARG_CAPS] = g_param_spec_boxed ("caps", "Caps",
      "Caps used to choose the output stream",
      GST_TYPE_CAPS, G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);
  g_object_class_install_property (object_class, ARG_CAPS,
      properties[ARG_CAPS]);

  /**
   * GESTrack:restriction-caps:
   *
   * The capabilities that specifies the final output format of the
   * #GESTrack. For example, for a video track, it would specify the
   * height, width, framerate and other properties of the stream.
   *
   * You may change this property after the track has been added to a
   * #GESTimeline, but it must remain compatible with the track's
   * #GESTrack:caps.
   *
   * Default value: #GST_CAPS_ANY.
   */
  properties[ARG_RESTRICTION_CAPS] =
      g_param_spec_boxed ("restriction-caps", "Restriction caps",
      "Caps used as a final filter on the output stream", GST_TYPE_CAPS,
      G_PARAM_READWRITE);
  g_object_class_install_property (object_class, ARG_RESTRICTION_CAPS,
      properties[ARG_RESTRICTION_CAPS]);

  /**
   * GESTrack:duration:
   *
   * Current duration of the track
   *
   * Default value: O
   */
  /* FIXME: is duration the duration of the timeline or the duration of
   * the underlying composition? */
  properties[ARG_DURATION] = g_param_spec_uint64 ("duration", "Duration",
      "The current duration of the track", 0, G_MAXUINT64, GST_SECOND,
      G_PARAM_READABLE);
  g_object_class_install_property (object_class, ARG_DURATION,
      properties[ARG_DURATION]);

  /**
   * GESTrack:track-type:
   *
   * The track type of the track. This controls the type of
   * #GESTrackElement-s that can be added to the track. This should
   * match with the track's #GESTrack:caps.
   *
   * Once a track has been added to a #GESTimeline, you should not change
   * this.
   */
  properties[ARG_TYPE] = g_param_spec_flags ("track-type", "TrackType",
      "Type of stream the track outputs",
      GES_TYPE_TRACK_TYPE, GES_TRACK_TYPE_CUSTOM,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);
  g_object_class_install_property (object_class, ARG_TYPE,
      properties[ARG_TYPE]);

  /**
   * GESTrack:mixing:
   *
   * Whether the track should support the mixing of #GESLayer data, such
   * as composing the video data of each layer (when part of the video
   * data is transparent, the next layer will become visible) or adding
   * together the audio data. As such, for audio and video tracks, you'll
   * likely want to keep this set to %TRUE.
   */
  properties[ARG_MIXING] = g_param_spec_boolean ("mixing", "Mixing",
      "Whether layer mixing is activated on the track or not",
      TRUE, G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_EXPLICIT_NOTIFY);
  g_object_class_install_property (object_class, ARG_MIXING,
      properties[ARG_MIXING]);

  /**
   * GESTrack:id:
   *
   * The #nlecomposition:id of the underlying #nlecomposition.
   *
   * Since: 1.18
   */
  properties[ARG_ID] =
      g_param_spec_string ("id", "Id", "The stream-id of the composition",
      NULL,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_DOC_SHOW_DEFAULT);
  g_object_class_install_property (object_class, ARG_ID, properties[ARG_ID]);

  gst_element_class_add_static_pad_template (gstelement_class,
      &ges_track_src_pad_template);

  /**
   * GESTrack::track-element-added:
   * @object: The #GESTrack
   * @effect: The element that was added
   *
   * Will be emitted after a track element is added to the track.
   */
  ges_track_signals[TRACK_ELEMENT_ADDED] =
      g_signal_new ("track-element-added", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_FIRST, 0, NULL, NULL, NULL,
      G_TYPE_NONE, 1, GES_TYPE_TRACK_ELEMENT);

  /**
   * GESTrack::track-element-removed:
   * @object: The #GESTrack
   * @effect: The element that was removed
   *
   * Will be emitted after a track element is removed from the track.
   */
  ges_track_signals[TRACK_ELEMENT_REMOVED] =
      g_signal_new ("track-element-removed", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_FIRST, 0, NULL, NULL, NULL,
      G_TYPE_NONE, 1, GES_TYPE_TRACK_ELEMENT);

  /**
   * GESTrack::commited:
   * @track: The #GESTrack
   *
   * This signal will be emitted once the changes initiated by
   * ges_track_commit() have been executed in the backend. In particular,
   * this will be emitted whenever the underlying #nlecomposition has been
   * committed (see #nlecomposition::commited).
   */
  ges_track_signals[COMMITED] =
      g_signal_new ("commited", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 0);

  klass->get_mixing_element = NULL;
}

static void
ges_track_init (GESTrack * self)
{
  self->priv = ges_track_get_instance_private (self);
  self->priv->valid_thread = g_thread_self ();

  self->priv->composition = gst_element_factory_make ("nlecomposition", NULL);
  self->priv->capsfilter = gst_element_factory_make ("capsfilter", NULL);
  self->priv->updating = TRUE;
  self->priv->trackelements_by_start = g_sequence_new (NULL);
  self->priv->trackelements_iter =
      g_hash_table_new (g_direct_hash, g_direct_equal);
  self->priv->create_element_for_gaps = NULL;
  self->priv->gaps = NULL;
  self->priv->mixing = TRUE;
  self->priv->restriction_caps = NULL;
  self->priv->last_gap_disabled = TRUE;

  g_signal_connect (G_OBJECT (self->priv->composition), "notify::duration",
      G_CALLBACK (composition_duration_cb), self);
  g_signal_connect (G_OBJECT (self->priv->composition), "commited",
      G_CALLBACK (composition_commited_cb), self);
}

/**
 * ges_track_new:
 * @type: The #GESTrack:track-type for the track
 * @caps: (transfer full): The #GESTrack:caps for the track
 *
 * Creates a new track with the given track-type and caps.
 *
 * If @type is #GES_TRACK_TYPE_VIDEO, and @caps is a subset of
 * "video/x-raw(ANY)", then a #GESVideoTrack is created. This will
 * automatically choose a gap creation method suitable for video data. You
 * will likely want to set #GESTrack:restriction-caps separately. You may
 * prefer to use the ges_video_track_new() method instead.
 *
 * If @type is #GES_TRACK_TYPE_AUDIO, and @caps is a subset of
 * "audio/x-raw(ANY)", then a #GESAudioTrack is created. This will
 * automatically choose a gap creation method suitable for audio data, and
 * will set the #GESTrack:restriction-caps to the default for
 * #GESAudioTrack. You may prefer to use the ges_audio_track_new() method
 * instead.
 *
 * Otherwise, a plain #GESTrack is returned. You will likely want to set
 * the #GESTrack:restriction-caps and call
 * ges_track_set_create_element_for_gap_func() on the returned track.
 *
 * Returns: (transfer floating): A new track.
 */
GESTrack *
ges_track_new (GESTrackType type, GstCaps * caps)
{
  GESTrack *track;
  GstCaps *tmpcaps;

  /* TODO Be smarter with well known track types */
  if (type == GES_TRACK_TYPE_VIDEO) {
    tmpcaps = gst_caps_new_empty_simple ("video/x-raw");
    gst_caps_set_features (tmpcaps, 0, gst_caps_features_new_any ());

    if (gst_caps_is_subset (caps, tmpcaps)) {
      track = GES_TRACK (ges_video_track_new ());
      ges_track_set_caps (track, caps);

      gst_caps_unref (tmpcaps);
      /* FIXME: ges_track_set_caps does not take ownership of caps,
       * so we also need to unref caps */
      return track;
    }
    gst_caps_unref (tmpcaps);
  } else if (type == GES_TRACK_TYPE_AUDIO) {
    tmpcaps = gst_caps_new_empty_simple ("audio/x-raw");
    gst_caps_set_features (tmpcaps, 0, gst_caps_features_new_any ());

    if (gst_caps_is_subset (caps, tmpcaps)) {
      track = GES_TRACK (ges_audio_track_new ());
      ges_track_set_caps (track, caps);

      gst_caps_unref (tmpcaps);
      /* FIXME: ges_track_set_caps does not take ownership of caps,
       * so we also need to unref caps */
      return track;
    }

    gst_caps_unref (tmpcaps);
  }

  track = g_object_new (GES_TYPE_TRACK, "caps", caps, "track-type", type, NULL);
  gst_caps_unref (caps);

  return track;
}


/**
 * ges_track_set_timeline:
 * @track: A #GESTrack
 * @timeline (nullable): A #GESTimeline
 *
 * Informs the track that it belongs to the given timeline. Calling this
 * does not actually add the track to the timeline. For that, you should
 * use ges_timeline_add_track(), which will also take care of informing
 * the track that it belongs to the timeline. As such, there is no need
 * for you to call this method.
 */
/* FIXME: this should probably be deprecated and only used internally */
void
ges_track_set_timeline (GESTrack * track, GESTimeline * timeline)
{
  GSequenceIter *it;
  g_return_if_fail (GES_IS_TRACK (track));
  g_return_if_fail (timeline == NULL || GES_IS_TIMELINE (timeline));
  GST_DEBUG ("track:%p, timeline:%p", track, timeline);

  track->priv->timeline = timeline;

  for (it = g_sequence_get_begin_iter (track->priv->trackelements_by_start);
      g_sequence_iter_is_end (it) == FALSE; it = g_sequence_iter_next (it)) {
    GESTimelineElement *trackelement =
        GES_TIMELINE_ELEMENT (g_sequence_get (it));
    ges_timeline_element_set_timeline (trackelement, timeline);
  }
  track_resort_and_fill_gaps (track);
}

/**
 * ges_track_set_caps:
 * @track: A #GESTrack
 * @caps: The new caps for @track
 *
 * Sets the #GESTrack:caps for the track. The new #GESTrack:caps of the
 * track will be a copy of @caps, except its #GstCapsFeatures will be
 * automatically set to #GST_CAPS_FEATURES_ANY.
 */
void
ges_track_set_caps (GESTrack * track, const GstCaps * caps)
{
  GESTrackPrivate *priv;
  gint i;

  g_return_if_fail (GES_IS_TRACK (track));
  CHECK_THREAD (track);

  GST_DEBUG ("track:%p, caps:%" GST_PTR_FORMAT, track, caps);
  g_return_if_fail (GST_IS_CAPS (caps));

  priv = track->priv;

  if (priv->caps)
    gst_caps_unref (priv->caps);
  priv->caps = gst_caps_copy (caps);

  for (i = 0; i < (int) gst_caps_get_size (priv->caps); i++)
    gst_caps_set_features (priv->caps, i, gst_caps_features_new_any ());

  g_object_set (priv->composition, "caps", caps, NULL);
  /* FIXME : update all trackelements ? */
}

/**
 * ges_track_set_restriction_caps:
 * @track: A #GESTrack
 * @caps: The new restriction-caps for @track
 *
 * Sets the #GESTrack:restriction-caps for the track.
 *
 * > **NOTE**: Restriction caps are **not** taken into account when
 * > using #GESPipeline:mode=#GES_PIPELINE_MODE_SMART_RENDER.
 */
void
ges_track_set_restriction_caps (GESTrack * track, const GstCaps * caps)
{
  GESTrackPrivate *priv;

  g_return_if_fail (GES_IS_TRACK (track));
  CHECK_THREAD (track);

  GST_DEBUG ("track:%p, restriction caps:%" GST_PTR_FORMAT, track, caps);
  g_return_if_fail (GST_IS_CAPS (caps));

  priv = track->priv;

  if (priv->restriction_caps)
    gst_caps_unref (priv->restriction_caps);
  priv->restriction_caps = gst_caps_copy (caps);

  if (!track->priv->timeline ||
      !ges_timeline_get_smart_rendering (track->priv->timeline))
    g_object_set (priv->capsfilter, "caps", caps, NULL);

  g_object_notify (G_OBJECT (track), "restriction-caps");
}

/**
 * ges_track_update_restriction_caps:
 * @track: A #GESTrack
 * @caps: The caps to update the restriction-caps with
 *
 * Updates the #GESTrack:restriction-caps of the track using the fields
 * found in the given caps. Each of the #GstStructure-s in @caps is
 * compared against the existing structure with the same index in the
 * current #GESTrack:restriction-caps. If there is no corresponding
 * existing structure at that index, then the new structure is simply
 * copied to that index. Otherwise, any fields in the new structure are
 * copied into the existing structure. This will replace existing values,
 * and may introduce new ones, but any fields 'missing' in the new
 * structure are left unchanged in the existing structure.
 *
 * For example, if the existing #GESTrack:restriction-caps are
 * "video/x-raw, width=480, height=360", and the updating caps is
 * "video/x-raw, format=I420, width=500; video/x-bayer, width=400", then
 * the new #GESTrack:restriction-caps after calling this will be
 * "video/x-raw, width=500, height=360, format=I420; video/x-bayer,
 * width=400".
 */
void
ges_track_update_restriction_caps (GESTrack * self, const GstCaps * caps)
{
  guint i;
  GstCaps *new_restriction_caps;

  g_return_if_fail (GES_IS_TRACK (self));
  CHECK_THREAD (self);

  if (!self->priv->restriction_caps) {
    ges_track_set_restriction_caps (self, caps);
    return;
  }

  new_restriction_caps = gst_caps_copy (self->priv->restriction_caps);
  for (i = 0; i < gst_caps_get_size (caps); i++) {
    GstStructure *new = gst_caps_get_structure (caps, i);

    if (gst_caps_get_size (new_restriction_caps) > i) {
      GstStructure *original = gst_caps_get_structure (new_restriction_caps, i);
      gst_structure_foreach (new, (GstStructureForeachFunc) update_field,
          original);
    } else
      gst_caps_append_structure (new_restriction_caps,
          gst_structure_copy (new));
    /* FIXME: maybe appended structure should also have its CapsFeatures
     * copied over? */
  }

  ges_track_set_restriction_caps (self, new_restriction_caps);
  gst_caps_unref (new_restriction_caps);
}

/**
 * ges_track_set_mixing:
 * @track: A #GESTrack
 * @mixing: Whether @track should be mixing
 *
 * Sets the #GESTrack:mixing for the track.
 */
void
ges_track_set_mixing (GESTrack * track, gboolean mixing)
{
  g_return_if_fail (GES_IS_TRACK (track));
  CHECK_THREAD (track);

  if (mixing == track->priv->mixing) {
    GST_DEBUG_OBJECT (track, "Mixing is already set to the same value");

    return;
  }

  if (!track->priv->mixing_operation) {
    GST_DEBUG_OBJECT (track, "Track will be set to mixing = %d", mixing);
    goto notify;
  }

  if (mixing) {
    if (!ges_nle_composition_add_object (track->priv->composition,
            track->priv->mixing_operation)) {
      GST_WARNING_OBJECT (track, "Could not add the mixer to our composition");
      return;
    }
  } else {
    if (!ges_nle_composition_remove_object (track->priv->composition,
            track->priv->mixing_operation)) {
      GST_WARNING_OBJECT (track,
          "Could not remove the mixer from our composition");
      return;
    }
  }

notify:
  track->priv->mixing = mixing;

  if (track->priv->timeline)
    ges_timeline_set_smart_rendering (track->priv->timeline,
        ges_timeline_get_smart_rendering (track->priv->timeline));
  g_object_notify_by_pspec (G_OBJECT (track), properties[ARG_MIXING]);

  GST_DEBUG_OBJECT (track, "The track has been set to mixing = %d", mixing);
}

static gboolean
remove_element_internal (GESTrack * track, GESTrackElement * object,
    gboolean emit, GError ** error)
{
  GSequenceIter *it;
  GESTrackPrivate *priv = track->priv;

  GST_DEBUG_OBJECT (track, "Removing %" GST_PTR_FORMAT, object);

  it = g_hash_table_lookup (priv->trackelements_iter, object);
  g_sequence_remove (it);

  if (remove_object_internal (track, object, emit, error) == TRUE) {
    ges_timeline_element_set_timeline (GES_TIMELINE_ELEMENT (object), NULL);

    return TRUE;
  }

  g_hash_table_insert (track->priv->trackelements_iter, object,
      g_sequence_insert_sorted (track->priv->trackelements_by_start, object,
          (GCompareDataFunc) element_start_compare, NULL));

  return FALSE;
}

/**
 * ges_track_add_element_full:
 * @track: A #GESTrack
 * @object: (transfer floating): The element to add
 * @error: (nullable): Return location for an error
 *
 * Adds the given track element to the track, which takes ownership of the
 * element.
 *
 * Note that this can fail if it would break a configuration rule of the
 * track's #GESTimeline.
 *
 * Note that a #GESTrackElement can only be added to one track.
 *
 * Returns: %TRUE if @object was successfully added to @track.
 * Since: 1.18
 */
gboolean
ges_track_add_element_full (GESTrack * track, GESTrackElement * object,
    GError ** error)
{
  GESTimeline *timeline;
  GESTimelineElement *el;

  g_return_val_if_fail (GES_IS_TRACK (track), FALSE);
  g_return_val_if_fail (GES_IS_TRACK_ELEMENT (object), FALSE);
  g_return_val_if_fail (!error || !*error, FALSE);

  el = GES_TIMELINE_ELEMENT (object);

  CHECK_THREAD (track);

  GST_DEBUG ("track:%p, object:%p", track, object);

  if (G_UNLIKELY (ges_track_element_get_track (object) != NULL)) {
    GST_WARNING ("Object already belongs to another track");
    gst_object_ref_sink (object);
    gst_object_unref (object);
    return FALSE;
  }

  if (!ges_track_element_set_track (object, track, error)) {
    GST_INFO_OBJECT (track, "Failed to set the track for %" GES_FORMAT,
        GES_ARGS (object));
    gst_object_ref_sink (object);
    gst_object_unref (object);
    return FALSE;
  }
  ges_timeline_element_set_timeline (el, NULL);

  GST_DEBUG ("Adding object %s to ourself %s",
      GST_OBJECT_NAME (ges_track_element_get_nleobject (object)),
      GST_OBJECT_NAME (track->priv->composition));

  if (G_UNLIKELY (!ges_nle_composition_add_object (track->priv->composition,
              ges_track_element_get_nleobject (object)))) {
    GST_WARNING ("Couldn't add object to the NleComposition");
    if (!ges_track_element_set_track (object, NULL, NULL))
      GST_ERROR_OBJECT (track, "Failed to unset track of element %"
          GES_FORMAT, GES_ARGS (object));
    gst_object_ref_sink (object);
    gst_object_unref (object);
    return FALSE;
  }

  gst_object_ref_sink (object);
  g_hash_table_insert (track->priv->trackelements_iter, object,
      g_sequence_insert_sorted (track->priv->trackelements_by_start, object,
          (GCompareDataFunc) element_start_compare, NULL));

  timeline = track->priv->timeline;
  ges_timeline_element_set_timeline (el, timeline);
  /* check that we haven't broken the timeline configuration by adding this
   * element to the track */
  if (timeline
      && !timeline_tree_can_move_element (timeline_get_tree (timeline), el,
          GES_TIMELINE_ELEMENT_LAYER_PRIORITY (el), el->start, el->duration,
          error)) {
    GST_INFO_OBJECT (track,
        "Could not add the track element %" GES_FORMAT
        " to the track because it breaks the timeline " "configuration rules",
        GES_ARGS (el));
    remove_element_internal (track, object, FALSE, NULL);
    return FALSE;
  }

  g_signal_emit (track, ges_track_signals[TRACK_ELEMENT_ADDED], 0,
      GES_TRACK_ELEMENT (object));

  return TRUE;
}

/**
 * ges_track_add_element:
 * @track: A #GESTrack
 * @object: (transfer floating): The element to add
 *
 * See ges_track_add_element(), which also gives an error.
 *
 * Returns: %TRUE if @object was successfully added to @track.
 */
gboolean
ges_track_add_element (GESTrack * track, GESTrackElement * object)
{
  return ges_track_add_element_full (track, object, NULL);
}

/**
 * ges_track_get_elements:
 * @track: A #GESTrack
 *
 * Gets the track elements contained in the track. The returned list is
 * sorted by the element's #GESTimelineElement:priority and
 * #GESTimelineElement:start.
 *
 * Returns: (transfer full) (element-type GESTrackElement): A list of
 * all the #GESTrackElement-s in @track.
 */
GList *
ges_track_get_elements (GESTrack * track)
{
  GList *ret = NULL;

  g_return_val_if_fail (GES_IS_TRACK (track), NULL);
  CHECK_THREAD (track);

  g_sequence_foreach (track->priv->trackelements_by_start,
      (GFunc) add_trackelement_to_list_foreach, &ret);

  ret = g_list_reverse (ret);
  return ret;
}

/**
 * ges_track_remove_element_full:
 * @track: A #GESTrack
 * @object: The element to remove
 * @error: (nullable): Return location for an error
 *
 * Removes the given track element from the track, which revokes
 * ownership of the element.
 *
 * Returns: %TRUE if @object was successfully removed from @track.
 * Since: 1.18
 */
gboolean
ges_track_remove_element_full (GESTrack * track, GESTrackElement * object,
    GError ** error)
{
  g_return_val_if_fail (GES_IS_TRACK (track), FALSE);
  g_return_val_if_fail (GES_IS_TRACK_ELEMENT (object), FALSE);
  g_return_val_if_fail (!error || !*error, FALSE);

  if (!track->priv->timeline
      || !ges_timeline_is_disposed (track->priv->timeline))
    CHECK_THREAD (track);

  return remove_element_internal (track, object, TRUE, error);
}

/**
 * ges_track_remove_element:
 * @track: A #GESTrack
 * @object: The element to remove
 *
 * See ges_track_remove_element_full(), which also returns an error.
 *
 * Returns: %TRUE if @object was successfully removed from @track.
 */
gboolean
ges_track_remove_element (GESTrack * track, GESTrackElement * object)
{
  return ges_track_remove_element_full (track, object, NULL);
}

/**
 * ges_track_get_caps:
 * @track: A #GESTrack
 *
 * Get the #GESTrack:caps of the track.
 *
 * Returns: (nullable): The caps of @track.
 */
const GstCaps *
ges_track_get_caps (GESTrack * track)
{
  g_return_val_if_fail (GES_IS_TRACK (track), NULL);
  CHECK_THREAD (track);

  return track->priv->caps;
}

/**
 * ges_track_get_timeline:
 * @track: A #GESTrack
 *
 * Get the timeline this track belongs to.
 *
 * Returns: (nullable): The timeline that @track belongs to, or %NULL if
 * it does not belong to a timeline.
 */
const GESTimeline *
ges_track_get_timeline (GESTrack * track)
{
  g_return_val_if_fail (GES_IS_TRACK (track), NULL);
  CHECK_THREAD (track);

  return track->priv->timeline;
}

/**
 * ges_track_get_mixing:
 * @track: A #GESTrack
 *
 * Gets the #GESTrack:mixing of the track.
 *
 * Returns: Whether @track is mixing.
 */
gboolean
ges_track_get_mixing (GESTrack * track)
{
  g_return_val_if_fail (GES_IS_TRACK (track), FALSE);

  return track->priv->mixing;
}

/**
 * ges_track_commit:
 * @track: A #GESTrack
 *
 * Commits all the pending changes for the elements contained in the
 * track.
 *
 * When changes are made to the timing or priority of elements within a
 * track, they are not directly executed for the underlying
 * #nlecomposition and its children. This method will finally execute
 * these changes so they are reflected in the data output of the track.
 *
 * Any pending changes will be executed in the backend. The
 * #GESTimeline::commited signal will be emitted once this has completed.
 *
 * Note that ges_timeline_commit() will call this method on all of its
 * tracks, so you are unlikely to need to use this directly.
 *
 * Returns: %TRUE if pending changes were committed, or %FALSE if nothing
 * needed to be committed.
 */
gboolean
ges_track_commit (GESTrack * track)
{
  g_return_val_if_fail (GES_IS_TRACK (track), FALSE);
  CHECK_THREAD (track);

  track_resort_and_fill_gaps (track);

  return ges_nle_object_commit (track->priv->composition, TRUE);
}


/**
 * ges_track_set_create_element_for_gap_func:
 * @track: A #GESTrack
 * @func: (scope notified): The function to be used to create a source
 * #GstElement that can fill gaps in @track
 *
 * Sets the function that will be used to create a #GstElement that can be
 * used as a source to fill the gaps of the track. A gap is a timeline
 * region where the track has no #GESTrackElement sources. Therefore, you
 * are likely to want the #GstElement returned by the function to always
 * produce 'empty' content, defined relative to the stream type, such as
 * transparent frames for a video, or mute samples for audio.
 *
 * #GESAudioTrack and #GESVideoTrack objects are created with such a
 * function already set appropriately.
 */
void
ges_track_set_create_element_for_gap_func (GESTrack * track,
    GESCreateElementForGapFunc func)
{
  g_return_if_fail (GES_IS_TRACK (track));
  CHECK_THREAD (track);

  track->priv->create_element_for_gaps = func;
}

/**
 * ges_track_get_restriction_caps:
 * @track: A #GESTrack
 *
 * Gets the #GESTrack:restriction-caps of the track.
 *
 * Returns: (transfer full) (nullable): The restriction-caps of @track.
 *
 * Since: 1.18
 */
GstCaps *
ges_track_get_restriction_caps (GESTrack * track)
{
  GESTrackPrivate *priv;

  g_return_val_if_fail (GES_IS_TRACK (track), NULL);
  CHECK_THREAD (track);

  priv = track->priv;

  if (priv->restriction_caps)
    return gst_caps_ref (priv->restriction_caps);

  return NULL;
}
