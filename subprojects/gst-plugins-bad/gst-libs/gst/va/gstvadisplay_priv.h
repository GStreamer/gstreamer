/* GStreamer
 * Copyright (C) 2026 Netflix Inc.
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

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/va/gstvadisplay.h>

#include "vasurfacebuffer.h"

G_BEGIN_DECLS

G_GNUC_INTERNAL
void                  gst_va_display_set_buffer_surface   (GstVaDisplay * self,
                                                           GstVaBufferSurface * buf);
G_GNUC_INTERNAL
GstVaBufferSurface *  gst_va_display_peek_buffer_surface  (GstVaDisplay * self,
                                                           GstMemory * mem);

G_GNUC_INTERNAL
void                  gst_va_display_set_aux_surface      (GstVaDisplay * self,
                                                           GstMemory * mem,
                                                           GstVaBufferSurface * buf);
G_GNUC_INTERNAL
GstVaBufferSurface *  gst_va_display_peek_aux_surface     (GstVaDisplay * self,
                                                           GstMemory * mem);

G_GNUC_INTERNAL
gboolean              gst_va_display_setup_dmabuf_memories (GstVaDisplay * self,
                                                            GstVideoInfoDmaDrm * drm_info,
                                                            GstMemory * mem[GST_VIDEO_MAX_PLANES],
                                                            uintptr_t fds[GST_VIDEO_MAX_PLANES],
                                                            gsize offset[GST_VIDEO_MAX_PLANES],
                                                            guint usage_hint);

G_END_DECLS
