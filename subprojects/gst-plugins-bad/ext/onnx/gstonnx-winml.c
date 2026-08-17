/* GStreamer
 * Copyright (C) 2026 Seungha Yang <seungha@centricular.com>
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
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <WinMLEpCatalog.h>
#include "gstonnx-winml.h"

#ifndef GST_DISABLE_GST_DEBUG
#define GST_CAT_DEFAULT ensure_debug_category()
static GstDebugCategory *
ensure_debug_category (void)
{
  static gsize cat_gonce = 0;
  if (g_once_init_enter (&cat_gonce)) {
    gsize cat_done;
    cat_done = (gsize) _gst_debug_category_new ("onnx-winml", 0, "onnx-winml");
    g_once_init_leave (&cat_gonce, cat_done);
  }

  return (GstDebugCategory *) cat_gonce;
}
#endif

static void CALLBACK
on_completion_cb (WinMLAsyncBlock * async_block)
{
  HRESULT hr = WinMLAsyncGetStatus (async_block, FALSE);

  if (SUCCEEDED (hr)) {
    GST_INFO ("WindowsML EP %s is ready", (gchar *) async_block->context);
  } else {
    GST_WARNING ("Failed to prepare WindowsML EP %s, hr: 0x%x",
        (gchar *) async_block->context, (guint) hr);
  }
}

static void CALLBACK
on_progress_cb (WinMLAsyncBlock * async_block, double progress)
{
  GST_INFO ("WindowsML EP \"%s\" installation progress: %.1f%%",
      (gchar *) async_block->context, progress);
}

gchar *
gst_onnx_winml_ep_catalog_find (const gchar * ep_name)
{
  WinMLEpCatalogHandle handle = NULL;
  WinMLEpHandle ep = NULL;
  WinMLEpReadyState state = WinMLEpReadyState_NotPresent;
  gchar *lib_path = NULL;
  size_t lib_path_size = 0;

  HRESULT hr = WinMLEpCatalogCreate (&handle);
  if (FAILED (hr)) {
    GST_ERROR ("Couldn't create catalog, hr: 0x%x", (guint) hr);
    return NULL;
  }

  hr = WinMLEpCatalogFindProvider (handle, ep_name, NULL, &ep);
  if (FAILED (hr)) {
    GST_ERROR ("Couldn't find EP %s, hr: 0x%x", ep_name, (guint) hr);
    goto error;
  }

  hr = WinMLEpGetReadyState (ep, &state);
  if (FAILED (hr)) {
    GST_ERROR ("Couldn't get ready state, hr: 0x%x", (guint) hr);
    goto error;
  }

  if (state != WinMLEpReadyState_Ready) {
    WinMLAsyncBlock async_block = { NULL, };

    GST_DEBUG ("EP %s not ready (current: %d), ensuring ready", ep_name, state);
    async_block.context = (void *) ep_name;
    async_block.callback = on_completion_cb;
    async_block.progress = on_progress_cb;

    hr = WinMLEpEnsureReadyAsync (ep, &async_block);
    if (FAILED (hr)) {
      GST_ERROR ("Couldn't ensure ready, hr: 0x%x", (guint) hr);
      goto error;
    }

    hr = WinMLAsyncGetStatus (&async_block, TRUE);
    WinMLAsyncClose (&async_block);

    if (FAILED (hr)) {
      GST_ERROR ("Operation waiting failed, hr: 0x%x", (guint) hr);
      goto error;
    }
  }

  hr = WinMLEpGetLibraryPathSize (ep, &lib_path_size);
  if (FAILED (hr) || lib_path_size == 0) {
    GST_ERROR ("Couldn't get library path size, hr: 0x%x", (guint) hr);
    goto error;
  }

  lib_path = (gchar *) g_malloc0 (lib_path_size);
  hr = WinMLEpGetLibraryPath (ep, lib_path_size, lib_path, NULL);
  if (FAILED (hr)) {
    GST_ERROR ("Couldn't get library path, hr: 0x%x", (guint) hr);
    goto error;
  }

  GST_DEBUG ("Library path: %s", lib_path);
  WinMLEpCatalogRelease (handle);

  return lib_path;

error:
  WinMLEpCatalogRelease (handle);
  g_free (lib_path);
  return NULL;
}
