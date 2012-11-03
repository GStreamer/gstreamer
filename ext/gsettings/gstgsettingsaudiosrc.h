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

#ifndef __GST_GSETTINGS_AUDIO_SRC_H__
#define __GST_GSETTINGS_AUDIO_SRC_H__

#include <gst/gst.h>
#include <gio/gio.h>
#include "gstswitchsrc.h"

G_BEGIN_DECLS

#define GST_TYPE_GSETTINGS_AUDIO_SRC \
  (gst_gsettings_audio_src_get_type ())
#define GST_GSETTINGS_AUDIO_SRC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_GSETTINGS_AUDIO_SRC, \
                               GstGSettingsAudioSrc))
#define GST_GSETTINGS_AUDIO_SRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_GSETTINGS_AUDIO_SRC, \
                            GstGSettingsAudioSrcClass))
#define GST_IS_GSETTINGS_AUDIO_SRC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_GSETTINGS_AUDIO_SRC))
#define GST_IS_GSETTINGS_AUDIO_SRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_GSETTINGS_AUDIO_SRC))

typedef struct _GstGSettingsAudioSrc {
  GstSwitchSrc parent;

  GSettings *settings;

  GMainContext *context;
  GMainLoop *loop;
  gulong changed_id;

  gchar *gsettings_str;
} GstGSettingsAudioSrc;

typedef struct _GstGSettingsAudioSrcClass {
  GstSwitchSrcClass parent_class;
} GstGSettingsAudioSrcClass;

GType   gst_gsettings_audio_src_get_type   (void);

G_END_DECLS

#endif /* __GST_GSETTINGS_AUDIO_SRC_H__ */
