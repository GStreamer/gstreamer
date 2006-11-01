/* GStreamer
* Copyright (C) 2005 Sebastien Moutte <sebastien@moutte.net>
*	
* Based on directfb video sink
* gstdirectdrawsink.c:
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstdirectdrawsink.h"

#include <fcntl.h>
#include <errno.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <string.h>

GST_DEBUG_CATEGORY_STATIC (directdrawsink_debug);
#define GST_CAT_DEFAULT directdrawsink_debug

/* elementfactory information */
static const GstElementDetails gst_directdrawsink_details =
GST_ELEMENT_DETAILS ("Video Sink (DIRECTDRAW)",
    "Sink/Video",
    "Output to a video card via DIRECTDRAW",
    "Sebastien Moutte <sebastien@moutte.net>");

GST_BOILERPLATE (GstDirectDrawSink, gst_directdrawsink, GstVideoSink,
    GST_TYPE_VIDEO_SINK);

static void gst_directdrawsink_finalize (GObject * object);

static void gst_directdrawsink_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_directdrawsink_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static GstCaps *gst_directdrawsink_get_caps (GstBaseSink * bsink);
static gboolean gst_directdrawsink_set_caps (GstBaseSink * bsink,
    GstCaps * caps);

static GstStateChangeReturn
gst_directdrawsink_change_state (GstElement * element,
    GstStateChange transition);

static GstFlowReturn
gst_directdrawsink_buffer_alloc (GstBaseSink * bsink, guint64 offset,
    guint size, GstCaps * caps, GstBuffer ** buf);

static void
gst_directdrawsink_get_times (GstBaseSink * bsink, GstBuffer * buf,
    GstClockTime * start, GstClockTime * end);

static GstFlowReturn
gst_directdrawsink_show_frame (GstBaseSink * bsink, GstBuffer * buf);

static gboolean gst_directdrawsink_setup_ddraw (GstDirectDrawSink * ddrawsink);
static gboolean gst_directdrawsink_create_default_window (GstDirectDrawSink *
    ddrawsink);
static gboolean gst_directdrawsink_create_ddraw_surfaces (GstDirectDrawSink *
    ddrawsink);

static GstCaps *gst_directdrawsink_get_ddrawcaps (GstDirectDrawSink *
    ddrawsink);

static void gst_directdrawsink_cleanup (GstDirectDrawSink * ddrawsink);
static void gst_directdrawsink_bufferpool_clear (GstDirectDrawSink * ddrawsink);


/*surfaces management functions*/
static void
gst_directdrawsink_surface_destroy (GstDirectDrawSink * ddrawsink,
    GstDDrawSurface * surface);

static GstDDrawSurface *gst_directdrawsink_surface_create (GstDirectDrawSink *
    ddrawsink, GstCaps * caps, size_t size);

static GstStaticPadTemplate directdrawsink_sink_factory =
    GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw-rgb, "
        "bpp = (int) { 8, 16, 24, 32 }, "
        "depth = (int) { 0, 8, 16, 24, 32 }, "
        "endianness = (int) LITTLE_ENDIAN, "
        "framerate = (fraction) [ 0, MAX ], "
        "width = (int) [ 1, MAX ], "
        "height = (int) [ 1, MAX ]"
        "; "
        "video/x-raw-yuv, "
        "framerate = (fraction) [ 0, MAX ], "
        "width = (int) [ 1, MAX ], "
        "height = (int) [ 1, MAX ], "
        "format = (fourcc) { YUY2, UYVY, YVU9, YV12, AYUV }")
    );

enum
{
  PROP_0,
  PROP_SURFACE,
  PROP_WINDOW,
  PROP_FULLSCREEN,
  PROP_KEEP_ASPECT_RATIO
};

/* Utility functions */
static gboolean
gst_ddrawvideosink_get_format_from_caps (GstCaps * caps,
    DDPIXELFORMAT * pPixelFormat)
{
  GstStructure *structure = NULL;
  gboolean ret = TRUE;

  /*check params */
  g_return_val_if_fail (pPixelFormat, FALSE);
  g_return_val_if_fail (caps, FALSE);

  /*init structure */
  memset (pPixelFormat, 0, sizeof (DDPIXELFORMAT));
  pPixelFormat->dwSize = sizeof (DDPIXELFORMAT);

  if (!(structure = gst_caps_get_structure (caps, 0)))
    return FALSE;

  if (gst_structure_has_name (structure, "video/x-raw-rgb")) {
    gint depth, bitcount, bitmask;

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
  } else if (gst_structure_has_name (structure, "video/x-raw-yuv")) {
    gint fourcc;

    pPixelFormat->dwFlags = DDPF_FOURCC;
    ret &= gst_structure_get_fourcc (structure, "format", &fourcc);
    pPixelFormat->dwFourCC = fourcc;
  } else {
    GST_CAT_WARNING (directdrawsink_debug,
        "unknown caps name received %" GST_PTR_FORMAT, caps);
    ret = FALSE;
  }

  return ret;
}

static GstCaps *
gst_ddrawvideosink_get_caps_from_format (DDPIXELFORMAT pixel_format)
{
  GstCaps *caps = NULL;
  gint bpp, depth;
  guint32 fourcc;

  if ((pixel_format.dwFlags & DDPF_RGB) == DDPF_RGB) {
    bpp = pixel_format.dwRGBBitCount;
    if (bpp != 32)
      depth = bpp;
    else {
      if ((pixel_format.dwFlags & DDPF_ALPHAPREMULT) == DDPF_ALPHAPREMULT)
        depth = 32;
      else
        depth = 24;
    }
    caps = gst_caps_new_simple ("video/x-raw-rgb",
        "bpp", G_TYPE_INT, bpp,
        "depth", G_TYPE_INT, depth,
        "endianness", G_TYPE_INT, G_LITTLE_ENDIAN, NULL);
  }

  if ((pixel_format.dwFlags & DDPF_YUV) == DDPF_YUV) {
    fourcc = pixel_format.dwFourCC;
    caps = gst_caps_new_simple ("video/x-raw-yuv",
        "format", GST_TYPE_FOURCC, fourcc, NULL);
  }

  g_assert (caps != NULL);

  return caps;
}

static void
gst_directdrawsink_center_rect (RECT src, RECT dst, RECT * result)
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
    /*new height */
    result_height = (long) (dst_width / src_ratio);

    result->left = dst.left;
    result->right = dst.right;
    result->top = dst.top + (dst_heigth - result_height) / 2;
    result->bottom = result->top + result_height;

  } else if (src_ratio < dst_ratio) {
    /*new width */
    result_width = (long) (dst_heigth * src_ratio);

    result->top = dst.top;
    result->bottom = dst.bottom;
    result->left = dst.left + (dst_width - result_width) / 2;
    result->right = result->left + result_width;

  } else {
    /*same ratio */
    memcpy (result, &dst, sizeof (RECT));
  }

  GST_CAT_INFO (directdrawsink_debug,
      "source is %dx%d dest is %dx%d, result is %dx%d with x,y %dx%d",
      src_width, src_height, dst_width, dst_heigth,
      result->right - result->left, result->bottom - result->top, result->left,
      result->right);
}

/*subclass of GstBuffer which manages surfaces lifetime*/
static void
gst_ddrawsurface_finalize (GstDDrawSurface * surface)
{
  GstDirectDrawSink *ddrawsink = NULL;

  g_return_if_fail (surface != NULL);

  ddrawsink = surface->ddrawsink;
  if (!ddrawsink)
    goto no_sink;

  /* If our geometry changed we can't reuse that image. */
  if ((surface->width != ddrawsink->video_width) ||
      (surface->height != ddrawsink->video_height) ||
      (memcmp (&surface->dd_pixel_format, &ddrawsink->dd_pixel_format,
              sizeof (DDPIXELFORMAT)) != 0)
      ) {
    GST_CAT_INFO (directdrawsink_debug,
        "destroy image as its size changed %dx%d vs current %dx%d",
        surface->width, surface->height, ddrawsink->video_width,
        ddrawsink->video_height);
    gst_directdrawsink_surface_destroy (ddrawsink, surface);

  } else {
    /* In that case we can reuse the image and add it to our image pool. */
    GST_CAT_INFO (directdrawsink_debug, "recycling image in pool");

    /* need to increment the refcount again to recycle */
    gst_buffer_ref (GST_BUFFER (surface));

    g_mutex_lock (ddrawsink->pool_lock);
    ddrawsink->buffer_pool = g_slist_prepend (ddrawsink->buffer_pool, surface);
    g_mutex_unlock (ddrawsink->pool_lock);
  }
  return;

no_sink:
  GST_CAT_WARNING (directdrawsink_debug, "no sink found");
  return;
}

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

  mini_object_class->finalize = (GstMiniObjectFinalizeFunction)
      gst_ddrawsurface_finalize;
}

GType
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

/* FIXME: this is problematic if there is more than one sink instance at the
 * same time, surely there exists a better solution than this? */
/* static GstDirectDrawSink *global_ddrawsink = NULL; */

/*GType
gst_directdrawsink_get_type (void)
{
  static GType directdrawsink_type = 0;

  if (!directdrawsink_type) {
    static const GTypeInfo directdrawsink_info = {
      sizeof (GstDirectDrawSinkClass),
      gst_directdrawsink_base_init,
      NULL,
      (GClassInitFunc) gst_directdrawsink_class_init,
      NULL,
      NULL,
      sizeof (GstDirectDrawSink),
      0,
      (GInstanceInitFunc) gst_directdrawsink_init,
    };

    directdrawsink_type =
        g_type_register_static (GST_TYPE_VIDEO_SINK, "GstDirectDrawSink",
        &directdrawsink_info, 0);
  }

  return directdrawsink_type;
}
*/
static void
gst_directdrawsink_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details (element_class, &gst_directdrawsink_details);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&directdrawsink_sink_factory));
}

static void
gst_directdrawsink_class_init (GstDirectDrawSinkClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseSinkClass *gstbasesink_class;

  gobject_class = (GObjectClass *) klass;
  gstbasesink_class = (GstBaseSinkClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  GST_DEBUG_CATEGORY_INIT (directdrawsink_debug, "directdrawsink", 0,
      "Direct draw sink");

  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_directdrawsink_finalize);

  gobject_class->get_property =
      GST_DEBUG_FUNCPTR (gst_directdrawsink_get_property);
  gobject_class->set_property =
      GST_DEBUG_FUNCPTR (gst_directdrawsink_set_property);

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_directdrawsink_change_state);
  gstbasesink_class->get_caps = GST_DEBUG_FUNCPTR (gst_directdrawsink_get_caps);
  gstbasesink_class->set_caps = GST_DEBUG_FUNCPTR (gst_directdrawsink_set_caps);
  gstbasesink_class->preroll =
      GST_DEBUG_FUNCPTR (gst_directdrawsink_show_frame);
  gstbasesink_class->render = GST_DEBUG_FUNCPTR (gst_directdrawsink_show_frame);

  gstbasesink_class->get_times =
      GST_DEBUG_FUNCPTR (gst_directdrawsink_get_times);
  gstbasesink_class->buffer_alloc =
      GST_DEBUG_FUNCPTR (gst_directdrawsink_buffer_alloc);

  /*install properties */
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_FULLSCREEN,
      g_param_spec_boolean ("fullscreen", "fullscreen",
          "boolean to activate fullscreen", FALSE, G_PARAM_READWRITE));

  /*extern window where we will display the video */
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_WINDOW,
      g_param_spec_long ("window", "Window",
          "The target window for video", G_MINLONG, G_MAXLONG, 0,
          G_PARAM_WRITABLE));

  /*extern surface where we will blit the video */
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_SURFACE,
      g_param_spec_pointer ("surface", "Surface",
          "The target surface for video", G_PARAM_WRITABLE));

  /*setup aspect ratio mode */
  g_object_class_install_property (G_OBJECT_CLASS (klass),
      PROP_KEEP_ASPECT_RATIO, g_param_spec_boolean ("keep-aspect-ratio",
          "keep-aspect-ratio", "boolean to video keep aspect ratio", FALSE,
          G_PARAM_READWRITE));

  /*should add a color_key property to permit applications to define the color used for overlays */
}

static void
gst_directdrawsink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstDirectDrawSink *ddrawsink;

  ddrawsink = GST_DIRECTDRAW_SINK (object);

  switch (prop_id) {
    case PROP_SURFACE:
      ddrawsink->extern_surface = g_value_get_pointer (value);
      break;
    case PROP_WINDOW:
      ddrawsink->video_window = (HWND) g_value_get_long (value);
      ddrawsink->resize_window = FALSE;
      break;
    case PROP_FULLSCREEN:
      /*ddrawsink->fullscreen = g_value_get_boolean (value); not working .... */
      break;
    case PROP_KEEP_ASPECT_RATIO:
      ddrawsink->keep_aspect_ratio = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_directdrawsink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstDirectDrawSink *ddrawsink;

  ddrawsink = GST_DIRECTDRAW_SINK (object);

  switch (prop_id) {
    case PROP_FULLSCREEN:
      g_value_set_boolean (value, ddrawsink->fullscreen);
      break;
    case PROP_KEEP_ASPECT_RATIO:
      g_value_set_boolean (value, ddrawsink->keep_aspect_ratio);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_directdrawsink_finalize (GObject * object)
{
  GstDirectDrawSink *ddrawsink;

  ddrawsink = GST_DIRECTDRAW_SINK (object);

  if (ddrawsink->pool_lock) {
    g_mutex_free (ddrawsink->pool_lock);
    ddrawsink->pool_lock = NULL;
  }
  if (ddrawsink->setup) {
    gst_directdrawsink_cleanup (ddrawsink);
  }
}

static void
gst_directdrawsink_init (GstDirectDrawSink * ddrawsink,
    GstDirectDrawSinkClass * g_class)
{
  /*init members variables */
  ddrawsink->ddraw_object = NULL;
  ddrawsink->primary_surface = NULL;
  ddrawsink->overlays = NULL;
  ddrawsink->clipper = NULL;
  ddrawsink->extern_surface = NULL;

  /*video default values */
  ddrawsink->video_height = 0;
  ddrawsink->video_width = 0;
  ddrawsink->fps_n = 0;
  ddrawsink->fps_d = 0;

  memset (&ddrawsink->dd_pixel_format, 0, sizeof (DDPIXELFORMAT));

  ddrawsink->caps = NULL;

  ddrawsink->window_thread = NULL;

  ddrawsink->bUseOverlay = FALSE;
  ddrawsink->color_key = 0;     /*need to be a public property and may be we can enable overlays when this property is set ... */

  ddrawsink->fullscreen = FALSE;
  ddrawsink->setup = FALSE;

  ddrawsink->display_modes = NULL;
  ddrawsink->buffer_pool = NULL;

  ddrawsink->resize_window = TRUE;      /*resize only our internal window to the video size */

  ddrawsink->pool_lock = g_mutex_new ();

  ddrawsink->keep_aspect_ratio = TRUE;
/*  ddrawsink->can_blit = TRUE;*/
}

static GstCaps *
gst_directdrawsink_get_caps (GstBaseSink * bsink)
{
  GstDirectDrawSink *ddrawsink;
  GstCaps *caps = NULL;

  ddrawsink = GST_DIRECTDRAW_SINK (bsink);

  if (!ddrawsink->setup) {
    caps = gst_caps_copy (gst_pad_get_pad_template_caps (GST_VIDEO_SINK_PAD
            (ddrawsink)));

    GST_CAT_INFO (directdrawsink_debug,
        "getcaps called and we are not setup yet, " "returning template %"
        GST_PTR_FORMAT, caps);
  } else {
    /*if (ddrawsink->extern_surface) {
     * We are not rendering to our own surface, returning this surface's
     *  pixel format *
     GST_WARNING ("using extern surface");
     caps = gst_ddrawvideosink_get_caps_from_format (ddrawsink->dd_pixel_format);
     } else */

    /* i think we can't really use the format of the extern surface as the application owning the surface doesn't know
       the format we will render. But we need to use overlays to overlay any format on the extern surface */
    caps = gst_caps_ref (ddrawsink->caps);
  }

  return caps;
}

static gboolean
gst_directdrawsink_set_caps (GstBaseSink * bsink, GstCaps * caps)
{
  GstDirectDrawSink *ddrawsink;
  GstStructure *structure = NULL;
  gboolean ret;
  const GValue *fps;

  ddrawsink = GST_DIRECTDRAW_SINK (bsink);

  structure = gst_caps_get_structure (caps, 0);
  if (!structure)
    return FALSE;

  ret = gst_structure_get_int (structure, "width", &ddrawsink->video_width);
  ret &= gst_structure_get_int (structure, "height", &ddrawsink->video_height);

  fps = gst_structure_get_value (structure, "framerate");
  ret &= (fps != NULL);

  ret &=
      gst_ddrawvideosink_get_format_from_caps (caps,
      &ddrawsink->dd_pixel_format);

  if (!ret)
    return FALSE;

  ddrawsink->fps_n = gst_value_get_fraction_numerator (fps);
  ddrawsink->fps_d = gst_value_get_fraction_denominator (fps);

  if (ddrawsink->video_window && ddrawsink->resize_window) {
    SetWindowPos (ddrawsink->video_window, NULL,
        0, 0, ddrawsink->video_width + (GetSystemMetrics (SM_CXSIZEFRAME) * 2),
        ddrawsink->video_height + GetSystemMetrics (SM_CYCAPTION) +
        (GetSystemMetrics (SM_CYSIZEFRAME) * 2), SWP_SHOWWINDOW | SWP_NOMOVE);
  }

  /*create overlays flipping chain */
  ret = gst_directdrawsink_create_ddraw_surfaces (ddrawsink);
  if (!ret && ddrawsink->bUseOverlay) {
    GST_CAT_WARNING (directdrawsink_debug,
        "Can not create overlay surface, reverting to no overlay display");
    ddrawsink->bUseOverlay = FALSE;
    ret = gst_directdrawsink_create_ddraw_surfaces (ddrawsink);
    if (ret) {
      return TRUE;
    }

    /*could not create draw surfaces even with fallback, so leave
       everything as is */
    ddrawsink->bUseOverlay = TRUE;
  }
  if (!ret) {
    GST_CAT_ERROR (directdrawsink_debug, "Can not create ddraw surface");
  }
  return ret;
}

static GstStateChangeReturn
gst_directdrawsink_change_state (GstElement * element,
    GstStateChange transition)
{
  GstDirectDrawSink *ddrawsink;

  ddrawsink = GST_DIRECTDRAW_SINK (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      if (ddrawsink->video_window == NULL && ddrawsink->extern_surface == NULL)
        if (!gst_directdrawsink_create_default_window (ddrawsink))
          return GST_STATE_CHANGE_FAILURE;

      if (!gst_directdrawsink_setup_ddraw (ddrawsink))
        return GST_STATE_CHANGE_FAILURE;

      if (!(ddrawsink->caps = gst_directdrawsink_get_ddrawcaps (ddrawsink)))
        return GST_STATE_CHANGE_FAILURE;

      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      break;
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:

      ddrawsink->fps_n = 0;
      ddrawsink->fps_d = 1;
      ddrawsink->video_width = 0;
      ddrawsink->video_height = 0;

      if (ddrawsink->buffer_pool)
        gst_directdrawsink_bufferpool_clear (ddrawsink);

      break;
    case GST_STATE_CHANGE_READY_TO_NULL:

      if (ddrawsink->setup)
        gst_directdrawsink_cleanup (ddrawsink);

      break;
  }

  return GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
}

/**
 * Get DirectDraw error message.
 * @hr: HRESULT code
 * Returns: Text representation of the error.
 */
char *
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


static GstFlowReturn
gst_directdrawsink_buffer_alloc (GstBaseSink * bsink, guint64 offset,
    guint size, GstCaps * caps, GstBuffer ** buf)
{
  GstDirectDrawSink *ddrawsink = NULL;
  GstDDrawSurface *surface = NULL;
  GstStructure *structure = NULL;
  GstFlowReturn ret = GST_FLOW_OK;

  ddrawsink = GST_DIRECTDRAW_SINK (bsink);
  GST_CAT_INFO (directdrawsink_debug, "a buffer of %d bytes was requested",
      size);

  structure = gst_caps_get_structure (caps, 0);

  g_mutex_lock (ddrawsink->pool_lock);

  /* Inspect our buffer pool */
  while (ddrawsink->buffer_pool) {
    surface = (GstDDrawSurface *) ddrawsink->buffer_pool->data;

    if (surface) {
      /* Removing from the pool */
      ddrawsink->buffer_pool = g_slist_delete_link (ddrawsink->buffer_pool,
          ddrawsink->buffer_pool);

      /* If the surface is invalid for our need, destroy */
      if ((surface->width != ddrawsink->video_width) ||
          (surface->height != ddrawsink->video_height) ||
          (memcmp (&surface->dd_pixel_format, &ddrawsink->dd_pixel_format,
                  sizeof (DDPIXELFORMAT)))
          ) {
        gst_directdrawsink_surface_destroy (ddrawsink, surface);
        surface = NULL;
      } else {
        /* We found a suitable surface */
        break;
      }
    }
  }

  /* We haven't found anything, creating a new one */
  if (!surface) {
    surface = gst_directdrawsink_surface_create (ddrawsink, caps, size);
  }

  /* Now we should have a surface, set appropriate caps on it */
  if (surface) {
    gst_buffer_set_caps (GST_BUFFER (surface), caps);
  }

  g_mutex_unlock (ddrawsink->pool_lock);

  *buf = GST_BUFFER (surface);

  return ret;
}

static gboolean
gst_directdrawsink_fill_colorkey (LPDIRECTDRAWSURFACE surface, DWORD dwColorKey)
{
  DDBLTFX ddbfx;

  if (!surface)
    return FALSE;

  ddbfx.dwSize = sizeof (DDBLTFX);
  ddbfx.dwFillColor = dwColorKey;

  if (IDirectDrawSurface_Blt (surface,
          NULL, NULL, NULL, DDBLT_COLORFILL | DDBLT_WAIT, &ddbfx) == DD_OK)
    return TRUE;
  else
    return FALSE;
}


static void
gst_directdrawsink_show_overlay (GstDirectDrawSink * ddrawsink)
{
  HRESULT hRes;
  RECT destsurf_rect, src_rect;
  POINT dest_surf_point;
  DDOVERLAYFX ddofx;
  LPDIRECTDRAWSURFACE surface = NULL;

  if (!ddrawsink || !ddrawsink->overlays)
    return;

  if (ddrawsink->extern_surface)
    surface = ddrawsink->extern_surface;
  else
    surface = ddrawsink->primary_surface;

  if (ddrawsink->extern_surface) {
    destsurf_rect.left = 0;
    destsurf_rect.top = 0;
    destsurf_rect.right = ddrawsink->out_width;
    destsurf_rect.bottom = ddrawsink->out_height;
  } else {
    dest_surf_point.x = 0;
    dest_surf_point.y = 0;
    ClientToScreen (ddrawsink->video_window, &dest_surf_point);
    GetClientRect (ddrawsink->video_window, &destsurf_rect);
    OffsetRect (&destsurf_rect, dest_surf_point.x, dest_surf_point.y);
  }

  if (ddrawsink->keep_aspect_ratio) {
    src_rect.top = 0;
    src_rect.left = 0;
    src_rect.bottom = ddrawsink->video_height;
    src_rect.right = ddrawsink->video_width;
    gst_directdrawsink_center_rect (src_rect, destsurf_rect, &destsurf_rect);
  }

  gst_directdrawsink_fill_colorkey (surface, ddrawsink->color_key);

  ddofx.dwSize = sizeof (DDOVERLAYFX);
  ddofx.dckDestColorkey.dwColorSpaceLowValue = ddrawsink->color_key;
  ddofx.dckDestColorkey.dwColorSpaceHighValue = ddrawsink->color_key;

  hRes = IDirectDrawSurface_UpdateOverlay (ddrawsink->overlays,
      NULL, surface, &destsurf_rect, DDOVER_KEYDESTOVERRIDE | DDOVER_SHOW,
      &ddofx);
}

static GstFlowReturn
gst_directdrawsink_show_frame (GstBaseSink * bsink, GstBuffer * buf)
{
  GstDirectDrawSink *ddrawsink;
  HRESULT hRes;

  DDSURFACEDESC surf_desc;
  RECT destsurf_rect, src_rect;
  POINT dest_surf_point;
  LPDIRECTDRAWSURFACE lpSurface = NULL;

  ddrawsink = GST_DIRECTDRAW_SINK (bsink);

  if (ddrawsink->extern_surface) {
    destsurf_rect.left = 0;
    destsurf_rect.top = 0;
    destsurf_rect.right = ddrawsink->out_width;
    destsurf_rect.bottom = ddrawsink->out_height;
  } else {
    dest_surf_point.x = 0;
    dest_surf_point.y = 0;
    ClientToScreen (ddrawsink->video_window, &dest_surf_point);
    GetClientRect (ddrawsink->video_window, &destsurf_rect);
    OffsetRect (&destsurf_rect, dest_surf_point.x, dest_surf_point.y);
  }

  if (ddrawsink->keep_aspect_ratio) {
    /*center image to dest image keeping aspect ratio */
    src_rect.top = 0;
    src_rect.left = 0;
    src_rect.bottom = ddrawsink->video_height;
    src_rect.right = ddrawsink->video_width;
    gst_directdrawsink_center_rect (src_rect, destsurf_rect, &destsurf_rect);
  }

  if (ddrawsink->bUseOverlay) {
    /*get the back buffer of the overlays flipping chain */
    DDSCAPS ddbackcaps;

    ddbackcaps.dwCaps = DDSCAPS_BACKBUFFER;
    IDirectDrawSurface_GetAttachedSurface (ddrawsink->overlays, &ddbackcaps,
        &lpSurface);
  } else {
    /*use our offscreen surface */
    lpSurface = ddrawsink->offscreen_surface;
  }

  if (lpSurface == NULL)
    return GST_FLOW_ERROR;

  if (!GST_IS_DDRAWSURFACE (buf) ||
      ((GST_IS_DDRAWSURFACE (buf)) && (GST_BUFFER (buf)->malloc_data))) {

    LPBYTE data = NULL;
    guint src_pitch, line;

    /* Check for lost surface */
    if (IDirectDrawSurface_IsLost (lpSurface) == DDERR_SURFACELOST) {
      IDirectDrawSurface_Restore (lpSurface);
    }

    ZeroMemory (&surf_desc, sizeof (surf_desc));
    surf_desc.dwSize = sizeof (surf_desc);

    /* Lock the surface */
    hRes =
        IDirectDrawSurface_Lock (lpSurface, NULL, &surf_desc, DDLOCK_WAIT,
        NULL);
    if (hRes != DD_OK) {
      GST_CAT_WARNING (directdrawsink_debug,
          "gst_directdrawsink_show_frame failed locking surface %s",
          DDErrorString (hRes));
      return GST_FLOW_ERROR;
    }

    /* Write data */
    data = surf_desc.lpSurface;

    /* Source video rowbytes */
    src_pitch = GST_BUFFER_SIZE (buf) / ddrawsink->video_height;

    /* Write each line respecting dest surface pitch */
    for (line = 0; line < surf_desc.dwHeight; line++) {
      memcpy (data, GST_BUFFER_DATA (buf) + (line * src_pitch), src_pitch);
      data += surf_desc.lPitch;
    }

    /* Unlock the surface */
    hRes = IDirectDrawSurface_Unlock (lpSurface, NULL);
    if (hRes != DD_OK) {
      GST_CAT_WARNING (directdrawsink_debug,
          "gst_directdrawsink_show_frame failed unlocking surface %s",
          DDErrorString (hRes));
      return GST_FLOW_ERROR;
    }

    if (ddrawsink->bUseOverlay) {
      /*Flip to front overlay */
      hRes =
          IDirectDrawSurface_Flip (ddrawsink->overlays, lpSurface, DDFLIP_WAIT);
      IDirectDrawSurface_Release (lpSurface);
      lpSurface = NULL;
    } else {
      if (ddrawsink->extern_surface) {
        if (ddrawsink->out_height == ddrawsink->video_height &&
            ddrawsink->out_width == ddrawsink->video_width) {
          /*Fast blit to extern surface */
          hRes = IDirectDrawSurface_BltFast (ddrawsink->extern_surface, 0, 0,
              lpSurface, NULL, DDBLTFAST_WAIT);

        } else {
          /*blit to extern surface (Blt will scale the video the dest rect surface if needed) */
          hRes =
              IDirectDrawSurface_Blt (ddrawsink->extern_surface, &destsurf_rect,
              lpSurface, NULL, DDBLT_WAIT, NULL);
        }
      } else {
        /*blit to primary surface ( Blt will scale the video the dest rect surface if needed */
        hRes =
            IDirectDrawSurface_Blt (ddrawsink->primary_surface, &destsurf_rect,
            lpSurface, NULL, DDBLT_WAIT, NULL);
      }
    }
  } else {

    GstDDrawSurface *surface = NULL;

    surface = GST_DDRAWSURFACE (buf);

    /* Unlocking surface before blit */
    IDirectDrawSurface_Unlock (surface->surface, NULL);
    surface->locked = FALSE;

    /* Check for lost surfaces */
    if (IDirectDrawSurface_IsLost (surface->surface) == DDERR_SURFACELOST) {
      IDirectDrawSurface_Restore (surface->surface);
    }

    if (ddrawsink->bUseOverlay) {
      /* blit to the overlays back buffer */
      hRes = IDirectDrawSurface_Blt (lpSurface, NULL,
          surface->surface, NULL, DDBLT_WAIT, NULL);

      hRes = IDirectDrawSurface_Flip (ddrawsink->overlays, NULL, DDFLIP_WAIT);
      if (hRes != DD_OK)
        GST_CAT_WARNING (directdrawsink_debug, "error flipping");

    } else {
      if (ddrawsink->extern_surface) {
        /*blit to the extern surface */
        if (ddrawsink->out_height == ddrawsink->video_height &&
            ddrawsink->out_width == ddrawsink->video_width) {
          /*Fast blit to extern surface */
          hRes = IDirectDrawSurface_BltFast (ddrawsink->extern_surface, 0, 0,
              surface->surface, NULL, DDBLTFAST_WAIT);

        } else {
          /*blit to extern surface (Blt will scale the video the dest rect surface if needed) */
          hRes =
              IDirectDrawSurface_Blt (ddrawsink->extern_surface, &destsurf_rect,
              surface->surface, NULL, DDBLT_WAIT, NULL);
        }
      } else {
        /*blit to our primary surface */
        hRes =
            IDirectDrawSurface_Blt (ddrawsink->primary_surface, &destsurf_rect,
            surface->surface, NULL, DDBLT_WAIT, NULL);
        if (hRes != DD_OK)
          GST_CAT_WARNING (directdrawsink_debug,
              "IDirectDrawSurface_Blt returned %s", DDErrorString (hRes));
        else
          GST_CAT_INFO (directdrawsink_debug,
              "allocated surface was blit to our primary",
              DDErrorString (hRes));
      }
    }
  }

  if (ddrawsink->bUseOverlay)
    gst_directdrawsink_show_overlay (ddrawsink);

  return GST_FLOW_OK;
}

static gboolean
gst_directdrawsink_setup_ddraw (GstDirectDrawSink * ddrawsink)
{
  gboolean bRet = TRUE;
  HRESULT hRes;
  DWORD dwCooperativeLevel;
  DDSURFACEDESC dd_surface_desc;

  /*UUID IDirectDraw7_ID;

     //IDirectDraw_QueryInterface()
     /*create an instance of the ddraw object 
     hRes = DirectDrawCreateEx (DDCREATE_EMULATIONONLY, (void**)&ddrawsink->ddraw_object,
     (REFIID)IID_IDirectDraw7, NULL);
   */
  hRes = DirectDrawCreate (NULL, &ddrawsink->ddraw_object, NULL);
  if (hRes != DD_OK || ddrawsink->ddraw_object == NULL) {
    GST_CAT_ERROR (directdrawsink_debug, "DirectDrawCreate failed with: %s",
        DDErrorString (hRes));
    return FALSE;
  }

  /*get ddraw caps for the current hardware */
/*  ddrawsink->DDDriverCaps.dwSize = sizeof (DDCAPS);
  ddrawsink->DDHELCaps.dwSize = sizeof (DDCAPS);
  hRes = IDirectDraw_GetCaps (ddrawsink->ddraw_object, &ddrawsink->DDDriverCaps, &ddrawsink->DDHELCaps);
*/
  /*set cooperative level */
  if (ddrawsink->fullscreen)
    dwCooperativeLevel = DDSCL_EXCLUSIVE | DDSCL_FULLSCREEN;
  else
    dwCooperativeLevel = DDSCL_NORMAL;

  hRes = IDirectDraw_SetCooperativeLevel (ddrawsink->ddraw_object,
      ddrawsink->video_window, dwCooperativeLevel);
  if (hRes != DD_OK) {
    GST_CAT_ERROR (directdrawsink_debug, "SetCooperativeLevel failed with: %s",
        DDErrorString (hRes));
    bRet = FALSE;
  }

  /*for fullscreen mode, setup display mode */
  if (ddrawsink->fullscreen) {
    hRes = IDirectDraw_SetDisplayMode (ddrawsink->ddraw_object, 1440, 900, 32);
  }

  if (!ddrawsink->extern_surface) {
    /*create our primary surface */
    memset (&dd_surface_desc, 0, sizeof (dd_surface_desc));
    dd_surface_desc.dwSize = sizeof (dd_surface_desc);
    dd_surface_desc.dwFlags = DDSD_CAPS;
    dd_surface_desc.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;

    hRes = IDirectDraw_CreateSurface (ddrawsink->ddraw_object, &dd_surface_desc,
        &ddrawsink->primary_surface, NULL);
    if (hRes != DD_OK) {
      GST_CAT_ERROR (directdrawsink_debug,
          "CreateSurface (primary) failed with: %s", DDErrorString (hRes));
      IDirectDraw_Release (ddrawsink->ddraw_object);
      return FALSE;
    }

    hRes = IDirectDraw_CreateClipper (ddrawsink->ddraw_object, 0,
        &ddrawsink->clipper, NULL);
    if (hRes == DD_OK) {
      hRes = IDirectDrawClipper_SetHWnd (ddrawsink->clipper, 0,
          ddrawsink->video_window);
      hRes = IDirectDrawSurface_SetClipper (ddrawsink->primary_surface,
          ddrawsink->clipper);
    }

  } else {
    DDSURFACEDESC desc_surface;

    desc_surface.dwSize = sizeof (DDSURFACEDESC);

    /*get extern surface size */
    hRes = IDirectDrawSurface_GetSurfaceDesc (ddrawsink->extern_surface,
        &desc_surface);
    if (hRes != DD_OK) {
      /*error while retrieving ext surface description */
      return FALSE;
    }

    ddrawsink->out_width = desc_surface.dwWidth;
    ddrawsink->out_height = desc_surface.dwHeight;

    /*get extern surface pixel format (FIXME not needed if we are using overlays) */
    ddrawsink->dd_pixel_format.dwSize = sizeof (DDPIXELFORMAT);
    hRes = IDirectDrawSurface_GetPixelFormat (ddrawsink->extern_surface,
        &ddrawsink->dd_pixel_format);
    if (hRes != DD_OK) {
      /*error while retrieving ext surface pixel format */
      GST_CAT_WARNING (directdrawsink_debug,
          "GetPixelFormat (ddrawsink->extern_surface) failed with: %s",
          DDErrorString (hRes));
      return FALSE;
    }

    /*get specific caps if needed ... */
  }

  ddrawsink->setup = TRUE;

  return bRet;
}

long FAR PASCAL
WndProc (HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
  switch (message) {
      /*case WM_ERASEBKGND:
         return TRUE; */
/*    case WM_WINDOWPOSCHANGED:
    case WM_MOVE:
    case WM_SIZE:
		if(global_ddrawsink && global_ddrawsink->bUseOverlay)
    			gst_directdrawsink_show_overlay(global_ddrawsink);
		break;
 case WM_PAINT:
		if(global_ddrawsink && global_ddrawsink->bUseOverlay)
    {
      if(global_ddrawsink->extern_surface)
        gst_directdrawsink_fill_colorkey(global_ddrawsink->extern_surface, 
            global_ddrawsink->color_key);
      else
        gst_directdrawsink_fill_colorkey(global_ddrawsink->primary_surface, 
            global_ddrawsink->color_key);
    }
    	break;
*/
    case WM_DESTROY:
      PostQuitMessage (0);
      break;
    case WM_CLOSE:
      DestroyWindow (hWnd);
      return 0;
  }
  return DefWindowProc (hWnd, message, wParam, lParam);
}

static gpointer
gst_directdrawsink_window_thread (GstDirectDrawSink * ddrawsink)
{
  WNDCLASS WndClass;

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
      WndClass.hInstance, NULL);

  if (ddrawsink->video_window == NULL)
    return FALSE;

  ReleaseSemaphore (ddrawsink->window_created_signal, 1, NULL);

  /*start message loop processing our default window messages */
  while (1) {
    MSG msg;

    if (!GetMessage (&msg, ddrawsink->video_window, 0, 0))
      break;
    DispatchMessage (&msg);
  }

  return NULL;
}

static gboolean
gst_directdrawsink_create_default_window (GstDirectDrawSink * ddrawsink)
{
  ddrawsink->window_created_signal = CreateSemaphore (NULL, 0, 1, NULL);
  if (ddrawsink->window_created_signal == NULL)
    return FALSE;

  ddrawsink->window_thread = g_thread_create (
      (GThreadFunc) gst_directdrawsink_window_thread, ddrawsink, TRUE, NULL);

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
  return FALSE;
}

static gboolean
gst_directdrawsink_create_ddraw_surfaces (GstDirectDrawSink * ddrawsink)
{
  DDSURFACEDESC dd_surface_desc;
  HRESULT hRes;

  memset (&dd_surface_desc, 0, sizeof (dd_surface_desc));
  dd_surface_desc.dwSize = sizeof (dd_surface_desc);

  dd_surface_desc.dwFlags =
      DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PIXELFORMAT;
  dd_surface_desc.dwHeight = ddrawsink->video_height;
  dd_surface_desc.dwWidth = ddrawsink->video_width;
  memcpy (&(dd_surface_desc.ddpfPixelFormat), &ddrawsink->dd_pixel_format,
      sizeof (DDPIXELFORMAT));

  if (ddrawsink->bUseOverlay) {
    /*create overlays flipping chain */
    dd_surface_desc.ddsCaps.dwCaps =
        DDSCAPS_OVERLAY | DDSCAPS_VIDEOMEMORY | DDSCAPS_FLIP | DDSCAPS_COMPLEX;
    dd_surface_desc.dwFlags |= DDSD_BACKBUFFERCOUNT;
    dd_surface_desc.dwBackBufferCount = 1;

    hRes = IDirectDraw_CreateSurface (ddrawsink->ddraw_object, &dd_surface_desc,
        &ddrawsink->overlays, NULL);

    if (hRes != DD_OK) {
      GST_CAT_WARNING (directdrawsink_debug,
          "create_ddraw_surfaces:CreateSurface(overlays) failed %s",
          DDErrorString (hRes));
      return FALSE;
    } else {
      GST_CAT_INFO (directdrawsink_debug,
          "An overlay surfaces flipping chain was created");
    }
  } else {
    dd_surface_desc.ddsCaps.dwCaps =
        DDSCAPS_OFFSCREENPLAIN /*|DDSCAPS_SYSTEMMEMORY */ ;

    hRes = IDirectDraw_CreateSurface (ddrawsink->ddraw_object, &dd_surface_desc,
        &ddrawsink->offscreen_surface, NULL);

    if (hRes != DD_OK) {
      GST_CAT_WARNING (directdrawsink_debug,
          "create_ddraw_surfaces:CreateSurface(offscreen) failed %s",
          DDErrorString (hRes));
      return FALSE;
    }
  }

  return TRUE;
}

static void
gst_directdrawsink_get_times (GstBaseSink * bsink, GstBuffer * buf,
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

static int
gst_directdrawsink_get_depth (LPDDPIXELFORMAT lpddpfPixelFormat)
{
  gint order = 0, binary;

  binary =
      lpddpfPixelFormat->dwRBitMask | lpddpfPixelFormat->
      dwGBitMask | lpddpfPixelFormat->dwBBitMask | lpddpfPixelFormat->
      dwRGBAlphaBitMask;
  while (binary != 0) {
    if ((binary % 2) == 1)
      order++;
    binary = binary >> 1;
  }
  return order;
}

HRESULT WINAPI
EnumModesCallback2 (LPDDSURFACEDESC lpDDSurfaceDesc, LPVOID lpContext)
{
  GstDirectDrawSink *ddrawsink = (GstDirectDrawSink *) lpContext;
  GstCaps *format_caps = NULL;

  if (!ddrawsink || !lpDDSurfaceDesc)
    return DDENUMRET_CANCEL;

  if ((lpDDSurfaceDesc->dwFlags & DDSD_PIXELFORMAT) != DDSD_PIXELFORMAT) {
    GST_CAT_INFO (directdrawsink_debug,
        "Display mode found with DDSD_PIXELFORMAT not set");
    return DDENUMRET_OK;
  }

  if ((lpDDSurfaceDesc->ddpfPixelFormat.dwFlags & DDPF_RGB) != DDPF_RGB)
    return DDENUMRET_OK;

  format_caps = gst_caps_new_simple ("video/x-raw-rgb",
      "width", GST_TYPE_INT_RANGE, 1, G_MAXINT,
      "height", GST_TYPE_INT_RANGE, 1, G_MAXINT,
      "framerate", GST_TYPE_FRACTION_RANGE, 0, 1, G_MAXINT, 1,
      "bpp", G_TYPE_INT, lpDDSurfaceDesc->ddpfPixelFormat.dwRGBBitCount,
      "depth", G_TYPE_INT,
      gst_directdrawsink_get_depth (&lpDDSurfaceDesc->ddpfPixelFormat),
      "endianness", G_TYPE_INT, G_LITTLE_ENDIAN, "red_mask", G_TYPE_INT,
      lpDDSurfaceDesc->ddpfPixelFormat.dwRBitMask, "green_mask", G_TYPE_INT,
      lpDDSurfaceDesc->ddpfPixelFormat.dwGBitMask, "blue_mask", G_TYPE_INT,
      lpDDSurfaceDesc->ddpfPixelFormat.dwBBitMask, NULL);

  if (format_caps) {
    gst_caps_append (ddrawsink->caps, format_caps);
  }

  return DDENUMRET_OK;
}

static GstCaps *
gst_directdrawsink_get_ddrawcaps (GstDirectDrawSink * ddrawsink)
{
  HRESULT hRes = S_OK;
  DWORD dwFourccCodeIndex = 0;
  LPDWORD pdwFourccCodes = NULL;
  DWORD dwNbFourccCodes = 0;
  GstCaps *format_caps = NULL;

  ddrawsink->caps = gst_caps_new_empty ();
  if (!ddrawsink->caps)
    return FALSE;

  /*enumerate display modes exposed by directdraw object */
  hRes =
      IDirectDraw_EnumDisplayModes (ddrawsink->ddraw_object, DDEDM_REFRESHRATES,
      NULL, ddrawsink, EnumModesCallback2);
  if (hRes != DD_OK) {
    GST_CAT_WARNING (directdrawsink_debug, "EnumDisplayModes returns: %s",
        DDErrorString (hRes));
    return FALSE;
  }

  /* enumerate non-rgb modes exposed by directdraw object */
  IDirectDraw_GetFourCCCodes (ddrawsink->ddraw_object, &dwNbFourccCodes, NULL);
  if (dwNbFourccCodes != 0) {
    pdwFourccCodes = g_new0 (DWORD, dwNbFourccCodes);
    if (!pdwFourccCodes)
      return FALSE;

    if (FAILED (IDirectDraw_GetFourCCCodes (ddrawsink->ddraw_object,
                &dwNbFourccCodes, pdwFourccCodes))) {
      g_free (pdwFourccCodes);
      return FALSE;
    }

    for (dwFourccCodeIndex = 0; dwFourccCodeIndex < dwNbFourccCodes;
        dwFourccCodeIndex++) {
      /*support only yuv formats YUY2, UYVY, YVU9, YV12, AYUV */
      if (pdwFourccCodes[dwFourccCodeIndex] == mmioFOURCC ('Y', 'U', 'Y', '2')
          || pdwFourccCodes[dwFourccCodeIndex] == mmioFOURCC ('U', 'Y', 'V',
              'Y')
          || pdwFourccCodes[dwFourccCodeIndex] == mmioFOURCC ('Y', 'V', 'U',
              '9')
          || pdwFourccCodes[dwFourccCodeIndex] == mmioFOURCC ('Y', 'V', '1',
              '2')
          || pdwFourccCodes[dwFourccCodeIndex] == mmioFOURCC ('A', 'Y', 'U',
              'V')
          ) {
        format_caps = gst_caps_new_simple ("video/x-raw-yuv",
            "format", GST_TYPE_FOURCC, pdwFourccCodes[dwFourccCodeIndex],
            "width", GST_TYPE_INT_RANGE, 1, G_MAXINT,
            "height", GST_TYPE_INT_RANGE, 1, G_MAXINT,
            "framerate", GST_TYPE_FRACTION_RANGE, 0, 1, G_MAXINT, 1, NULL);

        if (format_caps)
          gst_caps_append (ddrawsink->caps, format_caps);
      }
    }

    g_free (pdwFourccCodes);
  }

  if (gst_caps_is_empty (ddrawsink->caps)) {
    gst_caps_unref (ddrawsink->caps);

    GST_ELEMENT_ERROR (ddrawsink, STREAM, WRONG_TYPE, (NULL),
        ("No supported format found"));
    return NULL;
  }

  return ddrawsink->caps;
}

/* Creates miniobject and our internal surface */
static GstDDrawSurface *
gst_directdrawsink_surface_create (GstDirectDrawSink * ddrawsink,
    GstCaps * caps, size_t size)
{
  GstDDrawSurface *surface = NULL;
  GstStructure *structure = NULL;
  gint pitch;

  HRESULT hRes;
  DDSURFACEDESC surf_desc, surf_lock_desc;

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
    GST_WARNING ("failed getting geometry from caps %" GST_PTR_FORMAT, caps);
  }

  pitch = GST_ROUND_UP_8 (size / surface->height);

  if (!gst_ddrawvideosink_get_format_from_caps (caps,
          &surface->dd_pixel_format)) {
    GST_WARNING ("failed getting pixel format from caps %" GST_PTR_FORMAT,
        caps);
  }

  if (ddrawsink->ddraw_object) {
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

    hRes = IDirectDraw_CreateSurface (ddrawsink->ddraw_object, &surf_desc,
        &surface->surface, NULL);
    if (hRes != DD_OK) {
      /*gst_object_unref (surface);
         surface = NULL;
         goto beach; */
      goto surface_pitch_bad;
    }

    /* Locking the surface to acquire the memory pointer.
       Use DDLOCK_NOSYSLOCK to disable syslock which can cause a deadlock 
       if directdraw api is used while a buffer is lock */
    hRes = IDirectDrawSurface_Lock (surface->surface, NULL, &surf_lock_desc,
        DDLOCK_WAIT | DDLOCK_NOSYSLOCK, NULL);
    surface->locked = TRUE;

    if (surf_lock_desc.lPitch != pitch) {
      GST_CAT_INFO (directdrawsink_debug,
          "DDraw stride/pitch %d isn't as expected value %d, let's continue allocating buffer.",
          surf_lock_desc.lPitch, pitch);

      /*Unlock the surface as we will change it to use system memory with a GStreamer compatible pitch */
      hRes = IDirectDrawSurface_Unlock (surface->surface, NULL);
      goto surface_pitch_bad;
    }

    GST_CAT_INFO (directdrawsink_debug,
        "allocating a surface of %d bytes (stride=%d)\n", size,
        surf_lock_desc.lPitch);
    GST_BUFFER_DATA (surface) = surf_lock_desc.lpSurface;
    GST_BUFFER_SIZE (surface) = surf_lock_desc.lPitch * surface->height;
  } else {

  surface_pitch_bad:
    GST_BUFFER (surface)->malloc_data = g_malloc (size);
    GST_BUFFER_DATA (surface) = GST_BUFFER (surface)->malloc_data;
    GST_BUFFER_SIZE (surface) = size;

/*    surf_desc.dwSize = sizeof(DDSURFACEDESC);
    surf_desc.dwFlags = DDSD_PITCH | DDSD_LPSURFACE | DDSD_HEIGHT | DDSD_WIDTH ||DDSD_PIXELFORMAT;
    surf_desc.lpSurface = GST_BUFFER (surface)->malloc_data;
    surf_desc.lPitch = pitch;
    //surf_desc.dwHeight = surface->height;
    surf_desc.dwWidth = surface->width;
    hRes = IDirectDrawSurface7_SetSurfaceDesc(surface->surface, &surf_desc, 0);
    printf("%\n", DDErrorString(hRes));

    hRes = IDirectDrawSurface7_Lock (surface->surface, NULL, &surf_lock_desc,
        DDLOCK_WAIT | DDLOCK_NOSYSLOCK, NULL);
*/
    surface->surface = NULL;
    /*printf ("allocating a buffer of %d bytes\n", size); */
  }

  /* Keep a ref to our sink */
  surface->ddrawsink = gst_object_ref (ddrawsink);

beach:
  return surface;
}

/* We are called from the finalize method of miniobject, the object will be
 * destroyed so we just have to clean our internal stuff */
static void
gst_directdrawsink_surface_destroy (GstDirectDrawSink * ddrawsink,
    GstDDrawSurface * surface)
{
  g_return_if_fail (GST_IS_DIRECTDRAW_SINK (ddrawsink));

  /* Release our internal surface */
  if (surface->surface) {
    if (surface->locked) {
      IDirectDrawSurface_Unlock (surface->surface, NULL);
      surface->locked = FALSE;
    }
    IDirectDrawSurface_Release (surface->surface);
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

static void
gst_directdrawsink_bufferpool_clear (GstDirectDrawSink * ddrawsink)
{
  g_mutex_lock (ddrawsink->pool_lock);
  while (ddrawsink->buffer_pool) {
    GstDDrawSurface *surface = ddrawsink->buffer_pool->data;

    ddrawsink->buffer_pool = g_slist_delete_link (ddrawsink->buffer_pool,
        ddrawsink->buffer_pool);
    gst_directdrawsink_surface_destroy (ddrawsink, surface);
  }
  g_mutex_unlock (ddrawsink->pool_lock);
}

static void
gst_directdrawsink_cleanup (GstDirectDrawSink * ddrawsink)
{
  /* Post quit message and wait for our event window thread */
  if (ddrawsink->video_window)
    PostMessage (ddrawsink->video_window, WM_QUIT, 0, 0);
  if (ddrawsink->window_thread) {
    g_thread_join (ddrawsink->window_thread);
    ddrawsink->window_thread = NULL;
  }

  if (ddrawsink->buffer_pool) {
    gst_directdrawsink_bufferpool_clear (ddrawsink);
    ddrawsink->buffer_pool = NULL;
  }

  if (ddrawsink->display_modes) {
    GSList *walk = ddrawsink->display_modes;

    while (walk) {
      g_free (walk->data);
      walk = g_slist_next (walk);
    }
    g_slist_free (ddrawsink->display_modes);
    ddrawsink->display_modes = NULL;
  }

  if (ddrawsink->overlays) {
    IDirectDrawSurface_Release (ddrawsink->overlays);
    ddrawsink->overlays = NULL;
  }

  if (ddrawsink->offscreen_surface) {
    IDirectDrawSurface_Release (ddrawsink->offscreen_surface);
    ddrawsink->offscreen_surface = NULL;
  }

  if (ddrawsink->clipper) {
    IDirectDrawClipper_Release (ddrawsink->clipper);
    ddrawsink->clipper = NULL;
  }

  if (ddrawsink->primary_surface) {
    IDirectDrawSurface_Release (ddrawsink->primary_surface);
    ddrawsink->primary_surface = NULL;
  }

  if (ddrawsink->ddraw_object) {
    IDirectDraw_Release (ddrawsink->ddraw_object);
    ddrawsink->ddraw_object = NULL;
  }

  ddrawsink->setup = FALSE;
}
