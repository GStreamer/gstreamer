/* GStreamer AHardwareBuffer Library
 * Copyright (C) 2026 Dominique Leroux <dominique.p.leroux@gmail.com>
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

#ifndef __GST_AHARDWAREBUFFER_H__
#define __GST_AHARDWAREBUFFER_H__

#include <gst/gst.h>
#include <gst/allocators/allocators-prelude.h>

G_BEGIN_DECLS

typedef struct AHardwareBuffer AHardwareBuffer;

/**
 * GstAHardwareBufferMemoryQueryFunction:
 * @mem: a #GstMemory
 * @buffer: (out) (transfer none): location for the `AHardwareBuffer`
 *
 * Function used by memory implementations to expose AHardwareBuffer backing
 * through gst_ahardware_buffer_memory_peek_buffer().
 *
 * Returning %TRUE requires setting @buffer. @buffer must be a borrowed
 * reference owned by @mem and valid for as long as @mem is alive.
 *
 * Returns: %TRUE if @mem exposes an AHardwareBuffer.
 *
 * Since: 1.30
 */
typedef gboolean (*GstAHardwareBufferMemoryQueryFunction) (GstMemory * mem,
    AHardwareBuffer ** buffer);

/**
 * GST_CAPS_FEATURE_MEMORY_AHARDWAREBUFFER:
 *
 * Name of the caps feature for indicating the use of AHardwareBuffer-backed
 * memory.
 *
 * Since: 1.30
 */
#define GST_CAPS_FEATURE_MEMORY_AHARDWAREBUFFER "memory:AHardwareBuffer"

/**
 * GstAHardwareBufferFormat:
 * @GST_AHARDWARE_BUFFER_FORMAT_R8G8B8A8_UNORM: R8G8B8A8_UNORM format
 * @GST_AHARDWARE_BUFFER_FORMAT_R8G8B8X8_UNORM: R8G8B8X8_UNORM format
 * @GST_AHARDWARE_BUFFER_FORMAT_R8G8B8_UNORM: R8G8B8_UNORM format
 * @GST_AHARDWARE_BUFFER_FORMAT_R5G6B5_UNORM: R5G6B5_UNORM format
 * @GST_AHARDWARE_BUFFER_FORMAT_R16G16B16A16_FLOAT: R16G16B16A16_FLOAT format
 * @GST_AHARDWARE_BUFFER_FORMAT_BLOB: BLOB format
 * @GST_AHARDWARE_BUFFER_FORMAT_Y8Cb8Cr8_420: Y8Cb8Cr8_420 format
 * @GST_AHARDWARE_BUFFER_FORMAT_R10G10B10A2_UNORM: R10G10B10A2_UNORM format
 * @GST_AHARDWARE_BUFFER_FORMAT_D16_UNORM: D16_UNORM format
 * @GST_AHARDWARE_BUFFER_FORMAT_D24_UNORM: D24_UNORM format
 * @GST_AHARDWARE_BUFFER_FORMAT_D24_UNORM_S8_UINT: D24_UNORM_S8_UINT format
 * @GST_AHARDWARE_BUFFER_FORMAT_D32_FLOAT: D32_FLOAT format
 * @GST_AHARDWARE_BUFFER_FORMAT_D32_FLOAT_S8_UINT: D32_FLOAT_S8_UINT format
 * @GST_AHARDWARE_BUFFER_FORMAT_S8_UINT: S8_UINT format
 * @GST_AHARDWARE_BUFFER_FORMAT_YCbCr_P010: YCbCr_P010 format
 * @GST_AHARDWARE_BUFFER_FORMAT_R8_UNORM: R8_UNORM format
 * @GST_AHARDWARE_BUFFER_FORMAT_R16_UINT: R16_UINT format
 * @GST_AHARDWARE_BUFFER_FORMAT_R16G16_UINT: R16G16_UINT format
 * @GST_AHARDWARE_BUFFER_FORMAT_R10G10B10A10_UNORM: R10G10B10A10_UNORM format
 * @GST_AHARDWARE_BUFFER_FORMAT_YCbCr_P210: YCbCr_P210 format
 * @GST_AHARDWARE_BUFFER_FORMAT_R12_UINT: R12_UINT format
 * @GST_AHARDWARE_BUFFER_FORMAT_R14_UINT: R14_UINT format
 * @GST_AHARDWARE_BUFFER_FORMAT_R12G12_UINT: R12G12_UINT format
 * @GST_AHARDWARE_BUFFER_FORMAT_R14G14_UINT: R14G14_UINT format
 * @GST_AHARDWARE_BUFFER_FORMAT_R12G12B12A12_UINT: R12G12B12A12_UINT format
 * @GST_AHARDWARE_BUFFER_FORMAT_R14G14B14A14_UINT: R14G14B14A14_UINT format
 * @GST_AHARDWARE_BUFFER_FORMAT_B10G10R10A2_UNORM: B10G10R10A2_UNORM format
 * @GST_AHARDWARE_BUFFER_FORMAT_B10G10R10X2_UNORM: B10G10R10X2_UNORM format
 *
 * Android AHardwareBuffer format values. These correspond to the
 * `AHARDWAREBUFFER_FORMAT_*` constants in Android's `hardware_buffer.h`, but
 * are available regardless of which Android SDK version is used to build.
 *
 * Since: 1.30
 */
typedef enum {
  GST_AHARDWARE_BUFFER_FORMAT_R8G8B8A8_UNORM = 0x00000001,
  GST_AHARDWARE_BUFFER_FORMAT_R8G8B8X8_UNORM = 0x00000002,
  GST_AHARDWARE_BUFFER_FORMAT_R8G8B8_UNORM = 0x00000003,
  GST_AHARDWARE_BUFFER_FORMAT_R5G6B5_UNORM = 0x00000004,
  GST_AHARDWARE_BUFFER_FORMAT_R16G16B16A16_FLOAT = 0x00000016,
  GST_AHARDWARE_BUFFER_FORMAT_BLOB = 0x00000021,
  GST_AHARDWARE_BUFFER_FORMAT_Y8Cb8Cr8_420 = 0x00000023,
  GST_AHARDWARE_BUFFER_FORMAT_R10G10B10A2_UNORM = 0x0000002b,
  GST_AHARDWARE_BUFFER_FORMAT_D16_UNORM = 0x00000030,
  GST_AHARDWARE_BUFFER_FORMAT_D24_UNORM = 0x00000031,
  GST_AHARDWARE_BUFFER_FORMAT_D24_UNORM_S8_UINT = 0x00000032,
  GST_AHARDWARE_BUFFER_FORMAT_D32_FLOAT = 0x00000033,
  GST_AHARDWARE_BUFFER_FORMAT_D32_FLOAT_S8_UINT = 0x00000034,
  GST_AHARDWARE_BUFFER_FORMAT_S8_UINT = 0x00000035,
  GST_AHARDWARE_BUFFER_FORMAT_YCbCr_P010 = 0x00000036,
  GST_AHARDWARE_BUFFER_FORMAT_R8_UNORM = 0x00000038,
  GST_AHARDWARE_BUFFER_FORMAT_R16_UINT = 0x00000039,
  GST_AHARDWARE_BUFFER_FORMAT_R16G16_UINT = 0x0000003a,
  GST_AHARDWARE_BUFFER_FORMAT_R10G10B10A10_UNORM = 0x0000003b,
  GST_AHARDWARE_BUFFER_FORMAT_YCbCr_P210 = 0x0000003c,
  GST_AHARDWARE_BUFFER_FORMAT_R12_UINT = 0x0000003d,
  GST_AHARDWARE_BUFFER_FORMAT_R14_UINT = 0x0000003e,
  GST_AHARDWARE_BUFFER_FORMAT_R12G12_UINT = 0x0000003f,
  GST_AHARDWARE_BUFFER_FORMAT_R14G14_UINT = 0x00000040,
  GST_AHARDWARE_BUFFER_FORMAT_R12G12B12A12_UINT = 0x00000041,
  GST_AHARDWARE_BUFFER_FORMAT_R14G14B14A14_UINT = 0x00000042,
  GST_AHARDWARE_BUFFER_FORMAT_B10G10R10A2_UNORM = 0x00000043,
  GST_AHARDWARE_BUFFER_FORMAT_B10G10R10X2_UNORM = 0x00000044,
} GstAHardwareBufferFormat;

GST_ALLOCATORS_API
gchar *         gst_ahardware_buffer_format_to_caps_string
                                                           (guint32 format);

GST_ALLOCATORS_API
gboolean        gst_ahardware_buffer_format_from_caps_string
                                                           (const gchar * value,
                                                            guint32 * format);

GST_ALLOCATORS_API
gboolean        gst_is_ahardware_buffer_memory
                                                           (GstMemory * mem);

GST_ALLOCATORS_API
gboolean        gst_is_ahardware_buffer_buffer
                                                           (GstBuffer * buffer);

GST_ALLOCATORS_API
gboolean        gst_ahardware_buffer_memory_peek_buffer
                                                           (GstMemory * mem,
                                                            AHardwareBuffer ** buffer);

GST_ALLOCATORS_API
void            gst_ahardware_buffer_memory_register_query_function
                                                           (GType allocator_type,
                                                            GstAHardwareBufferMemoryQueryFunction query);

G_END_DECLS

#endif /* __GST_AHARDWAREBUFFER_H__ */
