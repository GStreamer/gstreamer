/* GStreamer
 *  Copyright (C) 2022 Intel Corporation
 *     Author: He Junyan <junyan.he@intel.com>
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

/**
 * SECTION:element-vavp9enc
 * @title: vavp9enc
 * @short_description: A VA-API based VP9 video encoder
 *
 * vavp9enc encodes raw video VA surfaces into VP9 bitstreams using
 * the installed and chosen [VA-API](https://01.org/linuxmedia/vaapi)
 * driver.
 *
 * The raw video frames in main memory can be imported into VA surfaces.
 *
 * ## Example launch line
 * ```
 * gst-launch-1.0 videotestsrc num-buffers=60 ! timeoverlay ! vavp9enc ! vp9parse ! mp4mux ! filesink location=test.mp4
 * ```
 *
 * Since: 1.24
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstvavp9enc.h"

#include <math.h>
#include <gst/codecparsers/gstvp9bitwriter.h>
#include <gst/va/gstva.h>
#include <gst/va/gstvavideoformat.h>
#include <gst/video/video.h>
#include <va/va_drmcommon.h>

#include "vacompat.h"
#include "gstvabaseenc.h"
#include "gstvaencoder.h"
#include "gstvacaps.h"
#include "gstvaprofile.h"
#include "gstvadisplay_priv.h"
#include "gstvapluginutils.h"

GST_DEBUG_CATEGORY_STATIC (gst_va_vp9enc_debug);
#define GST_CAT_DEFAULT gst_va_vp9enc_debug

#define GST_VA_VP9_ENC(obj)            ((GstVaVp9Enc *) obj)
#define GST_VA_VP9_ENC_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), G_TYPE_FROM_INSTANCE (obj), GstVaVp9EncClass))
#define GST_VA_VP9_ENC_CLASS(klass)    ((GstVaVp9EncClass *) klass)

typedef struct _GstVaVp9Enc GstVaVp9Enc;
typedef struct _GstVaVp9EncClass GstVaVp9EncClass;
typedef struct _GstVaVp9EncFrame GstVaVp9EncFrame;
typedef struct _GstVaVp9GFGroup GstVaVp9GFGroup;
typedef struct _GstVaVp9Ref GstVaVp9Ref;

enum
{
  PROP_KEYFRAME_INT = 1,
  PROP_GOLDEN_GROUP_SIZE,
  PROP_NUM_REF_FRAMES,
  PROP_HIERARCHICAL_LEVEL,
  PROP_BITRATE,
  PROP_TARGET_PERCENTAGE,
  PROP_TARGET_USAGE,
  PROP_CPB_SIZE,
  PROP_MBBRC,
  PROP_QP,
  PROP_MIN_QP,
  PROP_MAX_QP,
  PROP_LOOP_FILTER_LEVEL,
  PROP_SHARPNESS_LEVEL,
  PROP_RATE_CONTROL,
  N_PROPERTIES
};

static GParamSpec *properties[N_PROPERTIES];

static GstObjectClass *parent_class = NULL;

#define DEFAULT_BASE_QINDEX  60
#define DEFAULT_LOOP_FILTER_LEVEL  10
#define MAX_TILE_WIDTH_B64 64
#define MAX_FRAME_WIDTH 4096
#define MAX_FRAME_HEIGHT 4096
#define MAX_KEY_FRAME_INTERVAL  1024
#define MAX_GF_GROUP_SIZE  32
#define DEFAULT_GF_GROUP_SIZE  8
#define FRAME_TYPE_INVALID  -1
#define HIGHEST_PYRAMID_LEVELS  6
#define INVALID_PYRAMID_LEVEL  -1
#define FRAME_TYPE_REPEAT  0x80
/* The frame is golden frame */
#define FRAME_FLAG_GF  0x01
/* The frame is alt frame */
#define FRAME_FLAG_ALT  0x02
/* The frame is on the top level */
#define FRAME_FLAG_LEAF  0x04
/* The frame can be internal alt frame */
#define FRAME_FLAG_ALT_INL  0x08
/* The frame update the DPB reference */
#define FRAME_FLAG_UPDATE_REF  0x10
/* The frame is the last frame in a GF group */
#define FRAME_FLAG_LAST_IN_GF  0x20
/* The frame has already be encoded */
#define FRAME_FLAG_ALREADY_ENCODED  0x40
/* The frame is in a super frame */
#define FRAME_FLAG_IN_SUPER_FRAME  0x80
/* The frame has already outputted */
#define FRAME_FLAG_ALREADY_OUTPUTTED  0x100
/* The frame not show */
#define FRAME_FLAG_NOT_SHOW  0x200

struct _GstVaVp9GFGroup
{
  /* where this GF group start since key frame. */
  gint start_frame_offset;
  /* Total frame number of this group. */
  gint group_frame_num;
  /* Be different from group_frame_num, include repeat */
  gint output_frame_num;
  gint last_pushed_num;
  gint last_poped_index;
  guint8 highest_level;
  gboolean use_alt;
  GQueue *reorder_list;

  /* Include FRAME_TYPEs or FRAME_TYPE_REPEAT. */
  GstVp9FrameType frame_types[MAX_GF_GROUP_SIZE * 2];
  gint8 pyramid_levels[MAX_GF_GROUP_SIZE * 2];
  guint32 flags[MAX_GF_GROUP_SIZE * 2];
  /* offset from start_frame_num. */
  gint frame_offsets[MAX_GF_GROUP_SIZE * 2];
};

struct _GstVaVp9EncFrame
{
  GstVaEncFrame base;
  GstVp9FrameType type;
  /* VP9 does not define a frame number.
     This is a virtual number after the key frame. */
  gint frame_num;
  guint32 flags;
  guint pyramid_level;
  gboolean bidir_ref;
  gint8 ref_frame_idx[GST_VP9_REF_FRAME_MAX];
  /* The index in reference list to update */
  gint update_index;
  gint order_hint;
  /* repeat the current frame */
  gint8 repeat_index;
  guint repeat_frame_header_size;
  guint8 repeat_frame_header[32];
};

struct _GstVaVp9Ref
{
  GstVideoCodecFrame *frame;
  guint index_in_dpb;
};

struct _GstVaVp9EncClass
{
  GstVaBaseEncClass parent_class;

  GType rate_control_type;
  char rate_control_type_name[64];
  GEnumValue rate_control[16];
};

struct _GstVaVp9Enc
{
  /*< private > */
  GstVaBaseEnc parent;

  guint32 packed_headers;

  guint depth;
  guint chrome;

  /* properties */
  struct
  {
    /* kbps */
    guint bitrate;
    /* VA_RC_XXX */
    guint32 rc_ctrl;
    guint32 cpb_size;
    guint32 target_percentage;
    guint32 target_usage;
    guint keyframe_interval;
    guint max_hierarchical_level;
    guint gf_group_size;
    guint num_ref_frames;
    guint32 qp;
    guint32 min_qp;
    guint32 max_qp;
    guint32 mbbrc;
    gint32 filter_level;
    guint32 sharpness_level;
  } prop;

  struct
  {
    guint keyframe_interval;
    guint gf_group_size;
    guint max_level;
    guint num_ref_frames;
    guint forward_ref_num;
    guint backward_ref_num;
    guint frame_num_since_kf;
    GstVaVp9GFGroup current_group;
    GstVideoCodecFrame *last_keyframe;
    GstVideoCodecFrame *ref_list[GST_VP9_REF_FRAMES];
  } gop;

  struct
  {
    guint target_usage;
    guint32 target_percentage;
    guint32 cpb_size;
    guint32 cpb_length_bits;
    guint32 rc_ctrl_mode;
    guint max_bitrate;
    guint max_bitrate_bits;
    guint target_bitrate;
    guint target_bitrate_bits;
    guint32 base_qindex;
    guint32 min_qindex;
    guint32 max_qindex;
    guint32 mbbrc;
    gint32 filter_level;
    guint32 sharpness_level;
  } rc;

  /* The cached frames for super frame. */
  GstVideoCodecFrame *frames_in_super[GST_VP9_MAX_FRAMES_IN_SUPERFRAME - 1];
  guint frames_in_super_num;
};

static GstVaVp9EncFrame *
gst_va_vp9_enc_frame_new (void)
{
  GstVaVp9EncFrame *frame;

  frame = g_new (GstVaVp9EncFrame, 1);
  frame->frame_num = -1;
  frame->type = FRAME_TYPE_INVALID;
  frame->base.picture = NULL;
  frame->pyramid_level = 0;
  frame->flags = 0;
  frame->bidir_ref = FALSE;
  frame->update_index = -1;
  frame->order_hint = -1;
  frame->repeat_index = -1;
  frame->repeat_frame_header_size = 0;

  return frame;
}

static void
gst_va_vp9_enc_frame_free (gpointer pframe)
{
  GstVaVp9EncFrame *frame = pframe;

  g_clear_pointer (&frame->base.picture, gst_va_encode_picture_free);
  g_free (frame);
}

static gboolean
gst_va_vp9_enc_new_frame (GstVaBaseEnc * base, GstVideoCodecFrame * frame)
{
  GstVaVp9EncFrame *frame_in;

  frame_in = gst_va_vp9_enc_frame_new ();
  gst_va_set_enc_frame (frame, (GstVaEncFrame *) frame_in,
      gst_va_vp9_enc_frame_free);

  return TRUE;
}

static inline GstVaVp9EncFrame *
_enc_frame (GstVideoCodecFrame * frame)
{
  GstVaVp9EncFrame *enc_frame = gst_video_codec_frame_get_user_data (frame);

  g_assert (enc_frame);

  return enc_frame;
}

#ifndef GST_DISABLE_GST_DEBUG
static const char *
_vp9_get_frame_type_name (GstVp9FrameType frame_type)
{
  const gchar *frame_type_name = NULL;
  guint type = frame_type;

  if (type & FRAME_TYPE_REPEAT)
    return "Repeat";

  switch (type) {
    case GST_VP9_KEY_FRAME:
      frame_type_name = "Key";
      break;
    case GST_VP9_INTER_FRAME:
      frame_type_name = "Inter";
      break;
    default:
      frame_type_name = "Unknown";
      break;
  }

  return frame_type_name;
}

static const gchar *
_rate_control_get_name (guint32 rc_mode)
{
  GParamSpecEnum *spec;
  guint i;

  if (!(properties[PROP_RATE_CONTROL]
          && G_IS_PARAM_SPEC_ENUM (properties[PROP_RATE_CONTROL])))
    return NULL;

  spec = G_PARAM_SPEC_ENUM (properties[PROP_RATE_CONTROL]);
  for (i = 0; i < spec->enum_class->n_values; i++) {
    if (spec->enum_class->values[i].value == rc_mode)
      return spec->enum_class->values[i].value_nick;
  }

  return NULL;
}
#endif

static void
_vp9_print_gf_group (GstVaVp9Enc * self, GstVaVp9GFGroup * gf_group)
{
#ifndef GST_DISABLE_GST_DEBUG
  gint pushed_frame_num = gf_group->last_pushed_num < 0 ? 0 :
      gf_group->last_pushed_num - gf_group->start_frame_offset + 1;
  GString *str;
  gint i;

  if (gst_debug_category_get_threshold (GST_CAT_DEFAULT) < GST_LEVEL_LOG)
    return;

  str = g_string_new (NULL);

  g_string_append_printf (str, "\n============================"
      " GF Group ===========================\n");
  g_string_append_printf (str, " start:%d,  size:%d  ",
      gf_group->start_frame_offset, gf_group->group_frame_num);
  g_string_append_printf (str, "pushed:%d,  poped:%d  ",
      pushed_frame_num, gf_group->last_poped_index + 1);
  g_string_append_printf (str, "\n ALT: %s  max level: %d  output num: %d",
      gf_group->use_alt ? "yes" : "no", gf_group->highest_level,
      gf_group->output_frame_num);

  g_string_append_printf (str, "\n----------------------------"
      "-------------------------------------\n");
  g_string_append_printf (str, "|     |  type   | level |"
      "             flags            | offset |\n");

  g_string_append_printf (str, "----------------------------"
      "-------------------------------------\n");
  for (i = 0; i < gf_group->output_frame_num; i++) {
    g_string_append_printf (str, "| %3d ", i);
    g_string_append_printf (str, "| %-7s ",
        _vp9_get_frame_type_name (gf_group->frame_types[i]));

    g_string_append_printf (str, "| %5d | ", gf_group->pyramid_levels[i]);

    if (gf_group->flags[i] & FRAME_FLAG_GF) {
      g_string_append_printf (str, "GF ");
    } else {
      g_string_append_printf (str, "   ");
    }

    if (gf_group->flags[i] & FRAME_FLAG_LAST_IN_GF) {
      g_string_append_printf (str, "Last ");
    } else {
      g_string_append_printf (str, "     ");
    }

    if (gf_group->flags[i] & (FRAME_FLAG_ALT | FRAME_FLAG_ALT_INL)) {
      g_string_append_printf (str, "ALT ");
    } else {
      g_string_append_printf (str, "    ");
    }

    if (gf_group->flags[i] & FRAME_FLAG_NOT_SHOW) {
      g_string_append_printf (str, "Unshown ");
    } else {
      g_string_append_printf (str, "Shown   ");
    }

    if (gf_group->flags[i] & FRAME_FLAG_LEAF) {
      g_string_append_printf (str, "Leaf ");
    } else {
      g_string_append_printf (str, "     ");
    }

    if (gf_group->flags[i] & FRAME_FLAG_UPDATE_REF) {
      g_string_append_printf (str, "Ref ");
    } else {
      g_string_append_printf (str, "    ");
    }

    g_string_append_printf (str, "| %-5d  | ", gf_group->frame_offsets[i]);

    g_string_append_printf (str, "\n");
  }

  g_string_append_printf (str, "---------------------------"
      "--------------------------------------\n");

  GST_LOG_OBJECT (self, "%s", str->str);

  g_string_free (str, TRUE);

#endif
}

#ifndef GST_DISABLE_GST_DEBUG
static void
_vp9_print_ref_list (GstVaVp9Enc * self, GString * str)
{
  gint i;

  g_string_append_printf (str, "\n================== Reference List "
      "===================\n");

  g_string_append_printf (str, "|   index   |");
  for (i = 0; i < GST_VP9_REF_FRAMES; i++)
    g_string_append_printf (str, "%3d |", i);

  g_string_append_printf (str, "\n-------------------------------"
      "----------------------\n");

  g_string_append_printf (str, "| frame num |");
  for (i = 0; i < GST_VP9_REF_FRAMES; i++) {
    if (self->gop.ref_list[i]) {
      GstVaVp9EncFrame *va_frame = _enc_frame (self->gop.ref_list[i]);
      g_string_append_printf (str, "%3d |", va_frame->frame_num);
    } else {
      g_string_append_printf (str, "%3d |", -1);
    }
  }
  g_string_append_printf (str, "\n-------------------------------"
      "----------------------\n");
}
#endif

static void
_vp9_print_frame_reference (GstVaVp9Enc * self, GstVideoCodecFrame * frame)
{
#ifndef GST_DISABLE_GST_DEBUG
  GString *str;
  GstVaVp9EncFrame *va_frame;
  gint i;

  if (gst_debug_category_get_threshold (GST_CAT_DEFAULT) < GST_LEVEL_LOG)
    return;

  str = g_string_new (NULL);

  _vp9_print_ref_list (self, str);

  va_frame = _enc_frame (frame);

  g_string_append_printf (str, "Current %sframe num: %d,  ",
      va_frame->frame_num == 0 ? "key " : "", va_frame->frame_num);

  if (va_frame->type & FRAME_TYPE_REPEAT) {
    g_string_append_printf (str, "repeat index %d", va_frame->repeat_index);
    goto print;
  }

  g_string_append_printf (str, "Reference: [");

  for (i = GST_VP9_REF_FRAME_LAST; i < GST_VP9_REF_FRAME_MAX; i++) {
    switch (i) {
      case GST_VP9_REF_FRAME_LAST:
        g_string_append_printf (str, " %s", "Last");
        break;
      case GST_VP9_REF_FRAME_GOLDEN:
        g_string_append_printf (str, " %s", "Golden");
        break;
      case GST_VP9_REF_FRAME_ALTREF:
        g_string_append_printf (str, " %s", "Alt");
        break;
      default:
        g_assert_not_reached ();
        break;
    }
    g_string_append_printf (str, ":");

    if (va_frame->ref_frame_idx[i] == -1) {
      g_string_append_printf (str, "unused");
    } else {
      g_string_append_printf (str, "%d", va_frame->ref_frame_idx[i]);
    }

    if (i != GST_VP9_REF_FRAME_MAX - 1) {
      g_string_append_printf (str, ", ");
    } else {
      g_string_append_printf (str, " ");
    }
  }

  g_string_append_printf (str, "]");

print:
  GST_LOG_OBJECT (self, "%s", str->str);

  g_string_free (str, TRUE);
#endif
}

static void
_vp9_print_ref_list_update (GstVaVp9Enc * self, gint update_index,
    GstVideoCodecFrame * del_frame, GstVideoCodecFrame * add_frame)
{
#ifndef GST_DISABLE_GST_DEBUG
  GString *str;

  if (gst_debug_category_get_threshold (GST_CAT_DEFAULT) < GST_LEVEL_LOG)
    return;

  str = g_string_new (NULL);

  _vp9_print_ref_list (self, str);

  if (_enc_frame (add_frame)->frame_num == 0)
    g_string_append_printf (str, "Key frame clear all reference list.\n");

  if (update_index >= 0) {
    if (del_frame) {
      g_string_append_printf (str, "Replace index %d, delete frame num %d, "
          "add frame num %d.", update_index, _enc_frame (del_frame)->frame_num,
          _enc_frame (add_frame)->frame_num);
    } else {
      g_string_append_printf (str, "Add frame %d to index %d.",
          _enc_frame (add_frame)->frame_num, update_index);
    }
  }

  GST_LOG_OBJECT (self, "%s", str->str);

  g_string_free (str, TRUE);
#endif
}

/* [ start, end ) */
static void
_set_multi_layer (GstVaVp9GFGroup * gf_group, gint * frame_index,
    gint start, gint end, guint level, guint max_level)
{
  const gint num_frames_to_process = end - start;
  guint m = (start + end - 1) / 2;

  g_assert (level <= max_level);

  if (level == max_level || num_frames_to_process <= 2) {
    guint i;

    for (i = 0; i < num_frames_to_process; i++) {
      gf_group->frame_types[*frame_index] = GST_VP9_INTER_FRAME;
      gf_group->pyramid_levels[*frame_index] = level;
      gf_group->flags[*frame_index] = FRAME_FLAG_LEAF | FRAME_FLAG_UPDATE_REF;
      gf_group->frame_offsets[*frame_index] = start + i;
      (*frame_index)++;
    }
    return;
  }

  gf_group->frame_types[*frame_index] = GST_VP9_INTER_FRAME;
  gf_group->pyramid_levels[*frame_index] = level;
  gf_group->flags[*frame_index] = FRAME_FLAG_ALT_INL | FRAME_FLAG_UPDATE_REF;
  gf_group->flags[*frame_index] |= FRAME_FLAG_NOT_SHOW;
  gf_group->frame_offsets[*frame_index] = m;
  (*frame_index)++;

  /* The frames between start and this internal ALT */
  _set_multi_layer (gf_group, frame_index, start, m, level + 1, max_level);

  /* Repeat for this internal ALT frame */
  gf_group->frame_types[*frame_index] = FRAME_TYPE_REPEAT;
  gf_group->pyramid_levels[*frame_index] = -1;
  gf_group->flags[*frame_index] = 0;
  gf_group->frame_offsets[*frame_index] = m;
  (*frame_index)++;

  /* The frames between this internal ALT and end */
  _set_multi_layer (gf_group, frame_index, m + 1, end, level + 1, max_level);
}

static void
_vp9_init_gf_group (GstVaVp9GFGroup * gf_group, GQueue * reorder_list)
{
  guint i;

  gf_group->start_frame_offset = -1;
  gf_group->group_frame_num = 0;
  gf_group->last_pushed_num = -1;
  gf_group->use_alt = FALSE;
  gf_group->last_poped_index = -1;
  gf_group->output_frame_num = 0;

  for (i = 0; i < MAX_GF_GROUP_SIZE * 2; i++) {
    gf_group->frame_types[i] = FRAME_TYPE_INVALID;
    gf_group->pyramid_levels[i] = INVALID_PYRAMID_LEVEL;
    gf_group->flags[i] = 0;
    gf_group->frame_offsets[i] = -1;
  }

  /* We just use it, not own it. */
  gf_group->reorder_list = reorder_list;
}

static void
_vp9_start_gf_group (GstVaVp9Enc * self, GstVideoCodecFrame * gf_frame)
{
  GstVaVp9GFGroup *gf_group = &self->gop.current_group;
  guint group_size = self->gop.gf_group_size + 1;
  gboolean use_alt = self->gop.backward_ref_num > 0;
  guint max_level = self->gop.max_level;
  GstVaVp9EncFrame *frame = _enc_frame (gf_frame);
  gboolean key_frame_start = (frame->frame_num == 0);
  gint frame_index;
  guint i;

  if (use_alt) {
    /* At least 2 levels if bi-direction ref,
       1st for ALT, and 2nd for leaves. */
    g_assert (max_level >= 2);
  }

  /* + 1 for golden frame itself. */
  g_assert (group_size <= MAX_GF_GROUP_SIZE + 1);
  g_assert (max_level <= HIGHEST_PYRAMID_LEVELS);
  /* If size < 3, no backward ref needed. */
  g_assert (group_size > 3 || use_alt == FALSE);

  gf_group->start_frame_offset = frame->frame_num;
  gf_group->group_frame_num = group_size;
  gf_group->last_pushed_num = frame->frame_num;
  gf_group->use_alt = use_alt;

  gf_group->last_poped_index = -1;
  /* An already encoded frame as the GF,
     for example, the ALT of the previous GF group. */
  if (frame->flags & FRAME_FLAG_ALREADY_ENCODED)
    gf_group->last_poped_index = 0;

  for (i = 0; i < MAX_GF_GROUP_SIZE * 2; i++) {
    gf_group->frame_types[i] = FRAME_TYPE_INVALID;
    gf_group->pyramid_levels[i] = INVALID_PYRAMID_LEVEL;
    gf_group->flags[i] = 0;
    gf_group->frame_offsets[i] = -1;
  }

  frame_index = 0;
  /* GF frame is the first */
  gf_group->frame_types[frame_index] = key_frame_start ? GST_VP9_KEY_FRAME :
      GST_VP9_INTER_FRAME;
  gf_group->pyramid_levels[frame_index] = 0;
  gf_group->flags[frame_index] = FRAME_FLAG_GF;
  gf_group->flags[frame_index] |= FRAME_FLAG_UPDATE_REF;
  gf_group->frame_offsets[frame_index] = 0;
  frame_index++;

  /* No backward ref, in simple I/P mode */
  if (gf_group->use_alt == FALSE) {
    for (; frame_index < gf_group->group_frame_num; frame_index++) {
      gf_group->frame_types[frame_index] = GST_VP9_INTER_FRAME;
      gf_group->pyramid_levels[frame_index] = 1;
      gf_group->flags[frame_index] = FRAME_FLAG_UPDATE_REF | FRAME_FLAG_LEAF;
      if (frame_index == gf_group->group_frame_num - 1)
        gf_group->flags[frame_index] |= FRAME_FLAG_LAST_IN_GF;
      gf_group->frame_offsets[frame_index] = frame_index;
    }

    gf_group->output_frame_num = gf_group->group_frame_num;
    gf_group->highest_level = 1;

    _vp9_print_gf_group (self, gf_group);
    return;
  }

  /* ALT frame */
  gf_group->frame_types[frame_index] = GST_VP9_INTER_FRAME;
  gf_group->pyramid_levels[frame_index] = 1;
  gf_group->flags[frame_index] = FRAME_FLAG_ALT | FRAME_FLAG_UPDATE_REF;
  gf_group->flags[frame_index] |= FRAME_FLAG_LAST_IN_GF;
  gf_group->flags[frame_index] |= FRAME_FLAG_NOT_SHOW;
  gf_group->frame_offsets[frame_index] = gf_group->group_frame_num - 1;
  frame_index++;

  /* The frames between GF and ALT */
  _set_multi_layer (gf_group, &frame_index, 1,
      gf_group->group_frame_num - 1, 2, max_level);

  /* Repeat for ALT frame */
  gf_group->frame_types[frame_index] = FRAME_TYPE_REPEAT;
  gf_group->pyramid_levels[frame_index] = -1;
  gf_group->flags[frame_index] = 0;
  gf_group->frame_offsets[frame_index] = gf_group->group_frame_num - 1;
  frame_index++;

  gf_group->output_frame_num = frame_index;

  gf_group->highest_level = 0;
  for (i = 0; i < gf_group->output_frame_num; i++) {
    if (gf_group->highest_level < gf_group->pyramid_levels[i])
      gf_group->highest_level = gf_group->pyramid_levels[i];
  }

  _vp9_print_gf_group (self, gf_group);
  return;
}

static gboolean
_vp9_gf_group_push_frame (GstVaVp9GFGroup * gf_group,
    GstVideoCodecFrame * gst_frame)
{
  GstVaVp9EncFrame *frame = _enc_frame (gst_frame);
  gint pushed_frame_num = gf_group->last_pushed_num < 0 ? 0 :
      gf_group->last_pushed_num - gf_group->start_frame_offset + 1;

  /* No room for a new one. */
  g_return_val_if_fail (pushed_frame_num < gf_group->group_frame_num, FALSE);
  /* The frame num should just increase. */
  g_return_val_if_fail (frame->frame_num == gf_group->last_pushed_num + 1,
      FALSE);

  if (gf_group->use_alt)
    /* If we already begin pop, no more push again. */
    g_return_val_if_fail (gf_group->last_poped_index <= 0, FALSE);

  g_queue_push_tail (gf_group->reorder_list,
      gst_video_codec_frame_ref (gst_frame));

  gf_group->last_pushed_num = frame->frame_num;
  return TRUE;
}

static gboolean
_vp9_gf_group_pop_frame (GstVaVp9GFGroup * gf_group,
    GstVideoCodecFrame * ref_list[GST_VP9_REF_FRAMES],
    GstVideoCodecFrame ** out_frame)
{
  GstVaVp9EncFrame *vaframe;
  GstVideoCodecFrame *frame;
  gint pushed_frame_num = gf_group->last_pushed_num < 0 ? 0 :
      gf_group->last_pushed_num - gf_group->start_frame_offset + 1;
  guint i;

  g_assert (pushed_frame_num <= gf_group->group_frame_num);

  if (pushed_frame_num == 0)
    goto no_frame;

  if (!gf_group->use_alt) {
    g_assert (gf_group->last_poped_index < pushed_frame_num);

    if (gf_group->last_poped_index + 1 < pushed_frame_num) {
      gf_group->last_poped_index++;
      goto find_frame;
    }

    goto no_frame;
  }

  /* The first frame of a GF group has no backward ref, pop immediately. */
  if (gf_group->last_poped_index < 0) {
    gf_group->last_poped_index++;
    goto find_frame;
  }

  /* The ALT frame has not come. */
  if (pushed_frame_num < gf_group->group_frame_num)
    goto no_frame;

  gf_group->last_poped_index++;
  g_assert (gf_group->last_poped_index < gf_group->output_frame_num);

find_frame:
  vaframe = NULL;
  frame = NULL;

  /* If repeating some frame, it should be in reference list,
     or it should be in reorder list. */
  if (gf_group->frame_types[gf_group->last_poped_index] == FRAME_TYPE_REPEAT) {
    for (i = 0; i < GST_VP9_REF_FRAMES; i++) {
      GstVaVp9EncFrame *vaf;

      if (ref_list[i] == NULL)
        continue;

      vaf = _enc_frame (ref_list[i]);
      if (vaf->frame_num == gf_group->start_frame_offset +
          gf_group->frame_offsets[gf_group->last_poped_index]) {
        vaframe = vaf;
        frame = ref_list[i];
        break;
      }
    }

    g_return_val_if_fail (vaframe, FALSE);

    g_assert (vaframe->flags & FRAME_FLAG_ALREADY_ENCODED);
    vaframe->type |= FRAME_TYPE_REPEAT;
  } else {
    for (i = 0; i < g_queue_get_length (gf_group->reorder_list); i++) {
      GstVaVp9EncFrame *vaf;
      GstVideoCodecFrame *f;

      f = g_queue_peek_nth (gf_group->reorder_list, i);
      vaf = _enc_frame (f);
      if (vaf->frame_num == gf_group->start_frame_offset +
          gf_group->frame_offsets[gf_group->last_poped_index]) {
        vaframe = vaf;
        frame = f;
        break;
      }
    }
    /* We push the frame num in increment order, so it must exist. */
    g_return_val_if_fail (vaframe, FALSE);
    /* Clear that frame from reorder list. */
    g_queue_pop_nth (gf_group->reorder_list, i);

    vaframe->type = gf_group->frame_types[gf_group->last_poped_index];
    vaframe->pyramid_level =
        gf_group->pyramid_levels[gf_group->last_poped_index];
    vaframe->flags = gf_group->flags[gf_group->last_poped_index];

    /* unref frame popped from reorder queue */
    gst_video_codec_frame_unref (frame);
  }

  *out_frame = frame;
  return TRUE;

no_frame:
  *out_frame = NULL;
  return TRUE;
}

/* Force to finish current group, no matter how many frames we have. */
static void
_vp9_finish_current_gf_group (GstVaVp9Enc * self, GstVaVp9GFGroup * gf_group)
{
  gint frame_index;
  gint pushed_frame_num = gf_group->last_pushed_num < 0 ? 0 :
      gf_group->last_pushed_num - gf_group->start_frame_offset + 1;
  guint i;

  g_assert (pushed_frame_num <= gf_group->group_frame_num);

  /* Alt comes and already finished. */
  if (gf_group->use_alt && gf_group->last_poped_index > 0)
    return;

  /* Already pushed all frames. */
  if (pushed_frame_num == gf_group->group_frame_num)
    return;

  /* Not enough frames, no need to use backward ref. */
  if (gf_group->use_alt && pushed_frame_num <= 3)
    gf_group->use_alt = FALSE;

  if (gf_group->use_alt == FALSE) {
    g_assert (gf_group->last_poped_index < pushed_frame_num);

    gf_group->group_frame_num = pushed_frame_num;

    for (frame_index = 1; frame_index < gf_group->group_frame_num;
        frame_index++) {
      gf_group->frame_types[frame_index] = GST_VP9_INTER_FRAME;
      gf_group->pyramid_levels[frame_index] = 1;
      gf_group->flags[frame_index] = FRAME_FLAG_UPDATE_REF | FRAME_FLAG_LEAF;
      gf_group->frame_offsets[frame_index] = frame_index;
      if (frame_index == gf_group->group_frame_num - 1) {
        gf_group->flags[frame_index] |= FRAME_FLAG_LAST_IN_GF;
      }

    }

    gf_group->output_frame_num = gf_group->group_frame_num;
    gf_group->highest_level = 1;

    GST_LOG_OBJECT (self, "Finish current golden group.");
    _vp9_print_gf_group (self, gf_group);
    return;
  }

  g_assert (gf_group->highest_level >= 2);

  gf_group->group_frame_num = pushed_frame_num;

  frame_index = 1;
  /* ALT frame */
  gf_group->frame_types[frame_index] = GST_VP9_INTER_FRAME;
  gf_group->pyramid_levels[frame_index] = 1;
  gf_group->flags[frame_index] = FRAME_FLAG_ALT | FRAME_FLAG_UPDATE_REF;
  gf_group->flags[frame_index] |= FRAME_FLAG_LAST_IN_GF;
  gf_group->flags[frame_index] |= FRAME_FLAG_NOT_SHOW;
  gf_group->frame_offsets[frame_index] = gf_group->group_frame_num - 1;
  frame_index++;

  /* The frames between GF and ALT */
  _set_multi_layer (gf_group, &frame_index, 1, gf_group->group_frame_num - 1,
      2, gf_group->highest_level);

  /* Repeat of ALT frame */
  gf_group->frame_types[frame_index] = FRAME_TYPE_REPEAT;
  gf_group->pyramid_levels[frame_index] = -1;
  gf_group->flags[frame_index] = 0;
  gf_group->frame_offsets[frame_index] = gf_group->group_frame_num - 1;
  frame_index++;

  gf_group->output_frame_num = frame_index;

  gf_group->highest_level = 0;
  for (i = 0; i < gf_group->output_frame_num; i++) {
    if (gf_group->highest_level < gf_group->pyramid_levels[i])
      gf_group->highest_level = gf_group->pyramid_levels[i];
  }

  GST_LOG_OBJECT (self, "Finish current golden group.");
  _vp9_print_gf_group (self, gf_group);
  return;
}

static inline gboolean
_vp9_gf_group_is_empty (GstVaVp9GFGroup * gf_group)
{
  gint pushed_frame_num = gf_group->last_pushed_num < 0 ? 0 :
      gf_group->last_pushed_num - gf_group->start_frame_offset + 1;

  if (pushed_frame_num <= 0)
    return TRUE;

  if (gf_group->use_alt == FALSE) {
    g_assert (gf_group->last_poped_index + 1 <= pushed_frame_num);
    if (gf_group->last_poped_index + 1 == pushed_frame_num)
      return TRUE;

    return FALSE;
  }

  g_assert (gf_group->last_poped_index < gf_group->output_frame_num);
  if (gf_group->last_poped_index == gf_group->output_frame_num - 1)
    return TRUE;

  return FALSE;
}

static inline gboolean
_vp9_gf_group_is_finished (GstVaVp9GFGroup * gf_group)
{
  g_assert (gf_group->last_poped_index < gf_group->output_frame_num);
  if (gf_group->last_poped_index == gf_group->output_frame_num - 1)
    return TRUE;

  return FALSE;
}

static GstVideoCodecFrame *
_vp9_find_next_golden_frame (GstVaVp9Enc * self)
{
  guint i;
  GstVideoCodecFrame *f, *f_max_frame_num;
  GstVaVp9EncFrame *vaf;
  gint max_frame_num;

  g_assert (_vp9_gf_group_is_empty (&self->gop.current_group));

  f = NULL;
  f_max_frame_num = NULL;
  max_frame_num = -1;
  for (i = 0; i < GST_VP9_REF_FRAMES; i++) {
    if (self->gop.ref_list[i] == NULL)
      continue;

    vaf = _enc_frame (self->gop.ref_list[i]);
    if (vaf->flags & FRAME_FLAG_LAST_IN_GF) {
      /* Should not have 2 of group end frame at the same time. */
      g_assert (f == NULL);
      f = self->gop.ref_list[i];
    }

    if (vaf->frame_num > max_frame_num) {
      max_frame_num = vaf->frame_num;
      f_max_frame_num = self->gop.ref_list[i];
    }

    /* clear all flags about last GF group. */
    vaf->flags &= ~(FRAME_FLAG_LAST_IN_GF | FRAME_FLAG_ALT_INL |
        FRAME_FLAG_ALT | FRAME_FLAG_GF);
  }

  if (f == NULL)
    f = f_max_frame_num;

  vaf = _enc_frame (f);
  vaf->flags |= FRAME_FLAG_GF;

  GST_LOG_OBJECT (self, "Find the next golden frame num %d", vaf->frame_num);

  return f;
}

static gboolean
gst_va_vp9_enc_reorder_frame (GstVaBaseEnc * base, GstVideoCodecFrame * frame,
    gboolean bump_all, GstVideoCodecFrame ** out_frame)
{
  GstVaVp9Enc *self = GST_VA_VP9_ENC (base);
  GstVaVp9EncFrame *va_frame;

  *out_frame = NULL;

  if (bump_all) {
    g_return_val_if_fail (frame == NULL, FALSE);

    _vp9_finish_current_gf_group (self, &self->gop.current_group);

    if (!_vp9_gf_group_is_finished (&self->gop.current_group)) {
      g_assert (!_vp9_gf_group_is_empty (&self->gop.current_group));
      goto pop;
    }

    /* no more frames, the cached key frame is the last frame */
    if (self->gop.last_keyframe) {
      g_assert (_vp9_gf_group_is_empty (&self->gop.current_group));

      *out_frame = self->gop.last_keyframe;
      self->gop.last_keyframe = NULL;
    }

    goto finish;
  }

  /* Pop only. We can pop some frame if:
     1. The current GF group is not finished.
     2. Encountered a key frame last time and force to finish
     the current GF group. */
  if (frame == NULL) {
    if (!_vp9_gf_group_is_empty (&self->gop.current_group))
      goto pop;

    if (self->gop.last_keyframe) {
      GstVideoCodecFrame *f = self->gop.last_keyframe;
      self->gop.last_keyframe = NULL;

      _vp9_start_gf_group (self, f);
      goto pop;
    }

    goto finish;
  }

  if (self->gop.frame_num_since_kf == self->gop.keyframe_interval)
    self->gop.frame_num_since_kf = 0;

  if (GST_VIDEO_CODEC_FRAME_IS_FORCE_KEYFRAME (frame))
    self->gop.frame_num_since_kf = 0;

  va_frame = _enc_frame (frame);
  va_frame->frame_num = self->gop.frame_num_since_kf;
  self->gop.frame_num_since_kf++;

  GST_LOG_OBJECT (self, "push frame: system_frame_number %d, frame_num: %d",
      frame->system_frame_number, va_frame->frame_num);

  /* A new key frame force to finish the current GF group. */
  if (va_frame->frame_num == 0) {
    _vp9_finish_current_gf_group (self, &self->gop.current_group);

    g_queue_push_tail (&base->reorder_list, gst_video_codec_frame_ref (frame));

    if (_vp9_gf_group_is_finished (&self->gop.current_group)) {
      g_assert (_vp9_gf_group_is_empty (&self->gop.current_group));

      /* Already poped all of the last group,
         so begin a new group with this keyframe. */
      _vp9_start_gf_group (self, frame);
    } else {
      g_assert (!_vp9_gf_group_is_empty (&self->gop.current_group));

      /* The reorder() should exhaust all available frames in the
         reorder list before push a frame again, so the last key
         frame should already be popped. */
      g_return_val_if_fail (self->gop.last_keyframe == NULL, FALSE);
      self->gop.last_keyframe = frame;
    }

    goto pop;
  }

  if (_vp9_gf_group_is_finished (&self->gop.current_group)) {
    GstVideoCodecFrame *gf_frame;

    g_assert (_vp9_gf_group_is_empty (&self->gop.current_group));

    gf_frame = _vp9_find_next_golden_frame (self);
    /* At least, there are some frames inside the reference list. */
    g_assert (gf_frame);

    _vp9_start_gf_group (self, gf_frame);
  }

  if (!_vp9_gf_group_push_frame (&self->gop.current_group, frame)) {
    GST_WARNING_OBJECT (base, "Failed to push the frame,"
        " system_frame_number %d.", frame->system_frame_number);
    goto error;
  }

pop:
  frame = NULL;

  if (!_vp9_gf_group_pop_frame (&self->gop.current_group, self->gop.ref_list,
          out_frame))
    goto error;

finish:
  if (*out_frame) {
    va_frame = _enc_frame (*out_frame);
    GST_LOG_OBJECT (self, "pop frame: system_frame_number %d,"
        " frame_num: %d, frame_type %s", (*out_frame)->system_frame_number,
        va_frame->frame_num, _vp9_get_frame_type_name (va_frame->type));
  }

  return TRUE;

error:
  if (frame) {
    GST_ERROR_OBJECT (base, "Failed to reorder the frame,"
        " system_frame_number %d.", frame->system_frame_number);
  } else {
    GST_ERROR_OBJECT (base, "error when poping frame.");
  }
  return FALSE;
}

static gint
_vp9_sort_by_frame_num (gconstpointer a, gconstpointer b, gpointer user_data)
{
  GstVaVp9EncFrame *frame1 = _enc_frame (((GstVaVp9Ref *) a)->frame);
  GstVaVp9EncFrame *frame2 = _enc_frame (((GstVaVp9Ref *) b)->frame);

  g_assert (frame1->frame_num != frame2->frame_num);

  return frame1->frame_num - frame2->frame_num;
}

static gboolean
_vp9_assign_ref_index (GstVaVp9Enc * self, GstVideoCodecFrame * frame)
{
  GstVaVp9Ref all_refs[GST_VP9_REF_FRAMES];
  guint ref_num;
  gint forward_num, backward_num;
  gint forward_ref_num;
  GstVaVp9EncFrame *va_frame = _enc_frame (frame);
  gint i, index;
  gboolean gf_assigned;

  memset (va_frame->ref_frame_idx, -1, sizeof (va_frame->ref_frame_idx));

  if (va_frame->type & FRAME_TYPE_REPEAT) {
    va_frame->repeat_index = -1;

    for (i = 0; i < GST_VP9_REF_FRAMES; i++) {
      if (self->gop.ref_list[i] == frame) {
        va_frame->repeat_index = i;
        break;
      }
    }

    g_return_val_if_fail (va_frame->repeat_index >= 0, FALSE);
    goto finish;
  }

  /* key frame has no ref */
  if (va_frame->frame_num == 0) {
    g_assert (va_frame->type == GST_VP9_KEY_FRAME);
    va_frame->bidir_ref = FALSE;
    goto finish;
  }

  ref_num = forward_num = backward_num = 0;
  for (i = 0; i < GST_VP9_REF_FRAMES; i++) {
    GstVaVp9EncFrame *va_f;

    if (self->gop.ref_list[i] == NULL)
      continue;

    all_refs[ref_num].frame = self->gop.ref_list[i];
    all_refs[ref_num].index_in_dpb = i;
    ref_num++;

    va_f = _enc_frame (self->gop.ref_list[i]);
    g_assert (va_f->frame_num != va_frame->frame_num);
    if (va_f->frame_num < va_frame->frame_num) {
      forward_num++;
    } else {
      backward_num++;
      g_assert (va_f->flags & FRAME_FLAG_ALT ||
          va_f->flags & FRAME_FLAG_ALT_INL);
    }

    if (va_f->flags & FRAME_FLAG_GF)
      va_frame->ref_frame_idx[GST_VP9_REF_FRAME_GOLDEN] = i;
  }

  if (va_frame->ref_frame_idx[GST_VP9_REF_FRAME_GOLDEN] == -1) {
    GST_WARNING_OBJECT (self, "failed to find the golden frame.");
    return FALSE;
  }

  g_qsort_with_data (all_refs, ref_num, sizeof (GstVaVp9Ref),
      _vp9_sort_by_frame_num, NULL);

  /* Assign the forward references in order of:
   * 1. The last frame which has the smallest diff.
   * 2. The golden frame which can be a key frame with better quality.
   * 3. The other frames by inverse frame number order.
   */
  va_frame->bidir_ref = FALSE;
  gf_assigned = FALSE;

  index = forward_num - 1;
  g_assert (index >= 0);
  forward_ref_num = self->gop.forward_ref_num;
  g_assert (forward_ref_num > 0);

  /* The golden frame happens to be the last frame. */
  if (all_refs[index].index_in_dpb ==
      va_frame->ref_frame_idx[GST_VP9_REF_FRAME_GOLDEN]) {
    index--;
    forward_ref_num--;
    gf_assigned = TRUE;
  }

  if (index >= 0 && forward_ref_num > 0) {
    va_frame->ref_frame_idx[GST_VP9_REF_FRAME_LAST] =
        all_refs[index].index_in_dpb;
    index--;
    forward_ref_num--;
  } else {
    /* At least one forward reference.
       Just let the last frame be the same as the golden frame. */
    g_assert (gf_assigned);
    va_frame->ref_frame_idx[GST_VP9_REF_FRAME_LAST] =
        va_frame->ref_frame_idx[GST_VP9_REF_FRAME_GOLDEN];
  }

  if (!gf_assigned) {
    if (forward_ref_num == 0) {
      va_frame->ref_frame_idx[GST_VP9_REF_FRAME_GOLDEN] =
          va_frame->ref_frame_idx[GST_VP9_REF_FRAME_LAST];
    } else {
      /* The golden frame index is already found. */
      forward_ref_num--;

      if (index >= 0 && all_refs[index].index_in_dpb ==
          va_frame->ref_frame_idx[GST_VP9_REF_FRAME_GOLDEN])
        index--;
    }
  }

  /* Setting the backward refs */
  if (backward_num > 0 && self->gop.backward_ref_num > 0) {
    g_assert (self->gop.backward_ref_num == 1);
    g_assert (_enc_frame (all_refs[ref_num - 1].frame)->flags & FRAME_FLAG_ALT);

    va_frame->bidir_ref = TRUE;
    /* Set the ALTREF to the nearest future frame. */
    va_frame->ref_frame_idx[GST_VP9_REF_FRAME_ALTREF] =
        all_refs[forward_num].index_in_dpb;
  } else {
    /* If no backward refs, ALTREF is set to next forward. */
    if (index >= 0 && forward_ref_num > 0) {
      va_frame->ref_frame_idx[GST_VP9_REF_FRAME_ALTREF] =
          all_refs[index].index_in_dpb;
    } else {
      va_frame->ref_frame_idx[GST_VP9_REF_FRAME_ALTREF] =
          va_frame->ref_frame_idx[GST_VP9_REF_FRAME_GOLDEN];
    }
  }

finish:
  _vp9_print_frame_reference (self, frame);
  return TRUE;
}

static void
_vp9_find_ref_to_update (GstVaBaseEnc * base, GstVideoCodecFrame * frame)
{
  GstVaVp9Enc *self = GST_VA_VP9_ENC (base);
  GstVaVp9EncFrame *va_frame = _enc_frame (frame);
  gint slot;
  gint lowest_slot;
  gint lowest_frame_num = MAX_KEY_FRAME_INTERVAL + 1;
  gint i;

  if (va_frame->type & FRAME_TYPE_REPEAT)
    return;

  if ((va_frame->flags & FRAME_FLAG_UPDATE_REF) == 0) {
    /* Key frame should always clean the reference list. */
    g_assert (va_frame->type != GST_VP9_KEY_FRAME);
    return;
  }

  va_frame->update_index = -1;

  /* key frame will clear the whole ref list, just use the 0 */
  if (va_frame->type == GST_VP9_KEY_FRAME) {
    va_frame->update_index = 0;
    return;
  }

  /* 1. Find an empty slot in the reference list.
     2. If the list is full, kick out the non GF frame with lowest
     frame num. GF frame should not be kicked out because we always
     set GOLDEN to GF frame.
     3. If still not find, we drop ourself. */
  lowest_frame_num = MAX_KEY_FRAME_INTERVAL + 1;
  slot = lowest_slot = -1;
  for (i = 0; i < GST_VP9_REF_FRAMES; i++) {
    GstVaVp9EncFrame *va_f;

    if (self->gop.ref_list[i] == NULL) {
      slot = i;
      break;
    }

    va_f = _enc_frame (self->gop.ref_list[i]);
    if (va_f->flags & FRAME_FLAG_GF)
      continue;

    if (va_f->frame_num > va_frame->frame_num)
      continue;

    if (va_f->frame_num < lowest_frame_num) {
      lowest_frame_num = va_f->frame_num;
      lowest_slot = i;
    }
  }

  if (slot < 0 && lowest_slot >= 0)
    slot = lowest_slot;

  if (slot >= 0)
    va_frame->update_index = slot;
}

static void
_vp9_update_ref_list (GstVaBaseEnc * base, GstVideoCodecFrame * frame)
{
  GstVaVp9Enc *self = GST_VA_VP9_ENC (base);
  GstVaVp9EncFrame *va_frame = _enc_frame (frame);
  GstVideoCodecFrame *del_f;
  gint i;

  if (va_frame->type & FRAME_TYPE_REPEAT)
    return;

  /* key frame, clear the whole ref list. */
  if (va_frame->type == GST_VP9_KEY_FRAME) {
    g_assert (va_frame->update_index == 0);
    g_assert (va_frame->flags & FRAME_FLAG_UPDATE_REF);

    for (i = 0; i < GST_VP9_REF_FRAMES; i++) {
      if (self->gop.ref_list[i] == NULL)
        continue;

      g_queue_remove (&base->ref_list, self->gop.ref_list[i]);
      gst_video_codec_frame_unref (self->gop.ref_list[i]);
      self->gop.ref_list[i] = NULL;
    }

    g_assert (g_queue_is_empty (&base->ref_list));
  }

  /* We drop ourself. */
  if (va_frame->update_index < 0) {
    GST_DEBUG_OBJECT (self, "Drop the non ref frame %d,"
        " reference list unchanged", va_frame->frame_num);
    return;
  }

  del_f = self->gop.ref_list[va_frame->update_index];

  g_queue_push_tail (&base->ref_list, gst_video_codec_frame_ref (frame));
  self->gop.ref_list[va_frame->update_index] = frame;

  _vp9_print_ref_list_update (self, va_frame->update_index, del_f, frame);

  if (del_f) {
    g_queue_remove (&base->ref_list, del_f);
    gst_video_codec_frame_unref (del_f);
  }
}

static void
gst_va_vp9_enc_reset_state (GstVaBaseEnc * base)
{
  GstVaVp9Enc *self = GST_VA_VP9_ENC (base);

  GST_VA_BASE_ENC_CLASS (parent_class)->reset_state (base);

  GST_OBJECT_LOCK (self);
  self->rc.rc_ctrl_mode = self->prop.rc_ctrl;
  self->rc.target_usage = self->prop.target_usage;
  self->rc.base_qindex = self->prop.qp;
  self->rc.min_qindex = self->prop.min_qp;
  self->rc.max_qindex = self->prop.max_qp;
  self->rc.target_percentage = self->prop.target_percentage;
  self->rc.cpb_size = self->prop.cpb_size;
  self->rc.mbbrc = self->prop.mbbrc;
  self->rc.filter_level = self->prop.filter_level;
  self->rc.sharpness_level = self->prop.sharpness_level;

  self->gop.keyframe_interval = self->prop.keyframe_interval;
  self->gop.gf_group_size = self->prop.gf_group_size;
  self->gop.num_ref_frames = self->prop.num_ref_frames;
  self->gop.max_level = self->prop.max_hierarchical_level;
  GST_OBJECT_UNLOCK (self);

  self->packed_headers = 0;

  self->depth = 0;
  self->chrome = 0;

  _vp9_init_gf_group (&self->gop.current_group, &base->reorder_list);
  self->gop.last_keyframe = NULL;
  memset (self->gop.ref_list, 0, sizeof (self->gop.ref_list));
  self->gop.frame_num_since_kf = 0;
  self->gop.forward_ref_num = 0;
  self->gop.backward_ref_num = 0;

  self->rc.max_bitrate = 0;
  self->rc.target_bitrate = 0;
  self->rc.max_bitrate_bits = 0;
  self->rc.cpb_length_bits = 0;

  memset (self->frames_in_super, 0, sizeof (self->frames_in_super));
  self->frames_in_super_num = 0;
}

static guint
_vp9_get_rtformat (GstVaVp9Enc * self, GstVideoFormat format,
    guint * depth, guint * chrome)
{
  guint chroma;

  chroma = gst_va_chroma_from_video_format (format);

  switch (chroma) {
    case VA_RT_FORMAT_YUV400:
      *depth = 8;
      *chrome = 0;
      break;
    case VA_RT_FORMAT_YUV420:
      *depth = 8;
      *chrome = 1;
      break;
    case VA_RT_FORMAT_YUV420_10:
      *depth = 10;
      *chrome = 1;
      break;
    case VA_RT_FORMAT_YUV444:
      *depth = 8;
      *chrome = 3;
      break;
    case VA_RT_FORMAT_YUV422_10:
      *depth = 10;
      *chrome = 2;
      break;
    case VA_RT_FORMAT_YUV444_10:
      *depth = 10;
      *chrome = 3;
      break;
    default:
      chroma = 0;
      GST_ERROR_OBJECT (self, "Unsupported chroma for video format: %s",
          gst_video_format_to_string (format));
      break;
  }

  return chroma;
}

#define update_property(type, obj, old_val, new_val, prop_id)           \
  gst_va_base_enc_update_property_##type (obj, old_val, new_val, properties[prop_id])
#define update_property_uint(obj, old_val, new_val, prop_id)    \
  update_property (uint, obj, old_val, new_val, prop_id)
#define update_property_bool(obj, old_val, new_val, prop_id)    \
  update_property (bool, obj, old_val, new_val, prop_id)

static VAProfile
_vp9_decide_profile (GstVaVp9Enc * self, guint rt_format,
    guint depth, guint chrome)
{
  GstVaBaseEnc *base = GST_VA_BASE_ENC (self);
  GstCaps *allowed_caps = NULL;
  guint num_structures, i;
  GstStructure *structure;
  const GValue *v_profile;
  GArray *candidates = NULL;
  VAProfile va_profile, ret_profile = VAProfileNone;

  candidates = g_array_new (TRUE, TRUE, sizeof (VAProfile));

  /* First, check whether the downstream requires a specified profile. */
  allowed_caps = gst_pad_get_allowed_caps (GST_VIDEO_ENCODER_SRC_PAD (base));
  if (!allowed_caps)
    allowed_caps = gst_pad_query_caps (GST_VIDEO_ENCODER_SRC_PAD (base), NULL);

  if (allowed_caps && !gst_caps_is_empty (allowed_caps)) {
    num_structures = gst_caps_get_size (allowed_caps);
    for (i = 0; i < num_structures; i++) {
      structure = gst_caps_get_structure (allowed_caps, i);
      v_profile = gst_structure_get_value (structure, "profile");
      if (!v_profile)
        continue;

      if (G_VALUE_HOLDS_STRING (v_profile)) {
        va_profile =
            gst_va_profile_from_name (VP9, g_value_get_string (v_profile));
        g_array_append_val (candidates, va_profile);
      } else if (GST_VALUE_HOLDS_LIST (v_profile)) {
        guint j;

        for (j = 0; j < gst_value_list_get_size (v_profile); j++) {
          const GValue *p = gst_value_list_get_value (v_profile, j);
          if (!p)
            continue;

          va_profile = gst_va_profile_from_name (VP9, g_value_get_string (p));
          g_array_append_val (candidates, va_profile);
        }
      }
    }
  }

  if (candidates->len == 0) {
    GST_ERROR_OBJECT (self, "No available profile in caps");
    goto out;
  }

  va_profile = VAProfileNone;
  /* Profile Color | Depth Chroma | Subsampling
     0             | 8 bit/sample | 4:2:0
     1             | 8 bit        | 4:2:2, 4:4:4
     2             | 10 or 12 bit | 4:2:0
     3             | 10 or 12 bit | 4:2:2, 4:4:4     */
  if (chrome == 3 || chrome == 2) {
    /* 4:4:4 and 4:2:2 */
    if (depth == 8) {
      va_profile = VAProfileVP9Profile1;
    } else if (depth == 10 || depth == 12) {
      va_profile = VAProfileVP9Profile3;
    }
  } else if (chrome == 1) {
    /* 4:2:0 */
    if (depth == 8) {
      va_profile = VAProfileVP9Profile0;
    } else if (depth == 10 || depth == 12) {
      va_profile = VAProfileVP9Profile2;
    }
  }

  if (va_profile == VAProfileNone) {
    GST_ERROR_OBJECT (self, "Fails to find a suitable profile");
    goto out;
  }

  for (i = 0; i < candidates->len; i++) {
    VAProfile p;

    p = g_array_index (candidates, VAProfile, i);
    if (!gst_va_encoder_has_profile (base->encoder, p))
      continue;

    if ((rt_format & gst_va_encoder_get_rtformat (base->encoder,
                p, GST_VA_BASE_ENC_ENTRYPOINT (base))) == 0)
      continue;

    if (p == va_profile) {
      ret_profile = p;
      goto out;
    }
  }

out:
  if (ret_profile != VAProfileNone)
    GST_INFO_OBJECT (self, "Decide the profile: %s",
        gst_va_profile_name (ret_profile));

  return ret_profile;
}

static gboolean
_vp9_generate_gop_structure (GstVaVp9Enc * self)
{
  GstVaBaseEnc *base = GST_VA_BASE_ENC (self);
  guint32 list0, list1;

  /* If not set, generate a key frame every 2 second */
  if (self->gop.keyframe_interval == 0) {
    self->gop.keyframe_interval = (2 * GST_VIDEO_INFO_FPS_N (&base->in_info)
        + GST_VIDEO_INFO_FPS_D (&base->in_info) - 1) /
        GST_VIDEO_INFO_FPS_D (&base->in_info);
  }

  if (self->gop.keyframe_interval > MAX_KEY_FRAME_INTERVAL)
    self->gop.keyframe_interval = MAX_KEY_FRAME_INTERVAL;

  if (self->gop.gf_group_size >= self->gop.keyframe_interval)
    self->gop.gf_group_size = self->gop.keyframe_interval - 1;

  /* VP9 does not define reference list1 in spec. */
  if (!gst_va_encoder_get_max_num_reference (base->encoder, base->profile,
          GST_VA_BASE_ENC_ENTRYPOINT (base), &list0, NULL)) {
    GST_INFO_OBJECT (self, "Failed to get the max num reference");
    list0 = 1;
  }

  /* At most, 3 forward refs */
  if (list0 > 3)
    list0 = 3;

  if (self->gop.num_ref_frames == 0) {
    list0 = 0;
    list1 = 0;
    self->gop.keyframe_interval = 1;
    self->gop.gf_group_size = 0;
    GST_INFO_OBJECT (self,
        "No reference for each frame, key frame only stream");
  } else if (self->gop.num_ref_frames <= 2 || list0 <= 2) {
    list0 = MIN (self->gop.num_ref_frames, list0);
    list1 = 0;
    self->gop.num_ref_frames = list0;
    GST_INFO_OBJECT (self, "Only %d reference frames, disable backward ref",
        self->gop.num_ref_frames);
  } else {
    self->gop.num_ref_frames = 3;

    /* Only I/P mode is needed */
    if (self->gop.max_level < 2 || self->gop.gf_group_size < 3) {
      list0 = 3;
      list1 = 0;
    } else {
      list0 = 2;
      list1 = 1;
    }
  }

  if (self->gop.keyframe_interval == 1 || self->gop.num_ref_frames == 0) {
    /* Disable gf group and reference for key frame only stream. */
    self->gop.num_ref_frames = 0;
    list0 = 0;
    list1 = 0;
    self->gop.keyframe_interval = 1;
    self->gop.gf_group_size = 0;
    GST_INFO_OBJECT (self,
        "No reference for each frame, key frame only stream");
  }

  self->gop.forward_ref_num = list0;
  self->gop.backward_ref_num = list1;

  if (self->gop.backward_ref_num == 0)
    self->gop.max_level = 1;

  GST_INFO_OBJECT (self, "key frame interval %d, golden frame group size %d,"
      " max hierarchical level %d, reference num %d, forward ref num %d,"
      " backward ref num %d", self->gop.keyframe_interval,
      self->gop.gf_group_size, self->gop.max_level, self->gop.num_ref_frames,
      self->gop.forward_ref_num, self->gop.backward_ref_num);

  update_property_uint (base, &self->prop.keyframe_interval,
      self->gop.keyframe_interval, PROP_KEYFRAME_INT);
  update_property_uint (base, &self->prop.gf_group_size,
      self->gop.gf_group_size, PROP_GOLDEN_GROUP_SIZE);
  update_property_uint (base, &self->prop.num_ref_frames,
      self->gop.num_ref_frames, PROP_NUM_REF_FRAMES);
  update_property_uint (base, &self->prop.max_hierarchical_level,
      self->gop.max_level, PROP_HIERARCHICAL_LEVEL);

  _vp9_init_gf_group (&self->gop.current_group, &base->reorder_list);

  return TRUE;
}

static void
_vp9_calculate_coded_size (GstVaVp9Enc * self)
{
  GstVaBaseEnc *base = GST_VA_BASE_ENC (self);
  guint codedbuf_size = 0;
  gint width = GST_ROUND_UP_16 (base->width);
  gint height = GST_ROUND_UP_16 (base->height);

  switch (self->chrome) {
    case 0:
      /* 4:0:0 */
    case 1:
      /* 4:2:0 */
      codedbuf_size = (width * height * 3 / 2);
      break;
    case 2:
      /* 4:2:2 */
      codedbuf_size = (width * height * 2);
      break;
    case 3:
      /* 4:4:4 */
      codedbuf_size = (width * height * 3);
      break;
    default:
      g_assert_not_reached ();
      break;
  }

  codedbuf_size = codedbuf_size + (codedbuf_size * (self->depth - 8) / 8);

  if (self->rc.rc_ctrl_mode == VA_RC_CQP || self->rc.rc_ctrl_mode == VA_RC_ICQ) {
    if (self->rc.base_qindex > DEFAULT_BASE_QINDEX)
      codedbuf_size = codedbuf_size / 2;
  } else if (self->rc.max_bitrate_bits > 0) {
    guint64 frame_sz = gst_util_uint64_scale (self->rc.max_bitrate_bits / 8,
        GST_VIDEO_INFO_FPS_D (&base->in_info),
        GST_VIDEO_INFO_FPS_N (&base->in_info));

    /* FIXME: If average frame size is smaller than 1/10 coded buffer size,
       we shrink the coded buffer size to 1/2 to improve performance. */
    if (frame_sz * 10 < codedbuf_size)
      codedbuf_size = codedbuf_size / 2;
  } else {
    /* FIXME: Just use a rough 1/2 min compression ratio here. */
    codedbuf_size = codedbuf_size / 2;
  }

  base->codedbuf_size = codedbuf_size;

  GST_INFO_OBJECT (self, "Calculate codedbuf size: %u", base->codedbuf_size);
}

/* Normalizes bitrate (and CPB size) for HRD conformance */
static void
_vp9_calculate_bitrate_hrd (GstVaVp9Enc * self)
{
  guint bitrate_bits, cpb_bits_size;

  bitrate_bits = self->rc.max_bitrate * 1000;
  GST_DEBUG_OBJECT (self, "Max bitrate: %u bits/sec", bitrate_bits);
  self->rc.max_bitrate_bits = bitrate_bits;

  bitrate_bits = self->rc.target_bitrate * 1000;
  GST_DEBUG_OBJECT (self, "Target bitrate: %u bits/sec", bitrate_bits);
  self->rc.target_bitrate_bits = bitrate_bits;

  if (self->rc.cpb_size > 0 && self->rc.cpb_size < (self->rc.max_bitrate / 2)) {
    GST_INFO_OBJECT (self, "Too small cpb_size: %d", self->rc.cpb_size);
    self->rc.cpb_size = 0;
  }

  if (self->rc.cpb_size == 0) {
    /* We cache 2 second coded data by default. */
    self->rc.cpb_size = self->rc.max_bitrate * 2;
    GST_INFO_OBJECT (self, "Adjust cpb_size to: %d", self->rc.cpb_size);
  }

  cpb_bits_size = self->rc.cpb_size * 1000;

  GST_DEBUG_OBJECT (self, "HRD CPB size: %u bits", cpb_bits_size);
  self->rc.cpb_length_bits = cpb_bits_size;
}

static guint
_vp9_adjust_loopfilter_level_based_on_qindex (guint qindex)
{
  if (qindex >= 40) {
    return (gint32) (-18.98682 + 0.3967082 * (gfloat) qindex +
        0.0005054 * pow ((float) qindex - 127.5, 2) -
        9.692e-6 * pow ((float) qindex - 127.5, 3));
  } else {
    return qindex / 4;
  }
}

/* Estimates a good enough bitrate if none was supplied */
static gboolean
_vp9_ensure_rate_control (GstVaVp9Enc * self)
{
  /* User can specify the properties of: "bitrate", "target-percentage",
   * "max-qp", "min-qp", "qp", "loop-filter-level", "sharpness-level",
   * "mbbrc", "cpb-size", "rate-control" and "target-usage" to control
   * the RC behavior.
   *
   * "target-usage" is different from the others, it controls the encoding
   * speed and quality, while the others control encoding bit rate and
   * quality. The lower value has better quality(maybe bigger MV search
   * range) but slower speed, the higher value has faster speed but lower
   * quality. It is valid for all modes.
   *
   * The possible composition to control the bit rate and quality:
   *
   * 1. CQP mode: "rate-control=cqp", then "qp"(the qindex in VP9) specify
   *    the QP of frames(within the "max-qp" and "min-qp" range). The QP
   *    will not change during the whole stream. "loop-filter-level" and
   *    "sharpness-level" together determine how much the filtering can
   *    change the sample values. Other properties related to rate control
   *    are ignored.
   *
   * 2. CBR mode: "rate-control=CBR", then the "bitrate" specify the
   *    target bit rate and the "cpb-size" specifies the max coded
   *    picture buffer size to avoid overflow. If the "bitrate" is not
   *    set, it is calculated by the picture resolution and frame
   *    rate. If "cpb-size" is not set, it is set to the size of
   *    caching 2 second coded data. Encoder will try its best to make
   *    the QP with in the ["max-qp", "min-qp"] range. "mbbrc" can
   *    enable bit rate control in macro block level. Other paramters
   *    are ignored.
   *
   * 3. VBR mode: "rate-control=VBR", then the "bitrate" specify the
   *    target bit rate, "target-percentage" is used to calculate the
   *    max bit rate of VBR mode by ("bitrate" * 100) /
   *    "target-percentage". It is also used by driver to calculate
   *    the min bit rate. The "cpb-size" specifies the max coded
   *    picture buffer size to avoid overflow. If the "bitrate" is not
   *    set, the target bit rate will be calculated by the picture
   *    resolution and frame rate. Encoder will try its best to make
   *    the QP with in the ["max-qp", "min-qp"] range. "mbbrc" can
   *    enable bit rate control in macro block level. Other paramters
   *    are ignored.
   *
   * 4. VCM mode: "rate-control=VCM", then the "bitrate" specify the
   *    target bit rate, and encoder will try its best to make the QP
   *    with in the ["max-qp", "min-qp"] range. Other paramters are
   *    ignored.
   *
   * 5. ICQ mode: "rate-control=ICQ", which is similar to CQP mode
   *    except that its QP(qindex in VP9) may be increased or decreaed
   *    to avoid huge bit rate fluctuation. The "qp" specifies a quality
   *    factor as the base quality value. Other properties are ignored.
   *
   * 6. QVBR mode: "rate-control=QVBR", which is similar to VBR mode
   *    with the same usage of "bitrate", "target-percentage" and
   *    "cpb-size" properties. Besides that, the "qp"(the qindex in VP9)
   *    specifies a quality factor as the base quality value which the
   *    driver should try its best to meet. Other properties are ignored.
   *
   */

  GstVaBaseEnc *base = GST_VA_BASE_ENC (self);
  guint bitrate;
  guint32 rc_ctrl, rc_mode, quality_level;

  quality_level = gst_va_encoder_get_quality_level (base->encoder,
      base->profile, GST_VA_BASE_ENC_ENTRYPOINT (base));
  if (self->rc.target_usage > quality_level) {
    GST_INFO_OBJECT (self, "User setting target-usage: %d is not supported, "
        "fallback to %d", self->rc.target_usage, quality_level);
    self->rc.target_usage = quality_level;

    update_property_uint (base, &self->prop.target_usage, self->rc.target_usage,
        PROP_TARGET_USAGE);
  }

  GST_OBJECT_LOCK (self);
  rc_ctrl = self->prop.rc_ctrl;
  GST_OBJECT_UNLOCK (self);

  if (rc_ctrl != VA_RC_NONE) {
    rc_mode = gst_va_encoder_get_rate_control_mode (base->encoder,
        base->profile, GST_VA_BASE_ENC_ENTRYPOINT (base));
    if (!(rc_mode & rc_ctrl)) {
      guint32 defval =
          G_PARAM_SPEC_ENUM (properties[PROP_RATE_CONTROL])->default_value;
      GST_INFO_OBJECT (self, "The rate control mode %s is not supported, "
          "fallback to %s mode", _rate_control_get_name (rc_ctrl),
          _rate_control_get_name (defval));
      self->rc.rc_ctrl_mode = defval;

      update_property_uint (base, &self->prop.rc_ctrl, self->rc.rc_ctrl_mode,
          PROP_RATE_CONTROL);
    }
  } else {
    self->rc.rc_ctrl_mode = VA_RC_NONE;
  }

  if (self->rc.min_qindex > self->rc.max_qindex) {
    GST_INFO_OBJECT (self, "The min_qindex %d is bigger than the max_qindex"
        " %d, set it to the max_qindex", self->rc.min_qindex,
        self->rc.max_qindex);
    self->rc.min_qindex = self->rc.max_qindex;

    update_property_uint (base, &self->prop.min_qp, self->rc.min_qindex,
        PROP_MIN_QP);
  }

  /* Make the qp in the valid range */
  if (self->rc.base_qindex < self->rc.min_qindex) {
    if (self->rc.base_qindex != DEFAULT_BASE_QINDEX)
      GST_INFO_OBJECT (self, "The base_qindex %d is smaller than the"
          " min_qindex %d, set it to the min_qindex", self->rc.base_qindex,
          self->rc.min_qindex);
    self->rc.base_qindex = self->rc.min_qindex;
  }
  if (self->rc.base_qindex > self->rc.max_qindex) {
    if (self->rc.base_qindex != DEFAULT_BASE_QINDEX)
      GST_INFO_OBJECT (self, "The base_qindex %d is bigger than the"
          " max_qindex %d, set it to the max_qindex", self->rc.base_qindex,
          self->rc.max_qindex);
    self->rc.base_qindex = self->rc.max_qindex;
  }

  /* Calculate the loop filter level. */
  if (self->rc.rc_ctrl_mode == VA_RC_CQP) {
    if (self->rc.filter_level == -1)
      self->rc.filter_level =
          _vp9_adjust_loopfilter_level_based_on_qindex (self->rc.base_qindex);
  }

  GST_OBJECT_LOCK (self);
  bitrate = self->prop.bitrate;
  GST_OBJECT_UNLOCK (self);

  /* Calculate a bitrate if it is not set. */
  if ((self->rc.rc_ctrl_mode == VA_RC_CBR || self->rc.rc_ctrl_mode == VA_RC_VBR
          || self->rc.rc_ctrl_mode == VA_RC_VCM
          || self->rc.rc_ctrl_mode == VA_RC_QVBR) && bitrate == 0) {
    /* FIXME: Provide better estimation. */
    /* Choose the max value of all levels' MainCR which is 8, and x2 for
       conservative calculation. So just using a 1/16 compression ratio,
       12 bits per pixel for 4:2:0, 16 bits per pixel for 4:2:2 and 24 bits
       per pixel for 4:4:4. Also the depth should be considered. */
    guint64 factor;
    guint depth = 8, chrome = 1;
    guint bits_per_pix;

    if (!_vp9_get_rtformat (self,
            GST_VIDEO_INFO_FORMAT (&base->in_info), &depth, &chrome))
      g_assert_not_reached ();

    if (chrome == 3) {
      bits_per_pix = 24;
    } else if (chrome == 2) {
      bits_per_pix = 16;
    } else {
      bits_per_pix = 12;
    }
    bits_per_pix = bits_per_pix + bits_per_pix * (depth - 8) / 8;

    factor = (guint64) base->width * base->height * bits_per_pix / 16;
    bitrate = gst_util_uint64_scale (factor,
        GST_VIDEO_INFO_FPS_N (&base->in_info),
        GST_VIDEO_INFO_FPS_D (&base->in_info)) / 1000;

    GST_INFO_OBJECT (self, "target bitrate computed to %u kbps", bitrate);
  }

  /* Adjust the setting based on RC mode. */
  switch (self->rc.rc_ctrl_mode) {
    case VA_RC_NONE:
    case VA_RC_ICQ:
    case VA_RC_CQP:
      bitrate = 0;
      self->rc.max_bitrate = 0;
      self->rc.target_bitrate = 0;
      self->rc.target_percentage = 0;
      self->rc.cpb_size = 0;
      self->rc.mbbrc = 0;
      break;
    case VA_RC_CBR:
      self->rc.max_bitrate = bitrate;
      self->rc.target_bitrate = bitrate;
      self->rc.target_percentage = 100;
      self->rc.base_qindex = DEFAULT_BASE_QINDEX;
      self->rc.filter_level = DEFAULT_LOOP_FILTER_LEVEL;
      self->rc.sharpness_level = 0;
      break;
    case VA_RC_VBR:
      self->rc.base_qindex = DEFAULT_BASE_QINDEX;
      /* Fall through. */
    case VA_RC_QVBR:
      self->rc.target_percentage = MAX (10, self->rc.target_percentage);
      self->rc.max_bitrate = (guint) gst_util_uint64_scale_int (bitrate,
          100, self->rc.target_percentage);
      self->rc.target_bitrate = bitrate;
      self->rc.filter_level = DEFAULT_LOOP_FILTER_LEVEL;
      self->rc.sharpness_level = 0;
      break;
    case VA_RC_VCM:
      self->rc.max_bitrate = bitrate;
      self->rc.target_bitrate = bitrate;
      self->rc.target_percentage = 0;
      self->rc.base_qindex = DEFAULT_BASE_QINDEX;
      self->rc.filter_level = DEFAULT_LOOP_FILTER_LEVEL;
      self->rc.sharpness_level = 0;
      self->rc.cpb_size = 0;

      if (self->gop.max_level > 1) {
        GST_INFO_OBJECT (self, "VCM mode does not reorder frames");
        self->gop.max_level = 1;
      }
      break;
    default:
      GST_WARNING_OBJECT (self, "Unsupported rate control");
      return FALSE;
      break;
  }

  GST_DEBUG_OBJECT (self, "Max bitrate: %u kbps, target bitrate: %u kbps",
      self->rc.max_bitrate, self->rc.target_bitrate);

  if (self->rc.rc_ctrl_mode == VA_RC_CBR || self->rc.rc_ctrl_mode == VA_RC_VBR
      || self->rc.rc_ctrl_mode == VA_RC_VCM
      || self->rc.rc_ctrl_mode == VA_RC_QVBR)
    _vp9_calculate_bitrate_hrd (self);

  /* update & notifications */
  update_property_uint (base, &self->prop.bitrate, bitrate, PROP_BITRATE);
  update_property_uint (base, &self->prop.cpb_size, self->rc.cpb_size,
      PROP_CPB_SIZE);
  update_property_uint (base, &self->prop.target_percentage,
      self->rc.target_percentage, PROP_TARGET_PERCENTAGE);
  update_property_uint (base, &self->prop.qp, self->rc.base_qindex, PROP_QP);
  update_property_uint (base, ((guint *) (&self->prop.filter_level)),
      self->rc.filter_level, PROP_LOOP_FILTER_LEVEL);
  update_property_uint (base, &self->prop.sharpness_level,
      self->rc.sharpness_level, PROP_SHARPNESS_LEVEL);
  update_property_uint (base, &self->prop.mbbrc, self->rc.mbbrc, PROP_MBBRC);

  return TRUE;
}

static gboolean
_vp9_init_packed_headers (GstVaVp9Enc * self)
{
  GstVaBaseEnc *base = GST_VA_BASE_ENC (self);
  guint32 packed_headers;

  if (!gst_va_encoder_get_packed_headers (base->encoder, base->profile,
          GST_VA_BASE_ENC_ENTRYPOINT (base), &packed_headers))
    return FALSE;

  /* TODO: Need to implement bitwriter for VP9. */
  if (packed_headers & (VA_ENC_PACKED_HEADER_SEQUENCE |
          VA_ENC_PACKED_HEADER_PICTURE | VA_ENC_PACKED_HEADER_SLICE)) {
    GST_ERROR_OBJECT (self,
        "The packed header of VP9 is not supported now. "
        "The driver need to generate VP9 frame headers by itself.");
    return FALSE;
  }

  /* VP9 does not support meta data, either. */
  self->packed_headers = 0;

  return TRUE;
}

static gboolean
gst_va_vp9_enc_reconfig (GstVaBaseEnc * base)
{
  GstVaBaseEncClass *klass = GST_VA_BASE_ENC_GET_CLASS (base);
  GstVideoEncoder *venc = GST_VIDEO_ENCODER (base);
  GstVaVp9Enc *self = GST_VA_VP9_ENC (base);
  GstCaps *out_caps, *reconf_caps = NULL;
  GstVideoCodecState *output_state;
  GstVideoFormat format, reconf_format = GST_VIDEO_FORMAT_UNKNOWN;
  VAProfile profile;
  gboolean do_renegotiation = TRUE, do_reopen, need_negotiation;
  guint max_ref_frames, max_surfaces = 0,
      rt_format, depth = 0, chrome = 0, codedbuf_size, latency_num;
  gint width, height;
  GstClockTime latency;

  width = GST_VIDEO_INFO_WIDTH (&base->in_info);
  height = GST_VIDEO_INFO_HEIGHT (&base->in_info);
  format = GST_VIDEO_INFO_FORMAT (&base->in_info);
  codedbuf_size = base->codedbuf_size;
  latency_num = base->preferred_output_delay + self->gop.gf_group_size - 1;

  need_negotiation =
      !gst_va_encoder_get_reconstruct_pool_config (base->encoder, &reconf_caps,
      &max_surfaces);
  if (!need_negotiation && reconf_caps) {
    GstVideoInfo vi;
    if (!gst_video_info_from_caps (&vi, reconf_caps))
      return FALSE;
    reconf_format = GST_VIDEO_INFO_FORMAT (&vi);
  }

  rt_format = _vp9_get_rtformat (self, format, &depth, &chrome);
  if (!rt_format) {
    GST_ERROR_OBJECT (self, "unrecognized input format.");
    return FALSE;
  }

  profile = _vp9_decide_profile (self, rt_format, depth, chrome);
  if (profile == VAProfileNone)
    return FALSE;

  /* first check */
  do_reopen = !(base->profile == profile && base->rt_format == rt_format
      && format == reconf_format && width == base->width
      && height == base->height && self->prop.rc_ctrl == self->rc.rc_ctrl_mode
      && depth == self->depth && chrome == self->chrome);

  if (do_reopen && gst_va_encoder_is_open (base->encoder))
    gst_va_encoder_close (base->encoder);

  gst_va_base_enc_reset_state (base);

  if (base->is_live) {
    base->preferred_output_delay = 0;
  } else {
    /* FIXME: An experience value for most of the platforms. */
    base->preferred_output_delay = 4;
  }

  base->profile = profile;
  base->rt_format = rt_format;
  self->depth = depth;
  self->chrome = chrome;
  base->width = width;
  base->height = height;

  /* Frame rate is needed for rate control and PTS setting. */
  if (GST_VIDEO_INFO_FPS_N (&base->in_info) == 0
      || GST_VIDEO_INFO_FPS_D (&base->in_info) == 0) {
    GST_INFO_OBJECT (self, "Unknown framerate, just set to 30 fps");
    GST_VIDEO_INFO_FPS_N (&base->in_info) = 30;
    GST_VIDEO_INFO_FPS_D (&base->in_info) = 1;
  }
  base->frame_duration = gst_util_uint64_scale (GST_SECOND,
      GST_VIDEO_INFO_FPS_D (&base->in_info),
      GST_VIDEO_INFO_FPS_N (&base->in_info));

  GST_DEBUG_OBJECT (self, "resolution:%dx%d, frame duration is %"
      GST_TIME_FORMAT, base->width, base->height,
      GST_TIME_ARGS (base->frame_duration));

  if (!_vp9_ensure_rate_control (self))
    return FALSE;

  if (!_vp9_generate_gop_structure (self))
    return FALSE;

  _vp9_calculate_coded_size (self);

  if (!_vp9_init_packed_headers (self))
    return FALSE;

  /* Let the downstream know the new latency. */
  if (latency_num != base->preferred_output_delay + self->gop.gf_group_size - 1) {
    need_negotiation = TRUE;
    latency_num = base->preferred_output_delay + self->gop.gf_group_size - 1;
  }

  /* Set the latency */
  latency = gst_util_uint64_scale (latency_num,
      GST_VIDEO_INFO_FPS_D (&base->input_state->info) * GST_SECOND,
      GST_VIDEO_INFO_FPS_N (&base->input_state->info));
  gst_video_encoder_set_latency (venc, latency, latency);

  max_ref_frames = GST_VP9_REF_FRAMES;
  max_ref_frames += base->preferred_output_delay;
  base->min_buffers = max_ref_frames;
  max_ref_frames += 3 /* scratch frames */ ;

  /* second check after calculations */
  do_reopen |=
      !(max_ref_frames == max_surfaces && codedbuf_size == base->codedbuf_size);
  if (do_reopen && gst_va_encoder_is_open (base->encoder))
    gst_va_encoder_close (base->encoder);

  if (!gst_va_encoder_is_open (base->encoder)
      && !gst_va_encoder_open (base->encoder, base->profile,
          GST_VIDEO_INFO_FORMAT (&base->in_info), base->rt_format,
          base->width, base->height, base->codedbuf_size, max_ref_frames,
          self->rc.rc_ctrl_mode, self->packed_headers)) {
    GST_ERROR_OBJECT (self, "Failed to open the VA encoder.");
    return FALSE;
  }

  /* Add some tags */
  gst_va_base_enc_add_codec_tag (base, "VP9");

  out_caps = gst_va_profile_caps (base->profile, klass->entrypoint);
  g_assert (out_caps);
  out_caps = gst_caps_fixate (out_caps);

  gst_caps_set_simple (out_caps, "width", G_TYPE_INT, base->width,
      "height", G_TYPE_INT, base->height, "alignment",
      G_TYPE_STRING, "super-frame", NULL);

  if (!need_negotiation) {
    output_state = gst_video_encoder_get_output_state (venc);
    do_renegotiation = TRUE;
    if (output_state) {
      do_renegotiation = !gst_caps_is_subset (output_state->caps, out_caps);
      gst_video_codec_state_unref (output_state);
    }
    if (!do_renegotiation) {
      gst_caps_unref (out_caps);
      return TRUE;
    }
  }

  GST_DEBUG_OBJECT (self, "output caps is %" GST_PTR_FORMAT, out_caps);

  output_state =
      gst_video_encoder_set_output_state (venc, out_caps, base->input_state);
  gst_video_codec_state_unref (output_state);

  if (!gst_video_encoder_negotiate (venc)) {
    GST_ERROR_OBJECT (self, "Failed to negotiate with the downstream");
    return FALSE;
  }

  return TRUE;
}

static void
_vp9_clear_super_frames (GstVaVp9Enc * self)
{
  guint i;
  GstVaVp9EncFrame *frame_enc;

  for (i = 0; i < self->frames_in_super_num; i++) {
    frame_enc = _enc_frame (self->frames_in_super[i]);
    frame_enc->flags &= (~FRAME_FLAG_IN_SUPER_FRAME);
  }

  memset (self->frames_in_super, 0, sizeof (self->frames_in_super));
  self->frames_in_super_num = 0;
}

static gboolean
gst_va_vp9_enc_flush (GstVideoEncoder * venc)
{
  GstVaVp9Enc *self = GST_VA_VP9_ENC (venc);
  GstVaBaseEnc *base = GST_VA_BASE_ENC (self);

  _vp9_clear_super_frames (self);

  /* begin from an key frame after flush. */
  self->gop.frame_num_since_kf = 0;

  /* Parent's flush will release all frames for us. */
  _vp9_init_gf_group (&self->gop.current_group, &base->reorder_list);
  self->gop.last_keyframe = NULL;
  memset (self->gop.ref_list, 0, sizeof (self->gop.ref_list));

  return GST_VIDEO_ENCODER_CLASS (parent_class)->flush (venc);
}

static void
_vp9_fill_sequence_param (GstVaVp9Enc * self,
    VAEncSequenceParameterBufferVP9 * sequence)
{
  /* *INDENT-OFF* */
  *sequence = (VAEncSequenceParameterBufferVP9) {
    .max_frame_width = MAX_FRAME_WIDTH,
    .max_frame_height = MAX_FRAME_HEIGHT,
    .kf_auto = 0,
    .kf_min_dist = 1,
    .kf_max_dist = self->gop.keyframe_interval,
    .intra_period = self->gop.keyframe_interval,
    .bits_per_second = self->rc.target_bitrate_bits,
  };
  /* *INDENT-ON* */
}

static gboolean
_vp9_add_sequence_param (GstVaVp9Enc * self, GstVaEncodePicture * picture,
    VAEncSequenceParameterBufferVP9 * sequence)
{
  GstVaBaseEnc *base = GST_VA_BASE_ENC (self);

  if (!gst_va_encoder_add_param (base->encoder, picture,
          VAEncSequenceParameterBufferType, sequence, sizeof (*sequence))) {
    GST_ERROR_OBJECT (self, "Failed to create the sequence parameter");
    return FALSE;
  }

  return TRUE;
}

static gboolean
_vp9_fill_frame_param (GstVaVp9Enc * self, GstVaVp9EncFrame * va_frame,
    VAEncPictureParameterBufferVP9 * pic_param)
{
  GstVaBaseEnc *base = GST_VA_BASE_ENC (self);
  uint8_t refresh_frame_flags = 0xff;
  gint sb_cols = 0, min_log2_tile_columns = 0;
  guint i;

  g_assert (!(va_frame->type & FRAME_TYPE_REPEAT));

  /* Maximum width of a tile in units of superblocks is MAX_TILE_WIDTH_B64(64).
     When the width is big enough to partition more than MAX_TILE_WIDTH_B64(64)
     superblocks, we need multi tiles to handle it. */
  sb_cols = (base->width + 63) / 64;
  while ((MAX_TILE_WIDTH_B64 << min_log2_tile_columns) < sb_cols)
    ++min_log2_tile_columns;

  /* *INDENT-OFF* */
  if (va_frame->type != GST_VP9_KEY_FRAME) {
    if (va_frame->update_index >= 0) {
      refresh_frame_flags = (1 << va_frame->update_index);
    } else {
      refresh_frame_flags = 0;
    }
  }

  *pic_param = (VAEncPictureParameterBufferVP9) {
    .frame_width_src = base->width,
    .frame_height_src = base->height,
    .frame_width_dst = base->width,
    .frame_height_dst = base->height,
    .reconstructed_frame =
        gst_va_encode_picture_get_reconstruct_surface (va_frame->base.picture),
    /* Set it later. */
    .reference_frames = { 0, },
    .coded_buf = va_frame->base.picture->coded_buffer,
    .ref_flags.bits = {
      .force_kf = 0,
      /* Set all the refs later if inter frame. */
      .ref_frame_ctrl_l0 = 0,
      .ref_frame_ctrl_l1 = 0,
      .ref_last_idx = 0,
      .ref_last_sign_bias = 0,
      .ref_gf_idx = 0,
      .ref_gf_sign_bias = 0,
      .ref_arf_idx = 0,
      .ref_arf_sign_bias = 0,
      /* Do not support multi temporal now. */
      .temporal_id = 0,
    },
    .pic_flags.bits = {
      .frame_type = va_frame->type,
      .show_frame = !(va_frame->flags & FRAME_FLAG_NOT_SHOW),
      /* We do not support error resilient mode now. */
      .error_resilient_mode = 0,
      .intra_only = 0,
      .allow_high_precision_mv = 1,
      .mcomp_filter_type = 0,
      .frame_parallel_decoding_mode = 0,
      .reset_frame_context = 0,
      .refresh_frame_context = 0,
      .frame_context_idx = 0,
      .segmentation_enabled = 0,
      .segmentation_temporal_update = 0,
      .segmentation_update_map = 0,
      /* Do not use lossless mode now. */
      .lossless_mode = 0,
      .comp_prediction_mode = 0,
      .auto_segmentation = 0,
      .super_frame_flag = 0,
    },
    .refresh_frame_flags = refresh_frame_flags,
    .luma_ac_qindex = self->rc.base_qindex,
    .luma_dc_qindex_delta = 0,
    .chroma_ac_qindex_delta = 0,
    .chroma_dc_qindex_delta = 0,
    .filter_level = self->rc.filter_level,
    .sharpness_level = self->rc.sharpness_level,
    .ref_lf_delta = { 0, },
    .mode_lf_delta = { 0, },
    .bit_offset_ref_lf_delta = 0,
    .bit_offset_mode_lf_delta = 0,
    .bit_offset_lf_level = 0,
    .bit_offset_qindex = 0,
    .bit_offset_first_partition_size = 0,
    .bit_offset_segmentation = 0,
    .bit_size_segmentation = 0,
    .log2_tile_rows = 0,
    .log2_tile_columns = min_log2_tile_columns,
    .skip_frame_flag = 0,
  };
  /* *INDENT-ON* */

  if (va_frame->type == GST_VP9_INTER_FRAME) {
    for (i = 0; i < 8; i++) {
      if (self->gop.ref_list[i] == NULL) {
        pic_param->reference_frames[i] = VA_INVALID_SURFACE;
        continue;
      }

      pic_param->reference_frames[i] =
          gst_va_encode_picture_get_reconstruct_surface
          (_enc_frame (self->gop.ref_list[i])->base.picture);

    }

    pic_param->ref_flags.bits.ref_last_idx =
        va_frame->ref_frame_idx[GST_VP9_REF_FRAME_LAST];
    pic_param->ref_flags.bits.ref_gf_idx =
        va_frame->ref_frame_idx[GST_VP9_REF_FRAME_GOLDEN];
    pic_param->ref_flags.bits.ref_arf_idx =
        va_frame->ref_frame_idx[GST_VP9_REF_FRAME_ALTREF];

    pic_param->ref_flags.bits.ref_frame_ctrl_l0 = 0x7;
    pic_param->ref_flags.bits.ref_frame_ctrl_l0 = 0x7;
  } else {
    for (i = 0; i < 8; i++)
      pic_param->reference_frames[i] = VA_INVALID_SURFACE;

    pic_param->ref_flags.bits.ref_last_idx = 0;
    pic_param->ref_flags.bits.ref_gf_idx = 0;
    pic_param->ref_flags.bits.ref_arf_idx = 0;
  }

  return TRUE;
}

static gboolean
_vp9_encode_one_frame (GstVaVp9Enc * self, GstVaVp9EncFrame * va_frame)
{
  GstVaBaseEnc *base = GST_VA_BASE_ENC (self);
  VAEncPictureParameterBufferVP9 pic_param;

  if (!_vp9_fill_frame_param (self, va_frame, &pic_param)) {
    GST_ERROR_OBJECT (self, "Fails to fill the frame parameter.");
    return FALSE;
  }

  if (!gst_va_encoder_add_param (base->encoder, va_frame->base.picture,
          VAEncPictureParameterBufferType, &pic_param, sizeof (pic_param))) {
    GST_ERROR_OBJECT (self, "Failed to create the frame parameter");
    return FALSE;
  }

  if (!gst_va_encoder_encode (base->encoder, va_frame->base.picture)) {
    GST_ERROR_OBJECT (self, "Encode frame error");
    return FALSE;
  }

  return TRUE;
}

static void
_vp9_add_repeat_frame_header (GstVaVp9Enc * self, GstVaVp9EncFrame * va_frame)
{
  GstVaBaseEnc *base = GST_VA_BASE_ENC (self);
  guint profile = 0;
  GstVp9FrameHdr frame_hdr;

  switch (base->profile) {
    case VAProfileVP9Profile0:
      profile = 0;
      break;
    case VAProfileVP9Profile1:
      profile = 1;
      break;
    case VAProfileVP9Profile2:
      profile = 2;
      break;
    case VAProfileVP9Profile3:
      profile = 3;
      break;
    default:
      g_assert_not_reached ();
      break;
  }

  g_assert (va_frame->repeat_index >= 0 && va_frame->repeat_index <= 7);

  /* *INDENT-OFF* */
  frame_hdr = (GstVp9FrameHdr) {
    .profile = profile,
    .show_existing_frame = 1,
    .frame_to_show = va_frame->repeat_index,
  };
  /* *INDENT-ON* */

  memset (va_frame->repeat_frame_header, 0,
      sizeof (va_frame->repeat_frame_header));
  va_frame->repeat_frame_header_size = sizeof (va_frame->repeat_frame_header);
  gst_vp9_bit_writer_frame_header (&frame_hdr, va_frame->repeat_frame_header,
      &va_frame->repeat_frame_header_size);
}

static GstFlowReturn
gst_va_vp9_enc_encode_frame (GstVaBaseEnc * base,
    GstVideoCodecFrame * gst_frame, gboolean is_last)
{
  GstVaVp9Enc *self = GST_VA_VP9_ENC (base);
  GstVaVp9EncFrame *va_frame = _enc_frame (gst_frame);
  VAEncSequenceParameterBufferVP9 seq_param;

  if (!_vp9_assign_ref_index (self, gst_frame)) {
    GST_ERROR_OBJECT (self, "Failed to assign reference for frame:"
        "system_frame_number %d, frame_num: %d, frame_type %s",
        gst_frame->system_frame_number, va_frame->frame_num,
        _vp9_get_frame_type_name (va_frame->type));
    return GST_FLOW_ERROR;
  }

  if (va_frame->type & FRAME_TYPE_REPEAT) {
    g_assert (va_frame->flags & FRAME_FLAG_ALREADY_ENCODED);
    _vp9_add_repeat_frame_header (self, va_frame);
  } else {
    g_assert (va_frame->base.picture == NULL);
    va_frame->base.picture = gst_va_encode_picture_new (base->encoder,
        gst_frame->input_buffer);

    _vp9_find_ref_to_update (base, gst_frame);

    /* Repeat the sequence for each key. */
    if (va_frame->frame_num == 0) {
      if (!gst_va_base_enc_add_rate_control_parameter (base,
              va_frame->base.picture,
              self->rc.rc_ctrl_mode, self->rc.max_bitrate_bits,
              self->rc.target_percentage, self->rc.base_qindex,
              self->rc.min_qindex, self->rc.max_qindex, self->rc.mbbrc))
        return FALSE;

      if (!gst_va_base_enc_add_quality_level_parameter (base,
              va_frame->base.picture, self->rc.target_usage))
        return FALSE;

      if (!gst_va_base_enc_add_frame_rate_parameter (base,
              va_frame->base.picture))
        return FALSE;

      if (!gst_va_base_enc_add_hrd_parameter (base, va_frame->base.picture,
              self->rc.rc_ctrl_mode, self->rc.cpb_length_bits))
        return FALSE;

      _vp9_fill_sequence_param (self, &seq_param);
      if (!_vp9_add_sequence_param (self, va_frame->base.picture, &seq_param))
        return FALSE;
    }

    if (!_vp9_encode_one_frame (self, va_frame)) {
      GST_ERROR_OBJECT (self, "Fails to encode one frame.");
      return GST_FLOW_ERROR;
    }

    va_frame->flags |= FRAME_FLAG_ALREADY_ENCODED;
  }

  _vp9_update_ref_list (base, gst_frame);

  g_queue_push_tail (&base->output_list, gst_video_codec_frame_ref (gst_frame));

  return GST_FLOW_OK;
}

static GstBuffer *
_vp9_create_super_frame_output_buffer (GstVaVp9Enc * self,
    GstVideoCodecFrame * last_frame)
{
  GstVaBaseEnc *base = GST_VA_BASE_ENC (self);
  guint8 *data;
  guint total_sz, offset;
  GstVaVp9EncFrame *frame_enc;
  GstBuffer *buf = NULL;
  gint frame_size[GST_VP9_MAX_FRAMES_IN_SUPERFRAME] = { 0, };
  guint num;

  g_assert ((_enc_frame (last_frame)->flags & FRAME_TYPE_REPEAT) == 0);
  g_assert ((_enc_frame (last_frame)->flags & FRAME_FLAG_NOT_SHOW) == 0);
  g_assert (self->frames_in_super_num <= GST_VP9_MAX_FRAMES_IN_SUPERFRAME - 1);

  total_sz = (self->frames_in_super_num + 1) * base->codedbuf_size;

  data = g_malloc (total_sz);
  if (!data)
    goto error;

  offset = 0;
  for (num = 0; num < self->frames_in_super_num; num++) {
    frame_enc = _enc_frame (self->frames_in_super[num]);

    frame_size[num] = gst_va_base_enc_copy_output_data (base,
        frame_enc->base.picture, data + offset, total_sz - offset);
    if (frame_size[num] <= 0) {
      GST_ERROR_OBJECT (self, "Fails to copy the output data of "
          "system_frame_number %d, frame_num: %d",
          self->frames_in_super[num]->system_frame_number,
          frame_enc->frame_num);
      goto error;
    }

    offset += frame_size[num];
  }

  frame_enc = _enc_frame (last_frame);
  frame_size[num] = gst_va_base_enc_copy_output_data (base,
      frame_enc->base.picture, data + offset, total_sz - offset);
  if (frame_size[num] <= 0) {
    GST_ERROR_OBJECT (self, "Fails to copy the output data of "
        "system_frame_number %d, frame_num: %d",
        last_frame->system_frame_number, frame_enc->frame_num);
    goto error;
  }
  offset += frame_size[num];
  num++;

  if (gst_vp9_bit_writer_superframe_info (num, frame_size, data,
          &total_sz) != GST_VP9_BIT_WRITER_OK)
    goto error;

  buf = gst_video_encoder_allocate_output_buffer
      (GST_VIDEO_ENCODER_CAST (base), total_sz);
  if (!buf) {
    GST_ERROR_OBJECT (base, "Failed to create output buffer");
    goto error;
  }

  if (gst_buffer_fill (buf, 0, data, total_sz) != total_sz) {
    GST_ERROR_OBJECT (base, "Failed to write output buffer for super frame");
    goto error;
  }

  g_free (data);

  _vp9_clear_super_frames (self);

  return buf;

error:
  {
    if (data)
      g_free (data);

    _vp9_clear_super_frames (self);

    gst_clear_buffer (&buf);

    return NULL;
  }
}

static gboolean
gst_va_vp9_enc_prepare_output (GstVaBaseEnc * base,
    GstVideoCodecFrame * frame, gboolean * complete)
{
  GstVaVp9Enc *self = GST_VA_VP9_ENC (base);
  GstVaVp9EncFrame *frame_enc;
  GstBuffer *buf = NULL;

  frame_enc = _enc_frame (frame);

  GST_LOG_OBJECT (base, "Prepare to output: frame system_frame_number: %d,"
      "frame_num: %d, frame type: %s, flags: 0x%x, super_num is %d",
      frame->system_frame_number, frame_enc->frame_num,
      _vp9_get_frame_type_name (frame_enc->type), frame_enc->flags,
      self->frames_in_super_num);

  if (frame_enc->flags & FRAME_FLAG_NOT_SHOW &&
      (frame_enc->flags & FRAME_FLAG_ALREADY_OUTPUTTED) == 0) {
    self->frames_in_super[self->frames_in_super_num] = frame;
    self->frames_in_super_num++;
    g_assert (self->frames_in_super_num <=
        GST_VP9_MAX_FRAMES_IN_SUPERFRAME - 1);
    g_assert ((frame_enc->flags & FRAME_FLAG_IN_SUPER_FRAME) == 0);

    frame_enc->flags |= FRAME_FLAG_IN_SUPER_FRAME;
    frame_enc->flags |= FRAME_FLAG_ALREADY_OUTPUTTED;

    *complete = FALSE;

    gst_buffer_replace (&frame->output_buffer, NULL);

    return TRUE;
  }

  if (frame_enc->flags & FRAME_FLAG_ALREADY_OUTPUTTED) {
    gsize sz;

    /* Already outputted, must be a repeat this time. */
    g_assert (frame_enc->type & FRAME_TYPE_REPEAT);
    /* Should already sync and complete in the super frame. */
    g_assert ((frame_enc->flags & FRAME_FLAG_IN_SUPER_FRAME) == 0);

    buf = gst_video_encoder_allocate_output_buffer
        (GST_VIDEO_ENCODER_CAST (base), frame_enc->repeat_frame_header_size);
    if (!buf) {
      GST_ERROR_OBJECT (base, "Failed to create output buffer");
      return FALSE;
    }

    sz = gst_buffer_fill (buf, 0, frame_enc->repeat_frame_header,
        frame_enc->repeat_frame_header_size);

    if (sz != frame_enc->repeat_frame_header_size) {
      GST_ERROR_OBJECT (base, "Failed to write output buffer for repeat frame");
      gst_clear_buffer (&buf);
      return FALSE;
    }

    *complete = TRUE;
  } else {
    if (self->frames_in_super_num > 0) {
      buf = _vp9_create_super_frame_output_buffer (self, frame);
    } else {
      buf = gst_va_base_enc_create_output_buffer (base,
          frame_enc->base.picture, NULL, 0);
    }
    if (!buf) {
      GST_ERROR_OBJECT (base, "Failed to create output buffer%s",
          self->frames_in_super_num > 0 ? " for super frame" : "");
      return FALSE;
    }

    *complete = TRUE;

    frame_enc->flags |= FRAME_FLAG_ALREADY_OUTPUTTED;
  }

  GST_BUFFER_FLAG_SET (buf, GST_BUFFER_FLAG_MARKER);
  if (frame_enc->frame_num == 0) {
    GST_VIDEO_CODEC_FRAME_SET_SYNC_POINT (frame);
    GST_BUFFER_FLAG_UNSET (buf, GST_BUFFER_FLAG_DELTA_UNIT);
  } else {
    GST_VIDEO_CODEC_FRAME_UNSET_SYNC_POINT (frame);
    GST_BUFFER_FLAG_SET (buf, GST_BUFFER_FLAG_DELTA_UNIT);
  }

  gst_buffer_replace (&frame->output_buffer, buf);
  gst_clear_buffer (&buf);

  return TRUE;
}

/* *INDENT-OFF* */
static const gchar *sink_caps_str =
    GST_VIDEO_CAPS_MAKE_WITH_FEATURES (GST_CAPS_FEATURE_MEMORY_VA,
        "{ NV12 }") " ;"
    GST_VIDEO_CAPS_MAKE ("{ NV12 }");
/* *INDENT-ON* */

static const gchar *src_caps_str = "video/x-vp9,alignment=(string)super-frame";

static gpointer
_register_debug_category (gpointer data)
{
  GST_DEBUG_CATEGORY_INIT (gst_va_vp9enc_debug, "vavp9enc", 0,
      "VA vp9 encoder");

  return NULL;
}

static void
gst_va_vp9_enc_init (GTypeInstance * instance, gpointer g_class)
{
  GstVaVp9Enc *self = GST_VA_VP9_ENC (instance);

  /* default values */
  self->prop.bitrate = 0;
  self->prop.target_usage = 4;
  self->prop.cpb_size = 0;
  self->prop.target_percentage = 66;
  self->prop.gf_group_size = MAX_GF_GROUP_SIZE;
  self->prop.num_ref_frames = 7;
  self->prop.max_hierarchical_level = HIGHEST_PYRAMID_LEVELS;
  self->prop.keyframe_interval = MAX_KEY_FRAME_INTERVAL;
  self->prop.qp = DEFAULT_BASE_QINDEX;
  self->prop.min_qp = 0;
  self->prop.max_qp = 255;
  self->prop.mbbrc = 0;
  self->prop.filter_level = -1;
  self->prop.sharpness_level = 0;

  if (properties[PROP_RATE_CONTROL]) {
    self->prop.rc_ctrl =
        G_PARAM_SPEC_ENUM (properties[PROP_RATE_CONTROL])->default_value;
  } else {
    self->prop.rc_ctrl = VA_RC_NONE;
  }
}

static void
gst_va_vp9_enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVaVp9Enc *const self = GST_VA_VP9_ENC (object);
  GstVaBaseEnc *base = GST_VA_BASE_ENC (self);
  GstVaEncoder *encoder = NULL;
  gboolean no_effect;

  gst_object_replace ((GstObject **) (&encoder), (GstObject *) base->encoder);
  no_effect = (encoder && gst_va_encoder_is_open (encoder));
  if (encoder)
    gst_object_unref (encoder);

  GST_OBJECT_LOCK (self);

  switch (prop_id) {
    case PROP_KEYFRAME_INT:
      self->prop.keyframe_interval = g_value_get_uint (value);
      break;
    case PROP_GOLDEN_GROUP_SIZE:
      self->prop.gf_group_size = g_value_get_uint (value);
      break;
    case PROP_NUM_REF_FRAMES:
      self->prop.num_ref_frames = g_value_get_uint (value);
      break;
    case PROP_HIERARCHICAL_LEVEL:
      self->prop.max_hierarchical_level = g_value_get_uint (value);
      break;
    case PROP_QP:
      self->prop.qp = g_value_get_uint (value);
      no_effect = FALSE;
      g_atomic_int_set (&GST_VA_BASE_ENC (self)->reconf, TRUE);
      break;
    case PROP_MAX_QP:
      self->prop.max_qp = g_value_get_uint (value);
      break;
    case PROP_MIN_QP:
      self->prop.min_qp = g_value_get_uint (value);
      break;
    case PROP_BITRATE:
      self->prop.bitrate = g_value_get_uint (value);
      no_effect = FALSE;
      g_atomic_int_set (&GST_VA_BASE_ENC (self)->reconf, TRUE);
      break;
    case PROP_TARGET_USAGE:
      self->prop.target_usage = g_value_get_uint (value);
      no_effect = FALSE;
      g_atomic_int_set (&GST_VA_BASE_ENC (self)->reconf, TRUE);
      break;
    case PROP_TARGET_PERCENTAGE:
      self->prop.target_percentage = g_value_get_uint (value);
      no_effect = FALSE;
      g_atomic_int_set (&GST_VA_BASE_ENC (self)->reconf, TRUE);
      break;
    case PROP_CPB_SIZE:
      self->prop.cpb_size = g_value_get_uint (value);
      no_effect = FALSE;
      g_atomic_int_set (&GST_VA_BASE_ENC (self)->reconf, TRUE);
      break;
    case PROP_RATE_CONTROL:
      self->prop.rc_ctrl = g_value_get_enum (value);
      no_effect = FALSE;
      g_atomic_int_set (&GST_VA_BASE_ENC (self)->reconf, TRUE);
      break;
    case PROP_LOOP_FILTER_LEVEL:
      self->prop.filter_level = g_value_get_int (value);
      no_effect = FALSE;
      g_atomic_int_set (&GST_VA_BASE_ENC (self)->reconf, TRUE);
      break;
    case PROP_SHARPNESS_LEVEL:
      self->prop.sharpness_level = g_value_get_uint (value);
      no_effect = FALSE;
      g_atomic_int_set (&GST_VA_BASE_ENC (self)->reconf, TRUE);
      break;
    case PROP_MBBRC:{
      /* Macroblock-level rate control.
       * 0: use default,
       * 1: always enable,
       * 2: always disable,
       * other: reserved. */
      switch (g_value_get_enum (value)) {
        case GST_VA_FEATURE_DISABLED:
          self->prop.mbbrc = 2;
          break;
        case GST_VA_FEATURE_ENABLED:
          self->prop.mbbrc = 1;
          break;
        case GST_VA_FEATURE_AUTO:
          self->prop.mbbrc = 0;
          break;
      }
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
  }

  GST_OBJECT_UNLOCK (self);

  if (no_effect) {
#ifndef GST_DISABLE_GST_DEBUG
    GST_WARNING_OBJECT (self, "Property `%s` change may not take effect "
        "until the next encoder reconfig.", pspec->name);
#endif
  }
}

static void
gst_va_vp9_enc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstVaVp9Enc *const self = GST_VA_VP9_ENC (object);

  GST_OBJECT_LOCK (self);

  switch (prop_id) {
    case PROP_KEYFRAME_INT:
      g_value_set_uint (value, self->prop.keyframe_interval);
      break;
    case PROP_GOLDEN_GROUP_SIZE:
      g_value_set_uint (value, self->prop.gf_group_size);
      break;
    case PROP_NUM_REF_FRAMES:
      g_value_set_uint (value, self->prop.num_ref_frames);
      break;
    case PROP_HIERARCHICAL_LEVEL:
      g_value_set_uint (value, self->prop.max_hierarchical_level);
      break;
    case PROP_QP:
      g_value_set_uint (value, self->prop.qp);
      break;
    case PROP_MIN_QP:
      g_value_set_uint (value, self->prop.min_qp);
      break;
    case PROP_MAX_QP:
      g_value_set_uint (value, self->prop.max_qp);
      break;
    case PROP_BITRATE:
      g_value_set_uint (value, self->prop.bitrate);
      break;
    case PROP_TARGET_USAGE:
      g_value_set_uint (value, self->prop.target_usage);
      break;
    case PROP_TARGET_PERCENTAGE:
      g_value_set_uint (value, self->prop.target_percentage);
      break;
    case PROP_CPB_SIZE:
      g_value_set_uint (value, self->prop.cpb_size);
      break;
    case PROP_RATE_CONTROL:
      g_value_set_enum (value, self->prop.rc_ctrl);
      break;
    case PROP_MBBRC:
      g_value_set_enum (value, self->prop.mbbrc);
      break;
    case PROP_LOOP_FILTER_LEVEL:
      g_value_set_int (value, self->prop.filter_level);
      break;
    case PROP_SHARPNESS_LEVEL:
      g_value_set_uint (value, self->prop.sharpness_level);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
  }

  GST_OBJECT_UNLOCK (self);
}

static void
gst_va_vp9_enc_class_init (gpointer g_klass, gpointer class_data)
{
  GstCaps *src_doc_caps, *sink_doc_caps;
  GstPadTemplate *sink_pad_templ, *src_pad_templ;
  GObjectClass *object_class = G_OBJECT_CLASS (g_klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_klass);
  GstVideoEncoderClass *venc_class = GST_VIDEO_ENCODER_CLASS (g_klass);
  GstVaBaseEncClass *va_enc_class = GST_VA_BASE_ENC_CLASS (g_klass);
  GstVaVp9EncClass *vavp9enc_class = GST_VA_VP9_ENC_CLASS (g_klass);
  GstVaDisplay *display;
  GstVaEncoder *encoder;
  struct CData *cdata = class_data;
  gchar *long_name;
  const gchar *name, *desc;
  gint n_props = N_PROPERTIES;
  GParamFlags param_flags =
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT;

  if (cdata->entrypoint == VAEntrypointEncSlice) {
    desc = "VA-API based VP9 video encoder";
    name = "VA-API VP9 Encoder";
  } else {
    desc = "VA-API based VP9 low power video encoder";
    name = "VA-API VP9 Low Power Encoder";
  }

  if (cdata->description)
    long_name = g_strdup_printf ("%s in %s", name, cdata->description);
  else
    long_name = g_strdup (name);

  gst_element_class_set_metadata (element_class, long_name,
      "Codec/Encoder/Video/Hardware", desc, "He Junyan <junyan.he@intel.com>");

  sink_doc_caps = gst_caps_from_string (sink_caps_str);
  src_doc_caps = gst_caps_from_string (src_caps_str);

  parent_class = g_type_class_peek_parent (g_klass);

  va_enc_class->codec = VP9;
  va_enc_class->entrypoint = cdata->entrypoint;
  va_enc_class->render_device_path = g_strdup (cdata->render_device_path);
  sink_pad_templ = gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
      cdata->sink_caps);
  gst_element_class_add_pad_template (element_class, sink_pad_templ);

  gst_pad_template_set_documentation_caps (sink_pad_templ, sink_doc_caps);
  gst_caps_unref (sink_doc_caps);

  src_pad_templ = gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
      cdata->src_caps);
  gst_element_class_add_pad_template (element_class, src_pad_templ);

  gst_pad_template_set_documentation_caps (src_pad_templ, src_doc_caps);
  gst_caps_unref (src_doc_caps);

  object_class->set_property = gst_va_vp9_enc_set_property;
  object_class->get_property = gst_va_vp9_enc_get_property;

  venc_class->flush = GST_DEBUG_FUNCPTR (gst_va_vp9_enc_flush);
  va_enc_class->reset_state = GST_DEBUG_FUNCPTR (gst_va_vp9_enc_reset_state);
  va_enc_class->reconfig = GST_DEBUG_FUNCPTR (gst_va_vp9_enc_reconfig);
  va_enc_class->new_frame = GST_DEBUG_FUNCPTR (gst_va_vp9_enc_new_frame);
  va_enc_class->reorder_frame =
      GST_DEBUG_FUNCPTR (gst_va_vp9_enc_reorder_frame);
  va_enc_class->encode_frame = GST_DEBUG_FUNCPTR (gst_va_vp9_enc_encode_frame);
  va_enc_class->prepare_output =
      GST_DEBUG_FUNCPTR (gst_va_vp9_enc_prepare_output);

  {
    display = gst_va_display_platform_new (va_enc_class->render_device_path);
    encoder = gst_va_encoder_new (display, va_enc_class->codec,
        va_enc_class->entrypoint);
    if (gst_va_encoder_get_rate_control_enum (encoder,
            vavp9enc_class->rate_control)) {
      g_snprintf (vavp9enc_class->rate_control_type_name,
          G_N_ELEMENTS (vavp9enc_class->rate_control_type_name) - 1,
          "GstVaEncoderRateControl_%" GST_FOURCC_FORMAT "%s_%s",
          GST_FOURCC_ARGS (va_enc_class->codec),
          (va_enc_class->entrypoint == VAEntrypointEncSliceLP) ? "_LP" : "",
          g_path_get_basename (va_enc_class->render_device_path));
      vavp9enc_class->rate_control_type =
          g_enum_register_static (vavp9enc_class->rate_control_type_name,
          vavp9enc_class->rate_control);
      gst_type_mark_as_plugin_api (vavp9enc_class->rate_control_type, 0);
    }
    gst_object_unref (encoder);
    gst_object_unref (display);
  }

  g_free (long_name);
  g_free (cdata->description);
  g_free (cdata->render_device_path);
  gst_caps_unref (cdata->src_caps);
  gst_caps_unref (cdata->sink_caps);
  g_free (cdata);

  /**
   * GstVaVp9Enc:key-int-max:
   *
   * The maximal distance between two keyframes.
   */
  properties[PROP_KEYFRAME_INT] = g_param_spec_uint ("key-int-max",
      "Key frame maximal interval",
      "The maximal distance between two keyframes. It decides the size of GOP"
      " (0: auto-calculate)", 0, MAX_KEY_FRAME_INTERVAL, 60, param_flags);

  /**
   * GstVaVp9Enc:gf-group-size:
   *
   * The size of the golden frame group.
   */
  properties[PROP_GOLDEN_GROUP_SIZE] = g_param_spec_uint ("gf-group-size",
      "Golden frame group size",
      "The size of the golden frame group.",
      1, MAX_GF_GROUP_SIZE, DEFAULT_GF_GROUP_SIZE, param_flags);

  /**
   * GstVaVp9Enc:ref-frames:
   *
   * The number of reference frames.
   */
  properties[PROP_NUM_REF_FRAMES] = g_param_spec_uint ("ref-frames",
      "Number of Reference Frames",
      "Number of reference frames, including both the forward and the backward",
      0, 3, 3, param_flags);

  /**
   * GstVaVp9Enc:hierarchical-level:
   *
   * The hierarchical level for golden frame group.
   */
  properties[PROP_HIERARCHICAL_LEVEL] =
      g_param_spec_uint ("hierarchical-level", "The hierarchical level",
      "The hierarchical level for golden frame group. Setting to 1 disables "
      "all future reference", 1, HIGHEST_PYRAMID_LEVELS, HIGHEST_PYRAMID_LEVELS,
      param_flags);

  /**
   * GstVaVp9Enc:min-qp:
   *
   * The minimum quantizer value.
   */
  properties[PROP_MIN_QP] = g_param_spec_uint ("min-qp", "Minimum QP",
      "Minimum quantizer value for each frame", 0, 255, 0, param_flags);

  /**
   * GstVaVp9Enc:max-qp:
   *
   * The maximum quantizer value.
   */
  properties[PROP_MAX_QP] = g_param_spec_uint ("max-qp", "Maximum QP",
      "Maximum quantizer value for each frame", 1, 255, 255, param_flags);

  /**
   * GstVaVp9Enc:qp:
   *
   * The basic quantizer value for all frames.
   */
  properties[PROP_QP] = g_param_spec_uint ("qp", "The frame QP",
      "In CQP mode, it specifies the basic quantizer value for all frames. "
      "In ICQ and QVBR modes, it specifies a quality factor. In other "
      "modes, it is ignored", 0, 255, DEFAULT_BASE_QINDEX,
      param_flags | GST_PARAM_MUTABLE_PLAYING);

  /**
   * GstVaVp9Enc:bitrate:
   *
   * The desired target bitrate, expressed in kbps.
   * This is not available in CQP mode.
   *
   * CBR: This applies equally to the minimum, maximum and target bitrate.
   * VBR: This applies to the target bitrate. The driver will use the
   * "target-percentage" together to calculate the minimum and maximum bitrate.
   * VCM: This applies to the target bitrate. The minimum and maximum bitrate
   * are not needed.
   */
  properties[PROP_BITRATE] = g_param_spec_uint ("bitrate", "Bitrate (kbps)",
      "The desired bitrate expressed in kbps (0: auto-calculate)",
      0, 2000 * 1024, 0, param_flags | GST_PARAM_MUTABLE_PLAYING);

  /**
   * GstVaVp9Enc:target-percentage:
   *
   * The target percentage of the max bitrate, and expressed in uint,
   * equal to "target percentage"*100.
   * "target percentage" = "target bitrate" * 100 / "max bitrate"
   * This is available only when rate-control is VBR.
   * The driver uses it to calculate the minimum and maximum bitrate.
   */
  properties[PROP_TARGET_PERCENTAGE] = g_param_spec_uint ("target-percentage",
      "target bitrate percentage",
      "The percentage for 'target bitrate'/'maximum bitrate' (Only in VBR)",
      50, 100, 66, param_flags | GST_PARAM_MUTABLE_PLAYING);

  /**
   * GstVaVp9Enc:cpb-size:
   *
   * The desired max CPB size in Kb (0: auto-calculate).
   */
  properties[PROP_CPB_SIZE] = g_param_spec_uint ("cpb-size",
      "max CPB size in Kb",
      "The desired max CPB size in Kb (0: auto-calculate)", 0, 2000 * 1024, 0,
      param_flags | GST_PARAM_MUTABLE_PLAYING);

  /**
   * GstVaVp9Enc:target-usage:
   *
   * The target usage of the encoder. It controls and balances the encoding
   * speed and the encoding quality. The lower value has better quality but
   * slower speed, the higher value has faster speed but lower quality.
   */
  properties[PROP_TARGET_USAGE] = g_param_spec_uint ("target-usage",
      "target usage",
      "The target usage to control and balance the encoding speed/quality",
      1, 7, 4, param_flags | GST_PARAM_MUTABLE_PLAYING);

  /**
   * GstVaVp9Enc:mbbrc:
   *
   * Macroblock level bitrate control.
   * This is not compatible with Constant QP rate control.
   */
  properties[PROP_MBBRC] = g_param_spec_enum ("mbbrc",
      "Macroblock level Bitrate Control",
      "Macroblock level Bitrate Control. It is not compatible with CQP",
      GST_TYPE_VA_FEATURE, GST_VA_FEATURE_AUTO, param_flags);

  /**
   * GstVaVp9Enc:loop-filter-level:
   *
   * Controls the deblocking filter strength, -1 means auto calculation.
   */
  properties[PROP_LOOP_FILTER_LEVEL] = g_param_spec_int ("loop-filter-level",
      "Loop Filter Level",
      "Controls the deblocking filter strength, -1 means auto calculation",
      -1, 63, -1, param_flags | GST_PARAM_MUTABLE_PLAYING);

  /**
   * GstVaVp9Enc:sharpness-level:
   *
   * Controls the deblocking filter sensitivity.
   */
  properties[PROP_SHARPNESS_LEVEL] = g_param_spec_uint ("sharpness-level",
      "Sharpness Level",
      "Controls the deblocking filter sensitivity",
      0, 7, 0, param_flags | GST_PARAM_MUTABLE_PLAYING);

  if (vavp9enc_class->rate_control_type > 0) {
    properties[PROP_RATE_CONTROL] = g_param_spec_enum ("rate-control",
        "rate control mode",
        "The desired rate control mode for the encoder",
        vavp9enc_class->rate_control_type,
        vavp9enc_class->rate_control[0].value,
        GST_PARAM_CONDITIONALLY_AVAILABLE | GST_PARAM_MUTABLE_PLAYING
        | param_flags);
  } else {
    n_props--;
    properties[PROP_RATE_CONTROL] = NULL;
  }

  g_object_class_install_properties (object_class, n_props, properties);

  /**
   * GstVaFeature:
   * @GST_VA_FEATURE_DISABLED: The feature is disabled.
   * @GST_VA_FEATURE_ENABLED: The feature is enabled.
   * @GST_VA_FEATURE_AUTO: The feature is enabled automatically.
   *
   * Since: 1.24
   */
  gst_type_mark_as_plugin_api (GST_TYPE_VA_FEATURE, 0);
}

static GstCaps *
_complete_src_caps (GstCaps * srccaps)
{
  GstCaps *caps = gst_caps_copy (srccaps);
  GValue val = G_VALUE_INIT;

  g_value_init (&val, G_TYPE_STRING);
  g_value_set_string (&val, "super-frame");
  gst_caps_set_value (caps, "alignment", &val);
  g_value_unset (&val);

  return caps;
}

gboolean
gst_va_vp9_enc_register (GstPlugin * plugin, GstVaDevice * device,
    GstCaps * sink_caps, GstCaps * src_caps, guint rank,
    VAEntrypoint entrypoint)
{
  static GOnce debug_once = G_ONCE_INIT;
  GType type;
  GTypeInfo type_info = {
    .class_size = sizeof (GstVaVp9EncClass),
    .class_init = gst_va_vp9_enc_class_init,
    .instance_size = sizeof (GstVaVp9Enc),
    .instance_init = gst_va_vp9_enc_init,
  };
  struct CData *cdata;
  gboolean ret;
  gchar *type_name, *feature_name;

  g_return_val_if_fail (GST_IS_PLUGIN (plugin), FALSE);
  g_return_val_if_fail (GST_IS_VA_DEVICE (device), FALSE);
  g_return_val_if_fail (GST_IS_CAPS (sink_caps), FALSE);
  g_return_val_if_fail (GST_IS_CAPS (src_caps), FALSE);
  g_return_val_if_fail (entrypoint == VAEntrypointEncSlice ||
      entrypoint == VAEntrypointEncSliceLP, FALSE);

  cdata = g_new (struct CData, 1);
  cdata->entrypoint = entrypoint;
  cdata->description = NULL;
  cdata->render_device_path = g_strdup (device->render_device_path);
  cdata->sink_caps = gst_caps_ref (sink_caps);
  cdata->src_caps = _complete_src_caps (src_caps);

  /* class data will be leaked if the element never gets instantiated */
  GST_MINI_OBJECT_FLAG_SET (cdata->sink_caps,
      GST_MINI_OBJECT_FLAG_MAY_BE_LEAKED);
  GST_MINI_OBJECT_FLAG_SET (cdata->src_caps,
      GST_MINI_OBJECT_FLAG_MAY_BE_LEAKED);

  type_info.class_data = cdata;
  if (entrypoint == VAEntrypointEncSlice) {
    gst_va_create_feature_name (device, "GstVaVP9Enc", "GstVa%sVP9Enc",
        &type_name, "vavp9enc", "va%svp9enc", &feature_name,
        &cdata->description, &rank);
  } else {
    gst_va_create_feature_name (device, "GstVaVP9LPEnc", "GstVa%sVP9LPEnc",
        &type_name, "vavp9lpenc", "va%svp9lpenc", &feature_name,
        &cdata->description, &rank);
  }

  g_once (&debug_once, _register_debug_category, NULL);
  type = g_type_register_static (GST_TYPE_VA_BASE_ENC,
      type_name, &type_info, 0);
  ret = gst_element_register (plugin, feature_name, rank, type);

  g_free (type_name);
  g_free (feature_name);

  return ret;
}
