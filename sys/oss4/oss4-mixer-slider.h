/* GStreamer OSS4 mixer slider control
 * Copyright (C) 2007-2008 Tim-Philipp Müller <tim centricular net>
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

#ifndef GST_OSS4_MIXER_SLIDER_H
#define GST_OSS4_MIXER_SLIDER_H

#include <gst/gst.h>
#include <gst/interfaces/mixer.h>

#include "oss4-mixer.h"

G_BEGIN_DECLS

#define GST_TYPE_OSS4_MIXER_SLIDER            (gst_oss4_mixer_slider_get_type())
#define GST_OSS4_MIXER_SLIDER(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_OSS4_MIXER_SLIDER,GstOss4MixerSlider))
#define GST_OSS4_MIXER_SLIDER_CAST(obj)       ((GstOss4MixerSlider *)(obj))
#define GST_OSS4_MIXER_SLIDER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_OSS4_MIXER_SLIDER,GstOss4MixerSliderClass))
#define GST_IS_OSS4_MIXER_SLIDER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_OSS4_MIXER_SLIDER))
#define GST_IS_OSS4_MIXER_SLIDER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_OSS4_MIXER_SLIDER))

typedef struct _GstOss4MixerSlider GstOss4MixerSlider;
typedef struct _GstOss4MixerSliderClass GstOss4MixerSliderClass;

struct _GstOss4MixerSlider {
  GstMixerTrack  mixer_track;

  GstOss4MixerControl * mc;
  GstOss4Mixer        * mixer;      /* the mixer we belong to (no ref taken) */
  gint                  volumes[2]; /* left/mono, right                      */
};

struct _GstOss4MixerSliderClass {
  GstMixerTrackClass mixer_track_class;
};

GType           gst_oss4_mixer_slider_get_type (void);

GstMixerTrack * gst_oss4_mixer_slider_new (GstOss4Mixer * mixer, GstOss4MixerControl * mc);

gboolean        gst_oss4_mixer_slider_get_volume (GstOss4MixerSlider * s, gint * volumes);

gboolean        gst_oss4_mixer_slider_set_volume (GstOss4MixerSlider * s, const gint * volumes);

gboolean        gst_oss4_mixer_slider_set_record (GstOss4MixerSlider * s, gboolean record);

gboolean        gst_oss4_mixer_slider_set_mute (GstOss4MixerSlider * s, gboolean mute);

void            gst_oss4_mixer_slider_process_change_unlocked (GstMixerTrack * track);

G_END_DECLS

#endif /* GST_OSS4_MIXER_SLIDER_H */


