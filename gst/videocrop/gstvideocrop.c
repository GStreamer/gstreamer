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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <gst/gst.h>
#include <gst/video/video.h>

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
  gfloat	 fps;
  gint		 crop_left, crop_right, crop_top, crop_bottom;
};

struct _GstVideoCropClass {
  GstElementClass parent_class;
};

/* elementfactory information */
static GstElementDetails gst_video_crop_details = GST_ELEMENT_DETAILS (
  "video crop filter",
  "Filter/Effect/Video",
  "Crops video into a user defined region",
  "Wim Taymans <wim.taymans@chello.be>"
);


/* VideoCrop signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_LEFT,
  ARG_RIGHT,
  ARG_TOP,
  ARG_BOTTOM,
  /* FILL ME */
};

GST_PAD_TEMPLATE_FACTORY (video_crop_src_template_factory,
  "src",
  GST_PAD_SRC,
  GST_PAD_ALWAYS,
  gst_caps_new (
    "video_crop_src",
    "video/x-raw-yuv",
      GST_VIDEO_YUV_PAD_TEMPLATE_PROPS(
	      GST_PROPS_FOURCC (GST_STR_FOURCC ("I420")))
  )
)

GST_PAD_TEMPLATE_FACTORY (video_crop_sink_template_factory,
  "sink",
  GST_PAD_SINK,
  GST_PAD_ALWAYS,
  gst_caps_new (
    "video_crop_sink",
    "video/x-raw-yuv",
      GST_VIDEO_YUV_PAD_TEMPLATE_PROPS(
	      GST_PROPS_FOURCC (GST_STR_FOURCC ("I420")))
  )
)


static void		gst_video_crop_base_init	(gpointer g_class);
static void 		gst_video_crop_class_init	(GstVideoCropClass *klass);
static void 		gst_video_crop_init		(GstVideoCrop *video_crop);

static void		gst_video_crop_set_property	(GObject *object, guint prop_id, 
							 const GValue *value, GParamSpec *pspec);
static void		gst_video_crop_get_property	(GObject *object, guint prop_id, 
							 GValue *value, GParamSpec *pspec);

static GstPadLinkReturn
			gst_video_crop_sink_connect 	(GstPad *pad, GstCaps *caps);
static void 		gst_video_crop_chain 		(GstPad *pad, GstData *_data);

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
      gst_video_crop_base_init,
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
gst_video_crop_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details (element_class, &gst_video_crop_details);

  gst_element_class_add_pad_template (element_class, 
		  GST_PAD_TEMPLATE_GET (video_crop_sink_template_factory));
  gst_element_class_add_pad_template (element_class, 
		  GST_PAD_TEMPLATE_GET (video_crop_src_template_factory));
}
static void
gst_video_crop_class_init (GstVideoCropClass *klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*) klass;
  gstelement_class = (GstElementClass*) klass;

  parent_class = g_type_class_ref(GST_TYPE_ELEMENT);

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_LEFT,
    g_param_spec_int ("left", "Left", "Pixels to crop at left",
		      0, G_MAXINT, 0, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_RIGHT,
    g_param_spec_int ("right", "Right", "Pixels to crop at right",
		      0, G_MAXINT, 0, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_TOP,
    g_param_spec_int ("top", "Top", "Pixels to crop at top",
		      0, G_MAXINT, 0, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_BOTTOM,
    g_param_spec_int ("bottom", "Bottom", "Pixels to crop at bottom",
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

  video_crop->crop_right = 0;
  video_crop->crop_left = 0;
  video_crop->crop_top = 0;
  video_crop->crop_bottom = 0;

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
    case ARG_LEFT:
      video_crop->crop_left = g_value_get_int (value);
      break;
    case ARG_RIGHT:
      video_crop->crop_right = g_value_get_int (value);
      break;
    case ARG_TOP:
      video_crop->crop_top = g_value_get_int (value);
      break;
    case ARG_BOTTOM:
      video_crop->crop_bottom = g_value_get_int (value);
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
    case ARG_LEFT:
      g_value_set_int (value, video_crop->crop_left);
      break;
    case ARG_RIGHT:
      g_value_set_int (value, video_crop->crop_right);
      break;
    case ARG_TOP:
      g_value_set_int (value, video_crop->crop_top);
      break;
    case ARG_BOTTOM:
      g_value_set_int (value, video_crop->crop_bottom);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstPadLinkReturn
gst_video_crop_sink_connect (GstPad *pad, GstCaps *caps)
{
  GstVideoCrop *video_crop;

  /* we are not going to act on variable caps */
  if (!GST_CAPS_IS_FIXED (caps))
    return GST_PAD_LINK_DELAYED;

  video_crop = GST_VIDEO_CROP (gst_pad_get_parent (pad));

  gst_caps_get_int (caps, "width",  &video_crop->width);
  gst_caps_get_int (caps, "height", &video_crop->height);
  gst_caps_get_float (caps, "framerate", &video_crop->fps);

  return GST_PAD_LINK_OK;
}

#define GST_VIDEO_I420_SIZE(width,height) ((width)*(height) + ((width)/2)*((height)/2)*2)

#define GST_VIDEO_I420_Y_OFFSET(width,height) (0)
#define GST_VIDEO_I420_U_OFFSET(width,height) ((width)*(height))
#define GST_VIDEO_I420_V_OFFSET(width,height) ((width)*(height) + ((width/2)*(height/2)))

#define GST_VIDEO_I420_Y_ROWSTRIDE(width) (width)
#define GST_VIDEO_I420_U_ROWSTRIDE(width) ((width)/2)
#define GST_VIDEO_I420_V_ROWSTRIDE(width) ((width)/2)

static void
gst_video_crop_i420 (GstVideoCrop *video_crop, GstBuffer *src_buffer, GstBuffer *dest_buffer)
{
  guint8 *src;
  guint8 *dest;
  guint8 *srcY, *srcU, *srcV;
  guint8 *destY, *destU, *destV;
  gint out_width = video_crop->width -
	(video_crop->crop_left + video_crop->crop_right);
  gint out_height = video_crop->height -
	(video_crop->crop_top + video_crop->crop_bottom);
  gint src_stride;
  gint j;

  src = GST_BUFFER_DATA (src_buffer);
  dest = GST_BUFFER_DATA (dest_buffer);

  g_return_if_fail(GST_BUFFER_SIZE (dest_buffer) == GST_VIDEO_I420_SIZE(out_width,out_height));

  srcY = src + GST_VIDEO_I420_Y_OFFSET(video_crop->width, video_crop->height);
  destY = dest + GST_VIDEO_I420_Y_OFFSET(out_width, out_height);

  src_stride = GST_VIDEO_I420_Y_ROWSTRIDE(video_crop->width);

  /* copy Y plane first */

  srcY += src_stride * video_crop->crop_top + video_crop->crop_left;
  for (j = 0; j < out_height; j++) {
    memcpy (destY, srcY, out_width);
    srcY += src_stride;
    destY += out_width;
  }

  src_stride = GST_VIDEO_I420_U_ROWSTRIDE(video_crop->width);

  destU = dest + GST_VIDEO_I420_U_OFFSET(out_width, out_height);
  destV = dest + GST_VIDEO_I420_V_OFFSET(out_width, out_height);

  srcU = src + GST_VIDEO_I420_U_OFFSET(video_crop->width, video_crop->height);
  srcV = src + GST_VIDEO_I420_V_OFFSET(video_crop->width, video_crop->height);

  srcU += src_stride * (video_crop->crop_top/2) + (video_crop->crop_left/2);
  srcV += src_stride * (video_crop->crop_top/2) + (video_crop->crop_left/2);

  for (j = 0; j < out_height/2; j++) {
    /* copy U plane */
    memcpy (destU, srcU, out_width/2);
    srcU += src_stride;
    destU += out_width/2;

    /* copy V plane */
    memcpy (destV, srcV, out_width/2);
    srcV += src_stride;
    destV += out_width/2;
  }
}

static void
gst_video_crop_chain (GstPad *pad, GstData *_data)
{
  GstBuffer *buffer = GST_BUFFER (_data);
  GstVideoCrop *video_crop;
  GstBuffer *outbuf;
  gint new_width, new_height;

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

  new_width = video_crop->width -
	(video_crop->crop_left + video_crop->crop_right);
  new_height = video_crop->height -
	(video_crop->crop_top + video_crop->crop_bottom);

  if (GST_PAD_CAPS (video_crop->srcpad) == NULL) {
    if (gst_pad_try_set_caps (video_crop->srcpad,
			       GST_CAPS_NEW (
				       "video_crop_caps",
				       "video/x-raw-yuv",
				        "format",   GST_PROPS_FOURCC (GST_STR_FOURCC ("I420")),
				         "width",   GST_PROPS_INT (new_width),
				         "height",  GST_PROPS_INT (new_height),
                                         "framerate", GST_PROPS_FLOAT (video_crop->fps)
				       )) <= 0)
    {
      gst_element_error (GST_ELEMENT (video_crop), "could not negotiate pads");
      return;
    }
  }

  outbuf = gst_buffer_new_and_alloc ((new_width * new_height * 3) / 2);
  GST_BUFFER_TIMESTAMP (outbuf) = GST_BUFFER_TIMESTAMP (buffer);

  gst_video_crop_i420 (video_crop, buffer, outbuf);
  gst_buffer_unref (buffer);

  gst_pad_push (video_crop->srcpad, GST_DATA (outbuf));
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
plugin_init (GstPlugin *plugin)
{
  return gst_element_register (plugin, "videocrop", GST_RANK_PRIMARY, GST_TYPE_VIDEO_CROP);
}

GST_PLUGIN_DEFINE (
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "videocrop",
  "Crops video into a user defined region",
  plugin_init,
  VERSION,
  GST_LICENSE,
  GST_PACKAGE,
  GST_ORIGIN
)
