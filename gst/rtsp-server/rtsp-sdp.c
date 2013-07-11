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
/**
 * SECTION:rtsp-sdp
 * @short_description: Make SDP messages
 * @see_also: #GstRTSPMedia
 *
 * Last reviewed on 2013-07-11 (1.0.0)
 */

#include <string.h>

#include "rtsp-sdp.h"

static gboolean
get_info_from_tags (GstPad * pad, GstEvent ** event, gpointer user_data)
{
  GstSDPMedia *media = (GstSDPMedia *) user_data;

  if (GST_EVENT_TYPE (*event) == GST_EVENT_TAG) {
    GstTagList *tags;
    guint bitrate = 0;

    gst_event_parse_tag (*event, &tags);

    if (gst_tag_list_get_scope (tags) != GST_TAG_SCOPE_STREAM)
      return TRUE;

    if (!gst_tag_list_get_uint (tags, GST_TAG_MAXIMUM_BITRATE,
            &bitrate) || bitrate == 0)
      if (!gst_tag_list_get_uint (tags, GST_TAG_BITRATE, &bitrate) ||
          bitrate == 0)
        return TRUE;

    /* set bandwidth (kbits/s) */
    gst_sdp_media_add_bandwidth (media, GST_SDP_BWTYPE_AS, bitrate / 1000);

    return FALSE;

  }

  return TRUE;
}

static void
update_sdp_from_tags (GstRTSPStream * stream, GstSDPMedia * stream_media)
{
  GstPad *src_pad;

  src_pad = gst_rtsp_stream_get_srcpad (stream);

  gst_pad_sticky_events_foreach (src_pad, get_info_from_tags, stream_media);

  gst_object_unref (src_pad);
}

/**
 * gst_rtsp_sdp_from_media:
 * @sdp: a #GstSDPMessage
 * @info: info
 * @media: a #GstRTSPMedia
 *
 * Add @media specific info to @sdp. @info is used to configure the connection
 * information in the SDP.
 *
 * Returns: TRUE on success.
 */
gboolean
gst_rtsp_sdp_from_media (GstSDPMessage * sdp, GstSDPInfo * info,
    GstRTSPMedia * media)
{
  guint i, n_streams;
  gchar *rangestr;

  n_streams = gst_rtsp_media_n_streams (media);

  rangestr = gst_rtsp_media_get_range_string (media, FALSE, GST_RTSP_RANGE_NPT);
  if (rangestr == NULL)
    goto not_prepared;

  gst_sdp_message_add_attribute (sdp, "range", rangestr);
  g_free (rangestr);

  for (i = 0; i < n_streams; i++) {
    GstRTSPStream *stream;
    GstSDPMedia *smedia;
    GstStructure *s;
    const gchar *caps_str, *caps_enc, *caps_params;
    gchar *tmp;
    gint caps_pt, caps_rate;
    guint n_fields, j;
    gboolean first;
    GString *fmtp;
    GstCaps *caps;

    stream = gst_rtsp_media_get_stream (media, i);
    caps = gst_rtsp_stream_get_caps (stream);

    if (caps == NULL) {
      g_warning ("ignoring stream %d without media type", i);
      continue;
    }

    s = gst_caps_get_structure (caps, 0);
    if (s == NULL) {
      gst_caps_unref (caps);
      g_warning ("ignoring stream %d without media type", i);
      continue;
    }

    gst_sdp_media_new (&smedia);

    /* get media type and payload for the m= line */
    caps_str = gst_structure_get_string (s, "media");
    gst_sdp_media_set_media (smedia, caps_str);

    gst_structure_get_int (s, "payload", &caps_pt);
    tmp = g_strdup_printf ("%d", caps_pt);
    gst_sdp_media_add_format (smedia, tmp);
    g_free (tmp);

    gst_sdp_media_set_port_info (smedia, 0, 1);
    gst_sdp_media_set_proto (smedia, "RTP/AVP");

    /* for the c= line */
    if (info->is_ipv6) {
      gst_sdp_media_add_connection (smedia, "IN", "IP6", "::", 16, 0);
    } else {
      gst_sdp_media_add_connection (smedia, "IN", "IP4", "0.0.0.0", 16, 0);
    }

    /* get clock-rate, media type and params for the rtpmap attribute */
    gst_structure_get_int (s, "clock-rate", &caps_rate);
    caps_enc = gst_structure_get_string (s, "encoding-name");
    caps_params = gst_structure_get_string (s, "encoding-params");

    if (caps_enc) {
      if (caps_params)
        tmp = g_strdup_printf ("%d %s/%d/%s", caps_pt, caps_enc, caps_rate,
            caps_params);
      else
        tmp = g_strdup_printf ("%d %s/%d", caps_pt, caps_enc, caps_rate);

      gst_sdp_media_add_attribute (smedia, "rtpmap", tmp);
      g_free (tmp);
    }

    /* the config uri */
    tmp = gst_rtsp_stream_get_control (stream);
    gst_sdp_media_add_attribute (smedia, "control", tmp);
    g_free (tmp);

    /* collect all other properties and add them to fmtp or attributes */
    fmtp = g_string_new ("");
    g_string_append_printf (fmtp, "%d ", caps_pt);
    first = TRUE;
    n_fields = gst_structure_n_fields (s);
    for (j = 0; j < n_fields; j++) {
      const gchar *fname, *fval;

      fname = gst_structure_nth_field_name (s, j);

      /* filter out standard properties */
      if (!strcmp (fname, "media"))
        continue;
      if (!strcmp (fname, "payload"))
        continue;
      if (!strcmp (fname, "clock-rate"))
        continue;
      if (!strcmp (fname, "encoding-name"))
        continue;
      if (!strcmp (fname, "encoding-params"))
        continue;
      if (!strcmp (fname, "ssrc"))
        continue;
      if (!strcmp (fname, "clock-base"))
        continue;
      if (!strcmp (fname, "seqnum-base"))
        continue;

      if (g_str_has_prefix (fname, "a-")) {
        /* attribute */
        if ((fval = gst_structure_get_string (s, fname)))
          gst_sdp_media_add_attribute (smedia, fname + 2, fval);
        continue;
      }
      if (g_str_has_prefix (fname, "x-")) {
        /* attribute */
        if ((fval = gst_structure_get_string (s, fname)))
          gst_sdp_media_add_attribute (smedia, fname, fval);
        continue;
      }

      if ((fval = gst_structure_get_string (s, fname))) {
        g_string_append_printf (fmtp, "%s%s=%s", first ? "" : ";", fname, fval);
        first = FALSE;
      }
    }
    if (!first) {
      tmp = g_string_free (fmtp, FALSE);
      gst_sdp_media_add_attribute (smedia, "fmtp", tmp);
      g_free (tmp);
    } else {
      g_string_free (fmtp, TRUE);
    }

    update_sdp_from_tags (stream, smedia);

    gst_sdp_message_add_media (sdp, smedia);
    gst_sdp_media_free (smedia);
    gst_caps_unref (caps);
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

  return TRUE;

  /* ERRORS */
not_prepared:
  {
    GST_ERROR ("media %p is not prepared", media);
    return FALSE;
  }
}
