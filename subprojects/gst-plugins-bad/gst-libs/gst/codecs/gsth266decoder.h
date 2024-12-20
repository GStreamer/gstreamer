/* GStreamer
 * Copyright (C) 2023 He Junyan <junyan.he@intel.com>
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

#pragma once

#include <gst/codecs/codecs-prelude.h>

#include <gst/video/video.h>
#include <gst/codecparsers/gsth266parser.h>
#include <gst/codecs/gsth266picture.h>

G_BEGIN_DECLS

#define GST_TYPE_H266_DECODER            (gst_h266_decoder_get_type())
#define GST_H266_DECODER(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_H266_DECODER,GstH266Decoder))
#define GST_H266_DECODER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_H266_DECODER,GstH266DecoderClass))
#define GST_H266_DECODER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj),GST_TYPE_H266_DECODER,GstH266DecoderClass))
#define GST_IS_H266_DECODER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_H266_DECODER))
#define GST_IS_H266_DECODER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_H266_DECODER))

typedef struct _GstH266Decoder GstH266Decoder;
typedef struct _GstH266DecoderClass GstH266DecoderClass;
typedef struct _GstH266DecoderPrivate GstH266DecoderPrivate;

/**
 * GstH266Decoder:
 *
 * The opaque #GstH266Decoder data structure.
 *
 * Since: 1.26
 */
struct _GstH266Decoder
{
  /*< private > */
  GstVideoDecoder parent;

  /*< protected > */
  GstVideoCodecState *input_state;

  GArray *aps_list[GST_H266_APS_TYPE_MAX];
  /* Do not hold the reference. */
  GstH266Picture *RefPicList[2][GST_H266_MAX_REF_ENTRIES];
  guint NumRefIdxActive[2];
  gint RefPicPocList[2][GST_H266_MAX_REF_ENTRIES];
  gint RefPicLtPocList[2][GST_H266_MAX_REF_ENTRIES];
  gboolean inter_layer_ref[2][GST_H266_MAX_REF_ENTRIES];
  /* For inter layer refs */
  guint RefPicScale[2][GST_H266_MAX_REF_ENTRIES][2];
  gboolean RprConstraintsActiveFlag[2][GST_H266_MAX_REF_ENTRIES];

  /*< private > */
  GstH266DecoderPrivate *priv;
  gpointer padding[GST_PADDING_LARGE];
};

/**
 * GstH266DecoderClass:
 *
 * The opaque #GstH266DecoderClass data structure.
 *
 * Since: 1.26
 */
struct _GstH266DecoderClass
{
  GstVideoDecoderClass parent_class;

  /**
   * GstH266DecoderClass::new_sequence:
   * @decoder: a #GstH266Decoder
   * @sps: a #GstH266SPS
   * @max_dpb_size: the size of dpb including preferred output delay
   *   by subclass reported via get_preferred_output_delay method.
   *
   * Notifies subclass of video sequence update
   *
   * Since: 1.26
   */
  GstFlowReturn (*new_sequence) (GstH266Decoder * decoder,
                                 const GstH266SPS * sps,
                                 gint max_dpb_size);

  /**
   * GstH266DecoderClass::new_picture:
   * @decoder: a #GstH266Decoder
   * @frame: (transfer none): a #GstVideoCodecFrame
   * @picture: (transfer none): a #GstH266Picture
   *
   * Optional. Called whenever new #GstH266Picture is created.
   * Subclass can set implementation specific user data
   * on the #GstH266Picture via gst_h266_picture_set_user_data
   *
   * Since: 1.26
   */
  GstFlowReturn (*new_picture) (GstH266Decoder * decoder,
                                GstVideoCodecFrame * frame,
                                GstH266Picture * picture);

  /**
   * GstH266DecoderClass::start_picture:
   * @decoder: a #GstH266Decoder
   * @picture: (transfer none): a #GstH266Picture
   * @slice: (transfer none): a #GstH266Slice
   * @dpb: (transfer none): a #GstH266Dpb
   *
   * Optional. Called per one #GstH266Picture to notify subclass to prepare
   * decoding process for the #GstH266Picture
   *
   * Since: 1.26
   */
  GstFlowReturn (*start_picture) (GstH266Decoder * decoder,
                                  GstH266Picture * picture,
                                  GstH266Slice * slice,
                                  GstH266Dpb * dpb);

  /**
   * GstH266DecoderClass::decode_slice:
   * @decoder: a #GstH266Decoder
   * @picture: (transfer none): a #GstH266Picture
   * @slice: (transfer none): a #GstH266Slice
   *
   * Provides per slice data with parsed slice header and required raw bitstream
   * for subclass to decode it.
   *
   * Since: 1.26
   */
  GstFlowReturn (*decode_slice) (GstH266Decoder * decoder,
                                 GstH266Picture * picture,
                                 GstH266Slice * slice);

  /**
   * GstH266DecoderClass::end_picture:
   * @decoder: a #GstH266Decoder
   * @picture: (transfer none): a #GstH266Picture
   *
   * Optional. Called per one #GstH266Picture to notify subclass to finish
   * decoding process for the #GstH266Picture
   *
   * Since: 1.26
   */
  GstFlowReturn (*end_picture) (GstH266Decoder * decoder,
                                GstH266Picture * picture);

  /**
   * GstH266DecoderClass:output_picture:
   * @decoder: a #GstH266Decoder
   * @frame: (transfer full): a #GstVideoCodecFrame
   * @picture: (transfer full): a #GstH266Picture
   *
   * Called with a #GstH266Picture which is required to be outputted.
   *
   * Since: 1.26
   */
  GstFlowReturn (*output_picture) (GstH266Decoder * decoder,
                                   GstVideoCodecFrame * frame,
                                   GstH266Picture * picture);

  /**
   * GstH266DecoderClass::get_preferred_output_delay:
   * @decoder: a #GstH266Decoder
   * @live: whether upstream is live or not
   *
   * Optional. Called by baseclass to query whether delaying output is
   * preferred by subclass or not.
   *
   * Returns: the number of perferred delayed output frame
   *
   * Since: 1.26
   */
  guint (*get_preferred_output_delay) (GstH266Decoder * decoder,
                                       gboolean live);

  /*< private > */
  gpointer padding[GST_PADDING_LARGE];
};

G_DEFINE_AUTOPTR_CLEANUP_FUNC (GstH266Decoder, gst_object_unref)

GST_CODECS_API
GType gst_h266_decoder_get_type (void);

G_END_DECLS
