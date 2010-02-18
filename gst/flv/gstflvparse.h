/* GStreamer
 * Copyright (C) <2007> Julien Moutte <julien@moutte.net>
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifndef __FLV_PARSE_H__
#define __FLV_PARSE_H__

#include "gstflvdemux.h"

G_BEGIN_DECLS


GstFlowReturn gst_flv_parse_tag_script (GstFLVDemux * demux,
    GstBuffer *buffer);

GstFlowReturn gst_flv_parse_tag_audio (GstFLVDemux * demux, GstBuffer *buffer);

GstFlowReturn gst_flv_parse_tag_video (GstFLVDemux * demux, GstBuffer *buffer);

GstFlowReturn gst_flv_parse_tag_type (GstFLVDemux * demux, GstBuffer *buffer);

GstFlowReturn gst_flv_parse_header (GstFLVDemux * demux, GstBuffer *buffer);

GstClockTime gst_flv_parse_tag_timestamp (GstFLVDemux *demux, gboolean index,
                                          GstBuffer *buffer, size_t *tag_data_size);

G_END_DECLS
#endif /* __FLV_PARSE_H__ */
