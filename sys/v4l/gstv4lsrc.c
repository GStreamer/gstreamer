/* Gnome-Streamer
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

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <string.h>

//#define DEBUG_ENABLED
#include <gstv4lsrc.h>

#include <linux/videodev.h>

static GstElementDetails gst_v4lsrc_details = {
  "Video (v4l) Source",
  "Source/Video",
  "Read from a Video for Linux capture device",
  VERSION,
  "Wim Taymans <wim.taymans@tvd.be>",
  "(C) 2000",
};

/* V4lSrc signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_WIDTH,
  ARG_HEIGHT,
  ARG_FORMAT,
  ARG_TUNE,
  ARG_TUNED,
  ARG_INPUT,
  ARG_NORM,
  ARG_VOLUME,
  ARG_MUTE,
  ARG_AUDIO_MODE,
  ARG_COLOR,
  ARG_BRIGHT,
  ARG_HUE,
  ARG_CONTRAST,
  ARG_DEVICE,
};


static void			gst_v4lsrc_class_init	(GstV4lSrcClass *klass);
static void			gst_v4lsrc_init		(GstV4lSrc *v4lsrc);

static void			gst_v4lsrc_set_property	(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void			gst_v4lsrc_get_property	(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);

static GstElementStateReturn	gst_v4lsrc_change_state	(GstElement *element);
static void			gst_v4lsrc_close_v4l	(GstV4lSrc *src);
static gboolean			gst_v4lsrc_open_v4l	(GstV4lSrc *src);

static GstBuffer*		gst_v4lsrc_get		(GstPad *pad);
static GstPadNegotiateReturn 	gst_v4lsrc_negotiate 	(GstPad *pad, GstCaps **caps, gpointer *user_data);

static gboolean			gst_v4lsrc_sync_parms	(GstV4lSrc *v4lsrc);

static GstElementClass *parent_class = NULL;
////static guint gst_v4lsrc_signals[LAST_SIGNAL] = { 0 };

GType
gst_v4lsrc_get_type (void)
{
  static GType v4lsrc_type = 0;

  if (!v4lsrc_type) {
    static const GTypeInfo v4lsrc_info = {
      sizeof(GstV4lSrcClass),      NULL,
      NULL,
      (GClassInitFunc)gst_v4lsrc_class_init,
      NULL,
      NULL,
      sizeof(GstV4lSrc),
      0,
      (GInstanceInitFunc)gst_v4lsrc_init,
      NULL
    };
    v4lsrc_type = g_type_register_static(GST_TYPE_ELEMENT, "GstV4lSrc", &v4lsrc_info, 0);
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

  parent_class = g_type_class_ref(GST_TYPE_ELEMENT);

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_WIDTH,
    g_param_spec_int("width","width","width",
                     G_MININT,G_MAXINT,0,G_PARAM_READWRITE)); // CHECKME
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_HEIGHT,
    g_param_spec_int("height","height","height",
                     G_MININT,G_MAXINT,0,G_PARAM_READWRITE)); // CHECKME
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_FORMAT,
    g_param_spec_int("format","format","format",
                     G_MININT,G_MAXINT,0,G_PARAM_READWRITE)); // CHECKME

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_TUNE,
    g_param_spec_ulong("tune","tune","tune",
                       0,G_MAXULONG,0,G_PARAM_WRITABLE)); // CHECKME
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_TUNED,
    g_param_spec_boolean("tuned","tuned","tuned",
                         TRUE,G_PARAM_READABLE)); // CHECKME
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_INPUT,
    g_param_spec_int("input","input","input",
                     G_MININT,G_MAXINT,0,G_PARAM_WRITABLE)); // CHECKME
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_NORM,
    g_param_spec_int("norm","norm","norm",
                     G_MININT,G_MAXINT,0,G_PARAM_WRITABLE)); // CHECKME
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_VOLUME,
    g_param_spec_int("volume","volume","volume",
                     G_MININT,G_MAXINT,0,G_PARAM_READWRITE)); // CHECKME
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_MUTE,
    g_param_spec_boolean("mute","mute","mute",
                         TRUE,G_PARAM_READWRITE)); // CHECKME
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_AUDIO_MODE,
    g_param_spec_int("mode","mode","mode",
                     G_MININT,G_MAXINT,0,G_PARAM_READWRITE)); // CHECKME
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_COLOR,
    g_param_spec_int("color","color","color",
                     G_MININT,G_MAXINT,0,G_PARAM_READWRITE)); // CHECKME
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_BRIGHT,
    g_param_spec_int("bright","bright","bright",
                     G_MININT,G_MAXINT,0,G_PARAM_READWRITE)); // CHECKME
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_HUE,
    g_param_spec_int("hue","hue","hue",
                     G_MININT,G_MAXINT,0,G_PARAM_READWRITE)); // CHECKME
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_CONTRAST,
    g_param_spec_int("contrast","contrast","contrast",
                     G_MININT,G_MAXINT,0,G_PARAM_READWRITE)); // CHECKME
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_DEVICE,
    g_param_spec_string("device","device","device",
                        NULL, G_PARAM_READWRITE)); // CHECKME

  gobject_class->set_property = gst_v4lsrc_set_property;
  gobject_class->get_property = gst_v4lsrc_get_property;

  gstelement_class->change_state = gst_v4lsrc_change_state;
}

static void
gst_v4lsrc_init (GstV4lSrc *v4lsrc)
{
  v4lsrc->srcpad = gst_pad_new("src",GST_PAD_SRC);
  gst_element_add_pad(GST_ELEMENT(v4lsrc),v4lsrc->srcpad);

  gst_pad_set_get_function (v4lsrc->srcpad,gst_v4lsrc_get);
  gst_pad_set_negotiate_function (v4lsrc->srcpad,gst_v4lsrc_negotiate);

  /* if the destination cannot say what it wants, we give this */
  v4lsrc->width = 100;
  v4lsrc->height = 100;
  v4lsrc->format = 0;
  v4lsrc->buffer_size = v4lsrc->width * v4lsrc->height * 3;
  // make a grbber
  v4lsrc->grabber = grab_init();
  v4lsrc->device = NULL;
  v4lsrc->init = TRUE;
}

static GstPadNegotiateReturn
gst_v4lsrc_negotiate (GstPad *pad, GstCaps **caps, gpointer *user_data) 
{
  GstV4lSrc *v4lsrc;

  GST_DEBUG (0, "v4lsrc: negotiate %p\n", user_data); 

  v4lsrc = GST_V4LSRC (gst_pad_get_parent (pad));

  if (!*caps) {
    return GST_PAD_NEGOTIATE_FAIL;
  }
  else {
    gint width, height;
    gulong format;

    GST_DEBUG (0, "%08lx\n", gst_caps_get_fourcc_int (*caps, "format"));

    width  = gst_caps_get_int (*caps, "width");
    height = gst_caps_get_int (*caps, "height");

    format =  gst_caps_get_fourcc_int (*caps, "format");

    g_print ("v4lsrc: got format %08lx\n", format);

    switch (format) {
      case GST_MAKE_FOURCC ('R','G','B',' '):
      {
        gint depth, endianness, bpp;

        depth = gst_caps_get_int (*caps, "depth");
        bpp = gst_caps_get_int (*caps, "bpp");
        endianness = gst_caps_get_int (*caps, "endianness");

        GST_DEBUG (0, "%d\n", depth);
        g_print ("v4lsrc: got depth %d, bpp %d, endianness %d\n", depth, bpp, endianness);
	switch (depth) {
          case 15:
            v4lsrc->format = (endianness == G_LITTLE_ENDIAN ? 
      		     VIDEO_RGB15_LE:
      		     VIDEO_RGB15_BE);
            v4lsrc->buffer_size = width * height * 2;
            break;
          case 16:
            v4lsrc->format = (endianness == G_LITTLE_ENDIAN ? 
      		     VIDEO_RGB16_LE:
      		     VIDEO_RGB16_BE);
            v4lsrc->buffer_size = width * height * 2;
            break;
          case 24:
            v4lsrc->format = (endianness == G_LITTLE_ENDIAN ? 
      		     VIDEO_BGR24:
      		     VIDEO_RGB24);
            v4lsrc->buffer_size = width * height * 3;
            break;
          case 32:
            v4lsrc->format = (endianness == G_LITTLE_ENDIAN ? 
      		     VIDEO_BGR32:
      		     VIDEO_RGB32);
            v4lsrc->buffer_size = width * height * 4;
            break;
          default:
            *caps = NULL;
            return GST_PAD_NEGOTIATE_TRY;
	}
	break;
      }
      case GST_MAKE_FOURCC ('I','4','2','0'):
        v4lsrc->format = VIDEO_YUV420P;
        v4lsrc->buffer_size = width * height +
        		      width * height / 2;
	break;
      case GST_MAKE_FOURCC ('U','Y','V','Y'):
	if (G_BYTE_ORDER == G_BIG_ENDIAN) {
          v4lsrc->format = VIDEO_YUV422;
          v4lsrc->buffer_size = width * height * 2;
	  break;
	}
	else {
          *caps = NULL;
          return GST_PAD_NEGOTIATE_TRY;
	}
      case GST_MAKE_FOURCC ('Y','U','Y','2'):
	if (G_BYTE_ORDER == G_LITTLE_ENDIAN) {
          v4lsrc->format = VIDEO_YUV422;
          v4lsrc->buffer_size = width * height * 2;
	  break;
	}
	else {
          *caps = NULL;
          return GST_PAD_NEGOTIATE_TRY;
	}
      default:
        *caps = NULL;
        return GST_PAD_NEGOTIATE_TRY;
    }
    v4lsrc->width  = width;
    v4lsrc->height = height;

    if (gst_v4lsrc_sync_parms (v4lsrc)) {
      return GST_PAD_NEGOTIATE_AGREE;
    }
    else {
      *caps = NULL;
      return GST_PAD_NEGOTIATE_TRY;
    }
  }

  return GST_PAD_NEGOTIATE_FAIL;
}

static GstCaps*
gst_v4lsrc_create_caps (GstV4lSrc *src)
{
  GstCaps *caps;
  gulong fourcc = 0;
  gint width, height;

  width = src->width;
  height = src->height;

  switch (src->format) {
    case VIDEO_RGB08:
    case VIDEO_GRAY:
    case VIDEO_LUT2:
    case VIDEO_LUT4:
      caps = NULL;
      break;
    case VIDEO_RGB15_LE:
    case VIDEO_RGB16_LE:
    case VIDEO_RGB15_BE:
    case VIDEO_RGB16_BE:
    case VIDEO_BGR24:
    case VIDEO_BGR32:
    case VIDEO_RGB24:
    case VIDEO_RGB32:
      caps = NULL;
      break;
    case VIDEO_YUV422:
    case VIDEO_YUV422P:
    case VIDEO_YUV420P: {

      if (src->format == VIDEO_YUV422) {
        fourcc = GST_STR_FOURCC ("YUY2");
        src->buffer_size = width * height * 2;
      }
      else if (src->format == VIDEO_YUV422P) {
        fourcc = GST_STR_FOURCC ("YV12");
        src->buffer_size = width * height * 2;
      }
      else if (src->format == VIDEO_YUV420P) {
        fourcc = GST_STR_FOURCC ("I420");
        src->buffer_size = width * height +
        		      width * height / 2;
      }
      
      caps = GST_CAPS_NEW (
		      "v4lsrc_caps",
		      "video/raw",
		        "format",	GST_PROPS_FOURCC (fourcc),
			"width",	GST_PROPS_INT (src->width),
			"height",	GST_PROPS_INT (src->height)
			);
      break;
    }
    default:
      caps = NULL;
      break;
  }

  return caps;
}

static GstBuffer*
gst_v4lsrc_get (GstPad *pad)
{
  GstV4lSrc *v4lsrc;
  GstBuffer *buf = NULL;
  guint8 *grab_buf;

  g_return_val_if_fail (pad != NULL, NULL);

  v4lsrc = GST_V4LSRC (gst_pad_get_parent (pad));

  if (v4lsrc->format && v4lsrc->init) {
    gst_pad_set_caps (v4lsrc->srcpad, gst_v4lsrc_create_caps (v4lsrc));
    v4lsrc->init = FALSE;
  }
  else {
    if (!gst_pad_get_caps (v4lsrc->srcpad) && 
        !gst_pad_renegotiate (v4lsrc->srcpad)) {
      return NULL;
    }
  }

  buf = gst_buffer_new();
  GST_BUFFER_DATA(buf) = g_malloc(v4lsrc->buffer_size);
  GST_BUFFER_SIZE(buf) = v4lsrc->buffer_size;
  GST_DEBUG (0,"v4lsrc: making new buffer %p\n", GST_BUFFER_DATA(buf));

  GST_DEBUG (0,"v4lsrc: request buffer\n");
  // request a buffer from the grabber
  grab_buf = v4lsrc->grabber->grab_capture(v4lsrc->grabber, 0);
  //meta_pull->overlay_info->did_overlay = FALSE;

  g_assert(buf != NULL);

  GST_DEBUG (0,"v4lsrc: sending %d bytes in %p\n", GST_BUFFER_SIZE(buf), GST_BUFFER_DATA(buf));
  // copy the buffer
  memcpy(GST_BUFFER_DATA(buf), grab_buf, GST_BUFFER_SIZE(buf));

  GST_DEBUG (0,"v4lsrc: sent %d bytes in %p\n", GST_BUFFER_SIZE(buf), GST_BUFFER_DATA(buf));

  return buf;
}

static void
gst_v4lsrc_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  GstV4lSrc *src;
  int ret = 0;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_V4LSRC(object));
  src = GST_V4LSRC(object);

  switch (prop_id) {
    case ARG_WIDTH:
      src->width = g_value_get_int (value);
      gst_v4lsrc_sync_parms(src);
      break;
    case ARG_HEIGHT:
      src->height = g_value_get_int (value);
      gst_v4lsrc_sync_parms(src);
      break;
    case ARG_FORMAT:
      src->format = g_value_get_int (value);
      break;
    case ARG_TUNE:
      src->tune = g_value_get_ulong (value);
      ret = src->grabber->grab_tune(src->grabber, src->tune);
      break;
    case ARG_INPUT:
      src->input = g_value_get_int (value);
      ret = src->grabber->grab_input(src->grabber, src->input, -1);
      break;
    case ARG_NORM:
      src->norm = g_value_get_int (value);
      ret = src->grabber->grab_input(src->grabber, -1, src->norm);
      break;
    case ARG_VOLUME:
      src->volume = g_value_get_int (value);
      ret = src->grabber->grab_setattr(src->grabber, GRAB_ATTR_VOLUME, src->volume);
      break;
    case ARG_MUTE:
      src->mute = g_value_get_boolean (value);
      ret = src->grabber->grab_setattr(src->grabber, GRAB_ATTR_MUTE, src->mute);
      break;
    case ARG_AUDIO_MODE:
      src->audio_mode = g_value_get_int (value);
      ret = src->grabber->grab_setattr(src->grabber, GRAB_ATTR_MODE, src->audio_mode);
      break;
    case ARG_COLOR:
      src->color = g_value_get_int (value);
      ret = src->grabber->grab_setattr(src->grabber, GRAB_ATTR_COLOR, src->color);
      break;
    case ARG_BRIGHT:
      src->bright = g_value_get_int (value);
      ret = src->grabber->grab_setattr(src->grabber, GRAB_ATTR_BRIGHT, src->bright);
      break;
    case ARG_HUE:
      src->hue = g_value_get_int (value);
      ret = src->grabber->grab_setattr(src->grabber, GRAB_ATTR_HUE, src->hue);
      break;
    case ARG_CONTRAST:
      src->contrast = g_value_get_int (value);
      ret = src->grabber->grab_setattr(src->grabber, GRAB_ATTR_CONTRAST, src->contrast);
      break;
    case ARG_DEVICE:
      if (src->device)
	g_free (src->device);
      src->device = g_strdup (g_value_get_string (value));
      break;
    default:
      ret = -1;
      break;
  }
  if (ret == -1) {
    fprintf(stderr, "v4lsrc: error setting property\n");
  }
}

static void
gst_v4lsrc_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  GstV4lSrc *src;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_V4LSRC(object));
  src = GST_V4LSRC(object);

  g_print ("get arg\n");

  switch (prop_id) {
    case ARG_WIDTH:
      g_value_set_int (value, src->width);
      break;
    case ARG_HEIGHT:
      g_value_set_int (value, src->height);
      break;
    case ARG_TUNED:
      g_value_set_boolean (value, src->grabber->grab_tuned(src->grabber));
      break;
    case ARG_VOLUME:
      g_value_set_int (value, src->grabber->grab_getattr(src->grabber, GRAB_ATTR_VOLUME));
      break;
    case ARG_MUTE:
      g_value_set_int (value, src->grabber->grab_getattr(src->grabber, GRAB_ATTR_MUTE));
      break;
    case ARG_AUDIO_MODE:
      g_value_set_int (value, src->grabber->grab_getattr(src->grabber, GRAB_ATTR_MODE));
      break;
    case ARG_COLOR:
      g_value_set_int (value, src->grabber->grab_getattr(src->grabber, GRAB_ATTR_COLOR));
      break;
    case ARG_BRIGHT:
      g_value_set_int (value, src->grabber->grab_getattr(src->grabber, GRAB_ATTR_BRIGHT));
      break;
    case ARG_HUE:
      g_value_set_int (value, src->grabber->grab_getattr(src->grabber, GRAB_ATTR_HUE));
      break;
    case ARG_CONTRAST:
      g_value_set_int (value, src->grabber->grab_getattr(src->grabber, GRAB_ATTR_CONTRAST));
      break;
    case ARG_DEVICE:
      g_value_set_string (value, src->device);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstElementStateReturn
gst_v4lsrc_change_state (GstElement *element)
{
  g_return_val_if_fail(GST_IS_V4LSRC(element), FALSE);

  /* if going down into NULL state, close the file if it's open */
  if (GST_STATE_PENDING(element) == GST_STATE_NULL) {
    if (GST_FLAG_IS_SET(element,GST_V4LSRC_OPEN))
      gst_v4lsrc_close_v4l(GST_V4LSRC(element));
  /* otherwise (READY or higher) we need to open the sound card */
  } else {
    gst_info ("v4lsrc: opening\n");
    if (!GST_FLAG_IS_SET(element,GST_V4LSRC_OPEN)) {
      if (!gst_v4lsrc_open_v4l(GST_V4LSRC(element))) {
	gst_info ("v4lsrc: open failed\n");
        return GST_STATE_FAILURE;
      }
    }
  }

  if (GST_ELEMENT_CLASS(parent_class)->change_state)
    return GST_ELEMENT_CLASS(parent_class)->change_state(element);

  return GST_STATE_SUCCESS;
}

static gboolean
gst_v4lsrc_sync_parms (GstV4lSrc *src)
{
  gint linelength;
  gboolean success;

  g_return_val_if_fail(src != NULL, FALSE);
  g_return_val_if_fail(GST_IS_V4LSRC(src), FALSE);

  GST_DEBUG (0,"v4lsrc: resync %d %d %d\n", src->width, src->height, src->format);

  if (!src->grabber->opened) 
    return FALSE;

  if (src->grabber->grab_setparams(src->grabber, src->format, &src->width, &src->height, &linelength) != 0) {
    fprintf(stderr, "v4lsrc: error setting params\n");
    success = FALSE;
  }
  else {
    GST_DEBUG (0,"v4lsrc: resynced to %d %d %d\n", src->width, src->height, src->buffer_size);
    success = TRUE;
  }
  return success;
}

static gboolean
gst_v4lsrc_open_v4l (GstV4lSrc *src)
{
  g_return_val_if_fail(src->grabber != NULL, FALSE);
  g_return_val_if_fail(!src->grabber->opened, FALSE);

  if (src->grabber->grab_open(src->grabber, src->device) != -1) {
    gst_v4lsrc_sync_parms(src);
    GST_FLAG_SET(src, GST_V4LSRC_OPEN);
    return TRUE;
  }
  return FALSE;
}

static void
gst_v4lsrc_close_v4l (GstV4lSrc *src)
{
  g_return_if_fail(src->grabber != NULL);
  g_return_if_fail(src->grabber->opened);

  src->grabber->grab_close(src->grabber);
  GST_FLAG_UNSET(src, GST_V4LSRC_OPEN);
}

static gboolean
plugin_init (GModule *module, GstPlugin *plugin)
{
  GstElementFactory *factory;

  /* create an elementfactory for the v4lsrcparse element */
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

