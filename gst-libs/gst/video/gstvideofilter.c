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

static GstCaps *gst_videofilter_format_get_caps(GstVideofilterFormat *format)
{
  unsigned int fourcc;
  GstCaps *caps;

  if(format->filter_func==NULL)
    return NULL;

  fourcc = GST_MAKE_FOURCC(format->fourcc[0],format->fourcc[1],format->fourcc[2],format->fourcc[3]);

  if(format->bpp){
    caps = GST_CAPS_NEW ("videofilter", "video/x-raw-rgb",
		"format", GST_PROPS_FOURCC (fourcc),
		"depth", GST_PROPS_INT(format->bpp),
		"bpp", GST_PROPS_INT(format->depth),
		"endianness", GST_PROPS_INT(format->endianness),
		"red_mask", GST_PROPS_INT(format->red_mask),
		"green_mask", GST_PROPS_INT(format->green_mask),
		"blue_mask", GST_PROPS_INT(format->blue_mask));
  }else{
    caps = GST_CAPS_NEW ("videoflip", "video/x-raw-yuv",
		"format", GST_PROPS_FOURCC (fourcc),
		"height", GST_PROPS_INT_RANGE (1,G_MAXINT),
		"width", GST_PROPS_INT_RANGE (1,G_MAXINT),
		"framerate", GST_PROPS_FLOAT_RANGE (0,G_MAXFLOAT)
		);
  }

  return caps;
}

GstCaps * gst_videofilter_class_get_capslist(GstVideofilterClass *klass)
{
  static GstCaps *capslist = NULL;
  GstCaps *caps;
  int i;

  if (capslist){
    gst_caps_ref(capslist);
    return capslist;
  }

  for(i=0;i<klass->formats->len;i++){
    caps = gst_videofilter_format_get_caps(g_ptr_array_index(klass->formats,i));
    capslist = gst_caps_append(capslist, caps);
  }

  gst_caps_ref(capslist);
  return capslist;
}

static GstCaps *
gst_videofilter_sink_getcaps (GstPad *pad, GstCaps *caps)
{
  GstVideofilter *videofilter;
  GstVideofilterClass *klass;
  GstCaps *capslist = NULL;
  GstCaps *peercaps;
  GstCaps *sizecaps;
  int i;

  GST_DEBUG("gst_videofilter_sink_getcaps");
  videofilter = GST_VIDEOFILTER (gst_pad_get_parent (pad));
  
  klass = GST_VIDEOFILTER_CLASS(G_OBJECT_GET_CLASS(videofilter));

  /* get list of peer's caps */
  peercaps = gst_pad_get_allowed_caps (videofilter->srcpad);

  /* FIXME videofilter doesn't allow passthru of video formats it
   * doesn't understand. */
  /* Look through our list of caps and find those that match with
   * the peer's formats.  Create a list of them. */
  /* FIXME optimize if peercaps == NULL */
  for(i=0;i<klass->formats->len;i++){
    GstCaps *icaps;
    GstCaps *fromcaps = gst_videofilter_format_get_caps(g_ptr_array_index(
	  klass->formats,i));

    icaps = gst_caps_intersect(fromcaps, peercaps);
    //if(gst_caps_is_always_compatible(fromcaps, peercaps)){
    if(icaps != NULL){
      capslist = gst_caps_append(capslist, fromcaps);
    }
    //gst_caps_unref (fromcaps);
    if(icaps) gst_caps_unref (icaps);
  }
  gst_caps_unref (peercaps);

  sizecaps = GST_CAPS_NEW("videofilter_size","video/x-raw-yuv",
		"width", GST_PROPS_INT_RANGE (0, G_MAXINT),
		"height", GST_PROPS_INT_RANGE (0, G_MAXINT),
		"framerate", GST_PROPS_FLOAT_RANGE (0, G_MAXFLOAT));

  caps = gst_caps_intersect(capslist, sizecaps);
  gst_caps_unref (sizecaps);

  return caps;
}

static GstPadLinkReturn
gst_videofilter_src_link (GstPad *pad, GstCaps *caps)
{
  GstVideofilter *videofilter;
  GstPadLinkReturn ret;
  GstCaps *peercaps;

  GST_DEBUG("gst_videofilter_src_link");
  videofilter = GST_VIDEOFILTER (gst_pad_get_parent (pad));

  if (!GST_CAPS_IS_FIXED (caps)) {
    return GST_PAD_LINK_DELAYED;
  }

  gst_caps_debug(caps,"ack");

  videofilter->format = gst_videofilter_find_format_by_caps (videofilter,caps);
  g_return_val_if_fail(videofilter->format, GST_PAD_LINK_REFUSED);

  gst_caps_get_int (caps, "width", &videofilter->to_width);
  gst_caps_get_int (caps, "height", &videofilter->to_height);

  GST_DEBUG("width %d height %d",videofilter->to_width,videofilter->to_height);

  peercaps = gst_caps_copy(caps);

  gst_caps_set(peercaps, "width", GST_PROPS_INT_RANGE (0, G_MAXINT));
  gst_caps_set(peercaps, "height", GST_PROPS_INT_RANGE (0, G_MAXINT));

  ret = gst_pad_try_set_caps (videofilter->srcpad, peercaps);

  gst_caps_unref(peercaps);

  if(ret==GST_PAD_LINK_OK){
    caps = gst_pad_get_caps (videofilter->srcpad);

    gst_caps_get_int (caps, "width", &videofilter->from_width);
    gst_caps_get_int (caps, "height", &videofilter->from_height);
    //gst_videofilter_setup(videofilter);
  }

  return ret;
}

static GstPadLinkReturn
gst_videofilter_sink_link (GstPad *pad, GstCaps *caps)
{
  GstVideofilter *videofilter;
  GstPadLinkReturn ret;
  GstCaps *peercaps;

  GST_DEBUG("gst_videofilter_sink_link");
  videofilter = GST_VIDEOFILTER (gst_pad_get_parent (pad));

  if (!GST_CAPS_IS_FIXED (caps)) {
    return GST_PAD_LINK_DELAYED;
  }

  videofilter->format = gst_videofilter_find_format_by_caps (videofilter,caps);
  GST_DEBUG("sink_link: %s\n",gst_caps_to_string(caps));
  g_return_val_if_fail(videofilter->format, GST_PAD_LINK_REFUSED);

  gst_caps_get_int (caps, "width", &videofilter->from_width);
  gst_caps_get_int (caps, "height", &videofilter->from_height);
  gst_caps_get_float (caps, "framerate", &videofilter->framerate);

  gst_videofilter_setup(videofilter);

  peercaps = gst_caps_copy(caps);

  gst_caps_set(peercaps, "width", GST_PROPS_INT (videofilter->to_width));
  gst_caps_set(peercaps, "height", GST_PROPS_INT (videofilter->to_height));
  gst_caps_set(peercaps, "framerate", GST_PROPS_FLOAT (videofilter->framerate));

  GST_DEBUG("setting %s\n",gst_caps_to_string(peercaps));

  ret = gst_pad_try_set_caps (videofilter->srcpad, peercaps);

  //gst_caps_unref(peercaps);

  if(ret==GST_PAD_LINK_OK || ret==GST_PAD_LINK_DONE){
    caps = gst_pad_get_caps (videofilter->srcpad);

    //gst_caps_get_int (caps, "width", &videofilter->to_width);
    //gst_caps_get_int (caps, "height", &videofilter->to_height);
    //gst_videofilter_setup(videofilter);
  }

  return ret;
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
  gst_pad_set_link_function(videofilter->sinkpad,gst_videofilter_sink_link);
  gst_pad_set_getcaps_function(videofilter->sinkpad,gst_videofilter_sink_getcaps);

  pad_template = gst_element_class_get_pad_template(GST_ELEMENT_CLASS(g_class),
      "src");
  g_return_if_fail(pad_template != NULL);
  videofilter->srcpad = gst_pad_new_from_template(pad_template, "src");
  gst_element_add_pad(GST_ELEMENT(videofilter),videofilter->srcpad);
  gst_pad_set_link_function(videofilter->srcpad,gst_videofilter_src_link);
  //gst_pad_set_getcaps_function(videofilter->srcpad,gst_videofilter_src_getcaps);

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

  g_return_if_fail(GST_IS_VIDEOFILTER(videofilter));

  videofilter->to_width = width;
  videofilter->to_height = height;

  videofilter->to_buf_size = (videofilter->to_width * videofilter->to_height
      * videofilter->format->depth)/8;

  srccaps = gst_caps_copy(gst_pad_get_caps(videofilter->srcpad));

  if(!GST_CAPS_IS_FIXED(srccaps)){
    return;
  }

  gst_caps_set(srccaps, "width", GST_PROPS_INT (videofilter->to_width));
  gst_caps_set(srccaps, "height", GST_PROPS_INT (videofilter->to_height));

  ret = gst_pad_try_set_caps (videofilter->srcpad, srccaps);

  g_return_if_fail(ret<0);
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
      videofilter->format->depth) / 8;
  videofilter->to_buf_size = (videofilter->to_width * videofilter->to_height *
      videofilter->format->depth) / 8;

  videofilter->inited = TRUE;
}

GstVideofilterFormat *gst_videofilter_find_format_by_caps(GstVideofilter *videofilter,
    GstCaps *caps)
{
  int i;
  GstCaps *c;
  GstVideofilterClass *klass;
  GstVideofilterFormat *format;

  klass = GST_VIDEOFILTER_CLASS(G_OBJECT_GET_CLASS(videofilter));

  g_return_val_if_fail(caps != NULL, NULL);

  for(i=0;i<klass->formats->len;i++){
    format = g_ptr_array_index(klass->formats,i);
    c = gst_videofilter_format_get_caps(format);

    if(c){
      if(gst_caps_is_always_compatible(caps, c)){
	gst_caps_unref(c);
	return format;
      }
    }
    gst_caps_unref(c);
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
  GstCaps *caps;
  GstElementClass *element_class = GST_ELEMENT_CLASS (videofilter_class);

  caps = GST_CAPS_NEW("src","video/x-raw-yuv",
		"width", GST_PROPS_INT_RANGE (0, G_MAXINT),
		"height", GST_PROPS_INT_RANGE (0, G_MAXINT),
		"framerate", GST_PROPS_FLOAT_RANGE (0, G_MAXFLOAT));

  gst_element_class_add_pad_template (element_class,
      GST_PAD_TEMPLATE_NEW("src", GST_PAD_SRC, GST_PAD_ALWAYS, 
        gst_caps_intersect(caps,
          gst_videofilter_class_get_capslist (videofilter_class))));

  gst_element_class_add_pad_template (element_class,
      GST_PAD_TEMPLATE_NEW("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
        gst_caps_intersect(caps,
          gst_videofilter_class_get_capslist (videofilter_class))));
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
