/* GStreamer IOSurface Library
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

#ifndef __GST_IOSURFACE_H__
#define __GST_IOSURFACE_H__

#ifndef GST_USE_UNSTABLE_API
#warning "The GStreamer IOSurface library is unstable API and may change in future."
#warning "You can define GST_USE_UNSTABLE_API to avoid this warning."
#endif

#include <gst/gst.h>
#include <gst/iosurface/iosurface-prelude.h>

G_BEGIN_DECLS

typedef struct __IOSurface *IOSurfaceRef;

/**
 * GstIOSurfaceMemoryQueryFunction:
 * @mem: a #GstMemory
 * @surface: (out) (transfer none): location for the #IOSurfaceRef
 * @plane: (out): location for the IOSurface plane index represented by @mem
 *
 * Function used by memory implementations to expose IOSurface backing through
 * gst_iosurface_memory_peek_surface().
 *
 * Returning %TRUE requires setting both @surface and @plane. @surface must be
 * a borrowed reference owned by @mem and valid for as long as @mem is alive.
 *
 * Returns: %TRUE if @mem exposes an IOSurface.
 *
 * Since: 1.30
 */
typedef gboolean (*GstIOSurfaceMemoryQueryFunction) (GstMemory * mem,
    IOSurfaceRef * surface, guint * plane);

/**
 * GST_CAPS_FEATURE_MEMORY_IOSURFACE:
 *
 * Name of the caps feature for indicating the use of IOSurface-backed memory.
 *
 * Since: 1.30
 */
#define GST_CAPS_FEATURE_MEMORY_IOSURFACE "memory:IOSurface"

GST_IOSURFACE_API
gboolean        gst_is_iosurface_memory                    (GstMemory * mem);

GST_IOSURFACE_API
gboolean        gst_is_iosurface_buffer                    (GstBuffer * buffer);

GST_IOSURFACE_API
gboolean        gst_iosurface_memory_peek_surface          (GstMemory * mem,
                                                            IOSurfaceRef * surface,
                                                            guint * plane);

GST_IOSURFACE_API
void            gst_iosurface_memory_register_query_function
                                                           (GType allocator_type,
                                                            GstIOSurfaceMemoryQueryFunction query);

G_END_DECLS

#endif /* __GST_IOSURFACE_H__ */
