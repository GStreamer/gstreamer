/* GStreamer Editing Services
 * Copyright (C) 2015 Mathieu Duponchelle <mathieu.duponchelle@opencreed.com>
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

#include <ges/ges.h>
#include <gst/pbutils/pbutils.h>
#include <gst/pbutils/encoding-profile.h>

#pragma once

typedef struct
{
  gboolean mute;
  gboolean disable_mixing;
  gchar *save_path;
  gchar *save_only_path;
  gchar *load_path;
  GESTrackType track_types;
  gboolean needs_set_state;
  gboolean smartrender;
  gchar *scenario;
  gchar *testfile;
  gchar *format;
  gchar *outputuri;
  gchar *encoding_profile;
  gchar *profile_from;
  gchar *container_profile;
  gchar *videosink;
  gchar *audiosink;
  gboolean list_transitions;
  gboolean inspect_action_type;
  gchar *sanitized_timeline;
  gchar *video_track_caps;
  gchar *audio_track_caps;
  gboolean embed_nesteds;
  gboolean enable_validate;

  gboolean ignore_eos;
  gboolean interactive;
  gboolean forward_tags;
} GESLauncherParsedOptions;

gchar * sanitize_timeline_description (gchar **args, GESLauncherParsedOptions *opts);
gboolean get_flags_from_string (GType type, const gchar * str_flags, guint *val);
gchar * ensure_uri (const gchar * location);
void print_enum (GType enum_type);

void ges_print (GstDebugColorFlags c, gboolean err, gboolean nline, const gchar * format, va_list var_args);
void ges_ok (const gchar * format, ...);
void ges_warn (const gchar * format, ...);
void ges_printerr (const gchar * format, ...);

gchar * get_file_extension (gchar * uri);
void describe_encoding_profile (GstEncodingProfile *profile);
void print_timeline(GESTimeline *timeline);
