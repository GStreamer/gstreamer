/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) <2003> David Schleef <ds@schleef.org>
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

/*
 * This file was (probably) generated from gstvideotemplate.c,
 * $Id$
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

/*#define DEBUG_ENABLED */
#include <gstvideotemplate.h>
#include <string.h>

/* GstVideotemplate signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  /* FILL ME */
};

static void	gst_videotemplate_base_init	(gpointer g_class);
static void	gst_videotemplate_class_init	(gpointer g_class, gpointer class_data);
static void	gst_videotemplate_init		(GTypeInstance *instance, gpointer g_class);

static void	gst_videotemplate_set_property		(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void	gst_videotemplate_get_property		(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);

static void gst_videotemplate_planar411(GstVideofilter *videofilter, void *dest, void *src);
static void gst_videotemplate_setup(GstVideofilter *videofilter);

GType
gst_videotemplate_get_type (void)
{
  static GType videotemplate_type = 0;

  if (!videotemplate_type) {
    static const GTypeInfo videotemplate_info = {
      sizeof(GstVideotemplateClass),
      gst_videotemplate_base_init,
      NULL,
      gst_videotemplate_class_init,
      NULL,
      NULL,
      sizeof(GstVideotemplate),
      0,
      gst_videotemplate_init,
    };
    videotemplate_type = g_type_register_static(GST_TYPE_VIDEOFILTER,
        "GstVideotemplate", &videotemplate_info, 0);
  }
  return videotemplate_type;
}

static GstVideofilterFormat gst_videotemplate_formats[] = {
  { "I420", 12, gst_videotemplate_planar411, },
};

  
static void
gst_videotemplate_base_init (gpointer g_class)
{
  static GstElementDetails videotemplate_details = GST_ELEMENT_DETAILS (
    "Video Filter Template",
    "Filter/Effect/Video",
    "Template for a video filter",
    "David Schleef <ds@schleef.org>"
  );
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);
  GstVideofilterClass *videofilter_class = GST_VIDEOFILTER_CLASS (g_class);
  int i;
  
  gst_element_class_set_details (element_class, &videotemplate_details);

  for(i=0;i<G_N_ELEMENTS(gst_videotemplate_formats);i++){
    gst_videofilter_class_add_format(videofilter_class,
	gst_videotemplate_formats + i);
  }

  gst_videofilter_class_add_pad_templates (GST_VIDEOFILTER_CLASS (g_class));
}

static void
gst_videotemplate_class_init (gpointer g_class, gpointer class_data)
{
  GObjectClass *gobject_class;
  GstVideofilterClass *videofilter_class;

  gobject_class = G_OBJECT_CLASS (g_class);
  videofilter_class = GST_VIDEOFILTER_CLASS (g_class);

#if 0
  g_object_class_install_property(gobject_class, ARG_METHOD,
      g_param_spec_enum("method","method","method",
      GST_TYPE_VIDEOTEMPLATE_METHOD, GST_VIDEOTEMPLATE_METHOD_1,
      G_PARAM_READWRITE));
#endif

  gobject_class->set_property = gst_videotemplate_set_property;
  gobject_class->get_property = gst_videotemplate_get_property;

  videofilter_class->setup = gst_videotemplate_setup;
}

static void
gst_videotemplate_init (GTypeInstance *instance, gpointer g_class)
{
  GstVideotemplate *videotemplate = GST_VIDEOTEMPLATE (instance);
  GstVideofilter *videofilter;

  GST_DEBUG("gst_videotemplate_init");

  videofilter = GST_VIDEOFILTER(videotemplate);

  /* do stuff */
}

static void
gst_videotemplate_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  GstVideotemplate *src;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_VIDEOTEMPLATE(object));
  src = GST_VIDEOTEMPLATE(object);

  GST_DEBUG("gst_videotemplate_set_property");
  switch (prop_id) {
#if 0
    case ARG_METHOD:
      src->method = g_value_get_enum (value);
      break;
#endif
    default:
      break;
  }
}

static void
gst_videotemplate_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  GstVideotemplate *src;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_VIDEOTEMPLATE(object));
  src = GST_VIDEOTEMPLATE(object);

  switch (prop_id) {
#if 0
    case ARG_METHOD:
      g_value_set_enum (value, src->method);
      break;
#endif
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean plugin_init (GstPlugin *plugin)
{
  if(!gst_library_load("gstvideofilter"))
    return FALSE;

  return gst_element_register (plugin, "videotemplate", GST_RANK_NONE,
      GST_TYPE_VIDEOTEMPLATE);
}

GST_PLUGIN_DEFINE (
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "videotemplate",
  "Template for a video filter",
  plugin_init,
  VERSION,
  GST_LICENSE,
  GST_PACKAGE,
  GST_ORIGIN
)

static void gst_videotemplate_setup(GstVideofilter *videofilter)
{
  GstVideotemplate *videotemplate;

  g_return_if_fail(GST_IS_VIDEOTEMPLATE(videofilter));
  videotemplate = GST_VIDEOTEMPLATE(videofilter);

  /* if any setup needs to be done, do it here */

}

static void gst_videotemplate_planar411(GstVideofilter *videofilter,
    void *dest, void *src)
{
  GstVideotemplate *videotemplate;
  int width = gst_videofilter_get_input_width(videofilter);
  int height = gst_videofilter_get_input_height(videofilter);

  g_return_if_fail(GST_IS_VIDEOTEMPLATE(videofilter));
  videotemplate = GST_VIDEOTEMPLATE(videofilter);

  /* do something interesting here.  This simply copies the source
   * to the destination. */
  memcpy(dest,src,width * height + (width/2) * (height/2) * 2);
}

