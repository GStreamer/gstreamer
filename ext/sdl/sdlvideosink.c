/* GStreamer SDL plugin
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

/* let's not forget to mention that all this was based on aasink ;-) */

#include <config.h>
#include <signal.h>
#include <string.h>
#include <sys/time.h>
#include <stdlib.h>

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
  ARG_XID,
  ARG_FRAMES_DISPLAYED,
  ARG_FRAME_TIME,
};


static void                  gst_sdlvideosink_class_init   (GstSDLVideoSinkClass *klass);
static void                  gst_sdlvideosink_init         (GstSDLVideoSink      *sdlvideosink);

static gboolean              gst_sdlvideosink_create       (GstSDLVideoSink      *sdlvideosink,
                                                            gboolean              showlogo);
static GstPadConnectReturn   gst_sdlvideosink_sinkconnect  (GstPad               *pad,
                                                            GstCaps              *caps);
static void                  gst_sdlvideosink_chain        (GstPad               *pad,
                                                            GstBuffer            *buf);

static void                  gst_sdlvideosink_set_property (GObject              *object,
                                                            guint                 prop_id,
                                                            const GValue         *value,
                                                            GParamSpec           *pspec);
static void                  gst_sdlvideosink_get_property (GObject              *object,
                                                            guint                 prop_id,
                                                            GValue               *value,
                                                            GParamSpec           *pspec);
static GstElementStateReturn gst_sdlvideosink_change_state (GstElement           *element);


static GstCaps *capslist = NULL;
static GstPadTemplate *sink_template;

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
    sdlvideosink_type = g_type_register_static(GST_TYPE_ELEMENT,
      "GstSDLVideoSink", &sdlvideosink_info, 0);
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
    g_param_spec_int("width","Width","Width of the video window",
                     G_MININT,G_MAXINT,0,G_PARAM_READWRITE));
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_HEIGHT,
    g_param_spec_int("height","Height","Height of the video window",
                     G_MININT,G_MAXINT,0,G_PARAM_READWRITE));

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_XID,
    g_param_spec_int ("xid", "Xid", "The Xid of the window",
                      G_MININT, G_MAXINT, 0, G_PARAM_WRITABLE));

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_FRAMES_DISPLAYED,
    g_param_spec_int ("frames_displayed", "Frames Displayed", "The number of frames displayed so far",
                      G_MININT,G_MAXINT, 0, G_PARAM_READABLE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_FRAME_TIME,
    g_param_spec_int ("frame_time", "Frame time", "The interval between frames",
                      G_MININT, G_MAXINT, 0, G_PARAM_READABLE));

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
gst_sdlvideosink_set_clock (GstElement *element, GstClock *clock)
{
  GstSDLVideoSink *sdlvideosink;

  sdlvideosink = GST_SDLVIDEOSINK (element);
  
  sdlvideosink->clock = clock;
}

static void
gst_sdlvideosink_init (GstSDLVideoSink *sdlvideosink)
{
  sdlvideosink->sinkpad = gst_pad_new_from_template (sink_template, "sink");
  gst_element_add_pad (GST_ELEMENT (sdlvideosink), sdlvideosink->sinkpad);

  gst_pad_set_chain_function (sdlvideosink->sinkpad, gst_sdlvideosink_chain);
  gst_pad_set_connect_function (sdlvideosink->sinkpad, gst_sdlvideosink_sinkconnect);

  sdlvideosink->window_width = -1;
  sdlvideosink->window_height = -1;

  sdlvideosink->image_width = -1;
  sdlvideosink->image_height = -1;

  sdlvideosink->yuv_overlay = NULL;
  sdlvideosink->screen = NULL;

  sdlvideosink->window_id = -1; /* means "don't use" */

  sdlvideosink->capslist = capslist;

  sdlvideosink->clock = NULL;
  GST_ELEMENT (sdlvideosink)->setclockfunc    = gst_sdlvideosink_set_clock;

  GST_FLAG_SET(sdlvideosink, GST_ELEMENT_THREAD_SUGGESTED);
}


static gulong
gst_sdlvideosink_get_sdl_from_fourcc (GstSDLVideoSink *sdlvideosink,
                                      gulong           code)
{
  switch (code)
  {
    case GST_MAKE_FOURCC('I','4','2','0'):
    case GST_MAKE_FOURCC('I','Y','U','V'):
      return SDL_IYUV_OVERLAY;
    case GST_MAKE_FOURCC('Y','V','1','2'):
      return SDL_YV12_OVERLAY;
    case GST_MAKE_FOURCC('Y','U','Y','2'):
      return SDL_YUY2_OVERLAY;
    case GST_MAKE_FOURCC('U','Y','V','Y'):
      return SDL_UYVY_OVERLAY;
    case GST_MAKE_FOURCC('Y','V','Y','U'):
      return SDL_YVYU_OVERLAY;
    default: {
      gulong print_format;
      print_format = GULONG_FROM_LE(code);
      gst_element_error(GST_ELEMENT(sdlvideosink),
        "Unsupported format %08lx (%4.4s)",
        print_format, (char*)&print_format);
      return 0;
    }
  }
}


static gboolean
gst_sdlvideosink_lock (GstSDLVideoSink *sdlvideosink)
{
  /* Lock SDL/yuv-overlay */
  if (SDL_MUSTLOCK(sdlvideosink->screen))
  {
    if (SDL_LockSurface(sdlvideosink->screen) < 0)
    {
      gst_element_error(GST_ELEMENT(sdlvideosink),
        "SDL: couldn\'t lock the SDL video window: %s", SDL_GetError());
      return FALSE;
    }
  }
  if (SDL_LockYUVOverlay(sdlvideosink->yuv_overlay) < 0)
  {
    gst_element_error(GST_ELEMENT(sdlvideosink),
      "SDL: couldn\'t lock the SDL YUV overlay: %s", SDL_GetError());
    return FALSE;
  }

  return TRUE;
}


static void
gst_sdlvideosink_unlock (GstSDLVideoSink *sdlvideosink)
{
  /* Unlock SDL_yuv_overlay */
  SDL_UnlockYUVOverlay(sdlvideosink->yuv_overlay);
  if (SDL_MUSTLOCK(sdlvideosink->screen))
    SDL_UnlockSurface(sdlvideosink->screen);
}


static gboolean
gst_sdlvideosink_create (GstSDLVideoSink *sdlvideosink, gboolean showlogo)
{
  gulong print_format;
  guint8 *sbuffer;
  gint i;

  if (sdlvideosink->window_height <= 0)
    sdlvideosink->window_height = sdlvideosink->image_height;
  if (sdlvideosink->window_width <= 0)
    sdlvideosink->window_width = sdlvideosink->image_width;

  print_format = GULONG_FROM_LE (sdlvideosink->format);

  /* create a SDL window of the size requested by the user */
  sdlvideosink->screen = SDL_SetVideoMode(sdlvideosink->window_width,
    sdlvideosink->window_height, 0, SDL_SWSURFACE | SDL_RESIZABLE);
  if (showlogo) /* workaround for SDL bug - do it twice */
    sdlvideosink->screen = SDL_SetVideoMode(sdlvideosink->window_width,
      sdlvideosink->window_height, 0, SDL_SWSURFACE | SDL_RESIZABLE);
  if ( sdlvideosink->screen == NULL)
  {
    gst_element_error(GST_ELEMENT(sdlvideosink),
      "SDL: Couldn't set %dx%d: %s", sdlvideosink->window_width,
      sdlvideosink->window_height, SDL_GetError());
    return FALSE;
  }

  /* clean possible old YUV overlays (...) and create a new one */
  if (sdlvideosink->yuv_overlay)
    SDL_FreeYUVOverlay(sdlvideosink->yuv_overlay);
  sdlvideosink->yuv_overlay = SDL_CreateYUVOverlay(sdlvideosink->image_width,
    sdlvideosink->image_height, sdlvideosink->format, sdlvideosink->screen);
  if ( sdlvideosink->yuv_overlay == NULL )
  {
    gst_element_error(GST_ELEMENT(sdlvideosink),
      "SDL: Couldn't create SDL_yuv_overlay (%dx%d \'%4.4s\'): %s",
      sdlvideosink->image_width, sdlvideosink->image_height,
      (char*)&print_format, SDL_GetError());
    return FALSE;
  }
  else
  {
    g_message("Using a %dx%d %dbpp SDL screen with a %dx%d \'%4.4s\' YUV overlay\n",
      sdlvideosink->window_width, sdlvideosink->window_height,
      sdlvideosink->screen->format->BitsPerPixel,
      sdlvideosink->image_width, sdlvideosink->image_height,
      (gchar*)&print_format);
  }

  sdlvideosink->rect.x = 0;
  sdlvideosink->rect.y = 0;
  sdlvideosink->rect.w = sdlvideosink->window_width;
  sdlvideosink->rect.h = sdlvideosink->window_height;

  /* make stupid SDL *not* react on SIGINT */
  signal(SIGINT, SIG_DFL);

  if (showlogo)
  {
    SDL_Event event;
    while (SDL_PollEvent(&event));

    if (!gst_sdlvideosink_lock(sdlvideosink))
      return FALSE;

    /* Draw bands of color on the raw surface, as run indicator for debugging */
    sbuffer = (char *)sdlvideosink->screen->pixels;
    for (i=0; i<sdlvideosink->screen->h; i++) 
    {
      memset(sbuffer, (i*255)/sdlvideosink->screen->h,
        sdlvideosink->screen->w * sdlvideosink->screen->format->BytesPerPixel);
      sbuffer += sdlvideosink->screen->pitch;
    }

    /* Set the windows title */
    SDL_WM_SetCaption("GStreamer SDL Video Playback", "0000000"); 

    gst_sdlvideosink_unlock(sdlvideosink);

    SDL_UpdateRect(sdlvideosink->screen, 0, 0, sdlvideosink->rect.w, sdlvideosink->rect.h);
  }
  else
    SDL_DisplayYUVOverlay(sdlvideosink->yuv_overlay, &(sdlvideosink->rect));

  GST_DEBUG (0, "sdlvideosink: setting %08lx (%4.4s)", sdlvideosink->format, (gchar*)&print_format);
  
  /* TODO: is this the width of the input image stream or of the widget? */
  g_signal_emit (G_OBJECT (sdlvideosink), gst_sdlvideosink_signals[SIGNAL_HAVE_SIZE], 0,
		  sdlvideosink->window_width, sdlvideosink->window_height);

  return TRUE;
}

static GstPadConnectReturn
gst_sdlvideosink_sinkconnect (GstPad  *pad,
                              GstCaps *vscapslist)
{
  GstSDLVideoSink *sdlvideosink;
  GstCaps *caps;

  sdlvideosink = GST_SDLVIDEOSINK (gst_pad_get_parent (pad));

  /* we are not going to act on variable caps */
  if (!GST_CAPS_IS_FIXED (vscapslist))
    return GST_PAD_CONNECT_DELAYED;

  for (caps = vscapslist; caps != NULL; caps = vscapslist = vscapslist->next)
  {
    /* check whether it's in any way compatible */
    switch (gst_caps_get_fourcc_int(caps, "format"))
    {
      case GST_MAKE_FOURCC('I','4','2','0'):
      case GST_MAKE_FOURCC('I','Y','U','V'):
      case GST_MAKE_FOURCC('Y','V','1','2'):
      case GST_MAKE_FOURCC('Y','U','Y','2'):
      case GST_MAKE_FOURCC('Y','V','Y','U'):
      case GST_MAKE_FOURCC('U','Y','V','Y'):
        sdlvideosink->format = gst_sdlvideosink_get_sdl_from_fourcc(sdlvideosink,
                                     gst_caps_get_fourcc_int(caps, "format"));
        sdlvideosink->image_width = gst_caps_get_int(caps, "width");
        sdlvideosink->image_height = gst_caps_get_int(caps, "height");

        /* try it out */
        if (!gst_sdlvideosink_create(sdlvideosink, TRUE))
          return GST_PAD_CONNECT_REFUSED;

        return GST_PAD_CONNECT_OK;
    }
  }

  /* if we got here - it's not good */
  return GST_PAD_CONNECT_REFUSED;
}


static void
gst_sdlvideosink_chain (GstPad *pad, GstBuffer *buf)
{
  GstSDLVideoSink *sdlvideosink;
  SDL_Event event;

  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (buf != NULL);

  sdlvideosink = GST_SDLVIDEOSINK (gst_pad_get_parent (pad));

  GST_DEBUG (0,"videosink: clock wait: %llu", GST_BUFFER_TIMESTAMP(buf));

  while (SDL_PollEvent(&event))
  {
    switch(event.type)
    {
      case SDL_VIDEORESIZE:
        /* create a SDL window of the size requested by the user */
        sdlvideosink->window_width = event.resize.w;
        sdlvideosink->window_height = event.resize.h;
        gst_sdlvideosink_create(sdlvideosink, FALSE);
        break;
    }
  }

  if (sdlvideosink->clock) {
    gst_element_clock_wait (GST_ELEMENT (sdlvideosink),
		  sdlvideosink->clock, GST_BUFFER_TIMESTAMP (buf));
  }

  if (!gst_sdlvideosink_lock(sdlvideosink))
    return;

  /* buf->yuv */
  if (sdlvideosink->format == GST_MAKE_FOURCC('I','4','2','0') ||
      sdlvideosink->format == GST_MAKE_FOURCC('Y','V','1','2') ||
      sdlvideosink->format == GST_MAKE_FOURCC('I','Y','U','V'))
  {
    sdlvideosink->yuv[0] = GST_BUFFER_DATA(buf);
    sdlvideosink->yuv[1] = sdlvideosink->yuv[0] + sdlvideosink->image_width*sdlvideosink->image_height;
    sdlvideosink->yuv[2] = sdlvideosink->yuv[1] + sdlvideosink->image_width*sdlvideosink->image_height/4;
  }
  else
  {
    sdlvideosink->yuv[0] = GST_BUFFER_DATA(buf);
  }

  /* let's draw the data (*yuv[3]) on a SDL screen (*buffer) */
  sdlvideosink->yuv_overlay->pixels = sdlvideosink->yuv;

  gst_sdlvideosink_unlock(sdlvideosink);

  /* Show, baby, show! */
  SDL_DisplayYUVOverlay(sdlvideosink->yuv_overlay, &(sdlvideosink->rect));
  SDL_UpdateRect(sdlvideosink->screen,
    sdlvideosink->rect.x, sdlvideosink->rect.y,
    sdlvideosink->rect.w, sdlvideosink->rect.h);

  g_signal_emit(G_OBJECT(sdlvideosink),gst_sdlvideosink_signals[SIGNAL_FRAME_DISPLAYED],0);

  gst_buffer_unref(buf);
}


static void
gst_sdlvideosink_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  GstSDLVideoSink *sdlvideosink;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_SDLVIDEOSINK (object));
  sdlvideosink = GST_SDLVIDEOSINK(object);

  switch (prop_id)
  {
    case ARG_WIDTH:
      sdlvideosink->window_width = g_value_get_int(value);
      if (sdlvideosink->yuv_overlay)
        gst_sdlvideosink_create(sdlvideosink, FALSE);
      break;
    case ARG_HEIGHT:
      sdlvideosink->window_height = g_value_get_int(value);
      if (sdlvideosink->yuv_overlay)
        gst_sdlvideosink_create(sdlvideosink, FALSE);
      break;
    case ARG_XID:
      sdlvideosink->window_id = g_value_get_int(value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


static void
gst_sdlvideosink_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  GstSDLVideoSink *sdlvideosink;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_SDLVIDEOSINK (object));
  sdlvideosink = GST_SDLVIDEOSINK(object);

  switch (prop_id) {
    case ARG_WIDTH:
      g_value_set_int(value, sdlvideosink->window_width);
      break;
    case ARG_HEIGHT:
      g_value_set_int(value, sdlvideosink->window_height);
      break;
    case ARG_FRAMES_DISPLAYED:
      g_value_set_int (value, sdlvideosink->frames_displayed);
      break;
    case ARG_FRAME_TIME:
      g_value_set_int (value, sdlvideosink->frame_time/1000000);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


static GstElementStateReturn
gst_sdlvideosink_change_state (GstElement *element)
{
  GstSDLVideoSink *sdlvideosink;
  g_return_val_if_fail (GST_IS_SDLVIDEOSINK (element), GST_STATE_FAILURE);
  sdlvideosink = GST_SDLVIDEOSINK(element);

  switch (GST_STATE_TRANSITION (element))
  {
    case GST_STATE_NULL_TO_READY:
      /* Initialize the SDL library */
      if (sdlvideosink->window_id < 0)
        unsetenv("SDL_WINDOWID");
      else
      {
        char SDL_hack[32];
        sprintf(SDL_hack, "%d", sdlvideosink->window_id);
        setenv("SDL_WINDOWID", SDL_hack, 1);
      }
      if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_NOPARACHUTE) < 0 )
      {
        gst_element_error(element, "Couldn't initialize SDL: %s", SDL_GetError());
        return GST_STATE_FAILURE;
      }
      GST_FLAG_SET (sdlvideosink, GST_SDLVIDEOSINK_OPEN);
      break;
    case GST_STATE_READY_TO_NULL:
      /*if (sdlvideosink->yuv_overlay)
        SDL_FreeYUVOverlay(sdlvideosink->yuv_overlay);
      sdlvideosink->yuv_overlay = NULL;*/
      SDL_Quit();
      GST_FLAG_UNSET (sdlvideosink, GST_SDLVIDEOSINK_OPEN);
      break;
    default: /* do nothing */
      break;
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);

  return GST_STATE_SUCCESS;
}


static gboolean
plugin_init (GModule *module, GstPlugin *plugin)
{
  GstElementFactory *factory;
  GstCaps *caps;
  gint i;
  gulong format[6] = { GST_MAKE_FOURCC('I','4','2','0'),
                       GST_MAKE_FOURCC('I','Y','U','V'),
                       GST_MAKE_FOURCC('Y','V','1','2'),
                       GST_MAKE_FOURCC('Y','U','Y','2'),
                       GST_MAKE_FOURCC('Y','V','Y','U'),
                       GST_MAKE_FOURCC('U','Y','V','Y')
                     };

  /* create an elementfactory for the sdlvideosink element */
  factory = gst_elementfactory_new("sdlvideosink",GST_TYPE_SDLVIDEOSINK,
                                   &gst_sdlvideosink_details);
  g_return_val_if_fail(factory != NULL, FALSE);

  /* make a list of all available caps */
  for (i=0;i<6;i++)
  {
    caps = gst_caps_new ("sdlvideosink_caps",
                         "video/raw",
                         gst_props_new (
                            "format", GST_PROPS_FOURCC(format[i]),
                            "width",  GST_PROPS_INT_RANGE (0, G_MAXINT),
                            "height", GST_PROPS_INT_RANGE (0, G_MAXINT),
                            NULL       )
                        );
    capslist = gst_caps_append(capslist, caps);
  }

  sink_template = gst_padtemplate_new (
		  "sink",
                  GST_PAD_SINK,
  		  GST_PAD_ALWAYS,
		  capslist, NULL);

  gst_elementfactory_add_padtemplate (factory, sink_template);

  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));

  return TRUE;
}


GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "sdlvideosink",
  plugin_init
};
