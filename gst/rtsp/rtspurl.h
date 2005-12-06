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

#ifndef __RTSP_URL_H__
#define __RTSP_URL_H__

#include <glib.h>

#include <rtspdefs.h>

G_BEGIN_DECLS

#define RTSP_DEFAULT_PORT       554

typedef struct _RTSPUrl {
  RTSPProto   protocol;
  RTSPFamily  family;
  gchar      *user;
  gchar      *passwd;
  gchar      *host;
  guint16     port;
  gchar      *abspath;
} RTSPUrl;

RTSPResult      rtsp_url_parse          (const gchar *urlstr, RTSPUrl **url);
void            rtsp_url_free           (RTSPUrl *url);

G_END_DECLS

#endif /* __RTSP_URL_H__ */
