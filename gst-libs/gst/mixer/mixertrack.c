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

#include "mixertrack.h"

enum
{
  /* FILL ME */
  SIGNAL_VOLUME_CHANGED,
  SIGNAL_RECORD_TOGGLED,
  SIGNAL_MUTE_TOGGLED,
  LAST_SIGNAL
};

static void gst_mixer_track_class_init (GstMixerTrackClass * klass);
static void gst_mixer_track_init (GstMixerTrack * mixer);
static void gst_mixer_track_dispose (GObject * object);

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
  GObjectClass *object_klass = (GObjectClass *) klass;

  parent_class = g_type_class_ref (G_TYPE_OBJECT);

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
gst_mixer_track_init (GstMixerTrack * channel)
{
  channel->label = NULL;
  channel->min_volume = channel->max_volume = 0;
  channel->flags = 0;
  channel->num_channels = 0;
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
