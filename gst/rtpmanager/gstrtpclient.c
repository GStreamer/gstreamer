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

/**
 * SECTION:element-gstrtpclient
 * @see_also: gstrtpjitterbuffer, gstrtpbin, gstrtpsession
 *
 * This element handles RTP data from one client. It accepts multiple RTP streams that
 * should be synchronized together.
 * 
 * Normally the SSRCs that map to the same CNAME (as given in the RTCP SDES messages)
 * should be synchronized.
 * 
 * <refsect2>
 * <title>Example pipelines</title>
 * |[
 * FIXME: gst-launch
 * ]| FIXME: describe
 * </refsect2>
 *
 * Last reviewed on 2007-04-02 (0.10.5)
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <string.h>

#include "gstrtpclient.h"

/* elementfactory information */
static const GstElementDetails rtpclient_details =
GST_ELEMENT_DETAILS ("RTP Client",
    "Filter/Network/RTP",
    "Implement an RTP client",
    "Wim Taymans <wim.taymans@gmail.com>");

/* sink pads */
static GstStaticPadTemplate rtpclient_rtp_sink_template =
GST_STATIC_PAD_TEMPLATE ("rtp_sink_%d",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS ("application/x-rtp")
    );

static GstStaticPadTemplate rtpclient_sync_sink_template =
GST_STATIC_PAD_TEMPLATE ("sync_sink_%d",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS ("application/x-rtcp")
    );

/* src pads */
static GstStaticPadTemplate rtpclient_rtp_src_template =
GST_STATIC_PAD_TEMPLATE ("rtp_src_%d_%d",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS ("application/x-rtp")
    );

#define GST_RTP_CLIENT_GET_PRIVATE(obj)  \
   (G_TYPE_INSTANCE_GET_PRIVATE ((obj), GST_TYPE_RTP_CLIENT, GstRtpClientPrivate))

struct _GstRtpClientPrivate
{
  gint foo;
};

/* all the info needed to handle the stream with SSRC */
typedef struct
{
  GstRtpClient *client;

  /* the SSRC of this stream */
  guint32 ssrc;

  /* RTP and RTCP in */
  GstPad *rtp_sink;
  GstPad *sync_sink;

  /* the jitterbuffer */
  GstElement *jitterbuffer;
  /* the payload demuxer */
  GstElement *ptdemux;
  /* the new-pad signal */
  gulong new_pad_sig;
} GstRtpClientStream;

/* the PT demuxer found a new payload type */
static void
new_pad (GstElement * element, GstPad * pad, GstRtpClientStream * stream)
{
}

/* create a new stream for SSRC.
 *
 * We create a jitterbuffer and an payload demuxer for the SSRC. The sinkpad of
 * the jitterbuffer is ghosted to the bin. We connect a pad-added signal to
 * rtpptdemux so that we can ghost the payload pads outside.
 *
 *       +-----------------+     +---------------+
 *       | rtpjitterbuffer |     |  rtpptdemux   |
 *   +- sink              src - sink             |
 *  /    +-----------------+     +---------------+
 *
 */
static GstRtpClientStream *
create_stream (GstRtpClient * rtpclient, guint32 ssrc)
{
  GstRtpClientStream *stream;
  gchar *name;
  GstPad *srcpad, *sinkpad;
  GstPadLinkReturn res;

  stream = g_new0 (GstRtpClientStream, 1);
  stream->ssrc = ssrc;
  stream->client = rtpclient;

  stream->jitterbuffer = gst_element_factory_make ("gstrtpjitterbuffer", NULL);
  if (!stream->jitterbuffer)
    goto no_jitterbuffer;

  stream->ptdemux = gst_element_factory_make ("gstrtpptdemux", NULL);
  if (!stream->ptdemux)
    goto no_ptdemux;

  /* add elements to bin */
  gst_bin_add (GST_BIN_CAST (rtpclient), stream->jitterbuffer);
  gst_bin_add (GST_BIN_CAST (rtpclient), stream->ptdemux);

  /* link jitterbuffer and PT demuxer */
  srcpad = gst_element_get_static_pad (stream->jitterbuffer, "src");
  sinkpad = gst_element_get_static_pad (stream->ptdemux, "sink");
  res = gst_pad_link (srcpad, sinkpad);
  gst_object_unref (srcpad);
  gst_object_unref (sinkpad);

  if (res != GST_PAD_LINK_OK)
    goto could_not_link;

  /* add stream to list */
  rtpclient->streams = g_list_prepend (rtpclient->streams, stream);

  /* ghost sinkpad */
  name = g_strdup_printf ("rtp_sink_%d", ssrc);
  sinkpad = gst_element_get_static_pad (stream->jitterbuffer, "sink");
  stream->rtp_sink = gst_ghost_pad_new (name, sinkpad);
  gst_object_unref (sinkpad);
  g_free (name);
  gst_element_add_pad (GST_ELEMENT_CAST (rtpclient), stream->rtp_sink);

  /* add signal to ptdemuxer */
  stream->new_pad_sig =
      g_signal_connect (G_OBJECT (stream->ptdemux), "pad-added",
      G_CALLBACK (new_pad), stream);

  return stream;

  /* ERRORS */
no_jitterbuffer:
  {
    g_free (stream);
    g_warning ("gstrtpclient: could not create gstrtpjitterbuffer element");
    return NULL;
  }
no_ptdemux:
  {
    gst_object_unref (stream->jitterbuffer);
    g_free (stream);
    g_warning ("gstrtpclient: could not create gstrtpptdemux element");
    return NULL;
  }
could_not_link:
  {
    gst_bin_remove (GST_BIN_CAST (rtpclient), stream->jitterbuffer);
    gst_bin_remove (GST_BIN_CAST (rtpclient), stream->ptdemux);
    g_free (stream);
    g_warning ("gstrtpclient: could not link jitterbuffer and ptdemux element");
    return NULL;
  }
}

#if 0
static void
free_stream (GstRtpClientStream * stream)
{
  gst_object_unref (stream->jitterbuffer);
  g_free (stream);
}
#endif

/* find the stream for the given SSRC, return NULL if the stream did not exist
 */
static GstRtpClientStream *
find_stream_by_ssrc (GstRtpClient * client, guint32 ssrc)
{
  GstRtpClientStream *stream;
  GList *walk;

  for (walk = client->streams; walk; walk = g_list_next (walk)) {
    stream = (GstRtpClientStream *) walk->data;
    if (stream->ssrc == ssrc)
      return stream;
  }
  return NULL;
}

/* signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  PROP_0
};

/* GObject vmethods */
static void gst_rtp_client_finalize (GObject * object);
static void gst_rtp_client_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_rtp_client_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

/* GstElement vmethods */
static GstStateChangeReturn gst_rtp_client_change_state (GstElement * element,
    GstStateChange transition);
static GstPad *gst_rtp_client_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * name);
static void gst_rtp_client_release_pad (GstElement * element, GstPad * pad);

/*static guint gst_rtp_client_signals[LAST_SIGNAL] = { 0 }; */

GST_BOILERPLATE (GstRtpClient, gst_rtp_client, GstBin, GST_TYPE_BIN);

static void
gst_rtp_client_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  /* sink pads */
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&rtpclient_rtp_sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&rtpclient_sync_sink_template));

  /* src pads */
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&rtpclient_rtp_src_template));

  gst_element_class_set_details (element_class, &rtpclient_details);
}

static void
gst_rtp_client_class_init (GstRtpClientClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  g_type_class_add_private (klass, sizeof (GstRtpClientPrivate));

  gobject_class->finalize = gst_rtp_client_finalize;
  gobject_class->set_property = gst_rtp_client_set_property;
  gobject_class->get_property = gst_rtp_client_get_property;

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_rtp_client_change_state);
  gstelement_class->request_new_pad =
      GST_DEBUG_FUNCPTR (gst_rtp_client_request_new_pad);
  gstelement_class->release_pad =
      GST_DEBUG_FUNCPTR (gst_rtp_client_release_pad);
}

static void
gst_rtp_client_init (GstRtpClient * rtpclient, GstRtpClientClass * klass)
{
  rtpclient->priv = GST_RTP_CLIENT_GET_PRIVATE (rtpclient);
}

static void
gst_rtp_client_finalize (GObject * object)
{
  GstRtpClient *rtpclient;

  rtpclient = GST_RTP_CLIENT (object);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_rtp_client_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstRtpClient *rtpclient;

  rtpclient = GST_RTP_CLIENT (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_rtp_client_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstRtpClient *rtpclient;

  rtpclient = GST_RTP_CLIENT (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstStateChangeReturn
gst_rtp_client_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn res;
  GstRtpClient *rtpclient;

  rtpclient = GST_RTP_CLIENT (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      break;
    default:
      break;
  }

  res = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    default:
      break;
  }
  return res;
}

/* We have 2 request pads (rtp_sink_%d and sync_sink_%d), the %d is assumed to
 * be the SSRC of the stream.
 *
 * We require that the rtp pad is requested first for a particular SSRC, then
 * (optionaly) the sync pad can be requested. If no sync pad is requested, no
 * sync information can be exchanged for this stream.
 */
static GstPad *
gst_rtp_client_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * name)
{
  GstRtpClient *rtpclient;
  GstElementClass *klass;
  GstPadTemplate *rtp_sink_templ, *sync_sink_templ;
  guint32 ssrc;
  GstRtpClientStream *stream;
  GstPad *result;

  g_return_val_if_fail (templ != NULL, NULL);
  g_return_val_if_fail (GST_IS_RTP_CLIENT (element), NULL);

  if (templ->direction != GST_PAD_SINK)
    goto wrong_direction;

  rtpclient = GST_RTP_CLIENT (element);
  klass = GST_ELEMENT_GET_CLASS (element);

  /* figure out the template */
  rtp_sink_templ = gst_element_class_get_pad_template (klass, "rtp_sink_%d");
  sync_sink_templ = gst_element_class_get_pad_template (klass, "sync_sink_%d");

  if (templ != rtp_sink_templ && templ != sync_sink_templ)
    goto wrong_template;

  if (templ == rtp_sink_templ) {
    /* create new rtp sink pad. If a stream with the pad number already exists
     * we have an error, else we create the sinkpad, add a jitterbuffer and
     * ptdemuxer. */
    if (name == NULL || strlen (name) < 9)
      goto no_name;

    ssrc = atoi (&name[9]);

    /* see if a stream with that name exists, if so we have an error. */
    stream = find_stream_by_ssrc (rtpclient, ssrc);
    if (stream != NULL)
      goto stream_exists;

    /* ok, create new stream */
    stream = create_stream (rtpclient, ssrc);
    if (stream == NULL)
      goto stream_not_found;

    result = stream->rtp_sink;
  } else {
    /* create new rtp sink pad. We can only do this if the RTP pad was
     * requested before, meaning the session with the padnumber must exist. */
    if (name == NULL || strlen (name) < 10)
      goto no_name;

    ssrc = atoi (&name[10]);

    /* find stream */
    stream = find_stream_by_ssrc (rtpclient, ssrc);
    if (stream == NULL)
      goto stream_not_found;

    stream->sync_sink =
        gst_pad_new_from_static_template (&rtpclient_sync_sink_template, name);
    gst_element_add_pad (GST_ELEMENT_CAST (rtpclient), stream->sync_sink);

    result = stream->sync_sink;
  }

  return result;

  /* ERRORS */
wrong_direction:
  {
    g_warning ("gstrtpclient: request pad that is not a SINK pad");
    return NULL;
  }
wrong_template:
  {
    g_warning ("gstrtpclient: this is not our template");
    return NULL;
  }
no_name:
  {
    g_warning ("gstrtpclient: no padname was specified");
    return NULL;
  }
stream_exists:
  {
    g_warning ("gstrtpclient: stream with SSRC %d already registered", ssrc);
    return NULL;
  }
stream_not_found:
  {
    g_warning ("gstrtpclient: stream with SSRC %d not yet registered", ssrc);
    return NULL;
  }
}

static void
gst_rtp_client_release_pad (GstElement * element, GstPad * pad)
{
}
