/* G-Streamer hardware MJPEG video source plugin
 * Copyright (C) 2001 Ronald Bultje <rbultje@ronald.bitfreak.net>
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

static GstElementDetails gst_v4lmjpegsrc_details = {
  "Video (video4linux/MJPEG) Source",
  "Source/Video",
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
static GstPadNegotiateReturn gst_v4lmjpegsrc_negotiate    (GstPad         *pad,
                                                           GstCaps        **caps,
                                                           gpointer       *user_data);
static GstCaps*              gst_v4lmjpegsrc_create_caps  (GstV4lMjpegSrc *v4lmjpegsrc);
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
                                                           gint64         location,
                                                           gint           size,
                                                           gpointer       user_data);
static GstBuffer*            gst_v4lmjpegsrc_buffer_copy  (GstBuffer      *srcbuf);
static void                  gst_v4lmjpegsrc_buffer_free  (GstBuffer      *buf);


static GstElementClass *parent_class = NULL;
//static guint gst_v4lmjpegsrc_signals[LAST_SIGNAL] = { 0 };


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
    g_param_spec_int("x_offset","x_offset","x_offset",G_MININT,G_MAXINT,0,G_PARAM_WRITABLE));
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_Y_OFFSET,
    g_param_spec_int("y_offset","y_offset","y_offset",G_MININT,G_MAXINT,0,G_PARAM_WRITABLE));
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_F_WIDTH,
    g_param_spec_int("frame_width","frame_width","frame_width",G_MININT,G_MAXINT,0,G_PARAM_WRITABLE));
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_F_HEIGHT,
    g_param_spec_int("frame_height","frame_height","frame_height",G_MININT,G_MAXINT,0,G_PARAM_WRITABLE));

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_H_DECIMATION,
    g_param_spec_int("h_decimation","h_decimation","h_decimation",G_MININT,G_MAXINT,0,G_PARAM_WRITABLE));
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_V_DECIMATION,
    g_param_spec_int("v_decimation","v_decimation","v_decimation",G_MININT,G_MAXINT,0,G_PARAM_WRITABLE));

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_WIDTH,
    g_param_spec_int("width","width","width",G_MININT,G_MAXINT,0,G_PARAM_READABLE));
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_HEIGHT,
    g_param_spec_int("height","height","height",G_MININT,G_MAXINT,0,G_PARAM_READABLE));

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_QUALITY,
    g_param_spec_int("quality","quality","quality",G_MININT,G_MAXINT,0,G_PARAM_WRITABLE));

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_NUMBUFS,
    g_param_spec_int("num_buffers","num_buffers","num_buffers",G_MININT,G_MAXINT,0,G_PARAM_READWRITE));
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_BUFSIZE,
    g_param_spec_int("buffer_size","buffer_size","buffer_size",G_MININT,G_MAXINT,0,G_PARAM_READWRITE));

  gobject_class->set_property = gst_v4lmjpegsrc_set_property;
  gobject_class->get_property = gst_v4lmjpegsrc_get_property;

  gstelement_class->change_state = gst_v4lmjpegsrc_change_state;
}


static void
gst_v4lmjpegsrc_init (GstV4lMjpegSrc *v4lmjpegsrc)
{
  v4lmjpegsrc->srcpad = gst_pad_new("src", GST_PAD_SRC);
  gst_element_add_pad(GST_ELEMENT(v4lmjpegsrc), v4lmjpegsrc->srcpad);

  gst_pad_set_get_function (v4lmjpegsrc->srcpad, gst_v4lmjpegsrc_get);
  gst_pad_set_negotiate_function (v4lmjpegsrc->srcpad, gst_v4lmjpegsrc_negotiate);

  v4lmjpegsrc->bufferpool = gst_buffer_pool_new();
  gst_buffer_pool_set_buffer_new_function(v4lmjpegsrc->bufferpool, gst_v4lmjpegsrc_buffer_new);
  gst_buffer_pool_set_buffer_copy_function(v4lmjpegsrc->bufferpool, gst_v4lmjpegsrc_buffer_copy);
  gst_buffer_pool_set_buffer_free_function(v4lmjpegsrc->bufferpool, gst_v4lmjpegsrc_buffer_free);
  gst_buffer_pool_set_user_data(v4lmjpegsrc->bufferpool, v4lmjpegsrc);

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

  v4lmjpegsrc->init = TRUE;
}



static GstPadNegotiateReturn
gst_v4lmjpegsrc_negotiate (GstPad   *pad,
                           GstCaps  **caps,
                           gpointer *user_data) 
{
  GstV4lMjpegSrc *v4lmjpegsrc;

  v4lmjpegsrc = GST_V4LMJPEGSRC (gst_pad_get_parent (pad));

  if (!*caps) {
    return GST_PAD_NEGOTIATE_FAIL;
  }
  else {
    return GST_PAD_NEGOTIATE_AGREE;
  }

  return GST_PAD_NEGOTIATE_FAIL;
}


static GstCaps*
gst_v4lmjpegsrc_create_caps (GstV4lMjpegSrc *v4lmjpegsrc)
{
  GstCaps *caps;

  caps = GST_CAPS_NEW (
    "v4lmjpegsrc_caps",
    "video/jpeg", 
    "width",            GST_PROPS_INT(v4lmjpegsrc->end_width),
    "height",           GST_PROPS_INT(v4lmjpegsrc->end_height)
  );

  return caps;
}


static GstBuffer*
gst_v4lmjpegsrc_get (GstPad *pad)
{
  GstV4lMjpegSrc *v4lmjpegsrc;
  GstBuffer *buf;
  gint num;

  g_return_val_if_fail (pad != NULL, NULL);

  v4lmjpegsrc = GST_V4LMJPEGSRC (gst_pad_get_parent (pad));

  if (v4lmjpegsrc->init) {
    gst_pad_set_caps (v4lmjpegsrc->srcpad, gst_v4lmjpegsrc_create_caps (v4lmjpegsrc));
    v4lmjpegsrc->init = FALSE;
  }
  else {
    if (!gst_pad_get_caps (v4lmjpegsrc->srcpad) && 
        !gst_pad_renegotiate (v4lmjpegsrc->srcpad)) {
      return NULL;
    }
  }

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
  buf->timestamp = v4lmjpegsrc->bsync.timestamp.tv_sec * 1000000000 +
    v4lmjpegsrc->bsync.timestamp.tv_usec * 1000;

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
      parent_class->set_property(object, prop_id, value, pspec);
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
    case ARG_NUMBUFS:
      g_value_set_int(value, v4lmjpegsrc->breq.count);
      break;
    case ARG_BUFSIZE:
      g_value_set_int(value, v4lmjpegsrc->breq.size);
      break;
    default:
      parent_class->get_property(object, prop_id, value, pspec);
      break;
  }
}


static GstElementStateReturn
gst_v4lmjpegsrc_change_state (GstElement *element)
{
  GstV4lMjpegSrc *v4lmjpegsrc;
  
  g_return_val_if_fail(GST_IS_V4LMJPEGSRC(element), FALSE);
  
  v4lmjpegsrc = GST_V4LMJPEGSRC(element);

  switch (GST_STATE_PENDING(element)) {
    case GST_STATE_READY:
      if (GST_V4L_IS_ACTIVE(GST_V4LELEMENT(v4lmjpegsrc))) {
        /* stop capturing, unmap all buffers */
        if (!gst_v4lmjpegsrc_capture_deinit(v4lmjpegsrc))
          return GST_STATE_FAILURE;
      }
      break;
    case GST_STATE_PAUSED:
      if (!GST_V4L_IS_ACTIVE(GST_V4LELEMENT(v4lmjpegsrc))) {
        /* set buffer info */
        if (!gst_v4lmjpegsrc_set_buffer(v4lmjpegsrc, v4lmjpegsrc->numbufs, v4lmjpegsrc->bufsize))
          return GST_STATE_FAILURE;
        /* set capture parameters and mmap the buffers */
        if (!v4lmjpegsrc->frame_width && !v4lmjpegsrc->frame_height &&
            v4lmjpegsrc->x_offset < 0 && v4lmjpegsrc->y_offset < 0 &&
            v4lmjpegsrc->horizontal_decimation == v4lmjpegsrc->vertical_decimation)
        {
          if (!gst_v4lmjpegsrc_set_capture(v4lmjpegsrc,
              v4lmjpegsrc->horizontal_decimation, v4lmjpegsrc->quality))
            return GST_STATE_FAILURE;
        }
        else
        {
          if (!gst_v4lmjpegsrc_set_capture_m(v4lmjpegsrc,
              v4lmjpegsrc->x_offset, v4lmjpegsrc->y_offset,
              v4lmjpegsrc->frame_width, v4lmjpegsrc->frame_height,
              v4lmjpegsrc->horizontal_decimation, v4lmjpegsrc->vertical_decimation,
              v4lmjpegsrc->quality))
            return GST_STATE_FAILURE;
        }
        v4lmjpegsrc->init = TRUE;
        if (!gst_v4lmjpegsrc_capture_init(v4lmjpegsrc))
          return GST_STATE_FAILURE;
      }
      else {
        /* de-queue all queued buffers */
        if (!gst_v4lmjpegsrc_capture_stop(v4lmjpegsrc))
          return GST_STATE_FAILURE;
      }
      break;
    case GST_STATE_PLAYING:
      /* queue all buffer, start streaming capture */
      if (!gst_v4lmjpegsrc_capture_start(v4lmjpegsrc))
        return GST_STATE_FAILURE;
      break;
  }

  if (GST_ELEMENT_CLASS(parent_class)->change_state)
    return GST_ELEMENT_CLASS(parent_class)->change_state(element);

  return GST_STATE_SUCCESS;
}


static GstBuffer*
gst_v4lmjpegsrc_buffer_new (GstBufferPool *pool,
                            gint64        location,
                            gint          size,
                            gpointer      user_data)
{
  GstBuffer *buffer;

  buffer = gst_buffer_new();
  if (!buffer) return NULL;
  buffer->pool_private = user_data;

  /* TODO: add interlacing info to buffer as metadata */

  return buffer;
}


static GstBuffer*
gst_v4lmjpegsrc_buffer_copy (GstBuffer *srcbuf)
{
  GstBuffer *buffer;

  buffer = gst_buffer_new();
  if (!buffer) return NULL;
  GST_BUFFER_DATA(buffer) = g_malloc(GST_BUFFER_SIZE(srcbuf));
  if (!GST_BUFFER_DATA(buffer)) return NULL;
  GST_BUFFER_SIZE(buffer) = GST_BUFFER_SIZE(srcbuf);
  memcpy(GST_BUFFER_DATA(buffer), GST_BUFFER_DATA(srcbuf), GST_BUFFER_SIZE(srcbuf));
  GST_BUFFER_TIMESTAMP(buffer) = GST_BUFFER_TIMESTAMP(srcbuf);

  return buffer;
}


static void
gst_v4lmjpegsrc_buffer_free (GstBuffer *buf)
{
  GstV4lMjpegSrc *v4lmjpegsrc = buf->pool_private;
  int n;

  for (n=0;n<v4lmjpegsrc->breq.count;n++)
    if (GST_BUFFER_DATA(buf) == gst_v4lmjpegsrc_get_buffer(v4lmjpegsrc, n))
    {
      gst_v4lmjpegsrc_requeue_frame(v4lmjpegsrc, n);
      return;
    }

  gst_element_error(GST_ELEMENT(v4lmjpegsrc),
    "Couldn't find the buffer");
}


static gboolean
plugin_init (GModule *module, GstPlugin *plugin)
{
  GstElementFactory *factory;

  /* create an elementfactory for the v4lmjpegsrcparse element */
  factory = gst_elementfactory_new("v4lmjpegsrc",GST_TYPE_V4LMJPEGSRC,
                                   &gst_v4lmjpegsrc_details);
  g_return_val_if_fail(factory != NULL, FALSE);
  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));

  return TRUE;
}


GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "v4lmjpegsrc",
  plugin_init
};
