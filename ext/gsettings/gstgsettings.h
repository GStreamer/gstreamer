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

#ifndef __GST_GSETTINGS_H__
#define __GST_GSETTINGS_H__

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_GSETTINGS_SCHEMA "org.freedesktop.gstreamer-0.10.default-elements"
#define GST_GSETTINGS_PATH "/desktop/gstreamer/0.10/default-elements/"

#define GST_GSETTINGS_KEY_SOUNDS_AUDIOSINK "sounds-audiosink"
#define GST_GSETTINGS_KEY_MUSIC_AUDIOSINK "music-audiosink"
#define GST_GSETTINGS_KEY_CHAT_AUDIOSINK "chat-audiosink"
#define GST_GSETTINGS_KEY_AUDIOSRC "audiosrc"
#define GST_GSETTINGS_KEY_VIDEOSINK "videosink"
#define GST_GSETTINGS_KEY_VIDEOSRC "videosrc"
#define GST_GSETTINGS_KEY_VISUALIZATION "visualization"

G_END_DECLS

#endif /* __GST_GSETTINGS_H__ */
