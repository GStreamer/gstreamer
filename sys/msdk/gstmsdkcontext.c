/* GStreamer Intel MSDK plugin
 * Copyright (c) 2018, Intel Corporation
 * Copyright (c) 2018, Igalia S.L.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGDECE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "gstmsdkcontext.h"
#ifndef _WIN32
#include <fcntl.h>
#include <unistd.h>
#include <va/va_drm.h>
#endif

GST_DEBUG_CATEGORY_STATIC (gst_debug_msdkcontext);
#define GST_CAT_DEFAULT gst_debug_msdkcontext

#define GST_MSDK_CONTEXT_GET_PRIVATE(obj) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((obj), GST_TYPE_MSDK_CONTEXT, \
      GstMsdkContextPrivate))

#define gst_msdk_context_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstMsdkContext, gst_msdk_context, GST_TYPE_OBJECT,
    GST_DEBUG_CATEGORY_INIT (gst_debug_msdkcontext, "msdkcontext", 0,
        "MSDK Context"));

struct _GstMsdkContextPrivate
{
  mfxSession session;
  GList *cached_alloc_responses;
  gboolean hardware;
  gboolean is_joined;
  GstMsdkContextJobType job_type;
#ifndef _WIN32
  gint fd;
  VADisplay dpy;
#endif
};

#ifndef _WIN32
static gboolean
gst_msdk_context_use_vaapi (GstMsdkContext * context)
{
  gint fd;
  gint maj_ver, min_ver;
  VADisplay va_dpy = NULL;
  VAStatus va_status;
  mfxStatus status;
  GstMsdkContextPrivate *priv = context->priv;

  /* maybe /dev/dri/renderD128 */
  static const gchar *dri_path = "/dev/dri/card0";

  fd = open (dri_path, O_RDWR);
  if (fd < 0) {
    GST_ERROR ("Couldn't open %s", dri_path);
    return FALSE;
  }

  va_dpy = vaGetDisplayDRM (fd);
  if (!va_dpy) {
    GST_ERROR ("Couldn't get a VA DRM display");
    goto failed;
  }

  va_status = vaInitialize (va_dpy, &maj_ver, &min_ver);
  if (va_status != VA_STATUS_SUCCESS) {
    GST_ERROR ("Couldn't initialize VA DRM display");
    goto failed;
  }

  status = MFXVideoCORE_SetHandle (priv->session, MFX_HANDLE_VA_DISPLAY,
      (mfxHDL) va_dpy);
  if (status != MFX_ERR_NONE) {
    GST_ERROR ("Setting VAAPI handle failed (%s)",
        msdk_status_to_string (status));
    goto failed;
  }

  priv->fd = fd;
  priv->dpy = va_dpy;

  return TRUE;

failed:
  if (va_dpy)
    vaTerminate (va_dpy);
  close (fd);
  return FALSE;
}
#endif

static gboolean
gst_msdk_context_open (GstMsdkContext * context, gboolean hardware,
    GstMsdkContextJobType job_type)
{
  GstMsdkContextPrivate *priv = context->priv;

  priv->job_type = job_type;
  priv->hardware = hardware;
  priv->session = msdk_open_session (hardware);
  if (!priv->session)
    goto failed;

#ifndef _WIN32
  priv->fd = -1;

  if (hardware) {
    if (!gst_msdk_context_use_vaapi (context))
      goto failed;
  }
#endif

  return TRUE;

failed:
  msdk_close_session (priv->session);
  gst_object_unref (context);
  return FALSE;
}

static void
gst_msdk_context_init (GstMsdkContext * context)
{
  GstMsdkContextPrivate *priv = GST_MSDK_CONTEXT_GET_PRIVATE (context);

  context->priv = priv;
}

static void
gst_msdk_context_finalize (GObject * obj)
{
  GstMsdkContext *context = GST_MSDK_CONTEXT_CAST (obj);
  GstMsdkContextPrivate *priv = context->priv;

  if (priv->is_joined) {
    MFXDisjoinSession (priv->session);
    goto done;
  }

  msdk_close_session (priv->session);

#ifndef _WIN32
  if (priv->dpy)
    vaTerminate (priv->dpy);
  if (priv->fd >= 0)
    close (priv->fd);
#endif

done:
  G_OBJECT_CLASS (parent_class)->finalize (obj);
}

static void
gst_msdk_context_class_init (GstMsdkContextClass * klass)
{
  GObjectClass *const g_object_class = G_OBJECT_CLASS (klass);
  g_type_class_add_private (klass, sizeof (GstMsdkContextPrivate));

  g_object_class->finalize = gst_msdk_context_finalize;
}

GstMsdkContext *
gst_msdk_context_new (gboolean hardware, GstMsdkContextJobType job_type)
{
  GstMsdkContext *obj = g_object_new (GST_TYPE_MSDK_CONTEXT, NULL);

  if (obj && !gst_msdk_context_open (obj, hardware, job_type)) {
    if (obj)
      gst_object_unref (obj);
    return NULL;
  }

  return obj;
}

GstMsdkContext *
gst_msdk_context_new_with_parent (GstMsdkContext * parent)
{
  mfxStatus status;
  GstMsdkContext *obj = g_object_new (GST_TYPE_MSDK_CONTEXT, NULL);
  GstMsdkContextPrivate *priv = obj->priv;
  GstMsdkContextPrivate *parent_priv = parent->priv;

  status = MFXCloneSession (parent_priv->session, &priv->session);
  if (status != MFX_ERR_NONE) {
    GST_ERROR ("Failed to clone mfx session");
    return NULL;
  }

  status = MFXJoinSession (parent_priv->session, priv->session);
  if (status != MFX_ERR_NONE) {
    GST_ERROR ("Failed to join mfx session");
    return NULL;
  }

  priv->is_joined = TRUE;
  priv->hardware = parent_priv->hardware;
  priv->job_type = parent_priv->job_type;
#ifndef _WIN32
  priv->dpy = parent_priv->dpy;
  priv->fd = parent_priv->fd;

  if (priv->hardware) {
    status = MFXVideoCORE_SetHandle (priv->session, MFX_HANDLE_VA_DISPLAY,
        (mfxHDL) parent_priv->dpy);
  }
#endif

  return obj;
}

mfxSession
gst_msdk_context_get_session (GstMsdkContext * context)
{
  return context->priv->session;
}

gpointer
gst_msdk_context_get_handle (GstMsdkContext * context)
{
#ifndef _WIN32
  return context->priv->dpy;
#else
  return NULL;
#endif
}

gint
gst_msdk_context_get_fd (GstMsdkContext * context)
{
#ifndef _WIN32
  return context->priv->fd;
#else
  return -1;
#endif
}

static gint
_find_response (gconstpointer resp, gconstpointer comp_resp)
{
  GstMsdkAllocResponse *cached_resp = (GstMsdkAllocResponse *) resp;
  mfxFrameAllocResponse *_resp = (mfxFrameAllocResponse *) comp_resp;

  return cached_resp ? cached_resp->mem_ids != _resp->mids : -1;
}

static gint
_find_request (gconstpointer resp, gconstpointer req)
{
  GstMsdkAllocResponse *cached_resp = (GstMsdkAllocResponse *) resp;
  mfxFrameAllocRequest *_req = (mfxFrameAllocRequest *) req;

  return cached_resp ? cached_resp->request.Type != _req->Type : -1;
}

GstMsdkAllocResponse *
gst_msdk_context_get_cached_alloc_responses (GstMsdkContext * context,
    mfxFrameAllocResponse * resp)
{
  GstMsdkContextPrivate *priv = context->priv;
  GList *l =
      g_list_find_custom (priv->cached_alloc_responses, resp, _find_response);

  if (l)
    return l->data;
  else
    return NULL;
}

GstMsdkAllocResponse *
gst_msdk_context_get_cached_alloc_responses_by_request (GstMsdkContext *
    context, mfxFrameAllocRequest * req)
{
  GstMsdkContextPrivate *priv = context->priv;
  GList *l =
      g_list_find_custom (priv->cached_alloc_responses, req, _find_request);

  if (l)
    return l->data;
  else
    return NULL;
}

void
gst_msdk_context_add_alloc_response (GstMsdkContext * context,
    GstMsdkAllocResponse * resp)
{
  context->priv->cached_alloc_responses =
      g_list_append (context->priv->cached_alloc_responses, resp);
}

gboolean
gst_msdk_context_remove_alloc_response (GstMsdkContext * context,
    mfxFrameAllocResponse * resp)
{
  GstMsdkAllocResponse *msdk_resp;
  GstMsdkContextPrivate *priv = context->priv;
  GList *l =
      g_list_find_custom (priv->cached_alloc_responses, resp, _find_response);

  if (!l)
    return FALSE;

  msdk_resp = l->data;

  g_slice_free1 (sizeof (GstMsdkAllocResponse), msdk_resp);
  priv->cached_alloc_responses =
      g_list_delete_link (priv->cached_alloc_responses, l);

  return TRUE;
}

GstMsdkContextJobType
gst_msdk_context_get_job_type (GstMsdkContext * context)
{
  return context->priv->job_type;
}

void
gst_msdk_context_add_job_type (GstMsdkContext * context,
    GstMsdkContextJobType job_type)
{
  context->priv->job_type |= job_type;
}
