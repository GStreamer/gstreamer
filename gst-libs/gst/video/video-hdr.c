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
#include <gst/base/gstbitreader.h>

#include "video-hdr.h"

#define HDR10_PLUS_MAX_BEZIER_CURVE_ANCHORS 9
#define HDR10_PLUS_MAX_DIST_MAXRGB_PERCENTILES 9

#define N_ELEMENT_MASTERING_DISPLAY_INFO 10
#define MASTERING_FORMAT \
  "%d:%d:" \
  "%d:%d:" \
  "%d:%d:" \
  "%d:%d:" \
  "%d:%d"

#define MASTERING_PRINTF_ARGS(m) \
  (m)->display_primaries[0].x, (m)->display_primaries[0].y, \
  (m)->display_primaries[1].x, (m)->display_primaries[1].y, \
  (m)->display_primaries[2].x, (m)->display_primaries[2].y, \
  (m)->white_point.x, (m)->white_point.y, \
  (m)->max_display_mastering_luminance, \
  (m)->min_display_mastering_luminance

/**
 * gst_video_hdr_format_to_string:
 * @format: a #GstVideoHDRFormat
 *
 * Returns: (nullable): a string containing a descriptive name for
 * the #GstVideoHDRFormat if there is one, or %NULL otherwise.
 *
 * Since: 1.20
 */
const gchar *
gst_video_hdr_format_to_string (GstVideoHDRFormat format)
{
  switch (format) {
    case GST_VIDEO_HDR_FORMAT_HDR10:
      return "hdr10";
    case GST_VIDEO_HDR_FORMAT_HDR10_PLUS:
      return "hdr10+";
    default:
      return NULL;
  }
}

/**
 * gst_video_hdr_format_from_string:
 * @format: (nullable): a #GstVideoHDRFormat
 *
 * Returns: the #GstVideoHDRFormat for @format or GST_VIDEO_HDR_FORMAT_NONE when the
 * string is not a known format.
 *
 * Since: 1.20
 */
GstVideoHDRFormat
gst_video_hdr_format_from_string (const gchar * format)
{
  if (!g_strcmp0 (format, "hdr10"))
    return GST_VIDEO_HDR_FORMAT_HDR10;
  else if (!g_strcmp0 (format, "hdr10+"))
    return GST_VIDEO_HDR_FORMAT_HDR10_PLUS;

  return GST_VIDEO_HDR_FORMAT_NONE;
}

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
gst_video_mastering_display_info_from_string (GstVideoMasteringDisplayInfo *
    minfo, const gchar * mastering)
{
  gboolean ret = FALSE;
  gchar **split;
  gint i;
  gint idx = 0;
  guint64 val;

  g_return_val_if_fail (minfo != NULL, FALSE);
  g_return_val_if_fail (mastering != NULL, FALSE);

  split = g_strsplit (mastering, ":", -1);

  if (g_strv_length (split) != N_ELEMENT_MASTERING_DISPLAY_INFO)
    goto out;

  for (i = 0; i < G_N_ELEMENTS (minfo->display_primaries); i++) {
    if (!g_ascii_string_to_unsigned (split[idx++],
            10, 0, G_MAXUINT16, &val, NULL))
      goto out;

    minfo->display_primaries[i].x = (guint16) val;

    if (!g_ascii_string_to_unsigned (split[idx++],
            10, 0, G_MAXUINT16, &val, NULL))
      goto out;

    minfo->display_primaries[i].y = (guint16) val;
  }

  if (!g_ascii_string_to_unsigned (split[idx++],
          10, 0, G_MAXUINT16, &val, NULL))
    goto out;

  minfo->white_point.x = (guint16) val;

  if (!g_ascii_string_to_unsigned (split[idx++],
          10, 0, G_MAXUINT16, &val, NULL))
    goto out;

  minfo->white_point.y = (guint16) val;

  if (!g_ascii_string_to_unsigned (split[idx++],
          10, 0, G_MAXUINT32, &val, NULL))
    goto out;

  minfo->max_display_mastering_luminance = (guint32) val;

  if (!g_ascii_string_to_unsigned (split[idx++],
          10, 0, G_MAXUINT32, &val, NULL))
    goto out;

  minfo->min_display_mastering_luminance = (guint32) val;
  ret = TRUE;

out:
  g_strfreev (split);
  if (!ret)
    gst_video_mastering_display_info_init (minfo);

  return ret;
}

/**
 * gst_video_mastering_display_info_to_string:
 * @minfo: a #GstVideoMasteringDisplayInfo
 *
 * Convert @minfo to its string representation
 *
 * Returns: (transfer full): a string representation of @minfo
 *
 * Since: 1.18
 */
gchar *
gst_video_mastering_display_info_to_string (const GstVideoMasteringDisplayInfo *
    minfo)
{
  g_return_val_if_fail (minfo != NULL, NULL);

  return g_strdup_printf (MASTERING_FORMAT, MASTERING_PRINTF_ARGS (minfo));
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
gst_video_mastering_display_info_is_equal (const GstVideoMasteringDisplayInfo *
    minfo, const GstVideoMasteringDisplayInfo * other)
{
  gint i;

  g_return_val_if_fail (minfo != NULL, FALSE);
  g_return_val_if_fail (other != NULL, FALSE);

  for (i = 0; i < G_N_ELEMENTS (minfo->display_primaries); i++) {
    if (minfo->display_primaries[i].x != other->display_primaries[i].x ||
        minfo->display_primaries[i].y != other->display_primaries[i].y)
      return FALSE;
  }

  if (minfo->white_point.x != other->white_point.x ||
      minfo->white_point.y != other->white_point.y ||
      minfo->max_display_mastering_luminance !=
      other->max_display_mastering_luminance
      || minfo->min_display_mastering_luminance !=
      other->min_display_mastering_luminance)
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
gst_video_content_light_level_from_string (GstVideoContentLightLevel * linfo,
    const gchar * level)
{
  gboolean ret = FALSE;
  gchar **split;
  guint64 val;

  g_return_val_if_fail (linfo != NULL, FALSE);
  g_return_val_if_fail (level != NULL, FALSE);

  split = g_strsplit (level, ":", -1);

  if (g_strv_length (split) != 2)
    goto out;

  if (!g_ascii_string_to_unsigned (split[0], 10, 0, G_MAXUINT16, &val, NULL))
    goto out;

  linfo->max_content_light_level = (guint16) val;

  if (!g_ascii_string_to_unsigned (split[1], 10, 0, G_MAXUINT16, &val, NULL))
    goto out;

  linfo->max_frame_average_light_level = (guint16) val;

  ret = TRUE;

out:
  g_strfreev (split);
  if (!ret)
    gst_video_content_light_level_init (linfo);

  return ret;
}

/**
 * gst_video_content_light_level_to_string:
 * @linfo: a #GstVideoContentLightLevel
 *
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

  return g_strdup_printf ("%d:%d",
      linfo->max_content_light_level, linfo->max_frame_average_light_level);
}

/**
 * gst_video_content_light_level_is_equal:
 * @linfo: a #GstVideoContentLightLevel
 * @other: a #GstVideoContentLightLevel
 *
 * Checks equality between @linfo and @other.
 *
 * Returns: %TRUE if @linfo and @other are equal.
 *
 * Since: 1.20
 */
gboolean
gst_video_content_light_level_is_equal (const GstVideoContentLightLevel * linfo,
    const GstVideoContentLightLevel * other)
{
  g_return_val_if_fail (linfo != NULL, FALSE);
  g_return_val_if_fail (other != NULL, FALSE);

  return (linfo->max_content_light_level == other->max_content_light_level &&
      linfo->max_frame_average_light_level ==
      other->max_frame_average_light_level);
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

/* Dynamic HDR Meta implementation */

GType
gst_video_hdr_meta_api_get_type (void)
{
  static GType type = 0;

  if (g_once_init_enter (&type)) {
    static const gchar *tags[] = {
      GST_META_TAG_VIDEO_STR,
      NULL
    };
    GType _type = gst_meta_api_type_register ("GstVideoHDRMetaAPI", tags);
    g_once_init_leave (&type, _type);
  }
  return type;
}

static gboolean
gst_video_hdr_meta_transform (GstBuffer * dest, GstMeta * meta,
    GstBuffer * buffer, GQuark type, gpointer data)
{
  GstVideoHDRMeta *dmeta, *smeta;

  /* We always copy over the caption meta */
  smeta = (GstVideoHDRMeta *) meta;

  GST_DEBUG ("copy HDR metadata");
  dmeta =
      gst_buffer_add_video_hdr_meta (dest, smeta->format, smeta->data,
      smeta->size);
  if (!dmeta)
    return FALSE;

  return TRUE;
}

static gboolean
gst_video_hdr_meta_init (GstMeta * meta, gpointer params, GstBuffer * buffer)
{
  GstVideoHDRMeta *emeta = (GstVideoHDRMeta *) meta;

  emeta->data = NULL;

  return TRUE;
}

static void
gst_video_hdr_meta_free (GstMeta * meta, GstBuffer * buffer)
{
  GstVideoHDRMeta *emeta = (GstVideoHDRMeta *) meta;

  g_free (emeta->data);
}

const GstMetaInfo *
gst_video_hdr_meta_get_info (void)
{
  static const GstMetaInfo *meta_info = NULL;

  if (g_once_init_enter ((GstMetaInfo **) & meta_info)) {
    const GstMetaInfo *mi = gst_meta_register (GST_VIDEO_HDR_META_API_TYPE,
        "GstVideoHDRMeta",
        sizeof (GstVideoHDRMeta),
        gst_video_hdr_meta_init,
        gst_video_hdr_meta_free,
        gst_video_hdr_meta_transform);
    g_once_init_leave ((GstMetaInfo **) & meta_info, (GstMetaInfo *) mi);
  }
  return meta_info;
}

/**
 * gst_buffer_add_video_hdr_meta:
 * @buffer: a #GstBuffer
 * @format: The type of dynamic HDR contained in the meta.
 * @data: contains the dynamic HDR data
 * @size: The size in bytes of @data
 *
 * Attaches #GstVideoHDRMeta metadata to @buffer with the given
 * parameters.
 *
 * Returns: (transfer none): the #GstVideoHDRMeta on @buffer.
 *
 * Since: 1.20
 */
GstVideoHDRMeta *
gst_buffer_add_video_hdr_meta (GstBuffer * buffer,
    GstVideoHDRFormat format, const guint8 * data, gsize size)
{
  GstVideoHDRMeta *meta;

  g_return_val_if_fail (GST_IS_BUFFER (buffer), NULL);
  g_return_val_if_fail (data != NULL, NULL);

  meta = (GstVideoHDRMeta *) gst_buffer_add_meta (buffer,
      GST_VIDEO_HDR_META_INFO, NULL);
  g_assert (meta != NULL);

  meta->format = format;
  meta->data = g_memdup (data, size);
  meta->size = size;

  return meta;
}

#define CHECK_HDR10PLUS_REMAINING(br, needed) \
if (gst_bit_reader_get_remaining (&br) < needed) { \
  GST_DEBUG ("Not enough bits remaining %d, needed %d", gst_bit_reader_get_remaining (&br), needed); \
  return FALSE; \
}

/**
 * gst_video_hdr_parse_hdr10_plus:
 * @data: HDR10+ data
 * @size: size of data
 * @hdr10_plus: (out): #GstVideoHDR10Plus structure to fill in.
 *
 * Parse HDR10+ (SMPTE2094-40) user data and store in @hdr10_plus
 * For more details, see:
 * https://www.atsc.org/wp-content/uploads/2018/02/S34-301r2-A341-Amendment-2094-40-1.pdf
 * and SMPTE ST2094-40
 *
 * Returns: %TRUE if @data was successfully parsed to @hdr10_plus
 *
 * Since: 1.20
 */
gboolean
gst_video_hdr_parse_hdr10_plus (const guint8 * data, gsize size,
    GstVideoHDR10Plus * hdr10_plus)
{
  guint16 provider_oriented_code;
  int w, i, j;
  GstBitReader br;

  /* there must be at least one byte, and not more than GST_VIDEO_HDR10_PLUS_MAX_BYTES bytes */
  g_return_val_if_fail (data != NULL, FALSE);

  memset (hdr10_plus, 0, sizeof (GstVideoHDR10Plus));
  gst_bit_reader_init (&br, data, size);
  GST_MEMDUMP ("HDR10+", data, size);
  CHECK_HDR10PLUS_REMAINING (br, 2 + 8 + 8 + 2);
  provider_oriented_code = gst_bit_reader_get_bits_uint16_unchecked (&br, 16);
  if (provider_oriented_code != 0x0001)
    return FALSE;


  hdr10_plus->application_identifier =
      gst_bit_reader_get_bits_uint8_unchecked (&br, 8);
  hdr10_plus->application_version =
      gst_bit_reader_get_bits_uint8_unchecked (&br, 8);
  hdr10_plus->num_windows = gst_bit_reader_get_bits_uint8_unchecked (&br, 2);
  if (hdr10_plus->num_windows != GST_VIDEO_HDR10_PLUS_NUM_WINDOWS)
    return FALSE;
  for (w = 0; w < hdr10_plus->num_windows; w++) {
    CHECK_HDR10PLUS_REMAINING (br,
        16 + 16 + 16 + 16 + 16 + 16 + 8 + 16 + 16 + 16 + 1);
    hdr10_plus->processing_window[w].window_upper_left_corner_x =
        gst_bit_reader_get_bits_uint16_unchecked (&br, 16);
    hdr10_plus->processing_window[w].window_upper_left_corner_y =
        gst_bit_reader_get_bits_uint16_unchecked (&br, 16);
    hdr10_plus->processing_window[w].window_lower_right_corner_x =
        gst_bit_reader_get_bits_uint16_unchecked (&br, 16);
    hdr10_plus->processing_window[w].window_lower_right_corner_y =
        gst_bit_reader_get_bits_uint16_unchecked (&br, 16);
    hdr10_plus->processing_window[w].center_of_ellipse_x =
        gst_bit_reader_get_bits_uint16_unchecked (&br, 16);
    hdr10_plus->processing_window[w].center_of_ellipse_y =
        gst_bit_reader_get_bits_uint16_unchecked (&br, 16);
    hdr10_plus->processing_window[w].rotation_angle =
        gst_bit_reader_get_bits_uint8_unchecked (&br, 8);
    hdr10_plus->processing_window[w].semimajor_axis_internal_ellipse =
        gst_bit_reader_get_bits_uint16_unchecked (&br, 16);
    hdr10_plus->processing_window[w].semimajor_axis_external_ellipse =
        gst_bit_reader_get_bits_uint16_unchecked (&br, 16);
    hdr10_plus->processing_window[w].semiminor_axis_external_ellipse =
        gst_bit_reader_get_bits_uint16_unchecked (&br, 16);
    hdr10_plus->processing_window[w].overlap_process_option =
        gst_bit_reader_get_bits_uint8_unchecked (&br, 1);
  }
  CHECK_HDR10PLUS_REMAINING (br, 27 + 1);
  hdr10_plus->targeted_system_display_maximum_luminance =
      gst_bit_reader_get_bits_uint32_unchecked (&br, 27);
  hdr10_plus->targeted_system_display_actual_peak_luminance_flag =
      gst_bit_reader_get_bits_uint8_unchecked (&br, 1);
  if (hdr10_plus->targeted_system_display_actual_peak_luminance_flag) {
    CHECK_HDR10PLUS_REMAINING (br, 5 + 5);
    hdr10_plus->num_rows_targeted_system_display_actual_peak_luminance =
        gst_bit_reader_get_bits_uint8_unchecked (&br, 5);
    hdr10_plus->num_cols_targeted_system_display_actual_peak_luminance =
        gst_bit_reader_get_bits_uint8_unchecked (&br, 5);
    if (hdr10_plus->num_rows_targeted_system_display_actual_peak_luminance >
        GST_VIDEO_HDR10_PLUS_MAX_ROWS_TSD_APL)
      return FALSE;
    if (hdr10_plus->num_cols_targeted_system_display_actual_peak_luminance >
        GST_VIDEO_HDR10_PLUS_MAX_COLS_MD_APL)
      return FALSE;
    CHECK_HDR10PLUS_REMAINING (br,
        hdr10_plus->num_rows_targeted_system_display_actual_peak_luminance *
        hdr10_plus->num_cols_targeted_system_display_actual_peak_luminance * 4);
    for (i = 0;
        i < hdr10_plus->num_rows_targeted_system_display_actual_peak_luminance;
        i++) {
      for (j = 0;
          j <
          hdr10_plus->num_cols_targeted_system_display_actual_peak_luminance;
          j++)
        hdr10_plus->targeted_system_display_actual_peak_luminance[i][j] =
            gst_bit_reader_get_bits_uint8_unchecked (&br, 4);
    }
    for (w = 0; w < hdr10_plus->num_windows; w++) {
      CHECK_HDR10PLUS_REMAINING (br, (17 * 3));
      for (i = 0; i < 3; i++)
        hdr10_plus->processing_window[w].maxscl[i] =
            gst_bit_reader_get_bits_uint32_unchecked (&br, 17);
      CHECK_HDR10PLUS_REMAINING (br, 17 + 4);
      hdr10_plus->processing_window[w].average_maxrgb =
          gst_bit_reader_get_bits_uint32_unchecked (&br, 17);
      hdr10_plus->processing_window[w].num_distribution_maxrgb_percentiles =
          gst_bit_reader_get_bits_uint8_unchecked (&br, 4);
      if (hdr10_plus->processing_window[w].
          num_distribution_maxrgb_percentiles !=
          HDR10_PLUS_MAX_DIST_MAXRGB_PERCENTILES)
        return FALSE;
      CHECK_HDR10PLUS_REMAINING (br,
          hdr10_plus->processing_window[w].num_distribution_maxrgb_percentiles *
          (17 + 7));
      for (i = 0;
          i <
          hdr10_plus->processing_window[w].num_distribution_maxrgb_percentiles;
          i++) {
        hdr10_plus->processing_window[w].distribution_maxrgb_percentages[i] =
            gst_bit_reader_get_bits_uint8_unchecked (&br, 7);
        hdr10_plus->processing_window[w].distribution_maxrgb_percentiles[i] =
            gst_bit_reader_get_bits_uint32_unchecked (&br, 17);
      }
      CHECK_HDR10PLUS_REMAINING (br, 10)
          hdr10_plus->processing_window[w].fraction_bright_pixels =
          gst_bit_reader_get_bits_uint16_unchecked (&br, 10);
    }
  }
  CHECK_HDR10PLUS_REMAINING (br, 1)
      hdr10_plus->mastering_display_actual_peak_luminance_flag =
      gst_bit_reader_get_bits_uint8_unchecked (&br, 1);
  if (hdr10_plus->targeted_system_display_actual_peak_luminance_flag) {
    CHECK_HDR10PLUS_REMAINING (br, 5 + 5)
        hdr10_plus->num_rows_mastering_display_actual_peak_luminance =
        gst_bit_reader_get_bits_uint8_unchecked (&br, 5);
    hdr10_plus->num_cols_mastering_display_actual_peak_luminance =
        gst_bit_reader_get_bits_uint8_unchecked (&br, 5);
    if (hdr10_plus->num_rows_mastering_display_actual_peak_luminance >
        GST_VIDEO_HDR10_PLUS_MAX_ROWS_TSD_APL)
      return FALSE;
    if (hdr10_plus->num_cols_mastering_display_actual_peak_luminance >
        GST_VIDEO_HDR10_PLUS_MAX_COLS_MD_APL)
      return FALSE;
    CHECK_HDR10PLUS_REMAINING (br,
        hdr10_plus->num_rows_mastering_display_actual_peak_luminance *
        hdr10_plus->num_cols_mastering_display_actual_peak_luminance * 4)
        for (i = 0;
        i < hdr10_plus->num_rows_mastering_display_actual_peak_luminance; i++) {
      for (j = 0;
          j < hdr10_plus->num_cols_mastering_display_actual_peak_luminance; j++)
        hdr10_plus->mastering_display_actual_peak_luminance[i][j] =
            gst_bit_reader_get_bits_uint8_unchecked (&br, 4);
    }
    for (w = 0; w < hdr10_plus->num_windows; w++) {
      CHECK_HDR10PLUS_REMAINING (br, 1)
          hdr10_plus->processing_window[w].tone_mapping_flag =
          gst_bit_reader_get_bits_uint8_unchecked (&br, 1);
      if (hdr10_plus->processing_window[w].tone_mapping_flag) {
        CHECK_HDR10PLUS_REMAINING (br, 12 + 12 + 4)
            hdr10_plus->processing_window[w].knee_point_x =
            gst_bit_reader_get_bits_uint16_unchecked (&br, 12);
        hdr10_plus->processing_window[w].knee_point_y =
            gst_bit_reader_get_bits_uint16_unchecked (&br, 12);
        hdr10_plus->processing_window[w].num_bezier_curve_anchors =
            gst_bit_reader_get_bits_uint8_unchecked (&br, 4);
        if (hdr10_plus->processing_window[w].num_bezier_curve_anchors >
            HDR10_PLUS_MAX_BEZIER_CURVE_ANCHORS)
          return FALSE;
        CHECK_HDR10PLUS_REMAINING (br,
            10 * hdr10_plus->processing_window[w].num_bezier_curve_anchors);
        for (i = 0;
            i < hdr10_plus->processing_window[w].num_bezier_curve_anchors; i++)
          hdr10_plus->processing_window[w].bezier_curve_anchors[i] =
              gst_bit_reader_get_bits_uint16_unchecked (&br, 10);
      }
      CHECK_HDR10PLUS_REMAINING (br, 1);
      hdr10_plus->processing_window[w].color_saturation_mapping_flag =
          gst_bit_reader_get_bits_uint8_unchecked (&br, 1);
      if (hdr10_plus->processing_window[w].color_saturation_mapping_flag) {
        CHECK_HDR10PLUS_REMAINING (br, 6);
        hdr10_plus->processing_window[w].color_saturation_weight =
            gst_bit_reader_get_bits_uint8_unchecked (&br, 6);
      }
    }
  }
  return TRUE;
}
