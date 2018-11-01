/* GStreamer
 * Copyright (C) 2008 Wim Taymans <wim.taymans at gmail.com>
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

#ifndef __GST_RTSP_SERVER_H__
#define __GST_RTSP_SERVER_H__

#include <gst/gst.h>

G_BEGIN_DECLS

#include "rtsp-server-prelude.h"
#include "rtsp-server-object.h"
#include "rtsp-session-pool.h"
#include "rtsp-session.h"
#include "rtsp-media.h"
#include "rtsp-stream.h"
#include "rtsp-stream-transport.h"
#include "rtsp-address-pool.h"
#include "rtsp-thread-pool.h"
#include "rtsp-client.h"
#include "rtsp-context.h"
#include "rtsp-server.h"
#include "rtsp-mount-points.h"
#include "rtsp-media-factory.h"
#include "rtsp-permissions.h"
#include "rtsp-auth.h"
#include "rtsp-token.h"
#include "rtsp-session-media.h"
#include "rtsp-sdp.h"
#include "rtsp-media-factory-uri.h"
#include "rtsp-params.h"

#include "rtsp-onvif-client.h"
#include "rtsp-onvif-media-factory.h"
#include "rtsp-onvif-media.h"
#include "rtsp-onvif-server.h"

G_END_DECLS

#endif /* __GST_RTSP_SERVER_H__ */
