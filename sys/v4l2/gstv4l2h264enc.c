/*
 * Copyright (C) 2014 SUMOMO Computer Association
 *     Author: ayaka <ayaka@soulik.info>
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
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>

#include "gstv4l2h264enc.h"
#include "v4l2_calls.h"

#include <string.h>
#include <gst/gst-i18n-plugin.h>

GST_DEBUG_CATEGORY_STATIC (gst_v4l2_h264_enc_debug);
#define GST_CAT_DEFAULT gst_v4l2_h264_enc_debug


static GstStaticCaps src_template_caps =
GST_STATIC_CAPS ("video/x-h264, stream-format=(string) byte-stream, "
    "alignment=(string) au");

enum
{
  PROP_0,
  V4L2_STD_OBJECT_PROPS,
/* TODO add H264 controls
 * PROP_I_FRAME_QP,
 * PROP_P_FRAME_QP,
 * PROP_B_FRAME_QP,
 * PROP_MIN_QP,
 * PROP_MAX_QP,
 * PROP_8x8_TRANSFORM,
 * PROP_CPB_SIZE,
 * PROP_ENTROPY_MODE,
 * PROP_I_PERIOD,
 * PROP_LOOP_FILTER_ALPHA,
 * PROP_LOOP_FILTER_BETA,
 * PROP_LOOP_FILTER_MODE,
 * PROP_VUI_EXT_SAR_HEIGHT,
 * PROP_VUI_EXT_SAR_WIDTH,
 * PROP_VUI_SAR_ENABLED,
 * PROP_VUI_SAR_IDC,
 * PROP_SEI_FRAME_PACKING,
 * PROP_SEI_FP_CURRENT_FRAME_0,
 * PROP_SEI_FP_ARRANGEMENT_TYP,
 * ...
 * */
};

#define gst_v4l2_h264_enc_parent_class parent_class
G_DEFINE_TYPE (GstV4l2H264Enc, gst_v4l2_h264_enc, GST_TYPE_V4L2_VIDEO_ENC);

static void
gst_v4l2_h264_enc_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  /* TODO */
}

static void
gst_v4l2_h264_enc_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  /* TODO */
}

static gint
v4l2_profile_from_string (const gchar * profile)
{
  gint v4l2_profile = -1;

  if (g_str_equal (profile, "baseline")) {
    v4l2_profile = V4L2_MPEG_VIDEO_H264_PROFILE_BASELINE;
  } else if (g_str_equal (profile, "constrained-baseline")) {
    v4l2_profile = V4L2_MPEG_VIDEO_H264_PROFILE_CONSTRAINED_BASELINE;
  } else if (g_str_equal (profile, "main")) {
    v4l2_profile = V4L2_MPEG_VIDEO_H264_PROFILE_MAIN;
  } else if (g_str_equal (profile, "extended")) {
    v4l2_profile = V4L2_MPEG_VIDEO_H264_PROFILE_EXTENDED;
  } else if (g_str_equal (profile, "high")) {
    v4l2_profile = V4L2_MPEG_VIDEO_H264_PROFILE_HIGH;
  } else if (g_str_equal (profile, "high-10")) {
    v4l2_profile = V4L2_MPEG_VIDEO_H264_PROFILE_HIGH_10;
  } else if (g_str_equal (profile, "high-4:2:2")) {
    v4l2_profile = V4L2_MPEG_VIDEO_H264_PROFILE_HIGH_422;
  } else if (g_str_equal (profile, "high-4:4:4")) {
    v4l2_profile = V4L2_MPEG_VIDEO_H264_PROFILE_HIGH_444_PREDICTIVE;
  } else if (g_str_equal (profile, "high-10-intra")) {
    v4l2_profile = V4L2_MPEG_VIDEO_H264_PROFILE_HIGH_10_INTRA;
  } else if (g_str_equal (profile, "high-4:2:2-intra")) {
    v4l2_profile = V4L2_MPEG_VIDEO_H264_PROFILE_HIGH_422_INTRA;
  } else if (g_str_equal (profile, "high-4:4:4-intra")) {
    v4l2_profile = V4L2_MPEG_VIDEO_H264_PROFILE_HIGH_444_INTRA;
  } else if (g_str_equal (profile, "cavlc-4:4:4-intra")) {
    v4l2_profile = V4L2_MPEG_VIDEO_H264_PROFILE_CAVLC_444_INTRA;
  } else if (g_str_equal (profile, "scalable-baseline")) {
    v4l2_profile = V4L2_MPEG_VIDEO_H264_PROFILE_SCALABLE_BASELINE;
  } else if (g_str_equal (profile, "scalable-high")) {
    v4l2_profile = V4L2_MPEG_VIDEO_H264_PROFILE_SCALABLE_HIGH;
  } else if (g_str_equal (profile, "scalable-high-intra")) {
    v4l2_profile = V4L2_MPEG_VIDEO_H264_PROFILE_SCALABLE_HIGH_INTRA;
  } else if (g_str_equal (profile, "stereo-high")) {
    v4l2_profile = V4L2_MPEG_VIDEO_H264_PROFILE_STEREO_HIGH;
  } else if (g_str_equal (profile, "multiview-high")) {
    v4l2_profile = V4L2_MPEG_VIDEO_H264_PROFILE_MULTIVIEW_HIGH;
  } else {
    GST_WARNING ("Unsupported profile string '%s'", profile);
  }

  return v4l2_profile;
}

static const gchar *
v4l2_profile_to_string (gint v4l2_profile)
{
  switch (v4l2_profile) {
    case V4L2_MPEG_VIDEO_H264_PROFILE_BASELINE:
      return "baseline";
    case V4L2_MPEG_VIDEO_H264_PROFILE_CONSTRAINED_BASELINE:
      return "constrained-baseline";
    case V4L2_MPEG_VIDEO_H264_PROFILE_MAIN:
      return "main";
    case V4L2_MPEG_VIDEO_H264_PROFILE_EXTENDED:
      return "extended";
    case V4L2_MPEG_VIDEO_H264_PROFILE_HIGH:
      return "high";
    case V4L2_MPEG_VIDEO_H264_PROFILE_HIGH_10:
      return "high-10";
    case V4L2_MPEG_VIDEO_H264_PROFILE_HIGH_422:
      return "high-4:2:2";
    case V4L2_MPEG_VIDEO_H264_PROFILE_HIGH_444_PREDICTIVE:
      return "high-4:4:4";
    case V4L2_MPEG_VIDEO_H264_PROFILE_HIGH_10_INTRA:
      return "high-10-intra";
    case V4L2_MPEG_VIDEO_H264_PROFILE_HIGH_422_INTRA:
      return "high-4:2:2-intra";
    case V4L2_MPEG_VIDEO_H264_PROFILE_HIGH_444_INTRA:
      return "high-4:4:4-intra";
    case V4L2_MPEG_VIDEO_H264_PROFILE_CAVLC_444_INTRA:
      return "cavlc-4:4:4-intra";
    case V4L2_MPEG_VIDEO_H264_PROFILE_SCALABLE_BASELINE:
      return "scalable-baseline";
    case V4L2_MPEG_VIDEO_H264_PROFILE_SCALABLE_HIGH:
      return "scalable-high";
    case V4L2_MPEG_VIDEO_H264_PROFILE_SCALABLE_HIGH_INTRA:
      return "scalable-high-intra";
    case V4L2_MPEG_VIDEO_H264_PROFILE_STEREO_HIGH:
      return "stereo-high";
    case V4L2_MPEG_VIDEO_H264_PROFILE_MULTIVIEW_HIGH:
      return "multiview-high";
    default:
      GST_WARNING ("Unsupported V4L2 profile %i", v4l2_profile);
      break;
  }

  return NULL;
}

static gint
v4l2_level_from_string (const gchar * level)
{
  gint v4l2_level = -1;

  if (g_str_equal (level, "1"))
    v4l2_level = V4L2_MPEG_VIDEO_H264_LEVEL_1_0;
  else if (g_str_equal (level, "1b"))
    v4l2_level = V4L2_MPEG_VIDEO_H264_LEVEL_1B;
  else if (g_str_equal (level, "1.1"))
    v4l2_level = V4L2_MPEG_VIDEO_H264_LEVEL_1_1;
  else if (g_str_equal (level, "1.2"))
    v4l2_level = V4L2_MPEG_VIDEO_H264_LEVEL_1_2;
  else if (g_str_equal (level, "1.3"))
    v4l2_level = V4L2_MPEG_VIDEO_H264_LEVEL_1_3;
  else if (g_str_equal (level, "2"))
    v4l2_level = V4L2_MPEG_VIDEO_H264_LEVEL_2_0;
  else if (g_str_equal (level, "2.1"))
    v4l2_level = V4L2_MPEG_VIDEO_H264_LEVEL_2_1;
  else if (g_str_equal (level, "2.2"))
    v4l2_level = V4L2_MPEG_VIDEO_H264_LEVEL_2_2;
  else if (g_str_equal (level, "3"))
    v4l2_level = V4L2_MPEG_VIDEO_H264_LEVEL_3_0;
  else if (g_str_equal (level, "3.1"))
    v4l2_level = V4L2_MPEG_VIDEO_H264_LEVEL_3_1;
  else if (g_str_equal (level, "3.2"))
    v4l2_level = V4L2_MPEG_VIDEO_H264_LEVEL_3_2;
  else if (g_str_equal (level, "4"))
    v4l2_level = V4L2_MPEG_VIDEO_H264_LEVEL_4_0;
  else if (g_str_equal (level, "4.1"))
    v4l2_level = V4L2_MPEG_VIDEO_H264_LEVEL_4_1;
  else if (g_str_equal (level, "4.2"))
    v4l2_level = V4L2_MPEG_VIDEO_H264_LEVEL_4_2;
  else if (g_str_equal (level, "5"))
    v4l2_level = V4L2_MPEG_VIDEO_H264_LEVEL_5_0;
  else if (g_str_equal (level, "5.1"))
    v4l2_level = V4L2_MPEG_VIDEO_H264_LEVEL_5_1;
  else
    GST_WARNING ("Unsupported level '%s'", level);

  return v4l2_level;
}

static const gchar *
v4l2_level_to_string (gint v4l2_level)
{
  switch (v4l2_level) {
    case V4L2_MPEG_VIDEO_H264_LEVEL_1_0:
      return "1";
    case V4L2_MPEG_VIDEO_H264_LEVEL_1B:
      return "1b";
    case V4L2_MPEG_VIDEO_H264_LEVEL_1_1:
      return "1.1";
    case V4L2_MPEG_VIDEO_H264_LEVEL_1_2:
      return "1.2";
    case V4L2_MPEG_VIDEO_H264_LEVEL_1_3:
      return "1.3";
    case V4L2_MPEG_VIDEO_H264_LEVEL_2_0:
      return "2";
    case V4L2_MPEG_VIDEO_H264_LEVEL_2_1:
      return "2.1";
    case V4L2_MPEG_VIDEO_H264_LEVEL_2_2:
      return "2.2";
    case V4L2_MPEG_VIDEO_H264_LEVEL_3_0:
      return "3.0";
    case V4L2_MPEG_VIDEO_H264_LEVEL_3_1:
      return "3.1";
    case V4L2_MPEG_VIDEO_H264_LEVEL_3_2:
      return "3.2";
    case V4L2_MPEG_VIDEO_H264_LEVEL_4_0:
      return "4";
    case V4L2_MPEG_VIDEO_H264_LEVEL_4_1:
      return "4.1";
    case V4L2_MPEG_VIDEO_H264_LEVEL_4_2:
      return "4.2";
    case V4L2_MPEG_VIDEO_H264_LEVEL_5_0:
      return "5";
    case V4L2_MPEG_VIDEO_H264_LEVEL_5_1:
      return "5.1";
    default:
      GST_WARNING ("Unsupported V4L2 level %i", v4l2_level);
      break;
  }

  return NULL;
}

struct ProfileLevelCtx
{
  GstV4l2H264Enc *self;
  const gchar *profile;
  const gchar *level;
};

static gboolean
get_string_list (GstStructure * s, const gchar * field, GQueue * queue)
{
  const GValue *value;

  value = gst_structure_get_value (s, field);

  if (!value)
    return FALSE;

  if (GST_VALUE_HOLDS_LIST (value)) {
    guint i;

    if (gst_value_list_get_size (value) == 0)
      return FALSE;

    for (i = 0; i < gst_value_list_get_size (value); i++) {
      const GValue *item = gst_value_list_get_value (value, i);

      if (G_VALUE_HOLDS_STRING (item))
        g_queue_push_tail (queue, g_value_dup_string (item));
    }
  } else if (G_VALUE_HOLDS_STRING (value)) {
    g_queue_push_tail (queue, g_value_dup_string (value));
  }

  return TRUE;
}

static gboolean
negotiate_profile_and_level (GstCapsFeatures * features, GstStructure * s,
    gpointer user_data)
{
  struct ProfileLevelCtx *ctx = user_data;
  GstV4l2Object *v4l2object = GST_V4L2_VIDEO_ENC (ctx->self)->v4l2output;
  GQueue profiles = G_QUEUE_INIT;
  GQueue levels = G_QUEUE_INIT;
  gboolean failed = FALSE;

  if (get_string_list (s, "profile", &profiles)) {
    GList *l;

    for (l = profiles.head; l; l = l->next) {
      struct v4l2_control control = { 0, };
      gint v4l2_profile;
      const gchar *profile = l->data;

      GST_TRACE_OBJECT (ctx->self, "Trying profile %s", profile);

      control.id = V4L2_CID_MPEG_VIDEO_H264_PROFILE;
      control.value = v4l2_profile = v4l2_profile_from_string (profile);

      if (control.value < 0)
        continue;

      if (v4l2_ioctl (v4l2object->video_fd, VIDIOC_S_CTRL, &control) < 0) {
        GST_WARNING_OBJECT (ctx->self, "Failed to set H264 profile: '%s'",
            g_strerror (errno));
        break;
      }

      profile = v4l2_profile_to_string (control.value);

      if (control.value == v4l2_profile) {
        ctx->profile = profile;
        break;
      }

      if (g_list_find_custom (l, profile, g_str_equal)) {
        ctx->profile = profile;
        break;
      }
    }

    if (profiles.length && !ctx->profile)
      failed = TRUE;

    g_queue_foreach (&profiles, (GFunc) g_free, NULL);
    g_queue_clear (&profiles);
  }

  if (!failed && get_string_list (s, "level", &levels)) {
    GList *l;

    for (l = levels.head; l; l = l->next) {
      struct v4l2_control control = { 0, };
      gint v4l2_level;
      const gchar *level = l->data;

      GST_TRACE_OBJECT (ctx->self, "Trying level %s", level);

      control.id = V4L2_CID_MPEG_VIDEO_H264_LEVEL;
      control.value = v4l2_level = v4l2_level_from_string (level);

      if (control.value < 0)
        continue;

      if (v4l2_ioctl (v4l2object->video_fd, VIDIOC_S_CTRL, &control) < 0) {
        GST_WARNING_OBJECT (ctx->self, "Failed to set H264 level: '%s'",
            g_strerror (errno));
        break;
      }

      level = v4l2_level_to_string (control.value);

      if (control.value == v4l2_level) {
        ctx->level = level;
        break;
      }

      if (g_list_find_custom (l, level, g_str_equal)) {
        ctx->level = level;
        break;
      }
    }

    if (levels.length && !ctx->level)
      failed = TRUE;

    g_queue_foreach (&levels, (GFunc) g_free, NULL);
    g_queue_clear (&levels);
  }

  /* If it failed, we continue */
  return failed;
}

static gboolean
gst_v4l2_h264_enc_negotiate (GstVideoEncoder * encoder)
{
  GstV4l2H264Enc *self = GST_V4L2_H264_ENC (encoder);
  GstV4l2VideoEnc *venc = GST_V4L2_VIDEO_ENC (encoder);
  GstV4l2Object *v4l2object = venc->v4l2output;
  GstCaps *allowed_caps;
  struct ProfileLevelCtx ctx = { self, NULL, NULL };
  GstVideoCodecState *state;
  GstStructure *s;

  GST_DEBUG_OBJECT (self, "Negotiating H264 profile and level.");

  allowed_caps = gst_pad_get_allowed_caps (GST_VIDEO_ENCODER_SRC_PAD (encoder));

  if (allowed_caps) {

    if (gst_caps_is_empty (allowed_caps))
      goto not_negotiated;

    allowed_caps = gst_caps_make_writable (allowed_caps);

    /* negotiate_profile_and_level() will return TRUE on failure to keep
     * iterating, if gst_caps_foreach() returns TRUE it means there was no
     * compatible profile and level in any of the structure */
    if (gst_caps_foreach (allowed_caps, negotiate_profile_and_level, &ctx)) {
      goto no_profile_level;
    }
  }

  if (!ctx.profile) {
    struct v4l2_control control = { 0, };

    control.id = V4L2_CID_MPEG_VIDEO_H264_PROFILE;

    if (v4l2_ioctl (v4l2object->video_fd, VIDIOC_G_CTRL, &control) < 0)
      goto g_ctrl_failed;

    ctx.profile = v4l2_profile_to_string (control.value);
  }

  if (!ctx.level) {
    struct v4l2_control control = { 0, };

    control.id = V4L2_CID_MPEG_VIDEO_H264_LEVEL;

    if (v4l2_ioctl (v4l2object->video_fd, VIDIOC_G_CTRL, &control) < 0)
      goto g_ctrl_failed;

    ctx.level = v4l2_level_to_string (control.value);
  }

  GST_DEBUG_OBJECT (self, "Selected H264 profile %s at level %s",
      ctx.profile, ctx.level);

  state = gst_video_encoder_get_output_state (encoder);
  s = gst_caps_get_structure (state->caps, 0);
  gst_structure_set (s, "profile", G_TYPE_STRING, ctx.profile,
      "level", G_TYPE_STRING, ctx.level, NULL);

  return GST_VIDEO_ENCODER_CLASS (parent_class)->negotiate (encoder);

g_ctrl_failed:
  GST_WARNING_OBJECT (self, "Failed to get H264 profile and level: '%s'",
      g_strerror (errno));
  goto not_negotiated;

no_profile_level:
  GST_WARNING_OBJECT (self, "No compatible level and profiled in caps: %"
      GST_PTR_FORMAT, allowed_caps);
  goto not_negotiated;

not_negotiated:
  if (allowed_caps)
    gst_caps_unref (allowed_caps);
  return FALSE;
}

static void
gst_v4l2_h264_enc_init (GstV4l2H264Enc * self)
{

}

static void
gst_v4l2_h264_enc_class_init (GstV4l2H264EncClass * klass)
{
  GstElementClass *element_class;
  GObjectClass *gobject_class;
  GstVideoEncoderClass *baseclass;

  parent_class = g_type_class_peek_parent (klass);

  element_class = (GstElementClass *) klass;
  gobject_class = (GObjectClass *) klass;
  baseclass = GST_VIDEO_ENCODER_CLASS (klass);

  GST_DEBUG_CATEGORY_INIT (gst_v4l2_h264_enc_debug, "v4l2h264enc", 0,
      "V4L2 H.264 Encoder");

  gst_element_class_set_static_metadata (element_class,
      "V4L2 H.264 Encoder",
      "Codec/Encoder/Video",
      "Encode H.264 video streams via V4L2 API", "ayaka <ayaka@soulik.info>");

  gobject_class->set_property =
      GST_DEBUG_FUNCPTR (gst_v4l2_h264_enc_set_property);
  gobject_class->get_property =
      GST_DEBUG_FUNCPTR (gst_v4l2_h264_enc_get_property);
  baseclass->negotiate = GST_DEBUG_FUNCPTR (gst_v4l2_h264_enc_negotiate);
}

/* Probing functions */
gboolean
gst_v4l2_is_h264_enc (GstCaps * sink_caps, GstCaps * src_caps)
{
  gboolean ret = FALSE;
  GstCaps *codec_caps;

  codec_caps = gst_static_caps_get (&src_template_caps);

  if (gst_caps_is_subset (sink_caps, gst_v4l2_object_get_raw_caps ())
      && gst_caps_can_intersect (src_caps, codec_caps))
    ret = TRUE;

  gst_caps_unref (codec_caps);

  return ret;
}

gboolean
gst_v4l2_h264_enc_register (GstPlugin * plugin, const gchar * basename,
    const gchar * device_path, GstCaps * sink_caps, GstCaps * src_caps)
{
  return gst_v4l2_video_enc_register (plugin, GST_TYPE_V4L2_H264_ENC,
      "h264", basename, device_path, sink_caps,
      gst_static_caps_get (&src_template_caps), src_caps);
}
