/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *                    2002 Kristian Rietveld <kris@gtk.org>
 *                    2002,2003 Colin Walters <walters@gnu.org>
 *                    2001,2010 Bastien Nocera <hadess@hadess.net>
 *                    2010 Sebastian Dröge <sebastian.droege@collabora.co.uk>
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
 * the RTMP library, i.e. rtmp, rtmpt, rtmps, rtmpe, rtmfp, rtmpte and rtmpts.
 *
 * <refsect2>
 * <title>Example launch lines</title>
 * |[
 * gst-launch -v rtmpsrc location=rtmp://somehost/someurl ! fakesink
 * ]| Open an RTMP location and pass its content to fakesink.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <glib/gi18n-lib.h>

#include "gstrtmpsrc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gst/gst.h>

GST_DEBUG_CATEGORY_STATIC (rtmpsrc_debug);
#define GST_CAT_DEFAULT rtmpsrc_debug

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

enum
{
  PROP_0,
  PROP_LOCATION,
};

static void gst_rtmp_src_uri_handler_init (gpointer g_iface,
    gpointer iface_data);

static void gst_rtmp_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_rtmp_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_rtmp_src_finalize (GObject * object);

static gboolean gst_rtmp_src_stop (GstBaseSrc * src);
static gboolean gst_rtmp_src_start (GstBaseSrc * src);
static gboolean gst_rtmp_src_is_seekable (GstBaseSrc * src);
static GstFlowReturn gst_rtmp_src_create (GstPushSrc * pushsrc,
    GstBuffer ** buffer);
static gboolean gst_rtmp_src_query (GstBaseSrc * src, GstQuery * query);

static void
_do_init (GType gtype)
{
  static const GInterfaceInfo urihandler_info = {
    gst_rtmp_src_uri_handler_init,
    NULL,
    NULL
  };

  g_type_add_interface_static (gtype, GST_TYPE_URI_HANDLER, &urihandler_info);
}

GST_BOILERPLATE_FULL (GstRTMPSrc, gst_rtmp_src, GstPushSrc, GST_TYPE_PUSH_SRC,
    _do_init);

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
      "Bastien Nocera <hadess@hadess.net>, "
      "Sebastian Dröge <sebastian.droege@collabora.co.uk>");
}

static void
gst_rtmp_src_class_init (GstRTMPSrcClass * klass)
{
  GObjectClass *gobject_class;
  GstBaseSrcClass *gstbasesrc_class;
  GstPushSrcClass *gstpushsrc_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gstbasesrc_class = GST_BASE_SRC_CLASS (klass);
  gstpushsrc_class = GST_PUSH_SRC_CLASS (klass);

  gobject_class->finalize = gst_rtmp_src_finalize;
  gobject_class->set_property = gst_rtmp_src_set_property;
  gobject_class->get_property = gst_rtmp_src_get_property;

  /* properties */
  gst_element_class_install_std_props (GST_ELEMENT_CLASS (klass),
      "location", PROP_LOCATION, G_PARAM_READWRITE, NULL);

  gstbasesrc_class->start = GST_DEBUG_FUNCPTR (gst_rtmp_src_start);
  gstbasesrc_class->stop = GST_DEBUG_FUNCPTR (gst_rtmp_src_stop);
  gstbasesrc_class->is_seekable = GST_DEBUG_FUNCPTR (gst_rtmp_src_is_seekable);
  gstpushsrc_class->create = GST_DEBUG_FUNCPTR (gst_rtmp_src_create);
  gstbasesrc_class->query = GST_DEBUG_FUNCPTR (gst_rtmp_src_query);
}

static void
gst_rtmp_src_init (GstRTMPSrc * rtmpsrc, GstRTMPSrcClass * klass)
{
  rtmpsrc->curoffset = 0;
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
  static gchar *protocols[] =
      { (char *) "rtmp", (char *) "rtmpt", (char *) "rtmps", (char *) "rtmpe",
    (char *) "rtmfp", (char *) "rtmpte", (char *) "rtmpts", NULL
  };
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
  gchar *new_location;

  if (GST_STATE (src) >= GST_STATE_PAUSED)
    return FALSE;

  g_free (src->uri);
  src->uri = NULL;

  if (src->rtmp) {
    RTMP_Close (src->rtmp);
    RTMP_Free (src->rtmp);
    src->rtmp = NULL;
  }

  if (uri != NULL) {

    new_location = g_strdup (uri);

    src->rtmp = RTMP_Alloc ();
    RTMP_Init (src->rtmp);
    if (!RTMP_SetupURL (src->rtmp, new_location)) {
      GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ, NULL,
          ("Failed to setup URL '%s'", src->uri));
      g_free (new_location);
      RTMP_Free (src->rtmp);
      src->rtmp = NULL;
      return FALSE;
    } else {
      src->uri = g_strdup (uri);
      GST_DEBUG_OBJECT (src, "parsed uri '%s' properly", src->uri);
    }
  }

  GST_DEBUG_OBJECT (src, "Changed URI to %s", GST_STR_NULL (uri));

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
    case PROP_LOCATION:{
      gst_rtmp_src_uri_set_uri (GST_URI_HANDLER (src),
          g_value_get_string (value));
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
    case PROP_LOCATION:
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
gst_rtmp_src_create (GstPushSrc * pushsrc, GstBuffer ** buffer)
{
  GstRTMPSrc *src;
  GstBuffer *buf;
  guint8 *data;
  guint todo;
  int read;
  int size;

  src = GST_RTMP_SRC (pushsrc);

  g_return_val_if_fail (src->rtmp != NULL, GST_FLOW_ERROR);

  size = GST_BASE_SRC_CAST (pushsrc)->blocksize;

  GST_DEBUG ("reading from %" G_GUINT64_FORMAT
      ", size %u", src->curoffset, size);

  buf = gst_buffer_try_new_and_alloc (size);
  if (G_UNLIKELY (buf == NULL)) {
    GST_ERROR_OBJECT (src, "Failed to allocate %u bytes", size);
    return GST_FLOW_ERROR;
  }

  data = GST_BUFFER_DATA (buf);

  /* FIXME add FLV header first time around? */
  read = 0;

  todo = size;
  while (todo > 0) {
    read = RTMP_Read (src->rtmp, (char *) data, todo);

    if (G_UNLIKELY (read == 0))
      goto eos;

    if (G_UNLIKELY (read == -1))
      goto read_failed;

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

  return GST_FLOW_OK;

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
    case GST_QUERY_DURATION:{
      GstFormat format;
      gdouble duration;

      gst_query_parse_duration (query, &format, NULL);
      if (format == GST_FORMAT_TIME && src->rtmp) {
        duration = RTMP_GetDuration (src->rtmp);
        if (duration != 0.0) {
          gst_query_set_duration (query, format, duration * GST_SECOND);
          ret = TRUE;
        }
      }
      break;
    }
    default:
      ret = FALSE;
      break;
  }

  if (!ret)
    ret = GST_BASE_SRC_CLASS (parent_class)->query (basesrc, query);

  return ret;
}

static gboolean
gst_rtmp_src_is_seekable (GstBaseSrc * basesrc)
{
  GstRTMPSrc *src;

  src = GST_RTMP_SRC (basesrc);

  return FALSE;
}

/* open the file, do stuff necessary to go to PAUSED state */
static gboolean
gst_rtmp_src_start (GstBaseSrc * basesrc)
{
  GstRTMPSrc *src;

  src = GST_RTMP_SRC (basesrc);

  if (!src->uri) {
    GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ, (NULL), ("No filename given"));
    return FALSE;
  }

  src->curoffset = 0;

  /* open if required */
  if (!RTMP_IsConnected (src->rtmp)) {
    if (!RTMP_Connect (src->rtmp, NULL)) {
      GST_ELEMENT_ERROR (src, RESOURCE, NOT_FOUND, (NULL),
          ("Could not connect to RTMP stream \"%s\" for reading: %s (%d)",
              src->uri, "FIXME", 0));
      return FALSE;
    }
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

  src->curoffset = 0;

  return TRUE;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (rtmpsrc_debug, "rtmpsrc", 0, "RTMP Source");

  return gst_element_register (plugin, "rtmpsrc", GST_RANK_PRIMARY,
      GST_TYPE_RTMP_SRC);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "rtmpsrc",
    "RTMP source",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN);
