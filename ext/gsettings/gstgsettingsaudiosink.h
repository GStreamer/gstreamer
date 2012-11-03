/* GStreamer
 * Copyright (C) 2010 Sebastian Dröge <sebastian.droege@collabora.co.uk>
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

#ifndef __GST_GSETTINGS_AUDIO_SINK_H__
#define __GST_GSETTINGS_AUDIO_SINK_H__

#include <gst/gst.h>
#include <gio/gio.h>
#include "gstswitchsink.h"

G_BEGIN_DECLS

#define GST_TYPE_GSETTINGS_AUDIO_SINK \
  (gst_gsettings_audio_sink_get_type ())
#define GST_GSETTINGS_AUDIO_SINK(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_GSETTINGS_AUDIO_SINK, \
                               GstGSettingsAudioSink))
#define GST_GSETTINGS_AUDIO_SINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_GSETTINGS_AUDIO_SINK, \
                            GstGSettingsAudioSinkClass))
#define GST_IS_GSETTINGS_AUDIO_SINK(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_GSETTINGS_AUDIO_SINK))
#define GST_IS_GSETTINGS_AUDIO_SINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_GSETTINGS_AUDIO_SINK))

typedef enum
{
  GST_GSETTINGS_AUDIOSINK_PROFILE_SOUNDS,
  GST_GSETTINGS_AUDIOSINK_PROFILE_MUSIC,
  GST_GSETTINGS_AUDIOSINK_PROFILE_CHAT,
  GST_GSETTINGS_AUDIOSINK_PROFILE_NONE /* Internal value only */
} GstGSettingsAudioSinkProfile;

typedef struct _GstGSettingsAudioSink {
  GstSwitchSink parent;

  GSettings *settings;

  GMainContext *context;
  GMainLoop *loop;
  gulong changed_id;

  GstGSettingsAudioSinkProfile profile;
  gchar *gsettings_str;
} GstGSettingsAudioSink;

typedef struct _GstGSettingsAudioSinkClass {
  GstSwitchSinkClass parent_class;
} GstGSettingsAudioSinkClass;

GType   gst_gsettings_audio_sink_get_type   (void);

G_END_DECLS

#endif /* __GST_GSETTINGS_AUDIO_SINK_H__ */
