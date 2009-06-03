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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */
#include <string.h>

#include "rtsp-sdp.h"

/**
 * gst_rtsp_sdp_from_media:
 * @media: a #GstRTSPMedia
 *
 * Create a new sdp message for @media.
 *
 * Returns: a new sdp message for @media. gst_sdp_message_free() after usage.
 */
GstSDPMessage *
gst_rtsp_sdp_from_media (GstRTSPMedia *media)
{
  GstSDPMessage *sdp;
  guint i, n_streams;
  gchar *rangestr;

  n_streams = gst_rtsp_media_n_streams (media);

  gst_sdp_message_new (&sdp);

  /* some standard things first */
  gst_sdp_message_set_version (sdp, "0");
  gst_sdp_message_set_origin (sdp, "-", "1188340656180883", "1", "IN", "IP4", "127.0.0.1");
  gst_sdp_message_set_session_name (sdp, "Session streamed with GStreamer");
  gst_sdp_message_set_information (sdp, "rtsp-server");
  gst_sdp_message_add_time (sdp, "0", "0", NULL);
  gst_sdp_message_add_attribute (sdp, "tool", "GStreamer");
  gst_sdp_message_add_attribute (sdp, "type", "broadcast");
  rangestr = gst_rtsp_range_to_string (&media->range);
  gst_sdp_message_add_attribute (sdp, "range", rangestr);
  g_free (rangestr);

  for (i = 0; i < n_streams; i++) {
    GstRTSPMediaStream *stream;
    GstSDPMedia *smedia;
    GstStructure *s;
    const gchar *caps_str, *caps_enc, *caps_params;
    gchar *tmp;
    gint caps_pt, caps_rate;
    guint n_fields, j;
    gboolean first;
    GString *fmtp;

    stream = gst_rtsp_media_get_stream (media, i);

    if (stream->caps == NULL) {
      g_warning ("ignoring stream %d without media type", i);
      continue;
    }

    s = gst_caps_get_structure (stream->caps, 0);
    if (s == NULL) {
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
    gst_sdp_media_add_connection (smedia, "IN", "IP4", "127.0.0.1", 0, 0);

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
    tmp = g_strdup_printf ("stream=%d", i);
    gst_sdp_media_add_attribute (smedia, "control", tmp);
    g_free (tmp);

    /* collect all other properties and add them to fmtp */
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

      if ((fval = gst_structure_get_string (s, fname))) {
        g_string_append_printf (fmtp, "%s%s=%s", first ? "":";", fname, fval);
        first = FALSE;
      }
    }
    if (!first) {
      tmp = g_string_free (fmtp, FALSE);
      gst_sdp_media_add_attribute (smedia, "fmtp", tmp);
      g_free (tmp);
    }
    else {
      g_string_free (fmtp, TRUE);
    }
    gst_sdp_message_add_media (sdp, smedia);
    gst_sdp_media_free (smedia);
  }

  return sdp;
}
