/* GStreamer tmplayer format subtitle parser
 * Copyright (C) 2006 Tim-Philipp Müller <tim centricular net>
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

#include "tmplayerparse.h"

#include <string.h>

/* From http://forum.doom9.org/archive/index.php/t-81059.html:
 * 
 * TMPlayer format, which comes in five varieties:
 * 
 * time-base 00:00:00:
 * 00:00:50:This is the Earth at a time|when the dinosaurs roamed...
 * 00:00:53:
 * 00:00:54:a lush and fertile planet.
 * 00:00:56:
 * 
 * time-base 0:00:00:
 * 0:00:50:This is the Earth at a time|when the dinosaurs roamed...
 * 0:00:53:
 * 0:00:54:a lush and fertile planet.
 * 0:00:56:
 * 
 * time-base 00:00:00=
 * 00:00:50=This is the Earth at a time|when the dinosaurs roamed...
 * 00:00:53=
 * 00:00:54=a lush and fertile planet.
 * 00:00:56=
 * 
 * time-base 0:00:00=
 * 0:00:50=This is the Earth at a time|when the dinosaurs roamed...
 * 0:00:53=
 * 0:00:54=a lush and fertile planet.
 * 0:00:56=
 * 
 * and multiline time-base 00:00:00,1=
 * 00:00:50,1=This is the Earth at a time
 * 00:00:50,2=when the dinosaurs roamed...
 * 00:00:53,1=
 * 00:00:54,1=a lush and fertile planet.
 * 00:00:56,1=
 */

static gchar *
tmplayer_parse_line (ParserState * state, const gchar * line, guint line_num)
{
  GstClockTime ts = GST_CLOCK_TIME_NONE;
  const gchar *text_start = NULL;
  gboolean multiline = FALSE;
  gchar *ret = NULL;
  gchar divc = '\0';
  guint h, m, s, l = 1;

  if (sscanf (line, "%u:%02u:%02u,%u%c", &h, &m, &s, &l, &divc) == 5 &&
      (divc == '=')) {
    GST_LOG ("multiline format %u %u %u %u", h, m, s, l);
    ts = GST_SECOND * ((((h * 60) + m) * 60) + s);
    text_start = strchr (line, '=');
    multiline = TRUE;
  } else if (sscanf (line, "%u:%02u:%02u%c", &h, &m, &s, &divc) == 4 &&
      (divc == '=' || divc == ':')) {
    GST_LOG ("single line format %u %u %u %u %c", h, m, s, l, divc);
    ts = GST_SECOND * ((((h * 60) + m) * 60) + s);
    text_start = strchr (line + 6, divc);
  } else {
    GST_WARNING ("failed to parse line: '%s'", line);
    return NULL;
  }

  if (text_start == NULL || text_start[1] == '\0' ||
      (l == 1 && state->buf->len > 0)) {
    if (GST_CLOCK_TIME_IS_VALID (state->start_time) &&
        state->start_time < ts && line_num > 0) {
      ret = g_strndup (state->buf->str, state->buf->len);
      g_strdelimit (ret, "|", '\n');
      g_string_truncate (state->buf, 0);
      state->duration = ts - state->start_time;
    } else if (line_num > 0) {
      GST_WARNING ("end of subtitle unit but no valid start time?!");
    }
  } else {
    if (l > 1)
      g_string_append_c (state->buf, '\n');
    g_string_append (state->buf, text_start + 1);
    state->start_time = ts;
  }

  return ret;
}

gchar *
parse_tmplayer (ParserState * state, const gchar * line)
{
  gchar *ret;

  /* GST_LOG ("Parsing: %s", line); */

  ret = tmplayer_parse_line (state, line, state->state);
  ++state->state;

  return ret;
}
