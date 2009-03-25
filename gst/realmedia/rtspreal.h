/* GStreamer
 * Copyright (C) <2005,2006> Wim Taymans <wim@fluendo.com>
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

#ifndef __GST_RTSP_REAL_H__
#define __GST_RTSP_REAL_H__

#include <gst/gst.h>

#include "asmrules.h"

G_BEGIN_DECLS

#define GST_TYPE_RTSP_REAL  		(gst_rtsp_real_get_type())
#define GST_IS_RTSP_REAL(obj)  		(G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_RTSP_REAL))
#define GST_IS_RTSP_REAL_CLASS(klass) 	(G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_RTSP_REAL))
#define GST_RTSP_REAL(obj)  		(G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_RTSP_REAL, GstRTSPReal))
#define GST_RTSP_REAL_CLASS(klass)  	(G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_RTSP_REAL, GstRTSPRealClass))

typedef struct _GstRTSPReal GstRTSPReal;
typedef struct _GstRTSPRealClass GstRTSPRealClass;

typedef struct _GstRTSPRealStream GstRTSPRealStream;

struct _GstRTSPRealStream {
  guint  id;
  guint  max_bit_rate;
  guint  avg_bit_rate;
  guint  max_packet_size;
  guint  avg_packet_size;
  guint  start_time;
  guint  preroll;
  guint  duration;
  gchar *stream_name;
  guint  stream_name_len;
  gchar *mime_type;
  guint  mime_type_len;

  GstASMRuleBook *rulebook;

  gchar *type_specific_data;
  guint  type_specific_data_len;

  guint16 num_rules, j, sel, codec;
};

struct _GstRTSPReal {
  GstElement  element;

  gchar checksum[34];
  gchar challenge2[64];
  gchar etag[64];
  gboolean isreal;

  guint   n_streams;
  GList  *streams;

  guint  max_bit_rate;
  guint  avg_bit_rate;
  guint  max_packet_size;
  guint  avg_packet_size;
  guint  duration;

  gchar *rules;
};

struct _GstRTSPRealClass {
  GstElementClass parent_class;
};

GType gst_rtsp_real_get_type(void);

gboolean gst_rtsp_real_plugin_init (GstPlugin * plugin);

G_END_DECLS

#endif /* __GST_RTSP_REAL_H__ */
