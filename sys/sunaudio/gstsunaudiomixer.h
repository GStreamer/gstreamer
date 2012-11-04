/*
 * GStreamer - SunAudio mixer
 * Copyright (C) 2005,2006 Sun Microsystems, Inc.,
 *               Brian Cameron <brian.cameron@sun.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the Free
 * Software Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#ifndef __GST_SUNAUDIO_MIXER_H__
#define __GST_SUNAUDIO_MIXER_H__

#include "gstsunaudiomixerctrl.h"

G_BEGIN_DECLS

#define GST_SUNAUDIO_MIXER(obj)		   (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_SUNAUDIO_MIXER,GstSunAudioMixer))
#define GST_SUNAUDIO_MIXER_CLASS(klass)	   (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_SUNAUDIO_MIXER,GstSunAudioMixerClass))
#define GST_IS_SUNAUDIO_MIXER(obj)	   (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_SUNAUDIO_MIXER))
#define GST_IS_SUNAUDIO_MIXER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_SUNAUDIO_MIXER))
#define GST_TYPE_SUNAUDIO_MIXER		   (gst_sunaudiomixer_get_type())

typedef struct _GstSunAudioMixer GstSunAudioMixer;
typedef struct _GstSunAudioMixerClass GstSunAudioMixerClass;

struct _GstSunAudioMixer {
  GstElement		parent;

  GstSunAudioMixerCtrl	*mixer;
};

struct _GstSunAudioMixerClass {
  GstElementClass	parent;
};

GType		gst_sunaudiomixer_get_type		(void);

G_END_DECLS

#endif /* __GST_SUNAUDIO_MIXER_H__ */
