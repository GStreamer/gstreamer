/* GStreamer
 * Copyright (C) <2018-2019> Seungha Yang <seungha.yang@navercorp.com>
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <string.h>
#include <stdio.h>

#include "video-hdr.h"

#define N_ELEMENT_MASTERING_DISPLAY_INFO 20
#define MASTERING_FORMAT \
  "%d:%d:" \
  "%d:%d:" \
  "%d:%d:" \
  "%d:%d:" \
  "%d:%d:" \
  "%d:%d:" \
  "%d:%d:" \
  "%d:%d:" \
  "%d:%d:" \
  "%d:%d"

#define MASTERING_SCANF_ARGS(m) \
  &(m)->Rx_n, &(m)->Rx_d, &(m)->Ry_n, &(m)->Ry_d, \
  &(m)->Gx_n, &(m)->Gx_d, &(m)->Gy_n, &(m)->Gy_d, \
  &(m)->Bx_n, &(m)->Bx_d, &(m)->By_n, &(m)->By_d, \
  &(m)->Wx_n, &(m)->Wx_d, &(m)->Wy_n, &(m)->Wy_d, \
  &(m)->max_luma_n, &(m)->max_luma_d,             \
  &(m)->min_luma_n, &(m)->min_luma_d

#define RX_ARGS(m) (m)->Rx_n, (m)->Rx_d
#define RY_ARGS(m) (m)->Ry_n, (m)->Ry_d
#define GX_ARGS(m) (m)->Gx_n, (m)->Gx_d
#define GY_ARGS(m) (m)->Gy_n, (m)->Gy_d
#define BX_ARGS(m) (m)->Bx_n, (m)->Bx_d
#define BY_ARGS(m) (m)->By_n, (m)->By_d
#define WX_ARGS(m) (m)->Wx_n, (m)->Wx_d
#define WY_ARGS(m) (m)->Wy_n, (m)->Wy_d
#define MAX_LUMA_ARGS(m) (m)->max_luma_n, (m)->max_luma_d
#define MIN_LUMA_ARGS(m) (m)->min_luma_n, (m)->min_luma_d

#define MASTERING_PRINTF_ARGS(m) \
  RX_ARGS(m), RY_ARGS(m), \
  GX_ARGS(m), GY_ARGS(m), \
  BX_ARGS(m), BY_ARGS(m), \
  WX_ARGS(m), WY_ARGS(m), \
  MAX_LUMA_ARGS(m), MIN_LUMA_ARGS(m)

/**
 * gst_video_mastering_display_info_init:
 * @minfo: a #GstVideoMasteringDisplayInfo
 *
 * Initialize @minfo
 *
 * Since: 1.18
 */
void
gst_video_mastering_display_info_init (GstVideoMasteringDisplayInfo * minfo)
{
  g_return_if_fail (minfo != NULL);

  memset (minfo, 0, sizeof (GstVideoMasteringDisplayInfo));
}

#define DIVIDE_ARGS(val,gcd) \
{ \
  minfo->G_PASTE(val,_n) /= gcd; \
  minfo->G_PASTE(val,_d) /= gcd; \
}

static void
gst_video_mastering_display_info_normalize (GstVideoMasteringDisplayInfo *
    minfo)
{
  guint gcd;

  gcd = gst_util_greatest_common_divisor (RX_ARGS (minfo));
  DIVIDE_ARGS (Rx, gcd);

  gcd = gst_util_greatest_common_divisor (RY_ARGS (minfo));
  DIVIDE_ARGS (Ry, gcd);

  gcd = gst_util_greatest_common_divisor (GX_ARGS (minfo));
  DIVIDE_ARGS (Gx, gcd);

  gcd = gst_util_greatest_common_divisor (GY_ARGS (minfo));
  DIVIDE_ARGS (Gy, gcd);

  gcd = gst_util_greatest_common_divisor (BX_ARGS (minfo));
  DIVIDE_ARGS (Bx, gcd);

  gcd = gst_util_greatest_common_divisor (BY_ARGS (minfo));
  DIVIDE_ARGS (By, gcd);

  gcd = gst_util_greatest_common_divisor (WX_ARGS (minfo));
  DIVIDE_ARGS (Wx, gcd);

  gcd = gst_util_greatest_common_divisor (WY_ARGS (minfo));
  DIVIDE_ARGS (Wy, gcd);

  gcd = gst_util_greatest_common_divisor (MAX_LUMA_ARGS (minfo));
  DIVIDE_ARGS (max_luma, gcd);

  gcd = gst_util_greatest_common_divisor (MIN_LUMA_ARGS (minfo));
  DIVIDE_ARGS (min_luma, gcd);
}

/**
 * gst_video_mastering_display_info_from_string:
 * @minfo: (out): a #GstVideoMasteringDisplayInfo
 * @mastering: a #GstStructure representing #GstVideoMasteringDisplayInfo
 *
 * Extract #GstVideoMasteringDisplayInfo from @mastering
 *
 * Returns: %TRUE if @minfo was filled with @mastering
 *
 * Since: 1.18
 */
gboolean
    gst_video_mastering_display_info_from_string
    (GstVideoMasteringDisplayInfo * minfo, const gchar * mastering)
{
  GstVideoMasteringDisplayInfo tmp;

  g_return_val_if_fail (minfo != NULL, FALSE);
  g_return_val_if_fail (mastering != NULL, FALSE);

  if (sscanf (mastering, MASTERING_FORMAT,
          MASTERING_SCANF_ARGS (&tmp)) == N_ELEMENT_MASTERING_DISPLAY_INFO &&
      gst_video_mastering_display_info_is_valid (&tmp)) {
    gst_video_mastering_display_info_normalize (&tmp);
    *minfo = tmp;
    return TRUE;
  }

  return FALSE;
}

/**
 * gst_video_mastering_display_info_to_string:
 * @minfo: a #GstVideoMasteringDisplayInfo
 *
 * Convert @minfo to its string representation
 *
 * Returns: (transfer full) (nullable): a string representation of @minfo
 * or %NULL if @minfo has invalid chromaticity and/or luminance values
 *
 * Since: 1.18
 */
gchar *
gst_video_mastering_display_info_to_string (const
    GstVideoMasteringDisplayInfo * minfo)
{
  GstVideoMasteringDisplayInfo copy;

  g_return_val_if_fail (minfo != NULL, NULL);

  if (!gst_video_mastering_display_info_is_valid (minfo))
    return NULL;

  copy = *minfo;
  gst_video_mastering_display_info_normalize (&copy);

  return g_strdup_printf (MASTERING_FORMAT, MASTERING_PRINTF_ARGS (&copy));
}

/**
 * gst_video_mastering_display_info_is_equal:
 * @minfo: a #GstVideoMasteringDisplayInfo
 * @other: a #GstVideoMasteringDisplayInfo
 *
 * Checks equality between @minfo and @other.
 *
 * Returns: %TRUE if @minfo and @other are equal.
 *
 * Since: 1.18
 */
gboolean
gst_video_mastering_display_info_is_equal (const
    GstVideoMasteringDisplayInfo * minfo,
    const GstVideoMasteringDisplayInfo * other)
{
  if (gst_util_fraction_compare (RX_ARGS (minfo), RX_ARGS (other)) ||
      gst_util_fraction_compare (RY_ARGS (minfo), RY_ARGS (other)) ||
      gst_util_fraction_compare (GX_ARGS (minfo), GX_ARGS (other)) ||
      gst_util_fraction_compare (GY_ARGS (minfo), GY_ARGS (other)) ||
      gst_util_fraction_compare (BX_ARGS (minfo), BX_ARGS (other)) ||
      gst_util_fraction_compare (BY_ARGS (minfo), BY_ARGS (other)) ||
      gst_util_fraction_compare (WX_ARGS (minfo), WX_ARGS (other)) ||
      gst_util_fraction_compare (WY_ARGS (minfo), WY_ARGS (other)) ||
      gst_util_fraction_compare (MAX_LUMA_ARGS (minfo), MAX_LUMA_ARGS (other))
      || gst_util_fraction_compare (MIN_LUMA_ARGS (minfo),
          MIN_LUMA_ARGS (other)))
    return FALSE;

  return TRUE;
}

/**
 * gst_video_mastering_display_info_is_valid:
 * @minfo: a #GstVideoMasteringDisplayInfo
 *
 * Checks the minumum validity of @mininfo (not theoretical validation).
 *
 * Each x and y chromaticity coordinate should be in the range of [0, 1]
 * min_luma should be less than max_luma.
 *
 * Returns: %TRUE if @minfo satisfies the condition.
 *
 * Since: 1.18
 */
gboolean
gst_video_mastering_display_info_is_valid (const GstVideoMasteringDisplayInfo *
    minfo)
{
  GstVideoMasteringDisplayInfo other;

  gst_video_mastering_display_info_init (&other);

  if (!memcmp (minfo, &other, sizeof (GstVideoMasteringDisplayInfo)))
    return FALSE;

  /* should be valid fraction */
  if (!minfo->Rx_d || !minfo->Ry_d || !minfo->Gx_d || !minfo->Gy_d ||
      !minfo->Bx_d || !minfo->By_d || !minfo->Wx_d || !minfo->Wy_d ||
      !minfo->max_luma_d || !minfo->min_luma_d)
    return FALSE;

  /* should be less than one */
  if (gst_util_fraction_compare (RX_ARGS (minfo), 1, 1) > 0 ||
      gst_util_fraction_compare (RY_ARGS (minfo), 1, 1) > 0 ||
      gst_util_fraction_compare (GX_ARGS (minfo), 1, 1) > 0 ||
      gst_util_fraction_compare (GY_ARGS (minfo), 1, 1) > 0 ||
      gst_util_fraction_compare (BX_ARGS (minfo), 1, 1) > 0 ||
      gst_util_fraction_compare (BY_ARGS (minfo), 1, 1) > 0 ||
      gst_util_fraction_compare (WX_ARGS (minfo), 1, 1) > 0 ||
      gst_util_fraction_compare (WY_ARGS (minfo), 1, 1) > 0)
    return FALSE;

  if (gst_util_fraction_compare (MAX_LUMA_ARGS (minfo),
          MIN_LUMA_ARGS (minfo)) <= 0)
    return FALSE;

  return TRUE;
}

/**
 * gst_video_mastering_display_info_from_caps:
 * @minfo: a #GstVideoMasteringDisplayInfo
 * @caps: a #GstCaps
 *
 * Parse @caps and update @minfo
 *
 * Returns: %TRUE if @caps has #GstVideoMasteringDisplayInfo and could be parsed
 *
 * Since: 1.18
 */
gboolean
gst_video_mastering_display_info_from_caps (GstVideoMasteringDisplayInfo *
    minfo, const GstCaps * caps)
{
  GstStructure *structure;
  const gchar *s;

  g_return_val_if_fail (minfo != NULL, FALSE);
  g_return_val_if_fail (GST_IS_CAPS (caps), FALSE);

  structure = gst_caps_get_structure (caps, 0);

  if ((s = gst_structure_get_string (structure,
              "mastering-display-info")) == NULL)
    return FALSE;

  return gst_video_mastering_display_info_from_string (minfo, s);
}

/**
 * gst_video_mastering_display_info_add_to_caps:
 * @minfo: a #GstVideoMasteringDisplayInfo
 * @caps: a #GstCaps
 *
 * Set string representation of @minfo to @caps
 *
 * Returns: %TRUE if @minfo was successfully set to @caps
 *
 * Since: 1.18
 */
gboolean
gst_video_mastering_display_info_add_to_caps (const GstVideoMasteringDisplayInfo
    * minfo, GstCaps * caps)
{
  gchar *s;

  g_return_val_if_fail (minfo != NULL, FALSE);
  g_return_val_if_fail (GST_IS_CAPS (caps), FALSE);
  g_return_val_if_fail (gst_caps_is_writable (caps), FALSE);

  s = gst_video_mastering_display_info_to_string (minfo);
  if (!s)
    return FALSE;

  gst_caps_set_simple (caps, "mastering-display-info", G_TYPE_STRING, s, NULL);
  g_free (s);

  return TRUE;
}

/**
 * gst_video_content_light_level_init:
 * @linfo: a #GstVideoContentLightLevel
 *
 * Initialize @linfo
 *
 * Since: 1.18
 */
void
gst_video_content_light_level_init (GstVideoContentLightLevel * linfo)
{
  g_return_if_fail (linfo != NULL);

  memset (linfo, 0, sizeof (GstVideoContentLightLevel));
}

/**
 * gst_video_content_light_level_from_string:
 * @linfo: a #GstVideoContentLightLevel
 * @level: a content-light-level string from caps
 *
 * Parse the value of content-light-level caps field and update @minfo
 * with the parsed values.
 *
 * Returns: %TRUE if @linfo points to valid #GstVideoContentLightLevel.
 *
 * Since: 1.18
 */
gboolean
gst_video_content_light_level_from_string (GstVideoContentLightLevel *
    linfo, const gchar * level)
{
  guint maxCLL_n, maxCLL_d;
  guint maxFALL_n, maxFALL_d;

  g_return_val_if_fail (linfo != NULL, FALSE);
  g_return_val_if_fail (level != NULL, FALSE);

  if (sscanf (level, "%u:%u:%u:%u", &maxCLL_n, &maxCLL_d, &maxFALL_n,
          &maxFALL_d) == 4 && maxCLL_d != 0 && maxFALL_d != 0) {
    linfo->maxCLL_n = maxCLL_n;
    linfo->maxCLL_d = maxCLL_d;
    linfo->maxFALL_n = maxFALL_n;
    linfo->maxFALL_d = maxFALL_d;
    return TRUE;
  }

  return FALSE;
}

/**
 * gst_video_content_light_level_to_string:
 * @linfo: a #GstVideoContentLightLevel
 * Convert @linfo to its string representation.
 *
 * Returns: (transfer full): a string representation of @linfo.
 *
 * Since: 1.18
 */
gchar *
gst_video_content_light_level_to_string (const GstVideoContentLightLevel *
    linfo)
{
  g_return_val_if_fail (linfo != NULL, NULL);

  /* When maxCLL and/or maxFALL is zero, it means no upper bound is indicated.
   * But at least it should be valid fraction value */
  g_return_val_if_fail (linfo->maxCLL_d != 0 && linfo->maxFALL_d != 0, NULL);

  return g_strdup_printf ("%u:%u:%u:%u",
      linfo->maxCLL_n, linfo->maxCLL_d, linfo->maxFALL_n, linfo->maxFALL_d);
}

/**
 * gst_video_content_light_level_from_caps:
 * @linfo: a #GstVideoContentLightLevel
 * @caps: a #GstCaps
 *
 * Parse @caps and update @linfo
 *
 * Returns: if @caps has #GstVideoContentLightLevel and could be parsed
 *
 * Since: 1.18
 */
gboolean
gst_video_content_light_level_from_caps (GstVideoContentLightLevel * linfo,
    const GstCaps * caps)
{
  GstStructure *structure;
  const gchar *s;

  g_return_val_if_fail (linfo != NULL, FALSE);
  g_return_val_if_fail (GST_IS_CAPS (caps), FALSE);

  structure = gst_caps_get_structure (caps, 0);

  if ((s = gst_structure_get_string (structure, "content-light-level")) == NULL)
    return FALSE;

  return gst_video_content_light_level_from_string (linfo, s);
}

/**
 * gst_video_content_light_level_add_to_caps:
 * @linfo: a #GstVideoContentLightLevel
 * @caps: a #GstCaps
 *
 * Parse @caps and update @linfo
 *
 * Returns: %TRUE if @linfo was successfully set to @caps
 *
 * Since: 1.18
 */
gboolean
gst_video_content_light_level_add_to_caps (const GstVideoContentLightLevel *
    linfo, GstCaps * caps)
{
  gchar *s;

  g_return_val_if_fail (linfo != NULL, FALSE);
  g_return_val_if_fail (GST_IS_CAPS (caps), FALSE);
  g_return_val_if_fail (gst_caps_is_writable (caps), FALSE);

  s = gst_video_content_light_level_to_string (linfo);
  gst_caps_set_simple (caps, "content-light-level", G_TYPE_STRING, s, NULL);
  g_free (s);

  return TRUE;
}
