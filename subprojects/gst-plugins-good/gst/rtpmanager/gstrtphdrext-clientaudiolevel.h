/* GStreamer
 * Copyright (C) <2018> Havard Graff <havard.graff@gmail.com>
 * Copyright (C) <2020-2021> Guillaume Desmottes <guillaume.desmottes@collabora.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more
 */

#ifndef __GST_RTPHDREXT_CLIENT_AUDIO_LEVEL_H__
#define __GST_RTPHDREXT_CLIENT_AUDIO_LEVEL_H__

#include <gst/gst.h>
#include <gst/rtp/gstrtphdrext.h>

G_BEGIN_DECLS

#define GST_TYPE_RTP_HEADER_EXTENSION_CLIENT_AUDIO_LEVEL (gst_rtp_header_extension_client_audio_level_get_type())

G_DECLARE_FINAL_TYPE (GstRTPHeaderExtensionClientAudioLevel, gst_rtp_header_extension_client_audio_level, GST, RTP_HEADER_EXTENSION_CLIENT_AUDIO_LEVEL, GstRTPHeaderExtension)

GST_ELEMENT_REGISTER_DECLARE (rtphdrextclientaudiolevel);

G_END_DECLS

#endif /* __GST_RTPHDREXT_CLIENT_AUDIO_LEVEL_H__ */
