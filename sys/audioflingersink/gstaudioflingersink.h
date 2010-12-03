/* GStreamer
 * Copyright (C) <2009> Prajnashi S <prajnashi@gmail.com>
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
#ifndef __GST_AUDIOFLINGERSINK_H__
#define __GST_AUDIOFLINGERSINK_H__


#include <gst/gst.h>
#include "gstaudiosink.h"
#include "audioflinger_wrapper.h"


G_BEGIN_DECLS

#define GST_TYPE_AUDIOFLINGERSINK            (gst_audioflinger_sink_get_type())
#define GST_AUDIOFLINGERSINK(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_AUDIOFLINGERSINK,GstAudioFlingerSink))
#define GST_AUDIOFLINGERSINK_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_AUDIOFLINGERSINK,GstAudioFlingerSinkClass))
#define GST_IS_AUDIOFLINGERSINK(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_AUDIOFLINGERSINK))
#define GST_IS_AUDIOFLINGERSINK_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_AUDIOFLINGERSINK))

typedef struct _GstAudioFlingerSink GstAudioFlingerSink;
typedef struct _GstAudioFlingerSinkClass GstAudioFlingerSinkClass;

struct _GstAudioFlingerSink {
  GstAudioSink    sink;

  AudioFlingerDeviceHandle audioflinger_device;
  gboolean   m_init;
  gint   bytes_per_sample;
  gdouble  m_volume;
  gboolean   m_mute;
  gpointer   m_audiosink;
  GstCaps *probed_caps;
  gboolean eos;
  GstClock *audio_clock;
  GstClock *system_clock;
  GstClock *system_audio_clock;
  GstClock *exported_clock;
  gboolean export_system_audio_clock;
  gboolean may_provide_clock;
  gboolean slaving_disabled;
  guint64 last_resync_sample;
};

struct _GstAudioFlingerSinkClass {
  GstAudioSinkClass parent_class;
};

GType gst_audioflinger_sink_get_type(void);

 gboolean gst_audioflinger_sink_plugin_init (GstPlugin * plugin);

G_END_DECLS

#endif /* __GST_AUDIOFLINGERSINK_H__ */
