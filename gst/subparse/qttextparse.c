/* GStreamer QTtext subtitle parser
 * Copyright (c) 2009 Thiago Santos <thiago.sousa.santos collabora co uk>>
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

#include "qttextparse.h"

#include <string.h>

#define MIN_TO_NSEC  (60 * GST_SECOND)
#define HOUR_TO_NSEC (60 * MIN_TO_NSEC)

#define GST_QTTEXT_CONTEXT(state) ((GstQTTextContext *) (state)->user_data)

typedef struct _GstQTTextContext GstQTTextContext;

struct _GstQTTextContext
{
  /* timing variables */
  gint timescale;
  gboolean absolute;
  guint64 start_time;

};

void
qttext_context_init (ParserState * state)
{
  GstQTTextContext *context;

  state->user_data = g_new0 (GstQTTextContext, 1);

  context = GST_QTTEXT_CONTEXT (state);

  /* we use 1000 as a default */
  context->timescale = 1000;
  context->absolute = TRUE;
}

void
qttext_context_deinit (ParserState * state)
{
  g_free (state->user_data);
  state->user_data = NULL;
}

static gboolean
qttext_parse_tag (ParserState * state, const gchar * line, gint * index)
{
  gchar *next;
  gint next_index;

  g_assert (line[*index] == '{');

  next = strchr (line + *index, '}');
  if (next == NULL) {
    goto error_out;
  } else {
    next_index = 1 + (next - line);
  }
  g_assert (line[next_index - 1] == '}');

  *index = *index + 1;          /* skip the { */

  /* now identify our tag */
  if (strncmp (line + *index, "QTtext", 6) == 0) {
    /* NOP */
  } else {
    GST_WARNING ("Unused qttext tag starting at: %s", line + *index);
  }

  *index = next_index;
  return TRUE;

error_out:
  {
    GST_WARNING ("Failed to parse qttext tag at line %s", line);
    return FALSE;
  }
}

static guint64
qttext_parse_timestamp (ParserState * state, const gchar * line, gint index)
{
  int ret;
  gint hour, min, sec, dec;
  GstQTTextContext *context = GST_QTTEXT_CONTEXT (state);

  ret = sscanf (line + index, "[%d:%d:%d.%d]", &hour, &min, &sec, &dec);
  if (ret != 3 && ret != 4) {
    /* bad timestamp */
    GST_WARNING ("Bad qttext timestamp found: %s", line);
    return 0;
  }

  if (ret == 3) {
    /* be forgiving for missing decimal part */
    dec = 0;
  }

  /* parse the decimal part according to the timescale */
  g_assert (context->timescale != 0);
  dec = (GST_SECOND * dec) / context->timescale;

  /* return the result */
  return hour * HOUR_TO_NSEC + min * MIN_TO_NSEC + sec * GST_SECOND + dec;
}

static void
qttext_prepare_text (ParserState * state)
{
  if (state->buf == NULL) {
    state->buf = g_string_sized_new (256);      /* this should be enough */
  } else {
    g_string_append (state->buf, "\n");
  }

  /* TODO add the pango markup */
}

static void
qttext_parse_text (ParserState * state, const gchar * line, gint index)
{
  qttext_prepare_text (state);
  g_string_append (state->buf, line + index);
}

gchar *
parse_qttext (ParserState * state, const gchar * line)
{
  gint i;
  guint64 ts;
  gchar *ret = NULL;
  GstQTTextContext *context = GST_QTTEXT_CONTEXT (state);

  i = 0;
  while (line[i] != '\0') {
    /* find first interesting character from 'i' onwards */

    if (line[i] == '{') {
      /* this is a tag, parse it */
      if (!qttext_parse_tag (state, line, &i)) {
        break;
      }
    } else if (line[i] == '[') {
      /* this is a time, convert it to a timestamp */
      ts = qttext_parse_timestamp (state, line, i);

      /* check if we have pending text to send, in case we prepare it */
      if (state->buf) {
        ret = g_string_free (state->buf, FALSE);
        if (context->absolute)
          state->duration = ts - context->start_time;
        else
          state->duration = ts;
        state->start_time = context->start_time;
      }
      state->buf = NULL;

      if (ts == 0) {
        /* this is an error */
      } else {
        if (context->absolute)
          context->start_time = ts;
        else
          context->start_time += ts;
      }

      /* we assume there is nothing else on this line */
      break;

    } else if (line[i] == ' ' || line[i] == '\t') {
      i++;                      /* NOP */
    } else {
      /* this is the actual text, output the rest of the line as it */
      qttext_parse_text (state, line, i);
      break;
    }
  }
  return ret;
}
