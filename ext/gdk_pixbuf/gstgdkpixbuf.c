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
#include <string.h>

#include "gstgdkpixbuf.h"

static GstElementDetails plugin_details = {
  "GdkPixbuf image decoder",
  "Codec/Decoder/Image",
  "Decodes images in a video stream using GdkPixbuf",
  "David A. Schleef <ds@schleef.org>",
};

/* Filter signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_SILENT
};

GST_PAD_TEMPLATE_FACTORY (gst_gdk_pixbuf_sink_factory,
  "sink",
  GST_PAD_SINK,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW("gdk_pixbuf_sink", "image/png", NULL),
  GST_CAPS_NEW("gdk_pixbuf_sink", "image/jpeg", NULL),
  GST_CAPS_NEW("gdk_pixbuf_sink", "image/gif", NULL),
  GST_CAPS_NEW("gdk_pixbuf_sink", "image/x-icon", NULL),
  GST_CAPS_NEW("gdk_pixbuf_sink", "application/x-navi-animation", NULL),
  GST_CAPS_NEW("gdk_pixbuf_sink", "image/x-cmu-raster", NULL),
  GST_CAPS_NEW("gdk_pixbuf_sink", "image/x-sun-raster", NULL),
  GST_CAPS_NEW("gdk_pixbuf_sink", "image/x-pixmap", NULL),
  GST_CAPS_NEW("gdk_pixbuf_sink", "image/tiff", NULL),
  GST_CAPS_NEW("gdk_pixbuf_sink", "image/x-portable-anymap", NULL),
  GST_CAPS_NEW("gdk_pixbuf_sink", "image/x-portable-bitmap", NULL),
  GST_CAPS_NEW("gdk_pixbuf_sink", "image/x-portable-graymap", NULL),
  GST_CAPS_NEW("gdk_pixbuf_sink", "image/x-portable-pixmap", NULL),
  GST_CAPS_NEW("gdk_pixbuf_sink", "image/bmp", NULL),
  GST_CAPS_NEW("gdk_pixbuf_sink", "image/x-bmp", NULL),
  GST_CAPS_NEW("gdk_pixbuf_sink", "image/x-MS-bmp", NULL),
  GST_CAPS_NEW("gdk_pixbuf_sink", "image/vnd.wap.wbmp", NULL),
  GST_CAPS_NEW("gdk_pixbuf_sink", "image/x-bitmap", NULL),
  GST_CAPS_NEW("gdk_pixbuf_sink", "image/x-tga", NULL)
);

GST_PAD_TEMPLATE_FACTORY (gst_gdk_pixbuf_src_factory,
  "src",
  GST_PAD_SRC,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW(
    "gdk_pixbuf_src",
    "video/x-raw-rgb",
      "width", GST_PROPS_INT_RANGE(1,INT_MAX),
      "height", GST_PROPS_INT_RANGE(1,INT_MAX),
      /* well, it's needed for connectivity but this
       * doesn't really make sense... */
      "framerate", GST_PROPS_FLOAT_RANGE(0, G_MAXFLOAT),
      "bpp", GST_PROPS_INT(32),
      "depth", GST_PROPS_INT(24),
      "endianness", GST_PROPS_INT(G_BIG_ENDIAN),
      "red_mask", GST_PROPS_INT(0x00ff0000),
      "green_mask", GST_PROPS_INT(0x0000ff00),
      "blue_mask", GST_PROPS_INT(0x000000ff)
  )
);

static void     gst_gdk_pixbuf_base_init (gpointer g_class);
static void	gst_gdk_pixbuf_class_init	(GstGdkPixbufClass *klass);
static void	gst_gdk_pixbuf_init	(GstGdkPixbuf *filter);

static void	gst_gdk_pixbuf_set_property(GObject *object, guint prop_id,
                                                 const GValue *value,
					         GParamSpec *pspec);
static void	gst_gdk_pixbuf_get_property(GObject *object, guint prop_id,
                                                 GValue *value,
						 GParamSpec *pspec);

static void	gst_gdk_pixbuf_chain	(GstPad *pad, GstData *_data);
static void     gst_gdk_pixbuf_type_find (GstTypeFind *tf, gpointer ignore);

static GstElementClass *parent_class = NULL;

static GstPadLinkReturn
gst_gdk_pixbuf_sink_link (GstPad *pad, GstCaps *caps)
{
  GstGdkPixbuf *filter;

  filter = GST_GDK_PIXBUF (gst_pad_get_parent (pad));
  g_return_val_if_fail (filter != NULL, GST_PAD_LINK_REFUSED);
  g_return_val_if_fail (GST_IS_GDK_PIXBUF (filter),
                        GST_PAD_LINK_REFUSED);

  if (GST_CAPS_IS_FIXED (caps))
  {
    return GST_PAD_LINK_OK;
  }

  return GST_PAD_LINK_DELAYED;
}

#if GDK_PIXBUF_MAJOR == 2 && GDK_PIXBUF_MINOR < 2
/* gdk-pixbuf prior to 2.2 didn't have gdk_pixbuf_get_formats().
 * These are just the formats that gdk-pixbuf is known to support.
 * But maybe not -- it may have been compiled without an external
 * library. */
static GstCaps *gst_gdk_pixbuf_get_capslist(void)
{
  GstCaps *capslist;

  capslist = gst_caps_chain(
    GST_CAPS_NEW("gdk_pixbuf_sink", "image/png", NULL),
    GST_CAPS_NEW("gdk_pixbuf_sink", "image/jpeg", NULL),
    GST_CAPS_NEW("gdk_pixbuf_sink", "image/gif", NULL),
    GST_CAPS_NEW("gdk_pixbuf_sink", "image/x-icon", NULL),
    GST_CAPS_NEW("gdk_pixbuf_sink", "application/x-navi-animation", NULL),
    GST_CAPS_NEW("gdk_pixbuf_sink", "image/x-cmu-raster", NULL),
    GST_CAPS_NEW("gdk_pixbuf_sink", "image/x-sun-raster", NULL),
    GST_CAPS_NEW("gdk_pixbuf_sink", "image/x-pixmap", NULL),
    GST_CAPS_NEW("gdk_pixbuf_sink", "image/tiff", NULL),
    GST_CAPS_NEW("gdk_pixbuf_sink", "image/x-portable-anymap", NULL),
    GST_CAPS_NEW("gdk_pixbuf_sink", "image/x-portable-bitmap", NULL),
    GST_CAPS_NEW("gdk_pixbuf_sink", "image/x-portable-graymap", NULL),
    GST_CAPS_NEW("gdk_pixbuf_sink", "image/x-portable-pixmap", NULL),
    GST_CAPS_NEW("gdk_pixbuf_sink", "image/bmp", NULL),
    GST_CAPS_NEW("gdk_pixbuf_sink", "image/x-bmp", NULL),
    GST_CAPS_NEW("gdk_pixbuf_sink", "image/x-MS-bmp", NULL),
    GST_CAPS_NEW("gdk_pixbuf_sink", "image/vnd.wap.wbmp", NULL),
    GST_CAPS_NEW("gdk_pixbuf_sink", "image/x-bitmap", NULL),
    GST_CAPS_NEW("gdk_pixbuf_sink", "image/x-tga", NULL),
    NULL);

  return capslist;
}
#else
static GstCaps *gst_gdk_pixbuf_get_capslist(void)
{
  GSList *slist;
  GSList *slist0;
  GdkPixbufFormat *pixbuf_format;
  char **mimetypes;
  char **mimetype;
  static GstCaps *capslist = NULL;

  if(capslist==NULL){
    slist0 = gdk_pixbuf_get_formats();

    for(slist = slist0;slist;slist=g_slist_next(slist)){
      pixbuf_format = slist->data;
      mimetypes = gdk_pixbuf_format_get_mime_types(pixbuf_format);
      for(mimetype = mimetypes; *mimetype; mimetype++){
        capslist = gst_caps_append(capslist, gst_caps_new("ack",*mimetype,NULL));
      }
      g_free(mimetypes);
    }
    gst_caps_ref(capslist);
    gst_caps_sink(capslist);
    g_slist_free(slist0);

    g_print("%s\n",gst_caps_to_string(capslist));
  }

  return capslist;
}
#endif

static GstCaps *gst_gdk_pixbuf_sink_getcaps(GstPad *pad, GstCaps *caps)
{
  GstGdkPixbuf *filter;

  filter = GST_GDK_PIXBUF (gst_pad_get_parent (pad));
  g_return_val_if_fail (filter != NULL, NULL);
  g_return_val_if_fail (GST_IS_GDK_PIXBUF (filter), NULL);

  return gst_gdk_pixbuf_get_capslist();
}

GType
gst_gdk_pixbuf_get_type (void)
{
  static GType plugin_type = 0;

  if (!plugin_type)
  {
    static const GTypeInfo plugin_info =
    {
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
	                                  "GstGdkPixbuf",
	                                  &plugin_info, 0);
  }
  return plugin_type;
}

static void
gst_gdk_pixbuf_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class, gst_gdk_pixbuf_src_factory ());
  gst_element_class_add_pad_template (element_class, gst_gdk_pixbuf_sink_factory ());
  gst_element_class_set_details (element_class, &plugin_details);
}

/* initialize the plugin's class */
static void
gst_gdk_pixbuf_class_init (GstGdkPixbufClass *klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*) klass;
  gstelement_class = (GstElementClass*) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  g_object_class_install_property (gobject_class, ARG_SILENT,
    g_param_spec_boolean ("silent", "Silent", "Produce verbose output ?",
                          FALSE, G_PARAM_READWRITE));

  gobject_class->set_property = gst_gdk_pixbuf_set_property;
  gobject_class->get_property = gst_gdk_pixbuf_get_property;
}

static void
gst_gdk_pixbuf_init (GstGdkPixbuf *filter)
{
  filter->sinkpad = gst_pad_new_from_template (gst_gdk_pixbuf_sink_factory (),
                                               "sink");
  gst_pad_set_link_function (filter->sinkpad, gst_gdk_pixbuf_sink_link);
  gst_pad_set_getcaps_function (filter->sinkpad, gst_gdk_pixbuf_sink_getcaps);
  filter->srcpad = gst_pad_new_from_template (gst_gdk_pixbuf_src_factory (),
                                              "src");

  gst_element_add_pad (GST_ELEMENT (filter), filter->sinkpad);
  gst_element_add_pad (GST_ELEMENT (filter), filter->srcpad);
  gst_pad_set_chain_function (filter->sinkpad, gst_gdk_pixbuf_chain);

}

static void
gst_gdk_pixbuf_chain (GstPad *pad, GstData *_data)
{
  GstBuffer *buf = GST_BUFFER (_data);
  GstGdkPixbuf *filter;
  GdkPixbufLoader *pixbuf_loader;
  GdkPixbuf *pixbuf;
  GstBuffer *outbuf;
  GError *error = NULL;

  g_print("gst_gdk_pixbuf_chain\n");

  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (buf != NULL);

  filter = GST_GDK_PIXBUF (GST_OBJECT_PARENT (pad));
  g_return_if_fail (GST_IS_GDK_PIXBUF (filter));

  pixbuf_loader = gdk_pixbuf_loader_new();
  //pixbuf_loader = gdk_pixbuf_loader_new_with_type("gif");
  
  gdk_pixbuf_loader_write(pixbuf_loader, GST_BUFFER_DATA(buf),
      GST_BUFFER_SIZE(buf), &error);
  gdk_pixbuf_loader_close(pixbuf_loader, &error);
  pixbuf = gdk_pixbuf_loader_get_pixbuf(pixbuf_loader);

  g_print("width=%d height=%d\n", gdk_pixbuf_get_width(pixbuf),
      gdk_pixbuf_get_height(pixbuf));

  if(filter->image_size == 0){
    GstCaps *caps;

    filter->width = gdk_pixbuf_get_width(pixbuf);
    filter->height = gdk_pixbuf_get_height(pixbuf);
    filter->rowstride = gdk_pixbuf_get_rowstride(pixbuf);
    filter->image_size = filter->rowstride * filter->height;

    caps = gst_pad_get_caps(filter->srcpad);
    gst_caps_set(caps, "width", GST_PROPS_INT(filter->width));
    gst_caps_set(caps, "height", GST_PROPS_INT(filter->height));
    gst_caps_set(caps, "framerate", GST_PROPS_FLOAT(0.));

    gst_pad_try_set_caps(filter->srcpad, caps);
  }

  outbuf = gst_buffer_new();
  GST_BUFFER_TIMESTAMP(outbuf) = GST_BUFFER_TIMESTAMP(buf);
  GST_BUFFER_DURATION(outbuf) = GST_BUFFER_DURATION(buf);
  
  GST_BUFFER_SIZE(outbuf) = filter->image_size;
  GST_BUFFER_DATA(outbuf) = g_malloc(filter->image_size);

  memcpy(GST_BUFFER_DATA(outbuf), gdk_pixbuf_get_pixels(pixbuf),
      filter->image_size);

  g_object_unref(G_OBJECT(pixbuf_loader));

  gst_pad_push (filter->srcpad, GST_DATA (outbuf));
}

static void
gst_gdk_pixbuf_set_property (GObject *object, guint prop_id,
                                  const GValue *value, GParamSpec *pspec)
{
  GstGdkPixbuf *filter;

  g_return_if_fail (GST_IS_GDK_PIXBUF (object));
  filter = GST_GDK_PIXBUF (object);

  switch (prop_id)
  {
  case ARG_SILENT:
    //filter->silent = g_value_get_boolean (value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    break;
  }
}

static void
gst_gdk_pixbuf_get_property (GObject *object, guint prop_id,
                                  GValue *value, GParamSpec *pspec)
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

static void
gst_gdk_pixbuf_type_find (GstTypeFind *tf, gpointer ignore)
{
  guint8 *data;
  GdkPixbufLoader *pixbuf_loader;
  GdkPixbufFormat *format;

  data = gst_type_find_peek (tf, 0, GST_GDK_PIXBUF_TYPE_FIND_SIZE);
  if (data == NULL) return;

  GST_DEBUG ("gst_gdk_pixbuf_type_find");

  pixbuf_loader = gdk_pixbuf_loader_new();
  
  gdk_pixbuf_loader_write (pixbuf_loader, data, GST_GDK_PIXBUF_TYPE_FIND_SIZE,
      NULL);
  
  format = gdk_pixbuf_loader_get_format (pixbuf_loader);

  if (format != NULL) {
    gchar **mlist = gdk_pixbuf_format_get_mime_types(format);

    gst_type_find_suggest (tf, GST_TYPE_FIND_MINIMUM,
        gst_caps_new ("gdk_pixbuf_type_find", mlist[0], NULL));
  }

  gdk_pixbuf_loader_close (pixbuf_loader, NULL);
  g_object_unref (G_OBJECT (pixbuf_loader));
}

/* entry point to initialize the plug-in
 * initialize the plug-in itself
 * register the element factories and pad templates
 * register the features
 */
static gboolean
plugin_init (GstPlugin *plugin)
{
  if (!gst_element_register (plugin, "gdkpixbufdec", GST_RANK_NONE, GST_TYPE_GDK_PIXBUF))
    return FALSE;

  gst_type_find_register (plugin, "image/*", GST_RANK_MARGINAL,
			  gst_gdk_pixbuf_type_find, NULL, GST_CAPS_ANY, NULL);

  /* plugin initialisation succeeded */
  return TRUE;
}

/* this is the structure that gst-register looks for
 * so keep the name plugin_desc, or you cannot get your plug-in registered */
GST_PLUGIN_DEFINE (
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "gdkpixbuf",
  "GDK Pixbuf decoder",
  plugin_init,
  VERSION,
  "LGPL",
  GST_PACKAGE,
  GST_ORIGIN)
