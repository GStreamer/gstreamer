/*
 * Copyright (C) 2012,2018 Collabora Ltd.
 *   Author: Sebastian Dröge <sebastian.droege@collabora.co.uk>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation
 * version 2.1 of the License.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 *
 */

#ifndef __GST_AMC_CODEC_H__
#define __GST_AMC_CODEC_H__

#include <gst/gst.h>

#include "gstamc-format.h"
#include "gstamcsurfacetexture.h"

G_BEGIN_DECLS

typedef struct _GstAmcBuffer GstAmcBuffer;
typedef struct _GstAmcBufferInfo GstAmcBufferInfo;
typedef struct _GstAmcCodecVTable GstAmcCodecVTable;
typedef struct _GstAmcCodec GstAmcCodec;
typedef struct _GstAmcAImage GstAmcAImage;
typedef struct _GstAmcAImageReader GstAmcAImageReader;
typedef struct AHardwareBuffer AHardwareBuffer;
typedef enum _GstAmcAImageReaderAcquireResult GstAmcAImageReaderAcquireResult;

enum _GstAmcAImageReaderAcquireResult
{
  GST_AMC_AIMAGE_READER_ACQUIRE_OK,
  GST_AMC_AIMAGE_READER_ACQUIRE_TRY_AGAIN,
  GST_AMC_AIMAGE_READER_ACQUIRE_ERROR,
};

struct _GstAmcBuffer {
  guint8 *data;
  gsize size;
};

struct _GstAmcBufferInfo {
  gint flags;
  gint offset;
  gint64 presentation_time_us;
  gint size;
};

struct _GstAmcCodecVTable
{
  void           (* buffer_free)                   (GstAmcBuffer * buffer);

  gboolean       (* buffer_set_position_and_limit) (GstAmcBuffer * buffer,
                                                    GError ** err,
                                                    gint position,
                                                    gint limit);

  GstAmcCodec *  (* create)                        (const gchar * name,
                                                    gboolean is_encoder,
                                                    GError ** err);

  void           (* free)                          (GstAmcCodec * codec);

  gboolean       (* configure)                     (GstAmcCodec * codec,
                                                    GstAmcFormat * format,
                                                    GstAmcSurfaceTexture * surface_texture,
                                                    GError **err);

  GstAmcFormat * (* get_output_format)             (GstAmcCodec * codec,
                                                    GError **err);

  gboolean       (* start)                         (GstAmcCodec * codec,
                                                    GError **err);

  gboolean       (* stop)                          (GstAmcCodec * codec,
                                                    GError **err);

  gboolean       (* flush)                         (GstAmcCodec * codec,
                                                    GError **err);

  gboolean       (* release)                       (GstAmcCodec * codec,
                                                    GError **err);

  gboolean       (* request_key_frame)             (GstAmcCodec * codec,
                                                    GError **err);

  gboolean       (* have_dynamic_bitrate)          (void);

  gboolean       (* set_dynamic_bitrate)           (GstAmcCodec * codec,
                                                    GError **err,
                                                    gint bitrate);

  GstAmcBuffer * (* get_output_buffer)             (GstAmcCodec * codec,
                                                    gint index,
                                                    GError **err);

  GstAmcBuffer * (* get_input_buffer)              (GstAmcCodec * codec,
                                                    gint index,
                                                    GError **err);

  gint           (* dequeue_input_buffer)          (GstAmcCodec * codec,
                                                    gint64 timeoutUs,
                                                    GError **err);

  gint           (* dequeue_output_buffer)         (GstAmcCodec * codec,
                                                    GstAmcBufferInfo *info,
                                                    gint64 timeoutUs,
                                                    GError **err);

  gboolean       (* queue_input_buffer)            (GstAmcCodec * codec,
                                                    gint index,
                                                    const GstAmcBufferInfo *info,
                                                    GError **err);

  gboolean       (* release_output_buffer)         (GstAmcCodec * codec,
                                                    gint index,
                                                    gboolean render,
                                                    GError **err);

  GstAmcSurfaceTexture * (* new_surface_texture)   (GError ** err);

  gboolean       (* have_ahardware_buffer_output)  (void);

  gboolean       (* configure_with_image_reader)   (GstAmcCodec * codec,
                                                    GstAmcFormat * format,
                                                    GstAmcAImageReader * reader,
                                                    GError ** err);

  GstAmcAImageReader * (* new_image_reader)        (gint width,
                                                    gint height,
                                                    guint max_images,
                                                    GError ** err);

  GstAmcAImageReader * (* image_reader_ref)        (GstAmcAImageReader * reader);

  void           (* image_reader_unref)            (GstAmcAImageReader * reader);

  void           (* image_reader_set_flushing)     (GstAmcAImageReader * reader,
                                                    gboolean flushing);

  void           (* image_reader_notify_image_released)
                                                   (GstAmcAImageReader * reader);

  GstAmcAImageReaderAcquireResult
                 (* image_reader_acquire_next)     (GstAmcAImageReader * reader,
                                                    GstAmcAImage ** image,
                                                    gint * acquire_fence_fd,
                                                    GError ** err);

  gboolean       (* image_get_hardware_buffer)     (GstAmcAImage * image,
                                                    AHardwareBuffer ** buffer,
                                                    guint32 * format,
                                                    GError ** err);

  void           (* image_delete_async)            (GstAmcAImage * image,
                                                    gint release_fence_fd);
};

extern GstAmcCodecVTable *gst_amc_codec_vtable;

void gst_amc_buffer_free (GstAmcBuffer * buffer);
gboolean gst_amc_buffer_set_position_and_limit (GstAmcBuffer * buffer, GError ** err,
    gint position, gint limit);

GstAmcCodec * gst_amc_codec_new (const gchar *name, gboolean is_encoder, GError **err);
void gst_amc_codec_free (GstAmcCodec * codec);

gboolean gst_amc_codec_configure (GstAmcCodec * codec, GstAmcFormat * format, GstAmcSurfaceTexture * surface_texture, GError **err);
GstAmcFormat * gst_amc_codec_get_output_format (GstAmcCodec * codec, GError **err);

gboolean gst_amc_codec_start (GstAmcCodec * codec, GError **err);
gboolean gst_amc_codec_stop (GstAmcCodec * codec, GError **err);
gboolean gst_amc_codec_flush (GstAmcCodec * codec, GError **err);
gboolean gst_amc_codec_release (GstAmcCodec * codec, GError **err);
gboolean gst_amc_codec_request_key_frame (GstAmcCodec * codec, GError **err);
gboolean gst_amc_codec_have_dynamic_bitrate (void);
gboolean gst_amc_codec_set_dynamic_bitrate (GstAmcCodec * codec, GError **err, gint bitrate);

GstAmcBuffer * gst_amc_codec_get_output_buffer (GstAmcCodec * codec, gint index, GError **err);
GstAmcBuffer * gst_amc_codec_get_input_buffer (GstAmcCodec * codec, gint index, GError **err);

gint gst_amc_codec_dequeue_input_buffer (GstAmcCodec * codec, gint64 timeoutUs, GError **err);
gint gst_amc_codec_dequeue_output_buffer (GstAmcCodec * codec, GstAmcBufferInfo *info, gint64 timeoutUs, GError **err);

gboolean gst_amc_codec_queue_input_buffer (GstAmcCodec * codec, gint index, const GstAmcBufferInfo *info, GError **err);
gboolean gst_amc_codec_release_output_buffer (GstAmcCodec * codec, gint index, gboolean render, GError **err);

GstAmcSurfaceTexture * gst_amc_codec_new_surface_texture (GError ** err);

gboolean gst_amc_codec_have_ahardware_buffer_output (void);
gboolean gst_amc_codec_configure_with_image_reader (GstAmcCodec * codec,
    GstAmcFormat * format, GstAmcAImageReader * reader, GError ** err);
GstAmcAImageReader * gst_amc_codec_new_image_reader (gint width, gint height,
    guint max_images, GError ** err);
GstAmcAImageReader * gst_amc_image_reader_ref (GstAmcAImageReader * reader);
void gst_amc_image_reader_unref (GstAmcAImageReader * reader);
void gst_amc_image_reader_set_flushing (GstAmcAImageReader * reader,
    gboolean flushing);
void gst_amc_image_reader_notify_image_released (GstAmcAImageReader * reader);
GstAmcAImageReaderAcquireResult gst_amc_image_reader_acquire_next
    (GstAmcAImageReader * reader, GstAmcAImage ** image,
    gint * acquire_fence_fd, GError ** err);
gboolean gst_amc_image_get_hardware_buffer (GstAmcAImage * image,
    AHardwareBuffer ** buffer, guint32 * format, GError ** err);
void gst_amc_image_delete_async (GstAmcAImage * image, gint release_fence_fd);

G_END_DECLS

#endif /* __GST_AMC_CODEC_H__ */
