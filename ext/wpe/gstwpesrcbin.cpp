/* Copyright (C) <2018> Philippe Normand <philn@igalia.com>
 * Copyright (C) <2018> Žan Doberšek <zdobersek@igalia.com>
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
 * SECTION:element-wpesrc
 * @title: wpesrc
 *
 * FIXME The wpesrc element is used to produce a video texture representing a
 * web page rendered off-screen by WPE.
 *
 */

#include "gstwpesrcbin.h"
#include "gstwpevideosrc.h"
#include "WPEThreadedView.h"

struct _GstWpeSrc
{
  GstBin parent;

  GstElement *video_src;
};

enum
{
 PROP_0,
 PROP_LOCATION,
 PROP_DRAW_BACKGROUND
};

enum
{
 SIGNAL_LOAD_BYTES,
 LAST_SIGNAL
};

static guint gst_wpe_video_src_signals[LAST_SIGNAL] = { 0 };

static void gst_wpe_src_uri_handler_init (gpointer iface, gpointer data);

#define gst_wpe_src_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstWpeSrc, gst_wpe_src, GST_TYPE_BIN,
    G_IMPLEMENT_INTERFACE (GST_TYPE_URI_HANDLER, gst_wpe_src_uri_handler_init));

static GstStaticPadTemplate video_src_factory =
GST_STATIC_PAD_TEMPLATE ("video_src", GST_PAD_SRC, GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw(memory:GLMemory), "
                     "format = (string) RGBA, "
                     "width = " GST_VIDEO_SIZE_RANGE ", "
                     "height = " GST_VIDEO_SIZE_RANGE ", "
                     "framerate = " GST_VIDEO_FPS_RANGE ", "
                     "pixel-aspect-ratio = (fraction)1/1,"
                     "texture-target = (string)2D"
#if ENABLE_SHM_BUFFER_SUPPORT
                     "; video/x-raw, "
                     "format = (string) BGRA, "
                     "width = " GST_VIDEO_SIZE_RANGE ", "
                     "height = " GST_VIDEO_SIZE_RANGE ", "
                     "framerate = " GST_VIDEO_FPS_RANGE ", "
                     "pixel-aspect-ratio = (fraction)1/1"
#endif
                     ));

static void
gst_wpe_src_load_bytes (GstWpeVideoSrc * src, GBytes * bytes)
{
  GstWpeSrc *self = GST_WPE_SRC (src);

  if (self->video_src)
    g_signal_emit_by_name (self->video_src, "load-bytes", bytes, NULL);
}

static void
gst_wpe_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstWpeSrc *self = GST_WPE_SRC (object);

  if (self->video_src)
    g_object_get_property (G_OBJECT (self->video_src), pspec->name, value);
}

static void
gst_wpe_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstWpeSrc *self = GST_WPE_SRC (object);

  if (self->video_src)
    g_object_set_property (G_OBJECT (self->video_src), pspec->name, value);
}

static GstURIType
gst_wpe_src_uri_get_type (GType)
{
  return GST_URI_SRC;
}

static const gchar *const *
gst_wpe_src_get_protocols (GType)
{
  static const char *protocols[] = { "wpe", NULL };
  return protocols;
}

static gchar *
gst_wpe_src_get_uri (GstURIHandler * handler)
{
  GstWpeSrc *src = GST_WPE_SRC (handler);
  const gchar *location;
  g_object_get (src->video_src, "location", &location, NULL);
  return g_strdup_printf ("wpe://%s", location);
}

static gboolean
gst_wpe_src_set_uri (GstURIHandler * handler, const gchar * uri,
    GError ** error)
{
  GstWpeSrc *src = GST_WPE_SRC (handler);

  g_object_set (src->video_src, "location", uri + 6, NULL);
  return TRUE;
}

static void
gst_wpe_src_uri_handler_init (gpointer iface_ptr, gpointer data)
{
  GstURIHandlerInterface *iface = (GstURIHandlerInterface *) iface_ptr;

  iface->get_type = gst_wpe_src_uri_get_type;
  iface->get_protocols = gst_wpe_src_get_protocols;
  iface->get_uri = gst_wpe_src_get_uri;
  iface->set_uri = gst_wpe_src_set_uri;
}

static void
gst_wpe_src_init (GstWpeSrc * src)
{
  src->video_src = gst_element_factory_make ("wpevideosrc", NULL);

  gst_bin_add (GST_BIN_CAST (src), src->video_src);

  GstPad *pad =
      gst_element_get_static_pad (GST_ELEMENT_CAST (src->video_src), "src");

  GstPad *ghost_pad = gst_ghost_pad_new_from_template ("video_src", pad,
      gst_static_pad_template_get (&video_src_factory));
  GstProxyPad *proxy_pad =
      gst_proxy_pad_get_internal (GST_PROXY_PAD (ghost_pad));
  gst_pad_set_active (GST_PAD_CAST (proxy_pad), TRUE);
  gst_object_unref (proxy_pad);

  gst_element_add_pad (GST_ELEMENT_CAST (src), GST_PAD_CAST (ghost_pad));

  gst_object_unref (pad);
}

static void
gst_wpe_src_class_init (GstWpeSrcClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->set_property = gst_wpe_src_set_property;
  gobject_class->get_property = gst_wpe_src_get_property;

  g_object_class_install_property (gobject_class, PROP_LOCATION,
      g_param_spec_string ("location", "location", "The URL to display", "",
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (gobject_class, PROP_DRAW_BACKGROUND,
      g_param_spec_boolean ("draw-background", "Draws the background",
          "Whether to draw the WebView background", TRUE,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  gst_element_class_set_static_metadata (element_class, "WPE source",
      "Source/Video", "Creates a video stream from a WPE browser",
      "Philippe Normand <philn@igalia.com>, Žan Doberšek "
      "<zdobersek@igalia.com>");

  /**
   * GstWpeSrc::load-bytes:
   * @src: the object which received the signal
   * @bytes: the GBytes data to load
   *
   * Load the specified bytes into the internal webView.
   */
  gst_wpe_video_src_signals[SIGNAL_LOAD_BYTES] =
      g_signal_new_class_handler ("load-bytes", G_TYPE_FROM_CLASS (klass),
      static_cast < GSignalFlags > (G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION),
      G_CALLBACK (gst_wpe_src_load_bytes), NULL, NULL, NULL, G_TYPE_NONE, 1,
      G_TYPE_BYTES);

  gst_element_class_set_static_metadata (element_class, "WPE source",
      "Source/Video", "Creates a video stream from a WPE browser",
      "Philippe Normand <philn@igalia.com>, Žan Doberšek "
      "<zdobersek@igalia.com>");

  gst_element_class_add_static_pad_template (element_class, &video_src_factory);
}
