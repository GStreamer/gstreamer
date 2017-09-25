/* GStreamer
 * Copyright (C) 2017 Sebastian Dröge <sebastian@centricular.com>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include "rtsp-onvif-client.h"
#include "rtsp-onvif-server.h"
#include "rtsp-onvif-media-factory.h"

G_DEFINE_TYPE (GstRTSPOnvifClient, gst_rtsp_onvif_client, GST_TYPE_RTSP_CLIENT);

static gchar *
gst_rtsp_onvif_client_check_requirements (GstRTSPClient * client,
    GstRTSPContext * ctx, gchar ** requirements)
{
  GstRTSPMountPoints *mount_points = NULL;
  GstRTSPMediaFactory *factory = NULL;
  gchar *path = NULL;
  gboolean has_backchannel = FALSE;
  GString *unsupported = g_string_new ("");

  while (*requirements) {
    if (strcmp (*requirements, GST_RTSP_ONVIF_BACKCHANNEL_REQUIREMENT) == 0) {
      has_backchannel = TRUE;
    } else {
      if (unsupported->len)
        g_string_append (unsupported, ", ");
      g_string_append (unsupported, *requirements);
    }
    requirements++;
  }

  if (unsupported->len)
    goto out;

  mount_points = gst_rtsp_client_get_mount_points (client);
  if (!(path = gst_rtsp_mount_points_make_path (mount_points, ctx->uri)))
    goto out;

  if (!(factory = gst_rtsp_mount_points_match (mount_points, path, NULL)))
    goto out;

  if (has_backchannel && !GST_IS_RTSP_ONVIF_MEDIA_FACTORY (factory)) {
    if (unsupported->len)
      g_string_append (unsupported, ", ");
    g_string_append (unsupported, GST_RTSP_ONVIF_BACKCHANNEL_REQUIREMENT);
  } else if (has_backchannel) {
    GstRTSPOnvifMediaFactory *onvif_factory =
        GST_RTSP_ONVIF_MEDIA_FACTORY (factory);

    if (!gst_rtsp_onvif_media_factory_has_backchannel_support (onvif_factory)) {
      if (unsupported->len)
        g_string_append (unsupported, ", ");
      g_string_append (unsupported, GST_RTSP_ONVIF_BACKCHANNEL_REQUIREMENT);
    }
  }

out:
  if (path)
    g_free (path);
  if (factory)
    g_object_unref (factory);
  if (mount_points)
    g_object_unref (mount_points);

  return g_string_free (unsupported, FALSE);
}

static void
gst_rtsp_onvif_client_class_init (GstRTSPOnvifClientClass * klass)
{
  GstRTSPClientClass *client_klass = (GstRTSPClientClass *) klass;

  client_klass->check_requirements = gst_rtsp_onvif_client_check_requirements;
}

static void
gst_rtsp_onvif_client_init (GstRTSPOnvifClient * client)
{
}
