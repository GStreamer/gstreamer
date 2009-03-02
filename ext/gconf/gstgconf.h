/* GStreamer
 * Copyright (C) <2002> Thomas Vander Stichele <thomas@apestaart.org>
 * Copyright (C) <2006> Jürg Billeter <j@bitron.ch>
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

#ifndef GST_GCONF_H
#define GST_GCONF_H

/*
 * this library handles interaction with GConf
 */

#include <gst/gst.h>
#include <gconf/gconf-client.h>

G_BEGIN_DECLS

#define GST_GCONF_AUDIOSRC_KEY        "default/audiosrc"
#define GST_GCONF_AUDIOSINK_KEY       "default/audiosink"
#define GST_GCONF_MUSIC_AUDIOSINK_KEY "default/musicaudiosink"
#define GST_GCONF_CHAT_AUDIOSINK_KEY  "default/chataudiosink"
#define GST_GCONF_VIDEOSRC_KEY        "default/videosrc"
#define GST_GCONF_VIDEOSINK_KEY       "default/videosink"

typedef enum
{
  GCONF_PROFILE_SOUNDS,
  GCONF_PROFILE_MUSIC,
  GCONF_PROFILE_CHAT,
  GCONF_PROFILE_NONE /* Internal value only */
} GstGConfProfile;

gchar *         gst_gconf_get_string            (const gchar *key);
void            gst_gconf_set_string            (const gchar *key, 
                                                 const gchar *value);

const gchar *   gst_gconf_get_key_for_sink_profile (GstGConfProfile profile);

GstElement *    gst_gconf_render_bin_from_key           (const gchar *key);
GstElement *    gst_gconf_render_bin_with_default       (const gchar *bin,
    const gchar *default_sink);

GstElement *    gst_gconf_get_default_video_sink (void);
GstElement *    gst_gconf_get_default_audio_sink (int profile);
GstElement *    gst_gconf_get_default_video_src (void);
GstElement *    gst_gconf_get_default_audio_src (void);
GstElement *    gst_gconf_get_default_visualization_element (void);

G_END_DECLS

#endif /* GST_GCONF_H */
