/*
 * gstgdkpixbuf.c
 * GStreamer
 * Copyright (C) 1999-2001 Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) 2003 David A. Schleef <ds@schleef.org>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <gst/gst.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gst/video/video.h>
#include <string.h>

#include "gstgdkpixbuf.h"

GST_DEBUG_CATEGORY_STATIC (gst_gdk_pixbuf_debug);
#define GST_CAT_DEFAULT gst_gdk_pixbuf_debug

static GstElementDetails plugin_details = {
  "GdkPixbuf image decoder",
  "Codec/Decoder/Image",
  "Decodes images in a video stream using GdkPixbuf",
  "David A. Schleef <ds@schleef.org>",
};

/* Filter signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_SILENT
};

static GstStaticPadTemplate gst_gdk_pixbuf_sink_template =
    GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("image/png; "
        "image/jpeg; "
        "image/gif; "
        "image/x-icon; "
        "application/x-navi-animation; "
        "image/x-cmu-raster; "
        "image/x-sun-raster; "
        "image/x-pixmap; "
        "image/tiff; "
        "image/x-portable-anymap; "
        "image/x-portable-bitmap; "
        "image/x-portable-graymap; "
        "image/x-portable-pixmap; "
        "image/bmp; "
        "image/x-bmp; "
        "image/x-MS-bmp; "
        "image/vnd.wap.wbmp; " "image/x-bitmap; " "image/x-tga")
    );

static GstStaticPadTemplate gst_gdk_pixbuf_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_RGB)
    );

static void gst_gdk_pixbuf_base_init (gpointer g_class);
static void gst_gdk_pixbuf_class_init (GstGdkPixbufClass * klass);
static void gst_gdk_pixbuf_init (GstGdkPixbuf * filter);

static void gst_gdk_pixbuf_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_gdk_pixbuf_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static void gst_gdk_pixbuf_chain (GstPad * pad, GstData * _data);

#ifdef enable_typefind
static void gst_gdk_pixbuf_type_find (GstTypeFind * tf, gpointer ignore);
#endif

static GstElementClass *parent_class = NULL;

static GstPadLinkReturn
gst_gdk_pixbuf_sink_link (GstPad * pad, const GstCaps * caps)
{
  GstGdkPixbuf *filter;

  filter = GST_GDK_PIXBUF (gst_pad_get_parent (pad));
  g_return_val_if_fail (filter != NULL, GST_PAD_LINK_REFUSED);
  g_return_val_if_fail (GST_IS_GDK_PIXBUF (filter), GST_PAD_LINK_REFUSED);

  filter->framerate = 1.0;
  gst_structure_get_double (gst_caps_get_structure (caps, 0), "framerate",
      &filter->framerate);

  return GST_PAD_LINK_OK;
}

#if GDK_PIXBUF_MAJOR == 2 && GDK_PIXBUF_MINOR < 2
/* gdk-pixbuf prior to 2.2 didn't have gdk_pixbuf_get_formats().
 * These are just the formats that gdk-pixbuf is known to support.
 * But maybe not -- it may have been compiled without an external
 * library. */
static GstCaps *
gst_gdk_pixbuf_get_capslist (void)
{
  return gst_caps_copy (gst_static_caps_get (&gst_gdk_pixbuf_sink_template.
          static_caps));
}
#else
static GstCaps *
gst_gdk_pixbuf_get_capslist (void)
{
  GSList *slist;
  GSList *slist0;
  GdkPixbufFormat *pixbuf_format;
  char **mimetypes;
  char **mimetype;
  GstCaps *capslist = NULL;

  capslist = gst_caps_new_empty ();
  slist0 = gdk_pixbuf_get_formats ();

  for (slist = slist0; slist; slist = g_slist_next (slist)) {
    pixbuf_format = slist->data;
    mimetypes = gdk_pixbuf_format_get_mime_types (pixbuf_format);
    for (mimetype = mimetypes; *mimetype; mimetype++) {
      gst_caps_append_structure (capslist, gst_structure_new (*mimetype, NULL));
    }
    g_free (mimetypes);
  }
  g_slist_free (slist0);

  return capslist;
}
#endif

static GstCaps *
gst_gdk_pixbuf_sink_getcaps (GstPad * pad)
{
  GstGdkPixbuf *filter;

  filter = GST_GDK_PIXBUF (gst_pad_get_parent (pad));
  g_return_val_if_fail (filter != NULL, NULL);
  g_return_val_if_fail (GST_IS_GDK_PIXBUF (filter), NULL);

  return gst_gdk_pixbuf_get_capslist ();
}

GType
gst_gdk_pixbuf_get_type (void)
{
  static GType plugin_type = 0;

  if (!plugin_type) {
    static const GTypeInfo plugin_info = {
      sizeof (GstGdkPixbufClass),
      gst_gdk_pixbuf_base_init,
      NULL,
      (GClassInitFunc) gst_gdk_pixbuf_class_init,
      NULL,
      NULL,
      sizeof (GstGdkPixbuf),
      0,
      (GInstanceInitFunc) gst_gdk_pixbuf_init,
    };

    plugin_type = g_type_register_static (GST_TYPE_ELEMENT,
        "GstGdkPixbuf", &plugin_info, 0);
  }
  return plugin_type;
}

static void
gst_gdk_pixbuf_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_gdk_pixbuf_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_gdk_pixbuf_sink_template));
  gst_element_class_set_details (element_class, &plugin_details);
}

/* initialize the plugin's class */
static void
gst_gdk_pixbuf_class_init (GstGdkPixbufClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  g_object_class_install_property (gobject_class, ARG_SILENT,
      g_param_spec_boolean ("silent", "Silent", "Produce verbose output ?",
          FALSE, G_PARAM_READWRITE));

  gobject_class->set_property = gst_gdk_pixbuf_set_property;
  gobject_class->get_property = gst_gdk_pixbuf_get_property;
}

static void
gst_gdk_pixbuf_init (GstGdkPixbuf * filter)
{
  filter->sinkpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&gst_gdk_pixbuf_sink_template), "sink");
  gst_pad_set_link_function (filter->sinkpad, gst_gdk_pixbuf_sink_link);
  gst_pad_set_getcaps_function (filter->sinkpad, gst_gdk_pixbuf_sink_getcaps);

  filter->srcpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&gst_gdk_pixbuf_src_template), "src");
  gst_pad_use_explicit_caps (filter->srcpad);

  gst_element_add_pad (GST_ELEMENT (filter), filter->sinkpad);
  gst_element_add_pad (GST_ELEMENT (filter), filter->srcpad);
  gst_pad_set_chain_function (filter->sinkpad, gst_gdk_pixbuf_chain);

  GST_FLAG_SET (GST_ELEMENT (filter), GST_ELEMENT_EVENT_AWARE);
}

static void
gst_gdk_pixbuf_chain (GstPad * pad, GstData * _data)
{
  GstBuffer *buf = GST_BUFFER (_data);
  GstGdkPixbuf *filter;
  GError *error = NULL;
  gboolean push_buffer = FALSE;
  gboolean dump_buffer = FALSE;
  gboolean got_eos = FALSE;

  GST_DEBUG ("gst_gdk_pixbuf_chain");

  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (buf != NULL);

  filter = GST_GDK_PIXBUF (GST_OBJECT_PARENT (pad));
  g_return_if_fail (GST_IS_GDK_PIXBUF (filter));

  if (GST_IS_EVENT (_data)) {
    GstEvent *event = GST_EVENT (buf);

    switch (GST_EVENT_TYPE (event)) {
      case GST_EVENT_EOS:
        push_buffer = TRUE;
        got_eos = TRUE;
        break;
      case GST_EVENT_DISCONTINUOUS:
        dump_buffer = TRUE;
        break;
      default:
        gst_pad_event_default (pad, event);
        return;
    }
  }

  if (filter->last_timestamp != GST_BUFFER_TIMESTAMP (buf)) {
    push_buffer = TRUE;
  }

  if (push_buffer) {
    if (filter->pixbuf_loader != NULL) {
      GstBuffer *outbuf;
      GdkPixbuf *pixbuf;
      GError *error = NULL;

      if (!gdk_pixbuf_loader_close (filter->pixbuf_loader, &error)) {
        GST_ELEMENT_ERROR (filter, LIBRARY, SHUTDOWN, (NULL), (error->message));
        g_error_free (error);
        return;
      }

      pixbuf = gdk_pixbuf_loader_get_pixbuf (filter->pixbuf_loader);

      if (filter->image_size == 0) {
        GstCaps *caps;

        filter->width = gdk_pixbuf_get_width (pixbuf);
        filter->height = gdk_pixbuf_get_height (pixbuf);
        filter->rowstride = gdk_pixbuf_get_rowstride (pixbuf);
        filter->image_size = filter->rowstride * filter->height;

        caps = gst_caps_copy (gst_pad_get_pad_template_caps (filter->srcpad));
        gst_caps_set_simple (caps,
            "width", G_TYPE_INT, filter->width,
            "height", G_TYPE_INT, filter->height,
            "framerate", G_TYPE_DOUBLE, filter->framerate, NULL);

        gst_pad_set_explicit_caps (filter->srcpad, caps);
      }

      outbuf = gst_pad_alloc_buffer (filter->srcpad, GST_BUFFER_OFFSET_NONE,
          filter->image_size);
      GST_BUFFER_TIMESTAMP (outbuf) = GST_BUFFER_TIMESTAMP (buf);
      GST_BUFFER_DURATION (outbuf) = GST_BUFFER_DURATION (buf);

      memcpy (GST_BUFFER_DATA (outbuf), gdk_pixbuf_get_pixels (pixbuf),
          filter->image_size);

      gst_pad_push (filter->srcpad, GST_DATA (outbuf));

      g_object_unref (G_OBJECT (filter->pixbuf_loader));
      filter->pixbuf_loader = NULL;
      dump_buffer = FALSE;
    }
  }

  if (dump_buffer) {
    if (filter->pixbuf_loader != NULL) {
      gdk_pixbuf_loader_close (filter->pixbuf_loader, NULL);
      g_object_unref (G_OBJECT (filter->pixbuf_loader));
      filter->pixbuf_loader = NULL;
    }
  }

  if (GST_IS_BUFFER (_data)) {
    if (filter->pixbuf_loader == NULL) {
      filter->pixbuf_loader = gdk_pixbuf_loader_new ();
      filter->last_timestamp = GST_BUFFER_TIMESTAMP (buf);
    }

    gdk_pixbuf_loader_write (filter->pixbuf_loader, GST_BUFFER_DATA (buf),
        GST_BUFFER_SIZE (buf), &error);
    gst_buffer_unref (buf);
  }

  if (got_eos) {
    gst_pad_event_default (pad, GST_EVENT (_data));
  }
}

static void
gst_gdk_pixbuf_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstGdkPixbuf *filter;

  g_return_if_fail (GST_IS_GDK_PIXBUF (object));
  filter = GST_GDK_PIXBUF (object);

  switch (prop_id) {
    case ARG_SILENT:
      //filter->silent = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_gdk_pixbuf_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstGdkPixbuf *filter;

  g_return_if_fail (GST_IS_GDK_PIXBUF (object));
  filter = GST_GDK_PIXBUF (object);

  switch (prop_id) {
    case ARG_SILENT:
      //g_value_set_boolean (value, filter->silent);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

#define GST_GDK_PIXBUF_TYPE_FIND_SIZE 1024

#ifdef enable_typefind
static void
gst_gdk_pixbuf_type_find (GstTypeFind * tf, gpointer ignore)
{
  guint8 *data;
  GdkPixbufLoader *pixbuf_loader;
  GdkPixbufFormat *format;

  data = gst_type_find_peek (tf, 0, GST_GDK_PIXBUF_TYPE_FIND_SIZE);
  if (data == NULL)
    return;

  GST_DEBUG ("creating new loader");

  pixbuf_loader = gdk_pixbuf_loader_new ();

  gdk_pixbuf_loader_write (pixbuf_loader, data, GST_GDK_PIXBUF_TYPE_FIND_SIZE,
      NULL);

  format = gdk_pixbuf_loader_get_format (pixbuf_loader);

  if (format != NULL) {
    GstCaps *caps;
    gchar **p;
    gchar **mlist = gdk_pixbuf_format_get_mime_types (format);

    for (p = mlist; *p; ++p) {
      GST_DEBUG ("suggesting mime type %s", *p);
      caps = gst_caps_new_simple (*p, NULL);
      gst_type_find_suggest (tf, GST_TYPE_FIND_MINIMUM, caps);
      gst_caps_free (caps);
    }
    g_strfreev (mlist);
  }

  GST_DEBUG ("closing pixbuf loader, hope it doesn't hang ...");
  /* librsvg 2.4.x has a bug where it triggers an endless loop in trying
     to close a gzip that's not an svg; fixed upstream but no good way
     to work around it */
  gdk_pixbuf_loader_close (pixbuf_loader, NULL);
  GST_DEBUG ("closed pixbuf loader");
  g_object_unref (G_OBJECT (pixbuf_loader));
}
#endif

/* entry point to initialize the plug-in
 * initialize the plug-in itself
 * register the element factories and pad templates
 * register the features
 */
static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_gdk_pixbuf_debug, "gdkpixbuf", 0,
      "gdk pixbuf loader");

  if (!gst_element_register (plugin, "gdkpixbufdec", GST_RANK_NONE,
          GST_TYPE_GDK_PIXBUF))
    return FALSE;

#ifdef enable_typefind
  gst_type_find_register (plugin, "image/*", GST_RANK_MARGINAL,
      gst_gdk_pixbuf_type_find, NULL, GST_CAPS_ANY, NULL);
#endif

  /* plugin initialisation succeeded */
  return TRUE;
}

/* this is the structure that gst-register looks for
 * so keep the name plugin_desc, or you cannot get your plug-in registered */
GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "gdkpixbuf",
    "GDK Pixbuf decoder", plugin_init, VERSION, "LGPL", GST_PACKAGE, GST_ORIGIN)
