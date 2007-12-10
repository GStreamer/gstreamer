/* GStreamer
 * Copyright (C) <2007> Wim Taymans <wim.taymans@gmail.com>
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

#ifndef __GST_RTP_CLIENT_H__
#define __GST_RTP_CLIENT_H__

#include <gst/gst.h>

#define GST_TYPE_RTP_CLIENT \
  (gst_rtp_client_get_type())
#define GST_RTP_CLIENT(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_RTP_CLIENT,GstRtpClient))
#define GST_RTP_CLIENT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_RTP_CLIENT,GstRtpClientClass))
#define GST_IS_RTP_CLIENT(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_RTP_CLIENT))
#define GST_IS_RTP_CLIENT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_RTP_CLIENT))

typedef struct _GstRtpClient GstRtpClient;
typedef struct _GstRtpClientClass GstRtpClientClass;
typedef struct _GstRtpClientPrivate GstRtpClientPrivate;

struct _GstRtpClient {
  GstBin         parent_bin;

  /* a list of streams from a client */
  GList         *streams;

  /*< private >*/
  GstRtpClientPrivate *priv;
};

struct _GstRtpClientClass {
  GstBinClass   parent_class;
};

GType gst_rtp_client_get_type (void);

#endif /* __GST_RTP_CLIENT_H__ */
