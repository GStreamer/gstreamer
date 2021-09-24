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
  gboolean has_replay = FALSE;
  GString *unsupported = g_string_new ("");

  while (*requirements) {
    if (strcmp (*requirements, GST_RTSP_ONVIF_BACKCHANNEL_REQUIREMENT) == 0) {
      has_backchannel = TRUE;
    } else if (strcmp (*requirements, GST_RTSP_ONVIF_REPLAY_REQUIREMENT) == 0) {
      has_replay = TRUE;
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

  if (has_replay && !GST_IS_RTSP_ONVIF_MEDIA_FACTORY (factory)) {
    if (unsupported->len)
      g_string_append (unsupported, ", ");
    g_string_append (unsupported, GST_RTSP_ONVIF_REPLAY_REQUIREMENT);
  } else if (has_replay) {
    GstRTSPOnvifMediaFactory *onvif_factory =
        GST_RTSP_ONVIF_MEDIA_FACTORY (factory);

    if (!gst_rtsp_onvif_media_factory_has_replay_support (onvif_factory)) {
      if (unsupported->len)
        g_string_append (unsupported, ", ");
      g_string_append (unsupported, GST_RTSP_ONVIF_REPLAY_REQUIREMENT);
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

static GstRTSPStatusCode
gst_rtsp_onvif_client_adjust_play_mode (GstRTSPClient * client,
    GstRTSPContext * ctx, GstRTSPTimeRange ** range, GstSeekFlags * flags,
    gdouble * rate, GstClockTime * trickmode_interval,
    gboolean * enable_rate_control)
{
  GstRTSPStatusCode ret = GST_RTSP_STS_BAD_REQUEST;
  gchar **split = NULL;
  gchar *str;

  if (gst_rtsp_message_get_header (ctx->request, GST_RTSP_HDR_FRAMES,
          &str, 0) == GST_RTSP_OK) {

    split = g_strsplit (str, "/", 2);

    if (!g_strcmp0 (split[0], "intra")) {
      if (split[1]) {
        guint64 interval;
        gchar *end;

        interval = g_ascii_strtoull (split[1], &end, 10);

        if (!end || *end != '\0') {
          GST_ERROR ("Unexpected interval value %s", split[1]);
          goto done;
        }

        *trickmode_interval = interval * GST_MSECOND;
      }
      *flags |= GST_SEEK_FLAG_TRICKMODE_KEY_UNITS;
    } else if (!g_strcmp0 (split[0], "predicted")) {
      if (split[1]) {
        GST_ERROR ("Predicted frames mode does not allow an interval (%s)",
            str);
        goto done;
      }
      *flags |= GST_SEEK_FLAG_TRICKMODE_FORWARD_PREDICTED;
    } else {
      GST_ERROR ("Invalid frames mode (%s)", str);
      goto done;
    }
  }

  if (gst_rtsp_message_get_header (ctx->request, GST_RTSP_HDR_RATE_CONTROL,
          &str, 0) == GST_RTSP_OK) {
    if (!g_strcmp0 (str, "no")) {
      *enable_rate_control = FALSE;
    } else if (!g_strcmp0 (str, "yes")) {
      *enable_rate_control = TRUE;
    } else {
      GST_ERROR ("Invalid rate control header: %s", str);
      goto done;
    }
  }

  ret = GST_RTSP_STS_OK;

done:
  if (split)
    g_strfreev (split);
  return ret;
}

static GstRTSPStatusCode
gst_rtsp_onvif_client_adjust_play_response (GstRTSPClient * client,
    GstRTSPContext * ctx)
{
  GstRTSPStatusCode ret = GST_RTSP_STS_OK;
  gchar *str;

  if (gst_rtsp_message_get_header (ctx->request, GST_RTSP_HDR_RATE_CONTROL,
          &str, 0) == GST_RTSP_OK) {
    gst_rtsp_message_add_header (ctx->response, GST_RTSP_HDR_RATE_CONTROL,
        gst_rtsp_media_get_rate_control (ctx->media) ? "yes" : "no");
  }

  return ret;
}

static void
gst_rtsp_onvif_client_class_init (GstRTSPOnvifClientClass * klass)
{
  GstRTSPClientClass *client_klass = (GstRTSPClientClass *) klass;

  client_klass->check_requirements = gst_rtsp_onvif_client_check_requirements;
  client_klass->adjust_play_mode = gst_rtsp_onvif_client_adjust_play_mode;
  client_klass->adjust_play_response =
      gst_rtsp_onvif_client_adjust_play_response;
}

static void
gst_rtsp_onvif_client_init (GstRTSPOnvifClient * client)
{
}

/**
 * gst_rtsp_onvif_client_new:
 *
 * Create a new #GstRTSPOnvifClient instance.
 *
 * Returns: (transfer full): a new #GstRTSPOnvifClient
 * Since: 1.18
 */
GstRTSPClient *
gst_rtsp_onvif_client_new (void)
{
  GstRTSPClient *result;

  result = g_object_new (GST_TYPE_RTSP_ONVIF_CLIENT, NULL);

  return result;
}
