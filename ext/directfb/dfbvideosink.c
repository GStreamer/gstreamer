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

/**
 * SECTION:element-dfbvideosink
 *
 * DfbVideoSink renders video frames using the
 * <ulink url="http://www.directfb.org/">DirectFB</ulink> library.
 * Rendering can happen in two different modes :
 * <itemizedlist>
 * <listitem>
 *   <para>
 *   Standalone: this mode will take complete control of the monitor forcing
 *   <ulink url="http://www.directfb.org/">DirectFB</ulink> to fullscreen layout.
 *   This is convenient to test using the  gst-launch command line tool or
 *   other simple applications. It is possible to interrupt playback while
 *   being in this mode by pressing the Escape key.
 *   </para>
 *   <para>
 *   This mode handles navigation events for every input device supported by
 *   the <ulink url="http://www.directfb.org/">DirectFB</ulink> library, it will
 *   look for available video modes in the fb.modes file and try to switch
 *   the framebuffer video mode to the most suitable one. Depending on 
 *   hardware acceleration capabilities the element will handle scaling or not.
 *   If no acceleration is available it will do clipping or centering of the
 *   video frames respecting the original aspect ratio.
 *   </para>
 * </listitem>
 * <listitem>
 *   <para>
 *   Embedded: this mode will render video frames in a 
 *   #GstDfbVideoSink:surface provided by the
 *   application developer. This is a more advanced usage of the element and
 *   it is required to integrate video playback in existing 
 *   <ulink url="http://www.directfb.org/">DirectFB</ulink> applications.
 *   </para>
 *   <para>
 *   When using this mode the element just renders to the
 *   #GstDfbVideoSink:surface provided by the 
 *   application, that means it won't handle navigation events and won't resize
 *   the #GstDfbVideoSink:surface to fit video
 *   frames geometry. Application has to implement the necessary code to grab
 *   informations about the negotiated geometry and resize there
 *   #GstDfbVideoSink:surface accordingly.
 *   </para>
 * </listitem>
 * </itemizedlist>
 * For both modes the element implements a buffer pool allocation system to 
 * optimize memory allocation time and handle reverse negotiation. Indeed if 
 * you insert an element like videoscale in the pipeline the video sink will
 * negotiate with it to try get a scaled video for either the fullscreen layout
 * or the application provided external #GstDfbVideoSink:surface.
 *
 * <refsect2>
 * <title>Example application</title>
 * <para>
 * <include xmlns="http://www.w3.org/2003/XInclude" href="element-dfb-example.xml" />
 * </para>
 * </refsect2>
 * <refsect2>
 * <title>Example pipelines</title>
 * |[
 * gst-launch -v videotestsrc ! dfbvideosink hue=20000 saturation=40000 brightness=25000
 * ]| test the colorbalance interface implementation in dfbvideosink
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

/* Our interfaces */
#include <gst/interfaces/navigation.h>
#include <gst/interfaces/colorbalance.h>

/* Object header */
#include "dfbvideosink.h"

#include <string.h>
#include <stdlib.h>

/* Debugging category */
GST_DEBUG_CATEGORY_STATIC (dfbvideosink_debug);
#define GST_CAT_DEFAULT dfbvideosink_debug

/* Default template */
static GstStaticPadTemplate gst_dfbvideosink_sink_template_factory =
    GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw-rgb, "
        "framerate = (fraction) [ 0, MAX ], "
        "width = (int) [ 1, MAX ], "
        "height = (int) [ 1, MAX ]; "
        "video/x-raw-yuv, "
        "framerate = (fraction) [ 0, MAX ], "
        "width = (int) [ 1, MAX ], " "height = (int) [ 1, MAX ]")
    );

/* Signals and args */
enum
{
  ARG_0,
  ARG_SURFACE,
  ARG_CONTRAST,
  ARG_BRIGHTNESS,
  ARG_HUE,
  ARG_SATURATION,
  ARG_PIXEL_ASPECT_RATIO,
  ARG_VSYNC
};

static void gst_dfbvideosink_bufferpool_clear (GstDfbVideoSink * dfbvideosink);
static DFBSurfacePixelFormat gst_dfbvideosink_get_format_from_caps (GstCaps *
    caps);
static void gst_dfbvideosink_update_colorbalance (GstDfbVideoSink *
    dfbvideosink);
static void gst_dfbvideosink_surface_destroy (GstDfbVideoSink * dfbvideosink,
    GstDfbSurface * surface);

static GstVideoSinkClass *parent_class = NULL;
static GstBufferClass *surface_parent_class = NULL;

#ifndef GST_DISABLE_GST_DEBUG
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
#endif /* GST_DISABLE_GST_DEBUG */

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
  gboolean succeeded = FALSE;

  g_return_val_if_fail (GST_IS_DFBVIDEOSINK (dfbvideosink), NULL);

  surface = (GstDfbSurface *) gst_mini_object_new (GST_TYPE_DFBSURFACE);

  /* Keep a ref to our sink */
  surface->dfbvideosink = gst_object_ref (dfbvideosink);
  /* Surface is not locked yet */
  surface->locked = FALSE;

  structure = gst_caps_get_structure (caps, 0);

  if (!gst_structure_get_int (structure, "width", &surface->width) ||
      !gst_structure_get_int (structure, "height", &surface->height)) {
    GST_WARNING_OBJECT (dfbvideosink, "failed getting geometry from caps %"
        GST_PTR_FORMAT, caps);
    goto fallback;
  }

  /* Pixel format from caps */
  surface->pixel_format = gst_dfbvideosink_get_format_from_caps (caps);
  if (surface->pixel_format == DSPF_UNKNOWN) {
    goto fallback;
  }

  if (!dfbvideosink->dfb) {
    GST_DEBUG_OBJECT (dfbvideosink, "no DirectFB context to create a surface");
    goto fallback;
  }

  /* Creating an internal surface which will be used as GstBuffer, we used
     the detected pixel format and video dimensions */

  s_dsc.flags =
      DSDESC_PIXELFORMAT | DSDESC_WIDTH | DSDESC_HEIGHT /*| DSDESC_CAPS */ ;

  s_dsc.pixelformat = surface->pixel_format;
  s_dsc.width = surface->width;
  s_dsc.height = surface->height;
  /*s_dsc.caps = DSCAPS_VIDEOONLY; */

  ret = dfbvideosink->dfb->CreateSurface (dfbvideosink->dfb, &s_dsc,
      &surface->surface);
  if (ret != DFB_OK) {
    GST_WARNING_OBJECT (dfbvideosink, "failed creating a DirectFB surface");
    surface->surface = NULL;
    goto fallback;
  }

  /* Clearing surface */
  surface->surface->Clear (surface->surface, 0x00, 0x00, 0x00, 0xFF);

  /* Locking the surface to acquire the memory pointer */
  surface->surface->Lock (surface->surface, DSLF_WRITE, &data, &pitch);
  surface->locked = TRUE;
  GST_BUFFER_DATA (surface) = data;
  GST_BUFFER_SIZE (surface) = pitch * surface->height;

  /* Be carefull here. If size is different from the surface size
     (pitch * height), we can't use that surface through buffer alloc system
     or we are going to run into serious stride issues */
  if (GST_BUFFER_SIZE (surface) != size) {
    GST_WARNING_OBJECT (dfbvideosink, "DirectFB surface size (%dx%d=%d) "
        "differs from GStreamer requested size %u", pitch, surface->height,
        GST_BUFFER_SIZE (surface), (guint) size);
    goto fallback;
  }

  GST_DEBUG_OBJECT (dfbvideosink, "creating a %dx%d surface (%p) with %s "
      "pixel format, line pitch %d", surface->width, surface->height, surface,
      gst_dfbvideosink_get_format_name (surface->pixel_format), pitch);

  succeeded = TRUE;

  goto beach;

fallback:

  /* We allocate a standard buffer ourselves to store it in our buffer pool,
     this is an optimisation for memory allocation */
  GST_BUFFER (surface)->malloc_data = g_malloc (size);
  GST_BUFFER_DATA (surface) = GST_BUFFER (surface)->malloc_data;
  GST_BUFFER_SIZE (surface) = size;
  if (surface->surface) {
    if (surface->locked) {
      surface->surface->Unlock (surface->surface);
      surface->locked = FALSE;
    }
    surface->surface->Release (surface->surface);
    surface->surface = NULL;
  }
  GST_DEBUG_OBJECT (dfbvideosink, "allocating a buffer (%p) of %u bytes",
      surface, (guint) size);

  succeeded = TRUE;

beach:
  if (!succeeded) {
    gst_dfbvideosink_surface_destroy (dfbvideosink, surface);
    surface = NULL;
  }
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

  if (surface->dfbvideosink) {
    /* Release the ref to our sink */
    surface->dfbvideosink = NULL;
    gst_object_unref (dfbvideosink);
  }

  GST_MINI_OBJECT_CLASS (surface_parent_class)->finalize (GST_MINI_OBJECT
      (surface));
}

static gpointer
gst_dfbvideosink_event_thread (GstDfbVideoSink * dfbvideosink)
{
  DFBResult ret;

  while (dfbvideosink->running) {
    /* Wait for an event with a 50 ms timeout */
    dfbvideosink->event_buffer->
        WaitForEventWithTimeout (dfbvideosink->event_buffer, 0, 50);

    /* Do we have an event ? */
    ret = dfbvideosink->event_buffer->HasEvent (dfbvideosink->event_buffer);

    if (ret == DFB_OK) {
      DFBEvent event;

      GST_DEBUG_OBJECT (dfbvideosink, "we have an event");

      ret = dfbvideosink->event_buffer->GetEvent (dfbvideosink->event_buffer,
          &event);
      if (ret != DFB_OK) {      /* Error */
        GST_WARNING_OBJECT (dfbvideosink, "failed when getting event from "
            "event buffer");
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
              GST_DEBUG_OBJECT (dfbvideosink, "key press event %c !",
                  event.input.key_symbol);
              gst_navigation_send_key_event (GST_NAVIGATION (dfbvideosink),
                  "key-press", "prout");
          }
        } else if (event.input.type == DIET_BUTTONPRESS) {
          gint x, y;

          dfbvideosink->layer->GetCursorPosition (dfbvideosink->layer, &x, &y);

          GST_DEBUG_OBJECT (dfbvideosink, "button %d pressed at %dx%d",
              event.input.button, x, y);

          gst_navigation_send_mouse_event (GST_NAVIGATION (dfbvideosink),
              "mouse-button-press", event.input.button, x, y);
        } else if (event.input.type == DIET_BUTTONRELEASE) {
          gint x, y;

          dfbvideosink->layer->GetCursorPosition (dfbvideosink->layer, &x, &y);

          GST_DEBUG_OBJECT (dfbvideosink, "button %d released at %dx%d",
              event.input.button, x, y);

          gst_navigation_send_mouse_event (GST_NAVIGATION (dfbvideosink),
              "mouse-button-release", event.input.button, x, y);
        } else if (event.input.type == DIET_AXISMOTION) {
          gint x, y;

          dfbvideosink->layer->GetCursorPosition (dfbvideosink->layer, &x, &y);
          gst_navigation_send_mouse_event (GST_NAVIGATION (dfbvideosink),
              "mouse-move", 0, x, y);
        } else {
          GST_WARNING_OBJECT (dfbvideosink, "unhandled event type %d",
              event.input.type);
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

  GST_DEBUG_OBJECT (dfbvideosink, "inspecting display layer %d with name: %s",
      id, desc.name);

  if ((desc.type & DLTF_VIDEO) && (desc.caps & DLCAPS_SURFACE)) {
    GST_DEBUG_OBJECT (dfbvideosink,
        "this layer can handle live video and has a surface");
  } else {
    if (desc.caps & DLCAPS_SURFACE) {
      GST_DEBUG_OBJECT (dfbvideosink,
          "this layer can not handle live video but has a surface");
    } else {
      GST_DEBUG_OBJECT (dfbvideosink, "no we can't use that layer, really...");
      goto beach;
    }
  }

  ret = dfbvideosink->dfb->GetDisplayLayer (dfbvideosink->dfb, id, &layer);
  if (ret != DFB_OK) {
    GST_WARNING_OBJECT (dfbvideosink, "failed getting display layer %s",
        desc.name);
    goto beach;
  }

  ret = layer->GetConfiguration (layer, &dlc);
  if (ret != DFB_OK) {
    GST_WARNING_OBJECT (dfbvideosink,
        "failed getting display layer configuration");
    goto beach;
  }

  if ((dlc.flags & DLCONF_BUFFERMODE) && (dlc.buffermode & DLBM_FRONTONLY)) {
    GST_DEBUG_OBJECT (dfbvideosink, "no backbuffer");
  }
  if ((dlc.flags & DLCONF_BUFFERMODE) && (dlc.buffermode & DLBM_BACKVIDEO)) {
    GST_DEBUG_OBJECT (dfbvideosink, "backbuffer is in video memory");
    backbuffer = TRUE;
  }
  if ((dlc.flags & DLCONF_BUFFERMODE) && (dlc.buffermode & DLBM_BACKSYSTEM)) {
    GST_DEBUG_OBJECT (dfbvideosink, "backbuffer is in system memory");
    backbuffer = TRUE;
  }
  if ((dlc.flags & DLCONF_BUFFERMODE) && (dlc.buffermode & DLBM_TRIPLE)) {
    GST_DEBUG_OBJECT (dfbvideosink, "triple buffering");
    backbuffer = TRUE;
  }

  /* If the primary is suitable we prefer using it */
  if (dfbvideosink->layer_id != DLID_PRIMARY) {
    GST_DEBUG_OBJECT (dfbvideosink, "selecting layer named %s", desc.name);
    dfbvideosink->layer_id = id;
    dfbvideosink->backbuffer = backbuffer;
  } else {
    GST_DEBUG_OBJECT (dfbvideosink, "layer %s is suitable but the primary "
        "is currently selected and we prefer that one", desc.name);
  }

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

  GST_DEBUG_OBJECT (dfbvideosink, "adding video mode %dx%d at %d bpp", width,
      height, bpp);
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

  GST_DEBUG_OBJECT (dfbvideosink, "detected input device %s from vendor %s",
      desc.name, desc.vendor);

  /* Get that input device */
  ret = dfbvideosink->dfb->GetInputDevice (dfbvideosink->dfb, id, &device);
  if (ret != DFB_OK) {
    GST_WARNING_OBJECT (dfbvideosink, "failed when getting input device id %d",
        id);
    goto beach;
  }

  ret = device->AttachEventBuffer (device, dfbvideosink->event_buffer);
  if (ret != DFB_OK) {
    GST_WARNING_OBJECT (dfbvideosink, "failed when attaching input device "
        "%d to our event buffer", id);
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
  dfbvideosink->fps_d = 0;
  dfbvideosink->fps_n = 0;
  dfbvideosink->hw_scaling = FALSE;
  dfbvideosink->backbuffer = FALSE;
  dfbvideosink->pixel_format = DSPF_UNKNOWN;

  /* If we do it all by ourself we create the DirectFB context, get the 
     primary layer and use a fullscreen configuration */
  if (!dfbvideosink->ext_surface) {
    GST_DEBUG_OBJECT (dfbvideosink, "no external surface, taking over "
        "DirectFB fullscreen");
    if (!dfbvideosink->dfb) {
      DFBGraphicsDeviceDescription hw_caps;
      char *argv[] = { (char *) "-", (char *) "--dfb:quiet", NULL };
      int argc = 2;
      char **args;

      GST_DEBUG_OBJECT (dfbvideosink, "initializing DirectFB");

      args = argv;
      ret = DirectFBInit (&argc, &args);

      if (ret != DFB_OK) {
        GST_WARNING_OBJECT (dfbvideosink, "DirectFB initialization failed");
        goto beach;
      }

      ret = DirectFBCreate (&(dfbvideosink->dfb));

      if (ret != DFB_OK) {
        GST_WARNING_OBJECT (dfbvideosink, "failed creating the DirectFB "
            "main object");
        goto beach;
      }

      /* Get Hardware capabilities */
      ret = dfbvideosink->dfb->GetDeviceDescription (dfbvideosink->dfb,
          &hw_caps);

      if (ret != DFB_OK) {
        GST_WARNING_OBJECT (dfbvideosink, "failed grabbing the hardware "
            "capabilities");
        goto beach;
      }

      GST_DEBUG_OBJECT (dfbvideosink, "video card %s from vendor %s detected "
          "with %d bytes of video memory", hw_caps.name, hw_caps.vendor,
          hw_caps.video_memory);

      if (hw_caps.acceleration_mask & DFXL_BLIT) {
        GST_DEBUG_OBJECT (dfbvideosink, "Blit is accelerated");
      }
      if (hw_caps.acceleration_mask & DFXL_STRETCHBLIT) {
        GST_DEBUG_OBJECT (dfbvideosink, "StretchBlit is accelerated");
        dfbvideosink->hw_scaling = TRUE;
      } else {
        GST_DEBUG_OBJECT (dfbvideosink, "StretchBlit is not accelerated");
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
      GList *channels_list = NULL;
      DFBDisplayLayerDescription dl_desc;

      /* Get the best Display Layer */
      ret = dfbvideosink->dfb->GetDisplayLayer (dfbvideosink->dfb,
          dfbvideosink->layer_id, &dfbvideosink->layer);
      if (ret != DFB_OK) {
        GST_WARNING_OBJECT (dfbvideosink, "failed getting display layer");
        goto beach;
      }

      ret = dfbvideosink->layer->SetCooperativeLevel (dfbvideosink->layer,
          DLSCL_EXCLUSIVE);

      if (ret != DFB_OK) {
        GST_WARNING_OBJECT (dfbvideosink, "failed setting display layer to "
            "fullscreen mode");
        goto beach;
      }

      dfbvideosink->layer->GetDescription (dfbvideosink->layer, &dl_desc);

      /* Check that this layer is able to do colorbalance settings */
      if (dl_desc.caps & DLCAPS_BRIGHTNESS) {
        channels_list = g_list_append (channels_list, (char *) "BRIGHTNESS");
      }
      if (dl_desc.caps & DLCAPS_CONTRAST) {
        channels_list = g_list_append (channels_list, (char *) "CONTRAST");
      }
      if (dl_desc.caps & DLCAPS_HUE) {
        channels_list = g_list_append (channels_list, (char *) "HUE");
      }
      if (dl_desc.caps & DLCAPS_SATURATION) {
        channels_list = g_list_append (channels_list, (char *) "SATURATION");
      }

      if (channels_list) {
        GList *walk = channels_list;

        /* Generate Color balance channel list */
        while (walk) {
          GstColorBalanceChannel *channel = NULL;

          GST_DEBUG_OBJECT (dfbvideosink, "adding %s as a colorbalance channel",
              (const char *) walk->data);

          channel = g_object_new (GST_TYPE_COLOR_BALANCE_CHANNEL, NULL);
          channel->label = g_strdup (walk->data);
          channel->min_value = 0x0000;
          channel->max_value = 0xFFFF;

          dfbvideosink->cb_channels = g_list_append (dfbvideosink->cb_channels,
              channel);

          walk = g_list_next (walk);
        }

        /* If the colorbalance settings have not been touched we get current
           values as defaults and update our internal variables */
        if (!dfbvideosink->cb_changed) {
          DFBColorAdjustment cb_adjust;

          ret = dfbvideosink->layer->GetColorAdjustment (dfbvideosink->layer,
              &cb_adjust);

          if (ret != DFB_OK) {
            GST_WARNING_OBJECT (dfbvideosink, "failed when getting color "
                "adjustment from layer");
          }

          if (cb_adjust.flags & DCAF_BRIGHTNESS) {
            dfbvideosink->brightness = cb_adjust.brightness;
          } else {
            dfbvideosink->brightness = 0x8000;
          }
          if (cb_adjust.flags & DCAF_CONTRAST) {
            dfbvideosink->contrast = cb_adjust.contrast;
          } else {
            dfbvideosink->contrast = 0x8000;
          }
          if (cb_adjust.flags & DCAF_HUE) {
            dfbvideosink->hue = cb_adjust.hue;
          } else {
            dfbvideosink->hue = 0x8000;
          }
          if (cb_adjust.flags & DCAF_SATURATION) {
            dfbvideosink->saturation = cb_adjust.saturation;
          } else {
            dfbvideosink->saturation = 0x8000;
          }
          GST_DEBUG_OBJECT (dfbvideosink, "brightness %d, contrast %d, "
              "hue %d, saturation %d", dfbvideosink->brightness,
              dfbvideosink->contrast, dfbvideosink->hue,
              dfbvideosink->saturation);
        }

        g_list_free (channels_list);

        gst_dfbvideosink_update_colorbalance (dfbvideosink);
      }

      dfbvideosink->layer->SetBackgroundColor (dfbvideosink->layer,
          0x00, 0x00, 0x00, 0xFF);

      dfbvideosink->layer->EnableCursor (dfbvideosink->layer, TRUE);

      GST_DEBUG_OBJECT (dfbvideosink, "getting primary surface");
      dfbvideosink->layer->GetSurface (dfbvideosink->layer,
          &dfbvideosink->primary);

      dfbvideosink->primary->SetBlittingFlags (dfbvideosink->primary,
          DSBLIT_NOFX);
    }

    dfbvideosink->primary->GetPixelFormat (dfbvideosink->primary,
        &dfbvideosink->pixel_format);
  } else {
    DFBSurfaceCapabilities s_caps;

    GST_DEBUG_OBJECT (dfbvideosink, "getting pixel format from foreign "
        "surface %p", dfbvideosink->ext_surface);
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
    GST_DEBUG_OBJECT (dfbvideosink, "external surface is %dx%d and uses %s "
        "pixel format", dfbvideosink->out_width, dfbvideosink->out_height,
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

  GST_DEBUG_OBJECT (dfbvideosink, "cleaning up DirectFB environment");

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

  if (dfbvideosink->cb_channels) {
    GList *walk = dfbvideosink->cb_channels;

    while (walk) {
      GstColorBalanceChannel *channel = walk->data;

      g_object_unref (channel);
      walk = g_list_next (walk);
    }
    g_list_free (dfbvideosink->cb_channels);
    dfbvideosink->cb_channels = NULL;
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
        GST_WARNING ("unhandled YUV format %" GST_FOURCC_FORMAT,
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
    DFBSurfacePixelFormat format, gboolean accelerated)
{
  gboolean res = FALSE;
  DFBResult ret;
  IDirectFBSurface *surface = NULL;
  DFBSurfaceDescription s_dsc;
  DFBAccelerationMask mask;
  DFBDisplayLayerConfig dlc, prev_dlc;

  g_return_val_if_fail (GST_IS_DFBVIDEOSINK (dfbvideosink), FALSE);

  /* Create a surface of desired format */
  s_dsc.flags = DSDESC_PIXELFORMAT | DSDESC_WIDTH | DSDESC_HEIGHT;
  s_dsc.pixelformat = format;
  s_dsc.width = 10;
  s_dsc.height = 10;

  ret = dfbvideosink->dfb->CreateSurface (dfbvideosink->dfb, &s_dsc, &surface);
  if (ret != DFB_OK) {
    GST_WARNING_OBJECT (dfbvideosink, "failed creating surface with format %s",
        gst_dfbvideosink_get_format_name (format));
    goto beach;
  }

  /* Backup layer configuration */
  ret = dfbvideosink->layer->GetConfiguration (dfbvideosink->layer, &prev_dlc);
  if (ret != DFB_OK) {
    GST_WARNING_OBJECT (dfbvideosink, "failed when getting current layer "
        "configuration");
    goto beach;
  }

  /* Test configuration of the layer to this pixel format */
  dlc.flags = DLCONF_PIXELFORMAT;
  dlc.pixelformat = format;

  ret = dfbvideosink->layer->TestConfiguration (dfbvideosink->layer, &dlc,
      NULL);
  if (ret != DFB_OK) {
    GST_DEBUG_OBJECT (dfbvideosink, "our layer refuses to operate in pixel "
        "format %s", gst_dfbvideosink_get_format_name (format));
    goto beach;
  }

  ret = dfbvideosink->layer->SetConfiguration (dfbvideosink->layer, &dlc);
  if (ret != DFB_OK) {
    GST_WARNING_OBJECT (dfbvideosink, "our layer refuses to operate in pixel "
        "format, though this format was successfully tested earlied %s",
        gst_dfbvideosink_get_format_name (format));
    goto beach;
  }

  ret = dfbvideosink->primary->GetAccelerationMask (dfbvideosink->primary,
      surface, &mask);
  if (ret != DFB_OK) {
    GST_WARNING_OBJECT (dfbvideosink, "failed getting acceleration mask");
    goto beach;
  }

  /* Blitting from this format to our primary is accelerated */
  if ((mask & DFXL_BLIT) && accelerated) {
    GST_DEBUG_OBJECT (dfbvideosink, "blitting from format %s to our primary "
        "is accelerated", gst_dfbvideosink_get_format_name (format));
    res = TRUE;
  } else if (!accelerated) {
    GST_DEBUG_OBJECT (dfbvideosink, "blitting from format %s to our primary "
        "is not accelerated", gst_dfbvideosink_get_format_name (format));
    res = TRUE;
  }

  /* Restore original layer configuration */
  ret = dfbvideosink->layer->SetConfiguration (dfbvideosink->layer, &prev_dlc);
  if (ret != DFB_OK) {
    GST_WARNING_OBJECT (dfbvideosink, "failed when restoring layer "
        "configuration");
    goto beach;
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

  GST_DEBUG_OBJECT (dfbvideosink, "found video mode %dx%d for input at %dx%d",
      width, height, v_width, v_height);

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
    GST_DEBUG_OBJECT (dfbvideosink, "getcaps called and we are not setup yet, "
        "returning template %" GST_PTR_FORMAT, caps);
    goto beach;
  } else {
    GST_DEBUG_OBJECT (dfbvideosink, "getcaps called, checking our internal "
        "format");
    if (dfbvideosink->ext_surface) {
      /* We are not rendering to our own surface, returning this surface's
       *  pixel format */
      caps = gst_dfbvideosink_get_caps_from_format (dfbvideosink->pixel_format);
    } else {
      /* Try some formats */
      gboolean accelerated = TRUE;
      caps = gst_caps_new_empty ();

      do {
        if (gst_dfbvideosink_can_blit_from_format (dfbvideosink, DSPF_RGB16,
                accelerated)) {
          gst_caps_append (caps,
              gst_dfbvideosink_get_caps_from_format (DSPF_RGB16));
        }
        if (gst_dfbvideosink_can_blit_from_format (dfbvideosink, DSPF_RGB24,
                accelerated)) {
          gst_caps_append (caps,
              gst_dfbvideosink_get_caps_from_format (DSPF_RGB24));
        }
        if (gst_dfbvideosink_can_blit_from_format (dfbvideosink, DSPF_RGB32,
                accelerated)) {
          gst_caps_append (caps,
              gst_dfbvideosink_get_caps_from_format (DSPF_RGB32));
        }
        if (gst_dfbvideosink_can_blit_from_format (dfbvideosink, DSPF_ARGB,
                accelerated)) {
          gst_caps_append (caps,
              gst_dfbvideosink_get_caps_from_format (DSPF_ARGB));
        }
        if (gst_dfbvideosink_can_blit_from_format (dfbvideosink, DSPF_YUY2,
                accelerated)) {
          gst_caps_append (caps,
              gst_dfbvideosink_get_caps_from_format (DSPF_YUY2));
        }
        if (gst_dfbvideosink_can_blit_from_format (dfbvideosink, DSPF_UYVY,
                accelerated)) {
          gst_caps_append (caps,
              gst_dfbvideosink_get_caps_from_format (DSPF_UYVY));
        }
        if (gst_dfbvideosink_can_blit_from_format (dfbvideosink, DSPF_I420,
                accelerated)) {
          gst_caps_append (caps,
              gst_dfbvideosink_get_caps_from_format (DSPF_I420));
        }
        if (gst_dfbvideosink_can_blit_from_format (dfbvideosink, DSPF_YV12,
                accelerated)) {
          gst_caps_append (caps,
              gst_dfbvideosink_get_caps_from_format (DSPF_YV12));
        }
        accelerated = !accelerated;
      } while (accelerated == FALSE);
    }
  }

  for (i = 0; i < gst_caps_get_size (caps); i++) {
    GstStructure *structure = gst_caps_get_structure (caps, i);

    gst_structure_set (structure,
        "width", GST_TYPE_INT_RANGE, 1, G_MAXINT,
        "height", GST_TYPE_INT_RANGE, 1, G_MAXINT,
        "framerate", GST_TYPE_FRACTION_RANGE, 0, 1, G_MAXINT, 1, NULL);

    if (!dfbvideosink->hw_scaling && dfbvideosink->par) {
      int nom, den;

      nom = gst_value_get_fraction_numerator (dfbvideosink->par);
      den = gst_value_get_fraction_denominator (dfbvideosink->par);
      gst_structure_set (structure, "pixel-aspect-ratio",
          GST_TYPE_FRACTION, nom, den, NULL);
    }
  }

  GST_DEBUG_OBJECT (dfbvideosink, "returning our caps %" GST_PTR_FORMAT, caps);

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
  const GValue *framerate;
  DFBSurfacePixelFormat pixel_format = DSPF_UNKNOWN;

  dfbvideosink = GST_DFBVIDEOSINK (bsink);

  structure = gst_caps_get_structure (caps, 0);
  res = gst_structure_get_int (structure, "width", &video_width);
  res &= gst_structure_get_int (structure, "height", &video_height);
  framerate = gst_structure_get_value (structure, "framerate");
  res &= (framerate != NULL);
  if (!res) {
    goto beach;
  }

  dfbvideosink->fps_n = gst_value_get_fraction_numerator (framerate);
  dfbvideosink->fps_d = gst_value_get_fraction_denominator (framerate);

  pixel_format = gst_dfbvideosink_get_format_from_caps (caps);

  GST_DEBUG_OBJECT (dfbvideosink, "setcaps called with %" GST_PTR_FORMAT, caps);
  GST_DEBUG_OBJECT (dfbvideosink, "our format is: %dx%d %s video at %d/%d fps",
      video_width, video_height,
      gst_dfbvideosink_get_format_name (pixel_format), dfbvideosink->fps_n,
      dfbvideosink->fps_d);

  if (dfbvideosink->hw_scaling && dfbvideosink->par) {
    gint video_par_n, video_par_d;      /* video's PAR */
    gint display_par_n, display_par_d;  /* display's PAR */
    gint num, den;
    GValue display_ratio = { 0, };      /* display w/h ratio */
    const GValue *caps_par;

    /* get aspect ratio from caps if it's present, and
     * convert video width and height to a display width and height
     * using wd / hd = wv / hv * PARv / PARd
     * the ratio wd / hd will be stored in display_ratio */
    g_value_init (&display_ratio, GST_TYPE_FRACTION);

    /* get video's PAR */
    caps_par = gst_structure_get_value (structure, "pixel-aspect-ratio");
    if (caps_par) {
      video_par_n = gst_value_get_fraction_numerator (caps_par);
      video_par_d = gst_value_get_fraction_denominator (caps_par);
    } else {
      video_par_n = 1;
      video_par_d = 1;
    }
    /* get display's PAR */
    if (dfbvideosink->par) {
      display_par_n = gst_value_get_fraction_numerator (dfbvideosink->par);
      display_par_d = gst_value_get_fraction_denominator (dfbvideosink->par);
    } else {
      display_par_n = 1;
      display_par_d = 1;
    }

    gst_value_set_fraction (&display_ratio,
        video_width * video_par_n * display_par_d,
        video_height * video_par_d * display_par_n);

    num = gst_value_get_fraction_numerator (&display_ratio);
    den = gst_value_get_fraction_denominator (&display_ratio);
    GST_DEBUG_OBJECT (dfbvideosink,
        "video width/height: %dx%d, calculated display ratio: %d/%d",
        video_width, video_height, num, den);

    /* now find a width x height that respects this display ratio.
     * prefer those that have one of w/h the same as the incoming video
     * using wd / hd = num / den */

    /* start with same height, because of interlaced video */
    /* check hd / den is an integer scale factor, and scale wd with the PAR */
    if (video_height % den == 0) {
      GST_DEBUG_OBJECT (dfbvideosink, "keeping video height");
      GST_VIDEO_SINK_WIDTH (dfbvideosink) = video_height * num / den;
      GST_VIDEO_SINK_HEIGHT (dfbvideosink) = video_height;
    } else if (video_width % num == 0) {
      GST_DEBUG_OBJECT (dfbvideosink, "keeping video width");
      GST_VIDEO_SINK_WIDTH (dfbvideosink) = video_width;
      GST_VIDEO_SINK_HEIGHT (dfbvideosink) = video_width * den / num;
    } else {
      GST_DEBUG_OBJECT (dfbvideosink, "approximating while keeping height");
      GST_VIDEO_SINK_WIDTH (dfbvideosink) = video_height * num / den;
      GST_VIDEO_SINK_HEIGHT (dfbvideosink) = video_height;
    }
    GST_DEBUG_OBJECT (dfbvideosink, "scaling to %dx%d",
        GST_VIDEO_SINK_WIDTH (dfbvideosink),
        GST_VIDEO_SINK_HEIGHT (dfbvideosink));
  } else {
    if (dfbvideosink->par) {
      const GValue *par;

      par = gst_structure_get_value (structure, "pixel-aspect-ratio");
      if (par) {
        if (gst_value_compare (par, dfbvideosink->par) != GST_VALUE_EQUAL) {
          goto wrong_aspect;
        }
      }
    }
    GST_VIDEO_SINK_WIDTH (dfbvideosink) = video_width;
    GST_VIDEO_SINK_HEIGHT (dfbvideosink) = video_height;
  }

  /* Try to adapt the video mode to the video geometry */
  if (dfbvideosink->dfb) {
    DFBResult ret;
    GstDfbVMode vmode;

    GST_DEBUG_OBJECT (dfbvideosink, "trying to adapt the video mode to video "
        "geometry");

    /* Set video mode and layer configuration appropriately */
    if (gst_dfbvideosink_get_best_vmode (dfbvideosink,
            GST_VIDEO_SINK_WIDTH (dfbvideosink),
            GST_VIDEO_SINK_HEIGHT (dfbvideosink), &vmode)) {
      DFBDisplayLayerConfig lc;
      gint width, height, bpp;

      width = vmode.width;
      height = vmode.height;
      bpp = vmode.bpp;

      GST_DEBUG_OBJECT (dfbvideosink, "setting video mode to %dx%d at %d bpp",
          width, height, bpp);

      ret = dfbvideosink->dfb->SetVideoMode (dfbvideosink->dfb, width,
          height, bpp);
      if (ret != DFB_OK) {
        GST_WARNING_OBJECT (dfbvideosink, "failed setting video mode %dx%d "
            "at %d bpp", width, height, bpp);
      }

      lc.flags = DLCONF_PIXELFORMAT;
      lc.pixelformat = pixel_format;

      ret = dfbvideosink->layer->SetConfiguration (dfbvideosink->layer, &lc);
      if (ret != DFB_OK) {
        GST_WARNING_OBJECT (dfbvideosink, "failed setting layer pixelformat "
            "to %s", gst_dfbvideosink_get_format_name (pixel_format));
      } else {
        dfbvideosink->layer->GetConfiguration (dfbvideosink->layer, &lc);
        dfbvideosink->out_width = lc.width;
        dfbvideosink->out_height = lc.height;
        dfbvideosink->pixel_format = lc.pixelformat;
        GST_DEBUG_OBJECT (dfbvideosink, "layer %d now configured to %dx%d %s",
            dfbvideosink->layer_id, lc.width, lc.height,
            gst_dfbvideosink_get_format_name (lc.pixelformat));
      }
    }
  }

  if (pixel_format != dfbvideosink->pixel_format) {
    GST_WARNING_OBJECT (dfbvideosink, "setcaps sent us a different pixel "
        "format %s", gst_dfbvideosink_get_format_name (pixel_format));
    goto beach;
  }

  dfbvideosink->video_width = video_width;
  dfbvideosink->video_height = video_height;

  result = TRUE;

beach:
  return result;

/* ERRORS */
wrong_aspect:
  {
    GST_INFO_OBJECT (dfbvideosink, "pixel aspect ratio does not match");
    return FALSE;
  }
}

static GstStateChangeReturn
gst_dfbvideosink_change_state (GstElement * element, GstStateChange transition)
{
  GstDfbVideoSink *dfbvideosink;
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  dfbvideosink = GST_DFBVIDEOSINK (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      dfbvideosink->running = TRUE;
      if (!dfbvideosink->setup) {
        if (!gst_dfbvideosink_setup (dfbvideosink)) {
          GST_DEBUG_OBJECT (dfbvideosink, "setup failed when changing state "
              "from NULL to READY");
          GST_ELEMENT_ERROR (dfbvideosink, RESOURCE, OPEN_WRITE,
              (NULL), ("Failed initializing DirectFB system"));
          return GST_STATE_CHANGE_FAILURE;
        }
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
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (ret == GST_STATE_CHANGE_FAILURE)
    return ret;

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      dfbvideosink->fps_d = 0;
      dfbvideosink->fps_n = 0;
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
    default:
      break;
  }

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
      if (dfbvideosink->fps_n > 0) {
        *end =
            *start + (GST_SECOND * dfbvideosink->fps_d) / dfbvideosink->fps_n;
      }
    }
  }
}

static GstFlowReturn
gst_dfbvideosink_show_frame (GstBaseSink * bsink, GstBuffer * buf)
{
  GstDfbVideoSink *dfbvideosink = NULL;
  DFBResult res;
  GstVideoRectangle dst, src, result;
  GstFlowReturn ret = GST_FLOW_OK;
  gboolean mem_cpy = TRUE;

  dfbvideosink = GST_DFBVIDEOSINK (bsink);

  if (!dfbvideosink->setup) {
    ret = GST_FLOW_UNEXPECTED;
    goto beach;
  }

  /* Is that a buffer we allocated ourselves ? */
  if (GST_IS_DFBSURFACE (buf)) {
    GstDfbSurface *tmp_surface = GST_DFBSURFACE (buf);

    /* Does it have a surface ? */
    if (tmp_surface->surface) {
      mem_cpy = FALSE;
      GST_DEBUG_OBJECT (dfbvideosink, "we have a buffer (%p) we allocated "
          "ourselves and it has a surface, no memcpy then", buf);
    } else {
      /* No surface, that's a malloc */
      GST_DEBUG_OBJECT (dfbvideosink, "we have a buffer (%p) we allocated "
          "ourselves but it does not hold a surface", buf);
    }
  } else {
    /* Not our baby */
    GST_DEBUG_OBJECT (dfbvideosink, "we have a buffer (%p) we did not allocate",
        buf);
  }

  if (mem_cpy) {
    IDirectFBSurface *dest = NULL, *surface = NULL;
    guint8 *data;
    gint dest_pitch, src_pitch, line;
    GstStructure *structure;

    /* As we are not blitting no acceleration is possible. If the surface is
     * too small we do clipping, if it's too big we center. Theoretically as 
     * we are using buffer_alloc, there's a chance that we have been able to 
     * do reverse caps negotiation */

    if (dfbvideosink->ext_surface) {
      surface = dfbvideosink->ext_surface;
      GST_DEBUG_OBJECT (dfbvideosink, "memcpy to an external surface "
          "subsurface (vsync %d)", dfbvideosink->vsync);
    } else {
      surface = dfbvideosink->primary;
      GST_DEBUG_OBJECT (dfbvideosink, "memcpy to a primary subsurface "
          "(vsync %d)", dfbvideosink->vsync);
    }

    /* Get the video frame geometry from the buffer caps */
    structure = gst_caps_get_structure (GST_BUFFER_CAPS (buf), 0);
    if (structure) {
      gst_structure_get_int (structure, "width", &src.w);
      gst_structure_get_int (structure, "height", &src.h);
    } else {
      src.w = dfbvideosink->video_width;
      src.h = dfbvideosink->video_height;
    }
    res = surface->GetSize (surface, &dst.w, &dst.h);

    /* Center / Clip */
    gst_video_sink_center_rect (src, dst, &result, FALSE);

    res =
        surface->GetSubSurface (surface, (DFBRectangle *) (void *) &result,
        &dest);
    if (res != DFB_OK) {
      GST_WARNING_OBJECT (dfbvideosink, "failed when getting a sub surface");
      ret = GST_FLOW_UNEXPECTED;
      goto beach;
    }

    /* If we are not using Flip we wait for VSYNC before blit */
    if (!dfbvideosink->backbuffer && dfbvideosink->vsync) {
      dfbvideosink->layer->WaitForSync (dfbvideosink->layer);
    }

    res = dest->Lock (dest, DSLF_WRITE, (void *) &data, &dest_pitch);
    if (res != DFB_OK) {
      GST_WARNING_OBJECT (dfbvideosink, "failed locking the external "
          "subsurface for writing");
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
      if (dfbvideosink->vsync) {
        res = surface->Flip (surface, NULL, DSFLIP_ONSYNC);
      } else {
        res = surface->Flip (surface, NULL, DSFLIP_NONE);
      }
    }
  } else {
    /* Else we will [Stretch]Blit to our primary */
    GstDfbSurface *surface = GST_DFBSURFACE (buf);

    GST_DEBUG_OBJECT (dfbvideosink, "blitting to a primary surface (vsync %d)",
        dfbvideosink->vsync);

    src.w = GST_VIDEO_SINK_WIDTH (dfbvideosink);
    src.h = GST_VIDEO_SINK_HEIGHT (dfbvideosink);

    dfbvideosink->primary->GetSize (dfbvideosink->primary, &dst.w, &dst.h);

    /* Unlocking surface before blit */
    if (surface->locked) {
      surface->surface->Unlock (surface->surface);
      surface->locked = FALSE;
    }

    gst_video_sink_center_rect (src, dst, &result, dfbvideosink->hw_scaling);

    /* If we are not using Flip we wait for VSYNC before blit */
    if (!dfbvideosink->backbuffer && dfbvideosink->vsync) {
      dfbvideosink->layer->WaitForSync (dfbvideosink->layer);
    }

    if (dfbvideosink->hw_scaling) {
      dfbvideosink->primary->StretchBlit (dfbvideosink->primary,
          surface->surface, NULL, (DFBRectangle *) (void *) &result);
    } else {
      DFBRectangle clip;

      clip.x = clip.y = 0;
      clip.w = result.w;
      clip.h = result.h;
      dfbvideosink->primary->Blit (dfbvideosink->primary, surface->surface,
          &clip, result.x, result.y);
    }

    if (dfbvideosink->backbuffer) {
      if (dfbvideosink->vsync) {
        dfbvideosink->primary->Flip (dfbvideosink->primary, NULL,
            DSFLIP_ONSYNC);
      } else {
        dfbvideosink->primary->Flip (dfbvideosink->primary, NULL, DSFLIP_NONE);
      }
    }
  }

beach:
  return ret;
}

static void
gst_dfbvideosink_bufferpool_clear (GstDfbVideoSink * dfbvideosink)
{
  g_mutex_lock (dfbvideosink->pool_lock);
  while (dfbvideosink->buffer_pool) {
    GstDfbSurface *surface = dfbvideosink->buffer_pool->data;

    dfbvideosink->buffer_pool = g_slist_delete_link (dfbvideosink->buffer_pool,
        dfbvideosink->buffer_pool);
    gst_dfbvideosink_surface_destroy (dfbvideosink, surface);
  }
  g_mutex_unlock (dfbvideosink->pool_lock);
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

  GST_LOG_OBJECT (dfbvideosink, "a buffer of %u bytes was requested with caps "
      "%" GST_PTR_FORMAT " and offset %" G_GUINT64_FORMAT, size, caps, offset);

  if (G_UNLIKELY (!dfbvideosink->setup)) {
    GST_DEBUG_OBJECT (dfbvideosink, "we are not setup yet, can't allocate!");
    *buf = NULL;
    return ret;
  }

  desired_caps = gst_caps_copy (caps);

  structure = gst_caps_get_structure (desired_caps, 0);

  if (gst_structure_get_int (structure, "width", &width) &&
      gst_structure_get_int (structure, "height", &height)) {
    GstVideoRectangle dst, src, result;
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

    gst_video_sink_center_rect (src, dst, &result, TRUE);

    if (width != result.w || height != result.h) {
      GstPad *peer = gst_pad_get_peer (GST_VIDEO_SINK_PAD (dfbvideosink));

      if (!GST_IS_PAD (peer)) {
        /* Is this situation possible ? */
        goto alloc;
      }

      GST_DEBUG_OBJECT (dfbvideosink, "we would love to receive a %dx%d video",
          result.w, result.h);
      gst_structure_set (structure, "width", G_TYPE_INT, result.w, NULL);
      gst_structure_set (structure, "height", G_TYPE_INT, result.h, NULL);

      /* PAR property overrides the X calculated one */
      if (dfbvideosink->par) {
        gint nom, den;

        nom = gst_value_get_fraction_numerator (dfbvideosink->par);
        den = gst_value_get_fraction_denominator (dfbvideosink->par);
        gst_structure_set (structure, "pixel-aspect-ratio",
            GST_TYPE_FRACTION, nom, den, NULL);
      }

      if (gst_pad_accept_caps (peer, desired_caps)) {
        gint bpp;

        bpp = size / height / width;
        rev_nego = TRUE;
        width = result.w;
        height = result.h;
        size = bpp * width * height;
        GST_DEBUG_OBJECT (dfbvideosink, "peed pad accepts our desired caps %"
            GST_PTR_FORMAT " buffer size is now %d bytes", desired_caps, size);
      } else {
        GST_DEBUG_OBJECT (dfbvideosink, "peer pad does not accept our "
            "desired caps %" GST_PTR_FORMAT, desired_caps);
        rev_nego = FALSE;
        width = dfbvideosink->video_width;
        height = dfbvideosink->video_height;
      }
      gst_object_unref (peer);
    }
  }

alloc:
  /* Inspect our buffer pool */
  g_mutex_lock (dfbvideosink->pool_lock);
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
  g_mutex_unlock (dfbvideosink->pool_lock);

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

  *buf = GST_BUFFER (surface);

  gst_caps_unref (desired_caps);

  return ret;
}

/* Our subclass of GstBuffer */

static void
gst_dfbsurface_finalize (GstDfbSurface * surface)
{
  GstDfbVideoSink *dfbvideosink = NULL;

  g_return_if_fail (surface != NULL);

  dfbvideosink = surface->dfbvideosink;
  if (!dfbvideosink) {
    GST_WARNING_OBJECT (surface, "no sink found");
    goto beach;
  }

  /* If our geometry changed we can't reuse that image. */
  if ((surface->width != dfbvideosink->video_width) ||
      (surface->height != dfbvideosink->video_height) ||
      (surface->pixel_format != dfbvideosink->pixel_format)) {
    GST_DEBUG_OBJECT (dfbvideosink, "destroy surface %p as its size changed "
        "%dx%d vs current %dx%d", surface, surface->width, surface->height,
        dfbvideosink->video_width, dfbvideosink->video_height);
    gst_dfbvideosink_surface_destroy (dfbvideosink, surface);
  } else {
    /* In that case we can reuse the image and add it to our image pool. */
    GST_DEBUG_OBJECT (dfbvideosink, "recycling surface %p in pool", surface);
    /* need to increment the refcount again to recycle */
    gst_buffer_ref (GST_BUFFER (surface));
    g_mutex_lock (dfbvideosink->pool_lock);
    dfbvideosink->buffer_pool = g_slist_prepend (dfbvideosink->buffer_pool,
        surface);
    g_mutex_unlock (dfbvideosink->pool_lock);
  }

beach:
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

  surface_parent_class = g_type_class_peek_parent (g_class);

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
  g_assert (type == GST_TYPE_NAVIGATION || type == GST_TYPE_COLOR_BALANCE);
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
  GstVideoRectangle src, dst, result;
  double x, y, old_x, old_y;
  GstPad *pad = NULL;

  src.w = GST_VIDEO_SINK_WIDTH (dfbvideosink);
  src.h = GST_VIDEO_SINK_HEIGHT (dfbvideosink);
  dst.w = dfbvideosink->out_width;
  dst.h = dfbvideosink->out_height;
  gst_video_sink_center_rect (src, dst, &result, dfbvideosink->hw_scaling);

  event = gst_event_new_navigation (structure);

  /* Our coordinates can be wrong here if we centered the video */

  /* Converting pointer coordinates to the non scaled geometry */
  if (gst_structure_get_double (structure, "pointer_x", &old_x)) {
    x = old_x;

    if (x >= result.x && x <= (result.x + result.w)) {
      x -= result.x;
      x *= dfbvideosink->video_width;
      x /= result.w;
    } else {
      x = 0;
    }
    GST_DEBUG_OBJECT (dfbvideosink, "translated navigation event x "
        "coordinate from %f to %f", old_x, x);
    gst_structure_set (structure, "pointer_x", G_TYPE_DOUBLE, x, NULL);
  }
  if (gst_structure_get_double (structure, "pointer_y", &old_y)) {
    y = old_y;

    if (y >= result.y && y <= (result.y + result.h)) {
      y -= result.y;
      y *= dfbvideosink->video_height;
      y /= result.h;
    } else {
      y = 0;
    }
    GST_DEBUG_OBJECT (dfbvideosink, "translated navigation event y "
        "coordinate from %fd to %fd", old_y, y);
    gst_structure_set (structure, "pointer_y", G_TYPE_DOUBLE, y, NULL);
  }

  pad = gst_pad_get_peer (GST_VIDEO_SINK_PAD (dfbvideosink));

  if (GST_IS_PAD (pad) && GST_IS_EVENT (event)) {
    gst_pad_send_event (pad, event);

    gst_object_unref (pad);
  }
}

static void
gst_dfbvideosink_navigation_init (GstNavigationInterface * iface)
{
  iface->send_event = gst_dfbvideosink_navigation_send_event;
}

static void
gst_dfbvideosink_update_colorbalance (GstDfbVideoSink * dfbvideosink)
{
  g_return_if_fail (GST_IS_DFBVIDEOSINK (dfbvideosink));

  if (dfbvideosink->layer) {
    DFBColorAdjustment cb_adjust;

    cb_adjust.flags = DCAF_NONE;

    if (dfbvideosink->brightness >= 0) {
      cb_adjust.flags |= DCAF_BRIGHTNESS;
    }
    if (dfbvideosink->contrast >= 0) {
      cb_adjust.flags |= DCAF_CONTRAST;
    }
    if (dfbvideosink->hue >= 0) {
      cb_adjust.flags |= DCAF_HUE;
    }
    if (dfbvideosink->saturation >= 0) {
      cb_adjust.flags |= DCAF_SATURATION;
    }

    cb_adjust.brightness = dfbvideosink->brightness;
    cb_adjust.contrast = dfbvideosink->contrast;
    cb_adjust.hue = dfbvideosink->hue;
    cb_adjust.saturation = dfbvideosink->saturation;

    GST_DEBUG_OBJECT (dfbvideosink, "updating colorbalance: flags %d "
        "brightness %d contrast %d hue %d saturation %d", cb_adjust.flags,
        cb_adjust.brightness, cb_adjust.contrast, cb_adjust.hue,
        cb_adjust.saturation);
    dfbvideosink->layer->SetColorAdjustment (dfbvideosink->layer, &cb_adjust);
  }
}

static const GList *
gst_dfbvideosink_colorbalance_list_channels (GstColorBalance * balance)
{
  GstDfbVideoSink *dfbvideosink = GST_DFBVIDEOSINK (balance);

  g_return_val_if_fail (GST_IS_DFBVIDEOSINK (dfbvideosink), NULL);

  return dfbvideosink->cb_channels;
}

static void
gst_dfbvideosink_colorbalance_set_value (GstColorBalance * balance,
    GstColorBalanceChannel * channel, gint value)
{
  GstDfbVideoSink *dfbvideosink = GST_DFBVIDEOSINK (balance);

  g_return_if_fail (GST_IS_DFBVIDEOSINK (dfbvideosink));
  g_return_if_fail (channel->label != NULL);

  dfbvideosink->cb_changed = TRUE;

  if (g_ascii_strcasecmp (channel->label, "HUE") == 0) {
    dfbvideosink->hue = value;
  } else if (g_ascii_strcasecmp (channel->label, "SATURATION") == 0) {
    dfbvideosink->saturation = value;
  } else if (g_ascii_strcasecmp (channel->label, "CONTRAST") == 0) {
    dfbvideosink->contrast = value;
  } else if (g_ascii_strcasecmp (channel->label, "BRIGHTNESS") == 0) {
    dfbvideosink->brightness = value;
  } else {
    GST_WARNING_OBJECT (dfbvideosink, "got an unknown channel %s",
        channel->label);
    return;
  }

  gst_dfbvideosink_update_colorbalance (dfbvideosink);
}

static gint
gst_dfbvideosink_colorbalance_get_value (GstColorBalance * balance,
    GstColorBalanceChannel * channel)
{
  GstDfbVideoSink *dfbvideosink = GST_DFBVIDEOSINK (balance);
  gint value = 0;

  g_return_val_if_fail (GST_IS_DFBVIDEOSINK (dfbvideosink), 0);
  g_return_val_if_fail (channel->label != NULL, 0);

  if (g_ascii_strcasecmp (channel->label, "HUE") == 0) {
    value = dfbvideosink->hue;
  } else if (g_ascii_strcasecmp (channel->label, "SATURATION") == 0) {
    value = dfbvideosink->saturation;
  } else if (g_ascii_strcasecmp (channel->label, "CONTRAST") == 0) {
    value = dfbvideosink->contrast;
  } else if (g_ascii_strcasecmp (channel->label, "BRIGHTNESS") == 0) {
    value = dfbvideosink->brightness;
  } else {
    GST_WARNING_OBJECT (dfbvideosink, "got an unknown channel %s",
        channel->label);
  }

  return value;
}

static void
gst_dfbvideosink_colorbalance_init (GstColorBalanceClass * iface)
{
  GST_COLOR_BALANCE_TYPE (iface) = GST_COLOR_BALANCE_HARDWARE;
  iface->list_channels = gst_dfbvideosink_colorbalance_list_channels;
  iface->set_value = gst_dfbvideosink_colorbalance_set_value;
  iface->get_value = gst_dfbvideosink_colorbalance_get_value;
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
    case ARG_HUE:
      dfbvideosink->hue = g_value_get_int (value);
      dfbvideosink->cb_changed = TRUE;
      gst_dfbvideosink_update_colorbalance (dfbvideosink);
      break;
    case ARG_CONTRAST:
      dfbvideosink->contrast = g_value_get_int (value);
      dfbvideosink->cb_changed = TRUE;
      gst_dfbvideosink_update_colorbalance (dfbvideosink);
      break;
    case ARG_BRIGHTNESS:
      dfbvideosink->brightness = g_value_get_int (value);
      dfbvideosink->cb_changed = TRUE;
      gst_dfbvideosink_update_colorbalance (dfbvideosink);
      break;
    case ARG_SATURATION:
      dfbvideosink->saturation = g_value_get_int (value);
      dfbvideosink->cb_changed = TRUE;
      gst_dfbvideosink_update_colorbalance (dfbvideosink);
      break;
    case ARG_PIXEL_ASPECT_RATIO:
      g_free (dfbvideosink->par);
      dfbvideosink->par = g_new0 (GValue, 1);
      g_value_init (dfbvideosink->par, GST_TYPE_FRACTION);
      if (!g_value_transform (value, dfbvideosink->par)) {
        GST_WARNING_OBJECT (dfbvideosink, "Could not transform string to "
            "aspect ratio");
        gst_value_set_fraction (dfbvideosink->par, 1, 1);
      }
      GST_DEBUG_OBJECT (dfbvideosink, "set PAR to %d/%d",
          gst_value_get_fraction_numerator (dfbvideosink->par),
          gst_value_get_fraction_denominator (dfbvideosink->par));
      break;
    case ARG_VSYNC:
      dfbvideosink->vsync = g_value_get_boolean (value);
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
    case ARG_HUE:
      g_value_set_int (value, dfbvideosink->hue);
      break;
    case ARG_CONTRAST:
      g_value_set_int (value, dfbvideosink->contrast);
      break;
    case ARG_BRIGHTNESS:
      g_value_set_int (value, dfbvideosink->brightness);
      break;
    case ARG_SATURATION:
      g_value_set_int (value, dfbvideosink->saturation);
      break;
    case ARG_PIXEL_ASPECT_RATIO:
      if (dfbvideosink->par)
        g_value_transform (dfbvideosink->par, value);
      break;
    case ARG_VSYNC:
      g_value_set_boolean (value, dfbvideosink->vsync);
      break;
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
gst_dfbvideosink_finalize (GObject * object)
{
  GstDfbVideoSink *dfbvideosink;

  dfbvideosink = GST_DFBVIDEOSINK (object);

  if (dfbvideosink->par) {
    g_free (dfbvideosink->par);
    dfbvideosink->par = NULL;
  }
  if (dfbvideosink->pool_lock) {
    g_mutex_free (dfbvideosink->pool_lock);
    dfbvideosink->pool_lock = NULL;
  }
  if (dfbvideosink->setup) {
    gst_dfbvideosink_cleanup (dfbvideosink);
  }

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_dfbvideosink_init (GstDfbVideoSink * dfbvideosink)
{
  dfbvideosink->pool_lock = g_mutex_new ();
  dfbvideosink->buffer_pool = NULL;
  dfbvideosink->video_height = dfbvideosink->out_height = 0;
  dfbvideosink->video_width = dfbvideosink->out_width = 0;
  dfbvideosink->fps_d = 0;
  dfbvideosink->fps_n = 0;

  dfbvideosink->dfb = NULL;
  dfbvideosink->vmodes = NULL;
  dfbvideosink->layer_id = -1;
  dfbvideosink->layer = NULL;
  dfbvideosink->primary = NULL;
  dfbvideosink->event_buffer = NULL;
  dfbvideosink->event_thread = NULL;

  dfbvideosink->ext_surface = NULL;

  dfbvideosink->pixel_format = DSPF_UNKNOWN;

  dfbvideosink->hw_scaling = FALSE;
  dfbvideosink->backbuffer = FALSE;
  dfbvideosink->vsync = TRUE;
  dfbvideosink->setup = FALSE;
  dfbvideosink->running = FALSE;

  dfbvideosink->cb_channels = NULL;
  dfbvideosink->brightness = -1;
  dfbvideosink->contrast = -1;
  dfbvideosink->hue = -1;
  dfbvideosink->saturation = -1;

  dfbvideosink->par = NULL;
}

static void
gst_dfbvideosink_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details_simple (element_class, "DirectFB video sink",
      "Sink/Video",
      "A DirectFB based videosink", "Julien Moutte <julien@moutte.net>");

  gst_element_class_add_static_pad_template (element_class,
      &gst_dfbvideosink_sink_template_factory);
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

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->finalize = gst_dfbvideosink_finalize;
  gobject_class->set_property = gst_dfbvideosink_set_property;
  gobject_class->get_property = gst_dfbvideosink_get_property;

  g_object_class_install_property (gobject_class, ARG_SURFACE,
      g_param_spec_pointer ("surface", "Surface",
          "The target surface for video",
          G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, ARG_CONTRAST,
      g_param_spec_int ("contrast", "Contrast", "The contrast of the video",
          0x0000, 0xFFFF, 0x8000, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, ARG_BRIGHTNESS,
      g_param_spec_int ("brightness", "Brightness",
          "The brightness of the video", 0x0000, 0xFFFF, 0x8000,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, ARG_HUE,
      g_param_spec_int ("hue", "Hue", "The hue of the video", 0x0000, 0xFFFF,
          0x8000, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, ARG_SATURATION,
      g_param_spec_int ("saturation", "Saturation",
          "The saturation of the video", 0x0000, 0xFFFF, 0x8000,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, ARG_PIXEL_ASPECT_RATIO,
      g_param_spec_string ("pixel-aspect-ratio", "Pixel Aspect Ratio",
          "The pixel aspect ratio of the device", "1/1",
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, ARG_VSYNC,
      g_param_spec_boolean ("vsync", "Vertical synchronisation",
          "Wait for next vertical sync to draw frames", TRUE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

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
    static const GInterfaceInfo colorbalance_info = {
      (GInterfaceInitFunc) gst_dfbvideosink_colorbalance_init,
      NULL,
      NULL,
    };

    dfbvideosink_type = g_type_register_static (GST_TYPE_VIDEO_SINK,
        "GstDfbVideoSink", &dfbvideosink_info, 0);

    g_type_add_interface_static (dfbvideosink_type,
        GST_TYPE_IMPLEMENTS_INTERFACE, &iface_info);
    g_type_add_interface_static (dfbvideosink_type, GST_TYPE_NAVIGATION,
        &navigation_info);
    g_type_add_interface_static (dfbvideosink_type, GST_TYPE_COLOR_BALANCE,
        &colorbalance_info);
  }

  return dfbvideosink_type;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  if (!gst_element_register (plugin, "dfbvideosink", GST_RANK_MARGINAL,
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
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
