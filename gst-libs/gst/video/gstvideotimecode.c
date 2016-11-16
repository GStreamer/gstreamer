/* GStreamer
 * Copyright (C) <2016> Vivia Nikolaidou <vivia@toolsonair.com>
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

#include "gstvideotimecode.h"

G_DEFINE_BOXED_TYPE (GstVideoTimeCode, gst_video_time_code,
    (GBoxedCopyFunc) gst_video_time_code_copy,
    (GBoxedFreeFunc) gst_video_time_code_free);

/**
 * gst_video_time_code_is_valid:
 * @tc: #GstVideoTimeCode to check
 *
 * Returns: whether @tc is a valid timecode (supported frame rate,
 * hours/minutes/seconds/frames not overflowing)
 *
 * Since: 1.10
 */
gboolean
gst_video_time_code_is_valid (const GstVideoTimeCode * tc)
{
  g_return_val_if_fail (tc != NULL, FALSE);

  if (tc->hours > 24)
    return FALSE;
  if (tc->minutes >= 60)
    return FALSE;
  if (tc->seconds >= 60)
    return FALSE;
  if (tc->config.fps_d == 0)
    return FALSE;
  if ((tc->frames > tc->config.fps_n / tc->config.fps_d)
      && (tc->config.fps_n != 0 || tc->config.fps_d != 1))
    return FALSE;
  if (tc->config.fps_d == 1001) {
    if (tc->config.fps_n != 30000 && tc->config.fps_n != 60000)
      return FALSE;
  } else if (tc->config.fps_n % tc->config.fps_d != 0) {
    return FALSE;
  }

  return TRUE;
}

/**
 * gst_video_time_code_to_string:
 * @tc: #GstVideoTimeCode to convert
 *
 * Returns: the SMPTE ST 2059-1:2015 string representation of @tc. That will
 * take the form hh:mm:ss:ff . The last separator (between seconds and frames)
 * may vary:
 *
 * ';' for drop-frame, non-interlaced content and for drop-frame interlaced
 * field 2
 * ',' for drop-frame interlaced field 1
 * ':' for non-drop-frame, non-interlaced content and for non-drop-frame
 * interlaced field 2
 * '.' for non-drop-frame interlaced field 1
 *
 * Since: 1.10
 */
gchar *
gst_video_time_code_to_string (const GstVideoTimeCode * tc)
{
  gchar *ret;
  gboolean top_dot_present;
  gchar sep;

  g_return_val_if_fail (gst_video_time_code_is_valid (tc), NULL);

  /* Top dot is present for non-interlaced content, and for field 2 in
   * interlaced content */
  top_dot_present =
      !((tc->config.flags & GST_VIDEO_TIME_CODE_FLAGS_INTERLACED) != 0
      && tc->field_count == 1);

  if (tc->config.flags & GST_VIDEO_TIME_CODE_FLAGS_DROP_FRAME)
    sep = top_dot_present ? ';' : ',';
  else
    sep = top_dot_present ? ':' : '.';

  ret =
      g_strdup_printf ("%02d:%02d:%02d%c%02d", tc->hours, tc->minutes,
      tc->seconds, sep, tc->frames);

  return ret;
}

/**
 * gst_video_time_code_to_date_time:
 * @tc: #GstVideoTimeCode to convert
 *
 * The @tc.config->latest_daily_jam is required to be non-NULL.
 *
 * Returns: the #GDateTime representation of @tc.
 *
 * Since: 1.10
 */
GDateTime *
gst_video_time_code_to_date_time (const GstVideoTimeCode * tc)
{
  GDateTime *ret;
  GDateTime *ret2;
  gdouble add_us;

  g_return_val_if_fail (gst_video_time_code_is_valid (tc), NULL);
  g_return_val_if_fail (tc->config.latest_daily_jam != NULL, NULL);

  ret = g_date_time_ref (tc->config.latest_daily_jam);

  if (ret == NULL) {
    gchar *tc_str = gst_video_time_code_to_string (tc);
    GST_WARNING
        ("Asked to convert time code %s to GDateTime, but its latest daily jam is NULL",
        tc_str);
    g_free (tc_str);
    return NULL;
  }

  if (tc->config.fps_n == 0 && tc->config.fps_d == 1) {
    gchar *tc_str = gst_video_time_code_to_string (tc);
    GST_WARNING
        ("Asked to convert time code %s to GDateTime, but its framerate is unknown",
        tc_str);
    g_free (tc_str);
    return NULL;
  }

  gst_util_fraction_to_double (tc->frames * tc->config.fps_d, tc->config.fps_n,
      &add_us);
  if ((tc->config.flags & GST_VIDEO_TIME_CODE_FLAGS_INTERLACED)
      && tc->field_count == 1) {
    gdouble sub_us;

    gst_util_fraction_to_double (tc->config.fps_d, 2 * tc->config.fps_n,
        &sub_us);
    add_us -= sub_us;
  }

  ret2 = g_date_time_add_seconds (ret, add_us + tc->seconds);
  g_date_time_unref (ret);
  ret = g_date_time_add_minutes (ret2, tc->minutes);
  g_date_time_unref (ret2);
  ret2 = g_date_time_add_hours (ret, tc->hours);
  g_date_time_unref (ret);

  return ret2;
}

/**
 * gst_video_time_code_nsec_since_daily_jam:
 * @tc: a #GstVideoTimeCode
 *
 * Returns: how many nsec have passed since the daily jam of @tc .
 *
 * Since: 1.10
 */
guint64
gst_video_time_code_nsec_since_daily_jam (const GstVideoTimeCode * tc)
{
  guint64 frames, nsec;

  g_return_val_if_fail (gst_video_time_code_is_valid (tc), -1);

  if (tc->config.fps_n == 0 && tc->config.fps_d == 1) {
    gchar *tc_str = gst_video_time_code_to_string (tc);
    GST_WARNING
        ("Asked to calculate nsec since daily jam of time code %s, but its framerate is unknown",
        tc_str);
    g_free (tc_str);
    return -1;
  }

  frames = gst_video_time_code_frames_since_daily_jam (tc);
  nsec =
      gst_util_uint64_scale (frames, GST_SECOND * tc->config.fps_d,
      tc->config.fps_n);

  return nsec;
}

/**
 * gst_video_time_code_frames_since_daily_jam:
 * @tc: a #GstVideoTimeCode
 *
 * Returns: how many frames have passed since the daily jam of @tc .
 *
 * Since: 1.10
 */
guint64
gst_video_time_code_frames_since_daily_jam (const GstVideoTimeCode * tc)
{
  guint ff_nom;
  gdouble ff;

  g_return_val_if_fail (gst_video_time_code_is_valid (tc), -1);
  g_assert (tc->hours <= 24);
  g_assert (tc->minutes < 60);
  g_assert (tc->seconds < 60);
  g_assert (tc->frames <= tc->config.fps_n / tc->config.fps_d);

  gst_util_fraction_to_double (tc->config.fps_n, tc->config.fps_d, &ff);
  if (tc->config.fps_d == 1001) {
    ff_nom = tc->config.fps_n / 1000;
  } else {
    ff_nom = ff;
  }
  if (tc->config.flags & GST_VIDEO_TIME_CODE_FLAGS_DROP_FRAME) {
    /* these need to be truncated to integer: side effect, code looks cleaner
     * */
    guint ff_minutes = 60 * ff;
    guint ff_hours = 3600 * ff;
    /* for 30000/1001 we drop the first 2 frames per minute, for 60000/1001 we
     * drop the first 4 : so we use this number */
    guint dropframe_multiplier;

    if (tc->config.fps_n == 30000) {
      dropframe_multiplier = 2;
    } else if (tc->config.fps_n == 60000) {
      dropframe_multiplier = 4;
    } else {
      GST_ERROR ("Unsupported drop frame rate %u/%u", tc->config.fps_n,
          tc->config.fps_d);
      return -1;
    }

    return tc->frames + (ff_nom * tc->seconds) +
        (ff_minutes * tc->minutes) +
        dropframe_multiplier * ((gint) (tc->minutes / 10)) +
        (ff_hours * tc->hours);
  } else {
    return tc->frames + (ff_nom * (tc->seconds + (60 * (tc->minutes +
                    (60 * tc->hours)))));
  }

}

/**
 * gst_video_time_code_increment_frame:
 * @tc: a #GstVideoTimeCode
 *
 * Adds one frame to @tc .
 *
 * Since: 1.10
 */
void
gst_video_time_code_increment_frame (GstVideoTimeCode * tc)
{
  return gst_video_time_code_add_frames (tc, 1);
}

/**
 * gst_video_time_code_add_frames:
 * @tc: a #GstVideoTimeCode
 * @frames: How many frames to add or subtract
 *
 * Adds or subtracts @frames amount of frames to @tc .
 *
 * Since: 1.10
 */
void
gst_video_time_code_add_frames (GstVideoTimeCode * tc, gint64 frames)
{
  guint64 framecount;
  guint64 h_notmod24;
  guint64 h_new, min_new, sec_new, frames_new;
  gdouble ff;
  guint ff_nom;
  /* This allows for better readability than putting G_GUINT64_CONSTANT(60)
   * into a long calculation line */
  const guint64 sixty = 60;
  /* formulas found in SMPTE ST 2059-1:2015 section 9.4.3
   * and adapted for 60/1.001 as well as 30/1.001 */

  g_return_if_fail (gst_video_time_code_is_valid (tc));
  g_assert (tc->hours <= 24);
  g_assert (tc->minutes < 60);
  g_assert (tc->seconds < 60);
  g_assert (tc->frames <= tc->config.fps_n / tc->config.fps_d);

  gst_util_fraction_to_double (tc->config.fps_n, tc->config.fps_d, &ff);
  if (tc->config.fps_d == 1001) {
    ff_nom = tc->config.fps_n / 1000;
  } else {
    ff_nom = ff;
    if (tc->config.fps_d != 1)
      GST_WARNING ("Unsupported frame rate %u/%u, results may be wrong",
          tc->config.fps_n, tc->config.fps_d);
  }
  if (tc->config.flags & GST_VIDEO_TIME_CODE_FLAGS_DROP_FRAME) {
    /* these need to be truncated to integer: side effect, code looks cleaner
     * */
    guint ff_minutes = 60 * ff;
    guint ff_hours = 3600 * ff;
    /* a bunch of intermediate variables, to avoid monster code with possible
     * integer overflows */
    guint64 min_new_tmp1, min_new_tmp2, min_new_tmp3, min_new_denom;
    /* for 30000/1001 we drop the first 2 frames per minute, for 60000/1001 we
     * drop the first 4 : so we use this number */
    guint dropframe_multiplier;

    if (tc->config.fps_n == 30000)
      dropframe_multiplier = 2;
    else if (tc->config.fps_n == 60000)
      dropframe_multiplier = 4;
    else {
      GST_ERROR ("Unsupported drop frame rate %u/%u", tc->config.fps_n,
          tc->config.fps_d);
      return;
    }

    framecount =
        frames + tc->frames + (ff_nom * tc->seconds) +
        (ff_minutes * tc->minutes) +
        dropframe_multiplier * ((gint) (tc->minutes / 10)) +
        (ff_hours * tc->hours);
    h_notmod24 = gst_util_uint64_scale_int (framecount, 1, ff_hours);

    min_new_denom = sixty * ff_nom;
    min_new_tmp1 = (framecount - (h_notmod24 * ff_hours)) / min_new_denom;
    min_new_tmp2 = framecount + dropframe_multiplier * min_new_tmp1;
    min_new_tmp1 =
        (framecount - (h_notmod24 * ff_hours)) / (sixty * 10 * ff_nom);
    min_new_tmp3 =
        dropframe_multiplier * min_new_tmp1 + (h_notmod24 * ff_hours);
    min_new =
        gst_util_uint64_scale_int (min_new_tmp2 - min_new_tmp3, 1,
        min_new_denom);

    sec_new =
        (guint64) ((framecount - (ff_minutes * min_new) -
            dropframe_multiplier * ((gint) (min_new / 10)) -
            (ff_hours * h_notmod24)) / ff_nom);

    frames_new =
        framecount - (ff_nom * sec_new) - (ff_minutes * min_new) -
        (dropframe_multiplier * ((gint) (min_new / 10))) -
        (ff_hours * h_notmod24);
  } else {
    framecount =
        frames + tc->frames + (ff_nom * (tc->seconds + (sixty * (tc->minutes +
                    (sixty * tc->hours)))));
    h_notmod24 =
        gst_util_uint64_scale_int (framecount, 1, ff_nom * sixty * sixty);
    min_new =
        gst_util_uint64_scale_int ((framecount -
            (ff_nom * sixty * sixty * h_notmod24)), 1, (ff_nom * sixty));
    sec_new =
        gst_util_uint64_scale_int ((framecount - (ff_nom * sixty * (min_new +
                    (sixty * h_notmod24)))), 1, ff_nom);
    frames_new =
        framecount - (ff_nom * (sec_new + sixty * (min_new +
                (sixty * h_notmod24))));
    if (frames_new > ff_nom)
      frames_new = 0;
  }
  h_new = h_notmod24 % 24;

  g_assert (min_new < 60);
  g_assert (sec_new < 60);
  g_assert (frames_new < ff_nom);
  tc->hours = h_new;
  tc->minutes = min_new;
  tc->seconds = sec_new;
  tc->frames = frames_new;
}

/**
 * gst_video_time_code_compare:
 * @tc1: a #GstVideoTimeCode
 * @tc2: another #GstVideoTimeCode
 *
 * Compares @tc1 and @tc2 . If both have latest daily jam information, it is
 * taken into account. Otherwise, it is assumed that the daily jam of both
 * @tc1 and @tc2 was at the same time.
 *
 * Returns: 1 if @tc1 is after @tc2, -1 if @tc1 is before @tc2, 0 otherwise.
 *
 * Since: 1.10
 */
gint
gst_video_time_code_compare (const GstVideoTimeCode * tc1,
    const GstVideoTimeCode * tc2)
{
  g_return_val_if_fail (gst_video_time_code_is_valid (tc1), -1);
  g_return_val_if_fail (gst_video_time_code_is_valid (tc2), -1);

  if (tc1->config.latest_daily_jam == NULL
      || tc2->config.latest_daily_jam == NULL) {
    guint64 nsec1, nsec2;
#ifndef GST_DISABLE_GST_DEBUG
    gchar *str1, *str2;

    str1 = gst_video_time_code_to_string (tc1);
    str2 = gst_video_time_code_to_string (tc2);
    GST_INFO
        ("Comparing time codes %s and %s, but at least one of them has no "
        "latest daily jam information. Assuming they started together",
        str1, str2);
    g_free (str1);
    g_free (str2);
#endif
    if (tc1->hours > tc2->hours) {
      return 1;
    } else if (tc1->hours < tc2->hours) {
      return -1;
    }
    if (tc1->minutes > tc2->minutes) {
      return 1;
    } else if (tc1->minutes < tc2->minutes) {
      return -1;
    }
    if (tc1->seconds > tc2->seconds) {
      return 1;
    } else if (tc1->seconds < tc2->seconds) {
      return -1;
    }

    nsec1 =
        gst_util_uint64_scale (GST_SECOND,
        tc1->frames * tc1->config.fps_n, tc1->config.fps_d);
    nsec2 =
        gst_util_uint64_scale (GST_SECOND,
        tc2->frames * tc2->config.fps_n, tc2->config.fps_d);
    if (nsec1 > nsec2) {
      return 1;
    } else if (nsec1 < nsec2) {
      return -1;
    }
    if (tc1->config.flags & GST_VIDEO_TIME_CODE_FLAGS_INTERLACED) {
      if (tc1->field_count > tc2->field_count)
        return 1;
      else if (tc1->field_count < tc2->field_count)
        return -1;
    }
    return 0;
  } else {
    GDateTime *dt1, *dt2;
    gint ret;

    dt1 = gst_video_time_code_to_date_time (tc1);
    dt2 = gst_video_time_code_to_date_time (tc2);

    ret = g_date_time_compare (dt1, dt2);

    g_date_time_unref (dt1);
    g_date_time_unref (dt2);

    return ret;
  }
}

/**
 * gst_video_time_code_new:
 * @fps_n: Numerator of the frame rate
 * @fps_d: Denominator of the frame rate
 * @latest_daily_jam: The latest daily jam of the #GstVideoTimeCode
 * @flags: #GstVideoTimeCodeFlags
 * @hours: the hours field of #GstVideoTimeCode
 * @minutes: the minutes field of #GstVideoTimeCode
 * @seconds: the seconds field of #GstVideoTimeCode
 * @frames: the frames field of #GstVideoTimeCode
 * @field_count: Interlaced video field count
 *
 * @field_count is 0 for progressive, 1 or 2 for interlaced.
 * @latest_daiy_jam reference is stolen from caller.
 *
 * Returns: a new #GstVideoTimeCode with the given values.
 *
 * Since: 1.10
 */
GstVideoTimeCode *
gst_video_time_code_new (guint fps_n, guint fps_d, GDateTime * latest_daily_jam,
    GstVideoTimeCodeFlags flags, guint hours, guint minutes, guint seconds,
    guint frames, guint field_count)
{
  GstVideoTimeCode *tc;

  tc = g_new0 (GstVideoTimeCode, 1);
  gst_video_time_code_init (tc, fps_n, fps_d, latest_daily_jam, flags, hours,
      minutes, seconds, frames, field_count);
  return tc;
}

/**
 * gst_video_time_code_new_empty:
 *
 * Returns: a new empty #GstVideoTimeCode
 *
 * Since: 1.10
 */
GstVideoTimeCode *
gst_video_time_code_new_empty (void)
{
  GstVideoTimeCode *tc;

  tc = g_new0 (GstVideoTimeCode, 1);
  gst_video_time_code_clear (tc);
  return tc;
}

/**
 * gst_video_time_code_init:
 * @tc: a #GstVideoTimeCode
 * @fps_n: Numerator of the frame rate
 * @fps_d: Denominator of the frame rate
 * @latest_daily_jam: The latest daily jam of the #GstVideoTimeCode
 * @flags: #GstVideoTimeCodeFlags
 * @hours: the hours field of #GstVideoTimeCode
 * @minutes: the minutes field of #GstVideoTimeCode
 * @seconds: the seconds field of #GstVideoTimeCode
 * @frames: the frames field of #GstVideoTimeCode
 * @field_count: Interlaced video field count
 *
 * @field_count is 0 for progressive, 1 or 2 for interlaced.
 * @latest_daiy_jam reference is stolen from caller.
 *
 * Initializes @tc with the given values.
 *
 * Since: 1.10
 */
void
gst_video_time_code_init (GstVideoTimeCode * tc, guint fps_n, guint fps_d,
    GDateTime * latest_daily_jam, GstVideoTimeCodeFlags flags, guint hours,
    guint minutes, guint seconds, guint frames, guint field_count)
{
  tc->hours = hours;
  tc->minutes = minutes;
  tc->seconds = seconds;
  tc->frames = frames;
  tc->field_count = field_count;
  tc->config.fps_n = fps_n;
  tc->config.fps_d = fps_d;
  if (latest_daily_jam != NULL)
    tc->config.latest_daily_jam = g_date_time_ref (latest_daily_jam);
  else
    tc->config.latest_daily_jam = NULL;
  tc->config.flags = flags;

  g_return_if_fail (gst_video_time_code_is_valid (tc));
}

/**
 * gst_video_time_code_clear:
 * @tc: a #GstVideoTimeCode
 *
 * Initializes @tc with empty/zero/NULL values.
 *
 * Since: 1.10
 */
void
gst_video_time_code_clear (GstVideoTimeCode * tc)
{
  tc->hours = 0;
  tc->minutes = 0;
  tc->seconds = 0;
  tc->frames = 0;
  tc->field_count = 0;
  tc->config.fps_n = 0;
  tc->config.fps_d = 1;
  if (tc->config.latest_daily_jam != NULL)
    g_date_time_unref (tc->config.latest_daily_jam);
  tc->config.latest_daily_jam = NULL;
  tc->config.flags = 0;
}

/**
 * gst_video_time_code_copy:
 * @tc: a #GstVideoTimeCode
 *
 * Returns: a new #GstVideoTimeCode with the same values as @tc .
 *
 * Since: 1.10
 */
GstVideoTimeCode *
gst_video_time_code_copy (const GstVideoTimeCode * tc)
{
  return gst_video_time_code_new (tc->config.fps_n, tc->config.fps_d,
      tc->config.latest_daily_jam, tc->config.flags, tc->hours, tc->minutes,
      tc->seconds, tc->frames, tc->field_count);
}

/**
 * gst_video_time_code_free:
 * @tc: a #GstVideoTimeCode
 *
 * Frees @tc .
 *
 * Since: 1.10
 */
void
gst_video_time_code_free (GstVideoTimeCode * tc)
{
  if (tc->config.latest_daily_jam != NULL)
    g_date_time_unref (tc->config.latest_daily_jam);

  g_free (tc);
}
