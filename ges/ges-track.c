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
 * SECTION:ges-track
 * @short_description: Composition of objects
 *
 * Corresponds to one output format (i.e. audio OR video).
 *
 * Contains the compatible TrackElement(s).
 *
 * Wraps GNonLin's 'gnlcomposition' element.
 */

#include "ges-internal.h"
#include "ges-track.h"
#include "ges-track-element.h"
#include "ges-meta-container.h"

G_DEFINE_TYPE_WITH_CODE (GESTrack, ges_track, GST_TYPE_BIN,
    G_IMPLEMENT_INTERFACE (GES_TYPE_META_CONTAINER, NULL));

/* Structure that represents gaps and keep knowledge
 * of the gaps filled in the track */
typedef struct
{
  GstElement *gnlobj;

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

  GstElement *composition;      /* The composition associated with this track */
  GstPad *srcpad;               /* The source GhostPad */

  gboolean updating;

  /* Virtual method to create GstElement that fill gaps */
  GESCreateElementForGapFunc create_element_for_gaps;
};

enum
{
  ARG_0,
  ARG_CAPS,
  ARG_TYPE,
  ARG_DURATION,
  ARG_LAST,
  TRACK_ELEMENT_ADDED,
  TRACK_ELEMENT_REMOVED,
  LAST_SIGNAL
};

static guint ges_track_signals[LAST_SIGNAL] = { 0 };

static GParamSpec *properties[ARG_LAST];

static void pad_added_cb (GstElement * element, GstPad * pad, GESTrack * track);
static void
pad_removed_cb (GstElement * element, GstPad * pad, GESTrack * track);
static void composition_duration_cb (GstElement * composition, GParamSpec * arg
    G_GNUC_UNUSED, GESTrack * obj);

/* Private methods/functions/callbacks */
static void
add_trackelement_to_list_foreach (GESTrackElement * trackelement, GList ** list)
{
  g_object_ref (trackelement);
  *list = g_list_prepend (*list, trackelement);
}

static Gap *
gap_new (GESTrack * track, GstClockTime start, GstClockTime duration)
{
  GstElement *gnlsrc, *elem;

  Gap *new_gap;

  gnlsrc = gst_element_factory_make ("gnlsource", NULL);
  elem = track->priv->create_element_for_gaps (track);
  if (G_UNLIKELY (gst_bin_add (GST_BIN (gnlsrc), elem) == FALSE)) {
    GST_WARNING_OBJECT (track, "Could not create gap filler");

    if (gnlsrc)
      gst_object_unref (gnlsrc);

    if (elem)
      gst_object_unref (elem);

    return NULL;
  }

  if (G_UNLIKELY (gst_bin_add (GST_BIN (track->priv->composition),
              gnlsrc) == FALSE)) {
    GST_WARNING_OBJECT (track, "Could not add gap to the composition");

    if (gnlsrc)
      gst_object_unref (gnlsrc);

    if (elem)
      gst_object_unref (elem);

    return NULL;
  }

  new_gap = g_slice_new (Gap);
  new_gap->start = start;
  new_gap->duration = duration;
  new_gap->track = track;
  new_gap->gnlobj = gst_object_ref (gnlsrc);


  g_object_set (gnlsrc, "start", new_gap->start, "duration", new_gap->duration,
      "priority", 0, NULL);

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
  gst_bin_remove (GST_BIN (track->priv->composition), gap->gnlobj);
  gst_element_set_state (gap->gnlobj, GST_STATE_NULL);
  gst_object_unref (gap->gnlobj);

  g_slice_free (Gap, gap);
}

static inline void
update_gaps (GESTrack * track)
{
  Gap *gap;
  GSequenceIter *it;

  GESTrackElement *trackelement;
  GstClockTime start, end, duration = 0, timeline_duration;

  GESTrackPrivate *priv = track->priv;

  if (priv->create_element_for_gaps == NULL) {
    GST_INFO ("Not filling the gaps as no create_element_for_gaps vmethod"
        " provided");
    return;
  }

  /* 1- Remove all gaps */
  g_list_free_full (priv->gaps, (GDestroyNotify) free_gap);
  priv->gaps = NULL;

  /* 2- And recalculate gaps */
  for (it = g_sequence_get_begin_iter (priv->trackelements_by_start);
      g_sequence_iter_is_end (it) == FALSE; it = g_sequence_iter_next (it)) {
    trackelement = g_sequence_get (it);

    start = _START (trackelement);
    end = start + _DURATION (trackelement);

    if (start > duration) {
      /* 3- Fill gap */
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
}

static inline void
resort_and_fill_gaps (GESTrack * track)
{
  g_sequence_sort (track->priv->trackelements_by_start,
      (GCompareDataFunc) element_start_compare, NULL);

  if (track->priv->updating == TRUE) {
    update_gaps (track);
  }
}

/* callbacks */
static void
timeline_duration_changed_cb (GESTimeline * timeline,
    GParamSpec * arg, GESTrack * track)
{
  GESTrackPrivate *priv = track->priv;

  /* Remove the last gap on the timeline if not needed anymore */
  if (priv->updating == TRUE && priv->gaps) {
    Gap *gap = (Gap *) priv->gaps->data;
    GstClockTime tl_duration = ges_timeline_get_duration (timeline);

    if (gap->start + gap->duration > tl_duration) {
      free_gap (gap);
      priv->gaps = g_list_remove (priv->gaps, gap);
    }
  }
}

static void
sort_track_elements_cb (GESTrackElement * child,
    GParamSpec * arg G_GNUC_UNUSED, GESTrack * track)
{
  resort_and_fill_gaps (track);
}

static void
pad_added_cb (GstElement * element, GstPad * pad, GESTrack * track)
{
  GESTrackPrivate *priv = track->priv;

  GST_DEBUG ("track:%p, pad %s:%s", track, GST_DEBUG_PAD_NAME (pad));

  /* ghost the pad */
  priv->srcpad = gst_ghost_pad_new ("src", pad);

  gst_pad_set_active (priv->srcpad, TRUE);

  gst_element_add_pad (GST_ELEMENT (track), priv->srcpad);

  GST_DEBUG ("done");
}

static void
pad_removed_cb (GstElement * element, GstPad * pad, GESTrack * track)
{
  GESTrackPrivate *priv = track->priv;

  GST_DEBUG ("track:%p, pad %s:%s", track, GST_DEBUG_PAD_NAME (pad));

  if (G_LIKELY (priv->srcpad)) {
    gst_pad_set_active (priv->srcpad, FALSE);
    gst_element_remove_pad (GST_ELEMENT (track), priv->srcpad);
    priv->srcpad = NULL;
  }

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

/* GESCreateElementForGapFunc Gaps filler for raw tracks */
static GstElement *
create_element_for_raw_audio_gap (GESTrack * track)
{
  GstElement *elem;

  elem = gst_element_factory_make ("audiotestsrc", NULL);
  g_object_set (elem, "wave", 4, NULL);

  return elem;
}

static GstElement *
create_element_for_raw_video_gap (GESTrack * track)
{
  return gst_parse_bin_from_description
      ("videotestsrc pattern=2 name=src ! capsfilter caps=video/x-raw", TRUE,
      NULL);
}

/* Remove @object from @track, but keeps it in the sequence this is needed
 * when finalizing as we can not change a GSequence at the same time we are
 * accessing it
 */
static gboolean
remove_object_internal (GESTrack * track, GESTrackElement * object)
{
  GESTrackPrivate *priv;
  GstElement *gnlobject;

  GST_DEBUG_OBJECT (track, "object:%p", object);

  priv = track->priv;

  if (G_UNLIKELY (ges_track_element_get_track (object) != track)) {
    GST_WARNING ("Object belongs to another track");
    return FALSE;
  }

  if ((gnlobject = ges_track_element_get_gnlobject (object))) {
    GST_DEBUG ("Removing GnlObject '%s' from composition '%s'",
        GST_ELEMENT_NAME (gnlobject), GST_ELEMENT_NAME (priv->composition));

    if (!gst_bin_remove (GST_BIN (priv->composition), gnlobject)) {
      GST_WARNING ("Failed to remove gnlobject from composition");
      return FALSE;
    }

    gst_element_set_state (gnlobject, GST_STATE_NULL);
  }

  g_signal_handlers_disconnect_by_func (object, sort_track_elements_cb, NULL);

  ges_track_element_set_track (object, NULL);

  g_signal_emit (track, ges_track_signals[TRACK_ELEMENT_REMOVED], 0,
      GES_TRACK_ELEMENT (object));

  g_object_unref (object);

  return TRUE;
}

static void
dispose_trackelements_foreach (GESTrackElement * trackelement, GESTrack * track)
{
  GESClip *clip;

  clip = ges_track_element_get_clip (trackelement);

  remove_object_internal (track, trackelement);
  ges_clip_release_track_element (clip, trackelement);
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

  if (priv->composition) {
    gst_bin_remove (GST_BIN (object), priv->composition);
    priv->composition = NULL;
  }

  if (priv->caps) {
    gst_caps_unref (priv->caps);
    priv->caps = NULL;
  }

  G_OBJECT_CLASS (ges_track_parent_class)->dispose (object);
}

static void
ges_track_finalize (GObject * object)
{
  G_OBJECT_CLASS (ges_track_parent_class)->finalize (object);
}

static void
ges_track_class_init (GESTrackClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  g_type_class_add_private (klass, sizeof (GESTrackPrivate));

  object_class->get_property = ges_track_get_property;
  object_class->set_property = ges_track_set_property;
  object_class->dispose = ges_track_dispose;
  object_class->finalize = ges_track_finalize;

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
      GST_TYPE_CAPS, G_PARAM_READWRITE | G_PARAM_CONSTRUCT);
  g_object_class_install_property (object_class, ARG_CAPS,
      properties[ARG_CAPS]);

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
   * GESTrack::track-element-added:
   * @object: the #GESTrack
   * @effect: the #GESTrackElement that was added.
   *
   * Will be emitted after a track element was added to the track.
   *
   * Since: 0.10.2
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
   *
   * Since: 0.10.2
   */
  ges_track_signals[TRACK_ELEMENT_REMOVED] =
      g_signal_new ("track-element-removed", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_FIRST, 0, NULL, NULL, g_cclosure_marshal_generic,
      G_TYPE_NONE, 1, GES_TYPE_TRACK_ELEMENT);
}

static void
ges_track_init (GESTrack * self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GES_TYPE_TRACK, GESTrackPrivate);

  self->priv->composition = gst_element_factory_make ("gnlcomposition", NULL);
  self->priv->updating = TRUE;
  self->priv->trackelements_by_start = g_sequence_new (NULL);
  self->priv->trackelements_iter =
      g_hash_table_new (g_direct_hash, g_direct_equal);
  self->priv->create_element_for_gaps = NULL;
  self->priv->gaps = NULL;

  g_signal_connect (G_OBJECT (self->priv->composition), "notify::duration",
      G_CALLBACK (composition_duration_cb), self);
  g_signal_connect (self->priv->composition, "pad-added",
      (GCallback) pad_added_cb, self);
  g_signal_connect (self->priv->composition, "pad-removed",
      (GCallback) pad_removed_cb, self);

  if (!gst_bin_add (GST_BIN (self), self->priv->composition))
    GST_ERROR ("Couldn't add composition to bin !");
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

  track = g_object_new (GES_TYPE_TRACK, "caps", caps, "track-type", type, NULL);
  if (type == GES_TRACK_TYPE_VIDEO) {
    tmpcaps = gst_caps_new_empty_simple ("video/x-raw");

    if (gst_caps_is_equal (caps, tmpcaps))
      ges_track_set_create_element_for_gap_func (track,
          create_element_for_raw_video_gap);

    gst_caps_unref (tmpcaps);
  } else if (type == GES_TRACK_TYPE_AUDIO) {
    tmpcaps = gst_caps_new_empty_simple ("audio/x-raw");

    if (gst_caps_is_equal (caps, tmpcaps))
      ges_track_set_create_element_for_gap_func (track,
          create_element_for_raw_audio_gap);

    gst_caps_unref (tmpcaps);
  }
  gst_caps_unref (caps);

  return track;
}

/**
 * ges_track_video_raw_new:
 *
 * Creates a new #GESTrack of type #GES_TRACK_TYPE_VIDEO and with generic
 * raw video caps ("video/x-raw");
 *
 * Returns: A new #GESTrack.
 */
GESTrack *
ges_track_video_raw_new (void)
{
  GESTrack *track;
  GstCaps *caps = gst_caps_new_empty_simple ("video/x-raw");

  track = ges_track_new (GES_TRACK_TYPE_VIDEO, caps);
  ges_track_set_create_element_for_gap_func (track,
      create_element_for_raw_video_gap);

  GST_DEBUG_OBJECT (track, "New raw video track");

  return track;
}

/**
 * ges_track_audio_raw_new:
 *
 * Creates a new #GESTrack of type #GES_TRACK_TYPE_AUDIO and with generic
 * raw audio caps ("audio/x-raw");
 *
 * Returns: A new #GESTrack.
 */
GESTrack *
ges_track_audio_raw_new (void)
{
  GESTrack *track;
  GstCaps *caps = gst_caps_new_empty_simple ("audio/x-raw");

  track = ges_track_new (GES_TRACK_TYPE_AUDIO, caps);
  ges_track_set_create_element_for_gap_func (track,
      create_element_for_raw_audio_gap);

  GST_DEBUG_OBJECT (track, "New raw audio track %p",
      track->priv->create_element_for_gaps);
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

  if (track->priv->timeline)
    g_signal_handlers_disconnect_by_func (track->priv->timeline,
        timeline_duration_changed_cb, track);

  if (timeline)
    g_signal_connect (timeline, "notify::duration",
        G_CALLBACK (timeline_duration_changed_cb), track);

  track->priv->timeline = timeline;
}

/**
 * ges_track_set_caps:
 * @track: a #GESTrack
 * @caps: the #GstCaps to set
 *
 * Sets the given @caps on the track.
 */
void
ges_track_set_caps (GESTrack * track, const GstCaps * caps)
{
  GESTrackPrivate *priv;

  g_return_if_fail (GES_IS_TRACK (track));

  GST_DEBUG ("track:%p, caps:%" GST_PTR_FORMAT, track, caps);
  g_return_if_fail (GST_IS_CAPS (caps));

  priv = track->priv;

  if (priv->caps)
    gst_caps_unref (priv->caps);
  priv->caps = gst_caps_copy (caps);

  g_object_set (priv->composition, "caps", caps, NULL);
  /* FIXME : update all trackelements ? */
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
      GST_OBJECT_NAME (ges_track_element_get_gnlobject (object)),
      GST_OBJECT_NAME (track->priv->composition));

  if (G_UNLIKELY (!gst_bin_add (GST_BIN (track->priv->composition),
              ges_track_element_get_gnlobject (object)))) {
    GST_WARNING ("Couldn't add object to the GnlComposition");
    return FALSE;
  }

  g_object_ref_sink (object);
  g_hash_table_insert (track->priv->trackelements_iter, object,
      g_sequence_insert_sorted (track->priv->trackelements_by_start, object,
          (GCompareDataFunc) element_start_compare, NULL));

  g_signal_emit (track, ges_track_signals[TRACK_ELEMENT_ADDED], 0,
      GES_TRACK_ELEMENT (object));

  g_signal_connect (GES_TRACK_ELEMENT (object), "notify::start",
      G_CALLBACK (sort_track_elements_cb), track);

  g_signal_connect (GES_TRACK_ELEMENT (object), "notify::duration",
      G_CALLBACK (sort_track_elements_cb), track);

  g_signal_connect (GES_TRACK_ELEMENT (object), "notify::priority",
      G_CALLBACK (sort_track_elements_cb), track);

  resort_and_fill_gaps (track);

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
 * call g_object_ref() before removing it from the @track.
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

  if (remove_object_internal (track, object) == TRUE) {
    it = g_hash_table_lookup (priv->trackelements_iter, object);
    g_sequence_remove (it);

    resort_and_fill_gaps (track);

    return TRUE;
  }

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
 * ges_track_enable_update:
 * @track: a #GESTrack
 * @enabled: Whether the track should update on every change or not.
 *
 * Control whether the track is updated for every change happening within.
 *
 * Users will want to use this method with %FALSE before doing lots of changes,
 * and then call again with %TRUE for the changes to take effect in one go.
 *
 * Returns: %TRUE if the update status could be changed, else %FALSE.
 */
gboolean
ges_track_enable_update (GESTrack * track, gboolean enabled)
{
  gboolean update;

  g_return_val_if_fail (GES_IS_TRACK (track), FALSE);

  g_object_set (track->priv->composition, "update", enabled, NULL);
  g_object_get (track->priv->composition, "update", &update, NULL);

  track->priv->updating = update;

  if (update == TRUE)
    resort_and_fill_gaps (track);

  return update == enabled;
}

/**
 * ges_track_is_updating:
 * @track: a #GESTrack
 *
 * Get whether the track is updated for every change happening within or not.
 *
 * Returns: %TRUE if @track is updating on every changes, else %FALSE.
 */
gboolean
ges_track_is_updating (GESTrack * track)
{
  g_return_val_if_fail (GES_IS_TRACK (track), FALSE);

  return track->priv->updating;
}


/**
 * ges_track_set_create_element_for_gap_func:
 * @track: a #GESTrack
 * @func: (scope notified): The #GESCreateElementForGapFunc that will be used
 * to create #GstElement to fill gaps
 *
 * Sets the function that should be used to create the GstElement used to fill gaps.
 * To avoid to provide such a function we advice you to use the
 * #ges_track_audio_raw_new and #ges_track_video_raw_new constructor when possible.
 */
void
ges_track_set_create_element_for_gap_func (GESTrack * track,
    GESCreateElementForGapFunc func)
{
  g_return_if_fail (GES_IS_TRACK (track));

  track->priv->create_element_for_gaps = func;
}
