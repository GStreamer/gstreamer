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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <config.h>
#include <signal.h>
#include <string.h>
#include <sys/time.h>
#include <stdlib.h>

#include "sdlvideosink.h"

/* elementfactory information */
static GstElementDetails gst_sdlvideosink_details = {
  "Video sink",
  "Sink/Video",
  "LGPL",
  "An SDL-based videosink",
  VERSION,
  "Ronald Bultje <rbultje@ronald.bitfreak.net>",
  "(C) 2001",
};

static void                  gst_sdlvideosink_class_init   (GstSDLVideoSinkClass *klass);
static void                  gst_sdlvideosink_init         (GstSDLVideoSink      *sdlvideosink);

static gboolean              gst_sdlvideosink_create       (GstSDLVideoSink      *sdlvideosink,
                                                            gboolean              showlogo);
static GstPadLinkReturn   gst_sdlvideosink_sinkconnect  (GstPad               *pad,
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

GType
gst_sdlvideosink_get_type (void)
{
  static GType sdlvideosink_type = 0;

  if (!sdlvideosink_type) {
    static const GTypeInfo sdlvideosink_info = {
      sizeof (GstSDLVideoSinkClass),      NULL,
      NULL,
      (GClassInitFunc) gst_sdlvideosink_class_init,
      NULL,
      NULL,
      sizeof (GstSDLVideoSink),
      0,
      (GInstanceInitFunc) gst_sdlvideosink_init,
    };
    sdlvideosink_type = g_type_register_static(GST_TYPE_VIDEOSINK,
                                               "GstSDLVideoSink",
                                               &sdlvideosink_info, 0);
  }
  return sdlvideosink_type;
}


static void
gst_sdlvideosink_class_init (GstSDLVideoSinkClass *klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstVideoSinkClass *gstvs_class;

  gobject_class = (GObjectClass*) klass;
  gstelement_class = (GstElementClass*) klass;
  gstvs_class = (GstVideoSinkClass*) klass;
  
  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  gobject_class->set_property = gst_sdlvideosink_set_property;
  gobject_class->get_property = gst_sdlvideosink_get_property;

  gstelement_class->change_state = gst_sdlvideosink_change_state;
  
  /*gstvs_class->set_video_out = gst_sdlvideosink_set_video_out;
  gstvs_class->push_ui_event = gst_sdlvideosink_push_ui_event;
  gstvs_class->set_geometry = gst_sdlvideosink_set_geometry;*/
}

static void
gst_sdlvideosink_init (GstSDLVideoSink *sdlvideosink)
{
  GST_VIDEOSINK_PAD (sdlvideosink) = gst_pad_new_from_template (sink_template,
                                                               "sink");
  gst_element_add_pad (GST_ELEMENT (sdlvideosink),
                       GST_VIDEOSINK_PAD (sdlvideosink));

  gst_pad_set_chain_function (GST_VIDEOSINK_PAD (sdlvideosink),
                              gst_sdlvideosink_chain);
  gst_pad_set_link_function (GST_VIDEOSINK_PAD (sdlvideosink),
                             gst_sdlvideosink_sinkconnect);

  sdlvideosink->image_width = -1;
  sdlvideosink->image_height = -1;

  sdlvideosink->yuv_overlay = NULL;
  sdlvideosink->screen = NULL;

  sdlvideosink->window_id = -1; /* means "don't use" */

  sdlvideosink->capslist = capslist;

  GST_FLAG_SET(sdlvideosink, GST_ELEMENT_THREAD_SUGGESTED);
  GST_FLAG_SET(sdlvideosink, GST_ELEMENT_EVENT_AWARE);
}


static gulong
gst_sdlvideosink_get_sdl_from_fourcc (GstSDLVideoSink *sdlvideosink,
                                      gulong           code)
{
  switch (code)
  {
    case GST_MAKE_FOURCC('I','4','2','0'):
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
      gst_element_gerror(GST_ELEMENT(sdlvideosink), GST_ERROR_UNKNOWN,
        g_strdup ("unconverted error, file a bug"),
        g_strdup_printf("Unsupported format %08lx (" GST_FOURCC_FORMAT ")",
        code, GST_FOURCC_ARGS(code)));
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
      gst_element_gerror(GST_ELEMENT(sdlvideosink), GST_ERROR_UNKNOWN,
        g_strdup ("unconverted error, file a bug"),
        g_strdup_printf("SDL: couldn\'t lock the SDL video window: %s", SDL_GetError()));
      return FALSE;
    }
  }
  if (SDL_LockYUVOverlay(sdlvideosink->yuv_overlay) < 0)
  {
    gst_element_gerror(GST_ELEMENT(sdlvideosink), GST_ERROR_UNKNOWN,
      g_strdup ("unconverted error, file a bug"),
      g_strdup_printf("SDL: couldn\'t lock the SDL YUV overlay: %s", SDL_GetError()));
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
  guint8 *sbuffer;
  gint i;

  if (GST_VIDEOSINK_HEIGHT (sdlvideosink) <= 0)
    GST_VIDEOSINK_HEIGHT (sdlvideosink) = sdlvideosink->image_height;
  if (GST_VIDEOSINK_WIDTH (sdlvideosink) <= 0)
    GST_VIDEOSINK_WIDTH (sdlvideosink) = sdlvideosink->image_width;

  /* create a SDL window of the size requested by the user */
  sdlvideosink->screen = SDL_SetVideoMode(GST_VIDEOSINK_WIDTH (sdlvideosink),
    GST_VIDEOSINK_HEIGHT (sdlvideosink), 0, SDL_SWSURFACE | SDL_RESIZABLE);
  if (showlogo) /* workaround for SDL bug - do it twice */
    sdlvideosink->screen = SDL_SetVideoMode(GST_VIDEOSINK_WIDTH (sdlvideosink),
      GST_VIDEOSINK_HEIGHT (sdlvideosink), 0, SDL_SWSURFACE | SDL_RESIZABLE);
  if ( sdlvideosink->screen == NULL)
  {
    gst_element_gerror(GST_ELEMENT(sdlvideosink), GST_ERROR_UNKNOWN,
      g_strdup ("unconverted error, file a bug"),
      g_strdup_printf("SDL: Couldn't set %dx%d: %s", GST_VIDEOSINK_WIDTH (sdlvideosink),
      GST_VIDEOSINK_HEIGHT (sdlvideosink), SDL_GetError()));
    return FALSE;
  }

  /* clean possible old YUV overlays (...) and create a new one */
  if (sdlvideosink->yuv_overlay)
    SDL_FreeYUVOverlay(sdlvideosink->yuv_overlay);
  sdlvideosink->yuv_overlay = SDL_CreateYUVOverlay(GST_VIDEOSINK_WIDTH (sdlvideosink),
    GST_VIDEOSINK_WIDTH (sdlvideosink), sdlvideosink->format, sdlvideosink->screen);
  if ( sdlvideosink->yuv_overlay == NULL )
  {
    gst_element_gerror(GST_ELEMENT(sdlvideosink), GST_ERROR_UNKNOWN,
      g_strdup ("unconverted error, file a bug"),
      g_strdup_printf("SDL: Couldn't create SDL_yuv_overlay (%dx%d \'" GST_FOURCC_FORMAT "\'): %s",
      GST_VIDEOSINK_WIDTH (sdlvideosink), GST_VIDEOSINK_HEIGHT (sdlvideosink),
      GST_FOURCC_ARGS(sdlvideosink->format), SDL_GetError()));
    return FALSE;
  }
  else
  {
    g_message("Using a %dx%d %dbpp SDL screen with a %dx%d \'" GST_FOURCC_FORMAT "\' YUV overlay\n",
      GST_VIDEOSINK_WIDTH (sdlvideosink), GST_VIDEOSINK_HEIGHT (sdlvideosink),
      sdlvideosink->screen->format->BitsPerPixel,
      sdlvideosink->image_width, sdlvideosink->image_height,
      GST_FOURCC_ARGS(sdlvideosink->format));
  }

  sdlvideosink->rect.x = 0;
  sdlvideosink->rect.y = 0;
  sdlvideosink->rect.w = GST_VIDEOSINK_WIDTH (sdlvideosink);
  sdlvideosink->rect.h = GST_VIDEOSINK_HEIGHT (sdlvideosink);

  /* make stupid SDL *not* react on SIGINT */
  signal(SIGINT, SIG_DFL);

  if (showlogo)
  {
    SDL_Event event;
    while (SDL_PollEvent(&event));

    if (!gst_sdlvideosink_lock(sdlvideosink)) {
      g_message ("could not lock\n");
      return FALSE;
    }

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

  GST_DEBUG ("sdlvideosink: setting %08lx (" GST_FOURCC_FORMAT ")", sdlvideosink->format, GST_FOURCC_ARGS(sdlvideosink->format));

  gst_video_sink_got_video_size (GST_VIDEOSINK (sdlvideosink),
                                 GST_VIDEOSINK_WIDTH (sdlvideosink),
                                 GST_VIDEOSINK_HEIGHT (sdlvideosink));
  return TRUE;
}

static GstPadLinkReturn
gst_sdlvideosink_sinkconnect (GstPad  *pad,
                              GstCaps *vscapslist)
{
  GstSDLVideoSink *sdlvideosink;
  GstCaps *caps;

  sdlvideosink = GST_SDLVIDEOSINK (gst_pad_get_parent (pad));

  /* we are not going to act on variable caps */
  if (!GST_CAPS_IS_FIXED (vscapslist))
    return GST_PAD_LINK_DELAYED;

  for (caps = vscapslist; caps != NULL; caps = vscapslist = vscapslist->next)
  {
    guint32 format;

    gst_caps_get_fourcc_int(caps, "format", &format);

    /* check whether it's in any way compatible */
    switch (format)
    {
      case GST_MAKE_FOURCC('I','4','2','0'):
      case GST_MAKE_FOURCC('Y','V','1','2'):
      case GST_MAKE_FOURCC('Y','U','Y','2'):
      case GST_MAKE_FOURCC('Y','V','Y','U'):
      case GST_MAKE_FOURCC('U','Y','V','Y'):
        sdlvideosink->format = gst_sdlvideosink_get_sdl_from_fourcc (sdlvideosink, format);
        gst_caps_get_int(caps, "width", &sdlvideosink->image_width);
        gst_caps_get_int(caps, "height", &sdlvideosink->image_height);

        /* try it out */
        if (!gst_sdlvideosink_create(sdlvideosink, TRUE))
          return GST_PAD_LINK_REFUSED;

        return GST_PAD_LINK_OK;
    }
  }

  /* if we got here - it's not good */
  return GST_PAD_LINK_REFUSED;
}


static void
gst_sdlvideosink_chain (GstPad *pad, GstBuffer *buf)
{
  GstSDLVideoSink *sdlvideosink;
  SDL_Event sdl_event;

  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (buf != NULL);

  sdlvideosink = GST_SDLVIDEOSINK (gst_pad_get_parent (pad));


  while (SDL_PollEvent(&sdl_event))
  {
    switch(sdl_event.type)
    {
      case SDL_VIDEORESIZE:
        /* create a SDL window of the size requested by the user */
        GST_VIDEOSINK_WIDTH (sdlvideosink) = sdl_event.resize.w;
        GST_VIDEOSINK_HEIGHT (sdlvideosink) = sdl_event.resize.h;
        gst_sdlvideosink_create(sdlvideosink, FALSE);
        break;
    }
  }

  if (GST_IS_EVENT (buf)) {
    GstEvent *event = GST_EVENT (buf);
    gint64 offset;

    switch (GST_EVENT_TYPE (event)) {
      case GST_EVENT_DISCONTINUOUS:
	offset = GST_EVENT_DISCONT_OFFSET (event, 0).value;
	g_print ("sdl discont %" G_GINT64_FORMAT "\n", offset);
	/*gst_clock_handle_discont (sdlvideosink->clock, (guint64) GST_EVENT_DISCONT_OFFSET (event, 0).value);*/
	break;
      default:
	gst_pad_event_default (pad, event);
	return;
    }
    gst_event_unref (event);
    return;
  }

  GST_DEBUG ("videosink: clock wait: %" G_GUINT64_FORMAT, GST_BUFFER_TIMESTAMP(buf));
  if (GST_VIDEOSINK_CLOCK (sdlvideosink)) {
    GstClockID id = gst_clock_new_single_shot_id (
                      GST_VIDEOSINK_CLOCK (sdlvideosink),
                      GST_BUFFER_TIMESTAMP (buf));

    gst_element_clock_wait (GST_ELEMENT (sdlvideosink), id, NULL);
    gst_clock_id_free (id);
  }

  if (!gst_sdlvideosink_lock(sdlvideosink)) {
    g_message ("could not lock\n");
    return;
  }

  /* buf->yuv */
  if (sdlvideosink->format == GST_MAKE_FOURCC('I','4','2','0') ||
      sdlvideosink->format == GST_MAKE_FOURCC('Y','V','1','2'))
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

  gst_video_sink_frame_displayed (GST_VIDEOSINK (sdlvideosink));

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
        gst_element_gerror(element, GST_ERROR_UNKNOWN,
          g_strdup ("unconverted error, file a bug"),
          g_strdup_printf("Couldn't initialize SDL: %s", SDL_GetError()));
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
                       GST_MAKE_FOURCC('Y','V','1','2'),
                       GST_MAKE_FOURCC('Y','U','Y','2'),
                       GST_MAKE_FOURCC('Y','V','Y','U'),
                       GST_MAKE_FOURCC('U','Y','V','Y')
                     };

  /* Loading the library containing GstVideoSink, our parent object */
  if (!gst_library_load ("gstvideo"))
    return FALSE;
                     
  /* create an elementfactory for the sdlvideosink element */
  factory = gst_element_factory_new("sdlvideosink",GST_TYPE_SDLVIDEOSINK,
                                   &gst_sdlvideosink_details);
  g_return_val_if_fail(factory != NULL, FALSE);

  /* make a list of all available caps */
  for (i=0;i<5;i++)
  {
    caps = gst_caps_new ("sdlvideosink_caps",
                         "video/x-raw-yuv",
                         gst_props_new (
                            "format", GST_PROPS_FOURCC(format[i]),
                            "width",  GST_PROPS_INT_RANGE (0, G_MAXINT),
                            "height", GST_PROPS_INT_RANGE (0, G_MAXINT),
			    "framerate", GST_PROPS_FLOAT_RANGE (0, G_MAXFLOAT),
                            NULL       )
                        );
    capslist = gst_caps_append(capslist, caps);
  }

  sink_template = gst_pad_template_new (
		  "sink",
                  GST_PAD_SINK,
  		  GST_PAD_ALWAYS,
		  capslist, NULL);

  gst_element_factory_add_pad_template (factory, sink_template);

  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));

  return TRUE;
}


GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "sdlvideosink",
  plugin_init
};
