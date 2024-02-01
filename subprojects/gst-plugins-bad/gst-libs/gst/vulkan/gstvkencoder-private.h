/*
 * GStreamer
 * Copyright (C) 2023 Igalia, S.L.
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

#include <gst/vulkan/vulkan.h>

#define GST_TYPE_VULKAN_ENCODER         (gst_vulkan_encoder_get_type())
#define GST_VULKAN_ENCODER(o)           (G_TYPE_CHECK_INSTANCE_CAST((o), GST_TYPE_VULKAN_ENCODER, GstVulkanEncoder))
#define GST_VULKAN_ENCODER_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), GST_TYPE_VULKAN_ENCODER, GstVulkanEncoderClass))
#define GST_IS_VULKAN_ENCODER(o)        (G_TYPE_CHECK_INSTANCE_TYPE((o), GST_TYPE_VULKAN_ENCODER))
#define GST_IS_VULKAN_ENCODER_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE((k), GST_TYPE_VULKAN_ENCODER))
#define GST_VULKAN_ENCODER_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS((o), GST_TYPE_VULKAN_ENCODER, GstVulkanEncoderClass))
GST_VULKAN_API
GType gst_vulkan_encoder_get_type       (void);

typedef struct _GstVulkanEncoder GstVulkanEncoder;
typedef struct _GstVulkanEncoderClass GstVulkanEncoderClass;
typedef union _GstVulkanEncoderParameters GstVulkanEncoderParameters;
typedef union _GstVulkanEncoderParametersOverrides GstVulkanEncoderParametersOverrides;
typedef union _GstVulkanEncoderParametersFeedback GstVulkanEncoderParametersFeedback;
typedef struct _GstVulkanEncodePicture GstVulkanEncodePicture;

/**
 * GstVulkanEncodePicture:
 * @is_ref: picture is reference
 * @nb_refs: number of references
 * @slotIndex: slot index
 * @packed_headers: packed headers
 * @pic_num: picture number
 * @pic_order_cnt: order count
 * @width: picture width
 * @height: picture height
 * @fps_n: fps numerator
 * @fps_d: fps denominator
 * @in_buffer: input buffer
 * @out_buffer: output buffer
 *
 * It contains the whole state for encoding a single picture.
 *
 * Since: 1.24
 */
struct _GstVulkanEncodePicture
{
  gboolean is_ref;
  gint nb_refs;
  gint slotIndex;

  /* picture parameters */
  GPtrArray *packed_headers;

  gint pic_num;
  gint pic_order_cnt;

  gint width;
  gint height;

  gint fps_n;
  gint fps_d;

  GstBuffer *in_buffer;
  GstBuffer *dpb_buffer;
  GstBuffer *out_buffer;

  /* Input frame */
  GstVulkanImageView *img_view;
  GstVulkanImageView *dpb_view;

  VkVideoPictureResourceInfoKHR dpb;

  void              *codec_rc_info;
  void              *codec_pic_info;
  void              *codec_rc_layer_info;
  void              *codec_dpb_slot_info;
  void              *codec_quality_level;
};

/**
 * GstVulkanEncoder:
 * @parent: the parent #GstObject
 * @queue: the #GstVulkanQueue to command buffers will be allocated from
 *
 * Since: 1.24
 **/
struct _GstVulkanEncoder
{
  GstObject parent;

  GstVulkanQueue *queue;

  guint codec;

  /* <private> */
  gpointer _reserved        [GST_PADDING];
};

/**
 * GstVulkanEncoderClass:
 * @parent_class: the parent #GstObjectClass
 *
 * Since: 1.24
 */
struct _GstVulkanEncoderClass
{
  GstObjectClass parent;
  /* <private> */
  gpointer _reserved        [GST_PADDING];
};

union _GstVulkanEncoderParameters
{
  /*< private >*/
  VkVideoEncodeH264SessionParametersCreateInfoKHR h264;
  VkVideoEncodeH265SessionParametersCreateInfoKHR h265;
};

union _GstVulkanEncoderParametersOverrides
{
  /*< private >*/
  VkVideoEncodeH264SessionParametersGetInfoKHR h264;
  VkVideoEncodeH265SessionParametersGetInfoKHR h265;
};

union _GstVulkanEncoderParametersFeedback
{
  VkVideoEncodeH264SessionParametersFeedbackInfoKHR h264;
  VkVideoEncodeH265SessionParametersFeedbackInfoKHR h265;
};

G_DEFINE_AUTOPTR_CLEANUP_FUNC (GstVulkanEncoder, gst_object_unref)

GST_VULKAN_API
GstVulkanEncoder *      gst_vulkan_encoder_create_from_queue    (GstVulkanQueue * queue,
                                                                 guint codec);

GST_VULKAN_API
gboolean                gst_vulkan_encoder_start                (GstVulkanEncoder * self,
                                                                 GstVulkanVideoProfile * profile,
                                                                 guint32 out_buffer_size,
                                                                 GError ** error);
GST_VULKAN_API
gboolean                gst_vulkan_encoder_stop                 (GstVulkanEncoder * self);
GST_VULKAN_API
gboolean                gst_vulkan_encoder_update_video_session_parameters
                                                                (GstVulkanEncoder * self,
                                                                 GstVulkanEncoderParameters *enc_params,
                                                                 GError ** error);
GST_VULKAN_API
gboolean                gst_vulkan_encoder_video_session_parameters_overrides
                                                                (GstVulkanEncoder * self,
                                                                 GstVulkanEncoderParametersOverrides * params,
                                                                 GstVulkanEncoderParametersFeedback * feedback,
                                                                 gsize * data_size,
                                                                 gpointer * data,
                                                                 GError ** error);
GST_VULKAN_API
gboolean                gst_vulkan_encoder_create_dpb_pool      (GstVulkanEncoder * self,
                                                                 GstCaps * caps);
GST_VULKAN_API
gboolean                gst_vulkan_encoder_encode               (GstVulkanEncoder * self,
                                                                 GstVulkanEncodePicture * pic,
                                                                 GstVulkanEncodePicture ** ref_pics);
GST_VULKAN_API
gboolean                gst_vulkan_encoder_caps              (GstVulkanEncoder * self,
                                                                 GstVulkanVideoCapabilities * caps);
GST_VULKAN_API
GstCaps *               gst_vulkan_encoder_profile_caps         (GstVulkanEncoder * self);
GST_VULKAN_API
GstVulkanEncodePicture * gst_vulkan_encode_picture_new          (GstVulkanEncoder * self,
                                                                 GstBuffer * in_buffer,
                                                                 gint width,
                                                                 gint height,
                                                                 gboolean is_ref,
                                                                 gint nb_refs);
GST_VULKAN_API
void                     gst_vulkan_encode_picture_free         (GstVulkanEncodePicture * pic);
