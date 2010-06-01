/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *                    2001 Bastien Nocera <hadess@hadess.net>
 *                    2002 Kristian Rietveld <kris@gtk.org>
 *                    2002,2003 Colin Walters <walters@gnu.org>
 *
 * rtmpsrc.c:
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
 * SECTION:element-rtmpsrc
 *
 * This plugin reads data from a local or remote location specified
 * by an URI. This location can be specified using any protocol supported by
 * the RTMP library. Common protocols are 'file', 'http', 'ftp', or 'smb'.
 *
 * In case the #GstRTMPSrc:iradio-mode property is set and the
 * location is a http resource, rtmpsrc will send special icecast http
 * headers to the server to request additional icecast metainformation. If
 * the server is not an icecast server, it will display the same behaviour
 * as if the #GstRTMPSrc:iradio-mode property was not set. However,
 * if the server is in fact an icecast server, rtmpsrc will output
 * data with a media type of application/x-icy, in which case you will
 * need to use the #GstICYDemux element as follow-up element to extract
 * the icecast meta data and to determine the underlying media type.
 *
 * <refsect2>
 * <title>Example launch lines</title>
 * |[
 * gst-launch -v rtmpsrc location=file:///home/joe/foo.xyz ! fakesink
 * ]| The above pipeline will simply read a local file and do nothing with the
 * data read. Instead of rtmpsrc, we could just as well have used the
 * filesrc element here.
 * |[
 * gst-launch -v rtmpsrc location=smb://othercomputer/foo.xyz ! filesink location=/home/joe/foo.xyz
 * ]| The above pipeline will copy a file from a remote host to the local file
 * system using the Samba protocol.
 * |[
 * gst-launch -v rtmpsrc location=http://music.foobar.com/demo.mp3 ! mad ! audioconvert ! audioresample ! alsasink
 * ]| The above pipeline will read and decode and play an mp3 file from a
 * web server using the http protocol.
 * </refsect2>
 */

#define DEFAULT_RTMP_PORT 1935

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <glib/gi18n-lib.h>

#include "gstrtmpsrc.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <errno.h>
#include <string.h>

#include <gst/gst.h>
#include <gst/tag/tag.h>

GST_DEBUG_CATEGORY_STATIC (rtmpsrc_debug);
#define GST_CAT_DEFAULT rtmpsrc_debug

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

enum
{
  ARG_0,
  ARG_LOCATION,
};

static void gst_rtmp_src_base_init (gpointer g_class);
static void gst_rtmp_src_class_init (GstRTMPSrcClass * klass);
static void gst_rtmp_src_init (GstRTMPSrc * rtmpsrc);
static void gst_rtmp_src_finalize (GObject * object);
static void gst_rtmp_src_uri_handler_init (gpointer g_iface,
    gpointer iface_data);

static void gst_rtmp_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_rtmp_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_rtmp_src_stop (GstBaseSrc * src);
static gboolean gst_rtmp_src_start (GstBaseSrc * src);
static gboolean gst_rtmp_src_is_seekable (GstBaseSrc * src);
#if 0
static gboolean gst_rtmp_src_check_get_range (GstBaseSrc * src);
static gboolean gst_rtmp_src_get_size (GstBaseSrc * src, guint64 * size);
#endif
static GstFlowReturn gst_rtmp_src_create (GstBaseSrc * basesrc,
    guint64 offset, guint size, GstBuffer ** buffer);
#if 0
static gboolean gst_rtmp_src_query (GstBaseSrc * src, GstQuery * query);
#endif

static GstElementClass *parent_class = NULL;

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "rtmpsrc", GST_RANK_NONE,
      GST_TYPE_RTMP_SRC);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "rtmpsrc",
    "flvstreamer sources",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN);

GType
gst_rtmp_src_get_type (void)
{
  static GType rtmpsrc_type = 0;

  if (!rtmpsrc_type) {
    static const GTypeInfo rtmpsrc_info = {
      sizeof (GstRTMPSrcClass),
      gst_rtmp_src_base_init,
      NULL,
      (GClassInitFunc) gst_rtmp_src_class_init,
      NULL,
      NULL,
      sizeof (GstRTMPSrc),
      0,
      (GInstanceInitFunc) gst_rtmp_src_init,
    };
    static const GInterfaceInfo urihandler_info = {
      gst_rtmp_src_uri_handler_init,
      NULL,
      NULL
    };

    rtmpsrc_type =
        g_type_register_static (GST_TYPE_BASE_SRC,
        "GstRTMPSrc", &rtmpsrc_info, (GTypeFlags) 0);
    g_type_add_interface_static (rtmpsrc_type, GST_TYPE_URI_HANDLER,
        &urihandler_info);
  }
  return rtmpsrc_type;
}

static void
gst_rtmp_src_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&srctemplate));

  gst_element_class_set_details_simple (element_class,
      "RTMP Source",
      "Source/File",
      "Read RTMP streams",
      "Bastien Nocera <hadess@hadess.net>\n"
      "GStreamer maintainers <gstreamer-devel@lists.sourceforge.net>");

  GST_DEBUG_CATEGORY_INIT (rtmpsrc_debug, "rtmpsrc", 0, "RTMP Source");
}

static void
gst_rtmp_src_class_init (GstRTMPSrcClass * klass)
{
  GObjectClass *gobject_class;
  GstBaseSrcClass *gstbasesrc_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gstbasesrc_class = GST_BASE_SRC_CLASS (klass);

  parent_class = (GstElementClass *) g_type_class_peek_parent (klass);

  gobject_class->finalize = gst_rtmp_src_finalize;
  gobject_class->set_property = gst_rtmp_src_set_property;
  gobject_class->get_property = gst_rtmp_src_get_property;

  /* properties */
  gst_element_class_install_std_props (GST_ELEMENT_CLASS (klass),
      "location", ARG_LOCATION, G_PARAM_READWRITE, NULL);

  gstbasesrc_class->start = GST_DEBUG_FUNCPTR (gst_rtmp_src_start);
  gstbasesrc_class->stop = GST_DEBUG_FUNCPTR (gst_rtmp_src_stop);
#if 0
  gstbasesrc_class->get_size = GST_DEBUG_FUNCPTR (gst_rtmp_src_get_size);
#endif
  gstbasesrc_class->is_seekable = GST_DEBUG_FUNCPTR (gst_rtmp_src_is_seekable);
#if 0
  gstbasesrc_class->check_get_range =
      GST_DEBUG_FUNCPTR (gst_rtmp_src_check_get_range);
#endif
  gstbasesrc_class->create = GST_DEBUG_FUNCPTR (gst_rtmp_src_create);
#if 0
  gstbasesrc_class->query = GST_DEBUG_FUNCPTR (gst_rtmp_src_query);
#endif
}

static void
gst_rtmp_src_init (GstRTMPSrc * rtmpsrc)
{
  rtmpsrc->curoffset = 0;
  rtmpsrc->seekable = FALSE;
}

static void
gst_rtmp_src_finalize (GObject * object)
{
  GstRTMPSrc *rtmpsrc = GST_RTMP_SRC (object);

  g_free (rtmpsrc->uri);
  rtmpsrc->uri = NULL;

  if (rtmpsrc->rtmp) {
    RTMP_Close (rtmpsrc->rtmp);
    RTMP_Free (rtmpsrc->rtmp);
    rtmpsrc->rtmp = NULL;
  }

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

/*
 * URI interface support.
 */

static GstURIType
gst_rtmp_src_uri_get_type (void)
{
  return GST_URI_SRC;
}

static gchar **
gst_rtmp_src_uri_get_protocols (void)
{
  static gchar *protocols[] = { (char *) "rtmp", NULL };
  return protocols;
}

static const gchar *
gst_rtmp_src_uri_get_uri (GstURIHandler * handler)
{
  GstRTMPSrc *src = GST_RTMP_SRC (handler);

  return src->uri;
}

static gboolean
gst_rtmp_src_uri_set_uri (GstURIHandler * handler, const gchar * uri)
{
  GstRTMPSrc *src = GST_RTMP_SRC (handler);

  if (GST_STATE (src) == GST_STATE_PLAYING ||
      GST_STATE (src) == GST_STATE_PAUSED)
    return FALSE;

  g_object_set (G_OBJECT (src), "location", uri, NULL);
  g_message ("just set uri to %s", uri);

  return TRUE;
}

static void
gst_rtmp_src_uri_handler_init (gpointer g_iface, gpointer iface_data)
{
  GstURIHandlerInterface *iface = (GstURIHandlerInterface *) g_iface;

  iface->get_type = gst_rtmp_src_uri_get_type;
  iface->get_protocols = gst_rtmp_src_uri_get_protocols;
  iface->get_uri = gst_rtmp_src_uri_get_uri;
  iface->set_uri = gst_rtmp_src_uri_set_uri;
}

static void
gst_rtmp_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstRTMPSrc *src;

  src = GST_RTMP_SRC (object);

  switch (prop_id) {
    case ARG_LOCATION:{
      char *new_location;
      /* the element must be stopped or paused in order to do this */
      if (GST_STATE (src) == GST_STATE_PLAYING ||
          GST_STATE (src) == GST_STATE_PAUSED)
        break;

      g_free (src->uri);
      src->uri = NULL;

      if (src->rtmp) {
        RTMP_Close (src->rtmp);
        RTMP_Free (src->rtmp);
        src->rtmp = NULL;
      }

      new_location = g_value_dup_string (value);

      src->rtmp = RTMP_Alloc ();
      RTMP_Init (src->rtmp);
      if (!RTMP_SetupURL (src->rtmp, new_location)) {
        GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ, NULL,
            ("Failed to setup URL '%s'", src->uri));
        g_free (new_location);
        RTMP_Free (src->rtmp);
        src->rtmp = NULL;
      } else {
        src->uri = g_value_dup_string (value);
        g_message ("parsed uri '%s' properly", src->uri);
      }
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_rtmp_src_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstRTMPSrc *src;

  src = GST_RTMP_SRC (object);

  switch (prop_id) {
    case ARG_LOCATION:
      g_value_set_string (value, src->uri);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/*
 * Read a new buffer from src->reqoffset, takes care of events
 * and seeking and such.
 */
static GstFlowReturn
gst_rtmp_src_create (GstBaseSrc * basesrc, guint64 offset, guint size,
    GstBuffer ** buffer)
{
  GstRTMPSrc *src;
  GstBuffer *buf;
  guint8 *data;
  guint todo;
  int read;

  src = GST_RTMP_SRC (basesrc);

  g_return_val_if_fail (src->rtmp != NULL, GST_FLOW_ERROR);

  GST_DEBUG ("now at %" G_GINT64_FORMAT ", reading from %" G_GUINT64_FORMAT
      ", size %u", src->curoffset, offset, size);

  /* open if required */
  if (G_UNLIKELY (!RTMP_IsConnected (src->rtmp))) {
    if (!RTMP_Connect (src->rtmp, NULL)) {
      GST_ELEMENT_ERROR (src, RESOURCE, NOT_FOUND, (NULL),
          ("Could not connect to RTMP stream \"%s\" for reading: %s (%d)",
              src->uri, "FIXME", 0));
      return GST_FLOW_ERROR;
    }
  }

  /* seek if required */
  if (G_UNLIKELY (src->curoffset != offset)) {
    GST_DEBUG ("need to seek");
    if (src->seekable) {
#if 0
      GST_DEBUG ("seeking to %" G_GUINT64_FORMAT, offset);
      res = rtmp_seek (src->handle, RTMP_SEEK_START, offset);
      if (res != RTMP_OK)
        goto seek_failed;
      src->curoffset = offset;
#endif
    } else {
      goto cannot_seek;
    }
  }

  buf = gst_buffer_try_new_and_alloc (size);
  if (G_UNLIKELY (buf == NULL && size == 0)) {
    GST_ERROR_OBJECT (src, "Failed to allocate %u bytes", size);
    return GST_FLOW_ERROR;
  }

  data = GST_BUFFER_DATA (buf);

  /* FIXME add FLV header first time around? */
  read = 0;

  todo = size;
  while (todo > 0) {
    read = RTMP_Read (src->rtmp, (char *) &data, todo);

    if (G_UNLIKELY (read == -1))
      goto eos;

    if (G_UNLIKELY (read == -2))
      goto read_failed;

    /* FIXME handle -3 ? */

    if (read < todo) {
      data = &data[read];
      todo -= read;
    } else {
      todo = 0;
    }
    GST_LOG ("  got size %" G_GUINT64_FORMAT, read);
  }
  GST_BUFFER_OFFSET (buf) = src->curoffset;
  src->curoffset += size;

  /* we're done, return the buffer */
  *buffer = buf;

#if 0
  RTMPFileSize readbytes;
  guint todo;



  return GST_FLOW_OK;
#endif
  return GST_FLOW_OK;

//seek_failed:
  {
    GST_ELEMENT_ERROR (src, RESOURCE, SEEK, (NULL),
        ("Failed to seek to requested position %" G_GINT64_FORMAT ": %s",
            offset, "FIXME"));
    return GST_FLOW_ERROR;
  }
cannot_seek:
  {
    GST_ELEMENT_ERROR (src, RESOURCE, SEEK, (NULL),
        ("Requested seek from %" G_GINT64_FORMAT " to %" G_GINT64_FORMAT
            " on non-seekable stream", src->curoffset, offset));
    return GST_FLOW_ERROR;
  }
read_failed:
  {
    gst_buffer_unref (buf);
    GST_ELEMENT_ERROR (src, RESOURCE, READ, (NULL),
        ("Failed to read data: %s", "FIXME"));
    return GST_FLOW_ERROR;
  }
eos:
  {
    gst_buffer_unref (buf);
    GST_DEBUG_OBJECT (src, "Reading data gave EOS");
    return GST_FLOW_UNEXPECTED;
  }
}

#if 0
static gboolean
gst_rtmp_src_query (GstBaseSrc * basesrc, GstQuery * query)
{
  gboolean ret = FALSE;
  GstRTMPSrc *src = GST_RTMP_SRC (basesrc);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_URI:
      gst_query_set_uri (query, src->uri);
      ret = TRUE;
      break;
    default:
      ret = FALSE;
      break;
  }

  if (!ret)
    ret = GST_BASE_SRC_CLASS (parent_class)->query (basesrc, query);

  return ret;
}
#endif
static gboolean
gst_rtmp_src_is_seekable (GstBaseSrc * basesrc)
{
  GstRTMPSrc *src;

  src = GST_RTMP_SRC (basesrc);

  return src->seekable;
}

#if 0
static gboolean
gst_rtmp_src_check_get_range (GstBaseSrc * basesrc)
{
  GstRTMPSrc *src;
  const gchar *protocol;

  src = GST_RTMP_SRC (basesrc);

  if (src->uri == NULL) {
    GST_WARNING_OBJECT (src, "no URI set yet");
    return FALSE;
  }

  if (rtmp_uri_is_local (src->uri)) {
    GST_LOG_OBJECT (src, "local URI (%s), assuming random access is possible",
        GST_STR_NULL (src->uri_name));
    return TRUE;
  }

  /* blacklist certain protocols we know won't work getrange-based */
  protocol = rtmp_uri_get_scheme (src->uri);
  if (protocol == NULL)
    goto undecided;

  if (strcmp (protocol, "http") == 0 || strcmp (protocol, "https") == 0) {
    GST_LOG_OBJECT (src, "blacklisted protocol '%s', no random access possible"
        " (URI=%s)", protocol, GST_STR_NULL (src->uri_name));
    return FALSE;
  }

  /* fall through to undecided */

undecided:
  {
    /* don't know what to do, let the basesrc class decide for us */
    GST_LOG_OBJECT (src, "undecided about URI '%s', let base class handle it",
        GST_STR_NULL (src->uri_name));

    if (GST_BASE_SRC_CLASS (parent_class)->check_get_range)
      return GST_BASE_SRC_CLASS (parent_class)->check_get_range (basesrc);

    return FALSE;
  }
}
#endif

#if 0
static gboolean
gst_rtmp_src_get_size (GstBaseSrc * basesrc, guint64 * size)
{
  GstRTMPSrc *src;
  RTMPFileInfo *info;
  RTMPFileInfoOptions options;
  RTMPResult res;

  src = GST_RTMP_SRC (basesrc);

  *size = -1;
  info = rtmp_file_info_new ();
  options = RTMP_FILE_INFO_DEFAULT | RTMP_FILE_INFO_FOLLOW_LINKS;
  res = rtmp_get_file_info_from_handle (src->handle, info, options);
  if (res == RTMP_OK) {
    if ((info->valid_fields & RTMP_FILE_INFO_FIELDS_SIZE) != 0) {
      *size = info->size;
      GST_DEBUG_OBJECT (src, "from handle: %" G_GUINT64_FORMAT " bytes", *size);
    } else if (src->own_handle && rtmp_uri_is_local (src->uri)) {
      GST_DEBUG_OBJECT (src,
          "file size not known, file local, trying fallback");
      res = rtmp_get_file_info_uri (src->uri, info, options);
      if (res == RTMP_OK &&
          (info->valid_fields & RTMP_FILE_INFO_FIELDS_SIZE) != 0) {
        *size = info->size;
        GST_DEBUG_OBJECT (src, "from uri: %" G_GUINT64_FORMAT " bytes", *size);
      }
    }
  } else {
    GST_WARNING_OBJECT (src, "getting info failed: %s",
        rtmp_result_to_string (res));
  }
  rtmp_file_info_unref (info);

  if (*size == (RTMPFileSize) - 1)
    return FALSE;

  GST_DEBUG_OBJECT (src, "return size %" G_GUINT64_FORMAT, *size);

  return TRUE;
}
#endif

/* open the file, do stuff necessary to go to PAUSED state */
static gboolean
gst_rtmp_src_start (GstBaseSrc * basesrc)
{
  GstRTMPSrc *src;

  src = GST_RTMP_SRC (basesrc);

  g_message ("start called!");

  if (!src->uri) {
    GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ, (NULL), ("No filename given"));
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_rtmp_src_stop (GstBaseSrc * basesrc)
{
  GstRTMPSrc *src;

  src = GST_RTMP_SRC (basesrc);

//FIXME you can't run RTMP_Close multiple times
//  RTMP_Close (src->rtmp);

  g_message ("stop called!");

  src->curoffset = 0;

  return TRUE;
}

/*
 * vim: sw=2 ts=8 cindent noai bs=2
 */
