/* GStreamer
 * Copyright (C) <2008> Wim Taymans <wim.taymans@gmail.com>
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

#ifndef __GST_RTSP_GOOGLE_H__
#define __GST_RTSP_GOOGLE_H__

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_TYPE_RTSP_GOOGLE  		(gst_rtsp_google_get_type())
#define GST_IS_RTSP_GOOGLE(obj)  	(G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_RTSP_GOOGLE))
#define GST_IS_RTSP_GOOGLE_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_RTSP_GOOGLE))
#define GST_RTSP_GOOGLE(obj)  		(G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_RTSP_GOOGLE, GstRTSPGoogle))
#define GST_RTSP_GOOGLE_CLASS(klass)  	(G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_RTSP_GOOGLE, GstRTSPGoogleClass))

typedef struct _GstRTSPGoogle GstRTSPGoogle;
typedef struct _GstRTSPGoogleClass GstRTSPGoogleClass;

struct _GstRTSPGoogle {
  GstElement  element;

  gboolean active;
};

struct _GstRTSPGoogleClass {
  GstElementClass parent_class;
};

GType gst_rtsp_google_get_type(void);

G_END_DECLS

#endif /* __GST_RTSP_GOOGLE_H__ */
