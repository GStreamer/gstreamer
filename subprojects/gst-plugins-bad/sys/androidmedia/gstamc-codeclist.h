/*
 * Copyright (C) 2012,2018 Collabora Ltd.
 *   Author: Sebastian Dröge <sebastian.droege@collabora.co.uk>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation
 * version 2.1 of the License.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 *
 */

#ifndef __GST_AMC_CODECLIST_H__
#define __GST_AMC_CODECLIST_H__

#include <gst/gst.h>

G_BEGIN_DECLS

typedef struct _GstAmcCodecInfoHandle GstAmcCodecInfoHandle;
typedef struct _GstAmcCodecCapabilitiesHandle GstAmcCodecCapabilitiesHandle;
typedef struct _GstAmcCodecProfileLevel GstAmcCodecProfileLevel;

typedef struct _GstAmcVideoCapabilitiesHandle GstAmcVideoCapabilitiesHandle;

struct _GstAmcCodecProfileLevel
{
  gint profile;
  gint level;
};

typedef struct _GstAmcValueRange GstAmcValueRange;

struct _GstAmcValueRange
{
  gint lower;
  gint upper;
};

typedef struct _GstAmcDoubleRange GstAmcDoubleRange;

struct _GstAmcDoubleRange
{
  gdouble lower;
  gdouble upper;
};

gboolean gst_amc_codeclist_get_count (gint * count, GError **err);
GstAmcCodecInfoHandle * gst_amc_codeclist_get_codec_info_at (gint index,
    GError **err);

void gst_amc_codec_info_handle_free (GstAmcCodecInfoHandle * handle);
gchar * gst_amc_codec_info_handle_get_name (GstAmcCodecInfoHandle * handle,
    GError ** err);
gboolean gst_amc_codec_info_handle_is_encoder (GstAmcCodecInfoHandle * handle,
    gboolean * is_encoder, GError ** err);
gboolean gst_amc_codec_info_handle_is_hardware_accelerated (GstAmcCodecInfoHandle * handle,
    gboolean * is_hardware_accelerated, GError ** err);
gchar ** gst_amc_codec_info_handle_get_supported_types (
    GstAmcCodecInfoHandle * handle, gsize * length, GError ** err);
GstAmcCodecCapabilitiesHandle * gst_amc_codec_info_handle_get_capabilities_for_type (
    GstAmcCodecInfoHandle * handle, const gchar * type, GError ** err);

void gst_amc_codec_capabilities_handle_free (
    GstAmcCodecCapabilitiesHandle * handle);
gint * gst_amc_codec_capabilities_handle_get_color_formats (
    GstAmcCodecCapabilitiesHandle * handle, gsize * length, GError ** err);
GstAmcCodecProfileLevel * gst_amc_codec_capabilities_handle_get_profile_levels (
    GstAmcCodecCapabilitiesHandle * handle, gsize * length, GError ** err);

GstAmcVideoCapabilitiesHandle * gst_amc_capabilities_get_video_capabilities (GstAmcCodecCapabilitiesHandle * handle,
    GError **err);
void gst_amc_capabilities_video_capabilities_handle_free (GstAmcVideoCapabilitiesHandle * handle);
GstAmcValueRange gst_amc_video_capabilities_get_supported_widths (
  GstAmcVideoCapabilitiesHandle * handle, GError ** err);
GstAmcValueRange gst_amc_video_capabilities_get_supported_heights (
  GstAmcVideoCapabilitiesHandle * handle, GError ** err);
GstAmcValueRange gst_amc_video_capabilities_get_supported_framerates (
  GstAmcVideoCapabilitiesHandle * handle, GError ** err);
GstAmcValueRange gst_amc_video_capabilities_get_supported_widths_for (
  GstAmcVideoCapabilitiesHandle * handle, gint height, GError ** err);
GstAmcValueRange gst_amc_video_capabilities_get_supported_heights_for (
  GstAmcVideoCapabilitiesHandle * handle, gint width, GError ** err);
GstAmcDoubleRange gst_amc_video_capabilities_get_achievable_framerates_for (
  GstAmcVideoCapabilitiesHandle * handle, gint width, gint height, GError ** err);

G_END_DECLS

#endif /* __GST_AMC_CODECLIST_H__ */
