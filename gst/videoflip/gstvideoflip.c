/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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


/*#define DEBUG_ENABLED */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <gstvideoflip.h>
#include <videoflip.h>



/* elementfactory information */
static GstElementDetails videoflip_details = GST_ELEMENT_DETAILS (
  "Video scaler",
  "Filter/Effect/Video",
  "Resizes video",
  "Wim Taymans <wim.taymans@chello.be>"
);

/* GstVideoflip signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_METHOD,
  /* FILL ME */
};

static void	gst_videoflip_base_init		(gpointer g_class);
static void	gst_videoflip_class_init	(GstVideoflipClass *klass);
static void	gst_videoflip_init		(GstVideoflip *videoflip);

static void	gst_videoflip_set_property		(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void	gst_videoflip_get_property		(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);

static void	gst_videoflip_chain		(GstPad *pad, GstData *_data);
static GstCaps * gst_videoflip_get_capslist(void);


static GstElementClass *parent_class = NULL;

#define GST_TYPE_VIDEOFLIP_METHOD (gst_videoflip_method_get_type())

static GType
gst_videoflip_method_get_type(void)
{
  static GType videoflip_method_type = 0;
  static GEnumValue videoflip_methods[] = {
    { GST_VIDEOFLIP_METHOD_IDENTITY,	"0", "Identity (no rotation)" },
    { GST_VIDEOFLIP_METHOD_90R,		"1", "Rotate right 90 degrees" },
    { GST_VIDEOFLIP_METHOD_180,		"2", "Rotate 180 degrees" },
    { GST_VIDEOFLIP_METHOD_90L,		"3", "Rotate left 90 degrees" },
    { GST_VIDEOFLIP_METHOD_HORIZ,	"4", "Flip horizontally" },
    { GST_VIDEOFLIP_METHOD_VERT,	"5", "Flip vertically" },
    { GST_VIDEOFLIP_METHOD_TRANS,	"6", "Flip across upper left/lower right diagonal" },
    { GST_VIDEOFLIP_METHOD_OTHER,	"7", "Flip across upper right/lower left diagonal" },
    { 0, NULL, NULL },
  };
  if(!videoflip_method_type){
    videoflip_method_type = g_enum_register_static("GstVideoflipMethod",
	videoflip_methods);
  }
  return videoflip_method_type;
}

static GstPadTemplate *
gst_videoflip_src_template_factory(void)
{
  /* well, actually RGB too, but since there's no RGB format anyway */
  GstCaps *caps = gst_caps_from_string ("video/x-raw-yuv, "
	      "width = (int) [ 0, MAX ], "
	      "height = (int) [ 0, MAX ], "
	      "framerate = (double) [ 0, MAX ]");

  caps = gst_caps_intersect(caps, gst_videoflip_get_capslist ());

  return gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS, caps);
}

static GstPadTemplate *
gst_videoflip_sink_template_factory(void)
{
  GstCaps *caps = gst_caps_from_string ("video/x-raw-yuv, "
	      "width = (int) [ 0, MAX ], "
	      "height = (int) [ 0, MAX ], "
	      "framerate = (double) [ 0, MAX ]");

  caps = gst_caps_intersect(caps, gst_videoflip_get_capslist ());

  return gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS, caps);
}

GType
gst_videoflip_get_type (void)
{
  static GType videoflip_type = 0;

  if (!videoflip_type) {
    static const GTypeInfo videoflip_info = {
      sizeof(GstVideoflipClass),
      gst_videoflip_base_init,
      NULL,
      (GClassInitFunc)gst_videoflip_class_init,
      NULL,
      NULL,
      sizeof(GstVideoflip),
      0,
      (GInstanceInitFunc)gst_videoflip_init,
    };
    videoflip_type = g_type_register_static(GST_TYPE_ELEMENT, "GstVideoflip", &videoflip_info, 0);
  }
  return videoflip_type;
}

static void
gst_videoflip_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details (element_class, &videoflip_details);

  gst_element_class_add_pad_template (element_class,
      gst_videoflip_sink_template_factory ());
  gst_element_class_add_pad_template (element_class,
      gst_videoflip_src_template_factory ());
}
static void
gst_videoflip_class_init (GstVideoflipClass *klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_METHOD,
      g_param_spec_enum("method","method","method",
      GST_TYPE_VIDEOFLIP_METHOD, GST_VIDEOFLIP_METHOD_90R,
      G_PARAM_READWRITE));

  parent_class = g_type_class_ref(GST_TYPE_ELEMENT);

  gobject_class->set_property = gst_videoflip_set_property;
  gobject_class->get_property = gst_videoflip_get_property;

}

static GstCaps *
gst_videoflip_get_capslist(void)
{
  GstCaps *caps;
  GstStructure *structure;
  int i;

  caps = gst_caps_new_empty ();
  for(i=0;i<videoflip_n_formats;i++){
    structure = videoflip_get_cap (videoflip_formats + i);
    gst_caps_append_structure (caps, structure);
  }

  return caps;
}

static GstCaps *
gst_videoflip_sink_getcaps (GstPad *pad)
{
  GstVideoflip *videoflip;
  GstCaps *capslist = NULL;
  GstCaps *peercaps;
  GstCaps *sizecaps;
  GstCaps *caps;
  int i;

  GST_DEBUG ("gst_videoflip_sink_getcaps");
  videoflip = GST_VIDEOFLIP (gst_pad_get_parent (pad));
  
  /* get list of peer's caps */
  if(pad == videoflip->srcpad){
    peercaps = gst_pad_get_allowed_caps (videoflip->sinkpad);
  }else{
    peercaps = gst_pad_get_allowed_caps (videoflip->srcpad);
  }

  /* FIXME videoflip doesn't allow passthru of video formats it
   * doesn't understand. */
  /* Look through our list of caps and find those that match with
   * the peer's formats.  Create a list of them. */
  for(i=0;i<videoflip_n_formats;i++){
    GstCaps *fromcaps = gst_caps_new_full(videoflip_get_cap(
	  videoflip_formats + i), NULL);
    if(gst_caps_is_always_compatible(fromcaps, peercaps)){
      gst_caps_append(capslist, fromcaps);
    }
  }
  gst_caps_free (peercaps);

  sizecaps = gst_caps_from_string ("video/x-raw-yuv, "
	      "width = (int) [ 0, MAX ], "
	      "height = (int) [ 0, MAX ], "
	      "framerate = (double) [ 0, MAX ]");

  caps = gst_caps_intersect(capslist, sizecaps);
  gst_caps_free (sizecaps);

  return caps;
}


static GstPadLinkReturn
gst_videoflip_src_link (GstPad *pad, const GstCaps *caps)
{
  GstVideoflip *videoflip;
  GstStructure *structure;
  gboolean ret;

  GST_DEBUG ("gst_videoflip_src_link");
  videoflip = GST_VIDEOFLIP (gst_pad_get_parent (pad));
  
  structure = gst_caps_get_structure (caps, 0);

  videoflip->format = videoflip_find_by_caps (caps);
  g_return_val_if_fail(videoflip->format, GST_PAD_LINK_REFUSED);

  ret = gst_structure_get_int (structure, "width", &videoflip->to_width);
  ret &= gst_structure_get_int (structure, "height", &videoflip->to_height);

  if (!ret) return GST_PAD_LINK_REFUSED;

  return GST_PAD_LINK_OK;
}

static GstPadLinkReturn
gst_videoflip_sink_link (GstPad *pad, const GstCaps *caps)
{
  GstVideoflip *videoflip;
  GstStructure *structure;
  gboolean ret;

  GST_DEBUG ("gst_videoflip_sink_link");
  videoflip = GST_VIDEOFLIP (gst_pad_get_parent (pad));
  
  structure = gst_caps_get_structure (caps, 0);

  videoflip->format = videoflip_find_by_caps (caps);
  g_return_val_if_fail(videoflip->format, GST_PAD_LINK_REFUSED);

  ret = gst_structure_get_int (structure, "width", &videoflip->from_width);
  ret &= gst_structure_get_int (structure, "height", &videoflip->from_height);

  if (!ret) return GST_PAD_LINK_REFUSED;

  return GST_PAD_LINK_OK;
}

static void
gst_videoflip_init (GstVideoflip *videoflip)
{
  GST_DEBUG ("gst_videoflip_init");
  videoflip->sinkpad = gst_pad_new_from_template (
		  gst_videoflip_sink_template_factory(),
		  "sink");
  gst_element_add_pad(GST_ELEMENT(videoflip),videoflip->sinkpad);
  gst_pad_set_chain_function(videoflip->sinkpad,gst_videoflip_chain);
  gst_pad_set_link_function(videoflip->sinkpad,gst_videoflip_sink_link);
  gst_pad_set_getcaps_function(videoflip->sinkpad,gst_videoflip_sink_getcaps);

  videoflip->srcpad = gst_pad_new_from_template (
		  gst_videoflip_src_template_factory(),
		  "src");
  gst_element_add_pad(GST_ELEMENT(videoflip),videoflip->srcpad);
  gst_pad_set_link_function(videoflip->srcpad,gst_videoflip_src_link);
  //gst_pad_set_getcaps_function(videoflip->srcpad,gst_videoflip_getcaps);

  videoflip->inited = FALSE;
  videoflip->force_size = FALSE;
}


static void
gst_videoflip_chain (GstPad *pad, GstData *_data)
{
  GstBuffer *buf = GST_BUFFER (_data);
  GstVideoflip *videoflip;
  guchar *data;
  gulong size;
  GstBuffer *outbuf;

  GST_DEBUG ("gst_videoflip_chain");

  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (buf != NULL);

  videoflip = GST_VIDEOFLIP (gst_pad_get_parent (pad));
  g_return_if_fail (videoflip->inited);

  data = GST_BUFFER_DATA(buf);
  size = GST_BUFFER_SIZE(buf);

  if(videoflip->passthru){
    gst_pad_push(videoflip->srcpad, GST_DATA (buf));
    return;
  }

  GST_DEBUG ("gst_videoflip_chain: got buffer of %ld bytes in '%s'",size,
		              GST_OBJECT_NAME (videoflip));
 
  GST_DEBUG ("size=%ld from=%dx%d to=%dx%d fromsize=%ld (should be %d) tosize=%d",
	size,
	videoflip->from_width, videoflip->from_height,
	videoflip->to_width, videoflip->to_height,
  	size, videoflip->from_buf_size,
  	videoflip->to_buf_size);

  g_return_if_fail (size == videoflip->from_buf_size);

  outbuf = gst_buffer_new();
  /* FIXME: handle bufferpools */
  GST_BUFFER_SIZE(outbuf) = videoflip->to_buf_size;
  GST_BUFFER_DATA(outbuf) = g_malloc (videoflip->to_buf_size);
  GST_BUFFER_TIMESTAMP(outbuf) = GST_BUFFER_TIMESTAMP(buf);

  g_return_if_fail(videoflip->format);
  GST_DEBUG ("format %s",videoflip->format->fourcc);
  g_return_if_fail(videoflip->format->scale);

  videoflip->format->scale(videoflip, GST_BUFFER_DATA(outbuf), data);

  GST_DEBUG ("gst_videoflip_chain: pushing buffer of %d bytes in '%s'",GST_BUFFER_SIZE(outbuf),
	              GST_OBJECT_NAME (videoflip));

  gst_pad_push(videoflip->srcpad, GST_DATA (outbuf));

  gst_buffer_unref(buf);
}

static void
gst_videoflip_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  GstVideoflip *src;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_VIDEOFLIP(object));
  src = GST_VIDEOFLIP(object);

  GST_DEBUG ("gst_videoflip_set_property");
  switch (prop_id) {
    case ARG_METHOD:
      src->method = g_value_get_enum (value);
      break;
    default:
      break;
  }
}

static void
gst_videoflip_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  GstVideoflip *src;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_VIDEOFLIP(object));
  src = GST_VIDEOFLIP(object);

  switch (prop_id) {
    case ARG_METHOD:
      g_value_set_enum (value, src->method);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


static gboolean
plugin_init (GstPlugin *plugin)
{
  return gst_element_register (plugin, "videoflip", GST_RANK_NONE, GST_TYPE_VIDEOFLIP);
}

GST_PLUGIN_DEFINE (
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "videoflip",
  "Resizes video",
  plugin_init,
  VERSION,
  GST_LICENSE,
  GST_PACKAGE,
  GST_ORIGIN
)
