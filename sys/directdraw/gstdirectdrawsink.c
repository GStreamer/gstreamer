/* GStreamer
 * Copyright (C) 2005 Sebastien Moutte <sebastien@moutte.net>
 * Copyright (C) 2007 Pioneers of the Inevitable <songbird@songbirdnest.com>
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
 *
 * The development of this code was made possible due to the involvement
 * of Pioneers of the Inevitable, the creators of the Songbird Music player
 *
 */

/**
 * SECTION:element-directdrawsink
 *
 * DirectdrawSink renders video RGB frames to any win32 window. This element
 * can receive a window ID from the application through the #XOverlay interface
 * and will then render video frames in this window.
 * If no Window ID was provided by the application, the element will create its
 * own internal window and render into it.
 *
 * <refsect2>
 * <title>Example pipelines</title>
 * |[
 * gst-launch -v videotestsrc ! directdrawsink
 * ]| a simple pipeline to test the sink
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstdirectdrawsink.h"
#include <gst/video/video.h>

GST_DEBUG_CATEGORY_STATIC (directdrawsink_debug);
#define GST_CAT_DEFAULT directdrawsink_debug

static void gst_directdraw_sink_init_interfaces (GType type);

GST_BOILERPLATE_FULL (GstDirectDrawSink, gst_directdraw_sink, GstVideoSink,
    GST_TYPE_VIDEO_SINK, gst_directdraw_sink_init_interfaces);

static void gst_directdraw_sink_finalize (GObject * object);
static void gst_directdraw_sink_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_directdraw_sink_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);
static GstCaps *gst_directdraw_sink_get_caps (GstBaseSink * bsink);
static gboolean gst_directdraw_sink_set_caps (GstBaseSink * bsink,
    GstCaps * caps);
static GstStateChangeReturn gst_directdraw_sink_change_state (GstElement *
    element, GstStateChange transition);
static GstFlowReturn gst_directdraw_sink_buffer_alloc (GstBaseSink * bsink,
    guint64 offset, guint size, GstCaps * caps, GstBuffer ** buf);
static void gst_directdraw_sink_get_times (GstBaseSink * bsink, GstBuffer * buf,
    GstClockTime * start, GstClockTime * end);
static GstFlowReturn gst_directdraw_sink_show_frame (GstBaseSink * bsink,
    GstBuffer * buf);

/* utils */
static gboolean gst_directdraw_sink_setup_ddraw (GstDirectDrawSink * ddrawsink);
static gboolean gst_directdraw_sink_create_default_window (GstDirectDrawSink *
    ddrawsink);
static gboolean gst_directdraw_sink_check_primary_surface (GstDirectDrawSink *
    ddrawsink);
static gboolean gst_directdraw_sink_check_offscreen_surface (GstDirectDrawSink *
    ddrawsink);
static GstCaps *gst_directdraw_sink_get_ddrawcaps (GstDirectDrawSink *
    ddrawsink);
static GstCaps
    * gst_directdraw_sink_create_caps_from_surfacedesc (LPDDSURFACEDESC2 desc);
static void gst_directdraw_sink_cleanup (GstDirectDrawSink * ddrawsink);
static void gst_directdraw_sink_bufferpool_clear (GstDirectDrawSink *
    ddrawsink);
static int gst_directdraw_sink_get_depth (LPDDPIXELFORMAT lpddpfPixelFormat);
static gboolean gst_ddrawvideosink_get_format_from_caps (GstDirectDrawSink *
    ddrawsink, GstCaps * caps, DDPIXELFORMAT * pPixelFormat);
static void gst_directdraw_sink_center_rect (GstDirectDrawSink * ddrawsink,
    RECT src, RECT dst, RECT * result);
static const char *DDErrorString (HRESULT hr);
static long FAR PASCAL WndProc (HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

/* surfaces management functions */
static void gst_directdraw_sink_surface_destroy (GstDirectDrawSink * ddrawsink,
    GstDDrawSurface * surface);
static GstDDrawSurface *gst_directdraw_sink_surface_create (GstDirectDrawSink *
    ddrawsink, GstCaps * caps, size_t size);
static gboolean gst_directdraw_sink_surface_check (GstDirectDrawSink *
    ddrawsink, GstDDrawSurface * surface);

static GstStaticPadTemplate directdrawsink_sink_factory =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw-rgb, "
        "framerate = (fraction) [ 0, MAX ], "
        "width = (int) [ 1, MAX ], " "height = (int) [ 1, MAX ]")
    );

enum
{
  PROP_0,
  PROP_KEEP_ASPECT_RATIO
};

/* XOverlay interface implementation */
static gboolean
gst_directdraw_sink_interface_supported (GstImplementsInterface * iface,
    GType type)
{
  if (type == GST_TYPE_X_OVERLAY)
    return TRUE;
  else if (type == GST_TYPE_NAVIGATION)
    return TRUE;
  return FALSE;
}

static void
gst_directdraw_sink_interface_init (GstImplementsInterfaceClass * klass)
{
  klass->supported = gst_directdraw_sink_interface_supported;
}

static void
gst_directdraw_sink_set_window_handle (GstXOverlay * overlay,
    guintptr window_handle)
{
  GstDirectDrawSink *ddrawsink = GST_DIRECTDRAW_SINK (overlay);

  GST_OBJECT_LOCK (ddrawsink);
  /* check if we are already using this window id */
  if (ddrawsink->video_window == (HWND) window_handle) {
    GST_OBJECT_UNLOCK (ddrawsink);
    return;
  }

  if (window_handle) {
    HRESULT hres;
    RECT rect;

    /* If we had an internal window, close it first */
    if (ddrawsink->video_window && ddrawsink->our_video_window) {
      /* Trick to let the event thread know that it has to die silently */
      ddrawsink->our_video_window = FALSE;
      /* Post quit message and wait for our event window thread */
      PostMessage (ddrawsink->video_window, WM_QUIT, 0, 0);
    }

    ddrawsink->video_window = (HWND) window_handle;
    ddrawsink->our_video_window = FALSE;

    /* Hook WndProc and user_data */
    ddrawsink->previous_user_data = (LONG_PTR) SetWindowLongPtr (
        (HWND) window_handle, GWLP_USERDATA, (LONG_PTR) ddrawsink);
    ddrawsink->previous_wndproc = (WNDPROC) SetWindowLongPtr (
        (HWND) window_handle, GWLP_WNDPROC, (LONG_PTR) WndProc);
    if (!ddrawsink->previous_wndproc)
      GST_DEBUG_OBJECT (ddrawsink, "Failed to hook previous WndProc");

    /* Get initial window size. If it changes, we will track it from the
     * WndProc. */
    GetClientRect ((HWND) window_handle, &rect);
    ddrawsink->out_width = rect.right - rect.left;
    ddrawsink->out_height = rect.bottom - rect.top;

    if (ddrawsink->setup) {
      /* update the clipper object with the new window */
      hres = IDirectDrawClipper_SetHWnd (ddrawsink->clipper, 0,
          ddrawsink->video_window);
    }
  }
  /* FIXME: Handle the case where window_handle is 0 and we want the sink to
   * create a new window when playback was already started (after set_caps) */
  GST_OBJECT_UNLOCK (ddrawsink);
}

static void
gst_directdraw_sink_expose (GstXOverlay * overlay)
{
  GstDirectDrawSink *ddrawsink = GST_DIRECTDRAW_SINK (overlay);

  gst_directdraw_sink_show_frame (GST_BASE_SINK (ddrawsink), NULL);
}

static void
gst_directdraw_sink_xoverlay_interface_init (GstXOverlayClass * iface)
{
  iface->set_window_handle = gst_directdraw_sink_set_window_handle;
  iface->expose = gst_directdraw_sink_expose;
}

static void
gst_directdraw_sink_navigation_send_event (GstNavigation * navigation,
    GstStructure * structure)
{
  GstDirectDrawSink *ddrawsink = GST_DIRECTDRAW_SINK (navigation);
  GstEvent *event;
  GstVideoRectangle src, dst, result;
  RECT rect;
  gdouble x, y, old_x, old_y, xscale = 1.0, yscale=1.0;
  GstPad *pad = NULL;

  src.w = GST_VIDEO_SINK_WIDTH (ddrawsink);
  src.h = GST_VIDEO_SINK_HEIGHT (ddrawsink);
  GetClientRect ((HWND) ddrawsink->video_window, &rect);
  ddrawsink->out_width = rect.right - rect.left;
  ddrawsink->out_height = rect.bottom - rect.top;
  dst.w = ddrawsink->out_width;
  dst.h = ddrawsink->out_height;

  event = gst_event_new_navigation (structure);

  if (ddrawsink->keep_aspect_ratio) {
    gst_video_sink_center_rect (src, dst, &result, TRUE);
  } else {
    result.x = 0;
    result.y = 0;
    result.w = dst.w;
    result.h = dst.h;
  }

  /* We calculate scaling using the original video frames geometry to include
     pixel aspect ratio scaling. */
  xscale = (gdouble) ddrawsink->video_width / result.w;
  yscale = (gdouble) ddrawsink->video_height / result.h;

  /* Converting pointer coordinates to the non scaled geometry */
  if (gst_structure_get_double (structure, "pointer_x", &old_x)) {
    x = old_x;
    x = MIN (x, result.x + result.w);
    x = MAX (x - result.x, 0);
    gst_structure_set (structure, "pointer_x", G_TYPE_DOUBLE,
        (gdouble) x * xscale, NULL);
    GST_DEBUG_OBJECT (ddrawsink,
        "translated navigation event x coordinate from %f to %f", old_x, x);
  }
  if (gst_structure_get_double (structure, "pointer_y", &old_y)) {
    y = old_y;
    y = MIN (y, result.y + result.h);
    y = MAX (y - result.y, 0);
    gst_structure_set (structure, "pointer_y", G_TYPE_DOUBLE,
        (gdouble) y * yscale, NULL);
    GST_DEBUG_OBJECT (ddrawsink,
        "translated navigation event x coordinate from %f to %f", old_y, y);
  }

  pad = gst_pad_get_peer (GST_VIDEO_SINK_PAD (ddrawsink));

  if (GST_IS_PAD (pad) && GST_IS_EVENT (event)) {
    gst_pad_send_event (pad, event);

    gst_object_unref (pad);
  }
}

static void
gst_directdraw_sink_navigation_interface_init (GstNavigationInterface * iface)
{
  iface->send_event = gst_directdraw_sink_navigation_send_event;
}

static void
gst_directdraw_sink_init_interfaces (GType type)
{
  static const GInterfaceInfo iface_info = {
    (GInterfaceInitFunc) gst_directdraw_sink_interface_init,
    NULL,
    NULL,
  };

  static const GInterfaceInfo xoverlay_info = {
    (GInterfaceInitFunc) gst_directdraw_sink_xoverlay_interface_init,
    NULL,
    NULL,
  };

  static const GInterfaceInfo navigation_info = {
    (GInterfaceInitFunc) gst_directdraw_sink_navigation_interface_init,
    NULL,
    NULL,
  };

  g_type_add_interface_static (type, GST_TYPE_IMPLEMENTS_INTERFACE,
      &iface_info);
  g_type_add_interface_static (type, GST_TYPE_X_OVERLAY, &xoverlay_info);
  g_type_add_interface_static (type, GST_TYPE_NAVIGATION, &navigation_info);
}

/* Subclass of GstBuffer which manages buffer_pool surfaces lifetime    */
static void gst_ddrawsurface_finalize (GstMiniObject * mini_object);
static GstBufferClass *ddrawsurface_parent_class = NULL;

static void
gst_ddrawsurface_init (GstDDrawSurface * surface, gpointer g_class)
{
  surface->surface = NULL;
  surface->width = 0;
  surface->height = 0;
  surface->ddrawsink = NULL;
  surface->locked = FALSE;
  surface->system_memory = FALSE;
  memset (&surface->dd_pixel_format, 0, sizeof (DDPIXELFORMAT));
}

static void
gst_ddrawsurface_class_init (gpointer g_class, gpointer class_data)
{
  GstMiniObjectClass *mini_object_class = GST_MINI_OBJECT_CLASS (g_class);

  ddrawsurface_parent_class = g_type_class_peek_parent (g_class);

  mini_object_class->finalize = GST_DEBUG_FUNCPTR (gst_ddrawsurface_finalize);
}

static GType
gst_ddrawsurface_get_type (void)
{
  static GType _gst_ddrawsurface_type;

  if (G_UNLIKELY (_gst_ddrawsurface_type == 0)) {
    static const GTypeInfo ddrawsurface_info = {
      sizeof (GstBufferClass),
      NULL,
      NULL,
      gst_ddrawsurface_class_init,
      NULL,
      NULL,
      sizeof (GstDDrawSurface),
      0,
      (GInstanceInitFunc) gst_ddrawsurface_init,
      NULL
    };
    _gst_ddrawsurface_type = g_type_register_static (GST_TYPE_BUFFER,
        "GstDDrawSurface", &ddrawsurface_info, 0);
  }
  return _gst_ddrawsurface_type;
}

static void
gst_ddrawsurface_finalize (GstMiniObject * mini_object)
{
  GstDirectDrawSink *ddrawsink = NULL;
  GstDDrawSurface *surface;

  surface = (GstDDrawSurface *) mini_object;

  ddrawsink = surface->ddrawsink;
  if (!ddrawsink)
    goto no_sink;

  /* If our geometry changed we can't reuse that image. */
  if ((surface->width != ddrawsink->video_width) ||
      (surface->height != ddrawsink->video_height) ||
      (memcmp (&surface->dd_pixel_format, &ddrawsink->dd_pixel_format,
              sizeof (DDPIXELFORMAT)) != 0 ||
          !gst_directdraw_sink_surface_check (ddrawsink, surface))
      ) {
    GST_CAT_INFO_OBJECT (directdrawsink_debug, ddrawsink,
        "destroy image as its size changed %dx%d vs current %dx%d",
        surface->width, surface->height, ddrawsink->video_width,
        ddrawsink->video_height);
    gst_directdraw_sink_surface_destroy (ddrawsink, surface);
    GST_MINI_OBJECT_CLASS (ddrawsurface_parent_class)->finalize (mini_object);
  } else {
    /* In that case we can reuse the image and add it to our image pool. */
    GST_CAT_INFO_OBJECT (directdrawsink_debug, ddrawsink,
        "recycling image in pool");

    /* need to increment the refcount again to recycle */
    gst_buffer_ref (GST_BUFFER (surface));

    g_mutex_lock (ddrawsink->pool_lock);
    ddrawsink->buffer_pool = g_slist_prepend (ddrawsink->buffer_pool, surface);
    g_mutex_unlock (ddrawsink->pool_lock);
  }

  return;

no_sink:
  GST_CAT_WARNING (directdrawsink_debug, "no sink found");
  GST_MINI_OBJECT_CLASS (ddrawsurface_parent_class)->finalize (mini_object);
  return;
}

/************************************************************************/
/* Directdraw sink functions                                            */
/************************************************************************/
static void
gst_directdraw_sink_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_static_metadata (element_class,
      "Direct Draw Video Sink", "Sink/Video",
      "Output to a video card via Direct Draw",
      "Sebastien Moutte <sebastien@moutte.net>");
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&directdrawsink_sink_factory));
}

static void
gst_directdraw_sink_class_init (GstDirectDrawSinkClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseSinkClass *gstbasesink_class;

  gobject_class = (GObjectClass *) klass;
  gstbasesink_class = (GstBaseSinkClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  GST_DEBUG_CATEGORY_INIT (directdrawsink_debug, "directdrawsink", 0,
      "Directdraw sink");

  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_directdraw_sink_finalize);
  gobject_class->get_property =
      GST_DEBUG_FUNCPTR (gst_directdraw_sink_get_property);
  gobject_class->set_property =
      GST_DEBUG_FUNCPTR (gst_directdraw_sink_set_property);
  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_directdraw_sink_change_state);
  gstbasesink_class->get_caps =
      GST_DEBUG_FUNCPTR (gst_directdraw_sink_get_caps);
  gstbasesink_class->set_caps =
      GST_DEBUG_FUNCPTR (gst_directdraw_sink_set_caps);
  gstbasesink_class->preroll =
      GST_DEBUG_FUNCPTR (gst_directdraw_sink_show_frame);
  gstbasesink_class->render =
      GST_DEBUG_FUNCPTR (gst_directdraw_sink_show_frame);
  gstbasesink_class->get_times =
      GST_DEBUG_FUNCPTR (gst_directdraw_sink_get_times);
  gstbasesink_class->buffer_alloc =
      GST_DEBUG_FUNCPTR (gst_directdraw_sink_buffer_alloc);

  /* install properties */
  /* setup aspect ratio mode */
  g_object_class_install_property (G_OBJECT_CLASS (klass),
      PROP_KEEP_ASPECT_RATIO, g_param_spec_boolean ("force-aspect-ratio",
          "Force aspect ratio",
          "When enabled, scaling will respect original aspect ratio", TRUE,
          G_PARAM_READWRITE));
}

static void
gst_directdraw_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstDirectDrawSink *ddrawsink = GST_DIRECTDRAW_SINK (object);

  switch (prop_id) {
    case PROP_KEEP_ASPECT_RATIO:
      ddrawsink->keep_aspect_ratio = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_directdraw_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstDirectDrawSink *ddrawsink = GST_DIRECTDRAW_SINK (object);

  switch (prop_id) {
    case PROP_KEEP_ASPECT_RATIO:
      g_value_set_boolean (value, ddrawsink->keep_aspect_ratio);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_directdraw_sink_finalize (GObject * object)
{
  GstDirectDrawSink *ddrawsink = GST_DIRECTDRAW_SINK (object);

  if (ddrawsink->pool_lock) {
    g_mutex_free (ddrawsink->pool_lock);
    ddrawsink->pool_lock = NULL;
  }
  if (ddrawsink->caps) {
    gst_caps_unref (ddrawsink->caps);
    ddrawsink->caps = NULL;
  }
  if (ddrawsink->setup) {
    gst_directdraw_sink_cleanup (ddrawsink);
  }

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_directdraw_sink_init (GstDirectDrawSink * ddrawsink,
    GstDirectDrawSinkClass * g_class)
{
  /*init members variables */
  ddrawsink->ddraw_object = NULL;
  ddrawsink->primary_surface = NULL;
  ddrawsink->offscreen_surface = NULL;
  ddrawsink->clipper = NULL;
  ddrawsink->video_window = NULL;
  ddrawsink->our_video_window = TRUE;
  ddrawsink->previous_wndproc = NULL;
  ddrawsink->previous_user_data = (LONG_PTR)NULL;
  ddrawsink->last_buffer = NULL;
  ddrawsink->caps = NULL;
  ddrawsink->window_thread = NULL;
  ddrawsink->setup = FALSE;
  ddrawsink->buffer_pool = NULL;
  ddrawsink->keep_aspect_ratio = FALSE;
  ddrawsink->pool_lock = g_mutex_new ();
  ddrawsink->can_blit_between_colorspace = TRUE;
  ddrawsink->must_recreate_offscreen = FALSE;
  memset (&ddrawsink->dd_pixel_format, 0, sizeof (DDPIXELFORMAT));

  /*video default values */
  ddrawsink->video_height = 0;
  ddrawsink->video_width = 0;
  ddrawsink->fps_n = 0;
  ddrawsink->fps_d = 0;
}

static GstCaps *
gst_directdraw_sink_get_caps (GstBaseSink * bsink)
{
  GstDirectDrawSink *ddrawsink = GST_DIRECTDRAW_SINK (bsink);
  GstCaps *caps = NULL;

  if (!ddrawsink->setup) {
    caps = gst_caps_copy (gst_pad_get_pad_template_caps (GST_VIDEO_SINK_PAD
            (ddrawsink)));
    GST_CAT_INFO_OBJECT (directdrawsink_debug, ddrawsink,
        "getcaps called and we are not setup yet, " "returning template %"
        GST_PTR_FORMAT, caps);
  } else {
    caps = gst_caps_ref (ddrawsink->caps);
  }

  return caps;
}

static gboolean
gst_directdraw_sink_set_caps (GstBaseSink * bsink, GstCaps * caps)
{
  GstDirectDrawSink *ddrawsink = GST_DIRECTDRAW_SINK (bsink);
  GstStructure *structure = NULL;
  gboolean ret;
  const GValue *fps;
  gint par_n, par_d;

  structure = gst_caps_get_structure (caps, 0);
  if (!structure)
    return FALSE;

  if (!gst_video_parse_caps_pixel_aspect_ratio (caps, &par_n, &par_d)) {
    par_n = 1;
    par_d = 1;
  }

  ret = gst_structure_get_int (structure, "width", &ddrawsink->video_width);
  ret &= gst_structure_get_int (structure, "height", &ddrawsink->video_height);
  fps = gst_structure_get_value (structure, "framerate");
  ret &= (fps != NULL);
  ret &=
      gst_ddrawvideosink_get_format_from_caps (ddrawsink, caps,
      &ddrawsink->dd_pixel_format);
  if (!ret) {
    GST_ELEMENT_ERROR (ddrawsink, CORE, NEGOTIATION,
        ("Failed to get caps properties from caps"), (NULL));
    return FALSE;
  }
  GST_VIDEO_SINK_WIDTH (ddrawsink) = ddrawsink->video_width * par_n / par_d;
  GST_VIDEO_SINK_HEIGHT (ddrawsink) = ddrawsink->video_height;

  ddrawsink->fps_n = gst_value_get_fraction_numerator (fps);
  ddrawsink->fps_d = gst_value_get_fraction_denominator (fps);

  /* Notify application to set window id now */
  if (!ddrawsink->video_window) {
    gst_x_overlay_prepare_xwindow_id (GST_X_OVERLAY (ddrawsink));
  }

  /* If we still don't have a window at that stage we create our own */
  if (!ddrawsink->video_window) {
    gst_directdraw_sink_create_default_window (ddrawsink);
  }

  /* if we are rendering to our own window, resize it to video size */
  if (ddrawsink->video_window && ddrawsink->our_video_window) {
    SetWindowPos (ddrawsink->video_window, NULL,
        0, 0,
        GST_VIDEO_SINK_WIDTH (ddrawsink) +
        (GetSystemMetrics (SM_CXSIZEFRAME) * 2),
        GST_VIDEO_SINK_HEIGHT (ddrawsink) + GetSystemMetrics (SM_CYCAPTION) +
        (GetSystemMetrics (SM_CYSIZEFRAME) * 2), SWP_SHOWWINDOW | SWP_NOMOVE);
  }

  /* release the surface, we have to recreate it! */
  if (ddrawsink->offscreen_surface) {
    IDirectDrawSurface7_Release (ddrawsink->offscreen_surface);
    ddrawsink->offscreen_surface = NULL;
  }

  /* create an offscreen surface with the caps */
  ret = gst_directdraw_sink_check_offscreen_surface (ddrawsink);
  if (!ret) {
    GST_ELEMENT_ERROR (ddrawsink, CORE, NEGOTIATION,
        ("Can't create a directdraw offscreen surface with the input caps"),
        (NULL));
  }

  return ret;
}

static GstStateChangeReturn
gst_directdraw_sink_change_state (GstElement * element,
    GstStateChange transition)
{
  GstDirectDrawSink *ddrawsink = GST_DIRECTDRAW_SINK (element);
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      if (!gst_directdraw_sink_setup_ddraw (ddrawsink)) {
        ret = GST_STATE_CHANGE_FAILURE;
        goto beach;
      }

      if (!(ddrawsink->caps = gst_directdraw_sink_get_ddrawcaps (ddrawsink))) {
        ret = GST_STATE_CHANGE_FAILURE;
        goto beach;
      }
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      ddrawsink->fps_n = 0;
      ddrawsink->fps_d = 1;
      ddrawsink->video_width = 0;
      ddrawsink->video_height = 0;
      if (ddrawsink->buffer_pool)
        gst_directdraw_sink_bufferpool_clear (ddrawsink);
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      if (ddrawsink->setup)
        gst_directdraw_sink_cleanup (ddrawsink);
      break;
    default:
      break;
  }

beach:
  return ret;
}

static GstFlowReturn
gst_directdraw_sink_buffer_alloc (GstBaseSink * bsink, guint64 offset,
    guint size, GstCaps * caps, GstBuffer ** buf)
{
  GstDirectDrawSink *ddrawsink = GST_DIRECTDRAW_SINK (bsink);
  GstStructure *structure;
  gint width, height;
  GstDDrawSurface *surface = NULL;
  GstFlowReturn ret = GST_FLOW_OK;
  GstCaps *buffer_caps = caps;
  gboolean buffercaps_unref = FALSE;

  GST_CAT_INFO_OBJECT (directdrawsink_debug, ddrawsink,
      "a buffer of %u bytes was requested", size);

  structure = gst_caps_get_structure (caps, 0);
  if (!gst_structure_get_int (structure, "width", &width) ||
      !gst_structure_get_int (structure, "height", &height)) {
    GST_WARNING_OBJECT (ddrawsink, "invalid caps for buffer allocation %"
        GST_PTR_FORMAT, caps);
    return GST_FLOW_UNEXPECTED;
  }

  g_mutex_lock (ddrawsink->pool_lock);

  /* Inspect our buffer pool */
  while (ddrawsink->buffer_pool) {
    surface = (GstDDrawSurface *) ddrawsink->buffer_pool->data;
    if (surface) {
      /* Removing from the pool */
      ddrawsink->buffer_pool = g_slist_delete_link (ddrawsink->buffer_pool,
          ddrawsink->buffer_pool);

      /* If the surface is invalid for our need, destroy */
      if ((surface->width != width) ||
          (surface->height != height) ||
          (memcmp (&surface->dd_pixel_format, &ddrawsink->dd_pixel_format,
                  sizeof (DDPIXELFORMAT)) ||
              !gst_directdraw_sink_surface_check (ddrawsink, surface))
          ) {
        gst_directdraw_sink_surface_destroy (ddrawsink, surface);
        gst_buffer_unref (GST_BUFFER_CAST (surface));
        surface = NULL;
      } else {
        /* We found a suitable surface */
        break;
      }
    }
  }

  if (!ddrawsink->can_blit_between_colorspace) {
    /* Hardware doesn't support blit from one colorspace to another.
     * Check if the colorspace of the current display mode has changed since
     * the last negociation. If it's the case, we will have to renegociate
     */
    guint depth;
    HRESULT hres;
    DDSURFACEDESC2 surface_desc;
    DDSURFACEDESC2 *sd;

    if (!gst_structure_get_int (structure, "depth", (gint *) & depth)) {
      GST_CAT_DEBUG_OBJECT (directdrawsink_debug, ddrawsink,
          "Can't get depth from buffer_alloc caps");
      return GST_FLOW_ERROR;
    }
    surface_desc.dwSize = sizeof (surface_desc);
    sd = &surface_desc;
    hres =
        IDirectDraw7_GetDisplayMode (ddrawsink->ddraw_object,
        (DDSURFACEDESC *) sd);
    if (hres != DD_OK) {
      GST_CAT_DEBUG_OBJECT (directdrawsink_debug, ddrawsink,
          "Can't get current display mode (error=%ld)", (glong) hres);
      return GST_FLOW_ERROR;
    }

    if (depth != gst_directdraw_sink_get_depth (&surface_desc.ddpfPixelFormat)) {
      GstCaps *copy_caps = NULL;
      GstStructure *copy_structure = NULL;
      GstCaps *display_caps = NULL;
      GstStructure *display_structure = NULL;

      /* make a copy of the original caps */
      copy_caps = gst_caps_copy (caps);
      copy_structure = gst_caps_get_structure (copy_caps, 0);

      display_caps =
          gst_directdraw_sink_create_caps_from_surfacedesc (&surface_desc);
      if (display_caps) {
        display_structure = gst_caps_get_structure (display_caps, 0);
        if (display_structure) {
          gint bpp, endianness, red_mask, green_mask, blue_mask;

          /* get new display mode properties */
          gst_structure_get_int (display_structure, "depth", (gint *) & depth);
          gst_structure_get_int (display_structure, "bpp", &bpp);
          gst_structure_get_int (display_structure, "endianness", &endianness);
          gst_structure_get_int (display_structure, "red_mask", &red_mask);
          gst_structure_get_int (display_structure, "green_mask", &green_mask);
          gst_structure_get_int (display_structure, "blue_mask", &blue_mask);

          /* apply the new display mode changes to the previous caps */
          gst_structure_set (copy_structure,
              "bpp", G_TYPE_INT, bpp,
              "depth", G_TYPE_INT, depth,
              "endianness", G_TYPE_INT, endianness,
              "red_mask", G_TYPE_INT, red_mask,
              "green_mask", G_TYPE_INT, green_mask,
              "blue_mask", G_TYPE_INT, blue_mask, NULL);

          if (gst_pad_peer_accept_caps (GST_VIDEO_SINK_PAD (ddrawsink),
                  copy_caps)) {
            buffer_caps = copy_caps;
            buffercaps_unref = TRUE;
            /* update buffer size needed to store video frames according to new caps */
            size = width * height * (bpp / 8);

            /* update our member pixel format */
            gst_ddrawvideosink_get_format_from_caps (ddrawsink, buffer_caps,
                &ddrawsink->dd_pixel_format);
            ddrawsink->must_recreate_offscreen = TRUE;

            GST_CAT_DEBUG_OBJECT (directdrawsink_debug, ddrawsink,
                " desired caps %s \n\n new caps %s", gst_caps_to_string (caps),
                gst_caps_to_string (buffer_caps));
          } else {
            GST_CAT_DEBUG_OBJECT (directdrawsink_debug, ddrawsink,
                "peer refused caps re-negociation "
                "and we can't render with the current caps.");
            ret = GST_FLOW_ERROR;
          }
        }
        gst_caps_unref (display_caps);
      }

      if (!buffercaps_unref)
        gst_caps_unref (copy_caps);
    }
  }

  /* We haven't found anything, creating a new one */
  if (!surface) {
    surface = gst_directdraw_sink_surface_create (ddrawsink, buffer_caps, size);
  }

  /* Now we should have a surface, set appropriate caps on it */
  if (surface) {
    GST_BUFFER_FLAGS (GST_BUFFER (surface)) = 0;
    gst_buffer_set_caps (GST_BUFFER (surface), buffer_caps);
  }

  g_mutex_unlock (ddrawsink->pool_lock);

  *buf = GST_BUFFER (surface);

  if (buffercaps_unref)
    gst_caps_unref (buffer_caps);

  return ret;
}

static void
gst_directdraw_sink_draw_borders (GstDirectDrawSink * ddrawsink, RECT dst_rect)
{
  RECT win_rect, fill_rect;
  POINT win_point;
  HDC hdc;

  g_return_if_fail (GST_IS_DIRECTDRAW_SINK (ddrawsink));

  /* Get the target window rect */
  win_point.x = 0;
  win_point.y = 0;
  ClientToScreen (ddrawsink->video_window, &win_point);
  GetClientRect (ddrawsink->video_window, &win_rect);
  OffsetRect (&win_rect, win_point.x, win_point.y);

  /* We acquire a drawing context */
  if ((hdc = GetDC (ddrawsink->video_window))) {
    HBRUSH brush = CreateSolidBrush (RGB (0, 0, 0));

    /* arrange for logical coordinates that match screen coordinates */
    SetWindowOrgEx (hdc, win_point.x, win_point.y, NULL);
    /* Left border */
    if (dst_rect.left > win_rect.left) {
      fill_rect.left = win_rect.left;
      fill_rect.top = win_rect.top;
      fill_rect.bottom = win_rect.bottom;
      fill_rect.right = dst_rect.left;
      FillRect (hdc, &fill_rect, brush);
    }
    /* Right border */
    if (dst_rect.right < win_rect.right) {
      fill_rect.top = win_rect.top;
      fill_rect.left = dst_rect.right;
      fill_rect.bottom = win_rect.bottom;
      fill_rect.right = win_rect.right;
      FillRect (hdc, &fill_rect, brush);
    }
    /* Top border */
    if (dst_rect.top > win_rect.top) {
      fill_rect.top = win_rect.top;
      fill_rect.left = win_rect.left;
      fill_rect.right = win_rect.right;
      fill_rect.bottom = dst_rect.top;
      FillRect (hdc, &fill_rect, brush);
    }
    /* Bottom border */
    if (dst_rect.bottom < win_rect.bottom) {
      fill_rect.top = dst_rect.bottom;
      fill_rect.left = win_rect.left;
      fill_rect.right = win_rect.right;
      fill_rect.bottom = win_rect.bottom;
      FillRect (hdc, &fill_rect, brush);
    }
    DeleteObject (brush);
    ReleaseDC (ddrawsink->video_window, hdc);
  }
}

static GstFlowReturn
gst_directdraw_sink_show_frame (GstBaseSink * bsink, GstBuffer * buf)
{
  GstDirectDrawSink *ddrawsink = GST_DIRECTDRAW_SINK (bsink);
  HRESULT hRes;
  RECT destsurf_rect, src_rect;
  POINT dest_surf_point;

  if (buf) {
    /* save a reference to the input buffer */
    gst_buffer_ref (buf);
    if (ddrawsink->last_buffer != NULL)
      gst_buffer_unref (ddrawsink->last_buffer);
    ddrawsink->last_buffer = buf;
  } else {
    /* use last buffer */
    buf = ddrawsink->last_buffer;
  }

  if (buf == NULL) {
    GST_ERROR_OBJECT (ddrawsink, "No buffer to render.");
    return GST_FLOW_ERROR;
  } else if (!ddrawsink->video_window) {
    GST_WARNING_OBJECT (ddrawsink, "No video window to render to.");
    return GST_FLOW_ERROR;
  }

  /* get the video window position */
  GST_OBJECT_LOCK (ddrawsink);
  if (G_UNLIKELY (!ddrawsink->video_window)) {
    GST_OBJECT_UNLOCK (ddrawsink);
    GST_CAT_WARNING_OBJECT (directdrawsink_debug, ddrawsink,
        "gst_directdraw_sink_show_frame our video window disappeared");
    GST_ELEMENT_ERROR (ddrawsink, RESOURCE, NOT_FOUND,
        ("Output window was closed"), (NULL));
    return GST_FLOW_ERROR;
  }
  dest_surf_point.x = 0;
  dest_surf_point.y = 0;
  ClientToScreen (ddrawsink->video_window, &dest_surf_point);
  GetClientRect (ddrawsink->video_window, &destsurf_rect);
  OffsetRect (&destsurf_rect, dest_surf_point.x, dest_surf_point.y);

  /* Check to see if we have an area to draw to.
   * When the window is minimized, it will trigger the
   * "IDirectDrawSurface7_Blt (object's offscreen surface)" warning,
   * with a msg that the rectangle is invalid */
  if (destsurf_rect.right <= destsurf_rect.left ||
      destsurf_rect.bottom <= destsurf_rect.top) {
    GST_OBJECT_UNLOCK (ddrawsink);
    GST_DEBUG_OBJECT (ddrawsink, "invalid rendering window rectangle "
        "(%ld, %ld), (%ld, %ld)", destsurf_rect.left, destsurf_rect.top,
        destsurf_rect.right, destsurf_rect.bottom);
    goto beach;
  }

  if (ddrawsink->keep_aspect_ratio) {
    /* center image to dest image keeping aspect ratio */
    src_rect.top = 0;
    src_rect.left = 0;
    src_rect.bottom = GST_VIDEO_SINK_HEIGHT (ddrawsink);
    src_rect.right = GST_VIDEO_SINK_WIDTH (ddrawsink);
    gst_directdraw_sink_center_rect (ddrawsink, src_rect, destsurf_rect,
        &destsurf_rect);
    gst_directdraw_sink_draw_borders (ddrawsink, destsurf_rect);
  }
  GST_OBJECT_UNLOCK (ddrawsink);

  if (ddrawsink->must_recreate_offscreen && ddrawsink->offscreen_surface) {
    IDirectDrawSurface7_Release (ddrawsink->offscreen_surface);
    ddrawsink->offscreen_surface = NULL;
  }

  /* check for surfaces lost */
  if (!gst_directdraw_sink_check_primary_surface (ddrawsink) ||
      !gst_directdraw_sink_check_offscreen_surface (ddrawsink)) {
    return GST_FLOW_ERROR;
  }

  if (!GST_IS_DDRAWSURFACE (buf) ||
      ((GST_IS_DDRAWSURFACE (buf)) && (GST_BUFFER (buf)->malloc_data))) {
    /* We are receiving a system memory buffer so we will copy 
       to the memory of our offscreen surface and next blit this surface 
       on the primary surface */
    LPBYTE data = NULL;
    guint src_pitch, line;
    DDSURFACEDESC2 surf_desc;
    DDSURFACEDESC2 *sd;

    ZeroMemory (&surf_desc, sizeof (surf_desc));
    surf_desc.dwSize = sizeof (surf_desc);
    sd = &surf_desc;

    /* Lock the surface */
    hRes =
        IDirectDrawSurface7_Lock (ddrawsink->offscreen_surface, NULL,
        (DDSURFACEDESC *) sd, DDLOCK_WAIT, NULL);
    if (hRes != DD_OK) {
      GST_CAT_WARNING_OBJECT (directdrawsink_debug, ddrawsink,
          "gst_directdraw_sink_show_frame failed locking surface %s",
          DDErrorString (hRes));

      if (IDirectDrawSurface7_IsLost (ddrawsink->offscreen_surface) == DD_OK)
        return GST_FLOW_OK;
      else
        return GST_FLOW_ERROR;
    }

    /* Write each line respecting the destination surface pitch */
    data = surf_desc.lpSurface;
    if (ddrawsink->video_height) {
      src_pitch = GST_BUFFER_SIZE (buf) / ddrawsink->video_height;
      for (line = 0; line < surf_desc.dwHeight; line++) {
        memcpy (data, GST_BUFFER_DATA (buf) + (line * src_pitch), src_pitch);
        data += surf_desc.lPitch;
      }
    }

    /* Unlock the surface */
    hRes = IDirectDrawSurface7_Unlock (ddrawsink->offscreen_surface, NULL);
    if (hRes != DD_OK) {
      GST_CAT_WARNING_OBJECT (directdrawsink_debug, ddrawsink,
          "gst_directdraw_sink_show_frame failed unlocking surface %s",
          DDErrorString (hRes));
      return GST_FLOW_ERROR;
    }

    /* blit to primary surface ( Blt will scale the video the dest rect surface
     * if needed */
    hRes = IDirectDrawSurface7_Blt (ddrawsink->primary_surface, &destsurf_rect,
        ddrawsink->offscreen_surface, NULL, DDBLT_WAIT, NULL);
    if (hRes != DD_OK)          /* FIXME: Is it really safe to continue past here ? */
      GST_CAT_WARNING_OBJECT (directdrawsink_debug, ddrawsink,
          "IDirectDrawSurface7_Blt (object's offscreen surface) " "returned %s",
          DDErrorString (hRes));

  } else {
    /* We are receiving a directdraw surface (previously returned by our buffer
     * pool so we will simply blit it on the primary surface */
    GstDDrawSurface *surface = NULL;

    surface = GST_DDRAWSURFACE (buf);

    /* Unlocking surface before blit */
    IDirectDrawSurface7_Unlock (surface->surface, NULL);
    surface->locked = FALSE;

    /* blit to our primary surface */
    hRes = IDirectDrawSurface7_Blt (ddrawsink->primary_surface, &destsurf_rect,
        surface->surface, NULL, DDBLT_WAIT, NULL);
    if (hRes != DD_OK)          /* FIXME: Is it really safe to continue past here ? */
      GST_CAT_WARNING_OBJECT (directdrawsink_debug, ddrawsink,
          "IDirectDrawSurface7_Blt (offscreen surface from buffer_alloc) "
          "returned %s", DDErrorString (hRes));
  }

beach:
  return GST_FLOW_OK;
}

static void
gst_directdraw_sink_get_times (GstBaseSink * bsink, GstBuffer * buf,
    GstClockTime * start, GstClockTime * end)
{
  GstDirectDrawSink *ddrawsink;

  ddrawsink = GST_DIRECTDRAW_SINK (bsink);

  if (GST_BUFFER_TIMESTAMP_IS_VALID (buf)) {
    *start = GST_BUFFER_TIMESTAMP (buf);
    if (GST_BUFFER_DURATION_IS_VALID (buf)) {
      *end = *start + GST_BUFFER_DURATION (buf);
    } else {
      if (ddrawsink->fps_n > 0) {
        *end = *start + (GST_SECOND * ddrawsink->fps_d) / ddrawsink->fps_n;
      }
    }
  }
}

/* Utility functions */

/* this function fill a DDPIXELFORMAT using Gstreamer caps */
static gboolean
gst_ddrawvideosink_get_format_from_caps (GstDirectDrawSink * ddrawsink,
    GstCaps * caps, DDPIXELFORMAT * pPixelFormat)
{
  GstStructure *structure = NULL;
  gboolean ret = TRUE;

  /* check params */
  g_return_val_if_fail (pPixelFormat, FALSE);
  g_return_val_if_fail (caps, FALSE);

  /* init structure */
  memset (pPixelFormat, 0, sizeof (DDPIXELFORMAT));
  pPixelFormat->dwSize = sizeof (DDPIXELFORMAT);

  if (!(structure = gst_caps_get_structure (caps, 0))) {
    GST_CAT_ERROR_OBJECT (directdrawsink_debug, ddrawsink,
        "can't get structure pointer from caps");
    return FALSE;
  }

  if (gst_structure_has_name (structure, "video/x-raw-rgb")) {
    gint depth, bitcount, bitmask, endianness;

    pPixelFormat->dwFlags = DDPF_RGB;
    ret &= gst_structure_get_int (structure, "bpp", &bitcount);
    pPixelFormat->dwRGBBitCount = bitcount;
    ret &= gst_structure_get_int (structure, "depth", &depth);
    ret &= gst_structure_get_int (structure, "red_mask", &bitmask);
    pPixelFormat->dwRBitMask = bitmask;
    ret &= gst_structure_get_int (structure, "green_mask", &bitmask);
    pPixelFormat->dwGBitMask = bitmask;
    ret &= gst_structure_get_int (structure, "blue_mask", &bitmask);
    pPixelFormat->dwBBitMask = bitmask;

    gst_structure_get_int (structure, "endianness", &endianness);
    if (endianness == G_BIG_ENDIAN) {
      endianness = G_LITTLE_ENDIAN;
      pPixelFormat->dwRBitMask = GUINT32_TO_BE (pPixelFormat->dwRBitMask);
      pPixelFormat->dwGBitMask = GUINT32_TO_BE (pPixelFormat->dwGBitMask);
      pPixelFormat->dwBBitMask = GUINT32_TO_BE (pPixelFormat->dwBBitMask);
    }
  } else if (gst_structure_has_name (structure, "video/x-raw-yuv")) {
    guint32 fourcc;

    pPixelFormat->dwFlags = DDPF_FOURCC;
    ret &= gst_structure_get_fourcc (structure, "format", &fourcc);
    pPixelFormat->dwFourCC = fourcc;
  } else {
    GST_CAT_WARNING_OBJECT (directdrawsink_debug, ddrawsink,
        "unknown caps name received %" GST_PTR_FORMAT, caps);
    ret = FALSE;
  }

  return ret;
}

/* This function centers the RECT of source surface to
a dest surface and set the result RECT into result */
static void
gst_directdraw_sink_center_rect (GstDirectDrawSink * ddrawsink, RECT src,
    RECT dst, RECT * result)
{
  gdouble src_ratio, dst_ratio;
  long src_width = src.right;
  long src_height = src.bottom;
  long dst_width = dst.right - dst.left;
  long dst_heigth = dst.bottom - dst.top;
  long result_width = 0, result_height = 0;

  g_return_if_fail (result != NULL);

  src_ratio = (gdouble) src_width / src_height;
  dst_ratio = (gdouble) dst_width / dst_heigth;

  if (src_ratio > dst_ratio) {
    /* new height */
    result_height = (long) (dst_width / src_ratio);

    result->left = dst.left;
    result->right = dst.right;
    result->top = dst.top + (dst_heigth - result_height) / 2;
    result->bottom = result->top + result_height;

  } else if (src_ratio < dst_ratio) {
    /* new width */
    result_width = (long) (dst_heigth * src_ratio);

    result->top = dst.top;
    result->bottom = dst.bottom;
    result->left = dst.left + (dst_width - result_width) / 2;
    result->right = result->left + result_width;

  } else {
    /* same ratio */
    memcpy (result, &dst, sizeof (RECT));
  }

  GST_CAT_INFO_OBJECT (directdrawsink_debug, ddrawsink,
      "source is %ldx%ld dest is %ldx%ld, result is %ldx%ld with x,y %ldx%ld",
      src_width, src_height, dst_width, dst_heigth,
      result->right - result->left, result->bottom - result->top, result->left,
      result->right);
}

/**
 * Get DirectDraw error message.
 * @hr: HRESULT code
 * Returns: Text representation of the error.
 */
static const char *
DDErrorString (HRESULT hr)
{
  switch (hr) {
    case DDERR_ALREADYINITIALIZED:
      return "DDERR_ALREADYINITIALIZED";
    case DDERR_CANNOTATTACHSURFACE:
      return "DDERR_CANNOTATTACHSURFACE";
    case DDERR_CANNOTDETACHSURFACE:
      return "DDERR_CANNOTDETACHSURFACE";
    case DDERR_CURRENTLYNOTAVAIL:
      return "DDERR_CURRENTLYNOTAVAIL";
    case DDERR_EXCEPTION:
      return "DDERR_EXCEPTION";
    case DDERR_GENERIC:
      return "DDERR_GENERIC";
    case DDERR_HEIGHTALIGN:
      return "DDERR_HEIGHTALIGN";
    case DDERR_INCOMPATIBLEPRIMARY:
      return "DDERR_INCOMPATIBLEPRIMARY";
    case DDERR_INVALIDCAPS:
      return "DDERR_INVALIDCAPS";
    case DDERR_INVALIDCLIPLIST:
      return "DDERR_INVALIDCLIPLIST";
    case DDERR_INVALIDMODE:
      return "DDERR_INVALIDMODE";
    case DDERR_INVALIDOBJECT:
      return "DDERR_INVALIDOBJECT";
    case DDERR_INVALIDPARAMS:
      return "DDERR_INVALIDPARAMS";
    case DDERR_INVALIDPIXELFORMAT:
      return "DDERR_INVALIDPIXELFORMAT";
    case DDERR_INVALIDRECT:
      return "DDERR_INVALIDRECT";
    case DDERR_LOCKEDSURFACES:
      return "DDERR_LOCKEDSURFACES";
    case DDERR_NO3D:
      return "DDERR_NO3D";
    case DDERR_NOALPHAHW:
      return "DDERR_NOALPHAHW";
    case DDERR_NOCLIPLIST:
      return "DDERR_NOCLIPLIST";
    case DDERR_NOCOLORCONVHW:
      return "DDERR_NOCOLORCONVHW";
    case DDERR_NOCOOPERATIVELEVELSET:
      return "DDERR_NOCOOPERATIVELEVELSET";
    case DDERR_NOCOLORKEY:
      return "DDERR_NOCOLORKEY";
    case DDERR_NOCOLORKEYHW:
      return "DDERR_NOCOLORKEYHW";
    case DDERR_NODIRECTDRAWSUPPORT:
      return "DDERR_NODIRECTDRAWSUPPORT";
    case DDERR_NOEXCLUSIVEMODE:
      return "DDERR_NOEXCLUSIVEMODE";
    case DDERR_NOFLIPHW:
      return "DDERR_NOFLIPHW";
    case DDERR_NOGDI:
      return "DDERR_NOGDI";
    case DDERR_NOMIRRORHW:
      return "DDERR_NOMIRRORHW";
    case DDERR_NOTFOUND:
      return "DDERR_NOTFOUND";
    case DDERR_NOOVERLAYHW:
      return "DDERR_NOOVERLAYHW";
    case DDERR_NORASTEROPHW:
      return "DDERR_NORASTEROPHW";
    case DDERR_NOROTATIONHW:
      return "DDERR_NOROTATIONHW";
    case DDERR_NOSTRETCHHW:
      return "DDERR_NOSTRETCHHW";
    case DDERR_NOT4BITCOLOR:
      return "DDERR_NOT4BITCOLOR";
    case DDERR_NOT4BITCOLORINDEX:
      return "DDERR_NOT4BITCOLORINDEX";
    case DDERR_NOT8BITCOLOR:
      return "DDERR_NOT8BITCOLOR";
    case DDERR_NOTEXTUREHW:
      return "DDERR_NOTEXTUREHW";
    case DDERR_NOVSYNCHW:
      return "DDERR_NOVSYNCHW";
    case DDERR_NOZBUFFERHW:
      return "DDERR_NOZBUFFERHW";
    case DDERR_NOZOVERLAYHW:
      return "DDERR_NOZOVERLAYHW";
    case DDERR_OUTOFCAPS:
      return "DDERR_OUTOFCAPS";
    case DDERR_OUTOFMEMORY:
      return "DDERR_OUTOFMEMORY";
    case DDERR_OUTOFVIDEOMEMORY:
      return "DDERR_OUTOFVIDEOMEMORY";
    case DDERR_OVERLAYCANTCLIP:
      return "DDERR_OVERLAYCANTCLIP";
    case DDERR_OVERLAYCOLORKEYONLYONEACTIVE:
      return "DDERR_OVERLAYCOLORKEYONLYONEACTIVE";
    case DDERR_PALETTEBUSY:
      return "DDERR_PALETTEBUSY";
    case DDERR_COLORKEYNOTSET:
      return "DDERR_COLORKEYNOTSET";
    case DDERR_SURFACEALREADYATTACHED:
      return "DDERR_SURFACEALREADYATTACHED";
    case DDERR_SURFACEALREADYDEPENDENT:
      return "DDERR_SURFACEALREADYDEPENDENT";
    case DDERR_SURFACEBUSY:
      return "DDERR_SURFACEBUSY";
    case DDERR_CANTLOCKSURFACE:
      return "DDERR_CANTLOCKSURFACE";
    case DDERR_SURFACEISOBSCURED:
      return "DDERR_SURFACEISOBSCURED";
    case DDERR_SURFACELOST:
      return "DDERR_SURFACELOST";
    case DDERR_SURFACENOTATTACHED:
      return "DDERR_SURFACENOTATTACHED";
    case DDERR_TOOBIGHEIGHT:
      return "DDERR_TOOBIGHEIGHT";
    case DDERR_TOOBIGSIZE:
      return "DDERR_TOOBIGSIZE";
    case DDERR_TOOBIGWIDTH:
      return "DDERR_TOOBIGWIDTH";
    case DDERR_UNSUPPORTED:
      return "DDERR_UNSUPPORTED";
    case DDERR_UNSUPPORTEDFORMAT:
      return "DDERR_UNSUPPORTEDFORMAT";
    case DDERR_UNSUPPORTEDMASK:
      return "DDERR_UNSUPPORTEDMASK";
    case DDERR_VERTICALBLANKINPROGRESS:
      return "DDERR_VERTICALBLANKINPROGRESS";
    case DDERR_WASSTILLDRAWING:
      return "DDERR_WASSTILLDRAWING";
    case DDERR_XALIGN:
      return "DDERR_XALIGN";
    case DDERR_INVALIDDIRECTDRAWGUID:
      return "DDERR_INVALIDDIRECTDRAWGUID";
    case DDERR_DIRECTDRAWALREADYCREATED:
      return "DDERR_DIRECTDRAWALREADYCREATED";
    case DDERR_NODIRECTDRAWHW:
      return "DDERR_NODIRECTDRAWHW";
    case DDERR_PRIMARYSURFACEALREADYEXISTS:
      return "DDERR_PRIMARYSURFACEALREADYEXISTS";
    case DDERR_NOEMULATION:
      return "DDERR_NOEMULATION";
    case DDERR_REGIONTOOSMALL:
      return "DDERR_REGIONTOOSMALL";
    case DDERR_CLIPPERISUSINGHWND:
      return "DDERR_CLIPPERISUSINGHWND";
    case DDERR_NOCLIPPERATTACHED:
      return "DDERR_NOCLIPPERATTACHED";
    case DDERR_NOHWND:
      return "DDERR_NOHWND";
    case DDERR_HWNDSUBCLASSED:
      return "DDERR_HWNDSUBCLASSED";
    case DDERR_HWNDALREADYSET:
      return "DDERR_HWNDALREADYSET";
    case DDERR_NOPALETTEATTACHED:
      return "DDERR_NOPALETTEATTACHED";
    case DDERR_NOPALETTEHW:
      return "DDERR_NOPALETTEHW";
    case DDERR_BLTFASTCANTCLIP:
      return "DDERR_BLTFASTCANTCLIP";
    case DDERR_NOBLTHW:
      return "DDERR_NOBLTHW";
    case DDERR_NODDROPSHW:
      return "DDERR_NODDROPSHW";
    case DDERR_OVERLAYNOTVISIBLE:
      return "DDERR_OVERLAYNOTVISIBLE";
    case DDERR_NOOVERLAYDEST:
      return "DDERR_NOOVERLAYDEST";
    case DDERR_INVALIDPOSITION:
      return "DDERR_INVALIDPOSITION";
    case DDERR_NOTAOVERLAYSURFACE:
      return "DDERR_NOTAOVERLAYSURFACE";
    case DDERR_EXCLUSIVEMODEALREADYSET:
      return "DDERR_EXCLUSIVEMODEALREADYSET";
    case DDERR_NOTFLIPPABLE:
      return "DDERR_NOTFLIPPABLE";
    case DDERR_CANTDUPLICATE:
      return "DDERR_CANTDUPLICATE";
    case DDERR_NOTLOCKED:
      return "DDERR_NOTLOCKED";
    case DDERR_CANTCREATEDC:
      return "DDERR_CANTCREATEDC";
    case DDERR_NODC:
      return "DDERR_NODC";
    case DDERR_WRONGMODE:
      return "DDERR_WRONGMODE";
    case DDERR_IMPLICITLYCREATED:
      return "DDERR_IMPLICITLYCREATED";
    case DDERR_NOTPALETTIZED:
      return "DDERR_NOTPALETTIZED";
    case DDERR_UNSUPPORTEDMODE:
      return "DDERR_UNSUPPORTEDMODE";
    case DDERR_NOMIPMAPHW:
      return "DDERR_NOMIPMAPHW";
    case DDERR_INVALIDSURFACETYPE:
      return "DDERR_INVALIDSURFACETYPE";
    case DDERR_DCALREADYCREATED:
      return "DDERR_DCALREADYCREATED";
    case DDERR_CANTPAGELOCK:
      return "DDERR_CANTPAGELOCK";
    case DDERR_CANTPAGEUNLOCK:
      return "DDERR_CANTPAGEUNLOCK";
    case DDERR_NOTPAGELOCKED:
      return "DDERR_NOTPAGELOCKED";
    case DDERR_NOTINITIALIZED:
      return "DDERR_NOTINITIALIZED";
  }
  return "Unknown Error";
}

static gboolean
gst_directdraw_sink_setup_ddraw (GstDirectDrawSink * ddrawsink)
{
  gboolean bRet = TRUE;
  HRESULT hRes;
  /* create an instance of the ddraw object use DDCREATE_EMULATIONONLY as first
   * parameter to force Directdraw to use the hardware emulation layer */
  hRes = DirectDrawCreateEx ( /*DDCREATE_EMULATIONONLY */ 0,
      (void **) &ddrawsink->ddraw_object, &IID_IDirectDraw7, NULL);
  if (hRes != DD_OK || ddrawsink->ddraw_object == NULL) {
    GST_ELEMENT_ERROR (ddrawsink, RESOURCE, WRITE,
        ("Failed to create the DirectDraw object error=%s",
            DDErrorString (hRes)), (NULL));
    return FALSE;
  }

  /* set cooperative level */
  hRes = IDirectDraw7_SetCooperativeLevel (ddrawsink->ddraw_object,
      NULL, DDSCL_NORMAL);
  if (hRes != DD_OK) {
    GST_ELEMENT_ERROR (ddrawsink, RESOURCE, WRITE,
        ("Failed to set the set the cooperative level error=%s",
            DDErrorString (hRes)), (NULL));
    return FALSE;
  }

  /* setup the clipper object */
  hRes = IDirectDraw7_CreateClipper (ddrawsink->ddraw_object, 0,
      &ddrawsink->clipper, NULL);

  if (hRes == DD_OK && ddrawsink->video_window)
    IDirectDrawClipper_SetHWnd (ddrawsink->clipper, 0, ddrawsink->video_window);

  /* create our primary surface */
  if (!gst_directdraw_sink_check_primary_surface (ddrawsink))
    return FALSE;

  /* directdraw objects are setup */
  ddrawsink->setup = TRUE;

  return bRet;
}

static LRESULT FAR PASCAL
WndProc (HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
  GstDirectDrawSink *ddrawsink;
  LRESULT ret;

  ddrawsink = (GstDirectDrawSink *) GetWindowLongPtr (hWnd, GWLP_USERDATA);

  switch (message) {
    case WM_CREATE:{
      LPCREATESTRUCT crs = (LPCREATESTRUCT) lParam;
      /* Nail pointer to the video sink down to this window */
      SetWindowLongPtr (hWnd, GWLP_USERDATA, (LONG_PTR) crs->lpCreateParams);
      break;
    }
    case WM_SIZE:
    case WM_CHAR:
    case WM_KEYDOWN:
    case WM_KEYUP:
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_MBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_RBUTTONUP:
    case WM_MBUTTONUP:
    case WM_MOUSEMOVE:{
      GstDirectDrawSink *ddrawsink;
      ddrawsink = (GstDirectDrawSink *) GetWindowLongPtr (hWnd, GWLP_USERDATA);

      if (G_UNLIKELY (!ddrawsink))
        break;

      switch (message) {
        case WM_SIZE:{
          GST_OBJECT_LOCK (ddrawsink);
          ddrawsink->out_width = LOWORD (lParam);
          ddrawsink->out_height = HIWORD (lParam);
          GST_OBJECT_UNLOCK (ddrawsink);
          GST_DEBUG_OBJECT (ddrawsink, "Window size is %dx%d", LOWORD (wParam),
              HIWORD (wParam));
          break;
        }
        case WM_CHAR:
        case WM_KEYDOWN:
        case WM_KEYUP:{
          gunichar2 wcrep[128];
          if (GetKeyNameTextW (lParam, wcrep, 128)) {
            gchar *utfrep = g_utf16_to_utf8 (wcrep, 128, NULL, NULL, NULL);
            if (utfrep) {
              if (message == WM_CHAR || message == WM_KEYDOWN)
                gst_navigation_send_key_event (GST_NAVIGATION (ddrawsink),
                    "key-press", utfrep);
              if (message == WM_CHAR || message == WM_KEYUP)
                gst_navigation_send_key_event (GST_NAVIGATION (ddrawsink),
                    "key-release", utfrep);
              g_free (utfrep);
            }
          }
          break;
        }
        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_RBUTTONDOWN:
        case WM_RBUTTONUP:
        case WM_MBUTTONDOWN:
        case WM_MBUTTONUP:
        case WM_MOUSEMOVE:{
          gint x, y, button;
          const gchar *action;

          switch (message) {
            case WM_MOUSEMOVE:
              button = 0;
              action = "mouse-move";
              break;
            case WM_LBUTTONDOWN:
              button = 1;
              action = "mouse-button-press";
              break;
            case WM_LBUTTONUP:
              button = 1;
              action = "mouse-button-release";
              break;
            case WM_RBUTTONDOWN:
              button = 2;
              action = "mouse-button-press";
              break;
            case WM_RBUTTONUP:
              button = 2;
              action = "mouse-button-release";
              break;
            case WM_MBUTTONDOWN:
              button = 3;
              action = "mouse-button-press";
              break;
            case WM_MBUTTONUP:
              button = 3;
              action = "mouse-button-release";
              break;
            default:
              button = 4;
              action = NULL;
          }

          x = LOWORD (lParam);
          y = HIWORD (lParam);

          if (button == 0) {
            GST_DEBUG_OBJECT (ddrawsink, "Mouse moved to %dx%d", x, y);
          } else
            GST_DEBUG_OBJECT (ddrawsink, "Mouse button %d pressed at %dx%d",
                button, x, y);

          if (button < 4)
            gst_navigation_send_mouse_event (GST_NAVIGATION (ddrawsink),
                action, button, x, y);

          break;
        }
      }
      break;
    }
    case WM_ERASEBKGND:
      return TRUE;
    case WM_CLOSE:
      DestroyWindow (hWnd);
    case WM_DESTROY:
      PostQuitMessage (0);
      return 0;
  }
  if (ddrawsink && ddrawsink->previous_wndproc) {
    /* If there was a previous custom WndProc, call it */

    /* Temporarily restore the previous user_data */
    if (ddrawsink->previous_user_data)
      SetWindowLongPtr ( hWnd, GWLP_USERDATA, ddrawsink->previous_user_data );

    /* Call previous WndProc */
    ret = CallWindowProc (
        ddrawsink->previous_wndproc, hWnd, message, wParam, lParam);

    /* Point the user_data back to our ddraw_sink */
    SetWindowLongPtr ( hWnd, GWLP_USERDATA, (LONG_PTR)ddrawsink );
  } else {
    /* if there was no previous custom WndProc, call Window's default one */
    ret = DefWindowProc (hWnd, message, wParam, lParam);
  }

  return ret;
}

static gpointer
gst_directdraw_sink_window_thread (GstDirectDrawSink * ddrawsink)
{
  WNDCLASS WndClass;
  MSG msg;

  memset (&WndClass, 0, sizeof (WNDCLASS));
  WndClass.style = CS_HREDRAW | CS_VREDRAW;
  WndClass.hInstance = GetModuleHandle (NULL);
  WndClass.lpszClassName = "GStreamer-DirectDraw";
  WndClass.hbrBackground = (HBRUSH) GetStockObject (BLACK_BRUSH);
  WndClass.cbClsExtra = 0;
  WndClass.cbWndExtra = 0;
  WndClass.lpfnWndProc = WndProc;
  WndClass.hCursor = LoadCursor (NULL, IDC_ARROW);
  RegisterClass (&WndClass);

  ddrawsink->video_window = CreateWindowEx (0, "GStreamer-DirectDraw",
      "GStreamer-DirectDraw sink default window",
      WS_OVERLAPPEDWINDOW | WS_SIZEBOX, 0, 0, 640, 480, NULL, NULL,
      WndClass.hInstance, (LPVOID) ddrawsink);
  if (ddrawsink->video_window == NULL)
    return NULL;

  /* Set the clipper on that window */
  IDirectDrawClipper_SetHWnd (ddrawsink->clipper, 0, ddrawsink->video_window);

  /* signal application we created a window */
  gst_x_overlay_got_window_handle (GST_X_OVERLAY (ddrawsink),
      (guintptr) ddrawsink->video_window);

  ReleaseSemaphore (ddrawsink->window_created_signal, 1, NULL);

  /* start message loop processing our default window messages */
  while (GetMessage (&msg, NULL, 0, 0) != FALSE) {
    TranslateMessage (&msg);
    DispatchMessage (&msg);
  }

  GST_CAT_LOG_OBJECT (directdrawsink_debug, ddrawsink,
      "our window received WM_QUIT or error.");
  /* The window could have changed, if it is not ours anymore we don't
   * overwrite the current video window with NULL */
  if (ddrawsink->our_video_window) {
    GST_OBJECT_LOCK (ddrawsink);
    ddrawsink->video_window = NULL;
    GST_OBJECT_UNLOCK (ddrawsink);
  }

  return NULL;
}

static gboolean
gst_directdraw_sink_create_default_window (GstDirectDrawSink * ddrawsink)
{
  ddrawsink->window_created_signal = CreateSemaphore (NULL, 0, 1, NULL);
  if (ddrawsink->window_created_signal == NULL)
    return FALSE;

  ddrawsink->window_thread = g_thread_create (
      (GThreadFunc) gst_directdraw_sink_window_thread, ddrawsink, TRUE, NULL);

  if (ddrawsink->window_thread == NULL)
    goto failed;

  /* wait maximum 10 seconds for windows creating */
  if (WaitForSingleObject (ddrawsink->window_created_signal,
          10000) != WAIT_OBJECT_0)
    goto failed;

  CloseHandle (ddrawsink->window_created_signal);
  return TRUE;

failed:
  CloseHandle (ddrawsink->window_created_signal);
  GST_ELEMENT_ERROR (ddrawsink, RESOURCE, WRITE,
      ("Error creating our default window"), (NULL));

  return FALSE;
}

static gboolean
gst_directdraw_sink_check_primary_surface (GstDirectDrawSink * ddrawsink)
{
  HRESULT hres;
  DDSURFACEDESC2 dd_surface_desc;
  DDSURFACEDESC2 *sd;

  /* if our primary surface already exist, check if it's not lost */
  if (ddrawsink->primary_surface) {
    if (IDirectDrawSurface7_IsLost (ddrawsink->primary_surface) == DD_OK) {
      /* no problem with our primary surface */
      return TRUE;
    } else {
      /* our primary surface was lost, try to restore it */
      if (IDirectDrawSurface7_Restore (ddrawsink->primary_surface) == DD_OK) {
        /* restore is done */
        GST_CAT_LOG_OBJECT (directdrawsink_debug, ddrawsink,
            "Our primary surface" " was restored after lost");
        return TRUE;
      } else {
        /* failed to restore our primary surface, 
         * probably because the display mode was changed. 
         * Release this surface and recreate a new one.
         */
        GST_CAT_LOG_OBJECT (directdrawsink_debug, ddrawsink,
            "Our primary surface"
            " was lost and display mode has changed. Destroy and recreate our surface.");
        IDirectDrawSurface7_Release (ddrawsink->primary_surface);
        ddrawsink->primary_surface = NULL;

        /* also release offscreen surface */
        IDirectDrawSurface7_Release (ddrawsink->offscreen_surface);
        ddrawsink->offscreen_surface = NULL;
      }
    }
  }

  /* create our primary surface */
  memset (&dd_surface_desc, 0, sizeof (dd_surface_desc));
  dd_surface_desc.dwSize = sizeof (dd_surface_desc);
  dd_surface_desc.dwFlags = DDSD_CAPS;
  dd_surface_desc.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;
  sd = &dd_surface_desc;
  hres =
      IDirectDraw7_CreateSurface (ddrawsink->ddraw_object, (DDSURFACEDESC *) sd,
      &ddrawsink->primary_surface, NULL);
  if (hres != DD_OK) {
    GST_ELEMENT_ERROR (ddrawsink, RESOURCE, WRITE,
        ("Failed to create our primary surface error=%s", DDErrorString (hres)),
        (NULL));
    return FALSE;
  }

  /* attach our clipper object to the new primary surface */
  if (ddrawsink->clipper) {
    hres = IDirectDrawSurface7_SetClipper (ddrawsink->primary_surface,
        ddrawsink->clipper);
  }

  return TRUE;
}

static gboolean
gst_directdraw_sink_check_offscreen_surface (GstDirectDrawSink * ddrawsink)
{
  DDSURFACEDESC2 dd_surface_desc;
  DDSURFACEDESC2 *sd;
  HRESULT hres;

  /* if our offscreen surface already exist, check if it's not lost */
  if (ddrawsink->offscreen_surface) {
    if (IDirectDrawSurface7_IsLost (ddrawsink->offscreen_surface) == DD_OK) {
      /* no problem with our offscreen surface */
      return TRUE;
    } else {
      /* our offscreen surface was lost, try to restore it */
      if (IDirectDrawSurface7_Restore (ddrawsink->offscreen_surface) == DD_OK) {
        /* restore is done */
        GST_CAT_LOG_OBJECT (directdrawsink_debug, ddrawsink,
            "Our offscreen surface" " was restored after lost");
        return TRUE;
      } else {
        /* failed to restore our offscreen surface, 
         * probably because the display mode was changed. 
         * Release this surface and recreate a new one.
         */
        GST_CAT_LOG_OBJECT (directdrawsink_debug, ddrawsink,
            "Our offscreen surface"
            " was lost and display mode has changed. Destroy and recreate our surface.");
        IDirectDrawSurface7_Release (ddrawsink->offscreen_surface);
        ddrawsink->offscreen_surface = NULL;
      }
    }
  }

  memset (&dd_surface_desc, 0, sizeof (dd_surface_desc));
  dd_surface_desc.dwSize = sizeof (dd_surface_desc);
  dd_surface_desc.dwFlags =
      DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PIXELFORMAT;
  dd_surface_desc.dwHeight = ddrawsink->video_height;
  dd_surface_desc.dwWidth = ddrawsink->video_width;
  memcpy (&(dd_surface_desc.ddpfPixelFormat), &ddrawsink->dd_pixel_format,
      sizeof (DDPIXELFORMAT));

  dd_surface_desc.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN;
  sd = &dd_surface_desc;
  hres =
      IDirectDraw7_CreateSurface (ddrawsink->ddraw_object, (DDSURFACEDESC *) sd,
      &ddrawsink->offscreen_surface, NULL);
  if (hres != DD_OK) {
    GST_CAT_WARNING_OBJECT (directdrawsink_debug, ddrawsink,
        "create_ddraw_surface:CreateSurface (offscreen surface for buffer_pool) failed %s",
        DDErrorString (hres));
    return FALSE;
  }

  ddrawsink->must_recreate_offscreen = FALSE;

  return TRUE;
}

static int
gst_directdraw_sink_get_depth (LPDDPIXELFORMAT lpddpfPixelFormat)
{
  gint order = 0, binary;

  binary =
      lpddpfPixelFormat->
      dwRBitMask | lpddpfPixelFormat->dwGBitMask | lpddpfPixelFormat->
      dwBBitMask | lpddpfPixelFormat->dwRGBAlphaBitMask;
  while (binary != 0) {
    if ((binary % 2) == 1)
      order++;
    binary = binary >> 1;
  }
  return order;
}

static HRESULT WINAPI
EnumModesCallback2 (LPDDSURFACEDESC lpDDSurfaceDesc, LPVOID lpContext)
{
  GstDirectDrawSink *ddrawsink = (GstDirectDrawSink *) lpContext;
  GstCaps *format_caps = NULL;
  LPDDSURFACEDESC2 sd;

  if (!ddrawsink || !lpDDSurfaceDesc)
    return DDENUMRET_CANCEL;

  sd = (LPDDSURFACEDESC2) lpDDSurfaceDesc;
  if ((sd->dwFlags & DDSD_PIXELFORMAT) != DDSD_PIXELFORMAT) {
    GST_CAT_INFO_OBJECT (directdrawsink_debug, ddrawsink,
        "Display mode found with DDSD_PIXELFORMAT not set");
    return DDENUMRET_OK;
  }

  if ((sd->ddpfPixelFormat.dwFlags & DDPF_RGB) != DDPF_RGB)
    return DDENUMRET_OK;

  format_caps = gst_directdraw_sink_create_caps_from_surfacedesc (sd);

  if (format_caps) {
    gst_caps_append (ddrawsink->caps, format_caps);
  }

  return DDENUMRET_OK;
}

static GstCaps *
gst_directdraw_sink_create_caps_from_surfacedesc (LPDDSURFACEDESC2 desc)
{
  GstCaps *caps = NULL;
  gint endianness = G_LITTLE_ENDIAN;
  gint depth;

  if ((desc->ddpfPixelFormat.dwFlags & DDPF_RGB) != DDPF_RGB)
    return NULL;

  depth = gst_directdraw_sink_get_depth (&desc->ddpfPixelFormat);

  if (desc->ddpfPixelFormat.dwRGBBitCount == 24 ||
      desc->ddpfPixelFormat.dwRGBBitCount == 32) {
    /* ffmpegcolorspace handles 24/32 bpp RGB as big-endian. */
    endianness = G_BIG_ENDIAN;
    desc->ddpfPixelFormat.dwRBitMask =
        GUINT32_TO_BE (desc->ddpfPixelFormat.dwRBitMask);
    desc->ddpfPixelFormat.dwGBitMask =
        GUINT32_TO_BE (desc->ddpfPixelFormat.dwGBitMask);
    desc->ddpfPixelFormat.dwBBitMask =
        GUINT32_TO_BE (desc->ddpfPixelFormat.dwBBitMask);
    if (desc->ddpfPixelFormat.dwRGBBitCount == 24) {
      desc->ddpfPixelFormat.dwRBitMask >>= 8;
      desc->ddpfPixelFormat.dwGBitMask >>= 8;
      desc->ddpfPixelFormat.dwBBitMask >>= 8;
    }
  }

  caps = gst_caps_new_simple ("video/x-raw-rgb",
      "width", GST_TYPE_INT_RANGE, 1, G_MAXINT,
      "height", GST_TYPE_INT_RANGE, 1, G_MAXINT,
      "framerate", GST_TYPE_FRACTION_RANGE, 0, 1, G_MAXINT, 1,
      "bpp", G_TYPE_INT, desc->ddpfPixelFormat.dwRGBBitCount,
      "depth", G_TYPE_INT, depth,
      "endianness", G_TYPE_INT, endianness,
      "red_mask", G_TYPE_INT, desc->ddpfPixelFormat.dwRBitMask,
      "green_mask", G_TYPE_INT, desc->ddpfPixelFormat.dwGBitMask,
      "blue_mask", G_TYPE_INT, desc->ddpfPixelFormat.dwBBitMask, NULL);

  return caps;
}

static GstCaps *
gst_directdraw_sink_get_ddrawcaps (GstDirectDrawSink * ddrawsink)
{
  HRESULT hRes = S_OK;
  DDCAPS ddcaps_hardware;
  DDCAPS ddcaps_emulation;
  GstCaps *format_caps = NULL;

  ddrawsink->caps = gst_caps_new_empty ();
  if (!ddrawsink->caps)
    return FALSE;

  /* get hardware caps */
  ddcaps_hardware.dwSize = sizeof (DDCAPS);
  ddcaps_emulation.dwSize = sizeof (DDCAPS);
  IDirectDraw7_GetCaps (ddrawsink->ddraw_object, &ddcaps_hardware,
      &ddcaps_emulation);

  /* we don't test for DDCAPS_BLTSTRETCH on the hardware as the directdraw 
   * emulation layer can do it */
  if (!(ddcaps_hardware.dwCaps & DDCAPS_BLTFOURCC)) {
    DDSURFACEDESC2 surface_desc;
    DDSURFACEDESC2 *sd;

    GST_CAT_INFO_OBJECT (directdrawsink_debug, ddrawsink,
        "hardware doesn't support blit from one colorspace to another one. "
        "so we will create a caps with only the current display mode");

    /* save blit caps */
    ddrawsink->can_blit_between_colorspace = FALSE;

    surface_desc.dwSize = sizeof (surface_desc);
    sd = &surface_desc;
    hRes =
        IDirectDraw7_GetDisplayMode (ddrawsink->ddraw_object,
        (DDSURFACEDESC *) sd);
    if (hRes != DD_OK) {
      GST_ELEMENT_ERROR (ddrawsink, CORE, NEGOTIATION,
          ("Error getting the current display mode error=%s",
              DDErrorString (hRes)), (NULL));
      return NULL;
    }

    format_caps =
        gst_directdraw_sink_create_caps_from_surfacedesc (&surface_desc);
    if (format_caps) {
      gst_caps_append (ddrawsink->caps, format_caps);
    }

    GST_CAT_INFO_OBJECT (directdrawsink_debug, ddrawsink, "returning caps %s",
        gst_caps_to_string (ddrawsink->caps));
    return ddrawsink->caps;
  }

  GST_CAT_INFO_OBJECT (directdrawsink_debug, ddrawsink,
      "the hardware can blit from one colorspace to another, "
      "then enumerate the colorspace supported by the hardware");

  /* save blit caps */
  ddrawsink->can_blit_between_colorspace = TRUE;

  /* enumerate display modes exposed by directdraw object 
     to know supported RGB modes */
  hRes =
      IDirectDraw7_EnumDisplayModes (ddrawsink->ddraw_object,
      DDEDM_REFRESHRATES, NULL, ddrawsink, EnumModesCallback2);
  if (hRes != DD_OK) {
    GST_ELEMENT_ERROR (ddrawsink, CORE, NEGOTIATION,
        ("Error enumerating display modes error=%s", DDErrorString (hRes)),
        (NULL));

    return NULL;
  }

  if (gst_caps_is_empty (ddrawsink->caps)) {
    gst_caps_unref (ddrawsink->caps);
    ddrawsink->caps = NULL;
    GST_ELEMENT_ERROR (ddrawsink, CORE, NEGOTIATION,
        ("No supported caps found."), (NULL));
    return NULL;
  }

  /*GST_CAT_INFO_OBJECT (directdrawsink_debug, ddrawsink, "returning caps %s",
   * gst_caps_to_string (ddrawsink->caps)); */

  return ddrawsink->caps;
}

/* Creates miniobject and our internal surface */
static GstDDrawSurface *
gst_directdraw_sink_surface_create (GstDirectDrawSink * ddrawsink,
    GstCaps * caps, size_t size)
{
  GstDDrawSurface *surface = NULL;
  GstStructure *structure = NULL;
  gint pitch;

#if 0
  HRESULT hRes;
#endif
  DDSURFACEDESC2 surf_desc, surf_lock_desc;

  g_return_val_if_fail (GST_IS_DIRECTDRAW_SINK (ddrawsink), NULL);

  /*init structures */
  memset (&surf_desc, 0, sizeof (surf_desc));
  memset (&surf_lock_desc, 0, sizeof (surf_desc));
  surf_desc.dwSize = sizeof (surf_desc);
  surf_lock_desc.dwSize = sizeof (surf_lock_desc);

  /*create miniobject and initialize it */
  surface = (GstDDrawSurface *) gst_mini_object_new (GST_TYPE_DDRAWSURFACE);
  surface->locked = FALSE;

  structure = gst_caps_get_structure (caps, 0);
  if (!gst_structure_get_int (structure, "width", &surface->width) ||
      !gst_structure_get_int (structure, "height", &surface->height)) {
    GST_CAT_WARNING_OBJECT (directdrawsink_debug, ddrawsink,
        "failed getting geometry from caps %" GST_PTR_FORMAT, caps);
  }

  pitch = GST_ROUND_UP_8 (size / surface->height);
  if (!gst_ddrawvideosink_get_format_from_caps (ddrawsink, caps,
          &surface->dd_pixel_format)) {
    GST_CAT_WARNING_OBJECT (directdrawsink_debug, ddrawsink,
        "failed getting pixel format from caps %" GST_PTR_FORMAT, caps);
  }

  /* disable return of directdraw surface to buffer alloc because actually I
   * have no solution to handle display mode changes. The problem is that when
   * the display mode is changed surface's memory is freed then the upstream
   * filter would crash trying to write to this memory. Directdraw has a system
   * lock (DDLOCK_NOSYSLOCK to disable it) to prevent display mode changes 
   * when a surface memory is locked but we need to disable this lock to return
   * multiple buffers (surfaces) and do not lock directdraw API calls.
   */
#if 0
/*  if (ddrawsink->ddraw_object) {*/
  /* Creating an internal surface which will be used as GstBuffer, we used
     the detected pixel format and video dimensions */

  surf_desc.ddsCaps.dwCaps =
      DDSCAPS_OFFSCREENPLAIN /* | DDSCAPS_SYSTEMMEMORY */ ;
  surf_desc.dwFlags =
      DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PIXELFORMAT | DDSD_PITCH;
  surf_desc.dwHeight = surface->height;
  surf_desc.dwWidth = surface->width;
  memcpy (&(surf_desc.ddpfPixelFormat), &surface->dd_pixel_format,
      sizeof (DDPIXELFORMAT));

  hRes = IDirectDraw7_CreateSurface (ddrawsink->ddraw_object, &surf_desc,
      &surface->surface, NULL);
  if (hRes != DD_OK) {
    goto surface_pitch_bad;
  }

  /* Locking the surface to acquire the memory pointer.
     Use DDLOCK_NOSYSLOCK to disable syslock which can cause a deadlock 
     if directdraw api is used while a buffer is lock */
lock:
  hRes = IDirectDrawSurface7_Lock (surface->surface, NULL, &surf_lock_desc,
      DDLOCK_WAIT | DDLOCK_NOSYSLOCK, NULL);
  if (hRes == DDERR_SURFACELOST) {
    IDirectDrawSurface7_Restore (surface->surface);
    goto lock;
  }
  surface->locked = TRUE;

  if (surf_lock_desc.lPitch != pitch) {
    GST_CAT_INFO_OBJECT (directdrawsink_debug, ddrawsink,
        "DDraw stride/pitch %ld isn't as expected value %d, let's continue allocating a system memory buffer.",
        surf_lock_desc.lPitch, pitch);

    /*Unlock the surface as we will change it to use system memory with a GStreamer compatible pitch */
    hRes = IDirectDrawSurface_Unlock (surface->surface, NULL);
    goto surface_pitch_bad;
  }
  GST_BUFFER_DATA (surface) = surf_lock_desc.lpSurface;
  GST_BUFFER_SIZE (surface) = surf_lock_desc.lPitch * surface->height;
  GST_CAT_INFO_OBJECT (directdrawsink_debug, ddrawsink,
      "allocating a surface of %d bytes (stride=%ld)\n", size,
      surf_lock_desc.lPitch);

surface_pitch_bad:
#else
  GST_BUFFER (surface)->malloc_data = g_malloc (size);
  GST_BUFFER_DATA (surface) = GST_BUFFER (surface)->malloc_data;
  GST_BUFFER_SIZE (surface) = size;
  surface->surface = NULL;
  GST_CAT_INFO_OBJECT (directdrawsink_debug, ddrawsink,
      "allocating a system memory buffer of %" G_GSIZE_FORMAT " bytes", size);

#endif

  /* Keep a ref to our sink */
  surface->ddrawsink = gst_object_ref (ddrawsink);

  return surface;
}

/* We are called from the finalize method of miniobject, the object will be
 * destroyed so we just have to clean our internal stuff */
static void
gst_directdraw_sink_surface_destroy (GstDirectDrawSink * ddrawsink,
    GstDDrawSurface * surface)
{
  g_return_if_fail (GST_IS_DIRECTDRAW_SINK (ddrawsink));

  /* Release our internal surface */
  if (surface->surface) {
    if (surface->locked) {
      IDirectDrawSurface7_Unlock (surface->surface, NULL);
      surface->locked = FALSE;
    }
    IDirectDrawSurface7_Release (surface->surface);
    surface->surface = NULL;
  }

  if (GST_BUFFER (surface)->malloc_data) {
    g_free (GST_BUFFER (surface)->malloc_data);
    GST_BUFFER (surface)->malloc_data = NULL;
  }

  if (!surface->ddrawsink) {
    goto no_sink;
  }

  /* Release the ref to our sink */
  surface->ddrawsink = NULL;
  gst_object_unref (ddrawsink);

  return;

no_sink:
  GST_WARNING ("no sink found in surface");
  return;
}

static gboolean
gst_directdraw_sink_surface_check (GstDirectDrawSink * ddrawsink,
    GstDDrawSurface * surface)
{
  if (!surface->surface)
    return TRUE;                /* system memory buffer */

  if (IDirectDrawSurface7_IsLost (surface->surface) == DD_OK) {
    /* no problem with this surface */
    return TRUE;
  } else {
    /* this surface was lost, try to restore it */
    if (IDirectDrawSurface7_Restore (ddrawsink->offscreen_surface) == DD_OK) {
      /* restore is done */
      GST_CAT_LOG_OBJECT (directdrawsink_debug, ddrawsink, "A surface from our"
          " bufferpool was restored after lost");
      return TRUE;
    }
  }

  return FALSE;
}

static void
gst_directdraw_sink_bufferpool_clear (GstDirectDrawSink * ddrawsink)
{
  g_mutex_lock (ddrawsink->pool_lock);
  while (ddrawsink->buffer_pool) {
    GstDDrawSurface *surface = ddrawsink->buffer_pool->data;

    ddrawsink->buffer_pool = g_slist_delete_link (ddrawsink->buffer_pool,
        ddrawsink->buffer_pool);
    gst_directdraw_sink_surface_destroy (ddrawsink, surface);
    gst_buffer_unref (GST_BUFFER_CAST (surface));
  }
  g_mutex_unlock (ddrawsink->pool_lock);
}

static void
gst_directdraw_sink_cleanup (GstDirectDrawSink * ddrawsink)
{
  /* Post quit message and wait for our event window thread */
  if (ddrawsink->video_window && ddrawsink->our_video_window)
    PostMessage (ddrawsink->video_window, WM_QUIT, 0, 0);

  if (ddrawsink->window_thread) {
    g_thread_join (ddrawsink->window_thread);
    ddrawsink->window_thread = NULL;
  }

  if (ddrawsink->buffer_pool) {
    gst_directdraw_sink_bufferpool_clear (ddrawsink);
    ddrawsink->buffer_pool = NULL;
  }

  if (ddrawsink->offscreen_surface) {
    IDirectDrawSurface7_Release (ddrawsink->offscreen_surface);
    ddrawsink->offscreen_surface = NULL;
  }

  if (ddrawsink->clipper) {
    IDirectDrawClipper_Release (ddrawsink->clipper);
    ddrawsink->clipper = NULL;
  }

  if (ddrawsink->primary_surface) {
    IDirectDrawSurface7_Release (ddrawsink->primary_surface);
    ddrawsink->primary_surface = NULL;
  }

  if (ddrawsink->ddraw_object) {
    IDirectDraw7_Release (ddrawsink->ddraw_object);
    ddrawsink->ddraw_object = NULL;
  }

  if (ddrawsink->last_buffer) {
    gst_buffer_unref (ddrawsink->last_buffer);
    ddrawsink->last_buffer = NULL;
  }

  ddrawsink->setup = FALSE;
}
