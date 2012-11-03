/* GStreamer
 * Copyright (C) <2007> Leandro Melo de Sales <leandroal@gmail.com>
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifndef __GST_DCCP_CLIENT_SRC_H__
#define __GST_DCCP_CLIENT_SRC_H__

#include <gst/gst.h>
#include <gst/base/gstpushsrc.h>
#include <gst/base/gstbasesrc.h>

G_BEGIN_DECLS

#include "gstdccp_common.h"

#define GST_TYPE_DCCP_CLIENT_SRC \
  (gst_dccp_client_src_get_type())
#define GST_DCCP_CLIENT_SRC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_DCCP_CLIENT_SRC,GstDCCPClientSrc))
#define GST_DCCP_CLIENT_SRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_DCCP_CLIENT_SRC,GstDCCPClientSrcClass))
#define GST_IS_DCCP_CLIENT_SRC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_DCCP_CLIENT_SRC))
#define GST_IS_DCCP_CLIENT_SRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_DCCP_CLIENT_SRC))

typedef struct _GstDCCPClientSrc GstDCCPClientSrc;
typedef struct _GstDCCPClientSrcClass GstDCCPClientSrcClass;

struct _GstDCCPClientSrc {
  GstPushSrc element;

  /* server information */
  int port;
  gchar *host;
  struct sockaddr_in server_sin;

  /* socket */
  int sock_fd;
  gboolean closed;

  GstCaps *caps;
  uint8_t ccid;
};

struct _GstDCCPClientSrcClass {
  GstPushSrcClass parent_class;

  /* signals */
  void (*connected) (GstElement *src, gint fd);
};

GType gst_dccp_client_src_get_type (void);

G_END_DECLS

#endif /* __GST_DCCP_CLIENT_SRC_H__ */
