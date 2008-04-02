/* GStreamer OSS4 mixer on/off switch control
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

#ifndef GST_OSS4_MIXER_SWITCH_H
#define GST_OSS4_MIXER_SWITCH_H

#include <gst/gst.h>
#include <gst/interfaces/mixer.h>

#include "oss4-mixer.h"

G_BEGIN_DECLS

#define GST_TYPE_OSS4_MIXER_SWITCH            (gst_oss4_mixer_switch_get_type())
#define GST_OSS4_MIXER_SWITCH(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_OSS4_MIXER_SWITCH,GstOss4MixerSwitch))
#define GST_OSS4_MIXER_SWITCH_CAST(obj)       ((GstOss4MixerSwitch *)(obj))
#define GST_OSS4_MIXER_SWITCH_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_OSS4_MIXER_SWITCH,GstOss4MixerSwitchClass))
#define GST_IS_OSS4_MIXER_SWITCH(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_OSS4_MIXER_SWITCH))
#define GST_IS_OSS4_MIXER_SWITCH_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_OSS4_MIXER_SWITCH))

typedef struct _GstOss4MixerSwitch GstOss4MixerSwitch;
typedef struct _GstOss4MixerSwitchClass GstOss4MixerSwitchClass;

struct _GstOss4MixerSwitch {
  GstMixerTrack          mixer_track;

  GstOss4MixerControl  * mc;
  GstOss4Mixer         * mixer;      /* the mixer we belong to (no ref taken) */
};

struct _GstOss4MixerSwitchClass {
  GstMixerTrackClass mixer_track_class;
};

GType            gst_oss4_mixer_switch_get_type (void);

gboolean         gst_oss4_mixer_switch_set (GstOss4MixerSwitch * s, gboolean enabled);

gboolean         gst_oss4_mixer_switch_get (GstOss4MixerSwitch * s, gboolean * enabled);

GstMixerTrack  * gst_oss4_mixer_switch_new (GstOss4Mixer * mixer, GstOss4MixerControl * mc);

void             gst_oss4_mixer_switch_process_change_unlocked (GstMixerTrack * track);

G_END_DECLS

#endif /* GST_OSS4_MIXER_SWITCH_H */


