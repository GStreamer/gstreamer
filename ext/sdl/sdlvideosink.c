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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

/* let's not forget to mention that all this was based on aasink ;-) */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <signal.h>
#include <string.h>
#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#endif
#include <stdlib.h>

#include <gst/glib-compat-private.h>
#include <gst/interfaces/xoverlay.h>
#include <gst/interfaces/navigation.h>

#include "sdlvideosink.h"

GST_DEBUG_CATEGORY_EXTERN (sdl_debug);
#define GST_CAT_DEFAULT sdl_debug

/* These macros are adapted from videotestsrc.c 
 *  and/or gst-plugins/gst/games/gstvideoimage.c */
#define I420_Y_ROWSTRIDE(width) (GST_ROUND_UP_4(width))
#define I420_U_ROWSTRIDE(width) (GST_ROUND_UP_8(width)/2)
#define I420_V_ROWSTRIDE(width) ((GST_ROUND_UP_8(I420_Y_ROWSTRIDE(width)))/2)

#define I420_Y_OFFSET(w,h) (0)
#define I420_U_OFFSET(w,h) (I420_Y_OFFSET(w,h)+(I420_Y_ROWSTRIDE(w)*GST_ROUND_UP_2(h)))
#define I420_V_OFFSET(w,h) (I420_U_OFFSET(w,h)+(I420_U_ROWSTRIDE(w)*GST_ROUND_UP_2(h)/2))

#define I420_SIZE(w,h)     (I420_V_OFFSET(w,h)+(I420_V_ROWSTRIDE(w)*GST_ROUND_UP_2(h)/2))


enum
{
  PROP_0,
  PROP_FULLSCREEN
};

static void gst_sdlvideosink_interface_init (GstImplementsInterfaceClass *
    klass);
static gboolean gst_sdlvideosink_supported (GstImplementsInterface * iface,
    GType type);

static void gst_sdlvideosink_xoverlay_init (GstXOverlayClass * klass);
static void gst_sdlvideosink_xoverlay_set_window_handle
    (GstXOverlay * overlay, guintptr parent);

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

static void gst_sdlvideosink_navigation_init (GstNavigationInterface * iface);

static void gst_sdlv_process_events (GstSDLVideoSink * sdlvideosink);

static GstPadTemplate *sink_template;

static void
_do_init (GType type)
{
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
  static const GInterfaceInfo navigation_info = {
    (GInterfaceInitFunc) gst_sdlvideosink_navigation_init,
    NULL,
    NULL,
  };

  g_type_add_interface_static (type,
      GST_TYPE_IMPLEMENTS_INTERFACE, &iface_info);
  g_type_add_interface_static (type, GST_TYPE_X_OVERLAY, &xoverlay_info);
  g_type_add_interface_static (type, GST_TYPE_NAVIGATION, &navigation_info);

}

GST_BOILERPLATE_FULL (GstSDLVideoSink, gst_sdlvideosink, GstVideoSink,
    GST_TYPE_VIDEO_SINK, _do_init);

static void
gst_sdlvideosink_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);
  GstCaps *capslist;
  gint i;
  guint32 formats[] = {
    GST_MAKE_FOURCC ('I', '4', '2', '0'),
    GST_MAKE_FOURCC ('Y', 'V', '1', '2'),
    GST_MAKE_FOURCC ('Y', 'U', 'Y', '2'),
    GST_MAKE_FOURCC ('Y', 'V', 'Y', 'U'),
    GST_MAKE_FOURCC ('U', 'Y', 'V', 'Y')
  };

  /* make a list of all available caps */
  capslist = gst_caps_new_empty ();
  for (i = 0; i < G_N_ELEMENTS (formats); i++) {
    gst_caps_append_structure (capslist,
        gst_structure_new ("video/x-raw-yuv",
            "format", GST_TYPE_FOURCC, formats[i],
            "width", GST_TYPE_INT_RANGE, 1, G_MAXINT,
            "height", GST_TYPE_INT_RANGE, 1, G_MAXINT,
            "framerate", GST_TYPE_FRACTION_RANGE, 0, 1, 100, 1, NULL));
  }

  sink_template = gst_pad_template_new ("sink",
      GST_PAD_SINK, GST_PAD_ALWAYS, capslist);

  gst_element_class_add_pad_template (element_class, sink_template);
  gst_element_class_set_static_metadata (element_class, "SDL video sink",
      "Sink/Video", "An SDL-based videosink",
      "Ronald Bultje <rbultje@ronald.bitfreak.net>, "
      "Edgard Lima <edgard.lima@indt.org.br>, "
      "Jan Schmidt <thaytan@mad.scientist.com>");
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
      if (sdlvideosink->framerate_n > 0) {
        *end = timestamp +
            gst_util_uint64_scale_int (GST_SECOND, sdlvideosink->framerate_d,
            sdlvideosink->framerate_n);
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
          "If true it will be Full screen", FALSE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

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
gst_sdlvideosink_init (GstSDLVideoSink * sdlvideosink,
    GstSDLVideoSinkClass * g_class)
{

  sdlvideosink->width = -1;
  sdlvideosink->height = -1;
  sdlvideosink->framerate_n = 0;
  sdlvideosink->framerate_d = 1;
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
  GstSDLVideoSink *sdlvideosink = GST_SDLVIDEOSINK (interface);
  gboolean result = FALSE;

  /* check SDL for whether it was compiled against X, FB, etc. */
  if (iface_type == GST_TYPE_X_OVERLAY) {
    gchar tmp[4];

    if (!sdlvideosink->init) {
      g_mutex_lock (sdlvideosink->lock);
      SDL_Init (SDL_INIT_VIDEO);

      /* True if the video driver is X11 */
      result = (strcmp ("x11", SDL_VideoDriverName (tmp, 4)) == 0);
      SDL_QuitSubSystem (SDL_INIT_VIDEO);
      g_mutex_unlock (sdlvideosink->lock);
    } else
      result = sdlvideosink->is_xwindows;
  } else if (iface_type == GST_TYPE_NAVIGATION)
    result = TRUE;

  return result;
}

/* SDL Video sink and X overlay: 
 *
 * SDL supports creating an Xv window/overlay within an existing X window
 * through the horrible mechanism of setting the WINDOWID environment
 * variable.
 * It will then display the x overlay within that window, but not at the
 * full window size. Instead, we need to explicitly tell SDL the size.
 *
 * Unfortunately, the XOverlay interface in GStreamer doesn't supply
 * that information. The only way to get it would be to do what X[v]imagesink
 * does and retrieve it using X11 calls, and linking to Xlib. That would
 * defeat the whole purpose of using the SDL abstraction and plugin entirely
 * however. 
 *
 * I have no nice solution to this problem for you, dear readers.
 */
static void
gst_sdlvideosink_xoverlay_init (GstXOverlayClass * klass)
{
  klass->set_window_handle = gst_sdlvideosink_xoverlay_set_window_handle;
}

static void
gst_sdlvideosink_xoverlay_set_window_handle (GstXOverlay * overlay,
    guintptr handle)
{
  GstSDLVideoSink *sdlvideosink = GST_SDLVIDEOSINK (overlay);
  unsigned long parent = (unsigned long) handle;

  if (sdlvideosink->xwindow_id == parent)
    return;

  sdlvideosink->xwindow_id = parent;

  /* are we running yet? */
  if (sdlvideosink->init) {
    gboolean negotiated;

    g_mutex_lock (sdlvideosink->lock);

    negotiated = (sdlvideosink->overlay != NULL);

    if (negotiated)
      gst_sdlvideosink_destroy (sdlvideosink);

    /* Call initsdl to set the WINDOWID env var urk */
    gst_sdlvideosink_initsdl (sdlvideosink);

    if (negotiated)
      gst_sdlvideosink_create (sdlvideosink);

    g_mutex_unlock (sdlvideosink->lock);
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

/* Must be called with ->lock held */
static void
gst_sdlvideosink_deinitsdl (GstSDLVideoSink * sdlvideosink)
{
  if (sdlvideosink->init) {
    sdlvideosink->running = FALSE;
    if (sdlvideosink->event_thread) {
      g_mutex_unlock (sdlvideosink->lock);
      g_thread_join (sdlvideosink->event_thread);
      g_mutex_lock (sdlvideosink->lock);
      sdlvideosink->event_thread = NULL;
    }

    SDL_QuitSubSystem (SDL_INIT_VIDEO);
    sdlvideosink->init = FALSE;

  }
}

/* Process pending events. Call with ->lock held */
static void
gst_sdlv_process_events (GstSDLVideoSink * sdlvideosink)
{
  SDL_Event event;
  int numevents;
  char *keysym = NULL;

  do {
    SDL_PumpEvents ();
    numevents = SDL_PeepEvents (&event, 1, SDL_GETEVENT,
        SDL_KEYDOWNMASK | SDL_KEYUPMASK |
        SDL_MOUSEMOTIONMASK | SDL_MOUSEBUTTONDOWNMASK |
        SDL_MOUSEBUTTONUPMASK | SDL_QUITMASK | SDL_VIDEORESIZEMASK);

    if (numevents > 0 && (event.type == SDL_KEYUP || event.type == SDL_KEYDOWN)) {
      keysym = SDL_GetKeyName (event.key.keysym.sym);
    }

    if (numevents > 0) {
      g_mutex_unlock (sdlvideosink->lock);
      switch (event.type) {
        case SDL_MOUSEMOTION:
          gst_navigation_send_mouse_event (GST_NAVIGATION (sdlvideosink),
              "mouse-move", 0, event.motion.x, event.motion.y);
          break;
        case SDL_MOUSEBUTTONDOWN:
          gst_navigation_send_mouse_event (GST_NAVIGATION (sdlvideosink),
              "mouse-button-press",
              event.button.button, event.button.x, event.button.y);
          break;
        case SDL_MOUSEBUTTONUP:
          gst_navigation_send_mouse_event (GST_NAVIGATION (sdlvideosink),
              "mouse-button-release",
              event.button.button, event.button.x, event.button.y);
          break;
        case SDL_KEYUP:
          GST_DEBUG ("key press event %s !",
              SDL_GetKeyName (event.key.keysym.sym));
          gst_navigation_send_key_event (GST_NAVIGATION (sdlvideosink),
              "key-release", keysym);
          break;
        case SDL_KEYDOWN:
          if (SDLK_ESCAPE != event.key.keysym.sym) {
            GST_DEBUG ("key press event %s !",
                SDL_GetKeyName (event.key.keysym.sym));
            gst_navigation_send_key_event (GST_NAVIGATION (sdlvideosink),
                "key-press", keysym);
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
        case SDL_VIDEORESIZE:
          /* create a SDL window of the size requested by the user */
          g_mutex_lock (sdlvideosink->lock);
          GST_VIDEO_SINK_WIDTH (sdlvideosink) = event.resize.w;
          GST_VIDEO_SINK_HEIGHT (sdlvideosink) = event.resize.h;
          gst_sdlvideosink_create (sdlvideosink);
          g_mutex_unlock (sdlvideosink->lock);
          break;
      }
      g_mutex_lock (sdlvideosink->lock);
    }
  } while (numevents > 0);
}

static gpointer
gst_sdlvideosink_event_thread (GstSDLVideoSink * sdlvideosink)
{
  g_mutex_lock (sdlvideosink->lock);
  while (sdlvideosink->running) {
    gst_sdlv_process_events (sdlvideosink);

    /* Done events, sleep for 50 ms */
    g_mutex_unlock (sdlvideosink->lock);
    g_usleep (50000);
    g_mutex_lock (sdlvideosink->lock);
  }
  g_mutex_unlock (sdlvideosink->lock);

  return NULL;
}

/* Must be called with the SDL lock held */
static gboolean
gst_sdlvideosink_initsdl (GstSDLVideoSink * sdlvideosink)
{
  gst_sdlvideosink_deinitsdl (sdlvideosink);

  if (sdlvideosink->is_xwindows && !sdlvideosink->xwindow_id) {
    g_mutex_unlock (sdlvideosink->lock);
    gst_x_overlay_prepare_xwindow_id (GST_X_OVERLAY (sdlvideosink));
    g_mutex_lock (sdlvideosink->lock);
  }

  if (!sdlvideosink->xwindow_id) {
    g_unsetenv ("SDL_WINDOWID");
  } else {
    char SDL_hack[32];

    sprintf (SDL_hack, "%lu", sdlvideosink->xwindow_id);
    g_setenv ("SDL_WINDOWID", SDL_hack, 1);
  }

  /* Initialize the SDL library */
  if (SDL_Init (SDL_INIT_VIDEO | SDL_INIT_NOPARACHUTE) < 0)
    goto init_failed;

  sdlvideosink->init = TRUE;

  sdlvideosink->running = TRUE;
  sdlvideosink->event_thread =
      g_thread_create ((GThreadFunc) gst_sdlvideosink_event_thread,
      sdlvideosink, TRUE, NULL);

  return TRUE;

  /* ERRORS */
init_failed:
  {
    GST_ELEMENT_ERROR (sdlvideosink, LIBRARY, INIT, (NULL),
        ("Couldn't initialize SDL: %s", SDL_GetError ()));
    return FALSE;
  }
}

/* Must be called with the sdl lock held */
static void
gst_sdlvideosink_destroy (GstSDLVideoSink * sdlvideosink)
{
  if (sdlvideosink->overlay) {
    SDL_FreeYUVOverlay (sdlvideosink->overlay);
    sdlvideosink->overlay = NULL;
  }

  if (sdlvideosink->screen) {
    SDL_FreeSurface (sdlvideosink->screen);
    sdlvideosink->screen = NULL;
  }
  sdlvideosink->xwindow_id = 0;
}

/* Must be called with the sdl lock held */
static gboolean
gst_sdlvideosink_create (GstSDLVideoSink * sdlvideosink)
{
  if (GST_VIDEO_SINK_HEIGHT (sdlvideosink) <= 0)
    GST_VIDEO_SINK_HEIGHT (sdlvideosink) = sdlvideosink->height;
  if (GST_VIDEO_SINK_WIDTH (sdlvideosink) <= 0)
    GST_VIDEO_SINK_WIDTH (sdlvideosink) = sdlvideosink->width;

  gst_sdlvideosink_destroy (sdlvideosink);

  if (sdlvideosink->is_xwindows && !sdlvideosink->xwindow_id) {
    g_mutex_unlock (sdlvideosink->lock);
    gst_x_overlay_prepare_xwindow_id (GST_X_OVERLAY (sdlvideosink));
    g_mutex_lock (sdlvideosink->lock);
  }

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


  GST_DEBUG ("Using a %dx%d %dbpp SDL screen with a %dx%d \'%"
      GST_FOURCC_FORMAT "\' YUV overlay", GST_VIDEO_SINK_WIDTH (sdlvideosink),
      GST_VIDEO_SINK_HEIGHT (sdlvideosink),
      sdlvideosink->screen->format->BitsPerPixel, sdlvideosink->width,
      sdlvideosink->height, GST_FOURCC_ARGS (sdlvideosink->format));

  sdlvideosink->rect.x = 0;
  sdlvideosink->rect.y = 0;
  sdlvideosink->rect.w = GST_VIDEO_SINK_WIDTH (sdlvideosink);
  sdlvideosink->rect.h = GST_VIDEO_SINK_HEIGHT (sdlvideosink);

  /*SDL_DisplayYUVOverlay (sdlvideosink->overlay, &(sdlvideosink->rect)); */

  GST_DEBUG ("sdlvideosink: setting %08x (%" GST_FOURCC_FORMAT ")",
      sdlvideosink->format, GST_FOURCC_ARGS (sdlvideosink->format));

  return TRUE;

  /* ERRORS */
no_screen:
  {
    GST_ELEMENT_ERROR (sdlvideosink, LIBRARY, TOO_LAZY, (NULL),
        ("SDL: Couldn't set %dx%d: %s", GST_VIDEO_SINK_WIDTH (sdlvideosink),
            GST_VIDEO_SINK_HEIGHT (sdlvideosink), SDL_GetError ()));
    return FALSE;
  }
no_overlay:
  {
    GST_ELEMENT_ERROR (sdlvideosink, LIBRARY, TOO_LAZY, (NULL),
        ("SDL: Couldn't create SDL YUV overlay (%dx%d \'%" GST_FOURCC_FORMAT
            "\'): %s", sdlvideosink->width, sdlvideosink->height,
            GST_FOURCC_ARGS (sdlvideosink->format), SDL_GetError ()));
    return FALSE;
  }
}

static gboolean
gst_sdlvideosink_setcaps (GstBaseSink * bsink, GstCaps * vscapslist)
{
  GstSDLVideoSink *sdlvideosink;
  GstStructure *structure;
  gboolean res = TRUE;

  sdlvideosink = GST_SDLVIDEOSINK (bsink);

  structure = gst_caps_get_structure (vscapslist, 0);
  gst_structure_get_fourcc (structure, "format", &sdlvideosink->fourcc);
  sdlvideosink->format =
      gst_sdlvideosink_get_sdl_from_fourcc (sdlvideosink, sdlvideosink->fourcc);
  gst_structure_get_int (structure, "width", &sdlvideosink->width);
  gst_structure_get_int (structure, "height", &sdlvideosink->height);
  gst_structure_get_fraction (structure, "framerate",
      &sdlvideosink->framerate_n, &sdlvideosink->framerate_d);

  g_mutex_lock (sdlvideosink->lock);
  if (!sdlvideosink->format || !gst_sdlvideosink_create (sdlvideosink))
    res = FALSE;
  g_mutex_unlock (sdlvideosink->lock);

  return res;
}


static GstFlowReturn
gst_sdlvideosink_show_frame (GstBaseSink * bsink, GstBuffer * buf)
{

  GstSDLVideoSink *sdlvideosink;

  sdlvideosink = GST_SDLVIDEOSINK (bsink);

  g_mutex_lock (sdlvideosink->lock);
  if (!sdlvideosink->init ||
      !sdlvideosink->overlay || !sdlvideosink->overlay->pixels)
    goto not_init;

  /* if (GST_BUFFER_DATA (buf) != sdlvideosink->overlay->pixels[0]) */
  if (TRUE) {
    guint8 *out;
    gint l;

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
          u = y + I420_U_OFFSET (sdlvideosink->width, sdlvideosink->height);
          v = y + I420_V_OFFSET (sdlvideosink->width, sdlvideosink->height);
          break;
        default:
          gst_sdlvideosink_unlock (sdlvideosink);
          g_mutex_unlock (sdlvideosink->lock);
          g_return_val_if_reached (GST_FLOW_ERROR);
      }

      /* Y Plane */
      out = sdlvideosink->overlay->pixels[0];
      for (l = 0; l < sdlvideosink->height; l++) {
        memcpy (out, y, I420_Y_ROWSTRIDE (sdlvideosink->width));
        out += sdlvideosink->overlay->pitches[0];
        y += I420_Y_ROWSTRIDE (sdlvideosink->width);
      }

      /* U plane */
      out = sdlvideosink->overlay->pixels[1];
      for (l = 0; l < (sdlvideosink->height / 2); l++) {
        memcpy (out, u, I420_U_ROWSTRIDE (sdlvideosink->width));
        out += sdlvideosink->overlay->pitches[1];
        u += I420_U_ROWSTRIDE (sdlvideosink->width);
      }

      /* V plane */
      out = sdlvideosink->overlay->pixels[2];
      for (l = 0; l < (sdlvideosink->height / 2); l++) {
        memcpy (out, v, I420_V_ROWSTRIDE (sdlvideosink->width));
        out += sdlvideosink->overlay->pitches[2];
        v += I420_V_ROWSTRIDE (sdlvideosink->width);
      }
    } else {
      guint8 *in = GST_BUFFER_DATA (buf);
      gint in_stride = sdlvideosink->width * 2;

      out = sdlvideosink->overlay->pixels[0];

      for (l = 0; l < sdlvideosink->height; l++) {
        memcpy (out, in, in_stride);
        out += sdlvideosink->overlay->pitches[0];
        in += in_stride;
      }
    }
    gst_sdlvideosink_unlock (sdlvideosink);
  }

  /* Show, baby, show! */
  SDL_DisplayYUVOverlay (sdlvideosink->overlay, &(sdlvideosink->rect));

  /* Handle any resize */
  gst_sdlv_process_events (sdlvideosink);

  g_mutex_unlock (sdlvideosink->lock);

  return GST_FLOW_OK;

  /* ERRORS */
not_init:
  {
    GST_ELEMENT_ERROR (sdlvideosink, CORE, NEGOTIATION, (NULL),
        ("not negotiated."));
    g_mutex_unlock (sdlvideosink->lock);
    return GST_FLOW_NOT_NEGOTIATED;
  }
cannot_lock:
  {
    /* lock function posted detailed message */
    g_mutex_unlock (sdlvideosink->lock);
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
      sdlvideosink->is_xwindows = GST_IS_X_OVERLAY (sdlvideosink);
      g_mutex_lock (sdlvideosink->lock);
      if (!gst_sdlvideosink_initsdl (sdlvideosink)) {
        g_mutex_unlock (sdlvideosink->lock);
        goto init_failed;
      }
      GST_OBJECT_FLAG_SET (sdlvideosink, GST_SDLVIDEOSINK_OPEN);
      g_mutex_unlock (sdlvideosink->lock);
      break;
    default:                   /* do nothing */
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      sdlvideosink->framerate_n = 0;
      sdlvideosink->framerate_d = 1;
      g_mutex_lock (sdlvideosink->lock);
      gst_sdlvideosink_destroy (sdlvideosink);
      g_mutex_unlock (sdlvideosink->lock);
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      g_mutex_lock (sdlvideosink->lock);
      gst_sdlvideosink_deinitsdl (sdlvideosink);
      GST_OBJECT_FLAG_UNSET (sdlvideosink, GST_SDLVIDEOSINK_OPEN);
      g_mutex_unlock (sdlvideosink->lock);
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


static void
gst_sdlvideosink_navigation_send_event (GstNavigation * navigation,
    GstStructure * structure)
{
  GstSDLVideoSink *sdlvideosink = GST_SDLVIDEOSINK (navigation);
  GstEvent *event;
  GstVideoRectangle src, dst, result;
  double x, y, old_x, old_y;
  GstPad *pad = NULL;

  src.w = GST_VIDEO_SINK_WIDTH (sdlvideosink);
  src.h = GST_VIDEO_SINK_HEIGHT (sdlvideosink);
  dst.w = sdlvideosink->width;
  dst.h = sdlvideosink->height;
  gst_video_sink_center_rect (src, dst, &result, FALSE);

  event = gst_event_new_navigation (structure);

  /* Our coordinates can be wrong here if we centered the video */

  /* Converting pointer coordinates to the non scaled geometry */
  if (gst_structure_get_double (structure, "pointer_x", &old_x)) {
    x = old_x;

    if (x >= result.x && x <= (result.x + result.w)) {
      x -= result.x;
      x *= sdlvideosink->width;
      x /= result.w;
    } else {
      x = 0;
    }
    GST_DEBUG_OBJECT (sdlvideosink, "translated navigation event x "
        "coordinate from %f to %f", old_x, x);
    gst_structure_set (structure, "pointer_x", G_TYPE_DOUBLE, x, NULL);
  }
  if (gst_structure_get_double (structure, "pointer_y", &old_y)) {
    y = old_y;

    if (y >= result.y && y <= (result.y + result.h)) {
      y -= result.y;
      y *= sdlvideosink->height;
      y /= result.h;
    } else {
      y = 0;
    }
    GST_DEBUG_OBJECT (sdlvideosink, "translated navigation event y "
        "coordinate from %f to %f", old_y, y);
    gst_structure_set (structure, "pointer_y", G_TYPE_DOUBLE, y, NULL);
  }

  pad = gst_pad_get_peer (GST_VIDEO_SINK_PAD (sdlvideosink));

  if (GST_IS_PAD (pad) && GST_IS_EVENT (event)) {
    gst_pad_send_event (pad, event);

    gst_object_unref (pad);
  }
}

static void
gst_sdlvideosink_navigation_init (GstNavigationInterface * iface)
{
  iface->send_event = gst_sdlvideosink_navigation_send_event;
}
