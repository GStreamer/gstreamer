/* GStreamer
 * Copyright (C) 2025 Seungha Yang <seungha@centricular.com>
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

#include "gsth264reorder.h"
#include <gst/codecs/gsth264picture.h>
#include <gst/codecs/gsth264picture-private.h>
#include <gst/base/base.h>
#include <string.h>

GST_DEBUG_CATEGORY_STATIC (gst_h264_reorder_debug);
#define GST_CAT_DEFAULT gst_h264_reorder_debug

struct _GstH264Reorder
{
  GstObject parent;

  gboolean need_reorder;

  gint width;
  gint height;
  gint fps_n;
  gint fps_d;
  guint nal_length_size;
  gboolean is_avc;
  GstH264NalParser *parser;
  GstH264Dpb *dpb;
  GstH264SPS *active_sps;
  GstH264PPS *active_pps;
  GstH264Picture *current_picture;
  GstVideoCodecFrame *current_frame;
  GstH264Slice current_slice;
  GstH264Picture *last_field;

  gint max_frame_num;
  gint max_pic_num;
  gint max_long_term_frame_idx;

  gint prev_frame_num;
  gint prev_ref_frame_num;
  gint prev_frame_num_offset;
  gboolean prev_has_memmgmnt5;

  /* Values related to previously decoded reference picture */
  gboolean prev_ref_has_memmgmnt5;
  gint prev_ref_top_field_order_cnt;
  gint prev_ref_pic_order_cnt_msb;
  gint prev_ref_pic_order_cnt_lsb;

  GstH264PictureField prev_ref_field;

  /* Split packetized data into actual nal chunks (for malformed stream) */
  GArray *split_nalu;

  GArray *au_nalus;

  GPtrArray *frame_queue;
  GPtrArray *output_queue;
  guint32 system_num;
  guint32 present_num;

  GstClockTime latency;
};

static void gst_h264_reorder_finalize (GObject * object);

static GstH264Picture *gst_h264_reorder_split_frame (GstH264Reorder * self,
    GstH264Picture * picture);
static void gst_h264_reorder_bump_dpb (GstH264Reorder * self,
    GstH264Picture * current_picture);
static void gst_h264_reorder_add_to_dpb (GstH264Reorder * self,
    GstH264Picture * picture);

#define gst_h264_reorder_parent_class parent_class
G_DEFINE_TYPE (GstH264Reorder, gst_h264_reorder, GST_TYPE_OBJECT);

static void
gst_h264_reorder_class_init (GstH264ReorderClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = gst_h264_reorder_finalize;

  GST_DEBUG_CATEGORY_INIT (gst_h264_reorder_debug, "h264reorder", 0,
      "h264reorder");
}

static void
gst_h264_reorder_init (GstH264Reorder * self)
{
  self->parser = gst_h264_nal_parser_new ();
  self->dpb = gst_h264_dpb_new ();
  self->frame_queue =
      g_ptr_array_new_with_free_func (
      (GDestroyNotify) gst_video_codec_frame_unref);
  self->output_queue =
      g_ptr_array_new_with_free_func (
      (GDestroyNotify) gst_video_codec_frame_unref);

  self->split_nalu = g_array_new (FALSE, FALSE, sizeof (GstH264NalUnit));
  self->au_nalus = g_array_new (FALSE, FALSE, sizeof (GstH264NalUnit));
  self->fps_n = 25;
  self->fps_d = 1;
}

static void
gst_h264_reorder_finalize (GObject * object)
{
  GstH264Reorder *self = GST_H264_REORDER (object);

  gst_h264_nal_parser_free (self->parser);
  g_ptr_array_unref (self->frame_queue);
  g_ptr_array_unref (self->output_queue);
  g_array_unref (self->split_nalu);
  g_array_unref (self->au_nalus);
  gst_h264_dpb_free (self->dpb);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gint
gst_h264_reorder_get_max_num_reorder_frames (GstH264Reorder * self,
    GstH264SPS * sps, gint max_dpb_size)
{
  if (sps->vui_parameters_present_flag
      && sps->vui_parameters.bitstream_restriction_flag) {
    if (sps->vui_parameters.num_reorder_frames > max_dpb_size) {
      GST_WARNING_OBJECT (self,
          "max_num_reorder_frames present, but larger than MaxDpbFrames (%d > %d)",
          sps->vui_parameters.num_reorder_frames, max_dpb_size);
      return max_dpb_size;
    }

    return sps->vui_parameters.num_reorder_frames;
  } else if (sps->constraint_set3_flag) {
    /* If max_num_reorder_frames is not present, if profile id is equal to
     * 44, 86, 100, 110, 122, or 244 and constraint_set3_flag is equal to 1,
     * max_num_reorder_frames shall be inferred to be equal to 0 */
    switch (sps->profile_idc) {
      case 44:
      case 86:
      case 100:
      case 110:
      case 122:
      case 244:
        return 0;
      default:
        break;
    }
  }

  if (sps->profile_idc == 66 || sps->profile_idc == 83) {
    /* baseline, constrained baseline and scalable-baseline profiles
     * only contain I/P frames. */
    return 0;
  }

  return max_dpb_size;
}

typedef struct
{
  GstH264Level level;

  guint32 max_mbps;
  guint32 max_fs;
  guint32 max_dpb_mbs;
  guint32 max_main_br;
} LevelLimits;

static const LevelLimits level_limits_map[] = {
  {GST_H264_LEVEL_L1, 1485, 99, 396, 64},
  {GST_H264_LEVEL_L1B, 1485, 99, 396, 128},
  {GST_H264_LEVEL_L1_1, 3000, 396, 900, 192},
  {GST_H264_LEVEL_L1_2, 6000, 396, 2376, 384},
  {GST_H264_LEVEL_L1_3, 11800, 396, 2376, 768},
  {GST_H264_LEVEL_L2, 11880, 396, 2376, 2000},
  {GST_H264_LEVEL_L2_1, 19800, 792, 4752, 4000},
  {GST_H264_LEVEL_L2_2, 20250, 1620, 8100, 4000},
  {GST_H264_LEVEL_L3, 40500, 1620, 8100, 10000},
  {GST_H264_LEVEL_L3_1, 108000, 3600, 18000, 14000},
  {GST_H264_LEVEL_L3_2, 216000, 5120, 20480, 20000},
  {GST_H264_LEVEL_L4, 245760, 8192, 32768, 20000},
  {GST_H264_LEVEL_L4_1, 245760, 8192, 32768, 50000},
  {GST_H264_LEVEL_L4_2, 522240, 8704, 34816, 50000},
  {GST_H264_LEVEL_L5, 589824, 22080, 110400, 135000},
  {GST_H264_LEVEL_L5_1, 983040, 36864, 184320, 240000},
  {GST_H264_LEVEL_L5_2, 2073600, 36864, 184320, 240000},
  {GST_H264_LEVEL_L6, 4177920, 139264, 696320, 240000},
  {GST_H264_LEVEL_L6_1, 8355840, 139264, 696320, 480000},
  {GST_H264_LEVEL_L6_2, 16711680, 139264, 696320, 800000}
};

static gint
h264_level_to_max_dpb_mbs (GstH264Level level)
{
  gint i;
  for (i = 0; i < G_N_ELEMENTS (level_limits_map); i++) {
    if (level == level_limits_map[i].level)
      return level_limits_map[i].max_dpb_mbs;
  }

  return 0;
}

static void
gst_h264_reorder_set_output_buffer (GstH264Reorder * self, guint frame_num)
{
  gsize i, j;

  for (i = 0; i < self->frame_queue->len; i++) {
    GstVideoCodecFrame *frame = g_ptr_array_index (self->frame_queue, i);
    if (frame->system_frame_number != frame_num)
      continue;

    /* Copy frame at present index to  */
    if (!frame->output_buffer) {
      GST_LOG_OBJECT (self, "decoding order: %u, display order: %u",
          frame_num, self->present_num);
      frame->presentation_frame_number = self->present_num;
      self->present_num++;
      for (j = 0; j < self->frame_queue->len; j++) {
        GstVideoCodecFrame *other_frame =
            g_ptr_array_index (self->frame_queue, j);
        if (other_frame->system_frame_number ==
            frame->presentation_frame_number) {
          frame->output_buffer = gst_buffer_ref (other_frame->input_buffer);
          return;
        }
      }
    }

    break;
  }
}

static void
gst_h264_reorder_output_picture (GstH264Reorder * self,
    GstH264Picture * picture)
{
  guint frame_num = GST_CODEC_PICTURE_FRAME_NUMBER (picture);

  gst_h264_reorder_set_output_buffer (self, frame_num);
  if (picture->other_field && !picture->other_field->nonexisting) {
    guint other_frame_num =
        GST_CODEC_PICTURE_FRAME_NUMBER (picture->other_field);
    if (other_frame_num != frame_num) {
      GST_LOG_OBJECT (self, "Found separate frame for second field");
      gst_h264_reorder_set_output_buffer (self, other_frame_num);
    }
  }

  gst_h264_picture_unref (picture);

  /* Move completed frames to output queue */
  while (self->frame_queue->len > 0) {
    GstVideoCodecFrame *frame = g_ptr_array_index (self->frame_queue, 0);
    if (!frame->output_buffer)
      break;

    frame = g_ptr_array_steal_index (self->frame_queue, 0);
    g_ptr_array_add (self->output_queue, frame);
  }
}

GstH264Reorder *
gst_h264_reorder_new (gboolean need_reorder)
{
  GstH264Reorder *self = g_object_new (GST_TYPE_H264_REORDER, NULL);
  gst_object_ref_sink (self);

  self->need_reorder = need_reorder;

  return self;
}

void
gst_h264_reorder_drain (GstH264Reorder * reorder)
{
  GstH264Picture *picture;

  while ((picture = gst_h264_dpb_bump (reorder->dpb, TRUE)) != NULL) {
    gst_h264_reorder_output_picture (reorder, picture);
  }

  gst_clear_h264_picture (&reorder->last_field);
  gst_h264_dpb_clear (reorder->dpb);

  /* Frame queue should be empty or holding only current frame */
  while (reorder->frame_queue->len > 0) {
    GstVideoCodecFrame *frame = g_ptr_array_index (reorder->frame_queue, 0);
    if (frame == reorder->current_frame)
      break;

    GST_WARNING_OBJECT (reorder, "Remaining frame after drain %" GST_PTR_FORMAT,
        frame->input_buffer);

    /* Move to output queue anyway  */
    frame->output_buffer = gst_buffer_ref (frame->input_buffer);
    frame = g_ptr_array_steal_index (reorder->frame_queue, 0);
    g_ptr_array_add (reorder->output_queue, frame);
  }

  /* presentation number */
  if (reorder->current_frame)
    reorder->present_num = reorder->current_frame->system_frame_number;
  else
    reorder->present_num = reorder->system_num;
}

static void
gst_h264_reorder_process_sps (GstH264Reorder * self, GstH264SPS * sps)
{
  guint8 level;
  gint max_dpb_mbs;
  gint width_mb, height_mb;
  gint max_dpb_frames;
  gint max_dpb_size;
  gint prev_max_dpb_size;
  gint max_reorder_frames;
  gint prev_max_reorder_frames;
  gboolean prev_interlaced;
  gboolean interlaced;

  interlaced = !sps->frame_mbs_only_flag;

  /* Spec A.3.1 and A.3.2
   * For Baseline, Constrained Baseline and Main profile, the indicated level is
   * Level 1b if level_idc is equal to 11 and constraint_set3_flag is equal to 1
   */
  level = sps->level_idc;
  if (level == 11 && (sps->profile_idc == 66 || sps->profile_idc == 77) &&
      sps->constraint_set3_flag) {
    /* Level 1b */
    level = 9;
  }

  max_dpb_mbs = h264_level_to_max_dpb_mbs ((GstH264Level) level);
  max_dpb_frames = GST_H264_DPB_MAX_SIZE;

  width_mb = sps->width / 16;
  height_mb = sps->height / 16;

  if (max_dpb_mbs > 0) {
    max_dpb_frames = MIN (max_dpb_mbs / (width_mb * height_mb),
        GST_H264_DPB_MAX_SIZE);
  }

  if (sps->vui_parameters_present_flag
      && sps->vui_parameters.bitstream_restriction_flag) {
    max_dpb_frames = MAX (1, sps->vui_parameters.max_dec_frame_buffering);
  }

  max_dpb_size = MAX (max_dpb_frames, sps->num_ref_frames);
  if (max_dpb_size > GST_H264_DPB_MAX_SIZE) {
    GST_WARNING_OBJECT (self, "Too large calculated DPB size %d", max_dpb_size);
    max_dpb_size = GST_H264_DPB_MAX_SIZE;
  }

  prev_max_dpb_size = gst_h264_dpb_get_max_num_frames (self->dpb);
  prev_interlaced = gst_h264_dpb_get_interlaced (self->dpb);

  prev_max_reorder_frames = gst_h264_dpb_get_max_num_reorder_frames (self->dpb);
  max_reorder_frames =
      gst_h264_reorder_get_max_num_reorder_frames (self, sps, max_dpb_size);

  if (self->width != sps->width || self->height != sps->height ||
      prev_max_dpb_size != max_dpb_size || prev_interlaced != interlaced ||
      prev_max_reorder_frames != max_reorder_frames) {
    GST_DEBUG_OBJECT (self,
        "SPS updated, resolution: %dx%d -> %dx%d, dpb size: %d -> %d, "
        "interlaced %d -> %d, max_reorder_frames: %d -> %d",
        self->width, self->height, sps->width, sps->height,
        prev_max_dpb_size, max_dpb_size, prev_interlaced, interlaced,
        prev_max_reorder_frames, max_reorder_frames);

    gst_h264_reorder_drain (self);

    self->width = sps->width;
    self->height = sps->height;

    gst_h264_dpb_set_max_num_frames (self->dpb, max_dpb_size);
    gst_h264_dpb_set_interlaced (self->dpb, interlaced);
    gst_h264_dpb_set_max_num_reorder_frames (self->dpb, max_reorder_frames);
  }

  self->latency = gst_util_uint64_scale_int (max_dpb_size * GST_SECOND,
      self->fps_d, self->fps_n);
}

static gboolean
gst_h264_reorder_parse_sps (GstH264Reorder * self, GstH264NalUnit * nalu)
{
  GstH264SPS sps;
  GstH264ParserResult pres;
  gboolean ret = TRUE;

  pres = gst_h264_parse_sps (nalu, &sps);
  if (pres != GST_H264_PARSER_OK) {
    GST_WARNING_OBJECT (self, "Failed to parse SPS, result %d", pres);
    return FALSE;
  }

  GST_LOG_OBJECT (self, "SPS parsed");

  gst_h264_reorder_process_sps (self, &sps);
  if (gst_h264_parser_update_sps (self->parser, &sps) != GST_H264_PARSER_OK) {
    GST_WARNING_OBJECT (self, "Failed to update SPS");
    ret = FALSE;
  }

  gst_h264_sps_clear (&sps);

  return ret;
}

static gboolean
gst_h264_reorder_parse_pps (GstH264Reorder * self, GstH264NalUnit * nalu)
{
  GstH264PPS pps;
  GstH264ParserResult pres;
  gboolean ret = TRUE;

  pres = gst_h264_parse_pps (self->parser, nalu, &pps);
  if (pres != GST_H264_PARSER_OK) {
    GST_WARNING_OBJECT (self, "Failed to parse PPS, result %d", pres);
    return FALSE;
  }

  GST_LOG_OBJECT (self, "PPS parsed");

  pres = gst_h264_parser_update_pps (self->parser, &pps);
  if (pres != GST_H264_PARSER_OK) {
    GST_WARNING_OBJECT (self, "Failed to update PPS");
    ret = FALSE;
  }

  gst_h264_pps_clear (&pps);

  return ret;
}

static gboolean
gst_h264_reorder_parse_codec_data (GstH264Reorder * self, const guint8 * data,
    gsize size)
{
  GstH264DecoderConfigRecord *config = NULL;
  gboolean ret = TRUE;
  GstH264NalUnit *nalu;
  guint i;

  if (gst_h264_parser_parse_decoder_config_record (self->parser, data, size,
          &config) != GST_H264_PARSER_OK) {
    GST_WARNING_OBJECT (self, "Failed to parse codec-data");
    return FALSE;
  }

  self->nal_length_size = config->length_size_minus_one + 1;
  for (i = 0; i < config->sps->len; i++) {
    nalu = &g_array_index (config->sps, GstH264NalUnit, i);

    if (nalu->type != GST_H264_NAL_SPS)
      continue;

    ret = gst_h264_reorder_parse_sps (self, nalu);
    if (!ret) {
      GST_WARNING_OBJECT (self, "Failed to parse SPS");
      goto out;
    }
  }

  for (i = 0; i < config->pps->len; i++) {
    nalu = &g_array_index (config->pps, GstH264NalUnit, i);
    if (nalu->type != GST_H264_NAL_PPS)
      continue;

    ret = gst_h264_reorder_parse_pps (self, nalu);
    if (!ret) {
      GST_WARNING_OBJECT (self, "Failed to parse PPS");
      goto out;
    }
  }

out:
  gst_h264_decoder_config_record_free (config);
  return ret;
}

gboolean
gst_h264_reorder_set_caps (GstH264Reorder * self, GstCaps * caps,
    GstClockTime * latency)
{
  GstStructure *s;
  const gchar *str;
  const GValue *codec_data;
  gboolean ret = TRUE;
  gint fps_n, fps_d;

  GST_DEBUG_OBJECT (self, "Set caps %" GST_PTR_FORMAT, caps);

  self->nal_length_size = 4;
  self->is_avc = FALSE;

  s = gst_caps_get_structure (caps, 0);
  str = gst_structure_get_string (s, "stream-format");
  if (str && (g_strcmp0 (str, "avc") == 0 || g_strcmp0 (str, "avc3") == 0))
    self->is_avc = TRUE;

  if (gst_structure_get_fraction (s, "framerate", &fps_n, &fps_d) &&
      fps_n > 0 && fps_d > 0) {
    self->fps_n = fps_n;
    self->fps_d = fps_d;
  } else {
    self->fps_n = 25;
    self->fps_d = 1;
  }

  codec_data = gst_structure_get_value (s, "codec_data");
  if (codec_data && G_VALUE_TYPE (codec_data) == GST_TYPE_BUFFER) {
    GstBuffer *buf = gst_value_get_buffer (codec_data);
    GstMapInfo info;
    if (gst_buffer_map (buf, &info, GST_MAP_READ)) {
      ret = gst_h264_reorder_parse_codec_data (self, info.data, info.size);
      gst_buffer_unmap (buf, &info);
    } else {
      GST_ERROR_OBJECT (self, "Couldn't map codec data");
      ret = FALSE;
    }
  }

  if (self->need_reorder)
    *latency = self->latency;
  else
    *latency = 0;

  return ret;
}

static gboolean
gst_h264_reorder_handle_memory_management_opt (GstH264Reorder * self,
    GstH264Picture * picture)
{
  gint i;

  for (i = 0; i < G_N_ELEMENTS (picture->dec_ref_pic_marking.ref_pic_marking);
      i++) {
    GstH264RefPicMarking *ref_pic_marking =
        &picture->dec_ref_pic_marking.ref_pic_marking[i];
    guint8 type = ref_pic_marking->memory_management_control_operation;

    GST_TRACE_OBJECT (self, "memory management operation %d, type %d", i, type);

    /* Normal end of operations' specification */
    if (type == 0)
      return TRUE;

    switch (type) {
      case 4:
        self->max_long_term_frame_idx =
            ref_pic_marking->max_long_term_frame_idx_plus1 - 1;
        break;
      case 5:
        self->max_long_term_frame_idx = -1;
        break;
      default:
        break;
    }

    if (!gst_h264_dpb_perform_memory_management_control_operation (self->dpb,
            ref_pic_marking, picture)) {
      GST_WARNING_OBJECT (self, "memory management operation type %d failed",
          type);
      /* Most likely our implementation fault, but let's just perform
       * next MMCO if any */
    }
  }

  return TRUE;
}

static gboolean
gst_h264_reorder_sliding_window_picture_marking (GstH264Reorder * self,
    GstH264Picture * picture)
{
  const GstH264SPS *sps = self->active_sps;
  gint num_ref_pics;
  gint max_num_ref_frames;

  /* Skip this for the second field */
  if (picture->second_field)
    return TRUE;

  if (!sps) {
    GST_ERROR_OBJECT (self, "No active sps");
    return FALSE;
  }

  /* 8.2.5.3. Ensure the DPB doesn't overflow by discarding the oldest picture */
  num_ref_pics = gst_h264_dpb_num_ref_frames (self->dpb);
  max_num_ref_frames = MAX (1, sps->num_ref_frames);

  if (num_ref_pics < max_num_ref_frames)
    return TRUE;

  /* In theory, num_ref_pics shouldn't be larger than max_num_ref_frames
   * but it could happen if our implementation is wrong somehow or so.
   * Just try to remove reference pictures as many as possible in order to
   * avoid DPB overflow.
   */
  while (num_ref_pics >= max_num_ref_frames) {
    /* Max number of reference pics reached, need to remove one of the short
     * term ones. Find smallest frame_num_wrap short reference picture and mark
     * it as unused */
    GstH264Picture *to_unmark =
        gst_h264_dpb_get_lowest_frame_num_short_ref (self->dpb);

    if (num_ref_pics > max_num_ref_frames) {
      GST_WARNING_OBJECT (self,
          "num_ref_pics %d is larger than allowed maximum %d",
          num_ref_pics, max_num_ref_frames);
    }

    if (!to_unmark) {
      GST_WARNING_OBJECT (self, "Could not find a short ref picture to unmark");
      return FALSE;
    }

    GST_TRACE_OBJECT (self,
        "Unmark reference flag of picture %p (frame_num %d, poc %d)",
        to_unmark, to_unmark->frame_num, to_unmark->pic_order_cnt);

    gst_h264_picture_set_reference (to_unmark, GST_H264_PICTURE_REF_NONE, TRUE);
    gst_h264_picture_unref (to_unmark);

    num_ref_pics--;
  }

  return TRUE;
}

/* This method ensures that DPB does not overflow, either by removing
 * reference pictures as specified in the stream, or using a sliding window
 * procedure to remove the oldest one.
 * It also performs marking and unmarking pictures as reference.
 * See spac 8.2.5.1 */
static gboolean
gst_h264_reorder_reference_picture_marking (GstH264Reorder * self,
    GstH264Picture * picture)
{
  /* If the current picture is an IDR, all reference pictures are unmarked */
  if (picture->idr) {
    gst_h264_dpb_mark_all_non_ref (self->dpb);

    if (picture->dec_ref_pic_marking.long_term_reference_flag) {
      gst_h264_picture_set_reference (picture,
          GST_H264_PICTURE_REF_LONG_TERM, FALSE);
      picture->long_term_frame_idx = 0;
      self->max_long_term_frame_idx = 0;
    } else {
      gst_h264_picture_set_reference (picture,
          GST_H264_PICTURE_REF_SHORT_TERM, FALSE);
      self->max_long_term_frame_idx = -1;
    }

    return TRUE;
  }

  /* Not an IDR. If the stream contains instructions on how to discard pictures
   * from DPB and how to mark/unmark existing reference pictures, do so.
   * Otherwise, fall back to default sliding window process */
  if (picture->dec_ref_pic_marking.adaptive_ref_pic_marking_mode_flag) {
    if (picture->nonexisting) {
      GST_WARNING_OBJECT (self,
          "Invalid memory management operation for non-existing picture "
          "%p (frame_num %d, poc %d", picture, picture->frame_num,
          picture->pic_order_cnt);
    }

    return gst_h264_reorder_handle_memory_management_opt (self, picture);
  }

  return gst_h264_reorder_sliding_window_picture_marking (self, picture);
}

static void
output_picture_directly (GstH264Reorder * self, GstH264Picture * picture)
{
  GstH264Picture *out_pic = NULL;

  if (GST_H264_PICTURE_IS_FRAME (picture)) {
    g_assert (self->last_field == NULL);
    out_pic = g_steal_pointer (&picture);
    goto output;
  }

  if (self->last_field == NULL) {
    if (picture->second_field) {
      GST_WARNING ("Set the last output %p poc:%d, without first field",
          picture, picture->pic_order_cnt);

      goto output;
    }

    /* Just cache the first field. */
    self->last_field = g_steal_pointer (&picture);
  } else {
    if (!picture->second_field || !picture->other_field
        || picture->other_field != self->last_field) {
      GST_WARNING ("The last field %p poc:%d is not the pair of the "
          "current field %p poc:%d",
          self->last_field, self->last_field->pic_order_cnt,
          picture, picture->pic_order_cnt);

      gst_clear_h264_picture (&self->last_field);
      goto output;
    }

    GST_TRACE ("Pair the last field %p poc:%d and the current"
        " field %p poc:%d",
        self->last_field, self->last_field->pic_order_cnt,
        picture, picture->pic_order_cnt);

    out_pic = self->last_field;
    self->last_field = NULL;
    /* Link each field. */
    out_pic->other_field = picture;
  }

output:
  if (out_pic) {
    gst_h264_dpb_set_last_output (self->dpb, out_pic);
    gst_h264_reorder_output_picture (self, out_pic);
  }

  gst_clear_h264_picture (&picture);
}

static void
gst_h264_reorder_finish_picture (GstH264Reorder * self,
    GstH264Picture * picture)
{
  /* Finish processing the picture.
   * Start by storing previous picture data for later use */
  if (picture->ref) {
    gst_h264_reorder_reference_picture_marking (self, picture);
    self->prev_ref_has_memmgmnt5 = picture->mem_mgmt_5;
    self->prev_ref_top_field_order_cnt = picture->top_field_order_cnt;
    self->prev_ref_pic_order_cnt_msb = picture->pic_order_cnt_msb;
    self->prev_ref_pic_order_cnt_lsb = picture->pic_order_cnt_lsb;
    self->prev_ref_field = picture->field;
    self->prev_ref_frame_num = picture->frame_num;
  }

  self->prev_frame_num = picture->frame_num;
  self->prev_has_memmgmnt5 = picture->mem_mgmt_5;
  self->prev_frame_num_offset = picture->frame_num_offset;

  /* Remove unused (for reference or later output) pictures from DPB, marking
   * them as such */
  gst_h264_dpb_delete_unused (self->dpb);

  /* C.4.4 */
  if (picture->mem_mgmt_5) {
    GST_TRACE_OBJECT (self, "Memory management type 5, drain the DPB");
    gst_h264_reorder_drain (self);
  }

  gst_h264_reorder_bump_dpb (self, picture);

  /* Add a ref to avoid the case of directly outputed and destroyed. */
  gst_h264_picture_ref (picture);

  /* C.4.5.1, C.4.5.2
     - If the current decoded picture is the second field of a complementary
     reference field pair, add to DPB.
     C.4.5.1
     For A reference decoded picture, the "bumping" process is invoked
     repeatedly until there is an empty frame buffer, then add to DPB:
     C.4.5.2
     For a non-reference decoded picture, if there is empty frame buffer
     after bumping the smaller POC, add to DPB.
     Otherwise, output directly. */
  if ((picture->second_field && picture->other_field
          && picture->other_field->ref)
      || picture->ref || gst_h264_dpb_has_empty_frame_buffer (self->dpb)) {
    /* Split frame into top/bottom field pictures for reference picture marking
     * process. Even if current picture has field_pic_flag equal to zero,
     * if next picture is a field picture, complementary field pair of reference
     * frame should have individual pic_num and long_term_pic_num.
     */
    if (gst_h264_dpb_get_interlaced (self->dpb) &&
        GST_H264_PICTURE_IS_FRAME (picture)) {
      GstH264Picture *other_field =
          gst_h264_reorder_split_frame (self, picture);

      gst_h264_reorder_add_to_dpb (self, picture);
      if (!other_field) {
        GST_WARNING_OBJECT (self,
            "Couldn't split frame into complementary field pair");
        /* Keep decoding anyway... */
      } else {
        gst_h264_reorder_add_to_dpb (self, other_field);
      }
    } else {
      gst_h264_reorder_add_to_dpb (self, picture);
    }
  } else {
    output_picture_directly (self, picture);
  }

  GST_LOG_OBJECT (self,
      "Finishing picture %p (frame_num %d, poc %d), entries in DPB %d",
      picture, picture->frame_num, picture->pic_order_cnt,
      gst_h264_dpb_get_size (self->dpb));

  gst_h264_picture_unref (picture);
}

static void
gst_h264_reorder_finish_current_picture (GstH264Reorder * self)
{
  gst_h264_reorder_finish_picture (self, self->current_picture);
  self->current_picture = NULL;
}

static gboolean
gst_h264_reorder_find_first_field_picture (GstH264Reorder * self,
    GstH264Slice * slice, GstH264Picture ** first_field)
{
  const GstH264SliceHdr *slice_hdr = &slice->header;
  GstH264Picture *prev_field;
  gboolean in_dpb;

  *first_field = NULL;
  prev_field = NULL;
  in_dpb = FALSE;
  if (gst_h264_dpb_get_interlaced (self->dpb)) {
    if (self->last_field) {
      prev_field = self->last_field;
      in_dpb = FALSE;
    } else if (gst_h264_dpb_get_size (self->dpb) > 0) {
      GstH264Picture *prev_picture;
      GArray *pictures;

      pictures = gst_h264_dpb_get_pictures_all (self->dpb);
      prev_picture =
          g_array_index (pictures, GstH264Picture *, pictures->len - 1);
      g_array_unref (pictures); /* prev_picture should be held */

      /* Previous picture was a field picture. */
      if (!GST_H264_PICTURE_IS_FRAME (prev_picture)
          && !prev_picture->other_field) {
        prev_field = prev_picture;
        in_dpb = TRUE;
      }
    }
  } else {
    g_assert (self->last_field == NULL);
  }

  /* This is not a field picture */
  if (!slice_hdr->field_pic_flag) {
    if (!prev_field)
      return TRUE;

    GST_WARNING_OBJECT (self, "Previous picture %p (poc %d) is not complete",
        prev_field, prev_field->pic_order_cnt);
    goto error;
  }

  /* OK, this is the first field. */
  if (!prev_field)
    return TRUE;

  if (prev_field->frame_num != slice_hdr->frame_num) {
    GST_WARNING_OBJECT (self, "Previous picture %p (poc %d) is not complete",
        prev_field, prev_field->pic_order_cnt);
    goto error;
  } else {
    GstH264PictureField current_field = slice_hdr->bottom_field_flag ?
        GST_H264_PICTURE_FIELD_BOTTOM_FIELD : GST_H264_PICTURE_FIELD_TOP_FIELD;

    if (current_field == prev_field->field) {
      GST_WARNING_OBJECT (self,
          "Currnet picture and previous picture have identical field %d",
          current_field);
      goto error;
    }
  }

  *first_field = gst_h264_picture_ref (prev_field);
  return TRUE;

error:
  if (!in_dpb) {
    gst_clear_h264_picture (&self->last_field);
  } else {
    /* FIXME: implement fill gap field picture if it is already in DPB */
  }

  return FALSE;
}

static gboolean
gst_h264_reorder_calculate_poc (GstH264Reorder * self, GstH264Picture * picture)
{
  const GstH264SPS *sps = self->active_sps;

  if (!sps) {
    GST_ERROR_OBJECT (self, "No active SPS");
    return FALSE;
  }

  switch (picture->pic_order_cnt_type) {
    case 0:{
      /* See spec 8.2.1.1 */
      gint prev_pic_order_cnt_msb, prev_pic_order_cnt_lsb;
      gint max_pic_order_cnt_lsb;

      if (picture->idr) {
        prev_pic_order_cnt_msb = prev_pic_order_cnt_lsb = 0;
      } else {
        if (self->prev_ref_has_memmgmnt5) {
          if (self->prev_ref_field != GST_H264_PICTURE_FIELD_BOTTOM_FIELD) {
            prev_pic_order_cnt_msb = 0;
            prev_pic_order_cnt_lsb = self->prev_ref_top_field_order_cnt;
          } else {
            prev_pic_order_cnt_msb = 0;
            prev_pic_order_cnt_lsb = 0;
          }
        } else {
          prev_pic_order_cnt_msb = self->prev_ref_pic_order_cnt_msb;
          prev_pic_order_cnt_lsb = self->prev_ref_pic_order_cnt_lsb;
        }
      }

      max_pic_order_cnt_lsb = 1 << (sps->log2_max_pic_order_cnt_lsb_minus4 + 4);

      if ((picture->pic_order_cnt_lsb < prev_pic_order_cnt_lsb) &&
          (prev_pic_order_cnt_lsb - picture->pic_order_cnt_lsb >=
              max_pic_order_cnt_lsb / 2)) {
        picture->pic_order_cnt_msb =
            prev_pic_order_cnt_msb + max_pic_order_cnt_lsb;
      } else if ((picture->pic_order_cnt_lsb > prev_pic_order_cnt_lsb)
          && (picture->pic_order_cnt_lsb - prev_pic_order_cnt_lsb >
              max_pic_order_cnt_lsb / 2)) {
        picture->pic_order_cnt_msb =
            prev_pic_order_cnt_msb - max_pic_order_cnt_lsb;
      } else {
        picture->pic_order_cnt_msb = prev_pic_order_cnt_msb;
      }

      if (picture->field != GST_H264_PICTURE_FIELD_BOTTOM_FIELD) {
        picture->top_field_order_cnt =
            picture->pic_order_cnt_msb + picture->pic_order_cnt_lsb;
      }

      switch (picture->field) {
        case GST_H264_PICTURE_FIELD_FRAME:
          picture->top_field_order_cnt = picture->pic_order_cnt_msb +
              picture->pic_order_cnt_lsb;
          picture->bottom_field_order_cnt = picture->top_field_order_cnt +
              picture->delta_pic_order_cnt_bottom;
          break;
        case GST_H264_PICTURE_FIELD_TOP_FIELD:
          picture->top_field_order_cnt = picture->pic_order_cnt_msb +
              picture->pic_order_cnt_lsb;
          break;
        case GST_H264_PICTURE_FIELD_BOTTOM_FIELD:
          picture->bottom_field_order_cnt = picture->pic_order_cnt_msb +
              picture->pic_order_cnt_lsb;
          break;
      }
      break;
    }

    case 1:{
      gint abs_frame_num = 0;
      gint expected_pic_order_cnt = 0;
      gint i;

      /* See spec 8.2.1.2 */
      if (self->prev_has_memmgmnt5)
        self->prev_frame_num_offset = 0;

      if (picture->idr)
        picture->frame_num_offset = 0;
      else if (self->prev_frame_num > picture->frame_num)
        picture->frame_num_offset =
            self->prev_frame_num_offset + self->max_frame_num;
      else
        picture->frame_num_offset = self->prev_frame_num_offset;

      if (sps->num_ref_frames_in_pic_order_cnt_cycle != 0)
        abs_frame_num = picture->frame_num_offset + picture->frame_num;
      else
        abs_frame_num = 0;

      if (picture->nal_ref_idc == 0 && abs_frame_num > 0)
        --abs_frame_num;

      if (abs_frame_num > 0) {
        gint pic_order_cnt_cycle_cnt, frame_num_in_pic_order_cnt_cycle;
        gint expected_delta_per_pic_order_cnt_cycle = 0;

        if (sps->num_ref_frames_in_pic_order_cnt_cycle == 0) {
          GST_WARNING_OBJECT (self,
              "Invalid num_ref_frames_in_pic_order_cnt_cycle in stream");
          return FALSE;
        }

        pic_order_cnt_cycle_cnt =
            (abs_frame_num - 1) / sps->num_ref_frames_in_pic_order_cnt_cycle;
        frame_num_in_pic_order_cnt_cycle =
            (abs_frame_num - 1) % sps->num_ref_frames_in_pic_order_cnt_cycle;

        for (i = 0; i < sps->num_ref_frames_in_pic_order_cnt_cycle; i++) {
          expected_delta_per_pic_order_cnt_cycle +=
              sps->offset_for_ref_frame[i];
        }

        expected_pic_order_cnt = pic_order_cnt_cycle_cnt *
            expected_delta_per_pic_order_cnt_cycle;
        /* frame_num_in_pic_order_cnt_cycle is verified < 255 in parser */
        for (i = 0; i <= frame_num_in_pic_order_cnt_cycle; ++i)
          expected_pic_order_cnt += sps->offset_for_ref_frame[i];
      }

      if (!picture->nal_ref_idc)
        expected_pic_order_cnt += sps->offset_for_non_ref_pic;

      if (GST_H264_PICTURE_IS_FRAME (picture)) {
        picture->top_field_order_cnt =
            expected_pic_order_cnt + picture->delta_pic_order_cnt0;
        picture->bottom_field_order_cnt = picture->top_field_order_cnt +
            sps->offset_for_top_to_bottom_field + picture->delta_pic_order_cnt1;
      } else if (picture->field != GST_H264_PICTURE_FIELD_BOTTOM_FIELD) {
        picture->top_field_order_cnt =
            expected_pic_order_cnt + picture->delta_pic_order_cnt0;
      } else {
        picture->bottom_field_order_cnt = expected_pic_order_cnt +
            sps->offset_for_top_to_bottom_field + picture->delta_pic_order_cnt0;
      }
      break;
    }

    case 2:{
      gint temp_pic_order_cnt;

      /* See spec 8.2.1.3 */
      if (self->prev_has_memmgmnt5)
        self->prev_frame_num_offset = 0;

      if (picture->idr)
        picture->frame_num_offset = 0;
      else if (self->prev_frame_num > picture->frame_num)
        picture->frame_num_offset =
            self->prev_frame_num_offset + self->max_frame_num;
      else
        picture->frame_num_offset = self->prev_frame_num_offset;

      if (picture->idr) {
        temp_pic_order_cnt = 0;
      } else if (!picture->nal_ref_idc) {
        temp_pic_order_cnt =
            2 * (picture->frame_num_offset + picture->frame_num) - 1;
      } else {
        temp_pic_order_cnt =
            2 * (picture->frame_num_offset + picture->frame_num);
      }

      if (GST_H264_PICTURE_IS_FRAME (picture)) {
        picture->top_field_order_cnt = temp_pic_order_cnt;
        picture->bottom_field_order_cnt = temp_pic_order_cnt;
      } else if (picture->field == GST_H264_PICTURE_FIELD_BOTTOM_FIELD) {
        picture->bottom_field_order_cnt = temp_pic_order_cnt;
      } else {
        picture->top_field_order_cnt = temp_pic_order_cnt;
      }
      break;
    }

    default:
      GST_WARNING_OBJECT (self,
          "Invalid pic_order_cnt_type: %d", sps->pic_order_cnt_type);
      return FALSE;
  }

  switch (picture->field) {
    case GST_H264_PICTURE_FIELD_FRAME:
      picture->pic_order_cnt =
          MIN (picture->top_field_order_cnt, picture->bottom_field_order_cnt);
      break;
    case GST_H264_PICTURE_FIELD_TOP_FIELD:
      picture->pic_order_cnt = picture->top_field_order_cnt;
      break;
    case GST_H264_PICTURE_FIELD_BOTTOM_FIELD:
      picture->pic_order_cnt = picture->bottom_field_order_cnt;
      break;
    default:
      g_assert_not_reached ();
      return FALSE;
  }

  return TRUE;
}

static gboolean
gst_h264_reorder_init_gap_picture (GstH264Reorder * self,
    GstH264Picture * picture, gint frame_num)
{
  picture->nonexisting = TRUE;
  picture->nal_ref_idc = 1;
  picture->frame_num = picture->pic_num = frame_num;
  picture->dec_ref_pic_marking.adaptive_ref_pic_marking_mode_flag = FALSE;
  picture->ref = GST_H264_PICTURE_REF_SHORT_TERM;
  picture->ref_pic = TRUE;
  picture->dec_ref_pic_marking.long_term_reference_flag = FALSE;
  picture->field = GST_H264_PICTURE_FIELD_FRAME;

  return gst_h264_reorder_calculate_poc (self, picture);
}

static void
gst_h264_reorder_update_pic_nums (GstH264Reorder * self,
    GstH264Picture * current_picture, gint frame_num)
{
  GArray *dpb = gst_h264_dpb_get_pictures_all (self->dpb);
  gint i;

  for (i = 0; i < dpb->len; i++) {
    GstH264Picture *picture = g_array_index (dpb, GstH264Picture *, i);

    if (!GST_H264_PICTURE_IS_REF (picture))
      continue;

    if (GST_H264_PICTURE_IS_LONG_TERM_REF (picture)) {
      if (GST_H264_PICTURE_IS_FRAME (current_picture))
        picture->long_term_pic_num = picture->long_term_frame_idx;
      else if (current_picture->field == picture->field)
        picture->long_term_pic_num = 2 * picture->long_term_frame_idx + 1;
      else
        picture->long_term_pic_num = 2 * picture->long_term_frame_idx;
    } else {
      if (picture->frame_num > frame_num)
        picture->frame_num_wrap = picture->frame_num - self->max_frame_num;
      else
        picture->frame_num_wrap = picture->frame_num;

      if (GST_H264_PICTURE_IS_FRAME (current_picture))
        picture->pic_num = picture->frame_num_wrap;
      else if (picture->field == current_picture->field)
        picture->pic_num = 2 * picture->frame_num_wrap + 1;
      else
        picture->pic_num = 2 * picture->frame_num_wrap;
    }
  }

  g_array_unref (dpb);
}

static void
gst_h264_reorder_bump_dpb (GstH264Reorder * self,
    GstH264Picture * current_picture)
{
  while (gst_h264_dpb_needs_bump (self->dpb,
          current_picture, GST_H264_DPB_BUMP_NORMAL_LATENCY)) {
    GstH264Picture *to_output = gst_h264_dpb_bump (self->dpb, FALSE);

    if (!to_output) {
      GST_WARNING_OBJECT (self, "Bumping is needed but no picture to output");
      break;
    }

    gst_h264_reorder_output_picture (self, to_output);
  }
}

static GstH264Picture *
gst_h264_reorder_split_frame (GstH264Reorder * self, GstH264Picture * picture)
{
  GstH264Picture *other_field;

  g_assert (GST_H264_PICTURE_IS_FRAME (picture));

  other_field = gst_h264_picture_new ();
  other_field->other_field = picture;
  other_field->second_field = TRUE;

  GST_LOG_OBJECT (self, "Split picture %p, poc %d, frame num %d",
      picture, picture->pic_order_cnt, picture->frame_num);

  /* FIXME: enhance TFF decision by using picture timing SEI */
  if (picture->top_field_order_cnt < picture->bottom_field_order_cnt) {
    picture->field = GST_H264_PICTURE_FIELD_TOP_FIELD;
    picture->pic_order_cnt = picture->top_field_order_cnt;

    other_field->field = GST_H264_PICTURE_FIELD_BOTTOM_FIELD;
    other_field->pic_order_cnt = picture->bottom_field_order_cnt;
  } else {
    picture->field = GST_H264_PICTURE_FIELD_BOTTOM_FIELD;
    picture->pic_order_cnt = picture->bottom_field_order_cnt;

    other_field->field = GST_H264_PICTURE_FIELD_TOP_FIELD;
    other_field->pic_order_cnt = picture->top_field_order_cnt;
  }

  other_field->top_field_order_cnt = picture->top_field_order_cnt;
  other_field->bottom_field_order_cnt = picture->bottom_field_order_cnt;
  other_field->frame_num = picture->frame_num;
  other_field->ref = picture->ref;
  other_field->nonexisting = picture->nonexisting;
  GST_CODEC_PICTURE_COPY_FRAME_NUMBER (other_field, picture);
  other_field->field_pic_flag = picture->field_pic_flag;

  return other_field;
}

static void
gst_h264_reorder_add_to_dpb (GstH264Reorder * self, GstH264Picture * picture)
{
  if (!gst_h264_dpb_get_interlaced (self->dpb)) {
    g_assert (self->last_field == NULL);
    gst_h264_dpb_add (self->dpb, picture);
    return;
  }

  /* The first field of the last picture may not be able to enter the
     DPB if it is a non ref, but if the second field enters the DPB, we
     need to add both of them. */
  if (self->last_field && picture->other_field == self->last_field) {
    gst_h264_dpb_add (self->dpb, self->last_field);
    self->last_field = NULL;
  }

  gst_h264_dpb_add (self->dpb, picture);
}

static gboolean
gst_h264_reorder_handle_frame_num_gap (GstH264Reorder * self, gint frame_num)
{
  const GstH264SPS *sps = self->active_sps;
  gint unused_short_term_frame_num;

  if (!sps) {
    GST_ERROR_OBJECT (self, "No active sps");
    return FALSE;
  }

  if (self->prev_ref_frame_num == frame_num) {
    GST_TRACE_OBJECT (self,
        "frame_num == PrevRefFrameNum (%d), not a gap", frame_num);
    return TRUE;
  }

  if (((self->prev_ref_frame_num + 1) % self->max_frame_num) == frame_num) {
    GST_TRACE_OBJECT (self,
        "frame_num ==  (PrevRefFrameNum + 1) %% MaxFrameNum (%d), not a gap",
        frame_num);
    return TRUE;
  }

  if (gst_h264_dpb_get_size (self->dpb) == 0) {
    GST_TRACE_OBJECT (self, "DPB is empty, not a gap");
    return TRUE;
  }

  if (!sps->gaps_in_frame_num_value_allowed_flag) {
    /* This is likely the case where some frames were dropped.
     * then we need to keep decoding without error out */
    GST_WARNING_OBJECT (self, "Invalid frame num %d, maybe frame drop",
        frame_num);
    return TRUE;
  }

  GST_DEBUG_OBJECT (self, "Handling frame num gap %d -> %d (MaxFrameNum: %d)",
      self->prev_ref_frame_num, frame_num, self->max_frame_num);

  /* 7.4.3/7-23 */
  unused_short_term_frame_num =
      (self->prev_ref_frame_num + 1) % self->max_frame_num;
  while (unused_short_term_frame_num != frame_num) {
    GstH264Picture *picture = gst_h264_picture_new ();

    if (!gst_h264_reorder_init_gap_picture (self, picture,
            unused_short_term_frame_num)) {
      return FALSE;
    }

    gst_h264_reorder_update_pic_nums (self, picture,
        unused_short_term_frame_num);

    /* C.2.1 */
    if (!gst_h264_reorder_sliding_window_picture_marking (self, picture)) {
      GST_ERROR_OBJECT (self,
          "Couldn't perform sliding window picture marking");
      return FALSE;
    }

    gst_h264_dpb_delete_unused (self->dpb);
    gst_h264_reorder_bump_dpb (self, picture);

    /* the picture is short term ref, add to DPB. */
    if (gst_h264_dpb_get_interlaced (self->dpb)) {
      GstH264Picture *other_field =
          gst_h264_reorder_split_frame (self, picture);

      gst_h264_reorder_add_to_dpb (self, picture);
      gst_h264_reorder_add_to_dpb (self, other_field);
    } else {
      gst_h264_reorder_add_to_dpb (self, picture);
    }

    unused_short_term_frame_num++;
    unused_short_term_frame_num %= self->max_frame_num;
  }

  return TRUE;
}

static gboolean
gst_h264_reorder_fill_picture_from_slice (GstH264Reorder * self,
    const GstH264Slice * slice, GstH264Picture * picture)
{
  const GstH264SliceHdr *slice_hdr = &slice->header;
  const GstH264PPS *pps;
  const GstH264SPS *sps;

  pps = slice_hdr->pps;
  if (!pps) {
    GST_ERROR_OBJECT (self, "No pps in slice header");
    return FALSE;
  }

  sps = pps->sequence;
  if (!sps) {
    GST_ERROR_OBJECT (self, "No sps in pps");
    return FALSE;
  }

  picture->idr = slice->nalu.idr_pic_flag;
  picture->dec_ref_pic_marking = slice_hdr->dec_ref_pic_marking;
  picture->field_pic_flag = slice_hdr->field_pic_flag;

  if (picture->idr)
    picture->idr_pic_id = slice_hdr->idr_pic_id;

  if (slice_hdr->field_pic_flag) {
    picture->field =
        slice_hdr->bottom_field_flag ?
        GST_H264_PICTURE_FIELD_BOTTOM_FIELD : GST_H264_PICTURE_FIELD_TOP_FIELD;
  } else {
    picture->field = GST_H264_PICTURE_FIELD_FRAME;
  }

  picture->nal_ref_idc = slice->nalu.ref_idc;
  if (slice->nalu.ref_idc != 0) {
    gst_h264_picture_set_reference (picture,
        GST_H264_PICTURE_REF_SHORT_TERM, FALSE);
  }

  picture->frame_num = slice_hdr->frame_num;

  /* 7.4.3 */
  if (!slice_hdr->field_pic_flag)
    picture->pic_num = slice_hdr->frame_num;
  else
    picture->pic_num = 2 * slice_hdr->frame_num + 1;

  picture->pic_order_cnt_type = sps->pic_order_cnt_type;
  switch (picture->pic_order_cnt_type) {
    case 0:
      picture->pic_order_cnt_lsb = slice_hdr->pic_order_cnt_lsb;
      picture->delta_pic_order_cnt_bottom =
          slice_hdr->delta_pic_order_cnt_bottom;
      break;
    case 1:
      picture->delta_pic_order_cnt0 = slice_hdr->delta_pic_order_cnt[0];
      picture->delta_pic_order_cnt1 = slice_hdr->delta_pic_order_cnt[1];
      break;
    case 2:
      break;
    default:
      g_assert_not_reached ();
      return FALSE;
  }

  return TRUE;
}

static gboolean
gst_h264_reorder_init_current_picture (GstH264Reorder * self)
{
  if (!gst_h264_reorder_fill_picture_from_slice (self, &self->current_slice,
          self->current_picture)) {
    return FALSE;
  }

  if (!gst_h264_reorder_calculate_poc (self, self->current_picture))
    return FALSE;

  /* If the slice header indicates we will have to perform reference marking
   * process after this picture is decoded, store required data for that
   * purpose */
  if (self->current_slice.header.
      dec_ref_pic_marking.adaptive_ref_pic_marking_mode_flag) {
    self->current_picture->dec_ref_pic_marking =
        self->current_slice.header.dec_ref_pic_marking;
  }

  return TRUE;
}

static gboolean
gst_h264_reorder_start_current_picture (GstH264Reorder * self)
{
  const GstH264SPS *sps = self->active_sps;
  GstH264Picture *current_picture = self->current_picture;
  gint frame_num;

  self->max_frame_num = sps->max_frame_num;
  frame_num = self->current_slice.header.frame_num;
  if (self->current_slice.nalu.idr_pic_flag)
    self->prev_ref_frame_num = 0;

  if (!gst_h264_reorder_handle_frame_num_gap (self, frame_num))
    return FALSE;

  if (!gst_h264_reorder_init_current_picture (self))
    return FALSE;

  /* If the new picture is an IDR, flush DPB */
  if (current_picture->idr) {
    /* Ignores no_output_of_prior_pics_flag flag here. We don't do actual
     * decoding here */
    gst_h264_reorder_drain (self);
  }

  gst_h264_reorder_update_pic_nums (self, current_picture, frame_num);

  return TRUE;
}

static gboolean
gst_h264_reorder_parse_slice (GstH264Reorder * self, GstH264NalUnit * nalu)
{
  GstH264ParserResult pres = GST_H264_PARSER_OK;

  memset (&self->current_slice, 0, sizeof (GstH264Slice));

  pres = gst_h264_parser_parse_slice_hdr (self->parser, nalu,
      &self->current_slice.header, FALSE, TRUE);

  if (pres != GST_H264_PARSER_OK) {
    GST_ERROR_OBJECT (self, "Failed to parse slice header, ret %d", pres);
    memset (&self->current_slice, 0, sizeof (GstH264Slice));

    return FALSE;
  }

  self->current_slice.nalu = *nalu;
  self->active_pps = self->current_slice.header.pps;
  self->active_sps = self->active_pps->sequence;

  /* Check whether field picture boundary within given codec frame.
   * This might happen in case that upstream sent buffer per frame unit,
   * not picture unit (i.e., AU unit).
   * If AU boundary is detected, then finish first field picture we decoded
   * in this chain, we should finish the current picture and
   * start new field picture decoding */
  if (gst_h264_dpb_get_interlaced (self->dpb) && self->current_picture &&
      !GST_H264_PICTURE_IS_FRAME (self->current_picture) &&
      !self->current_picture->second_field) {
    GstH264PictureField prev_field = self->current_picture->field;
    GstH264PictureField cur_field = GST_H264_PICTURE_FIELD_FRAME;
    if (self->current_slice.header.field_pic_flag)
      cur_field = self->current_slice.header.bottom_field_flag ?
          GST_H264_PICTURE_FIELD_BOTTOM_FIELD :
          GST_H264_PICTURE_FIELD_TOP_FIELD;

    if (cur_field != prev_field) {
      GST_LOG_OBJECT (self,
          "Found new field picture, finishing the first field picture");
      gst_h264_reorder_finish_current_picture (self);
    }
  }

  if (!self->current_picture) {
    GstH264Picture *picture = NULL;
    GstH264Picture *first_field = NULL;

    if (!gst_h264_reorder_find_first_field_picture (self,
            &self->current_slice, &first_field)) {
      GST_ERROR_OBJECT (self, "Couldn't find or determine first picture");
      return FALSE;
    }

    picture = gst_h264_picture_new ();
    if (first_field) {
      picture->other_field = first_field;
      picture->second_field = TRUE;
      gst_h264_picture_unref (first_field);
    }

    /* This allows accessing the frame from the picture. */
    GST_CODEC_PICTURE_FRAME_NUMBER (picture) =
        self->current_frame->system_frame_number;
    self->current_picture = picture;

    if (!gst_h264_reorder_start_current_picture (self)) {
      GST_WARNING_OBJECT (self, "start picture failed");
      return FALSE;
    }
  }

  self->max_pic_num = self->current_slice.header.max_pic_num;

  return TRUE;
}

static gboolean
gst_h264_reorder_decode_nal (GstH264Reorder * self, GstH264NalUnit * nalu)
{
  gboolean ret = TRUE;

  GST_LOG_OBJECT (self, "Parsed nal type: %d, offset %d, size %d",
      nalu->type, nalu->offset, nalu->size);

  switch (nalu->type) {
    case GST_H264_NAL_SPS:
      ret = gst_h264_reorder_parse_sps (self, nalu);
      break;
    case GST_H264_NAL_PPS:
      ret = gst_h264_reorder_parse_pps (self, nalu);
      break;
    case GST_H264_NAL_SLICE:
    case GST_H264_NAL_SLICE_DPA:
    case GST_H264_NAL_SLICE_DPB:
    case GST_H264_NAL_SLICE_DPC:
    case GST_H264_NAL_SLICE_IDR:
    case GST_H264_NAL_SLICE_EXT:
      ret = gst_h264_reorder_parse_slice (self, nalu);
      break;
    default:
      break;
  }

  return ret;
}

/* A53-4 Table 6.7 */
#define A53_USER_DATA_ID_GA94 0x47413934
#define A53_USER_DATA_ID_DTG1 0x44544731

/* CEA-708 Table 2 */
#define CEA_708_PROCESS_CC_DATA_FLAG 0x40
#define CEA_708_PROCESS_EM_DATA_FLAG 0x80

/* country codes */
#define ITU_T_T35_COUNTRY_CODE_UK 0xB4
#define ITU_T_T35_COUNTRY_CODE_US 0xB5

/* provider codes */
#define ITU_T_T35_MANUFACTURER_US_ATSC  0x31
#define ITU_T_T35_MANUFACTURER_US_DIRECTV 0x2F

/* custom id for SCTE 20 608 */
#define USER_DATA_ID_SCTE_20_CC 0xFFFFFFFE
/* custom id for DirecTV */
#define USER_DATA_ID_DIRECTV_CC 0xFFFFFFFF

/* A53-4 Table 6.9 */
#define A53_USER_DATA_TYPE_CODE_CC_DATA 0x03
#define A53_USER_DATA_TYPE_CODE_BAR_DATA 0x06

/* Copied from gstvideoparseutils.c */
gboolean
gst_h264_reorder_is_cea708_sei (guint8 country_code, const guint8 * data,
    gsize size)
{
  guint16 provider_code;
  GstByteReader br;
  guint32 user_data_id = 0;
  guint8 user_data_type_code = 0;

  if (country_code != ITU_T_T35_COUNTRY_CODE_UK &&
      country_code != ITU_T_T35_COUNTRY_CODE_US) {
    return FALSE;
  }

  if (!data || size < 2)
    return FALSE;

  gst_byte_reader_init (&br, data, size);
  provider_code = gst_byte_reader_get_uint16_be_unchecked (&br);

  switch (provider_code) {
    case ITU_T_T35_MANUFACTURER_US_ATSC:
      if (!gst_byte_reader_peek_uint32_be (&br, &user_data_id))
        return FALSE;

      switch (user_data_id) {
        case A53_USER_DATA_ID_DTG1:
        case A53_USER_DATA_ID_GA94:
          /* ANSI/SCTE 128-2010a section 8.1.2 */
          if (!gst_byte_reader_get_uint32_be (&br, &user_data_id))
            return FALSE;
          break;
        default:
          /* check for SCTE 20 */
          if (user_data_id >> 24 == A53_USER_DATA_TYPE_CODE_CC_DATA) {
            user_data_id = USER_DATA_ID_SCTE_20_CC;
            gst_byte_reader_skip (&br, 1);
          }
          break;
      }
      break;
    case ITU_T_T35_MANUFACTURER_US_DIRECTV:
      user_data_id = USER_DATA_ID_DIRECTV_CC;
      break;
    default:
      return FALSE;
  }

  switch (user_data_id) {
    case USER_DATA_ID_DIRECTV_CC:
    case A53_USER_DATA_ID_GA94:
      if (!gst_byte_reader_get_uint8 (&br, &user_data_type_code))
        return FALSE;

      if (user_data_type_code == A53_USER_DATA_TYPE_CODE_CC_DATA)
        return TRUE;
      break;
    default:
      break;
  }

  return FALSE;
}

static GstBuffer *
gst_h264_reorder_remove_caption_sei (GstH264Reorder * self, GstBuffer * buffer)
{
  GstH264ParserResult pres = GST_H264_PARSER_OK;
  GstMapInfo map;
  GstH264NalUnit nalu;
  guint i;
  gboolean have_sei = FALSE;
  GstBuffer *new_buf;

  g_array_set_size (self->au_nalus, 0);

  gst_buffer_map (buffer, &map, GST_MAP_READ);
  if (self->is_avc) {
    guint offset = 0;
    gsize consumed = 0;
    guint i;

    do {
      pres = gst_h264_parser_identify_and_split_nalu_avc (self->parser,
          map.data, offset, map.size, self->nal_length_size,
          self->split_nalu, &consumed);
      if (pres != GST_H264_PARSER_OK)
        break;

      for (i = 0; i < self->split_nalu->len; i++) {
        nalu = g_array_index (self->split_nalu, GstH264NalUnit, i);
        g_array_append_val (self->au_nalus, nalu);
      }

      offset += consumed;
    } while (pres == GST_H264_PARSER_OK);
  } else {
    pres = gst_h264_parser_identify_nalu (self->parser,
        map.data, 0, map.size, &nalu);

    if (pres == GST_H264_PARSER_NO_NAL_END)
      pres = GST_H264_PARSER_OK;

    while (pres == GST_H264_PARSER_OK) {
      g_array_append_val (self->au_nalus, nalu);

      pres = gst_h264_parser_identify_nalu (self->parser,
          map.data, nalu.offset + nalu.size, map.size, &nalu);

      if (pres == GST_H264_PARSER_NO_NAL_END)
        pres = GST_H264_PARSER_OK;
    }
  }

  /* Fast scan without parsing */
  for (i = 0; i < self->au_nalus->len; i++) {
    GstH264NalUnit *nl = &g_array_index (self->au_nalus, GstH264NalUnit, i);

    /* gst_h264_parser_parse_sei() will fail if SPS was not set  */
    if (nl->type == GST_H264_NAL_SPS) {
      GstH264SPS sps;
      pres = gst_h264_parser_parse_sps (self->parser, nl, &sps);
      if (pres == GST_H264_PARSER_OK)
        gst_h264_sps_clear (&sps);
    } else if (nl->type == GST_H264_NAL_SEI) {
      have_sei = TRUE;
    }
  }

  if (!have_sei) {
    GST_LOG_OBJECT (self, "Buffer without SEI, %" GST_PTR_FORMAT, buffer);
    gst_buffer_unmap (buffer, &map);
    g_array_set_size (self->au_nalus, 0);
    return gst_buffer_ref (buffer);
  }

  new_buf = gst_buffer_new ();
  gst_buffer_copy_into (new_buf, buffer, GST_BUFFER_COPY_METADATA, 0, -1);

  for (i = 0; i < self->au_nalus->len; i++) {
    GstH264NalUnit *nl = &g_array_index (self->au_nalus, GstH264NalUnit, i);
    GstMemory *mem = NULL;

    if (nl->type == GST_H264_NAL_SEI) {
      GArray *msg = NULL;
      gint j;
      gst_h264_parser_parse_sei (self->parser, nl, &msg);
      gboolean have_caption_sei = FALSE;

      for (j = 0; j < (gint) msg->len; j++) {
        GstH264SEIMessage *sei = &g_array_index (msg, GstH264SEIMessage, j);
        GstH264RegisteredUserData *rud;
        if (sei->payloadType != GST_H264_SEI_REGISTERED_USER_DATA)
          continue;

        rud = &sei->payload.registered_user_data;

        if (!gst_h264_reorder_is_cea708_sei (rud->country_code,
                rud->data, rud->size)) {
          continue;
        }

        GST_LOG_OBJECT (self, "Found CEA708 caption SEI");
        have_caption_sei = TRUE;

        g_array_remove_index (msg, j);
        j--;
      }

      if (have_caption_sei) {
        if (msg->len > 0) {
          /* Creates new SEI memory */
          if (self->is_avc)
            mem = gst_h264_create_sei_memory_avc (self->nal_length_size, msg);
          else
            mem = gst_h264_create_sei_memory (4, msg);

          if (!mem)
            GST_ERROR_OBJECT (self, "Couldn't create SEI memory");
          else
            gst_buffer_append_memory (new_buf, mem);
        }
      } else {
        gsize size = nl->size + (nl->offset - nl->sc_offset);
        gpointer *data = g_memdup2 (nl->data + nl->sc_offset, size);
        mem = gst_memory_new_wrapped (0, data, size, 0, size, data, g_free);
        gst_buffer_append_memory (new_buf, mem);
      }

      g_array_unref (msg);
    } else {
      gsize size = nl->size + (nl->offset - nl->sc_offset);
      gpointer *data = g_memdup2 (nl->data + nl->sc_offset, size);
      mem = gst_memory_new_wrapped (0, data, size, 0, size, data, g_free);
      gst_buffer_append_memory (new_buf, mem);
    }
  }

  gst_buffer_unmap (buffer, &map);
  g_array_set_size (self->au_nalus, 0);

  return new_buf;
}

gboolean
gst_h264_reorder_push (GstH264Reorder * reorder, GstVideoCodecFrame * frame,
    GstClockTime * latency)
{
  GstBuffer *in_buf;
  GstH264NalUnit nalu;
  GstH264ParserResult pres = GST_H264_PARSER_OK;
  GstMapInfo map;
  gboolean decode_ret = TRUE;

  frame->system_frame_number = reorder->system_num;
  frame->decode_frame_number = reorder->system_num;

  GST_LOG_OBJECT (reorder,
      "Push frame %u, frame queue size: %u, output queue size %u",
      frame->system_frame_number, reorder->frame_queue->len,
      reorder->output_queue->len);

  in_buf = gst_h264_reorder_remove_caption_sei (reorder, frame->input_buffer);
  if (in_buf) {
    gst_buffer_unref (frame->input_buffer);
    frame->input_buffer = in_buf;
  } else {
    in_buf = frame->input_buffer;
  }

  reorder->system_num++;

  if (!reorder->need_reorder) {
    g_ptr_array_add (reorder->output_queue, frame);
    *latency = 0;
    return TRUE;
  }

  g_ptr_array_add (reorder->frame_queue, frame);
  reorder->current_frame = frame;

  gst_buffer_map (in_buf, &map, GST_MAP_READ);
  if (reorder->is_avc) {
    guint offset = 0;
    gsize consumed = 0;
    guint i;

    do {
      pres = gst_h264_parser_identify_and_split_nalu_avc (reorder->parser,
          map.data, offset, map.size, reorder->nal_length_size,
          reorder->split_nalu, &consumed);
      if (pres != GST_H264_PARSER_OK)
        break;

      for (i = 0; i < reorder->split_nalu->len; i++) {
        GstH264NalUnit *nl =
            &g_array_index (reorder->split_nalu, GstH264NalUnit, i);
        decode_ret = gst_h264_reorder_decode_nal (reorder, nl);
        if (!decode_ret)
          break;
      }

      offset += consumed;
    } while (pres == GST_H264_PARSER_OK && decode_ret);
  } else {
    pres = gst_h264_parser_identify_nalu (reorder->parser,
        map.data, 0, map.size, &nalu);

    if (pres == GST_H264_PARSER_NO_NAL_END)
      pres = GST_H264_PARSER_OK;

    while (pres == GST_H264_PARSER_OK && decode_ret) {
      decode_ret = gst_h264_reorder_decode_nal (reorder, &nalu);

      pres = gst_h264_parser_identify_nalu (reorder->parser,
          map.data, nalu.offset + nalu.size, map.size, &nalu);

      if (pres == GST_H264_PARSER_NO_NAL_END)
        pres = GST_H264_PARSER_OK;
    }
  }

  gst_buffer_unmap (in_buf, &map);

  if (!decode_ret) {
    GST_ERROR_OBJECT (reorder, "Couldn't decode frame");
    gst_clear_h264_picture (&reorder->current_picture);
    reorder->current_frame = NULL;

    g_ptr_array_remove (reorder->frame_queue, frame);
    reorder->system_num--;

    return FALSE;
  }

  if (!reorder->current_picture) {
    GST_DEBUG_OBJECT (reorder,
        "AU buffer without slice data, current frame %u",
        frame->system_frame_number);

    g_ptr_array_remove (reorder->frame_queue, frame);
    reorder->current_frame = NULL;
    reorder->system_num--;

    return FALSE;
  }

  gst_h264_reorder_finish_picture (reorder, reorder->current_picture);
  reorder->current_picture = NULL;
  reorder->current_frame = NULL;

  *latency = reorder->latency;

  return TRUE;
}

GstVideoCodecFrame *
gst_h264_reorder_pop (GstH264Reorder * reorder)
{
  if (!reorder->output_queue->len) {
    GST_LOG_OBJECT (reorder, "Empty output queue, frames queue size %u",
        reorder->frame_queue->len);
    return NULL;
  }

  return g_ptr_array_steal_index (reorder->output_queue, 0);
}

guint
gst_h264_reorder_get_num_buffered (GstH264Reorder * reorder)
{
  return reorder->frame_queue->len + reorder->output_queue->len;
}

GstBuffer *
gst_h264_reorder_insert_sei (GstH264Reorder * reorder, GstBuffer * au,
    GArray * sei)
{
  GstMemory *mem;
  GstBuffer *new_buf;

  if (reorder->is_avc)
    mem = gst_h264_create_sei_memory_avc (reorder->nal_length_size, sei);
  else
    mem = gst_h264_create_sei_memory (4, sei);

  if (!mem) {
    GST_ERROR_OBJECT (reorder, "Couldn't create SEI memory");
    return NULL;
  }

  if (reorder->is_avc) {
    new_buf = gst_h264_parser_insert_sei_avc (reorder->parser,
        reorder->nal_length_size, au, mem);
  } else {
    new_buf = gst_h264_parser_insert_sei (reorder->parser, au, mem);
  }

  gst_memory_unref (mem);
  return new_buf;
}
