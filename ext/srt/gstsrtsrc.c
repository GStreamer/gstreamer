/* GStreamer
 * Copyright (C) 2018, Collabora Ltd.
 * Copyright (C) 2018, SK Telecom, Co., Ltd.
 *   Author: Jeongseok Kim <jeongseok.kim@sk.com>
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
 * SECTION:element-srtsrc
 * @title: srtsrc
 *
 * srtsrc is a network source that reads <ulink url="http://www.srtalliance.org/">SRT</ulink>
 * packets from the network.
 *
 * <refsect2>
 * <title>Examples</title>
 * |[
 * gst-launch-1.0 -v srtsrc uri="srt://127.0.0.1:7001" ! fakesink
 * ]| This pipeline shows how to connect SRT server by setting #GstSRTSrc:uri property.
 *
 * |[
 * gst-launch-1.0 -v srtsrc uri="srt://:7001?mode=listener" ! fakesink
 * ]| This pipeline shows how to wait SRT connection by setting #GstSRTSrc:uri property.
 *
 * |[
 * gst-launch-1.0 -v srtclientsrc uri="srt://192.168.1.10:7001?mode=rendez-vous" ! fakesink
 * ]| This pipeline shows how to connect SRT server by setting #GstSRTSrc:uri property and using the rendez-vous mode.
 * </refsect2>
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "gstsrtsrc.h"

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

#define GST_CAT_DEFAULT gst_debug_srt_src
GST_DEBUG_CATEGORY (GST_CAT_DEFAULT);

enum
{
  SIG_CALLER_ADDED,
  SIG_CALLER_REMOVED,

  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

static void gst_srt_src_uri_handler_init (gpointer g_iface,
    gpointer iface_data);
static gchar *gst_srt_src_uri_get_uri (GstURIHandler * handler);
static gboolean gst_srt_src_uri_set_uri (GstURIHandler * handler,
    const gchar * uri, GError ** error);

#define gst_srt_src_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstSRTSrc, gst_srt_src,
    GST_TYPE_PUSH_SRC,
    G_IMPLEMENT_INTERFACE (GST_TYPE_URI_HANDLER, gst_srt_src_uri_handler_init)
    GST_DEBUG_CATEGORY_INIT (GST_CAT_DEFAULT, "srtsrc", 0, "SRT Source"));

static void
gst_srt_src_caller_added_cb (int sock, GSocketAddress * addr,
    GstSRTObject * srtobject)
{
  g_signal_emit (srtobject->element, signals[SIG_CALLER_ADDED], 0, sock, addr);
}

static void
gst_srt_src_caller_removed_cb (int sock, GSocketAddress * addr,
    GstSRTObject * srtobject)
{
  g_signal_emit (srtobject->element, signals[SIG_CALLER_REMOVED], 0, sock,
      addr);
}

static gboolean
gst_srt_src_start (GstBaseSrc * bsrc)
{
  GstSRTSrc *self = GST_SRT_SRC (bsrc);
  GError *error = NULL;
  gboolean ret = FALSE;
  GstSRTConnectionMode connection_mode = GST_SRT_CONNECTION_MODE_NONE;

  gst_structure_get_enum (self->srtobject->parameters, "mode",
      GST_TYPE_SRT_CONNECTION_MODE, (gint *) & connection_mode);

  if (connection_mode == GST_SRT_CONNECTION_MODE_LISTENER) {
    ret =
        gst_srt_object_open_full (self->srtobject, gst_srt_src_caller_added_cb,
        gst_srt_src_caller_removed_cb, self->cancellable, &error);
  } else {
    ret = gst_srt_object_open (self->srtobject, self->cancellable, &error);
  }

  if (!ret) {
    GST_WARNING_OBJECT (self, "Failed to open SRT: %s", error->message);
    g_clear_error (&error);
  }

  return ret;
}

static gboolean
gst_srt_src_stop (GstBaseSrc * bsrc)
{
  GstSRTSrc *self = GST_SRT_SRC (bsrc);

  gst_srt_object_close (self->srtobject);

  return TRUE;
}

static GstFlowReturn
gst_srt_src_fill (GstPushSrc * src, GstBuffer * outbuf)
{
  GstSRTSrc *self = GST_SRT_SRC (src);
  GstFlowReturn ret = GST_FLOW_OK;
  GstMapInfo info;
  GError *err = NULL;
  gssize recv_len;

  if (g_cancellable_is_cancelled (self->cancellable)) {
    ret = GST_FLOW_FLUSHING;
  }

  if (!gst_buffer_map (outbuf, &info, GST_MAP_WRITE)) {
    GST_ELEMENT_ERROR (src, RESOURCE, READ,
        ("Could not map the buffer for writing "), (NULL));
    ret = GST_FLOW_ERROR;
    goto out;
  }

  recv_len = gst_srt_object_read (self->srtobject, info.data,
      gst_buffer_get_size (outbuf), self->cancellable, &err);

  gst_buffer_unmap (outbuf, &info);

  if (g_cancellable_is_cancelled (self->cancellable)) {
    ret = GST_FLOW_FLUSHING;
    goto out;
  }

  if (recv_len < 0) {
    GST_ELEMENT_ERROR (src, RESOURCE, READ, (NULL), ("%s", err->message));
    ret = GST_FLOW_ERROR;
    g_clear_error (&err);
    goto out;
  } else if (recv_len == 0) {
    ret = GST_FLOW_EOS;
    goto out;
  }

  gst_buffer_resize (outbuf, 0, recv_len);

  GST_LOG_OBJECT (src,
      "filled buffer from _get of size %" G_GSIZE_FORMAT ", ts %"
      GST_TIME_FORMAT ", dur %" GST_TIME_FORMAT
      ", offset %" G_GINT64_FORMAT ", offset_end %" G_GINT64_FORMAT,
      gst_buffer_get_size (outbuf),
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (outbuf)),
      GST_TIME_ARGS (GST_BUFFER_DURATION (outbuf)),
      GST_BUFFER_OFFSET (outbuf), GST_BUFFER_OFFSET_END (outbuf));

out:
  return ret;
}

static void
gst_srt_src_init (GstSRTSrc * self)
{
  self->srtobject = gst_srt_object_new (GST_ELEMENT (self));
  self->cancellable = g_cancellable_new ();

  gst_base_src_set_format (GST_BASE_SRC (self), GST_FORMAT_TIME);
  gst_base_src_set_live (GST_BASE_SRC (self), TRUE);
  gst_base_src_set_do_timestamp (GST_BASE_SRC (self), TRUE);

  gst_srt_object_set_uri (self->srtobject, GST_SRT_DEFAULT_URI, NULL);

}

static void
gst_srt_src_finalize (GObject * object)
{
  GstSRTSrc *self = GST_SRT_SRC (object);

  g_clear_object (&self->cancellable);
  gst_srt_object_destroy (self->srtobject);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_srt_src_unlock (GstBaseSrc * bsrc)
{
  GstSRTSrc *self = GST_SRT_SRC (bsrc);

  g_cancellable_cancel (self->cancellable);
  gst_srt_object_wakeup (self->srtobject);

  return TRUE;
}

static gboolean
gst_srt_src_unlock_stop (GstBaseSrc * bsrc)
{
  GstSRTSrc *self = GST_SRT_SRC (bsrc);

  g_cancellable_reset (self->cancellable);

  return TRUE;
}

static void
gst_srt_src_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstSRTSrc *self = GST_SRT_SRC (object);

  if (!gst_srt_object_set_property_helper (self->srtobject, prop_id, value,
          pspec)) {
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
  }
}

static void
gst_srt_src_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstSRTSrc *self = GST_SRT_SRC (object);

  if (!gst_srt_object_get_property_helper (self->srtobject, prop_id, value,
          pspec)) {
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
  }
}

static void
gst_srt_src_class_init (GstSRTSrcClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);
  GstBaseSrcClass *gstbasesrc_class = GST_BASE_SRC_CLASS (klass);
  GstPushSrcClass *gstpushsrc_class = GST_PUSH_SRC_CLASS (klass);

  gobject_class->set_property = gst_srt_src_set_property;
  gobject_class->get_property = gst_srt_src_get_property;
  gobject_class->finalize = gst_srt_src_finalize;

  /**
   * GstSRTSrc::caller-added:
   * @gstsrtsink: the srtsink element that emitted this signal
   * @sock: the client socket descriptor that was added to srtsink
   * @addr: the #GSocketAddress that describes the @sock
   * 
   * The given socket descriptor was added to srtsink.
   */
  signals[SIG_CALLER_ADDED] =
      g_signal_new ("caller-added", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstSRTSrcClass, caller_added),
      NULL, NULL, g_cclosure_marshal_generic, G_TYPE_NONE,
      2, G_TYPE_INT, G_TYPE_SOCKET_ADDRESS);

  /**
   * GstSRTSrc::caller-removed:
   * @gstsrtsink: the srtsink element that emitted this signal
   * @sock: the client socket descriptor that was added to srtsink
   * @addr: the #GSocketAddress that describes the @sock
   *
   * The given socket descriptor was removed from srtsink.
   */
  signals[SIG_CALLER_REMOVED] =
      g_signal_new ("caller-removed", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstSRTSrcClass,
          caller_added), NULL, NULL, g_cclosure_marshal_generic, G_TYPE_NONE,
      2, G_TYPE_INT, G_TYPE_SOCKET_ADDRESS);

  gst_srt_object_install_properties_helper (gobject_class);

  gst_element_class_add_static_pad_template (gstelement_class, &src_template);
  gst_element_class_set_metadata (gstelement_class,
      "SRT source", "Source/Network",
      "Receive data over the network via SRT",
      "Justin Kim <justin.joy.9to5@gmail.com>");

  gstbasesrc_class->start = GST_DEBUG_FUNCPTR (gst_srt_src_start);
  gstbasesrc_class->stop = GST_DEBUG_FUNCPTR (gst_srt_src_stop);
  gstbasesrc_class->unlock = GST_DEBUG_FUNCPTR (gst_srt_src_unlock);
  gstbasesrc_class->unlock_stop = GST_DEBUG_FUNCPTR (gst_srt_src_unlock_stop);

  gstpushsrc_class->fill = GST_DEBUG_FUNCPTR (gst_srt_src_fill);
}

static GstURIType
gst_srt_src_uri_get_type (GType type)
{
  return GST_URI_SRC;
}

static const gchar *const *
gst_srt_src_uri_get_protocols (GType type)
{
  static const gchar *protocols[] = { GST_SRT_DEFAULT_URI_SCHEME, NULL };

  return protocols;
}

static gchar *
gst_srt_src_uri_get_uri (GstURIHandler * handler)
{
  gchar *uri_str;
  GstSRTSrc *self = GST_SRT_SRC (handler);

  GST_OBJECT_LOCK (self);
  uri_str = gst_uri_to_string (self->srtobject->uri);
  GST_OBJECT_UNLOCK (self);

  return uri_str;
}

static gboolean
gst_srt_src_uri_set_uri (GstURIHandler * handler,
    const gchar * uri, GError ** error)
{
  GstSRTSrc *self = GST_SRT_SRC (handler);

  return gst_srt_object_set_uri (self->srtobject, uri, error);
}

static void
gst_srt_src_uri_handler_init (gpointer g_iface, gpointer iface_data)
{
  GstURIHandlerInterface *iface = (GstURIHandlerInterface *) g_iface;

  iface->get_type = gst_srt_src_uri_get_type;
  iface->get_protocols = gst_srt_src_uri_get_protocols;
  iface->get_uri = gst_srt_src_uri_get_uri;
  iface->set_uri = gst_srt_src_uri_set_uri;
}
