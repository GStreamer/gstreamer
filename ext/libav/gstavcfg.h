/* GStreamer
 * Copyright (C) <2006> Mark Nauwelaerts <manauw@skynet.be>
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


#ifndef __GST_FFMPEGCFG_H__
#define __GST_FFMPEGCFG_H__

#include <glib-object.h>
#include <libavcodec/avcodec.h>

G_BEGIN_DECLS

void gst_ffmpeg_cfg_init (void);

void gst_ffmpeg_cfg_install_properties (GObjectClass * klass, AVCodec *in_plugin, guint base, gint flags);

gboolean gst_ffmpeg_cfg_set_property (AVCodecContext *refcontext,
    const GValue * value, GParamSpec * pspec);

gboolean gst_ffmpeg_cfg_get_property (AVCodecContext *refcontext,
    GValue * value, GParamSpec * pspec);

void gst_ffmpeg_cfg_fill_context (GObject *object, AVCodecContext * context);
void gst_ffmpeg_cfg_finalize (void);

G_END_DECLS


#endif /* __GST_FFMPEGCFG_H__ */
