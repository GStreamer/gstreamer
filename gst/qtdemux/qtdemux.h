/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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


#ifndef __GST_QTDEMUX_H__
#define __GST_QTDEMUX_H__

#include <gst/gst.h>
#include <gst/bytestream/bytestream.h>
#include <gst/getbits/getbits.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


#define GST_TYPE_QTDEMUX \
  (gst_qtdemux_get_type())
#define GST_QTDEMUX(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_QTDEMUX,GstQTDemux))
#define GST_QTDEMUX_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_QTDEMUX,GstQTDemux))
#define GST_IS_QTDEMUX(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_QTDEMUX))
#define GST_IS_QTDEMUX_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_QTDEMUX))

#define GST_QTDEMUX_MAX_STREAMS		8

typedef struct _GstQTDemux GstQTDemux;
typedef struct _GstQTDemuxClass GstQTDemuxClass;
typedef struct _QtDemuxStream QtDemuxStream;

struct _GstQTDemux {
  GstElement element;

  /* pads */
  GstPad *sinkpad;

  QtDemuxStream *streams[GST_QTDEMUX_MAX_STREAMS];
  int n_streams;
  int n_video_streams;
  int n_audio_streams;

  GstByteStream *bs;

  GNode *moov_node;
  GNode *moov_node_compressed;

  guint32 timescale;
  guint32 duration;

  int state;

  int offset;

  GstTagList *tag_list;

  /* track stuff */

  guint64 last_ts;
  gboolean need_discont;
  gboolean need_flush;
};

struct _GstQTDemuxClass {
  GstElementClass parent_class;
};

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __GST_QTDEMUX_H__ */
