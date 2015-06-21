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
 * @short_description: Composition of objects
 *
 * Corresponds to one output format (i.e. audio OR video).
 *
 * Contains the compatible TrackElement(s).
 */

#include "ges-internal.h"
#include "ges-track.h"
#include "ges-track-element.h"
#include "ges-meta-container.h"
#include "ges-video-track.h"
#include "ges-audio-track.h"
#include "nle/nleobject.h"

G_DEFINE_TYPE_WITH_CODE (GESTrack, ges_track, GST_TYPE_BIN,
    G_IMPLEMENT_INTERFACE (GES_TYPE_META_CONTAINER, NULL));

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
};

enum
{
  ARG_0,
  ARG_CAPS,
  ARG_RESTRICTION_CAPS,
  ARG_TYPE,
  ARG_DURATION,
  ARG_MIXING,
  ARG_LAST,
  TRACK_ELEMENT_ADDED,
  TRACK_ELEMENT_REMOVED,
  COMMITED,
  LAST_SIGNAL
};

static guint ges_track_signals[LAST_SIGNAL] = { 0 };

static GParamSpec *properties[ARG_LAST];

static void composition_duration_cb (GstElement * composition, GParamSpec * arg
    G_GNUC_UNUSED, GESTrack * obj);

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

  if (G_UNLIKELY (nle_composition_add_object (track->priv->composition,
              nlesrc) == FALSE)) {
    GST_WARNING_OBJECT (track, "Could not add gap to the composition");

    if (nlesrc)
      gst_object_unref (nlesrc);

    if (elem)
      gst_object_unref (elem);

    return NULL;
  }

  new_gap = g_slice_new (Gap);
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
  nle_composition_remove_object (track->priv->composition, gap->nleobj);

  g_slice_free (Gap, gap);
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

      priv->duration = timeline_duration;
    }
  }

  GST_DEBUG_OBJECT (track, "Adding a one second gap at the end");
  gap = gap_new (track, timeline_duration, 1);
  priv->gaps = g_list_prepend (priv->gaps, gap);

  /* 4- Remove old gaps */
  g_list_free_full (gaps, (GDestroyNotify) free_gap);
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
sort_track_elements_cb (GESTrackElement * child,
    GParamSpec * arg G_GNUC_UNUSED, GESTrack * track)
{
  g_sequence_sort (track->priv->trackelements_by_start,
      (GCompareDataFunc) element_start_compare, NULL);
}

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

/* FIXME: Find out how to avoid doing this "hack" using the GDestroyNotify
 * function pointer in the trackelements_by_start GSequence
 *
 * Remove @object from @track, but keeps it in the sequence this is needed
 * when finalizing as we can not change a GSequence at the same time we are
 * accessing it
 */
static gboolean
remove_object_internal (GESTrack * track, GESTrackElement * object)
{
  GESTrackPrivate *priv;
  GstElement *nleobject;

  GST_DEBUG_OBJECT (track, "object:%p", object);

  priv = track->priv;

  if (G_UNLIKELY (ges_track_element_get_track (object) != track)) {
    GST_WARNING ("Object belongs to another track");
    return FALSE;
  }

  if ((nleobject = ges_track_element_get_nleobject (object))) {
    GST_DEBUG ("Removing NleObject '%s' from composition '%s'",
        GST_ELEMENT_NAME (nleobject), GST_ELEMENT_NAME (priv->composition));

    if (!nle_composition_remove_object (priv->composition, nleobject)) {
      GST_WARNING ("Failed to remove nleobject from composition");
      return FALSE;
    }
  }

  g_signal_handlers_disconnect_by_func (object, sort_track_elements_cb, NULL);

  ges_track_element_set_track (object, NULL);
  ges_timeline_element_set_timeline (GES_TIMELINE_ELEMENT (object), NULL);

  g_signal_emit (track, ges_track_signals[TRACK_ELEMENT_REMOVED], 0,
      GES_TRACK_ELEMENT (object));

  gst_object_unref (object);

  return TRUE;
}

static void
dispose_trackelements_foreach (GESTrackElement * trackelement, GESTrack * track)
{
  remove_object_internal (track, trackelement);
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
  nle_object_commit (NLE_OBJECT (track->priv->composition), TRUE);

  if (priv->mixing_operation)
    gst_object_unref (priv->mixing_operation);

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

  if (self->type == GES_TRACK_TYPE_VIDEO) {
    componame =
        g_strdup_printf ("(video)%s",
        GST_OBJECT_NAME (self->priv->composition));
  } else if (self->type == GES_TRACK_TYPE_AUDIO) {
    componame =
        g_strdup_printf ("(audio)%s",
        GST_OBJECT_NAME (self->priv->composition));
  }

  if (componame) {
    gst_object_set_name (GST_OBJECT (self->priv->composition), componame);

    g_free (componame);
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
      gst_object_unref (nleobject);

      return;
    }
    g_object_set (nleobject, "expandable", TRUE, NULL);

    if (self->priv->mixing) {
      if (!nle_composition_add_object (self->priv->composition, nleobject)) {
        GST_WARNING_OBJECT (self, "Could not add the mixer to our composition");

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

  g_type_class_add_private (klass, sizeof (GESTrackPrivate));

  object_class->get_property = ges_track_get_property;
  object_class->set_property = ges_track_set_property;
  object_class->dispose = ges_track_dispose;
  object_class->finalize = ges_track_finalize;
  object_class->constructed = ges_track_constructed;

  /**
   * GESTrack:caps:
   *
   * Caps used to filter/choose the output stream. This is generally set to
   * a generic set of caps like 'video/x-raw' for raw video.
   *
   * Default value: #GST_CAPS_ANY.
   */
  properties[ARG_CAPS] = g_param_spec_boxed ("caps", "Caps",
      "Caps used to filter/choose the output stream",
      GST_TYPE_CAPS, G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);
  g_object_class_install_property (object_class, ARG_CAPS,
      properties[ARG_CAPS]);

  /**
   * GESTrack:restriction-caps:
   *
   * Caps used to filter/choose the output stream.
   *
   * Default value: #GST_CAPS_ANY.
   */
  properties[ARG_RESTRICTION_CAPS] =
      g_param_spec_boxed ("restriction-caps", "Restriction caps",
      "Caps used to filter/choose the output stream", GST_TYPE_CAPS,
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
  properties[ARG_DURATION] = g_param_spec_uint64 ("duration", "Duration",
      "The current duration of the track", 0, G_MAXUINT64, GST_SECOND,
      G_PARAM_READABLE);
  g_object_class_install_property (object_class, ARG_DURATION,
      properties[ARG_DURATION]);

  /**
   * GESTrack:track-type:
   *
   * Type of stream the track outputs. This is used when creating the #GESTrack
   * to specify in generic terms what type of content will be outputted.
   *
   * It also serves as a 'fast' way to check what type of data will be outputted
   * from the #GESTrack without having to actually check the #GESTrack's caps
   * property.
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
   * Whether layer mixing is activated or not on the track.
   */
  properties[ARG_MIXING] = g_param_spec_boolean ("mixing", "Mixing",
      "Whether layer mixing is activated on the track or not",
      TRUE, G_PARAM_READWRITE | G_PARAM_CONSTRUCT);
  g_object_class_install_property (object_class, ARG_MIXING,
      properties[ARG_MIXING]);

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&ges_track_src_pad_template));

  /**
   * GESTrack::track-element-added:
   * @object: the #GESTrack
   * @effect: the #GESTrackElement that was added.
   *
   * Will be emitted after a track element was added to the track.
   */
  ges_track_signals[TRACK_ELEMENT_ADDED] =
      g_signal_new ("track-element-added", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_FIRST, 0, NULL, NULL, g_cclosure_marshal_generic,
      G_TYPE_NONE, 1, GES_TYPE_TRACK_ELEMENT);

  /**
   * GESTrack::track-element-removed:
   * @object: the #GESTrack
   * @effect: the #GESTrackElement that was removed.
   *
   * Will be emitted after a track element was removed from the track.
   */
  ges_track_signals[TRACK_ELEMENT_REMOVED] =
      g_signal_new ("track-element-removed", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_FIRST, 0, NULL, NULL, g_cclosure_marshal_generic,
      G_TYPE_NONE, 1, GES_TYPE_TRACK_ELEMENT);

  /**
   * GESTrack::commited:
   * @track: the #GESTrack
   */
  ges_track_signals[COMMITED] =
      g_signal_new ("commited", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 0);

  klass->get_mixing_element = NULL;
}

static void
ges_track_init (GESTrack * self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GES_TYPE_TRACK, GESTrackPrivate);

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

  g_signal_connect (G_OBJECT (self->priv->composition), "notify::duration",
      G_CALLBACK (composition_duration_cb), self);
  g_signal_connect (G_OBJECT (self->priv->composition), "commited",
      G_CALLBACK (composition_commited_cb), self);
}

/**
 * ges_track_new:
 * @type: The type of track
 * @caps: (transfer full): The caps to restrict the output of the track to.
 *
 * Creates a new #GESTrack with the given @type and @caps.
 *
 * The newly created track will steal a reference to the caps. If you wish to
 * use those caps elsewhere, you will have to take an extra reference.
 *
 * Returns: A new #GESTrack.
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
 * @track: a #GESTrack
 * @timeline: a #GESTimeline
 *
 * Sets @timeline as the timeline controlling @track.
 */
void
ges_track_set_timeline (GESTrack * track, GESTimeline * timeline)
{
  GST_DEBUG ("track:%p, timeline:%p", track, timeline);

  track->priv->timeline = timeline;
  track_resort_and_fill_gaps (track);
}

/**
 * ges_track_set_caps:
 * @track: a #GESTrack
 * @caps: the #GstCaps to set
 *
 * Sets the given @caps on the track.
 * Note that the capsfeatures of @caps will always be set
 * to ANY. If you want to restrict them, you should
 * do it in #ges_track_set_restriction_caps.
 */
void
ges_track_set_caps (GESTrack * track, const GstCaps * caps)
{
  GESTrackPrivate *priv;
  gint i;

  g_return_if_fail (GES_IS_TRACK (track));

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
 * @track: a #GESTrack
 * @caps: the #GstCaps to set
 *
 * Sets the given @caps as the caps the track has to output.
 */
void
ges_track_set_restriction_caps (GESTrack * track, const GstCaps * caps)
{
  GESTrackPrivate *priv;

  g_return_if_fail (GES_IS_TRACK (track));

  GST_DEBUG ("track:%p, restriction caps:%" GST_PTR_FORMAT, track, caps);
  g_return_if_fail (GST_IS_CAPS (caps));

  priv = track->priv;

  if (priv->restriction_caps)
    gst_caps_unref (priv->restriction_caps);
  priv->restriction_caps = gst_caps_copy (caps);

  g_object_set (priv->capsfilter, "caps", caps, NULL);

  g_object_notify (G_OBJECT (track), "restriction-caps");
}

/**
 * ges_track_update_restriction_caps:
 * @track: a #GESTrack
 * @caps: the #GstCaps to update with
 *
 * Updates the restriction caps by modifying all the fields present in @caps
 * in the original restriction caps. If for example the current restriction caps
 * are video/x-raw, format=I420, width=360 and @caps is video/x-raw, format=RGB,
 * the restriction caps will be updated to video/x-raw, format=RGB, width=360.
 *
 * Modification happens for each structure in the new caps, and
 * one can add new fields or structures through that function.
 */
void
ges_track_update_restriction_caps (GESTrack * self, const GstCaps * caps)
{
  guint i;
  GstCaps *new_restriction_caps = gst_caps_copy (self->priv->restriction_caps);

  for (i = 0; i < gst_caps_get_size (caps); i++) {
    GstStructure *new = gst_caps_get_structure (caps, i);

    if (gst_caps_get_size (new_restriction_caps) > i) {
      GstStructure *original = gst_caps_get_structure (new_restriction_caps, i);
      gst_structure_foreach (new, (GstStructureForeachFunc) update_field,
          original);
    } else
      gst_caps_append_structure (new_restriction_caps,
          gst_structure_copy (new));
  }

  ges_track_set_restriction_caps (self, new_restriction_caps);
}

/**
 * ges_track_set_mixing:
 * @track: a #GESTrack
 * @mixing: TRUE if the track should be mixing, FALSE otherwise.
 *
 * Sets if the #GESTrack should be mixing.
 */
void
ges_track_set_mixing (GESTrack * track, gboolean mixing)
{
  g_return_if_fail (GES_IS_TRACK (track));

  if (!track->priv->mixing_operation) {
    GST_DEBUG_OBJECT (track, "Track will be set to mixing = %d", mixing);
    track->priv->mixing = mixing;
    return;
  }

  if (mixing == track->priv->mixing) {
    GST_DEBUG_OBJECT (track, "Mixing is already set to the same value");
  }

  if (mixing) {
    /* increase ref count to hold the object */
    gst_object_ref (track->priv->mixing_operation);
    if (!nle_composition_add_object (track->priv->composition,
            track->priv->mixing_operation)) {
      GST_WARNING_OBJECT (track, "Could not add the mixer to our composition");
      return;
    }
  } else {
    if (!nle_composition_remove_object (track->priv->composition,
            track->priv->mixing_operation)) {
      GST_WARNING_OBJECT (track,
          "Could not remove the mixer from our composition");
      return;
    }
  }

  track->priv->mixing = mixing;

  GST_DEBUG_OBJECT (track, "The track has been set to mixing = %d", mixing);
}

/**
 * ges_track_add_element:
 * @track: a #GESTrack
 * @object: (transfer full): the #GESTrackElement to add
 *
 * Adds the given object to the track. Sets the object's controlling track,
 * and thus takes ownership of the @object.
 *
 * An object can only be added to one track.
 *
 * Returns: #TRUE if the object was properly added. #FALSE if the track does not
 * want to accept the object.
 */
gboolean
ges_track_add_element (GESTrack * track, GESTrackElement * object)
{
  g_return_val_if_fail (GES_IS_TRACK (track), FALSE);
  g_return_val_if_fail (GES_IS_TRACK_ELEMENT (object), FALSE);

  GST_DEBUG ("track:%p, object:%p", track, object);

  if (G_UNLIKELY (ges_track_element_get_track (object) != NULL)) {
    GST_WARNING ("Object already belongs to another track");
    return FALSE;
  }

  if (G_UNLIKELY (!ges_track_element_set_track (object, track))) {
    GST_ERROR ("Couldn't properly add the object to the Track");
    return FALSE;
  }

  GST_DEBUG ("Adding object %s to ourself %s",
      GST_OBJECT_NAME (ges_track_element_get_nleobject (object)),
      GST_OBJECT_NAME (track->priv->composition));

  if (G_UNLIKELY (!nle_composition_add_object (track->priv->composition,
              ges_track_element_get_nleobject (object)))) {
    GST_WARNING ("Couldn't add object to the NleComposition");
    return FALSE;
  }

  gst_object_ref_sink (object);
  g_hash_table_insert (track->priv->trackelements_iter, object,
      g_sequence_insert_sorted (track->priv->trackelements_by_start, object,
          (GCompareDataFunc) element_start_compare, NULL));

  ges_timeline_element_set_timeline (GES_TIMELINE_ELEMENT (object),
      track->priv->timeline);
  g_signal_emit (track, ges_track_signals[TRACK_ELEMENT_ADDED], 0,
      GES_TRACK_ELEMENT (object));

  g_signal_connect (GES_TRACK_ELEMENT (object), "notify::start",
      G_CALLBACK (sort_track_elements_cb), track);

  g_signal_connect (GES_TRACK_ELEMENT (object), "notify::duration",
      G_CALLBACK (sort_track_elements_cb), track);

  g_signal_connect (GES_TRACK_ELEMENT (object), "notify::priority",
      G_CALLBACK (sort_track_elements_cb), track);

  return TRUE;
}

/**
 * ges_track_get_elements:
 * @track: a #GESTrack
 *
 * Gets the #GESTrackElement contained in @track
 *
 * Returns: (transfer full) (element-type GESTrackElement): the list of
 * #GESTrackElement present in the Track sorted by priority and start.
 */
GList *
ges_track_get_elements (GESTrack * track)
{
  GList *ret = NULL;

  g_return_val_if_fail (GES_IS_TRACK (track), NULL);

  g_sequence_foreach (track->priv->trackelements_by_start,
      (GFunc) add_trackelement_to_list_foreach, &ret);

  ret = g_list_reverse (ret);
  return ret;
}

/**
 * ges_track_remove_element:
 * @track: a #GESTrack
 * @object: the #GESTrackElement to remove
 *
 * Removes the object from the track and unparents it.
 * Unparenting it means the reference owned by @track on the @object will be
 * removed. If you wish to use the @object after this function, make sure you
 * call gst_object_ref() before removing it from the @track.
 *
 * Returns: #TRUE if the object was removed, else #FALSE if the track
 * could not remove the object (like if it didn't belong to the track).
 */
gboolean
ges_track_remove_element (GESTrack * track, GESTrackElement * object)
{
  GSequenceIter *it;
  GESTrackPrivate *priv;

  g_return_val_if_fail (GES_IS_TRACK (track), FALSE);
  g_return_val_if_fail (GES_IS_TRACK_ELEMENT (object), FALSE);

  priv = track->priv;

  GST_DEBUG_OBJECT (track, "Removing %" GST_PTR_FORMAT, object);

  it = g_hash_table_lookup (priv->trackelements_iter, object);
  g_sequence_remove (it);
  track_resort_and_fill_gaps (track);

  if (remove_object_internal (track, object) == TRUE) {
    ges_timeline_element_set_timeline (GES_TIMELINE_ELEMENT (object), NULL);

    return TRUE;
  }

  g_hash_table_insert (track->priv->trackelements_iter, object,
      g_sequence_insert_sorted (track->priv->trackelements_by_start, object,
          (GCompareDataFunc) element_start_compare, NULL));

  return FALSE;
}

/**
 * ges_track_get_caps:
 * @track: a #GESTrack
 *
 * Get the #GstCaps this track is configured to output.
 *
 * Returns: The #GstCaps this track is configured to output.
 */
const GstCaps *
ges_track_get_caps (GESTrack * track)
{
  g_return_val_if_fail (GES_IS_TRACK (track), NULL);

  return track->priv->caps;
}

/**
 * ges_track_get_timeline:
 * @track: a #GESTrack
 *
 * Get the #GESTimeline this track belongs to. Can be %NULL.
 *
 * Returns: The #GESTimeline this track belongs to. Can be %NULL.
 */
const GESTimeline *
ges_track_get_timeline (GESTrack * track)
{
  g_return_val_if_fail (GES_IS_TRACK (track), NULL);

  return track->priv->timeline;
}

/**
 * ges_track_get_mixing:
 * @track: a #GESTrack
 *
 *  Gets if the underlying #NleComposition contains an expandable mixer.
 *
 * Returns: #True if there is a mixer, #False otherwise.
 */
gboolean
ges_track_get_mixing (GESTrack * track)
{
  g_return_val_if_fail (GES_IS_TRACK (track), FALSE);

  return track->priv->mixing;
}

/**
 * ges_track_commit:
 * @track: a #GESTrack
 *
 * Commits all the pending changes of the TrackElement contained in the
 * track.
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
ges_track_commit (GESTrack * track)
{
  g_return_val_if_fail (GES_IS_TRACK (track), FALSE);

  track_resort_and_fill_gaps (track);

  return nle_object_commit (NLE_OBJECT (track->priv->composition), TRUE);
}


/**
 * ges_track_set_create_element_for_gap_func:
 * @track: a #GESTrack
 * @func: (scope notified): The #GESCreateElementForGapFunc that will be used
 * to create #GstElement to fill gaps
 *
 * Sets the function that should be used to create the GstElement used to fill gaps.
 * To avoid to provide such a function we advice you to use the
 * #ges_audio_track_new and #ges_video_track_new constructor when possible.
 */
void
ges_track_set_create_element_for_gap_func (GESTrack * track,
    GESCreateElementForGapFunc func)
{
  g_return_if_fail (GES_IS_TRACK (track));

  track->priv->create_element_for_gaps = func;
}
