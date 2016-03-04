/*
 *
 * GStreamer
 * Copyright (C) 1999-2001 Erik Walthinsen <omega@cse.ogi.edu>
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
#  include <config.h>
#endif

#include <gst/gst.h>
#include <stdio.h>
#include <string.h>
#include "gstmms.h"

#define DEFAULT_CONNECTION_SPEED    0

enum
{
  PROP_0,
  PROP_LOCATION,
  PROP_CONNECTION_SPEED
};


GST_DEBUG_CATEGORY_STATIC (mmssrc_debug);
#define GST_CAT_DEFAULT mmssrc_debug

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-ms-asf")
    );

static void gst_mms_finalize (GObject * gobject);
static void gst_mms_uri_handler_init (gpointer g_iface, gpointer iface_data);

static void gst_mms_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_mms_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_mms_query (GstBaseSrc * src, GstQuery * query);

static gboolean gst_mms_start (GstBaseSrc * bsrc);
static gboolean gst_mms_stop (GstBaseSrc * bsrc);
static gboolean gst_mms_is_seekable (GstBaseSrc * src);
static gboolean gst_mms_get_size (GstBaseSrc * src, guint64 * size);
static gboolean gst_mms_prepare_seek_segment (GstBaseSrc * src,
    GstEvent * event, GstSegment * segment);
static gboolean gst_mms_do_seek (GstBaseSrc * src, GstSegment * segment);

static GstFlowReturn gst_mms_create (GstPushSrc * psrc, GstBuffer ** buf);

static gboolean gst_mms_uri_set_uri (GstURIHandler * handler,
    const gchar * uri, GError ** error);

#define gst_mms_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstMMS, gst_mms, GST_TYPE_PUSH_SRC,
    G_IMPLEMENT_INTERFACE (GST_TYPE_URI_HANDLER, gst_mms_uri_handler_init));

/* initialize the plugin's class */
static void
gst_mms_class_init (GstMMSClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstElementClass *gstelement_class = (GstElementClass *) klass;
  GstBaseSrcClass *gstbasesrc_class = (GstBaseSrcClass *) klass;
  GstPushSrcClass *gstpushsrc_class = (GstPushSrcClass *) klass;

  gobject_class->set_property = gst_mms_set_property;
  gobject_class->get_property = gst_mms_get_property;
  gobject_class->finalize = gst_mms_finalize;

  g_object_class_install_property (gobject_class, PROP_LOCATION,
      g_param_spec_string ("location", "location",
          "Host URL to connect to. Accepted are mms://, mmsu://, mmst:// URL types",
          NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /* Note: connection-speed is intentionaly limited to G_MAXINT as libmms
   * uses int for it */
  g_object_class_install_property (gobject_class, PROP_CONNECTION_SPEED,
      g_param_spec_uint64 ("connection-speed", "Connection Speed",
          "Network connection speed in kbps (0 = unknown)",
          0, G_MAXINT / 1000, DEFAULT_CONNECTION_SPEED,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_add_static_pad_template (gstelement_class, &src_factory);

  gst_element_class_set_static_metadata (gstelement_class,
      "MMS streaming source", "Source/Network",
      "Receive data streamed via MSFT Multi Media Server protocol",
      "Maciej Katafiasz <mathrick@users.sourceforge.net>");

  GST_DEBUG_CATEGORY_INIT (mmssrc_debug, "mmssrc", 0, "MMS Source Element");

  gstbasesrc_class->start = GST_DEBUG_FUNCPTR (gst_mms_start);
  gstbasesrc_class->stop = GST_DEBUG_FUNCPTR (gst_mms_stop);

  gstpushsrc_class->create = GST_DEBUG_FUNCPTR (gst_mms_create);

  gstbasesrc_class->is_seekable = GST_DEBUG_FUNCPTR (gst_mms_is_seekable);
  gstbasesrc_class->get_size = GST_DEBUG_FUNCPTR (gst_mms_get_size);
  gstbasesrc_class->prepare_seek_segment =
      GST_DEBUG_FUNCPTR (gst_mms_prepare_seek_segment);
  gstbasesrc_class->do_seek = GST_DEBUG_FUNCPTR (gst_mms_do_seek);
  gstbasesrc_class->query = GST_DEBUG_FUNCPTR (gst_mms_query);
}

/* initialize the new element
 * instantiate pads and add them to element
 * set functions
 * initialize structure
 */
static void
gst_mms_init (GstMMS * mmssrc)
{
  mmssrc->uri_name = NULL;
  mmssrc->current_connection_uri_name = NULL;
  mmssrc->connection = NULL;
  mmssrc->connection_speed = DEFAULT_CONNECTION_SPEED;
}

static void
gst_mms_finalize (GObject * gobject)
{
  GstMMS *mmssrc = GST_MMS (gobject);

  /* We may still have a connection open, as we preserve unused / pristine
     open connections in stop to reuse them in start. */
  if (mmssrc->connection) {
    mmsx_close (mmssrc->connection);
    mmssrc->connection = NULL;
  }

  if (mmssrc->current_connection_uri_name) {
    g_free (mmssrc->current_connection_uri_name);
    mmssrc->current_connection_uri_name = NULL;
  }

  if (mmssrc->uri_name) {
    g_free (mmssrc->uri_name);
    mmssrc->uri_name = NULL;
  }

  G_OBJECT_CLASS (parent_class)->finalize (gobject);
}

/* FIXME operating in TIME rather than BYTES could remove this altogether
 * and be more convenient elsewhere */
static gboolean
gst_mms_query (GstBaseSrc * src, GstQuery * query)
{
  GstMMS *mmssrc = GST_MMS (src);
  gboolean res = TRUE;
  GstFormat format;
  gint64 value;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_POSITION:
      gst_query_parse_position (query, &format, &value);
      if (format != GST_FORMAT_BYTES) {
        res = FALSE;
        break;
      }
      value = (gint64) mmsx_get_current_pos (mmssrc->connection);
      gst_query_set_position (query, format, value);
      break;
    case GST_QUERY_DURATION:
      if (!mmsx_get_seekable (mmssrc->connection)) {
        res = FALSE;
        break;
      }
      gst_query_parse_duration (query, &format, &value);
      switch (format) {
        case GST_FORMAT_BYTES:
          value = (gint64) mmsx_get_length (mmssrc->connection);
          gst_query_set_duration (query, format, value);
          break;
        case GST_FORMAT_TIME:
          value = mmsx_get_time_length (mmssrc->connection) * GST_SECOND;
          gst_query_set_duration (query, format, value);
          break;
        default:
          res = FALSE;
      }
      break;
    default:
      /* chain to parent */
      res =
          GST_BASE_SRC_CLASS (parent_class)->query (GST_BASE_SRC (src), query);
      break;
  }

  return res;
}


static gboolean
gst_mms_prepare_seek_segment (GstBaseSrc * src, GstEvent * event,
    GstSegment * segment)
{
  GstSeekType cur_type, stop_type;
  gint64 cur, stop;
  GstSeekFlags flags;
  GstFormat seek_format;
  gdouble rate;

  gst_event_parse_seek (event, &rate, &seek_format, &flags,
      &cur_type, &cur, &stop_type, &stop);

  if (seek_format != GST_FORMAT_BYTES && seek_format != GST_FORMAT_TIME) {
    GST_LOG_OBJECT (src, "Only byte or time seeking is supported");
    return FALSE;
  }

  if (stop_type != GST_SEEK_TYPE_NONE) {
    GST_LOG_OBJECT (src, "Stop seeking not supported");
    return FALSE;
  }

  if (cur_type != GST_SEEK_TYPE_NONE && cur_type != GST_SEEK_TYPE_SET) {
    GST_LOG_OBJECT (src, "Only absolute seeking is supported");
    return FALSE;
  }

  /* We would like to convert from GST_FORMAT_TIME to GST_FORMAT_BYTES here
     when needed, but we cannot as to do that we need to actually do the seek,
     so we handle this in do_seek instead. */

  /* FIXME implement relative seeking, we could do any needed relevant
     seeking calculations here (in seek_format metrics), before the re-init
     of the segment. */

  gst_segment_init (segment, seek_format);
  gst_segment_do_seek (segment, rate, seek_format, flags, cur_type, cur,
      stop_type, stop, NULL);

  return TRUE;
}

static gboolean
gst_mms_do_seek (GstBaseSrc * src, GstSegment * segment)
{
  gint64 start;
  GstMMS *mmssrc = GST_MMS (src);

  if (segment->format == GST_FORMAT_TIME) {
    if (!mmsx_time_seek (NULL, mmssrc->connection,
            (double) segment->start / GST_SECOND)) {
      GST_LOG_OBJECT (mmssrc, "mmsx_time_seek() failed");
      return FALSE;
    }
    start = mmsx_get_current_pos (mmssrc->connection);
    GST_INFO_OBJECT (mmssrc, "sought to %" GST_TIME_FORMAT ", offset after "
        "seek: %" G_GINT64_FORMAT, GST_TIME_ARGS (segment->start), start);
  } else if (segment->format == GST_FORMAT_BYTES) {
    start = mmsx_seek (NULL, mmssrc->connection, segment->start, SEEK_SET);
    /* mmsx_seek will close and reopen the connection when seeking with the
       mmsh protocol, if the reopening fails this is indicated with -1 */
    if (start == -1) {
      GST_DEBUG_OBJECT (mmssrc, "connection broken during seek");
      return FALSE;
    }
    GST_INFO_OBJECT (mmssrc, "sought to: %" G_GINT64_FORMAT " bytes, "
        "result: %" G_GINT64_FORMAT, segment->start, start);
  } else {
    GST_DEBUG_OBJECT (mmssrc, "unsupported seek segment format: %s",
        GST_STR_NULL (gst_format_get_name (segment->format)));
    return FALSE;
  }
  gst_segment_init (segment, GST_FORMAT_BYTES);
  gst_segment_do_seek (segment, segment->rate, GST_FORMAT_BYTES,
      GST_SEEK_FLAG_NONE, GST_SEEK_TYPE_SET, start, GST_SEEK_TYPE_NONE,
      segment->stop, NULL);
  return TRUE;
}


/* get function
 * this function generates new data when needed
 */


static GstFlowReturn
gst_mms_create (GstPushSrc * psrc, GstBuffer ** buf)
{
  GstMMS *mmssrc = GST_MMS (psrc);
  guint8 *data;
  guint blocksize;
  gint result;
  goffset offset;

  *buf = NULL;

  offset = mmsx_get_current_pos (mmssrc->connection);

  /* Check if a seek perhaps has wrecked our connection */
  if (offset == -1) {
    GST_ERROR_OBJECT (mmssrc,
        "connection broken (probably an error during mmsx_seek_time during a convert query) returning FLOW_ERROR");
    return GST_FLOW_ERROR;
  }

  /* Choose blocksize best for optimum performance */
  if (offset == 0)
    blocksize = mmsx_get_asf_header_len (mmssrc->connection);
  else
    blocksize = mmsx_get_asf_packet_len (mmssrc->connection);

  data = g_try_malloc (blocksize);
  if (!data) {
    GST_ERROR_OBJECT (mmssrc, "Failed to allocate %u bytes", blocksize);
    return GST_FLOW_ERROR;
  }

  GST_LOG_OBJECT (mmssrc, "reading %d bytes", blocksize);
  result = mmsx_read (NULL, mmssrc->connection, (char *) data, blocksize);
  /* EOS? */
  if (result == 0)
    goto eos;

  *buf = gst_buffer_new_wrapped (data, result);
  GST_BUFFER_OFFSET (*buf) = offset;

  GST_LOG_OBJECT (mmssrc, "Returning buffer with offset %" G_GOFFSET_FORMAT
      " and size %u", offset, result);

  return GST_FLOW_OK;

eos:
  {
    GST_DEBUG_OBJECT (mmssrc, "EOS");
    g_free (data);
    *buf = NULL;
    return GST_FLOW_EOS;
  }
}

static gboolean
gst_mms_is_seekable (GstBaseSrc * src)
{
  GstMMS *mmssrc = GST_MMS (src);

  return mmsx_get_seekable (mmssrc->connection);
}

static gboolean
gst_mms_get_size (GstBaseSrc * src, guint64 * size)
{
  GstMMS *mmssrc = GST_MMS (src);

  /* non seekable usually means live streams, and get_length() returns,
     erm, interesting values for live streams */
  if (!mmsx_get_seekable (mmssrc->connection))
    return FALSE;

  *size = mmsx_get_length (mmssrc->connection);
  return TRUE;
}

static gboolean
gst_mms_start (GstBaseSrc * bsrc)
{
  GstMMS *mms = GST_MMS (bsrc);
  guint bandwidth_avail;

  if (!mms->uri_name || *mms->uri_name == '\0')
    goto no_uri;

  if (mms->connection_speed)
    bandwidth_avail = mms->connection_speed;
  else
    bandwidth_avail = G_MAXINT;

  /* If we already have a connection, and the uri isn't changed, reuse it,
     as connecting is expensive. */
  if (mms->connection) {
    if (!strcmp (mms->uri_name, mms->current_connection_uri_name)) {
      GST_DEBUG_OBJECT (mms, "Reusing existing connection for %s",
          mms->uri_name);
      return TRUE;
    } else {
      mmsx_close (mms->connection);
      g_free (mms->current_connection_uri_name);
      mms->current_connection_uri_name = NULL;
    }
  }

  /* FIXME: pass some sane arguments here */
  GST_DEBUG_OBJECT (mms,
      "Trying mms_connect (%s) with bandwidth constraint of %d bps",
      mms->uri_name, bandwidth_avail);
  mms->connection = mmsx_connect (NULL, NULL, mms->uri_name, bandwidth_avail);
  if (mms->connection) {
    /* Save the uri name so that it can be checked for connection reusing,
       see above. */
    mms->current_connection_uri_name = g_strdup (mms->uri_name);
    GST_DEBUG_OBJECT (mms, "Connect successful");
    return TRUE;
  } else {
    gchar *url, *location;

    GST_ERROR_OBJECT (mms,
        "Could not connect to this stream, redirecting to rtsp");
    location = strstr (mms->uri_name, "://");
    if (location == NULL || *location == '\0' || *(location + 3) == '\0')
      goto no_uri;
    url = g_strdup_printf ("rtsp://%s", location + 3);

    gst_element_post_message (GST_ELEMENT_CAST (mms),
        gst_message_new_element (GST_OBJECT_CAST (mms),
            gst_structure_new ("redirect", "new-location", G_TYPE_STRING, url,
                NULL)));

    /* post an error message as well, so that applications that don't handle
     * redirect messages get to see a proper error message */
    GST_ELEMENT_ERROR (mms, RESOURCE, OPEN_READ,
        ("Could not connect to streaming server."),
        ("A redirect message was posted on the bus and should have been "
            "handled by the application."));

    return FALSE;
  }

no_uri:
  {
    GST_ELEMENT_ERROR (mms, RESOURCE, OPEN_READ,
        ("No URI to open specified"), (NULL));
    return FALSE;
  }
}

static gboolean
gst_mms_stop (GstBaseSrc * bsrc)
{
  GstMMS *mms = GST_MMS (bsrc);

  if (mms->connection != NULL) {
    /* Check if the connection is still pristine, that is if no more then
       just the mmslib cached asf header has been read. If it is still pristine
       preserve it as we often are re-started with the same URL and connecting
       is expensive */
    if (mmsx_get_current_pos (mms->connection) >
        mmsx_get_asf_header_len (mms->connection)) {
      mmsx_close (mms->connection);
      mms->connection = NULL;
      g_free (mms->current_connection_uri_name);
      mms->current_connection_uri_name = NULL;
    }
  }
  return TRUE;
}

static void
gst_mms_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstMMS *mmssrc = GST_MMS (object);

  switch (prop_id) {
    case PROP_LOCATION:
      gst_mms_uri_set_uri (GST_URI_HANDLER (mmssrc),
          g_value_get_string (value), NULL);
      break;
    case PROP_CONNECTION_SPEED:
      GST_OBJECT_LOCK (mmssrc);
      mmssrc->connection_speed = g_value_get_uint64 (value) * 1000;
      GST_OBJECT_UNLOCK (mmssrc);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_mms_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstMMS *mmssrc = GST_MMS (object);

  GST_OBJECT_LOCK (mmssrc);
  switch (prop_id) {
    case PROP_LOCATION:
      if (mmssrc->uri_name)
        g_value_set_string (value, mmssrc->uri_name);
      break;
    case PROP_CONNECTION_SPEED:
      g_value_set_uint64 (value, mmssrc->connection_speed / 1000);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (mmssrc);
}

/* entry point to initialize the plug-in
 * initialize the plug-in itself
 * register the element factories and pad templates
 * register the features
 */
static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "mmssrc", GST_RANK_NONE, GST_TYPE_MMS);
}

static GstURIType
gst_mms_uri_get_type (GType type)
{
  return GST_URI_SRC;
}

static const gchar *const *
gst_mms_uri_get_protocols (GType type)
{
  static const gchar *protocols[] = { "mms", "mmsh", "mmst", "mmsu", NULL };

  return protocols;
}

static gchar *
gst_mms_uri_get_uri (GstURIHandler * handler)
{
  GstMMS *src = GST_MMS (handler);

  /* FIXME: make thread-safe */
  return g_strdup (src->uri_name);
}

static gchar *
gst_mms_src_make_valid_uri (const gchar * uri)
{
  gchar *protocol;
  const gchar *colon, *tmp;
  gsize len;

  if (!uri || !gst_uri_is_valid (uri))
    return NULL;

  protocol = gst_uri_get_protocol (uri);

  if ((strcmp (protocol, "mms") != 0) && (strcmp (protocol, "mmsh") != 0) &&
      (strcmp (protocol, "mmst") != 0) && (strcmp (protocol, "mmsu") != 0)) {
    g_free (protocol);
    return FALSE;
  }
  g_free (protocol);

  colon = strstr (uri, "://");
  if (!colon)
    return NULL;

  tmp = colon + 3;
  len = strlen (tmp);
  if (len == 0)
    return NULL;

  /* libmms segfaults if there's no hostname or
   * no / after the hostname
   */
  colon = strstr (tmp, "/");
  if (colon == tmp)
    return NULL;

  if (strstr (tmp, "/") == NULL) {
    gchar *ret;

    len = strlen (uri);
    ret = g_malloc0 (len + 2);
    memcpy (ret, uri, len);
    ret[len] = '/';
    return ret;
  } else {
    return g_strdup (uri);
  }
}

static gboolean
gst_mms_uri_set_uri (GstURIHandler * handler, const gchar * uri,
    GError ** error)
{
  GstMMS *src = GST_MMS (handler);
  gchar *fixed_uri;

  fixed_uri = gst_mms_src_make_valid_uri (uri);
  if (!fixed_uri && uri) {
    g_set_error (error, GST_URI_ERROR, GST_URI_ERROR_BAD_URI,
        "Invalid MMS URI");
    return FALSE;
  }

  GST_OBJECT_LOCK (src);
  g_free (src->uri_name);
  src->uri_name = fixed_uri;
  GST_OBJECT_UNLOCK (src);

  return TRUE;
}

static void
gst_mms_uri_handler_init (gpointer g_iface, gpointer iface_data)
{
  GstURIHandlerInterface *iface = (GstURIHandlerInterface *) g_iface;

  iface->get_type = gst_mms_uri_get_type;
  iface->get_protocols = gst_mms_uri_get_protocols;
  iface->get_uri = gst_mms_uri_get_uri;
  iface->set_uri = gst_mms_uri_set_uri;
}


/* this is the structure that gst-register looks for
 * so keep the name plugin_desc, or you cannot get your plug-in registered */
GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    mms,
    "Microsoft Multi Media Server streaming protocol support",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
