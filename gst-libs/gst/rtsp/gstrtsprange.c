/* GStreamer
 * Copyright (C) <2005,2006> Wim Taymans <wim@fluendo.com>
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
/*
 * Unless otherwise indicated, Source Code is licensed under MIT license.
 * See further explanation attached in License Statement (distributed in the file
 * LICENSE).
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is furnished to do
 * so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/**
 * SECTION:gstrtsprange
 * @short_description: dealing with time ranges
 *  
 * Provides helper functions to deal with time ranges.
 *  
 * Last reviewed on 2007-07-25 (0.10.14)
 */


#include <stdio.h>
#include <string.h>

#include "gstrtsprange.h"

static gdouble
gst_strtod (const gchar * dstr)
{
  gchar s[G_ASCII_DTOSTR_BUF_SIZE] = { 0, };

  /* canonicalise floating point string so we can handle float strings
   * in the form "24.930" or "24,930" irrespective of the current locale.
   * We should always be getting floats in 24.930 format with a floating point,
   * but let's accept malformed ones as well, easy mistake to make after all */
  g_strlcpy (s, dstr, sizeof (s));
  g_strdelimit (s, ",", '.');
  return g_ascii_strtod (s, NULL);
}

/* npt-time     =   "now" | npt-sec | npt-hhmmss
 * npt-sec      =   1*DIGIT [ "." *DIGIT ]
 * npt-hhmmss   =   npt-hh ":" npt-mm ":" npt-ss [ "." *DIGIT ]
 * npt-hh       =   1*DIGIT     ; any positive number
 * npt-mm       =   1*2DIGIT    ; 0-59
 * npt-ss       =   1*2DIGIT    ; 0-59
 */
static GstRTSPResult
parse_npt_time (const gchar * str, GstRTSPTime * time)
{
  if (strncmp (str, "now", 3) == 0) {
    time->type = GST_RTSP_TIME_NOW;
  } else if (str[0] == '\0') {
    time->type = GST_RTSP_TIME_END;
  } else if (strstr (str, ":")) {
    gint hours, mins;

    sscanf (str, "%2d:%2d:", &hours, &mins);
    str = strchr (str, ':') + 1;
    str = strchr (str, ':') + 1;
    time->type = GST_RTSP_TIME_SECONDS;
    time->seconds = ((hours * 60) + mins) * 60 + gst_strtod (str);
  } else {
    time->type = GST_RTSP_TIME_SECONDS;
    time->seconds = gst_strtod (str);
  }
  return GST_RTSP_OK;
}

/* npt-range    =   ( npt-time "-" [ npt-time ] ) | ( "-" npt-time )
 */
static GstRTSPResult
parse_npt_range (const gchar * str, GstRTSPTimeRange * range)
{
  GstRTSPResult res;
  gchar *p;

  range->unit = GST_RTSP_RANGE_NPT;

  /* find '-' separator */
  p = strstr (str, "-");
  if (p == NULL)
    return GST_RTSP_EINVAL;

  if ((res = parse_npt_time (str, &range->min)) != GST_RTSP_OK)
    goto done;

  res = parse_npt_time (p + 1, &range->max);

done:
  return res;
}

static GstRTSPResult
parse_clock_range (const gchar * str, GstRTSPTimeRange * range)
{
  return GST_RTSP_ENOTIMPL;
}

static GstRTSPResult
parse_smpte_range (const gchar * str, GstRTSPTimeRange * range)
{
  return GST_RTSP_ENOTIMPL;
}

/**
 * gst_rtsp_range_parse:
 * @rangestr: a range string to parse
 * @range: location to hold the #GstRTSPTimeRange result
 *
 * Parse @rangestr to a #GstRTSPTimeRange.
 *
 * Returns: #GST_RTSP_OK on success.
 */
GstRTSPResult
gst_rtsp_range_parse (const gchar * rangestr, GstRTSPTimeRange ** range)
{
  GstRTSPResult ret;
  GstRTSPTimeRange *res;
  gchar *p;

  g_return_val_if_fail (rangestr != NULL, GST_RTSP_EINVAL);
  g_return_val_if_fail (range != NULL, GST_RTSP_EINVAL);

  res = g_new0 (GstRTSPTimeRange, 1);

  p = (gchar *) rangestr;
  /* first figure out the units of the range */
  if (g_str_has_prefix (p, "npt=")) {
    ret = parse_npt_range (p + 4, res);
  } else if (g_str_has_prefix (p, "clock=")) {
    ret = parse_clock_range (p + 6, res);
  } else if (g_str_has_prefix (p, "smpte=")) {
    res->unit = GST_RTSP_RANGE_SMPTE;
    ret = parse_smpte_range (p + 6, res);
  } else if (g_str_has_prefix (p, "smpte-30-drop=")) {
    res->unit = GST_RTSP_RANGE_SMPTE_30_DROP;
    ret = parse_smpte_range (p + 14, res);
  } else if (g_str_has_prefix (p, "smpte-25=")) {
    res->unit = GST_RTSP_RANGE_SMPTE_25;
    ret = parse_smpte_range (p + 9, res);
  } else
    goto invalid;

  if (ret != GST_RTSP_OK)
    goto invalid;

  *range = res;
  return ret;

  /* ERRORS */
invalid:
  {
    gst_rtsp_range_free (res);
    return GST_RTSP_EINVAL;
  }
}

static gboolean
npt_time_string (const GstRTSPTime * time, GString * string)
{
  gchar dstrbuf[G_ASCII_DTOSTR_BUF_SIZE] = { 0, };
  gboolean res = TRUE;;

  switch (time->type) {
    case GST_RTSP_TIME_SECONDS:
      /* need to format floating point value strings as in C locale */
      g_ascii_dtostr (dstrbuf, G_ASCII_DTOSTR_BUF_SIZE, time->seconds);
      g_string_append (string, dstrbuf);
      break;
    case GST_RTSP_TIME_NOW:
      g_string_append (string, "now");
      break;
    case GST_RTSP_TIME_END:
      break;
    default:
      res = FALSE;
      break;
  }
  return res;
}

static gboolean
npt_range_string (const GstRTSPTimeRange * range, GString * string)
{
  gboolean res;

  if (!(res = npt_time_string (&range->min, string)))
    goto done;

  g_string_append (string, "-");

  if (!(res = npt_time_string (&range->max, string)))
    goto done;

done:
  return res;
}

/**
 * gst_rtsp_range_to_string:
 * @range: a #GstRTSPTimeRange
 *
 * Convert @range into a string representation.
 *
 * Returns: The string representation of @range. g_free() after usage.
 */
gchar *
gst_rtsp_range_to_string (const GstRTSPTimeRange * range)
{
  gchar *result = NULL;
  GString *string;

  g_return_val_if_fail (range != NULL, NULL);

  string = g_string_new ("");

  switch (range->unit) {
    case GST_RTSP_RANGE_NPT:
      g_string_append (string, "npt=");
      if (!npt_range_string (range, string)) {
        g_string_free (string, TRUE);
        string = NULL;
      }
      break;
    case GST_RTSP_RANGE_SMPTE:
    case GST_RTSP_RANGE_SMPTE_30_DROP:
    case GST_RTSP_RANGE_SMPTE_25:
    case GST_RTSP_RANGE_CLOCK:
    default:
      g_warning ("time range unit not yet implemented");
      g_string_free (string, TRUE);
      string = NULL;
      break;
  }
  if (string)
    result = g_string_free (string, FALSE);

  return result;
}

/**
 * gst_rtsp_range_free:
 * @range: a #GstRTSPTimeRange
 *
 * Free the memory allocated by @range.
 */
void
gst_rtsp_range_free (GstRTSPTimeRange * range)
{
  g_free (range);
}
