/* GStreamer
 * Copyright (C) <2002> Thomas Vander Stichele <thomas@apestaart.org>
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

gchar *gst_gconf_get_string (const gchar * key);
void gst_gconf_set_string (const gchar * key, const gchar * value);

GstElement *gst_gconf_render_bin_from_key (const gchar * key);
GstElement *gst_gconf_render_bin_from_description (const gchar * description);

GstElement *gst_gconf_get_default_video_sink (void);
GstElement *gst_gconf_get_default_audio_sink (void);
GstElement *gst_gconf_get_default_video_src (void);
GstElement *gst_gconf_get_default_audio_src (void);
GstElement *gst_gconf_get_default_visualization_element (void);

#endif /* GST_GCONF_H */
