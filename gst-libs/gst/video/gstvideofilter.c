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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
/*#define DEBUG_ENABLED */
#include <gstvideofilter.h>



/* GstVideofilter signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_METHOD,
  /* FILL ME */
};

static void     gst_videofilter_base_init       (gpointer g_class);
static void	gst_videofilter_class_init	(gpointer g_class, gpointer class_data);
static void	gst_videofilter_init		(GTypeInstance *instance, gpointer g_class);

static void	gst_videofilter_set_property		(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void	gst_videofilter_get_property		(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);

static void	gst_videofilter_chain		(GstPad *pad, GstData *_data);
GstCaps * gst_videofilter_class_get_capslist(GstVideofilterClass *klass);
static void gst_videofilter_setup(GstVideofilter *videofilter);

static GstElementClass *parent_class = NULL;

GType
gst_videofilter_get_type (void)
{
  static GType videofilter_type = 0;

  if (!videofilter_type) {
    static const GTypeInfo videofilter_info = {
      sizeof(GstVideofilterClass),
      gst_videofilter_base_init,
      NULL,
      gst_videofilter_class_init,
      NULL,
      NULL,
      sizeof(GstVideofilter),
      0,
      gst_videofilter_init,
    };
    videofilter_type = g_type_register_static(GST_TYPE_ELEMENT,
	"GstVideofilter", &videofilter_info, G_TYPE_FLAG_ABSTRACT);
  }
  return videofilter_type;
}

static void gst_videofilter_base_init (gpointer g_class)
{
  static GstElementDetails videofilter_details = {
    "Video scaler",
    "Filter/Effect/Video",
    "Resizes video",
    "David Schleef <ds@schleef.org>"
  };
  GstVideofilterClass *klass = (GstVideofilterClass *) g_class;
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  klass->formats = g_ptr_array_new();

  gst_element_class_set_details (element_class, &videofilter_details);
}

static void gst_videofilter_class_init (gpointer g_class, gpointer class_data)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstVideofilterClass *klass;

  klass = (GstVideofilterClass *)g_class;
  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  parent_class = g_type_class_ref(GST_TYPE_ELEMENT);

  gobject_class->set_property = gst_videofilter_set_property;
  gobject_class->get_property = gst_videofilter_get_property;
}

static GstStructure *gst_videofilter_format_get_structure(GstVideofilterFormat *format)
{
  unsigned int fourcc;
  GstStructure *structure;

  if(format->filter_func==NULL)
    return NULL;

  fourcc = GST_MAKE_FOURCC(format->fourcc[0],format->fourcc[1],format->fourcc[2],format->fourcc[3]);

  if(format->depth){
    structure = gst_structure_new ("video/x-raw-rgb",
	"depth", G_TYPE_INT, format->depth,
	"bpp", G_TYPE_INT, format->bpp,
	"endianness", G_TYPE_INT, format->endianness,
	"red_mask", G_TYPE_INT, format->red_mask,
	"green_mask", G_TYPE_INT, format->green_mask,
	"blue_mask", G_TYPE_INT, format->blue_mask, NULL);
  }else{
    structure = gst_structure_new ("video/x-raw-yuv",
	"format", GST_TYPE_FOURCC, fourcc, NULL);
  }

  gst_structure_set(structure,
      "height", GST_TYPE_INT_RANGE, 1, G_MAXINT,
      "width", GST_TYPE_INT_RANGE, 1, G_MAXINT,
      "framerate", GST_TYPE_DOUBLE_RANGE, 0.0, G_MAXDOUBLE,
      NULL);

  return structure;
}

GstCaps * gst_videofilter_class_get_capslist(GstVideofilterClass *klass)
{
  GstCaps *caps;
  GstStructure *structure;
  int i;

  caps = gst_caps_new_empty();
  for(i=0;i<klass->formats->len;i++){
    structure = gst_videofilter_format_get_structure(g_ptr_array_index(klass->formats,i));
    gst_caps_append_structure (caps, structure);
  }

  return caps;
}

static GstCaps *
gst_videofilter_getcaps (GstPad *pad)
{
  GstVideofilter *videofilter;
  GstVideofilterClass *klass;
  //GstCaps *caps;
  GstCaps *othercaps;
  GstPad *otherpad;
  //int i;

  GST_DEBUG("gst_videofilter_getcaps");
  videofilter = GST_VIDEOFILTER (gst_pad_get_parent (pad));
  
  klass = GST_VIDEOFILTER_CLASS(G_OBJECT_GET_CLASS(videofilter));

  otherpad = (pad == videofilter->srcpad) ? videofilter->sinkpad :
    videofilter->srcpad;

  othercaps = gst_pad_get_allowed_caps (otherpad);

  return othercaps;
#if 0
  /* FIXME videofilter doesn't allow passthru of video formats it
   * doesn't understand. */
  /* Look through our list of caps and find those that match with
   * the peer's formats.  Create a list of them. */
  /* FIXME optimize if peercaps == NULL */
  caps = gst_caps_new_empty ();
  for(i=0;i<klass->formats->len;i++){
    GstCaps *icaps;
    GstCaps *fromcaps;
    
    fromcaps = gst_caps_new_full (gst_videofilter_format_get_structure (
	  g_ptr_array_index (klass->formats,i)), NULL);

    icaps = gst_caps_intersect (fromcaps, peercaps);
    if(icaps != NULL){
      gst_caps_append (caps, fromcaps);
    } else {
      gst_caps_free (fromcaps);
    }
    if(icaps) gst_caps_free (icaps);
  }
  gst_caps_free (peercaps);

  return caps;
#endif
}

static GstPadLinkReturn
gst_videofilter_link (GstPad *pad, const GstCaps *caps)
{
  GstVideofilter *videofilter;
  GstStructure *structure;
  gboolean ret;
  int width, height;
  double framerate;
  GstPadLinkReturn lret;
  GstPad *otherpad;

  GST_DEBUG("gst_videofilter_src_link");
  videofilter = GST_VIDEOFILTER (gst_pad_get_parent (pad));

  otherpad = (pad == videofilter->srcpad) ? videofilter->sinkpad :
    videofilter->srcpad;

  structure = gst_caps_get_structure (caps, 0);

  videofilter->format = gst_videofilter_find_format_by_structure (
      videofilter, structure);
  g_return_val_if_fail(videofilter->format, GST_PAD_LINK_REFUSED);

  ret = gst_structure_get_int (structure, "width", &width);
  ret &= gst_structure_get_int (structure, "height", &height);
  ret &= gst_structure_get_double (structure, "framerate", &framerate);

  if (!ret) return GST_PAD_LINK_REFUSED;

  lret = gst_pad_try_set_caps (otherpad, caps);
  if (GST_PAD_LINK_FAILED (lret)) return lret;

  GST_DEBUG("width %d height %d",width,height);

#if 0
  if (pad == videofilter->srcpad) {
    videofilter->to_width = width;
    videofilter->to_height = height;
  } else {
    videofilter->from_width = width;
    videofilter->from_height = height;
  }
#endif
  videofilter->to_width = width;
  videofilter->to_height = height;
  videofilter->from_width = width;
  videofilter->from_height = height;
  videofilter->framerate = framerate;

  gst_videofilter_setup(videofilter);

  return GST_PAD_LINK_OK;
}

static void
gst_videofilter_init (GTypeInstance *instance, gpointer g_class)
{
  GstVideofilter *videofilter = GST_VIDEOFILTER (instance);
  GstPadTemplate *pad_template;

  GST_DEBUG("gst_videofilter_init");

  pad_template = gst_element_class_get_pad_template(GST_ELEMENT_CLASS(g_class),
      "sink");
  g_return_if_fail(pad_template != NULL);
  videofilter->sinkpad = gst_pad_new_from_template(pad_template, "sink");
  gst_element_add_pad(GST_ELEMENT(videofilter),videofilter->sinkpad);
  gst_pad_set_chain_function(videofilter->sinkpad,gst_videofilter_chain);
  gst_pad_set_link_function(videofilter->sinkpad,gst_videofilter_link);
  gst_pad_set_getcaps_function(videofilter->sinkpad,gst_videofilter_getcaps);

  pad_template = gst_element_class_get_pad_template(GST_ELEMENT_CLASS(g_class),
      "src");
  g_return_if_fail(pad_template != NULL);
  videofilter->srcpad = gst_pad_new_from_template(pad_template, "src");
  gst_element_add_pad(GST_ELEMENT(videofilter),videofilter->srcpad);
  gst_pad_set_link_function(videofilter->srcpad,gst_videofilter_link);
  gst_pad_set_getcaps_function(videofilter->srcpad,gst_videofilter_getcaps);

  videofilter->inited = FALSE;
}

static void
gst_videofilter_chain (GstPad *pad, GstData *_data)
{
  GstBuffer *buf = GST_BUFFER (_data);
  GstVideofilter *videofilter;
  guchar *data;
  gulong size;
  GstBuffer *outbuf;

  GST_DEBUG ("gst_videofilter_chain");

  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (buf != NULL);

  videofilter = GST_VIDEOFILTER (gst_pad_get_parent (pad));
  //g_return_if_fail (videofilter->inited);

  data = GST_BUFFER_DATA(buf);
  size = GST_BUFFER_SIZE(buf);

  if(videofilter->passthru){
    gst_pad_push(videofilter->srcpad, GST_DATA (buf));
    return;
  }

  GST_DEBUG ("gst_videofilter_chain: got buffer of %ld bytes in '%s'",size,
		              GST_OBJECT_NAME (videofilter));
 
  GST_DEBUG("size=%ld from=%dx%d to=%dx%d fromsize=%ld (should be %d) tosize=%d",
	size,
	videofilter->from_width, videofilter->from_height,
	videofilter->to_width, videofilter->to_height,
  	size, videofilter->from_buf_size,
  	videofilter->to_buf_size);

  g_return_if_fail (size == videofilter->from_buf_size);

  outbuf = gst_buffer_new();
  /* FIXME: handle bufferpools */
  GST_BUFFER_SIZE(outbuf) = videofilter->to_buf_size;
  GST_BUFFER_DATA(outbuf) = g_malloc (videofilter->to_buf_size);
  GST_BUFFER_TIMESTAMP(outbuf) = GST_BUFFER_TIMESTAMP(buf);

  g_return_if_fail(videofilter->format);
  GST_DEBUG ("format %s",videofilter->format->fourcc);

  videofilter->in_buf = buf;
  videofilter->out_buf = outbuf;

  videofilter->format->filter_func(videofilter, GST_BUFFER_DATA(outbuf), data);

  GST_DEBUG ("gst_videofilter_chain: pushing buffer of %d bytes in '%s'",GST_BUFFER_SIZE(outbuf),
	              GST_OBJECT_NAME (videofilter));

  gst_pad_push(videofilter->srcpad, GST_DATA (outbuf));

  gst_buffer_unref(buf);
}

static void
gst_videofilter_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  GstVideofilter *src;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_VIDEOFILTER(object));
  src = GST_VIDEOFILTER(object);

  GST_DEBUG("gst_videofilter_set_property");
  switch (prop_id) {
    default:
      break;
  }
}

static void
gst_videofilter_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  GstVideofilter *src;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_VIDEOFILTER(object));
  src = GST_VIDEOFILTER(object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

int gst_videofilter_get_input_width(GstVideofilter *videofilter)
{
  g_return_val_if_fail(GST_IS_VIDEOFILTER(videofilter),0);

  return videofilter->from_width;
}

int gst_videofilter_get_input_height(GstVideofilter *videofilter)
{
  g_return_val_if_fail(GST_IS_VIDEOFILTER(videofilter),0);

  return videofilter->from_height;
}

void gst_videofilter_set_output_size(GstVideofilter *videofilter,
    int width, int height)
{
  int ret;
  GstCaps *srccaps;
  GstStructure *structure;

  g_return_if_fail(GST_IS_VIDEOFILTER(videofilter));

  videofilter->to_width = width;
  videofilter->to_height = height;

  videofilter->to_buf_size = (videofilter->to_width * videofilter->to_height
      * videofilter->format->bpp)/8;

  srccaps = gst_caps_copy(gst_pad_get_caps(videofilter->srcpad));
  structure = gst_caps_get_structure (srccaps, 0);

  gst_structure_set (structure, "width", G_TYPE_INT, width,
      "height", G_TYPE_INT, height, NULL);

  ret = gst_pad_try_set_caps (videofilter->srcpad, srccaps);

  if (ret < 0) {
    g_critical ("could not set output size");
  }
}

static void gst_videofilter_setup(GstVideofilter *videofilter)
{
  GstVideofilterClass *klass;

  klass = GST_VIDEOFILTER_CLASS(G_OBJECT_GET_CLASS(videofilter));

  if(klass->setup){
    klass->setup(videofilter);
  }

  if(videofilter->to_width == 0){
    videofilter->to_width = videofilter->from_width;
  }
  if(videofilter->to_height == 0){
    videofilter->to_height = videofilter->from_height;
  }

  g_return_if_fail(videofilter->format != NULL);
  g_return_if_fail(videofilter->from_width > 0);
  g_return_if_fail(videofilter->from_height > 0);
  g_return_if_fail(videofilter->to_width > 0);
  g_return_if_fail(videofilter->to_height > 0);

  videofilter->from_buf_size = (videofilter->from_width * videofilter->from_height *
      videofilter->format->bpp) / 8;
  videofilter->to_buf_size = (videofilter->to_width * videofilter->to_height *
      videofilter->format->bpp) / 8;

  videofilter->inited = TRUE;
}

GstVideofilterFormat *gst_videofilter_find_format_by_structure (
    GstVideofilter *videofilter, const GstStructure *structure)
{
  int i;
  GstVideofilterClass *klass;
  GstVideofilterFormat *format;
  gboolean ret;

  klass = GST_VIDEOFILTER_CLASS(G_OBJECT_GET_CLASS(videofilter));

  g_return_val_if_fail(structure != NULL, NULL);

  if (strcmp (gst_structure_get_name (structure), "video/x-raw-yuv") == 0) {
    guint32 fourcc;

    ret = gst_structure_get_fourcc (structure, "format", &fourcc);
    if (!ret) return NULL;
    for(i=0;i<klass->formats->len;i++){
      guint32 format_fourcc;
      format = g_ptr_array_index(klass->formats,i);
      format_fourcc = GST_STR_FOURCC (format->fourcc);
      if (format->depth == 0 && format_fourcc == fourcc) {
        return format;
      }
    }
  } else if (strcmp (gst_structure_get_name (structure), "video/x-raw-rgb")
      == 0) {
    int bpp;
    int depth;
    int endianness;
    int red_mask;
    int green_mask;
    int blue_mask;

    ret = gst_structure_get_int (structure, "bpp", &bpp);
    ret &= gst_structure_get_int (structure, "depth", &depth);
    ret &= gst_structure_get_int (structure, "endianness", &endianness);
    ret &= gst_structure_get_int (structure, "red_mask", &red_mask);
    ret &= gst_structure_get_int (structure, "green_mask", &green_mask);
    ret &= gst_structure_get_int (structure, "blue_mask", &blue_mask);
    if (!ret) return NULL;
    for(i=0;i<klass->formats->len;i++){
      format = g_ptr_array_index(klass->formats,i);
      if (format->bpp == bpp && format->depth == depth &&
          format->endianness == endianness && format->red_mask == red_mask &&
          format->green_mask == green_mask && format->blue_mask == blue_mask) {
        return format;
      }
    }
  }

  return NULL;
}

void gst_videofilter_class_add_format(GstVideofilterClass *videofilterclass,
    GstVideofilterFormat *format)
{
  g_ptr_array_add(videofilterclass->formats, format);
}

void gst_videofilter_class_add_pad_templates (GstVideofilterClass *videofilter_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (videofilter_class);

  gst_element_class_add_pad_template (element_class,
      gst_pad_template_new("src", GST_PAD_SRC, GST_PAD_ALWAYS,
	gst_videofilter_class_get_capslist (videofilter_class)));

  gst_element_class_add_pad_template (element_class,
      gst_pad_template_new("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
	gst_videofilter_class_get_capslist (videofilter_class)));
}

static gboolean
plugin_init (GstPlugin *plugin)
{
  return TRUE;
}

GST_PLUGIN_DEFINE (
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "gstvideofilter",
  "Video filter parent class",
  plugin_init,
  VERSION,
  "LGPL",
  GST_PACKAGE,
  GST_ORIGIN
)
