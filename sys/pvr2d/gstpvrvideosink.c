/* GStreamer
 *
 * Copyright (C) 2011 Collabora Ltda
 * Copyright (C) 2011 Texas Instruments
 *  @author: Luciana Fujii Pontello <luciana.fujii@collabora.co.uk>
 *  @author: Edward Hervey <edward@collabora.com>
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

/* Object header */
#include "gstpvrvideosink.h"
#include "gstpvrbufferpool.h"

#include <gst/video/gstvideosink.h>
#include <gst/video/videooverlay.h>
/* Helper functions */
#include <gst/video/gstvideometa.h>

/* Debugging category */
#include <gst/gstinfo.h>

#define LINUX
#include <dri2_ws.h>
#include <services.h>
#include <img_defs.h>
#include <servicesext.h>

#define DEFAULT_QUEUE_SIZE 12

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
static void gst_pvrvideosink_xwindow_draw_borders (GstPVRVideoSink *
    pvrvideosink, GstXWindow * xwindow, GstVideoRectangle rect);
static void gst_pvrvideosink_expose (GstVideoOverlay * overlay);
static void gst_pvrvideosink_xwindow_destroy (GstPVRVideoSink * pvrvideosink,
    GstXWindow * xwindow);
static void gst_pvrvideosink_dcontext_free (GstDrawContext * dcontext);

static void gst_pvrvideosink_videooverlay_init (GstVideoOverlayInterface *
    iface);

#define gst_pvrvideosink_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstPVRVideoSink, gst_pvrvideosink, GST_TYPE_VIDEO_SINK,
    G_IMPLEMENT_INTERFACE (GST_TYPE_VIDEO_OVERLAY,
        gst_pvrvideosink_videooverlay_init));


static GstStaticPadTemplate gst_pvrvideosink_sink_template_factory =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE ("NV12")));

enum
{
  PROP_0,
  PROP_FORCE_ASPECT_RATIO,
  PROP_WINDOW_WIDTH,
  PROP_WINDOW_HEIGHT
};

/* ============================================================= */
/*                                                               */
/*                       Private Methods                         */
/*                                                               */
/* ============================================================= */

/* pvrvideo buffers */

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
        pvrvideosink->dcontext->wsegl_table->
        pfnWSEGL_DeleteDrawable (pvrvideosink->dcontext->drawable_handle);
    if (glerror != WSEGL_SUCCESS) {
      GST_ERROR_OBJECT (pvrvideosink, "Error destroying drawable");
      return;
    }
    glerror =
        pvrvideosink->dcontext->wsegl_table->
        pfnWSEGL_CreateWindowDrawable (pvrvideosink->dcontext->display_handle,
        pvrvideosink->dcontext->glconfig,
        &pvrvideosink->dcontext->drawable_handle,
        (NativeWindowType) pvrvideosink->xwindow->window,
        &pvrvideosink->dcontext->rotation);
    if (glerror != WSEGL_SUCCESS) {
      GST_ERROR_OBJECT (pvrvideosink, "Error creating drawable");
      return;
    }
    glerror =
        pvrvideosink->dcontext->wsegl_table->
        pfnWSEGL_GetDrawableParameters (pvrvideosink->dcontext->drawable_handle,
        &source_params, &pvrvideosink->render_params);
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

    gst_pvrvideosink_expose (GST_VIDEO_OVERLAY (pvrvideosink));

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
  GstDrawContext *dcontext = NULL;
  PVR2DERROR pvr_error;
  gint refresh_rate;
  DRI2WSDisplay *displayImpl;
  WSEGLError glerror;
  const WSEGLCaps *glcaps;

  GST_DEBUG_OBJECT (pvrvideosink, "Getting draw context");

  dcontext = g_new0 (GstDrawContext, 1);
  dcontext->x_lock = g_mutex_new ();

  dcontext->p_blt_info = g_new0 (PVR2D_3DBLT_EXT, 1);
  if (!dcontext->p_blt_info)
    goto p_blt_info_alloc_failed;

  dcontext->p_blt2d_info = g_new0 (PVR2DBLTINFO, 1);

  GST_LOG_OBJECT (pvrvideosink, "Opening X Display");
  dcontext->x_display = XOpenDisplay (NULL);

  if (dcontext->x_display == NULL)
    goto fail_open_display;

  GST_LOG_OBJECT (pvrvideosink, "WSEGL_GetFunctionTablePointer()");
  dcontext->wsegl_table = WSEGL_GetFunctionTablePointer ();

  GST_LOG_OBJECT (pvrvideosink, "pfnWSEGL_IsDisplayValid()");
  glerror = dcontext->wsegl_table->pfnWSEGL_IsDisplayValid (
      (NativeDisplayType) dcontext->x_display);

  if (glerror != WSEGL_SUCCESS)
    goto display_invalid;

  GST_LOG_OBJECT (pvrvideosink, "pfnWSEGL_InitialiseDisplay()");

  glerror = dcontext->wsegl_table->pfnWSEGL_InitialiseDisplay (
      (NativeDisplayType) dcontext->x_display, &dcontext->display_handle,
      &glcaps, &dcontext->glconfig);
  if (glerror != WSEGL_SUCCESS)
    goto display_init_failed;

  displayImpl = (DRI2WSDisplay *) dcontext->display_handle;
  dcontext->pvr_context = displayImpl->hContext;

  GST_LOG_OBJECT (pvrvideosink, "PVR2DGetScreenMode()");

  pvr_error = PVR2DGetScreenMode (dcontext->pvr_context,
      &dcontext->display_format, &dcontext->display_width,
      &dcontext->display_height, &dcontext->stride, &refresh_rate);
  if (pvr_error != PVR2D_OK)
    goto screen_mode_failed;

  GST_DEBUG_OBJECT (pvrvideosink,
      "Got format:%d, width:%d, height:%d, stride:%d, refresh_rate:%d",
      dcontext->display_format, dcontext->display_width,
      dcontext->display_height, dcontext->stride, refresh_rate);

  dcontext->screen_num = DefaultScreen (dcontext->x_display);
  dcontext->black = XBlackPixel (dcontext->x_display, dcontext->screen_num);

  GST_DEBUG_OBJECT (pvrvideosink, "Returning dcontext %p", dcontext);

  return dcontext;

p_blt_info_alloc_failed:
  {
    GST_ERROR_OBJECT (pvrvideosink, "Alloc of bltinfo failed");
    gst_pvrvideosink_dcontext_free (dcontext);
    return NULL;
  }

fail_open_display:
  {
    GST_ERROR_OBJECT (pvrvideosink, "Failed to open X Display");
    gst_pvrvideosink_dcontext_free (dcontext);
    return NULL;
  }

display_invalid:
  {
    GST_ERROR_OBJECT (pvrvideosink, "Display is not valid (glerror:%d)",
        glerror);
    gst_pvrvideosink_dcontext_free (dcontext);
    return NULL;
  }

display_init_failed:
  {
    GST_ERROR_OBJECT (pvrvideosink, "Error initializing display (glerror:%d)",
        glerror);
    gst_pvrvideosink_dcontext_free (dcontext);
    return NULL;
  }

screen_mode_failed:
  {
    GST_ERROR_OBJECT (pvrvideosink, "Failed to get screen mode. error : %s",
        gst_pvr2d_error_get_string (pvr_error));
    gst_pvrvideosink_dcontext_free (dcontext);
    return NULL;
  }
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
      dcontext->wsegl_table->
      pfnWSEGL_CreateWindowDrawable (dcontext->display_handle,
      dcontext->glconfig, &(dcontext->drawable_handle),
      (NativeWindowType) xwindow->window, &(dcontext->rotation));

  if (glerror != WSEGL_SUCCESS) {
    GST_ERROR_OBJECT (pvrvideosink, "Error creating drawable");
    return NULL;
  }
  glerror =
      dcontext->wsegl_table->
      pfnWSEGL_GetDrawableParameters (dcontext->drawable_handle, &source_params,
      &pvrvideosink->render_params);
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
  gint video_width;
  gint video_height;
  gboolean draw_border = FALSE;
  PPVR2D_3DBLT_EXT p_blt_3d;
  PVR2DMEMINFO *src_mem;
  PVR2DFORMAT pvr_format;
  GstVideoRectangle result;
  GstPVRMeta *meta;
  GstVideoCropMeta *cropmeta;

  GST_DEBUG_OBJECT (pvrvideosink, "buffer %p", buffer);

  pvr_format =
      GST_VIDEO_INFO_FORMAT (&pvrvideosink->info) ==
      GST_VIDEO_FORMAT_NV12 ? PVR2D_YUV420_2PLANE : PVR2D_ARGB8888;

  g_mutex_lock (pvrvideosink->flow_lock);
  if (buffer == NULL)
    buffer = pvrvideosink->current_buffer;

  if (buffer == NULL)
    goto done;

  meta = gst_buffer_get_pvr_meta (buffer);
  if (G_UNLIKELY (meta == NULL))
    goto no_pvr_meta;

  src_mem = meta->src_mem;
  p_blt_3d = dcontext->p_blt_info;

  video_width = GST_VIDEO_SINK_WIDTH (pvrvideosink);
  video_height = GST_VIDEO_SINK_HEIGHT (pvrvideosink);

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
  p_blt_3d->sDst.Stride = 4 * pvrvideosink->render_params.ui32Stride;
  p_blt_3d->sDst.Format = PVR2D_ARGB8888;
  p_blt_3d->sDst.SurfWidth = pvrvideosink->xwindow->width;
  p_blt_3d->sDst.SurfHeight = pvrvideosink->xwindow->height;

  p_blt_3d->rcDest.left = result.x;
  p_blt_3d->rcDest.top = result.y;
  p_blt_3d->rcDest.right = result.w + result.x;
  p_blt_3d->rcDest.bottom = result.h + result.y;

  p_blt_3d->sSrc.pSurfMemInfo = src_mem;
  p_blt_3d->sSrc.SurfOffset = 0;
  p_blt_3d->sSrc.Stride = GST_VIDEO_INFO_COMP_STRIDE (&pvrvideosink->info, 0);
  p_blt_3d->sSrc.Format = pvr_format;
  p_blt_3d->sSrc.SurfWidth = video_width;
  p_blt_3d->sSrc.SurfHeight = video_height;

  /* If buffer has crop information, use that */
  if ((cropmeta = gst_buffer_get_video_crop_meta (buffer))) {
    p_blt_3d->rcSource.left = cropmeta->x;
    p_blt_3d->rcSource.top = cropmeta->y;
    p_blt_3d->rcSource.right = cropmeta->x + cropmeta->width;
    p_blt_3d->rcSource.bottom = cropmeta->y + cropmeta->height;
  } else {
    p_blt_3d->rcSource.left = 0;
    p_blt_3d->rcSource.top = 0;
    p_blt_3d->rcSource.right = video_width;
    p_blt_3d->rcSource.bottom = video_height;
  }

  p_blt_3d->hUseCode = NULL;

  if (GST_VIDEO_INFO_FORMAT (&pvrvideosink->info) == GST_VIDEO_FORMAT_NV12)
    p_blt_3d->bDisableDestInput = TRUE;
  else
    /* blit fails for RGB without this... not sure why yet... */
    p_blt_3d->bDisableDestInput = FALSE;

  GST_DEBUG_OBJECT (pvrvideosink, "about to blit");

  pvr_error = PVR2DBlt3DExt (pvrvideosink->dcontext->pvr_context,
      dcontext->p_blt_info);

  if (pvr_error != PVR2D_OK) {
    GST_ERROR_OBJECT (pvrvideosink, "Failed to blit. Error : %s",
        gst_pvr2d_error_get_string (pvr_error));
    goto done;
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
  return;

  /* Error cases */

no_pvr_meta:
  {
    g_mutex_unlock (pvrvideosink->flow_lock);
    GST_ERROR_OBJECT (pvrvideosink, "Got a buffer without GstPVRMeta");
    return;
  }
}

static void
gst_pvrvideosink_destroy_drawable (GstPVRVideoSink * pvrvideosink)
{
  GST_DEBUG_OBJECT (pvrvideosink, "dcontext : %p", pvrvideosink->dcontext);

  if (pvrvideosink->dcontext != NULL) {
    if (pvrvideosink->dcontext->drawable_handle) {
      GST_DEBUG_OBJECT (pvrvideosink, "Deleting Drawable (drawable_handle:%p)",
          pvrvideosink->dcontext->drawable_handle);
      pvrvideosink->dcontext->wsegl_table->
          pfnWSEGL_DeleteDrawable (pvrvideosink->dcontext->drawable_handle);
    }

    GST_DEBUG_OBJECT (pvrvideosink, "Closing display (display_handle:%p)",
        pvrvideosink->dcontext->display_handle);
    pvrvideosink->dcontext->wsegl_table->
        pfnWSEGL_CloseDisplay (pvrvideosink->dcontext->display_handle);
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
  p_blt2d_info->DstStride = 4 * pvrvideosink->render_params.ui32Stride;
  p_blt2d_info->DstFormat = PVR2D_ARGB8888;
  p_blt2d_info->DstSurfWidth = pvrvideosink->xwindow->width;
  p_blt2d_info->DstSurfHeight = pvrvideosink->xwindow->height;
  p_blt2d_info->DstX = rect.x;
  p_blt2d_info->DstY = rect.y;
  p_blt2d_info->DSizeX = rect.w;
  p_blt2d_info->DSizeY = rect.h;

  pvr_error = PVR2DBlt (pvrvideosink->dcontext->pvr_context, p_blt2d_info);

  if (pvr_error != PVR2D_OK) {
    GST_ERROR_OBJECT (pvrvideosink, "Failed to blit. Error : %s",
        gst_pvr2d_error_get_string (pvr_error));
    goto done;
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
  GstVideoInfo info;
  GstStructure *structure;
  GstBufferPool *oldpool, *newpool;

  pvrvideosink = GST_PVRVIDEOSINK (bsink);

  GST_DEBUG_OBJECT (pvrvideosink,
      "sinkconnect possible caps with given caps %" GST_PTR_FORMAT, caps);

  if (!gst_video_info_from_caps (&info, caps))
    goto invalid_format;

  GST_VIDEO_SINK_WIDTH (pvrvideosink) = info.width;
  GST_VIDEO_SINK_HEIGHT (pvrvideosink) = info.height;

  /* Notify application to set xwindow id now */
  g_mutex_lock (pvrvideosink->flow_lock);
  if (!pvrvideosink->xwindow) {
    g_mutex_unlock (pvrvideosink->flow_lock);
    gst_video_overlay_prepare_window_handle (GST_VIDEO_OVERLAY (pvrvideosink));
  } else {
    g_mutex_unlock (pvrvideosink->flow_lock);
  }

  g_mutex_lock (pvrvideosink->flow_lock);
  if (!pvrvideosink->xwindow)
    pvrvideosink->xwindow = gst_pvrvideosink_create_window (pvrvideosink,
        GST_VIDEO_SINK_WIDTH (pvrvideosink),
        GST_VIDEO_SINK_HEIGHT (pvrvideosink));
  g_mutex_unlock (pvrvideosink->flow_lock);

  pvrvideosink->info = info;

  /* After a resize, we want to redraw the borders in case the new frame size
   * doesn't cover the same area */
  pvrvideosink->redraw_borders = TRUE;

  /* create a new pool for the new configuration */
  newpool = gst_pvr_buffer_pool_new (GST_ELEMENT_CAST (pvrvideosink));

  /* PVR needs at least 3 buffers */
  structure = gst_buffer_pool_get_config (newpool);
  gst_buffer_pool_config_set (structure, caps, GST_VIDEO_INFO_SIZE (&info), 3,
      0, 0, 15);
  if (!gst_buffer_pool_set_config (newpool, structure))
    goto config_failed;

  oldpool = pvrvideosink->pool;
  pvrvideosink->pool = newpool;
  g_mutex_unlock (pvrvideosink->flow_lock);

  /* unref the old sink */
  if (oldpool) {
    /* we don't deactivate, some elements might still be using it, it will
     * be deactivated when the last ref is gone */
    gst_object_unref (oldpool);
  }

  return TRUE;

config_failed:
  {
    GST_ERROR_OBJECT (pvrvideosink, "failed to set config.");
    g_mutex_unlock (pvrvideosink->flow_lock);
    return FALSE;
  }

invalid_format:
  {
    GST_DEBUG_OBJECT (pvrvideosink,
        "Could not locate image format from caps %" GST_PTR_FORMAT, caps);
    return FALSE;
  }
}

static GstCaps *
gst_pvrvideosink_getcaps (GstBaseSink * bsink, GstCaps * filter)
{
  GstPVRVideoSink *pvrvideosink;
  GstCaps *caps;

  GST_DEBUG_OBJECT (bsink, "filter:%" GST_PTR_FORMAT, filter);

  pvrvideosink = GST_PVRVIDEOSINK (bsink);

  /* FIXME : If we have curently configured caps, we should return those
   * intersected with the filter*/

  caps = gst_pad_get_pad_template_caps (GST_BASE_SINK (pvrvideosink)->sinkpad);
  if (filter) {
    GstCaps *intersection;

    intersection =
        gst_caps_intersect_full (filter, caps, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (caps);
    caps = intersection;
  }

  GST_DEBUG_OBJECT (bsink, "Returning %" GST_PTR_FORMAT, caps);

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
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
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
      gint fps_n, fps_d;
      fps_n = GST_VIDEO_INFO_FPS_N (&pvrvideosink->info);
      fps_d = GST_VIDEO_INFO_FPS_D (&pvrvideosink->info);
      if (fps_n > 0) {
        *end = *start + gst_util_uint64_scale_int (GST_SECOND, fps_d, fps_n);
      }
    }
  }
}

static GstFlowReturn
gst_pvrvideosink_show_frame (GstVideoSink * vsink, GstBuffer * buf)
{
  GstPVRVideoSink *pvrvideosink = GST_PVRVIDEOSINK (vsink);
  GstPVRMeta *meta;

  GST_DEBUG_OBJECT (pvrvideosink, "render buffer: %p", buf);

  meta = gst_buffer_get_pvr_meta (buf);

  if (G_UNLIKELY (meta == NULL)) {
    meta = gst_buffer_add_pvr_meta (buf, GST_ELEMENT_CAST (pvrvideosink));
    if (meta == NULL)
      goto meta_failure;
  }

  gst_pvrvideosink_blit (pvrvideosink, buf);

  return GST_FLOW_OK;

meta_failure:
  {
    GST_WARNING_OBJECT (pvrvideosink, "Failed to map incoming buffer");
    return GST_FLOW_ERROR;
  }
}

static gboolean
gst_pvrvideosink_propose_allocation (GstBaseSink * bsink, GstQuery * query)
{
  GstPVRVideoSink *pvrvideosink = (GstPVRVideoSink *) bsink;
  GstBufferPool *pool;
  GstStructure *config;
  GstCaps *caps;
  guint size;
  gboolean need_pool;

  gst_query_parse_allocation (query, &caps, &need_pool);

  if (caps == NULL)
    goto no_caps;

  g_mutex_lock (pvrvideosink->flow_lock);
  if ((pool = pvrvideosink->pool))
    gst_object_ref (pool);
  g_mutex_unlock (pvrvideosink->flow_lock);

  if (pool != NULL) {
    const GstCaps *pcaps;

    /* we had a pool, check caps */
    GST_DEBUG_OBJECT (pvrvideosink, "check existing pool caps");
    config = gst_buffer_pool_get_config (pool);
    gst_buffer_pool_config_get (config, &pcaps, &size, NULL, NULL, NULL, NULL);
    gst_structure_free (config);

    if (!gst_caps_is_equal (caps, pcaps)) {
      GST_DEBUG_OBJECT (pvrvideosink, "pool has different caps");
      /* different caps, we can't use this pool */
      gst_object_unref (pool);
      pool = NULL;
    }
  }

  if (pool == NULL && need_pool) {
    GstVideoInfo info;

    GST_DEBUG_OBJECT (pvrvideosink, "create new pool");
    pool = gst_pvr_buffer_pool_new (GST_ELEMENT_CAST (pvrvideosink));

    if (!gst_video_info_from_caps (&info, caps))
      goto invalid_caps;

    /* the normal size of a frame */
    size = info.size;

    config = gst_buffer_pool_get_config (pool);
    gst_buffer_pool_config_set (config, caps, size, 0, 0, 0, 0);
    if (!gst_buffer_pool_set_config (pool, config))
      goto config_failed;
  }
  /* we need at least 3 buffers */
  gst_query_set_allocation_params (query, size, 3, 0, 0, 0, pool);

  /* we also support various metadata */
  gst_query_add_allocation_meta (query, GST_VIDEO_CROP_META_API, NULL);

  gst_object_unref (pool);

  return TRUE;

  /* ERRORS */
no_caps:
  {
    GST_DEBUG_OBJECT (bsink, "no caps specified");
    return FALSE;
  }
invalid_caps:
  {
    GST_DEBUG_OBJECT (bsink, "invalid caps specified");
    return FALSE;
  }
config_failed:
  {
    GST_DEBUG_OBJECT (bsink, "failed setting config");
    return FALSE;
  }
}

/* Interfaces stuff */

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
gst_pvrvideosink_set_window_handle (GstVideoOverlay * overlay, guintptr id)
{
  XID window_handle = id;
  GstPVRVideoSink *pvrvideosink = GST_PVRVIDEOSINK (overlay);
  GstXWindow *xwindow = NULL;

  g_return_if_fail (GST_IS_PVRVIDEOSINK (pvrvideosink));

  g_mutex_lock (pvrvideosink->flow_lock);

  /* If we already use that window return */
  if (pvrvideosink->xwindow && (window_handle == pvrvideosink->xwindow->window)) {
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

  /* If a window is there already we destroy it */
  if (pvrvideosink->xwindow) {
    gst_pvrvideosink_xwindow_destroy (pvrvideosink, pvrvideosink->xwindow);
    pvrvideosink->xwindow = NULL;
  }

  /* If the xid is 0 we will create an internal one in buffer_alloc */
  if (window_handle != 0) {
    XWindowAttributes attr;
    WSEGLError glerror;
    WSEGLDrawableParams source_params;
    PVRSRV_CLIENT_MEM_INFO *client_mem_info;

    xwindow = g_new0 (GstXWindow, 1);
    xwindow->window = window_handle;

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
        pvrvideosink->dcontext->wsegl_table->
        pfnWSEGL_CreateWindowDrawable (pvrvideosink->dcontext->display_handle,
        pvrvideosink->dcontext->glconfig,
        &(pvrvideosink->dcontext->drawable_handle),
        (NativeWindowType) xwindow->window,
        &(pvrvideosink->dcontext->rotation));

    if (glerror != WSEGL_SUCCESS) {
      GST_ERROR_OBJECT (pvrvideosink, "Error creating drawable");
      return;
    }
    glerror =
        pvrvideosink->dcontext->wsegl_table->
        pfnWSEGL_GetDrawableParameters (pvrvideosink->dcontext->drawable_handle,
        &source_params, &pvrvideosink->render_params);

    client_mem_info =
        (PVRSRV_CLIENT_MEM_INFO *) pvrvideosink->render_params.hPrivateData;
    PVR2DMEMINFO_INITIALISE (&pvrvideosink->dcontext->dst_mem, client_mem_info);
  }

  if (xwindow)
    pvrvideosink->xwindow = xwindow;

  g_mutex_unlock (pvrvideosink->flow_lock);
}

static void
gst_pvrvideosink_expose (GstVideoOverlay * overlay)
{
  GstPVRVideoSink *pvrvideosink = GST_PVRVIDEOSINK (overlay);

  gst_pvrvideosink_blit (pvrvideosink, NULL);
}

static void
gst_pvrvideosink_set_event_handling (GstVideoOverlay * overlay,
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
gst_pvrvideosink_set_render_rectangle (GstVideoOverlay * overlay, gint x,
    gint y, gint width, gint height)
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
gst_pvrvideosink_videooverlay_init (GstVideoOverlayInterface * iface)
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

void
gst_pvrvideosink_track_buffer (GstPVRVideoSink * pvrsink, GstBuffer * buffer)
{
  GST_DEBUG_OBJECT (pvrsink, "Adding buffer %p to tracked buffers", buffer);
  pvrsink->metabuffers = g_list_prepend (pvrsink->metabuffers, buffer);
}

void
gst_pvrvideosink_untrack_buffer (GstPVRVideoSink * pvrsink, GstBuffer * buffer)
{
  GST_DEBUG_OBJECT (pvrsink, "Removing buffer %p from tracked buffers", buffer);
  pvrsink->metabuffers = g_list_remove_all (pvrsink->metabuffers, buffer);
}

static void
gst_pvrvideosink_release_pvr_metas (GstPVRVideoSink * pvrsink)
{
  GstBuffer *buf;
  GstPVRMeta *pvrmeta;

  GST_DEBUG_OBJECT (pvrsink, "Releasing pending PVR metas");

  while (pvrsink->metabuffers) {
    buf = (GstBuffer *) pvrsink->metabuffers->data;

    pvrmeta = gst_buffer_get_pvr_meta (buf);
    if (pvrmeta)
      gst_buffer_remove_meta (buf, (GstMeta *) pvrmeta);
  }

  GST_DEBUG_OBJECT (pvrsink, "Done");
}

static void
gst_pvrvideosink_dcontext_free (GstDrawContext * dcontext)
{
  GST_DEBUG ("Freeing dcontext %p", dcontext);

  if (dcontext->p_blt_info)
    g_free (dcontext->p_blt_info);

  if (dcontext->p_blt2d_info)
    g_free (dcontext->p_blt2d_info);

  if (dcontext->x_lock)
    g_mutex_lock (dcontext->x_lock);
  if (dcontext->x_display) {
    GST_LOG ("Closing display");
    XCloseDisplay (dcontext->x_display);
  }
  if (dcontext->x_lock) {
    g_mutex_unlock (dcontext->x_lock);
    g_mutex_free (dcontext->x_lock);
  }

  g_free (dcontext);
}

static void
gst_pvrvideosink_dcontext_clear (GstPVRVideoSink * pvrvideosink)
{
  GstDrawContext *dcontext;

  GST_DEBUG_OBJECT (pvrvideosink, "Clearing dcontext %p",
      pvrvideosink->dcontext);

  GST_OBJECT_LOCK (pvrvideosink);
  if (!pvrvideosink->dcontext) {
    GST_OBJECT_UNLOCK (pvrvideosink);
    return;
  }

  dcontext = pvrvideosink->dcontext;
  pvrvideosink->dcontext = NULL;
  GST_OBJECT_UNLOCK (pvrvideosink);

  gst_pvrvideosink_dcontext_free (dcontext);
}

static void
gst_pvrvideosink_reset (GstPVRVideoSink * pvrvideosink)
{
  GThread *thread;

  GST_DEBUG_OBJECT (pvrvideosink, "Resetting");

  GST_OBJECT_LOCK (pvrvideosink);
  pvrvideosink->running = FALSE;
  thread = pvrvideosink->event_thread;
  pvrvideosink->event_thread = NULL;
  GST_OBJECT_UNLOCK (pvrvideosink);

  if (thread)
    g_thread_join (thread);

  if (pvrvideosink->current_buffer) {
    GST_LOG_OBJECT (pvrvideosink, "Removing cached buffer");
    gst_buffer_unref (pvrvideosink->current_buffer);
    pvrvideosink->current_buffer = NULL;
  }

  if (pvrvideosink->pool) {
    GST_LOG_OBJECT (pvrvideosink, "Unreffing pool");
    gst_object_unref (pvrvideosink->pool);
    pvrvideosink->pool = NULL;
  }
  memset (&pvrvideosink->render_params, 0, sizeof (WSEGLDrawableParams));

  pvrvideosink->render_rect.x = pvrvideosink->render_rect.y = 0;
  pvrvideosink->render_rect.w = pvrvideosink->render_rect.h = 0;
  pvrvideosink->have_render_rect = FALSE;

  gst_pvrvideosink_release_pvr_metas (pvrvideosink);

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

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_pvrvideosink_init (GstPVRVideoSink * pvrvideosink)
{
  pvrvideosink->running = FALSE;

  pvrvideosink->flow_lock = g_mutex_new ();
  pvrvideosink->pool = NULL;

  pvrvideosink->keep_aspect = FALSE;
  pvrvideosink->current_caps = NULL;
  pvrvideosink->dcontext = NULL;
  pvrvideosink->xwindow = NULL;
  pvrvideosink->redraw_borders = TRUE;
  pvrvideosink->current_buffer = NULL;
  pvrvideosink->event_thread = NULL;
  memset (&pvrvideosink->render_params, 0, sizeof (WSEGLDrawableParams));
}

static void
gst_pvrvideosink_class_init (GstPVRVideoSinkClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseSinkClass *gstbasesink_class;
  GstVideoSinkClass *videosink_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasesink_class = (GstBaseSinkClass *) klass;
  videosink_class = (GstVideoSinkClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->finalize = gst_pvrvideosink_finalize;
  gobject_class->set_property = gst_pvrvideosink_set_property;
  gobject_class->get_property = gst_pvrvideosink_get_property;

  g_object_class_install_property (gobject_class, PROP_FORCE_ASPECT_RATIO,
      g_param_spec_boolean ("force-aspect-ratio", "Force aspect ratio",
          "When enabled, reverse caps negotiation (scaling) will respect "
          "original aspect ratio", TRUE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_set_static_metadata (gstelement_class,
      "PVR Video sink", "Sink/Video",
      "A PVR videosink",
      "Luciana Fujii Pontello <luciana.fujii@collabora.co.uk");

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_pvrvideosink_sink_template_factory));

  gstelement_class->change_state = gst_pvrvideosink_change_state;

  gstbasesink_class->set_caps = GST_DEBUG_FUNCPTR (gst_pvrvideosink_setcaps);
  gstbasesink_class->get_caps = GST_DEBUG_FUNCPTR (gst_pvrvideosink_getcaps);
  gstbasesink_class->propose_allocation =
      GST_DEBUG_FUNCPTR (gst_pvrvideosink_propose_allocation);
  gstbasesink_class->get_times = GST_DEBUG_FUNCPTR (gst_pvrvideosink_get_times);

  videosink_class->show_frame = GST_DEBUG_FUNCPTR (gst_pvrvideosink_show_frame);
}
