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

G_DEFINE_TYPE (GESTrack, ges_track, GST_TYPE_BIN);

enum
{
  ARG_0,
  ARG_CAPS,
  ARG_TYPE
};

static void pad_added_cb (GstElement * element, GstPad * pad, GESTrack * track);

static void
pad_removed_cb (GstElement * element, GstPad * pad, GESTrack * track);

#define C_ENUM(v) ((guint) v)
static void
register_ges_track_type_select_result (GType * id)
{
  static const GFlagsValue values[] = {
    {C_ENUM (GES_TRACK_TYPE_UNKNOWN), "GES_TRACK_TYPE_UNKNOWN", "unknown"},
    {C_ENUM (GES_TRACK_TYPE_AUDIO), "GES_TRACK_TYPE_AUDIO", "audio"},
    {C_ENUM (GES_TRACK_TYPE_VIDEO), "GES_TRACK_TYPE_VIDEO", "video"},
    {C_ENUM (GES_TRACK_TYPE_TEXT), "GES_TRACK_TYPE_TEXT", "text"},
    {C_ENUM (GES_TRACK_TYPE_CUSTOM), "GES_TRACK_TYPE_CUSTOM", "custom"},
    {0, NULL, NULL}
  };

  *id = g_flags_register_static ("GESTrackType", values);
}

GType
ges_track_type_get_type (void)
{
  static GType id;
  static GOnce once = G_ONCE_INIT;

  g_once (&once, (GThreadFunc) register_ges_track_type_select_result, &id);
  return id;
}

static void
ges_track_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GESTrack *track = GES_TRACK (object);

  switch (property_id) {
    case ARG_CAPS:
      gst_value_set_caps (value, track->caps);
      break;
    case ARG_TYPE:
      g_value_set_flags (value, track->type);
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

  /* FIXME : Remove all TrackObjects ! */

  if (track->composition) {
    gst_bin_remove (GST_BIN (object), track->composition);
    track->composition = NULL;
  }

  if (track->caps) {
    gst_caps_unref (track->caps);
    track->caps = NULL;
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
  g_object_class_install_property (object_class, ARG_CAPS,
      g_param_spec_boxed ("caps", "Caps",
          "Caps used to filter/choose the output stream",
          GST_TYPE_CAPS, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

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
  g_object_class_install_property (object_class, ARG_TYPE,
      g_param_spec_flags ("track-type", "TrackType",
          "Type of stream the track outputs",
          GES_TYPE_TRACK_TYPE, GES_TRACK_TYPE_CUSTOM,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
}

static void
ges_track_init (GESTrack * self)
{
  self->composition = gst_element_factory_make ("gnlcomposition", NULL);

  g_signal_connect (self->composition, "pad-added", (GCallback) pad_added_cb,
      self);
  g_signal_connect (self->composition, "pad-removed",
      (GCallback) pad_removed_cb, self);

  if (!gst_bin_add (GST_BIN (self), self->composition))
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
ges_track_video_raw_new ()
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
ges_track_audio_raw_new ()
{
  GESTrack *track;
  GstCaps *caps = gst_caps_from_string ("audio/x-raw-int;audio/x-raw-float");

  track = ges_track_new (GES_TRACK_TYPE_AUDIO, caps);

  return track;
}

void
ges_track_set_timeline (GESTrack * track, GESTimeline * timeline)
{
  GST_DEBUG ("track:%p, timeline:%p", track, timeline);

  track->timeline = timeline;
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
  GST_DEBUG ("track:%p, caps:%" GST_PTR_FORMAT, track, caps);

  g_return_if_fail (GST_IS_CAPS (caps));

  if (track->caps)
    gst_caps_unref (track->caps);
  track->caps = gst_caps_copy (caps);

  g_object_set (track->composition, "caps", caps, NULL);
  /* FIXME : update all trackobjects ? */
}

/**
 * ges_track_add_object:
 * @track: a #GESTrack
 * @object: the #GESTrackObject to add
 *
 * Adds the given object to the track.
 *
 * Returns: #TRUE if the object was properly added. #FALSE if the track does not
 * want to accept the object.
 */
gboolean
ges_track_add_object (GESTrack * track, GESTrackObject * object)
{
  GST_DEBUG ("track:%p, object:%p", track, object);

  if (G_UNLIKELY (object->track != NULL)) {
    GST_WARNING ("Object already belongs to another track");
    return FALSE;
  }

  if (G_UNLIKELY (object->gnlobject != NULL)) {
    GST_ERROR ("TrackObject doesn't have a gnlobject !");
    return FALSE;
  }

  if (G_UNLIKELY (!ges_track_object_set_track (object, track))) {
    GST_ERROR ("Couldn't properly add the object to the Track");
    return FALSE;
  }

  GST_DEBUG ("Adding object to ourself");

  /* make sure the object has a valid gnlobject ! */
  if (G_UNLIKELY (!gst_bin_add (GST_BIN (track->composition),
              object->gnlobject))) {
    GST_WARNING ("Couldn't add object to the GnlComposition");
    return FALSE;
  }

  return TRUE;
}

/**
 * ges_track_remove_object:
 * @track: a #GESTrack
 * @object: the #GESTrackObject to remove
 *
 * Removes the object from the track.
 *
 * Returns: #TRUE if the object was removed, else #FALSE if the track
 * could not remove the object (like if it didn't belong to the track).
 */
gboolean
ges_track_remove_object (GESTrack * track, GESTrackObject * object)
{
  GST_DEBUG ("track:%p, object:%p", track, object);

  if (G_UNLIKELY (object->track != track)) {
    GST_WARNING ("Object belongs to another track");
    return FALSE;
  }

  if (G_LIKELY (object->gnlobject != NULL)) {
    GST_DEBUG ("Removing GnlObject from composition");
    if (!gst_bin_remove (GST_BIN (track->composition), object->gnlobject)) {
      GST_WARNING ("Failed to remove gnlobject from composition");
      return FALSE;
    }
  }

  ges_track_object_set_track (object, NULL);

  return TRUE;
}

static void
pad_added_cb (GstElement * element, GstPad * pad, GESTrack * track)
{
  GST_DEBUG ("track:%p, pad %s:%s", track, GST_DEBUG_PAD_NAME (pad));

  /* ghost the pad */
  track->srcpad = gst_ghost_pad_new ("src", pad);

  gst_pad_set_active (track->srcpad, TRUE);

  gst_element_add_pad (GST_ELEMENT (track), track->srcpad);

  GST_DEBUG ("done");
}

static void
pad_removed_cb (GstElement * element, GstPad * pad, GESTrack * track)
{
  GST_DEBUG ("track:%p, pad %s:%s", track, GST_DEBUG_PAD_NAME (pad));

  if (G_LIKELY (track->srcpad)) {
    gst_pad_set_active (track->srcpad, FALSE);
    gst_element_remove_pad (GST_ELEMENT (track), track->srcpad);
    track->srcpad = NULL;
  }

  GST_DEBUG ("done");
}
