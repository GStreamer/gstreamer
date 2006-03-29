/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (c) 2004 Ronald S. Bultje <rbultje@ronald.bitfreak.net>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <regex.h>

#include "gstsubparse.h"
#include "gstssaparse.h"

GST_DEBUG_CATEGORY_STATIC (sub_parse_debug);
#define GST_CAT_DEFAULT sub_parse_debug

static GstElementDetails sub_parse_details =
GST_ELEMENT_DETAILS ("Subtitle parser",
    "Codec/Parser/Subtitle",
    "Parses subtitle (.sub) files into text streams",
    "Gustavo J. A. M. Carneiro <gjc@inescporto.pt>\n"
    "Ronald S. Bultje <rbultje@ronald.bitfreak.net>");

static GstStaticPadTemplate sink_templ = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-subtitle")
    );

static GstStaticPadTemplate src_templ = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("text/plain; text/x-pango-markup")
    );

static void gst_sub_parse_base_init (GstSubParseClass * klass);
static void gst_sub_parse_class_init (GstSubParseClass * klass);
static void gst_sub_parse_init (GstSubParse * subparse);

static gboolean gst_sub_parse_src_event (GstPad * pad, GstEvent * event);
static gboolean gst_sub_parse_sink_event (GstPad * pad, GstEvent * event);

static GstStateChangeReturn gst_sub_parse_change_state (GstElement * element,
    GstStateChange transition);

static GstFlowReturn gst_sub_parse_chain (GstPad * sinkpad, GstBuffer * buf);

static GstElementClass *parent_class = NULL;

GType
gst_sub_parse_get_type (void)
{
  static GType sub_parse_type = 0;

  if (!sub_parse_type) {
    static const GTypeInfo sub_parse_info = {
      sizeof (GstSubParseClass),
      (GBaseInitFunc) gst_sub_parse_base_init,
      NULL,
      (GClassInitFunc) gst_sub_parse_class_init,
      NULL,
      NULL,
      sizeof (GstSubParse),
      0,
      (GInstanceInitFunc) gst_sub_parse_init,
    };

    sub_parse_type = g_type_register_static (GST_TYPE_ELEMENT,
        "GstSubParse", &sub_parse_info, 0);
  }

  return sub_parse_type;
}

static void
gst_sub_parse_base_init (GstSubParseClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_templ));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_templ));
  gst_element_class_set_details (element_class, &sub_parse_details);
}

static void
gst_sub_parse_dispose (GObject * object)
{
  GstSubParse *subparse = GST_SUBPARSE (object);

  GST_DEBUG_OBJECT (subparse, "cleaning up subtitle parser");

  if (subparse->segment) {
    gst_segment_free (subparse->segment);
    subparse->segment = NULL;
  }

  GST_CALL_PARENT (G_OBJECT_CLASS, dispose, (object));
}

static void
gst_sub_parse_class_init (GstSubParseClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  object_class->dispose = gst_sub_parse_dispose;

  element_class->change_state = gst_sub_parse_change_state;
}

static void
gst_sub_parse_init (GstSubParse * subparse)
{
  subparse->sinkpad = gst_pad_new_from_static_template (&sink_templ, "sink");
  gst_pad_set_chain_function (subparse->sinkpad,
      GST_DEBUG_FUNCPTR (gst_sub_parse_chain));
  gst_pad_set_event_function (subparse->sinkpad,
      GST_DEBUG_FUNCPTR (gst_sub_parse_sink_event));
  gst_element_add_pad (GST_ELEMENT (subparse), subparse->sinkpad);

  subparse->srcpad = gst_pad_new_from_static_template (&src_templ, "src");
  gst_pad_set_event_function (subparse->srcpad,
      GST_DEBUG_FUNCPTR (gst_sub_parse_src_event));
  gst_element_add_pad (GST_ELEMENT (subparse), subparse->srcpad);

  subparse->textbuf = g_string_new (NULL);
  subparse->parser_type = GST_SUB_PARSE_FORMAT_UNKNOWN;
  subparse->flushing = FALSE;
  subparse->segment = gst_segment_new ();
  if (subparse->segment) {
    gst_segment_init (subparse->segment, GST_FORMAT_TIME);
    subparse->need_segment = TRUE;
  } else {
    GST_WARNING_OBJECT (subparse, "segment creation failed");
    g_assert_not_reached ();
  }
}

/*
 * Source pad functions.
 */

static gboolean
gst_sub_parse_src_event (GstPad * pad, GstEvent * event)
{
  GstSubParse *self = GST_SUBPARSE (gst_pad_get_parent (pad));
  gboolean ret = FALSE;

  GST_DEBUG ("Handling %s event", GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
    {
      GstFormat format;
      GstSeekType start_type, stop_type;
      gint64 start, stop;
      gdouble rate;
      gboolean update;

      gst_event_parse_seek (event, &rate, &format, &self->segment_flags,
          &start_type, &start, &stop_type, &stop);

      if (format != GST_FORMAT_TIME) {
        GST_WARNING_OBJECT (self, "we only support seeking in TIME format");
        gst_event_unref (event);
        goto beach;
      }

      /* Convert that seek to a seeking in bytes at position 0,
         FIXME: could use an index */
      ret = gst_pad_push_event (self->sinkpad,
          gst_event_new_seek (rate, GST_FORMAT_BYTES, self->segment_flags,
              GST_SEEK_TYPE_SET, 0, GST_SEEK_TYPE_NONE, 0));

      if (ret) {
        /* Apply the seek to our segment */
        gst_segment_set_seek (self->segment, rate, format, self->segment_flags,
            start_type, start, stop_type, stop, &update);

        GST_DEBUG_OBJECT (self, "segment configured from %" GST_TIME_FORMAT
            " to %" GST_TIME_FORMAT ", position %" GST_TIME_FORMAT,
            GST_TIME_ARGS (self->segment->start),
            GST_TIME_ARGS (self->segment->stop),
            GST_TIME_ARGS (self->segment->last_stop));

        self->next_offset = 0;

        self->need_segment = TRUE;
      } else {
        GST_WARNING_OBJECT (self, "seek to 0 bytes failed");
      }

      gst_event_unref (event);
      break;
    }
    default:
      ret = gst_pad_event_default (pad, event);
      break;
  }

beach:
  gst_object_unref (self);

  return ret;
}

static gchar *
convert_encoding (GstSubParse * self, const gchar * str, gsize len)
{
  const gchar *encoding;
  GError *err = NULL;
  gchar *ret;

  if (self->valid_utf8) {
    if (g_utf8_validate (str, len, NULL)) {
      GST_LOG_OBJECT (self, "valid UTF-8, no conversion needed");
      return g_strndup (str, len);
    }
    GST_INFO_OBJECT (self, "invalid UTF-8!");
    self->valid_utf8 = FALSE;
  }

  encoding = g_getenv ("GST_SUBTITLE_ENCODING");
  if (encoding == NULL || *encoding == '\0') {
    /* if local encoding is UTF-8 and no encoding specified
     * via the environment variable, assume ISO-8859-15 */
    if (g_get_charset (&encoding)) {
      encoding = "ISO-8859-15";
    }
  }

  ret = g_convert_with_fallback (str, len, "UTF-8", encoding, "*", NULL,
      NULL, &err);

  if (err) {
    GST_WARNING_OBJECT (self, "could not convert string from '%s' to UTF-8: %s",
        encoding, err->message);
    g_error_free (err);

    /* invalid input encoding, fall back to ISO-8859-15 (always succeeds) */
    ret = g_convert_with_fallback (str, len, "UTF-8", "ISO-8859-15", "*",
        NULL, NULL, NULL);
  }

  GST_LOG_OBJECT (self, "successfully converted %d characters from %s to UTF-8"
      "%s", len, encoding, (err) ? " , using ISO-8859-15 as fallback" : "");

  return ret;
}

static gchar *
get_next_line (GstSubParse * self)
{
  char *line = NULL;
  const char *line_end;
  int line_len;
  gboolean have_r = FALSE;

  line_end = strchr (self->textbuf->str, '\n');

  if (!line_end) {
    /* end-of-line not found; return for more data */
    return NULL;
  }

  /* get rid of '\r' */
  if (line_end != self->textbuf->str && *(line_end - 1) == '\r') {
    line_end--;
    have_r = TRUE;
  }

  line_len = line_end - self->textbuf->str;
  line = convert_encoding (self, self->textbuf->str, line_len);
  self->textbuf = g_string_erase (self->textbuf, 0,
      line_len + (have_r ? 2 : 1));
  return line;
}

static gchar *
parse_mdvdsub (ParserState * state, const gchar * line)
{
  const gchar *line_split;
  gchar *line_chunk;
  guint start_frame, end_frame;
  gint64 clip_start = 0, clip_stop = 0;
  gboolean in_seg = FALSE;

  /* FIXME: hardcoded for now, but detecting the correct value is
   * not going to be easy, I suspect... */
  const double frames_per_sec = 24000 / 1001.;
  GString *markup;
  gchar *ret;

  /* style variables */
  gboolean italic;
  gboolean bold;
  guint fontsize;

  if (sscanf (line, "{%u}{%u}", &start_frame, &end_frame) != 2) {
    g_warning ("Parse of the following line, assumed to be in microdvd .sub"
        " format, failed:\n%s", line);
    return NULL;
  }

  state->start_time = (start_frame - 1000) / frames_per_sec * GST_SECOND;
  state->duration = (end_frame - start_frame) / frames_per_sec * GST_SECOND;

  /* Check our segment start/stop */
  in_seg = gst_segment_clip (state->segment, GST_FORMAT_TIME,
      state->start_time, state->start_time + state->duration, &clip_start,
      &clip_stop);

  /* No need to parse that text if it's out of segment */
  if (in_seg) {
    state->start_time = clip_start;
    state->duration = clip_stop - clip_start;
  } else {
    return NULL;
  }

  /* skip the {%u}{%u} part */
  line = strchr (line, '}') + 1;
  line = strchr (line, '}') + 1;

  markup = g_string_new (NULL);
  while (1) {
    italic = FALSE;
    bold = FALSE;
    fontsize = 0;
    /* parse style markup */
    if (strncmp (line, "{y:i}", 5) == 0) {
      italic = TRUE;
      line = strchr (line, '}') + 1;
    }
    if (strncmp (line, "{y:b}", 5) == 0) {
      bold = TRUE;
      line = strchr (line, '}') + 1;
    }
    if (sscanf (line, "{s:%u}", &fontsize) == 1) {
      line = strchr (line, '}') + 1;
    }
    if ((line_split = strchr (line, '|')))
      line_chunk = g_markup_escape_text (line, line_split - line);
    else
      line_chunk = g_markup_escape_text (line, strlen (line));
    markup = g_string_append (markup, "<span");
    if (italic)
      g_string_append (markup, " style=\"italic\"");
    if (bold)
      g_string_append (markup, " weight=\"bold\"");
    if (fontsize)
      g_string_append_printf (markup, " size=\"%u\"", fontsize * 1000);
    g_string_append_printf (markup, ">%s</span>", line_chunk);
    g_free (line_chunk);
    if (line_split) {
      g_string_append (markup, "\n");
      line = line_split + 1;
    } else {
      break;
    }
  }
  ret = markup->str;
  g_string_free (markup, FALSE);
  GST_DEBUG ("parse_mdvdsub returning (%f+%f): %s",
      state->start_time / (double) GST_SECOND,
      state->duration / (double) GST_SECOND, ret);
  return ret;
}

/* we want to escape text in general, but retain basic markup like
 * <i></i>, <u></u>, and <b></b>. The easiest and safest way is to
 * just unescape a white list of allowed markups again after
 * escaping everything (the text between these simple markers isn't
 * necessarily escaped, so it seems best to do it like this) */
static void
subrip_unescape_formatting (gchar * txt)
{
  gchar *pos;

  for (pos = txt; pos != NULL && *pos != '\0'; ++pos) {
    if (g_ascii_strncasecmp (pos, "&lt;u&gt;", 9) == 0 ||
        g_ascii_strncasecmp (pos, "&lt;i&gt;", 9) == 0 ||
        g_ascii_strncasecmp (pos, "&lt;b&gt;", 9) == 0) {
      pos[0] = '<';
      pos[1] = g_ascii_tolower (pos[4]);
      pos[2] = '>';
      /* move NUL terminator as well */
      g_memmove (pos + 3, pos + 9, strlen (pos + 9) + 1);
      pos += 2;
    }
  }

  for (pos = txt; pos != NULL && *pos != '\0'; ++pos) {
    if (g_ascii_strncasecmp (pos, "&lt;/u&gt;", 10) == 0 ||
        g_ascii_strncasecmp (pos, "&lt;/i&gt;", 10) == 0 ||
        g_ascii_strncasecmp (pos, "&lt;/b&gt;", 10) == 0) {
      pos[0] = '<';
      pos[1] = '/';
      pos[2] = g_ascii_tolower (pos[5]);
      pos[3] = '>';
      /* move NUL terminator as well */
      g_memmove (pos + 4, pos + 10, strlen (pos + 10) + 1);
      pos += 3;
    }
  }
}

static gchar *
parse_subrip (ParserState * state, const gchar * line)
{
  guint h1, m1, s1, ms1;
  guint h2, m2, s2, ms2;
  int subnum;
  gchar *ret;

  switch (state->state) {
    case 0:
      /* looking for a single integer */
      if (sscanf (line, "%u", &subnum) == 1)
        state->state = 1;
      return NULL;
    case 1:
      /* looking for start_time --> end_time */
      if (sscanf (line, "%u:%u:%u,%u --> %u:%u:%u,%u",
              &h1, &m1, &s1, &ms1, &h2, &m2, &s2, &ms2) == 8) {
        state->state = 2;
        state->start_time =
            (((guint64) h1) * 3600 + m1 * 60 + s1) * GST_SECOND +
            ms1 * GST_MSECOND;
        state->duration =
            (((guint64) h2) * 3600 + m2 * 60 + s2) * GST_SECOND +
            ms2 * GST_MSECOND - state->start_time;
      } else {
        GST_DEBUG ("error parsing subrip time line");
        state->state = 0;
      }
      return NULL;
    case 2:
    {                           /* No need to parse that text if it's out of segment */
      gint64 clip_start = 0, clip_stop = 0;
      gboolean in_seg = FALSE;

      /* Check our segment start/stop */
      in_seg = gst_segment_clip (state->segment, GST_FORMAT_TIME,
          state->start_time, state->start_time + state->duration,
          &clip_start, &clip_stop);

      if (in_seg) {
        state->start_time = clip_start;
        state->duration = clip_stop - clip_start;
      } else {
        state->state = 0;
        return NULL;
      }
    }
      /* looking for subtitle text; empty line ends this
       * subtitle entry */
      if (state->buf->len)
        g_string_append_c (state->buf, '\n');
      g_string_append (state->buf, line);
      if (strlen (line) == 0) {
        ret = g_markup_escape_text (state->buf->str, state->buf->len);
        g_string_truncate (state->buf, 0);
        state->state = 0;
        subrip_unescape_formatting (ret);
        return ret;
      }
      return NULL;
    default:
      g_return_val_if_reached (NULL);
  }
}

static gchar *
parse_mpsub (ParserState * state, const gchar * line)
{
  gchar *ret;
  float t1, t2;

  switch (state->state) {
    case 0:
      /* looking for two floats (offset, duration) */
      if (sscanf (line, "%f %f", &t1, &t2) == 2) {
        state->state = 1;
        state->start_time += state->duration + GST_SECOND * t1;
        state->duration = GST_SECOND * t2;
      }
      return NULL;
    case 1:
    {                           /* No need to parse that text if it's out of segment */
      gint64 clip_start = 0, clip_stop = 0;
      gboolean in_seg = FALSE;

      /* Check our segment start/stop */
      in_seg = gst_segment_clip (state->segment, GST_FORMAT_TIME,
          state->start_time, state->start_time + state->duration,
          &clip_start, &clip_stop);

      if (in_seg) {
        state->start_time = clip_start;
        state->duration = clip_stop - clip_start;
      } else {
        state->state = 0;
        return NULL;
      }
    }
      /* looking for subtitle text; empty line ends this
       * subtitle entry */
      if (state->buf->len)
        g_string_append_c (state->buf, '\n');
      g_string_append (state->buf, line);
      if (strlen (line) == 0) {
        ret = g_strdup (state->buf->str);
        g_string_truncate (state->buf, 0);
        state->state = 0;
        return ret;
      }
      return NULL;
    default:
      g_assert_not_reached ();
      return NULL;
  }
}

static void
parser_state_init (ParserState * state)
{
  GST_DEBUG ("initialising parser");

  if (state->buf) {
    g_string_truncate (state->buf, 0);
  } else {
    state->buf = g_string_new (NULL);
  }

  state->start_time = 0;
  state->duration = 0;
  state->state = 0;
  state->segment = NULL;
}

static void
parser_state_dispose (ParserState * state)
{
  if (state->buf) {
    g_string_free (state->buf, TRUE);
    state->buf = NULL;
  }
}

/*
 * FIXME: maybe we should pass along a second argument, the preceding
 * text buffer, because that is how this originally worked, even though
 * I don't really see the use of that.
 */

static GstSubParseFormat
gst_sub_parse_data_format_autodetect (gchar * match_str)
{
  static gboolean need_init_regexps = TRUE;
  static regex_t mdvd_rx;
  static regex_t subrip_rx;

  /* initialize the regexps used the first time around */
  if (need_init_regexps) {
    int err;
    char errstr[128];

    need_init_regexps = FALSE;
    if ((err = regcomp (&mdvd_rx, "^\\{[0-9]+\\}\\{[0-9]+\\}",
                REG_EXTENDED | REG_NEWLINE | REG_NOSUB) != 0) ||
        (err = regcomp (&subrip_rx, "^1(\x0d)?\x0a"
                "[0-9][0-9]:[0-9][0-9]:[0-9][0-9],[0-9]{3}"
                " --> [0-9][0-9]:[0-9][0-9]:[0-9][0-9],[0-9]{3}",
                REG_EXTENDED | REG_NEWLINE | REG_NOSUB)) != 0) {
      regerror (err, &subrip_rx, errstr, 127);
      GST_WARNING ("Compilation of subrip regex failed: %s", errstr);
    }
  }

  if (regexec (&mdvd_rx, match_str, 0, NULL, 0) == 0) {
    GST_LOG ("subparse: MicroDVD (frame based) format detected");
    return GST_SUB_PARSE_FORMAT_MDVDSUB;
  }
  if (regexec (&subrip_rx, match_str, 0, NULL, 0) == 0) {
    GST_LOG ("subparse: SubRip (time based) format detected");
    return GST_SUB_PARSE_FORMAT_SUBRIP;
  }
  if (!strncmp (match_str, "FORMAT=TIME", 11)) {
    GST_LOG ("subparse: MPSub (time based) format detected");
    return GST_SUB_PARSE_FORMAT_MPSUB;
  }

  GST_WARNING ("subparse: subtitle format autodetection failed!");
  return GST_SUB_PARSE_FORMAT_UNKNOWN;
}

static GstCaps *
gst_sub_parse_format_autodetect (GstSubParse * self)
{
  gchar *data;
  GstSubParseFormat format;

  if (strlen (self->textbuf->str) < 35) {
    GST_DEBUG ("File too small to be a subtitles file");
    return NULL;
  }

  data = g_strndup (self->textbuf->str, 35);
  format = gst_sub_parse_data_format_autodetect (data);
  g_free (data);

  self->parser_type = format;
  parser_state_init (&self->state);

  switch (format) {
    case GST_SUB_PARSE_FORMAT_MDVDSUB:
      self->parse_line = parse_mdvdsub;
      return gst_caps_new_simple ("text/x-pango-markup", NULL);
    case GST_SUB_PARSE_FORMAT_SUBRIP:
      self->parse_line = parse_subrip;
      return gst_caps_new_simple ("text/x-pango-markup", NULL);
    case GST_SUB_PARSE_FORMAT_MPSUB:
      self->parse_line = parse_mpsub;
      return gst_caps_new_simple ("text/plain", NULL);
    case GST_SUB_PARSE_FORMAT_UNKNOWN:
    default:
      GST_DEBUG ("no subtitle format detected");
      GST_ELEMENT_ERROR (self, STREAM, WRONG_TYPE,
          ("The input is not a valid/supported subtitle file"), (NULL));
      return NULL;
  }
}

static void
feed_textbuf (GstSubParse * self, GstBuffer * buf)
{
  if (GST_BUFFER_OFFSET (buf) != self->offset) {
    /* flush the parser state */
    parser_state_init (&self->state);
    g_string_truncate (self->textbuf, 0);
  }

  self->textbuf = g_string_append_len (self->textbuf,
      (gchar *) GST_BUFFER_DATA (buf), GST_BUFFER_SIZE (buf));
  self->offset = GST_BUFFER_OFFSET (buf) + GST_BUFFER_SIZE (buf);
  self->next_offset = self->offset;

  gst_buffer_unref (buf);
}

static GstFlowReturn
handle_buffer (GstSubParse * self, GstBuffer * buf)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GstCaps *caps = NULL;
  gchar *line, *subtitle;

  feed_textbuf (self, buf);

  /* make sure we know the format */
  if (G_UNLIKELY (self->parser_type == GST_SUB_PARSE_FORMAT_UNKNOWN)) {
    if (!(caps = gst_sub_parse_format_autodetect (self))) {
      return GST_FLOW_UNEXPECTED;
    }
    if (!gst_pad_set_caps (self->srcpad, caps)) {
      gst_caps_unref (caps);
      return GST_FLOW_UNEXPECTED;
    }
    gst_caps_unref (caps);
  }

  while ((line = get_next_line (self)) && !self->flushing) {
    /* Set segment on our parser state machine */
    self->state.segment = self->segment;
    /* Now parse the line, out of segment lines will just return NULL */
    GST_DEBUG ("Parsing line '%s'", line);
    subtitle = self->parse_line (&self->state, line);
    g_free (line);

    if (subtitle) {
      guint subtitle_len = strlen (subtitle);

      ret = gst_pad_alloc_buffer_and_set_caps (self->srcpad,
          GST_BUFFER_OFFSET_NONE, subtitle_len,
          GST_PAD_CAPS (self->srcpad), &buf);

      if (ret == GST_FLOW_OK) {
        memcpy (GST_BUFFER_DATA (buf), subtitle, subtitle_len);
        GST_BUFFER_TIMESTAMP (buf) = self->state.start_time;
        GST_BUFFER_DURATION (buf) = self->state.duration;

        gst_segment_set_last_stop (self->segment, GST_FORMAT_TIME,
            self->state.start_time);

        GST_DEBUG ("Sending text '%s', %" GST_TIME_FORMAT " + %"
            GST_TIME_FORMAT, subtitle, GST_TIME_ARGS (self->state.start_time),
            GST_TIME_ARGS (self->state.duration));

        ret = gst_pad_push (self->srcpad, buf);
      }

      g_free (subtitle);
      subtitle = NULL;

      if (GST_FLOW_IS_FATAL (ret))
        break;
    }
  }

  return ret;
}

static GstFlowReturn
gst_sub_parse_chain (GstPad * sinkpad, GstBuffer * buf)
{
  GstFlowReturn ret;
  GstSubParse *self;

  GST_DEBUG ("gst_sub_parse_chain");
  self = GST_SUBPARSE (gst_pad_get_parent (sinkpad));

  /* Push newsegment if needed */
  if (self->need_segment) {
    gst_pad_push_event (self->srcpad, gst_event_new_new_segment (FALSE,
            self->segment->rate, self->segment->format,
            self->segment->last_stop, self->segment->stop,
            self->segment->time));
    self->need_segment = FALSE;
  }

  ret = handle_buffer (self, buf);

  gst_object_unref (self);

  return ret;
}

static gboolean
gst_sub_parse_sink_event (GstPad * pad, GstEvent * event)
{
  GstSubParse *self = GST_SUBPARSE (gst_pad_get_parent (pad));
  gboolean ret = FALSE;

  GST_DEBUG ("Handling %s event", GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:{
      /* Make sure the last subrip chunk is pushed out even
       * if the file does not have an empty line at the end */
      if (self->parser_type == GST_SUB_PARSE_FORMAT_SUBRIP) {
        GstBuffer *buf = gst_buffer_new_and_alloc (1 + 1);

        GST_DEBUG ("EOS. Pushing remaining text (if any)");
        GST_BUFFER_DATA (buf)[0] = '\n';
        GST_BUFFER_DATA (buf)[1] = '\0';        /* play it safe */
        GST_BUFFER_SIZE (buf) = 1;
        GST_BUFFER_OFFSET (buf) = self->offset;
        gst_sub_parse_chain (pad, buf);
      }
      ret = gst_pad_event_default (pad, event);
      break;
    }
    case GST_EVENT_NEWSEGMENT:
    {
      GstFormat format;
      gdouble rate;
      gint64 start, stop, time;
      gboolean update;

      GST_DEBUG_OBJECT (self, "received new segment");

      gst_event_parse_new_segment (event, &update, &rate, &format, &start,
          &stop, &time);

      /* now copy over the values */
      gst_segment_set_newsegment (self->segment, update, rate, format,
          start, stop, time);

      ret = TRUE;
      gst_event_unref (event);
      break;
    }
    case GST_EVENT_FLUSH_START:
    {
      self->flushing = TRUE;

      ret = gst_pad_event_default (pad, event);
      break;
    }
    case GST_EVENT_FLUSH_STOP:
    {
      self->flushing = FALSE;

      ret = gst_pad_event_default (pad, event);
      break;
    }
    default:
      ret = gst_pad_event_default (pad, event);
      break;
  }

  gst_object_unref (self);

  return ret;
}


static GstStateChangeReturn
gst_sub_parse_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
  GstSubParse *self = GST_SUBPARSE (element);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      /* format detection will init the parser state */
      self->offset = self->next_offset = 0;
      self->parser_type = GST_SUB_PARSE_FORMAT_UNKNOWN;
      self->valid_utf8 = TRUE;
      break;
    default:
      break;
  }

  ret = parent_class->change_state (element, transition);
  if (ret == GST_STATE_CHANGE_FAILURE)
    return ret;

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      parser_state_dispose (&self->state);
      self->parser_type = GST_SUB_PARSE_FORMAT_UNKNOWN;
      break;
    default:
      break;
  }

  return ret;
}

/*
 * Typefind support.
 */

static GstStaticCaps sub_caps = GST_STATIC_CAPS ("application/x-subtitle");

#define SUB_CAPS (gst_static_caps_get (&sub_caps))

static void
gst_subparse_type_find (GstTypeFind * tf, gpointer private)
{
  const guint8 *data;
  GstSubParseFormat format;
  gchar *str;

  if (!(data = gst_type_find_peek (tf, 0, 36)))
    return;

  /* make sure string passed to _autodetect() is NUL-terminated */
  str = g_strndup ((gchar *) data, 35);
  format = gst_sub_parse_data_format_autodetect (str);
  g_free (str);

  switch (format) {
    case GST_SUB_PARSE_FORMAT_MDVDSUB:
      GST_DEBUG ("MicroDVD format detected");
      break;
    case GST_SUB_PARSE_FORMAT_SUBRIP:
      GST_DEBUG ("SubRip format detected");
      break;
    case GST_SUB_PARSE_FORMAT_MPSUB:
      GST_DEBUG ("MPSub format detected");
      break;
    case GST_SUB_PARSE_FORMAT_UNKNOWN:
      GST_DEBUG ("no subtitle format detected");
      return;
  }

  /* if we're here, it's ok */
  gst_type_find_suggest (tf, GST_TYPE_FIND_MAXIMUM, SUB_CAPS);
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  static gchar *sub_exts[] = { "srt", "sub", "mpsub", "mdvd", NULL };

  GST_DEBUG_CATEGORY_INIT (sub_parse_debug, "subparse", 0, ".sub parser");

  if (!gst_type_find_register (plugin, "subparse_typefind", GST_RANK_MARGINAL,
          gst_subparse_type_find, sub_exts, SUB_CAPS, NULL, NULL))
    return FALSE;

  if (!gst_element_register (plugin, "subparse",
          GST_RANK_PRIMARY, GST_TYPE_SUBPARSE) ||
      !gst_element_register (plugin, "ssaparse",
          GST_RANK_PRIMARY, GST_TYPE_SSA_PARSE)) {
    return FALSE;
  }

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "subparse",
    "Subtitle parsing",
    plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
