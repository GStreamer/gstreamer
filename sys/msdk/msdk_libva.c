/* GStreamer Intel MSDK plugin
 * Copyright (c) 2016, Oblong Industries, Inc.
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
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */


/* TODO:
 *   - discover dri_path instead of having it hardcoded
 */

#include <fcntl.h>
#include <unistd.h>

#include <va/va_drm.h>
#include "msdk.h"

GST_DEBUG_CATEGORY_EXTERN (gst_msdkenc_debug);
#define GST_CAT_DEFAULT gst_msdkenc_debug

struct _MsdkContext
{
  mfxSession session;
  gint fd;
  VADisplay dpy;
};

static gboolean
msdk_use_vaapi_on_context (MsdkContext * context)
{
  gint fd;
  gint maj_ver, min_ver;
  VADisplay va_dpy = NULL;
  VAStatus va_status;
  mfxStatus status;
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

  status = MFXVideoCORE_SetHandle (context->session, MFX_HANDLE_VA_DISPLAY,
      (mfxHDL) va_dpy);
  if (status != MFX_ERR_NONE) {
    GST_ERROR ("Setting VAAPI handle failed (%s)",
        msdk_status_to_string (status));
    goto failed;
  }

  context->fd = fd;
  context->dpy = va_dpy;

  return TRUE;

failed:
  if (va_dpy)
    vaTerminate (va_dpy);
  close (fd);
  return FALSE;
}

MsdkContext *
msdk_open_context (gboolean hardware)
{
  MsdkContext *context = g_slice_new0 (MsdkContext);
  context->fd = -1;

  context->session = msdk_open_session (hardware);
  if (!context->session)
    goto failed;

  if (hardware) {
    if (!msdk_use_vaapi_on_context (context))
      goto failed;
  }

  return context;

failed:
  msdk_close_session (context->session);
  g_slice_free (MsdkContext, context);
  return NULL;
}

void
msdk_close_context (MsdkContext * context)
{
  if (!context)
    return;

  msdk_close_session (context->session);
  if (context->dpy)
    vaTerminate (context->dpy);
  if (context->fd >= 0)
    close (context->fd);
  g_slice_free (MsdkContext, context);
}

mfxSession
msdk_context_get_session (MsdkContext * context)
{
  return context->session;
}
