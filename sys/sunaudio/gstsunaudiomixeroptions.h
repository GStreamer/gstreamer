/*
 * GStreamer SunAudio mixer track implementation
 * Copyright (C) 2009 Sun Microsystems, Inc.,
 *               Brian Cameron <brian.cameron@sun.com>
 *               Garrett D'Amore <garrett.damore@sun.com>
 *
 * gstsunaudiomixeroptions.h: Sun Audio mixer options object
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
 */

#ifndef __GST_SUNAUDIO_MIXER_OPTIONS_H__
#define __GST_SUNAUDIO_MIXER_OPTIONS_H__


#include "gstsunaudiomixer.h"
#include <gst/interfaces/mixeroptions.h>


G_BEGIN_DECLS


#define GST_SUNAUDIO_MIXER_OPTIONS(obj)         (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_SUNAUDIO_MIXER_OPTIONS, GstSunAudioMixerOptions))
#define GST_SUNAUDIO_MIXER_OPTIONS_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_SUNAUDIO_MIXER_OPTIONS, GstSunAudioMixerOptionsClass))
#define GST_IS_SUNAUDIO_MIXER_OPTIONS(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_SUNAUDIO_MIXER_OPTIONS))
#define GST_IS_SUNAUDIO_MIXER_OPTIONS_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_SUNAUDIO_MIXER_OPTIONS))
#define GST_TYPE_SUNAUDIO_MIXER_OPTIONS             (gst_sunaudiomixer_options_get_type())


typedef struct _GstSunAudioMixerOptions GstSunAudioMixerOptions;
typedef struct _GstSunAudioMixerOptionsClass GstSunAudioMixerOptionsClass;


struct _GstSunAudioMixerOptions {
  GstMixerOptions        parent;
  gint                  track_num;
  GQuark		names[8];	/* only 8 possible */
  gint			avail;		/* mask of avail */
};

struct _GstSunAudioMixerOptionsClass {
  GstMixerOptionsClass parent;
};


GType           gst_sunaudiomixer_options_get_type (void);
GstMixerOptions *gst_sunaudiomixer_options_new     (GstSunAudioMixerCtrl *mixer, gint track_num);


G_END_DECLS


#endif /* __GST_SUNAUDIO_MIXER_OPTIONS_H__ */
