/* GStreamer
 * Copyright (C) 2010 Marc-Andre Lureau <marcandre.lureau@gmail.com>
 * Copyright (C) 2015 Tim-Philipp Müller <tim@centricular.com>
 *
 * Copyright (C) 2021-2022 Centricular Ltd
 *   Author: Edward Hervey <edward@centricular.com>
 *   Author: Jan Schmidt <jan@centricular.com>
 *
 * m3u8.c:
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

#include <stdlib.h>
#include <math.h>
#include <errno.h>
#include <glib.h>
#include <gmodule.h>
#include <string.h>

#include <gst/pbutils/pbutils.h>
#include "m3u8.h"
#include "gstadaptivedemux.h"
#include "gsthlselements.h"

#define GST_CAT_DEFAULT hls2_debug

static gchar *uri_join (const gchar * uri, const gchar * path);
static void
gst_m3u8_media_segment_fill_partial_stream_times (GstM3U8MediaSegment *
    segment);

GstHLSMediaPlaylist *
gst_hls_media_playlist_ref (GstHLSMediaPlaylist * m3u8)
{
  g_assert (m3u8 != NULL && m3u8->ref_count > 0);

  g_atomic_int_add (&m3u8->ref_count, 1);
  return m3u8;
}

void
gst_hls_media_playlist_unref (GstHLSMediaPlaylist * self)
{
  g_return_if_fail (self != NULL && self->ref_count > 0);

  if (g_atomic_int_dec_and_test (&self->ref_count)) {
    g_free (self->uri);
    g_free (self->base_uri);

    g_ptr_array_free (self->segments, TRUE);

    if (self->preload_hints != NULL)
      g_ptr_array_free (self->preload_hints, TRUE);

    g_free (self->last_data);
    g_mutex_clear (&self->lock);
    g_free (self);
  }
}

static GstM3U8MediaSegment *
gst_m3u8_media_segment_new (gchar * uri, gchar * title, GstClockTime duration,
    gint64 sequence, gint64 discont_sequence, gint64 size, gint64 offset)
{
  GstM3U8MediaSegment *file;

  file = g_new0 (GstM3U8MediaSegment, 1);
  file->uri = uri;
  file->title = title;
  file->duration = duration;
  file->sequence = sequence;
  file->discont_sequence = discont_sequence;
  file->ref_count = 1;

  file->stream_time = GST_CLOCK_STIME_NONE;

  file->size = size;
  if (size != -1 && offset != -1) {
    file->offset = offset;
  }

  return file;
}

GstM3U8MediaSegment *
gst_m3u8_media_segment_ref (GstM3U8MediaSegment * mfile)
{
  g_assert (mfile != NULL && mfile->ref_count > 0);

  g_atomic_int_add (&mfile->ref_count, 1);
  return mfile;
}

void
gst_m3u8_media_segment_unref (GstM3U8MediaSegment * self)
{
  g_return_if_fail (self != NULL && self->ref_count > 0);

  if (g_atomic_int_dec_and_test (&self->ref_count)) {
    if (self->init_file)
      gst_m3u8_init_file_unref (self->init_file);
    g_free (self->title);
    g_free (self->uri);
    g_free (self->key);
    if (self->datetime)
      g_date_time_unref (self->datetime);
    if (self->partial_segments)
      g_ptr_array_free (self->partial_segments, TRUE);
    g_free (self);
  }
}

GstM3U8PartialSegment *
gst_m3u8_partial_segment_ref (GstM3U8PartialSegment * part)
{
  g_assert (part != NULL && part->ref_count > 0);

  g_atomic_int_add (&part->ref_count, 1);
  return part;
}

void
gst_m3u8_partial_segment_unref (GstM3U8PartialSegment * part)
{
  g_return_if_fail (part != NULL && part->ref_count > 0);

  if (g_atomic_int_dec_and_test (&part->ref_count)) {
    g_free (part->uri);
    g_free (part);
  }
}

GstM3U8PreloadHint *
gst_m3u8_preload_hint_ref (GstM3U8PreloadHint * hint)
{
  g_assert (hint != NULL && hint->ref_count > 0);

  g_atomic_int_add (&hint->ref_count, 1);
  return hint;
}

void
gst_m3u8_preload_hint_unref (GstM3U8PreloadHint * hint)
{
  g_return_if_fail (hint != NULL && hint->ref_count > 0);

  if (g_atomic_int_dec_and_test (&hint->ref_count)) {
    g_free (hint->uri);
    g_free (hint);
  }
}

static GstM3U8InitFile *
gst_m3u8_init_file_new (gchar * uri, gint64 size, gint64 offset)
{
  GstM3U8InitFile *file;

  file = g_new0 (GstM3U8InitFile, 1);
  file->uri = uri;
  file->ref_count = 1;

  file->size = size;
  if (size != -1 && offset != -1) {
    file->offset = offset;
  }

  return file;
}

GstM3U8InitFile *
gst_m3u8_init_file_ref (GstM3U8InitFile * ifile)
{
  g_assert (ifile != NULL && ifile->ref_count > 0);

  g_atomic_int_add (&ifile->ref_count, 1);
  return ifile;
}

void
gst_m3u8_init_file_unref (GstM3U8InitFile * self)
{
  g_return_if_fail (self != NULL && self->ref_count > 0);

  if (g_atomic_int_dec_and_test (&self->ref_count)) {
    g_free (self->uri);
    g_free (self);
  }
}

gboolean
gst_m3u8_init_file_equal (const GstM3U8InitFile * ifile1,
    const GstM3U8InitFile * ifile2)
{
  if (ifile1 == ifile2)
    return TRUE;

  if (ifile1 == NULL && ifile2 != NULL)
    return FALSE;
  if (ifile1 != NULL && ifile2 == NULL)
    return FALSE;

  if (!g_str_equal (ifile1->uri, ifile2->uri))
    return FALSE;
  if (ifile1->offset != ifile2->offset)
    return FALSE;
  if (ifile1->size != ifile2->size)
    return FALSE;

  return TRUE;
}

static gboolean
int_from_string (gchar * ptr, gchar ** endptr, gint * val)
{
  gchar *end;
  gint64 ret;

  g_return_val_if_fail (ptr != NULL, FALSE);
  g_return_val_if_fail (val != NULL, FALSE);

  errno = 0;
  ret = g_ascii_strtoll (ptr, &end, 10);
  if ((errno == ERANGE && (ret == G_MAXINT64 || ret == G_MININT64))
      || (errno != 0 && ret == 0)) {
    GST_WARNING ("%s", g_strerror (errno));
    return FALSE;
  }

  if (ret > G_MAXINT || ret < G_MININT) {
    GST_WARNING ("%s", g_strerror (ERANGE));
    return FALSE;
  }

  if (endptr)
    *endptr = end;

  *val = (gint) ret;

  return end != ptr;
}

static gboolean
int64_from_string (gchar * ptr, gchar ** endptr, gint64 * val)
{
  gchar *end;
  gint64 ret;

  g_return_val_if_fail (ptr != NULL, FALSE);
  g_return_val_if_fail (val != NULL, FALSE);

  errno = 0;
  ret = g_ascii_strtoll (ptr, &end, 10);
  if ((errno == ERANGE && (ret == G_MAXINT64 || ret == G_MININT64))
      || (errno != 0 && ret == 0)) {
    GST_WARNING ("%s", g_strerror (errno));
    return FALSE;
  }

  if (endptr)
    *endptr = end;

  *val = ret;

  return end != ptr;
}

static gboolean
double_from_string (gchar * ptr, gchar ** endptr, gdouble * val)
{
  gchar *end;
  gdouble ret;

  g_return_val_if_fail (ptr != NULL, FALSE);
  g_return_val_if_fail (val != NULL, FALSE);

  errno = 0;
  ret = g_ascii_strtod (ptr, &end);
  if ((errno == ERANGE && (ret == HUGE_VAL || ret == -HUGE_VAL))
      || (errno != 0 && ret == 0)) {
    GST_WARNING ("%s", g_strerror (errno));
    return FALSE;
  }

  if (!isfinite (ret)) {
    GST_WARNING ("%s", g_strerror (ERANGE));
    return FALSE;
  }

  if (endptr)
    *endptr = end;

  *val = (gdouble) ret;

  return end != ptr;
}

static gboolean
time_from_double_in_string (gchar * ptr, gchar ** endptr, GstClockTime * val)
{
  double fval;
  if (!double_from_string (ptr, endptr, &fval)) {
    return FALSE;
  }
  *val = fval * (gdouble) GST_SECOND;
  return TRUE;
}

static gboolean
parse_attributes (gchar ** ptr, gchar ** a, gchar ** v)
{
  gchar *end = NULL, *p, *ve;

  g_return_val_if_fail (ptr != NULL, FALSE);
  g_return_val_if_fail (*ptr != NULL, FALSE);
  g_return_val_if_fail (a != NULL, FALSE);
  g_return_val_if_fail (v != NULL, FALSE);

  /* [attribute=value,]* */

  *a = *ptr;
  end = p = g_utf8_strchr (*ptr, -1, ',');
  if (end) {
    gchar *q = g_utf8_strchr (*ptr, -1, '"');
    if (q && q < end) {
      /* special case, such as CODECS="avc1.77.30, mp4a.40.2" */
      q = g_utf8_next_char (q);
      if (q) {
        q = g_utf8_strchr (q, -1, '"');
      }
      if (q) {
        end = p = g_utf8_strchr (q, -1, ',');
      }
    }
  }
  if (end) {
    do {
      end = g_utf8_next_char (end);
    } while (end && *end == ' ');
    *p = '\0';
  }

  *v = p = g_utf8_strchr (*ptr, -1, '=');
  if (*v) {
    *p = '\0';
    *v = g_utf8_next_char (*v);
    if (**v == '"') {
      ve = g_utf8_next_char (*v);
      if (ve) {
        ve = g_utf8_strchr (ve, -1, '"');
      }
      if (ve) {
        *v = g_utf8_next_char (*v);
        *ve = '\0';
      } else {
        GST_WARNING ("Cannot remove quotation marks from %s", *a);
      }
    }
  } else {
    GST_WARNING ("missing = after attribute");
    return FALSE;
  }

  *ptr = end;
  return TRUE;
}

GstHLSMediaPlaylist *
gst_hls_media_playlist_new (const gchar * uri, const gchar * base_uri)
{
  GstHLSMediaPlaylist *m3u8;

  m3u8 = g_new0 (GstHLSMediaPlaylist, 1);

  m3u8->uri = g_strdup (uri);
  m3u8->base_uri = g_strdup (base_uri);

  m3u8->version = 1;
  m3u8->type = GST_HLS_PLAYLIST_TYPE_UNDEFINED;
  m3u8->targetduration = GST_CLOCK_TIME_NONE;
  m3u8->partial_targetduration = GST_CLOCK_TIME_NONE;
  m3u8->media_sequence = 0;
  m3u8->discont_sequence = 0;
  m3u8->endlist = FALSE;
  m3u8->i_frame = FALSE;
  m3u8->allowcache = TRUE;

  m3u8->ext_x_key_present = FALSE;
  m3u8->ext_x_pdt_present = FALSE;

  m3u8->segments =
      g_ptr_array_new_full (16, (GDestroyNotify) gst_m3u8_media_segment_unref);

  m3u8->duration = 0;

  m3u8->skip_boundary = GST_CLOCK_TIME_NONE;
  m3u8->hold_back = GST_CLOCK_TIME_NONE;
  m3u8->part_hold_back = GST_CLOCK_TIME_NONE;

  g_mutex_init (&m3u8->lock);
  m3u8->ref_count = 1;

  return m3u8;
}

void
gst_hls_media_playlist_dump (GstHLSMediaPlaylist * self)
{
#ifndef GST_DISABLE_GST_DEBUG
  guint idx;
  gchar *datestring;

  GST_DEBUG ("uri              : %s", self->uri);
  GST_DEBUG ("base_uri         : %s", self->base_uri);

  GST_DEBUG ("version          : %d", self->version);

  GST_DEBUG ("targetduration   : %" GST_TIME_FORMAT,
      GST_TIME_ARGS (self->targetduration));
  GST_DEBUG ("partial segment targetduration   : %" GST_TIME_FORMAT,
      GST_TIME_ARGS (self->partial_targetduration));
  GST_DEBUG ("media_sequence   : %" G_GINT64_FORMAT, self->media_sequence);
  GST_DEBUG ("discont_sequence : %" G_GINT64_FORMAT, self->discont_sequence);

  GST_DEBUG ("endlist          : %s",
      self->endlist ? "present" : "NOT present");
  GST_DEBUG ("i_frame          : %s", self->i_frame ? "YES" : "NO");

  GST_DEBUG ("EXT-X-KEY        : %s",
      self->ext_x_key_present ? "present" : "NOT present");
  GST_DEBUG ("EXT-X-PROGRAM-DATE-TIME : %s",
      self->ext_x_pdt_present ? "present" : "NOT present");

  GST_DEBUG ("duration         : %" GST_TIME_FORMAT,
      GST_TIME_ARGS (self->duration));

  GST_DEBUG ("skip boundary    : %" GST_TIME_FORMAT,
      GST_TIME_ARGS (self->skip_boundary));

  GST_DEBUG ("skip dateranges  : %s", self->can_skip_dateranges ? "YES" : "NO");

  GST_DEBUG ("hold back        : %" GST_TIME_FORMAT,
      GST_TIME_ARGS (self->hold_back));
  GST_DEBUG ("part hold back   : %" GST_TIME_FORMAT,
      GST_TIME_ARGS (self->part_hold_back));

  GST_DEBUG ("can block reloads: %s", self->can_block_reload ? "YES" : "NO");

  GST_DEBUG ("Segments : %d", self->segments->len);
  for (idx = 0; idx < self->segments->len; idx++) {
    GstM3U8MediaSegment *segment = g_ptr_array_index (self->segments, idx);

    GST_DEBUG ("  sequence:%" G_GINT64_FORMAT " discont_sequence:%"
        G_GINT64_FORMAT, segment->sequence, segment->discont_sequence);
    GST_DEBUG ("    partial only: %s", segment->partial_only ? "YES" : "NO");
    GST_DEBUG ("    stream_time : %" GST_STIME_FORMAT,
        GST_STIME_ARGS (segment->stream_time));
    GST_DEBUG ("    duration    :  %" GST_TIME_FORMAT,
        GST_TIME_ARGS (segment->duration));
    if (segment->title)
      GST_DEBUG ("    title       : %s", segment->title);
    GST_DEBUG ("    discont     : %s", segment->discont ? "YES" : "NO");
    if (segment->datetime) {
      datestring = g_date_time_format_iso8601 (segment->datetime);
      GST_DEBUG ("    date/time    : %s", datestring);
      g_free (datestring);
    }
    GST_DEBUG ("    uri         : %s %" G_GUINT64_FORMAT " %" G_GINT64_FORMAT,
        segment->uri, segment->offset, segment->size);

    GST_DEBUG ("    is gap      : %s", segment->is_gap ? "YES" : "NO");

    if (segment->partial_segments != NULL) {
      guint part_idx;
      for (part_idx = 0; part_idx < segment->partial_segments->len; part_idx++) {
        GstM3U8PartialSegment *part =
            g_ptr_array_index (segment->partial_segments, part_idx);
        GST_DEBUG ("    partial segment %u:", part_idx);
        GST_DEBUG ("      uri         : %s %" G_GUINT64_FORMAT " %"
            G_GINT64_FORMAT, part->uri, part->offset, part->size);
        GST_DEBUG ("      stream_time : %" GST_STIME_FORMAT,
            GST_STIME_ARGS (part->stream_time));
        GST_DEBUG ("      duration    : %" GST_TIME_FORMAT,
            GST_TIME_ARGS (part->duration));
        GST_DEBUG ("      is gap      : %s", part->is_gap ? "YES" : "NO");
        GST_DEBUG ("      independent : %s", part->independent ? "YES" : "NO");
      }
    }
  }

  if (self->preload_hints) {
    GST_DEBUG ("Preload Hints: %d", self->preload_hints->len);
    for (idx = 0; idx < self->preload_hints->len; idx++) {
      GstM3U8PreloadHint *hint = g_ptr_array_index (self->preload_hints, idx);
      const gchar *hint_type_str;
      switch (hint->hint_type) {
        case M3U8_PRELOAD_HINT_MAP:
          hint_type_str = "MAP";
          break;
        case M3U8_PRELOAD_HINT_PART:
          hint_type_str = "PART";
          break;
        default:
          g_assert_not_reached ();
      }

      GST_DEBUG ("    preload hint %u: type %s", idx, hint_type_str);
      GST_DEBUG ("      uri         : %s %" G_GUINT64_FORMAT " %"
          G_GINT64_FORMAT, hint->uri, hint->offset, hint->size);
    }
  }
#endif
}

static void
gst_hls_media_playlist_postprocess_pdt (GstHLSMediaPlaylist * self)
{
  gint idx, len = self->segments->len;
  gint first_pdt = -1;
  GstM3U8MediaSegment *previous = NULL;
  GstM3U8MediaSegment *segment = NULL;

  /* Iterate forward, and make sure datetimes are coherent */
  for (idx = 0; idx < len; idx++, previous = segment) {
    segment = g_ptr_array_index (self->segments, idx);

#define ABSDIFF(a,b) ((a) > (b) ? (a) - (b) : (b) - (a))

    if (segment->datetime) {
      if (first_pdt == -1)
        first_pdt = idx;
      if (!segment->discont && previous && previous->datetime) {
        GstClockTimeDiff diff = g_date_time_difference (segment->datetime,
            previous->datetime) * GST_USECOND;
        if (ABSDIFF (diff, previous->duration) > 500 * GST_MSECOND) {
          GST_LOG ("PDT diff %" GST_STIME_FORMAT " previous duration %"
              GST_TIME_FORMAT, GST_STIME_ARGS (diff),
              GST_TIME_ARGS (previous->duration));
          g_date_time_unref (segment->datetime);
          segment->datetime =
              g_date_time_add (previous->datetime,
              previous->duration / GST_USECOND);
        }
      }
    } else {
      if (segment->discont) {
        GST_WARNING ("Discont segment doesn't have a PDT !");
      } else if (previous) {
        if (previous->datetime) {
          segment->datetime =
              g_date_time_add (previous->datetime,
              previous->duration / GST_USECOND);
          GST_LOG
              ("Generated new PDT based on previous segment PDT and duration");
        } else {
          GST_LOG ("Missing PDT, but can't generate it from previous one");
        }
      }
    }
  }

  if (first_pdt != -1 && first_pdt != 0) {
    GST_LOG ("Scanning backwards from %d", first_pdt);
    previous = g_ptr_array_index (self->segments, first_pdt);
    for (idx = first_pdt - 1; idx >= 0; idx = idx - 1) {
      GST_LOG ("%d", idx);
      segment = g_ptr_array_index (self->segments, idx);
      if (!segment->datetime && previous->datetime) {
        segment->datetime =
            g_date_time_add (previous->datetime,
            -(segment->duration / GST_USECOND));
      }
      previous = segment;
    }
  }
}

static GstM3U8PartialSegment *
gst_m3u8_parse_partial_segment (gchar * data, const gchar * base_uri)
{
  gchar *v, *a;
  gboolean have_duration = FALSE;
  GstM3U8PartialSegment *part = g_new0 (GstM3U8PartialSegment, 1);

  part->ref_count = 1;
  part->stream_time = GST_CLOCK_STIME_NONE;
  part->size = -1;

  while (data != NULL && parse_attributes (&data, &a, &v)) {
    if (strcmp (a, "URI") == 0) {
      g_free (part->uri);
      part->uri = uri_join (base_uri, v);
    } else if (strcmp (a, "DURATION") == 0) {
      if (!time_from_double_in_string (v, NULL, &part->duration)) {
        GST_WARNING ("Can't read EXT-X-PART duration");
        goto malformed_line;
      }
      have_duration = TRUE;
    } else if (strcmp (a, "INDEPENDENT") == 0) {
      part->independent = g_ascii_strcasecmp (v, "yes") == 0;
    } else if (strcmp (a, "GAP") == 0) {
      part->is_gap = g_ascii_strcasecmp (v, "yes") == 0;
    } else if (strcmp (a, "BYTERANGE") == 0) {
      if (int64_from_string (v, &v, &part->size)) {
        goto malformed_line;
      }
      if (*v == '@' && !int64_from_string (v + 1, &v, &part->offset)) {
        goto malformed_line;
      }
    }
  }

  if (part->uri == NULL || !have_duration) {
    goto required_attributes_missing;
  }

  return part;

required_attributes_missing:
  {
    GST_WARNING
        ("EXT-X-PART description is missing required URI or DURATION attributes");
    gst_m3u8_partial_segment_unref (part);
    return NULL;
  }
malformed_line:
  {
    GST_WARNING ("Invalid EXT-X-PART entry in playlist");
    gst_m3u8_partial_segment_unref (part);
    return NULL;
  }
}

static GstM3U8PreloadHint *
gst_m3u8_parse_preload_hint (gchar * data, const gchar * base_uri)
{
  gchar *v, *a;
  GstM3U8PreloadHint *hint = g_new0 (GstM3U8PreloadHint, 1);
  gboolean have_hint_type = FALSE;

  hint->ref_count = 1;
  hint->size = -1;

  while (data != NULL && parse_attributes (&data, &a, &v)) {
    if (strcmp (a, "TYPE") == 0) {
      if (g_ascii_strcasecmp (v, "MAP") == 0) {
        hint->hint_type = M3U8_PRELOAD_HINT_MAP;
      } else if (g_ascii_strcasecmp (v, "PART") == 0) {
        hint->hint_type = M3U8_PRELOAD_HINT_PART;
      } else {
        GST_WARNING ("Unknown Preload Hint type %s", v);
        goto malformed_line;
      }
      have_hint_type = TRUE;
    } else if (strcmp (a, "URI") == 0) {
      g_free (hint->uri);
      hint->uri = uri_join (base_uri, v);
    } else if (strcmp (a, "BYTERANGE-START") == 0) {
      if (int64_from_string (v, NULL, &hint->offset)) {
        goto malformed_line;
      }
    } else if (strcmp (a, "BYTERANGE-LENGTH") == 0) {
      if (int64_from_string (v, NULL, &hint->size)) {
        goto malformed_line;
      }
    }
  }

  if (hint->uri == NULL || !have_hint_type) {
    goto required_attributes_missing;
  }

  return hint;

required_attributes_missing:
  {
    GST_WARNING
        ("EXT-X-PRELOAD-HINT is missing required URI or TYPE attributes");
    gst_m3u8_preload_hint_unref (hint);
    return NULL;
  }
malformed_line:
  {
    GST_WARNING ("Invalid EXT-X-PRELOAD-HINT entry in playlist");
    gst_m3u8_preload_hint_unref (hint);
    return NULL;
  }
}

static void
parse_server_control (GstHLSMediaPlaylist * self, gchar * data)
{
  gchar *v, *a;

  while (data != NULL && parse_attributes (&data, &a, &v)) {
    if (strcmp (a, "CAN-SKIP-UNTIL") == 0) {
      if (!time_from_double_in_string (v, NULL, &self->skip_boundary)) {
        GST_WARNING ("Can't read Skip Boundary value");
        goto malformed_line;
      }
    } else if (strcmp (a, "CAN-SKIP-DATERANGES") == 0) {
      self->can_skip_dateranges = g_ascii_strcasecmp (v, "YES") == 0;
    } else if (strcmp (a, "HOLD-BACK") == 0) {
      if (!time_from_double_in_string (v, NULL, &self->hold_back)) {
        GST_WARNING ("Can't read Hold-Back value");
        goto malformed_line;
      }
    } else if (strcmp (a, "PART-HOLD-BACK") == 0) {
      if (!time_from_double_in_string (v, NULL, &self->part_hold_back)) {
        GST_WARNING ("Can't read Part-Hold-Back value");
        goto malformed_line;
      }
    } else if (strcmp (a, "CAN-BLOCK-RELOAD") == 0) {
      self->can_block_reload = g_ascii_strcasecmp (v, "YES") == 0;
    }
  }

  return;
malformed_line:
  {
    GST_WARNING ("Invalid EXT-X-SERVER-CONTROL entry in playlist");
    return;
  }
}

/* Parse and create a new GstHLSMediaPlaylist */
GstHLSMediaPlaylist *
gst_hls_media_playlist_parse (gchar * data, const gchar * uri,
    const gchar * base_uri)
{
  gchar *input_data = data;
  GstHLSMediaPlaylist *self;
  gint val;
  GstClockTime duration, partial_duration;
  gchar *title, *end;
  gboolean discontinuity = FALSE;
  gchar *current_key = NULL;
  gboolean have_iv = FALSE;
  guint8 iv[16] = { 0, };
  gint64 size = -1, offset = -1;
  gint64 mediasequence = 0;
  gint64 dsn = 0;
  GDateTime *date_time = NULL;
  GstM3U8InitFile *last_init_file = NULL;
  GstM3U8MediaSegment *previous = NULL;
  GPtrArray *partial_segments = NULL;
  gboolean is_gap = FALSE;

  GST_LOG ("uri: %s", uri);
  GST_LOG ("base_uri: %s", base_uri);
  GST_TRACE ("data:\n%s", data);

  if (!g_str_has_prefix (data, "#EXTM3U")) {
    GST_WARNING ("Data doesn't start with #EXTM3U");
    g_free (data);
    return NULL;
  }

  if (g_strrstr (data, "\n#EXT-X-STREAM-INF:") != NULL) {
    GST_WARNING ("Not a media playlist, but a master playlist!");
    g_free (data);
    return NULL;
  }

  self = gst_hls_media_playlist_new (uri, base_uri);

  /* Store a copy of the data */
  self->last_data = g_strdup (data);

  duration = 0;
  partial_duration = 0;
  title = NULL;
  data += 7;
  while (TRUE) {
    gchar *r;

    end = g_utf8_strchr (data, -1, '\n');
    if (end)
      *end = '\0';

    r = g_utf8_strchr (data, -1, '\r');
    if (r)
      *r = '\0';

    if (data[0] != '#' && data[0] != '\0') {
      if (duration <= 0) {
        GST_LOG ("%s: got line without EXTINF, dropping", data);
        goto next_line;
      }

      data = uri_join (self->base_uri ? self->base_uri : self->uri, data);

      /* Let's check this is not a bogus duplicate entry */
      if (previous && !discontinuity && !g_strcmp0 (data, previous->uri)
          && (offset == -1 || previous->offset == offset)) {
        GST_WARNING ("Dropping duplicate segment entry");
        g_free (data);
        data = NULL;
        date_time = NULL;
        duration = 0;
        partial_duration = 0;
        g_free (title);
        title = NULL;
        discontinuity = FALSE;
        size = offset = -1;
        is_gap = FALSE;
        if (partial_segments != NULL) {
          g_ptr_array_free (partial_segments, TRUE);
          partial_segments = NULL;
        }
        goto next_line;
      }
      if (data != NULL) {
        GstM3U8MediaSegment *file;
        /* We can finally create the segment */
        /* The discontinuity sequence number is only stored if the header has
         * EXT-X-DISCONTINUITY-SEQUENCE present.  */
        file =
            gst_m3u8_media_segment_new (data, title, duration, mediasequence++,
            dsn, size, offset);
        self->duration += duration;

        file->is_gap = is_gap;

        /* set encryption params */
        if (current_key != NULL) {
          file->key = g_strdup (current_key);
          if (have_iv) {
            memcpy (file->iv, iv, sizeof (iv));
          } else {
            /* An EXT-X-KEY tag with a KEYFORMAT of "identity" that does
             * not have an IV attribute indicates that the Media Sequence
             * Number is to be used as the IV when decrypting a Media
             * Segment, by putting its big-endian binary representation
             * into a 16-octet (128-bit) buffer and padding (on the left)
             * with zeros. */
            guint8 *iv = file->iv + 12;
            GST_WRITE_UINT32_BE (iv, file->sequence);
          }
        }

        file->datetime = date_time;
        file->discont = discontinuity;
        if (last_init_file)
          file->init_file = gst_m3u8_init_file_ref (last_init_file);

        file->partial_segments = partial_segments;
        partial_segments = NULL;

        date_time = NULL;       /* Ownership was passed to the segment */
        duration = 0;
        partial_duration = 0;
        title = NULL;           /* Ownership was passed to the segment */
        discontinuity = FALSE;
        size = offset = -1;
        g_ptr_array_add (self->segments, file);
        previous = file;
      }

    } else if (g_str_has_prefix (data, "#EXTINF:")) {
      if (!time_from_double_in_string (data + 8, &data, &duration)) {
        GST_WARNING ("Can't read EXTINF duration");
        goto next_line;
      }

      /* As of protocol version 6, targetduration is maximum segment duration
       * rounded to nearest integer seconds,so can be up to 0.5 seconds too low */
      if (self->targetduration > 0
          && duration > (self->targetduration + GST_SECOND / 2)) {
        GST_DEBUG ("EXTINF duration (%" GST_TIME_FORMAT ") > TARGETDURATION (%"
            GST_TIME_FORMAT ")", GST_TIME_ARGS (duration),
            GST_TIME_ARGS (self->targetduration));
      }
      if (!data || *data != ',')
        goto next_line;
      data = g_utf8_next_char (data);
      if (data != end) {
        g_free (title);
        title = g_strdup (data);
      }
    } else if (g_str_has_prefix (data, "#EXT-X-")) {
      gchar *data_ext_x = data + 7;

      /* All these entries start with #EXT-X- */
      if (g_str_has_prefix (data_ext_x, "ENDLIST")) {
        self->endlist = TRUE;
      } else if (g_str_has_prefix (data_ext_x, "VERSION:")) {
        if (int_from_string (data + 15, &data, &val))
          self->version = val;
      } else if (g_str_has_prefix (data_ext_x, "PLAYLIST-TYPE:")) {
        if (!g_strcmp0 (data + 21, "VOD"))
          self->type = GST_HLS_PLAYLIST_TYPE_VOD;
        else if (!g_strcmp0 (data + 21, "EVENT"))
          self->type = GST_HLS_PLAYLIST_TYPE_EVENT;
        else
          GST_WARNING ("Unknown playlist type '%s'", data + 21);
      } else if (g_str_has_prefix (data_ext_x, "TARGETDURATION:")) {
        if (int_from_string (data + 22, &data, &val))
          self->targetduration = val * GST_SECOND;
      } else if (g_str_has_prefix (data_ext_x, "MEDIA-SEQUENCE:")) {
        if (int_from_string (data + 22, &data, &val))
          self->media_sequence = mediasequence = val;
      } else if (g_str_has_prefix (data_ext_x, "DISCONTINUITY-SEQUENCE:")) {
        if (int_from_string (data + 30, &data, &val)
            && val != self->discont_sequence) {
          dsn = self->discont_sequence = val;
          self->has_ext_x_dsn = TRUE;
        }
      } else if (g_str_has_prefix (data_ext_x, "DISCONTINUITY")) {
        dsn++;
        discontinuity = TRUE;
      } else if (g_str_has_prefix (data_ext_x, "PROGRAM-DATE-TIME:")) {
        date_time = g_date_time_new_from_iso8601 (data + 25, NULL);
        if (date_time)
          self->ext_x_pdt_present = TRUE;
      } else if (g_str_has_prefix (data_ext_x, "ALLOW-CACHE:")) {
        self->allowcache = g_ascii_strcasecmp (data + 19, "YES") == 0;
      } else if (g_str_has_prefix (data_ext_x, "KEY:")) {
        gchar *v, *a;

        data = data + 11;

        /* IV and KEY are only valid until the next #EXT-X-KEY */
        have_iv = FALSE;
        g_free (current_key);
        current_key = NULL;
        while (data && parse_attributes (&data, &a, &v)) {
          if (g_str_equal (a, "URI")) {
            current_key =
                uri_join (self->base_uri ? self->base_uri : self->uri, v);
          } else if (g_str_equal (a, "IV")) {
            gchar *ivp = v;
            gint i;

            if (strlen (ivp) < 32 + 2 || (!g_str_has_prefix (ivp, "0x")
                    && !g_str_has_prefix (ivp, "0X"))) {
              GST_WARNING ("Can't read IV");
              continue;
            }

            ivp += 2;
            for (i = 0; i < 16; i++) {
              gint h, l;

              h = g_ascii_xdigit_value (*ivp);
              ivp++;
              l = g_ascii_xdigit_value (*ivp);
              ivp++;
              if (h == -1 || l == -1) {
                i = -1;
                break;
              }
              iv[i] = (h << 4) | l;
            }

            if (i == -1) {
              GST_WARNING ("Can't read IV");
              continue;
            }
            have_iv = TRUE;
          } else if (g_str_equal (a, "METHOD")) {
            if (!g_str_equal (v, "AES-128") && !g_str_equal (v, "NONE")) {
              GST_WARNING ("Encryption method %s not supported", v);
              continue;
            }
            self->ext_x_key_present = TRUE;
          }
        }
      } else if (g_str_has_prefix (data_ext_x, "BYTERANGE:")) {
        gchar *v = data + 17;

        size = -1;
        offset = -1;

        if (!int64_from_string (v, &v, &size))
          goto next_line;

        if (*v == '@' && !int64_from_string (v + 1, &v, &offset))
          goto next_line;

        /* Either there must be an offset, or there must
         * be a previous segment to calculate from */
        if (offset == -1) {
          if (previous == NULL)
            goto next_line;

          offset = previous->offset + previous->size;
        }
      } else if (g_str_has_prefix (data_ext_x, "MAP:")) {
        gchar *v, *a, *header_uri = NULL;

        data = data + 11;

        while (data != NULL && parse_attributes (&data, &a, &v)) {
          if (strcmp (a, "URI") == 0) {
            header_uri =
                uri_join (self->base_uri ? self->base_uri : self->uri, v);
          } else if (strcmp (a, "BYTERANGE") == 0) {
            if (!int64_from_string (v, &v, &size)) {
              g_free (header_uri);
              goto next_line;
            }
            if (*v == '@' && !int64_from_string (v + 1, &v, &offset)) {
              g_free (header_uri);
              goto next_line;
            }
          }
        }

        if (header_uri) {
          GstM3U8InitFile *init_file;
          init_file = gst_m3u8_init_file_new (header_uri, size, offset);

          if (last_init_file)
            gst_m3u8_init_file_unref (last_init_file);

          last_init_file = init_file;
        }
      } else if (g_str_has_prefix (data_ext_x, "GAP:")) {
        is_gap = TRUE;
      } else if (g_str_has_prefix (data_ext_x, "PART:")) {
        GstM3U8PartialSegment *part = NULL;

        part =
            gst_m3u8_parse_partial_segment (data + strlen ("#EXT-X-PART:"),
            self->base_uri ? self->base_uri : self->uri);
        if (part == NULL)
          goto next_line;

        if (partial_segments == NULL) {
          partial_segments = g_ptr_array_new_full (2,
              (GDestroyNotify) gst_m3u8_partial_segment_unref);
        }
        g_ptr_array_add (partial_segments, part);
        partial_duration += part->duration;

      } else if (g_str_has_prefix (data_ext_x, "PART-INF:")) {
        gchar *v, *a;

        data += strlen ("#EXT-X-PART-INF:");

        while (data != NULL && parse_attributes (&data, &a, &v)) {
          if (strcmp (a, "PART-TARGET") == 0) {
            if (!time_from_double_in_string (v, NULL,
                    &self->partial_targetduration)) {
              GST_WARNING ("Invalid PART-TARGET");
              goto next_line;
            }
          }
        }
      } else if (g_str_has_prefix (data_ext_x, "SERVER-CONTROL:")) {
        data += strlen ("#EXT-X-SERVER-CONTROL:");
        parse_server_control (self, data);
      } else if (g_str_has_prefix (data_ext_x, "PRELOAD-HINT:")) {
        GstM3U8PreloadHint *hint = NULL;

        hint =
            gst_m3u8_parse_preload_hint (data + strlen ("#EXT-X-PRELOAD-HINT:"),
            self->base_uri ? self->base_uri : self->uri);
        if (hint == NULL)
          goto next_line;

        if (self->preload_hints == NULL) {
          self->preload_hints = g_ptr_array_new_full (1,
              (GDestroyNotify) gst_m3u8_preload_hint_unref);
        }
        g_ptr_array_add (self->preload_hints, hint);
      } else {
        GST_LOG ("Ignored line: %s", data);
      }
    } else if (data[0]) {
      /* Log non-empty lines */
      GST_LOG ("Ignored line: `%s`", data);
    }

  next_line:
    if (!end)
      break;
    data = g_utf8_next_char (end);      /* skip \n */
  }

  /* If there are trailing partial segments at the end,
   * create a dummy segment to hold them */
  if (partial_segments != NULL) {
    GstM3U8MediaSegment *file;
    GST_DEBUG ("Creating dummy segment for trailing partial segments");

    /* The discontinuity sequence number is only stored if the header has
     * EXT-X-DISCONTINUITY-SEQUENCE present.  */
    file =
        gst_m3u8_media_segment_new (NULL, title, partial_duration,
        mediasequence++, dsn, size, offset);

    file->partial_only = TRUE;

    self->duration += partial_duration;

    file->is_gap = is_gap;

    /* set encryption params */
    if (current_key != NULL) {
      file->key = g_strdup (current_key);
      if (have_iv) {
        memcpy (file->iv, iv, sizeof (iv));
      } else {
        /* An EXT-X-KEY tag with a KEYFORMAT of "identity" that does
         * not have an IV attribute indicates that the Media Sequence
         * Number is to be used as the IV when decrypting a Media
         * Segment, by putting its big-endian binary representation
         * into a 16-octet (128-bit) buffer and padding (on the left)
         * with zeros. */
        guint8 *iv = file->iv + 12;
        GST_WRITE_UINT32_BE (iv, file->sequence);
      }
    }

    file->datetime = date_time;
    file->discont = discontinuity;
    if (last_init_file)
      file->init_file = gst_m3u8_init_file_ref (last_init_file);

    file->partial_segments = partial_segments;
    partial_segments = NULL;

    date_time = NULL;           /* Ownership was passed to the partial segment */
    duration = 0;
    partial_duration = 0;
    title = NULL;               /* Ownership was passed to the partial segment */
    discontinuity = FALSE;
    size = offset = -1;
    g_ptr_array_add (self->segments, file);
    previous = file;
  }

  /* Clean up date that wasn't freed / handed to a segment */
  g_free (current_key);
  current_key = NULL;
  if (date_time)
    g_date_time_unref (date_time);
  g_free (title);

  g_free (input_data);

  if (last_init_file)
    gst_m3u8_init_file_unref (last_init_file);

  if (self->segments->len == 0) {
    GST_ERROR ("Invalid media playlist, it does not contain any media files");
    gst_hls_media_playlist_unref (self);
    return NULL;
  }

  /* Now go over the parsed data to ensure MSN and/or PDT are set */
  if (self->ext_x_pdt_present)
    gst_hls_media_playlist_postprocess_pdt (self);

  /* If we are not live, the stream time can be directly applied */
  if (!GST_HLS_MEDIA_PLAYLIST_IS_LIVE (self)) {
    gint iter, len = self->segments->len;
    GstClockTimeDiff stream_time = 0;

    for (iter = 0; iter < len; iter++) {
      GstM3U8MediaSegment *segment = g_ptr_array_index (self->segments, iter);
      segment->stream_time = stream_time;
      gst_m3u8_media_segment_fill_partial_stream_times (segment);

      stream_time += segment->duration;
    }
  }

  gst_hls_media_playlist_dump (self);
  return self;
}

/* Returns TRUE if the m3u8 as the same data as playlist_data  */
gboolean
gst_hls_media_playlist_has_same_data (GstHLSMediaPlaylist * self,
    gchar * playlist_data)
{
  gboolean ret;

  GST_HLS_MEDIA_PLAYLIST_LOCK (self);

  ret = self->last_data && g_str_equal (self->last_data, playlist_data);

  GST_HLS_MEDIA_PLAYLIST_UNLOCK (self);

  return ret;
}

GstM3U8MediaSegment *
gst_hls_media_playlist_seek (GstHLSMediaPlaylist * playlist, gboolean forward,
    GstSeekFlags flags, GstClockTimeDiff ts)
{
  gboolean snap_nearest =
      (flags & GST_SEEK_FLAG_SNAP_NEAREST) == GST_SEEK_FLAG_SNAP_NEAREST;
  gboolean snap_after =
      (flags & GST_SEEK_FLAG_SNAP_AFTER) == GST_SEEK_FLAG_SNAP_AFTER;
  guint idx;
  GstM3U8MediaSegment *res = NULL;

  GST_DEBUG ("ts:%" GST_STIME_FORMAT " forward:%d playlist uri: %s",
      GST_STIME_ARGS (ts), forward, playlist->uri);

  for (idx = 0; idx < playlist->segments->len; idx++) {
    GstM3U8MediaSegment *cand = g_ptr_array_index (playlist->segments, idx);

    /* If only full segments was request, skip any segment that
     * only has EXT-X-PARTs attached */
    if (cand->partial_only && !(flags & GST_HLS_M3U8_SEEK_FLAG_ALLOW_PARTIAL))
      continue;

    if ((forward & snap_after) || snap_nearest) {
      if (cand->stream_time >= ts ||
          (snap_nearest && (ts - cand->stream_time < cand->duration / 2))) {
        res = cand;
        goto out;
      }
    } else if (!forward && snap_after) {
      GstClockTime next_pos = cand->stream_time + cand->duration;

      if (next_pos <= ts && ts < next_pos + cand->duration) {
        res = cand;
        goto out;
      }
    } else if ((cand->stream_time <= ts || idx == 0)
        && ts < cand->stream_time + cand->duration) {
      res = cand;
      goto out;
    }
  }

out:
  if (res) {
    GST_DEBUG ("Returning segment sn:%" G_GINT64_FORMAT " stream_time:%"
        GST_STIME_FORMAT " duration:%" GST_TIME_FORMAT, res->sequence,
        GST_STIME_ARGS (res->stream_time), GST_TIME_ARGS (res->duration));
    gst_m3u8_media_segment_ref (res);
  } else {
    GST_DEBUG ("Couldn't find a match");
  }

  return res;
}

/* Recalculate all segment DSN based on the DSN of the provided anchor segment
 * (which must belong to the playlist). */
static void
gst_hls_media_playlist_recalculate_dsn (GstHLSMediaPlaylist * playlist,
    GstM3U8MediaSegment * anchor)
{
  guint idx;
  gint iter;
  GstM3U8MediaSegment *cand, *prev;

  if (!g_ptr_array_find (playlist->segments, anchor, &idx)) {
    g_assert (FALSE);
  }

  g_assert (idx != -1);

  GST_DEBUG ("Re-calculating DSN from segment #%d %" G_GINT64_FORMAT,
      idx, anchor->discont_sequence);

  /* Forward */
  prev = anchor;
  for (iter = idx + 1; iter < playlist->segments->len; iter++) {
    cand = g_ptr_array_index (playlist->segments, iter);
    if (cand->discont)
      cand->discont_sequence = prev->discont_sequence + 1;
    else
      cand->discont_sequence = prev->discont_sequence;
    prev = cand;
  }

  /* Backward */
  prev = anchor;
  for (iter = idx - 1; iter >= 0; iter--) {
    cand = g_ptr_array_index (playlist->segments, iter);
    if (prev->discont)
      cand->discont_sequence = prev->discont_sequence - 1;
    else
      cand->discont_sequence = prev->discont_sequence;
    prev = cand;
  }
}


static void
gst_m3u8_media_segment_fill_partial_stream_times (GstM3U8MediaSegment * segment)
{
  guint idx;
  GstClockTimeDiff stream_time = segment->stream_time;

  if (segment->partial_segments == NULL)
    return;

  for (idx = 0; idx < segment->partial_segments->len; idx++) {
    GstM3U8PartialSegment *part =
        g_ptr_array_index (segment->partial_segments, idx);

    part->stream_time = stream_time;
    stream_time += part->duration;
  }
}

/* Recalculate all segment stream time based on the stream time of the provided
 * anchor segment (which must belong to the playlist) */
void
gst_hls_media_playlist_recalculate_stream_time (GstHLSMediaPlaylist * playlist,
    GstM3U8MediaSegment * anchor)
{
  guint idx;
  gint iter;
  GstM3U8MediaSegment *cand, *prev;

  if (!g_ptr_array_find (playlist->segments, anchor, &idx)) {
    g_assert (FALSE);
  }

  g_assert (GST_CLOCK_TIME_IS_VALID (anchor->stream_time));
  g_assert (idx != -1);

  GST_DEBUG ("Re-calculating stream times from segment #%d %" GST_TIME_FORMAT,
      idx, GST_TIME_ARGS (anchor->stream_time));

  /* Forward */
  prev = anchor;
  for (iter = idx + 1; iter < playlist->segments->len; iter++) {
    cand = g_ptr_array_index (playlist->segments, iter);
    cand->stream_time = prev->stream_time + prev->duration;
    GST_DEBUG ("Forward iter %d %" GST_STIME_FORMAT, iter,
        GST_STIME_ARGS (cand->stream_time));
    gst_m3u8_media_segment_fill_partial_stream_times (cand);
    prev = cand;
  }

  /* Backward */
  prev = anchor;
  for (iter = idx - 1; iter >= 0; iter--) {
    cand = g_ptr_array_index (playlist->segments, iter);
    cand->stream_time = prev->stream_time - cand->duration;
    GST_DEBUG ("Backward iter %d %" GST_STIME_FORMAT, iter,
        GST_STIME_ARGS (cand->stream_time));
    gst_m3u8_media_segment_fill_partial_stream_times (cand);
    prev = cand;
  }
}

/* If a segment with the same URI, size, offset, SN and DSN is present in the
 * playlist, returns that one */
static GstM3U8MediaSegment *
gst_hls_media_playlist_find_by_uri (GstHLSMediaPlaylist * playlist,
    GstM3U8MediaSegment * segment)
{
  guint idx;

  for (idx = 0; idx < playlist->segments->len; idx++) {
    GstM3U8MediaSegment *cand = g_ptr_array_index (playlist->segments, idx);

    if (cand->sequence == segment->sequence &&
        cand->discont_sequence == segment->discont_sequence &&
        cand->offset == segment->offset && cand->size == segment->size &&
        !g_strcmp0 (cand->uri, segment->uri)) {
      return cand;
    }
  }

  return NULL;
}

/* Find the equivalent segment in the given playlist.
 *
 * The returned segment does *NOT* have increased reference !
 *
 * If the provided segment is just before the first entry of the playlist, it
 * will be added to the playlist (with a reference) and is_before will be set to
 * TRUE.
 */
static GstM3U8MediaSegment *
find_segment_in_playlist (GstHLSMediaPlaylist * playlist,
    GstM3U8MediaSegment * segment, gboolean * is_before, gboolean * matched_pdt)
{
  GstM3U8MediaSegment *res = NULL;
  guint idx;

  *is_before = FALSE;
  *matched_pdt = FALSE;

  /* The easy one. Happens when stream times need to be re-synced in an existing
   * playlist */
  if (g_ptr_array_find (playlist->segments, segment, NULL)) {
    GST_DEBUG ("Present as-is in playlist");
    return segment;
  }

  /* If there is an identical segment with the same URI and SN, use that one */
  res = gst_hls_media_playlist_find_by_uri (playlist, segment);
  if (res) {
    GST_DEBUG ("Using same URI/DSN/SN match");
    return res;
  }

  /* Try with PDT */
  if (segment->datetime && playlist->ext_x_pdt_present) {
#ifndef GST_DISABLE_GST_DEBUG
    gchar *pdtstring = g_date_time_format_iso8601 (segment->datetime);
    GST_DEBUG ("Search by datetime for %s", pdtstring);
    g_free (pdtstring);
#endif
    for (idx = 0; idx < playlist->segments->len; idx++) {
      GstM3U8MediaSegment *cand = g_ptr_array_index (playlist->segments, idx);

      if (idx == 0 && cand->datetime) {
        /* Special case for segments which are just before the 1st one (within
         * 20ms). We add another reference because it now also belongs to the
         * current playlist */
        GDateTime *seg_end = g_date_time_add (segment->datetime,
            segment->duration / GST_USECOND);
        GstClockTimeDiff ddiff =
            g_date_time_difference (cand->datetime, seg_end) * GST_USECOND;
        g_date_time_unref (seg_end);
        if (ABS (ddiff) < 20 * GST_MSECOND) {
          /* The reference segment ends within 20ms of the first segment, it is just before */
          GST_DEBUG ("Reference segment ends within %" GST_STIME_FORMAT
              " of first playlist segment, inserting before",
              GST_STIME_ARGS (ddiff));
          g_ptr_array_insert (playlist->segments, 0,
              gst_m3u8_media_segment_ref (segment));
          *is_before = TRUE;
          *matched_pdt = TRUE;
          return segment;
        }
        if (ddiff > 0) {
          /* If the reference segment is completely before the first segment, bail out */
          GST_DEBUG ("Reference segment ends before first segment");
          break;
        }
      }

      if (cand->datetime
          && g_date_time_difference (cand->datetime, segment->datetime) >= 0) {
        GST_DEBUG ("Picking by date time");
        *matched_pdt = TRUE;
        return cand;
      }
    }
  }

  /* If not live, we can match by stream time */
  if (!GST_HLS_MEDIA_PLAYLIST_IS_LIVE (playlist)) {
    GST_DEBUG ("Search by Stream time for %" GST_STIME_FORMAT " duration:%"
        GST_TIME_FORMAT, GST_STIME_ARGS (segment->stream_time),
        GST_TIME_ARGS (segment->duration));
    for (idx = 0; idx < playlist->segments->len; idx++) {
      GstM3U8MediaSegment *cand = g_ptr_array_index (playlist->segments, idx);

      /* If the candidate starts at or after the previous stream time */
      if (cand->stream_time >= segment->stream_time) {
        return cand;
      }

      /* If the previous end stream time is before the candidate end stream time */
      if ((segment->stream_time + segment->duration) <
          (cand->stream_time + cand->duration)) {
        return cand;
      }
    }
  }

  /* Fallback with MSN */
  GST_DEBUG ("Search by Media Sequence Number for sn:%" G_GINT64_FORMAT " dsn:%"
      G_GINT64_FORMAT, segment->sequence, segment->discont_sequence);
  for (idx = 0; idx < playlist->segments->len; idx++) {
    GstM3U8MediaSegment *cand = g_ptr_array_index (playlist->segments, idx);

    /* Ignore non-matching DSN if needed */
    if ((segment->discont_sequence != cand->discont_sequence)
        && playlist->has_ext_x_dsn)
      continue;

    if (idx == 0 && cand->sequence == segment->sequence + 1) {
      /* Special case for segments just before the 1st one. We add another
       * reference because it now also belongs to the current playlist */
      GST_DEBUG ("reference segment is just before 1st segment, inserting");
      g_ptr_array_insert (playlist->segments, 0,
          gst_m3u8_media_segment_ref (segment));
      *is_before = TRUE;
      return segment;
    }

    if (cand->sequence == segment->sequence) {
      return cand;
    }
  }

  return NULL;
}

/* Given a media segment (potentially from another media playlist), find the
 * equivalent media segment in this playlist.
 *
 * This will also recalculate all stream times based on that segment stream
 * time (i.e. "sync" the playlist to that previous time).
 *
 * If an equivalent/identical one is found it is returned with
 * the reference count incremented
 */
GstM3U8MediaSegment *
gst_hls_media_playlist_sync_to_segment (GstHLSMediaPlaylist * playlist,
    GstM3U8MediaSegment * segment)
{
  GstM3U8MediaSegment *res = NULL;
  gboolean is_before;
#ifndef GST_DISABLE_GST_DEBUG
  gchar *pdtstring;
#endif

  g_return_val_if_fail (playlist, NULL);
  g_return_val_if_fail (segment, NULL);

  GST_DEBUG ("Re-syncing to segment %" GST_STIME_FORMAT " duration:%"
      GST_TIME_FORMAT " sn:%" G_GINT64_FORMAT "/dsn:%" G_GINT64_FORMAT
      " uri:%s in playlist %s", GST_STIME_ARGS (segment->stream_time),
      GST_TIME_ARGS (segment->duration), segment->sequence,
      segment->discont_sequence, segment->uri, playlist->uri);

  gboolean matched_pdt = FALSE;
  res = find_segment_in_playlist (playlist, segment, &is_before, &matched_pdt);

  /* For live playlists we re-calculate all stream times based on the existing
   * stream time. Non-live playlists have their stream time calculated at
   * parsing time. */
  if (res) {
    if (!is_before)
      gst_m3u8_media_segment_ref (res);
    if (res->stream_time == GST_CLOCK_STIME_NONE) {
      GstClockTimeDiff stream_time_offset = 0;
      /* If there is a PDT on both segments, adjust the stream time
       * by the difference to align them precisely (hopefully).
       */
      if (matched_pdt) {
        /* If matched_pdt is TRUE, there must be PDT present in both segments */
        g_assert (res->datetime);
        g_assert (segment->datetime);

        stream_time_offset =
            g_date_time_difference (res->datetime,
            segment->datetime) * GST_USECOND;

        GST_DEBUG ("Transferring stream time %" GST_STIMEP_FORMAT
            " adjusted by PDT offset %" GST_STIMEP_FORMAT,
            &segment->stream_time, &stream_time_offset);
      }
      res->stream_time = segment->stream_time + stream_time_offset;
      gst_m3u8_media_segment_fill_partial_stream_times (res);
    }
    if (GST_HLS_MEDIA_PLAYLIST_IS_LIVE (playlist))
      gst_hls_media_playlist_recalculate_stream_time (playlist, res);
    /* If the playlist didn't specify a reference discont sequence number, we
     * carry over the one from the reference segment */
    if (!playlist->has_ext_x_dsn
        && res->discont_sequence != segment->discont_sequence) {
      res->discont_sequence = segment->discont_sequence;
      gst_hls_media_playlist_recalculate_dsn (playlist, res);
    }
    if (is_before) {
      GST_DEBUG ("Dropping segment from before the playlist");
      g_ptr_array_remove_index (playlist->segments, 0);
      res = NULL;
    }
  }
#ifndef GST_DISABLE_GST_DEBUG
  if (res) {
    pdtstring =
        res->datetime ? g_date_time_format_iso8601 (res->datetime) : NULL;
    GST_DEBUG ("Returning segment sn:%" G_GINT64_FORMAT " dsn:%" G_GINT64_FORMAT
        " stream_time:%" GST_STIME_FORMAT " duration:%" GST_TIME_FORMAT
        " datetime:%s", res->sequence, res->discont_sequence,
        GST_STIME_ARGS (res->stream_time), GST_TIME_ARGS (res->duration),
        pdtstring);
    g_free (pdtstring);
  } else {
    GST_DEBUG ("Could not find a match");
  }
#endif

  return res;
}

GstM3U8MediaSegment *
gst_hls_media_playlist_get_starting_segment (GstHLSMediaPlaylist * self)
{
  GstM3U8MediaSegment *res;

  GST_DEBUG ("playlist %s", self->uri);

  if (!GST_HLS_MEDIA_PLAYLIST_IS_LIVE (self)) {
    /* For non-live, we just grab the first one */
    res = g_ptr_array_index (self->segments, 0);
  } else {
    /* Live playlist */
    res =
        g_ptr_array_index (self->segments,
        MAX ((gint) self->segments->len - GST_M3U8_LIVE_MIN_FRAGMENT_DISTANCE -
            1, 0));
  }

  if (res) {
    GST_DEBUG ("Using segment sn:%" G_GINT64_FORMAT " dsn:%" G_GINT64_FORMAT,
        res->sequence, res->discont_sequence);
    gst_m3u8_media_segment_ref (res);
  }

  return res;
}

/* Calls this to carry over stream time, DSN, ... from one playlist to another.
 *
 * This should be used when a reference media segment couldn't be matched in the
 * playlist, but we still want to carry over the information from a reference
 * playlist to an updated one. This can happen with live playlists where the
 * reference media segment is no longer present but the playlists intersect */
gboolean
gst_hls_media_playlist_sync_to_playlist (GstHLSMediaPlaylist * playlist,
    GstHLSMediaPlaylist * reference)
{
  GstM3U8MediaSegment *res = NULL;
  GstM3U8MediaSegment *cand = NULL;
  guint idx;
  gboolean is_before;
  gboolean matched_pdt = FALSE;

  g_return_val_if_fail (playlist && reference, FALSE);

retry_without_dsn:
  /* The new playlist is supposed to be an update of the reference playlist,
   * therefore we will try from the last segment of the reference playlist and
   * go backwards */
  for (idx = reference->segments->len - 1; idx; idx--) {
    cand = g_ptr_array_index (reference->segments, idx);
    res = find_segment_in_playlist (playlist, cand, &is_before, &matched_pdt);
    if (res)
      break;
  }

  if (res == NULL) {
    if (playlist->has_ext_x_dsn) {
      /* There is a possibility that the server doesn't have coherent DSN
       * across variants/renditions. If we reach this section, this means that
       * we have already attempted matching by PDT, URI, stream time. The last
       * matching would have been by MSN/DSN, therefore try it again without
       * taking DSN into account. */
      GST_DEBUG ("Retrying matching without taking DSN into account");
      playlist->has_ext_x_dsn = FALSE;
      goto retry_without_dsn;
    }
    GST_WARNING ("Could not synchronize media playlists");
    return FALSE;
  }

  /* Carry over reference stream time */
  if (res->stream_time == GST_CLOCK_STIME_NONE) {
    GstClockTimeDiff stream_time_offset = 0;
    /* If there is a PDT on both segments, adjust the stream time
     * by the difference to align them precisely (hopefully).
     */
    if (matched_pdt) {
      /* If matched_pdt is TRUE, there must be PDT present in both segments */
      g_assert (playlist->ext_x_pdt_present && res->datetime);
      g_assert (reference->ext_x_pdt_present && cand->datetime);

      stream_time_offset =
          g_date_time_difference (res->datetime, cand->datetime) * GST_USECOND;
      GST_DEBUG ("Transferring stream time %" GST_STIMEP_FORMAT
          " adjusted by PDT offset %" GST_STIMEP_FORMAT, &cand->stream_time,
          &stream_time_offset);

    }
    res->stream_time = cand->stream_time + stream_time_offset;
    gst_m3u8_media_segment_fill_partial_stream_times (res);
  }

  if (GST_HLS_MEDIA_PLAYLIST_IS_LIVE (playlist))
    gst_hls_media_playlist_recalculate_stream_time (playlist, res);
  /* If the playlist didn't specify a reference discont sequence number, we
   * carry over the one from the reference segment */
  if (!playlist->has_ext_x_dsn
      && res->discont_sequence != cand->discont_sequence) {
    res->discont_sequence = cand->discont_sequence;
    gst_hls_media_playlist_recalculate_dsn (playlist, res);
  }
  if (is_before) {
    g_ptr_array_remove_index (playlist->segments, 0);
  }

  return TRUE;
}

gboolean
gst_hls_media_playlist_has_next_fragment (GstHLSMediaPlaylist * m3u8,
    GstM3U8MediaSegment * current, gboolean forward)
{
  guint idx;
  gboolean have_next = TRUE;

  g_return_val_if_fail (m3u8 != NULL, FALSE);
  g_return_val_if_fail (current != NULL, FALSE);

  GST_DEBUG ("playlist %s", m3u8->uri);

  GST_HLS_MEDIA_PLAYLIST_LOCK (m3u8);

  if (!g_ptr_array_find (m3u8->segments, current, &idx))
    have_next = FALSE;
  else if (idx == 0 && !forward)
    have_next = FALSE;
  else if (forward && idx == (m3u8->segments->len - 1))
    have_next = FALSE;

  GST_HLS_MEDIA_PLAYLIST_UNLOCK (m3u8);

  GST_DEBUG ("Returning %d", have_next);

  return have_next;
}


GstM3U8MediaSegment *
gst_hls_media_playlist_advance_fragment (GstHLSMediaPlaylist * m3u8,
    GstM3U8MediaSegment * current, gboolean forward,
    gboolean allow_partial_only_segment)
{
  GstM3U8MediaSegment *file = NULL;
  guint idx;

  g_return_val_if_fail (m3u8 != NULL, NULL);
  g_return_val_if_fail (current != NULL, NULL);

  GST_HLS_MEDIA_PLAYLIST_LOCK (m3u8);

  GST_DEBUG ("playlist %s", m3u8->uri);

  if (m3u8->segments->len < 2) {
    GST_DEBUG ("Playlist only contains one fragment, can't advance");
    goto out;
  }

  if (!g_ptr_array_find (m3u8->segments, current, &idx)) {
    GST_ERROR ("Requested to advance froma fragment not present in playlist");
    goto out;
  }

  if (forward && idx < (m3u8->segments->len - 1)) {
    file =
        gst_m3u8_media_segment_ref (g_ptr_array_index (m3u8->segments,
            idx + 1));
  } else if (!forward && idx > 0) {
    file =
        gst_m3u8_media_segment_ref (g_ptr_array_index (m3u8->segments,
            idx - 1));
  }

  if (file && file->partial_only && !allow_partial_only_segment) {
    GST_LOG
        ("Ignoring segment with only partials as full segment was requested");
    file = NULL;
  }

  if (file)
    GST_DEBUG ("Advanced to segment sn:%" G_GINT64_FORMAT " dsn:%"
        G_GINT64_FORMAT, file->sequence, file->discont_sequence);
  else
    GST_DEBUG ("Could not find %s fragment", forward ? "next" : "previous");

out:
  GST_HLS_MEDIA_PLAYLIST_UNLOCK (m3u8);

  return file;
}

GstClockTime
gst_hls_media_playlist_get_duration (GstHLSMediaPlaylist * m3u8)
{
  GstClockTime duration = GST_CLOCK_TIME_NONE;

  g_return_val_if_fail (m3u8 != NULL, GST_CLOCK_TIME_NONE);

  GST_DEBUG ("playlist %s", m3u8->uri);

  GST_HLS_MEDIA_PLAYLIST_LOCK (m3u8);
  /* We can only get the duration for on-demand streams */
  if (m3u8->endlist) {
    if (m3u8->segments->len) {
      GstM3U8MediaSegment *first = g_ptr_array_index (m3u8->segments, 0);
      GstM3U8MediaSegment *last =
          g_ptr_array_index (m3u8->segments, m3u8->segments->len - 1);
      duration = last->stream_time + last->duration - first->stream_time;
      if (duration != m3u8->duration)
        GST_ERROR ("difference in calculated duration ? %" GST_TIME_FORMAT
            " vs %" GST_TIME_FORMAT, GST_TIME_ARGS (duration),
            GST_TIME_ARGS (m3u8->duration));
    }
    duration = m3u8->duration;
  }
  GST_HLS_MEDIA_PLAYLIST_UNLOCK (m3u8);

  GST_DEBUG ("duration %" GST_TIME_FORMAT, GST_TIME_ARGS (duration));

  return duration;
}

gchar *
gst_hls_media_playlist_get_uri (GstHLSMediaPlaylist * m3u8)
{
  gchar *uri;

  GST_HLS_MEDIA_PLAYLIST_LOCK (m3u8);
  uri = g_strdup (m3u8->uri);
  GST_HLS_MEDIA_PLAYLIST_UNLOCK (m3u8);

  return uri;
}

gboolean
gst_hls_media_playlist_is_live (GstHLSMediaPlaylist * m3u8)
{
  gboolean is_live;

  g_return_val_if_fail (m3u8 != NULL, FALSE);

  GST_HLS_MEDIA_PLAYLIST_LOCK (m3u8);
  is_live = GST_HLS_MEDIA_PLAYLIST_IS_LIVE (m3u8);
  GST_HLS_MEDIA_PLAYLIST_UNLOCK (m3u8);

  return is_live;
}

static gchar *
uri_join (const gchar * uri1, const gchar * uri2)
{
  gchar *uri_copy, *tmp, *ret = NULL;

  if (gst_uri_is_valid (uri2))
    return g_strdup (uri2);

  uri_copy = g_strdup (uri1);
  if (uri2[0] != '/') {
    /* uri2 is a relative uri2 */
    /* look for query params */
    tmp = g_utf8_strchr (uri_copy, -1, '?');
    if (tmp) {
      /* find last / char, ignoring query params */
      tmp = g_utf8_strrchr (uri_copy, tmp - uri_copy, '/');
    } else {
      /* find last / char in URL */
      tmp = g_utf8_strrchr (uri_copy, -1, '/');
    }
    if (!tmp)
      goto out;


    *tmp = '\0';
    ret = g_strdup_printf ("%s/%s", uri_copy, uri2);
  } else {
    /* uri2 is an absolute uri2 */
    char *scheme, *hostname;

    scheme = uri_copy;
    /* find the : in <scheme>:// */
    tmp = g_utf8_strchr (uri_copy, -1, ':');
    if (!tmp)
      goto out;

    *tmp = '\0';

    /* skip :// */
    hostname = tmp + 3;

    tmp = g_utf8_strchr (hostname, -1, '/');
    if (tmp)
      *tmp = '\0';

    ret = g_strdup_printf ("%s://%s%s", scheme, hostname, uri2);
  }

out:
  g_free (uri_copy);
  if (!ret)
    GST_WARNING ("Can't build a valid uri from '%s' '%s'", uri1, uri2);

  return ret;
}

gboolean
gst_hls_media_playlist_has_lost_sync (GstHLSMediaPlaylist * m3u8,
    GstClockTime position)
{
  GstM3U8MediaSegment *first;

  if (m3u8->segments->len < 1)
    return TRUE;
  first = g_ptr_array_index (m3u8->segments, 0);

  GST_DEBUG ("position %" GST_TIME_FORMAT " first %" GST_STIME_FORMAT
      " duration %" GST_STIME_FORMAT, GST_TIME_ARGS (position),
      GST_STIME_ARGS (first->stream_time), GST_STIME_ARGS (first->duration));

  if (first->stream_time <= 0)
    return FALSE;

  /* If we're definitely before the first fragment, we lost sync */
  if ((position + (first->duration / 2)) < first->stream_time)
    return TRUE;
  return FALSE;
}

gboolean
gst_hls_media_playlist_get_seek_range (GstHLSMediaPlaylist * m3u8,
    gint64 * start, gint64 * stop)
{
  GstM3U8MediaSegment *first, *last;
  guint min_distance = 1;

  g_return_val_if_fail (m3u8 != NULL, FALSE);

  if (m3u8->segments->len < 1)
    return FALSE;

  first = g_ptr_array_index (m3u8->segments, 0);
  *start = first->stream_time;

  if (GST_HLS_MEDIA_PLAYLIST_IS_LIVE (m3u8) && m3u8->segments->len > 1) {
    /* min_distance is used to make sure the seek range is never closer than
       GST_M3U8_LIVE_MIN_FRAGMENT_DISTANCE fragments from the end of a live
       playlist - see 6.3.3. "Playing the Playlist file" of the HLS draft */
    min_distance =
        MIN (GST_M3U8_LIVE_MIN_FRAGMENT_DISTANCE, m3u8->segments->len - 1);
  }

  last = g_ptr_array_index (m3u8->segments, m3u8->segments->len - min_distance);
  *stop = last->stream_time + last->duration;

  return TRUE;
}

GstClockTime
gst_hls_media_playlist_recommended_buffering_threshold (GstHLSMediaPlaylist *
    playlist)
{
  if (!playlist->duration || !GST_CLOCK_TIME_IS_VALID (playlist->duration)
      || playlist->segments->len == 0)
    return GST_CLOCK_TIME_NONE;

  /* The recommended buffering threshold is 1.5 average segment duration */
  return 3 * (playlist->duration / playlist->segments->len) / 2;
}

GstHLSRenditionStream *
gst_hls_rendition_stream_ref (GstHLSRenditionStream * media)
{
  g_assert (media != NULL && media->ref_count > 0);
  g_atomic_int_add (&media->ref_count, 1);
  return media;
}

void
gst_hls_rendition_stream_unref (GstHLSRenditionStream * media)
{
  g_assert (media != NULL && media->ref_count > 0);
  if (g_atomic_int_dec_and_test (&media->ref_count)) {
    if (media->caps)
      gst_caps_unref (media->caps);
    g_free (media->group_id);
    g_free (media->name);
    g_free (media->uri);
    g_free (media->lang);
    g_free (media);
  }
}

static GstHLSRenditionStreamType
gst_m3u8_get_hls_media_type_from_string (const gchar * type_name)
{
  if (strcmp (type_name, "AUDIO") == 0)
    return GST_HLS_RENDITION_STREAM_TYPE_AUDIO;
  if (strcmp (type_name, "VIDEO") == 0)
    return GST_HLS_RENDITION_STREAM_TYPE_VIDEO;
  if (strcmp (type_name, "SUBTITLES") == 0)
    return GST_HLS_RENDITION_STREAM_TYPE_SUBTITLES;
  if (strcmp (type_name, "CLOSED_CAPTIONS") == 0)
    return GST_HLS_RENDITION_STREAM_TYPE_CLOSED_CAPTIONS;

  return GST_HLS_RENDITION_STREAM_TYPE_INVALID;
}

#define GST_HLS_RENDITION_STREAM_TYPE_NAME(mtype) gst_hls_rendition_stream_type_get_name(mtype)
const gchar *
gst_hls_rendition_stream_type_get_name (GstHLSRenditionStreamType mtype)
{
  static const gchar *nicks[GST_HLS_N_MEDIA_TYPES] = { "audio", "video",
    "subtitle", "closed-captions"
  };

  if (mtype < 0 || mtype >= GST_HLS_N_MEDIA_TYPES)
    return "invalid";

  return nicks[mtype];
}

/* returns copy of string with surrounding quotation marks removed */
static gchar *
gst_m3u8_unquote (const gchar * str)
{
  const gchar *start, *end;

  start = strchr (str, '"');
  if (start == NULL)
    return g_strdup (str);
  end = strchr (start + 1, '"');
  if (end == NULL) {
    GST_WARNING ("Broken quoted string [%s] - can't find end quote", str);
    return g_strdup (start + 1);
  }
  return g_strndup (start + 1, (gsize) (end - (start + 1)));
}

static GstHLSRenditionStream *
gst_m3u8_parse_media (gchar * desc, const gchar * base_uri)
{
  GstHLSRenditionStream *media;
  gchar *a, *v;

  media = g_new0 (GstHLSRenditionStream, 1);
  media->ref_count = 1;
  media->mtype = GST_HLS_RENDITION_STREAM_TYPE_INVALID;

  GST_LOG ("parsing %s", desc);
  while (desc != NULL && parse_attributes (&desc, &a, &v)) {
    if (strcmp (a, "TYPE") == 0) {
      media->mtype = gst_m3u8_get_hls_media_type_from_string (v);
    } else if (strcmp (a, "GROUP-ID") == 0) {
      g_free (media->group_id);
      media->group_id = gst_m3u8_unquote (v);
    } else if (strcmp (a, "NAME") == 0) {
      g_free (media->name);
      media->name = gst_m3u8_unquote (v);
    } else if (strcmp (a, "URI") == 0) {
      gchar *uri;

      g_free (media->uri);
      uri = gst_m3u8_unquote (v);
      media->uri = uri_join (base_uri, uri);
      g_free (uri);
    } else if (strcmp (a, "LANGUAGE") == 0) {
      g_free (media->lang);
      media->lang = gst_m3u8_unquote (v);
    } else if (strcmp (a, "DEFAULT") == 0) {
      media->is_default = g_ascii_strcasecmp (v, "yes") == 0;
    } else if (strcmp (a, "FORCED") == 0) {
      media->forced = g_ascii_strcasecmp (v, "yes") == 0;
    } else if (strcmp (a, "AUTOSELECT") == 0) {
      media->autoselect = g_ascii_strcasecmp (v, "yes") == 0;
    } else {
      /* unhandled: ASSOC-LANGUAGE, INSTREAM-ID, CHARACTERISTICS,
       * STABLE-RENDITION-ID, CHANNELS */
      GST_FIXME ("EXT-X-MEDIA: unhandled attribute: %s = %s", a, v);
    }
  }

  if (media->mtype == GST_HLS_RENDITION_STREAM_TYPE_INVALID)
    goto required_attributes_missing;

  if (media->group_id == NULL || media->name == NULL)
    goto required_attributes_missing;

  if (media->mtype == GST_HLS_RENDITION_STREAM_TYPE_CLOSED_CAPTIONS)
    goto uri_with_cc;

  GST_DEBUG ("media: %s, group '%s', name '%s', uri '%s', %s %s %s, lang=%s",
      GST_HLS_RENDITION_STREAM_TYPE_NAME (media->mtype), media->group_id,
      media->name, media->uri, media->is_default ? "default" : "-",
      media->autoselect ? "autoselect" : "-", media->forced ? "forced" : "-",
      media->lang ? media->lang : "??");

  return media;

uri_with_cc:
  {
    GST_WARNING ("closed captions EXT-X-MEDIA should not have URI specified");
    goto out_error;
  }
required_attributes_missing:
  {
    GST_WARNING ("EXT-X-MEDIA description is missing required attributes");
    goto out_error;
  }

out_error:
  {
    gst_hls_rendition_stream_unref (media);
    return NULL;
  }
}

GstStreamType
gst_hls_get_stream_type_from_structure (GstStructure * st)
{
  const gchar *name = gst_structure_get_name (st);

  if (g_str_has_prefix (name, "audio/"))
    return GST_STREAM_TYPE_AUDIO;

  if (g_str_has_prefix (name, "video/"))
    return GST_STREAM_TYPE_VIDEO;

  if (g_str_has_prefix (name, "application/x-subtitle"))
    return GST_STREAM_TYPE_TEXT;

  return 0;
}

GstStreamType
gst_hls_get_stream_type_from_caps (GstCaps * caps)
{
  GstStreamType ret = 0;
  guint i, nb;
  nb = gst_caps_get_size (caps);
  for (i = 0; i < nb; i++) {
    GstStructure *cand = gst_caps_get_structure (caps, i);

    ret |= gst_hls_get_stream_type_from_structure (cand);
  }

  return ret;
}

static GstHLSVariantStream *
gst_hls_variant_stream_new (void)
{
  GstHLSVariantStream *stream;

  stream = g_new0 (GstHLSVariantStream, 1);
  stream->refcount = 1;
  stream->codecs_stream_type = 0;
  return stream;
}

GstHLSVariantStream *
hls_variant_stream_ref (GstHLSVariantStream * stream)
{
  g_atomic_int_inc (&stream->refcount);
  return stream;
}

void
hls_variant_stream_unref (GstHLSVariantStream * stream)
{
  if (g_atomic_int_dec_and_test (&stream->refcount)) {
    gint i;

    g_free (stream->name);
    g_free (stream->uri);
    g_free (stream->codecs);
    if (stream->caps)
      gst_caps_unref (stream->caps);
    for (i = 0; i < GST_HLS_N_MEDIA_TYPES; ++i) {
      g_free (stream->media_groups[i]);
    }
    g_list_free_full (stream->fallback, g_free);
    g_free (stream);
  }
}

static GstHLSVariantStream *
gst_hls_variant_parse (gchar * data, const gchar * base_uri)
{
  GstHLSVariantStream *stream;
  gchar *v, *a;

  stream = gst_hls_variant_stream_new ();
  stream->iframe = g_str_has_prefix (data, "#EXT-X-I-FRAME-STREAM-INF:");
  data += stream->iframe ? 26 : 18;

  while (data && parse_attributes (&data, &a, &v)) {
    if (g_str_equal (a, "BANDWIDTH")) {
      if (!stream->bandwidth) {
        if (!int_from_string (v, NULL, &stream->bandwidth))
          GST_WARNING ("Error while reading BANDWIDTH");
      }
    } else if (g_str_equal (a, "AVERAGE-BANDWIDTH")) {
      GST_DEBUG
          ("AVERAGE-BANDWIDTH attribute available. Using it as stream bandwidth");
      if (!int_from_string (v, NULL, &stream->bandwidth))
        GST_WARNING ("Error while reading AVERAGE-BANDWIDTH");
    } else if (g_str_equal (a, "PROGRAM-ID")) {
      if (!int_from_string (v, NULL, &stream->program_id))
        GST_WARNING ("Error while reading PROGRAM-ID");
    } else if (g_str_equal (a, "CODECS")) {
      g_free (stream->codecs);
      stream->codecs = g_strdup (v);
      stream->caps = gst_codec_utils_caps_from_mime_codec (stream->codecs);
      stream->codecs_stream_type =
          gst_hls_get_stream_type_from_caps (stream->caps);
    } else if (g_str_equal (a, "RESOLUTION")) {
      if (!int_from_string (v, &v, &stream->width))
        GST_WARNING ("Error while reading RESOLUTION width");
      if (!v || *v != 'x') {
        GST_WARNING ("Missing height");
      } else {
        v = g_utf8_next_char (v);
        if (!int_from_string (v, NULL, &stream->height))
          GST_WARNING ("Error while reading RESOLUTION height");
      }
    } else if (stream->iframe && g_str_equal (a, "URI")) {
      stream->uri = uri_join (base_uri, v);
    } else if (g_str_equal (a, "AUDIO")) {
      g_free (stream->media_groups[GST_HLS_RENDITION_STREAM_TYPE_AUDIO]);
      stream->media_groups[GST_HLS_RENDITION_STREAM_TYPE_AUDIO] =
          gst_m3u8_unquote (v);
    } else if (g_str_equal (a, "SUBTITLES")) {
      g_free (stream->media_groups[GST_HLS_RENDITION_STREAM_TYPE_SUBTITLES]);
      stream->media_groups[GST_HLS_RENDITION_STREAM_TYPE_SUBTITLES] =
          gst_m3u8_unquote (v);
    } else if (g_str_equal (a, "VIDEO")) {
      g_free (stream->media_groups[GST_HLS_RENDITION_STREAM_TYPE_VIDEO]);
      stream->media_groups[GST_HLS_RENDITION_STREAM_TYPE_VIDEO] =
          gst_m3u8_unquote (v);
    } else if (g_str_equal (a, "CLOSED-CAPTIONS")) {
      /* closed captions will be embedded inside the video stream, ignore */
    }
  }

  return stream;
}

static gchar *
generate_variant_stream_name (gchar * uri, gint bandwidth)
{
  gchar *checksum = g_compute_checksum_for_string (G_CHECKSUM_SHA1, uri, -1);
  gchar *res = g_strdup_printf ("variant-%dbps-%s", bandwidth, checksum);

  g_free (checksum);
  return res;
}

static GstHLSVariantStream *
find_variant_stream_by_name (GList * list, const gchar * name)
{
  for (; list != NULL; list = list->next) {
    GstHLSVariantStream *variant_stream = list->data;

    if (variant_stream->name != NULL && !strcmp (variant_stream->name, name))
      return variant_stream;
  }
  return NULL;
}

static GstHLSVariantStream *
find_variant_stream_by_uri (GList * list, const gchar * uri)
{
  for (; list != NULL; list = list->next) {
    GstHLSVariantStream *variant_stream = list->data;

    if (variant_stream->uri != NULL && !strcmp (variant_stream->uri, uri))
      return variant_stream;
  }
  return NULL;
}

static GstHLSVariantStream *
find_variant_stream_for_fallback (GList * list, GstHLSVariantStream * fallback)
{
  for (; list != NULL; list = list->next) {
    GstHLSVariantStream *variant_stream = list->data;

    if (variant_stream->bandwidth == fallback->bandwidth &&
        variant_stream->width == fallback->width &&
        variant_stream->height == fallback->height &&
        variant_stream->iframe == fallback->iframe &&
        !g_strcmp0 (variant_stream->codecs, fallback->codecs))
      return variant_stream;
  }
  return NULL;
}

static GstHLSMasterPlaylist *
gst_hls_master_playlist_new (void)
{
  GstHLSMasterPlaylist *playlist;

  playlist = g_new0 (GstHLSMasterPlaylist, 1);
  playlist->refcount = 1;
  playlist->is_simple = FALSE;

  return playlist;
}

void
hls_master_playlist_unref (GstHLSMasterPlaylist * playlist)
{
  if (g_atomic_int_dec_and_test (&playlist->refcount)) {
    g_list_free_full (playlist->renditions,
        (GDestroyNotify) gst_hls_rendition_stream_unref);
    g_list_free_full (playlist->variants,
        (GDestroyNotify) gst_hls_variant_stream_unref);
    g_list_free_full (playlist->iframe_variants,
        (GDestroyNotify) gst_hls_variant_stream_unref);
    if (playlist->default_variant)
      gst_hls_variant_stream_unref (playlist->default_variant);
    g_free (playlist->last_data);
    g_free (playlist);
  }
}

static gint
hls_media_compare_func (GstHLSRenditionStream * ma, GstHLSRenditionStream * mb)
{
  if (ma->mtype != mb->mtype)
    return ma->mtype - mb->mtype;

  return strcmp (ma->name, mb->name) || strcmp (ma->group_id, mb->group_id);
}

static GstCaps *
stream_get_media_caps (GstHLSVariantStream * stream,
    GstHLSRenditionStreamType mtype)
{
  GstStructure *st = NULL;
  GstCaps *ret;
  guint i, nb;

  if (stream->caps == NULL)
    return NULL;

  nb = gst_caps_get_size (stream->caps);
  for (i = 0; i < nb; i++) {
    GstStructure *cand = gst_caps_get_structure (stream->caps, i);
    const gchar *name = gst_structure_get_name (cand);
    gboolean matched;

    switch (mtype) {
      case GST_HLS_RENDITION_STREAM_TYPE_AUDIO:
        matched = g_str_has_prefix (name, "audio/");
        break;
      case GST_HLS_RENDITION_STREAM_TYPE_VIDEO:
        matched = g_str_has_prefix (name, "video/");
        break;
      case GST_HLS_RENDITION_STREAM_TYPE_SUBTITLES:
        matched = g_str_has_prefix (name, "application/x-subtitle");
        break;
      default:
        matched = FALSE;
        break;
    }

    if (!matched)
      continue;

    if (st) {
      GST_WARNING ("More than one caps for the same type, can't match");
      return NULL;
    }

    st = cand;
  }

  if (!st)
    return NULL;

  ret = gst_caps_new_empty ();
  gst_caps_append_structure (ret, gst_structure_copy (st));
  return ret;

}

static gint
gst_hls_variant_stream_compare_by_bitrate (gconstpointer a, gconstpointer b)
{
  const GstHLSVariantStream *vs_a = (const GstHLSVariantStream *) a;
  const GstHLSVariantStream *vs_b = (const GstHLSVariantStream *) b;

  if (vs_a->bandwidth == vs_b->bandwidth)
    return g_strcmp0 (vs_a->name, vs_b->name);

  return vs_a->bandwidth - vs_b->bandwidth;
}

/**
 * gst_hls_master_playlist_new_from_data:
 * @data: (transfer full): The manifest to parse
 * @base_uri: The URI of the manifest
 *
 * Parse the provided manifest and construct the master playlist.
 *
 * Returns: The parse GstHLSMasterPlaylist , or NULL if there was an error.
 */
GstHLSMasterPlaylist *
hls_master_playlist_new_from_data (gchar * data, const gchar * base_uri)
{
  GstHLSMasterPlaylist *playlist;
  GstHLSVariantStream *pending_stream, *existing_stream;
  gchar *end, *free_data = data;
  gint val;
  GList *tmp;
  GstStreamType most_seen_types = 0;

  if (!g_str_has_prefix (data, "#EXTM3U")) {
    GST_WARNING ("Data doesn't start with #EXTM3U");
    g_free (free_data);
    return NULL;
  }

  playlist = gst_hls_master_playlist_new ();

  /* store data before we modify it for parsing */
  playlist->last_data = g_strdup (data);

  GST_TRACE ("data:\n%s", data);

  /* Detect early whether this manifest describes a simple media playlist or
   * not */
  if (strstr (data, "\n#EXTINF:") != NULL) {
    GST_INFO ("This is a simple media playlist, not a master playlist");

    pending_stream = gst_hls_variant_stream_new ();
    pending_stream->name = g_strdup ("media-playlist");
    pending_stream->uri = g_strdup (base_uri);
    playlist->variants = g_list_append (playlist->variants, pending_stream);
    playlist->default_variant = gst_hls_variant_stream_ref (pending_stream);
    playlist->is_simple = TRUE;

    return playlist;
  }

  /* Beginning of the actual master playlist parsing */
  pending_stream = NULL;
  data += 7;
  while (TRUE) {
    gchar *r;

    end = g_utf8_strchr (data, -1, '\n');
    if (end)
      *end = '\0';

    r = g_utf8_strchr (data, -1, '\r');
    if (r)
      *r = '\0';

    if (data[0] != '#' && data[0] != '\0') {
      gchar *name, *uri;

      if (pending_stream == NULL) {
        GST_LOG ("%s: got non-empty line without EXT-STREAM-INF, dropping",
            data);
        goto next_line;
      }

      uri = uri_join (base_uri, data);
      if (uri == NULL)
        goto next_line;

      pending_stream->name = name =
          generate_variant_stream_name (uri, pending_stream->bandwidth);
      pending_stream->uri = uri;

      if (find_variant_stream_by_name (playlist->variants, name)
          || find_variant_stream_by_uri (playlist->variants, uri)) {
        GST_DEBUG ("Already have a list with this name or URI: %s", name);
        gst_hls_variant_stream_unref (pending_stream);
      } else if ((existing_stream =
              find_variant_stream_for_fallback (playlist->variants,
                  pending_stream))) {
        GST_DEBUG ("Adding to %s fallback URI %s", existing_stream->name,
            pending_stream->uri);
        existing_stream->fallback =
            g_list_append (existing_stream->fallback,
            g_strdup (pending_stream->uri));
        gst_hls_variant_stream_unref (pending_stream);
      } else {
        GST_INFO ("stream %s @ %u: %s", name, pending_stream->bandwidth, uri);
        playlist->variants = g_list_append (playlist->variants, pending_stream);
        /* use first stream in the playlist as default */
        if (playlist->default_variant == NULL) {
          playlist->default_variant =
              gst_hls_variant_stream_ref (pending_stream);
        }
      }
      pending_stream = NULL;
    } else if (g_str_has_prefix (data, "#EXT-X-VERSION:")) {
      if (int_from_string (data + 15, &data, &val))
        playlist->version = val;
    } else if (g_str_has_prefix (data, "#EXT-X-STREAM-INF:") ||
        g_str_has_prefix (data, "#EXT-X-I-FRAME-STREAM-INF:")) {
      GstHLSVariantStream *stream = gst_hls_variant_parse (data, base_uri);

      if (stream->iframe) {
        if (find_variant_stream_by_uri (playlist->iframe_variants, stream->uri)) {
          GST_DEBUG ("Already have a list with this URI");
          gst_hls_variant_stream_unref (stream);
        } else {
          playlist->iframe_variants =
              g_list_append (playlist->iframe_variants, stream);
        }
      } else {
        if (pending_stream != NULL) {
          GST_WARNING ("variant stream without uri, dropping");
          gst_hls_variant_stream_unref (pending_stream);
        }
        pending_stream = stream;
      }
    } else if (g_str_has_prefix (data, "#EXT-X-MEDIA:")) {
      GstHLSRenditionStream *media;

      media = gst_m3u8_parse_media (data + strlen ("#EXT-X-MEDIA:"), base_uri);

      if (media == NULL)
        goto next_line;

      if (g_list_find_custom (playlist->renditions, media,
              (GCompareFunc) hls_media_compare_func)) {
        GST_DEBUG ("Dropping duplicate alternate rendition group : %s", data);
        gst_hls_rendition_stream_unref (media);
        goto next_line;
      }
      playlist->renditions = g_list_append (playlist->renditions, media);
      GST_INFO ("Stored media %s / group %s", media->name, media->group_id);
    } else if (*data != '\0') {
      GST_LOG ("Ignored line: %s", data);
    }

  next_line:
    if (!end)
      break;
    data = g_utf8_next_char (end);      /* skip \n */
  }

  if (pending_stream != NULL) {
    GST_WARNING ("#EXT-X-STREAM-INF without uri, dropping");
    gst_hls_variant_stream_unref (pending_stream);
  }

  g_free (free_data);

  if (playlist->variants == NULL) {
    GST_WARNING ("Master playlist without any media playlists!");
    gst_hls_master_playlist_unref (playlist);
    return NULL;
  }

  /* reorder variants by bitrate */
  playlist->variants =
      g_list_sort (playlist->variants,
      (GCompareFunc) gst_hls_variant_stream_compare_by_bitrate);

  playlist->iframe_variants =
      g_list_sort (playlist->iframe_variants,
      (GCompareFunc) gst_hls_variant_stream_compare_by_bitrate);

#ifndef GST_DISABLE_GST_DEBUG
  /* Sanity check : If there are no codecs, a stream shouldn't point to
   * alternate rendition groups.
   *
   * Write a warning to help with further debugging if this causes issues
   * later */
  for (tmp = playlist->variants; tmp; tmp = tmp->next) {
    GstHLSVariantStream *stream = tmp->data;

    if (stream->codecs == NULL) {
      if (stream->media_groups[0] || stream->media_groups[1]
          || stream->media_groups[2] || stream->media_groups[3]) {
        GST_WARNING
            ("Variant specifies alternate rendition groups but has no codecs specified");
      }
    }
  }
#endif

  /* Filter out audio-only variants from audio+video stream */
  for (tmp = playlist->variants; tmp; tmp = tmp->next) {
    GstHLSVariantStream *stream = tmp->data;

    most_seen_types |= stream->codecs_stream_type;
  }

  /* Flag the playlist to indicate whether all codecs are known or not on variants */
  playlist->have_codecs = most_seen_types != 0;

  GST_DEBUG ("have_codecs:%d most_seen_types:%d", playlist->have_codecs,
      most_seen_types);

  /* Filter out audio-only variants from audio+video stream */
  if (playlist->have_codecs && most_seen_types != GST_STREAM_TYPE_AUDIO) {
    tmp = playlist->variants;
    while (tmp) {
      GstHLSVariantStream *stream = tmp->data;

      if (stream->codecs_stream_type != most_seen_types &&
          stream->codecs_stream_type == GST_STREAM_TYPE_AUDIO) {
        GST_DEBUG ("Remove variant with partial stream types %s", stream->name);
        tmp = playlist->variants = g_list_remove (playlist->variants, stream);
        gst_hls_variant_stream_unref (stream);
      } else
        tmp = tmp->next;
    }
  }

  if (playlist->renditions) {
    guint i;
    /* Assign information from variants to alternate rendition groups. Note that
     * at this point we know that there are caps present on the variants */
    for (tmp = playlist->variants; tmp; tmp = tmp->next) {
      GstHLSVariantStream *stream = tmp->data;

      GST_DEBUG ("Post-processing Variant Stream '%s'", stream->name);

      for (i = 0; i < GST_HLS_N_MEDIA_TYPES; ++i) {
        gchar *alt_rend_group = stream->media_groups[i];

        if (alt_rend_group) {
          gboolean alt_in_variant = FALSE;
          GstCaps *media_caps = stream_get_media_caps (stream, i);
          GList *altlist;
          if (!media_caps)
            continue;
          for (altlist = playlist->renditions; altlist; altlist = altlist->next) {
            GstHLSRenditionStream *media = altlist->data;
            if (media->mtype != i
                || g_strcmp0 (media->group_id, alt_rend_group))
              continue;
            GST_DEBUG ("  %s caps:%" GST_PTR_FORMAT " media %s, uri: %s",
                GST_HLS_RENDITION_STREAM_TYPE_NAME (i), media_caps, media->name,
                media->uri);
            if (media->uri == NULL) {
              GST_DEBUG ("  Media is present in main variant stream");
              alt_in_variant = TRUE;
            } else {
              /* Assign caps to media */
              if (media->caps && !gst_caps_is_equal (media->caps, media_caps)) {
                GST_ERROR ("  Media already has different caps %"
                    GST_PTR_FORMAT, media->caps);
              } else {
                GST_DEBUG ("  Assigning caps %" GST_PTR_FORMAT, media_caps);
                gst_caps_replace (&media->caps, media_caps);
              }
            }
          }
          if (!alt_in_variant) {
            GstCaps *new_caps = gst_caps_subtract (stream->caps, media_caps);
            gst_caps_replace (&stream->caps, new_caps);
            gst_caps_unref (new_caps);
          }
          gst_caps_unref (media_caps);
        }
      }
      GST_DEBUG ("Stream Ends up with caps %" GST_PTR_FORMAT, stream->caps);
    }
  }

  GST_DEBUG
      ("parsed master playlist with %d streams, %d I-frame streams and %d alternative rendition groups",
      g_list_length (playlist->variants),
      g_list_length (playlist->iframe_variants),
      g_list_length (playlist->renditions));


  return playlist;
}

GstHLSVariantStream *
hls_master_playlist_get_variant_for_bitrate (GstHLSMasterPlaylist *
    playlist, GstHLSVariantStream * current_variant, guint bitrate,
    guint min_bitrate)
{
  GstHLSVariantStream *variant = current_variant;
  GstHLSVariantStream *variant_by_min = current_variant;
  GList *l;

  /* variant lists are sorted low to high, so iterate from highest to lowest */
  if (current_variant == NULL || !current_variant->iframe)
    l = g_list_last (playlist->variants);
  else
    l = g_list_last (playlist->iframe_variants);

  while (l != NULL) {
    variant = l->data;
    if (variant->bandwidth >= min_bitrate)
      variant_by_min = variant;
    if (variant->bandwidth <= bitrate)
      break;
    l = l->prev;
  }

  /* If variant bitrate is above the min_bitrate (or min_bitrate == 0)
   * return it now */
  if (variant && variant->bandwidth >= min_bitrate)
    return variant;

  /* Otherwise, return the last (lowest bitrate) variant we saw that
   * was higher than the min_bitrate */
  return variant_by_min;
}

static gboolean
remove_uncommon (GQuark field_id, GValue * value, GstStructure * st2)
{
  const GValue *other;
  GValue dest = G_VALUE_INIT;

  other = gst_structure_id_get_value (st2, field_id);

  if (other == NULL || (G_VALUE_TYPE (value) != G_VALUE_TYPE (other)))
    return FALSE;

  if (!gst_value_intersect (&dest, value, other))
    return FALSE;

  g_value_reset (value);
  g_value_copy (&dest, value);
  g_value_reset (&dest);

  return TRUE;
}

/* Merge all common structures from caps1 and caps2
 *
 * Returns empty caps if a structure is not present in both */
static GstCaps *
gst_caps_merge_common (GstCaps * caps1, GstCaps * caps2)
{
  guint it1, it2;
  GstCaps *res = gst_caps_new_empty ();

  for (it1 = 0; it1 < gst_caps_get_size (caps1); it1++) {
    GstStructure *st1 = gst_caps_get_structure (caps1, it1);
    GstStructure *merged = NULL;
    const gchar *name1 = gst_structure_get_name (st1);

    for (it2 = 0; it2 < gst_caps_get_size (caps2); it2++) {
      GstStructure *st2 = gst_caps_get_structure (caps2, it2);
      if (gst_structure_has_name (st2, name1)) {
        if (merged == NULL)
          merged = gst_structure_copy (st1);
        gst_structure_filter_and_map_in_place (merged,
            (GstStructureFilterMapFunc) remove_uncommon, st2);
      }
    }

    if (merged == NULL)
      goto fail;
    gst_caps_append_structure (res, merged);
  }

  return res;

fail:
  {
    GST_WARNING ("Failed to create common caps of %"
        GST_PTR_FORMAT " and %" GST_PTR_FORMAT, caps1, caps2);
    gst_caps_unref (res);
    return NULL;
  }
}

GstCaps *
hls_master_playlist_get_common_caps (GstHLSMasterPlaylist * playlist)
{
  GList *tmp;
  GstCaps *res = NULL;

  for (tmp = playlist->variants; tmp; tmp = tmp->next) {
    GstHLSVariantStream *stream = tmp->data;

    GST_DEBUG ("stream caps %" GST_PTR_FORMAT, stream->caps);
    if (!stream->caps) {
      /* If one of the stream doesn't have *any* caps, we can't reliably return
       * any common caps */
      if (res)
        gst_caps_unref (res);
      res = NULL;
      goto beach;
    }
    if (!res) {
      res = gst_caps_copy (stream->caps);
    } else {
      GstCaps *common_caps = gst_caps_merge_common (res, stream->caps);
      gst_caps_unref (res);
      res = common_caps;
      if (!res)
        goto beach;
    }
  }

  res = gst_caps_simplify (res);

beach:
  GST_DEBUG ("Returning common caps %" GST_PTR_FORMAT, res);

  return res;
}

GstStreamType
gst_stream_type_from_hls_type (GstHLSRenditionStreamType mtype)
{
  switch (mtype) {
    case GST_HLS_RENDITION_STREAM_TYPE_AUDIO:
      return GST_STREAM_TYPE_AUDIO;
    case GST_HLS_RENDITION_STREAM_TYPE_VIDEO:
      return GST_STREAM_TYPE_VIDEO;
    case GST_HLS_RENDITION_STREAM_TYPE_SUBTITLES:
      return GST_STREAM_TYPE_TEXT;
    default:
      return GST_STREAM_TYPE_UNKNOWN;
  }
}
