/* GStreamer
 * Copyright (C) 2026 Victor Jaquez
 *               2026 Netflix Inc.
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

#ifndef G_OS_WIN32
#include <unistd.h>
#endif

G_BEGIN_DECLS

G_GNUC_INTERNAL
void                  gst_va_memories_set_surface         (GstVaDisplay * display,
                                                           guint surface,
                                                           GstMemory * mem[GST_VIDEO_MAX_PLANES],
                                                           guint n_mems);

static inline goffset
gst_va_get_fd_size (gint fd)
{
#ifndef G_OS_WIN32
  return lseek (fd, 0, SEEK_END);
#else
  return 0;
#endif
}

G_END_DECLS
