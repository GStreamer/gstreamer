/* GStreamer
 * Copyright (C) 2025 Igalia, S.L.
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
#include "config.h"
#endif

#include "gstvkvideocaps.h"
#include "gst/vulkan/gstvkvideo-private.h"

GST_DEBUG_CATEGORY_EXTERN (gst_vulkan_debug);
#define GST_CAT_DEFAULT gst_vulkan_debug

static gboolean
try_profile (GstVulkanPhysicalDevice * device, GstVulkanVideoProfile * profile,
    GstCaps ** codec_caps, GstCaps ** raw_caps)
{
  gboolean ret;
  GstVulkanVideoCapabilities vkcaps;
  GstCaps *codec, *raw = NULL;
  GArray *vkformats;
  GError *err = NULL;

  ret =
      gst_vulkan_video_try_configuration (device, profile, &vkcaps, &codec,
      &vkformats, &err);
  if (!ret) {
    GST_LOG ("Couldn't get configuration for 0x%x, %u [%d %d]: %s",
        profile->profile.videoCodecOperation,
        profile->profile.chromaSubsampling,
        profile->profile.chromaBitDepth, profile->profile.lumaBitDepth,
        err ? err->message : "Unknown error");
    g_clear_error (&err);
    return FALSE;
  }

  if (!codec || gst_caps_is_empty (codec)) {
    GST_DEBUG ("No codec caps could be generated");
    g_clear_pointer (&vkformats, g_array_unref);
    gst_clear_caps (&codec);
    return FALSE;
  }

  gst_caps_set_simple (codec, "width", GST_TYPE_INT_RANGE,
      vkcaps.caps.minCodedExtent.width, vkcaps.caps.maxCodedExtent.width,
      "height", GST_TYPE_INT_RANGE, vkcaps.caps.minCodedExtent.height,
      vkcaps.caps.maxCodedExtent.height, NULL);

  for (int i = 0; i < gst_caps_get_size (codec); i++) {
    GstStructure *st = gst_caps_get_structure (codec, i);

    /* these fields are removed because they aren't exposed by all the parsers
     * for negotiation, and no other decoder/encoder element exposes them in
     * their pad templates */
    gst_structure_remove_fields (st, "interlace-mode", "bit-depth-luma",
        "bit-depth-chroma", "chroma-format", "film-grain", NULL);
  }

  /* generate raw caps given the possible output formats */
  raw = gst_caps_new_empty ();
  for (int i = 0; i < vkformats->len; i++) {
    GstCaps *raw_next = NULL;
    VkVideoFormatPropertiesKHR *fmt =
        &g_array_index (vkformats, VkVideoFormatPropertiesKHR, i);
    GstVideoFormat format = gst_vulkan_format_to_video_format (fmt->format);

    if (format == GST_VIDEO_FORMAT_UNKNOWN) {
      GST_DEBUG ("Missing mapping to output format %u", fmt->format);
      continue;
    }

    raw_next = gst_caps_new_simple ("video/x-raw", "format", G_TYPE_STRING,
        gst_video_format_to_string (format), "width", GST_TYPE_INT_RANGE,
        vkcaps.caps.minCodedExtent.width, vkcaps.caps.maxCodedExtent.width,
        "height", GST_TYPE_INT_RANGE, vkcaps.caps.minCodedExtent.height,
        vkcaps.caps.maxCodedExtent.height, NULL);
    raw = gst_caps_merge (raw, raw_next);
  }

  g_array_unref (vkformats);

  if (gst_caps_is_empty (raw)) {
    gst_caps_unref (codec);
    gst_caps_unref (raw);
    GST_DEBUG ("Couldn't get configuration for %u, %u [%d %d]: %s",
        profile->profile.videoCodecOperation,
        profile->profile.chromaSubsampling,
        profile->profile.chromaBitDepth, profile->profile.lumaBitDepth,
        "Invalid output format");
    return FALSE;
  }

  *codec_caps = codec;
  *raw_caps = raw;

  return TRUE;
}

static void
build_profile (GstVulkanVideoProfile * profile,
    VkVideoCodecOperationFlagBitsKHR codec)
{
  /* *INDENT-OFF* */
  *profile = (GstVulkanVideoProfile) {
    .profile = {
      .sType = VK_STRUCTURE_TYPE_VIDEO_PROFILE_INFO_KHR,
      .pNext = &profile->usage,
      .videoCodecOperation = codec,
    }
  };

  if (GST_VULKAN_VIDEO_CODEC_OPERATION_IS_DECODE (codec)) {
    profile->usage.decode = (VkVideoDecodeUsageInfoKHR) {
      .sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_USAGE_INFO_KHR,
      .pNext = &profile->codec,
      .videoUsageHints = VK_VIDEO_DECODE_USAGE_DEFAULT_KHR,};
  } else if (GST_VULKAN_VIDEO_CODEC_OPERATION_IS_ENCODE (codec)) {
    profile->usage.encode = (VkVideoEncodeUsageInfoKHR) {
      .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_USAGE_INFO_KHR,
      .pNext = &profile->codec,
      .videoUsageHints = VK_VIDEO_ENCODE_USAGE_DEFAULT_KHR,
      .videoContentHints = VK_VIDEO_ENCODE_CONTENT_DEFAULT_KHR,
      .tuningMode = VK_VIDEO_ENCODE_TUNING_MODE_DEFAULT_KHR,
    };
  } else {
    g_assert_not_reached ();
  }
  /* *INDENT-ON* */
}

static const VkVideoChromaSubsamplingFlagBitsKHR chroma_map[] = {
  VK_VIDEO_CHROMA_SUBSAMPLING_MONOCHROME_BIT_KHR,
  VK_VIDEO_CHROMA_SUBSAMPLING_420_BIT_KHR,
  VK_VIDEO_CHROMA_SUBSAMPLING_422_BIT_KHR,
  VK_VIDEO_CHROMA_SUBSAMPLING_444_BIT_KHR,
};

static const VkVideoComponentBitDepthFlagsKHR bit_depth_map[] = {
  VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR,
  VK_VIDEO_COMPONENT_BIT_DEPTH_10_BIT_KHR,
  VK_VIDEO_COMPONENT_BIT_DEPTH_12_BIT_KHR
};

/* Try to generate GStreamer caps given the Vulkan profile. The caps can be
 * empty if function fails */
static inline void
try_get_caps (GstVulkanPhysicalDevice * device, GstVulkanVideoProfile * profile,
    GstCaps * codec_caps, GstCaps * raw_caps)
{
  for (int j = 0; j < G_N_ELEMENTS (chroma_map); j++) {
    profile->profile.chromaSubsampling = chroma_map[j];

    for (int k = 0; k < G_N_ELEMENTS (bit_depth_map); k++) {
      profile->profile.chromaBitDepth = bit_depth_map[k];
      for (int l = 0; l < G_N_ELEMENTS (bit_depth_map); l++) {
        profile->profile.lumaBitDepth = bit_depth_map[l];

        if (profile->profile.chromaSubsampling ==
            VK_VIDEO_CHROMA_SUBSAMPLING_MONOCHROME_BIT_KHR
            && profile->profile.chromaBitDepth != profile->profile.lumaBitDepth)
          continue;

        {
          GstCaps *codec = NULL, *raw = NULL;

          if (!try_profile (device, profile, &codec, &raw))
            continue;

          codec_caps = gst_caps_merge (codec_caps, codec);
          raw_caps = gst_caps_merge (raw_caps, raw);
        }
      }
    }
  }
}

static inline gboolean
check_caps (GstCaps ** codec_caps, GstCaps ** raw_caps)
{
  if (gst_caps_is_empty (*codec_caps) || gst_caps_is_empty (*raw_caps)) {
    gst_clear_caps (codec_caps);
    gst_clear_caps (raw_caps);
    return FALSE;
  }

  *codec_caps = gst_caps_simplify (*codec_caps);
  *raw_caps = gst_caps_simplify (*raw_caps);
  return TRUE;
}

static const StdVideoH264ProfileIdc h264_profile_idc[] = {
  STD_VIDEO_H264_PROFILE_IDC_HIGH, STD_VIDEO_H264_PROFILE_IDC_MAIN,
  STD_VIDEO_H264_PROFILE_IDC_BASELINE,
};

static const VkVideoDecodeH264PictureLayoutFlagBitsKHR h264_layout_map[] = {
  VK_VIDEO_DECODE_H264_PICTURE_LAYOUT_PROGRESSIVE_KHR,
  VK_VIDEO_DECODE_H264_PICTURE_LAYOUT_INTERLACED_INTERLEAVED_LINES_BIT_KHR,
  VK_VIDEO_DECODE_H264_PICTURE_LAYOUT_INTERLACED_SEPARATE_PLANES_BIT_KHR,
};

static void
h26x_complete_caps (GstCaps * caps, char **stream_formats)
{
  int i;
  GValue stream_format_value = G_VALUE_INIT;

  for (i = 0; stream_formats[i]; i++);

  if (i > 1) {
    g_value_init (&stream_format_value, GST_TYPE_LIST);
    for (int i = 0; stream_formats[i]; i++) {
      GValue value = G_VALUE_INIT;

      g_value_init (&value, G_TYPE_STRING);
      g_value_set_string (&value, stream_formats[i]);
      gst_value_list_append_value (&stream_format_value, &value);
      g_value_unset (&value);
    }
  } else {
    g_value_init (&stream_format_value, G_TYPE_STRING);
    g_value_set_string (&stream_format_value, stream_formats[0]);
  }

  gst_caps_set_value (caps, "stream-format", &stream_format_value);
  g_value_unset (&stream_format_value);
  gst_caps_set_simple (caps, "alignment", G_TYPE_STRING, "au", NULL);
}

static gboolean
h264_encode_caps (GstVulkanPhysicalDevice * device,
    GstVulkanVideoProfile * profile, GstCaps ** codec_caps_ptr,
    GstCaps ** raw_caps_ptr)
{
  GstCaps *codec_caps, *raw_caps;
  const char *stream_format[] = { "byte-stream", NULL };

  profile->codec.h264enc.sType =
      VK_STRUCTURE_TYPE_VIDEO_ENCODE_H264_PROFILE_INFO_KHR;

  codec_caps = gst_caps_new_empty ();
  raw_caps = gst_caps_new_empty ();

  for (int i = 0; i < G_N_ELEMENTS (h264_profile_idc); i++) {
    profile->codec.h264enc.stdProfileIdc = h264_profile_idc[i];

    try_get_caps (device, profile, codec_caps, raw_caps);
  }

  if (!check_caps (&codec_caps, &raw_caps))
    return FALSE;

  h26x_complete_caps (codec_caps, (char **) stream_format);

  gst_caps_set_features_simple (raw_caps,
      gst_caps_features_new (GST_CAPS_FEATURE_MEMORY_VULKAN_IMAGE, NULL));

  *codec_caps_ptr = codec_caps;
  *raw_caps_ptr = raw_caps;

  return TRUE;
}

static const StdVideoH265ProfileIdc h265_profile_idc[] = {
  STD_VIDEO_H265_PROFILE_IDC_MAIN, STD_VIDEO_H265_PROFILE_IDC_MAIN_10,
  STD_VIDEO_H265_PROFILE_IDC_MAIN_STILL_PICTURE,
  STD_VIDEO_H265_PROFILE_IDC_FORMAT_RANGE_EXTENSIONS,
  STD_VIDEO_H265_PROFILE_IDC_SCC_EXTENSIONS,
};

static gboolean
h265_encode_caps (GstVulkanPhysicalDevice * device,
    GstVulkanVideoProfile * profile, GstCaps ** codec_caps_ptr,
    GstCaps ** raw_caps_ptr)
{
  GstCaps *codec_caps, *raw_caps;
  const char *stream_format[] = { "byte-stream", NULL };

  profile->codec.h265enc.sType =
      VK_STRUCTURE_TYPE_VIDEO_ENCODE_H265_PROFILE_INFO_KHR;

  codec_caps = gst_caps_new_empty ();
  raw_caps = gst_caps_new_empty ();

  for (int i = 0; i < G_N_ELEMENTS (h265_profile_idc); i++) {
    profile->codec.h265enc.stdProfileIdc = h265_profile_idc[i];

    try_get_caps (device, profile, codec_caps, raw_caps);
  }

  if (!check_caps (&codec_caps, &raw_caps))
    return FALSE;

  h26x_complete_caps (codec_caps, (char **) stream_format);

  gst_caps_set_features_simple (raw_caps,
      gst_caps_features_new (GST_CAPS_FEATURE_MEMORY_VULKAN_IMAGE, NULL));

  *codec_caps_ptr = codec_caps;
  *raw_caps_ptr = raw_caps;

  return TRUE;
}

static gboolean
h264_decode_caps (GstVulkanPhysicalDevice * device,
    GstVulkanVideoProfile * profile, GstCaps ** codec_caps_ptr,
    GstCaps ** raw_caps_ptr)
{
  GstCaps *codec_caps, *raw_caps;
  const char *stream_format[] = { "avc", "byte-stream", NULL };

  profile->codec.h264dec.sType =
      VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_PROFILE_INFO_KHR;

  codec_caps = gst_caps_new_empty ();
  raw_caps = gst_caps_new_empty ();

  for (int i = 0; i < G_N_ELEMENTS (h264_profile_idc); i++) {
    profile->codec.h264dec.stdProfileIdc = h264_profile_idc[i];

    for (int j = 0; j < G_N_ELEMENTS (h264_layout_map); j++) {
      profile->codec.h264dec.pictureLayout = h264_layout_map[j];

      try_get_caps (device, profile, codec_caps, raw_caps);
    }
  }

  if (!check_caps (&codec_caps, &raw_caps))
    return FALSE;

  h26x_complete_caps (codec_caps, (char **) stream_format);

  /* HACK: add baseline and extended profiles if constrained-baseline is
   * supported */
  {
    const GstStructure *structure = gst_caps_get_structure (codec_caps, 0);
    const GValue *profiles_value =
        gst_structure_get_value (structure, "profile");
    gboolean has_constrained_baseline = FALSE;

    if (GST_VALUE_HOLDS_LIST (profiles_value)) {
      for (int i = 0; i < gst_value_list_get_size (profiles_value); i++) {
        const GValue *profile = gst_value_list_get_value (profiles_value, i);
        if (G_VALUE_HOLDS_STRING (profile)) {
          const gchar *profile_str = g_value_get_string (profile);
          if (g_strcmp0 (profile_str, "constrained-baseline") == 0) {
            has_constrained_baseline = TRUE;
            break;
          }
        }
      }
    } else if (G_VALUE_HOLDS_STRING (profiles_value)) {
      const gchar *profile_str = g_value_get_string (profiles_value);
      has_constrained_baseline =
          (g_strcmp0 (profile_str, "constrained-baseline") == 0);
    }

    if (has_constrained_baseline) {
      const char *profiles[] = { "baseline", "extended" };
      GValue new_profiles = G_VALUE_INIT;

      g_value_init (&new_profiles, GST_TYPE_LIST);
      g_value_copy (profiles_value, &new_profiles);

      for (int i = 0; i < G_N_ELEMENTS (profiles); i++) {
        GValue value = G_VALUE_INIT;

        g_value_init (&value, G_TYPE_STRING);
        g_value_set_string (&value, profiles[i]);
        gst_value_list_append_value (&new_profiles, &value);
        g_value_unset (&value);
      }

      gst_caps_set_value (codec_caps, "profile", &new_profiles);
      g_value_unset (&new_profiles);
    }
  }

  gst_caps_set_features_simple (raw_caps,
      gst_caps_features_new (GST_CAPS_FEATURE_MEMORY_VULKAN_IMAGE, NULL));

  *codec_caps_ptr = codec_caps;
  *raw_caps_ptr = raw_caps;

  return TRUE;
}

static gboolean
h265_decode_caps (GstVulkanPhysicalDevice * device,
    GstVulkanVideoProfile * profile, GstCaps ** codec_caps_ptr,
    GstCaps ** raw_caps_ptr)
{
  GstCaps *codec_caps, *raw_caps;
  const char *stream_format[] = { "hvc1", "hev1", "byte-stream", NULL };

  profile->codec.h265dec.sType =
      VK_STRUCTURE_TYPE_VIDEO_DECODE_H265_PROFILE_INFO_KHR;

  codec_caps = gst_caps_new_empty ();
  raw_caps = gst_caps_new_empty ();

  for (int i = 0; i < G_N_ELEMENTS (h265_profile_idc); i++) {
    profile->codec.h265dec.stdProfileIdc = h265_profile_idc[i];

    try_get_caps (device, profile, codec_caps, raw_caps);
  }

  if (!check_caps (&codec_caps, &raw_caps))
    return FALSE;

  h26x_complete_caps (codec_caps, (char **) stream_format);

  gst_caps_set_features_simple (raw_caps,
      gst_caps_features_new (GST_CAPS_FEATURE_MEMORY_VULKAN_IMAGE, NULL));

  *codec_caps_ptr = codec_caps;
  *raw_caps_ptr = raw_caps;

  return TRUE;
}

static const StdVideoAV1Profile av1_profile[] = {
  STD_VIDEO_AV1_PROFILE_MAIN, STD_VIDEO_AV1_PROFILE_HIGH,
  STD_VIDEO_AV1_PROFILE_PROFESSIONAL,
};

static const VkBool32 av1_film_grain_map[] = {
  VK_TRUE, VK_FALSE,
};

static gboolean
av1_decode_caps (GstVulkanPhysicalDevice * device,
    GstVulkanVideoProfile * profile, GstCaps ** codec_caps_ptr,
    GstCaps ** raw_caps_ptr)
{
  GstCaps *codec_caps, *raw_caps;

  profile->codec.av1dec.sType =
      VK_STRUCTURE_TYPE_VIDEO_DECODE_AV1_PROFILE_INFO_KHR;

  codec_caps = gst_caps_new_empty ();
  raw_caps = gst_caps_new_empty ();

  for (int i = 0; i < G_N_ELEMENTS (av1_profile); i++) {
    profile->codec.av1dec.stdProfile = av1_profile[i];

    for (int j = 0; j < G_N_ELEMENTS (av1_film_grain_map); j++) {
      profile->codec.av1dec.filmGrainSupport = av1_film_grain_map[j];

      try_get_caps (device, profile, codec_caps, raw_caps);
    }
  }

  if (!check_caps (&codec_caps, &raw_caps))
    return FALSE;

  gst_caps_set_simple (codec_caps, "alignment", G_TYPE_STRING, "frame",
      "stream-format", G_TYPE_STRING, "obu-stream", NULL);

  gst_caps_set_features_simple (raw_caps,
      gst_caps_features_new (GST_CAPS_FEATURE_MEMORY_VULKAN_IMAGE, NULL));

  *codec_caps_ptr = codec_caps;
  *raw_caps_ptr = raw_caps;

  return TRUE;
}

static const StdVideoVP9Profile vp9_profile[] = {
  STD_VIDEO_VP9_PROFILE_0, STD_VIDEO_VP9_PROFILE_1, STD_VIDEO_VP9_PROFILE_2,
  STD_VIDEO_VP9_PROFILE_3,
};

static gboolean
vp9_decode_caps (GstVulkanPhysicalDevice * device,
    GstVulkanVideoProfile * profile, GstCaps ** codec_caps_ptr,
    GstCaps ** raw_caps_ptr)
{
  GstCaps *codec_caps, *raw_caps;

  profile->codec.vp9dec.sType =
      VK_STRUCTURE_TYPE_VIDEO_DECODE_VP9_PROFILE_INFO_KHR;

  codec_caps = gst_caps_new_empty ();
  raw_caps = gst_caps_new_empty ();

  for (int i = 0; i < G_N_ELEMENTS (vp9_profile); i++) {
    profile->codec.vp9dec.stdProfile = vp9_profile[i];

    try_get_caps (device, profile, codec_caps, raw_caps);
  }

  if (!check_caps (&codec_caps, &raw_caps))
    return FALSE;

  gst_caps_set_simple (codec_caps, "alignment", G_TYPE_STRING, "frame", NULL);

  gst_caps_set_features_simple (raw_caps,
      gst_caps_features_new (GST_CAPS_FEATURE_MEMORY_VULKAN_IMAGE, NULL));

  *codec_caps_ptr = codec_caps;
  *raw_caps_ptr = raw_caps;

  return TRUE;
}

/**
 * gst_vulkan_physical_device_codec_caps:
 * @device: a #GstVulkanPhysicalDevice
 * @codec: (type int): Vulkan codec operation type
 * @codec_caps: (out) (not nullable) (transfer full): the codec #GstCaps
 * @raw_caps: (out) (not nullable) (transfer full): the raw #GstCaps
 *
 * Returns: whether the @codec_caps and @raw_caps were extracted from the
 *   @device configured for @codec.
 */
gboolean
gst_vulkan_physical_device_codec_caps (GstVulkanPhysicalDevice * device,
    VkVideoCodecOperationFlagBitsKHR codec, GstCaps ** codec_caps,
    GstCaps ** raw_caps)
{
  GstVulkanVideoProfile profile;

  g_return_val_if_fail (GST_IS_VULKAN_PHYSICAL_DEVICE (device), FALSE);

  build_profile (&profile, codec);

  switch (codec) {
    case VK_VIDEO_CODEC_OPERATION_ENCODE_H264_BIT_KHR:
      return h264_encode_caps (device, &profile, codec_caps, raw_caps);
    case VK_VIDEO_CODEC_OPERATION_ENCODE_H265_BIT_KHR:
      return h265_encode_caps (device, &profile, codec_caps, raw_caps);
    case VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_KHR:
      return h264_decode_caps (device, &profile, codec_caps, raw_caps);
    case VK_VIDEO_CODEC_OPERATION_DECODE_H265_BIT_KHR:
      return h265_decode_caps (device, &profile, codec_caps, raw_caps);
    case VK_VIDEO_CODEC_OPERATION_DECODE_AV1_BIT_KHR:
      return av1_decode_caps (device, &profile, codec_caps, raw_caps);
    case VK_VIDEO_CODEC_OPERATION_ENCODE_AV1_BIT_KHR:
      return FALSE;             /* unimplemented */
    case VK_VIDEO_CODEC_OPERATION_DECODE_VP9_BIT_KHR:
      return vp9_decode_caps (device, &profile, codec_caps, raw_caps);
    default:
      g_assert_not_reached ();
  }

  return FALSE;
}
