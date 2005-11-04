/* GStreamer DirectFB plugin
 * Copyright (C) 2005 Julien MOUTTE <julien@moutte.net>
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
#include <gst/interfaces/navigation.h>

/* Object header */
#include "dfbvideosink.h"

#include <string.h>
#include <liboil/liboil.h>

/* Debugging category */
GST_DEBUG_CATEGORY (dfbvideosink_debug);
#define GST_CAT_DEFAULT dfbvideosink_debug

/* ElementFactory information */
static GstElementDetails gst_dfbvideosink_details =
GST_ELEMENT_DETAILS ("Video sink",
    "Sink/Video",
    "A DirectFB based videosink",
    "Julien Moutte <julien@moutte.net>");

/* Default template */
static GstStaticPadTemplate gst_dfbvideosink_sink_template_factory =
    GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw-rgb, "
        "framerate = (double) [ 0.0, MAX ], "
        "width = (int) [ 1, MAX ], "
        "height = (int) [ 1, MAX ]; "
        "video/x-raw-yuv, "
        "framerate = (double) [ 0.0, MAX ], "
        "width = (int) [ 1, MAX ], " "height = (int) [ 1, MAX ]")
    );

/* Signals and args */
enum
{
  ARG_0,
  ARG_SURFACE
};

static void gst_dfbvideosink_bufferpool_clear (GstDfbVideoSink * dfbvideosink);
static DFBSurfacePixelFormat gst_dfbvideosink_get_format_from_caps (GstCaps *
    caps);

static GstVideoSinkClass *parent_class = NULL;

static const char *
gst_dfbvideosink_get_format_name (DFBSurfacePixelFormat format)
{
  switch (format) {
    case DSPF_ARGB1555:
      return "ARGB1555";
    case DSPF_RGB16:
      return "RGB16";
    case DSPF_RGB24:
      return "RGB24";
    case DSPF_RGB32:
      return "RGB32";
    case DSPF_ARGB:
      return "ARGB";
    case DSPF_A8:
      return "A8";
    case DSPF_YUY2:
      return "YUY2";
    case DSPF_RGB332:
      return "RGB33";
    case DSPF_UYVY:
      return "UYVY";
    case DSPF_I420:
      return "I420";
    case DSPF_YV12:
      return "YV12";
    case DSPF_LUT8:
      return "LUT8";
    case DSPF_ALUT44:
      return "ALUT44";
    case DSPF_AiRGB:
      return "AiRGB";
    case DSPF_A1:
      return "A1";
    case DSPF_NV12:
      return "NV12";
    case DSPF_NV16:
      return "NV16";
    case DSPF_ARGB2554:
      return "ARGB2554";
    case DSPF_ARGB4444:
      return "ARGB4444";
    case DSPF_NV21:
      return "NV21";
    default:
      return "UNKNOWN";
  }
}

/* Creates miniobject and our internal surface */
static GstDfbSurface *
gst_dfbvideosink_surface_create (GstDfbVideoSink * dfbvideosink, GstCaps * caps,
    size_t size)
{
  GstDfbSurface *surface = NULL;
  GstStructure *structure = NULL;
  DFBResult ret;
  DFBSurfaceDescription s_dsc;
  gpointer data;
  gint pitch;

  g_return_val_if_fail (GST_IS_DFBVIDEOSINK (dfbvideosink), NULL);

  surface = (GstDfbSurface *) gst_mini_object_new (GST_TYPE_DFBSURFACE);

  surface->locked = FALSE;

  structure = gst_caps_get_structure (caps, 0);

  if (!gst_structure_get_int (structure, "width", &surface->width) ||
      !gst_structure_get_int (structure, "height", &surface->height)) {
    GST_WARNING ("failed getting geometry from caps %" GST_PTR_FORMAT, caps);
  }

  surface->pixel_format = gst_dfbvideosink_get_format_from_caps (caps);

  if (dfbvideosink->dfb) {
    /* Creating an internal surface which will be used as GstBuffer, we used
       the detected pixel format and video dimensions */

    s_dsc.flags =
        DSDESC_PIXELFORMAT | DSDESC_WIDTH | DSDESC_HEIGHT | DSDESC_CAPS;

    s_dsc.pixelformat = surface->pixel_format;
    s_dsc.width = surface->width;
    s_dsc.height = surface->height;
    s_dsc.caps = DSCAPS_VIDEOONLY;

    ret = dfbvideosink->dfb->CreateSurface (dfbvideosink->dfb, &s_dsc,
        &surface->surface);
    if (ret != DFB_OK) {
      GST_WARNING ("failed creating a DirectFB surface");
      gst_object_unref (surface);
      surface = NULL;
      goto beach;
    }

    /* Clearing surface */
    surface->surface->Clear (surface->surface, 0x00, 0x00, 0x00, 0xFF);

    /* Locking the surface to acquire the memory pointer */
    surface->surface->Lock (surface->surface, DSLF_WRITE, &data, &pitch);
    surface->locked = TRUE;
    GST_BUFFER_DATA (surface) = data;
    GST_BUFFER_SIZE (surface) = pitch * surface->height;

    GST_DEBUG ("creating a %dx%d surface with %s pixel format, line pitch %d",
        surface->width, surface->height,
        gst_dfbvideosink_get_format_name (surface->pixel_format), pitch);
  } else {
    GST_BUFFER (surface)->malloc_data = g_malloc (size);
    GST_BUFFER_DATA (surface) = GST_BUFFER (surface)->malloc_data;
    GST_BUFFER_SIZE (surface) = size;
    surface->surface = NULL;
    GST_DEBUG ("allocating a buffer of %d bytes", size);
  }

  /* Keep a ref to our sink */
  surface->dfbvideosink = gst_object_ref (dfbvideosink);

beach:
  return surface;
}

/* We are called from the finalize method of miniobject, the object will be
 * destroyed so we just have to clean our internal stuff */
static void
gst_dfbvideosink_surface_destroy (GstDfbVideoSink * dfbvideosink,
    GstDfbSurface * surface)
{
  g_return_if_fail (GST_IS_DFBVIDEOSINK (dfbvideosink));

  /* Release our internal surface */
  if (surface->surface) {
    if (surface->locked) {
      surface->surface->Unlock (surface->surface);
      surface->locked = FALSE;
    }
    surface->surface->Release (surface->surface);
    surface->surface = NULL;
  }

  if (GST_BUFFER (surface)->malloc_data) {
    g_free (GST_BUFFER (surface)->malloc_data);
    GST_BUFFER (surface)->malloc_data = NULL;
  }

  if (!surface->dfbvideosink) {
    goto no_sink;
  }

  /* Release the ref to our sink */
  surface->dfbvideosink = NULL;
  gst_object_unref (dfbvideosink);

  return;

no_sink:
  GST_WARNING ("no sink found in surface");
  return;
}

static gpointer
gst_dfbvideosink_event_thread (GstDfbVideoSink * dfbvideosink)
{
  DFBResult ret;

  while (dfbvideosink->running) {
    /* Wait for an event with a 50 ms timeout */
    dfbvideosink->event_buffer->WaitForEventWithTimeout (dfbvideosink->
        event_buffer, 0, 50);

    /* Do we have an event ? */
    ret = dfbvideosink->event_buffer->HasEvent (dfbvideosink->event_buffer);

    if (ret == DFB_OK) {
      DFBEvent event;

      GST_DEBUG ("we have an event");

      ret = dfbvideosink->event_buffer->GetEvent (dfbvideosink->event_buffer,
          &event);
      if (ret != DFB_OK) {      /* Error */
        GST_WARNING ("failed when getting event from event buffer");
      } else {                  /* Handle event */
        if (event.input.type == DIET_KEYPRESS) {
          switch (event.input.key_symbol) {
            case DIKS_ESCAPE:
            {
              GST_ELEMENT_ERROR (dfbvideosink, RESOURCE, OPEN_WRITE,
                  ("Video output device is gone."),
                  ("We were running fullscreen and user "
                      "pressed the ESC key, stopping playback."));
            }
            default:
              GST_DEBUG ("key press event %c !", event.input.key_symbol);
              gst_navigation_send_key_event (GST_NAVIGATION (dfbvideosink),
                  "key-press", "prout");
          }
        } else if (event.input.type == DIET_BUTTONPRESS) {
          gint x, y;

          dfbvideosink->layer->GetCursorPosition (dfbvideosink->layer, &x, &y);

          GST_DEBUG ("button %d pressed at %dx%d", event.input.button, x, y);

          gst_navigation_send_mouse_event (GST_NAVIGATION (dfbvideosink),
              "mouse-button-press", event.input.button, x, y);
        } else if (event.input.type == DIET_BUTTONRELEASE) {
          gint x, y;

          dfbvideosink->layer->GetCursorPosition (dfbvideosink->layer, &x, &y);

          GST_DEBUG ("button %d released at %dx%d", event.input.button, x, y);

          gst_navigation_send_mouse_event (GST_NAVIGATION (dfbvideosink),
              "mouse-button-release", event.input.button, x, y);
        } else if (event.input.type == DIET_AXISMOTION) {
          /* Mouse moves have no abs nor rel values */
          if ((event.input.flags & DIEF_AXISABS) ||
              (event.input.flags & DIEF_AXISREL)) {
            GST_DEBUG ("joypad move ?");
          } else {
            gint x, y;

            dfbvideosink->layer->GetCursorPosition (dfbvideosink->layer, &x,
                &y);
            gst_navigation_send_mouse_event (GST_NAVIGATION (dfbvideosink),
                "mouse-move", 0, x, y);
          }
        } else {
          GST_WARNING ("unhandled event type %d", event.input.type);
        }
      }
    }
  }
  return NULL;
}

static DFBEnumerationResult
gst_dfbvideosink_enum_layers (DFBDisplayLayerID id,
    DFBDisplayLayerDescription desc, void *data)
{
  GstDfbVideoSink *dfbvideosink = NULL;
  IDirectFBDisplayLayer *layer = NULL;
  DFBDisplayLayerConfig dlc;
  DFBResult ret;
  gboolean backbuffer = FALSE;

  g_return_val_if_fail (GST_IS_DFBVIDEOSINK (data), DFENUM_CANCEL);

  dfbvideosink = GST_DFBVIDEOSINK (data);

  GST_DEBUG ("inspecting display layer %d with name: %s", id, desc.name);

  if ((desc.type & DLTF_VIDEO) && (desc.caps & DLCAPS_SURFACE)) {
    GST_DEBUG ("this layer can handle live video and has a surface");
    goto beach;                 /* It seems that kind of overlay is not very well supported */
  } else {
    if (desc.caps & DLCAPS_SURFACE) {
      GST_DEBUG ("this layer can not handle live video but has a surface");
    } else {
      GST_DEBUG ("no we can't use that layer, really...");
      goto beach;
    }
  }

  ret = dfbvideosink->dfb->GetDisplayLayer (dfbvideosink->dfb, id, &layer);
  if (ret != DFB_OK) {
    GST_WARNING ("failed getting display layer %s", desc.name);
    goto beach;
  }

  ret = layer->GetConfiguration (layer, &dlc);
  if (ret != DFB_OK) {
    GST_WARNING ("failed getting display layer configuration");
    goto beach;
  }

  if ((dlc.flags & DLCONF_BUFFERMODE) && (dlc.buffermode & DLBM_FRONTONLY)) {
    GST_DEBUG ("no backbuffer");
  }
  if ((dlc.flags & DLCONF_BUFFERMODE) && (dlc.buffermode & DLBM_BACKVIDEO)) {
    GST_DEBUG ("backbuffer is in video memory");
    backbuffer = TRUE;
  }
  if ((dlc.flags & DLCONF_BUFFERMODE) && (dlc.buffermode & DLBM_BACKSYSTEM)) {
    GST_DEBUG ("backbuffer is in system memory");
    backbuffer = TRUE;
  }
  if ((dlc.flags & DLCONF_BUFFERMODE) && (dlc.buffermode & DLBM_TRIPLE)) {
    GST_DEBUG ("triple buffering");
    backbuffer = TRUE;
  }

  dfbvideosink->backbuffer = backbuffer;
  dfbvideosink->layer_id = id;

beach:
  if (layer) {
    layer->Release (layer);
  }
  return DFENUM_OK;
}

static DFBEnumerationResult
gst_dfbvideosink_enum_vmodes (gint width, gint height, gint bpp, void *data)
{
  GstDfbVideoSink *dfbvideosink = NULL;
  GstDfbVMode *vmode = NULL;

  g_return_val_if_fail (GST_IS_DFBVIDEOSINK (data), DFENUM_CANCEL);

  dfbvideosink = GST_DFBVIDEOSINK (data);

  GST_DEBUG ("adding video mode %dx%d at %d bpp", width, height, bpp);
  vmode = g_new0 (GstDfbVMode, 1);
  vmode->width = width;
  vmode->height = height;
  vmode->bpp = bpp;

  /* We need to know the maximum video geometry we can accept for the caps */
  if (width > dfbvideosink->out_width) {
    dfbvideosink->out_width = width;
  }
  if (height > dfbvideosink->out_height) {
    dfbvideosink->out_height = height;
  }

  dfbvideosink->vmodes = g_slist_append (dfbvideosink->vmodes, vmode);

  return DFENUM_OK;
}

static DFBEnumerationResult
gst_dfbvideosink_enum_devices (DFBInputDeviceID id,
    DFBInputDeviceDescription desc, void *data)
{
  GstDfbVideoSink *dfbvideosink = NULL;
  IDirectFBInputDevice *device = NULL;
  DFBResult ret;

  g_return_val_if_fail (GST_IS_DFBVIDEOSINK (data), DFENUM_CANCEL);

  dfbvideosink = GST_DFBVIDEOSINK (data);

  GST_DEBUG ("detected input device %s from vendor %s", desc.name, desc.vendor);

  /* Get that input device */
  ret = dfbvideosink->dfb->GetInputDevice (dfbvideosink->dfb, id, &device);
  if (ret != DFB_OK) {
    GST_WARNING ("failed when getting input device id %d", id);
    goto beach;
  }

  ret = device->AttachEventBuffer (device, dfbvideosink->event_buffer);
  if (ret != DFB_OK) {
    GST_WARNING ("failed when attaching input device %d to our event buffer",
        id);
  }

beach:
  if (device) {
    device->Release (device);
  }
  return DFENUM_OK;
}

static gboolean
gst_dfbvideosink_setup (GstDfbVideoSink * dfbvideosink)
{
  DFBResult ret;

  g_return_val_if_fail (GST_IS_DFBVIDEOSINK (dfbvideosink), FALSE);

  dfbvideosink->video_width = 0;
  dfbvideosink->video_height = 0;
  dfbvideosink->out_width = 0;
  dfbvideosink->out_height = 0;
  dfbvideosink->framerate = 0.0;
  dfbvideosink->hw_scaling = FALSE;
  dfbvideosink->backbuffer = FALSE;
  dfbvideosink->pixel_format = DSPF_UNKNOWN;

  /* If we do it all by ourself we create the DirectFB context, get the 
     primary layer and use a fullscreen configuration */
  if (!dfbvideosink->ext_surface) {
    GST_DEBUG ("no external surface, taking over DirectFB fullscreen");
    if (!dfbvideosink->dfb) {
      DFBGraphicsDeviceDescription hw_caps;

      GST_DEBUG ("initializing DirectFB");

      ret = DirectFBInit (0, NULL);

      if (ret != DFB_OK) {
        GST_WARNING ("DirectFB initialization failed");
        goto beach;
      }

      ret = DirectFBCreate (&(dfbvideosink->dfb));

      if (ret != DFB_OK) {
        GST_WARNING ("failed creating the DirectFB main object");
        goto beach;
      }

      /* Get Hardwared capabilities */
      ret = dfbvideosink->dfb->GetDeviceDescription (dfbvideosink->dfb,
          &hw_caps);

      if (ret != DFB_OK) {
        GST_WARNING ("failed grabbing the hardware capabilities");
        goto beach;
      }

      GST_DEBUG ("video card %s from vendor %s detected with %d bytes of "
          "video memory", hw_caps.name, hw_caps.vendor, hw_caps.video_memory);

      if (hw_caps.acceleration_mask & DFXL_BLIT) {
        GST_DEBUG ("Blit is accelerated");
      }
      if (hw_caps.acceleration_mask & DFXL_STRETCHBLIT) {
        GST_DEBUG ("StretchBlit is accelerated");
        dfbvideosink->hw_scaling = TRUE;
      } else {
        GST_DEBUG ("StretchBlit is not accelerated");
        dfbvideosink->hw_scaling = FALSE;
      }

      dfbvideosink->layer_id = -1;

      /* Inspect all the Display layers */
      dfbvideosink->dfb->EnumDisplayLayers (dfbvideosink->dfb,
          gst_dfbvideosink_enum_layers, dfbvideosink);
      /* Inspect all Video modes */
      dfbvideosink->dfb->EnumVideoModes (dfbvideosink->dfb,
          gst_dfbvideosink_enum_vmodes, dfbvideosink);

      /* Create an event buffer for input */
      dfbvideosink->dfb->CreateEventBuffer (dfbvideosink->dfb,
          &dfbvideosink->event_buffer);

      /* Inspect all Input devices */
      dfbvideosink->dfb->EnumInputDevices (dfbvideosink->dfb,
          gst_dfbvideosink_enum_devices, dfbvideosink);
      /* Create a thread to handle those events */
      dfbvideosink->event_thread = g_thread_create (
          (GThreadFunc) gst_dfbvideosink_event_thread,
          dfbvideosink, TRUE, NULL);
    }
    if (!dfbvideosink->layer) {
      /* Get the best Display Layer */
      ret = dfbvideosink->dfb->GetDisplayLayer (dfbvideosink->dfb,
          dfbvideosink->layer_id, &dfbvideosink->layer);
      if (ret != DFB_OK) {
        GST_WARNING ("failed getting display layer");
        goto beach;
      }

      ret = dfbvideosink->layer->SetCooperativeLevel (dfbvideosink->layer,
          DLSCL_EXCLUSIVE);

      if (ret != DFB_OK) {
        GST_WARNING ("failed setting display layer to fullscreen mode");
        goto beach;
      }

      dfbvideosink->layer->SetBackgroundColor (dfbvideosink->layer,
          0x00, 0x00, 0x00, 0xFF);

      dfbvideosink->layer->EnableCursor (dfbvideosink->layer, TRUE);

      GST_DEBUG ("getting primary surface");
      dfbvideosink->layer->GetSurface (dfbvideosink->layer,
          &dfbvideosink->primary);
    }

    dfbvideosink->primary->GetPixelFormat (dfbvideosink->primary,
        &dfbvideosink->pixel_format);
  } else {
    DFBSurfaceCapabilities s_caps;

    GST_DEBUG ("getting pixel format from foreign surface %p",
        dfbvideosink->ext_surface);
    dfbvideosink->ext_surface->GetPixelFormat (dfbvideosink->ext_surface,
        &dfbvideosink->pixel_format);
    dfbvideosink->ext_surface->GetSize (dfbvideosink->ext_surface,
        &dfbvideosink->out_width, &dfbvideosink->out_height);
    dfbvideosink->ext_surface->GetCapabilities (dfbvideosink->ext_surface,
        &s_caps);
    if ((s_caps & DSCAPS_DOUBLE) || (s_caps & DSCAPS_TRIPLE)) {
      dfbvideosink->backbuffer = TRUE;
    } else {
      dfbvideosink->backbuffer = FALSE;
    }
    GST_DEBUG ("external surface is %dx%d and uses %s pixel format",
        dfbvideosink->out_width, dfbvideosink->out_height,
        gst_dfbvideosink_get_format_name (dfbvideosink->pixel_format));
  }

  dfbvideosink->setup = TRUE;

beach:
  return dfbvideosink->setup;
}

static void
gst_dfbvideosink_cleanup (GstDfbVideoSink * dfbvideosink)
{
  g_return_if_fail (GST_IS_DFBVIDEOSINK (dfbvideosink));

  GST_DEBUG ("cleaning up DirectFB environment");

  /* Wait for our event thread */
  if (dfbvideosink->event_thread) {
    g_thread_join (dfbvideosink->event_thread);
    dfbvideosink->event_thread = NULL;
  }

  if (dfbvideosink->event_buffer) {
    dfbvideosink->event_buffer->Release (dfbvideosink->event_buffer);
    dfbvideosink->event_buffer = NULL;
  }

  if (dfbvideosink->vmodes) {
    GSList *walk = dfbvideosink->vmodes;

    while (walk) {
      g_free (walk->data);
      walk = g_slist_next (walk);
    }
    g_slist_free (dfbvideosink->vmodes);
    dfbvideosink->vmodes = NULL;
  }

  if (dfbvideosink->buffer_pool) {
    gst_dfbvideosink_bufferpool_clear (dfbvideosink);
  }

  if (dfbvideosink->primary) {
    dfbvideosink->primary->Release (dfbvideosink->primary);
    dfbvideosink->primary = NULL;
  }

  if (dfbvideosink->layer) {
    dfbvideosink->layer->EnableCursor (dfbvideosink->layer, FALSE);
    dfbvideosink->layer->Release (dfbvideosink->layer);
    dfbvideosink->layer = NULL;
  }

  if (dfbvideosink->dfb) {
    dfbvideosink->dfb->Release (dfbvideosink->dfb);
    dfbvideosink->dfb = NULL;
  }

  dfbvideosink->setup = FALSE;
}

static DFBSurfacePixelFormat
gst_dfbvideosink_get_format_from_caps (GstCaps * caps)
{
  GstStructure *structure;
  gboolean ret;
  DFBSurfacePixelFormat pixel_format = DSPF_UNKNOWN;

  g_return_val_if_fail (GST_IS_CAPS (caps), DSPF_UNKNOWN);

  structure = gst_caps_get_structure (caps, 0);

  if (gst_structure_has_name (structure, "video/x-raw-rgb")) {
    gint bpp, depth;

    ret = gst_structure_get_int (structure, "bpp", &bpp);
    ret &= gst_structure_get_int (structure, "depth", &depth);

    if (!ret) {
      goto beach;
    }

    switch (bpp) {
      case 16:
        pixel_format = DSPF_RGB16;
        break;
      case 24:
        pixel_format = DSPF_RGB24;
        break;
      case 32:
        if (depth == 24) {
          pixel_format = DSPF_RGB32;
        } else if (depth == 32) {
          pixel_format = DSPF_ARGB;
        } else {
          goto beach;
        }
        break;
      default:
        GST_WARNING ("unhandled RGB format, bpp %d, depth %d", bpp, depth);
        goto beach;
    }
  } else if (gst_structure_has_name (structure, "video/x-raw-yuv")) {
    guint32 fourcc;

    ret = gst_structure_get_fourcc (structure, "format", &fourcc);

    if (!ret) {
      GST_WARNING ("failed grabbing fourcc from caps %" GST_PTR_FORMAT, caps);
      goto beach;
    }

    switch (fourcc) {
      case GST_MAKE_FOURCC ('I', '4', '2', '0'):
        pixel_format = DSPF_I420;
        break;
      case GST_MAKE_FOURCC ('Y', 'V', '1', '2'):
        pixel_format = DSPF_YV12;
        break;
      case GST_MAKE_FOURCC ('Y', 'U', 'Y', '2'):
        pixel_format = DSPF_YUY2;
        break;
      case GST_MAKE_FOURCC ('U', 'Y', 'V', 'Y'):
        pixel_format = DSPF_UYVY;
        break;
      default:
        GST_WARNING ("unhandled YUV format" GST_FOURCC_FORMAT,
            GST_FOURCC_ARGS (fourcc));
        goto beach;
    }
  } else {
    GST_WARNING ("unknown caps name received %" GST_PTR_FORMAT, caps);
    goto beach;
  }

beach:
  return pixel_format;
}

static GstCaps *
gst_dfbvideosink_get_caps_from_format (DFBSurfacePixelFormat format)
{
  GstCaps *caps = NULL;
  gboolean is_rgb = FALSE, is_yuv = FALSE;
  gint bpp, depth;
  guint32 fourcc;

  g_return_val_if_fail (format != DSPF_UNKNOWN, NULL);

  switch (format) {
    case DSPF_RGB16:
      is_rgb = TRUE;
      bpp = 16;
      depth = 16;
      break;
    case DSPF_RGB24:
      is_rgb = TRUE;
      bpp = 24;
      depth = 24;
      break;
    case DSPF_RGB32:
      is_rgb = TRUE;
      bpp = 32;
      depth = 24;
      break;
    case DSPF_ARGB:
      is_rgb = TRUE;
      bpp = 32;
      depth = 32;
      break;
    case DSPF_YUY2:
      is_yuv = TRUE;
      fourcc = GST_MAKE_FOURCC ('Y', 'U', 'Y', '2');
      break;
    case DSPF_UYVY:
      is_yuv = TRUE;
      fourcc = GST_MAKE_FOURCC ('U', 'Y', 'V', 'Y');
      break;
    case DSPF_I420:
      is_yuv = TRUE;
      fourcc = GST_MAKE_FOURCC ('I', '4', '2', '0');
      break;
    case DSPF_YV12:
      is_yuv = TRUE;
      fourcc = GST_MAKE_FOURCC ('Y', 'V', '1', '2');
      break;
    default:
      GST_WARNING ("unknown pixel format %s",
          gst_dfbvideosink_get_format_name (format));
      goto beach;
  }

  if (is_rgb) {
    caps = gst_caps_new_simple ("video/x-raw-rgb",
        "bpp", G_TYPE_INT, bpp, "depth", G_TYPE_INT, depth, NULL);
  } else if (is_yuv) {
    caps = gst_caps_new_simple ("video/x-raw-yuv",
        "format", GST_TYPE_FOURCC, fourcc, NULL);
  } else {
    GST_WARNING ("neither rgb nor yuv, something strange here");
  }

beach:
  return caps;
}

static gboolean
gst_dfbvideosink_can_blit_from_format (GstDfbVideoSink * dfbvideosink,
    DFBSurfacePixelFormat format)
{
  gboolean res = FALSE;
  DFBResult ret;
  IDirectFBSurface *surface = NULL;
  DFBSurfaceDescription s_dsc;
  DFBAccelerationMask mask;
  DFBDisplayLayerConfig dlc;

  g_return_val_if_fail (GST_IS_DFBVIDEOSINK (dfbvideosink), FALSE);

  /* Create a surface of desired format */
  s_dsc.flags = DSDESC_PIXELFORMAT | DSDESC_WIDTH | DSDESC_HEIGHT;
  s_dsc.pixelformat = format;
  s_dsc.width = 1;
  s_dsc.height = 1;

  ret = dfbvideosink->dfb->CreateSurface (dfbvideosink->dfb, &s_dsc, &surface);
  if (ret != DFB_OK) {
    GST_WARNING ("failed creating surface with format %s",
        gst_dfbvideosink_get_format_name (format));
    goto beach;
  }

  /* Test configuration of the layer to this pixel format */
  dlc.flags = DLCONF_PIXELFORMAT;
  dlc.pixelformat = format;

  ret = dfbvideosink->layer->TestConfiguration (dfbvideosink->layer, &dlc,
      NULL);
  if (ret != DFB_OK) {
    GST_DEBUG ("our layer refuses to operate in pixel format %s",
        gst_dfbvideosink_get_format_name (format));
    goto beach;
  }

  ret = dfbvideosink->layer->SetConfiguration (dfbvideosink->layer, &dlc);
  if (ret != DFB_OK) {
    GST_WARNING ("our layer refuses to operate in pixel format, though this "
        "format was successfully tested earlied %s",
        gst_dfbvideosink_get_format_name (format));
    goto beach;
  }

  ret = dfbvideosink->primary->GetAccelerationMask (dfbvideosink->primary,
      surface, &mask);
  if (ret != DFB_OK) {
    GST_WARNING ("failed getting acceleration mask");
    goto beach;
  }

  /* Blitting from this format to our primary is accelerated */
  if (mask & DFXL_BLIT) {
    GST_DEBUG ("blitting from format %s to our primary is accelerated",
        gst_dfbvideosink_get_format_name (format));
    res = TRUE;
  } else {
    GST_DEBUG ("blitting from format %s to our primary is not accelerated",
        gst_dfbvideosink_get_format_name (format));
    res = TRUE;
  }

beach:
  if (surface) {
    surface->Release (surface);
  }
  return res;
}

static gboolean
gst_dfbvideosink_get_best_vmode (GstDfbVideoSink * dfbvideosink, gint v_width,
    gint v_height, GstDfbVMode * best_vmode)
{
  GSList *walk = NULL;
  gboolean ret = FALSE;
  gint width, height, bpp;
  GstDfbVMode *vmode = NULL;

  g_return_val_if_fail (GST_IS_DFBVIDEOSINK (dfbvideosink), FALSE);

  if (!dfbvideosink->vmodes) {
    goto beach;
  }

  walk = dfbvideosink->vmodes;

  vmode = (GstDfbVMode *) walk->data;

  /* First mode */
  width = vmode->width;
  height = vmode->height;
  bpp = vmode->bpp;

  while (walk) {
    gint wgap, hgap, best_wgap, best_hgap;

    vmode = (GstDfbVMode *) walk->data;

    /* What are the gaps */
    wgap = abs (vmode->width - v_width);
    hgap = abs (vmode->height - v_height);
    best_wgap = abs (width - v_width);
    best_hgap = abs (height - v_height);

    /* If this mode is better we ll use that */
    if (wgap + hgap < best_wgap + best_hgap) {
      width = vmode->width;
      height = vmode->height;
      bpp = vmode->bpp;
    }

    walk = g_slist_next (walk);
  }

  GST_DEBUG ("found video mode %dx%d for input at %dx%d", width, height,
      v_width, v_height);

  best_vmode->width = width;
  best_vmode->height = height;
  best_vmode->bpp = bpp;

  ret = TRUE;

beach:
  return ret;
}

static GstCaps *
gst_dfbvideosink_getcaps (GstBaseSink * bsink)
{
  GstDfbVideoSink *dfbvideosink;
  GstCaps *caps = NULL;
  gint i;

  dfbvideosink = GST_DFBVIDEOSINK (bsink);

  if (!dfbvideosink->setup) {
    caps = gst_caps_copy (gst_pad_get_pad_template_caps (GST_VIDEO_SINK_PAD
            (dfbvideosink)));
    GST_DEBUG ("getcaps called and we are not setup yet, "
        "returning template %" GST_PTR_FORMAT, caps);
    goto beach;
  } else {
    GST_DEBUG ("getcaps called, checking our internal format");
    if (dfbvideosink->ext_surface) {
      /* We are not rendering to our own surface, returning this surface's
       *  pixel format */
      caps = gst_dfbvideosink_get_caps_from_format (dfbvideosink->pixel_format);
    } else {
      /* Try some formats */
      caps = gst_caps_new_empty ();

      if (gst_dfbvideosink_can_blit_from_format (dfbvideosink, DSPF_RGB16)) {
        gst_caps_append (caps,
            gst_dfbvideosink_get_caps_from_format (DSPF_RGB16));
      }
      if (gst_dfbvideosink_can_blit_from_format (dfbvideosink, DSPF_RGB24)) {
        gst_caps_append (caps,
            gst_dfbvideosink_get_caps_from_format (DSPF_RGB24));
      }
      if (gst_dfbvideosink_can_blit_from_format (dfbvideosink, DSPF_YUY2)) {
        gst_caps_append (caps,
            gst_dfbvideosink_get_caps_from_format (DSPF_YUY2));
      }
      if (gst_dfbvideosink_can_blit_from_format (dfbvideosink, DSPF_UYVY)) {
        gst_caps_append (caps,
            gst_dfbvideosink_get_caps_from_format (DSPF_UYVY));
      }
      if (gst_dfbvideosink_can_blit_from_format (dfbvideosink, DSPF_I420)) {
        gst_caps_append (caps,
            gst_dfbvideosink_get_caps_from_format (DSPF_I420));
      }
      if (gst_dfbvideosink_can_blit_from_format (dfbvideosink, DSPF_YV12)) {
        gst_caps_append (caps,
            gst_dfbvideosink_get_caps_from_format (DSPF_YV12));
      }
    }
  }

  for (i = 0; i < gst_caps_get_size (caps); i++) {
    GstStructure *structure = gst_caps_get_structure (caps, i);

    gst_structure_set (structure,
        "width", GST_TYPE_INT_RANGE, 1, G_MAXINT,
        "height", GST_TYPE_INT_RANGE, 1, G_MAXINT,
        "framerate", GST_TYPE_DOUBLE_RANGE, 0.0, G_MAXDOUBLE, NULL);
  }

  GST_DEBUG ("returning our caps %" GST_PTR_FORMAT, caps);

beach:
  return caps;
}

static gboolean
gst_dfbvideosink_setcaps (GstBaseSink * bsink, GstCaps * caps)
{
  GstDfbVideoSink *dfbvideosink;
  GstStructure *structure;
  gboolean res, result = FALSE;
  gint video_width, video_height;
  DFBSurfacePixelFormat pixel_format = DSPF_UNKNOWN;

  dfbvideosink = GST_DFBVIDEOSINK (bsink);

  structure = gst_caps_get_structure (caps, 0);
  res = gst_structure_get_int (structure, "width", &video_width);
  res &= gst_structure_get_int (structure, "height", &video_height);
  res &= gst_structure_get_double (structure, "framerate",
      &dfbvideosink->framerate);
  if (!res) {
    goto beach;
  }

  pixel_format = gst_dfbvideosink_get_format_from_caps (caps);

  GST_DEBUG ("setcaps called, %dx%d %s video at %f fps", video_width,
      video_height, gst_dfbvideosink_get_format_name (pixel_format),
      dfbvideosink->framerate);

  /* Try to adapt the video mode to the video geometry */
  if (dfbvideosink->dfb) {
    DFBResult ret;
    GstDfbVMode vmode;

    GST_DEBUG ("trying to adapt the video mode to video geometry");

    /* Set video mode and layer configuration appropriately */
    if (gst_dfbvideosink_get_best_vmode (dfbvideosink, video_width,
            video_height, &vmode)) {
      DFBDisplayLayerConfig lc;
      gint width, height, bpp;

      width = vmode.width;
      height = vmode.height;
      bpp = vmode.bpp;

      GST_DEBUG ("setting video mode to %dx%d at %d bpp", width, height, bpp);

      ret = dfbvideosink->dfb->SetVideoMode (dfbvideosink->dfb, width,
          height, bpp);
      if (ret != DFB_OK) {
        GST_WARNING ("failed setting video mode %dx%d at %d bpp", width,
            height, bpp);
      }

      lc.flags = DLCONF_WIDTH | DLCONF_HEIGHT | DLCONF_PIXELFORMAT;
      lc.width = width;
      lc.height = height;
      lc.pixelformat = pixel_format;

      ret = dfbvideosink->layer->SetConfiguration (dfbvideosink->layer, &lc);
      if (ret != DFB_OK) {
        GST_WARNING ("failed setting layer configuration to  %dx%d", width,
            height);
      } else {
        dfbvideosink->out_width = width;
        dfbvideosink->out_height = height;
        dfbvideosink->pixel_format = pixel_format;
      }
    }
  }

  if (pixel_format != dfbvideosink->pixel_format) {
    GST_WARNING ("setcaps sent us a different pixel format %s",
        gst_dfbvideosink_get_format_name (pixel_format));
    goto beach;
  }

  dfbvideosink->video_width = video_width;
  dfbvideosink->video_height = video_height;

  result = TRUE;

beach:
  return result;
}

static GstStateChangeReturn
gst_dfbvideosink_change_state (GstElement * element, GstStateChange transition)
{
  GstDfbVideoSink *dfbvideosink;
  GstStateChangeReturn ret;

  dfbvideosink = GST_DFBVIDEOSINK (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      dfbvideosink->running = TRUE;
      if (!dfbvideosink->setup) {
        gst_dfbvideosink_setup (dfbvideosink);
      }
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      /* Blank surface if we have one */
      if (dfbvideosink->ext_surface) {
        dfbvideosink->ext_surface->Clear (dfbvideosink->ext_surface,
            0x00, 0x00, 0x00, 0xFF);
      }
      if (dfbvideosink->primary) {
        dfbvideosink->primary->Clear (dfbvideosink->primary, 0x00, 0x00,
            0x00, 0xFF);
      }
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      break;
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      dfbvideosink->framerate = 0;
      dfbvideosink->video_width = 0;
      dfbvideosink->video_height = 0;

      if (dfbvideosink->buffer_pool) {
        gst_dfbvideosink_bufferpool_clear (dfbvideosink);
      }
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      dfbvideosink->running = FALSE;
      if (dfbvideosink->setup) {
        gst_dfbvideosink_cleanup (dfbvideosink);
      }
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  return ret;
}

static void
gst_dfbvideosink_get_times (GstBaseSink * bsink, GstBuffer * buf,
    GstClockTime * start, GstClockTime * end)
{
  GstDfbVideoSink *dfbvideosink;

  dfbvideosink = GST_DFBVIDEOSINK (bsink);

  if (GST_BUFFER_TIMESTAMP_IS_VALID (buf)) {
    *start = GST_BUFFER_TIMESTAMP (buf);
    if (GST_BUFFER_DURATION_IS_VALID (buf)) {
      *end = *start + GST_BUFFER_DURATION (buf);
    } else {
      if (dfbvideosink->framerate > 0) {
        *end = *start + GST_SECOND / dfbvideosink->framerate;
      }
    }
  }
}

static void
gst_dfbvideosink_center_rect (DFBRectangle src, DFBRectangle dst,
    DFBRectangle * result, gboolean scale)
{
  g_return_if_fail (result != NULL);

  if (!scale) {
    result->w = MIN (src.w, dst.w);
    result->h = MIN (src.h, dst.h);
    result->x = (dst.w - result->w) / 2;
    result->y = (dst.h - result->h) / 2;
  } else {
    gdouble src_ratio, dst_ratio;

    src_ratio = (gdouble) src.w / src.h;
    dst_ratio = (gdouble) dst.w / dst.h;

    if (src_ratio > dst_ratio) {
      result->w = dst.w;
      result->h = dst.w / src_ratio;
      result->x = 0;
      result->y = (dst.h - result->h) / 2;
    } else if (src_ratio < dst_ratio) {
      result->w = dst.h * src_ratio;
      result->h = dst.h;
      result->x = (dst.w - result->w) / 2;
      result->y = 0;
    } else {
      result->x = 0;
      result->y = 0;
      result->w = dst.w;
      result->h = dst.h;
    }
  }

  GST_DEBUG ("source is %dx%d dest is %dx%d, result is %dx%d with x,y %dx%d",
      src.w, src.h, dst.w, dst.h, result->w, result->h, result->x, result->y);
}

static GstFlowReturn
gst_dfbvideosink_show_frame (GstBaseSink * bsink, GstBuffer * buf)
{
  GstDfbVideoSink *dfbvideosink = NULL;
  DFBResult res;
  DFBRectangle dst, src, result;
  GstFlowReturn ret = GST_FLOW_OK;

  dfbvideosink = GST_DFBVIDEOSINK (bsink);

  if (!dfbvideosink->setup) {
    ret = GST_FLOW_UNEXPECTED;
    goto beach;
  }

  if (GST_IS_DFBSURFACE (buf)) {
    GstDfbSurface *surface = GST_DFBSURFACE (buf);

    src.w = surface->width;
    src.h = surface->height;
  } else {
    src.w = dfbvideosink->video_width;
    src.h = dfbvideosink->video_height;
  }

  /* If we are rendering from a buffer we did not allocate or to an external
   * surface, we will memcpy data */
  if (!GST_IS_DFBSURFACE (buf) || dfbvideosink->ext_surface) {
    IDirectFBSurface *dest = NULL, *surface = NULL;
    gpointer data;
    gint dest_pitch, src_pitch, line;

    /* As we are not blitting no acceleration is possible. If the surface is
     * too small we do clipping, if it's too big we center. Theoretically as 
     * we are using buffer_alloc, there's a chance that we have been able to 
     * do reverse caps negotiation */

    if (dfbvideosink->ext_surface) {
      surface = dfbvideosink->ext_surface;
      GST_DEBUG ("memcpy to an external surface subsurface");
    } else {
      surface = dfbvideosink->primary;
      GST_DEBUG ("memcpy to a primary subsurface");
    }

    res = surface->GetSize (surface, &dst.w, &dst.h);

    /* Center / Clip */
    gst_dfbvideosink_center_rect (src, dst, &result, FALSE);

    res = surface->GetSubSurface (surface, &result, &dest);
    if (res != DFB_OK) {
      GST_WARNING ("failed when getting a sub surface");
      ret = GST_FLOW_UNEXPECTED;
      goto beach;
    }

    res = dest->Lock (dest, DSLF_WRITE, &data, &dest_pitch);
    if (res != DFB_OK) {
      GST_WARNING ("failed locking the external subsurface for writing");
      ret = GST_FLOW_ERROR;
      goto beach;
    }

    /* Source video rowbytes */
    src_pitch = GST_BUFFER_SIZE (buf) / src.h;

    /* Write each line respecting subsurface pitch */
    for (line = 0; line < result.h; line++) {
      /* We do clipping */
      memcpy (data, GST_BUFFER_DATA (buf) + (line * src_pitch),
          MIN (src_pitch, dest_pitch));
      data += dest_pitch;
    }

    res = dest->Unlock (dest);

    res = dest->Release (dest);

    if (dfbvideosink->backbuffer) {
      res = surface->Flip (surface, NULL, 0);
    }
  } else if (dfbvideosink->primary) {
    /* Else we will [Stretch]Blit to our primary */
    GstDfbSurface *surface = GST_DFBSURFACE (buf);

    GST_DEBUG ("blitting to a primary surface");

    dfbvideosink->primary->GetSize (dfbvideosink->primary, &dst.w, &dst.h);

    /* Unlocking surface before blit */
    surface->surface->Unlock (surface->surface);
    surface->locked = FALSE;

    gst_dfbvideosink_center_rect (src, dst, &result, dfbvideosink->hw_scaling);

    if (dfbvideosink->hw_scaling) {
      dfbvideosink->primary->StretchBlit (dfbvideosink->primary,
          surface->surface, NULL, &result);
    } else {
      DFBRectangle clip;

      clip.x = clip.y = 0;
      clip.w = result.w;
      clip.h = result.h;
      dfbvideosink->primary->Blit (dfbvideosink->primary, surface->surface,
          &clip, result.x, result.y);
    }

    if (dfbvideosink->backbuffer) {
      dfbvideosink->primary->Flip (dfbvideosink->primary, NULL, 0);
    }
  } else {
    GST_WARNING ("no primary, no external surface what's going on ?");
    ret = GST_FLOW_UNEXPECTED;
    goto beach;
  }

beach:
  return ret;
}

static void
gst_dfbvideosink_bufferpool_clear (GstDfbVideoSink * dfbvideosink)
{
  while (dfbvideosink->buffer_pool) {
    GstDfbSurface *surface = dfbvideosink->buffer_pool->data;

    dfbvideosink->buffer_pool = g_slist_delete_link (dfbvideosink->buffer_pool,
        dfbvideosink->buffer_pool);
    gst_dfbvideosink_surface_destroy (dfbvideosink, surface);
  }
}

/* For every buffer request we create a custom buffer containing and
 * IDirectFBSurface or allocate a previously created one that's not used
 * anymore. */
static GstFlowReturn
gst_dfbvideosink_buffer_alloc (GstBaseSink * bsink, guint64 offset, guint size,
    GstCaps * caps, GstBuffer ** buf)
{
  GstDfbVideoSink *dfbvideosink;
  GstDfbSurface *surface = NULL;
  GstFlowReturn ret = GST_FLOW_OK;

  gboolean rev_nego = FALSE;
  gint width, height;

  GstCaps *desired_caps = NULL;
  GstStructure *structure = NULL;

  dfbvideosink = GST_DFBVIDEOSINK (bsink);

  GST_DEBUG ("a buffer of %d bytes was requested with caps %" GST_PTR_FORMAT
      " and offset %llu", size, caps, offset);

  desired_caps = gst_caps_copy (caps);

  structure = gst_caps_get_structure (desired_caps, 0);

  if (gst_structure_get_int (structure, "width", &width) &&
      gst_structure_get_int (structure, "height", &height)) {
    DFBRectangle dst, src, result;
    GstDfbVMode vmode;

    /* If we can do hardware scaling we don't do reverse negotiation */
    if (dfbvideosink->hw_scaling) {
      goto alloc;
    }

    /* Our desired geometry respects aspect ratio */
    src.w = width;
    src.h = height;
    /* We should adapt the destination to the most suitable video mode */
    if (gst_dfbvideosink_get_best_vmode (dfbvideosink, width, height, &vmode)) {
      dst.w = vmode.width;
      dst.h = vmode.height;
    } else {
      if (dfbvideosink->ext_surface) {
        dfbvideosink->ext_surface->GetSize (dfbvideosink->ext_surface, &dst.w,
            &dst.h);
      } else {
        dfbvideosink->primary->GetSize (dfbvideosink->primary, &dst.w, &dst.h);
      }
      dfbvideosink->out_width = dst.w;
      dfbvideosink->out_height = dst.h;
    }

    gst_dfbvideosink_center_rect (src, dst, &result, TRUE);

    if (width != result.w && height != result.h) {
      GstPad *peer = gst_pad_get_peer (GST_VIDEO_SINK_PAD (dfbvideosink));

      if (!GST_IS_PAD (peer)) {
        /* Is this situation possible ? */
        goto alloc;
      }

      GST_DEBUG ("we would love to receive a %dx%d video", result.w, result.h);
      gst_structure_set (structure, "width", G_TYPE_INT, result.w, NULL);
      gst_structure_set (structure, "height", G_TYPE_INT, result.h, NULL);

      if (gst_pad_accept_caps (peer, desired_caps)) {
        GST_DEBUG ("peed pad accepts our desired caps %" GST_PTR_FORMAT,
            desired_caps);
        rev_nego = TRUE;
        width = result.w;
        height = result.h;
      } else {
        GST_DEBUG ("peer pad does not accept our desired caps %" GST_PTR_FORMAT,
            desired_caps);
        rev_nego = FALSE;
        width = dfbvideosink->video_width;
        height = dfbvideosink->video_height;
      }
      gst_object_unref (peer);
    }
  }

alloc:
  /* Inspect our buffer pool */
  while (dfbvideosink->buffer_pool) {
    surface = (GstDfbSurface *) dfbvideosink->buffer_pool->data;

    if (surface) {
      /* Removing from the pool */
      dfbvideosink->buffer_pool =
          g_slist_delete_link (dfbvideosink->buffer_pool,
          dfbvideosink->buffer_pool);

      /* If the surface is invalid for our need, destroy */
      if ((surface->width != width) ||
          (surface->height != height) ||
          (surface->pixel_format != dfbvideosink->pixel_format)) {
        gst_dfbvideosink_surface_destroy (dfbvideosink, surface);
        surface = NULL;
      } else {
        /* We found a suitable surface */
        break;
      }
    }
  }

  /* We haven't found anything, creating a new one */
  if (!surface) {
    if (rev_nego) {
      surface = gst_dfbvideosink_surface_create (dfbvideosink, desired_caps,
          size);
    } else {
      surface = gst_dfbvideosink_surface_create (dfbvideosink, caps, size);
    }
  }
  /* Now we should have a surface, set appropriate caps on it */
  if (surface) {
    if (rev_nego) {
      gst_buffer_set_caps (GST_BUFFER (surface), desired_caps);
    } else {
      gst_buffer_set_caps (GST_BUFFER (surface), caps);
    }
  }

  gst_caps_unref (desired_caps);

  *buf = GST_BUFFER (surface);

beach:
  return ret;
}

/* Our subclass of GstBuffer */

static void
gst_dfbsurface_finalize (GstDfbSurface * surface)
{
  GstDfbVideoSink *dfbvideosink = NULL;

  g_return_if_fail (surface != NULL);

  dfbvideosink = surface->dfbvideosink;
  if (!dfbvideosink)
    goto no_sink;

  /* If our geometry changed we can't reuse that image. */
  if ((surface->width != dfbvideosink->video_width) ||
      (surface->height != dfbvideosink->video_height) ||
      (surface->pixel_format != dfbvideosink->pixel_format)) {
    GST_DEBUG ("destroy image as its size changed %dx%d vs current %dx%d",
        surface->width, surface->height,
        dfbvideosink->video_width, dfbvideosink->video_height);
    gst_dfbvideosink_surface_destroy (dfbvideosink, surface);
  } else {
    /* In that case we can reuse the image and add it to our image pool. */
    GST_DEBUG ("recycling image in pool");
    /* need to increment the refcount again to recycle */
    gst_buffer_ref (GST_BUFFER (surface));
    dfbvideosink->buffer_pool = g_slist_prepend (dfbvideosink->buffer_pool,
        surface);
  }
  return;

no_sink:
  GST_WARNING ("no sink found");
  return;
}

static void
gst_dfbsurface_init (GstDfbSurface * surface, gpointer g_class)
{
  surface->surface = NULL;
  surface->width = 0;
  surface->height = 0;
  surface->pixel_format = DSPF_UNKNOWN;
  surface->dfbvideosink = NULL;
}

static void
gst_dfbsurface_class_init (gpointer g_class, gpointer class_data)
{
  GstMiniObjectClass *mini_object_class = GST_MINI_OBJECT_CLASS (g_class);

  mini_object_class->finalize = (GstMiniObjectFinalizeFunction)
      gst_dfbsurface_finalize;
}

GType
gst_dfbsurface_get_type (void)
{
  static GType _gst_dfbsurface_type;

  if (G_UNLIKELY (_gst_dfbsurface_type == 0)) {
    static const GTypeInfo dfbsurface_info = {
      sizeof (GstBufferClass),
      NULL,
      NULL,
      gst_dfbsurface_class_init,
      NULL,
      NULL,
      sizeof (GstDfbSurface),
      0,
      (GInstanceInitFunc) gst_dfbsurface_init,
      NULL
    };
    _gst_dfbsurface_type = g_type_register_static (GST_TYPE_BUFFER,
        "GstDfbSurface", &dfbsurface_info, 0);
  }
  return _gst_dfbsurface_type;
}

/* Interfaces stuff */

static gboolean
gst_dfbvideosink_interface_supported (GstImplementsInterface * iface,
    GType type)
{
  g_assert (type == GST_TYPE_NAVIGATION);
  return TRUE;
}

static void
gst_dfbvideosink_interface_init (GstImplementsInterfaceClass * klass)
{
  klass->supported = gst_dfbvideosink_interface_supported;
}

static void
gst_dfbvideosink_navigation_send_event (GstNavigation * navigation,
    GstStructure * structure)
{
  GstDfbVideoSink *dfbvideosink = GST_DFBVIDEOSINK (navigation);
  GstEvent *event;
  DFBRectangle src, dst, result;
  gint width, height;
  double x, y;
  GstPad *pad = NULL;

  src.w = dfbvideosink->video_width;
  src.h = dfbvideosink->video_height;
  dst.w = dfbvideosink->out_width;
  dst.h = dfbvideosink->out_height;
  gst_dfbvideosink_center_rect (src, dst, &result, dfbvideosink->hw_scaling);

  event = gst_event_new_navigation (structure);

  /* Our coordinates can be wrong here if we centered the video */

  /* Converting pointer coordinates to the non scaled geometry */
  if (gst_structure_get_double (structure, "pointer_x", &x)) {
    double old_x = x;

    if (x >= result.x && x <= (result.x + result.w)) {
      x -= result.x;
      x *= dfbvideosink->video_width;
      x /= result.w;
    } else {
      x = 0;
    }
    GST_DEBUG ("translated navigation event x coordinate from %f to %f",
        old_x, x);
    gst_structure_set (structure, "pointer_x", G_TYPE_DOUBLE, x, NULL);
  }
  if (gst_structure_get_double (structure, "pointer_y", &y)) {
    double old_y = y;

    if (y >= result.y && y <= (result.y + result.h)) {
      y -= result.y;
      y *= dfbvideosink->video_height;
      y /= result.h;
    } else {
      y = 0;
    }
    GST_DEBUG ("translated navigation event y coordinate from %fd to %fd",
        old_y, y);
    gst_structure_set (structure, "pointer_y", G_TYPE_DOUBLE, y, NULL);
  }

  pad = gst_pad_get_peer (GST_VIDEO_SINK_PAD (dfbvideosink));

  if (GST_IS_PAD (pad)) {
    gst_pad_send_event (pad, event);

    gst_object_unref (pad);
  }
}

static void
gst_dfbvideosink_navigation_init (GstNavigationInterface * iface)
{
  iface->send_event = gst_dfbvideosink_navigation_send_event;
}

/* Properties */

static void
gst_dfbvideosink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstDfbVideoSink *dfbvideosink;

  g_return_if_fail (GST_IS_DFBVIDEOSINK (object));
  dfbvideosink = GST_DFBVIDEOSINK (object);

  switch (prop_id) {
    case ARG_SURFACE:
      dfbvideosink->ext_surface = g_value_get_pointer (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_dfbvideosink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstDfbVideoSink *dfbvideosink;

  g_return_if_fail (GST_IS_DFBVIDEOSINK (object));
  dfbvideosink = GST_DFBVIDEOSINK (object);

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

static void
gst_dfbvideosink_init (GstDfbVideoSink * dfbvideosink)
{
  dfbvideosink->pixel_format = DSPF_UNKNOWN;
  dfbvideosink->video_height = 0;
  dfbvideosink->video_width = 0;
  dfbvideosink->framerate = 0;

  dfbvideosink->dfb = NULL;
  dfbvideosink->layer = NULL;
  dfbvideosink->ext_surface = NULL;
  dfbvideosink->primary = NULL;
  dfbvideosink->setup = FALSE;
  dfbvideosink->running = FALSE;
}

static void
gst_dfbvideosink_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details (element_class, &gst_dfbvideosink_details);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_dfbvideosink_sink_template_factory));
}

static void
gst_dfbvideosink_class_init (GstDfbVideoSinkClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseSinkClass *gstbasesink_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasesink_class = (GstBaseSinkClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_VIDEO_SINK);

  gobject_class->set_property = gst_dfbvideosink_set_property;
  gobject_class->get_property = gst_dfbvideosink_get_property;

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_SURFACE,
      g_param_spec_pointer ("surface", "Surface",
          "The target surface for video", G_PARAM_WRITABLE));

  gstelement_class->change_state = gst_dfbvideosink_change_state;

  gstbasesink_class->get_caps = gst_dfbvideosink_getcaps;
  gstbasesink_class->set_caps = gst_dfbvideosink_setcaps;
  gstbasesink_class->buffer_alloc = gst_dfbvideosink_buffer_alloc;
  gstbasesink_class->get_times = gst_dfbvideosink_get_times;
  gstbasesink_class->preroll = gst_dfbvideosink_show_frame;
  gstbasesink_class->render = gst_dfbvideosink_show_frame;
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
gst_dfbvideosink_get_type (void)
{
  static GType dfbvideosink_type = 0;

  if (!dfbvideosink_type) {
    static const GTypeInfo dfbvideosink_info = {
      sizeof (GstDfbVideoSinkClass),
      gst_dfbvideosink_base_init,
      NULL,
      (GClassInitFunc) gst_dfbvideosink_class_init,
      NULL,
      NULL,
      sizeof (GstDfbVideoSink),
      0,
      (GInstanceInitFunc) gst_dfbvideosink_init,
    };
    static const GInterfaceInfo iface_info = {
      (GInterfaceInitFunc) gst_dfbvideosink_interface_init,
      NULL,
      NULL,
    };
    static const GInterfaceInfo navigation_info = {
      (GInterfaceInitFunc) gst_dfbvideosink_navigation_init,
      NULL,
      NULL,
    };

    dfbvideosink_type = g_type_register_static (GST_TYPE_VIDEO_SINK,
        "GstDfbVideoSink", &dfbvideosink_info, 0);

    g_type_add_interface_static (dfbvideosink_type,
        GST_TYPE_IMPLEMENTS_INTERFACE, &iface_info);
    g_type_add_interface_static (dfbvideosink_type, GST_TYPE_NAVIGATION,
        &navigation_info);
  }

  return dfbvideosink_type;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  if (!gst_element_register (plugin, "dfbvideosink", GST_RANK_PRIMARY,
          GST_TYPE_DFBVIDEOSINK))
    return FALSE;

  GST_DEBUG_CATEGORY_INIT (dfbvideosink_debug, "dfbvideosink", 0,
      "DirectFB video sink element");

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "dfbvideosink",
    "DirectFB video output plugin",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE, GST_ORIGIN)
