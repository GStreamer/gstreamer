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

#include <gst/gst.h>

#include <string.h>

#define GST_TYPE_VIDEO_CROP \
  (gst_video_crop_get_type())
#define GST_VIDEO_CROP(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_VIDEO_CROP,GstVideoCrop))
#define GST_VIDEO_CROP_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_VIDEO_CROP,GstVideoCropClass))
#define GST_IS_VIDEO_CROP(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_VIDEO_CROP))
#define GST_IS_VIDEO_CROP_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_VIDEO_CROP))

typedef struct _GstVideoCrop GstVideoCrop;
typedef struct _GstVideoCropClass GstVideoCropClass;

struct _GstVideoCrop {
  GstElement 	 element;

  /* pads */
  GstPad 	*sinkpad;
  GstPad	*srcpad;

  /* caps */
  gint		 width, height;
  gint		 crop_x, crop_y;
  gint		 crop_width, crop_height;
};

struct _GstVideoCropClass {
  GstElementClass parent_class;
};

/* elementfactory information */
static GstElementDetails gst_video_crop_details = {
  "video crop filter",
  "Filter/Video/Crop",
  "LGPL",
  "Crops video into a user defined region",
  VERSION,
  "Wim Taymans <wim.taymans@chello.be>",
  "(C) 2002",
};


/* VideoCrop signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_X,
  ARG_Y,
  ARG_WIDTH,
  ARG_HEIGHT,
  /* FILL ME */
};

GST_PAD_TEMPLATE_FACTORY (video_crop_src_template_factory,
  "src",
  GST_PAD_SRC,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW (
    "video_crop_src",
    "video/raw",
      "format",   GST_PROPS_FOURCC (GST_STR_FOURCC ("I420"))
  )
)

GST_PAD_TEMPLATE_FACTORY (video_crop_sink_template_factory,
  "sink",
  GST_PAD_SINK,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW (
    "video_crop_sink",
    "video/raw",
      "format",   GST_PROPS_FOURCC (GST_STR_FOURCC ("I420"))
  )
)


static void 		gst_video_crop_class_init	(GstVideoCropClass *klass);
static void 		gst_video_crop_init		(GstVideoCrop *video_crop);

static void		gst_video_crop_set_property	(GObject *object, guint prop_id, 
							 const GValue *value, GParamSpec *pspec);
static void		gst_video_crop_get_property	(GObject *object, guint prop_id, 
							 GValue *value, GParamSpec *pspec);

static GstPadConnectReturn
			gst_video_crop_sink_connect 	(GstPad *pad, GstCaps *caps);
static void 		gst_video_crop_chain 		(GstPad *pad, GstBuffer *buffer);

static GstElementStateReturn
			gst_video_crop_change_state	(GstElement *element);


static GstElementClass *parent_class = NULL;
/* static guint gst_video_crop_signals[LAST_SIGNAL] = { 0 }; */

GType
gst_video_crop_get_type (void)
{
  static GType video_crop_type = 0;

  if (!video_crop_type) {
    static const GTypeInfo video_crop_info = {
      sizeof(GstVideoCropClass),      
      NULL,
      NULL,
      (GClassInitFunc)gst_video_crop_class_init,
      NULL,
      NULL,
      sizeof(GstVideoCrop),
      0,
      (GInstanceInitFunc)gst_video_crop_init,
    };
    video_crop_type = g_type_register_static(GST_TYPE_ELEMENT, "GstVideoCrop", &video_crop_info, 0);
  }
  return video_crop_type;
}

static void
gst_video_crop_class_init (GstVideoCropClass *klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*) klass;
  gstelement_class = (GstElementClass*) klass;

  parent_class = g_type_class_ref(GST_TYPE_ELEMENT);

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_X,
    g_param_spec_int ("x", "X", "X offset of image",
		      0, G_MAXINT, 0, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_Y,
    g_param_spec_int ("y", "Y", "Y offset of image",
		      0, G_MAXINT, 0, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_WIDTH,
    g_param_spec_int ("width", "Width", "Width of image",
		      0, G_MAXINT, 0, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_HEIGHT,
    g_param_spec_int ("height", "Height", "Height of image",
		      0, G_MAXINT, 0, G_PARAM_READWRITE));

  gobject_class->set_property = gst_video_crop_set_property;
  gobject_class->get_property = gst_video_crop_get_property;

  gstelement_class->change_state = gst_video_crop_change_state;
}

static void
gst_video_crop_init (GstVideoCrop *video_crop)
{
  /* create the sink and src pads */
  video_crop->sinkpad = gst_pad_new_from_template(
		  GST_PAD_TEMPLATE_GET (video_crop_sink_template_factory), "sink");
  gst_element_add_pad (GST_ELEMENT (video_crop), video_crop->sinkpad);
  gst_pad_set_chain_function (video_crop->sinkpad, GST_DEBUG_FUNCPTR (gst_video_crop_chain));
  gst_pad_set_link_function (video_crop->sinkpad, GST_DEBUG_FUNCPTR (gst_video_crop_sink_connect));

  video_crop->srcpad = gst_pad_new_from_template(
		  GST_PAD_TEMPLATE_GET (video_crop_src_template_factory), "src");
  gst_element_add_pad (GST_ELEMENT (video_crop), video_crop->srcpad);

  video_crop->crop_x = 0;
  video_crop->crop_y = 0;
  video_crop->crop_width = 128;
  video_crop->crop_height = 128;

  GST_FLAG_SET (video_crop, GST_ELEMENT_EVENT_AWARE);
}

/* do we need this function? */
static void
gst_video_crop_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  GstVideoCrop *video_crop;
	    
  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_VIDEO_CROP (object));
	      
  video_crop = GST_VIDEO_CROP (object);

  switch (prop_id) {
    case ARG_X:
      video_crop->crop_x = g_value_get_int (value);
      break;
    case ARG_Y:
      video_crop->crop_y = g_value_get_int (value);
      break;
    case ARG_WIDTH:
      video_crop->crop_width = g_value_get_int (value);
      break;
    case ARG_HEIGHT:
      video_crop->crop_height = g_value_get_int (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}
static void
gst_video_crop_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  GstVideoCrop *video_crop;
	    
  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_VIDEO_CROP (object));
	      
  video_crop = GST_VIDEO_CROP (object);

  switch (prop_id) {
    case ARG_X:
      g_value_set_int (value, video_crop->crop_x);
      break;
    case ARG_Y:
      g_value_set_int (value, video_crop->crop_y);
      break;
    case ARG_WIDTH:
      g_value_set_int (value, video_crop->crop_width);
      break;
    case ARG_HEIGHT:
      g_value_set_int (value, video_crop->crop_height);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstPadConnectReturn
gst_video_crop_sink_connect (GstPad *pad, GstCaps *caps)
{
  GstVideoCrop *video_crop;

  /* we are not going to act on variable caps */
  if (!GST_CAPS_IS_FIXED (caps))
    return GST_PAD_LINK_DELAYED;

  video_crop = GST_VIDEO_CROP (gst_pad_get_parent (pad));

  gst_caps_get_int (caps, "width",  &video_crop->width);
  gst_caps_get_int (caps, "height", &video_crop->height);

  if (video_crop->crop_width + video_crop->crop_x > video_crop->width)
    video_crop->crop_width = video_crop->width - video_crop->crop_x;
  if (video_crop->crop_height + video_crop->crop_y > video_crop->height)
    video_crop->crop_height = video_crop->height - video_crop->crop_y;

  return GST_PAD_LINK_OK;
}

static void
gst_video_crop_i420 (GstVideoCrop *video_crop, GstBuffer *src_buffer, GstBuffer *dest_buffer)
{
  guint8 *srcY, *srcU, *srcV;
  guint8 *destY, *destU, *destV;
  gint width = video_crop->crop_width;
  gint src_stride = video_crop->width;
  gint frame_size = video_crop->width * video_crop->height;
  gint crop_height;
  gint j;

  srcY = GST_BUFFER_DATA (src_buffer) + (src_stride * video_crop->crop_y + video_crop->crop_x);
  destY = GST_BUFFER_DATA (dest_buffer);

  crop_height = video_crop->crop_height;

  /* copy Y plane first */
  for (j = crop_height; j; j--) {
    memcpy (destY, srcY, width);
    srcY += src_stride;
    destY += width;
  }

  width >>= 1;
  src_stride >>= 1;
  crop_height >>= 1;

  destU = destY;
  destV = destU + ((video_crop->crop_width * crop_height) >> 1);

  srcU = GST_BUFFER_DATA (src_buffer) + frame_size + ((src_stride * video_crop->crop_y + video_crop->crop_x) >> 1);
  srcV = srcU + (frame_size >> 2);

  /* copy U plane */
  for (j = crop_height; j; j--) {
    memcpy (destU, srcU, width);
    srcU += src_stride;
    destU += width;
  }
  /* copy U plane */
  for (j = crop_height; j; j--) {
    memcpy (destV, srcV, width);
    srcV += src_stride;
    destV += width;
  }
}

static void
gst_video_crop_chain (GstPad *pad, GstBuffer *buffer)
{
  GstVideoCrop *video_crop;
  GstBuffer *outbuf;

  video_crop = GST_VIDEO_CROP (gst_pad_get_parent (pad));

  if (GST_IS_EVENT (buffer)) {
    GstEvent *event = GST_EVENT (buffer);

    switch (GST_EVENT_TYPE (event)) {
      default:
	gst_pad_event_default (pad, event);
	break;
    }
    return;
  }

  if (GST_PAD_CAPS (video_crop->srcpad) == NULL) {
    if (gst_pad_try_set_caps (video_crop->srcpad,
			       GST_CAPS_NEW (
				       "video_crop_caps",
				       "video/raw",
				        "format",   GST_PROPS_FOURCC (GST_STR_FOURCC ("I420")),
				         "width",   GST_PROPS_INT (video_crop->crop_width),
				         "height",  GST_PROPS_INT (video_crop->crop_height)
				       )) <= 0)
    {
      gst_element_error (GST_ELEMENT (video_crop), "could not negotiate pads");
      return;
    }
  }

  outbuf = gst_buffer_new_and_alloc ((video_crop->crop_width * video_crop->crop_height * 3) / 2);
  GST_BUFFER_TIMESTAMP (outbuf) = GST_BUFFER_TIMESTAMP (buffer);

  gst_video_crop_i420 (video_crop, buffer, outbuf);
  gst_buffer_unref (buffer);

  gst_pad_push (video_crop->srcpad, outbuf);
}

static GstElementStateReturn
gst_video_crop_change_state (GstElement *element)
{
  GstVideoCrop *video_crop;

  video_crop = GST_VIDEO_CROP (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_NULL_TO_READY:
      break;
    case GST_STATE_READY_TO_PAUSED:
      break;
    case GST_STATE_PAUSED_TO_PLAYING:
      break;
    case GST_STATE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_PAUSED_TO_READY:
      break;
    case GST_STATE_READY_TO_NULL:
      break;
  }

  parent_class->change_state (element);

  return GST_STATE_SUCCESS;
}

static gboolean
plugin_init (GModule *module, GstPlugin *plugin)
{
  GstElementFactory *factory;

  /* create an elementfactory for the videocrop element */
  factory = gst_element_factory_new ("videocrop", GST_TYPE_VIDEO_CROP, &gst_video_crop_details);
  g_return_val_if_fail (factory != NULL, FALSE);

  gst_element_factory_add_pad_template (factory, 
		  GST_PAD_TEMPLATE_GET (video_crop_sink_template_factory));
  gst_element_factory_add_pad_template (factory, 
		  GST_PAD_TEMPLATE_GET (video_crop_src_template_factory));
  gst_element_factory_set_rank (factory, GST_ELEMENT_RANK_PRIMARY);

  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));

  return TRUE;
}

GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "videocrop",
  plugin_init
};
