/*
 *  gstvaapiprofile.c - VA profile abstraction
 *
 *  Copyright (C) 2010-2011 Splitted-Desktop Systems
 *    Author: Gwenole Beauchesne <gwenole.beauchesne@splitted-desktop.com>
 *  Copyright (C) 2012-2013 Intel Corporation
 *    Author: Gwenole Beauchesne <gwenole.beauchesne@intel.com>
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public License
 *  as published by the Free Software Foundation; either version 2.1
 *  of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free
 *  Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA 02110-1301 USA
 */

/**
 * SECTION:gstvaapiprofile
 * @short_description: VA profile abstraction
 */

#include "sysdeps.h"
#include <gst/gstbuffer.h>
#include "gstvaapicompat.h"
#include "gstvaapiprofile.h"
#include "gstvaapiutils.h"
#include "gstvaapiworkarounds.h"

typedef struct _GstVaapiCodecMap GstVaapiCodecMap;
typedef struct _GstVaapiProfileMap GstVaapiProfileMap;
typedef struct _GstVaapiEntrypointMap GstVaapiEntrypointMap;

struct _GstVaapiCodecMap
{
  GstVaapiCodec codec;
  const gchar *name;
};

struct _GstVaapiProfileMap
{
  GstVaapiProfile profile;
  VAProfile va_profile;
  const char *media_str;
  const gchar *profile_str;
};

struct _GstVaapiEntrypointMap
{
  GstVaapiEntrypoint entrypoint;
  VAEntrypoint va_entrypoint;
};

/* Codecs */
static const GstVaapiCodecMap gst_vaapi_codecs[] = {
  {GST_VAAPI_CODEC_MPEG1, "mpeg1"},
  {GST_VAAPI_CODEC_MPEG2, "mpeg2"},
  {GST_VAAPI_CODEC_MPEG4, "mpeg4"},
  {GST_VAAPI_CODEC_H263, "h263"},
  {GST_VAAPI_CODEC_H264, "h264"},
  {GST_VAAPI_CODEC_WMV3, "wmv3"},
  {GST_VAAPI_CODEC_VC1, "vc1"},
  {GST_VAAPI_CODEC_JPEG, "jpeg"},
  {GST_VAAPI_CODEC_VP8, "vp8"},
  {GST_VAAPI_CODEC_H265, "h265"},
  {GST_VAAPI_CODEC_VP9, "vp9"},
  {GST_VAAPI_CODEC_AV1, "av1"},
  {0,}
};

/* Profiles */
static const GstVaapiProfileMap gst_vaapi_profiles[] = {
  {GST_VAAPI_PROFILE_MPEG2_SIMPLE, VAProfileMPEG2Simple,
      "video/mpeg, mpegversion=2", "simple"},
  {GST_VAAPI_PROFILE_MPEG2_MAIN, VAProfileMPEG2Main,
      "video/mpeg, mpegversion=2", "main"},
  {GST_VAAPI_PROFILE_MPEG4_SIMPLE, VAProfileMPEG4Simple,
      "video/mpeg, mpegversion=4", "simple"},
  {GST_VAAPI_PROFILE_MPEG4_ADVANCED_SIMPLE, VAProfileMPEG4AdvancedSimple,
      "video/mpeg, mpegversion=4", "advanced-simple"},
  {GST_VAAPI_PROFILE_MPEG4_MAIN, VAProfileMPEG4Main,
      "video/mpeg, mpegversion=4", "main"},
  {GST_VAAPI_PROFILE_MPEG4_ADVANCED_SIMPLE, VAProfileMPEG4AdvancedSimple,
      "video/x-divx, divxversion=5", "advanced-simple"},
  {GST_VAAPI_PROFILE_MPEG4_ADVANCED_SIMPLE, VAProfileMPEG4AdvancedSimple,
      "video/x-xvid", "advanced-simple"},
  {GST_VAAPI_PROFILE_H263_BASELINE, VAProfileH263Baseline,
      "video/x-h263, variant=itu, h263version=h263", "baseline"},
#if !VA_CHECK_VERSION(1,0,0)
  {GST_VAAPI_PROFILE_H264_BASELINE, VAProfileH264Baseline,
      "video/x-h264", "baseline"},
#endif
  {GST_VAAPI_PROFILE_H264_CONSTRAINED_BASELINE,
        VAProfileH264ConstrainedBaseline,
      "video/x-h264", "constrained-baseline"},
  {GST_VAAPI_PROFILE_H264_MAIN, VAProfileH264Main,
      "video/x-h264", "main"},
  {GST_VAAPI_PROFILE_H264_HIGH, VAProfileH264High,
      "video/x-h264", "high"},
  {GST_VAAPI_PROFILE_H264_MULTIVIEW_HIGH, VAProfileH264MultiviewHigh,
      "video/x-h264", "multiview-high"},
  {GST_VAAPI_PROFILE_H264_STEREO_HIGH, VAProfileH264StereoHigh,
      "video/x-h264", "stereo-high"},
  {GST_VAAPI_PROFILE_VC1_SIMPLE, VAProfileVC1Simple,
      "video/x-wmv, wmvversion=3", "simple"},
  {GST_VAAPI_PROFILE_VC1_MAIN, VAProfileVC1Main,
      "video/x-wmv, wmvversion=3", "main"},
  {GST_VAAPI_PROFILE_VC1_ADVANCED, VAProfileVC1Advanced,
      "video/x-wmv, wmvversion=3, format=(string)WVC1", "advanced"},
  {GST_VAAPI_PROFILE_JPEG_BASELINE, VAProfileJPEGBaseline,
      "image/jpeg", NULL},
  {GST_VAAPI_PROFILE_VP8, VAProfileVP8Version0_3,
      "video/x-vp8", NULL},
  {GST_VAAPI_PROFILE_H265_MAIN, VAProfileHEVCMain,
      "video/x-h265", "main"},
  {GST_VAAPI_PROFILE_H265_MAIN10, VAProfileHEVCMain10,
      "video/x-h265", "main-10"},
#if VA_CHECK_VERSION(1,2,0)
  {GST_VAAPI_PROFILE_H265_MAIN_422_10, VAProfileHEVCMain422_10,
      "video/x-h265", "main-422-10"},
  {GST_VAAPI_PROFILE_H265_MAIN_444, VAProfileHEVCMain444,
      "video/x-h265", "main-444"},
  {GST_VAAPI_PROFILE_H265_MAIN_444_10, VAProfileHEVCMain444_10,
      "video/x-h265", "main-444-10"},
  {GST_VAAPI_PROFILE_H265_MAIN12, VAProfileHEVCMain12,
      "video/x-h265", "main-12"},
  {GST_VAAPI_PROFILE_H265_MAIN_444_12, VAProfileHEVCMain444_12,
      "video/x-h265", "main-444-12"},
  {GST_VAAPI_PROFILE_H265_MAIN_422_12, VAProfileHEVCMain422_12,
      "video/x-h265", "main-422-12"},
  {GST_VAAPI_PROFILE_H265_SCREEN_EXTENDED_MAIN, VAProfileHEVCSccMain,
      "video/x-h265", "screen-extended-main"},
  {GST_VAAPI_PROFILE_H265_SCREEN_EXTENDED_MAIN_10, VAProfileHEVCSccMain10,
      "video/x-h265", "screen-extended-main-10"},
  {GST_VAAPI_PROFILE_H265_SCREEN_EXTENDED_MAIN_444, VAProfileHEVCSccMain444,
      "video/x-h265", "screen-extended-main-444"},
#endif
#if VA_CHECK_VERSION(1,8,0)
  {GST_VAAPI_PROFILE_H265_SCREEN_EXTENDED_MAIN_444_10,
        VAProfileHEVCSccMain444_10,
      "video/x-h265", "screen-extended-main-444-10"},
#endif
  {GST_VAAPI_PROFILE_VP9_0, VAProfileVP9Profile0,
      "video/x-vp9", "0"},
  {GST_VAAPI_PROFILE_VP9_1, VAProfileVP9Profile1,
      "video/x-vp9", "1"},
  {GST_VAAPI_PROFILE_VP9_2, VAProfileVP9Profile2,
      "video/x-vp9", "2"},
  {GST_VAAPI_PROFILE_VP9_3, VAProfileVP9Profile3,
      "video/x-vp9", "3"},
#if VA_CHECK_VERSION(1,8,0)
  /* Spec A.2:
     "Main" compliant decoders must be able to decode streams with
     seq_profile equal to 0.
     "High" compliant decoders must be able to decode streams with
     seq_profile less than or equal to 1.
     "Professional" compliant decoders must be able to decode streams
     with seq_profile less than or equal to 2.

     The correct relationship between profile "main" "high" "professional"
     and seq_profile "0" "1" "2" should be:
     main <------> { 0 }
     high <------> { main, 1 }
     professional <------> { high, 2 }

     So far, all vaapi decoders can support "0" when they support "1",
     we just map "0" to "main" and "1" to "high" in caps string.  */
  {GST_VAAPI_PROFILE_AV1_0, VAProfileAV1Profile0,
      "video/x-av1", "main"},
  {GST_VAAPI_PROFILE_AV1_1, VAProfileAV1Profile1,
      "video/x-av1", "high"},
#endif
  {0,}
};

/* Entry-points */
static const GstVaapiEntrypointMap gst_vaapi_entrypoints[] = {
  {GST_VAAPI_ENTRYPOINT_VLD, VAEntrypointVLD},
  {GST_VAAPI_ENTRYPOINT_IDCT, VAEntrypointIDCT},
  {GST_VAAPI_ENTRYPOINT_MOCO, VAEntrypointMoComp},
  {GST_VAAPI_ENTRYPOINT_SLICE_ENCODE, VAEntrypointEncSlice},
  {GST_VAAPI_ENTRYPOINT_PICTURE_ENCODE, VAEntrypointEncPicture},
#if VA_CHECK_VERSION(0,39,1)
  {GST_VAAPI_ENTRYPOINT_SLICE_ENCODE_LP, VAEntrypointEncSliceLP},
#endif
  {0,}
};

static const GstVaapiCodecMap *
get_codecs_map (GstVaapiCodec codec)
{
  const GstVaapiCodecMap *m;

  for (m = gst_vaapi_codecs; m->codec; m++)
    if (m->codec == codec)
      return m;
  return NULL;
}

static const GstVaapiProfileMap *
get_profiles_map (GstVaapiProfile profile)
{
  const GstVaapiProfileMap *m;

  for (m = gst_vaapi_profiles; m->profile; m++)
    if (m->profile == profile)
      return m;
  return NULL;
}

static const GstVaapiEntrypointMap *
get_entrypoints_map (GstVaapiEntrypoint entrypoint)
{
  const GstVaapiEntrypointMap *m;

  for (m = gst_vaapi_entrypoints; m->entrypoint; m++)
    if (m->entrypoint == entrypoint)
      return m;
  return NULL;
}

/**
 * gst_vaapi_codec_get_name:
 * @codec: a #GstVaapiCodec
 *
 * Returns a string representation for the supplied @codec type.
 *
 * Return value: the statically allocated string representation of @codec
 */
const gchar *
gst_vaapi_codec_get_name (GstVaapiCodec codec)
{
  const GstVaapiCodecMap *const m = get_codecs_map (codec);

  return m ? m->name : NULL;
}

/**
 * gst_vaapi_profile:
 * @profile: a #VAProfile
 *
 * Converts a VA profile into the corresponding #GstVaapiProfile. If
 * the profile cannot be represented by #GstVaapiProfile, then zero is
 * returned.
 *
 * Return value: the #GstVaapiProfile describing the @profile
 */
GstVaapiProfile
gst_vaapi_profile (VAProfile profile)
{
  const GstVaapiProfileMap *m;

  for (m = gst_vaapi_profiles; m->profile; m++)
    if (m->va_profile == profile)
      return m->profile;
  return 0;
}

/**
 * gst_vaapi_profile_get_name:
 * @profile: a #GstVaapiProfile
 *
 * Returns a string representation for the supplied @profile.
 *
 * Return value: the statically allocated string representation of @profile
 */
const gchar *
gst_vaapi_profile_get_name (GstVaapiProfile profile)
{
  const GstVaapiProfileMap *const m = get_profiles_map (profile);

  return m ? m->profile_str : NULL;
}

/**
 * gst_vaapi_profile_get_va_name:
 * @profile: a #GstVaapiProfile
 *
 * Returns a string representation for the supplied @profile as VAProfile.
 *
 * Return value: the statically allocated string representation of
 * @profile as VAProfile
 */
const gchar *
gst_vaapi_profile_get_va_name (GstVaapiProfile profile)
{
  const GstVaapiProfileMap *const m = get_profiles_map (profile);

  return m ? string_of_VAProfile (m->va_profile) : NULL;
}

/**
 * gst_vaapi_profile_get_media_type_name:
 * @profile: a #GstVaapiProfileo
 *
 * Returns a string representation for the media type of the supplied
 * @profile.
 *
 * Return value: the statically allocated string representation of
 *   @profile media type
 */
const gchar *
gst_vaapi_profile_get_media_type_name (GstVaapiProfile profile)
{
  const GstVaapiProfileMap *const m = get_profiles_map (profile);

  return m ? m->media_str : NULL;
}

/**
 * gst_vaapi_profile_from_codec_data:
 * @codec: a #GstVaapiCodec
 * @buffer: a #GstBuffer holding code data
 *
 * Tries to parse VA profile from @buffer data and @codec information.
 *
 * Return value: the #GstVaapiProfile described in @buffer
 */
static GstVaapiProfile
gst_vaapi_profile_from_codec_data_h264 (GstBuffer * buffer)
{
  /* MPEG-4 Part 15: Advanced Video Coding (AVC) file format */
  guchar buf[3];

  if (gst_buffer_extract (buffer, 0, buf, sizeof (buf)) != sizeof (buf))
    return 0;

  if (buf[0] != 1)              /* configurationVersion = 1 */
    return 0;

  switch (buf[1]) {             /* AVCProfileIndication */
    case 66:
      return ((buf[2] & 0x40) ?
          GST_VAAPI_PROFILE_H264_CONSTRAINED_BASELINE :
          GST_VAAPI_PROFILE_H264_BASELINE);
    case 77:
      return GST_VAAPI_PROFILE_H264_MAIN;
    case 100:
      return GST_VAAPI_PROFILE_H264_HIGH;
    case 118:
      return GST_VAAPI_PROFILE_H264_MULTIVIEW_HIGH;
    case 128:
      return GST_VAAPI_PROFILE_H264_STEREO_HIGH;

  }
  return 0;
}

static GstVaapiProfile
gst_vaapi_profile_from_codec_data_h265 (GstBuffer * buffer)
{
  /* ISO/IEC 14496-15:  HEVC file format */
  guchar buf[3];

  if (gst_buffer_extract (buffer, 0, buf, sizeof (buf)) != sizeof (buf))
    return 0;

  if (buf[0] != 1)              /* configurationVersion = 1 */
    return 0;

  if (buf[1] & 0xc0)            /* general_profile_space = 0 */
    return 0;

  /* We may not recognize the exactly correct profile, which needs more
     info such as depth, chroma and constraint_flag. We just return the
     first one that belongs to that profile IDC. */
  switch (buf[1] & 0x1f) {      /* HEVCProfileIndication */
    case 1:
      return GST_VAAPI_PROFILE_H265_MAIN;
    case 2:
      return GST_VAAPI_PROFILE_H265_MAIN10;
    case 3:
      return GST_VAAPI_PROFILE_H265_MAIN_STILL_PICTURE;
    case 4:
      return GST_VAAPI_PROFILE_H265_MAIN_422_10;
    case 9:
      return GST_VAAPI_PROFILE_H265_SCREEN_EXTENDED_MAIN;
  }
  return 0;
}

static GstVaapiProfile
gst_vaapi_profile_from_codec_data (GstVaapiCodec codec, GstBuffer * buffer)
{
  GstVaapiProfile profile;

  if (!codec || !buffer)
    return 0;

  switch (codec) {
    case GST_VAAPI_CODEC_H264:
      profile = gst_vaapi_profile_from_codec_data_h264 (buffer);
      break;
    case GST_VAAPI_CODEC_H265:
      profile = gst_vaapi_profile_from_codec_data_h265 (buffer);
      break;
    default:
      profile = 0;
      break;
  }
  return profile;
}

/**
 * gst_vaapi_profile_from_caps:
 * @caps: a #GstCaps
 *
 * Converts @caps into the corresponding #GstVaapiProfile. If the
 * profile cannot be represented by #GstVaapiProfile, then zero is
 * returned.
 *
 * Return value: the #GstVaapiProfile describing the @caps
 */
GstVaapiProfile
gst_vaapi_profile_from_caps (const GstCaps * caps)
{
  const GstVaapiProfileMap *m;
  GstCaps *caps_test;
  GstStructure *structure;
  const gchar *profile_str;
  GstVaapiProfile profile, best_profile;
  GstBuffer *codec_data = NULL;
  const gchar *name;
  gsize namelen;

  if (!caps)
    return 0;

  structure = gst_caps_get_structure (caps, 0);
  if (!structure)
    return 0;

  name = gst_structure_get_name (structure);
  namelen = strlen (name);

  profile_str = gst_structure_get_string (structure, "profile");
  if (!profile_str) {
    const GValue *v_codec_data;
    v_codec_data = gst_structure_get_value (structure, "codec_data");
    if (v_codec_data)
      codec_data = gst_value_get_buffer (v_codec_data);
  }

  profile = 0;
  best_profile = 0;
  for (m = gst_vaapi_profiles; !profile && m->profile; m++) {
    if (strncmp (name, m->media_str, namelen) != 0)
      continue;
    caps_test = gst_caps_from_string (m->media_str);
    if (gst_caps_is_always_compatible (caps, caps_test)) {
      best_profile = m->profile;
      if (profile_str && m->profile_str &&
          strcmp (profile_str, m->profile_str) == 0)
        profile = best_profile;
    }
    if (!profile) {
      profile =
          gst_vaapi_profile_from_codec_data (gst_vaapi_profile_get_codec
          (m->profile), codec_data);
      if (!profile && WORKAROUND_QTDEMUX_NO_H263_PROFILES
          && strncmp (name, "video/x-h263", namelen) == 0) {
        /* HACK: qtdemux does not report profiles for h263 */
        profile = m->profile;
      }

      /* Consider HEVC -intra profiles. Just map them to their
       * non-intra profiles */
      if (!profile && profile_str
          && strncmp (name, "video/x-h265", namelen) == 0
          && g_str_has_prefix (profile_str, m->profile_str)
          && strncmp (profile_str + strlen (m->profile_str), "-intra", 6) == 0) {
        profile = m->profile;
      }
    }
    gst_caps_unref (caps_test);
  }
  return profile ? profile : best_profile;
}

/**
 * gst_vaapi_get_codec_from_caps:
 * @caps: a #GstCaps
 *
 * Converts @caps into the corresponding #GstVaapiCodec. If we can
 * not recognize the #GstVaapiCodec, then zero is returned.
 *
 * Return value: the #GstVaapiCodec describing the @caps
 */
GstVaapiCodec
gst_vaapi_get_codec_from_caps (const GstCaps * caps)
{
  GstStructure *structure;
  const gchar *name;
  gsize namelen;
  const GstVaapiProfileMap *m;
  GstVaapiProfile profile;

  if (!caps)
    return 0;

  structure = gst_caps_get_structure (caps, 0);
  if (!structure)
    return 0;

  name = gst_structure_get_name (structure);
  namelen = strlen (name);

  profile = GST_VAAPI_PROFILE_UNKNOWN;
  for (m = gst_vaapi_profiles; m->profile; m++) {
    if (strncmp (name, m->media_str, namelen) == 0) {
      profile = m->profile;
      break;
    }
  }

  if (profile == GST_VAAPI_PROFILE_UNKNOWN)
    return 0;

  return gst_vaapi_profile_get_codec (profile);
}

/**
 * gst_vaapi_profile_get_va_profile:
 * @profile: a #GstVaapiProfile
 *
 * Converts a #GstVaapiProfile into the corresponding VA profile. If
 * no matching VA profile was found, -1 is returned and this error
 * must be reported to be fixed.
 *
 * Return value: the VA profile, or -1 if none was found
 */
VAProfile
gst_vaapi_profile_get_va_profile (GstVaapiProfile profile)
{
  const GstVaapiProfileMap *const m = get_profiles_map (profile);

  return m ? m->va_profile : (VAProfile) - 1;
}

/**
 * gst_vaapi_profile_get_caps:
 * @profile: a #GstVaapiProfile
 *
 * Converts a #GstVaapiProfile into the corresponding #GstCaps. If no
 * matching caps were found, %NULL is returned.
 *
 * Return value: the newly allocated #GstCaps, or %NULL if none was found
 */
GstCaps *
gst_vaapi_profile_get_caps (GstVaapiProfile profile)
{
  const GstVaapiProfileMap *m;
  GstCaps *out_caps, *caps;

  out_caps = gst_caps_new_empty ();
  if (!out_caps)
    return NULL;

  for (m = gst_vaapi_profiles; m->profile; m++) {
    if (m->profile != profile)
      continue;
    caps = gst_caps_from_string (m->media_str);
    if (!caps)
      continue;
    gst_caps_set_simple (caps, "profile", G_TYPE_STRING, m->profile_str, NULL);
    out_caps = gst_caps_merge (out_caps, caps);
  }
  return out_caps;
}

/**
 * gst_vaapi_profile_get_codec:
 * @profile: a #GstVaapiProfile
 *
 * Extracts the #GstVaapiCodec from @profile.
 *
 * Return value: the #GstVaapiCodec from @profile
 */
GstVaapiCodec
gst_vaapi_profile_get_codec (GstVaapiProfile profile)
{
  GstVaapiCodec codec;

  switch (profile) {
    case GST_VAAPI_PROFILE_VC1_SIMPLE:
    case GST_VAAPI_PROFILE_VC1_MAIN:
      codec = GST_VAAPI_CODEC_WMV3;
      break;
    case GST_VAAPI_PROFILE_VC1_ADVANCED:
      codec = GST_VAAPI_CODEC_VC1;
      break;
    case GST_VAAPI_PROFILE_JPEG_BASELINE:
      codec = GST_VAAPI_CODEC_JPEG;
      break;
    default:
      codec = (guint32) profile & GST_MAKE_FOURCC (0xff, 0xff, 0xff, 0);
      break;
  }
  return codec;
}

/**
 * gst_vaapi_entrypoint:
 * @entrypoint: a #VAEntrypoint
 *
 * Converts a VA entry-point into the corresponding #GstVaapiEntrypoint.
 * If the entry-point cannot be represented by #GstVaapiEntrypoint,
 * then zero is returned.
 *
 * Return value: the #GstVaapiEntrypoint describing the @entrypoint
 */
GstVaapiEntrypoint
gst_vaapi_entrypoint (VAEntrypoint entrypoint)
{
  const GstVaapiEntrypointMap *m;

  for (m = gst_vaapi_entrypoints; m->entrypoint; m++)
    if (m->va_entrypoint == entrypoint)
      return m->entrypoint;
  return 0;
}

/**
 * gst_vaapi_entrypoint_get_va_entrypoint:
 * @entrypoint: a #GstVaapiEntrypoint
 *
 * Converts a #GstVaapiEntrypoint into the corresponding VA
 * entry-point. If no matching VA entry-point was found, -1 is
 * returned and this error must be reported to be fixed.
 *
 * Return value: the VA entry-point, or -1 if none was found
 */
VAEntrypoint
gst_vaapi_entrypoint_get_va_entrypoint (GstVaapiEntrypoint entrypoint)
{
  const GstVaapiEntrypointMap *const m = get_entrypoints_map (entrypoint);

  return m ? m->va_entrypoint : (VAEntrypoint) - 1;
}
