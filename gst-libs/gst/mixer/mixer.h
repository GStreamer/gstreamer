/* GStreamer Mixer
 * Copyright (C) 2003 Ronald Bultje <rbultje@ronald.bitfreak.net>
 *
 * mixer.h: mixer interface design
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

#ifndef __GST_MIXER_H__
#define __GST_MIXER_H__

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_TYPE_MIXER \
  (gst_mixer_get_type ())
#define GST_MIXER(obj) \
  (GST_INTERFACE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_MIXER, GstMixer))
#define GST_MIXER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_MIXER, GstMixerClass))
#define GST_IS_MIXER(obj) \
  (GST_INTERFACE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_MIXER))
#define GST_IS_MIXER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_MIXER))
#define GST_MIXER_GET_CLASS(inst) \
  (G_TYPE_INSTANCE_GET_INTERFACE ((inst), GST_TYPE_MIXER, GstMixerClass))

/* In this interface, a `track' is a unit of recording or playback, pretty much
 * equivalent to what comes in or goes out through a GstPad. Each track can have
 * one or more `channels', which are logical parts of the track. A `stereo
 * track', then, would be one stream with two channels, while a `mono track'
 * would be a stream with a single channel. More complex examples are possible
 * as well ; for example, professional audio hardware might handle audio tracks
 * with 8 or 16 channels each.
 *
 * All these are audio terms. I don't know exactly what this would translate to
 * for video, but a track might be an entire video stream, and a channel might
 * be the information for one of the colors in the stream.
 */

#define GST_MIXER_TRACK_INPUT  (1<<0)
#define GST_MIXER_TRACK_OUTPUT (1<<1)
#define GST_MIXER_TRACK_MUTE   (1<<2)
#define GST_MIXER_TRACK_RECORD (1<<3)

typedef struct _GstMixerTrack {
  gchar *label;
  gint   num_channels,
         flags,
	 min_volume, max_volume;
} GstMixerTrack;

#define GST_MIXER_TRACK_HAS_FLAG(track, flag) \
  ((track)->flags & flag)

typedef struct _GstMixer GstMixer;

typedef struct _GstMixerClass {
  GTypeInterface klass;

  /* virtual functions */
  const GList *  (* list_tracks)   (GstMixer      *mixer);

  void           (* set_volume)    (GstMixer      *mixer,
				    GstMixerTrack *track,
				    gint          *volumes);
  void           (* get_volume)    (GstMixer      *mixer,
				    GstMixerTrack *track,
				    gint          *volumes);

  void           (* set_mute)      (GstMixer      *mixer,
				    GstMixerTrack *track,
				    gboolean       mute);
  void           (* set_record)    (GstMixer      *mixer,
				    GstMixerTrack *track,
				    gboolean       record);

  GST_CLASS_PADDING
} GstMixerClass;

GType		gst_mixer_get_type	(void);

/* virtual class function wrappers */
const GList *	gst_mixer_list_tracks	(GstMixer      *mixer);
void		gst_mixer_set_volume	(GstMixer      *mixer,
					 GstMixerTrack *track,
					 gint          *volumes);
void		gst_mixer_get_volume	(GstMixer      *mixer,
					 GstMixerTrack *track,
					 gint          *volumes);
void		gst_mixer_set_mute	(GstMixer      *mixer,
					 GstMixerTrack *track,
					 gboolean       mute);
void		gst_mixer_set_record	(GstMixer      *mixer,
					 GstMixerTrack *track,
					 gboolean       record);

G_END_DECLS

#endif /* __GST_MIXER_H__ */
