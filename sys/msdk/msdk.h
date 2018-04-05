/* GStreamer Intel MSDK plugin
 * Copyright (c) 2016, Oblong Industries, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef __MSDK_H__
#define __MSDK_H__

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include <gst/gst.h>
#include <gst/video/video.h>

#ifdef HAVE_LIBMFX
#  include <mfx/mfxvideo.h>
#else
#  include "mfxvideo.h"
#endif

G_BEGIN_DECLS

mfxSession msdk_open_session (mfxIMPL impl);
void msdk_close_session (mfxSession session);

gboolean msdk_is_available (void);

mfxFrameSurface1 *msdk_get_free_surface (mfxFrameSurface1 * surfaces,
    guint size);
void msdk_frame_to_surface (GstVideoFrame * frame, mfxFrameSurface1 * surface);

const gchar *msdk_status_to_string (mfxStatus status);

void gst_msdk_set_video_alignment (GstVideoInfo * info,
    GstVideoAlignment * alignment);

/* Conversion from Gstreamer to libmfx */
gint gst_msdk_get_mfx_chroma_from_format (GstVideoFormat format);
gint gst_msdk_get_mfx_fourcc_from_format (GstVideoFormat format);
void gst_msdk_set_mfx_frame_info_from_video_info (mfxFrameInfo * mfx_info,
    GstVideoInfo * info);

gboolean
gst_msdk_is_msdk_buffer (GstBuffer * buf);

mfxFrameSurface1 *
gst_msdk_get_surface_from_buffer (GstBuffer * buf);

G_END_DECLS

#endif /* __MSDK_H__ */
