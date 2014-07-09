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

#ifndef __GST_ANALYZER_SINK_H__
#define __GST_ANALYZER_SINK_H__

#include <gst/gst.h>
#include <gst/base/gstbasesink.h>
#include <gst/codecparsers/gstmpegvideoparser.h>
#include <gst/codecparsers/gstmpegvideometa.h>
#include "mpeg_xml.h"

G_BEGIN_DECLS

#define GST_TYPE_ANALYZER_SINK \
  (gst_analyzer_sink_get_type())
#define GST_ANALYZER_SINK(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_ANALYZER_SINK,GstAnalyzerSink))
#define GST_ANALYZER_SINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_ANALYZER_SINK,GstAnalyzerSinkClass))
#define GST_IS_ANALYZER_SINK(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_ANALYZER_SINK))
#define GST_IS_ANALYZER_SINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_ANALYZER_SINK))
#define GST_ANALYZER_SINK_CAST(obj) ((GstAnalyzerSink *)obj)

typedef enum {
  GST_ANALYZER_CODEC_UNKNOWN = 0,
  GST_ANALYZER_CODEC_MPEG2_VIDEO = 1,
  GST_ANALYZER_CODEC_H264 = 2,
  GST_ANALYZER_CODEC_VC1 = 3,
  GST_ANALYZER_CODEC_MPEG4_PART_TWO = 4,
  GST_ANALYZER_CODEC_H265 = 5,
  GST_ANALYZER_CODEC_VP8 = 6,
  GST_ANALYZER_CODEC_VP9 = 7
} GstAnalyzerCodecType;

typedef struct _GstAnalyzerSink GstAnalyzerSink;
typedef struct _GstAnalyzerSinkClass GstAnalyzerSinkClass;

/**
 * GstAnalyzerSink:
 *
 * The opaque #GstAnalyzerSink data structure.
 */
struct _GstAnalyzerSink {
  GstBaseSink		element;

  gboolean		dump;
  gint                  num_buffers;
  gint                  num_buffers_left;
  gint			frame_num;
  gchar*		location;

  GstAnalyzerCodecType  codec_type;

  /* codec specific headers */
  Mpeg2Headers *mpeg2_hdrs;
};

struct _GstAnalyzerSinkClass {
  GstBaseSinkClass parent_class;

  /* signals */
  void (*new_frame) (GstElement *element, GstBuffer *buf, gint frame_num);
};

G_GNUC_INTERNAL GType gst_analyzer_sink_get_type (void);

G_END_DECLS

#endif /* __GST_ANALYZER_SINK_H__ */
