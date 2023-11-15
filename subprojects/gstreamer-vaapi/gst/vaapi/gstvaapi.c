/*
 *  gstvaapi.c - VA-API element registration
 *
 *  Copyright (C) 2011-2013 Intel Corporation
 *    Author: Gwenole Beauchesne <gwenole.beauchesne@intel.com>
 *  Copyright (C) 2011 Collabora Ltd.
 *    Author: Nicolas Dufresne <nicolas.dufresne@collabora.co.uk>
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

#include "gstcompat.h"
#include "gstvaapidecode.h"
#include "gstvaapioverlay.h"
#include "gstvaapipostproc.h"
#include "gstvaapisink.h"
#include "gstvaapidecodebin.h"

#if GST_VAAPI_USE_ENCODERS
#include "gstvaapiencode_h264.h"
#include "gstvaapiencode_mpeg2.h"
#include "gstvaapiencode_jpeg.h"
#include "gstvaapiencode_vp8.h"
#include "gstvaapiencode_h265.h"

#if GST_VAAPI_USE_VP9_ENCODER
#include "gstvaapiencode_vp9.h"
#endif
#endif

gboolean _gst_vaapi_has_video_processing = FALSE;

#define PLUGIN_NAME     "vaapi"
#define PLUGIN_DESC     "VA-API based elements"
#define PLUGIN_LICENSE  "LGPL"

static void
plugin_add_dependencies (GstPlugin * plugin)
{
  const gchar *envvars[] = { "GST_VAAPI_ALL_DRIVERS", "LIBVA_DRIVER_NAME",
    "DISPLAY", "WAYLAND_DISPLAY", "GST_VAAPI_DRM_DEVICE", NULL
  };
  const gchar *kernel_paths[] = { "/dev/dri", NULL };
  const gchar *kernel_names[] = { "card", "render", NULL };

  /* features get updated upon changes in /dev/dri/card* */
  gst_plugin_add_dependency (plugin, NULL, kernel_paths, kernel_names,
      GST_PLUGIN_DEPENDENCY_FLAG_FILE_NAME_IS_PREFIX);

  /* features get updated upon changes in VA environment variables */
  gst_plugin_add_dependency (plugin, envvars, NULL, NULL,
      GST_PLUGIN_DEPENDENCY_FLAG_NONE);

  /* features get updated upon changes in default VA drivers
   * directory */
  gst_plugin_add_dependency_simple (plugin, "LIBVA_DRIVERS_PATH",
      VA_DRIVERS_PATH, "_drv_video.so",
      GST_PLUGIN_DEPENDENCY_FLAG_FILE_NAME_IS_SUFFIX |
      GST_PLUGIN_DEPENDENCY_FLAG_PATHS_ARE_DEFAULT_ONLY);
}

static GArray *
profiles_get_codecs (GArray * profiles)
{
  guint i;
  GArray *codecs;
  GstVaapiProfile profile;
  GstVaapiCodec codec;

  codecs = g_array_new (FALSE, FALSE, sizeof (GstVaapiCodec));
  if (!codecs)
    return NULL;

  for (i = 0; i < profiles->len; i++) {
    profile = g_array_index (profiles, GstVaapiProfile, i);
    codec = gst_vaapi_profile_get_codec (profile);
    if (gst_vaapi_codecs_has_codec (codecs, codec))
      continue;
    g_array_append_val (codecs, codec);
  }

  return codecs;
}

static GArray *
display_get_decoder_codecs (GstVaapiDisplay * display)
{
  GArray *profiles, *codecs;

  profiles = gst_vaapi_display_get_decode_profiles (display);
  if (!profiles)
    return NULL;

  codecs = profiles_get_codecs (profiles);
  g_array_unref (profiles);
  return codecs;
}

#if GST_VAAPI_USE_ENCODERS
static GArray *
display_get_encoder_codecs (GstVaapiDisplay * display)
{
  GArray *profiles, *codecs;

  profiles = gst_vaapi_display_get_encode_profiles (display);
  if (!profiles)
    return NULL;

  codecs = profiles_get_codecs (profiles);
  g_array_unref (profiles);
  return codecs;
}

typedef struct _GstVaapiEncoderMap GstVaapiEncoderMap;
struct _GstVaapiEncoderMap
{
  GstVaapiCodec codec;
  guint rank;
  const gchar *name;
    GType (*register_type) (GstVaapiDisplay * display);
};

#define DEF_ENC(CODEC,codec)          \
  {GST_VAAPI_CODEC_##CODEC,           \
   GST_RANK_PRIMARY,                  \
   "vaapi" G_STRINGIFY (codec) "enc", \
   gst_vaapiencode_##codec##_register_type}

static const GstVaapiEncoderMap vaapi_encode_map[] = {
  DEF_ENC (H264, h264),
  DEF_ENC (MPEG2, mpeg2),
  DEF_ENC (JPEG, jpeg),
  DEF_ENC (VP8, vp8),
#if GST_VAAPI_USE_VP9_ENCODER
  DEF_ENC (VP9, vp9),
#endif
  DEF_ENC (H265, h265),
};

#undef DEF_ENC

static void
gst_vaapiencode_register (GstPlugin * plugin, GstVaapiDisplay * display)
{
  guint i, j;
  GArray *codecs;
  GstVaapiCodec codec;

  codecs = display_get_encoder_codecs (display);
  if (!codecs)
    return;

  for (i = 0; i < codecs->len; i++) {
    codec = g_array_index (codecs, GstVaapiCodec, i);
    for (j = 0; j < G_N_ELEMENTS (vaapi_encode_map); j++) {
      if (vaapi_encode_map[j].codec == codec) {
        gst_element_register (plugin, vaapi_encode_map[j].name,
            vaapi_encode_map[j].rank,
            vaapi_encode_map[j].register_type (display));
        break;
      }
    }
  }

  g_array_unref (codecs);
}
#endif

static gboolean
plugin_init (GstPlugin * plugin)
{
  GstVaapiDisplay *display;
  GArray *decoders;
  guint rank;

  plugin_add_dependencies (plugin);

  display = gst_vaapi_create_test_display ();
  if (!display)
    goto error_no_display;
  if (!gst_vaapi_driver_is_whitelisted (display))
    goto unsupported_driver;

  _gst_vaapi_has_video_processing =
      gst_vaapi_display_has_video_processing (display);

  decoders = display_get_decoder_codecs (display);
  if (decoders) {
    gst_vaapidecode_register (plugin, decoders);
    gst_element_register (plugin, "vaapidecodebin",
        GST_RANK_NONE, GST_TYPE_VAAPI_DECODE_BIN);
    g_array_unref (decoders);
  }

  if (_gst_vaapi_has_video_processing) {
    gst_vaapioverlay_register (plugin, display);

    gst_element_register (plugin, "vaapipostproc",
        GST_RANK_NONE, GST_TYPE_VAAPIPOSTPROC);
  }

  rank = GST_RANK_NONE;
  gst_element_register (plugin, "vaapisink", rank, GST_TYPE_VAAPISINK);

#if GST_VAAPI_USE_ENCODERS
  gst_vaapiencode_register (plugin, display);
#endif

  gst_object_unref (display);

  return TRUE;

  /* ERRORS: */
error_no_display:
  {
    GST_WARNING ("Cannot create a VA display");
    /* Avoid blacklisting: failure to create a display could be a
     * transient condition */
    return TRUE;
  }
unsupported_driver:
  {
    gst_object_unref (display);
    return TRUE;                /* return TRUE to avoid get blacklisted */
  }
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR, GST_VERSION_MINOR,
    vaapi, PLUGIN_DESC, plugin_init,
    PACKAGE_VERSION, PLUGIN_LICENSE, PACKAGE, PACKAGE_BUGREPORT)
