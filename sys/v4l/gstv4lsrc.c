/* G-Streamer BT8x8/V4L frame grabber plugin
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
#include <sys/time.h>
#include "v4lsrc_calls.h"


static GstElementDetails gst_v4lsrc_details = {
  "Video (video4linux/raw) Source",
  "Source/Video",
  "Reads raw frames from a video4linux (BT8x8) device",
  VERSION,
  "Ronald Bultje <rbultje@ronald.bitfreak.net>",
  "(C) 2001",
};

/* V4lSrc signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

/* arguments */
enum {
  ARG_0,
  ARG_WIDTH,
  ARG_HEIGHT,
  ARG_PALETTE
};


/* init functions */
static void                  gst_v4lsrc_class_init   (GstV4lSrcClass *klass);
static void                  gst_v4lsrc_init         (GstV4lSrc      *v4lsrc);

/* pad/buffer functions */
static GstPadNegotiateReturn gst_v4lsrc_negotiate    (GstPad         *pad,
                                                      GstCaps        **caps,
                                                      gpointer       *user_data);
static GstCaps*              gst_v4lsrc_create_caps  (GstV4lSrc      *v4lsrc);
static GstBuffer*            gst_v4lsrc_get          (GstPad         *pad);

/* get/set params */
static void                  gst_v4lsrc_set_property (GObject        *object,
                                                      guint          prop_id,
                                                      const GValue   *value,
                                                      GParamSpec     *pspec);
static void                  gst_v4lsrc_get_property (GObject        *object,
                                                      guint          prop_id,
                                                      GValue         *value,
                                                      GParamSpec     *pspec);

/* state handling */
static GstElementStateReturn gst_v4lsrc_change_state (GstElement     *element);

/* bufferpool functions */
static GstBuffer*            gst_v4lsrc_buffer_new   (GstBufferPool  *pool,
                                                      gint64         location,
                                                      gint           size,
                                                      gpointer       user_data);
static GstBuffer*            gst_v4lsrc_buffer_copy  (GstBuffer      *srcbuf);
static void                  gst_v4lsrc_buffer_free  (GstBuffer      *buf);


static GstElementClass *parent_class = NULL;\
//static guint gst_v4lsrc_signals[LAST_SIGNAL] = { 0 };


GType
gst_v4lsrc_get_type (void)
{
  static GType v4lsrc_type = 0;

  if (!v4lsrc_type) {
    static const GTypeInfo v4lsrc_info = {
      sizeof(GstV4lSrcClass),
      NULL,
      NULL,
      (GClassInitFunc)gst_v4lsrc_class_init,
      NULL,
      NULL,
      sizeof(GstV4lSrc),
      0,
      (GInstanceInitFunc)gst_v4lsrc_init,
      NULL
    };
    v4lsrc_type = g_type_register_static(GST_TYPE_V4LELEMENT, "GstV4lSrc", &v4lsrc_info, 0);
  }
  return v4lsrc_type;
}


static void
gst_v4lsrc_class_init (GstV4lSrcClass *klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  parent_class = g_type_class_ref(GST_TYPE_V4LELEMENT);

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_WIDTH,
    g_param_spec_int("width","width","width",
    G_MININT,G_MAXINT,0,G_PARAM_READWRITE));
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_HEIGHT,
    g_param_spec_int("height","height","height",
    G_MININT,G_MAXINT,0,G_PARAM_READWRITE));
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_PALETTE,
    g_param_spec_int("palette","palette","palette",
    G_MININT,G_MAXINT,0,G_PARAM_READWRITE));

  gobject_class->set_property = gst_v4lsrc_set_property;
  gobject_class->get_property = gst_v4lsrc_get_property;

  gstelement_class->change_state = gst_v4lsrc_change_state;
}


static void
gst_v4lsrc_init (GstV4lSrc *v4lsrc)
{
  v4lsrc->srcpad = gst_pad_new("src", GST_PAD_SRC);
  gst_element_add_pad(GST_ELEMENT(v4lsrc), v4lsrc->srcpad);

  gst_pad_set_get_function (v4lsrc->srcpad, gst_v4lsrc_get);
  gst_pad_set_negotiate_function (v4lsrc->srcpad, gst_v4lsrc_negotiate);

  v4lsrc->bufferpool = gst_buffer_pool_new();
  gst_buffer_pool_set_buffer_new_function(v4lsrc->bufferpool, gst_v4lsrc_buffer_new);
  gst_buffer_pool_set_buffer_copy_function(v4lsrc->bufferpool, gst_v4lsrc_buffer_copy);
  gst_buffer_pool_set_buffer_free_function(v4lsrc->bufferpool, gst_v4lsrc_buffer_free);
  gst_buffer_pool_set_user_data(v4lsrc->bufferpool, v4lsrc);

  v4lsrc->palette = VIDEO_PALETTE_YUV420P;
  v4lsrc->width = 160;
  v4lsrc->height = 120;
  v4lsrc->buffer_size = v4lsrc->width * v4lsrc->height * 1.5;

  v4lsrc->init = TRUE;
}


static GstPadNegotiateReturn
gst_v4lsrc_negotiate (GstPad   *pad,
                      GstCaps  **caps,
                      gpointer *user_data) 
{
  GstV4lSrc *v4lsrc;

  v4lsrc = GST_V4LSRC (gst_pad_get_parent (pad));

  if (!*caps) {
    return GST_PAD_NEGOTIATE_FAIL;
  }
  else {
    gint width, height;
    gulong format;

    width  = gst_caps_get_int (*caps, "width");
    height = gst_caps_get_int (*caps, "height");
    format = gst_caps_get_fourcc_int (*caps, "format");

    switch (format) {
      case GST_MAKE_FOURCC ('R','G','B',' '):
      {
        gint depth;

        depth = gst_caps_get_int (*caps, "depth");

        switch (depth) {
          case 15:
            v4lsrc->palette = VIDEO_PALETTE_RGB555;
            v4lsrc->buffer_size = width * height * 2;
            break;
          case 16:
            v4lsrc->palette = VIDEO_PALETTE_RGB565;
            v4lsrc->buffer_size = width * height * 2;
            break;
          case 24:
            v4lsrc->palette = VIDEO_PALETTE_RGB24;
            v4lsrc->buffer_size = width * height * 3;
            break;
          case 32:
            v4lsrc->palette = VIDEO_PALETTE_RGB32;
            v4lsrc->buffer_size = width * height * 4;
            break;
          default:
            *caps = NULL;
            return GST_PAD_NEGOTIATE_TRY;
        }

        break;
      }

      case GST_MAKE_FOURCC ('I','4','2','0'):
        v4lsrc->palette = VIDEO_PALETTE_YUV420P;
        v4lsrc->buffer_size = width * height * 1.5;
        break;

      case GST_MAKE_FOURCC ('U','Y','V','Y'):
        v4lsrc->palette = VIDEO_PALETTE_UYVY; //YUV422?;
        v4lsrc->buffer_size = width * height * 2;
        break;

      case GST_MAKE_FOURCC ('Y','U','Y','2'):
        v4lsrc->palette = VIDEO_PALETTE_YUYV; //YUV422?;
        v4lsrc->buffer_size = width * height * 2;
        break;

      /* TODO: add YUV4:2:2 planar and YUV4:2:0 packed, maybe also YUV4:1:1? */

      default:
        *caps = NULL;
        return GST_PAD_NEGOTIATE_TRY;

    }

    /* if we get here, it's okay */
    v4lsrc->width  = width;
    v4lsrc->height = height;

    return GST_PAD_NEGOTIATE_AGREE;
  }

  return GST_PAD_NEGOTIATE_FAIL;
}


static GstCaps*
gst_v4lsrc_create_caps (GstV4lSrc *v4lsrc)
{
  GstCaps *caps = NULL;
  gulong fourcc = 0;

  switch (v4lsrc->palette) {
    case VIDEO_PALETTE_RGB555:
    case VIDEO_PALETTE_RGB565:
    case VIDEO_PALETTE_RGB24:
    case VIDEO_PALETTE_RGB32:
    {
      int depth=0, bpp=0;

      fourcc = GST_STR_FOURCC ("RGB ");

      switch (v4lsrc->palette) {
        case VIDEO_PALETTE_RGB555:
          depth = 15;
          bpp = 2;
          break;
        case VIDEO_PALETTE_RGB565:
          depth = 16;
          bpp = 2;
          break;
        case VIDEO_PALETTE_RGB24:
          depth = 24;
          bpp = 3;
          break;
        case VIDEO_PALETTE_RGB32:
          depth = 32;
          bpp = 4;
          break;
      }

      caps = GST_CAPS_NEW (
        "v4lsrc_caps",
        "video/raw",
        "format",      GST_PROPS_FOURCC (fourcc),
        "width",       GST_PROPS_INT (v4lsrc->width),
        "height",      GST_PROPS_INT (v4lsrc->height),
        "bpp",         GST_PROPS_INT (bpp),
        "depth",       GST_PROPS_INT (depth)
      );

      v4lsrc->buffer_size = v4lsrc->width * v4lsrc->height * bpp;

      break;
    }

    case VIDEO_PALETTE_YUV422:
    case VIDEO_PALETTE_YUV420P:
    {
      switch (v4lsrc->palette) {
        case VIDEO_PALETTE_YUV422:
          fourcc = (G_BYTE_ORDER == G_BIG_ENDIAN) ?
            GST_STR_FOURCC("UYVY") : GST_STR_FOURCC("YUY2");
          v4lsrc->buffer_size = v4lsrc->width * v4lsrc->height * 2;
          break;
        case VIDEO_PALETTE_YUYV:
          fourcc = GST_STR_FOURCC("YUY2");
          v4lsrc->buffer_size = v4lsrc->width * v4lsrc->height * 2;
          break;
        case VIDEO_PALETTE_UYVY:
          fourcc = GST_STR_FOURCC("UYVY");
          v4lsrc->buffer_size = v4lsrc->width * v4lsrc->height * 2;
          break;
        case VIDEO_PALETTE_YUV420P:
          fourcc = GST_STR_FOURCC("I420");
          v4lsrc->buffer_size = v4lsrc->width * v4lsrc->height * 1.5;
          break;
      }

      caps = GST_CAPS_NEW (
        "v4lsrc_caps",
        "video/raw",
        "format",      GST_PROPS_FOURCC (fourcc),
        "width",       GST_PROPS_INT (v4lsrc->width),
        "height",      GST_PROPS_INT (v4lsrc->height)
      );

      break;
    }

    default:
      return NULL;
  }

  return caps;
}


static GstBuffer*
gst_v4lsrc_get (GstPad *pad)
{
  GstV4lSrc *v4lsrc;
  GstBuffer *buf;
  gint num;

  g_return_val_if_fail (pad != NULL, NULL);

  v4lsrc = GST_V4LSRC (gst_pad_get_parent (pad));

  if (v4lsrc->init) {
    gst_pad_set_caps (v4lsrc->srcpad, gst_v4lsrc_create_caps (v4lsrc));
    v4lsrc->init = FALSE;
  }
  else {
    if (!gst_pad_get_caps (v4lsrc->srcpad) && 
        !gst_pad_renegotiate (v4lsrc->srcpad)) {
      return NULL;
    }
  }

  buf = gst_buffer_new_from_pool(v4lsrc->bufferpool, 0, 0);
  if (!buf)
  {
    gst_element_error(GST_ELEMENT(v4lsrc),
      "Failed to create a new GstBuffer");
    return NULL;
  }

  /* grab a frame from the device */
  if (!gst_v4lsrc_grab_frame(v4lsrc, &num))
    return NULL;
  GST_BUFFER_DATA(buf) = gst_v4lsrc_get_buffer(v4lsrc, num);
  GST_BUFFER_SIZE(buf) = v4lsrc->buffer_size;
  buf->timestamp = v4lsrc->timestamp_soft_sync[num].tv_sec * 1000000000 +
    v4lsrc->timestamp_soft_sync[num].tv_usec * 1000;

  return buf;
}


static void
gst_v4lsrc_set_property (GObject      *object,
                         guint        prop_id,
                         const GValue *value,
                         GParamSpec   *pspec)
{
  GstV4lSrc *v4lsrc;

  g_return_if_fail(GST_IS_V4LSRC(object));
  v4lsrc = GST_V4LSRC(object);

  switch (prop_id) {
    case ARG_WIDTH:
      v4lsrc->width = g_value_get_int(value);
      break;

    case ARG_HEIGHT:
      v4lsrc->height = g_value_get_int(value);
      break;

    case ARG_PALETTE:
      v4lsrc->palette = g_value_get_int(value);
      break;

    default:
      /*parent_class->set_property(object, prop_id, value, pspec);*/
      break;
  }
}


static void
gst_v4lsrc_get_property (GObject    *object,
                         guint      prop_id,
                         GValue     *value,
                         GParamSpec *pspec)
{
  GstV4lSrc *v4lsrc;

  g_return_if_fail(GST_IS_V4LSRC(object));
  v4lsrc = GST_V4LSRC(object);

  switch (prop_id) {
    case ARG_WIDTH:
      g_value_set_int(value, v4lsrc->mmap.width);
      break;

    case ARG_HEIGHT:
      g_value_set_int(value, v4lsrc->mmap.height);
      break;

    case ARG_PALETTE:
      g_value_set_int(value, v4lsrc->mmap.format);
      break;

    default:
      /*parent_class->get_property(object, prop_id, value, pspec);*/
      break;
  }
}


static GstElementStateReturn
gst_v4lsrc_change_state (GstElement *element)
{
  GstV4lSrc *v4lsrc;
  
  g_return_val_if_fail(GST_IS_V4LSRC(element), FALSE);
  
  v4lsrc = GST_V4LSRC(element);

  if (GST_ELEMENT_CLASS(parent_class)->change_state)
    return GST_ELEMENT_CLASS(parent_class)->change_state(element);

  switch (GST_STATE_TRANSITION(element)) {
    case GST_STATE_NULL_TO_READY:
      if (GST_V4LELEMENT(v4lsrc)->norm >= VIDEO_MODE_PAL &&
          GST_V4LELEMENT(v4lsrc)->norm < VIDEO_MODE_AUTO &&
          GST_V4LELEMENT(v4lsrc)->channel < 0)
        if (!gst_v4l_set_chan_norm(GST_V4LELEMENT(v4lsrc),
             0, GST_V4LELEMENT(v4lsrc)->norm))
          return GST_STATE_FAILURE;
      break;
    case GST_STATE_READY_TO_PAUSED:
      /* set capture parameters and mmap the buffers */
      if (!gst_v4lsrc_set_capture(v4lsrc, v4lsrc->width, v4lsrc->height, v4lsrc->palette))
        return GST_STATE_FAILURE;
      v4lsrc->init = TRUE;
      if (!gst_v4lsrc_capture_init(v4lsrc))
        return GST_STATE_FAILURE;
      break;
    case GST_STATE_PAUSED_TO_PLAYING:
      /* queue all buffer, start streaming capture */
      if (!gst_v4lsrc_capture_start(v4lsrc))
        return GST_STATE_FAILURE;
      break;
    case GST_STATE_PLAYING_TO_PAUSED:
      /* de-queue all queued buffers */
      if (!gst_v4lsrc_capture_stop(v4lsrc))
        return GST_STATE_FAILURE;
      break;
    case GST_STATE_PAUSED_TO_READY:
      /* stop capturing, unmap all buffers */
      if (!gst_v4lsrc_capture_deinit(v4lsrc))
        return GST_STATE_FAILURE;
      break;
  }

  return GST_STATE_SUCCESS;
}


static GstBuffer*
gst_v4lsrc_buffer_new (GstBufferPool *pool,
                       gint64        location,
                       gint          size,
                       gpointer      user_data)
{
  GstBuffer *buffer;

  buffer = gst_buffer_new();
  if (!buffer) return NULL;
  buffer->pool_private = user_data;

  /* TODO: add interlacing info to buffer as metadata (height>288 or 240 = topfieldfirst, else noninterlaced) */

  return buffer;
}


static GstBuffer*
gst_v4lsrc_buffer_copy (GstBuffer *srcbuf)
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
gst_v4lsrc_buffer_free (GstBuffer *buf)
{
  GstV4lSrc *v4lsrc = buf->pool_private;
  int n;

  for (n=0;n<v4lsrc->mbuf.frames;n++)
    if (GST_BUFFER_DATA(buf) == gst_v4lsrc_get_buffer(v4lsrc, n))
    {
      gst_v4lsrc_requeue_frame(v4lsrc, n);
      return;
    }

  gst_element_error(GST_ELEMENT(v4lsrc),
    "Couldn't find the buffer");
}


static gboolean
plugin_init (GModule   *module,
             GstPlugin *plugin)
{
  GstElementFactory *factory;

  /* create an elementfactory for the v4lsrc */
  factory = gst_elementfactory_new("v4lsrc",GST_TYPE_V4LSRC,
                                   &gst_v4lsrc_details);
  g_return_val_if_fail(factory != NULL, FALSE);
  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));

  return TRUE;
}


GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "v4lsrc",
  plugin_init
};
