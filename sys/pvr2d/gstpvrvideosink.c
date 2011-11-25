/* GStreamer
 *
 * Copyright (C) 2011 Collabora Ltda
 * Copyright (C) 2011 Texas Instruments
 *  @author: Luciana Fujii Pontello <luciana.fujii@collabora.co.uk>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
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

/* Object header */
#include "gstpvrvideosink.h"

#include "gstpvrbufferpool.h"
#include <gst/video/gstvideosink.h>
#include <gst/interfaces/xoverlay.h>

/* Debugging category */
#include <gst/gstinfo.h>

#define LINUX
#include <dri2_ws.h>
#include <services.h>
#include <img_defs.h>
#include <servicesext.h>

#define DEFAULT_QUEUE_SIZE 12
#define DEFAULT_MIN_QUEUED_BUFS 1

GST_DEBUG_CATEGORY_EXTERN (gst_debug_pvrvideosink);
#define GST_CAT_DEFAULT gst_debug_pvrvideosink

#define PVR2DMEMINFO_INITIALISE(d, s) \
{ \
  (d)->hPrivateData = (IMG_VOID *)(s); \
  (d)->hPrivateMapData = (IMG_VOID *)(s->hKernelMemInfo); \
  (d)->ui32DevAddr = (IMG_UINT32) (s)->sDevVAddr.uiAddr; \
  (d)->ui32MemSize = (s)->uAllocSize; \
  (d)->pBase = (s)->pvLinAddr;\
  (d)->ulFlags = (s)->ui32Flags;\
}

/* end of internal definitions */

static void gst_pvrvideosink_reset (GstPVRVideoSink * pvrvideosink);
static GstFlowReturn gst_pvrvideosink_buffer_alloc (GstBaseSink * bsink,
    guint64 offset, guint size, GstCaps * caps, GstBuffer ** buf);
static void gst_pvrvideosink_xwindow_draw_borders (GstPVRVideoSink *
    pvrvideosink, GstXWindow * xwindow, GstVideoRectangle rect);
static void gst_pvrvideosink_expose (GstXOverlay * overlay);
static void gst_pvrvideosink_xwindow_destroy (GstPVRVideoSink * pvrvideosink,
    GstXWindow * xwindow);

static GstStaticPadTemplate gst_pvrvideosink_sink_template_factory =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw-yuv, "
        "format = (fourcc) NV12, "
        "width = " GST_VIDEO_SIZE_RANGE ", "
        "height = " GST_VIDEO_SIZE_RANGE ", "
        "framerate = " GST_VIDEO_FPS_RANGE));

enum
{
  PROP_0,
  PROP_FORCE_ASPECT_RATIO,
  PROP_WINDOW_WIDTH,
  PROP_WINDOW_HEIGHT
};

static GstVideoSinkClass *parent_class = NULL;

/* ============================================================= */
/*                                                               */
/*                       Private Methods                         */
/*                                                               */
/* ============================================================= */

/* pvrvideo buffers */

#define GST_TYPE_PVRVIDEO_BUFFER (gst_pvrvideo_buffer_get_type())

#define GST_IS_PVRVIDEO_BUFFER(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_PVRVIDEO_BUFFER))
#define GST_PVRVIDEO_BUFFER(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_PVRVIDEO_BUFFER, GstPVRVideoBuffer))
#define GST_PVRVIDEO_BUFFER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_PVRVIDEO_BUFFER, GstPVRVideoBufferClass))

static void
gst_pvrvideosink_xwindow_update_geometry (GstPVRVideoSink * pvrvideosink)
{
  XWindowAttributes attr;
  WSEGLError glerror;
  WSEGLDrawableParams source_params;
  PVRSRV_CLIENT_MEM_INFO *client_mem_info;

  /* Update the window geometry */
  g_mutex_lock (pvrvideosink->dcontext->x_lock);
  if (G_UNLIKELY (pvrvideosink->xwindow == NULL)) {
    g_mutex_unlock (pvrvideosink->dcontext->x_lock);
    return;
  }
  pvrvideosink->redraw_borders = TRUE;

  XGetWindowAttributes (pvrvideosink->dcontext->x_display,
      pvrvideosink->xwindow->window, &attr);

  pvrvideosink->xwindow->width = attr.width;
  pvrvideosink->xwindow->height = attr.height;

  if (!pvrvideosink->have_render_rect) {
    pvrvideosink->render_rect.x = pvrvideosink->render_rect.y = 0;
    pvrvideosink->render_rect.w = attr.width;
    pvrvideosink->render_rect.h = attr.height;
  }
  if (pvrvideosink->dcontext != NULL) {
    glerror =
        pvrvideosink->dcontext->
        wsegl_table->pfnWSEGL_DeleteDrawable (pvrvideosink->dcontext->
        drawable_handle);
    if (glerror != WSEGL_SUCCESS) {
      GST_ERROR_OBJECT (pvrvideosink, "Error destroying drawable");
      return;
    }
    glerror =
        pvrvideosink->dcontext->
        wsegl_table->pfnWSEGL_CreateWindowDrawable (pvrvideosink->dcontext->
        display_handle, pvrvideosink->dcontext->glconfig,
        &pvrvideosink->dcontext->drawable_handle,
        (NativeWindowType) pvrvideosink->xwindow->window,
        &pvrvideosink->dcontext->rotation);
    if (glerror != WSEGL_SUCCESS) {
      GST_ERROR_OBJECT (pvrvideosink, "Error creating drawable");
      return;
    }
    glerror =
        pvrvideosink->dcontext->
        wsegl_table->pfnWSEGL_GetDrawableParameters (pvrvideosink->dcontext->
        drawable_handle, &source_params, &pvrvideosink->render_params);
    if (glerror != WSEGL_SUCCESS) {
      GST_ERROR_OBJECT (pvrvideosink, "Error getting Drawable params");
      return;
    }

    client_mem_info =
        (PVRSRV_CLIENT_MEM_INFO *) pvrvideosink->render_params.hPrivateData;
    PVR2DMEMINFO_INITIALISE (&pvrvideosink->dcontext->dst_mem, client_mem_info);
  }

  g_mutex_unlock (pvrvideosink->dcontext->x_lock);
}

/* This function handles XEvents that might be in the queue. It generates
   GstEvent that will be sent upstream in the pipeline to handle interactivity
   and navigation. It will also listen for configure events on the window to
   trigger caps renegotiation so on the fly software scaling can work. */
static void
gst_pvrvideosink_handle_xevents (GstPVRVideoSink * pvrvideosink)
{
  XEvent e;
  gboolean exposed = FALSE;
  gboolean configured = FALSE;

  g_mutex_lock (pvrvideosink->flow_lock);
  g_mutex_lock (pvrvideosink->dcontext->x_lock);

  /* Handle Expose */
  while (XCheckWindowEvent (pvrvideosink->dcontext->x_display,
          pvrvideosink->xwindow->window, ExposureMask | StructureNotifyMask,
          &e)) {
    switch (e.type) {
      case Expose:
        exposed = TRUE;
        break;
      case ConfigureNotify:
        g_mutex_unlock (pvrvideosink->dcontext->x_lock);
        gst_pvrvideosink_xwindow_update_geometry (pvrvideosink);
        g_mutex_lock (pvrvideosink->dcontext->x_lock);
        configured = TRUE;
        break;
      default:
        break;
    }
  }

  if (exposed || configured) {
    g_mutex_unlock (pvrvideosink->dcontext->x_lock);
    g_mutex_unlock (pvrvideosink->flow_lock);

    gst_pvrvideosink_expose (GST_X_OVERLAY (pvrvideosink));

    g_mutex_lock (pvrvideosink->flow_lock);
    g_mutex_lock (pvrvideosink->dcontext->x_lock);
  }

  /* Handle Display events */
  while (XPending (pvrvideosink->dcontext->x_display)) {
    XNextEvent (pvrvideosink->dcontext->x_display, &e);

    switch (e.type) {
      case ClientMessage:{
        Atom wm_delete;

        wm_delete = XInternAtom (pvrvideosink->dcontext->x_display,
            "WM_DELETE_WINDOW", True);
        if (wm_delete != None && wm_delete == (Atom) e.xclient.data.l[0]) {
          /* Handle window deletion by posting an error on the bus */
          GST_ELEMENT_ERROR (pvrvideosink, RESOURCE, NOT_FOUND,
              ("Output window was closed"), (NULL));

          g_mutex_unlock (pvrvideosink->dcontext->x_lock);
          gst_pvrvideosink_xwindow_destroy (pvrvideosink,
              pvrvideosink->xwindow);
          pvrvideosink->xwindow = NULL;
          g_mutex_lock (pvrvideosink->dcontext->x_lock);
        }
        break;
      }
      default:
        break;
    }
  }

  g_mutex_unlock (pvrvideosink->dcontext->x_lock);
  g_mutex_unlock (pvrvideosink->flow_lock);
}

static gpointer
gst_pvrvideosink_event_thread (GstPVRVideoSink * pvrvideosink)
{
  GST_OBJECT_LOCK (pvrvideosink);
  while (pvrvideosink->running) {
    GST_OBJECT_UNLOCK (pvrvideosink);

    if (pvrvideosink->xwindow) {
      gst_pvrvideosink_handle_xevents (pvrvideosink);
    }
    g_usleep (G_USEC_PER_SEC / 20);

    GST_OBJECT_LOCK (pvrvideosink);
  }
  GST_OBJECT_UNLOCK (pvrvideosink);

  return NULL;
}

static void
gst_pvrvideosink_manage_event_thread (GstPVRVideoSink * pvrvideosink)
{
  GThread *thread = NULL;

  /* don't start the thread too early */
  if (pvrvideosink->dcontext == NULL) {
    return;
  }

  GST_OBJECT_LOCK (pvrvideosink);
  if (!pvrvideosink->event_thread) {
    /* Setup our event listening thread */
    GST_DEBUG_OBJECT (pvrvideosink, "run xevent thread");
    pvrvideosink->running = TRUE;
    pvrvideosink->event_thread = g_thread_create (
        (GThreadFunc) gst_pvrvideosink_event_thread, pvrvideosink, TRUE, NULL);
  }
  GST_OBJECT_UNLOCK (pvrvideosink);

  /* Wait for our event thread to finish */
  if (thread)
    g_thread_join (thread);
}


static GstDrawContext *
gst_pvrvideosink_get_dcontext (GstPVRVideoSink * pvrvideosink)
{
  GstDrawContext *dcontext;
  PVR2DERROR pvr_error;
  gint refresh_rate;
  DRI2WSDisplay *displayImpl;
  WSEGLError glerror;
  const WSEGLCaps *glcaps;

  dcontext = g_new0 (GstDrawContext, 1);
  dcontext->p_blt_info = 0;
  dcontext->x_lock = g_mutex_new ();

  dcontext->p_blt_info = g_new0 (PVR2D_3DBLT_EXT, 1);
  if (!dcontext->p_blt_info) {
    GST_ERROR_OBJECT (pvrvideosink, "Alloc of bltinfo failed");
    return NULL;
  }
  dcontext->p_blt2d_info = g_new0 (PVR2DBLTINFO, 1);

  dcontext->x_display = XOpenDisplay (NULL);

  dcontext->wsegl_table = WSEGL_GetFunctionTablePointer ();
  glerror = dcontext->wsegl_table->pfnWSEGL_IsDisplayValid (
      (NativeDisplayType) dcontext->x_display);

  if (glerror != WSEGL_SUCCESS) {
    GST_ERROR_OBJECT (pvrvideosink, "Display is not valid");
    return NULL;
  }

  glerror = dcontext->wsegl_table->pfnWSEGL_InitialiseDisplay (
      (NativeDisplayType) dcontext->x_display, &dcontext->display_handle,
      &glcaps, &dcontext->glconfig);
  if (glerror != WSEGL_SUCCESS) {
    GST_ERROR_OBJECT (pvrvideosink, "Error initializing display");
    return NULL;
  }

  displayImpl = (DRI2WSDisplay *) dcontext->display_handle;
  dcontext->pvr_context = displayImpl->hContext;

  pvr_error = PVR2DGetScreenMode (dcontext->pvr_context,
      &dcontext->display_format, &dcontext->display_width,
      &dcontext->display_height, &dcontext->stride, &refresh_rate);
  if (pvr_error != PVR2D_OK) {
    GST_ERROR_OBJECT (pvrvideosink, "Failed) to get screen mode"
        "returned %d", pvr_error);
    return NULL;
  }
  dcontext->screen_num = DefaultScreen (dcontext->x_display);
  dcontext->black = XBlackPixel (dcontext->x_display, dcontext->screen_num);

  return dcontext;
}

static void
gst_pvrvideosink_xwindow_set_title (GstPVRVideoSink * pvrvideosink,
    GstXWindow * xwindow, const gchar * media_title)
{
  if (media_title) {
    g_free (pvrvideosink->media_title);
    pvrvideosink->media_title = g_strdup (media_title);
  }
  if (xwindow) {
    /* we have a window */
    if (xwindow->internal) {
      XTextProperty xproperty;
      const gchar *app_name;
      const gchar *title = NULL;
      gchar *title_mem = NULL;

      /* set application name as a title */
      app_name = g_get_application_name ();

      if (app_name && pvrvideosink->media_title) {
        title = title_mem = g_strconcat (pvrvideosink->media_title, " : ",
            app_name, NULL);
      } else if (app_name) {
        title = app_name;
      } else if (pvrvideosink->media_title) {
        title = pvrvideosink->media_title;
      }

      if (title) {
        if ((XStringListToTextProperty (((char **) &title), 1,
                    &xproperty)) != 0) {
          XSetWMName (pvrvideosink->dcontext->x_display, xwindow->window,
              &xproperty);
          XFree (xproperty.value);
        }

        g_free (title_mem);
      }
    }
  }
}

static GstXWindow *
gst_pvrvideosink_create_window (GstPVRVideoSink * pvrvideosink, gint width,
    gint height)
{
  WSEGLError glerror;
  WSEGLDrawableParams source_params;
  Window root;
  GstXWindow *xwindow;
  GstDrawContext *dcontext;
  XGCValues values;
  Atom wm_delete;
  PVRSRV_CLIENT_MEM_INFO *client_mem_info;

  GST_DEBUG_OBJECT (pvrvideosink, "begin");

  dcontext = pvrvideosink->dcontext;
  xwindow = g_new0 (GstXWindow, 1);

  xwindow->internal = TRUE;
  pvrvideosink->render_rect.x = pvrvideosink->render_rect.y = 0;
  pvrvideosink->render_rect.w = width;
  pvrvideosink->render_rect.h = height;
  xwindow->width = width;
  xwindow->height = height;
  xwindow->internal = TRUE;

  g_mutex_lock (pvrvideosink->dcontext->x_lock);

  root = DefaultRootWindow (dcontext->x_display);
  xwindow->window = XCreateSimpleWindow (dcontext->x_display, root, 0, 0,
      width, height, 2, 2, pvrvideosink->dcontext->black);
  XSelectInput (dcontext->x_display, xwindow->window,
      ExposureMask | StructureNotifyMask);

  /* Tell the window manager we'd like delete client messages instead of
   * being killed */
  wm_delete = XInternAtom (pvrvideosink->dcontext->x_display,
      "WM_DELETE_WINDOW", True);
  if (wm_delete != None) {
    (void) XSetWMProtocols (pvrvideosink->dcontext->x_display, xwindow->window,
        &wm_delete, 1);
  }

  XMapWindow (dcontext->x_display, xwindow->window);

  /* We have to do that to prevent X from redrawing the background on
   * ConfigureNotify. This takes away flickering of video when resizing. */
  XSetWindowBackgroundPixmap (pvrvideosink->dcontext->x_display,
      xwindow->window, None);

  gst_pvrvideosink_xwindow_set_title (pvrvideosink, xwindow, NULL);

  xwindow->gc = XCreateGC (pvrvideosink->dcontext->x_display,
      xwindow->window, 0, &values);

  g_mutex_unlock (pvrvideosink->dcontext->x_lock);

  glerror =
      dcontext->wsegl_table->pfnWSEGL_CreateWindowDrawable (dcontext->
      display_handle, dcontext->glconfig, &(dcontext->drawable_handle),
      (NativeWindowType) xwindow->window, &(dcontext->rotation));

  if (glerror != WSEGL_SUCCESS) {
    GST_ERROR_OBJECT (pvrvideosink, "Error creating drawable");
    return NULL;
  }
  glerror =
      dcontext->wsegl_table->pfnWSEGL_GetDrawableParameters (dcontext->
      drawable_handle, &source_params, &pvrvideosink->render_params);
  client_mem_info =
      (PVRSRV_CLIENT_MEM_INFO *) pvrvideosink->render_params.hPrivateData;
  PVR2DMEMINFO_INITIALISE (&dcontext->dst_mem, client_mem_info);

  GST_DEBUG_OBJECT (pvrvideosink, "end");
  return xwindow;
}

static void
gst_pvrvideosink_blit (GstPVRVideoSink * pvrvideosink, GstBuffer * buffer)
{
  PVR2DERROR pvr_error;
  GstDrawContext *dcontext = pvrvideosink->dcontext;
  GstCaps *caps;
  GstStructure *structure;
  gint video_width;
  gint video_height;
  gboolean ret;
  gboolean draw_border = FALSE;
  PPVR2D_3DBLT_EXT p_blt_3d;
  PVR2DMEMINFO *src_mem;
  PVR2DFORMAT pvr_format = pvrvideosink->format == GST_VIDEO_FORMAT_NV12 ?
      PVR2D_YUV420_2PLANE : PVR2D_ARGB8888;
  GstVideoRectangle result;

  GST_DEBUG_OBJECT (pvrvideosink, "begin");
  g_mutex_lock (pvrvideosink->flow_lock);
  if (buffer == NULL)
    buffer = pvrvideosink->current_buffer;

  if (buffer == NULL) {
    g_mutex_unlock (pvrvideosink->flow_lock);
    return;
  }

  caps = GST_BUFFER_CAPS (buffer);
  src_mem = gst_ducati_buffer_get_meminfo ((GstDucatiBuffer *) buffer);
  p_blt_3d = dcontext->p_blt_info;

  structure = gst_caps_get_structure (caps, 0);
  ret = gst_structure_get_int (structure, "width", &video_width);
  ret &= gst_structure_get_int (structure, "height", &video_height);
  if (!ret) {
    GST_ERROR_OBJECT (pvrvideosink, "Failed to get dimensions of the buffer");
    goto done;
  }

  g_mutex_lock (pvrvideosink->dcontext->x_lock);

  /* Draw borders when displaying the first frame. After this
     draw borders only on expose event or after a size change. */
  if (!(pvrvideosink->current_buffer) || pvrvideosink->redraw_borders) {
    draw_border = TRUE;
  }

  /* Store a reference to the last image we put, lose the previous one */
  if (buffer && pvrvideosink->current_buffer != buffer) {
    if (pvrvideosink->current_buffer) {
      GST_LOG_OBJECT (pvrvideosink, "unreffing %p",
          pvrvideosink->current_buffer);
      gst_buffer_unref (GST_BUFFER_CAST (pvrvideosink->current_buffer));
    }
    GST_LOG_OBJECT (pvrvideosink, "reffing %p as our current buffer", buffer);
    pvrvideosink->current_buffer = gst_buffer_ref (buffer);
  }

  if (pvrvideosink->keep_aspect) {
    GstVideoRectangle src, dst;

    src.w = GST_VIDEO_SINK_WIDTH (pvrvideosink);
    src.h = GST_VIDEO_SINK_HEIGHT (pvrvideosink);
    dst.w = pvrvideosink->render_rect.w;
    dst.h = pvrvideosink->render_rect.h;
    gst_video_sink_center_rect (src, dst, &result, TRUE);
    result.x += pvrvideosink->render_rect.x;
    result.y += pvrvideosink->render_rect.y;
  } else {
    memcpy (&result, &pvrvideosink->render_rect, sizeof (GstVideoRectangle));
  }

  p_blt_3d->sDst.pSurfMemInfo = &dcontext->dst_mem;
  p_blt_3d->sDst.SurfOffset = 0;
  p_blt_3d->sDst.Stride =
      gst_video_format_get_row_stride (GST_VIDEO_FORMAT_BGRx, 0,
      pvrvideosink->render_params.ui32Stride);
  p_blt_3d->sDst.Format = PVR2D_ARGB8888;
  p_blt_3d->sDst.SurfWidth = pvrvideosink->xwindow->width;
  p_blt_3d->sDst.SurfHeight = pvrvideosink->xwindow->height;

  p_blt_3d->rcDest.left = result.x;
  p_blt_3d->rcDest.top = result.y;
  p_blt_3d->rcDest.right = result.w + result.x;
  p_blt_3d->rcDest.bottom = result.h + result.y;

  p_blt_3d->sSrc.pSurfMemInfo = src_mem;
  p_blt_3d->sSrc.SurfOffset = 0;
  p_blt_3d->sSrc.Stride = pvrvideosink->rowstride;
  p_blt_3d->sSrc.Format = pvr_format;
  p_blt_3d->sSrc.SurfWidth = video_width;
  p_blt_3d->sSrc.SurfHeight = video_height;

  p_blt_3d->rcSource.left = 0;
  p_blt_3d->rcSource.top = 0;
  p_blt_3d->rcSource.right = video_width;
  p_blt_3d->rcSource.bottom = video_height;

  p_blt_3d->hUseCode = NULL;

  if (pvrvideosink->format == GST_VIDEO_FORMAT_NV12)
    p_blt_3d->bDisableDestInput = TRUE;
  else
    /* blit fails for RGB without this... not sure why yet... */
    p_blt_3d->bDisableDestInput = FALSE;

  pvr_error = PVR2DBlt3DExt (pvrvideosink->dcontext->pvr_context,
      dcontext->p_blt_info);

  switch (pvr_error) {
    case PVR2D_OK:
      break;
    case PVR2DERROR_DEVICE_UNAVAILABLE:
      GST_ERROR_OBJECT (pvrvideosink, "Failed to blit, device unavailable");
      goto done;
      break;
    case PVR2DERROR_INVALID_CONTEXT:
      GST_ERROR_OBJECT (pvrvideosink, "Failed to blit, invalid context");
      goto done;
      break;
    case PVR2DERROR_INVALID_PARAMETER:
      GST_ERROR_OBJECT (pvrvideosink, "Failed to blit, invalid parameter");
      goto done;
      break;
    case PVR2DERROR_HW_FEATURE_NOT_SUPPORTED:
      GST_ERROR_OBJECT (pvrvideosink, "Failed to blit, "
          "hardware feature not supported");
      goto done;
      break;
    case PVR2DERROR_GENERIC_ERROR:
      GST_ERROR_OBJECT (pvrvideosink, "Failed to blit, generic error");
      goto done;
      break;
    default:
      GST_ERROR_OBJECT (pvrvideosink, "Failed to blit, "
          "undefined error %d", pvr_error);
      goto done;
      break;
  }
  dcontext->wsegl_table->pfnWSEGL_SwapDrawable (dcontext->drawable_handle, 1);

  if (draw_border) {
    gst_pvrvideosink_xwindow_draw_borders (pvrvideosink, pvrvideosink->xwindow,
        result);
    pvrvideosink->redraw_borders = FALSE;
  }
  g_mutex_unlock (pvrvideosink->dcontext->x_lock);

done:
  GST_DEBUG_OBJECT (pvrvideosink, "end");
  g_mutex_unlock (pvrvideosink->flow_lock);
}

static void
gst_pvrvideosink_destroy_drawable (GstPVRVideoSink * pvrvideosink)
{
  if (pvrvideosink->dcontext != NULL) {
    if (pvrvideosink->dcontext->drawable_handle)
      pvrvideosink->dcontext->
          wsegl_table->pfnWSEGL_DeleteDrawable (pvrvideosink->dcontext->
          drawable_handle);

    pvrvideosink->dcontext->wsegl_table->pfnWSEGL_CloseDisplay (pvrvideosink->
        dcontext->display_handle);
  }
}

/* We are called with the x_lock taken */
static void
gst_pvrvideosink_pvrfill_rectangle (GstPVRVideoSink * pvrvideosink,
    GstVideoRectangle rect)
{
  PVR2DERROR pvr_error;
  PPVR2DBLTINFO p_blt2d_info = 0;
  GstDrawContext *dcontext = pvrvideosink->dcontext;

  GST_DEBUG_OBJECT (pvrvideosink, "begin");

  p_blt2d_info = dcontext->p_blt2d_info;

  p_blt2d_info->pDstMemInfo = &dcontext->dst_mem;
  p_blt2d_info->BlitFlags = PVR2D_BLIT_DISABLE_ALL;
  p_blt2d_info->DstOffset = 0;
  p_blt2d_info->CopyCode = PVR2DROPclear;
  p_blt2d_info->DstStride =
      gst_video_format_get_row_stride (GST_VIDEO_FORMAT_BGRx, 0,
      pvrvideosink->render_params.ui32Stride);
  p_blt2d_info->DstFormat = PVR2D_ARGB8888;
  p_blt2d_info->DstSurfWidth = pvrvideosink->xwindow->width;
  p_blt2d_info->DstSurfHeight = pvrvideosink->xwindow->height;
  p_blt2d_info->DstX = rect.x;
  p_blt2d_info->DstY = rect.y;
  p_blt2d_info->DSizeX = rect.w;
  p_blt2d_info->DSizeY = rect.h;

  pvr_error = PVR2DBlt (pvrvideosink->dcontext->pvr_context, p_blt2d_info);

  switch (pvr_error) {
    case PVR2D_OK:
      break;
    case PVR2DERROR_DEVICE_UNAVAILABLE:
      GST_ERROR_OBJECT (pvrvideosink, "Failed to blit, device unavailable");
      goto done;
      break;
    case PVR2DERROR_INVALID_CONTEXT:
      GST_ERROR_OBJECT (pvrvideosink, "Failed to blit, invalid context");
      goto done;
      break;
    case PVR2DERROR_INVALID_PARAMETER:
      GST_ERROR_OBJECT (pvrvideosink, "Failed to blit, invalid parameter");
      goto done;
      break;
    case PVR2DERROR_HW_FEATURE_NOT_SUPPORTED:
      GST_ERROR_OBJECT (pvrvideosink, "Failed to blit, "
          "hardware feature not supported");
      goto done;
      break;
    case PVR2DERROR_GENERIC_ERROR:
      GST_ERROR_OBJECT (pvrvideosink, "Failed to blit, generic error");
      goto done;
      break;
    default:
      GST_ERROR_OBJECT (pvrvideosink, "Failed to blit, "
          "undefined error %d", pvr_error);
      goto done;
      break;
  }
  dcontext->wsegl_table->pfnWSEGL_SwapDrawable (dcontext->drawable_handle, 1);

done:
  GST_DEBUG_OBJECT (pvrvideosink, "end");
}

/* We are called with the x_lock taken */
static void
gst_pvrvideosink_xwindow_draw_borders (GstPVRVideoSink * pvrvideosink,
    GstXWindow * xwindow, GstVideoRectangle rect)
{
  gint t1, t2;
  GstVideoRectangle result;

  g_return_if_fail (GST_IS_PVRVIDEOSINK (pvrvideosink));
  g_return_if_fail (xwindow != NULL);

  /* Left border */
  result.x = pvrvideosink->render_rect.x;
  result.y = pvrvideosink->render_rect.y;
  result.w = rect.x - pvrvideosink->render_rect.x;
  result.h = pvrvideosink->render_rect.h;
  if (rect.x > pvrvideosink->render_rect.x)
    gst_pvrvideosink_pvrfill_rectangle (pvrvideosink, result);

  /* Right border */
  t1 = rect.x + rect.w;
  t2 = pvrvideosink->render_rect.x + pvrvideosink->render_rect.w;
  result.x = t1;
  result.y = pvrvideosink->render_rect.y;
  result.w = t2 - t1;
  result.h = pvrvideosink->render_rect.h;
  if (t1 < t2)
    gst_pvrvideosink_pvrfill_rectangle (pvrvideosink, result);

  /* Top border */
  result.x = pvrvideosink->render_rect.x;
  result.y = pvrvideosink->render_rect.y;
  result.w = pvrvideosink->render_rect.w;
  result.h = rect.y - pvrvideosink->render_rect.y;
  if (rect.y > pvrvideosink->render_rect.y)
    gst_pvrvideosink_pvrfill_rectangle (pvrvideosink, result);

  /* Bottom border */
  t1 = rect.y + rect.h;
  t2 = pvrvideosink->render_rect.y + pvrvideosink->render_rect.h;
  result.x = pvrvideosink->render_rect.x;
  result.y = t1;
  result.w = pvrvideosink->render_rect.w;
  result.h = t2 - t1;
  if (t1 < t2)
    gst_pvrvideosink_pvrfill_rectangle (pvrvideosink, result);
}

/* Element stuff */

static gboolean
gst_pvrvideosink_setcaps (GstBaseSink * bsink, GstCaps * caps)
{
  GstPVRVideoSink *pvrvideosink;
  gboolean ret = TRUE;
  GstStructure *structure;
  gint new_width, new_height;
  const GValue *fps;
  GstQuery *query;

  pvrvideosink = GST_PVRVIDEOSINK (bsink);

  GST_DEBUG_OBJECT (pvrvideosink,
      "sinkconnect possible caps with given caps %", caps);

  structure = gst_caps_get_structure (caps, 0);

  ret = gst_video_format_parse_caps_strided (caps, &pvrvideosink->format,
      &new_width, &new_height, &pvrvideosink->rowstride);
  if (pvrvideosink->rowstride == 0)
    pvrvideosink->rowstride =
        gst_video_format_get_row_stride (pvrvideosink->format, 0, new_width);
  fps = gst_structure_get_value (structure, "framerate");
  ret &= (fps != NULL);
  if (!ret) {
    GST_ERROR_OBJECT (pvrvideosink, "problem at parsing caps");
    return FALSE;
  }

  if (pvrvideosink->current_caps) {
    GST_DEBUG_OBJECT (pvrvideosink, "already have caps set");
    if (gst_caps_is_equal (pvrvideosink->current_caps, caps)) {
      GST_DEBUG_OBJECT (pvrvideosink, "caps are equal!");
      return TRUE;
    }
    GST_DEBUG_OBJECT (pvrvideosink, "caps are different");
  }

  g_mutex_lock (pvrvideosink->pool_lock);
  if (pvrvideosink->buffer_pool) {
    if (!gst_caps_is_equal (pvrvideosink->buffer_pool->caps, caps)) {
      GST_INFO_OBJECT (pvrvideosink, "in set caps, pool->caps != caps");
      gst_pvr_bufferpool_stop_running (pvrvideosink->buffer_pool, FALSE);
      pvrvideosink->buffer_pool = NULL;
    }
  }
  g_mutex_unlock (pvrvideosink->pool_lock);

  /* query to find if anyone upstream using these buffers has any
   * minimum requirements:
   */
  query = gst_query_new_buffers (caps);
  if (gst_element_query (GST_ELEMENT (pvrvideosink), query)) {
    gint min_buffers;

    gst_query_parse_buffers_count (query, &min_buffers);

    GST_DEBUG_OBJECT (pvrvideosink, "min_buffers=%d", min_buffers);

    /* XXX need to account for some buffers used by queue, etc.. probably
     * queue should handle query, pass on to sink pad, and then add some
     * number of buffers to the min, so this value is dynamic depending
     * on the pipeline?
     */
    if (min_buffers != -1) {
      min_buffers += 3 + pvrvideosink->min_queued_bufs;
      pvrvideosink->num_buffers_can_change = FALSE;
    }

    if (min_buffers > pvrvideosink->num_buffers) {
      pvrvideosink->num_buffers = min_buffers;
    }
  }
  gst_query_unref (query);

  /* Notify application to set xwindow id now */
  g_mutex_lock (pvrvideosink->flow_lock);
  if (!pvrvideosink->xwindow) {
    g_mutex_unlock (pvrvideosink->flow_lock);
    gst_x_overlay_prepare_xwindow_id (GST_X_OVERLAY (pvrvideosink));
  } else {
    g_mutex_unlock (pvrvideosink->flow_lock);
  }
  GST_VIDEO_SINK_WIDTH (pvrvideosink) = new_width;
  GST_VIDEO_SINK_HEIGHT (pvrvideosink) = new_height;

  g_mutex_lock (pvrvideosink->flow_lock);
  if (!pvrvideosink->xwindow)
    pvrvideosink->xwindow = gst_pvrvideosink_create_window (pvrvideosink,
        new_width, new_height);
  g_mutex_unlock (pvrvideosink->flow_lock);

  pvrvideosink->fps_n = gst_value_get_fraction_numerator (fps);
  pvrvideosink->fps_d = gst_value_get_fraction_denominator (fps);

  pvrvideosink->current_caps = gst_caps_ref (caps);

  return TRUE;
}

static GstCaps *
gst_pvrvideosink_getcaps (GstBaseSink * bsink)
{
  GstPVRVideoSink *pvrvideosink;
  GstCaps *caps;

  pvrvideosink = GST_PVRVIDEOSINK (bsink);

  caps = gst_caps_copy (gst_pad_get_pad_template_caps (GST_BASE_SINK
          (pvrvideosink)->sinkpad));
  return caps;
}

static GstStateChangeReturn
gst_pvrvideosink_change_state (GstElement * element, GstStateChange transition)
{
  GstPVRVideoSink *pvrvideosink;
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
  GstDrawContext *dcontext;

  pvrvideosink = GST_PVRVIDEOSINK (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      if (pvrvideosink->dcontext == NULL) {
        dcontext = gst_pvrvideosink_get_dcontext (pvrvideosink);
        if (dcontext == NULL)
          return GST_STATE_CHANGE_FAILURE;
        GST_OBJECT_LOCK (pvrvideosink);
        pvrvideosink->dcontext = dcontext;
        GST_OBJECT_UNLOCK (pvrvideosink);
      }
      gst_pvrvideosink_manage_event_thread (pvrvideosink);
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      g_mutex_lock (pvrvideosink->pool_lock);
      pvrvideosink->pool_invalid = FALSE;
      g_mutex_unlock (pvrvideosink->pool_lock);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      g_mutex_lock (pvrvideosink->pool_lock);
      pvrvideosink->pool_invalid = TRUE;
      g_mutex_unlock (pvrvideosink->pool_lock);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      pvrvideosink->fps_n = 0;
      pvrvideosink->fps_d = 1;
      GST_VIDEO_SINK_WIDTH (pvrvideosink) = 0;
      GST_VIDEO_SINK_HEIGHT (pvrvideosink) = 0;
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      gst_pvrvideosink_reset (pvrvideosink);
      break;
    default:
      break;
  }

  return ret;
}

static void
gst_pvrvideosink_get_times (GstBaseSink * bsink, GstBuffer * buf,
    GstClockTime * start, GstClockTime * end)
{
  GstPVRVideoSink *pvrvideosink;

  pvrvideosink = GST_PVRVIDEOSINK (bsink);

  if (GST_BUFFER_TIMESTAMP_IS_VALID (buf)) {
    *start = GST_BUFFER_TIMESTAMP (buf);
    if (GST_BUFFER_DURATION_IS_VALID (buf)) {
      *end = *start + GST_BUFFER_DURATION (buf);
    } else {
      if (pvrvideosink->fps_n > 0) {
        *end = *start +
            gst_util_uint64_scale_int (GST_SECOND, pvrvideosink->fps_d,
            pvrvideosink->fps_n);
      }
    }
  }
}

static GstFlowReturn
gst_pvrvideosink_show_frame (GstBaseSink * vsink, GstBuffer * buf)
{
  GstPVRVideoSink *pvrvideosink;
  GstBuffer *newbuf = NULL;
  g_return_val_if_fail (buf != NULL, GST_FLOW_ERROR);

  pvrvideosink = GST_PVRVIDEOSINK (vsink);

  GST_DEBUG_OBJECT (pvrvideosink, "render buffer: %p", buf);

  if (!GST_IS_BUFFER (buf)) {
    GstFlowReturn ret;

    /* special case check for sub-buffers:  In certain cases, places like
     * GstBaseTransform, which might check that the buffer is writable
     * before copying metadata, timestamp, and such, will find that the
     * buffer has more than one reference to it.  In these cases, they
     * will create a sub-buffer with an offset=0 and length equal to the
     * original buffer size.
     *
     * This could happen in two scenarios: (1) a tee in the pipeline, and
     * (2) because the refcnt is incremented in gst_mini_object_free()
     * before the finalize function is called, and decremented after it
     * returns..  but returning this buffer to the buffer pool in the
     * finalize function, could wake up a thread blocked in _buffer_alloc()
     * which could run and get a buffer w/ refcnt==2 before the thread
     * originally unref'ing the buffer returns from finalize function and
     * decrements the refcnt back to 1!
     */
    if (buf->parent &&
        (GST_BUFFER_DATA (buf) == GST_BUFFER_DATA (buf->parent)) &&
        (GST_BUFFER_SIZE (buf) == GST_BUFFER_SIZE (buf->parent))) {
      GST_DEBUG_OBJECT (pvrvideosink, "I have a sub-buffer!");
      return gst_pvrvideosink_show_frame (vsink, buf->parent);
    }

    GST_DEBUG_OBJECT (pvrvideosink,
        "slow-path.. I got a %s so I need to memcpy",
        g_type_name (G_OBJECT_TYPE (buf)));

    ret = gst_pvrvideosink_buffer_alloc (GST_BASE_SINK (vsink),
        GST_BUFFER_OFFSET (buf), GST_BUFFER_SIZE (buf), GST_BUFFER_CAPS (buf),
        &newbuf);

    if (GST_FLOW_OK != ret) {
      GST_DEBUG_OBJECT (pvrvideosink, "dropping frame!!");
      return GST_FLOW_OK;
    }

    memcpy (GST_BUFFER_DATA (newbuf),
        GST_BUFFER_DATA (buf),
        MIN (GST_BUFFER_SIZE (newbuf), GST_BUFFER_SIZE (buf)));

    GST_DEBUG_OBJECT (pvrvideosink, "render copied buffer: %p", newbuf);

    buf = newbuf;
  }

  gst_pvrvideosink_blit (pvrvideosink, buf);

  if (newbuf) {
    gst_buffer_unref (newbuf);
  }

  return GST_FLOW_OK;
}


/* Buffer management
 *
 * The buffer_alloc function must either return a buffer with given size and
 * caps or create a buffer with different caps attached to the buffer. This
 * last option is called reverse negotiation, ie, where the sink suggests a
 * different format from the upstream peer. 
 *
 * We try to do reverse negotiation when our geometry changes and we like a
 * resized buffer.
 */
static GstFlowReturn
gst_pvrvideosink_buffer_alloc (GstBaseSink * bsink, guint64 offset, guint size,
    GstCaps * caps, GstBuffer ** buf)
{
  GstPVRVideoSink *pvrvideosink;
  GstDucatiBuffer *pvrvideo = NULL;
  GstStructure *structure = NULL;
  GstFlowReturn ret = GST_FLOW_OK;
  gint width, height;

  pvrvideosink = GST_PVRVIDEOSINK (bsink);

  GST_DEBUG_OBJECT (pvrvideosink, "begin");

  if (G_UNLIKELY (!caps)) {
    GST_WARNING_OBJECT (pvrvideosink,
        "have no caps, doing fallback allocation");
    *buf = NULL;
    ret = GST_FLOW_OK;
    goto beach;
  }

  g_mutex_lock (pvrvideosink->pool_lock);
  if (G_UNLIKELY (pvrvideosink->pool_invalid)) {
    GST_DEBUG_OBJECT (pvrvideosink, "the pool is flushing");
    ret = GST_FLOW_WRONG_STATE;
    g_mutex_unlock (pvrvideosink->pool_lock);
    goto beach;
  } else {
    g_mutex_unlock (pvrvideosink->pool_lock);
  }

  GST_LOG_OBJECT (pvrvideosink,
      "a buffer of %d bytes was requested with caps %" GST_PTR_FORMAT
      " and offset %" G_GUINT64_FORMAT, size, caps, offset);

  /* get struct to see what is requested */
  structure = gst_caps_get_structure (caps, 0);
  if (!gst_structure_get_int (structure, "width", &width) ||
      !gst_structure_get_int (structure, "height", &height)) {
    GST_WARNING_OBJECT (pvrvideosink, "invalid caps for buffer allocation %"
        GST_PTR_FORMAT, caps);
    ret = GST_FLOW_NOT_NEGOTIATED;
    goto beach;
  }

  g_mutex_lock (pvrvideosink->pool_lock);
  /* initialize the buffer pool if not initialized yet */
  if (G_UNLIKELY (!pvrvideosink->buffer_pool ||
          pvrvideosink->buffer_pool->size != size)) {
    if (pvrvideosink->buffer_pool) {
      GST_INFO_OBJECT (pvrvideosink, "in buffer alloc, pool->size != size");
      gst_pvr_bufferpool_stop_running (pvrvideosink->buffer_pool, FALSE);
    }

    GST_LOG_OBJECT (pvrvideosink, "Creating a buffer pool with %d buffers",
        pvrvideosink->num_buffers);
    if (!(pvrvideosink->buffer_pool =
            gst_pvr_bufferpool_new (GST_ELEMENT (pvrvideosink),
                caps, pvrvideosink->num_buffers, size,
                pvrvideosink->dcontext->pvr_context))) {
      g_mutex_unlock (pvrvideosink->pool_lock);
      return GST_FLOW_ERROR;
    }
  }
  pvrvideo = gst_pvr_bufferpool_get (pvrvideosink->buffer_pool, NULL);
  g_mutex_unlock (pvrvideosink->pool_lock);

  *buf = GST_BUFFER_CAST (pvrvideo);

beach:
  return ret;
}

/* Interfaces stuff */

static gboolean
gst_pvrvideosink_interface_supported (GstImplementsInterface * iface,
    GType type)
{
  if (type == GST_TYPE_X_OVERLAY)
    return TRUE;
  else
    return FALSE;
}

static void
gst_pvrvideosink_interface_init (GstImplementsInterfaceClass * klass)
{
  klass->supported = gst_pvrvideosink_interface_supported;
}

/* This function destroys a GstXWindow */
static void
gst_pvrvideosink_xwindow_destroy (GstPVRVideoSink * pvrvideosink,
    GstXWindow * xwindow)
{
  g_return_if_fail (xwindow != NULL);

  g_mutex_lock (pvrvideosink->dcontext->x_lock);

  /* If we did not create that window we just free the GC and let it live */
  if (xwindow->internal)
    XDestroyWindow (pvrvideosink->dcontext->x_display, xwindow->window);
  else
    XSelectInput (pvrvideosink->dcontext->x_display, xwindow->window, 0);

  XFreeGC (pvrvideosink->dcontext->x_display, xwindow->gc);

  XSync (pvrvideosink->dcontext->x_display, FALSE);

  g_mutex_unlock (pvrvideosink->dcontext->x_lock);

  g_free (xwindow);
}

static void
gst_pvrvideosink_set_window_handle (GstXOverlay * overlay, guintptr id)
{
  XID xwindow_id = id;
  GstPVRVideoSink *pvrvideosink = GST_PVRVIDEOSINK (overlay);
  GstXWindow *xwindow = NULL;

  g_return_if_fail (GST_IS_PVRVIDEOSINK (pvrvideosink));

  g_mutex_lock (pvrvideosink->flow_lock);

  /* If we already use that window return */
  if (pvrvideosink->xwindow && (xwindow_id == pvrvideosink->xwindow->window)) {
    g_mutex_unlock (pvrvideosink->flow_lock);
    return;
  }

  /* If the element has not initialized the X11 context try to do so */
  if (!pvrvideosink->dcontext && !(pvrvideosink->dcontext =
          gst_pvrvideosink_get_dcontext (pvrvideosink))) {
    g_mutex_unlock (pvrvideosink->flow_lock);
    /* we have thrown a GST_ELEMENT_ERROR now */
    return;
  }

  /* Clear image pool as the images are unusable anyway */
  g_mutex_lock (pvrvideosink->pool_lock);
  if (pvrvideosink->buffer_pool) {
    gst_pvr_bufferpool_stop_running (pvrvideosink->buffer_pool, FALSE);
    pvrvideosink->buffer_pool = NULL;
  }
  g_mutex_unlock (pvrvideosink->pool_lock);

  /* If a window is there already we destroy it */
  if (pvrvideosink->xwindow) {
    gst_pvrvideosink_xwindow_destroy (pvrvideosink, pvrvideosink->xwindow);
    pvrvideosink->xwindow = NULL;
  }

  /* If the xid is 0 we will create an internal one in buffer_alloc */
  if (xwindow_id != 0) {
    XWindowAttributes attr;
    WSEGLError glerror;
    WSEGLDrawableParams source_params;
    PVRSRV_CLIENT_MEM_INFO *client_mem_info;

    xwindow = g_new0 (GstXWindow, 1);
    xwindow->window = xwindow_id;

    /* Set the event we want to receive and create a GC */
    g_mutex_lock (pvrvideosink->dcontext->x_lock);

    XGetWindowAttributes (pvrvideosink->dcontext->x_display, xwindow->window,
        &attr);

    xwindow->width = attr.width;
    xwindow->height = attr.height;
    xwindow->internal = FALSE;
    if (!pvrvideosink->have_render_rect) {
      pvrvideosink->render_rect.x = pvrvideosink->render_rect.y = 0;
      pvrvideosink->render_rect.w = attr.width;
      pvrvideosink->render_rect.h = attr.height;
    }
    XSelectInput (pvrvideosink->dcontext->x_display, xwindow->window,
        ExposureMask | StructureNotifyMask);

    XSetWindowBackgroundPixmap (pvrvideosink->dcontext->x_display,
        xwindow->window, None);

    XMapWindow (pvrvideosink->dcontext->x_display, xwindow->window);
    xwindow->gc = XCreateGC (pvrvideosink->dcontext->x_display,
        xwindow->window, 0, NULL);
    g_mutex_unlock (pvrvideosink->dcontext->x_lock);

    glerror =
        pvrvideosink->dcontext->
        wsegl_table->pfnWSEGL_CreateWindowDrawable (pvrvideosink->dcontext->
        display_handle, pvrvideosink->dcontext->glconfig,
        &(pvrvideosink->dcontext->drawable_handle),
        (NativeWindowType) xwindow->window,
        &(pvrvideosink->dcontext->rotation));

    if (glerror != WSEGL_SUCCESS) {
      GST_ERROR_OBJECT (pvrvideosink, "Error creating drawable");
      return;
    }
    glerror =
        pvrvideosink->dcontext->
        wsegl_table->pfnWSEGL_GetDrawableParameters (pvrvideosink->dcontext->
        drawable_handle, &source_params, &pvrvideosink->render_params);

    client_mem_info =
        (PVRSRV_CLIENT_MEM_INFO *) pvrvideosink->render_params.hPrivateData;
    PVR2DMEMINFO_INITIALISE (&pvrvideosink->dcontext->dst_mem, client_mem_info);
  }

  if (xwindow)
    pvrvideosink->xwindow = xwindow;

  g_mutex_unlock (pvrvideosink->flow_lock);
}

static void
gst_pvrvideosink_expose (GstXOverlay * overlay)
{
  GstPVRVideoSink *pvrvideosink = GST_PVRVIDEOSINK (overlay);

  gst_pvrvideosink_blit (pvrvideosink, NULL);
}

static void
gst_pvrvideosink_set_event_handling (GstXOverlay * overlay,
    gboolean handle_events)
{
  GstPVRVideoSink *pvrvideosink = GST_PVRVIDEOSINK (overlay);

  g_mutex_lock (pvrvideosink->flow_lock);

  if (G_UNLIKELY (!pvrvideosink->xwindow)) {
    g_mutex_unlock (pvrvideosink->flow_lock);
    return;
  }

  g_mutex_lock (pvrvideosink->dcontext->x_lock);

  XSelectInput (pvrvideosink->dcontext->x_display,
      pvrvideosink->xwindow->window, ExposureMask | StructureNotifyMask);

  g_mutex_unlock (pvrvideosink->dcontext->x_lock);

  g_mutex_unlock (pvrvideosink->flow_lock);
}

static void
gst_pvrvideosink_set_render_rectangle (GstXOverlay * overlay, gint x, gint y,
    gint width, gint height)
{
  GstPVRVideoSink *pvrvideosink = GST_PVRVIDEOSINK (overlay);

  /* FIXME: how about some locking? */
  if (width >= 0 && height >= 0) {
    pvrvideosink->render_rect.x = x;
    pvrvideosink->render_rect.y = y;
    pvrvideosink->render_rect.w = width;
    pvrvideosink->render_rect.h = height;
    pvrvideosink->have_render_rect = TRUE;
  } else {
    pvrvideosink->render_rect.x = 0;
    pvrvideosink->render_rect.y = 0;
    pvrvideosink->render_rect.w = pvrvideosink->xwindow->width;
    pvrvideosink->render_rect.h = pvrvideosink->xwindow->height;
    pvrvideosink->have_render_rect = FALSE;
  }
}

static void
gst_pvrvideosink_xoverlay_init (GstXOverlayClass * iface)
{
  iface->set_window_handle = gst_pvrvideosink_set_window_handle;
  iface->expose = gst_pvrvideosink_expose;
  iface->handle_events = gst_pvrvideosink_set_event_handling;
  iface->set_render_rectangle = gst_pvrvideosink_set_render_rectangle;
}

/* =========================================== */
/*                                             */
/*              Init & Class init              */
/*                                             */
/* =========================================== */

static void
gst_pvrvideosink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstPVRVideoSink *pvrvideosink;

  g_return_if_fail (GST_IS_PVRVIDEOSINK (object));

  pvrvideosink = GST_PVRVIDEOSINK (object);

  switch (prop_id) {
    case PROP_FORCE_ASPECT_RATIO:
      pvrvideosink->keep_aspect = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_pvrvideosink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstPVRVideoSink *pvrvideosink;

  g_return_if_fail (GST_IS_PVRVIDEOSINK (object));

  pvrvideosink = GST_PVRVIDEOSINK (object);

  switch (prop_id) {
    case PROP_FORCE_ASPECT_RATIO:
      g_value_set_boolean (value, pvrvideosink->keep_aspect);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_pvrvideosink_dcontext_clear (GstPVRVideoSink * pvrvideosink)
{
  GstDrawContext *dcontext;

  GST_OBJECT_LOCK (pvrvideosink);
  if (!pvrvideosink->dcontext) {
    GST_OBJECT_UNLOCK (pvrvideosink);
    return;
  }

  dcontext = pvrvideosink->dcontext;
  pvrvideosink->dcontext = NULL;
  GST_OBJECT_UNLOCK (pvrvideosink);

  if (dcontext->p_blt_info)
    g_free (dcontext->p_blt_info);

  g_mutex_lock (dcontext->x_lock);
  XCloseDisplay (dcontext->x_display);
  g_mutex_unlock (dcontext->x_lock);
  g_mutex_free (dcontext->x_lock);

  g_free (dcontext);
}

static void
gst_pvrvideosink_reset (GstPVRVideoSink * pvrvideosink)
{
  GThread *thread;

  GST_OBJECT_LOCK (pvrvideosink);
  pvrvideosink->running = FALSE;
  thread = pvrvideosink->event_thread;
  pvrvideosink->event_thread = NULL;
  GST_OBJECT_UNLOCK (pvrvideosink);

  if (thread)
    g_thread_join (thread);

  if (pvrvideosink->current_buffer) {
    gst_buffer_unref (pvrvideosink->current_buffer);
    pvrvideosink->current_buffer = NULL;
  }

  g_mutex_lock (pvrvideosink->pool_lock);
  pvrvideosink->pool_invalid = TRUE;
  if (pvrvideosink->buffer_pool) {
    gst_pvr_bufferpool_stop_running (pvrvideosink->buffer_pool, TRUE);
    pvrvideosink->buffer_pool = NULL;
  }
  g_mutex_unlock (pvrvideosink->pool_lock);
  memset (&pvrvideosink->render_params, 0, sizeof (WSEGLDrawableParams));

  pvrvideosink->render_rect.x = pvrvideosink->render_rect.y = 0;
  pvrvideosink->render_rect.w = pvrvideosink->render_rect.h = 0;
  pvrvideosink->have_render_rect = FALSE;

  gst_pvrvideosink_destroy_drawable (pvrvideosink);

  if (pvrvideosink->xwindow) {
    gst_pvrvideosink_xwindow_destroy (pvrvideosink, pvrvideosink->xwindow);
    pvrvideosink->xwindow = NULL;
  }

  gst_pvrvideosink_dcontext_clear (pvrvideosink);
}

static void
gst_pvrvideosink_finalize (GObject * object)
{
  GstPVRVideoSink *pvrvideosink;

  pvrvideosink = GST_PVRVIDEOSINK (object);

  gst_pvrvideosink_reset (pvrvideosink);

  if (pvrvideosink->flow_lock) {
    g_mutex_free (pvrvideosink->flow_lock);
    pvrvideosink->flow_lock = NULL;
  }
  if (pvrvideosink->pool_lock) {
    g_mutex_free (pvrvideosink->pool_lock);
    pvrvideosink->pool_lock = NULL;
  }

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_pvrvideosink_init (GstPVRVideoSink * pvrvideosink)
{
  pvrvideosink->running = FALSE;

  pvrvideosink->fps_n = 0;
  pvrvideosink->fps_d = 1;

  pvrvideosink->flow_lock = g_mutex_new ();
  pvrvideosink->pool_lock = g_mutex_new ();
  pvrvideosink->buffer_pool = NULL;
  pvrvideosink->pool_invalid = TRUE;

  pvrvideosink->keep_aspect = FALSE;
  pvrvideosink->current_caps = NULL;
  pvrvideosink->num_buffers = DEFAULT_QUEUE_SIZE;
  pvrvideosink->num_buffers_can_change = TRUE;
  pvrvideosink->min_queued_bufs = DEFAULT_MIN_QUEUED_BUFS;
  pvrvideosink->dcontext = NULL;
  pvrvideosink->xwindow = NULL;
  pvrvideosink->redraw_borders = TRUE;
  pvrvideosink->current_buffer = NULL;
  pvrvideosink->event_thread = NULL;
  memset (&pvrvideosink->render_params, 0, sizeof (WSEGLDrawableParams));
}

static void
gst_pvrvideosink_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details_simple (element_class,
      "PVR Video sink", "Sink/Video",
      "A PVR videosink",
      "Luciana Fujii Pontello <luciana.fujii@collabora.co.uk");

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_pvrvideosink_sink_template_factory));
}

static void
gst_pvrvideosink_class_init (GstPVRVideoSinkClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseSinkClass *gstbasesink_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasesink_class = (GstBaseSinkClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->finalize = gst_pvrvideosink_finalize;
  gobject_class->set_property = gst_pvrvideosink_set_property;
  gobject_class->get_property = gst_pvrvideosink_get_property;

  g_object_class_install_property (gobject_class, PROP_FORCE_ASPECT_RATIO,
      g_param_spec_boolean ("force-aspect-ratio", "Force aspect ratio",
          "When enabled, reverse caps negotiation (scaling) will respect "
          "original aspect ratio", FALSE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gstelement_class->change_state = gst_pvrvideosink_change_state;

  gstbasesink_class->set_caps = GST_DEBUG_FUNCPTR (gst_pvrvideosink_setcaps);
  gstbasesink_class->get_caps = GST_DEBUG_FUNCPTR (gst_pvrvideosink_getcaps);
  gstbasesink_class->buffer_alloc =
      GST_DEBUG_FUNCPTR (gst_pvrvideosink_buffer_alloc);
  gstbasesink_class->get_times = GST_DEBUG_FUNCPTR (gst_pvrvideosink_get_times);

  gstbasesink_class->render = GST_DEBUG_FUNCPTR (gst_pvrvideosink_show_frame);
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
gst_pvrvideosink_get_type (void)
{
  static GType pvrvideosink_type = 0;

  if (!pvrvideosink_type) {
    static const GTypeInfo pvrvideosink_info = {
      sizeof (GstPVRVideoSinkClass),
      gst_pvrvideosink_base_init,
      NULL,
      (GClassInitFunc) gst_pvrvideosink_class_init,
      NULL,
      NULL,
      sizeof (GstPVRVideoSink), 0, (GInstanceInitFunc) gst_pvrvideosink_init,
    };
    static const GInterfaceInfo iface_info = {
      (GInterfaceInitFunc) gst_pvrvideosink_interface_init, NULL, NULL,
    };
    static const GInterfaceInfo overlay_info = {
      (GInterfaceInitFunc) gst_pvrvideosink_xoverlay_init, NULL, NULL,
    };

    pvrvideosink_type = g_type_register_static (GST_TYPE_VIDEO_SINK,
        "GstPVRVideoSink", &pvrvideosink_info, 0);

    g_type_add_interface_static (pvrvideosink_type,
        GST_TYPE_IMPLEMENTS_INTERFACE, &iface_info);
    g_type_add_interface_static (pvrvideosink_type, GST_TYPE_X_OVERLAY,
        &overlay_info);
  }

  return pvrvideosink_type;
}
