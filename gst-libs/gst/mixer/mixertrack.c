/* GStreamer Mixer
 * Copyright (C) 2003 Ronald Bultje <rbultje@ronald.bitfreak.net>
 *
 * mixertrack.c: mixer track object design
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <glib.h>

#include "mixertrack.h"

enum
{
  /* FILL ME */
  SIGNAL_VOLUME_CHANGED,
  SIGNAL_RECORD_TOGGLED,
  SIGNAL_MUTE_TOGGLED,
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_LABEL,
  ARG_MIN_VOLUME,
  ARG_MAX_VOLUME,
  ARG_FLAGS,
  ARG_NUM_CHANNELS
};

static void gst_mixer_track_class_init (GstMixerTrackClass * klass);
static void gst_mixer_track_init (GstMixerTrack * mixer);
static void gst_mixer_track_dispose (GObject * object);

static void gst_mixer_track_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GObjectClass *parent_class = NULL;
static guint signals[LAST_SIGNAL] = { 0 };

GType
gst_mixer_track_get_type (void)
{
  static GType gst_mixer_track_type = 0;

  if (!gst_mixer_track_type) {
    static const GTypeInfo mixer_track_info = {
      sizeof (GstMixerTrackClass),
      NULL,
      NULL,
      (GClassInitFunc) gst_mixer_track_class_init,
      NULL,
      NULL,
      sizeof (GstMixerTrack),
      0,
      (GInstanceInitFunc) gst_mixer_track_init,
      NULL
    };

    gst_mixer_track_type =
        g_type_register_static (G_TYPE_OBJECT,
        "GstMixerTrack", &mixer_track_info, 0);
  }

  return gst_mixer_track_type;
}

static void
gst_mixer_track_class_init (GstMixerTrackClass * klass)
{
  GObjectClass *object_klass = G_OBJECT_CLASS (klass);

  parent_class = g_type_class_ref (G_TYPE_OBJECT);

  object_klass->get_property = gst_mixer_track_get_property;

  g_object_class_install_property (object_klass, ARG_LABEL,
      g_param_spec_string ("label", "Track label",
          "The label assigned to the track", NULL, G_PARAM_READABLE));

  g_object_class_install_property (object_klass, ARG_MIN_VOLUME,
      g_param_spec_int ("min_volume", "Minimum volume level",
          "The minimum possible volume level", G_MININT32, G_MAXINT,
          0, G_PARAM_READABLE));

  g_object_class_install_property (object_klass, ARG_MAX_VOLUME,
      g_param_spec_int ("max_volume", "Maximum volume level",
          "The maximum possible volume level", G_MININT32, G_MAXINT,
          0, G_PARAM_READABLE));

  g_object_class_install_property (object_klass, ARG_FLAGS,
      g_param_spec_uint ("flags", "Flags",
          "Flags indicating the type of mixer track",
          0, G_MAXUINT32, 0, G_PARAM_READABLE));

  g_object_class_install_property (object_klass, ARG_NUM_CHANNELS,
      g_param_spec_int ("num_channels", "Number of channels",
          "The number of channels contained within the track",
          0, G_MAXINT, 0, G_PARAM_READABLE));

  signals[SIGNAL_RECORD_TOGGLED] =
      g_signal_new ("record_toggled", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstMixerTrackClass,
          record_toggled),
      NULL, NULL, g_cclosure_marshal_VOID__BOOLEAN,
      G_TYPE_NONE, 1, G_TYPE_BOOLEAN);
  signals[SIGNAL_MUTE_TOGGLED] =
      g_signal_new ("mute_toggled", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstMixerTrackClass,
          mute_toggled),
      NULL, NULL, g_cclosure_marshal_VOID__BOOLEAN,
      G_TYPE_NONE, 1, G_TYPE_BOOLEAN);
  signals[SIGNAL_VOLUME_CHANGED] =
      g_signal_new ("volume_changed", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstMixerTrackClass,
          volume_changed),
      NULL, NULL, g_cclosure_marshal_VOID__POINTER,
      G_TYPE_NONE, 1, G_TYPE_POINTER);

  object_klass->dispose = gst_mixer_track_dispose;
}

static void
gst_mixer_track_init (GstMixerTrack * mixer_track)
{
  mixer_track->label = NULL;
  mixer_track->min_volume = mixer_track->max_volume = 0;
  mixer_track->flags = 0;
  mixer_track->num_channels = 0;
}

static void
gst_mixer_track_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstMixerTrack *mixer_track;

  mixer_track = GST_MIXER_TRACK (object);

  switch (prop_id) {
    case ARG_LABEL:
      g_value_set_string (value, mixer_track->label);
      break;
    case ARG_MIN_VOLUME:
      g_value_set_int (value, mixer_track->min_volume);
      break;
    case ARG_MAX_VOLUME:
      g_value_set_int (value, mixer_track->max_volume);
      break;
    case ARG_FLAGS:
      g_value_set_uint (value, (guint32) mixer_track->flags);
      break;
    case ARG_NUM_CHANNELS:
      g_value_set_int (value, mixer_track->num_channels);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_mixer_track_dispose (GObject * object)
{
  GstMixerTrack *channel = GST_MIXER_TRACK (object);

  if (channel->label)
    g_free (channel->label);

  if (parent_class->dispose)
    parent_class->dispose (object);
}
