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

GST_DEBUG_CATEGORY_STATIC (subparse_debug);
#define GST_CAT_DEFAULT subparse_debug

/* format enum */
typedef enum
{
  GST_SUB_PARSE_FORMAT_UNKNOWN = 0,
  GST_SUB_PARSE_FORMAT_MDVDSUB = 1,
  GST_SUB_PARSE_FORMAT_SUBRIP = 2,
  GST_SUB_PARSE_FORMAT_MPSUB = 3
} GstSubParseFormat;

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

static void gst_subparse_base_init (GstSubparseClass * klass);
static void gst_subparse_class_init (GstSubparseClass * klass);
static void gst_subparse_init (GstSubparse * subparse);

static const GstFormat *gst_subparse_formats (GstPad * pad);
static const GstEventMask *gst_subparse_eventmask (GstPad * pad);
static gboolean gst_subparse_event (GstPad * pad, GstEvent * event);

static GstElementStateReturn gst_subparse_change_state (GstElement * element);
static void gst_subparse_loop (GstElement * element);

#if 0
static GstCaps *gst_subparse_type_find (GstBuffer * buf, gpointer private);
#endif

static GstElementClass *parent_class = NULL;

GType
gst_subparse_get_type (void)
{
  static GType subparse_type = 0;

  if (!subparse_type) {
    static const GTypeInfo subparse_info = {
      sizeof (GstSubparseClass),
      (GBaseInitFunc) gst_subparse_base_init,
      NULL,
      (GClassInitFunc) gst_subparse_class_init,
      NULL,
      NULL,
      sizeof (GstSubparse),
      0,
      (GInstanceInitFunc) gst_subparse_init,
    };

    subparse_type = g_type_register_static (GST_TYPE_ELEMENT,
        "GstSubparse", &subparse_info, 0);
  }

  return subparse_type;
}

static void
gst_subparse_base_init (GstSubparseClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  static GstElementDetails subparse_details = {
    "Subtitle parsers",
    "Codec/Parser/Subtitle",
    "Parses subtitle (.sub) files into text streams",
    "Gustavo J. A. M. Carneiro <gjc@inescporto.pt>\n"
        "Ronald S. Bultje <rbultje@ronald.bitfreak.net>"
  };

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_templ));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_templ));
  gst_element_class_set_details (element_class, &subparse_details);
}

static void
gst_subparse_class_init (GstSubparseClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  element_class->change_state = gst_subparse_change_state;
}

static void
gst_subparse_init (GstSubparse * subparse)
{
  subparse->sinkpad =
      gst_pad_new_from_template (gst_static_pad_template_get (&sink_templ),
      "sink");
  gst_element_add_pad (GST_ELEMENT (subparse), subparse->sinkpad);

  subparse->srcpad =
      gst_pad_new_from_template (gst_static_pad_template_get (&src_templ),
      "src");
  gst_pad_use_explicit_caps (subparse->srcpad);
  gst_pad_set_formats_function (subparse->srcpad, gst_subparse_formats);
  gst_pad_set_event_function (subparse->srcpad, gst_subparse_event);
  gst_pad_set_event_mask_function (subparse->srcpad, gst_subparse_eventmask);
  gst_element_add_pad (GST_ELEMENT (subparse), subparse->srcpad);

  gst_element_set_loop_function (GST_ELEMENT (subparse), gst_subparse_loop);

  subparse->textbuf = g_string_new (NULL);
  subparse->parser.type = GST_SUB_PARSE_FORMAT_UNKNOWN;
  subparse->parser_detected = FALSE;
  subparse->seek_time = GST_CLOCK_TIME_NONE;
  subparse->flush = FALSE;
}

/*
 * Source pad functions.
 */

static const GstFormat *
gst_subparse_formats (GstPad * pad)
{
  static const GstFormat formats[] = {
    GST_FORMAT_TIME,
    0
  };

  return formats;
}

static const GstEventMask *
gst_subparse_eventmask (GstPad * pad)
{
  static const GstEventMask masks[] = {
    {GST_EVENT_SEEK, GST_SEEK_METHOD_SET},
    {0, 0}
  };

  return masks;
}

static gboolean
gst_subparse_event (GstPad * pad, GstEvent * event)
{
  GstSubparse *self = GST_SUBPARSE (gst_pad_get_parent (pad));
  gboolean res = FALSE;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
      if (!(GST_EVENT_SEEK_FORMAT (event) == GST_FORMAT_TIME &&
              GST_EVENT_SEEK_METHOD (event) == GST_SEEK_METHOD_SET))
        break;
      self->seek_time = GST_EVENT_SEEK_OFFSET (event);
      res = TRUE;
      break;
    default:
      break;
  }

  gst_event_unref (event);

  return res;
}

/*
 * TRUE = continue, FALSE = stop.
 */

static gboolean
gst_subparse_handle_event (GstSubparse * self, GstEvent * event)
{
  gboolean res = TRUE;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_INTERRUPT:
      gst_event_unref (event);
      res = FALSE;
      break;
    case GST_EVENT_EOS:
      res = FALSE;
      /* fall-through */
    default:
      gst_pad_event_default (self->sinkpad, event);
      break;
  }

  return res;
}

static gchar *
convert_encoding (GstSubparse * self, const gchar * str, gsize len)
{
  gsize bytes_read, bytes_written;
  gchar *rv;
  GString *converted;

  converted = g_string_new (NULL);
  while (len) {
#ifndef GST_DISABLE_GST_DEBUG
    gchar *dbg = g_strndup (str, len);

    GST_DEBUG ("Trying to convert '%s'", dbg);
    g_free (dbg);
#endif

    rv = g_locale_to_utf8 (str, len, &bytes_read, &bytes_written, NULL);
    if (rv) {
      g_string_append_len (converted, rv, bytes_written);
      g_free (rv);

      len -= bytes_read;
      str += bytes_read;
    }
    if (len) {
      /* conversion error ocurred => skip one char */
      len--;
      str++;
      g_string_append_c (converted, '?');
    }
  }
  rv = converted->str;
  g_string_free (converted, FALSE);
  GST_DEBUG ("Converted to '%s'", rv);
  return rv;
}

static gchar *
get_next_line (GstSubparse * self)
{
  GstBuffer *buf;
  const char *line_end;
  int line_len;
  gboolean have_r = FALSE;
  gchar *line;

  if ((line_end = strchr (self->textbuf->str, '\n')) == NULL) {
    /* end-of-line not found; try to get more data */
    buf = NULL;
    do {
      GstData *data = gst_pad_pull (self->sinkpad);

      if (GST_IS_EVENT (data)) {
        if (!gst_subparse_handle_event (self, GST_EVENT (data)))
          return NULL;
      } else {
        buf = GST_BUFFER (data);
      }
    } while (!buf);
    self->textbuf = g_string_append_len (self->textbuf,
        GST_BUFFER_DATA (buf), GST_BUFFER_SIZE (buf));
    gst_buffer_unref (buf);
    /* search for end-of-line again */
    line_end = strchr (self->textbuf->str, '\n');
  }
  /* get rid of '\r' */
  if ((int) (line_end - self->textbuf->str) > 0 &&
      self->textbuf->str[(int) (line_end - self->textbuf->str) - 1] == '\r') {
    line_end--;
    have_r = TRUE;
  }

  if (line_end) {
    line_len = line_end - self->textbuf->str;
    line = convert_encoding (self, self->textbuf->str, line_len);
    self->textbuf = g_string_erase (self->textbuf, 0,
        line_len + (have_r ? 2 : 1));
    return line;
  }
  return NULL;
}

static gchar *
parse_mdvdsub (GstSubparse * self, guint64 * out_start_time,
    guint64 * out_end_time, gboolean after_seek)
{
  gchar *line, *line_start, *line_split, *line_chunk;
  guint start_frame, end_frame;

  /* FIXME: hardcoded for now, but detecting the correct value is
   * not going to be easy, I suspect... */
  const double frames_per_sec = 24000 / 1001.;
  GString *markup;
  gchar *rv;

  /* style variables */
  gboolean italic;
  gboolean bold;
  guint fontsize;

  line = line_start = get_next_line (self);
  if (!line)
    return NULL;

  if (sscanf (line, "{%u}{%u}", &start_frame, &end_frame) != 2) {
    g_warning ("Parse of the following line, assumed to be in microdvd .sub"
        " format, failed:\n%s", line);
    g_free (line_start);
    return NULL;
  }
  *out_start_time = (start_frame - 1000) / frames_per_sec * GST_SECOND;
  *out_end_time = (end_frame - 1000) / frames_per_sec * GST_SECOND;
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
    } else
      break;
  }
  rv = markup->str;
  g_string_free (markup, FALSE);
  g_free (line_start);
  GST_DEBUG ("parse_mdvdsub returning (start=%f, end=%f): %s",
      *out_start_time / (double) GST_SECOND,
      *out_end_time / (double) GST_SECOND, rv);
  return rv;
}

static void
parse_mdvdsub_init (GstSubparse * self)
{
  self->parser.deinit = NULL;
  self->parser.parse = parse_mdvdsub;
}

static gchar *
parse_subrip (GstSubparse * self, guint64 * out_start_time,
    guint64 * out_end_time, gboolean after_seek)
{
  gchar *line;
  guint h1, m1, s1, ms1;
  guint h2, m2, s2, ms2;
  int subnum;

  while (1) {
    switch (self->state.subrip.state) {
      case 0:
        /* looking for a single integer */
        line = get_next_line (self);
        if (!line)
          return NULL;
        if (sscanf (line, "%u", &subnum) == 1)
          self->state.subrip.state = 1;
        g_free (line);
        break;
      case 1:
        /* looking for start_time --> end_time */
        line = get_next_line (self);
        if (!line)
          return NULL;
        if (sscanf (line, "%u:%u:%u,%u --> %u:%u:%u,%u",
                &h1, &m1, &s1, &ms1, &h2, &m2, &s2, &ms2) == 8) {
          self->state.subrip.state = 2;
          self->state.subrip.time1 =
              (((guint64) h1) * 3600 + m1 * 60 + s1) * GST_SECOND +
              ms1 * GST_MSECOND;
          self->state.subrip.time2 =
              (((guint64) h2) * 3600 + m2 * 60 + s2) * GST_SECOND +
              ms2 * GST_MSECOND;
        } else {
          GST_DEBUG (0, "error parsing subrip time line");
          self->state.subrip.state = 0;
        }
        g_free (line);
        break;
      case 2:
        /* looking for subtitle text; empty line ends this
         * subtitle entry */
        line = get_next_line (self);
        if (!line)
          return NULL;
        if (self->state.subrip.buf->len)
          g_string_append_c (self->state.subrip.buf, '\n');
        g_string_append (self->state.subrip.buf, line);
        if (strlen (line) == 0) {
          gchar *rv;

          g_free (line);
          *out_start_time = self->state.subrip.time1;
          *out_end_time = self->state.subrip.time2;
          rv = g_markup_escape_text (self->state.subrip.buf->str,
              self->state.subrip.buf->len);
          g_string_truncate (self->state.subrip.buf, 0);
          self->state.subrip.state = 0;
          return rv;
        }
        g_free (line);
    }
  }
}

static void
parse_subrip_deinit (GstSubparse * self)
{
  g_string_free (self->state.subrip.buf, TRUE);
}

static void
parse_subrip_init (GstSubparse * self)
{
  self->state.subrip.state = 0;
  self->state.subrip.buf = g_string_new (NULL);
  self->parser.parse = parse_subrip;
  self->parser.deinit = parse_subrip_deinit;
}


static gchar *
parse_mpsub (GstSubparse * self, guint64 * out_start_time,
    guint64 * out_end_time, gboolean after_seek)
{
  gchar *line;
  float t1, t2;

  if (after_seek) {
    self->state.mpsub.time = 0;
  }

  while (1) {
    switch (self->state.mpsub.state) {
      case 0:
        /* looking for two floats (offset, duration) */
        line = get_next_line (self);
        if (!line)
          return NULL;
        if (sscanf (line, "%f %f", &t1, &t2) == 2) {
          self->state.mpsub.state = 1;
          self->state.mpsub.time += GST_SECOND * t1;
        }
        g_free (line);
        break;
      case 1:
        /* looking for subtitle text; empty line ends this
         * subtitle entry */
        line = get_next_line (self);
        if (!line)
          return NULL;
        if (self->state.mpsub.buf->len)
          g_string_append_c (self->state.mpsub.buf, '\n');
        g_string_append (self->state.mpsub.buf, line);
        if (strlen (line) == 0) {
          gchar *rv;

          g_free (line);
          *out_start_time = self->state.mpsub.time;
          *out_end_time = self->state.mpsub.time + t2 * GST_SECOND;
          self->state.mpsub.time += t2 * GST_SECOND;
          rv = g_markup_escape_text (self->state.mpsub.buf->str,
              self->state.mpsub.buf->len);
          rv = g_strdup (self->state.mpsub.buf->str);
          g_string_truncate (self->state.mpsub.buf, 0);
          self->state.mpsub.state = 0;
          return rv;
        }
        g_free (line);
        break;
    }
  }

  return NULL;
}

static void
parse_mpsub_deinit (GstSubparse * self)
{
  g_string_free (self->state.mpsub.buf, TRUE);
}

static void
parse_mpsub_init (GstSubparse * self)
{
  self->state.mpsub.state = 0;
  self->state.mpsub.buf = g_string_new (NULL);
  self->parser.deinit = parse_mpsub_deinit;
  self->parser.parse = parse_mpsub;
}

/*
 * FIXME: maybe we should pass along a second argument, the preceding
 * text buffer, because that is how this originally worked, even though
 * I don't really see the use of that.
 */

static GstSubParseFormat
gst_subparse_buffer_format_autodetect (GstBuffer * buf)
{
  static gboolean need_init_regexps = TRUE;
  static regex_t mdvd_rx;
  static regex_t subrip_rx;

  /* Copy out chars to guard against short non-null-terminated buffers */
  const gint match_chars = 35;
  gchar *match_str =
      g_strndup ((const gchar *) GST_BUFFER_DATA (buf), MIN (match_chars,
          GST_BUFFER_SIZE (buf)));

  if (!match_str)
    return GST_SUB_PARSE_FORMAT_UNKNOWN;

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
    g_free (match_str);
    return GST_SUB_PARSE_FORMAT_MDVDSUB;
  }
  if (regexec (&subrip_rx, match_str, 0, NULL, 0) == 0) {
    GST_LOG ("subparse: SubRip (time based) format detected");
    g_free (match_str);
    return GST_SUB_PARSE_FORMAT_SUBRIP;
  }
  if (!strncmp (match_str, "FORMAT=TIME", 11)) {
    GST_LOG ("subparse: MPSub (time based) format detected");
    g_free (match_str);
    return GST_SUB_PARSE_FORMAT_MPSUB;
  }

  GST_WARNING ("subparse: subtitle format autodetection failed!");
  g_free (match_str);
  return GST_SUB_PARSE_FORMAT_UNKNOWN;
}

static gboolean
gst_subparse_format_autodetect (GstSubparse * self)
{
  GstBuffer *buf = NULL;
  GstSubParseFormat format;
  gboolean res = TRUE;

  do {
    GstData *data = gst_pad_pull (self->sinkpad);

    if (GST_IS_EVENT (data)) {
      if (!gst_subparse_handle_event (self, GST_EVENT (data)))
        return FALSE;
    } else {
      buf = GST_BUFFER (data);
    }
  } while (!buf);
  self->textbuf = g_string_append_len (self->textbuf, GST_BUFFER_DATA (buf),
      GST_BUFFER_SIZE (buf));
  format = gst_subparse_buffer_format_autodetect (buf);
  gst_buffer_unref (buf);
  self->parser_detected = TRUE;
  self->parser.type = format;
  switch (format) {
    case GST_SUB_PARSE_FORMAT_MDVDSUB:
      GST_DEBUG ("MicroDVD format detected");
      parse_mdvdsub_init (self);
      res = gst_pad_set_explicit_caps (self->srcpad,
          gst_caps_new_simple ("text/x-pango-markup", NULL));
      break;
    case GST_SUB_PARSE_FORMAT_SUBRIP:
      GST_DEBUG ("SubRip format detected");
      parse_subrip_init (self);
      res = gst_pad_set_explicit_caps (self->srcpad,
          gst_caps_new_simple ("text/plain", NULL));
      break;
    case GST_SUB_PARSE_FORMAT_MPSUB:
      GST_DEBUG ("MPSub format detected");
      parse_mpsub_init (self);
      res = gst_pad_set_explicit_caps (self->srcpad,
          gst_caps_new_simple ("text/plain", NULL));
      break;
    case GST_SUB_PARSE_FORMAT_UNKNOWN:
    default:
      GST_DEBUG ("no subtitle format detected");
      GST_ELEMENT_ERROR (self, STREAM, WRONG_TYPE,
          ("The input is not a valid/supported subtitle file"), (NULL));
      res = FALSE;
      break;
  }

  return res;
}

/*
 * parse input, getting a start and end time
 * then parse next input, and if next start time > current end time, send
 * clear buffer.
 */

static void
gst_subparse_loop (GstElement * element)
{
  GstSubparse *self;
  GstBuffer *buf;
  guint64 start_time, end_time, need_time = GST_CLOCK_TIME_NONE;
  gchar *subtitle;
  gboolean after_seek = FALSE;

  GST_DEBUG ("gst_subparse_loop");
  self = GST_SUBPARSE (element);

  /* make sure we know the format */
  if (!self->parser_detected) {
    if (!gst_subparse_format_autodetect (self))
      return;
  }

  /* handle seeks */
  if (GST_CLOCK_TIME_IS_VALID (self->seek_time)) {
    GstEvent *seek;

    seek = gst_event_new_seek (GST_SEEK_FLAG_FLUSH | GST_FORMAT_BYTES |
        GST_SEEK_METHOD_SET, 0);
    if (gst_pad_send_event (GST_PAD_PEER (self->sinkpad), seek)) {
      need_time = self->seek_time;
      after_seek = TRUE;

      if (self->flush) {
        gst_pad_push (self->srcpad, GST_DATA (gst_event_new (GST_EVENT_FLUSH)));
        self->flush = FALSE;
      }
      gst_pad_push (self->srcpad,
          GST_DATA (gst_event_new_discontinuous (FALSE,
                  GST_FORMAT_TIME, need_time, GST_FORMAT_UNDEFINED)));
    }

    self->seek_time = GST_CLOCK_TIME_NONE;
  }

  /* get a next buffer */
  GST_INFO ("getting text buffer");
  if (!self->parser.parse || self->parser.type == GST_SUB_PARSE_FORMAT_UNKNOWN) {
    GST_ELEMENT_ERROR (self, LIBRARY, INIT, (NULL), (NULL));
    return;
  }

  do {
    subtitle = self->parser.parse (self, &start_time, &end_time, after_seek);
    if (!subtitle)
      return;
    after_seek = FALSE;

    if (GST_CLOCK_TIME_IS_VALID (need_time) && end_time < need_time) {
      g_free (subtitle);
    } else {
      need_time = GST_CLOCK_TIME_NONE;
      GST_DEBUG ("subparse: loop: text %s, start %lld, end %lld\n",
          subtitle, start_time, end_time);

      buf = gst_buffer_new ();
      GST_BUFFER_DATA (buf) = subtitle;
      GST_BUFFER_SIZE (buf) = strlen (subtitle);
      GST_BUFFER_TIMESTAMP (buf) = start_time;
      GST_BUFFER_DURATION (buf) = end_time - start_time;
      GST_DEBUG ("sending text buffer %s at %lld", subtitle, start_time);
      gst_pad_push (self->srcpad, GST_DATA (buf));
    }
  } while (GST_CLOCK_TIME_IS_VALID (need_time));
}

static GstElementStateReturn
gst_subparse_change_state (GstElement * element)
{
  GstSubparse *self = GST_SUBPARSE (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_PAUSED_TO_READY:
      if (self->parser.deinit)
        self->parser.deinit (self);
      self->parser.type = GST_SUB_PARSE_FORMAT_UNKNOWN;
      self->parser_detected = FALSE;
      self->seek_time = GST_CLOCK_TIME_NONE;
      self->flush = FALSE;
      break;
    default:
      break;
  }

  return parent_class->change_state (element);
}

#if 0
/* typefinding stuff */
static GstTypeDefinition subparse_definition = {
  "subparse/x-text",
  "text/plain",
  ".sub",
  gst_subparse_type_find,
};
static GstCaps *
gst_subparse_type_find (GstBuffer * buf, gpointer private)
{
  GstSubParseFormat format;

  format = gst_subparse_buffer_format_autodetect (buf);
  switch (format) {
    case GST_SUB_PARSE_FORMAT_MDVDSUB:
      GST_DEBUG (GST_CAT_PLUGIN_INFO, "MicroDVD format detected");
      return gst_caps_new ("subparse_type_find", "text/plain", NULL);
    case GST_SUB_PARSE_FORMAT_SUBRIP:
      GST_DEBUG (GST_CAT_PLUGIN_INFO, "SubRip format detected");
      return gst_caps_new ("subparse_type_find", "text/plain", NULL);
    case GST_SUB_PARSE_FORMAT_UNKNOWN:
      GST_DEBUG (GST_CAT_PLUGIN_INFO, "no subtitle format detected");
      break;
  }
  /* don't know which this is */
  return NULL;
}
#endif

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (subparse_debug, "subparse", 0, ".sub parser");

  return gst_element_register (plugin, "subparse",
      GST_RANK_PRIMARY, GST_TYPE_SUBPARSE);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "subparse",
    "Subtitle (.sub) file parsing",
    plugin_init, VERSION, "LGPL", GST_PACKAGE, GST_ORIGIN)
