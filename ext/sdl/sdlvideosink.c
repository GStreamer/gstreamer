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

#include <gst/xoverlay/xoverlay.h>

#include "sdlvideosink.h"

/* elementfactory information */
static GstElementDetails gst_sdlvideosink_details = {
  "Video sink",
  "Sink/Video",
  "An SDL-based videosink",
  "Ronald Bultje <rbultje@ronald.bitfreak.net>",
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

static GstPadLinkReturn
gst_sdlvideosink_sinkconnect (GstPad * pad, const GstCaps * caps);
static GstCaps *gst_sdlvideosink_fixate (GstPad * pad, const GstCaps * caps);
static void gst_sdlvideosink_chain (GstPad * pad, GstData * data);

static void gst_sdlvideosink_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_sdlvideosink_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);
static GstElementStateReturn
gst_sdlvideosink_change_state (GstElement * element);


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

    sdlvideosink_type = g_type_register_static (GST_TYPE_VIDEOSINK,
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
  gulong format[6] = { GST_MAKE_FOURCC ('I', '4', '2', '0'),
    GST_MAKE_FOURCC ('Y', 'V', '1', '2'),
    GST_MAKE_FOURCC ('Y', 'U', 'Y', '2'),
    GST_MAKE_FOURCC ('Y', 'V', 'Y', 'U'),
    GST_MAKE_FOURCC ('U', 'Y', 'V', 'Y')
  };

  /* make a list of all available caps */
  capslist = gst_caps_new_empty ();
  for (i = 0; i < 5; i++) {
    gst_caps_append_structure (capslist,
        gst_structure_new ("video/x-raw-yuv",
            "format", GST_TYPE_FOURCC, format[i],
            "width", GST_TYPE_INT_RANGE, 1, G_MAXINT,
            "height", GST_TYPE_INT_RANGE, 1, G_MAXINT,
            "framerate", GST_TYPE_DOUBLE_RANGE, 1.0, 100.0, NULL));
  }

  sink_template = gst_pad_template_new ("sink",
      GST_PAD_SINK, GST_PAD_ALWAYS, capslist);

  gst_element_class_add_pad_template (element_class, sink_template);
  gst_element_class_set_details (element_class, &gst_sdlvideosink_details);
}

static void
gst_sdlvideosink_dispose (GObject * obj)
{
  g_mutex_free (GST_SDLVIDEOSINK (obj)->lock);

  if (((GObjectClass *) parent_class)->dispose)
    ((GObjectClass *) parent_class)->dispose (obj);
}

static void
gst_sdlvideosink_class_init (GstSDLVideoSinkClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstVideoSinkClass *gstvs_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstvs_class = (GstVideoSinkClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  gobject_class->set_property = gst_sdlvideosink_set_property;
  gobject_class->get_property = gst_sdlvideosink_get_property;
  gobject_class->dispose = gst_sdlvideosink_dispose;

  gstelement_class->change_state = gst_sdlvideosink_change_state;

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
  GST_VIDEOSINK_PAD (sdlvideosink) = gst_pad_new_from_template (sink_template,
      "sink");
  gst_element_add_pad (GST_ELEMENT (sdlvideosink),
      GST_VIDEOSINK_PAD (sdlvideosink));

  gst_pad_set_chain_function (GST_VIDEOSINK_PAD (sdlvideosink),
      gst_sdlvideosink_chain);
  gst_pad_set_link_function (GST_VIDEOSINK_PAD (sdlvideosink),
      gst_sdlvideosink_sinkconnect);
  gst_pad_set_fixate_function (GST_VIDEOSINK_PAD (sdlvideosink),
      gst_sdlvideosink_fixate);

  sdlvideosink->width = -1;
  sdlvideosink->height = -1;

  sdlvideosink->overlay = NULL;
  sdlvideosink->screen = NULL;

  sdlvideosink->xwindow_id = 0;

  //sdlvideosink->capslist = capslist;

  sdlvideosink->init = FALSE;

  sdlvideosink->lock = g_mutex_new ();

#if 0
  sdlvideosink->bufferpool = gst_buffer_pool_new (NULL, /* free */
      NULL,                     /* copy */
      (GstBufferPoolBufferNewFunction) gst_sdlvideosink_buffer_new, NULL,       /* buffer copy, the default is fine */
      (GstBufferPoolBufferFreeFunction) gst_sdlvideosink_buffer_free,
      sdlvideosink);
#endif

  GST_FLAG_SET (sdlvideosink, GST_ELEMENT_THREAD_SUGGESTED);
  GST_FLAG_SET (sdlvideosink, GST_ELEMENT_EVENT_AWARE);
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
    case GST_MAKE_FOURCC ('I', '4', '2', '0'):
      return SDL_IYUV_OVERLAY;
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
  if (!sdlvideosink->screen || !sdlvideosink->overlay) {
    GST_ELEMENT_ERROR (sdlvideosink, LIBRARY, TOO_LAZY, (NULL),
        ("Tried to lock screen without being set-up"));
    return FALSE;
  }

  /* Lock SDL/yuv-overlay */
  if (SDL_MUSTLOCK (sdlvideosink->screen)) {
    if (SDL_LockSurface (sdlvideosink->screen) < 0) {
      GST_ELEMENT_ERROR (sdlvideosink, LIBRARY, TOO_LAZY, (NULL),
          ("SDL: couldn't lock the SDL video window: %s", SDL_GetError ()));
      return FALSE;
    }
  }
  if (SDL_LockYUVOverlay (sdlvideosink->overlay) < 0) {
    GST_ELEMENT_ERROR (sdlvideosink, LIBRARY, TOO_LAZY, (NULL),
        ("SDL: couldn\'t lock the SDL YUV overlay: %s", SDL_GetError ()));
    return FALSE;
  }

  sdlvideosink->init = TRUE;

  return TRUE;
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
  if (sdlvideosink->init) {
    SDL_Quit ();
    sdlvideosink->init = FALSE;
  }
}

static gboolean
gst_sdlvideosink_initsdl (GstSDLVideoSink * sdlvideosink)
{
  gst_sdlvideosink_deinitsdl (sdlvideosink);

  if (!sdlvideosink->xwindow_id) {
    unsetenv ("SDL_WINDOWID");
  } else {
    char SDL_hack[32];

    sprintf (SDL_hack, "%lu", sdlvideosink->xwindow_id);
    setenv ("SDL_WINDOWID", SDL_hack, 1);
  }

  /* Initialize the SDL library */
  if (SDL_Init (SDL_INIT_VIDEO | SDL_INIT_NOPARACHUTE) < 0) {
    GST_ELEMENT_ERROR (sdlvideosink, LIBRARY, INIT, (NULL),
        ("Couldn't initialize SDL: %s", SDL_GetError ()));
    return FALSE;
  }

  return TRUE;
}

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
}

static gboolean
gst_sdlvideosink_create (GstSDLVideoSink * sdlvideosink)
{
  if (GST_VIDEOSINK_HEIGHT (sdlvideosink) <= 0)
    GST_VIDEOSINK_HEIGHT (sdlvideosink) = sdlvideosink->height;
  if (GST_VIDEOSINK_WIDTH (sdlvideosink) <= 0)
    GST_VIDEOSINK_WIDTH (sdlvideosink) = sdlvideosink->width;

  gst_sdlvideosink_destroy (sdlvideosink);

  /* create a SDL window of the size requested by the user */
  sdlvideosink->screen = SDL_SetVideoMode (GST_VIDEOSINK_WIDTH (sdlvideosink),
      GST_VIDEOSINK_HEIGHT (sdlvideosink), 0, SDL_HWSURFACE | SDL_RESIZABLE);
  if (sdlvideosink->screen == NULL) {
    GST_ELEMENT_ERROR (sdlvideosink, LIBRARY, TOO_LAZY, (NULL),
        ("SDL: Couldn't set %dx%d: %s", GST_VIDEOSINK_WIDTH (sdlvideosink),
            GST_VIDEOSINK_HEIGHT (sdlvideosink), SDL_GetError ()));
    return FALSE;
  }

  /* create a new YUV overlay */
  sdlvideosink->overlay = SDL_CreateYUVOverlay (sdlvideosink->width,
      sdlvideosink->height, sdlvideosink->format, sdlvideosink->screen);
  if (sdlvideosink->overlay == NULL) {
    GST_ELEMENT_ERROR (sdlvideosink, LIBRARY, TOO_LAZY, (NULL),
        ("SDL: Couldn't create SDL YUV overlay (%dx%d \'" GST_FOURCC_FORMAT
            "\'): %s", sdlvideosink->width, sdlvideosink->height,
            GST_FOURCC_ARGS (sdlvideosink->format), SDL_GetError ()));
    return FALSE;
  } else {
    GST_DEBUG ("Using a %dx%d %dbpp SDL screen with a %dx%d \'"
        GST_FOURCC_FORMAT "\' YUV overlay", GST_VIDEOSINK_WIDTH (sdlvideosink),
        GST_VIDEOSINK_HEIGHT (sdlvideosink),
        sdlvideosink->screen->format->BitsPerPixel, sdlvideosink->width,
        sdlvideosink->height, GST_FOURCC_ARGS (sdlvideosink->format));
  }

  sdlvideosink->rect.x = 0;
  sdlvideosink->rect.y = 0;
  sdlvideosink->rect.w = GST_VIDEOSINK_WIDTH (sdlvideosink);
  sdlvideosink->rect.h = GST_VIDEOSINK_HEIGHT (sdlvideosink);

  SDL_DisplayYUVOverlay (sdlvideosink->overlay, &(sdlvideosink->rect));

  GST_DEBUG ("sdlvideosink: setting %08x (" GST_FOURCC_FORMAT ")",
      sdlvideosink->format, GST_FOURCC_ARGS (sdlvideosink->format));

  gst_x_overlay_got_desired_size (GST_X_OVERLAY (sdlvideosink),
      GST_VIDEOSINK_WIDTH (sdlvideosink), GST_VIDEOSINK_HEIGHT (sdlvideosink));
  return TRUE;
}

static GstCaps *
gst_sdlvideosink_fixate (GstPad * pad, const GstCaps * caps)
{
  GstStructure *structure;
  GstCaps *newcaps;

  if (gst_caps_get_size (caps) > 1)
    return NULL;

  newcaps = gst_caps_copy (caps);
  structure = gst_caps_get_structure (newcaps, 0);

  if (gst_caps_structure_fixate_field_nearest_int (structure, "width", 320)) {
    return newcaps;
  }
  if (gst_caps_structure_fixate_field_nearest_int (structure, "height", 240)) {
    return newcaps;
  }
  if (gst_caps_structure_fixate_field_nearest_double (structure, "framerate",
          30.0)) {
    return newcaps;
  }

  gst_caps_free (newcaps);
  return NULL;
}

static GstPadLinkReturn
gst_sdlvideosink_sinkconnect (GstPad * pad, const GstCaps * vscapslist)
{
  GstSDLVideoSink *sdlvideosink;
  guint32 format;
  GstStructure *structure;

  sdlvideosink = GST_SDLVIDEOSINK (gst_pad_get_parent (pad));

  structure = gst_caps_get_structure (vscapslist, 0);
  gst_structure_get_fourcc (structure, "format", &format);
  sdlvideosink->format =
      gst_sdlvideosink_get_sdl_from_fourcc (sdlvideosink, format);
  gst_structure_get_int (structure, "width", &sdlvideosink->width);
  gst_structure_get_int (structure, "height", &sdlvideosink->height);

  if (!sdlvideosink->format || !gst_sdlvideosink_create (sdlvideosink))
    return GST_PAD_LINK_REFUSED;

  return GST_PAD_LINK_OK;
}


static void
gst_sdlvideosink_chain (GstPad * pad, GstData * _data)
{
  GstBuffer *buf = GST_BUFFER (_data);
  GstSDLVideoSink *sdlvideosink;
  SDL_Event sdl_event;

  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (buf != NULL);

  sdlvideosink = GST_SDLVIDEOSINK (gst_pad_get_parent (pad));

  if (GST_IS_EVENT (buf)) {
    GstEvent *event = GST_EVENT (buf);
    gint64 offset;

    switch (GST_EVENT_TYPE (event)) {
      case GST_EVENT_DISCONTINUOUS:
        offset = GST_EVENT_DISCONT_OFFSET (event, 0).value;
        /*gst_clock_handle_discont (sdlvideosink->clock,
           (guint64) GST_EVENT_DISCONT_OFFSET (event, 0).value); */
        break;
      default:
        gst_pad_event_default (pad, event);
        return;
    }
    gst_event_unref (event);
    return;
  }

  if (GST_VIDEOSINK_CLOCK (sdlvideosink) && GST_BUFFER_TIMESTAMP_IS_VALID (buf)) {
    gst_element_wait (GST_ELEMENT (sdlvideosink), GST_BUFFER_TIMESTAMP (buf));
  }

  if (GST_BUFFER_DATA (buf) != sdlvideosink->overlay->pixels[0]) {
    if (!gst_sdlvideosink_lock (sdlvideosink)) {
      return;
    }

    /* buf->yuv - FIXME: bufferpool! */
    if (sdlvideosink->format == SDL_IYUV_OVERLAY ||
        sdlvideosink->format == SDL_YV12_OVERLAY) {
      memcpy (sdlvideosink->overlay->pixels[0], GST_BUFFER_DATA (buf),
          sdlvideosink->width * sdlvideosink->height);
      memcpy (sdlvideosink->overlay->pixels[1],
          GST_BUFFER_DATA (buf) + sdlvideosink->width * sdlvideosink->height,
          sdlvideosink->width * sdlvideosink->height / 4);
      memcpy (sdlvideosink->overlay->pixels[2],
          GST_BUFFER_DATA (buf) +
          sdlvideosink->width * sdlvideosink->height * 5 / 4,
          sdlvideosink->width * sdlvideosink->height / 4);
    } else {
      memcpy (sdlvideosink->overlay->pixels[0], GST_BUFFER_DATA (buf),
          sdlvideosink->width * sdlvideosink->height * 2);
    }

    gst_sdlvideosink_unlock (sdlvideosink);
  }

  gst_buffer_unref (buf);

  /* Show, baby, show! */
  SDL_DisplayYUVOverlay (sdlvideosink->overlay, &(sdlvideosink->rect));

  while (SDL_PollEvent (&sdl_event)) {
    switch (sdl_event.type) {
      case SDL_VIDEORESIZE:
        /* create a SDL window of the size requested by the user */
        GST_VIDEOSINK_WIDTH (sdlvideosink) = sdl_event.resize.w;
        GST_VIDEOSINK_HEIGHT (sdlvideosink) = sdl_event.resize.h;
        gst_sdlvideosink_create (sdlvideosink);
        break;
    }
  }
}


static void
gst_sdlvideosink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstSDLVideoSink *sdlvideosink;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_SDLVIDEOSINK (object));
  sdlvideosink = GST_SDLVIDEOSINK (object);

  switch (prop_id) {
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

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_SDLVIDEOSINK (object));
  sdlvideosink = GST_SDLVIDEOSINK (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


static GstElementStateReturn
gst_sdlvideosink_change_state (GstElement * element)
{
  GstSDLVideoSink *sdlvideosink;

  g_return_val_if_fail (GST_IS_SDLVIDEOSINK (element), GST_STATE_FAILURE);
  sdlvideosink = GST_SDLVIDEOSINK (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_NULL_TO_READY:
      if (!gst_sdlvideosink_initsdl (sdlvideosink))
        return GST_STATE_FAILURE;
      GST_FLAG_SET (sdlvideosink, GST_SDLVIDEOSINK_OPEN);
      break;
    case GST_STATE_PAUSED_TO_READY:
      gst_sdlvideosink_destroy (sdlvideosink);
      break;
    case GST_STATE_READY_TO_NULL:
      gst_sdlvideosink_deinitsdl (sdlvideosink);
      GST_FLAG_UNSET (sdlvideosink, GST_SDLVIDEOSINK_OPEN);
      break;
    default:                   /* do nothing */
      break;
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);

  return GST_STATE_SUCCESS;
}


static gboolean
plugin_init (GstPlugin * plugin)
{
  /* Loading the library containing GstVideoSink, our parent object */
  if (!gst_library_load ("gstvideo"))
    return FALSE;

  if (!gst_element_register (plugin, "sdlvideosink", GST_RANK_NONE,
          GST_TYPE_SDLVIDEOSINK))
    return FALSE;

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "sdlvideosink",
    "SDL Video Sink", plugin_init, VERSION, "LGPL", GST_PACKAGE, GST_ORIGIN)
