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

#ifndef __GST_VP9_DECODER_H__
#define __GST_VP9_DECODER_H__

#include <gst/codecs/codecs-prelude.h>

#include <gst/video/video.h>
#include <gst/codecparsers/gstvp9parser.h>
#include <gst/codecs/gstvp9picture.h>

G_BEGIN_DECLS

#define GST_TYPE_VP9_DECODER            (gst_vp9_decoder_get_type())
#define GST_VP9_DECODER(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_VP9_DECODER,GstVp9Decoder))
#define GST_VP9_DECODER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_VP9_DECODER,GstVp9DecoderClass))
#define GST_VP9_DECODER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj),GST_TYPE_VP9_DECODER,GstVp9DecoderClass))
#define GST_IS_VP9_DECODER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_VP9_DECODER))
#define GST_IS_VP9_DECODER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_VP9_DECODER))
#define GST_VP9_DECODER_CAST(obj)       ((GstVP9Decoder*)obj)

typedef struct _GstVp9Decoder GstVp9Decoder;
typedef struct _GstVp9DecoderClass GstVp9DecoderClass;
typedef struct _GstVp9DecoderPrivate GstVp9DecoderPrivate;

/**
 * GstVp9Decoder:
 *
 * The opaque #GstVp9Decoder data structure.
 */
struct _GstVp9Decoder
{
  /*< private >*/
  GstVideoDecoder parent;

  /*< protected >*/
  GstVideoCodecState * input_state;

  /*< private >*/
  GstVp9DecoderPrivate *priv;
  gpointer padding[GST_PADDING_LARGE];
};

/**
 * GstVp9DecoderClass:
 * @new_sequence:      Notifies subclass of SPS update
 * @new_picture:       Optional.
 *                     Called whenever new #GstVp9Picture is created.
 *                     Subclass can set implementation specific user data
 *                     on the #GstVp9Picture via gst_vp9_picture_set_user_data()
 * @duplicate_picture: Duplicate the #GstVp9Picture
 * @start_picture:     Optional.
 *                     Called per one #GstVp9Picture to notify subclass to prepare
 *                     decoding process for the #GstVp9Picture
 * @decode_slice:      Provides per slice data with parsed slice header and
 *                     required raw bitstream for subclass to decode it
 * @end_picture:       Optional.
 *                     Called per one #GstVp9Picture to notify subclass to finish
 *                     decoding process for the #GstVp9Picture
 * @output_picture:    Called with a #GstVp9Picture which is required to be outputted.
 *                     Subclass can retrieve parent #GstVideoCodecFrame by using
 *                     gst_video_decoder_get_frame() with system_frame_number
 *                     and the #GstVideoCodecFrame must be consumed by subclass via
 *                     gst_video_decoder_{finish,drop,release}_frame().
 */
struct _GstVp9DecoderClass
{
  GstVideoDecoderClass parent_class;

  gboolean        (*new_sequence)      (GstVp9Decoder * decoder,
                                        const GstVp9FrameHdr * frame_hdr);

  /**
   * GstVp9Decoder:new_picture:
   * @decoder: a #GstVp9Decoder
   * @frame: (nullable): (transfer none): a #GstVideoCodecFrame
   * @picture: (transfer none): a #GstVp9Picture
   *
   * FIXME 1.20: vp9parse element can splitting super frames,
   * and then we can ensure non-null @frame
   */
  gboolean        (*new_picture)       (GstVp9Decoder * decoder,
                                        GstVideoCodecFrame * frame,
                                        GstVp9Picture * picture);

  GstVp9Picture * (*duplicate_picture) (GstVp9Decoder * decoder,
                                        GstVp9Picture * picture);

  gboolean        (*start_picture)     (GstVp9Decoder * decoder,
                                        GstVp9Picture * picture);

  gboolean        (*decode_picture)    (GstVp9Decoder * decoder,
                                        GstVp9Picture * picture,
                                        GstVp9Dpb * dpb);

  gboolean        (*end_picture)       (GstVp9Decoder * decoder,
                                        GstVp9Picture * picture);

  /**
   * GstVp9Decoder:output_picture:
   * @decoder: a #GstVp9Decoder
   * @frame: (nullable): (transfer full): a #GstVideoCodecFrame
   * @picture: (transfer full): a #GstVp9Picture
   *
   * FIXME 1.20: vp9parse element can splitting super frames,
   * and then we can ensure non-null @frame
   */
  GstFlowReturn   (*output_picture)    (GstVp9Decoder * decoder,
                                        GstVideoCodecFrame * frame,
                                        GstVp9Picture * picture);

  /*< private >*/
  gpointer padding[GST_PADDING_LARGE];
};

G_DEFINE_AUTOPTR_CLEANUP_FUNC(GstVp9Decoder, gst_object_unref)

GST_CODECS_API
GType gst_vp9_decoder_get_type (void);

G_END_DECLS

#endif /* __GST_VP9_DECODER_H__ */
