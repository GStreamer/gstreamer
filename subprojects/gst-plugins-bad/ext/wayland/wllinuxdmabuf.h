/* GStreamer Wayland video sink
 *
 * Copyright (C) 2016 STMicroelectronics SA
 * Copyright (C) 2016 Fabien Dessenne <fabien.dessenne@st.com>
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
 * License along with this library; if not, write to the Free
 * Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 */

#ifndef __GST_WL_LINUX_DMABUF_H__
#define __GST_WL_LINUX_DMABUF_H__

#include "gstwaylandsink.h"

G_BEGIN_DECLS

#ifndef GST_CAPS_FEATURE_MEMORY_DMABUF
#define GST_CAPS_FEATURE_MEMORY_DMABUF "memory:DMABuf"
#endif

struct wl_buffer * gst_wl_linux_dmabuf_construct_wl_buffer (GstBuffer * buf,
    GstWlDisplay * display, const GstVideoInfo * info);

G_END_DECLS

#endif /* __GST_WL_LINUX_DMABUF_H__ */
