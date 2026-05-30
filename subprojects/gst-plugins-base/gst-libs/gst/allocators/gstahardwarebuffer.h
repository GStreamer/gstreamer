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
