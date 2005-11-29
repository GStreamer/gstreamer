/* GStreamer
 * Copyright (C) 2005 Wim Taymans <wim@fluendo.com>
 *
 * gstsegment.c: GstSegment subsystem
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


#include "gst_private.h"

#include "gstutils.h"
#include "gstsegment.h"

/**
 * SECTION:gstsegment
 * @short_description: Structure describing the configured region of interest
 *                     in a media file.
 * @see_also: #GstEvent
 *
 * This helper structure holds the relevant values for tracking the region of
 * interest in a media file, called a segment.
 *
 * Last reviewed on 2005-20-09 (0.9.5)
 */

static GstSegment *
gst_segment_copy (GstSegment * segment)
{
  GstSegment *result = NULL;

  if (segment) {
    result = gst_segment_new ();
    memcpy (result, segment, sizeof (GstSegment));
  }
  return NULL;
}

GType
gst_segment_get_type (void)
{
  static GType gst_segment_type = 0;

  if (!gst_segment_type) {
    gst_segment_type = g_boxed_type_register_static ("GstSegment",
        (GBoxedCopyFunc) gst_segment_copy, (GBoxedFreeFunc) gst_segment_free);
  }

  return gst_segment_type;
}

/**
 * gst_segment_new:
 *
 * Allocate a new #GstSegment structure and initialize it using 
 * gst_segment_init().
 *
 * Returns: a new #GstSegment, free with gst_segment_free().
 */
GstSegment *
gst_segment_new (void)
{
  GstSegment *result;

  result = g_new0 (GstSegment, 1);
  gst_segment_init (result, GST_FORMAT_UNDEFINED);

  return result;
}

/**
 * gst_segment_free:
 * @segment: a #GstSegment
 *
 * Free the allocated segment @segment.
 */
void
gst_segment_free (GstSegment * segment)
{
  g_free (segment);
}

/**
 * gst_segment_init:
 * @segment: a #GstSegment structure.
 * @format: the format of the segment.
 *
 * Initialize @segment to its default values, which is a rate of 1.0, a
 * start time of 0.
 */
void
gst_segment_init (GstSegment * segment, GstFormat format)
{
  g_return_if_fail (segment != NULL);

  segment->rate = 1.0;
  segment->abs_rate = 1.0;
  segment->format = format;
  segment->flags = 0;
  segment->start = 0;
  segment->stop = -1;
  segment->time = 0;
  segment->accum = 0;
  segment->last_stop = -1;
  segment->duration = -1;
}

/**
 * gst_segment_set_duration:
 * @segment: a #GstSegment structure.
 * @format: the format of the segment.
 * @duration: the duration of the segment info.
 *
 * Set the duration of the segment to @duration. This function is mainly
 * used by elements that perform seeking and know the total duration of the
 * segment.
 */
void
gst_segment_set_duration (GstSegment * segment, GstFormat format,
    gint64 duration)
{
  g_return_if_fail (segment != NULL);

  if (segment->format == GST_FORMAT_UNDEFINED)
    segment->format = format;
  else
    g_return_if_fail (segment->format == format);

  segment->duration = duration;
}

/**
 * gst_segment_set_last_stop:
 * @segment: a #GstSegment structure.
 * @format: the format of the segment.
 * @position: the position 
 *
 * Set the last observed stop position in the segment to @position.
 */
void
gst_segment_set_last_stop (GstSegment * segment, GstFormat format,
    gint64 position)
{
  g_return_if_fail (segment != NULL);

  if (segment->format == GST_FORMAT_UNDEFINED)
    segment->format = format;
  else
    g_return_if_fail (segment->format == format);

  segment->last_stop = position;
}

/**
 * gst_segment_set_seek:
 * @segment: a #GstSegment structure.
 * @rate: the rate of the segment.
 * @format: the format of the segment.
 * @flags: the seek flags for the segment
 * @cur_type: the seek method
 * @cur: the seek start value
 * @stop_type: the seek method
 * @stop: the seek stop value
 * @update: boolean holding whether an update the current segment is
 *    needed.
 *
 * Update the segment structure with the field values of a seek event.
 */
void
gst_segment_set_seek (GstSegment * segment, gdouble rate,
    GstFormat format, GstSeekFlags flags,
    GstSeekType cur_type, gint64 cur,
    GstSeekType stop_type, gint64 stop, gboolean * update)
{
  gboolean update_stop, update_start;

  g_return_if_fail (rate != 0.0);
  g_return_if_fail (segment != NULL);

  if (segment->format == GST_FORMAT_UNDEFINED)
    segment->format = format;
  else
    g_return_if_fail (segment->format == format);

  update_stop = update_start = TRUE;

  /* start is never invalid */
  switch (cur_type) {
    case GST_SEEK_TYPE_NONE:
      /* no update to segment */
      cur = segment->start;
      update_start = FALSE;
      break;
    case GST_SEEK_TYPE_SET:
      /* cur holds desired position */
      break;
    case GST_SEEK_TYPE_CUR:
      /* add cur to currently configure segment */
      cur = segment->start + cur;
      break;
    case GST_SEEK_TYPE_END:
      if (segment->duration != -1) {
        /* add cur to total length */
        cur = segment->duration + cur;
      } else {
        /* no update if duration unknown */
        cur = segment->start;
        update_start = FALSE;
      }
      break;
  }
  /* bring in sane range */
  if (segment->duration != -1)
    cur = CLAMP (cur, 0, segment->duration);
  else
    cur = MAX (cur, 0);

  /* stop can be -1 if we have not configured a stop. */
  switch (stop_type) {
    case GST_SEEK_TYPE_NONE:
      stop = segment->stop;
      update_stop = FALSE;
      break;
    case GST_SEEK_TYPE_SET:
      /* stop folds required value */
      break;
    case GST_SEEK_TYPE_CUR:
      if (segment->stop != -1)
        stop = segment->stop + stop;
      else
        stop = -1;
      break;
    case GST_SEEK_TYPE_END:
      if (segment->duration != -1)
        stop = segment->duration + stop;
      else {
        stop = segment->stop;
        update_stop = FALSE;
      }
      break;
  }

  /* if we have a valid stop time, make sure it is clipped */
  if (stop != -1) {
    if (segment->duration != -1)
      stop = CLAMP (stop, 0, segment->duration);
    else
      stop = MAX (stop, 0);
  }

  /* we can't have stop before start */
  if (stop != -1)
    g_return_if_fail (cur <= stop);

  segment->rate = rate;
  segment->abs_rate = ABS (rate);
  segment->flags = flags;
  segment->start = cur;
  segment->stop = stop;

  if (update)
    *update = update_start || update_stop;
}

/**
 * gst_segment_set_newsegment:
 * @segment: a #GstSegment structure.
 * @update: flag indicating a new segment is started or updated
 * @rate: the rate of the segment.
 * @format: the format of the segment.
 * @start: the new start value
 * @stop: the new stop value
 * @time: the new stream time
 *
 * Update the segment structure with the field values of a new segment event.
 */
void
gst_segment_set_newsegment (GstSegment * segment, gboolean update, gdouble rate,
    GstFormat format, gint64 start, gint64 stop, gint64 time)
{
  gint64 duration;

  g_return_if_fail (rate != 0.0);
  g_return_if_fail (segment != NULL);

  if (segment->format == GST_FORMAT_UNDEFINED)
    segment->format = format;

  /* any other format with 0 also gives time 0, the other values are
   * invalid in the format though. */
  if (format != segment->format && start == 0) {
    format = segment->format;
    if (stop != 0)
      stop = -1;
    if (time != 0)
      time = -1;
  }

  g_return_if_fail (segment->format == format);

  if (update) {
    /* an update to the current segment is done, elapsed time is
     * difference between the old start and new start. */
    duration = start - segment->start;
  } else {
    /* the new segment has to be aligned with the old segment.
     * We first update the accumulated time of the previous
     * segment. the accumulated time is used when syncing to the
     * clock. 
     */
    if (GST_CLOCK_TIME_IS_VALID (segment->stop)) {
      duration = segment->stop - segment->start;
    } else if (GST_CLOCK_TIME_IS_VALID (segment->last_stop)) {
      /* else use last seen timestamp as segment stop */
      duration = segment->last_stop - segment->start;
    } else {
      /* else we don't know */
      duration = 0;
    }
  }
  /* use previous rate to calculate duration */
  segment->accum += gst_gdouble_to_guint64 (
      (gst_guint64_to_gdouble (duration) / segment->abs_rate));
  /* then update the current segment */
  segment->rate = rate;
  segment->abs_rate = ABS (rate);
  segment->start = start;
  segment->stop = stop;
  segment->time = time;
}

/**
 * gst_segment_to_stream_time:
 * @segment: a #GstSegment structure.
 * @format: the format of the segment.
 * @position: the position in the segment
 *
 * Translate @position to stream time using the currently configured 
 * segment.
 *
 * This function is typically used by elements that need to operate on
 * the stream time of the buffers it receives, such as effect plugins.
 *
 * Returns: the position in stream_time.
 */
gint64
gst_segment_to_stream_time (GstSegment * segment, GstFormat format,
    gint64 position)
{
  gint64 result, time;

  g_return_val_if_fail (segment != NULL, FALSE);

  if (segment->format == GST_FORMAT_UNDEFINED)
    segment->format = format;
  else
    g_return_val_if_fail (segment->format == format, FALSE);

  if ((time = segment->time) == -1)
    time = 0;

  if (position != -1)
    result = ((position - segment->start) / segment->abs_rate) + time;
  else
    result = -1;

  return result;
}

/**
 * gst_segment_to_running_time:
 * @segment: a #GstSegment structure.
 * @format: the format of the segment.
 * @position: the position in the segment
 *
 * Translate @position to the total running time using the currently configured 
 * segment.
 *
 * This function is typically used by elements that need to synchronize to the
 * global clock in a pipeline.
 *
 * Returns: the position as the total running time.
 */
gint64
gst_segment_to_running_time (GstSegment * segment, GstFormat format,
    gint64 position)
{
  gint64 result;

  g_return_val_if_fail (segment != NULL, -1);

  if (segment->format == GST_FORMAT_UNDEFINED)
    segment->format = format;
  else if (segment->accum)
    g_return_val_if_fail (segment->format == format, -1);

  if (position != -1)
    result = ((position - segment->start) / segment->abs_rate) + segment->accum;
  else
    result = -1;

  return result;
}

/**
 * gst_segment_clip:
 * @segment: a #GstSegment structure.
 * @format: the format of the segment.
 * @start: the start position in the segment
 * @stop: the stop position in the segment
 * @clip_start: the clipped start position in the segment
 * @clip_stop: the clipped stop position in the segment
 *
 * Clip the given @start and @stop values to the segment boundaries given
 * in @segment.
 *
 * Returns: TRUE if the given @start and @stop times fall partially in 
 *     @segment, FALSE if the values are completely outside of the segment.
 */
gboolean
gst_segment_clip (GstSegment * segment, GstFormat format, gint64 start,
    gint64 stop, gint64 * clip_start, gint64 * clip_stop)
{
  g_return_val_if_fail (segment != NULL, FALSE);

  if (segment->format == GST_FORMAT_UNDEFINED)
    segment->format = format;
  else
    g_return_val_if_fail (segment->format == format, FALSE);

  /* we need a valid start position */
  if (start == -1)
    return FALSE;

  /* if we have a stop position and start is bigger, we're
   * outside of the segment */
  if (segment->stop != -1 && start >= segment->stop)
    return FALSE;

  /* if a stop position is given and is before the segment start,
   * we're outside of the segment */
  if (stop != -1 && stop <= segment->start)
    return FALSE;

  if (clip_start)
    *clip_start = MAX (start, segment->start);

  if (clip_stop) {
    if (stop == -1)
      *clip_stop = segment->stop;
    else if (segment->stop == -1)
      *clip_stop = MAX (-1, stop);
    else
      *clip_stop = MIN (stop, segment->stop);

    if (segment->duration != -1)
      *clip_stop = MIN (*clip_stop, segment->duration);
  }

  return TRUE;
}
