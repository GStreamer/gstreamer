/* GStreamer
 * Copyright (C) 2011 David Hoyt <dhoyt@hoytsoft.org>
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

#include <glib.h>
#include <glib/gprintf.h>
#include <gmodule.h>

#include "dx.h"
#include "directx9/dx9.h"
#include "directx10/dx10.h"
#include "directx11/dx11.h"



static void
init_supported_apis (void)
{
  /* Gather information we'll need about each version of DirectX. */
  /* Insert in reverse order of desired priority due to the g_list_prepend() call in directx_determine_best_available_api(). */
  INITIALIZE_SUPPORTED_DIRECTX_API (DIRECTX_9);
  /* TODO: Add DirectX 10 support. */
  /*INITIALIZE_SUPPORTED_DIRECTX_API(DIRECTX_10); */
  /* TODO: Add DirectX 11 support. */
  /*INITIALIZE_SUPPORTED_DIRECTX_API(DIRECTX_11); */
}



/* Function declarations */
static DirectXAPI *directx_determine_best_available_api (void);

/* Mutex macros */
#define DIRECTX_LOCK	  g_static_rec_mutex_lock (&dx_lock);
#define DIRECTX_UNLOCK  g_static_rec_mutex_unlock (&dx_lock);

typedef struct _DirectXInfo DirectXInfo;
struct _DirectXInfo
{
  gboolean initialized;
  gboolean supported;

  DirectXInitParams *init_params;
  DirectXAPI *best_api;
  GList *supported_api_list;
  gint32 supported_api_count;
};

/* Private vars */
static DirectXInfo dx;
static GStaticRecMutex dx_lock = G_STATIC_REC_MUTEX_INIT;

gboolean
directx_initialize (DirectXInitParams * init_params)
{
  DIRECTX_LOCK if (dx.initialized)
      goto success;

  dx.init_params = NULL;
  dx.init_params = init_params;

  init_supported_apis ();

  dx.best_api = directx_determine_best_available_api ();
  dx.supported = (dx.best_api != NULL
      && !DIRECTX_VERSION_IS_UNKNOWN (dx.best_api->version));
  dx.initialized = TRUE;

success:
  DIRECTX_UNLOCK return TRUE;
}

gboolean
directx_api_initialize (DirectXAPI * api)
{
  if (!api)
    return FALSE;

  DIRECTX_LOCK if (!directx_is_initialized ())
      goto error;

  if (api->initialized)
    goto success;

  /* API init */
  api->initialize (api);

  /* Component initialization */
  DIRECTX_COMPONENT_INIT (DIRECTX_D3D (api));
  DIRECTX_COMPONENT_INIT (DIRECTX_DINPUT (api));
  DIRECTX_COMPONENT_INIT (DIRECTX_DSOUND (api));
  DIRECTX_COMPONENT_INIT (DIRECTX_DWRITE (api));
  DIRECTX_COMPONENT_INIT (DIRECTX_D2D (api));
  DIRECTX_COMPONENT_INIT (DIRECTX_DCOMPUTE (api));

  /* All done */
  api->initialized = TRUE;

success:
  DIRECTX_UNLOCK return TRUE;
error:
  DIRECTX_UNLOCK return FALSE;
}

gboolean
directx_initialize_best_available_api (void)
{
  return directx_api_initialize (directx_get_best_available_api ());
}

gboolean
directx_is_initialized (void)
{
  gboolean initialized = FALSE;

  DIRECTX_LOCK initialized = dx.initialized;
  DIRECTX_UNLOCK return initialized;
}

gboolean
directx_api_is_initialized (const DirectXAPI * api)
{
  if (!api)
    return FALSE;
  {
    gboolean initialized;

    DIRECTX_LOCK initialized = api->initialized;
    DIRECTX_UNLOCK return initialized;
  }
}

gboolean
directx_best_available_api_is_initialized (void)
{
  return directx_api_is_initialized (directx_get_best_available_api ());
}

gboolean
directx_is_supported (void)
{
  return dx.supported;
}

GList *
directx_get_supported_apis (void)
{
  return dx.supported_api_list;
}

gint32
directx_get_supported_api_count (void)
{
  return dx.supported_api_count;
}

DirectXAPI *
directx_get_best_available_api (void)
{
  return dx.best_api;
}

void
directx_log_debug (const gchar * file, const gchar * function, gint line,
    const gchar * format, ...)
{
  if (!dx.init_params || !dx.init_params->log_debug)
    return;
  {
    va_list args;
    va_start (args, format);
    dx.init_params->log_debug (file, function, line, format, args);
    va_end (args);
  }
}

void
directx_log_warning (const gchar * file, const gchar * function, gint line,
    const gchar * format, ...)
{
  if (!dx.init_params || !dx.init_params->log_warning)
    return;
  {
    va_list args;
    va_start (args, format);
    dx.init_params->log_warning (file, function, line, format, args);
    va_end (args);
  }
}

void
directx_log_error (const gchar * file, const gchar * function, gint line,
    const gchar * format, ...)
{
  if (!dx.init_params || !dx.init_params->log_error)
    return;
  {
    va_list args;
    va_start (args, format);
    dx.init_params->log_error (file, function, line, format, args);
    va_end (args);
  }
}

/* This should only be called through use of the DIRECTX_API() macro. It should never be called directly. */
gboolean
directx_add_supported_api (DirectXAPI * api)
{
  if (!api)
    return FALSE;

  DIRECTX_LOCK {

    /* Add to our GList containing all of our supported APIs. */
    /* GLists are doubly-linked lists and calling prepend() prevents it from having to traverse the entire list just to add one item. */
    dx.supported_api_list = g_list_prepend (dx.supported_api_list, api);
    dx.supported_api_count++;

  }
/*success:*/
  DIRECTX_UNLOCK return TRUE;
}

static DirectXAPI *
directx_determine_best_available_api (void)
{
  if (!g_module_supported ())
    return NULL;

  {
    GList *item;
    GModule *lib;
    DirectXAPI *dxlib = NULL;

    DIRECTX_LOCK {
      /* Search supported APIs (DirectX9, DirectX10, etc.) looking for the first one that works. */
      DIRECTX_DEBUG
          ("Searching supported DirectX APIs for the best (most recent) one available");
      for (item = g_list_first (dx.supported_api_list); item; item = item->next) {
        if ((dxlib = (DirectXAPI *) item->data) == NULL)
          continue;

        DIRECTX_DEBUG ("Determining support for %s", dxlib->description);
        DIRECTX_DEBUG ("Searching for module \"%s\" with the symbol \"%s\"",
            dxlib->module_test, dxlib->symbol_test);

        /* Can we locate and open a Direct3D library (e.g. d3d9.dll or d3d10.dll)? */
        if ((lib =
                g_module_open (dxlib->module_test,
                    G_MODULE_BIND_LAZY)) != NULL) {
          /* Look for a symbol/function (e.g. "Direct3DCreate9") in the module and if it exists, we found one! */
          gpointer symbol;
          if (g_module_symbol (lib, dxlib->symbol_test, &symbol)) {
            g_module_close (lib);
            DIRECTX_DEBUG ("Selected %s", dxlib->description);
            goto done;
          }
          /* Ensure we don't have a mem leak. */
          g_module_close (lib);
        }
      }

    }
  done:
    DIRECTX_UNLOCK return dxlib;
  }
}
