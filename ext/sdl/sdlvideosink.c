/* GStreamer SDL plugin
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

/* let's not forget to mention that all this was based on aasink ;-) */

#include <config.h>
#include <signal.h>
#include <string.h>
#include <sys/time.h>

#include "sdlvideosink.h"

static GstElementDetails gst_sdlvideosink_details = {
  "Video sink",
  "Sink/Video",
  "An SDL-based videosink",
  VERSION,
  "Ronald Bultje <rbultje@ronald.bitfreak.net>",
  "(C) 2001",
};

/* sdlvideosink signals and args */
enum {
  SIGNAL_FRAME_DISPLAYED,
  SIGNAL_HAVE_SIZE,
  LAST_SIGNAL
};


enum {
  ARG_0,
  ARG_WIDTH,
  ARG_HEIGHT,
  ARG_DRIVER,
  ARG_DITHER,
  ARG_BRIGHTNESS,
  ARG_CONTRAST,
  ARG_GAMMA,
  ARG_INVERSION,
  ARG_RANDOMVAL,
  ARG_FRAMES_DISPLAYED,
  ARG_FRAME_TIME,
};

GST_PADTEMPLATE_FACTORY (sink_template,
  "sink",
  GST_PAD_SINK,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW (
    "sdlvideosink_caps",
    "video/raw",
      "format", 	GST_PROPS_FOURCC (GST_STR_FOURCC ("I420")),
        "width", 	GST_PROPS_INT_RANGE (0, G_MAXINT),
        "height", 	GST_PROPS_INT_RANGE (0, G_MAXINT)
  )
)
    
static void	gst_sdlvideosink_class_init		(GstSDLVideoSinkClass *klass);
static void	gst_sdlvideosink_init			(GstSDLVideoSink *sdlvideosink);

static void	gst_sdlvideosink_chain			(GstPad *pad, GstBuffer *buf);

static void	gst_sdlvideosink_set_property		(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void	gst_sdlvideosink_get_property		(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);
static void	gst_sdlvideosink_close			(GstSDLVideoSink *sdlvideosink);
static GstElementStateReturn gst_sdlvideosink_change_state (GstElement *element);

static GstElementClass *parent_class = NULL;
static guint gst_sdlvideosink_signals[LAST_SIGNAL] = { 0 };

GType
gst_sdlvideosink_get_type (void)
{
  static GType sdlvideosink_type = 0;

  if (!sdlvideosink_type) {
    static const GTypeInfo sdlvideosink_info = {
      sizeof(GstSDLVideoSinkClass),      NULL,
      NULL,
      (GClassInitFunc)gst_sdlvideosink_class_init,
      NULL,
      NULL,
      sizeof(GstSDLVideoSink),
      0,
      (GInstanceInitFunc)gst_sdlvideosink_init,
    };
    sdlvideosink_type = g_type_register_static(GST_TYPE_ELEMENT, "GstSDLVideoSink", &sdlvideosink_info, 0);
  }
  return sdlvideosink_type;
}

static void
gst_sdlvideosink_class_init (GstSDLVideoSinkClass *klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_WIDTH,
    g_param_spec_int("width","width","width",
                     G_MININT,G_MAXINT,0,G_PARAM_READWRITE)); // CHECKME
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_HEIGHT,
    g_param_spec_int("height","height","height",
                     G_MININT,G_MAXINT,0,G_PARAM_READWRITE)); // CHECKME
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_BRIGHTNESS,
    g_param_spec_int("brightness","brightness","brightness",
                     G_MININT,G_MAXINT,0,G_PARAM_READWRITE)); // CHECKME
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_CONTRAST,
    g_param_spec_int("contrast","contrast","contrast",
                     G_MININT,G_MAXINT,0,G_PARAM_READWRITE)); // CHECKME
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_GAMMA,
    g_param_spec_float("gamma","gamma","gamma",
                       0.0,5.0,1.0,G_PARAM_READWRITE)); // CHECKME
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_INVERSION,
    g_param_spec_boolean("inversion","inversion","inversion",
                         TRUE,G_PARAM_READWRITE)); // CHECKME
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_RANDOMVAL,
    g_param_spec_int("randomval","randomval","randomval",
                     G_MININT,G_MAXINT,0,G_PARAM_READWRITE)); // CHECKME
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_FRAMES_DISPLAYED,
    g_param_spec_int("frames_displayed","frames_displayed","frames_displayed",
                     G_MININT,G_MAXINT,0,G_PARAM_READABLE)); // CHECKME
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_FRAME_TIME,
    g_param_spec_int("frame_time","frame_time","frame_time",
                     G_MININT,G_MAXINT,0,G_PARAM_READABLE)); // CHECKME

  gobject_class->set_property = gst_sdlvideosink_set_property;
  gobject_class->get_property = gst_sdlvideosink_get_property;

  gst_sdlvideosink_signals[SIGNAL_FRAME_DISPLAYED] =
    g_signal_new ("frame_displayed", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST,
                   G_STRUCT_OFFSET (GstSDLVideoSinkClass, frame_displayed), NULL, NULL,
                   g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);
  gst_sdlvideosink_signals[SIGNAL_HAVE_SIZE] =
    g_signal_new ("have_size", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST,
                   G_STRUCT_OFFSET (GstSDLVideoSinkClass, have_size), NULL, NULL,
                   gst_marshal_VOID__INT_INT, G_TYPE_NONE, 2,
		   G_TYPE_UINT, G_TYPE_UINT);


  gstelement_class->change_state = gst_sdlvideosink_change_state;
}

static void
gst_sdlvideosink_newcaps (GstPad *pad, GstCaps *caps)
{
  GstSDLVideoSink *sdlvideosink;
  gulong print_format;

  sdlvideosink = GST_SDLVIDEOSINK (gst_pad_get_parent (pad));

  sdlvideosink->width =  gst_caps_get_int (caps, "width");
  sdlvideosink->height =  gst_caps_get_int (caps, "height");

  print_format = GULONG_FROM_LE (sdlvideosink->format);

  sdlvideosink->screen = SDL_SetVideoMode(sdlvideosink->width, sdlvideosink->height,
    0, SDL_SWSURFACE);
  if ( sdlvideosink->screen == NULL /* || sdlvideosink->buffer == NULL */) {
    GST_ERROR(0, "SDL: Couldn't set %dx%d: %s", sdlvideosink->width,
      sdlvideosink->height, SDL_GetError());
    printf("error\n");
    return;
  }
  else {
    GST_DEBUG(0, "SDL: Set %dx%d @ %d bpp\n", sdlvideosink->width,
      sdlvideosink->height, sdlvideosink->screen->format->BitsPerPixel);
  }

  sdlvideosink->yuv_overlay = SDL_CreateYUVOverlay(sdlvideosink->width, sdlvideosink->height,
    SDL_IYUV_OVERLAY, sdlvideosink->screen);
  if ( sdlvideosink->yuv_overlay == NULL ) {
    GST_ERROR(0, "SDL: Couldn't create SDL_yuv_overlay: %s\n", SDL_GetError());
    return;
  }

  sdlvideosink->rect.x = 0;
  sdlvideosink->rect.y = 0;
  sdlvideosink->rect.w = sdlvideosink->width;
  sdlvideosink->rect.h = sdlvideosink->height;
  SDL_DisplayYUVOverlay(sdlvideosink->yuv_overlay, &(sdlvideosink->rect));

  /* make stupid SDL *not* react on SIGINT */
  signal(SIGINT, SIG_DFL);

  GST_DEBUG (0, "sdlvideosink: setting %08lx (%4.4s)\n", sdlvideosink->format, (gchar*)&print_format);
  
  g_signal_emit (G_OBJECT (sdlvideosink), gst_sdlvideosink_signals[SIGNAL_HAVE_SIZE], 0,
		  sdlvideosink->width, sdlvideosink->height);
}

static void
gst_sdlvideosink_init (GstSDLVideoSink *sdlvideosink)
{
  sdlvideosink->sinkpad = gst_pad_new_from_template (
		  GST_PADTEMPLATE_GET (sink_template), "sink");
  gst_element_add_pad (GST_ELEMENT (sdlvideosink), sdlvideosink->sinkpad);
  gst_pad_set_chain_function (sdlvideosink->sinkpad, gst_sdlvideosink_chain);
  gst_pad_set_newcaps_function (sdlvideosink->sinkpad, gst_sdlvideosink_newcaps);

  sdlvideosink->clock = gst_clock_get_system();
  gst_clock_register(sdlvideosink->clock, GST_OBJECT(sdlvideosink));

  sdlvideosink->width = -1;
  sdlvideosink->height = -1;

  GST_FLAG_SET(sdlvideosink, GST_ELEMENT_THREAD_SUGGESTED);
}

static void
gst_sdlvideosink_chain (GstPad *pad, GstBuffer *buf)
{
  GstSDLVideoSink *sdlvideosink;
  GstClockTimeDiff jitter;

  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (buf != NULL);

  sdlvideosink = GST_SDLVIDEOSINK (gst_pad_get_parent (pad));

  if (!GST_BUFFER_FLAG_IS_SET(buf, GST_BUFFER_FLUSH)) {
    GST_DEBUG (0,"videosink: clock wait: %llu\n", GST_BUFFER_TIMESTAMP(buf));

    jitter = gst_clock_current_diff(sdlvideosink->clock, GST_BUFFER_TIMESTAMP (buf));

    if (jitter > 500000 || jitter < -500000)
    {
      GST_DEBUG (0, "jitter: %lld\n", jitter);
      gst_clock_set (sdlvideosink->clock, GST_BUFFER_TIMESTAMP (buf));
    }
    else {
      gst_clock_wait(sdlvideosink->clock, GST_BUFFER_TIMESTAMP(buf), GST_OBJECT(sdlvideosink));
    }
  }

  /* Lock SDL/yuv-overlay */
  if ( SDL_LockSurface(sdlvideosink->screen) < 0 ) {
    GST_ERROR(0, "SDL_LockSurface(sdlvideosink->screen) < 0");
    return;
  }
  if (SDL_LockYUVOverlay(sdlvideosink->yuv_overlay) < 0) {
    GST_ERROR(0, "SDL_LockYUVOverlay(sdlvideosink->yuv_overlay) < 0");
    return;
  }

  /* buf->yuv */
  sdlvideosink->yuv[0] = GST_BUFFER_DATA(buf);
  sdlvideosink->yuv[1] = sdlvideosink->yuv[0] + sdlvideosink->width*sdlvideosink->height;
  sdlvideosink->yuv[2] = sdlvideosink->yuv[1] + sdlvideosink->width*sdlvideosink->height/4;

  /* let's draw the data (*yuv[3]) on a SDL screen (*buffer) */
  sdlvideosink->yuv_overlay->pixels = sdlvideosink->yuv;

  /* Unlock SDL_yuv_overlay */
  SDL_UnlockYUVOverlay(sdlvideosink->yuv_overlay);
  SDL_UnlockSurface(sdlvideosink->screen);

  /* Show, baby, show! */
  SDL_DisplayYUVOverlay(sdlvideosink->yuv_overlay, &(sdlvideosink->rect));
  SDL_UpdateRect(sdlvideosink->screen, 0, 0, sdlvideosink->width, sdlvideosink->height);

  g_signal_emit(G_OBJECT(sdlvideosink),gst_sdlvideosink_signals[SIGNAL_FRAME_DISPLAYED],0);

  gst_buffer_unref(buf);
}


static void
gst_sdlvideosink_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  GstSDLVideoSink *sdlvideosink;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_SDLVIDEOSINK (object));

  sdlvideosink = GST_SDLVIDEOSINK (object);
}

static void
gst_sdlvideosink_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  GstSDLVideoSink *sdlvideosink;

  /* it's not null if we got it, but it might not be ours */
  sdlvideosink = GST_SDLVIDEOSINK(object);

  switch (prop_id) {
    case ARG_FRAMES_DISPLAYED: {
      g_value_set_int (value, sdlvideosink->frames_displayed);
      break;
    }
    case ARG_FRAME_TIME: {
      g_value_set_int (value, sdlvideosink->frame_time/1000000);
      break;
    }
    default: {
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
  }
}

static gboolean
gst_sdlvideosink_open (GstSDLVideoSink *sdlvideosink)
{
  g_return_val_if_fail (!GST_FLAG_IS_SET (sdlvideosink ,GST_SDLVIDEOSINK_OPEN), FALSE);

  /* Initialize the SDL library */
  if( SDL_Init(SDL_INIT_VIDEO | SDL_INIT_NOPARACHUTE) < 0 ) {
    GST_ERROR(0, "Couldn't initialize SDL: %s\n", SDL_GetError());
    return FALSE;
  }
  GST_FLAG_SET (sdlvideosink, GST_SDLVIDEOSINK_OPEN);

  return TRUE;
}

static void
gst_sdlvideosink_close (GstSDLVideoSink *sdlvideosink)
{
  g_return_if_fail (GST_FLAG_IS_SET (sdlvideosink ,GST_SDLVIDEOSINK_OPEN));

  SDL_FreeYUVOverlay(sdlvideosink->yuv_overlay);
  SDL_Quit();

  GST_FLAG_UNSET (sdlvideosink, GST_SDLVIDEOSINK_OPEN);
}

static GstElementStateReturn
gst_sdlvideosink_change_state (GstElement *element)
{
  g_return_val_if_fail (GST_IS_SDLVIDEOSINK (element), GST_STATE_FAILURE);

  if (GST_STATE_PENDING (element) == GST_STATE_NULL) {
    if (GST_FLAG_IS_SET (element, GST_SDLVIDEOSINK_OPEN))
      gst_sdlvideosink_close (GST_SDLVIDEOSINK (element));
  } else {
    if (!GST_FLAG_IS_SET (element, GST_SDLVIDEOSINK_OPEN)) {
      if (!gst_sdlvideosink_open (GST_SDLVIDEOSINK (element)))
        return GST_STATE_FAILURE;
    }
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);

  return GST_STATE_SUCCESS;
}

static gboolean
plugin_init (GModule *module, GstPlugin *plugin)
{
  GstElementFactory *factory;

  /* create an elementfactory for the sdlvideosink element */
  factory = gst_elementfactory_new("sdlvideosink",GST_TYPE_SDLVIDEOSINK,
                                   &gst_sdlvideosink_details);
  g_return_val_if_fail(factory != NULL, FALSE);

  gst_elementfactory_add_padtemplate (factory, 
		  GST_PADTEMPLATE_GET (sink_template));

  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));

  return TRUE;
}

GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "sdlvideosink",
  plugin_init
};
