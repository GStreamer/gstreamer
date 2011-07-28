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
 * SECTION:ges-track
 * @short_description: Composition of objects
 *
 * Corresponds to one output format (i.e. audio OR video).
 *
 * Contains the compatible TrackObject(s).
 *
 * Wraps GNonLin's 'gnlcomposition' element.
 */

#include "ges-internal.h"
#include "ges-track.h"
#include "ges-track-object.h"
#include "gesmarshal.h"

G_DEFINE_TYPE (GESTrack, ges_track, GST_TYPE_BIN);

struct _GESTrackPrivate
{
  /*< private > */
  GESTimeline *timeline;
  GList *trackobjects;
  guint64 duration;

  GstCaps *caps;

  GstElement *composition;      /* The composition associated with this track */
  GstPad *srcpad;               /* The source GhostPad */
};

enum
{
  ARG_0,
  ARG_CAPS,
  ARG_TYPE,
  ARG_DURATION,
  ARG_LAST,
  TRACK_OBJECT_ADDED,
  TRACK_OBJECT_REMOVED,
  LAST_SIGNAL
};

static guint ges_track_signals[LAST_SIGNAL] = { 0 };

static GParamSpec *properties[ARG_LAST];

static void pad_added_cb (GstElement * element, GstPad * pad, GESTrack * track);
static void
pad_removed_cb (GstElement * element, GstPad * pad, GESTrack * track);
static void composition_duration_cb (GstElement * composition, GParamSpec * arg
    G_GNUC_UNUSED, GESTrack * obj);
static void
sort_track_objects_cb (GESTrackObject * child,
    GParamSpec * arg G_GNUC_UNUSED, GESTrack * track);

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

  while (priv->trackobjects) {
    GESTrackObject *trobj = GES_TRACK_OBJECT (priv->trackobjects->data);
    ges_track_remove_object (track, trobj);
    ges_timeline_object_release_track_object ((GESTimelineObject *)
        ges_track_object_get_timeline_object (trobj), trobj);
  }

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
   * GESTrack:caps
   *
   * Caps used to filter/choose the output stream. This is generally set to
   * a generic set of caps like 'video/x-raw-rgb;video/x-raw-yuv' for raw video.
   *
   * Default value: #GST_CAPS_ANY.
   */
  properties[ARG_CAPS] = g_param_spec_boxed ("caps", "Caps",
      "Caps used to filter/choose the output stream",
      GST_TYPE_CAPS, G_PARAM_READWRITE | G_PARAM_CONSTRUCT);
  g_object_class_install_property (object_class, ARG_CAPS,
      properties[ARG_CAPS]);

  /**
   * GESTrack:duration
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
   * GESTrack:track-type
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
   * GESTrack::track-object-added
   * @object: the #GESTrack
   * @effect: the #GESTrackObject that was added.
   *
   * Will be emitted after a track object was added to the track.
   *
   * Since: 0.10.2
   */
  ges_track_signals[TRACK_OBJECT_ADDED] =
      g_signal_new ("track-object-added", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_FIRST, 0, NULL, NULL, ges_marshal_VOID__OBJECT,
      G_TYPE_NONE, 1, GES_TYPE_TRACK_OBJECT);

  /**
   * GESTrack::track-object-removed
   * @object: the #GESTrack
   * @effect: the #GESTrackObject that was removed.
   *
   * Will be emitted after a track object was removed from the track.
   *
   * Since: 0.10.2
   */
  ges_track_signals[TRACK_OBJECT_REMOVED] =
      g_signal_new ("track-object-removed", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_FIRST, 0, NULL, NULL, ges_marshal_VOID__OBJECT,
      G_TYPE_NONE, 1, GES_TYPE_TRACK_OBJECT);
}

static void
ges_track_init (GESTrack * self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GES_TYPE_TRACK, GESTrackPrivate);

  self->priv->composition = gst_element_factory_make ("gnlcomposition", NULL);

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
 * @caps: The caps to restrict the output of the track to.
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

  track = g_object_new (GES_TYPE_TRACK, "caps", caps, "track-type", type, NULL);
  gst_caps_unref (caps);

  return track;
}

/**
 * ges_track_video_raw_new:
 *
 * Creates a new #GESTrack of type #GES_TRACK_TYPE_VIDEO and with generic
 * raw video caps ("video/x-raw-yuv;video/x-raw-rgb");
 *
 * Returns: A new #GESTrack.
 */
GESTrack *
ges_track_video_raw_new (void)
{
  GESTrack *track;
  GstCaps *caps = gst_caps_from_string ("video/x-raw-yuv;video/x-raw-rgb");

  track = ges_track_new (GES_TRACK_TYPE_VIDEO, caps);

  return track;
}

/**
 * ges_track_audio_raw_new:
 *
 * Creates a new #GESTrack of type #GES_TRACK_TYPE_AUDIO and with generic
 * raw audio caps ("audio/x-raw-int;audio/x-raw-float");
 *
 * Returns: A new #GESTrack.
 */
GESTrack *
ges_track_audio_raw_new (void)
{
  GESTrack *track;
  GstCaps *caps = gst_caps_from_string ("audio/x-raw-int;audio/x-raw-float");

  track = ges_track_new (GES_TRACK_TYPE_AUDIO, caps);

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
  g_return_if_fail (GST_IS_CAPS (caps));

  GST_DEBUG ("track:%p, caps:%" GST_PTR_FORMAT, track, caps);

  priv = track->priv;

  if (priv->caps)
    gst_caps_unref (priv->caps);
  priv->caps = gst_caps_copy (caps);

  g_object_set (priv->composition, "caps", caps, NULL);
  /* FIXME : update all trackobjects ? */
}


/* FIXME : put the compare function in the utils */

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

/**
 * ges_track_add_object:
 * @track: a #GESTrack
 * @object: (transfer full): the #GESTrackObject to add
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
ges_track_add_object (GESTrack * track, GESTrackObject * object)
{
  g_return_val_if_fail (GES_IS_TRACK (track), FALSE);
  g_return_val_if_fail (GES_IS_TRACK_OBJECT (object), FALSE);

  GST_DEBUG ("track:%p, object:%p", track, object);

  if (G_UNLIKELY (ges_track_object_get_track (object) != NULL)) {
    GST_WARNING ("Object already belongs to another track");
    return FALSE;
  }

  /* At this point, the track object shouldn't have any gnlobject since
   * it hasn't been added to a track yet.
   * FIXME : This check seems a bit obsolete */
  if (G_UNLIKELY (ges_track_object_get_gnlobject (object) != NULL)) {
    GST_ERROR ("TrackObject already controls a gnlobject !");
    return FALSE;
  }

  if (G_UNLIKELY (!ges_track_object_set_track (object, track))) {
    GST_ERROR ("Couldn't properly add the object to the Track");
    return FALSE;
  }

  GST_DEBUG ("Adding object to ourself");

  if (G_UNLIKELY (!gst_bin_add (GST_BIN (track->priv->composition),
              ges_track_object_get_gnlobject (object)))) {
    GST_WARNING ("Couldn't add object to the GnlComposition");
    return FALSE;
  }

  g_object_ref_sink (object);
  track->priv->trackobjects =
      g_list_insert_sorted (track->priv->trackobjects, object,
      (GCompareFunc) objects_start_compare);

  g_signal_emit (track, ges_track_signals[TRACK_OBJECT_ADDED], 0,
      GES_TRACK_OBJECT (object));

  g_signal_connect (GES_TRACK_OBJECT (object), "notify::start",
      G_CALLBACK (sort_track_objects_cb), track);

  g_signal_connect (GES_TRACK_OBJECT (object), "notify::priority",
      G_CALLBACK (sort_track_objects_cb), track);

  return TRUE;
}

GList *
ges_track_get_objects (GESTrack * track)
{
  GList *ret = NULL;
  GList *tmp;

  g_return_val_if_fail (GES_IS_TRACK (track), NULL);

  for (tmp = track->priv->trackobjects; tmp; tmp = tmp->next) {
    ret = g_list_prepend (ret, tmp->data);
    g_object_ref (tmp->data);
  }

  ret = g_list_reverse (ret);
  return ret;
}

/**
 * ges_track_remove_object:
 * @track: a #GESTrack
 * @object: the #GESTrackObject to remove
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
ges_track_remove_object (GESTrack * track, GESTrackObject * object)
{
  GESTrackPrivate *priv;
  GstElement *gnlobject;

  g_return_val_if_fail (GES_IS_TRACK (track), FALSE);
  g_return_val_if_fail (GES_IS_TRACK_OBJECT (object), FALSE);

  GST_DEBUG ("track:%p, object:%p", track, object);

  priv = track->priv;

  if (G_UNLIKELY (ges_track_object_get_track (object) != track)) {
    GST_WARNING ("Object belongs to another track");
    return FALSE;
  }

  if ((gnlobject = ges_track_object_get_gnlobject (object))) {
    GST_DEBUG ("Removing GnlObject '%s' from composition '%s'",
        GST_ELEMENT_NAME (gnlobject), GST_ELEMENT_NAME (priv->composition));
    if (!gst_bin_remove (GST_BIN (priv->composition), gnlobject)) {
      GST_WARNING ("Failed to remove gnlobject from composition");
      return FALSE;
    }
  }

  ges_track_object_set_track (object, NULL);
  priv->trackobjects = g_list_remove (priv->trackobjects, object);

  g_signal_emit (track, ges_track_signals[TRACK_OBJECT_REMOVED], 0,
      GES_TRACK_OBJECT (object));

  g_object_unref (object);

  return TRUE;
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
    GParamSpec * arg G_GNUC_UNUSED, GESTrack * obj)
{
  guint64 duration;

  g_object_get (composition, "duration", &duration, NULL);


  if (obj->priv->duration != duration) {
    GST_DEBUG ("composition duration : %" GST_TIME_FORMAT " current : %"
        GST_TIME_FORMAT, GST_TIME_ARGS (duration),
        GST_TIME_ARGS (obj->priv->duration));

    obj->priv->duration = duration;

#if GLIB_CHECK_VERSION(2,26,0)
    g_object_notify_by_pspec (G_OBJECT (obj), properties[ARG_DURATION]);
#else
    g_object_notify (G_OBJECT (obj), "duration");
#endif
  }
}

static void
sort_track_objects_cb (GESTrackObject * child,
    GParamSpec * arg G_GNUC_UNUSED, GESTrack * track)
{
  track->priv->trackobjects =
      g_list_sort (track->priv->trackobjects,
      (GCompareFunc) objects_start_compare);
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
