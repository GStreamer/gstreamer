/* GStreamer DirectFB plugin
 * Copyright (C) 2004 Julien MOUTTE <julien@moutte.net>
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

/* Our interfaces */
#include <gst/navigation/navigation.h>

/* Object header */
#include "directfbvideosink.h"

/* Debugging category */
#include <gst/gstinfo.h>
GST_DEBUG_CATEGORY_STATIC (gst_debug_directfbvideosink);
#define GST_CAT_DEFAULT gst_debug_directfbvideosink

/* ElementFactory information */
static GstElementDetails gst_directfbvideosink_details =
GST_ELEMENT_DETAILS ("Video sink",
    "Sink/Video",
    "A DirectFB based videosink",
    "Julien Moutte <julien@moutte.net>");

/* Default template */
static GstStaticPadTemplate gst_directfbvideosink_sink_template_factory =
    GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw-rgb, "
        "framerate = (double) [ 1.0, 100.0 ], "
        "width = (int) [ 1, MAX ], "
        "height = (int) [ 1, MAX ]; "
        "video/x-raw-yuv, "
        "framerate = (double) [ 1.0, 100.0 ], "
        "width = (int) [ 1, MAX ], " "height = (int) [ 1, MAX ]")
    );

/* Signals and args */
enum
{
  ARG_0,
  ARG_SURFACE,
};

static GstVideoSinkClass *parent_class = NULL;

/* ============================================================= */
/*                                                               */
/*                       Private Methods                         */
/*                                                               */
/* ============================================================= */

/* Creates internal surface */
static gboolean
gst_directfbvideosink_create (GstDirectFBVideoSink * directfbvideosink)
{
  DFBResult ret;
  DFBSurfaceDescription s_dsc;

  g_return_val_if_fail (directfbvideosink != NULL, FALSE);
  g_return_val_if_fail (GST_IS_DIRECTFBVIDEOSINK (directfbvideosink), FALSE);

  /* Creating an internal surface which will be used as GstBuffer, we used
     the detected pixel format and video dimensions */
  s_dsc.flags = DSDESC_PIXELFORMAT | DSDESC_WIDTH | DSDESC_HEIGHT;
  s_dsc.pixelformat = directfbvideosink->pixel_format;
  s_dsc.width = directfbvideosink->video_width;
  s_dsc.height = directfbvideosink->video_height;

  GST_DEBUG_OBJECT (directfbvideosink, "creating our internal surface");

  ret = directfbvideosink->directfb->CreateSurface (directfbvideosink->directfb,
      &s_dsc, &directfbvideosink->surface);
  if (ret != DFB_OK) {
    directfbvideosink->surface = NULL;
    return FALSE;
  }

  /* Clearing surface */
  directfbvideosink->surface->Clear (directfbvideosink->surface,
      0x00, 0x00, 0x00, 0xFF);

  return TRUE;
}

static gboolean
gst_directfbvideosink_get_pixel_format (GstDirectFBVideoSink *
    directfbvideosink)
{
  DFBResult ret;

  g_return_val_if_fail (directfbvideosink != NULL, FALSE);
  g_return_val_if_fail (GST_IS_DIRECTFBVIDEOSINK (directfbvideosink), FALSE);

  /* If we do it all by ourself we create the DirectFB context, get the 
     primary layer and use a fullscreen configuration */
  if (!directfbvideosink->foreign_surface) {
    if (!directfbvideosink->directfb) {
      GST_DEBUG_OBJECT (directfbvideosink, "initializing DirectFB");
      ret = DirectFBInit (0, NULL);

      if (ret != DFB_OK)
        return FALSE;

      ret = DirectFBCreate (&(directfbvideosink->directfb));

      if (ret != DFB_OK)
        return FALSE;
    }
    if (!directfbvideosink->layer) {
      ret =
          directfbvideosink->directfb->GetDisplayLayer (directfbvideosink->
          directfb, DLID_PRIMARY, &directfbvideosink->layer);
      if (ret != DFB_OK)
        return FALSE;
      directfbvideosink->layer->SetCooperativeLevel (directfbvideosink->layer,
          DFSCL_FULLSCREEN);
      directfbvideosink->layer->SetBackgroundColor (directfbvideosink->layer,
          0x00, 0x00, 0x00, 0xFF);
      GST_DEBUG_OBJECT (directfbvideosink, "getting primary surface");
      directfbvideosink->layer->GetSurface (directfbvideosink->layer,
          &directfbvideosink->primary);
    }

    directfbvideosink->primary->GetPixelFormat (directfbvideosink->primary,
        &directfbvideosink->pixel_format);
  } else {
    GST_DEBUG_OBJECT (directfbvideosink, "getting pixel format "
        "from foreign surface %p", directfbvideosink->foreign_surface);
    directfbvideosink->foreign_surface->GetPixelFormat (directfbvideosink->
        foreign_surface, &directfbvideosink->pixel_format);
  }

  return TRUE;
}

static DFBSurfacePixelFormat
gst_directfbvideosink_get_format_from_fourcc (GstDirectFBVideoSink *
    directfbvideosink, guint32 code)
{
  switch (code) {
    case GST_MAKE_FOURCC ('I', '4', '2', '0'):
      return DSPF_I420;
    case GST_MAKE_FOURCC ('Y', 'V', '1', '2'):
      return DSPF_YV12;
    case GST_MAKE_FOURCC ('Y', 'U', 'Y', '2'):
      return DSPF_YUY2;
    case GST_MAKE_FOURCC ('U', 'Y', 'V', 'Y'):
      return DSPF_UYVY;
    default:{
      return DSPF_UNKNOWN;
    }
  }
}

static GstCaps *
gst_directfbvideosink_fixate (GstPad * pad, const GstCaps * caps)
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

static GstCaps *
gst_directfbvideosink_getcaps (GstPad * pad)
{
  GstDirectFBVideoSink *directfbvideosink;
  gint bpp = 0;
  gboolean is_yuv = FALSE, is_rgb = FALSE;
  guint32 gst_format = 0;
  GstCaps *caps = NULL;

  directfbvideosink = GST_DIRECTFBVIDEOSINK (gst_pad_get_parent (pad));

  GST_DEBUG_OBJECT (directfbvideosink, "getcaps called, identifying "
      "a valid pixel format");

  /* We need a primary surface or a foreign one to get pixel format */
  if (!directfbvideosink->primary && !directfbvideosink->foreign_surface) {
    gst_directfbvideosink_get_pixel_format (directfbvideosink);
  }

  switch (directfbvideosink->pixel_format) {
    case DSPF_RGB16:
      is_rgb = TRUE;
      bpp = 16;
      break;
    case DSPF_RGB24:
      is_rgb = TRUE;
      bpp = 24;
      break;
    case DSPF_RGB32:
      is_rgb = TRUE;
      bpp = 32;
      break;
    case DSPF_YUY2:
      is_yuv = TRUE;
      gst_format = GST_MAKE_FOURCC ('Y', 'U', 'Y', '2');
      break;
    case DSPF_UYVY:
      is_yuv = TRUE;
      gst_format = GST_MAKE_FOURCC ('U', 'Y', 'V', 'Y');
      break;
    case DSPF_I420:
      is_yuv = TRUE;
      gst_format = GST_MAKE_FOURCC ('I', '4', '2', '0');
      break;
    case DSPF_YV12:
      is_yuv = TRUE;
      gst_format = GST_MAKE_FOURCC ('Y', 'V', '1', '2');
      break;
    default:
      GST_ELEMENT_ERROR (directfbvideosink, RESOURCE, WRITE, (NULL),
          ("Unsupported format %d", directfbvideosink->pixel_format));
  }

  if (is_rgb) {
    caps = gst_caps_new_simple ("video/x-raw-rgb",
        "bpp", G_TYPE_INT, bpp,
        "width", GST_TYPE_INT_RANGE, 1, G_MAXINT,
        "height", GST_TYPE_INT_RANGE, 1, G_MAXINT,
        "framerate", GST_TYPE_DOUBLE_RANGE, 1.0, 100.0, NULL);
  } else if (is_yuv) {
    caps = gst_caps_new_simple ("video/x-raw-yuv",
        "width", GST_TYPE_INT_RANGE, 1, G_MAXINT,
        "height", GST_TYPE_INT_RANGE, 1, G_MAXINT,
        "format", GST_TYPE_FOURCC, gst_format,
        "framerate", GST_TYPE_DOUBLE_RANGE, 1.0, 100.0, NULL);
  } else {
    GST_ELEMENT_ERROR (directfbvideosink, RESOURCE, WRITE, (NULL),
        ("Unsupported format neither RGB nor YUV"));
  }

  return caps;
}

static GstPadLinkReturn
gst_directfbvideosink_sink_link (GstPad * pad, const GstCaps * caps)
{
  GstDirectFBVideoSink *directfbvideosink;
  GstStructure *structure;
  gboolean ret;
  gint video_width, video_height;
  DFBSurfacePixelFormat pixel_format = DSPF_UNKNOWN;

  directfbvideosink = GST_DIRECTFBVIDEOSINK (gst_pad_get_parent (pad));

  structure = gst_caps_get_structure (caps, 0);
  ret = gst_structure_get_int (structure, "width", &video_width);
  ret &= gst_structure_get_int (structure, "height", &video_height);
  ret &= gst_structure_get_double (structure, "framerate",
      &directfbvideosink->framerate);
  if (!ret) {
    return GST_PAD_LINK_REFUSED;
  }

  /* Check wether we have RGB or YUV video */
  if (g_ascii_strcasecmp (gst_structure_get_name (structure),
          "video/x-raw-rgb") == 0) {
    gint bpp = 0;

    gst_structure_get_int (structure, "bpp", &bpp);
    GST_DEBUG_OBJECT (directfbvideosink,
        "linking with rgb (bpp %d) %dx%d", bpp, video_width, video_height);
    switch (bpp) {
      case 16:
        pixel_format = DSPF_RGB16;
        break;
      case 24:
        pixel_format = DSPF_RGB24;
        break;
      case 32:
        pixel_format = DSPF_RGB32;
        break;
    }
  } else if (g_ascii_strcasecmp (gst_structure_get_name (structure),
          "video/x-raw-yuv") == 0) {
    gint im_format = 0;

    gst_structure_get_fourcc (structure, "format", &im_format);
    GST_DEBUG_OBJECT (directfbvideosink,
        "linking with yuv (" GST_FOURCC_FORMAT ") %dx%d",
        GST_FOURCC_ARGS (im_format), video_width, video_height);
    pixel_format =
        gst_directfbvideosink_get_format_from_fourcc (directfbvideosink,
        im_format);
  } else {
    return GST_PAD_LINK_REFUSED;
  }

  if (pixel_format != directfbvideosink->pixel_format) {
    return GST_PAD_LINK_REFUSED;
  }

  directfbvideosink->video_width = video_width;
  directfbvideosink->video_height = video_height;

  if (gst_directfbvideosink_create (directfbvideosink))
    return GST_PAD_LINK_OK;
  else
    return GST_PAD_LINK_REFUSED;
}

static GstStateChangeReturn
gst_directfbvideosink_change_state (GstElement * element,
    GstStateChange transition)
{
  GstDirectFBVideoSink *directfbvideosink;

  directfbvideosink = GST_DIRECTFBVIDEOSINK (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      /* Blank surface if we have one */
      if (directfbvideosink->foreign_surface) {
        directfbvideosink->foreign_surface->Clear (directfbvideosink->
            foreign_surface, 0x00, 0x00, 0x00, 0xFF);
      }
      if (directfbvideosink->primary) {
        directfbvideosink->primary->Clear (directfbvideosink->primary,
            0x00, 0x00, 0x00, 0xFF);
      }
      directfbvideosink->time = 0;
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      break;
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      directfbvideosink->framerate = 0;
      GST_VIDEOSINK_WIDTH (directfbvideosink) = 0;
      GST_VIDEOSINK_HEIGHT (directfbvideosink) = 0;
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  return GST_STATE_CHANGE_SUCCESS;
}

static void
gst_directfbvideosink_chain (GstPad * pad, GstData * data)
{
  GstBuffer *buf = NULL;
  GstDirectFBVideoSink *directfbvideosink;

  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (data != NULL);

  directfbvideosink = GST_DIRECTFBVIDEOSINK (gst_pad_get_parent (pad));

  if (GST_IS_EVENT (data)) {
    gst_pad_event_default (pad, GST_EVENT (data));
    return;
  }

  buf = GST_BUFFER (data);
  /* update time */
  if (GST_BUFFER_TIMESTAMP_IS_VALID (buf)) {
    directfbvideosink->time = GST_BUFFER_TIMESTAMP (buf);
  }
  GST_LOG_OBJECT (directfbvideosink, "clock wait: %" GST_TIME_FORMAT,
      GST_TIME_ARGS (directfbvideosink->time));

  if (GST_VIDEOSINK_CLOCK (directfbvideosink)) {
    gst_element_wait (GST_ELEMENT (directfbvideosink), directfbvideosink->time);
  }

  /* Actual drawing */
  if (directfbvideosink->surface) {
    if (directfbvideosink->surface_locked) {
      GST_DEBUG_OBJECT (directfbvideosink, "unlocking surface %p",
          directfbvideosink->surface);
      directfbvideosink->surface->Unlock (directfbvideosink->surface);
      directfbvideosink->surface_locked = FALSE;
    }

    if (directfbvideosink->foreign_surface) {
      /* FIXME : Check return values */
      directfbvideosink->foreign_surface->Blit (directfbvideosink->
          foreign_surface, directfbvideosink->surface, NULL, 0, 0);

      directfbvideosink->foreign_surface->Flip (directfbvideosink->
          foreign_surface, NULL, 0);
    } else {
      gint width, height;
      gdouble video_ratio, screen_ratio;
      DFBRectangle dst;

      directfbvideosink->primary->GetSize (directfbvideosink->primary,
          &width, &height);

      video_ratio = (gdouble) directfbvideosink->video_width /
          directfbvideosink->video_height;
      screen_ratio = (gdouble) width / height;
      if (video_ratio > screen_ratio) {
        dst.w = width;
        dst.h = width / video_ratio;;
        dst.x = 0;
        dst.y = (height - dst.h) / 2;
      } else if (video_ratio < screen_ratio) {
        dst.w = height * screen_ratio;
        dst.h = height;
        dst.x = (width - dst.w) / 2;
        dst.y = 0;
      } else {
        dst.x = 0;
        dst.y = 0;
        dst.w = width;
        dst.h = height;
      }
      GST_DEBUG_OBJECT (directfbvideosink,
          "video output at %dx%d size %dx%d", dst.x, dst.y, dst.w, dst.h);
      directfbvideosink->primary->StretchBlit (directfbvideosink->primary,
          directfbvideosink->surface, NULL, &dst);

      directfbvideosink->primary->Flip (directfbvideosink->primary, NULL, 0);
    }
  }

  gst_buffer_unref (buf);
}

static void
gst_directfbvideosink_buffer_free (GstBuffer * buffer)
{
  GstDirectFBVideoSink *directfbvideosink;

  directfbvideosink = GST_BUFFER_PRIVATE (buffer);

  if ((directfbvideosink->surface) && (directfbvideosink->surface_locked)) {
    GST_DEBUG_OBJECT (directfbvideosink, "unlocking surface %p",
        directfbvideosink->surface);
    directfbvideosink->surface->Unlock (directfbvideosink->surface);
    directfbvideosink->surface_locked = FALSE;
  }
}

static GstBuffer *
gst_directfbvideosink_buffer_alloc (GstPad * pad, guint64 offset, guint size)
{
  GstDirectFBVideoSink *directfbvideosink;
  GstBuffer *buffer = NULL;

  directfbvideosink = GST_DIRECTFBVIDEOSINK (gst_pad_get_parent (pad));

  if ((directfbvideosink->surface) && (!directfbvideosink->surface_locked)) {
    DFBResult ret;
    void *data;
    gint pitch = 0;

    directfbvideosink->surface_locked = TRUE;
    GST_DEBUG_OBJECT (directfbvideosink, "locking surface %p",
        directfbvideosink->surface);
    ret = directfbvideosink->surface->Lock (directfbvideosink->surface,
        DSLF_WRITE, &data, &pitch);
    if (ret != DFB_OK) {
      GST_DEBUG_OBJECT (directfbvideosink, "failed locking surface %p",
          directfbvideosink->surface);
    }

    buffer = gst_buffer_new ();

    GST_BUFFER_PRIVATE (buffer) = directfbvideosink;
    GST_BUFFER_DATA (buffer) = data;
    GST_BUFFER_FREE_DATA_FUNC (buffer) = gst_directfbvideosink_buffer_free;
    GST_BUFFER_SIZE (buffer) = size;
  }

  return buffer;
}

/* Interfaces stuff */

static gboolean
gst_directfbvideosink_interface_supported (GstImplementsInterface * iface,
    GType type)
{
  g_assert (type == GST_TYPE_NAVIGATION);
  return TRUE;
}

static void
gst_directfbvideosink_interface_init (GstImplementsInterfaceClass * klass)
{
  klass->supported = gst_directfbvideosink_interface_supported;
}

static void
gst_directfbvideosink_navigation_send_event (GstNavigation * navigation,
    GstStructure * structure)
{
  GstDirectFBVideoSink *directfbvideosink = GST_DIRECTFBVIDEOSINK (navigation);
  GstEvent *event;
  gint width, height;
  double x, y;

  if (directfbvideosink->foreign_surface) {
    directfbvideosink->foreign_surface->GetSize (directfbvideosink->
        foreign_surface, &width, &height);
  } else if (directfbvideosink->primary) {
    directfbvideosink->primary->GetSize (directfbvideosink->primary,
        &width, &height);
  } else {
    /* We don't have any target to adjust coordinates */
    width = directfbvideosink->video_width;
    height = directfbvideosink->video_height;
  }

  event = gst_event_new (GST_EVENT_NAVIGATION);
  event->event_data.structure.structure = structure;

  /* Converting pointer coordinates to the non scaled geometry */
  if (gst_structure_get_double (structure, "pointer_x", &x)) {
    x *= directfbvideosink->video_width;
    x /= width;
    gst_structure_set (structure, "pointer_x", G_TYPE_DOUBLE, x, NULL);
  }
  if (gst_structure_get_double (structure, "pointer_y", &y)) {
    y *= directfbvideosink->video_height;
    y /= height;
    gst_structure_set (structure, "pointer_y", G_TYPE_DOUBLE, y, NULL);
  }

  gst_pad_send_event (gst_pad_get_peer (GST_VIDEOSINK_PAD (directfbvideosink)),
      event);
}

static void
gst_directfbvideosink_navigation_init (GstNavigationInterface * iface)
{
  iface->send_event = gst_directfbvideosink_navigation_send_event;
}

/* Properties */

static void
gst_directfbvideosink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstDirectFBVideoSink *directfbvideosink;

  g_return_if_fail (GST_IS_DIRECTFBVIDEOSINK (object));
  directfbvideosink = GST_DIRECTFBVIDEOSINK (object);

  switch (prop_id) {
    case ARG_SURFACE:
      directfbvideosink->foreign_surface = g_value_get_pointer (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_directfbvideosink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstDirectFBVideoSink *directfbvideosink;

  g_return_if_fail (GST_IS_DIRECTFBVIDEOSINK (object));
  directfbvideosink = GST_DIRECTFBVIDEOSINK (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* =========================================== */
/*                                             */
/*              Init & Class init              */
/*                                             */
/* =========================================== */

/* Finalize is called only once, dispose can be called multiple times. */
static void
gst_directfbvideosink_finalize (GObject * object)
{
  GstDirectFBVideoSink *directfbvideosink;

  directfbvideosink = GST_DIRECTFBVIDEOSINK (object);

  if (directfbvideosink->surface) {
    directfbvideosink->surface->Release (directfbvideosink->surface);
    directfbvideosink->surface = NULL;
  }

  if (directfbvideosink->primary) {
    directfbvideosink->primary->Release (directfbvideosink->primary);
    directfbvideosink->primary = NULL;
  }

  if (directfbvideosink->layer) {
    directfbvideosink->layer->Release (directfbvideosink->layer);
    directfbvideosink->layer = NULL;
  }

  if (directfbvideosink->directfb) {
    directfbvideosink->directfb->Release (directfbvideosink->directfb);
    directfbvideosink->directfb = NULL;
  }

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_directfbvideosink_init (GstDirectFBVideoSink * directfbvideosink)
{
  GST_VIDEOSINK_PAD (directfbvideosink) =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&gst_directfbvideosink_sink_template_factory), "sink");

  gst_element_add_pad (GST_ELEMENT (directfbvideosink),
      GST_VIDEOSINK_PAD (directfbvideosink));

  gst_pad_set_chain_function (GST_VIDEOSINK_PAD (directfbvideosink),
      gst_directfbvideosink_chain);
  gst_pad_set_link_function (GST_VIDEOSINK_PAD (directfbvideosink),
      gst_directfbvideosink_sink_link);
  gst_pad_set_getcaps_function (GST_VIDEOSINK_PAD (directfbvideosink),
      gst_directfbvideosink_getcaps);
  gst_pad_set_fixate_function (GST_VIDEOSINK_PAD (directfbvideosink),
      gst_directfbvideosink_fixate);
  gst_pad_set_bufferalloc_function (GST_VIDEOSINK_PAD (directfbvideosink),
      gst_directfbvideosink_buffer_alloc);

  directfbvideosink->pixel_format = DSPF_UNKNOWN;
  directfbvideosink->video_height = 0;
  directfbvideosink->video_width = 0;
  directfbvideosink->framerate = 0;

  directfbvideosink->directfb = NULL;
  directfbvideosink->layer = NULL;
  directfbvideosink->surface = NULL;
  directfbvideosink->foreign_surface = NULL;
  directfbvideosink->primary = NULL;
  directfbvideosink->internal_surface = FALSE;
  directfbvideosink->surface_locked = FALSE;

  GST_OBJECT_FLAG_SET (directfbvideosink, GST_ELEMENT_THREAD_SUGGESTED);
  GST_OBJECT_FLAG_SET (directfbvideosink, GST_ELEMENT_EVENT_AWARE);
}

static void
gst_directfbvideosink_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details (element_class, &gst_directfbvideosink_details);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get
      (&gst_directfbvideosink_sink_template_factory));
}

static void
gst_directfbvideosink_class_init (GstDirectFBVideoSinkClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_VIDEOSINK);

  g_object_class_install_property (G_OBJECT_CLASS (klass),
      ARG_SURFACE,
      g_param_spec_pointer ("surface", "Surface",
          "The target surface for video", G_PARAM_WRITABLE));

  gobject_class->finalize = gst_directfbvideosink_finalize;
  gobject_class->set_property = gst_directfbvideosink_set_property;
  gobject_class->get_property = gst_directfbvideosink_get_property;

  gstelement_class->change_state = gst_directfbvideosink_change_state;
}

/* ============================================================= */
/*                                                               */
/*                       Public Methods                          */
/*                                                               */
/* ============================================================= */

/* =========================================== */
/*                                             */
/*          Object typing & Creation           */
/*                                             */
/* =========================================== */

GType
gst_directfbvideosink_get_type (void)
{
  static GType directfbvideosink_type = 0;

  if (!directfbvideosink_type) {
    static const GTypeInfo directfbvideosink_info = {
      sizeof (GstDirectFBVideoSinkClass),
      gst_directfbvideosink_base_init,
      NULL,
      (GClassInitFunc) gst_directfbvideosink_class_init,
      NULL,
      NULL,
      sizeof (GstDirectFBVideoSink),
      0,
      (GInstanceInitFunc) gst_directfbvideosink_init,
    };
    static const GInterfaceInfo iface_info = {
      (GInterfaceInitFunc) gst_directfbvideosink_interface_init,
      NULL,
      NULL,
    };
    static const GInterfaceInfo navigation_info = {
      (GInterfaceInitFunc) gst_directfbvideosink_navigation_init,
      NULL,
      NULL,
    };

    directfbvideosink_type = g_type_register_static (GST_TYPE_VIDEOSINK,
        "GstDirectFBVideoSink", &directfbvideosink_info, 0);

    g_type_add_interface_static (directfbvideosink_type,
        GST_TYPE_IMPLEMENTS_INTERFACE, &iface_info);
    g_type_add_interface_static (directfbvideosink_type, GST_TYPE_NAVIGATION,
        &navigation_info);
  }

  return directfbvideosink_type;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  /* Loading the library containing GstVideoSink, our parent object */
  if (!gst_library_load ("gstvideo"))
    return FALSE;

  if (!gst_element_register (plugin, "directfbvideosink",
          GST_RANK_PRIMARY, GST_TYPE_DIRECTFBVIDEOSINK))
    return FALSE;

  GST_DEBUG_CATEGORY_INIT (gst_debug_directfbvideosink,
      "directfbvideosink", 0, "directfbvideosink element");

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "directfbvideosink",
    "DirectFB video output plugin",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE, GST_ORIGIN)
