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

#include <signal.h>
#include <string.h>
#include <sys/time.h>
#include <stdlib.h>

#include <gst/interfaces/xoverlay.h>

#include "sdlvideosink.h"

GST_DEBUG_CATEGORY_STATIC (sdlvideosink_debug);
#define GST_CAT_DEFAULT sdlvideosink_debug

/* These macros are adapted from videotestsrc.c 
 *  and/or gst-plugins/gst/games/gstvideoimage.c */
#define I420_Y_ROWSTRIDE(width) (GST_ROUND_UP_4(width))
#define I420_U_ROWSTRIDE(width) (GST_ROUND_UP_8(width)/2)
#define I420_V_ROWSTRIDE(width) ((GST_ROUND_UP_8(I420_Y_ROWSTRIDE(width)))/2)

#define I420_Y_OFFSET(w,h) (0)
#define I420_U_OFFSET(w,h) (I420_Y_OFFSET(w,h)+(I420_Y_ROWSTRIDE(w)*GST_ROUND_UP_2(h)))
#define I420_V_OFFSET(w,h) (I420_U_OFFSET(w,h)+(I420_U_ROWSTRIDE(w)*GST_ROUND_UP_2(h)/2))

#define I420_SIZE(w,h)     (I420_V_OFFSET(w,h)+(I420_V_ROWSTRIDE(w)*GST_ROUND_UP_2(h)/2))

/* elementfactory information */
static GstElementDetails gst_sdlvideosink_details = {
  "Video sink",
  "Sink/Video",
  "An SDL-based videosink",
  "Ronald Bultje <rbultje@ronald.bitfreak.net>",
};


enum
{
  PROP_0,
  PROP_FULLSCREEN
};

static void gst_sdlvideosink_base_init (gpointer g_class);
static void gst_sdlvideosink_class_init (GstSDLVideoSinkClass * klass);
static void gst_sdlvideosink_init (GstSDLVideoSink * sdl);

static void gst_sdlvideosink_interface_init (GstImplementsInterfaceClass *
    klass);
static gboolean gst_sdlvideosink_supported (GstImplementsInterface * iface,
    GType type);

static void gst_sdlvideosink_xoverlay_init (GstXOverlayClass * klass);
static void gst_sdlvideosink_xoverlay_set_xwindow_id
    (GstXOverlay * overlay, unsigned long parent);

static gboolean gst_sdlvideosink_lock (GstSDLVideoSink * sdl);
static void gst_sdlvideosink_unlock (GstSDLVideoSink * sdl);

static gboolean gst_sdlvideosink_initsdl (GstSDLVideoSink * sdl);
static void gst_sdlvideosink_deinitsdl (GstSDLVideoSink * sdl);

static gboolean gst_sdlvideosink_create (GstSDLVideoSink * sdl);
static void gst_sdlvideosink_destroy (GstSDLVideoSink * sdl);

static gboolean gst_sdlvideosink_setcaps (GstBaseSink * bsink, GstCaps * caps);

static GstFlowReturn gst_sdlvideosink_show_frame (GstBaseSink * bsink,
    GstBuffer * buff);

static void gst_sdlvideosink_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_sdlvideosink_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);
static GstStateChangeReturn
gst_sdlvideosink_change_state (GstElement * element, GstStateChange transition);


static GstPadTemplate *sink_template;

static GstElementClass *parent_class = NULL;

GType
gst_sdlvideosink_get_type (void)
{
  static GType sdlvideosink_type = 0;

  if (!sdlvideosink_type) {
    static const GTypeInfo sdlvideosink_info = {
      sizeof (GstSDLVideoSinkClass),
      gst_sdlvideosink_base_init,
      NULL,
      (GClassInitFunc) gst_sdlvideosink_class_init,
      NULL,
      NULL,
      sizeof (GstSDLVideoSink),
      0,
      (GInstanceInitFunc) gst_sdlvideosink_init,
    };
    static const GInterfaceInfo iface_info = {
      (GInterfaceInitFunc) gst_sdlvideosink_interface_init,
      NULL,
      NULL,
    };
    static const GInterfaceInfo xoverlay_info = {
      (GInterfaceInitFunc) gst_sdlvideosink_xoverlay_init,
      NULL,
      NULL,
    };

    sdlvideosink_type = g_type_register_static (GST_TYPE_VIDEO_SINK,
        "GstSDLVideoSink", &sdlvideosink_info, 0);
    g_type_add_interface_static (sdlvideosink_type,
        GST_TYPE_IMPLEMENTS_INTERFACE, &iface_info);
    g_type_add_interface_static (sdlvideosink_type, GST_TYPE_X_OVERLAY,
        &xoverlay_info);
  }

  return sdlvideosink_type;
}

static void
gst_sdlvideosink_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);
  GstCaps *capslist;
  gint i;
  guint32 formats[] = {
    GST_MAKE_FOURCC ('I', '4', '2', '0'),
    GST_MAKE_FOURCC ('Y', 'V', '1', '2'),
    GST_MAKE_FOURCC ('Y', 'U', 'Y', '2')
/*
    GST_MAKE_FOURCC ('Y', 'V', 'Y', 'U'),
    GST_MAKE_FOURCC ('U', 'Y', 'V', 'Y')
*/
  };

  /* make a list of all available caps */
  capslist = gst_caps_new_empty ();
  for (i = 0; i < G_N_ELEMENTS (formats); i++) {
    gst_caps_append_structure (capslist,
        gst_structure_new ("video/x-raw-yuv",
            "format", GST_TYPE_FOURCC, formats[i],
            "width", GST_TYPE_INT_RANGE, 1, G_MAXINT,
            "height", GST_TYPE_INT_RANGE, 1, G_MAXINT,
            "framerate", GST_TYPE_DOUBLE_RANGE, (gdouble) 1.0,
            (gdouble) 100.0, NULL));
  }

  sink_template = gst_pad_template_new ("sink",
      GST_PAD_SINK, GST_PAD_ALWAYS, capslist);

  gst_element_class_add_pad_template (element_class, sink_template);
  gst_element_class_set_details (element_class, &gst_sdlvideosink_details);

  GST_DEBUG_CATEGORY_INIT (sdlvideosink_debug, "sdlvideosink", 0,
      "SDL video sink element");
}

static void
gst_sdlvideosink_finalize (GObject * obj)
{
  g_mutex_free (GST_SDLVIDEOSINK (obj)->lock);

  G_OBJECT_CLASS (parent_class)->finalize (obj);
}

static void
gst_sdlvideosink_get_times (GstBaseSink * basesink, GstBuffer * buffer,
    GstClockTime * start, GstClockTime * end)
{
  GstSDLVideoSink *sdlvideosink = GST_SDLVIDEOSINK (basesink);
  GstClockTime timestamp, duration;

  timestamp = GST_BUFFER_TIMESTAMP (buffer);
  if (GST_CLOCK_TIME_IS_VALID (timestamp)) {
    *start = timestamp;
    duration = GST_BUFFER_DURATION (buffer);
    if (GST_CLOCK_TIME_IS_VALID (duration)) {
      *end = timestamp + duration;
    } else {
      if (sdlvideosink->framerate > 0) {
        *end = timestamp + GST_SECOND / sdlvideosink->framerate;
      }
    }
  }
}

static void
gst_sdlvideosink_class_init (GstSDLVideoSinkClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseSinkClass *gstvs_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstvs_class = (GstBaseSinkClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->set_property = gst_sdlvideosink_set_property;
  gobject_class->get_property = gst_sdlvideosink_get_property;

  gobject_class->finalize = gst_sdlvideosink_finalize;

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_sdlvideosink_change_state);

  gstvs_class->set_caps = GST_DEBUG_FUNCPTR (gst_sdlvideosink_setcaps);
  gstvs_class->get_times = GST_DEBUG_FUNCPTR (gst_sdlvideosink_get_times);
  gstvs_class->preroll = GST_DEBUG_FUNCPTR (gst_sdlvideosink_show_frame);
  gstvs_class->render = GST_DEBUG_FUNCPTR (gst_sdlvideosink_show_frame);

  g_object_class_install_property (gobject_class, PROP_FULLSCREEN,
      g_param_spec_boolean ("fullscreen", "Fullscreen",
          "If true it will be Full screen", FALSE, G_PARAM_READWRITE));

  /*gstvs_class->set_video_out = gst_sdlvideosink_set_video_out;
     gstvs_class->push_ui_event = gst_sdlvideosink_push_ui_event;
     gstvs_class->set_geometry = gst_sdlvideosink_set_geometry; */
}

#if 0
/* FIXME */
static GstBuffer *
gst_sdlvideosink_buffer_new (GstBufferPool * pool,
    gint64 location, guint size, gpointer user_data)
{
  GstSDLVideoSink *sdlvideosink = GST_SDLVIDEOSINK (user_data);
  GstBuffer *buffer;

  if (!sdlvideosink->overlay)
    return NULL;

  if (!gst_sdlvideosink_lock (sdlvideosink)) {
    return NULL;
  }

  /* this protects the buffer from being written over multiple times */
  g_mutex_lock (sdlvideosink->lock);

  buffer = gst_buffer_new ();
  GST_BUFFER_FLAG_SET (buffer, GST_BUFFER_DONTFREE);
  GST_BUFFER_DATA (buffer) = sdlvideosink->overlay->pixels[0];
  if (sdlvideosink->format == SDL_YV12_OVERLAY ||
      sdlvideosink->format == SDL_IYUV_OVERLAY) {
    GST_BUFFER_SIZE (buffer) =
        sdlvideosink->width * sdlvideosink->height * 3 / 2;
  } else {
    GST_BUFFER_SIZE (buffer) = sdlvideosink->width * sdlvideosink->height * 2;
  }
  GST_BUFFER_MAXSIZE (buffer) = GST_BUFFER_SIZE (buffer);

  return buffer;
}

static void
gst_sdlvideosink_buffer_free (GstBufferPool * pool,
    GstBuffer * buffer, gpointer user_data)
{
  GstSDLVideoSink *sdlvideosink = GST_SDLVIDEOSINK (user_data);

  g_mutex_unlock (sdlvideosink->lock);
  gst_sdlvideosink_unlock (sdlvideosink);

  gst_buffer_default_free (buffer);
}


static GstBufferPool *
gst_sdlvideosink_get_bufferpool (GstPad * pad)
{
  GstSDLVideoSink *sdlvideosink = GST_SDLVIDEOSINK (gst_pad_get_parent (pad));

  if (sdlvideosink->overlay)
    return sdlvideosink->bufferpool;

  return NULL;
}
#endif

static void
gst_sdlvideosink_init (GstSDLVideoSink * sdlvideosink)
{

  sdlvideosink->width = -1;
  sdlvideosink->height = -1;
  sdlvideosink->framerate = 0;
  sdlvideosink->full_screen = FALSE;

  sdlvideosink->overlay = NULL;
  sdlvideosink->screen = NULL;

  sdlvideosink->xwindow_id = 0;

  //sdlvideosink->capslist = capslist;

  sdlvideosink->init = FALSE;

  sdlvideosink->event_thread = NULL;
  sdlvideosink->running = FALSE;

  sdlvideosink->lock = g_mutex_new ();
}

static void
gst_sdlvideosink_interface_init (GstImplementsInterfaceClass * klass)
{
  klass->supported = gst_sdlvideosink_supported;
}

static gboolean
gst_sdlvideosink_supported (GstImplementsInterface * interface,
    GType iface_type)
{
  g_assert (iface_type == GST_TYPE_X_OVERLAY);

  /* FIXME: check SDL for whether it was compiled against X, FB, etc. */
  return (GST_STATE (interface) != GST_STATE_NULL);
}

static void
gst_sdlvideosink_xoverlay_init (GstXOverlayClass * klass)
{
  klass->set_xwindow_id = gst_sdlvideosink_xoverlay_set_xwindow_id;
}

static void
gst_sdlvideosink_xoverlay_set_xwindow_id (GstXOverlay * overlay,
    unsigned long parent)
{
  GstSDLVideoSink *sdlvideosink = GST_SDLVIDEOSINK (overlay);

  sdlvideosink->xwindow_id = parent;

  /* are we running yet? */
  if (sdlvideosink->init) {
    gboolean negotiated = (sdlvideosink->overlay != NULL);

    if (negotiated)
      gst_sdlvideosink_destroy (sdlvideosink);

    gst_sdlvideosink_initsdl (sdlvideosink);

    if (negotiated)
      gst_sdlvideosink_create (sdlvideosink);
  }
}

static guint32
gst_sdlvideosink_get_sdl_from_fourcc (GstSDLVideoSink * sdlvideosink,
    guint32 code)
{
  switch (code) {
      /* Note: SDL_IYUV_OVERLAY does not always work for I420 */
    case GST_MAKE_FOURCC ('I', '4', '2', '0'):
      return SDL_YV12_OVERLAY;
    case GST_MAKE_FOURCC ('Y', 'V', '1', '2'):
      return SDL_YV12_OVERLAY;
    case GST_MAKE_FOURCC ('Y', 'U', 'Y', '2'):
      return SDL_YUY2_OVERLAY;
    case GST_MAKE_FOURCC ('U', 'Y', 'V', 'Y'):
      return SDL_UYVY_OVERLAY;
    case GST_MAKE_FOURCC ('Y', 'V', 'Y', 'U'):
      return SDL_YVYU_OVERLAY;
    default:
      return 0;
  }
}

static gboolean
gst_sdlvideosink_lock (GstSDLVideoSink * sdlvideosink)
{
  /* assure that we've got a screen */
  if (!sdlvideosink->screen || !sdlvideosink->overlay)
    goto no_setup;

  /* Lock SDL/yuv-overlay */
  if (SDL_MUSTLOCK (sdlvideosink->screen)) {
    if (SDL_LockSurface (sdlvideosink->screen) < 0)
      goto could_not_lock;
  }
  if (SDL_LockYUVOverlay (sdlvideosink->overlay) < 0)
    goto lock_yuv;

  return TRUE;

  /* ERRORS */
no_setup:
  {
    GST_ELEMENT_ERROR (sdlvideosink, LIBRARY, TOO_LAZY, (NULL),
        ("Tried to lock screen without being set-up"));
    return FALSE;
  }
could_not_lock:
  {
    GST_ELEMENT_ERROR (sdlvideosink, LIBRARY, TOO_LAZY, (NULL),
        ("SDL: couldn't lock the SDL video window: %s", SDL_GetError ()));
    return FALSE;
  }
lock_yuv:
  {
    GST_ELEMENT_ERROR (sdlvideosink, LIBRARY, TOO_LAZY, (NULL),
        ("SDL: couldn\'t lock the SDL YUV overlay: %s", SDL_GetError ()));
    return FALSE;
  }
}


static void
gst_sdlvideosink_unlock (GstSDLVideoSink * sdlvideosink)
{
  /* Unlock SDL_overlay */
  SDL_UnlockYUVOverlay (sdlvideosink->overlay);
  if (SDL_MUSTLOCK (sdlvideosink->screen))
    SDL_UnlockSurface (sdlvideosink->screen);
}

static void
gst_sdlvideosink_deinitsdl (GstSDLVideoSink * sdlvideosink)
{
  g_mutex_lock (sdlvideosink->lock);

  if (sdlvideosink->init) {
    sdlvideosink->running = FALSE;
    if (sdlvideosink->event_thread) {
      g_thread_join (sdlvideosink->event_thread);
      sdlvideosink->event_thread = NULL;
    }

    SDL_Quit ();
    sdlvideosink->init = FALSE;

  }

  g_mutex_unlock (sdlvideosink->lock);
}

int
SDL_WaitEventTimeout (SDL_Event * event, Uint32 timeout)
{
  Uint32 i;
  int numevents = 0;

  for (i = 0; i < timeout; i += 10) {
    SDL_PumpEvents ();
    /*  numevents = SDL_PeepEvents(event, 1, SDL_GETEVENT, SDL_ALLEVENTS); */
    numevents =
        SDL_PeepEvents (event, 1, SDL_GETEVENT,
        SDL_KEYDOWNMASK | SDL_KEYUPMASK |
        /* SDL_MOUSEMOTIONMASK | SDL_MOUSEBUTTONDOWNMASK | SDL_MOUSEBUTTONUPMASK | */
        SDL_QUITMASK);
    switch (numevents) {
      case -1:
        return 0;
        break;
      case 0:
        SDL_Delay (10);
        break;
      default:
        return numevents;
        break;
    }
  }

  return 0;
}

static gpointer
gst_sdlvideosink_event_thread (GstSDLVideoSink * sdlvideosink)
{

  SDL_Event event;

  while (sdlvideosink->running) {
    if (SDL_WaitEventTimeout (&event, 50)) {

      switch (event.type) {
        case SDL_KEYDOWN:
          if (SDLK_ESCAPE != event.key.keysym.sym) {
            break;
          } else {
            /* fall through */
          }
        case SDL_QUIT:
          sdlvideosink->running = FALSE;
          GST_ELEMENT_ERROR (sdlvideosink, RESOURCE, OPEN_WRITE,
              ("Video output device is gone."),
              ("We were running fullscreen and user "
                  "pressed the ESC key, stopping playback."));
          break;
      }

    }

  }

  return NULL;

}

static gboolean
gst_sdlvideosink_initsdl (GstSDLVideoSink * sdlvideosink)
{
  gst_sdlvideosink_deinitsdl (sdlvideosink);

  g_mutex_lock (sdlvideosink->lock);

  if (!sdlvideosink->xwindow_id) {
    unsetenv ("SDL_WINDOWID");
  } else {
    char SDL_hack[32];

    sprintf (SDL_hack, "%lu", sdlvideosink->xwindow_id);
    setenv ("SDL_WINDOWID", SDL_hack, 1);
  }

  /* Initialize the SDL library */
  if (SDL_Init (SDL_INIT_VIDEO | SDL_INIT_NOPARACHUTE) < 0)
    goto init_failed;

  sdlvideosink->init = TRUE;

  sdlvideosink->running = TRUE;
  sdlvideosink->event_thread =
      g_thread_create ((GThreadFunc) gst_sdlvideosink_event_thread,
      sdlvideosink, TRUE, NULL);

  g_mutex_unlock (sdlvideosink->lock);

  return TRUE;

  /* ERRORS */
init_failed:
  {
    GST_ELEMENT_ERROR (sdlvideosink, LIBRARY, INIT, (NULL),
        ("Couldn't initialize SDL: %s", SDL_GetError ()));
    g_mutex_unlock (sdlvideosink->lock);
    return FALSE;
  }
}

static void
gst_sdlvideosink_destroy (GstSDLVideoSink * sdlvideosink)
{
  g_mutex_lock (sdlvideosink->lock);

  if (sdlvideosink->overlay) {
    SDL_FreeYUVOverlay (sdlvideosink->overlay);
    sdlvideosink->overlay = NULL;
  }

  if (sdlvideosink->screen) {
    SDL_FreeSurface (sdlvideosink->screen);
    sdlvideosink->screen = NULL;
  }

  g_mutex_unlock (sdlvideosink->lock);
}

static gboolean
gst_sdlvideosink_create (GstSDLVideoSink * sdlvideosink)
{
  if (GST_VIDEO_SINK_HEIGHT (sdlvideosink) <= 0)
    GST_VIDEO_SINK_HEIGHT (sdlvideosink) = sdlvideosink->height;
  if (GST_VIDEO_SINK_WIDTH (sdlvideosink) <= 0)
    GST_VIDEO_SINK_WIDTH (sdlvideosink) = sdlvideosink->width;

  gst_sdlvideosink_destroy (sdlvideosink);

  g_mutex_lock (sdlvideosink->lock);

  /* create a SDL window of the size requested by the user */
  if (sdlvideosink->full_screen) {
    sdlvideosink->screen =
        SDL_SetVideoMode (GST_VIDEO_SINK_WIDTH (sdlvideosink),
        GST_VIDEO_SINK_HEIGHT (sdlvideosink), 0,
        SDL_SWSURFACE | SDL_FULLSCREEN);
  } else {
    sdlvideosink->screen =
        SDL_SetVideoMode (GST_VIDEO_SINK_WIDTH (sdlvideosink),
        GST_VIDEO_SINK_HEIGHT (sdlvideosink), 0, SDL_HWSURFACE | SDL_RESIZABLE);
  }
  if (sdlvideosink->screen == NULL)
    goto no_screen;

  /* create a new YUV overlay */
  sdlvideosink->overlay = SDL_CreateYUVOverlay (sdlvideosink->width,
      sdlvideosink->height, sdlvideosink->format, sdlvideosink->screen);
  if (sdlvideosink->overlay == NULL)
    goto no_overlay;


  GST_DEBUG ("Using a %dx%d %dbpp SDL screen with a %dx%d \'"
      GST_FOURCC_FORMAT "\' YUV overlay", GST_VIDEO_SINK_WIDTH (sdlvideosink),
      GST_VIDEO_SINK_HEIGHT (sdlvideosink),
      sdlvideosink->screen->format->BitsPerPixel, sdlvideosink->width,
      sdlvideosink->height, GST_FOURCC_ARGS (sdlvideosink->format));

  sdlvideosink->rect.x = 0;
  sdlvideosink->rect.y = 0;
  sdlvideosink->rect.w = GST_VIDEO_SINK_WIDTH (sdlvideosink);
  sdlvideosink->rect.h = GST_VIDEO_SINK_HEIGHT (sdlvideosink);

  /*SDL_DisplayYUVOverlay (sdlvideosink->overlay, &(sdlvideosink->rect)); */

  GST_DEBUG ("sdlvideosink: setting %08x (" GST_FOURCC_FORMAT ")",
      sdlvideosink->format, GST_FOURCC_ARGS (sdlvideosink->format));

  g_mutex_unlock (sdlvideosink->lock);

  return TRUE;

  /* ERRORS */
no_screen:
  {
    GST_ELEMENT_ERROR (sdlvideosink, LIBRARY, TOO_LAZY, (NULL),
        ("SDL: Couldn't set %dx%d: %s", GST_VIDEO_SINK_WIDTH (sdlvideosink),
            GST_VIDEO_SINK_HEIGHT (sdlvideosink), SDL_GetError ()));
    g_mutex_unlock (sdlvideosink->lock);
    return FALSE;
  }
no_overlay:
  {
    GST_ELEMENT_ERROR (sdlvideosink, LIBRARY, TOO_LAZY, (NULL),
        ("SDL: Couldn't create SDL YUV overlay (%dx%d \'" GST_FOURCC_FORMAT
            "\'): %s", sdlvideosink->width, sdlvideosink->height,
            GST_FOURCC_ARGS (sdlvideosink->format), SDL_GetError ()));
    g_mutex_unlock (sdlvideosink->lock);
    return FALSE;
  }
}

static gboolean
gst_sdlvideosink_setcaps (GstBaseSink * bsink, GstCaps * vscapslist)
{
  GstSDLVideoSink *sdlvideosink;
  guint32 format;
  GstStructure *structure;

  sdlvideosink = GST_SDLVIDEOSINK (bsink);

  structure = gst_caps_get_structure (vscapslist, 0);
  gst_structure_get_fourcc (structure, "format", &sdlvideosink->fourcc);
  sdlvideosink->format =
      gst_sdlvideosink_get_sdl_from_fourcc (sdlvideosink, sdlvideosink->fourcc);
  gst_structure_get_int (structure, "width", &sdlvideosink->width);
  gst_structure_get_int (structure, "height", &sdlvideosink->height);
  gst_structure_get_double (structure, "framerate", &sdlvideosink->framerate);

  if (!sdlvideosink->format || !gst_sdlvideosink_create (sdlvideosink))
    return FALSE;

  gst_x_overlay_got_desired_size (GST_X_OVERLAY (sdlvideosink),
      sdlvideosink->width, sdlvideosink->height);

  return TRUE;
}


static GstFlowReturn
gst_sdlvideosink_show_frame (GstBaseSink * bsink, GstBuffer * buf)
{

  GstSDLVideoSink *sdlvideosink;
  SDL_Event sdl_event;

  sdlvideosink = GST_SDLVIDEOSINK (bsink);

  if (!sdlvideosink->init ||
      !sdlvideosink->overlay || !sdlvideosink->overlay->pixels)
    goto not_init;

  /* if (GST_BUFFER_DATA (buf) != sdlvideosink->overlay->pixels[0]) */
  if (TRUE) {
    if (!gst_sdlvideosink_lock (sdlvideosink))
      goto cannot_lock;

    /* buf->yuv - FIXME: bufferpool! */
    if (sdlvideosink->format == SDL_YV12_OVERLAY) {
      guint8 *y, *u, *v;

      switch (sdlvideosink->fourcc) {
        case GST_MAKE_FOURCC ('I', '4', '2', '0'):
          y = GST_BUFFER_DATA (buf);
          /* I420 is YV12 with switched colour planes and different offsets */
          v = y + I420_U_OFFSET (sdlvideosink->width, sdlvideosink->height);
          u = y + I420_V_OFFSET (sdlvideosink->width, sdlvideosink->height);
          break;
        case GST_MAKE_FOURCC ('Y', 'V', '1', '2'):
          y = GST_BUFFER_DATA (buf);
          u = y + sdlvideosink->width * sdlvideosink->height;
          v = y + sdlvideosink->width * sdlvideosink->height * 5 / 4;
          break;
        default:
          g_assert_not_reached ();
      }

      memcpy (sdlvideosink->overlay->pixels[0], y,
          sdlvideosink->width * sdlvideosink->height);
      memcpy (sdlvideosink->overlay->pixels[1], u,
          sdlvideosink->width * sdlvideosink->height / 4);
      memcpy (sdlvideosink->overlay->pixels[2], v,
          sdlvideosink->width * sdlvideosink->height / 4);
    } else {
      memcpy (sdlvideosink->overlay->pixels[0], GST_BUFFER_DATA (buf),
          sdlvideosink->width * sdlvideosink->height * 2);
    }
    gst_sdlvideosink_unlock (sdlvideosink);
  }

  /* Show, baby, show! */
  SDL_DisplayYUVOverlay (sdlvideosink->overlay, &(sdlvideosink->rect));

  while (SDL_PollEvent (&sdl_event)) {
    switch (sdl_event.type) {
      case SDL_VIDEORESIZE:
        /* create a SDL window of the size requested by the user */
        GST_VIDEO_SINK_WIDTH (sdlvideosink) = sdl_event.resize.w;
        GST_VIDEO_SINK_HEIGHT (sdlvideosink) = sdl_event.resize.h;
        gst_sdlvideosink_create (sdlvideosink);
        break;
    }
  }

  return GST_FLOW_OK;

  /* ERRORS */
not_init:
  {
    GST_ELEMENT_ERROR (sdlvideosink, CORE, NEGOTIATION, (NULL),
        ("not negotiated."));
    return GST_FLOW_NOT_NEGOTIATED;
  }
cannot_lock:
  {
    /* lock function posted detailed message */
    return GST_FLOW_ERROR;
  }
}


static void
gst_sdlvideosink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstSDLVideoSink *sdlvideosink;

  sdlvideosink = GST_SDLVIDEOSINK (object);

  switch (prop_id) {
    case PROP_FULLSCREEN:
      sdlvideosink->full_screen = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


static void
gst_sdlvideosink_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstSDLVideoSink *sdlvideosink;

  sdlvideosink = GST_SDLVIDEOSINK (object);

  switch (prop_id) {
    case PROP_FULLSCREEN:
      g_value_set_boolean (value, sdlvideosink->full_screen);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


static GstStateChangeReturn
gst_sdlvideosink_change_state (GstElement * element, GstStateChange transition)
{
  GstSDLVideoSink *sdlvideosink;
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  g_return_val_if_fail (GST_IS_SDLVIDEOSINK (element),
      GST_STATE_CHANGE_FAILURE);
  sdlvideosink = GST_SDLVIDEOSINK (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      if (!gst_sdlvideosink_initsdl (sdlvideosink))
        goto init_failed;
      GST_OBJECT_FLAG_SET (sdlvideosink, GST_SDLVIDEOSINK_OPEN);
      break;
    default:                   /* do nothing */
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      sdlvideosink->framerate = 0;
      gst_sdlvideosink_destroy (sdlvideosink);
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      gst_sdlvideosink_deinitsdl (sdlvideosink);
      GST_OBJECT_FLAG_UNSET (sdlvideosink, GST_SDLVIDEOSINK_OPEN);
      break;
    default:                   /* do nothing */
      break;
  }
  return ret;

init_failed:
  {
    /* method posted detailed error message */
    GST_DEBUG_OBJECT (sdlvideosink, "init failed");
    return GST_STATE_CHANGE_FAILURE;
  }
}


static gboolean
plugin_init (GstPlugin * plugin)
{
  if (!gst_element_register (plugin, "sdlvideosink", GST_RANK_NONE,
          GST_TYPE_SDLVIDEOSINK))
    return FALSE;

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "sdlvideosink",
    "SDL Video Sink", plugin_init, VERSION, "LGPL", GST_PACKAGE, GST_ORIGIN)
