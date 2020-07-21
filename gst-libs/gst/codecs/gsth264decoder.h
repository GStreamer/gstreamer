/* GStreamer
 * Copyright (C) 2019 Seungha Yang <seungha.yang@navercorp.com>
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

#ifndef __GST_H264_DECODER_H__
#define __GST_H264_DECODER_H__

#include <gst/codecs/codecs-prelude.h>

#include <gst/video/video.h>
#include <gst/codecparsers/gsth264parser.h>
#include <gst/codecs/gsth264picture.h>

G_BEGIN_DECLS

#define GST_TYPE_H264_DECODER            (gst_h264_decoder_get_type())
#define GST_H264_DECODER(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_H264_DECODER,GstH264Decoder))
#define GST_H264_DECODER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_H264_DECODER,GstH264DecoderClass))
#define GST_H264_DECODER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj),GST_TYPE_H264_DECODER,GstH264DecoderClass))
#define GST_IS_H264_DECODER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_H264_DECODER))
#define GST_IS_H264_DECODER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_H264_DECODER))
#define GST_H264_DECODER_CAST(obj)       ((GstH264Decoder*)obj)

typedef struct _GstH264Decoder GstH264Decoder;
typedef struct _GstH264DecoderClass GstH264DecoderClass;
typedef struct _GstH264DecoderPrivate GstH264DecoderPrivate;

/**
 * GstH264Decoder:
 *
 * The opaque #GstH264Decoder data structure.
 */
struct _GstH264Decoder
{
  /*< private >*/
  GstVideoDecoder parent;

  /*< protected >*/
  GstVideoCodecState * input_state;

  /*< private >*/
  GstH264DecoderPrivate *priv;
  gpointer padding[GST_PADDING_LARGE];
};

/**
 * GstH264DecoderClass:
 * @new_sequence:   Notifies subclass of SPS update
 * @new_picture:    Optional.
 *                  Called whenever new #GstH264Picture is created.
 *                  Subclass can set implementation specific user data
 *                  on the #GstH264Picture via gst_h264_picture_set_user_data()
 * @start_picture:  Optional.
 *                  Called per one #GstH264Picture to notify subclass to prepare
 *                  decoding process for the #GstH264Picture
 * @decode_slice:   Provides per slice data with parsed slice header and
 *                  required raw bitstream for subclass to decode it.
 *                  if gst_h264_decoder_set_process_ref_pic_lists() is called
 *                  with %TRUE by the subclass, @ref_pic_list0 and @ref_pic_list1
 *                  are non-%NULL.
 * @end_picture:    Optional.
 *                  Called per one #GstH264Picture to notify subclass to finish
 *                  decoding process for the #GstH264Picture
 * @output_picture: Called with a #GstH264Picture which is required to be outputted.
 *                  Subclass can retrieve parent #GstVideoCodecFrame by using
 *                  gst_video_decoder_get_frame() with system_frame_number
 *                  and the #GstVideoCodecFrame must be consumed by subclass via
 *                  gst_video_decoder_{finish,drop,release}_frame().
 */
struct _GstH264DecoderClass
{
  GstVideoDecoderClass parent_class;

  gboolean      (*new_sequence)     (GstH264Decoder * decoder,
                                     const GstH264SPS * sps,
                                     gint max_dpb_size);

  /**
   * GstH264Decoder:new_picture:
   * @decoder: a #GstH264Decoder
   * @frame: (transfer none): a #GstVideoCodecFrame
   * @picture: (transfer none): a #GstH264Picture
   */
  gboolean      (*new_picture)      (GstH264Decoder * decoder,
                                     GstVideoCodecFrame * frame,
                                     GstH264Picture * picture);

  gboolean      (*start_picture)    (GstH264Decoder * decoder,
                                     GstH264Picture * picture,
                                     GstH264Slice * slice,
                                     GstH264Dpb * dpb);

  gboolean      (*decode_slice)     (GstH264Decoder * decoder,
                                     GstH264Picture * picture,
                                     GstH264Slice * slice,
                                     GArray * ref_pic_list0,
                                     GArray * ref_pic_list1);

  gboolean      (*end_picture)      (GstH264Decoder * decoder,
                                     GstH264Picture * picture);

  /**
   * GstH264Decoder:output_picture:
   * @decoder: a #GstH264Decoder
   * @frame: (transfer full): a #GstVideoCodecFrame
   * @picture: (transfer full): a #GstH264Picture
   */
  GstFlowReturn (*output_picture)   (GstH264Decoder * decoder,
                                     GstVideoCodecFrame * frame,
                                     GstH264Picture * picture);

  /*< private >*/
  gpointer padding[GST_PADDING_LARGE];
};

G_DEFINE_AUTOPTR_CLEANUP_FUNC(GstH264Decoder, gst_object_unref)

GST_CODECS_API
GType gst_h264_decoder_get_type (void);

GST_CODECS_API
void gst_h264_decoder_set_process_ref_pic_lists (GstH264Decoder * decoder,
                                                 gboolean process);

GST_CODECS_API
GstH264Picture * gst_h264_decoder_get_picture   (GstH264Decoder * decoder,
                                                 guint32 system_frame_number);

G_END_DECLS

#endif /* __GST_H264_DECODER_H__ */
