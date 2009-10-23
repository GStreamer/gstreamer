/* GStreamer
 * Copyright (C) <2007> Wim Taymans <wim.taymans@gmail.com>
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

#include "gstplay-enum.h"

#define C_ENUM(v) ((gint) v)
#define C_FLAGS(v) ((guint) v)

static void
register_gst_autoplug_select_result (GType * id)
{
  static const GEnumValue values[] = {
    {C_ENUM (GST_AUTOPLUG_SELECT_TRY), "GST_AUTOPLUG_SELECT_TRY", "try"},
    {C_ENUM (GST_AUTOPLUG_SELECT_EXPOSE), "GST_AUTOPLUG_SELECT_EXPOSE",
        "expose"},
    {C_ENUM (GST_AUTOPLUG_SELECT_SKIP), "GST_AUTOPLUG_SELECT_SKIP", "skip"},
    {0, NULL, NULL}
  };
  *id = g_enum_register_static ("GstAutoplugSelectResult", values);
}

GType
gst_autoplug_select_result_get_type (void)
{
  static GType id;
  static GOnce once = G_ONCE_INIT;

  g_once (&once, (GThreadFunc) register_gst_autoplug_select_result, &id);
  return id;
}

static void
register_gst_play_flags (GType * id)
{
  static const GFlagsValue values[] = {
    {C_FLAGS (GST_PLAY_FLAG_VIDEO), "Render the video stream", "video"},
    {C_FLAGS (GST_PLAY_FLAG_AUDIO), "Render the audio stream", "audio"},
    {C_FLAGS (GST_PLAY_FLAG_TEXT), "Render subtitles", "text"},
    {C_FLAGS (GST_PLAY_FLAG_VIS),
        "Render visualisation when no video is present", "vis"},
    {C_FLAGS (GST_PLAY_FLAG_SOFT_VOLUME), "Use software volume", "soft-volume"},
    {C_FLAGS (GST_PLAY_FLAG_NATIVE_AUDIO), "Only use native audio formats",
        "native-audio"},
    {C_FLAGS (GST_PLAY_FLAG_NATIVE_VIDEO), "Only use native video formats",
        "native-video"},
    {C_FLAGS (GST_PLAY_FLAG_DOWNLOAD), "Attempt progressive download buffering",
        "download"},
    {C_FLAGS (GST_PLAY_FLAG_BUFFERING), "Buffer demuxed/parsed data",
        "buffering"},
    {0, NULL, NULL}
  };
  *id = g_flags_register_static ("GstPlayFlags", values);
}

GType
gst_play_flags_get_type (void)
{
  static GType id;
  static GOnce once = G_ONCE_INIT;

  g_once (&once, (GThreadFunc) register_gst_play_flags, &id);
  return id;
}
