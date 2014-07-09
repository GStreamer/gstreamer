/*
 * Copyright (c) 2013, Intel Corporation.
 * Author: Sreerenj Balachandran <sreerenj.balachandran@intel.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for
 * more details.
 *
 * You should have received a copy of the GNU Lesser General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */
#ifndef __GST_OPEN_CODEC_ANALYSER_MPEG_XML__
#define __GST_OPEN_CODEC_ANALYSER_MPEG_XML__

#include <gst/codecparsers/gstmpegvideometa.h>
#include <gst/codecparsers/gstmpegvideoparser.h>
#include <libxml/tree.h>
#include <libxml/encoding.h>
#include <libxml/xmlwriter.h>

typedef struct {
  GstMpegVideoSequenceHdr        *sequencehdr;
  GstMpegVideoSequenceExt        *sequenceext;
  GstMpegVideoSequenceDisplayExt *sequencedispext;
  GstMpegVideoQuantMatrixExt     *quantext;
}Mpeg2Headers;

gboolean
analyzer_create_mpeg2video_frame_xml (GstMpegVideoMeta *mpeg_meta,
                                      gchar *location,
				      gint frame_num,
				      Mpeg2Headers *mpeg2_hdrs);

gboolean
analyzer_create_mpeg2video_frame_hex (GstMpegVideoMeta *mpeg_meta,
                                      gint frame_num, guint *data);
#endif
