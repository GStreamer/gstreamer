/* GStreamer Mixer
 * Copyright (C) 2003 Ronald Bultje <rbultje@ronald.bitfreak.net>
 *
 * mixertrack.h: mixer track object
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

#ifndef __GST_MIXER_TRACK_H__
#define __GST_MIXER_TRACK_H__

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_TYPE_MIXER_TRACK \
  (gst_mixer_track_get_type ())
#define GST_MIXER_TRACK(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_MIXER_TRACK, \
			       GstMixerTrack))
#define GST_MIXER_TRACK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_MIXER_TRACK, \
			    GstMixerTrackClass))
#define GST_IS_MIXER_TRACK(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_MIXER_TRACK))
#define GST_IS_MIXER_TRACK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_MIXER_TRACK))

/*
 * Naming:
 *
 * A track is a single input/output stream (e.g. line-in,
 * microphone, etc.). Channels are then single streams
 * within a track. A mono stream has one channel, a stereo
 * stream has two, etc.
 *
 * Input tracks can have 'recording' enabled, which means
 * that any input will be hearable into the speakers that
 * are attached to the output. Mute is obvious. A track
 * flagged as master is the master volume track on this
 * mixer, which means that setting this track will change
 * the hearable volume on any output.
 */

typedef enum {
  GST_MIXER_TRACK_INPUT  = (1<<0),
  GST_MIXER_TRACK_OUTPUT = (1<<1),
  GST_MIXER_TRACK_MUTE   = (1<<2),
  GST_MIXER_TRACK_RECORD = (1<<3),
  GST_MIXER_TRACK_MASTER = (1<<4),
  GST_MIXER_TRACK_SOFTWARE = (1<<5)
} GstMixerTrackFlags;

#define GST_MIXER_TRACK_HAS_FLAG(channel, flag) \
  ((channel)->flags & flag)

typedef struct _GstMixerTrack {
  GObject            parent;

  gchar             *label;
  GstMixerTrackFlags flags;
  gint               num_channels,
	             min_volume,
	             max_volume;
} GstMixerTrack;

typedef struct _GstMixerTrackClass {
  GObjectClass parent;

  /* signals */
  void (* mute_toggled)   (GstMixerTrack *channel,
			   gboolean       mute);
  void (* record_toggled) (GstMixerTrack *channel,
			   gboolean       record);
  void (* volume_changed) (GstMixerTrack *channel,
			   gint          *volumes);

  gpointer _gst_reserved[GST_PADDING];
} GstMixerTrackClass;

GType		gst_mixer_track_get_type	(void);

G_END_DECLS

#endif /* __GST_MIXER_TRACK_H__ */
