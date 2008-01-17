/* GStreamer h264 parser
 * Copyright (C) 2005 Michal Benes <michal.benes@itonis.tv>
 *
 * gsth264parse.h:
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


#ifndef __GST_H264_PARSE_H__
#define __GST_H264_PARSE_H__

#include <gst/gst.h>
#include <gst/base/gstadapter.h>

G_BEGIN_DECLS

#define GST_TYPE_H264PARSE \
  (gst_h264_parse_get_type())
#define GST_H264PARSE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_H264PARSE,GstH264Parse))
#define GST_H264PARSE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_H264PARSE,GstH264ParseClass))
#define GST_IS_H264PARSE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_H264PARSE))
#define GST_IS_H264PARSE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_H264PARSE))

typedef struct _GstH264Parse GstH264Parse;
typedef struct _GstH264ParseClass GstH264ParseClass;

typedef struct _GstNalList GstNalList;

struct _GstH264Parse
{
  GstElement element;

  GstPad *sinkpad;
  GstPad *srcpad;

  gboolean split_packetized;

  GstSegment segment;
  gboolean packetized;
  gboolean discont;

  /* gather/decode queues for reverse playback */
  GList *gather;
  GstBuffer *prev;
  GstNalList *decode;
  gint decode_len;
  gboolean have_sps;
  gboolean have_pps;
  gboolean have_i_frame;

  GstAdapter *adapter;
};

struct _GstH264ParseClass
{
  GstElementClass parent_class;
};

GType gst_h264_parse_get_type (void);

G_END_DECLS

#endif /* __GST_H264_PARSE_H__ */
