/*
 * GStreamer SunAudio mixer track implementation
 * Copyright (C) 2005,2006 Sun Microsystems, Inc.,
 *               Brian Cameron <brian.cameron@sun.com>
 *
 * gstsunaudiomixertrack.h: SunAudio mixer tracks
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

#ifndef __GST_SUNAUDIO_MIXER_TRACK_H__
#define __GST_SUNAUDIO_MIXER_TRACK_H__

#include <gst/gst.h>
#include <gst/interfaces/mixer.h>

G_BEGIN_DECLS

typedef enum
{
   GST_SUNAUDIO_TRACK_OUTPUT   = 0,
   GST_SUNAUDIO_TRACK_LINE_IN  = 1,
   GST_SUNAUDIO_TRACK_MONITOR  = 2,
} GstSunAudioTrackType;

#define MIXER_DEVICES 3

#define GST_TYPE_SUNAUDIO_MIXER_TRACK \
  (gst_sunaudiomixer_track_get_type ())
#define GST_SUNAUDIO_MIXER_TRACK(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_SUNAUDIO_MIXER_TRACK, \
			       GstSunAudioMixerTrack))
#define GST_SUNAUDIO_MIXER_TRACK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_SUNAUDIO_MIXER_TRACK, \
			    GstSunAudioMixerTrackClass))
#define GST_IS_SUNAUDIO_MIXER_TRACK(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_SUNAUDIO_MIXER_TRACK))
#define GST_IS_SUNAUDIO_MIXER_TRACK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_SUNAUDIO_MIXER_TRACK))

typedef struct _GstSunAudioMixerTrack {
  GstMixerTrack parent;

  gint                  gain;
  gint                  balance;
  GstSunAudioTrackType track_num;
} GstSunAudioMixerTrack;

typedef struct _GstSunAudioMixerTrackClass {
  GstMixerTrackClass parent;
} GstSunAudioMixerTrackClass;

GType		gst_sunaudiomixer_track_get_type	(void);
GstMixerTrack*	gst_sunaudiomixer_track_new		(GstSunAudioTrackType track_num, gint max_chans, gint flags);

G_END_DECLS

#endif /* __GST_SUNAUDIO_MIXER_TRACK_H__ */
