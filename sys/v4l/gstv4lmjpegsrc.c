/* G-Streamer hardware MJPEG video source plugin
 * Copyright (C) 2001-2002 Ronald Bultje <rbultje@ronald.bitfreak.net>
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

#include <string.h>
#include "v4lmjpegsrc_calls.h"

/* elementfactory information */
static GstElementDetails gst_v4lmjpegsrc_details = {
  "Video (video4linux/MJPEG) Source",
  "Source/Video",
  "LGPL",
  "Reads MJPEG-encoded frames from a zoran MJPEG/video4linux device",
  VERSION,
  "Ronald Bultje <rbultje@ronald.bitfreak.net>",
  "(C) 2001",
};

/* V4lMjpegSrc signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

/* arguments */
enum {
  ARG_0,
  ARG_X_OFFSET,
  ARG_Y_OFFSET,
  ARG_F_WIDTH,
  ARG_F_HEIGHT,
  ARG_H_DECIMATION,
  ARG_V_DECIMATION,
  ARG_WIDTH,
  ARG_HEIGHT,
  ARG_QUALITY,
  ARG_NUMBUFS,
  ARG_BUFSIZE
};


/* init functions */
static void                  gst_v4lmjpegsrc_class_init   (GstV4lMjpegSrcClass *klass);
static void                  gst_v4lmjpegsrc_init         (GstV4lMjpegSrc *v4lmjpegsrc);

/* pad/buffer functions */
static gboolean              gst_v4lmjpegsrc_srcconvert   (GstPad         *pad,
                                                           GstFormat      src_format,
                                                           gint64         src_value,
                                                           GstFormat      *dest_format,
                                                           gint64         *dest_value);
static GstPadConnectReturn   gst_v4lmjpegsrc_srcconnect   (GstPad         *pad,
                                                           GstCaps        *caps);
static GstBuffer*            gst_v4lmjpegsrc_get          (GstPad         *pad);

/* get/set params */
static void                  gst_v4lmjpegsrc_set_property (GObject        *object,
                                                           guint          prop_id,
                                                           const GValue   *value,
                                                           GParamSpec     *pspec);
static void                  gst_v4lmjpegsrc_get_property (GObject        *object,
                                                           guint          prop_id,
                                                           GValue         *value,
                                                           GParamSpec     *pspec);

/* state handling */
static GstElementStateReturn gst_v4lmjpegsrc_change_state (GstElement     *element);

/* bufferpool functions */
static GstBuffer*            gst_v4lmjpegsrc_buffer_new   (GstBufferPool  *pool,
                                                           guint64        location,
                                                           guint          size,
                                                           gpointer       user_data);
static void                  gst_v4lmjpegsrc_buffer_free  (GstBufferPool  *pool,
							   GstBuffer      *buf,
                                                           gpointer       user_data);


static GstCaps *capslist = NULL;
static GstPadTemplate *src_template;

static GstElementClass *parent_class = NULL;
/*static guint gst_v4lmjpegsrc_signals[LAST_SIGNAL] = { 0 }; */


GType
gst_v4lmjpegsrc_get_type (void)
{
  static GType v4lmjpegsrc_type = 0;

  if (!v4lmjpegsrc_type) {
    static const GTypeInfo v4lmjpegsrc_info = {
      sizeof(GstV4lMjpegSrcClass),
      NULL,
      NULL,
      (GClassInitFunc)gst_v4lmjpegsrc_class_init,
      NULL,
      NULL,
      sizeof(GstV4lMjpegSrc),
      0,
      (GInstanceInitFunc)gst_v4lmjpegsrc_init,
      NULL
    };
    v4lmjpegsrc_type = g_type_register_static(GST_TYPE_V4LELEMENT, "GstV4lMjpegSrc", &v4lmjpegsrc_info, 0);
  }
  return v4lmjpegsrc_type;
}


static void
gst_v4lmjpegsrc_class_init (GstV4lMjpegSrcClass *klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  parent_class = g_type_class_ref(GST_TYPE_V4LELEMENT);

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_X_OFFSET,
    g_param_spec_int("x_offset","x_offset","x_offset",
    G_MININT,G_MAXINT,0,G_PARAM_WRITABLE));
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_Y_OFFSET,
    g_param_spec_int("y_offset","y_offset","y_offset",
    G_MININT,G_MAXINT,0,G_PARAM_WRITABLE));
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_F_WIDTH,
    g_param_spec_int("frame_width","frame_width","frame_width",
    G_MININT,G_MAXINT,0,G_PARAM_WRITABLE));
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_F_HEIGHT,
    g_param_spec_int("frame_height","frame_height","frame_height",
    G_MININT,G_MAXINT,0,G_PARAM_WRITABLE));

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_H_DECIMATION,
    g_param_spec_int("h_decimation","h_decimation","h_decimation",
    G_MININT,G_MAXINT,0,G_PARAM_WRITABLE));
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_V_DECIMATION,
    g_param_spec_int("v_decimation","v_decimation","v_decimation",
    G_MININT,G_MAXINT,0,G_PARAM_WRITABLE));

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_WIDTH,
    g_param_spec_int("width","width","width",
    G_MININT,G_MAXINT,0,G_PARAM_READABLE));
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_HEIGHT,
    g_param_spec_int("height","height","height",
    G_MININT,G_MAXINT,0,G_PARAM_READABLE));

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_QUALITY,
    g_param_spec_int("quality","quality","quality",
    G_MININT,G_MAXINT,0,G_PARAM_WRITABLE));

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_NUMBUFS,
    g_param_spec_int("num_buffers","num_buffers","num_buffers",
    G_MININT,G_MAXINT,0,G_PARAM_READWRITE));
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_BUFSIZE,
    g_param_spec_int("buffer_size","buffer_size","buffer_size",
    G_MININT,G_MAXINT,0,G_PARAM_READWRITE));

  gobject_class->set_property = gst_v4lmjpegsrc_set_property;
  gobject_class->get_property = gst_v4lmjpegsrc_get_property;

  gstelement_class->change_state = gst_v4lmjpegsrc_change_state;
}


static void
gst_v4lmjpegsrc_init (GstV4lMjpegSrc *v4lmjpegsrc)
{
  v4lmjpegsrc->srcpad = gst_pad_new_from_template (src_template, "src");
  gst_element_add_pad(GST_ELEMENT(v4lmjpegsrc), v4lmjpegsrc->srcpad);

  gst_pad_set_get_function (v4lmjpegsrc->srcpad, gst_v4lmjpegsrc_get);
  gst_pad_set_link_function (v4lmjpegsrc->srcpad, gst_v4lmjpegsrc_srcconnect);
  gst_pad_set_convert_function (v4lmjpegsrc->srcpad, gst_v4lmjpegsrc_srcconvert);

  v4lmjpegsrc->bufferpool = gst_buffer_pool_new(
		  			NULL,
					NULL,
					(GstBufferPoolBufferNewFunction)gst_v4lmjpegsrc_buffer_new,
					NULL,
					(GstBufferPoolBufferFreeFunction)gst_v4lmjpegsrc_buffer_free,
					v4lmjpegsrc);

  v4lmjpegsrc->frame_width = 0;
  v4lmjpegsrc->frame_height = 0;
  v4lmjpegsrc->x_offset = -1;
  v4lmjpegsrc->y_offset = -1;

  v4lmjpegsrc->horizontal_decimation = 4;
  v4lmjpegsrc->vertical_decimation = 4;

  v4lmjpegsrc->end_width = 0;
  v4lmjpegsrc->end_height = 0;

  v4lmjpegsrc->quality = 50;

  v4lmjpegsrc->numbufs = 64;
  v4lmjpegsrc->bufsize = 256;
}


static gboolean
gst_v4lmjpegsrc_srcconvert (GstPad    *pad,
                            GstFormat  src_format,
                            gint64     src_value,
                            GstFormat *dest_format,
                            gint64    *dest_value)
{
  GstV4lMjpegSrc *v4lmjpegsrc;
  gint norm;
  gdouble fps;

  v4lmjpegsrc = GST_V4LMJPEGSRC (gst_pad_get_parent (pad));

  if (!GST_V4L_IS_OPEN(GST_V4LELEMENT(v4lmjpegsrc)))
    return FALSE;

  if (!gst_v4l_get_chan_norm(GST_V4LELEMENT(v4lmjpegsrc), NULL, &norm))
    return FALSE;

  if (norm == VIDEO_MODE_NTSC)
    fps = 30000/1001;
  else
    fps = 25.;

  switch (src_format) {
    case GST_FORMAT_TIME:
      switch (*dest_format) {
        case GST_FORMAT_DEFAULT:
          *dest_format = GST_FORMAT_UNITS;
          /* fall-through */
        case GST_FORMAT_UNITS:
          *dest_value = src_value * fps / GST_SECOND;
          break;
        default:
          return FALSE;
      }
      break;

    case GST_FORMAT_UNITS:
      switch (*dest_format) {
        case GST_FORMAT_DEFAULT:
          *dest_format = GST_FORMAT_TIME;
          /* fall-through */
        case GST_FORMAT_TIME:
          *dest_value = src_value * GST_SECOND / fps;
          break;
        default:
          return FALSE;
      }
      break;

    default:
      return FALSE;
  }

  return TRUE;
}


static GstPadConnectReturn
gst_v4lmjpegsrc_srcconnect (GstPad  *pad,
                            GstCaps *caps)
{
  GstPadConnectReturn ret_val;
  GstV4lMjpegSrc *v4lmjpegsrc;

  v4lmjpegsrc = GST_V4LMJPEGSRC (gst_pad_get_parent (pad));

  /* in case the buffers are active (which means that we already
   * did capsnego before and didn't clean up), clean up anyways */
  if (GST_V4L_IS_ACTIVE(GST_V4LELEMENT(v4lmjpegsrc)))
  {
    if (!gst_v4lmjpegsrc_capture_deinit(v4lmjpegsrc))
      return GST_PAD_LINK_REFUSED;
  }
  else if (!GST_V4L_IS_OPEN(GST_V4LELEMENT(v4lmjpegsrc)))
  {
    return GST_PAD_LINK_DELAYED;
  }

  /* Note: basically, we don't give a damn about the opposite caps here.
   * that might seem odd, but it isn't. we know that the opposite caps is
   * either NULL or has mime type video/jpeg, and in both cases, we'll set
   * our own mime type back and it'll work. Other properties are to be set
   * by the src, not by the opposite caps */

  /* set buffer info */
  if (!gst_v4lmjpegsrc_set_buffer(v4lmjpegsrc, v4lmjpegsrc->numbufs, v4lmjpegsrc->bufsize))
    return GST_PAD_LINK_REFUSED;

  /* set capture parameters and mmap the buffers */
  if (!v4lmjpegsrc->frame_width && !v4lmjpegsrc->frame_height &&
       v4lmjpegsrc->x_offset < 0 && v4lmjpegsrc->y_offset < 0 &&
       v4lmjpegsrc->horizontal_decimation == v4lmjpegsrc->vertical_decimation)
  {
    if (!gst_v4lmjpegsrc_set_capture(v4lmjpegsrc,
        v4lmjpegsrc->horizontal_decimation, v4lmjpegsrc->quality))
      return GST_PAD_LINK_REFUSED;
  }
  else
  {
    if (!gst_v4lmjpegsrc_set_capture_m(v4lmjpegsrc,
         v4lmjpegsrc->x_offset, v4lmjpegsrc->y_offset,
         v4lmjpegsrc->frame_width, v4lmjpegsrc->frame_height,
         v4lmjpegsrc->horizontal_decimation, v4lmjpegsrc->vertical_decimation,
         v4lmjpegsrc->quality))
      return GST_PAD_LINK_REFUSED;
  }
  /* we now have an actual width/height - *set it* */
  caps = gst_caps_new("v4lmjpegsrc_caps",
                      "video/jpeg",
                      gst_props_new(
                        "width",  GST_PROPS_INT(v4lmjpegsrc->end_width),
                        "height", GST_PROPS_INT(v4lmjpegsrc->end_height),
                        NULL       ) );
  if ((ret_val = gst_pad_try_set_caps(v4lmjpegsrc->srcpad, caps)) == GST_PAD_LINK_REFUSED)
    return GST_PAD_LINK_REFUSED;
  else if (ret_val == GST_PAD_LINK_DELAYED)
    return GST_PAD_LINK_DELAYED;

  if (!gst_v4lmjpegsrc_capture_init(v4lmjpegsrc))
    return GST_PAD_LINK_REFUSED;

  return GST_PAD_LINK_DONE;
}


static GstBuffer*
gst_v4lmjpegsrc_get (GstPad *pad)
{
  GstV4lMjpegSrc *v4lmjpegsrc;
  GstBuffer *buf;
  gint num;

  g_return_val_if_fail (pad != NULL, NULL);

  v4lmjpegsrc = GST_V4LMJPEGSRC (gst_pad_get_parent (pad));

  buf = gst_buffer_new_from_pool(v4lmjpegsrc->bufferpool, 0, 0);
  if (!buf)
  {
    gst_element_error(GST_ELEMENT(v4lmjpegsrc),
      "Failed to create a new GstBuffer");
    return NULL;
  }

  /* grab a frame from the device */
  if (!gst_v4lmjpegsrc_grab_frame(v4lmjpegsrc, &num, &(GST_BUFFER_SIZE(buf))))
    return NULL;
  GST_BUFFER_DATA(buf) = gst_v4lmjpegsrc_get_buffer(v4lmjpegsrc, num);
  if (!v4lmjpegsrc->first_timestamp)
    v4lmjpegsrc->first_timestamp = v4lmjpegsrc->bsync.timestamp.tv_sec * GST_SECOND +
      v4lmjpegsrc->bsync.timestamp.tv_usec * GST_SECOND/1000000;
  GST_BUFFER_TIMESTAMP(buf) = v4lmjpegsrc->bsync.timestamp.tv_sec * GST_SECOND +
    v4lmjpegsrc->bsync.timestamp.tv_usec * GST_SECOND/1000000 - v4lmjpegsrc->first_timestamp;

  return buf;
}


static void
gst_v4lmjpegsrc_set_property (GObject      *object,
                              guint        prop_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
  GstV4lMjpegSrc *v4lmjpegsrc;

  g_return_if_fail(GST_IS_V4LMJPEGSRC(object));
  v4lmjpegsrc = GST_V4LMJPEGSRC(object);

  switch (prop_id) {
    case ARG_X_OFFSET:
      v4lmjpegsrc->x_offset = g_value_get_int(value);
      break;
    case ARG_Y_OFFSET:
      v4lmjpegsrc->y_offset = g_value_get_int(value);
      break;
    case ARG_F_WIDTH:
      v4lmjpegsrc->frame_width = g_value_get_int(value);
      break;
    case ARG_F_HEIGHT:
      v4lmjpegsrc->frame_height = g_value_get_int(value);
      break;
    case ARG_H_DECIMATION:
      v4lmjpegsrc->horizontal_decimation = g_value_get_int(value);
      break;
    case ARG_V_DECIMATION:
      v4lmjpegsrc->vertical_decimation = g_value_get_int(value);
      break;
    case ARG_QUALITY:
      v4lmjpegsrc->quality = g_value_get_int(value);
      break;
    case ARG_NUMBUFS:
      v4lmjpegsrc->numbufs = g_value_get_int(value);
      break;
    case ARG_BUFSIZE:
      v4lmjpegsrc->bufsize = g_value_get_int(value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


static void
gst_v4lmjpegsrc_get_property (GObject    *object,
                              guint      prop_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
  GstV4lMjpegSrc *v4lmjpegsrc;

  g_return_if_fail(GST_IS_V4LMJPEGSRC(object));
  v4lmjpegsrc = GST_V4LMJPEGSRC(object);

  switch (prop_id) {
    case ARG_WIDTH:
      g_value_set_int(value, v4lmjpegsrc->end_width);
      break;
    case ARG_HEIGHT:
      g_value_set_int(value, v4lmjpegsrc->end_height);
      break;
    case ARG_X_OFFSET:
      g_value_set_int(value, v4lmjpegsrc->x_offset);
      break;
    case ARG_Y_OFFSET:
      g_value_set_int(value, v4lmjpegsrc->y_offset);
      break;
    case ARG_F_WIDTH:
      g_value_set_int(value, v4lmjpegsrc->frame_width);
      break;
    case ARG_F_HEIGHT:
      g_value_set_int(value, v4lmjpegsrc->frame_height);
      break;
    case ARG_H_DECIMATION:
      g_value_set_int(value, v4lmjpegsrc->horizontal_decimation);
      break;
    case ARG_V_DECIMATION:
      g_value_set_int(value, v4lmjpegsrc->vertical_decimation);
      break;
    case ARG_QUALITY:
      g_value_set_int(value, v4lmjpegsrc->quality);
      break;
    case ARG_NUMBUFS:
      g_value_set_int(value, v4lmjpegsrc->breq.count);
      break;
    case ARG_BUFSIZE:
      g_value_set_int(value, v4lmjpegsrc->breq.size);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


static GstElementStateReturn
gst_v4lmjpegsrc_change_state (GstElement *element)
{
  GstV4lMjpegSrc *v4lmjpegsrc;
  GstElementStateReturn parent_value;

  g_return_val_if_fail(GST_IS_V4LMJPEGSRC(element), GST_STATE_FAILURE);
  
  v4lmjpegsrc = GST_V4LMJPEGSRC(element);

  switch (GST_STATE_TRANSITION(element)) {
    case GST_STATE_READY_TO_PAUSED:
      /* actual buffer set-up used to be done here - but I moved
       * it to capsnego itself */
      v4lmjpegsrc->first_timestamp = 0;
      break;
    case GST_STATE_PAUSED_TO_PLAYING:
      /* queue all buffer, start streaming capture */
      if (!gst_v4lmjpegsrc_capture_start(v4lmjpegsrc))
        return GST_STATE_FAILURE;
      break;
    case GST_STATE_PLAYING_TO_PAUSED:
      /* de-queue all queued buffers */
      if (!gst_v4lmjpegsrc_capture_stop(v4lmjpegsrc))
        return GST_STATE_FAILURE;
      break;
    case GST_STATE_PAUSED_TO_READY:
      /* stop capturing, unmap all buffers */
      if (!gst_v4lmjpegsrc_capture_deinit(v4lmjpegsrc))
        return GST_STATE_FAILURE;
      break;
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state) {
    parent_value = GST_ELEMENT_CLASS (parent_class)->change_state (element);
  } else {
    parent_value = GST_STATE_FAILURE;
  }

  if (GST_STATE_TRANSITION(element) == GST_STATE_NULL_TO_READY)
  {
    /* do autodetection if no input/norm is selected yet */
    if ((GST_V4LELEMENT(v4lmjpegsrc)->norm < VIDEO_MODE_PAL ||
         GST_V4LELEMENT(v4lmjpegsrc)->norm == VIDEO_MODE_AUTO) ||
        (GST_V4LELEMENT(v4lmjpegsrc)->channel < 0 ||
         GST_V4LELEMENT(v4lmjpegsrc)->channel == V4L_MJPEG_INPUT_AUTO))
    {
      gint norm, input;

      if (GST_V4LELEMENT(v4lmjpegsrc)->norm < 0)
        norm = VIDEO_MODE_AUTO;
      else
        norm = GST_V4LELEMENT(v4lmjpegsrc)->norm;

      if (GST_V4LELEMENT(v4lmjpegsrc)->channel < 0)
        input = V4L_MJPEG_INPUT_AUTO;
      else
        input = GST_V4LELEMENT(v4lmjpegsrc)->channel;

      if (!gst_v4lmjpegsrc_set_input_norm(v4lmjpegsrc, input, norm))
        return GST_STATE_FAILURE;
    }
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return parent_value;

  return GST_STATE_SUCCESS;
}


static GstBuffer*
gst_v4lmjpegsrc_buffer_new (GstBufferPool *pool,
                            guint64       location,
                            guint         size,
                            gpointer      user_data)
{
  GstBuffer *buffer;
  GstV4lMjpegSrc *v4lmjpegsrc = GST_V4LMJPEGSRC(user_data);

  if (!GST_V4L_IS_ACTIVE(GST_V4LELEMENT(v4lmjpegsrc)))
    return NULL;

  buffer = gst_buffer_new();
  if (!buffer)
    return NULL;

  /* TODO: add interlacing info to buffer as metadata */
  GST_BUFFER_MAXSIZE(buffer) = v4lmjpegsrc->breq.size;
  GST_BUFFER_FLAG_SET(buffer, GST_BUFFER_DONTFREE);

  return buffer;
}


static void
gst_v4lmjpegsrc_buffer_free (GstBufferPool *pool, GstBuffer *buf, gpointer user_data)
{
  GstV4lMjpegSrc *v4lmjpegsrc = GST_V4LMJPEGSRC (user_data);
  int n;

  if (gst_element_get_state(GST_ELEMENT(v4lmjpegsrc)) != GST_STATE_PLAYING)
    return; /* we've already cleaned up ourselves */

  for (n=0;n<v4lmjpegsrc->breq.count;n++)
    if (GST_BUFFER_DATA(buf) == gst_v4lmjpegsrc_get_buffer(v4lmjpegsrc, n))
    {
      gst_v4lmjpegsrc_requeue_frame(v4lmjpegsrc, n);
      break;
    }

  if (n == v4lmjpegsrc->breq.count)
    gst_element_error(GST_ELEMENT(v4lmjpegsrc),
      "Couldn't find the buffer");

  /* free the buffer struct et all */
  gst_buffer_default_free(buf);
}


static gboolean
plugin_init (GModule *module, GstPlugin *plugin)
{
  GstElementFactory *factory;
  GstCaps *caps;

  /* create an elementfactory for the v4lmjpegsrcparse element */
  factory = gst_element_factory_new("v4lmjpegsrc",GST_TYPE_V4LMJPEGSRC,
                                   &gst_v4lmjpegsrc_details);
  g_return_val_if_fail(factory != NULL, FALSE);

  caps = gst_caps_new ("v4lmjpegsrc_caps",
                       "video/jpeg",
                       gst_props_new (
                          "width",  GST_PROPS_INT_RANGE (0, G_MAXINT),
                          "height", GST_PROPS_INT_RANGE (0, G_MAXINT),
                          NULL       )
                      );
  capslist = gst_caps_append(capslist, caps);

  src_template = gst_pad_template_new (
		  "src",
                  GST_PAD_SRC,
  		  GST_PAD_ALWAYS,
		  capslist, NULL);

  gst_element_factory_add_pad_template (factory, src_template);

  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));

  return TRUE;
}


GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "v4lmjpegsrc",
  plugin_init
};
