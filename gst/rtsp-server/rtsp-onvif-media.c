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

/**
 * SECTION:rtsp-onvif-media
 * @short_description: The ONVIF media pipeline
 * @see_also: #GstRTSPMedia, #GstRTSPOnvifMediaFactory, #GstRTSPStream, #GstRTSPSession,
 *     #GstRTSPSessionMedia
 *
 * a #GstRTSPOnvifMedia contains the complete GStreamer pipeline to manage the
 * streaming to the clients. The actual data transfer is done by the
 * #GstRTSPStream objects that are created and exposed by the #GstRTSPMedia.
 *
 * On top of #GstRTSPMedia this subclass adds special ONVIF features.
 * Special ONVIF features that are currently supported is a backchannel for
 * the client to send back media to the server in a normal PLAY media. To
 * handle the ONVIF backchannel, a #GstRTSPOnvifMediaFactory and
 * #GstRTSPOnvifServer has to be used.
 *
 * Since: 1.14
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "rtsp-onvif-media.h"
#include "rtsp-latency-bin.h"

struct GstRTSPOnvifMediaPrivate
{
  GMutex lock;
  guint backchannel_bandwidth;
};

G_DEFINE_TYPE_WITH_PRIVATE (GstRTSPOnvifMedia, gst_rtsp_onvif_media,
    GST_TYPE_RTSP_MEDIA);

static gboolean
gst_rtsp_onvif_media_setup_sdp (GstRTSPMedia * media, GstSDPMessage * sdp,
    GstSDPInfo * info)
{
  guint i, n_streams;
  gchar *rangestr;
  gboolean res;

  /* Mostly a copy of gst_rtsp_sdp_from_media() which handles the backchannel
   * stream separately and adds sendonly/recvonly attributes to each media
   */

  n_streams = gst_rtsp_media_n_streams (media);

  rangestr = gst_rtsp_media_get_range_string (media, FALSE, GST_RTSP_RANGE_NPT);
  if (rangestr == NULL)
    goto not_prepared;

  gst_sdp_message_add_attribute (sdp, "range", rangestr);
  g_free (rangestr);

  res = TRUE;
  for (i = 0; res && (i < n_streams); i++) {
    GstRTSPStream *stream;
    GstCaps *caps = NULL;
    GstRTSPProfile profiles;
    guint mask;
    GstPad *sinkpad = NULL;
    guint n_caps, j;

    /* Mostly a copy of gst_rtsp_sdp_from_stream() which handles the
     * backchannel stream separately */

    stream = gst_rtsp_media_get_stream (media, i);

    if ((sinkpad = gst_rtsp_stream_get_sinkpad (stream))) {
      caps = gst_pad_query_caps (sinkpad, NULL);
    } else {
      caps = gst_rtsp_stream_get_caps (stream);
    }

    if (caps == NULL) {
      GST_ERROR ("stream %p has no caps", stream);
      res = FALSE;
      if (sinkpad)
        gst_object_unref (sinkpad);
      break;
    } else if (!sinkpad && !gst_caps_is_fixed (caps)) {
      GST_ERROR ("stream %p has unfixed caps", stream);
      res = FALSE;
      gst_caps_unref (caps);
      break;
    }

    n_caps = gst_caps_get_size (caps);
    for (j = 0; res && j < n_caps; j++) {
      GstStructure *s = gst_caps_get_structure (caps, j);
      GstCaps *media_caps = gst_caps_new_full (gst_structure_copy (s), NULL);

      if (!gst_caps_is_fixed (media_caps)) {
        GST_ERROR ("media caps for stream %p are not all fixed", stream);
        res = FALSE;
        gst_caps_unref (media_caps);
        break;
      }

      /* make a new media for each profile */
      profiles = gst_rtsp_stream_get_profiles (stream);
      mask = 1;
      res = TRUE;
      while (res && (profiles >= mask)) {
        GstRTSPProfile prof = profiles & mask;

        if (prof) {
          res = gst_rtsp_sdp_make_media (sdp, info, stream, media_caps, prof);
          if (res) {
            GstSDPMedia *smedia =
                &g_array_index (sdp->medias, GstSDPMedia, sdp->medias->len - 1);
            gchar *x_onvif_track, *media_str;

            media_str =
                g_ascii_strup (gst_structure_get_string (s, "media"), -1);
            x_onvif_track =
                g_strdup_printf ("%s%03d", media_str, sdp->medias->len - 1);
            gst_sdp_media_add_attribute (smedia, "x-onvif-track",
                x_onvif_track);
            g_free (x_onvif_track);
            g_free (media_str);

            if (sinkpad) {
              GstRTSPOnvifMedia *onvif_media = GST_RTSP_ONVIF_MEDIA (media);

              gst_sdp_media_add_attribute (smedia, "sendonly", "");
              if (onvif_media->priv->backchannel_bandwidth > 0)
                gst_sdp_media_add_bandwidth (smedia, GST_SDP_BWTYPE_AS,
                    onvif_media->priv->backchannel_bandwidth);
            } else {
              gst_sdp_media_add_attribute (smedia, "recvonly", "");
            }
          }
        }

        mask <<= 1;
      }

      if (sinkpad) {
        GstStructure *s = gst_caps_get_structure (media_caps, 0);
        gint pt = -1;

        if (!gst_structure_get_int (s, "payload", &pt) || pt < 0) {
          GST_ERROR ("stream %p has no payload type", stream);
          res = FALSE;
          gst_caps_unref (media_caps);
          gst_object_unref (sinkpad);
          break;
        }

        gst_rtsp_stream_set_pt_map (stream, pt, media_caps);
      }

      gst_caps_unref (media_caps);
    }

    gst_caps_unref (caps);
    if (sinkpad)
      gst_object_unref (sinkpad);
  }

  {
    GstNetTimeProvider *provider;

    if ((provider =
            gst_rtsp_media_get_time_provider (media, info->server_ip, 0))) {
      GstClock *clock;
      gchar *address, *str;
      gint port;

      g_object_get (provider, "clock", &clock, "address", &address, "port",
          &port, NULL);

      str = g_strdup_printf ("GstNetTimeProvider %s %s:%d %" G_GUINT64_FORMAT,
          g_type_name (G_TYPE_FROM_INSTANCE (clock)), address, port,
          gst_clock_get_time (clock));

      gst_sdp_message_add_attribute (sdp, "x-gst-clock", str);
      g_free (str);
      gst_object_unref (clock);
      g_free (address);
      gst_object_unref (provider);
    }
  }

  return res;

  /* ERRORS */
not_prepared:
  {
    GST_ERROR ("media %p is not prepared", media);
    return FALSE;
  }
}

static void
gst_rtsp_onvif_media_finalize (GObject * object)
{
  GstRTSPOnvifMedia *media = GST_RTSP_ONVIF_MEDIA (object);

  g_mutex_clear (&media->priv->lock);

  G_OBJECT_CLASS (gst_rtsp_onvif_media_parent_class)->finalize (object);
}

static void
gst_rtsp_onvif_media_class_init (GstRTSPOnvifMediaClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstRTSPMediaClass *media_class = (GstRTSPMediaClass *) klass;

  gobject_class->finalize = gst_rtsp_onvif_media_finalize;

  media_class->setup_sdp = gst_rtsp_onvif_media_setup_sdp;
}

static void
gst_rtsp_onvif_media_init (GstRTSPOnvifMedia * media)
{
  media->priv = gst_rtsp_onvif_media_get_instance_private (media);
  g_mutex_init (&media->priv->lock);
}

/**
 * gst_rtsp_onvif_media_collect_backchannel:
 * @media: a #GstRTSPOnvifMedia
 *
 * Find the ONVIF backchannel depayloader element. It should be named
 * 'depay_backchannel', be placed in a bin called 'onvif-backchannel'
 * and return all supported RTP caps on a caps query. Complete RTP caps with
 * at least the payload type, clock-rate and encoding-name are required.
 *
 * A new #GstRTSPStream is created for the backchannel if found.
 *
 * Returns: %TRUE if a backchannel stream could be found and created
 *
 * Since: 1.14
 */
gboolean
gst_rtsp_onvif_media_collect_backchannel (GstRTSPOnvifMedia * media)
{
  GstElement *element, *backchannel_bin = NULL;
  GstElement *latency_bin;
  GstPad *pad = NULL;
  gboolean ret = FALSE;

  g_return_val_if_fail (GST_IS_RTSP_ONVIF_MEDIA (media), FALSE);

  element = gst_rtsp_media_get_element (GST_RTSP_MEDIA (media));
  if (!element)
    return ret;

  backchannel_bin =
      gst_bin_get_by_name (GST_BIN (element), "onvif-backchannel");
  if (!backchannel_bin)
    goto out;

  /* We don't want the backchannel element, which is a receiver, to affect
   * latency on the complete pipeline. That's why we remove it from the
   * pipeline and add it to a @GstRTSPLatencyBin which will prevent it from
   * messing up pipelines latency. The extra reference is needed so that it
   * is not freed in case the pipeline holds the the only ref to it.
   *
   * TODO: a more generic solution should be implemented in
   * gst_rtsp_media_collect_streams() where all receivers are encapsulated
   * in a @GstRTSPLatencyBin in cases when there are senders too. */
  gst_object_ref (backchannel_bin);
  gst_bin_remove (GST_BIN (element), backchannel_bin);

  latency_bin = gst_rtsp_latency_bin_new (backchannel_bin);
  g_assert (latency_bin);

  gst_bin_add (GST_BIN (element), latency_bin);

  pad = gst_element_get_static_pad (latency_bin, "sink");
  if (!pad)
    goto out;

  gst_rtsp_media_create_stream (GST_RTSP_MEDIA (media), latency_bin, pad);
  ret = TRUE;

out:
  if (pad)
    gst_object_unref (pad);
  if (backchannel_bin)
    gst_object_unref (backchannel_bin);
  gst_object_unref (element);

  return ret;
}

/**
 * gst_rtsp_onvif_media_set_backchannel_bandwidth:
 * @media: a #GstRTSPMedia
 * @bandwidth: the bandwidth in bits per second
 *
 * Set the configured/supported bandwidth of the ONVIF backchannel pipeline in
 * bits per second.
 *
 * Since: 1.14
 */
void
gst_rtsp_onvif_media_set_backchannel_bandwidth (GstRTSPOnvifMedia * media,
    guint bandwidth)
{
  g_return_if_fail (GST_IS_RTSP_ONVIF_MEDIA (media));

  g_mutex_lock (&media->priv->lock);
  media->priv->backchannel_bandwidth = bandwidth;
  g_mutex_unlock (&media->priv->lock);
}

/**
 * gst_rtsp_onvif_media_get_backchannel_bandwidth:
 * @media: a #GstRTSPMedia
 *
 * Get the configured/supported bandwidth of the ONVIF backchannel pipeline in
 * bits per second.
 *
 * Returns: the configured/supported backchannel bandwidth.
 *
 * Since: 1.14
 */
guint
gst_rtsp_onvif_media_get_backchannel_bandwidth (GstRTSPOnvifMedia * media)
{
  guint bandwidth;

  g_return_val_if_fail (GST_IS_RTSP_ONVIF_MEDIA (media), 0);

  g_mutex_lock (&media->priv->lock);
  bandwidth = media->priv->backchannel_bandwidth;
  g_mutex_unlock (&media->priv->lock);

  return bandwidth;
}
