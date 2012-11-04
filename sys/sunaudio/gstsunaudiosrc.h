/* GStreamer - SunAudio source
 * Copyright (C) 2005,2006 Sun Microsystems, Inc.,
 *               Brian Cameron <brian.cameron@sun.com>
 *
 * gstsunaudiosrc.h:
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
 * Boston, MA 02110-1301, USA.
 */

#ifndef __GST_SUNAUDIO_SRC_H__
#define __GST_SUNAUDIO_SRC_H__

#include <sys/audioio.h>
#include <gst/gst.h>
#include <gst/audio/gstaudiosrc.h>

#include "gstsunaudiomixerctrl.h"

G_BEGIN_DECLS

#define GST_TYPE_SUNAUDIO_SRC     (gst_sunaudiosrc_get_type())
#define GST_SUNAUDIO_SRC(obj)     (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_SUNAUDIO_SRC,GstSunAudioSrc))
#define GST_SUNAUDIO_SRC_CLASS(klass)  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_SUNAUDIO_SRC,GstSunAudioSrcClass))
#define GST_IS_SUNAUDIO_SRC(obj)       (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_SUNAUDIO_SRC))
#define GST_IS_SUNAUDIO_SRC_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_SUNAUDIO_SRC))

typedef struct _GstSunAudioSrc GstSunAudioSrc;
typedef struct _GstSunAudioSrcClass GstSunAudioSrcClass;

struct _GstSunAudioSrc {
  GstAudioSrc    src;

  gchar *device;
  gint   fd;
  gint   control_fd;

  audio_device_t dev;
  audio_info_t info;

  gint   bytes_per_sample;

  GstSunAudioMixerCtrl *mixer;
};

struct _GstSunAudioSrcClass {
  GstAudioSrcClass parent_class;
};

GType gst_sunaudiosrc_get_type(void);

G_END_DECLS

#endif /* __GST_SUNAUDIO_SRC_H__ */

