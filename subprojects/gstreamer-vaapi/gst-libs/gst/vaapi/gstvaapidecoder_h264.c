/*
 *  gstvaapidecoder_h264.c - H.264 decoder
 *
 *  Copyright (C) 2011-2014 Intel Corporation
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
 * SECTION:gstvaapidecoder_h264
 * @short_description: H.264 decoder
 */

#include "sysdeps.h"
#include <gst/base/gstadapter.h>
#include <gst/codecparsers/gsth264parser.h>
#include "gstvaapidecoder_h264.h"
#include "gstvaapidecoder_objects.h"
#include "gstvaapidecoder_priv.h"
#include "gstvaapidisplay_priv.h"
#include "gstvaapiutils_h264_priv.h"

#define DEBUG 1
#include "gstvaapidebug.h"

/* Defined to 1 if strict ordering of DPB is needed. Only useful for debug */
#define USE_STRICT_DPB_ORDERING 0

typedef struct _GstVaapiDecoderH264Private GstVaapiDecoderH264Private;
typedef struct _GstVaapiDecoderH264Class GstVaapiDecoderH264Class;
typedef struct _GstVaapiFrameStore GstVaapiFrameStore;
typedef struct _GstVaapiFrameStoreClass GstVaapiFrameStoreClass;
typedef struct _GstVaapiParserInfoH264 GstVaapiParserInfoH264;
typedef struct _GstVaapiPictureH264 GstVaapiPictureH264;
typedef struct _GstVaapiStereo3DInfo GstVaapiStereo3DInfo;

// Used for field_poc[]
#define TOP_FIELD       0
#define BOTTOM_FIELD    1

/* ------------------------------------------------------------------------- */
/* --- H.264 Parser Info                                                 --- */
/* ------------------------------------------------------------------------- */

/*
 * Extended decoder unit flags:
 *
 * @GST_VAAPI_DECODER_UNIT_AU_START: marks the start of an access unit.
 * @GST_VAAPI_DECODER_UNIT_AU_END: marks the end of an access unit.
 */
enum
{
  /* This flag does not strictly follow the definitions (7.4.1.2.3)
     for detecting the start of an access unit as we are only
     interested in knowing if the current slice is the first one or
     the last one in the current access unit */
  GST_VAAPI_DECODER_UNIT_FLAG_AU_START =
      (GST_VAAPI_DECODER_UNIT_FLAG_LAST << 0),
  GST_VAAPI_DECODER_UNIT_FLAG_AU_END = (GST_VAAPI_DECODER_UNIT_FLAG_LAST << 1),

  GST_VAAPI_DECODER_UNIT_FLAGS_AU = (GST_VAAPI_DECODER_UNIT_FLAG_AU_START |
      GST_VAAPI_DECODER_UNIT_FLAG_AU_END),
};

#define GST_VAAPI_PARSER_INFO_H264(obj) \
    ((GstVaapiParserInfoH264 *)(obj))

struct _GstVaapiParserInfoH264
{
  GstVaapiMiniObject parent_instance;
  GstH264NalUnit nalu;
  union
  {
    GstH264SPS sps;
    GstH264PPS pps;
    GArray *sei;
    GstH264SliceHdr slice_hdr;
  } data;
  guint state;
  guint flags;                  // Same as decoder unit flags (persistent)
  guint view_id;                // View ID of slice
  guint voc;                    // View order index (VOIdx) of slice
};

static void
gst_vaapi_parser_info_h264_finalize (GstVaapiParserInfoH264 * pi)
{
  if (!pi->nalu.valid)
    return;

  switch (pi->nalu.type) {
    case GST_H264_NAL_SPS:
    case GST_H264_NAL_SUBSET_SPS:
      gst_h264_sps_clear (&pi->data.sps);
      break;
    case GST_H264_NAL_PPS:
      gst_h264_pps_clear (&pi->data.pps);
      break;
    case GST_H264_NAL_SEI:
      if (pi->data.sei) {
        g_array_unref (pi->data.sei);
        pi->data.sei = NULL;
      }
      break;
  }
}

static inline const GstVaapiMiniObjectClass *
gst_vaapi_parser_info_h264_class (void)
{
  static const GstVaapiMiniObjectClass GstVaapiParserInfoH264Class = {
    .size = sizeof (GstVaapiParserInfoH264),
    .finalize = (GDestroyNotify) gst_vaapi_parser_info_h264_finalize
  };
  return &GstVaapiParserInfoH264Class;
}

static inline GstVaapiParserInfoH264 *
gst_vaapi_parser_info_h264_new (void)
{
  return (GstVaapiParserInfoH264 *)
      gst_vaapi_mini_object_new (gst_vaapi_parser_info_h264_class ());
}

#define gst_vaapi_parser_info_h264_ref(pi) \
    gst_vaapi_mini_object_ref(GST_VAAPI_MINI_OBJECT(pi))

#define gst_vaapi_parser_info_h264_unref(pi) \
    gst_vaapi_mini_object_unref(GST_VAAPI_MINI_OBJECT(pi))

#define gst_vaapi_parser_info_h264_replace(old_pi_ptr, new_pi)          \
    gst_vaapi_mini_object_replace((GstVaapiMiniObject **)(old_pi_ptr),  \
        (GstVaapiMiniObject *)(new_pi))

/* ------------------------------------------------------------------------- */
/* --- H.264 Pictures                                                    --- */
/* ------------------------------------------------------------------------- */

/*
 * Extended picture flags:
 *
 * @GST_VAAPI_PICTURE_FLAG_IDR: flag that specifies an IDR picture
 * @GST_VAAPI_PICTURE_FLAG_INTER_VIEW: flag that indicates the picture
 *   may be used for inter-view prediction
 * @GST_VAAPI_PICTURE_FLAG_ANCHOR: flag that specifies an anchor picture,
 *   i.e. a picture that is decoded with only inter-view prediction,
 *   and not inter prediction
 * @GST_VAAPI_PICTURE_FLAG_AU_START: flag that marks the start of an
 *   access unit (AU)
 * @GST_VAAPI_PICTURE_FLAG_AU_END: flag that marks the end of an
 *   access unit (AU)
 * @GST_VAAPI_PICTURE_FLAG_GHOST: flag that specifies a "non-existing"
 *   picture, without any viable GstVideoCodecFrame associated to it.
 *   i.e. a dummy picture with some valid contents
 * @GST_VAAPI_PICTURE_FLAG_SHORT_TERM_REFERENCE: flag that specifies
 *     "used for short-term reference"
 * @GST_VAAPI_PICTURE_FLAG_LONG_TERM_REFERENCE: flag that specifies
 *     "used for long-term reference"
 * @GST_VAAPI_PICTURE_FLAGS_REFERENCE: mask covering any kind of
 *     reference picture (short-term reference or long-term reference)
 */
enum
{
  GST_VAAPI_PICTURE_FLAG_IDR = (GST_VAAPI_PICTURE_FLAG_LAST << 0),
  GST_VAAPI_PICTURE_FLAG_REFERENCE2 = (GST_VAAPI_PICTURE_FLAG_LAST << 1),
  GST_VAAPI_PICTURE_FLAG_INTER_VIEW = (GST_VAAPI_PICTURE_FLAG_LAST << 2),
  GST_VAAPI_PICTURE_FLAG_ANCHOR = (GST_VAAPI_PICTURE_FLAG_LAST << 3),
  GST_VAAPI_PICTURE_FLAG_AU_START = (GST_VAAPI_PICTURE_FLAG_LAST << 4),
  GST_VAAPI_PICTURE_FLAG_AU_END = (GST_VAAPI_PICTURE_FLAG_LAST << 5),
  GST_VAAPI_PICTURE_FLAG_GHOST = (GST_VAAPI_PICTURE_FLAG_LAST << 6),

  GST_VAAPI_PICTURE_FLAG_SHORT_TERM_REFERENCE =
      (GST_VAAPI_PICTURE_FLAG_REFERENCE),
  GST_VAAPI_PICTURE_FLAG_LONG_TERM_REFERENCE =
      (GST_VAAPI_PICTURE_FLAG_REFERENCE | GST_VAAPI_PICTURE_FLAG_REFERENCE2),
  GST_VAAPI_PICTURE_FLAGS_REFERENCE =
      (GST_VAAPI_PICTURE_FLAG_SHORT_TERM_REFERENCE |
      GST_VAAPI_PICTURE_FLAG_LONG_TERM_REFERENCE),
};

#define GST_VAAPI_PICTURE_IS_IDR(picture) \
    (GST_VAAPI_PICTURE_FLAG_IS_SET(picture, GST_VAAPI_PICTURE_FLAG_IDR))

#define GST_VAAPI_PICTURE_IS_SHORT_TERM_REFERENCE(picture)      \
    ((GST_VAAPI_PICTURE_FLAGS(picture) &                        \
      GST_VAAPI_PICTURE_FLAGS_REFERENCE) ==                     \
     GST_VAAPI_PICTURE_FLAG_SHORT_TERM_REFERENCE)

#define GST_VAAPI_PICTURE_IS_LONG_TERM_REFERENCE(picture)       \
    ((GST_VAAPI_PICTURE_FLAGS(picture) &                        \
      GST_VAAPI_PICTURE_FLAGS_REFERENCE) ==                     \
     GST_VAAPI_PICTURE_FLAG_LONG_TERM_REFERENCE)

#define GST_VAAPI_PICTURE_IS_INTER_VIEW(picture) \
    (GST_VAAPI_PICTURE_FLAG_IS_SET(picture, GST_VAAPI_PICTURE_FLAG_INTER_VIEW))

#define GST_VAAPI_PICTURE_IS_ANCHOR(picture) \
    (GST_VAAPI_PICTURE_FLAG_IS_SET(picture, GST_VAAPI_PICTURE_FLAG_ANCHOR))

#define GST_VAAPI_PICTURE_H264(picture) \
    ((GstVaapiPictureH264 *)(picture))

struct _GstVaapiPictureH264
{
  GstVaapiPicture base;
  GstH264SliceHdr *last_slice_hdr;
  guint structure;
  gint32 field_poc[2];
  gint32 frame_num;             // Original frame_num from slice_header()
  gint32 frame_num_wrap;        // Temporary for ref pic marking: FrameNumWrap
  gint32 long_term_frame_idx;   // Temporary for ref pic marking: LongTermFrameIdx
  gint32 pic_num;               // Temporary for ref pic marking: PicNum
  gint32 long_term_pic_num;     // Temporary for ref pic marking: LongTermPicNum
  GstVaapiPictureH264 *other_field;     // Temporary for ref pic marking: other field in the same frame store
  guint output_flag:1;
  guint output_needed:1;
};

GST_VAAPI_CODEC_DEFINE_TYPE (GstVaapiPictureH264, gst_vaapi_picture_h264);

void
gst_vaapi_picture_h264_destroy (GstVaapiPictureH264 * picture)
{
  gst_vaapi_picture_destroy (GST_VAAPI_PICTURE (picture));
}

gboolean
gst_vaapi_picture_h264_create (GstVaapiPictureH264 * picture,
    const GstVaapiCodecObjectConstructorArgs * args)
{
  if (!gst_vaapi_picture_create (GST_VAAPI_PICTURE (picture), args))
    return FALSE;

  picture->structure = picture->base.structure;
  picture->field_poc[0] = G_MAXINT32;
  picture->field_poc[1] = G_MAXINT32;
  picture->output_needed = FALSE;
  return TRUE;
}

static inline GstVaapiPictureH264 *
gst_vaapi_picture_h264_new (GstVaapiDecoderH264 * decoder)
{
  return (GstVaapiPictureH264 *)
      gst_vaapi_codec_object_new (&GstVaapiPictureH264Class,
      GST_VAAPI_CODEC_BASE (decoder), NULL,
      sizeof (VAPictureParameterBufferH264), NULL, 0, 0);
}

static inline void
gst_vaapi_picture_h264_set_reference (GstVaapiPictureH264 * picture,
    guint reference_flags, gboolean other_field)
{
  if (!picture)
    return;
  GST_VAAPI_PICTURE_FLAG_UNSET (picture, GST_VAAPI_PICTURE_FLAGS_REFERENCE);
  GST_VAAPI_PICTURE_FLAG_SET (picture, reference_flags);

  if (!other_field || !(picture = picture->other_field))
    return;
  GST_VAAPI_PICTURE_FLAG_UNSET (picture, GST_VAAPI_PICTURE_FLAGS_REFERENCE);
  GST_VAAPI_PICTURE_FLAG_SET (picture, reference_flags);
}

static inline GstVaapiPictureH264 *
gst_vaapi_picture_h264_new_field (GstVaapiPictureH264 * picture)
{
  g_return_val_if_fail (picture, NULL);

  return (GstVaapiPictureH264 *) gst_vaapi_picture_new_field (&picture->base);
}

static inline GstVaapiPictureH264 *
gst_vaapi_picture_h264_new_clone (GstVaapiPictureH264 * picture)
{
  g_return_val_if_fail (picture, NULL);

  return (GstVaapiPictureH264 *) gst_vaapi_picture_new_clone (&picture->base);
}

/* ------------------------------------------------------------------------- */
/* --- Frame Buffers (DPB)                                               --- */
/* ------------------------------------------------------------------------- */

struct _GstVaapiFrameStore
{
  /*< private > */
  GstVaapiMiniObject parent_instance;

  guint view_id;
  guint structure;
  GstVaapiPictureH264 *buffers[2];
  guint num_buffers;
  guint output_needed;
  guint output_called;
};

static void
gst_vaapi_frame_store_finalize (gpointer object)
{
  GstVaapiFrameStore *const fs = object;
  guint i;

  for (i = 0; i < fs->num_buffers; i++)
    gst_vaapi_picture_replace (&fs->buffers[i], NULL);
}

static GstVaapiFrameStore *
gst_vaapi_frame_store_new (GstVaapiPictureH264 * picture)
{
  GstVaapiFrameStore *fs;

  static const GstVaapiMiniObjectClass GstVaapiFrameStoreClass = {
    sizeof (GstVaapiFrameStore),
    gst_vaapi_frame_store_finalize
  };

  fs = (GstVaapiFrameStore *)
      gst_vaapi_mini_object_new (&GstVaapiFrameStoreClass);
  if (!fs)
    return NULL;

  fs->view_id = picture->base.view_id;
  fs->structure = picture->structure;
  fs->buffers[0] = gst_vaapi_picture_ref (picture);
  fs->buffers[1] = NULL;
  fs->num_buffers = 1;
  fs->output_needed = 0;
  fs->output_called = 0;

  if (picture->output_flag) {
    picture->output_needed = TRUE;
    fs->output_needed++;
  }
  return fs;
}

static gboolean
gst_vaapi_frame_store_add (GstVaapiFrameStore * fs,
    GstVaapiPictureH264 * picture)
{
  guint field;

  g_return_val_if_fail (fs->num_buffers == 1, FALSE);
  g_return_val_if_fail (!GST_VAAPI_PICTURE_IS_FRAME (picture), FALSE);
  g_return_val_if_fail (!GST_VAAPI_PICTURE_IS_FIRST_FIELD (picture), FALSE);

  gst_vaapi_picture_replace (&fs->buffers[fs->num_buffers++], picture);
  if (picture->output_flag) {
    picture->output_needed = TRUE;
    fs->output_needed++;
  }

  fs->structure = GST_VAAPI_PICTURE_STRUCTURE_FRAME;

  field = picture->structure == GST_VAAPI_PICTURE_STRUCTURE_TOP_FIELD ?
      TOP_FIELD : BOTTOM_FIELD;

  if (fs->buffers[0]->field_poc[field] != G_MAXINT32)
    return FALSE;
  fs->buffers[0]->field_poc[field] = picture->field_poc[field];

  if (picture->field_poc[!field] != G_MAXINT32)
    return FALSE;
  picture->field_poc[!field] = fs->buffers[0]->field_poc[!field];

  return TRUE;
}

static gboolean
gst_vaapi_frame_store_split_fields (GstVaapiFrameStore * fs, gboolean tff)
{
  GstVaapiPictureH264 *const first_field = fs->buffers[0];
  GstVaapiPictureH264 *second_field;

  g_return_val_if_fail (fs->num_buffers == 1, FALSE);

  first_field->base.structure = tff ?
      GST_VAAPI_PICTURE_STRUCTURE_TOP_FIELD :
      GST_VAAPI_PICTURE_STRUCTURE_BOTTOM_FIELD;
  GST_VAAPI_PICTURE_FLAG_SET (first_field, GST_VAAPI_PICTURE_FLAG_INTERLACED);

  second_field = gst_vaapi_picture_h264_new_field (first_field);
  if (!second_field)
    return FALSE;
  gst_vaapi_picture_h264_set_reference (second_field,
      GST_VAAPI_PICTURE_FLAGS (first_field) & GST_VAAPI_PICTURE_FLAGS_REFERENCE,
      FALSE);
  gst_vaapi_picture_replace (&fs->buffers[fs->num_buffers++], second_field);
  gst_vaapi_picture_unref (second_field);

  second_field->frame_num = first_field->frame_num;
  second_field->field_poc[0] = first_field->field_poc[0];
  second_field->field_poc[1] = first_field->field_poc[1];
  second_field->output_flag = first_field->output_flag;
  if (second_field->output_flag) {
    second_field->output_needed = TRUE;
    fs->output_needed++;
  }
  return TRUE;
}

static inline gboolean
gst_vaapi_frame_store_has_frame (GstVaapiFrameStore * fs)
{
  return fs->structure == GST_VAAPI_PICTURE_STRUCTURE_FRAME;
}

static inline gboolean
gst_vaapi_frame_store_is_complete (GstVaapiFrameStore * fs)
{
  return gst_vaapi_frame_store_has_frame (fs) ||
      GST_VAAPI_PICTURE_IS_ONEFIELD (fs->buffers[0]);
}

static inline gboolean
gst_vaapi_frame_store_has_reference (GstVaapiFrameStore * fs)
{
  guint i;

  for (i = 0; i < fs->num_buffers; i++) {
    if (GST_VAAPI_PICTURE_IS_REFERENCE (fs->buffers[i]))
      return TRUE;
  }
  return FALSE;
}

static gboolean
gst_vaapi_frame_store_has_inter_view (GstVaapiFrameStore * fs)
{
  guint i;

  for (i = 0; i < fs->num_buffers; i++) {
    if (GST_VAAPI_PICTURE_IS_INTER_VIEW (fs->buffers[i]))
      return TRUE;
  }
  return FALSE;
}

#define gst_vaapi_frame_store_ref(fs) \
    gst_vaapi_mini_object_ref(GST_VAAPI_MINI_OBJECT(fs))

#define gst_vaapi_frame_store_unref(fs) \
    gst_vaapi_mini_object_unref(GST_VAAPI_MINI_OBJECT(fs))

#define gst_vaapi_frame_store_replace(old_fs_p, new_fs)                 \
    gst_vaapi_mini_object_replace((GstVaapiMiniObject **)(old_fs_p),    \
        (GstVaapiMiniObject *)(new_fs))

/* ------------------------------------------------------------------------- */
/* --- H.264 3D Info                                                     --- */
/* ------------------------------------------------------------------------- */
/**
 * GstVaapiStereo3DInfo:
 * @mode: the #GstVideoMultiviewMode.
 * @flags: the #GstVideoMultiviewFlags.
 * @id: the id number.
 * @repetition_period: 0 means once, 1 means always, >1 compare with poc.
 */
struct _GstVaapiStereo3DInfo
{
  GstVideoMultiviewMode mode;
  GstVideoMultiviewFlags flags;
  guint id;
  guint repetition_period;
};

/* ------------------------------------------------------------------------- */
/* --- H.264 Decoder                                                     --- */
/* ------------------------------------------------------------------------- */

#define GST_VAAPI_DECODER_H264_CAST(decoder) \
    ((GstVaapiDecoderH264 *)(decoder))

typedef enum
{
  GST_H264_VIDEO_STATE_GOT_SPS = 1 << 0,
  GST_H264_VIDEO_STATE_GOT_PPS = 1 << 1,
  GST_H264_VIDEO_STATE_GOT_SLICE = 1 << 2,
  GST_H264_VIDEO_STATE_GOT_I_FRAME = 1 << 3,    // persistent across SPS
  GST_H264_VIDEO_STATE_GOT_P_SLICE = 1 << 4,    // predictive (all non-intra)

  GST_H264_VIDEO_STATE_VALID_PICTURE_HEADERS = (GST_H264_VIDEO_STATE_GOT_SPS |
      GST_H264_VIDEO_STATE_GOT_PPS),
  GST_H264_VIDEO_STATE_VALID_PICTURE =
      (GST_H264_VIDEO_STATE_VALID_PICTURE_HEADERS |
      GST_H264_VIDEO_STATE_GOT_SLICE)
} GstH264VideoState;

struct _GstVaapiDecoderH264Private
{
  GstH264NalParser *parser;
  guint parser_state;
  guint decoder_state;
  GstVaapiStreamAlignH264 stream_alignment;
  GstVaapiPictureH264 *current_picture;
  GstVaapiParserInfoH264 *sps[GST_H264_MAX_SPS_COUNT];
  GstVaapiParserInfoH264 *active_sps;
  GstVaapiParserInfoH264 *pps[GST_H264_MAX_PPS_COUNT];
  GstVaapiParserInfoH264 *active_pps;
  GstVaapiParserInfoH264 *prev_pi;
  GstVaapiParserInfoH264 *prev_slice_pi;
  GstVaapiFrameStore **prev_ref_frames;
  GstVaapiFrameStore **prev_frames;
  guint prev_frames_alloc;
  GstVaapiFrameStore **dpb;
  guint dpb_count;
  guint dpb_size;
  guint dpb_size_max;
  guint max_views;
  GstVaapiProfile profile;
  GstVaapiEntrypoint entrypoint;
  GstVaapiChromaType chroma_type;
  GPtrArray *inter_views;
  GstVaapiPictureH264 *short_ref[32];
  guint short_ref_count;
  GstVaapiPictureH264 *long_ref[32];
  guint long_ref_count;
  GstVaapiPictureH264 *RefPicList0[32];
  guint RefPicList0_count;
  GstVaapiPictureH264 *RefPicList1[32];
  guint RefPicList1_count;
  guint nal_length_size;
  guint mb_width;
  guint mb_height;
  guint pic_structure;          // pic_struct (from SEI pic_timing() or inferred)
  gint32 field_poc[2];          // 0:TopFieldOrderCnt / 1:BottomFieldOrderCnt
  gint32 poc_msb;               // PicOrderCntMsb
  gint32 poc_lsb;               // pic_order_cnt_lsb (from slice_header())
  gint32 prev_poc_msb;          // prevPicOrderCntMsb
  gint32 prev_poc_lsb;          // prevPicOrderCntLsb
  gint32 frame_num_offset;      // FrameNumOffset
  gint32 frame_num;             // frame_num (from slice_header())
  gint32 prev_frame_num;        // prevFrameNum
  gint32 prev_ref_frame_num;    // prevRefFrameNum
  gboolean prev_pic_has_mmco5;  // prevMmco5Pic
  gboolean prev_pic_reference;  // previous picture is a reference
  guint prev_pic_structure;     // previous picture structure
  guint is_opened:1;
  guint is_avcC:1;
  guint has_context:1;
  guint progressive_sequence:1;
  guint top_field_first:1;

  gboolean force_low_latency;
  gboolean base_only;

  GstVaapiStereo3DInfo stereo_info;
};

/**
 * GstVaapiDecoderH264:
 *
 * A decoder based on H264.
 */
struct _GstVaapiDecoderH264
{
  /*< private > */
  GstVaapiDecoder parent_instance;
  GstVaapiDecoderH264Private priv;
};

/**
 * GstVaapiDecoderH264Class:
 *
 * A decoder class based on H264.
 */
struct _GstVaapiDecoderH264Class
{
  /*< private > */
  GstVaapiDecoderClass parent_class;
};

G_DEFINE_TYPE (GstVaapiDecoderH264, gst_vaapi_decoder_h264,
    GST_TYPE_VAAPI_DECODER);

static gboolean
exec_ref_pic_marking (GstVaapiDecoderH264 * decoder,
    GstVaapiPictureH264 * picture);

static gboolean
exec_ref_pic_marking_sliding_window (GstVaapiDecoderH264 * decoder);

static gboolean
is_inter_view_reference_for_next_pictures (GstVaapiDecoderH264 * decoder,
    GstVaapiPictureH264 * picture);

static inline gboolean
is_inter_view_reference_for_next_frames (GstVaapiDecoderH264 * decoder,
    GstVaapiFrameStore * fs)
{
  return is_inter_view_reference_for_next_pictures (decoder, fs->buffers[0]);
}

/* Determines if the supplied profile is one of the MVC set */
static gboolean
is_mvc_profile (GstH264Profile profile)
{
  return profile == GST_H264_PROFILE_MULTIVIEW_HIGH ||
      profile == GST_H264_PROFILE_STEREO_HIGH;
}

/* Determines the view_id from the supplied NAL unit */
static inline guint
get_view_id (GstH264NalUnit * nalu)
{
  return GST_H264_IS_MVC_NALU (nalu) ? nalu->extension.mvc.view_id : 0;
}

/* Determines the view order index (VOIdx) from the supplied view_id */
static gint
get_view_order_index (GstH264SPS * sps, guint16 view_id)
{
  GstH264SPSExtMVC *mvc;
  gint i;

  if (!sps || sps->extension_type != GST_H264_NAL_EXTENSION_MVC)
    return 0;

  mvc = &sps->extension.mvc;
  for (i = 0; i <= mvc->num_views_minus1; i++) {
    if (mvc->view[i].view_id == view_id)
      return i;
  }
  GST_ERROR ("failed to find VOIdx from view_id (%d)", view_id);
  return -1;
}

/* Determines NumViews */
static guint
get_num_views (GstH264SPS * sps)
{
  return 1 + (sps->extension_type == GST_H264_NAL_EXTENSION_MVC ?
      sps->extension.mvc.num_views_minus1 : 0);
}

/* Get number of reference frames to use */
static guint
get_max_dec_frame_buffering (GstH264SPS * sps)
{
  guint num_views, max_dpb_frames;
  guint max_dec_frame_buffering, PicSizeMbs;
  GstVaapiLevelH264 level;
  const GstVaapiH264LevelLimits *level_limits;

  /* Table A-1 - Level limits */
  if (G_UNLIKELY (sps->level_idc == 11 && sps->constraint_set3_flag))
    level = GST_VAAPI_LEVEL_H264_L1b;
  else
    level = gst_vaapi_utils_h264_get_level (sps->level_idc);
  level_limits = gst_vaapi_utils_h264_get_level_limits (level);
  if (G_UNLIKELY (!level_limits)) {
    GST_FIXME ("unsupported level_idc value (%d)", sps->level_idc);
    max_dec_frame_buffering = 16;
  } else {
    PicSizeMbs = ((sps->pic_width_in_mbs_minus1 + 1) *
        (sps->pic_height_in_map_units_minus1 + 1) *
        (sps->frame_mbs_only_flag ? 1 : 2));
    max_dec_frame_buffering = level_limits->MaxDpbMbs / PicSizeMbs;
  }
  if (is_mvc_profile (sps->profile_idc))
    max_dec_frame_buffering <<= 1;

  /* VUI parameters */
  if (sps->vui_parameters_present_flag) {
    GstH264VUIParams *const vui_params = &sps->vui_parameters;
    if (vui_params->bitstream_restriction_flag)
      max_dec_frame_buffering = vui_params->max_dec_frame_buffering;
    else {
      switch (sps->profile_idc) {
        case 44:               // CAVLC 4:4:4 Intra profile
        case GST_H264_PROFILE_SCALABLE_HIGH:
        case GST_H264_PROFILE_HIGH:
        case GST_H264_PROFILE_HIGH10:
        case GST_H264_PROFILE_HIGH_422:
        case GST_H264_PROFILE_HIGH_444:
          if (sps->constraint_set3_flag)
            max_dec_frame_buffering = 0;
          break;
      }
    }
  }

  num_views = get_num_views (sps);
  max_dpb_frames = 16 * (num_views > 1 ? g_bit_storage (num_views - 1) : 1);
  if (max_dec_frame_buffering > max_dpb_frames)
    max_dec_frame_buffering = max_dpb_frames;
  else if (max_dec_frame_buffering < sps->num_ref_frames)
    max_dec_frame_buffering = sps->num_ref_frames;
  return MAX (1, max_dec_frame_buffering);
}

static void
array_remove_index_fast (void *array, guint * array_length_ptr, guint index)
{
  gpointer *const entries = array;
  guint num_entries = *array_length_ptr;

  g_return_if_fail (index < num_entries);

  if (index != --num_entries)
    entries[index] = entries[num_entries];
  entries[num_entries] = NULL;
  *array_length_ptr = num_entries;
}

#if 1
static inline void
array_remove_index (void *array, guint * array_length_ptr, guint index)
{
  array_remove_index_fast (array, array_length_ptr, index);
}
#else
static void
array_remove_index (void *array, guint * array_length_ptr, guint index)
{
  gpointer *const entries = array;
  const guint num_entries = *array_length_ptr - 1;
  guint i;

  g_return_if_fail (index <= num_entries);

  for (i = index; i < num_entries; i++)
    entries[i] = entries[i + 1];
  entries[num_entries] = NULL;
  *array_length_ptr = num_entries;
}
#endif

#define ARRAY_REMOVE_INDEX(array, index) \
    array_remove_index(array, &array##_count, index)

static void
dpb_remove_index (GstVaapiDecoderH264 * decoder, guint index)
{
  GstVaapiDecoderH264Private *const priv = &decoder->priv;
  guint i, num_frames = --priv->dpb_count;

  if (USE_STRICT_DPB_ORDERING) {
    for (i = index; i < num_frames; i++)
      gst_vaapi_frame_store_replace (&priv->dpb[i], priv->dpb[i + 1]);
  } else if (index != num_frames)
    gst_vaapi_frame_store_replace (&priv->dpb[index], priv->dpb[num_frames]);
  gst_vaapi_frame_store_replace (&priv->dpb[num_frames], NULL);
}

static gboolean
dpb_output (GstVaapiDecoderH264 * decoder, GstVaapiFrameStore * fs)
{
  GstVaapiPictureH264 *picture = NULL;
  guint i;

  g_return_val_if_fail (fs != NULL, FALSE);

  fs->output_called++;
  if (!gst_vaapi_frame_store_is_complete (fs))
    return TRUE;

  for (i = 0; i < fs->num_buffers; i++) {
    GstVaapiPictureH264 *const pic = fs->buffers[i];
    if (pic == NULL)
      return FALSE;
    pic->output_needed = FALSE;
    if (!GST_VAAPI_PICTURE_FLAG_IS_SET (pic, GST_VAAPI_PICTURE_FLAG_GHOST))
      picture = pic;
  }

  fs->output_needed = 0;
  fs->output_called = 0;
  if (!picture)
    return TRUE;
  return gst_vaapi_picture_output (GST_VAAPI_PICTURE_CAST (picture));
}

static inline void
dpb_evict (GstVaapiDecoderH264 * decoder, GstVaapiPictureH264 * picture,
    guint i)
{
  GstVaapiDecoderH264Private *const priv = &decoder->priv;
  GstVaapiFrameStore *const fs = priv->dpb[i];

  if (!fs->output_needed && !gst_vaapi_frame_store_has_reference (fs))
    dpb_remove_index (decoder, i);
}

/* Finds the picture with the nearest previous POC and same structure */
static gint
dpb_find_nearest_prev_poc (GstVaapiDecoderH264 * decoder,
    GstVaapiPictureH264 * picture, guint picture_structure,
    GstVaapiPictureH264 ** found_picture_ptr)
{
  GstVaapiDecoderH264Private *const priv = &decoder->priv;
  GstVaapiPictureH264 *found_picture = NULL;
  guint i, j, found_index = -1;

  g_return_val_if_fail (picture != NULL, -1);

  if (!picture_structure)
    picture_structure = picture->base.structure;

  for (i = 0; i < priv->dpb_count; i++) {
    GstVaapiFrameStore *const fs = priv->dpb[i];
    if (picture->base.view_id != fs->view_id)
      continue;
    for (j = 0; j < fs->num_buffers; j++) {
      GstVaapiPictureH264 *const pic = fs->buffers[j];
      if (pic->base.structure != picture_structure)
        continue;
      if (pic->base.poc >= picture->base.poc)
        continue;
      if (!found_picture || found_picture->base.poc < pic->base.poc)
        found_picture = pic, found_index = i;
    }
  }

  if (found_picture_ptr)
    *found_picture_ptr = found_picture;
  return found_index;
}

/* Finds the picture with the lowest POC that needs to be output */
static gint
dpb_find_lowest_poc_for_output (GstVaapiDecoderH264 * decoder,
    GstVaapiPictureH264 * picture, GstVaapiPictureH264 ** found_picture_ptr,
    gboolean * can_be_output)
{
  GstVaapiDecoderH264Private *const priv = &decoder->priv;
  GstVaapiPictureH264 *found_picture = NULL;
  guint i, j, found_index = -1, found_poc = -1;
  gboolean is_first = TRUE;
  gint last_output_poc = -1;

  for (i = 0; i < priv->dpb_count; i++) {
    GstVaapiFrameStore *const fs = priv->dpb[i];
    if (!fs->output_needed) {
      /* find the maximum poc of any previously output frames that are
       * still held in the DPB. */
      if (can_be_output != NULL) {
        for (j = 0; j < fs->num_buffers; j++) {
          if (is_first || fs->buffers[j]->base.poc > last_output_poc) {
            is_first = FALSE;
            last_output_poc = fs->buffers[j]->base.poc;
          }
        }
      }
      continue;
    }
    if (picture && picture->base.view_id != fs->view_id)
      continue;
    for (j = 0; j < fs->num_buffers; j++) {
      GstVaapiPictureH264 *const pic = fs->buffers[j];
      if (!pic->output_needed)
        continue;
      if (!found_picture || found_picture->base.poc > pic->base.poc ||
          (found_picture->base.poc == pic->base.poc &&
              found_picture->base.voc > pic->base.voc))
        found_picture = pic, found_index = i, found_poc = pic->base.poc;
    }
  }

  if (can_be_output != NULL) {
    /* found_picture can be output if it's the first frame in the DPB,
     * or if there's no gap between it and the most recently output
     * frame. */
    *can_be_output = FALSE;
    if (found_picture &&
        gst_vaapi_frame_store_is_complete (priv->dpb[found_index])) {
      if (is_first) {
        *can_be_output = TRUE;
      } else if (((int) (found_poc)) > ((int) (last_output_poc))) {
        *can_be_output = (found_poc - last_output_poc) <= 2;
      } else {
        /* A frame with a higher poc has already been sent.  No choice
         * now but to drop this frame */
        GST_WARNING ("dropping out-of-sequence frame");
        priv->dpb[found_index]->output_needed = FALSE;
      }
    }
  }

  if (found_picture_ptr)
    *found_picture_ptr = found_picture;
  return found_index;
}

/* Finds the picture with the lowest POC that needs to be output */
static gint
dpb_find_lowest_poc (GstVaapiDecoderH264 * decoder,
    GstVaapiPictureH264 * picture, GstVaapiPictureH264 ** found_picture_ptr)
{
  return dpb_find_lowest_poc_for_output (decoder, picture, found_picture_ptr,
      NULL);
}


/* Finds the picture with the lowest VOC that needs to be output */
static gint
dpb_find_lowest_voc (GstVaapiDecoderH264 * decoder,
    GstVaapiPictureH264 * picture, GstVaapiPictureH264 ** found_picture_ptr)
{
  GstVaapiDecoderH264Private *const priv = &decoder->priv;
  GstVaapiPictureH264 *found_picture = NULL;
  guint i, j, found_index = -1;

  for (i = 0; i < priv->dpb_count; i++) {
    GstVaapiFrameStore *const fs = priv->dpb[i];
    if (!fs->output_needed || fs->view_id == picture->base.view_id)
      continue;
    for (j = 0; j < fs->num_buffers; j++) {
      GstVaapiPictureH264 *const pic = fs->buffers[j];
      if (!pic->output_needed || pic->base.poc != picture->base.poc)
        continue;
      if (!found_picture || found_picture->base.voc > pic->base.voc)
        found_picture = pic, found_index = i;
    }
  }

  if (found_picture_ptr)
    *found_picture_ptr = found_picture;
  return found_index;
}

static gboolean
dpb_output_other_views (GstVaapiDecoderH264 * decoder,
    GstVaapiPictureH264 * picture, guint voc)
{
  GstVaapiDecoderH264Private *const priv = &decoder->priv;
  GstVaapiPictureH264 *found_picture;
  gint found_index;
  gboolean success;

  if (priv->max_views == 1)
    return TRUE;

  /* Emit all other view components that were in the same access
     unit than the picture we have just found */
  found_picture = picture;
  for (;;) {
    found_index = dpb_find_lowest_voc (decoder, found_picture, &found_picture);
    if (found_index < 0 || found_picture->base.voc >= voc)
      break;
    success = dpb_output (decoder, priv->dpb[found_index]);
    dpb_evict (decoder, found_picture, found_index);
    if (!success)
      return FALSE;
  }
  return TRUE;
}

static gboolean
dpb_bump (GstVaapiDecoderH264 * decoder, GstVaapiPictureH264 * picture)
{
  GstVaapiDecoderH264Private *const priv = &decoder->priv;
  GstVaapiPictureH264 *found_picture;
  gint found_index;
  gboolean success;

  found_index = dpb_find_lowest_poc (decoder, picture, &found_picture);
  if (found_index < 0)
    return FALSE;

  gst_vaapi_picture_ref (found_picture);

  if (picture && picture->base.poc != found_picture->base.poc)
    dpb_output_other_views (decoder, found_picture, found_picture->base.voc);

  success = dpb_output (decoder, priv->dpb[found_index]);

  dpb_evict (decoder, found_picture, found_index);
  if (priv->max_views == 1)
    goto done;

  if (picture && picture->base.poc != found_picture->base.poc)
    dpb_output_other_views (decoder, found_picture, G_MAXUINT32);

done:
  gst_vaapi_picture_unref (found_picture);
  return success;
}

static void
dpb_output_ready_frames (GstVaapiDecoderH264 * decoder)
{
  GstVaapiDecoderH264Private *const priv = &decoder->priv;
  gboolean can_output = FALSE;
  gint found_index;

  while (TRUE) {
    found_index = dpb_find_lowest_poc_for_output (decoder,
        priv->current_picture, NULL, &can_output);
    if (found_index < 0 || !can_output)
      break;
    dpb_output (decoder, priv->dpb[found_index]);
  }
}

static void
dpb_clear (GstVaapiDecoderH264 * decoder, GstVaapiPictureH264 * picture)
{
  GstVaapiDecoderH264Private *const priv = &decoder->priv;
  guint i, n;

  for (i = 0; i < priv->dpb_count; i++) {
    if (picture && picture->base.view_id != priv->dpb[i]->view_id)
      continue;
    gst_vaapi_frame_store_replace (&priv->dpb[i], NULL);
  }

  /* Compact the resulting DPB, i.e. remove holes */
  for (i = 0, n = 0; i < priv->dpb_count; i++) {
    if (priv->dpb[i]) {
      if (i != n) {
        priv->dpb[n] = priv->dpb[i];
        priv->dpb[i] = NULL;
      }
      n++;
    }
  }
  priv->dpb_count = n;

  /* Clear previous frame buffers only if this is a "flush-all" operation,
     or if the picture is the first one in the access unit */
  if (priv->prev_frames && (!picture ||
          GST_VAAPI_PICTURE_FLAG_IS_SET (picture,
              GST_VAAPI_PICTURE_FLAG_AU_START))) {
    for (i = 0; i < priv->max_views; i++)
      gst_vaapi_frame_store_replace (&priv->prev_frames[i], NULL);
  }

  /* Clear previous reference frame buffers only if this is a "flush-all"
     operation, or if the picture is part of an IDR NAL */
  if (priv->prev_ref_frames && (!picture ||
          GST_VAAPI_PICTURE_FLAG_IS_SET (picture,
              GST_VAAPI_PICTURE_FLAG_IDR))) {
    for (i = 0; i < priv->max_views; i++)
      gst_vaapi_frame_store_replace (&priv->prev_ref_frames[i], NULL);
  }
}

static void
dpb_flush (GstVaapiDecoderH264 * decoder, GstVaapiPictureH264 * picture)
{
  GstVaapiDecoderH264Private *const priv = &decoder->priv;
  guint i;

  /* Detect broken frames and mark them as having a single field if
     needed */
  for (i = 0; i < priv->dpb_count; i++) {
    GstVaapiFrameStore *const fs = priv->dpb[i];
    if (!fs->output_needed || gst_vaapi_frame_store_is_complete (fs))
      continue;
    GST_VAAPI_PICTURE_FLAG_SET (fs->buffers[0],
        GST_VAAPI_PICTURE_FLAG_ONEFIELD);
  }

  /* Output any frame remaining in DPB */
  while (dpb_bump (decoder, picture));
  dpb_clear (decoder, picture);
}

static void
dpb_prune_mvc (GstVaapiDecoderH264 * decoder, GstVaapiPictureH264 * picture)
{
  GstVaapiDecoderH264Private *const priv = &decoder->priv;
  const gboolean is_last_picture =      /* in the access unit */
      GST_VAAPI_PICTURE_FLAG_IS_SET (picture, GST_VAAPI_PICTURE_FLAG_AU_END);
  guint i;

  // Remove all unused inter-view only reference components of the current AU
  i = 0;
  while (i < priv->dpb_count) {
    GstVaapiFrameStore *const fs = priv->dpb[i];
    if (fs->view_id != picture->base.view_id &&
        !fs->output_needed && !gst_vaapi_frame_store_has_reference (fs) &&
        (is_last_picture ||
            !is_inter_view_reference_for_next_frames (decoder, fs)))
      dpb_remove_index (decoder, i);
    else
      i++;
  }
}

static gboolean
dpb_add (GstVaapiDecoderH264 * decoder, GstVaapiPictureH264 * picture)
{
  GstVaapiDecoderH264Private *const priv = &decoder->priv;
  GstVaapiFrameStore *fs;
  guint i;

  if (priv->max_views > 1)
    dpb_prune_mvc (decoder, picture);

  // Remove all unused pictures
  if (!GST_VAAPI_PICTURE_IS_IDR (picture)) {
    i = 0;
    while (i < priv->dpb_count) {
      GstVaapiFrameStore *const fs = priv->dpb[i];
      if (fs->view_id == picture->base.view_id &&
          !fs->output_needed && !gst_vaapi_frame_store_has_reference (fs))
        dpb_remove_index (decoder, i);
      else
        i++;
    }
  }
  // Check if picture is the second field and the first field is still in DPB
  if (GST_VAAPI_PICTURE_IS_INTERLACED (picture) &&
      !GST_VAAPI_PICTURE_IS_FIRST_FIELD (picture)) {
    fs = priv->prev_frames[picture->base.voc];
    if (!fs || &fs->buffers[0]->base != picture->base.parent_picture)
      return FALSE;
    if (!gst_vaapi_frame_store_add (fs, picture))
      return FALSE;

    if (fs->output_called)
      return dpb_output (decoder, fs);
    return TRUE;
  }
  // Try to output the previous frame again if it was not submitted yet
  // e.g. delayed while waiting for the next field, or a field gap was closed
  fs = priv->prev_frames[picture->base.voc];
  if (fs && fs->output_called)
    dpb_output (decoder, fs);

  // Create new frame store, and split fields if necessary
  fs = gst_vaapi_frame_store_new (picture);
  if (!fs)
    return FALSE;
  gst_vaapi_frame_store_replace (&priv->prev_frames[picture->base.voc], fs);
  gst_vaapi_frame_store_unref (fs);

  if (!priv->progressive_sequence && gst_vaapi_frame_store_has_frame (fs)) {
    if (!gst_vaapi_frame_store_split_fields (fs, priv->top_field_first))
      return FALSE;
  }
  // C.4.5.1 - Storage and marking of a reference decoded picture into the DPB
  if (GST_VAAPI_PICTURE_IS_REFERENCE (picture)) {
    while (priv->dpb_count == priv->dpb_size) {
      if (!dpb_bump (decoder, picture))
        return FALSE;
    }
    gst_vaapi_frame_store_replace (&priv->prev_ref_frames[picture->base.voc],
        fs);
  }
  // C.4.5.2 - Storage and marking of a non-reference decoded picture into the DPB
  else {
    const gboolean StoreInterViewOnlyRefFlag =
        !GST_VAAPI_PICTURE_FLAG_IS_SET (picture,
        GST_VAAPI_PICTURE_FLAG_AU_END) &&
        GST_VAAPI_PICTURE_FLAG_IS_SET (picture,
        GST_VAAPI_PICTURE_FLAG_INTER_VIEW);
    if (!picture->output_flag && !StoreInterViewOnlyRefFlag)
      return TRUE;
    while (priv->dpb_count == priv->dpb_size) {
      GstVaapiPictureH264 *found_picture;
      if (!StoreInterViewOnlyRefFlag) {
        if (dpb_find_lowest_poc (decoder, picture, &found_picture) < 0 ||
            found_picture->base.poc > picture->base.poc)
          return dpb_output (decoder, fs);
      }
      if (!dpb_bump (decoder, picture))
        return FALSE;
    }
  }
  gst_vaapi_frame_store_replace (&priv->dpb[priv->dpb_count++], fs);
  return TRUE;
}

static gboolean
dpb_reset (GstVaapiDecoderH264 * decoder, guint dpb_size)
{
  GstVaapiDecoderH264Private *const priv = &decoder->priv;

  if (dpb_size > priv->dpb_size_max) {
    priv->dpb = g_try_realloc_n (priv->dpb, dpb_size, sizeof (*priv->dpb));
    if (!priv->dpb)
      return FALSE;
    memset (&priv->dpb[priv->dpb_size_max], 0,
        (dpb_size - priv->dpb_size_max) * sizeof (*priv->dpb));
    priv->dpb_size_max = dpb_size;
  }
  priv->dpb_size = dpb_size;

  GST_DEBUG ("DPB size %u", priv->dpb_size);
  return TRUE;
}

static void
unref_inter_view (GstVaapiPictureH264 * picture)
{
  if (!picture)
    return;
  GST_VAAPI_PICTURE_FLAG_UNSET (picture, GST_VAAPI_PICTURE_FLAG_INTER_VIEW);
  gst_vaapi_picture_unref (picture);
}

/* Resets MVC resources */
static gboolean
mvc_reset (GstVaapiDecoderH264 * decoder)
{
  GstVaapiDecoderH264Private *const priv = &decoder->priv;
  guint i;

  // Resize array of inter-view references
  if (!priv->inter_views) {
    priv->inter_views = g_ptr_array_new_full (priv->max_views,
        (GDestroyNotify) unref_inter_view);
    if (!priv->inter_views)
      return FALSE;
  }
  // Resize array of previous frame buffers
  for (i = priv->max_views; i < priv->prev_frames_alloc; i++) {
    gst_vaapi_frame_store_replace (&priv->prev_ref_frames[i], NULL);
    gst_vaapi_frame_store_replace (&priv->prev_frames[i], NULL);
  }

  priv->prev_ref_frames = g_try_realloc_n (priv->prev_ref_frames,
      priv->max_views, sizeof (*priv->prev_ref_frames));
  if (!priv->prev_ref_frames)
    goto error_allocate;

  priv->prev_frames = g_try_realloc_n (priv->prev_frames, priv->max_views,
      sizeof (*priv->prev_frames));
  if (!priv->prev_frames)
    goto error_allocate;

  for (i = priv->prev_frames_alloc; i < priv->max_views; i++) {
    priv->prev_ref_frames[i] = NULL;
    priv->prev_frames[i] = NULL;
  }
  priv->prev_frames_alloc = priv->max_views;
  return TRUE;

  /* ERRORS */
error_allocate:
  {
    g_free (priv->prev_ref_frames);
    priv->prev_ref_frames = NULL;
    g_free (priv->prev_frames);
    priv->prev_frames = NULL;
    priv->prev_frames_alloc = 0;
    return FALSE;
  }
}

static GstVaapiDecoderStatus
get_status (GstH264ParserResult result)
{
  GstVaapiDecoderStatus status;

  switch (result) {
    case GST_H264_PARSER_OK:
      status = GST_VAAPI_DECODER_STATUS_SUCCESS;
      break;
    case GST_H264_PARSER_NO_NAL_END:
      status = GST_VAAPI_DECODER_STATUS_ERROR_NO_DATA;
      break;
    case GST_H264_PARSER_ERROR:
      status = GST_VAAPI_DECODER_STATUS_ERROR_BITSTREAM_PARSER;
      break;
    default:
      status = GST_VAAPI_DECODER_STATUS_ERROR_UNKNOWN;
      break;
  }
  return status;
}

static void
gst_vaapi_decoder_h264_close (GstVaapiDecoderH264 * decoder)
{
  GstVaapiDecoderH264Private *const priv = &decoder->priv;

  gst_vaapi_picture_replace (&priv->current_picture, NULL);
  gst_vaapi_parser_info_h264_replace (&priv->prev_slice_pi, NULL);
  gst_vaapi_parser_info_h264_replace (&priv->prev_pi, NULL);

  dpb_clear (decoder, NULL);

  if (priv->inter_views) {
    g_ptr_array_unref (priv->inter_views);
    priv->inter_views = NULL;
  }

  if (priv->parser) {
    gst_h264_nal_parser_free (priv->parser);
    priv->parser = NULL;
  }
}

static gboolean
gst_vaapi_decoder_h264_open (GstVaapiDecoderH264 * decoder)
{
  GstVaapiDecoderH264Private *const priv = &decoder->priv;

  gst_vaapi_decoder_h264_close (decoder);

  priv->parser = gst_h264_nal_parser_new ();
  if (!priv->parser)
    return FALSE;
  return TRUE;
}

static void
gst_vaapi_decoder_h264_destroy (GstVaapiDecoder * base_decoder)
{
  GstVaapiDecoderH264 *const decoder =
      GST_VAAPI_DECODER_H264_CAST (base_decoder);
  GstVaapiDecoderH264Private *const priv = &decoder->priv;
  guint i;

  gst_vaapi_decoder_h264_close (decoder);
  priv->is_opened = FALSE;

  g_clear_pointer (&priv->dpb, g_free);
  priv->dpb_size_max = priv->dpb_size = 0;

  g_clear_pointer (&priv->prev_ref_frames, g_free);
  g_clear_pointer (&priv->prev_frames, g_free);
  priv->prev_frames_alloc = 0;

  for (i = 0; i < G_N_ELEMENTS (priv->pps); i++)
    gst_vaapi_parser_info_h264_replace (&priv->pps[i], NULL);
  gst_vaapi_parser_info_h264_replace (&priv->active_pps, NULL);

  for (i = 0; i < G_N_ELEMENTS (priv->sps); i++)
    gst_vaapi_parser_info_h264_replace (&priv->sps[i], NULL);
  gst_vaapi_parser_info_h264_replace (&priv->active_sps, NULL);
}

static gboolean
gst_vaapi_decoder_h264_create (GstVaapiDecoder * base_decoder)
{
  GstVaapiDecoderH264 *const decoder =
      GST_VAAPI_DECODER_H264_CAST (base_decoder);
  GstVaapiDecoderH264Private *const priv = &decoder->priv;

  priv->profile = GST_VAAPI_PROFILE_UNKNOWN;
  priv->entrypoint = GST_VAAPI_ENTRYPOINT_VLD;
  priv->chroma_type = GST_VAAPI_CHROMA_TYPE_YUV420;
  priv->prev_pic_structure = GST_VAAPI_PICTURE_STRUCTURE_FRAME;
  priv->progressive_sequence = TRUE;
  priv->top_field_first = FALSE;
  priv->stereo_info.mode = GST_VIDEO_MULTIVIEW_MODE_MONO;
  priv->stereo_info.flags = GST_VIDEO_MULTIVIEW_FLAGS_NONE;
  return TRUE;
}

/* Limited reset can just needs to get the decoder
 * ready to process fresh data after a flush.
 * Preserves the existing DPB allocation and any SPS/PPS */
static GstVaapiDecoderStatus
gst_vaapi_decoder_h264_reset (GstVaapiDecoder * base_decoder)
{
  GstVaapiDecoderH264 *const decoder =
      GST_VAAPI_DECODER_H264_CAST (base_decoder);
  GstVaapiDecoderH264Private *const priv = &decoder->priv;

  gst_vaapi_decoder_h264_close (decoder);
  priv->is_opened = FALSE;

  priv->dpb_size = 0;

  g_clear_pointer (&priv->prev_ref_frames, g_free);
  g_clear_pointer (&priv->prev_frames, g_free);
  priv->prev_frames_alloc = 0;
  gst_vaapi_parser_info_h264_replace (&priv->active_pps, NULL);
  gst_vaapi_parser_info_h264_replace (&priv->active_sps, NULL);

  priv->profile = GST_VAAPI_PROFILE_UNKNOWN;
  priv->entrypoint = GST_VAAPI_ENTRYPOINT_VLD;
  priv->chroma_type = GST_VAAPI_CHROMA_TYPE_YUV420;
  priv->prev_pic_structure = GST_VAAPI_PICTURE_STRUCTURE_FRAME;
  priv->progressive_sequence = TRUE;
  priv->top_field_first = FALSE;

  return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

/* Activates the supplied PPS */
static GstH264PPS *
ensure_pps (GstVaapiDecoderH264 * decoder, GstH264PPS * pps)
{
  GstVaapiDecoderH264Private *const priv = &decoder->priv;
  GstVaapiParserInfoH264 *const pi = priv->pps[pps->id];

  gst_vaapi_parser_info_h264_replace (&priv->active_pps, pi);
  return pi ? &pi->data.pps : NULL;
}

/* Returns the active PPS */
static inline GstH264PPS *
get_pps (GstVaapiDecoderH264 * decoder)
{
  GstVaapiParserInfoH264 *const pi = decoder->priv.active_pps;

  return pi ? &pi->data.pps : NULL;
}

/* Activate the supplied SPS */
static GstH264SPS *
ensure_sps (GstVaapiDecoderH264 * decoder, GstH264SPS * sps)
{
  GstVaapiDecoderH264Private *const priv = &decoder->priv;
  GstVaapiParserInfoH264 *const pi = priv->sps[sps->id];

  /* Propagate "got I-frame" state to the next SPS unit if the
     current sequence was not ended */
  if (pi && priv->active_sps)
    pi->state |= (priv->active_sps->state & GST_H264_VIDEO_STATE_GOT_I_FRAME);

  gst_vaapi_parser_info_h264_replace (&priv->active_sps, pi);
  return pi ? &pi->data.sps : NULL;
}

/* Returns the active SPS */
static inline GstH264SPS *
get_sps (GstVaapiDecoderH264 * decoder)
{
  GstVaapiParserInfoH264 *const pi = decoder->priv.active_sps;

  return pi ? &pi->data.sps : NULL;
}

static void
fill_profiles (GstVaapiProfile profiles[], guint * n_profiles_ptr,
    GstVaapiProfile profile)
{
  guint n_profiles = *n_profiles_ptr;

  profiles[n_profiles++] = profile;
  switch (profile) {
    case GST_VAAPI_PROFILE_H264_MAIN:
      profiles[n_profiles++] = GST_VAAPI_PROFILE_H264_HIGH;
      break;
    default:
      break;
  }
  *n_profiles_ptr = n_profiles;
}

/* Fills in compatible profiles for MVC decoding */
static void
fill_profiles_mvc (GstVaapiDecoderH264 * decoder, GstVaapiProfile profiles[],
    guint * n_profiles_ptr, guint dpb_size)
{
  const gchar *const vendor_string =
      gst_vaapi_display_get_vendor_string (GST_VAAPI_DECODER_DISPLAY (decoder));

  gboolean add_high_profile = FALSE;
  struct map
  {
    const gchar *str;
    guint str_len;
  };
  const struct map *m;

  // Drivers that support slice level decoding
  if (vendor_string && dpb_size <= 16) {
    static const struct map drv_names[] = {
      {"Intel i965 driver", 17},
      {NULL, 0}
    };
    for (m = drv_names; m->str != NULL && !add_high_profile; m++) {
      if (g_ascii_strncasecmp (vendor_string, m->str, m->str_len) == 0)
        add_high_profile = TRUE;
    }
  }

  if (add_high_profile)
    fill_profiles (profiles, n_profiles_ptr, GST_VAAPI_PROFILE_H264_HIGH);
}

static GstVaapiProfile
get_profile (GstVaapiDecoderH264 * decoder, GstH264SPS * sps, guint dpb_size)
{
  GstVaapiDecoderH264Private *const priv = &decoder->priv;
  GstVaapiDisplay *const display = GST_VAAPI_DECODER_DISPLAY (decoder);
  GstVaapiProfile profile, profiles[4];
  guint i, n_profiles = 0;

  profile = gst_vaapi_utils_h264_get_profile (sps->profile_idc);
  if (!profile)
    return GST_VAAPI_PROFILE_UNKNOWN;

  fill_profiles (profiles, &n_profiles, profile);
  switch (profile) {
    case GST_VAAPI_PROFILE_H264_BASELINE:
      GST_INFO ("Baseline stream to be processed as Constrained-Baseline or "
          "Main");
      fill_profiles (profiles, &n_profiles,
          GST_VAAPI_PROFILE_H264_CONSTRAINED_BASELINE);
      fill_profiles (profiles, &n_profiles, GST_VAAPI_PROFILE_H264_MAIN);
      break;
    case GST_VAAPI_PROFILE_H264_EXTENDED:
      if (sps->constraint_set1_flag) {  // A.2.2 (main profile)
        fill_profiles (profiles, &n_profiles, GST_VAAPI_PROFILE_H264_MAIN);
      }
      break;
    case GST_VAAPI_PROFILE_H264_MULTIVIEW_HIGH:
      if (priv->max_views == 2) {
        fill_profiles (profiles, &n_profiles,
            GST_VAAPI_PROFILE_H264_STEREO_HIGH);
      }
      fill_profiles_mvc (decoder, profiles, &n_profiles, dpb_size);
      break;
    case GST_VAAPI_PROFILE_H264_STEREO_HIGH:
      if (sps->frame_mbs_only_flag) {
        fill_profiles (profiles, &n_profiles,
            GST_VAAPI_PROFILE_H264_MULTIVIEW_HIGH);
      }
      fill_profiles_mvc (decoder, profiles, &n_profiles, dpb_size);
      break;
    default:
      break;
  }

  /* If the preferred profile (profiles[0]) matches one that we already
     found, then just return it now instead of searching for it again */
  if (profiles[0] == priv->profile)
    return priv->profile;

  for (i = 0; i < n_profiles; i++) {
    if (gst_vaapi_display_has_decoder (display, profiles[i], priv->entrypoint))
      return profiles[i];
  }
  return GST_VAAPI_PROFILE_UNKNOWN;
}

static GstVaapiDecoderStatus
ensure_context (GstVaapiDecoderH264 * decoder, GstH264SPS * sps)
{
  GstVaapiDecoder *const base_decoder = GST_VAAPI_DECODER_CAST (decoder);
  GstVaapiDecoderH264Private *const priv = &decoder->priv;
  GstVaapiContextInfo info;
  GstVaapiProfile profile;
  GstVaapiChromaType chroma_type;
  gboolean reset_context = FALSE;
  guint mb_width, mb_height, dpb_size, num_views;

  num_views = get_num_views (sps);
  if (priv->max_views < num_views) {
    priv->max_views = num_views;
    reset_context = TRUE;
    GST_DEBUG ("maximum number of views changed to %u", num_views);
  }

  dpb_size = get_max_dec_frame_buffering (sps);
  if (priv->dpb_size < dpb_size) {
    GST_DEBUG ("DPB size increased");
    reset_context = TRUE;
  }

  profile = get_profile (decoder, sps, dpb_size);
  if (!profile) {
    GST_ERROR ("unsupported profile_idc %u", sps->profile_idc);
    return GST_VAAPI_DECODER_STATUS_ERROR_UNSUPPORTED_PROFILE;
  }

  if (!priv->profile || (priv->profile != profile && priv->max_views == 1)) {
    GST_DEBUG ("profile changed to %x", profile);
    reset_context = TRUE;
    priv->profile = profile;
  }

  if (reset_context) {
    switch (num_views) {
      case 1:
        /* Frame-packed mode details should be used if we got */
        if (priv->stereo_info.mode != GST_VIDEO_MULTIVIEW_MODE_NONE) {
          gst_vaapi_decoder_set_multiview_mode (base_decoder,
              2, priv->stereo_info.mode, priv->stereo_info.flags);
        } else {
          gst_vaapi_decoder_set_multiview_mode (base_decoder,
              num_views, GST_VIDEO_MULTIVIEW_MODE_NONE,
              GST_VIDEO_MULTIVIEW_FLAGS_NONE);
        }
        break;
      case 2:                  /* Assume stereo */
        if (profile == GST_VAAPI_PROFILE_H264_STEREO_HIGH) {
          GST_DEBUG ("Stereo profile - frame-by-frame output, %d views",
              num_views);
          gst_vaapi_decoder_set_multiview_mode (base_decoder, num_views,
              GST_VIDEO_MULTIVIEW_MODE_FRAME_BY_FRAME,
              GST_VIDEO_MULTIVIEW_FLAGS_NONE);
          break;
        }
        /* non-stereo 2 views. Fall through */
      default:
        GST_DEBUG ("Multiview profile - frame-by-frame output, %d views",
            num_views);
        gst_vaapi_decoder_set_multiview_mode (base_decoder, num_views,
            GST_VIDEO_MULTIVIEW_MODE_MULTIVIEW_FRAME_BY_FRAME,
            GST_VIDEO_MULTIVIEW_FLAGS_NONE);
        break;
    }
  }

  chroma_type = gst_vaapi_utils_h264_get_chroma_type (sps->chroma_format_idc);
  if (!chroma_type) {
    GST_ERROR ("unsupported chroma_format_idc %u", sps->chroma_format_idc);
    return GST_VAAPI_DECODER_STATUS_ERROR_UNSUPPORTED_CHROMA_FORMAT;
  }

  if (priv->chroma_type != chroma_type) {
    GST_DEBUG ("chroma format changed");
    reset_context = TRUE;
    priv->chroma_type = chroma_type;
  }

  mb_width = sps->pic_width_in_mbs_minus1 + 1;
  mb_height =
      (sps->pic_height_in_map_units_minus1 + 1) << !sps->frame_mbs_only_flag;
  if (priv->mb_width != mb_width || priv->mb_height != mb_height) {
    GST_DEBUG ("size changed");
    reset_context = TRUE;
    priv->mb_width = mb_width;
    priv->mb_height = mb_height;
  }

  if (priv->progressive_sequence != sps->frame_mbs_only_flag) {
    GST_DEBUG ("interlacing-mode changed");
    priv->progressive_sequence = sps->frame_mbs_only_flag;
    gst_vaapi_decoder_set_interlaced (base_decoder,
        !priv->progressive_sequence);
    priv->top_field_first = FALSE;
  }

  gst_vaapi_decoder_set_pixel_aspect_ratio (base_decoder,
      sps->vui_parameters.par_n, sps->vui_parameters.par_d);

  if (!reset_context && priv->has_context)
    return GST_VAAPI_DECODER_STATUS_SUCCESS;

  /* XXX: fix surface size when cropping is implemented */
  /* *INDENT-OFF* */
  info = (GstVaapiContextInfo) {
    .profile = priv->profile,
    .entrypoint = priv->entrypoint,
    .chroma_type = priv->chroma_type,
    .width = sps->width,
    .height = sps->height,
    .ref_frames = dpb_size,
  };
  /* *INDENT-ON* */

  if (!gst_vaapi_decoder_ensure_context (GST_VAAPI_DECODER (decoder), &info))
    return GST_VAAPI_DECODER_STATUS_ERROR_UNKNOWN;
  priv->has_context = TRUE;

  /* Reset DPB */
  if (!dpb_reset (decoder, dpb_size))
    return GST_VAAPI_DECODER_STATUS_ERROR_ALLOCATION_FAILED;

  /* Reset MVC data */
  if (!mvc_reset (decoder))
    return GST_VAAPI_DECODER_STATUS_ERROR_ALLOCATION_FAILED;
  return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static void
fill_iq_matrix_4x4 (VAIQMatrixBufferH264 * iq_matrix, const GstH264PPS * pps,
    const GstH264SPS * sps)
{
  guint i;

  /* There are always 6 4x4 scaling lists */
  g_assert (G_N_ELEMENTS (iq_matrix->ScalingList4x4) == 6);
  g_assert (G_N_ELEMENTS (iq_matrix->ScalingList4x4[0]) == 16);

  for (i = 0; i < G_N_ELEMENTS (iq_matrix->ScalingList4x4); i++)
    gst_h264_quant_matrix_4x4_get_raster_from_zigzag (iq_matrix->ScalingList4x4
        [i], pps->scaling_lists_4x4[i]);
}

static void
fill_iq_matrix_8x8 (VAIQMatrixBufferH264 * iq_matrix, const GstH264PPS * pps,
    const GstH264SPS * sps)
{
  guint i, n;

  /* If chroma_format_idc != 3, there are up to 2 8x8 scaling lists */
  if (!pps->transform_8x8_mode_flag)
    return;

  g_assert (G_N_ELEMENTS (iq_matrix->ScalingList8x8) >= 2);
  g_assert (G_N_ELEMENTS (iq_matrix->ScalingList8x8[0]) == 64);

  n = (sps->chroma_format_idc != 3) ? 2 : 6;
  for (i = 0; i < n; i++) {
    gst_h264_quant_matrix_8x8_get_raster_from_zigzag (iq_matrix->ScalingList8x8
        [i], pps->scaling_lists_8x8[i]);
  }
}

static GstVaapiDecoderStatus
ensure_quant_matrix (GstVaapiDecoderH264 * decoder,
    GstVaapiPictureH264 * picture)
{
  GstVaapiPicture *const base_picture = &picture->base;
  GstH264PPS *const pps = get_pps (decoder);
  GstH264SPS *const sps = get_sps (decoder);
  VAIQMatrixBufferH264 *iq_matrix;

  base_picture->iq_matrix = GST_VAAPI_IQ_MATRIX_NEW (H264, decoder);
  if (!base_picture->iq_matrix) {
    GST_ERROR ("failed to allocate IQ matrix");
    return GST_VAAPI_DECODER_STATUS_ERROR_ALLOCATION_FAILED;
  }
  iq_matrix = base_picture->iq_matrix->param;

  /* XXX: we can only support 4:2:0 or 4:2:2 since ScalingLists8x8[]
     is not large enough to hold lists for 4:4:4 */
  if (sps->chroma_format_idc == 3)
    return GST_VAAPI_DECODER_STATUS_ERROR_UNSUPPORTED_CHROMA_FORMAT;

  fill_iq_matrix_4x4 (iq_matrix, pps, sps);
  fill_iq_matrix_8x8 (iq_matrix, pps, sps);

  return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static inline gboolean
is_valid_state (guint state, guint ref_state)
{
  return (state & ref_state) == ref_state;
}

static GstVaapiDecoderStatus
decode_current_picture (GstVaapiDecoderH264 * decoder)
{
  GstVaapiDecoderH264Private *const priv = &decoder->priv;
  GstVaapiParserInfoH264 *const sps_pi = decoder->priv.active_sps;
  GstVaapiPictureH264 *const picture = priv->current_picture;

  if (!is_valid_state (priv->decoder_state, GST_H264_VIDEO_STATE_VALID_PICTURE))
    goto drop_frame;

  priv->decoder_state |= sps_pi->state;
  if (!(priv->decoder_state & GST_H264_VIDEO_STATE_GOT_I_FRAME)) {
    if (priv->decoder_state & GST_H264_VIDEO_STATE_GOT_P_SLICE)
      goto drop_frame;
    sps_pi->state |= GST_H264_VIDEO_STATE_GOT_I_FRAME;
  }

  priv->decoder_state = 0;
  priv->pic_structure = GST_H264_SEI_PIC_STRUCT_FRAME;

  if (!picture)
    return GST_VAAPI_DECODER_STATUS_SUCCESS;

  if (!gst_vaapi_picture_decode (GST_VAAPI_PICTURE_CAST (picture)))
    goto error;
  if (!exec_ref_pic_marking (decoder, picture))
    goto error;
  if (!dpb_add (decoder, picture))
    goto error;

  if (priv->force_low_latency)
    dpb_output_ready_frames (decoder);
  gst_vaapi_picture_replace (&priv->current_picture, NULL);
  return GST_VAAPI_DECODER_STATUS_SUCCESS;

  /* ERRORS */
error:
  {
    /* XXX: fix for cases where first field failed to be decoded */
    gst_vaapi_picture_replace (&priv->current_picture, NULL);
    return GST_VAAPI_DECODER_STATUS_ERROR_UNKNOWN;
  }

drop_frame:
  {
    priv->decoder_state = 0;
    priv->pic_structure = GST_H264_SEI_PIC_STRUCT_FRAME;
    return (GstVaapiDecoderStatus) GST_VAAPI_DECODER_STATUS_DROP_FRAME;
  }
}

static GstVaapiDecoderStatus
parse_sps (GstVaapiDecoderH264 * decoder, GstVaapiDecoderUnit * unit)
{
  GstVaapiDecoderH264Private *const priv = &decoder->priv;
  GstVaapiParserInfoH264 *const pi = unit->parsed_info;
  GstH264SPS *const sps = &pi->data.sps;
  GstH264ParserResult result;

  GST_DEBUG ("parse SPS");

  priv->parser_state = 0;

  /* Variables that don't have inferred values per the H.264
     standard but that should get a default value anyway */
  sps->log2_max_pic_order_cnt_lsb_minus4 = 0;

  result = gst_h264_parser_parse_sps (priv->parser, &pi->nalu, sps);
  if (result != GST_H264_PARSER_OK)
    return get_status (result);

  priv->parser_state |= GST_H264_VIDEO_STATE_GOT_SPS;
  return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static GstVaapiDecoderStatus
parse_subset_sps (GstVaapiDecoderH264 * decoder, GstVaapiDecoderUnit * unit)
{
  GstVaapiDecoderH264Private *const priv = &decoder->priv;
  GstVaapiParserInfoH264 *const pi = unit->parsed_info;
  GstH264SPS *const sps = &pi->data.sps;
  GstH264ParserResult result;

  GST_DEBUG ("parse subset SPS");

  /* Variables that don't have inferred values per the H.264
     standard but that should get a default value anyway */
  sps->log2_max_pic_order_cnt_lsb_minus4 = 0;

  result = gst_h264_parser_parse_subset_sps (priv->parser, &pi->nalu, sps);
  if (result != GST_H264_PARSER_OK)
    return get_status (result);

  priv->parser_state |= GST_H264_VIDEO_STATE_GOT_SPS;
  return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static GstVaapiDecoderStatus
parse_pps (GstVaapiDecoderH264 * decoder, GstVaapiDecoderUnit * unit)
{
  GstVaapiDecoderH264Private *const priv = &decoder->priv;
  GstVaapiParserInfoH264 *const pi = unit->parsed_info;
  GstH264PPS *const pps = &pi->data.pps;
  GstH264ParserResult result;

  GST_DEBUG ("parse PPS");

  /* Variables that don't have inferred values per the H.264
     standard but that should get a default value anyway */
  pps->slice_group_map_type = 0;
  pps->slice_group_change_rate_minus1 = 0;
  pps->slice_group_id = NULL;

  result = gst_h264_parser_parse_pps (priv->parser, &pi->nalu, pps);

  /* PPS's sps id might be an ignored subset sps in SVC streams */
  if (priv->base_only && result == GST_H264_PARSER_BROKEN_LINK) {
    pi->nalu.valid = FALSE;
    return GST_VAAPI_DECODER_STATUS_SUCCESS;
  }

  priv->parser_state &= GST_H264_VIDEO_STATE_GOT_SPS;

  if (result != GST_H264_PARSER_OK)
    return get_status (result);

  priv->parser_state |= GST_H264_VIDEO_STATE_GOT_PPS;

  if (pps->num_slice_groups_minus1 > 0) {
    GST_FIXME ("FMO is not supported");
    return GST_VAAPI_DECODER_STATUS_ERROR_BITSTREAM_PARSER;
  }

  return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static GstVaapiDecoderStatus
parse_sei (GstVaapiDecoderH264 * decoder, GstVaapiDecoderUnit * unit)
{
  GstVaapiDecoderH264Private *const priv = &decoder->priv;
  GstVaapiParserInfoH264 *const pi = unit->parsed_info;
  GArray **const sei_ptr = &pi->data.sei;
  GstH264ParserResult result;

  GST_DEBUG ("parse SEI");

  result = gst_h264_parser_parse_sei (priv->parser, &pi->nalu, sei_ptr);
  if (result != GST_H264_PARSER_OK) {
    GST_WARNING ("failed to parse SEI messages");
    return get_status (result);
  }
  return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static GstVaapiDecoderStatus
parse_slice (GstVaapiDecoderH264 * decoder, GstVaapiDecoderUnit * unit)
{
  GstVaapiDecoderH264Private *const priv = &decoder->priv;
  GstVaapiParserInfoH264 *const pi = unit->parsed_info;
  GstH264SliceHdr *const slice_hdr = &pi->data.slice_hdr;
  GstH264NalUnit *const nalu = &pi->nalu;
  GstH264SPS *sps;
  GstH264ParserResult result;

  GST_DEBUG ("parse slice");

  priv->parser_state &= (GST_H264_VIDEO_STATE_GOT_SPS |
      GST_H264_VIDEO_STATE_GOT_PPS);

  /* Propagate Prefix NAL unit info, if necessary */
  switch (nalu->type) {
    case GST_H264_NAL_SLICE:
    case GST_H264_NAL_SLICE_IDR:{
      GstVaapiParserInfoH264 *const prev_pi = priv->prev_pi;
      if (prev_pi && prev_pi->nalu.type == GST_H264_NAL_PREFIX_UNIT) {
        /* MVC sequences shall have a Prefix NAL unit immediately
           preceding this NAL unit */
        pi->nalu.extension_type = prev_pi->nalu.extension_type;
        pi->nalu.extension = prev_pi->nalu.extension;
      } else {
        /* In the very unlikely case there is no Prefix NAL unit
           immediately preceding this NAL unit, try to infer some
           defaults (H.7.4.1.1) */
        GstH264NalUnitExtensionMVC *const mvc = &pi->nalu.extension.mvc;
        mvc->non_idr_flag = !(nalu->type == GST_H264_NAL_SLICE_IDR);
        nalu->idr_pic_flag = !mvc->non_idr_flag;
        mvc->priority_id = 0;
        mvc->view_id = 0;
        mvc->temporal_id = 0;
        mvc->anchor_pic_flag = 0;
        mvc->inter_view_flag = 1;
      }
      break;
    }
  }

  /* Variables that don't have inferred values per the H.264
     standard but that should get a default value anyway */
  slice_hdr->cabac_init_idc = 0;
  slice_hdr->direct_spatial_mv_pred_flag = 0;

  result = gst_h264_parser_parse_slice_hdr (priv->parser, &pi->nalu,
      slice_hdr, TRUE, TRUE);
  if (result != GST_H264_PARSER_OK)
    return get_status (result);

  sps = slice_hdr->pps->sequence;

  /* Update MVC data */
  pi->view_id = get_view_id (&pi->nalu);
  pi->voc = get_view_order_index (sps, pi->view_id);

  priv->parser_state |= GST_H264_VIDEO_STATE_GOT_SLICE;
  if (!GST_H264_IS_I_SLICE (slice_hdr))
    priv->parser_state |= GST_H264_VIDEO_STATE_GOT_P_SLICE;
  return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static GstVaapiDecoderStatus
decode_sps (GstVaapiDecoderH264 * decoder, GstVaapiDecoderUnit * unit)
{
  GstVaapiDecoderH264Private *const priv = &decoder->priv;
  GstVaapiParserInfoH264 *const pi = unit->parsed_info;
  GstH264SPS *const sps = &pi->data.sps;

  GST_DEBUG ("decode SPS");

  gst_vaapi_parser_info_h264_replace (&priv->sps[sps->id], pi);
  return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static GstVaapiDecoderStatus
decode_subset_sps (GstVaapiDecoderH264 * decoder, GstVaapiDecoderUnit * unit)
{
  GstVaapiDecoderH264Private *const priv = &decoder->priv;
  GstVaapiParserInfoH264 *const pi = unit->parsed_info;
  GstH264SPS *const sps = &pi->data.sps;

  GST_DEBUG ("decode subset SPS");

  gst_vaapi_parser_info_h264_replace (&priv->sps[sps->id], pi);
  return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static GstVaapiDecoderStatus
decode_pps (GstVaapiDecoderH264 * decoder, GstVaapiDecoderUnit * unit)
{
  GstVaapiDecoderH264Private *const priv = &decoder->priv;
  GstVaapiParserInfoH264 *const pi = unit->parsed_info;
  GstH264PPS *const pps = &pi->data.pps;

  GST_DEBUG ("decode PPS");

  gst_vaapi_parser_info_h264_replace (&priv->pps[pps->id], pi);
  return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static gboolean
decode_sei_frame_packing (GstVaapiDecoderH264 * decoder,
    const GstH264FramePacking * frame_packing)
{
  GstVaapiDecoderH264Private *const priv = &decoder->priv;
  GstVideoMultiviewMode saved_mode = priv->stereo_info.mode;
  GstVideoMultiviewFlags saved_flags = priv->stereo_info.flags;
  gboolean left = TRUE;
  gboolean frame_revert = FALSE;

  /* Only IDs from 0->255 and 512->2^31-1 are valid. Ignore others */
  if ((frame_packing->frame_packing_id >= 256 &&
          frame_packing->frame_packing_id < 512) ||
      (frame_packing->frame_packing_id >= (1U << 31)))
    return FALSE;

  if (frame_packing->frame_packing_cancel_flag) {
    if (priv->stereo_info.id == frame_packing->frame_packing_id)
      priv->stereo_info = (GstVaapiStereo3DInfo) {
      GST_VIDEO_MULTIVIEW_MODE_MONO, GST_VIDEO_MULTIVIEW_FLAGS_NONE, 256, 0};
    return TRUE;
  }

  if (frame_packing->frame_packing_repetition_period != 1) {
    GST_WARNING ("SEI: repetition_period != 1 is not unsupported");
    return FALSE;
  }

  if (frame_packing->frame_packing_type > GST_H264_FRAME_PACKING_NONE) {
    GST_WARNING ("SEI: unsupported frame_packing_type %d",
        frame_packing->frame_packing_type);
    return FALSE;
  }

  if (frame_packing->content_interpretation_type >= 3) {
    GST_WARNING ("SEI: unsupported content_interpretation_type %d",
        frame_packing->frame_packing_type);
    return FALSE;
  }

  /* TODO: frame frame0/1_grid_position_x/y are ignored now. */

  priv->stereo_info = (GstVaapiStereo3DInfo) {
  GST_VIDEO_MULTIVIEW_MODE_MONO, GST_VIDEO_MULTIVIEW_FLAGS_NONE, 256, 0};

  switch (frame_packing->frame_packing_type) {
    case GST_H264_FRAME_PACKING_CHECKERBOARD_INTERLEAVING:
      priv->stereo_info.mode = GST_VIDEO_MULTIVIEW_MODE_CHECKERBOARD;
      break;
    case GST_H264_FRAME_PACKING_COLUMN_INTERLEAVING:
      priv->stereo_info.mode = GST_VIDEO_MULTIVIEW_MODE_COLUMN_INTERLEAVED;
      break;
    case GST_H264_FRAME_PACKING_ROW_INTERLEAVING:
      priv->stereo_info.mode = GST_VIDEO_MULTIVIEW_MODE_ROW_INTERLEAVED;
      break;
    case GST_H264_FRAME_PACKING_SIDE_BY_SIDE:
      if (frame_packing->quincunx_sampling_flag) {
        priv->stereo_info.mode = GST_VIDEO_MULTIVIEW_MODE_SIDE_BY_SIDE_QUINCUNX;
      } else {
        priv->stereo_info.mode = GST_VIDEO_MULTIVIEW_MODE_SIDE_BY_SIDE;
      }
      break;
    case GST_H264_FRAME_PACKING_TOP_BOTTOM:
      priv->stereo_info.mode = GST_VIDEO_MULTIVIEW_MODE_TOP_BOTTOM;
      break;
    case GST_H264_FRAME_PACKING_TEMPORAL_INTERLEAVING:
      priv->stereo_info.mode = GST_VIDEO_MULTIVIEW_MODE_FRAME_BY_FRAME;
      break;
    default:
      priv->stereo_info.mode = GST_VIDEO_MULTIVIEW_MODE_MONO;
      break;
  }

  /* Spec does not describe multi-IDs case, we just keep one valid */
  priv->stereo_info.id = frame_packing->frame_packing_id;
  priv->stereo_info.repetition_period =
      frame_packing->frame_packing_repetition_period;

  if (frame_packing->content_interpretation_type == 2)
    frame_revert = TRUE;

  if (frame_packing->frame_packing_type ==
      GST_H264_FRAME_PACKING_TEMPORAL_INTERLEAVING) {
    if (frame_packing->current_frame_is_frame0_flag) {
      left = TRUE;
    } else {
      left = FALSE;
    }

    if (frame_revert)
      left = !left;
  }

  if (!left)
    priv->stereo_info.flags |= GST_VIDEO_MULTIVIEW_FLAGS_RIGHT_VIEW_FIRST;

  if (frame_packing->frame_packing_type == GST_H264_FRAME_PACKING_SIDE_BY_SIDE
      && frame_packing->spatial_flipping_flag) {
    if (frame_packing->frame0_flipped_flag !=
        ((priv->stereo_info.flags &
                GST_VIDEO_MULTIVIEW_FLAGS_RIGHT_VIEW_FIRST) != 0)) {
      priv->stereo_info.flags |= GST_VIDEO_MULTIVIEW_FLAGS_LEFT_FLOPPED;
    } else {
      priv->stereo_info.flags |= GST_VIDEO_MULTIVIEW_FLAGS_RIGHT_FLOPPED;
    }
  }
  if (frame_packing->frame_packing_type == GST_H264_FRAME_PACKING_TOP_BOTTOM
      && frame_packing->spatial_flipping_flag !=
      ((priv->stereo_info.flags &
              GST_VIDEO_MULTIVIEW_FLAGS_RIGHT_VIEW_FIRST) != 0)) {
    if (frame_packing->frame0_flipped_flag) {
      priv->stereo_info.flags |= GST_VIDEO_MULTIVIEW_FLAGS_LEFT_FLIPPED;
    } else {
      priv->stereo_info.flags |= GST_VIDEO_MULTIVIEW_FLAGS_RIGHT_FLIPPED;
    }
  }

  if (saved_mode != priv->stereo_info.mode
      || saved_flags != priv->stereo_info.flags) {
    gst_vaapi_decoder_set_multiview_mode (GST_VAAPI_DECODER_CAST (decoder),
        2, priv->stereo_info.mode, priv->stereo_info.flags);
  }

  return TRUE;
}

static GstVaapiDecoderStatus
decode_sei (GstVaapiDecoderH264 * decoder, GstVaapiDecoderUnit * unit)
{
  GstVaapiDecoderH264Private *const priv = &decoder->priv;
  GstVaapiParserInfoH264 *const pi = unit->parsed_info;
  guint i;

  GST_DEBUG ("decode SEI messages");

  for (i = 0; i < pi->data.sei->len; i++) {
    const GstH264SEIMessage *const sei =
        &g_array_index (pi->data.sei, GstH264SEIMessage, i);

    switch (sei->payloadType) {
      case GST_H264_SEI_PIC_TIMING:{
        const GstH264PicTiming *const pic_timing = &sei->payload.pic_timing;
        if (pic_timing->pic_struct_present_flag)
          priv->pic_structure = pic_timing->pic_struct;
        break;
      }
      case GST_H264_SEI_FRAME_PACKING:
        decode_sei_frame_packing (decoder, &sei->payload.frame_packing);
        break;
      default:
        break;
    }
  }
  return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static GstVaapiDecoderStatus
decode_sequence_end (GstVaapiDecoderH264 * decoder)
{
  GstVaapiDecoderH264Private *const priv = &decoder->priv;
  GstVaapiParserInfoH264 *const sps_pi = decoder->priv.active_sps;

  GST_DEBUG ("decode sequence-end");

  /* Sequence ended, don't try to propagate "got I-frame" state
     beyond this point */
  if (sps_pi)
    sps_pi->state &= ~GST_H264_VIDEO_STATE_GOT_I_FRAME;

  dpb_flush (decoder, NULL);

  /* Reset defaults, should there be a new sequence available next */
  priv->max_views = 1;
  return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

/* 8.2.1.1 - Decoding process for picture order count type 0 */
static void
init_picture_poc_0 (GstVaapiDecoderH264 * decoder,
    GstVaapiPictureH264 * picture, GstH264SliceHdr * slice_hdr)
{
  GstVaapiDecoderH264Private *const priv = &decoder->priv;
  GstH264SPS *const sps = get_sps (decoder);
  const gint32 MaxPicOrderCntLsb =
      1 << (sps->log2_max_pic_order_cnt_lsb_minus4 + 4);
  gint32 temp_poc;

  GST_DEBUG ("decode picture order count type 0");

  if (GST_VAAPI_PICTURE_IS_IDR (picture)) {
    priv->prev_poc_msb = 0;
    priv->prev_poc_lsb = 0;
  } else if (priv->prev_pic_has_mmco5) {
    priv->prev_poc_msb = 0;
    priv->prev_poc_lsb =
        (priv->prev_pic_structure == GST_VAAPI_PICTURE_STRUCTURE_BOTTOM_FIELD ?
        0 : priv->field_poc[TOP_FIELD]);
  } else {
    priv->prev_poc_msb = priv->poc_msb;
    priv->prev_poc_lsb = priv->poc_lsb;
  }

  // (8-3)
  priv->poc_lsb = slice_hdr->pic_order_cnt_lsb;
  if (priv->poc_lsb < priv->prev_poc_lsb &&
      (priv->prev_poc_lsb - priv->poc_lsb) >= (MaxPicOrderCntLsb / 2))
    priv->poc_msb = priv->prev_poc_msb + MaxPicOrderCntLsb;
  else if (priv->poc_lsb > priv->prev_poc_lsb &&
      (priv->poc_lsb - priv->prev_poc_lsb) > (MaxPicOrderCntLsb / 2))
    priv->poc_msb = priv->prev_poc_msb - MaxPicOrderCntLsb;
  else
    priv->poc_msb = priv->prev_poc_msb;

  temp_poc = priv->poc_msb + priv->poc_lsb;
  switch (picture->structure) {
    case GST_VAAPI_PICTURE_STRUCTURE_FRAME:
      // (8-4, 8-5)
      priv->field_poc[TOP_FIELD] = temp_poc;
      priv->field_poc[BOTTOM_FIELD] = temp_poc +
          slice_hdr->delta_pic_order_cnt_bottom;
      break;
    case GST_VAAPI_PICTURE_STRUCTURE_TOP_FIELD:
      // (8-4)
      priv->field_poc[TOP_FIELD] = temp_poc;
      break;
    case GST_VAAPI_PICTURE_STRUCTURE_BOTTOM_FIELD:
      // (8-5)
      priv->field_poc[BOTTOM_FIELD] = temp_poc;
      break;
  }
}

/* 8.2.1.2 - Decoding process for picture order count type 1 */
static void
init_picture_poc_1 (GstVaapiDecoderH264 * decoder,
    GstVaapiPictureH264 * picture, GstH264SliceHdr * slice_hdr)
{
  GstVaapiDecoderH264Private *const priv = &decoder->priv;
  GstH264SPS *const sps = get_sps (decoder);
  const gint32 MaxFrameNum = 1 << (sps->log2_max_frame_num_minus4 + 4);
  gint32 prev_frame_num_offset, abs_frame_num, expected_poc;
  guint i;

  GST_DEBUG ("decode picture order count type 1");

  if (priv->prev_pic_has_mmco5)
    prev_frame_num_offset = 0;
  else
    prev_frame_num_offset = priv->frame_num_offset;

  // (8-6)
  if (GST_VAAPI_PICTURE_IS_IDR (picture))
    priv->frame_num_offset = 0;
  else if (priv->prev_frame_num > priv->frame_num)
    priv->frame_num_offset = prev_frame_num_offset + MaxFrameNum;
  else
    priv->frame_num_offset = prev_frame_num_offset;

  // (8-7)
  if (sps->num_ref_frames_in_pic_order_cnt_cycle != 0)
    abs_frame_num = priv->frame_num_offset + priv->frame_num;
  else
    abs_frame_num = 0;
  if (!GST_VAAPI_PICTURE_IS_REFERENCE (picture) && abs_frame_num > 0)
    abs_frame_num = abs_frame_num - 1;

  if (abs_frame_num > 0) {
    gint32 expected_delta_per_poc_cycle;
    gint32 poc_cycle_cnt, frame_num_in_poc_cycle;

    expected_delta_per_poc_cycle = 0;
    for (i = 0; i < sps->num_ref_frames_in_pic_order_cnt_cycle; i++)
      expected_delta_per_poc_cycle += sps->offset_for_ref_frame[i];

    // (8-8)
    poc_cycle_cnt = (abs_frame_num - 1) /
        sps->num_ref_frames_in_pic_order_cnt_cycle;
    frame_num_in_poc_cycle = (abs_frame_num - 1) %
        sps->num_ref_frames_in_pic_order_cnt_cycle;

    // (8-9)
    expected_poc = poc_cycle_cnt * expected_delta_per_poc_cycle;
    for (i = 0; i <= frame_num_in_poc_cycle; i++)
      expected_poc += sps->offset_for_ref_frame[i];
  } else
    expected_poc = 0;
  if (!GST_VAAPI_PICTURE_IS_REFERENCE (picture))
    expected_poc += sps->offset_for_non_ref_pic;

  // (8-10)
  switch (picture->structure) {
    case GST_VAAPI_PICTURE_STRUCTURE_FRAME:
      priv->field_poc[TOP_FIELD] = expected_poc +
          slice_hdr->delta_pic_order_cnt[0];
      priv->field_poc[BOTTOM_FIELD] = priv->field_poc[TOP_FIELD] +
          sps->offset_for_top_to_bottom_field +
          slice_hdr->delta_pic_order_cnt[1];
      break;
    case GST_VAAPI_PICTURE_STRUCTURE_TOP_FIELD:
      priv->field_poc[TOP_FIELD] = expected_poc +
          slice_hdr->delta_pic_order_cnt[0];
      break;
    case GST_VAAPI_PICTURE_STRUCTURE_BOTTOM_FIELD:
      priv->field_poc[BOTTOM_FIELD] = expected_poc +
          sps->offset_for_top_to_bottom_field +
          slice_hdr->delta_pic_order_cnt[0];
      break;
  }
}

/* 8.2.1.3 - Decoding process for picture order count type 2 */
static void
init_picture_poc_2 (GstVaapiDecoderH264 * decoder,
    GstVaapiPictureH264 * picture, GstH264SliceHdr * slice_hdr)
{
  GstVaapiDecoderH264Private *const priv = &decoder->priv;
  GstH264SPS *const sps = get_sps (decoder);
  const gint32 MaxFrameNum = 1 << (sps->log2_max_frame_num_minus4 + 4);
  gint32 prev_frame_num_offset, temp_poc;

  GST_DEBUG ("decode picture order count type 2");

  if (priv->prev_pic_has_mmco5)
    prev_frame_num_offset = 0;
  else
    prev_frame_num_offset = priv->frame_num_offset;

  // (8-11)
  if (GST_VAAPI_PICTURE_IS_IDR (picture))
    priv->frame_num_offset = 0;
  else if (priv->prev_frame_num > priv->frame_num)
    priv->frame_num_offset = prev_frame_num_offset + MaxFrameNum;
  else
    priv->frame_num_offset = prev_frame_num_offset;

  // (8-12)
  if (GST_VAAPI_PICTURE_IS_IDR (picture))
    temp_poc = 0;
  else if (!GST_VAAPI_PICTURE_IS_REFERENCE (picture))
    temp_poc = 2 * (priv->frame_num_offset + priv->frame_num) - 1;
  else
    temp_poc = 2 * (priv->frame_num_offset + priv->frame_num);

  // (8-13)
  if (picture->structure != GST_VAAPI_PICTURE_STRUCTURE_BOTTOM_FIELD)
    priv->field_poc[TOP_FIELD] = temp_poc;
  if (picture->structure != GST_VAAPI_PICTURE_STRUCTURE_TOP_FIELD)
    priv->field_poc[BOTTOM_FIELD] = temp_poc;
}

/* 8.2.1 - Decoding process for picture order count */
static void
init_picture_poc (GstVaapiDecoderH264 * decoder,
    GstVaapiPictureH264 * picture, GstH264SliceHdr * slice_hdr)
{
  GstVaapiDecoderH264Private *const priv = &decoder->priv;
  GstH264SPS *const sps = get_sps (decoder);

  switch (sps->pic_order_cnt_type) {
    case 0:
      init_picture_poc_0 (decoder, picture, slice_hdr);
      break;
    case 1:
      init_picture_poc_1 (decoder, picture, slice_hdr);
      break;
    case 2:
      init_picture_poc_2 (decoder, picture, slice_hdr);
      break;
  }

  if (picture->structure != GST_VAAPI_PICTURE_STRUCTURE_BOTTOM_FIELD)
    picture->field_poc[TOP_FIELD] = priv->field_poc[TOP_FIELD];
  if (picture->structure != GST_VAAPI_PICTURE_STRUCTURE_TOP_FIELD)
    picture->field_poc[BOTTOM_FIELD] = priv->field_poc[BOTTOM_FIELD];
  picture->base.poc = MIN (picture->field_poc[0], picture->field_poc[1]);
}

static int
compare_picture_pic_num_dec (const void *a, const void *b)
{
  const GstVaapiPictureH264 *const picA = *(GstVaapiPictureH264 **) a;
  const GstVaapiPictureH264 *const picB = *(GstVaapiPictureH264 **) b;

  return picB->pic_num - picA->pic_num;
}

static int
compare_picture_long_term_pic_num_inc (const void *a, const void *b)
{
  const GstVaapiPictureH264 *const picA = *(GstVaapiPictureH264 **) a;
  const GstVaapiPictureH264 *const picB = *(GstVaapiPictureH264 **) b;

  return picA->long_term_pic_num - picB->long_term_pic_num;
}

static int
compare_picture_poc_dec (const void *a, const void *b)
{
  const GstVaapiPictureH264 *const picA = *(GstVaapiPictureH264 **) a;
  const GstVaapiPictureH264 *const picB = *(GstVaapiPictureH264 **) b;

  return picB->base.poc - picA->base.poc;
}

static int
compare_picture_poc_inc (const void *a, const void *b)
{
  const GstVaapiPictureH264 *const picA = *(GstVaapiPictureH264 **) a;
  const GstVaapiPictureH264 *const picB = *(GstVaapiPictureH264 **) b;

  return picA->base.poc - picB->base.poc;
}

static int
compare_picture_frame_num_wrap_dec (const void *a, const void *b)
{
  const GstVaapiPictureH264 *const picA = *(GstVaapiPictureH264 **) a;
  const GstVaapiPictureH264 *const picB = *(GstVaapiPictureH264 **) b;

  return picB->frame_num_wrap - picA->frame_num_wrap;
}

static int
compare_picture_long_term_frame_idx_inc (const void *a, const void *b)
{
  const GstVaapiPictureH264 *const picA = *(GstVaapiPictureH264 **) a;
  const GstVaapiPictureH264 *const picB = *(GstVaapiPictureH264 **) b;

  return picA->long_term_frame_idx - picB->long_term_frame_idx;
}

/* 8.2.4.1 - Decoding process for picture numbers */
static void
init_picture_refs_pic_num (GstVaapiDecoderH264 * decoder,
    GstVaapiPictureH264 * picture, GstH264SliceHdr * slice_hdr)
{
  GstVaapiDecoderH264Private *const priv = &decoder->priv;
  GstH264SPS *const sps = get_sps (decoder);
  const gint32 MaxFrameNum = 1 << (sps->log2_max_frame_num_minus4 + 4);
  guint i;

  GST_DEBUG ("decode picture numbers");

  for (i = 0; i < priv->short_ref_count; i++) {
    GstVaapiPictureH264 *const pic = priv->short_ref[i];

    // (H.8.2)
    if (pic->base.view_id != picture->base.view_id)
      continue;

    // (8-27)
    if (pic->frame_num > priv->frame_num)
      pic->frame_num_wrap = pic->frame_num - MaxFrameNum;
    else
      pic->frame_num_wrap = pic->frame_num;

    // (8-28, 8-30, 8-31)
    if (GST_VAAPI_PICTURE_IS_FRAME (picture))
      pic->pic_num = pic->frame_num_wrap;
    else {
      if (pic->structure == picture->structure)
        pic->pic_num = 2 * pic->frame_num_wrap + 1;
      else
        pic->pic_num = 2 * pic->frame_num_wrap;
    }
  }

  for (i = 0; i < priv->long_ref_count; i++) {
    GstVaapiPictureH264 *const pic = priv->long_ref[i];

    // (H.8.2)
    if (pic->base.view_id != picture->base.view_id)
      continue;

    // (8-29, 8-32, 8-33)
    if (GST_VAAPI_PICTURE_IS_FRAME (picture))
      pic->long_term_pic_num = pic->long_term_frame_idx;
    else {
      if (pic->structure == picture->structure)
        pic->long_term_pic_num = 2 * pic->long_term_frame_idx + 1;
      else
        pic->long_term_pic_num = 2 * pic->long_term_frame_idx;
    }
  }
}

#define SORT_REF_LIST(list, n, compare_func) \
    qsort(list, n, sizeof(*(list)), compare_picture_##compare_func)

static void
init_picture_refs_fields_1 (guint picture_structure,
    GstVaapiPictureH264 * RefPicList[32],
    guint * RefPicList_count,
    GstVaapiPictureH264 * ref_list[32], guint ref_list_count)
{
  guint i, j, n;

  i = 0;
  j = 0;
  n = *RefPicList_count;
  do {
    g_assert (n < 32);
    for (; i < ref_list_count; i++) {
      if (ref_list[i]->structure == picture_structure) {
        RefPicList[n++] = ref_list[i++];
        break;
      }
    }
    for (; j < ref_list_count; j++) {
      if (ref_list[j]->structure != picture_structure) {
        RefPicList[n++] = ref_list[j++];
        break;
      }
    }
  } while (i < ref_list_count || j < ref_list_count);
  *RefPicList_count = n;
}

static inline void
init_picture_refs_fields (GstVaapiPictureH264 * picture,
    GstVaapiPictureH264 * RefPicList[32],
    guint * RefPicList_count,
    GstVaapiPictureH264 * short_ref[32],
    guint short_ref_count,
    GstVaapiPictureH264 * long_ref[32], guint long_ref_count)
{
  guint n = 0;

  /* 8.2.4.2.5 - reference picture lists in fields */
  init_picture_refs_fields_1 (picture->structure, RefPicList, &n,
      short_ref, short_ref_count);
  init_picture_refs_fields_1 (picture->structure, RefPicList, &n,
      long_ref, long_ref_count);
  *RefPicList_count = n;
}

/* Finds the inter-view reference picture with the supplied view id */
static GstVaapiPictureH264 *
find_inter_view_reference (GstVaapiDecoderH264 * decoder, guint16 view_id)
{
  GPtrArray *const inter_views = decoder->priv.inter_views;
  guint i;

  for (i = 0; i < inter_views->len; i++) {
    GstVaapiPictureH264 *const picture = g_ptr_array_index (inter_views, i);
    if (picture->base.view_id == view_id)
      return picture;
  }

  GST_WARNING ("failed to find inter-view reference picture for view_id: %d",
      view_id);
  return NULL;
}

/* Checks whether the view id exists in the supplied list of view ids */
static gboolean
find_view_id (guint16 view_id, const guint16 * view_ids, guint num_view_ids)
{
  guint i;

  for (i = 0; i < num_view_ids; i++) {
    if (view_ids[i] == view_id)
      return TRUE;
  }
  return FALSE;
}

static gboolean
find_view_id_in_view (guint16 view_id, const GstH264SPSExtMVCView * view,
    gboolean is_anchor)
{
  if (is_anchor)
    return (find_view_id (view_id, view->anchor_ref_l0,
            view->num_anchor_refs_l0) ||
        find_view_id (view_id, view->anchor_ref_l1, view->num_anchor_refs_l1));

  return (find_view_id (view_id, view->non_anchor_ref_l0,
          view->num_non_anchor_refs_l0) ||
      find_view_id (view_id, view->non_anchor_ref_l1,
          view->num_non_anchor_refs_l1));
}

/* Checks whether the inter-view reference picture with the supplied
   view id is used for decoding the current view component picture */
static gboolean
is_inter_view_reference_for_picture (GstVaapiDecoderH264 * decoder,
    guint16 view_id, GstVaapiPictureH264 * picture)
{
  const GstH264SPS *const sps = get_sps (decoder);
  gboolean is_anchor;

  if (!GST_VAAPI_PICTURE_IS_MVC (picture) ||
      sps->extension_type != GST_H264_NAL_EXTENSION_MVC)
    return FALSE;

  is_anchor = GST_VAAPI_PICTURE_IS_ANCHOR (picture);
  return find_view_id_in_view (view_id,
      &sps->extension.mvc.view[picture->base.voc], is_anchor);
}

/* Checks whether the supplied inter-view reference picture is used
   for decoding the next view component pictures */
static gboolean
is_inter_view_reference_for_next_pictures (GstVaapiDecoderH264 * decoder,
    GstVaapiPictureH264 * picture)
{
  const GstH264SPS *const sps = get_sps (decoder);
  gboolean is_anchor;
  guint i, num_views;

  if (!GST_VAAPI_PICTURE_IS_MVC (picture) ||
      sps->extension_type != GST_H264_NAL_EXTENSION_MVC)
    return FALSE;

  is_anchor = GST_VAAPI_PICTURE_IS_ANCHOR (picture);
  num_views = sps->extension.mvc.num_views_minus1 + 1;
  for (i = picture->base.voc + 1; i < num_views; i++) {
    const GstH264SPSExtMVCView *const view = &sps->extension.mvc.view[i];
    if (find_view_id_in_view (picture->base.view_id, view, is_anchor))
      return TRUE;
  }
  return FALSE;
}

/* H.8.2.1 - Initialization process for inter-view prediction references */
static gboolean
init_picture_refs_mvc_1 (GstVaapiDecoderH264 * decoder,
    GstVaapiPictureH264 ** ref_list, guint * ref_list_count_ptr, guint num_refs,
    const guint16 * view_ids, guint num_view_ids)
{
  guint j, n;

  n = *ref_list_count_ptr;
  for (j = 0; j < num_view_ids && n < num_refs; j++) {
    GstVaapiPictureH264 *pic;

    if (!(pic = find_inter_view_reference (decoder, view_ids[j])))
      return FALSE;

    ref_list[n++] = pic;
  }

  *ref_list_count_ptr = n;

  return TRUE;
}

static inline gboolean
init_picture_refs_mvc (GstVaapiDecoderH264 * decoder,
    GstVaapiPictureH264 * picture, GstH264SliceHdr * slice_hdr, guint list)
{
  GstVaapiDecoderH264Private *const priv = &decoder->priv;
  const GstH264SPS *const sps = get_sps (decoder);
  const GstH264SPSExtMVCView *view;
  gboolean ret = TRUE;

  GST_DEBUG ("initialize reference picture list for inter-view prediction");

  if (sps->extension_type != GST_H264_NAL_EXTENSION_MVC)
    return TRUE;
  view = &sps->extension.mvc.view[picture->base.voc];

#define INVOKE_INIT_PICTURE_REFS_MVC(ref_list, view_list) do {          \
        ret = init_picture_refs_mvc_1(decoder,                          \
            priv->RefPicList##ref_list,                                 \
            &priv->RefPicList##ref_list##_count,                        \
            slice_hdr->num_ref_idx_l##ref_list##_active_minus1 + 1,     \
            view->view_list##_l##ref_list,                              \
            view->num_##view_list##s_l##ref_list);                      \
    } while (0)

  if (list == 0) {
    if (GST_VAAPI_PICTURE_IS_ANCHOR (picture))
      INVOKE_INIT_PICTURE_REFS_MVC (0, anchor_ref);
    else
      INVOKE_INIT_PICTURE_REFS_MVC (0, non_anchor_ref);
  } else {
    if (GST_VAAPI_PICTURE_IS_ANCHOR (picture))
      INVOKE_INIT_PICTURE_REFS_MVC (1, anchor_ref);
    else
      INVOKE_INIT_PICTURE_REFS_MVC (1, non_anchor_ref);
  }

  return ret;

#undef INVOKE_INIT_PICTURE_REFS_MVC
}

static gboolean
init_picture_refs_p_slice (GstVaapiDecoderH264 * decoder,
    GstVaapiPictureH264 * picture, GstH264SliceHdr * slice_hdr)
{
  GstVaapiDecoderH264Private *const priv = &decoder->priv;
  GstVaapiPictureH264 **ref_list;
  guint i;
  gboolean ret = TRUE;

  GST_DEBUG ("decode reference picture list for P and SP slices");

  if (GST_VAAPI_PICTURE_IS_FRAME (picture)) {
    /* 8.2.4.2.1 - P and SP slices in frames */
    if (priv->short_ref_count > 0) {
      ref_list = priv->RefPicList0;
      for (i = 0; i < priv->short_ref_count; i++)
        ref_list[i] = priv->short_ref[i];
      SORT_REF_LIST (ref_list, i, pic_num_dec);
      priv->RefPicList0_count += i;
    }

    if (priv->long_ref_count > 0) {
      ref_list = &priv->RefPicList0[priv->RefPicList0_count];
      for (i = 0; i < priv->long_ref_count; i++)
        ref_list[i] = priv->long_ref[i];
      SORT_REF_LIST (ref_list, i, long_term_pic_num_inc);
      priv->RefPicList0_count += i;
    }
  } else {
    /* 8.2.4.2.2 - P and SP slices in fields */
    GstVaapiPictureH264 *short_ref[32];
    guint short_ref_count = 0;
    GstVaapiPictureH264 *long_ref[32];
    guint long_ref_count = 0;

    if (priv->short_ref_count > 0) {
      for (i = 0; i < priv->short_ref_count; i++)
        short_ref[i] = priv->short_ref[i];
      SORT_REF_LIST (short_ref, i, frame_num_wrap_dec);
      short_ref_count = i;
    }

    if (priv->long_ref_count > 0) {
      for (i = 0; i < priv->long_ref_count; i++)
        long_ref[i] = priv->long_ref[i];
      SORT_REF_LIST (long_ref, i, long_term_frame_idx_inc);
      long_ref_count = i;
    }

    init_picture_refs_fields (picture,
        priv->RefPicList0, &priv->RefPicList0_count,
        short_ref, short_ref_count, long_ref, long_ref_count);
  }

  if (GST_VAAPI_PICTURE_IS_MVC (picture)) {
    /* RefPicList0 */
    ret = init_picture_refs_mvc (decoder, picture, slice_hdr, 0);
  }

  return ret;
}

static gboolean
init_picture_refs_b_slice (GstVaapiDecoderH264 * decoder,
    GstVaapiPictureH264 * picture, GstH264SliceHdr * slice_hdr)
{
  GstVaapiDecoderH264Private *const priv = &decoder->priv;
  GstVaapiPictureH264 **ref_list;
  guint i, n;
  gboolean ret = TRUE;

  GST_DEBUG ("decode reference picture list for B slices");

  if (GST_VAAPI_PICTURE_IS_FRAME (picture)) {
    /* 8.2.4.2.3 - B slices in frames */

    /* RefPicList0 */
    if (priv->short_ref_count > 0) {
      // 1. Short-term references
      ref_list = priv->RefPicList0;
      for (n = 0, i = 0; i < priv->short_ref_count; i++) {
        if (priv->short_ref[i]->base.poc < picture->base.poc)
          ref_list[n++] = priv->short_ref[i];
      }
      SORT_REF_LIST (ref_list, n, poc_dec);
      priv->RefPicList0_count += n;

      ref_list = &priv->RefPicList0[priv->RefPicList0_count];
      for (n = 0, i = 0; i < priv->short_ref_count; i++) {
        if (priv->short_ref[i]->base.poc >= picture->base.poc)
          ref_list[n++] = priv->short_ref[i];
      }
      SORT_REF_LIST (ref_list, n, poc_inc);
      priv->RefPicList0_count += n;
    }

    if (priv->long_ref_count > 0) {
      // 2. Long-term references
      ref_list = &priv->RefPicList0[priv->RefPicList0_count];
      for (n = 0, i = 0; i < priv->long_ref_count; i++)
        ref_list[n++] = priv->long_ref[i];
      SORT_REF_LIST (ref_list, n, long_term_pic_num_inc);
      priv->RefPicList0_count += n;
    }

    /* RefPicList1 */
    if (priv->short_ref_count > 0) {
      // 1. Short-term references
      ref_list = priv->RefPicList1;
      for (n = 0, i = 0; i < priv->short_ref_count; i++) {
        if (priv->short_ref[i]->base.poc > picture->base.poc)
          ref_list[n++] = priv->short_ref[i];
      }
      SORT_REF_LIST (ref_list, n, poc_inc);
      priv->RefPicList1_count += n;

      ref_list = &priv->RefPicList1[priv->RefPicList1_count];
      for (n = 0, i = 0; i < priv->short_ref_count; i++) {
        if (priv->short_ref[i]->base.poc <= picture->base.poc)
          ref_list[n++] = priv->short_ref[i];
      }
      SORT_REF_LIST (ref_list, n, poc_dec);
      priv->RefPicList1_count += n;
    }

    if (priv->long_ref_count > 0) {
      // 2. Long-term references
      ref_list = &priv->RefPicList1[priv->RefPicList1_count];
      for (n = 0, i = 0; i < priv->long_ref_count; i++)
        ref_list[n++] = priv->long_ref[i];
      SORT_REF_LIST (ref_list, n, long_term_pic_num_inc);
      priv->RefPicList1_count += n;
    }
  } else {
    /* 8.2.4.2.4 - B slices in fields */
    GstVaapiPictureH264 *short_ref0[32];
    guint short_ref0_count = 0;
    GstVaapiPictureH264 *short_ref1[32];
    guint short_ref1_count = 0;
    GstVaapiPictureH264 *long_ref[32];
    guint long_ref_count = 0;

    /* refFrameList0ShortTerm */
    if (priv->short_ref_count > 0) {
      ref_list = short_ref0;
      for (n = 0, i = 0; i < priv->short_ref_count; i++) {
        if (priv->short_ref[i]->base.poc <= picture->base.poc)
          ref_list[n++] = priv->short_ref[i];
      }
      SORT_REF_LIST (ref_list, n, poc_dec);
      short_ref0_count += n;

      ref_list = &short_ref0[short_ref0_count];
      for (n = 0, i = 0; i < priv->short_ref_count; i++) {
        if (priv->short_ref[i]->base.poc > picture->base.poc)
          ref_list[n++] = priv->short_ref[i];
      }
      SORT_REF_LIST (ref_list, n, poc_inc);
      short_ref0_count += n;
    }

    /* refFrameList1ShortTerm */
    if (priv->short_ref_count > 0) {
      ref_list = short_ref1;
      for (n = 0, i = 0; i < priv->short_ref_count; i++) {
        if (priv->short_ref[i]->base.poc > picture->base.poc)
          ref_list[n++] = priv->short_ref[i];
      }
      SORT_REF_LIST (ref_list, n, poc_inc);
      short_ref1_count += n;

      ref_list = &short_ref1[short_ref1_count];
      for (n = 0, i = 0; i < priv->short_ref_count; i++) {
        if (priv->short_ref[i]->base.poc <= picture->base.poc)
          ref_list[n++] = priv->short_ref[i];
      }
      SORT_REF_LIST (ref_list, n, poc_dec);
      short_ref1_count += n;
    }

    /* refFrameListLongTerm */
    if (priv->long_ref_count > 0) {
      for (i = 0; i < priv->long_ref_count; i++)
        long_ref[i] = priv->long_ref[i];
      SORT_REF_LIST (long_ref, i, long_term_frame_idx_inc);
      long_ref_count = i;
    }

    init_picture_refs_fields (picture,
        priv->RefPicList0, &priv->RefPicList0_count,
        short_ref0, short_ref0_count, long_ref, long_ref_count);

    init_picture_refs_fields (picture,
        priv->RefPicList1, &priv->RefPicList1_count,
        short_ref1, short_ref1_count, long_ref, long_ref_count);
  }

  /* Check whether RefPicList1 is identical to RefPicList0, then
     swap if necessary */
  if (priv->RefPicList1_count > 1 &&
      priv->RefPicList1_count == priv->RefPicList0_count &&
      memcmp (priv->RefPicList0, priv->RefPicList1,
          priv->RefPicList0_count * sizeof (priv->RefPicList0[0])) == 0) {
    GstVaapiPictureH264 *const tmp = priv->RefPicList1[0];
    priv->RefPicList1[0] = priv->RefPicList1[1];
    priv->RefPicList1[1] = tmp;
  }

  if (GST_VAAPI_PICTURE_IS_MVC (picture)) {
    /* RefPicList0 */
    ret = init_picture_refs_mvc (decoder, picture, slice_hdr, 0);

    /* RefPicList1 */
    ret = init_picture_refs_mvc (decoder, picture, slice_hdr, 1);
  }

  return ret;
}

#undef SORT_REF_LIST

static gint
find_short_term_reference (GstVaapiDecoderH264 * decoder, gint32 pic_num)
{
  GstVaapiDecoderH264Private *const priv = &decoder->priv;
  guint i;

  for (i = 0; i < priv->short_ref_count; i++) {
    if (priv->short_ref[i]->pic_num == pic_num)
      return i;
  }
  GST_ERROR ("found no short-term reference picture with PicNum = %d", pic_num);
  return -1;
}

static gint
find_long_term_reference (GstVaapiDecoderH264 * decoder,
    gint32 long_term_pic_num)
{
  GstVaapiDecoderH264Private *const priv = &decoder->priv;
  guint i;

  for (i = 0; i < priv->long_ref_count; i++) {
    if (priv->long_ref[i]->long_term_pic_num == long_term_pic_num)
      return i;
  }
  GST_ERROR ("found no long-term reference picture with LongTermPicNum = %d",
      long_term_pic_num);
  return -1;
}

static gboolean
exec_picture_refs_modification_1 (GstVaapiDecoderH264 * decoder,
    GstVaapiPictureH264 * picture, GstH264SliceHdr * slice_hdr, guint list)
{
  GstVaapiDecoderH264Private *const priv = &decoder->priv;
  GstH264SPS *const sps = get_sps (decoder);
  GstH264RefPicListModification *ref_pic_list_modification;
  guint num_ref_pic_list_modifications;
  GstVaapiPictureH264 **ref_list;
  guint *ref_list_count_ptr, ref_list_idx = 0;
  const guint16 *view_ids = NULL;
  guint i, j, n, num_refs, num_view_ids = 0;
  gint found_ref_idx;
  gint32 MaxPicNum, CurrPicNum, picNumPred, picViewIdxPred;
  gboolean ret = TRUE;

  GST_DEBUG ("modification process of reference picture list %u", list);

  if (list == 0) {
    ref_pic_list_modification = slice_hdr->ref_pic_list_modification_l0;
    num_ref_pic_list_modifications = slice_hdr->n_ref_pic_list_modification_l0;
    ref_list = priv->RefPicList0;
    ref_list_count_ptr = &priv->RefPicList0_count;
    num_refs = slice_hdr->num_ref_idx_l0_active_minus1 + 1;

    if (GST_VAAPI_PICTURE_IS_MVC (picture) &&
        sps->extension_type == GST_H264_NAL_EXTENSION_MVC) {
      const GstH264SPSExtMVCView *const view =
          &sps->extension.mvc.view[picture->base.voc];
      if (GST_VAAPI_PICTURE_IS_ANCHOR (picture)) {
        view_ids = view->anchor_ref_l0;
        num_view_ids = view->num_anchor_refs_l0;
      } else {
        view_ids = view->non_anchor_ref_l0;
        num_view_ids = view->num_non_anchor_refs_l0;
      }
    }
  } else {
    ref_pic_list_modification = slice_hdr->ref_pic_list_modification_l1;
    num_ref_pic_list_modifications = slice_hdr->n_ref_pic_list_modification_l1;
    ref_list = priv->RefPicList1;
    ref_list_count_ptr = &priv->RefPicList1_count;
    num_refs = slice_hdr->num_ref_idx_l1_active_minus1 + 1;

    if (GST_VAAPI_PICTURE_IS_MVC (picture) &&
        sps->extension_type == GST_H264_NAL_EXTENSION_MVC) {
      const GstH264SPSExtMVCView *const view =
          &sps->extension.mvc.view[picture->base.voc];
      if (GST_VAAPI_PICTURE_IS_ANCHOR (picture)) {
        view_ids = view->anchor_ref_l1;
        num_view_ids = view->num_anchor_refs_l1;
      } else {
        view_ids = view->non_anchor_ref_l1;
        num_view_ids = view->num_non_anchor_refs_l1;
      }
    }
  }

  if (!GST_VAAPI_PICTURE_IS_FRAME (picture)) {
    MaxPicNum = 1 << (sps->log2_max_frame_num_minus4 + 5);      // 2 * MaxFrameNum
    CurrPicNum = 2 * slice_hdr->frame_num + 1;  // 2 * frame_num + 1
  } else {
    MaxPicNum = 1 << (sps->log2_max_frame_num_minus4 + 4);      // MaxFrameNum
    CurrPicNum = slice_hdr->frame_num;  // frame_num
  }

  picNumPred = CurrPicNum;
  picViewIdxPred = -1;

  for (i = 0; i < num_ref_pic_list_modifications; i++) {
    GstH264RefPicListModification *const l = &ref_pic_list_modification[i];
    if (l->modification_of_pic_nums_idc == 3)
      break;

    /* 8.2.4.3.1 - Short-term reference pictures */
    if (l->modification_of_pic_nums_idc == 0
        || l->modification_of_pic_nums_idc == 1) {
      gint32 abs_diff_pic_num = l->value.abs_diff_pic_num_minus1 + 1;
      gint32 picNum, picNumNoWrap;

      // (8-34)
      if (l->modification_of_pic_nums_idc == 0) {
        picNumNoWrap = picNumPred - abs_diff_pic_num;
        if (picNumNoWrap < 0)
          picNumNoWrap += MaxPicNum;
      }
      // (8-35)
      else {
        picNumNoWrap = picNumPred + abs_diff_pic_num;
        if (picNumNoWrap >= MaxPicNum)
          picNumNoWrap -= MaxPicNum;
      }
      picNumPred = picNumNoWrap;

      // (8-36)
      picNum = picNumNoWrap;
      if (picNum > CurrPicNum)
        picNum -= MaxPicNum;

      // (8-37)
      for (j = num_refs; j > ref_list_idx; j--)
        ref_list[j] = ref_list[j - 1];
      found_ref_idx = find_short_term_reference (decoder, picNum);
      ref_list[ref_list_idx++] =
          found_ref_idx >= 0 ? priv->short_ref[found_ref_idx] : NULL;
      n = ref_list_idx;
      for (j = ref_list_idx; j <= num_refs; j++) {
        gint32 PicNumF;
        if (!ref_list[j])
          continue;
        PicNumF =
            GST_VAAPI_PICTURE_IS_SHORT_TERM_REFERENCE (ref_list[j]) ?
            ref_list[j]->pic_num : MaxPicNum;
        if (PicNumF != picNum ||
            ref_list[j]->base.view_id != picture->base.view_id)
          ref_list[n++] = ref_list[j];
      }
    }

    /* 8.2.4.3.2 - Long-term reference pictures */
    else if (l->modification_of_pic_nums_idc == 2) {

      for (j = num_refs; j > ref_list_idx; j--)
        ref_list[j] = ref_list[j - 1];
      found_ref_idx =
          find_long_term_reference (decoder, l->value.long_term_pic_num);
      ref_list[ref_list_idx++] =
          found_ref_idx >= 0 ? priv->long_ref[found_ref_idx] : NULL;
      n = ref_list_idx;
      for (j = ref_list_idx; j <= num_refs; j++) {
        gint32 LongTermPicNumF;
        if (!ref_list[j])
          continue;
        LongTermPicNumF =
            GST_VAAPI_PICTURE_IS_LONG_TERM_REFERENCE (ref_list[j]) ?
            ref_list[j]->long_term_pic_num : INT_MAX;
        if (LongTermPicNumF != l->value.long_term_pic_num ||
            ref_list[j]->base.view_id != picture->base.view_id)
          ref_list[n++] = ref_list[j];
      }
    }

    /* H.8.2.2.3 - Inter-view prediction reference pictures */
    else if ((GST_VAAPI_PICTURE_IS_MVC (picture) &&
            sps->extension_type == GST_H264_NAL_EXTENSION_MVC) &&
        (l->modification_of_pic_nums_idc == 4 ||
            l->modification_of_pic_nums_idc == 5)) {
      gint32 abs_diff_view_idx = l->value.abs_diff_view_idx_minus1 + 1;
      gint32 picViewIdx, targetViewId;

      // (H-6)
      if (l->modification_of_pic_nums_idc == 4) {
        picViewIdx = picViewIdxPred - abs_diff_view_idx;
        if (picViewIdx < 0)
          picViewIdx += num_view_ids;
      }
      // (H-7)
      else {
        picViewIdx = picViewIdxPred + abs_diff_view_idx;
        if (picViewIdx >= num_view_ids)
          picViewIdx -= num_view_ids;
      }
      picViewIdxPred = picViewIdx;

      // (H-8, H-9)
      targetViewId = view_ids[picViewIdx];

      // (H-10)
      for (j = num_refs; j > ref_list_idx; j--)
        ref_list[j] = ref_list[j - 1];
      ref_list[ref_list_idx++] =
          find_inter_view_reference (decoder, targetViewId);
      n = ref_list_idx;
      for (j = ref_list_idx; j <= num_refs; j++) {
        if (!ref_list[j])
          continue;
        if (ref_list[j]->base.view_id != targetViewId ||
            ref_list[j]->base.poc != picture->base.poc)
          ref_list[n++] = ref_list[j];
      }
    }
  }

  for (i = 0; i < num_refs; i++) {
    if (!ref_list[i]) {
      ret = FALSE;
      GST_ERROR ("list %u entry %u is empty", list, i);
    }
  }

  *ref_list_count_ptr = num_refs;

  return ret;
}

/* 8.2.4.3 - Modification process for reference picture lists */
static gboolean
exec_picture_refs_modification (GstVaapiDecoderH264 * decoder,
    GstVaapiPictureH264 * picture, GstH264SliceHdr * slice_hdr)
{
  gboolean ret = TRUE;

  GST_DEBUG ("execute ref_pic_list_modification()");

  /* RefPicList0 */
  if (!GST_H264_IS_I_SLICE (slice_hdr) && !GST_H264_IS_SI_SLICE (slice_hdr) &&
      slice_hdr->ref_pic_list_modification_flag_l0)
    ret = exec_picture_refs_modification_1 (decoder, picture, slice_hdr, 0);

  /* RefPicList1 */
  if (GST_H264_IS_B_SLICE (slice_hdr) &&
      slice_hdr->ref_pic_list_modification_flag_l1)
    ret = exec_picture_refs_modification_1 (decoder, picture, slice_hdr, 1);

  return ret;
}

static gboolean
check_picture_ref_corruption (GstVaapiDecoderH264 * decoder,
    GstVaapiPictureH264 * RefPicList[32], guint RefPicList_count)
{
  const guint corrupted_flags =
      GST_VAAPI_PICTURE_FLAG_CORRUPTED | GST_VAAPI_PICTURE_FLAG_GHOST;
  guint i;

  for (i = 0; i < RefPicList_count; i++) {
    GstVaapiPictureH264 *const picture = RefPicList[i];
    if (picture && (GST_VAAPI_PICTURE_FLAGS (picture) & corrupted_flags))
      return TRUE;
  }
  return FALSE;
}

static void
mark_picture_refs (GstVaapiDecoderH264 * decoder, GstVaapiPictureH264 * picture)
{
  GstVaapiDecoderH264Private *const priv = &decoder->priv;

  if (GST_VAAPI_PICTURE_IS_CORRUPTED (picture))
    return;

  if (check_picture_ref_corruption (decoder,
          priv->RefPicList0, priv->RefPicList0_count) ||
      check_picture_ref_corruption (decoder,
          priv->RefPicList1, priv->RefPicList1_count))
    GST_VAAPI_PICTURE_FLAG_SET (picture, GST_VAAPI_PICTURE_FLAG_CORRUPTED);
}

static void
init_picture_ref_lists (GstVaapiDecoderH264 * decoder,
    GstVaapiPictureH264 * picture)
{
  GstVaapiDecoderH264Private *const priv = &decoder->priv;
  guint i, j, short_ref_count, long_ref_count;

  short_ref_count = 0;
  long_ref_count = 0;
  if (GST_VAAPI_PICTURE_IS_FRAME (picture)) {
    for (i = 0; i < priv->dpb_count; i++) {
      GstVaapiFrameStore *const fs = priv->dpb[i];
      GstVaapiPictureH264 *pic;
      if (!gst_vaapi_frame_store_has_frame (fs))
        continue;
      pic = fs->buffers[0];
      if (pic->base.view_id != picture->base.view_id)
        continue;
      if (GST_VAAPI_PICTURE_IS_SHORT_TERM_REFERENCE (pic))
        priv->short_ref[short_ref_count++] = pic;
      else if (GST_VAAPI_PICTURE_IS_LONG_TERM_REFERENCE (pic))
        priv->long_ref[long_ref_count++] = pic;
      pic->structure = GST_VAAPI_PICTURE_STRUCTURE_FRAME;
      pic->other_field = fs->buffers[1];
    }
  } else {
    for (i = 0; i < priv->dpb_count; i++) {
      GstVaapiFrameStore *const fs = priv->dpb[i];
      for (j = 0; j < fs->num_buffers; j++) {
        GstVaapiPictureH264 *const pic = fs->buffers[j];
        if (pic->base.view_id != picture->base.view_id)
          continue;
        if (GST_VAAPI_PICTURE_IS_SHORT_TERM_REFERENCE (pic))
          priv->short_ref[short_ref_count++] = pic;
        else if (GST_VAAPI_PICTURE_IS_LONG_TERM_REFERENCE (pic))
          priv->long_ref[long_ref_count++] = pic;
        pic->structure = pic->base.structure;
        pic->other_field = fs->buffers[j ^ 1];
      }
    }
  }

  for (i = short_ref_count; i < priv->short_ref_count; i++)
    priv->short_ref[i] = NULL;
  priv->short_ref_count = short_ref_count;

  for (i = long_ref_count; i < priv->long_ref_count; i++)
    priv->long_ref[i] = NULL;
  priv->long_ref_count = long_ref_count;
}

static gboolean
init_picture_refs (GstVaapiDecoderH264 * decoder,
    GstVaapiPictureH264 * picture, GstH264SliceHdr * slice_hdr)
{
  GstVaapiDecoderH264Private *const priv = &decoder->priv;
  guint i, num_refs;
  gboolean ret = TRUE;

  init_picture_ref_lists (decoder, picture);
  init_picture_refs_pic_num (decoder, picture, slice_hdr);

  priv->RefPicList0_count = 0;
  priv->RefPicList1_count = 0;

  switch (slice_hdr->type % 5) {
    case GST_H264_P_SLICE:
    case GST_H264_SP_SLICE:
      ret = init_picture_refs_p_slice (decoder, picture, slice_hdr);
      break;
    case GST_H264_B_SLICE:
      ret = init_picture_refs_b_slice (decoder, picture, slice_hdr);
      break;
    default:
      break;
  }

  switch (slice_hdr->type % 5) {
    case GST_H264_B_SLICE:
      num_refs = 1 + slice_hdr->num_ref_idx_l1_active_minus1;
      for (i = priv->RefPicList1_count; i < num_refs; i++)
        priv->RefPicList1[i] = NULL;
      priv->RefPicList1_count = num_refs;

      // fall-through
    case GST_H264_P_SLICE:
    case GST_H264_SP_SLICE:
      num_refs = 1 + slice_hdr->num_ref_idx_l0_active_minus1;
      for (i = priv->RefPicList0_count; i < num_refs; i++)
        priv->RefPicList0[i] = NULL;
      priv->RefPicList0_count = num_refs;
      break;
    default:
      break;
  }

  ret = ret && exec_picture_refs_modification (decoder, picture, slice_hdr);

  mark_picture_refs (decoder, picture);

  return ret;
}

static GstVaapiPictureH264 *
fill_picture_other_field_gap (GstVaapiDecoderH264 * decoder,
    GstVaapiPictureH264 * f0)
{
  GstVaapiDecoderH264Private *const priv = &decoder->priv;
  GstVaapiPictureH264 *prev_picture, *f1;
  gint prev_frame_index;
  guint picture_structure;

  picture_structure = f0->base.structure;
  switch (picture_structure) {
    case GST_VAAPI_PICTURE_STRUCTURE_TOP_FIELD:
      picture_structure = GST_VAAPI_PICTURE_STRUCTURE_BOTTOM_FIELD;
      break;
    case GST_VAAPI_PICTURE_STRUCTURE_BOTTOM_FIELD:
      picture_structure = GST_VAAPI_PICTURE_STRUCTURE_TOP_FIELD;
      break;
    default:
      g_assert (0 && "unexpected picture structure");
      return NULL;
  }
  GST_VAAPI_PICTURE_FLAG_SET (f0, GST_VAAPI_PICTURE_FLAG_ONEFIELD);

  prev_frame_index = dpb_find_nearest_prev_poc (decoder, f0,
      picture_structure, &prev_picture);
  if (prev_frame_index < 0)
    goto error_find_field;

  f1 = gst_vaapi_picture_h264_new_field (f0);
  if (!f1)
    goto error_allocate_field;

  gst_vaapi_surface_proxy_replace (&f1->base.proxy, prev_picture->base.proxy);
  f1->base.surface = GST_VAAPI_SURFACE_PROXY_SURFACE (f1->base.proxy);
  f1->base.surface_id = GST_VAAPI_SURFACE_PROXY_SURFACE_ID (f1->base.proxy);
  f1->base.poc++;
  f1->structure = f1->base.structure;

  /* XXX: clone other H.264 picture specific flags */
  GST_VAAPI_PICTURE_FLAG_SET (f1,
      (GST_VAAPI_PICTURE_FLAG_SKIPPED | GST_VAAPI_PICTURE_FLAG_GHOST));

  gst_vaapi_picture_h264_set_reference (f1, 0, FALSE);
  gst_vaapi_picture_replace (&priv->current_picture, f1);
  gst_vaapi_picture_unref (f1);

  init_picture_ref_lists (decoder, f1);
  init_picture_refs_pic_num (decoder, f1, NULL);
  if (!exec_ref_pic_marking_sliding_window (decoder))
    goto error_exec_ref_pic_marking;
  if (!dpb_add (decoder, f1))
    goto error_append_field;
  return f1;

  /* ERRORS */
error_find_field:
  {
    GST_ERROR ("failed to find field with POC nearest to %d", f0->base.poc);
    return NULL;
  }
error_allocate_field:
  {
    GST_ERROR ("failed to allocate missing field for previous frame store");
    return NULL;
  }
error_exec_ref_pic_marking:
  {
    GST_ERROR ("failed to execute reference picture marking process");
    return NULL;
  }
error_append_field:
  {
    GST_ERROR ("failed to add missing field into previous frame store");
    return NULL;
  }
}

static gboolean
fill_picture_gaps (GstVaapiDecoderH264 * decoder, GstVaapiPictureH264 * picture,
    GstH264SliceHdr * slice_hdr)
{
  GstVaapiDecoderH264Private *const priv = &decoder->priv;
  GstH264SPS *const sps = get_sps (decoder);
  const gint32 MaxFrameNum = 1 << (sps->log2_max_frame_num_minus4 + 4);
  gint32 prev_frame_num;
  GstVaapiFrameStore *prev_frame;
  GstVaapiPicture *base_picture;
  GstVaapiPictureH264 *lost_picture, *prev_picture;
  GstH264SliceHdr lost_slice_hdr;
  gboolean success = FALSE;

  if (priv->prev_ref_frame_num == priv->frame_num)
    return TRUE;
  if ((priv->prev_ref_frame_num + 1) % MaxFrameNum == priv->frame_num)
    return TRUE;
  if (priv->dpb_count == 0)
    return TRUE;

  prev_frame = priv->prev_ref_frames[picture->base.voc];
  g_assert (prev_frame != NULL && prev_frame->buffers[0] != NULL);
  prev_picture = gst_vaapi_picture_ref (prev_frame->buffers[0]);
  gst_vaapi_picture_ref (picture);

  lost_slice_hdr = *slice_hdr;
  lost_slice_hdr.field_pic_flag = 0;
  if (sps->pic_order_cnt_type == 1) {
    lost_slice_hdr.delta_pic_order_cnt[0] = 0;
    lost_slice_hdr.delta_pic_order_cnt[1] = 0;
  }
  lost_slice_hdr.dec_ref_pic_marking.adaptive_ref_pic_marking_mode_flag = 0;

  /* XXX: this process is incorrect for MVC */
  /* Reduce frame num gaps so we don't have to create unnecessary
   * dummy pictures */
  prev_frame_num = priv->prev_ref_frame_num;
  if (prev_frame_num > slice_hdr->frame_num)
    prev_frame_num -= MaxFrameNum;

  if ((slice_hdr->frame_num - prev_frame_num) - 1 > sps->num_ref_frames) {
    prev_frame_num = (slice_hdr->frame_num - sps->num_ref_frames) - 1;

    if (prev_frame_num < 0)
      prev_frame_num += MaxFrameNum;
  }
  priv->frame_num = prev_frame_num;

  for (;;) {
    priv->prev_ref_frame_num = priv->frame_num;
    priv->frame_num = (priv->prev_ref_frame_num + 1) % MaxFrameNum;
    if (priv->frame_num == slice_hdr->frame_num)
      break;

    /* Create new picture */
    lost_picture = gst_vaapi_picture_h264_new_clone (prev_picture);
    if (!lost_picture)
      goto error_allocate_picture;

    base_picture = &lost_picture->base;
    base_picture->type = GST_VAAPI_PICTURE_TYPE_NONE;
    base_picture->pts = GST_CLOCK_TIME_NONE;
    base_picture->structure = GST_VAAPI_PICTURE_STRUCTURE_FRAME;
    lost_picture->frame_num = priv->frame_num;
    lost_picture->frame_num_wrap = priv->frame_num;
    lost_picture->structure = base_picture->structure;

    GST_VAAPI_PICTURE_FLAG_SET (lost_picture,
        (GST_VAAPI_PICTURE_FLAG_SKIPPED |
            GST_VAAPI_PICTURE_FLAG_GHOST |
            GST_VAAPI_PICTURE_FLAG_SHORT_TERM_REFERENCE));

    if (sps->pic_order_cnt_type != 0)
      init_picture_poc (decoder, lost_picture, &lost_slice_hdr);
    else {
      base_picture->poc = prev_picture->base.poc + 2;
      if (prev_picture->field_poc[0] != G_MAXINT32)
        lost_picture->field_poc[0] = prev_picture->field_poc[0] + 2;
      if (prev_picture->field_poc[1] != G_MAXINT32)
        lost_picture->field_poc[1] = prev_picture->field_poc[1] + 2;
    }

    gst_vaapi_picture_replace (&prev_picture, lost_picture);
    gst_vaapi_picture_replace (&priv->current_picture, lost_picture);
    gst_vaapi_picture_unref (lost_picture);

    init_picture_ref_lists (decoder, lost_picture);
    init_picture_refs_pic_num (decoder, lost_picture, &lost_slice_hdr);
    if (!exec_ref_pic_marking_sliding_window (decoder))
      goto error_exec_ref_pic_marking;
    if (!dpb_add (decoder, lost_picture))
      goto error_dpb_add;
    gst_vaapi_picture_replace (&priv->current_picture, NULL);
  }
  success = TRUE;

cleanup:
  priv->frame_num = slice_hdr->frame_num;
  priv->prev_ref_frame_num = (priv->frame_num + MaxFrameNum - 1) % MaxFrameNum;
  gst_vaapi_picture_replace (&prev_picture, NULL);
  gst_vaapi_picture_replace (&priv->current_picture, picture);
  gst_vaapi_picture_unref (picture);
  return success;

  /* ERRORS */
error_allocate_picture:
  {
    GST_ERROR ("failed to allocate lost picture");
    goto cleanup;
  }
error_exec_ref_pic_marking:
  {
    GST_ERROR ("failed to execute reference picture marking process");
    goto cleanup;
  }
error_dpb_add:
  {
    GST_ERROR ("failed to store lost picture into the DPB");
    goto cleanup;
  }
}

static gboolean
init_picture (GstVaapiDecoderH264 * decoder,
    GstVaapiPictureH264 * picture, GstVaapiParserInfoH264 * pi)
{
  GstVaapiDecoderH264Private *const priv = &decoder->priv;
  GstVaapiPicture *const base_picture = &picture->base;
  GstH264SliceHdr *const slice_hdr = &pi->data.slice_hdr;

  if (priv->prev_pic_reference)
    priv->prev_ref_frame_num = priv->frame_num;
  priv->prev_frame_num = priv->frame_num;
  priv->frame_num = slice_hdr->frame_num;
  picture->frame_num = priv->frame_num;
  picture->frame_num_wrap = priv->frame_num;
  picture->output_flag = TRUE;  /* XXX: conformant to Annex A only */

  /* If it's a cloned picture, it has some assignments from parent
   * picture already.  In addition, base decoder doesn't set valid pts
   * to the frame corresponding to cloned picture.
   */
  if (G_LIKELY (!base_picture->parent_picture)) {
    base_picture->pts = GST_VAAPI_DECODER_CODEC_FRAME (decoder)->pts;
    base_picture->type = GST_VAAPI_PICTURE_TYPE_NONE;
    base_picture->view_id = pi->view_id;
    base_picture->voc = pi->voc;
  }

  /* Initialize extensions */
  switch (pi->nalu.extension_type) {
    case GST_H264_NAL_EXTENSION_MVC:{
      GstH264NalUnitExtensionMVC *const mvc = &pi->nalu.extension.mvc;

      GST_VAAPI_PICTURE_FLAG_SET (picture, GST_VAAPI_PICTURE_FLAG_MVC);
      if (mvc->inter_view_flag)
        GST_VAAPI_PICTURE_FLAG_SET (picture, GST_VAAPI_PICTURE_FLAG_INTER_VIEW);
      if (mvc->anchor_pic_flag)
        GST_VAAPI_PICTURE_FLAG_SET (picture, GST_VAAPI_PICTURE_FLAG_ANCHOR);
      break;
    }
  }

  /* Reset decoder state for IDR pictures */
  if (pi->nalu.idr_pic_flag) {
    GST_DEBUG ("<IDR>");
    GST_VAAPI_PICTURE_FLAG_SET (picture, GST_VAAPI_PICTURE_FLAG_IDR);
    dpb_flush (decoder, picture);
  } else if (!fill_picture_gaps (decoder, picture, slice_hdr))
    return FALSE;

  /* Initialize picture structure */
  if (slice_hdr->field_pic_flag) {
    GST_VAAPI_PICTURE_FLAG_SET (picture, GST_VAAPI_PICTURE_FLAG_INTERLACED);
    priv->pic_structure = slice_hdr->bottom_field_flag ?
        GST_H264_SEI_PIC_STRUCT_BOTTOM_FIELD :
        GST_H264_SEI_PIC_STRUCT_TOP_FIELD;
  }

  base_picture->structure = GST_VAAPI_PICTURE_STRUCTURE_FRAME;
  switch (priv->pic_structure) {
    case GST_H264_SEI_PIC_STRUCT_TOP_FIELD:
      base_picture->structure = GST_VAAPI_PICTURE_STRUCTURE_TOP_FIELD;
      if (GST_VAAPI_PICTURE_IS_FIRST_FIELD (picture))
        priv->top_field_first = TRUE;
      break;
    case GST_H264_SEI_PIC_STRUCT_BOTTOM_FIELD:
      base_picture->structure = GST_VAAPI_PICTURE_STRUCTURE_BOTTOM_FIELD;
      break;
    case GST_H264_SEI_PIC_STRUCT_TOP_BOTTOM_TOP:
      GST_VAAPI_PICTURE_FLAG_SET (picture, GST_VAAPI_PICTURE_FLAG_RFF);
      // fall-through
    case GST_H264_SEI_PIC_STRUCT_TOP_BOTTOM:
      if (GST_VAAPI_PICTURE_IS_FIRST_FIELD (picture))
        priv->top_field_first = TRUE;
      break;
    case GST_H264_SEI_PIC_STRUCT_BOTTOM_TOP_BOTTOM:
      GST_VAAPI_PICTURE_FLAG_SET (picture, GST_VAAPI_PICTURE_FLAG_RFF);
      break;
    case GST_H264_SEI_PIC_STRUCT_FRAME:
      if (!priv->progressive_sequence && priv->dpb_count == 0)
        priv->top_field_first = TRUE;
      break;
  }
  picture->structure = base_picture->structure;
  if (priv->top_field_first)
    GST_VAAPI_PICTURE_FLAG_SET (picture, GST_VAAPI_PICTURE_FLAG_TFF);

  /* Initialize reference flags */
  if (pi->nalu.ref_idc) {
    GstH264DecRefPicMarking *const dec_ref_pic_marking =
        &slice_hdr->dec_ref_pic_marking;

    if (GST_VAAPI_PICTURE_IS_IDR (picture) &&
        dec_ref_pic_marking->long_term_reference_flag)
      GST_VAAPI_PICTURE_FLAG_SET (picture,
          GST_VAAPI_PICTURE_FLAG_LONG_TERM_REFERENCE);
    else
      GST_VAAPI_PICTURE_FLAG_SET (picture,
          GST_VAAPI_PICTURE_FLAG_SHORT_TERM_REFERENCE);
  }

  init_picture_poc (decoder, picture, slice_hdr);
  return TRUE;
}

/* 8.2.5.3 - Sliding window decoded reference picture marking process */
static gboolean
exec_ref_pic_marking_sliding_window (GstVaapiDecoderH264 * decoder)
{
  GstVaapiDecoderH264Private *const priv = &decoder->priv;
  GstH264SPS *const sps = get_sps (decoder);
  GstVaapiPictureH264 *ref_picture;
  guint i, m, max_num_ref_frames;

  GST_DEBUG ("reference picture marking process (sliding window)");

  if (!GST_VAAPI_PICTURE_IS_FIRST_FIELD (priv->current_picture))
    return TRUE;

  max_num_ref_frames = sps->num_ref_frames;
  if (max_num_ref_frames == 0)
    max_num_ref_frames = 1;
  if (!GST_VAAPI_PICTURE_IS_FRAME (priv->current_picture))
    max_num_ref_frames <<= 1;

  if (priv->short_ref_count + priv->long_ref_count < max_num_ref_frames)
    return TRUE;
  if (priv->short_ref_count < 1)
    return FALSE;

  for (m = 0, i = 1; i < priv->short_ref_count; i++) {
    GstVaapiPictureH264 *const picture = priv->short_ref[i];
    if (picture->frame_num_wrap < priv->short_ref[m]->frame_num_wrap)
      m = i;
  }

  ref_picture = priv->short_ref[m];
  gst_vaapi_picture_h264_set_reference (ref_picture, 0, TRUE);
  ARRAY_REMOVE_INDEX (priv->short_ref, m);

  /* Both fields need to be marked as "unused for reference", so
     remove the other field from the short_ref[] list as well */
  if (!GST_VAAPI_PICTURE_IS_FRAME (priv->current_picture)
      && ref_picture->other_field) {
    for (i = 0; i < priv->short_ref_count; i++) {
      if (priv->short_ref[i] == ref_picture->other_field) {
        ARRAY_REMOVE_INDEX (priv->short_ref, i);
        break;
      }
    }
  }
  return TRUE;
}

static inline gint32
get_picNumX (GstVaapiPictureH264 * picture,
    GstH264RefPicMarking * ref_pic_marking)
{
  gint32 pic_num;

  if (GST_VAAPI_PICTURE_IS_FRAME (picture))
    pic_num = picture->frame_num_wrap;
  else
    pic_num = 2 * picture->frame_num_wrap + 1;
  pic_num -= ref_pic_marking->difference_of_pic_nums_minus1 + 1;
  return pic_num;
}

/* 8.2.5.4.1. Mark short-term reference picture as "unused for reference" */
static void
exec_ref_pic_marking_adaptive_mmco_1 (GstVaapiDecoderH264 * decoder,
    GstVaapiPictureH264 * picture, GstH264RefPicMarking * ref_pic_marking)
{
  GstVaapiDecoderH264Private *const priv = &decoder->priv;
  gint32 i, picNumX;

  picNumX = get_picNumX (picture, ref_pic_marking);
  i = find_short_term_reference (decoder, picNumX);
  if (i < 0)
    return;

  gst_vaapi_picture_h264_set_reference (priv->short_ref[i], 0,
      GST_VAAPI_PICTURE_IS_FRAME (picture));
  ARRAY_REMOVE_INDEX (priv->short_ref, i);
}

/* 8.2.5.4.2. Mark long-term reference picture as "unused for reference" */
static void
exec_ref_pic_marking_adaptive_mmco_2 (GstVaapiDecoderH264 * decoder,
    GstVaapiPictureH264 * picture, GstH264RefPicMarking * ref_pic_marking)
{
  GstVaapiDecoderH264Private *const priv = &decoder->priv;
  gint32 i;

  i = find_long_term_reference (decoder, ref_pic_marking->long_term_pic_num);
  if (i < 0)
    return;

  gst_vaapi_picture_h264_set_reference (priv->long_ref[i], 0,
      GST_VAAPI_PICTURE_IS_FRAME (picture));
  ARRAY_REMOVE_INDEX (priv->long_ref, i);
}

/* 8.2.5.4.3. Assign LongTermFrameIdx to a short-term reference picture */
static void
exec_ref_pic_marking_adaptive_mmco_3 (GstVaapiDecoderH264 * decoder,
    GstVaapiPictureH264 * picture, GstH264RefPicMarking * ref_pic_marking)
{
  GstVaapiDecoderH264Private *const priv = &decoder->priv;
  GstVaapiPictureH264 *ref_picture, *other_field;
  gint32 i, picNumX;

  for (i = 0; i < priv->long_ref_count; i++) {
    if (priv->long_ref[i]->long_term_frame_idx ==
        ref_pic_marking->long_term_frame_idx)
      break;
  }
  if (i != priv->long_ref_count) {
    gst_vaapi_picture_h264_set_reference (priv->long_ref[i], 0, TRUE);
    ARRAY_REMOVE_INDEX (priv->long_ref, i);
  }

  picNumX = get_picNumX (picture, ref_pic_marking);
  i = find_short_term_reference (decoder, picNumX);
  if (i < 0)
    return;

  ref_picture = priv->short_ref[i];
  ARRAY_REMOVE_INDEX (priv->short_ref, i);
  priv->long_ref[priv->long_ref_count++] = ref_picture;

  ref_picture->long_term_frame_idx = ref_pic_marking->long_term_frame_idx;
  gst_vaapi_picture_h264_set_reference (ref_picture,
      GST_VAAPI_PICTURE_FLAG_LONG_TERM_REFERENCE,
      GST_VAAPI_PICTURE_IS_COMPLETE (picture));

  /* Assign LongTermFrameIdx to the other field if it was also
     marked as "used for long-term reference */
  other_field = ref_picture->other_field;
  if (other_field && GST_VAAPI_PICTURE_IS_LONG_TERM_REFERENCE (other_field))
    other_field->long_term_frame_idx = ref_pic_marking->long_term_frame_idx;
}

/* 8.2.5.4.4. Mark pictures with LongTermFramIdx > max_long_term_frame_idx
 * as "unused for reference" */
static void
exec_ref_pic_marking_adaptive_mmco_4 (GstVaapiDecoderH264 * decoder,
    GstVaapiPictureH264 * picture, GstH264RefPicMarking * ref_pic_marking)
{
  GstVaapiDecoderH264Private *const priv = &decoder->priv;
  gint32 i, long_term_frame_idx;

  long_term_frame_idx = ref_pic_marking->max_long_term_frame_idx_plus1 - 1;

  for (i = 0; i < priv->long_ref_count; i++) {
    if (priv->long_ref[i]->long_term_frame_idx <= long_term_frame_idx)
      continue;
    gst_vaapi_picture_h264_set_reference (priv->long_ref[i], 0, FALSE);
    ARRAY_REMOVE_INDEX (priv->long_ref, i);
    i--;
  }
}

/* 8.2.5.4.5. Mark all reference pictures as "unused for reference" */
static void
exec_ref_pic_marking_adaptive_mmco_5 (GstVaapiDecoderH264 * decoder,
    GstVaapiPictureH264 * picture, GstH264RefPicMarking * ref_pic_marking)
{
  GstVaapiDecoderH264Private *const priv = &decoder->priv;

  dpb_flush (decoder, picture);

  priv->prev_pic_has_mmco5 = TRUE;

  /* The picture shall be inferred to have had frame_num equal to 0 (7.4.3) */
  priv->frame_num = 0;
  priv->frame_num_offset = 0;
  picture->frame_num = 0;

  /* Update TopFieldOrderCnt and BottomFieldOrderCnt (8.2.1) */
  if (picture->structure != GST_VAAPI_PICTURE_STRUCTURE_BOTTOM_FIELD)
    picture->field_poc[TOP_FIELD] -= picture->base.poc;
  if (picture->structure != GST_VAAPI_PICTURE_STRUCTURE_TOP_FIELD)
    picture->field_poc[BOTTOM_FIELD] -= picture->base.poc;
  picture->base.poc = 0;
}

/* 8.2.5.4.6. Assign a long-term frame index to the current picture */
static void
exec_ref_pic_marking_adaptive_mmco_6 (GstVaapiDecoderH264 * decoder,
    GstVaapiPictureH264 * picture, GstH264RefPicMarking * ref_pic_marking)
{
  GstVaapiDecoderH264Private *const priv = &decoder->priv;
  GstVaapiPictureH264 *other_field;
  guint i;

  for (i = 0; i < priv->long_ref_count; i++) {
    if (priv->long_ref[i]->long_term_frame_idx ==
        ref_pic_marking->long_term_frame_idx)
      break;
  }
  if (i != priv->long_ref_count) {
    gst_vaapi_picture_h264_set_reference (priv->long_ref[i], 0, TRUE);
    ARRAY_REMOVE_INDEX (priv->long_ref, i);
  }

  picture->long_term_frame_idx = ref_pic_marking->long_term_frame_idx;
  gst_vaapi_picture_h264_set_reference (picture,
      GST_VAAPI_PICTURE_FLAG_LONG_TERM_REFERENCE,
      GST_VAAPI_PICTURE_IS_COMPLETE (picture));

  /* Assign LongTermFrameIdx to the other field if it was also
     marked as "used for long-term reference */
  other_field = GST_VAAPI_PICTURE_H264 (picture->base.parent_picture);
  if (other_field && GST_VAAPI_PICTURE_IS_LONG_TERM_REFERENCE (other_field))
    other_field->long_term_frame_idx = ref_pic_marking->long_term_frame_idx;
}

/* 8.2.5.4. Adaptive memory control decoded reference picture marking process */
static gboolean
exec_ref_pic_marking_adaptive (GstVaapiDecoderH264 * decoder,
    GstVaapiPictureH264 * picture,
    GstH264DecRefPicMarking * dec_ref_pic_marking)
{
  guint i;

  typedef void (*exec_ref_pic_marking_adaptive_mmco_func) (GstVaapiDecoderH264 *
      decoder, GstVaapiPictureH264 * picture,
      GstH264RefPicMarking * ref_pic_marking);

  static const exec_ref_pic_marking_adaptive_mmco_func mmco_funcs[] = {
    NULL,
    exec_ref_pic_marking_adaptive_mmco_1,
    exec_ref_pic_marking_adaptive_mmco_2,
    exec_ref_pic_marking_adaptive_mmco_3,
    exec_ref_pic_marking_adaptive_mmco_4,
    exec_ref_pic_marking_adaptive_mmco_5,
    exec_ref_pic_marking_adaptive_mmco_6,
  };

  GST_DEBUG ("reference picture marking process (adaptive memory control)");

  for (i = 0; i < dec_ref_pic_marking->n_ref_pic_marking; i++) {
    GstH264RefPicMarking *const ref_pic_marking =
        &dec_ref_pic_marking->ref_pic_marking[i];

    const guint mmco = ref_pic_marking->memory_management_control_operation;
    if (mmco < G_N_ELEMENTS (mmco_funcs) && mmco_funcs[mmco])
      mmco_funcs[mmco] (decoder, picture, ref_pic_marking);
    else {
      GST_ERROR ("unhandled MMCO %u", mmco);
      return FALSE;
    }
  }
  return TRUE;
}

/* 8.2.5 - Execute reference picture marking process */
static gboolean
exec_ref_pic_marking (GstVaapiDecoderH264 * decoder,
    GstVaapiPictureH264 * picture)
{
  GstVaapiDecoderH264Private *const priv = &decoder->priv;

  priv->prev_pic_reference = GST_VAAPI_PICTURE_IS_REFERENCE (picture);
  priv->prev_pic_has_mmco5 = FALSE;
  priv->prev_pic_structure = picture->structure;

  if (GST_VAAPI_PICTURE_IS_INTER_VIEW (picture))
    g_ptr_array_add (priv->inter_views, gst_vaapi_picture_ref (picture));

  if (!priv->prev_pic_reference)
    return TRUE;

  if (!GST_VAAPI_PICTURE_IS_IDR (picture)) {
    GstH264DecRefPicMarking *const dec_ref_pic_marking =
        &picture->last_slice_hdr->dec_ref_pic_marking;
    if (dec_ref_pic_marking->adaptive_ref_pic_marking_mode_flag) {
      if (!exec_ref_pic_marking_adaptive (decoder, picture,
              dec_ref_pic_marking))
        return FALSE;
    } else {
      if (!exec_ref_pic_marking_sliding_window (decoder))
        return FALSE;
    }
  }
  return TRUE;
}

static void
vaapi_init_picture (VAPictureH264 * pic)
{
  pic->picture_id = VA_INVALID_ID;
  pic->frame_idx = 0;
  pic->flags = VA_PICTURE_H264_INVALID;
  pic->TopFieldOrderCnt = 0;
  pic->BottomFieldOrderCnt = 0;
}

static void
vaapi_fill_picture (VAPictureH264 * pic, GstVaapiPictureH264 * picture,
    guint picture_structure)
{
  if (!picture_structure)
    picture_structure = picture->structure;

  pic->picture_id = picture->base.surface_id;
  pic->flags = 0;

  if (GST_VAAPI_PICTURE_IS_LONG_TERM_REFERENCE (picture)) {
    pic->flags |= VA_PICTURE_H264_LONG_TERM_REFERENCE;
    pic->frame_idx = picture->long_term_frame_idx;
  } else {
    if (GST_VAAPI_PICTURE_IS_SHORT_TERM_REFERENCE (picture))
      pic->flags |= VA_PICTURE_H264_SHORT_TERM_REFERENCE;
    pic->frame_idx = picture->frame_num;
  }

  switch (picture_structure) {
    case GST_VAAPI_PICTURE_STRUCTURE_FRAME:
      pic->TopFieldOrderCnt = picture->field_poc[TOP_FIELD];
      pic->BottomFieldOrderCnt = picture->field_poc[BOTTOM_FIELD];
      break;
    case GST_VAAPI_PICTURE_STRUCTURE_TOP_FIELD:
      pic->flags |= VA_PICTURE_H264_TOP_FIELD;
      pic->TopFieldOrderCnt = picture->field_poc[TOP_FIELD];
      pic->BottomFieldOrderCnt = 0;
      break;
    case GST_VAAPI_PICTURE_STRUCTURE_BOTTOM_FIELD:
      pic->flags |= VA_PICTURE_H264_BOTTOM_FIELD;
      pic->BottomFieldOrderCnt = picture->field_poc[BOTTOM_FIELD];
      pic->TopFieldOrderCnt = 0;
      break;
  }
}

static void
vaapi_fill_picture_for_RefPicListX (VAPictureH264 * pic,
    GstVaapiPictureH264 * picture)
{
  vaapi_fill_picture (pic, picture, 0);

  /* H.8.4 - MVC inter prediction and inter-view prediction process */
  if (GST_VAAPI_PICTURE_IS_INTER_VIEW (picture)) {
    /* The inter-view reference components and inter-view only
       reference components that are included in the reference
       picture lists are considered as not being marked as "used for
       short-term reference" or "used for long-term reference" */
    pic->flags &= ~(VA_PICTURE_H264_SHORT_TERM_REFERENCE |
        VA_PICTURE_H264_LONG_TERM_REFERENCE);
  }
}

static gboolean
fill_picture (GstVaapiDecoderH264 * decoder, GstVaapiPictureH264 * picture)
{
  GstVaapiDecoderH264Private *const priv = &decoder->priv;
  GstVaapiPicture *const base_picture = &picture->base;
  GstH264PPS *const pps = get_pps (decoder);
  GstH264SPS *const sps = get_sps (decoder);
  VAPictureParameterBufferH264 *const pic_param = base_picture->param;
  guint i, n;

  /* Fill in VAPictureParameterBufferH264 */
  vaapi_fill_picture (&pic_param->CurrPic, picture, 0);

  for (i = 0, n = 0; i < priv->dpb_count; i++) {
    GstVaapiFrameStore *const fs = priv->dpb[i];
    if ((gst_vaapi_frame_store_has_reference (fs) &&
            fs->view_id == picture->base.view_id) ||
        (gst_vaapi_frame_store_has_inter_view (fs) &&
            is_inter_view_reference_for_picture (decoder, fs->view_id,
                picture)))
      vaapi_fill_picture (&pic_param->ReferenceFrames[n++], fs->buffers[0],
          fs->structure);
    if (n >= G_N_ELEMENTS (pic_param->ReferenceFrames))
      break;
  }
  for (; n < G_N_ELEMENTS (pic_param->ReferenceFrames); n++)
    vaapi_init_picture (&pic_param->ReferenceFrames[n]);

#define COPY_FIELD(s, f) \
    pic_param->f = (s)->f

#define COPY_BFM(a, s, f) \
    pic_param->a.bits.f = (s)->f

  pic_param->picture_width_in_mbs_minus1 = priv->mb_width - 1;
  pic_param->picture_height_in_mbs_minus1 = priv->mb_height - 1;
  pic_param->frame_num = priv->frame_num;

  COPY_FIELD (sps, bit_depth_luma_minus8);
  COPY_FIELD (sps, bit_depth_chroma_minus8);
  COPY_FIELD (sps, num_ref_frames);
  if (pic_param->num_ref_frames == 0)
    pic_param->num_ref_frames = priv->dpb_size;

#if !VA_CHECK_VERSION(1,0,0)
  /* Deprecate H.264 baseline profile and FMO support */
  COPY_FIELD (pps, num_slice_groups_minus1);
  COPY_FIELD (pps, slice_group_map_type);
  COPY_FIELD (pps, slice_group_change_rate_minus1);
#endif
  COPY_FIELD (pps, pic_init_qp_minus26);
  COPY_FIELD (pps, pic_init_qs_minus26);
  COPY_FIELD (pps, chroma_qp_index_offset);
  COPY_FIELD (pps, second_chroma_qp_index_offset);

  pic_param->seq_fields.value = 0;      /* reset all bits */
  pic_param->seq_fields.bits.residual_colour_transform_flag =
      sps->separate_colour_plane_flag;
  pic_param->seq_fields.bits.MinLumaBiPredSize8x8 = sps->level_idc >= 31;       /* A.3.3.2 */

  COPY_BFM (seq_fields, sps, chroma_format_idc);
  COPY_BFM (seq_fields, sps, gaps_in_frame_num_value_allowed_flag);
  COPY_BFM (seq_fields, sps, frame_mbs_only_flag);
  COPY_BFM (seq_fields, sps, mb_adaptive_frame_field_flag);
  COPY_BFM (seq_fields, sps, direct_8x8_inference_flag);
  COPY_BFM (seq_fields, sps, log2_max_frame_num_minus4);
  COPY_BFM (seq_fields, sps, pic_order_cnt_type);
  COPY_BFM (seq_fields, sps, log2_max_pic_order_cnt_lsb_minus4);
  COPY_BFM (seq_fields, sps, delta_pic_order_always_zero_flag);

  pic_param->pic_fields.value = 0;      /* reset all bits */
  pic_param->pic_fields.bits.field_pic_flag =
      GST_VAAPI_PICTURE_IS_INTERLACED (picture);
  pic_param->pic_fields.bits.reference_pic_flag =
      GST_VAAPI_PICTURE_IS_REFERENCE (picture);

  COPY_BFM (pic_fields, pps, entropy_coding_mode_flag);
  COPY_BFM (pic_fields, pps, weighted_pred_flag);
  COPY_BFM (pic_fields, pps, weighted_bipred_idc);
  COPY_BFM (pic_fields, pps, transform_8x8_mode_flag);
  COPY_BFM (pic_fields, pps, constrained_intra_pred_flag);
  COPY_BFM (pic_fields, pps, pic_order_present_flag);
  COPY_BFM (pic_fields, pps, deblocking_filter_control_present_flag);
  COPY_BFM (pic_fields, pps, redundant_pic_cnt_present_flag);
  return TRUE;
}

/* Detection of the first VCL NAL unit of a primary coded picture (7.4.1.2.4) */
static gboolean
is_new_picture (GstVaapiParserInfoH264 * pi, GstVaapiParserInfoH264 * prev_pi)
{
  GstH264SliceHdr *const slice_hdr = &pi->data.slice_hdr;
  GstH264PPS *const pps = slice_hdr->pps;
  GstH264SPS *const sps = pps->sequence;
  GstH264SliceHdr *prev_slice_hdr;

  if (!prev_pi)
    return TRUE;
  prev_slice_hdr = &prev_pi->data.slice_hdr;

#define CHECK_EXPR(expr, field_name) do {              \
        if (!(expr)) {                                 \
            GST_DEBUG(field_name " differs in value"); \
            return TRUE;                               \
        }                                              \
    } while (0)

#define CHECK_VALUE(new_slice_hdr, old_slice_hdr, field) \
    CHECK_EXPR(((new_slice_hdr)->field == (old_slice_hdr)->field), #field)

  /* view_id differs in value and VOIdx of current slice_hdr is less
     than the VOIdx of the prev_slice_hdr */
  CHECK_VALUE (pi, prev_pi, view_id);

  /* frame_num differs in value, regardless of inferred values to 0 */
  CHECK_VALUE (slice_hdr, prev_slice_hdr, frame_num);

  /* pic_parameter_set_id differs in value */
  CHECK_VALUE (slice_hdr, prev_slice_hdr, pps);

  /* field_pic_flag differs in value */
  CHECK_VALUE (slice_hdr, prev_slice_hdr, field_pic_flag);

  /* bottom_field_flag is present in both and differs in value */
  if (slice_hdr->field_pic_flag && prev_slice_hdr->field_pic_flag)
    CHECK_VALUE (slice_hdr, prev_slice_hdr, bottom_field_flag);

  /* nal_ref_idc differs in value with one of the nal_ref_idc values is 0 */
  CHECK_EXPR ((pi->nalu.ref_idc != 0) ==
      (prev_pi->nalu.ref_idc != 0), "nal_ref_idc");

  /* POC type is 0 for both and either pic_order_cnt_lsb differs in
     value or delta_pic_order_cnt_bottom differs in value */
  if (sps->pic_order_cnt_type == 0) {
    CHECK_VALUE (slice_hdr, prev_slice_hdr, pic_order_cnt_lsb);
    if (pps->pic_order_present_flag && !slice_hdr->field_pic_flag)
      CHECK_VALUE (slice_hdr, prev_slice_hdr, delta_pic_order_cnt_bottom);
  }

  /* POC type is 1 for both and either delta_pic_order_cnt[0]
     differs in value or delta_pic_order_cnt[1] differs in value */
  else if (sps->pic_order_cnt_type == 1) {
    CHECK_VALUE (slice_hdr, prev_slice_hdr, delta_pic_order_cnt[0]);
    CHECK_VALUE (slice_hdr, prev_slice_hdr, delta_pic_order_cnt[1]);
  }

  /* IdrPicFlag differs in value */
  CHECK_VALUE (&pi->nalu, &prev_pi->nalu, idr_pic_flag);

  /* IdrPicFlag is equal to 1 for both and idr_pic_id differs in value */
  if (pi->nalu.idr_pic_flag)
    CHECK_VALUE (slice_hdr, prev_slice_hdr, idr_pic_id);

#undef CHECK_EXPR
#undef CHECK_VALUE
  return FALSE;
}

/* Detection of a new access unit, assuming we are already in presence
   of a new picture */
static inline gboolean
is_new_access_unit (GstVaapiParserInfoH264 * pi,
    GstVaapiParserInfoH264 * prev_pi)
{
  if (!prev_pi || prev_pi->view_id == pi->view_id)
    return TRUE;
  return pi->voc < prev_pi->voc;
}

/* Determines whether the supplied picture has the same field parity
   than a picture specified through the other slice header */
static inline gboolean
same_field_parity (GstVaapiPictureH264 * field, GstH264SliceHdr * slice_hdr)
{
  g_return_val_if_fail (GST_VAAPI_PICTURE_IS_INTERLACED (field), FALSE);

  return ((field->base.structure == GST_VAAPI_PICTURE_STRUCTURE_BOTTOM_FIELD) ^
      slice_hdr->bottom_field_flag) == 0;
}

/* Finds the first field picture corresponding to the supplied picture */
static GstVaapiPictureH264 *
find_first_field (GstVaapiDecoderH264 * decoder, GstVaapiParserInfoH264 * pi)
{
  GstVaapiDecoderH264Private *const priv = &decoder->priv;
  GstH264SliceHdr *const slice_hdr = &pi->data.slice_hdr;
  GstVaapiFrameStore *fs;
  GstVaapiPictureH264 *f0, *f1;

  fs = priv->prev_frames[pi->voc];
  if (!fs)
    return NULL;

  f0 = fs->buffers[0];
  if (!slice_hdr->field_pic_flag) {
    if (!gst_vaapi_frame_store_has_frame (fs))
      fill_picture_other_field_gap (decoder, f0);
    return NULL;
  }

  /* At this point, the current frame is known to be interlaced */
  if (gst_vaapi_frame_store_has_frame (fs)) {
    return NULL;
  }

  /* At this point, the previous frame is interlaced and contains a
     single field */
  if (f0->frame_num == slice_hdr->frame_num) {
    f1 = f0;
    if (same_field_parity (f0, slice_hdr)) {
      fill_picture_other_field_gap (decoder, f0);
      f1 = NULL;
    }
    return f1;
  }

  fill_picture_other_field_gap (decoder, f0);
  return NULL;
}

static GstVaapiDecoderStatus
decode_picture (GstVaapiDecoderH264 * decoder, GstVaapiDecoderUnit * unit)
{
  GstVaapiDecoderH264Private *const priv = &decoder->priv;
  GstVaapiParserInfoH264 *const pi = unit->parsed_info;
  GstH264SliceHdr *const slice_hdr = &pi->data.slice_hdr;
  GstH264PPS *const pps = ensure_pps (decoder, slice_hdr->pps);
  GstH264SPS *const sps = ensure_sps (decoder, slice_hdr->pps->sequence);
  GstVaapiPictureH264 *picture, *first_field;
  GstVaapiDecoderStatus status;

  if (!(pps && sps))
    return GST_VAAPI_DECODER_STATUS_ERROR_UNKNOWN;

  status = ensure_context (decoder, sps);
  if (status != GST_VAAPI_DECODER_STATUS_SUCCESS)
    return status;

  priv->decoder_state = 0;

  first_field = find_first_field (decoder, pi);
  if (first_field) {
    /* Re-use current picture where the first field was decoded */
    picture = gst_vaapi_picture_h264_new_field (first_field);
    if (!picture) {
      GST_ERROR ("failed to allocate field picture");
      return GST_VAAPI_DECODER_STATUS_ERROR_ALLOCATION_FAILED;
    }
  } else {
    /* Create new picture */
    picture = gst_vaapi_picture_h264_new (decoder);
    if (!picture) {
      GST_ERROR ("failed to allocate picture");
      return GST_VAAPI_DECODER_STATUS_ERROR_ALLOCATION_FAILED;
    }
  }
  gst_vaapi_picture_replace (&priv->current_picture, picture);
  gst_vaapi_picture_unref (picture);

  /* Clear inter-view references list if this is the primary coded
     picture of the current access unit */
  if (pi->flags & GST_VAAPI_DECODER_UNIT_FLAG_AU_START)
    g_ptr_array_set_size (priv->inter_views, 0);

  /* Update cropping rectangle */
  if (sps->frame_cropping_flag) {
    GstVaapiRectangle crop_rect;
    crop_rect.x = sps->crop_rect_x;
    crop_rect.y = sps->crop_rect_y;
    crop_rect.width = sps->crop_rect_width;
    crop_rect.height = sps->crop_rect_height;
    gst_vaapi_picture_set_crop_rect (&picture->base, &crop_rect);
  }

  status = ensure_quant_matrix (decoder, picture);
  if (status != GST_VAAPI_DECODER_STATUS_SUCCESS) {
    GST_ERROR ("failed to reset quantizer matrix");
    return status;
  }

  if (!init_picture (decoder, picture, pi))
    return GST_VAAPI_DECODER_STATUS_ERROR_UNKNOWN;
  if (!fill_picture (decoder, picture))
    return GST_VAAPI_DECODER_STATUS_ERROR_UNKNOWN;

  priv->decoder_state = pi->state;
  return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static inline guint
get_slice_data_bit_offset (GstH264SliceHdr * slice_hdr, guint nal_header_bytes)
{
  guint epb_count;

  epb_count = slice_hdr->n_emulation_prevention_bytes;
  return 8 * nal_header_bytes + slice_hdr->header_size - epb_count * 8;
}

static gboolean
fill_pred_weight_table (GstVaapiDecoderH264 * decoder,
    GstVaapiSlice * slice, GstH264SliceHdr * slice_hdr)
{
  VASliceParameterBufferH264 *const slice_param = slice->param;
  GstH264PPS *const pps = get_pps (decoder);
  GstH264SPS *const sps = get_sps (decoder);
  GstH264PredWeightTable *const w = &slice_hdr->pred_weight_table;
  guint num_weight_tables = 0;
  gint i, j;

  if (pps->weighted_pred_flag &&
      (GST_H264_IS_P_SLICE (slice_hdr) || GST_H264_IS_SP_SLICE (slice_hdr)))
    num_weight_tables = 1;
  else if (pps->weighted_bipred_idc == 1 && GST_H264_IS_B_SLICE (slice_hdr))
    num_weight_tables = 2;
  else
    num_weight_tables = 0;

  slice_param->luma_log2_weight_denom = 0;
  slice_param->chroma_log2_weight_denom = 0;
  slice_param->luma_weight_l0_flag = 0;
  slice_param->chroma_weight_l0_flag = 0;
  slice_param->luma_weight_l1_flag = 0;
  slice_param->chroma_weight_l1_flag = 0;

  if (num_weight_tables < 1)
    return TRUE;

  slice_param->luma_log2_weight_denom = w->luma_log2_weight_denom;
  slice_param->chroma_log2_weight_denom = w->chroma_log2_weight_denom;

  slice_param->luma_weight_l0_flag = 1;
  for (i = 0; i <= slice_param->num_ref_idx_l0_active_minus1; i++) {
    slice_param->luma_weight_l0[i] = w->luma_weight_l0[i];
    slice_param->luma_offset_l0[i] = w->luma_offset_l0[i];
  }

  slice_param->chroma_weight_l0_flag = sps->chroma_array_type != 0;
  if (slice_param->chroma_weight_l0_flag) {
    for (i = 0; i <= slice_param->num_ref_idx_l0_active_minus1; i++) {
      for (j = 0; j < 2; j++) {
        slice_param->chroma_weight_l0[i][j] = w->chroma_weight_l0[i][j];
        slice_param->chroma_offset_l0[i][j] = w->chroma_offset_l0[i][j];
      }
    }
  }

  if (num_weight_tables < 2)
    return TRUE;

  slice_param->luma_weight_l1_flag = 1;
  for (i = 0; i <= slice_param->num_ref_idx_l1_active_minus1; i++) {
    slice_param->luma_weight_l1[i] = w->luma_weight_l1[i];
    slice_param->luma_offset_l1[i] = w->luma_offset_l1[i];
  }

  slice_param->chroma_weight_l1_flag = sps->chroma_array_type != 0;
  if (slice_param->chroma_weight_l1_flag) {
    for (i = 0; i <= slice_param->num_ref_idx_l1_active_minus1; i++) {
      for (j = 0; j < 2; j++) {
        slice_param->chroma_weight_l1[i][j] = w->chroma_weight_l1[i][j];
        slice_param->chroma_offset_l1[i][j] = w->chroma_offset_l1[i][j];
      }
    }
  }
  return TRUE;
}

static gboolean
fill_RefPicList (GstVaapiDecoderH264 * decoder,
    GstVaapiSlice * slice, GstH264SliceHdr * slice_hdr)
{
  GstVaapiDecoderH264Private *const priv = &decoder->priv;
  VASliceParameterBufferH264 *const slice_param = slice->param;
  guint i, num_ref_lists = 0;

  slice_param->num_ref_idx_l0_active_minus1 = 0;
  slice_param->num_ref_idx_l1_active_minus1 = 0;

  /* ensure empty list by default */
  vaapi_init_picture (&slice_param->RefPicList0[0]);
  vaapi_init_picture (&slice_param->RefPicList1[0]);

  if (GST_H264_IS_B_SLICE (slice_hdr))
    num_ref_lists = 2;
  else if (GST_H264_IS_I_SLICE (slice_hdr))
    num_ref_lists = 0;
  else
    num_ref_lists = 1;

  if (num_ref_lists < 1)
    return TRUE;

  slice_param->num_ref_idx_l0_active_minus1 =
      slice_hdr->num_ref_idx_l0_active_minus1;

  for (i = 0; i < priv->RefPicList0_count && priv->RefPicList0[i]; i++)
    vaapi_fill_picture_for_RefPicListX (&slice_param->RefPicList0[i],
        priv->RefPicList0[i]);
  if (i < 32)
    vaapi_init_picture (&slice_param->RefPicList0[i]);

  if (num_ref_lists < 2)
    return TRUE;

  slice_param->num_ref_idx_l1_active_minus1 =
      slice_hdr->num_ref_idx_l1_active_minus1;

  for (i = 0; i < priv->RefPicList1_count && priv->RefPicList1[i]; i++)
    vaapi_fill_picture_for_RefPicListX (&slice_param->RefPicList1[i],
        priv->RefPicList1[i]);
  if (i < 32)
    vaapi_init_picture (&slice_param->RefPicList1[i]);

  return TRUE;
}

static gboolean
fill_slice (GstVaapiDecoderH264 * decoder,
    GstVaapiSlice * slice, GstVaapiParserInfoH264 * pi)
{
  VASliceParameterBufferH264 *const slice_param = slice->param;
  GstH264SliceHdr *const slice_hdr = &pi->data.slice_hdr;

  /* Fill in VASliceParameterBufferH264 */
  slice_param->slice_data_bit_offset =
      get_slice_data_bit_offset (slice_hdr, pi->nalu.header_bytes);
  slice_param->first_mb_in_slice = slice_hdr->first_mb_in_slice;
  slice_param->slice_type = slice_hdr->type % 5;
  slice_param->direct_spatial_mv_pred_flag =
      slice_hdr->direct_spatial_mv_pred_flag;
  slice_param->cabac_init_idc = slice_hdr->cabac_init_idc;
  slice_param->slice_qp_delta = slice_hdr->slice_qp_delta;
  slice_param->disable_deblocking_filter_idc =
      slice_hdr->disable_deblocking_filter_idc;
  slice_param->slice_alpha_c0_offset_div2 =
      slice_hdr->slice_alpha_c0_offset_div2;
  slice_param->slice_beta_offset_div2 = slice_hdr->slice_beta_offset_div2;

  if (!fill_RefPicList (decoder, slice, slice_hdr))
    return FALSE;
  if (!fill_pred_weight_table (decoder, slice, slice_hdr))
    return FALSE;
  return TRUE;
}

static GstVaapiDecoderStatus
decode_slice (GstVaapiDecoderH264 * decoder, GstVaapiDecoderUnit * unit)
{
  GstVaapiDecoderH264Private *const priv = &decoder->priv;
  GstVaapiParserInfoH264 *const pi = unit->parsed_info;
  GstVaapiPictureH264 *const picture = priv->current_picture;
  GstH264SliceHdr *const slice_hdr = &pi->data.slice_hdr;
  GstVaapiSlice *slice;
  GstBuffer *const buffer =
      GST_VAAPI_DECODER_CODEC_FRAME (decoder)->input_buffer;
  GstMapInfo map_info;

  GST_DEBUG ("slice (%u bytes)", pi->nalu.size);

  if (!is_valid_state (pi->state, GST_H264_VIDEO_STATE_VALID_PICTURE_HEADERS)) {
    GST_WARNING ("failed to receive enough headers to decode slice");
    return GST_VAAPI_DECODER_STATUS_SUCCESS;
  }

  if (!ensure_pps (decoder, slice_hdr->pps)) {
    GST_ERROR ("failed to activate PPS");
    return GST_VAAPI_DECODER_STATUS_ERROR_UNKNOWN;
  }

  if (!ensure_sps (decoder, slice_hdr->pps->sequence)) {
    GST_ERROR ("failed to activate SPS");
    return GST_VAAPI_DECODER_STATUS_ERROR_UNKNOWN;
  }

  if (!gst_buffer_map (buffer, &map_info, GST_MAP_READ)) {
    GST_ERROR ("failed to map buffer");
    return GST_VAAPI_DECODER_STATUS_ERROR_UNKNOWN;
  }

  /* Check wether this is the first/last slice in the current access unit */
  if (pi->flags & GST_VAAPI_DECODER_UNIT_FLAG_AU_START)
    GST_VAAPI_PICTURE_FLAG_SET (picture, GST_VAAPI_PICTURE_FLAG_AU_START);
  if (pi->flags & GST_VAAPI_DECODER_UNIT_FLAG_AU_END)
    GST_VAAPI_PICTURE_FLAG_SET (picture, GST_VAAPI_PICTURE_FLAG_AU_END);

  slice = GST_VAAPI_SLICE_NEW (H264, decoder,
      (map_info.data + unit->offset + pi->nalu.offset), pi->nalu.size);
  gst_buffer_unmap (buffer, &map_info);
  if (!slice) {
    GST_ERROR ("failed to allocate slice");
    return GST_VAAPI_DECODER_STATUS_ERROR_ALLOCATION_FAILED;
  }

  if (!init_picture_refs (decoder, picture, slice_hdr)) {
    gst_vaapi_mini_object_unref (GST_VAAPI_MINI_OBJECT (slice));
    return GST_VAAPI_DECODER_STATUS_ERROR_UNKNOWN;
  }

  if (!fill_slice (decoder, slice, pi)) {
    gst_vaapi_mini_object_unref (GST_VAAPI_MINI_OBJECT (slice));
    return GST_VAAPI_DECODER_STATUS_ERROR_UNKNOWN;
  }

  gst_vaapi_picture_add_slice (GST_VAAPI_PICTURE_CAST (picture), slice);
  picture->last_slice_hdr = slice_hdr;
  return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static inline gint
scan_for_start_code (GstAdapter * adapter, guint ofs, guint size, guint32 * scp)
{
  if (size == 0)
    return -1;

  return (gint) gst_adapter_masked_scan_uint32_peek (adapter,
      0xffffff00, 0x00000100, ofs, size, scp);
}

static GstVaapiDecoderStatus
decode_unit (GstVaapiDecoderH264 * decoder, GstVaapiDecoderUnit * unit)
{
  GstVaapiDecoderH264Private *const priv = &decoder->priv;
  GstVaapiParserInfoH264 *const pi = unit->parsed_info;
  GstVaapiDecoderStatus status;

  priv->decoder_state |= pi->state;
  switch (pi->nalu.type) {
    case GST_H264_NAL_SPS:
      status = decode_sps (decoder, unit);
      break;
    case GST_H264_NAL_SUBSET_SPS:
      status = decode_subset_sps (decoder, unit);
      break;
    case GST_H264_NAL_PPS:
      status = decode_pps (decoder, unit);
      break;
    case GST_H264_NAL_SLICE_EXT:
    case GST_H264_NAL_SLICE_IDR:
      /* fall-through. IDR specifics are handled in init_picture() */
    case GST_H264_NAL_SLICE:
      status = decode_slice (decoder, unit);
      break;
    case GST_H264_NAL_SEQ_END:
    case GST_H264_NAL_STREAM_END:
      status = decode_sequence_end (decoder);
      break;
    case GST_H264_NAL_SEI:
      status = decode_sei (decoder, unit);
      break;
    case GST_H264_NAL_SLICE_DPA:
    case GST_H264_NAL_SLICE_DPB:
    case GST_H264_NAL_SLICE_DPC:
    case GST_H264_NAL_AU_DELIMITER:
    case GST_H264_NAL_FILLER_DATA:
    case GST_H264_NAL_SPS_EXT:
    case GST_H264_NAL_PREFIX_UNIT:
    case GST_H264_NAL_DEPTH_SPS:
    case GST_H264_NAL_SLICE_AUX:
    case GST_H264_NAL_SLICE_DEPTH:
      GST_DEBUG ("unsupported NAL unit type %d, just skip", pi->nalu.type);
      status = GST_VAAPI_DECODER_STATUS_SUCCESS;
      break;
    default:
      GST_WARNING ("unknown NAL unit type id %d, skip", pi->nalu.type);
      status = GST_VAAPI_DECODER_STATUS_SUCCESS;
      break;
  }
  return status;
}

static GstVaapiDecoderStatus
gst_vaapi_decoder_h264_decode_codec_data (GstVaapiDecoder * base_decoder,
    const guchar * buf, guint buf_size)
{
  GstVaapiDecoderH264 *const decoder =
      GST_VAAPI_DECODER_H264_CAST (base_decoder);
  GstVaapiDecoderH264Private *const priv = &decoder->priv;
  GstVaapiDecoderStatus status;
  GstVaapiDecoderUnit unit;
  GstVaapiParserInfoH264 *pi = NULL;
  GstH264ParserResult result;
  guint i, ofs, num_sps, num_pps;

  if (!priv->is_opened)
    return GST_VAAPI_DECODER_STATUS_SUCCESS;

  unit.parsed_info = NULL;

  if (buf_size < 7)
    return GST_VAAPI_DECODER_STATUS_ERROR_NO_DATA;

  if (buf[0] != 1) {
    GST_ERROR ("failed to decode codec-data, not in avcC format");
    return GST_VAAPI_DECODER_STATUS_ERROR_BITSTREAM_PARSER;
  }

  priv->nal_length_size = (buf[4] & 0x03) + 1;

  num_sps = buf[5] & 0x1f;
  ofs = 6;

  for (i = 0; i < num_sps; i++) {
    pi = gst_vaapi_parser_info_h264_new ();
    if (!pi)
      return GST_VAAPI_DECODER_STATUS_ERROR_ALLOCATION_FAILED;
    unit.parsed_info = pi;

    result = gst_h264_parser_identify_nalu_avc (priv->parser,
        buf, ofs, buf_size, 2, &pi->nalu);
    if (result != GST_H264_PARSER_OK) {
      status = get_status (result);
      goto cleanup;
    }

    status = parse_sps (decoder, &unit);
    if (status != GST_VAAPI_DECODER_STATUS_SUCCESS)
      goto cleanup;
    ofs = pi->nalu.offset + pi->nalu.size;

    pi->state = priv->parser_state;
    pi->flags = 0;

    status = decode_sps (decoder, &unit);
    if (status != GST_VAAPI_DECODER_STATUS_SUCCESS)
      goto cleanup;
    gst_vaapi_parser_info_h264_replace (&pi, NULL);
  }

  num_pps = buf[ofs];
  ofs++;

  for (i = 0; i < num_pps; i++) {
    pi = gst_vaapi_parser_info_h264_new ();
    if (!pi)
      return GST_VAAPI_DECODER_STATUS_ERROR_ALLOCATION_FAILED;
    unit.parsed_info = pi;

    result = gst_h264_parser_identify_nalu_avc (priv->parser,
        buf, ofs, buf_size, 2, &pi->nalu);
    if (result != GST_H264_PARSER_OK) {
      status = get_status (result);
      goto cleanup;
    }

    status = parse_pps (decoder, &unit);
    if (status != GST_VAAPI_DECODER_STATUS_SUCCESS)
      goto cleanup;
    ofs = pi->nalu.offset + pi->nalu.size;

    pi->state = priv->parser_state;
    pi->flags = 0;

    status = decode_pps (decoder, &unit);
    if (status != GST_VAAPI_DECODER_STATUS_SUCCESS)
      goto cleanup;
    gst_vaapi_parser_info_h264_replace (&pi, NULL);
  }

  priv->is_avcC = TRUE;
  status = GST_VAAPI_DECODER_STATUS_SUCCESS;

cleanup:
  {
    gst_vaapi_parser_info_h264_replace (&pi, NULL);
    return status;
  }
}

static GstVaapiDecoderStatus
ensure_decoder (GstVaapiDecoderH264 * decoder)
{
  GstVaapiDecoderH264Private *const priv = &decoder->priv;
  GstVaapiDecoderStatus status;

  if (!priv->is_opened) {
    priv->is_opened = gst_vaapi_decoder_h264_open (decoder);
    if (!priv->is_opened)
      return GST_VAAPI_DECODER_STATUS_ERROR_UNSUPPORTED_CODEC;

    status =
        gst_vaapi_decoder_decode_codec_data (GST_VAAPI_DECODER_CAST (decoder));
    if (status != GST_VAAPI_DECODER_STATUS_SUCCESS)
      return status;
  }
  return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static GstVaapiDecoderStatus
gst_vaapi_decoder_h264_parse (GstVaapiDecoder * base_decoder,
    GstAdapter * adapter, gboolean at_eos, GstVaapiDecoderUnit * unit)
{
  GstVaapiDecoderH264 *const decoder =
      GST_VAAPI_DECODER_H264_CAST (base_decoder);
  GstVaapiDecoderH264Private *const priv = &decoder->priv;
  GstVaapiParserState *const ps = GST_VAAPI_PARSER_STATE (base_decoder);
  GstVaapiParserInfoH264 *pi;
  GstVaapiDecoderStatus status;
  GstH264ParserResult result;
  guchar *buf;
  guint i, size, buf_size, nalu_size, flags;
  guint32 start_code;
  gint ofs, ofs2;
  gboolean at_au_end = FALSE;

  status = ensure_decoder (decoder);
  if (status != GST_VAAPI_DECODER_STATUS_SUCCESS)
    return status;

  switch (priv->stream_alignment) {
    case GST_VAAPI_STREAM_ALIGN_H264_NALU:
    case GST_VAAPI_STREAM_ALIGN_H264_AU:
      size = gst_adapter_available_fast (adapter);
      break;
    default:
      size = gst_adapter_available (adapter);
      break;
  }

  if (priv->is_avcC) {
    if (size < priv->nal_length_size)
      return GST_VAAPI_DECODER_STATUS_ERROR_NO_DATA;

    buf = (guchar *) & start_code;
    g_assert (priv->nal_length_size <= sizeof (start_code));
    gst_adapter_copy (adapter, buf, 0, priv->nal_length_size);

    nalu_size = 0;
    for (i = 0; i < priv->nal_length_size; i++)
      nalu_size = (nalu_size << 8) | buf[i];

    buf_size = priv->nal_length_size + nalu_size;
    if (size < buf_size)
      return GST_VAAPI_DECODER_STATUS_ERROR_NO_DATA;
    else if (priv->stream_alignment == GST_VAAPI_STREAM_ALIGN_H264_AU)
      at_au_end = (buf_size == size);
  } else {
    if (size < 4)
      return GST_VAAPI_DECODER_STATUS_ERROR_NO_DATA;

    if (priv->stream_alignment == GST_VAAPI_STREAM_ALIGN_H264_NALU) {
      buf_size = size;
      ofs = scan_for_start_code (adapter, 4, size - 4, NULL);
      if (ofs > 0)
        buf_size = ofs;
    } else {
      ofs = scan_for_start_code (adapter, 0, size, NULL);
      if (ofs < 0)
        return GST_VAAPI_DECODER_STATUS_ERROR_NO_DATA;

      if (ofs > 0) {
        gst_adapter_flush (adapter, ofs);
        size -= ofs;
      }

      ofs2 = ps->input_offset2 - ofs - 4;
      if (ofs2 < 4)
        ofs2 = 4;

      ofs = G_UNLIKELY (size < ofs2 + 4) ? -1 :
          scan_for_start_code (adapter, ofs2, size - ofs2, NULL);
      if (ofs < 0) {
        // Assume the whole NAL unit is present if end-of-stream
        // or stream buffers aligned on access unit boundaries
        if (priv->stream_alignment == GST_VAAPI_STREAM_ALIGN_H264_AU)
          at_au_end = TRUE;
        else if (!at_eos) {
          ps->input_offset2 = size;
          return GST_VAAPI_DECODER_STATUS_ERROR_NO_DATA;
        }
        ofs = size;
      }
      buf_size = ofs;
    }
  }
  ps->input_offset2 = 0;

  buf = (guchar *) gst_adapter_map (adapter, buf_size);
  if (!buf)
    return GST_VAAPI_DECODER_STATUS_ERROR_NO_DATA;

  unit->size = buf_size;

  pi = gst_vaapi_parser_info_h264_new ();
  if (!pi)
    return GST_VAAPI_DECODER_STATUS_ERROR_ALLOCATION_FAILED;

  gst_vaapi_decoder_unit_set_parsed_info (unit,
      pi, (GDestroyNotify) gst_vaapi_mini_object_unref);

  if (priv->is_avcC)
    result = gst_h264_parser_identify_nalu_avc (priv->parser,
        buf, 0, buf_size, priv->nal_length_size, &pi->nalu);
  else
    result = gst_h264_parser_identify_nalu_unchecked (priv->parser,
        buf, 0, buf_size, &pi->nalu);
  status = get_status (result);
  if (status != GST_VAAPI_DECODER_STATUS_SUCCESS)
    goto exit;

  if (priv->base_only && (pi->nalu.type == GST_H264_NAL_PREFIX_UNIT
          || pi->nalu.type == GST_H264_NAL_SUBSET_SPS
          || pi->nalu.type == GST_H264_NAL_SLICE_EXT)) {
    GST_VAAPI_DECODER_UNIT_FLAG_SET (unit, GST_VAAPI_DECODER_UNIT_FLAG_SKIP);
    pi->nalu.valid = FALSE;
    return GST_VAAPI_DECODER_STATUS_SUCCESS;
  }
  switch (pi->nalu.type) {
    case GST_H264_NAL_SPS:
      status = parse_sps (decoder, unit);
      break;
    case GST_H264_NAL_SUBSET_SPS:
      status = parse_subset_sps (decoder, unit);
      break;
    case GST_H264_NAL_PPS:
      status = parse_pps (decoder, unit);
      break;
    case GST_H264_NAL_SEI:
      status = parse_sei (decoder, unit);
      break;
    case GST_H264_NAL_SLICE_EXT:
      if (!GST_H264_IS_MVC_NALU (&pi->nalu)) {
        status = GST_VAAPI_DECODER_STATUS_SUCCESS;
        break;
      }
      /* fall-through */
    case GST_H264_NAL_SLICE_IDR:
    case GST_H264_NAL_SLICE:
      status = parse_slice (decoder, unit);
      break;
    default:
      status = GST_VAAPI_DECODER_STATUS_SUCCESS;
      break;
  }
  if (status != GST_VAAPI_DECODER_STATUS_SUCCESS)
    goto exit;

  flags = 0;
  if (at_au_end) {
    flags |= GST_VAAPI_DECODER_UNIT_FLAG_FRAME_END |
        GST_VAAPI_DECODER_UNIT_FLAG_AU_END;
  }
  switch (pi->nalu.type) {
    case GST_H264_NAL_AU_DELIMITER:
      flags |= GST_VAAPI_DECODER_UNIT_FLAG_AU_START;
      flags |= GST_VAAPI_DECODER_UNIT_FLAG_FRAME_START;
      /* fall-through */
    case GST_H264_NAL_FILLER_DATA:
      flags |= GST_VAAPI_DECODER_UNIT_FLAG_SKIP;
      break;
    case GST_H264_NAL_STREAM_END:
      flags |= GST_VAAPI_DECODER_UNIT_FLAG_STREAM_END;
      /* fall-through */
    case GST_H264_NAL_SEQ_END:
      flags |= GST_VAAPI_DECODER_UNIT_FLAG_FRAME_END;
      flags |= GST_VAAPI_DECODER_UNIT_FLAG_AU_END;
      break;
    case GST_H264_NAL_SPS:
    case GST_H264_NAL_SUBSET_SPS:
    case GST_H264_NAL_PPS:
    case GST_H264_NAL_SEI:
      flags |= GST_VAAPI_DECODER_UNIT_FLAG_AU_START;
      flags |= GST_VAAPI_DECODER_UNIT_FLAG_FRAME_START;
      break;
    case GST_H264_NAL_SLICE_EXT:
      if (!GST_H264_IS_MVC_NALU (&pi->nalu)) {
        flags |= GST_VAAPI_DECODER_UNIT_FLAG_SKIP;
        break;
      }
      /* fall-through */
    case GST_H264_NAL_SLICE_IDR:
    case GST_H264_NAL_SLICE:
      flags |= GST_VAAPI_DECODER_UNIT_FLAG_SLICE;
      if (priv->prev_pi &&
          (priv->prev_pi->flags & GST_VAAPI_DECODER_UNIT_FLAG_AU_END)) {
        flags |= GST_VAAPI_DECODER_UNIT_FLAG_AU_START |
            GST_VAAPI_DECODER_UNIT_FLAG_FRAME_START;
      } else if (is_new_picture (pi, priv->prev_slice_pi)) {
        flags |= GST_VAAPI_DECODER_UNIT_FLAG_FRAME_START;
        if (is_new_access_unit (pi, priv->prev_slice_pi))
          flags |= GST_VAAPI_DECODER_UNIT_FLAG_AU_START;
      }
      gst_vaapi_parser_info_h264_replace (&priv->prev_slice_pi, pi);
      break;
    case GST_H264_NAL_SPS_EXT:
    case GST_H264_NAL_SLICE_AUX:
      /* skip SPS extension and auxiliary slice for now */
      flags |= GST_VAAPI_DECODER_UNIT_FLAG_SKIP;
      break;
    case GST_H264_NAL_PREFIX_UNIT:
      /* skip Prefix NAL units for now */
      flags |= GST_VAAPI_DECODER_UNIT_FLAG_SKIP |
          GST_VAAPI_DECODER_UNIT_FLAG_AU_START |
          GST_VAAPI_DECODER_UNIT_FLAG_FRAME_START;
      break;
    default:
      if (pi->nalu.type >= 14 && pi->nalu.type <= 18)
        flags |= GST_VAAPI_DECODER_UNIT_FLAG_AU_START |
            GST_VAAPI_DECODER_UNIT_FLAG_FRAME_START;
      break;
  }
  if ((flags & GST_VAAPI_DECODER_UNIT_FLAGS_AU) && priv->prev_slice_pi)
    priv->prev_slice_pi->flags |= GST_VAAPI_DECODER_UNIT_FLAG_AU_END;
  GST_VAAPI_DECODER_UNIT_FLAG_SET (unit, flags);

  pi->nalu.data = NULL;
  pi->state = priv->parser_state;
  pi->flags = flags;
  gst_vaapi_parser_info_h264_replace (&priv->prev_pi, pi);
  return GST_VAAPI_DECODER_STATUS_SUCCESS;

exit:
  {
    gst_adapter_flush (adapter, unit->size);
    gst_vaapi_parser_info_h264_unref (pi);
    return status;
  }
}

static GstVaapiDecoderStatus
gst_vaapi_decoder_h264_decode (GstVaapiDecoder * base_decoder,
    GstVaapiDecoderUnit * unit)
{
  GstVaapiDecoderH264 *const decoder =
      GST_VAAPI_DECODER_H264_CAST (base_decoder);
  GstVaapiDecoderStatus status;

  status = ensure_decoder (decoder);
  if (status != GST_VAAPI_DECODER_STATUS_SUCCESS)
    return status;
  return decode_unit (decoder, unit);
}

static GstVaapiDecoderStatus
gst_vaapi_decoder_h264_start_frame (GstVaapiDecoder * base_decoder,
    GstVaapiDecoderUnit * unit)
{
  GstVaapiDecoderH264 *const decoder =
      GST_VAAPI_DECODER_H264_CAST (base_decoder);

  return decode_picture (decoder, unit);
}

static GstVaapiDecoderStatus
gst_vaapi_decoder_h264_end_frame (GstVaapiDecoder * base_decoder)
{
  GstVaapiDecoderH264 *const decoder =
      GST_VAAPI_DECODER_H264_CAST (base_decoder);

  return decode_current_picture (decoder);
}

static GstVaapiDecoderStatus
gst_vaapi_decoder_h264_flush (GstVaapiDecoder * base_decoder)
{
  GstVaapiDecoderH264 *const decoder =
      GST_VAAPI_DECODER_H264_CAST (base_decoder);

  dpb_flush (decoder, NULL);
  return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static void
gst_vaapi_decoder_h264_finalize (GObject * object)
{
  GstVaapiDecoder *const base_decoder = GST_VAAPI_DECODER (object);

  gst_vaapi_decoder_h264_destroy (base_decoder);
  G_OBJECT_CLASS (gst_vaapi_decoder_h264_parent_class)->finalize (object);
}

static void
gst_vaapi_decoder_h264_class_init (GstVaapiDecoderH264Class * klass)
{
  GObjectClass *const object_class = G_OBJECT_CLASS (klass);
  GstVaapiDecoderClass *const decoder_class = GST_VAAPI_DECODER_CLASS (klass);

  decoder_class->reset = gst_vaapi_decoder_h264_reset;
  decoder_class->parse = gst_vaapi_decoder_h264_parse;
  decoder_class->decode = gst_vaapi_decoder_h264_decode;
  decoder_class->start_frame = gst_vaapi_decoder_h264_start_frame;
  decoder_class->end_frame = gst_vaapi_decoder_h264_end_frame;
  decoder_class->flush = gst_vaapi_decoder_h264_flush;
  decoder_class->decode_codec_data = gst_vaapi_decoder_h264_decode_codec_data;

  object_class->finalize = gst_vaapi_decoder_h264_finalize;
}

static void
gst_vaapi_decoder_h264_init (GstVaapiDecoderH264 * decoder)
{
  GstVaapiDecoder *const base_decoder = GST_VAAPI_DECODER (decoder);

  gst_vaapi_decoder_h264_create (base_decoder);
}

/**
 * gst_vaapi_decoder_h264_set_alignment:
 * @decoder: a #GstVaapiDecoderH264
 * @alignment: the #GstVaapiStreamAlignH264
 *
 * Specifies how stream buffers are aligned / fed, i.e. the boundaries
 * of each buffer that is supplied to the decoder. This could be no
 * specific alignment, NAL unit boundaries, or access unit boundaries.
 */
void
gst_vaapi_decoder_h264_set_alignment (GstVaapiDecoderH264 * decoder,
    GstVaapiStreamAlignH264 alignment)
{
  g_return_if_fail (decoder != NULL);

  decoder->priv.stream_alignment = alignment;
}

/**
 * gst_vaapi_decoder_h264_set_base_only:
 * @decoder: a #GstVaapiDecoderH264
 * @base_only: %TRUE to force decoding the base view only
 *
 * if @base_only is %TRUE only the base view of MVC or SVC encoded streams
 * is decoded.
 *
 **/
void
gst_vaapi_decoder_h264_set_base_only (GstVaapiDecoderH264 * decoder,
    gboolean base_only)
{
  g_return_if_fail (decoder != NULL);

  decoder->priv.base_only = base_only;
}

/**
 * gst_vaapi_decoder_h264_set_low_latency:
 * @decoder: a #GstVaapiDecoderH264
 * @force_low_latency: %TRUE if force low latency
 *
 * if @force_low_latency is %TRUE the decoded frames are pushed soon
 * as possible, instead of to wait until decoded picture buffer (DPB)
 * release them.
 *
 * This violate the H.264 specification but it is useful for some live
 * sources.
 **/
void
gst_vaapi_decoder_h264_set_low_latency (GstVaapiDecoderH264 * decoder,
    gboolean force_low_latency)
{
  g_return_if_fail (decoder != NULL);

  decoder->priv.force_low_latency = force_low_latency;
}

/**
 * gst_vaapi_decoder_h264_get_low_latency:
 * @decoder: a #GstVaapiDecoderH264
 *
 * Returns: %TRUE if the low latency mode is enabled; otherwise
 * %FALSE.
 **/
gboolean
gst_vaapi_decoder_h264_get_low_latency (GstVaapiDecoderH264 * decoder)
{
  g_return_val_if_fail (decoder != NULL, FALSE);

  return decoder->priv.force_low_latency;
}

/**
 * gst_vaapi_decoder_h264_new:
 * @display: a #GstVaapiDisplay
 * @caps: a #GstCaps holding codec information
 *
 * Creates a new #GstVaapiDecoder for MPEG-2 decoding.  The @caps can
 * hold extra information like codec-data and pictured coded size.
 *
 * Return value: the newly allocated #GstVaapiDecoder object
 */
GstVaapiDecoder *
gst_vaapi_decoder_h264_new (GstVaapiDisplay * display, GstCaps * caps)
{
  return g_object_new (GST_TYPE_VAAPI_DECODER_H264, "display", display,
      "caps", caps, NULL);
}
