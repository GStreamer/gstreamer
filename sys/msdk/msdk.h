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
#include <gst/allocators/allocators.h>

#include <mfxvideo.h>
#include <mfxplugin.h>

G_BEGIN_DECLS

#define GST_MSDK_CAPS_MAKE(format) \
  GST_VIDEO_CAPS_MAKE (format) ", " \
  "interlace-mode = (string) progressive"

#ifndef _WIN32
#define GST_MSDK_CAPS_MAKE_WITH_DMABUF_FEATURE(dmaformat) \
  GST_VIDEO_CAPS_MAKE_WITH_FEATURES(GST_CAPS_FEATURE_MEMORY_DMABUF, dmaformat) ", " \
  "interlace-mode = (string) progressive"
#else
#define GST_MSDK_CAPS_MAKE_WITH_DMABUF_FEATURE(dmaformat) ""
#endif

#define GST_MSDK_CAPS_STR(format,dmaformat) \
  GST_MSDK_CAPS_MAKE (format) "; " \
  GST_MSDK_CAPS_MAKE_WITH_DMABUF_FEATURE (dmaformat)

mfxSession msdk_open_session (mfxIMPL impl);
void msdk_close_session (mfxSession session);

gboolean msdk_is_available (void);

mfxFrameSurface1 *msdk_get_free_surface (mfxFrameSurface1 * surfaces,
    guint size);
void msdk_frame_to_surface (GstVideoFrame * frame, mfxFrameSurface1 * surface);

const gchar *msdk_status_to_string (mfxStatus status);

void gst_msdk_set_video_alignment (GstVideoInfo * info, guint alloc_w, guint alloc_h,
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

GstVideoFormat
gst_msdk_get_video_format_from_mfx_fourcc (mfxU32 fourcc);

void
gst_msdk_update_mfx_frame_info_from_mfx_video_param (mfxFrameInfo * mfx_info,
    mfxVideoParam * param);

void
gst_msdk_get_mfx_video_orientation_from_video_direction (guint value,
    guint * mfx_mirror, guint * mfx_rotation);

gboolean
gst_msdk_load_plugin (mfxSession session, const mfxPluginUID * uid,
    mfxU32 version, const gchar * plugin);

mfxU16
msdk_get_platform_codename (mfxSession session);

G_END_DECLS

#endif /* __MSDK_H__ */
