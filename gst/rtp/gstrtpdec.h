/* GStreamer
 * Copyright (C) <2005> Wim Taymans <wim@fluendo.com>
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

#ifndef __GST_RTPDEC_H__
#define __GST_RTPDEC_H__

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_TYPE_RTPDEC  		(gst_rtpdec_get_type())
#define GST_IS_RTPDEC(obj)  		(G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_RTPDEC))
#define GST_IS_RTPDEC_CLASS(obj) 	(G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_RTPDEC))
#define GST_RTPDEC(obj)  		(G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_RTPDEC, GstRTPDec))
#define GST_RTPDEC_CLASS(klass)  	(G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_RTPDEC, GstRTPDecClass))

typedef struct _GstRTPDec GstRTPDec;
typedef struct _GstRTPDecClass GstRTPDecClass;

struct _GstRTPDec {
  GstElement element;

  GstPad *sink_rtp;
  GstPad *sink_rtcp;
  GstPad *src_rtp;
  GstPad *src_rtcp;
};

struct _GstRTPDecClass {
  GstElementClass parent_class;
};

gboolean gst_rtpdec_plugin_init (GstPlugin * plugin);

GType gst_rtpdec_get_type(void);

G_END_DECLS

#endif /* __GST_RTPDEC_H__ */
