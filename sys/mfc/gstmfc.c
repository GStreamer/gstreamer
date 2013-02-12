/*
 * Copyright (C) 2012 Collabora Ltd.
 *     Author: Sebastian Dröge <sebastian.droege@collabora.co.uk>
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

#include <gst/gst.h>
#include "gstmfcdec.h"

GST_DEBUG_CATEGORY_EXTERN (GST_CAT_PLUGIN_LOADING);

static gboolean
plugin_init (GstPlugin * plugin)
{
  struct mfc_dec_context *context;

  /* Just check here once if we can create a MFC context, i.e.
   * if the hardware is available */
  mfc_dec_init_debug ();
  context = mfc_dec_create (CODEC_TYPE_H264);
  if (!context) {
    GST_CAT_DEBUG (GST_CAT_PLUGIN_LOADING,
        "Failed to initialize MFC decoder context");
    return TRUE;
  }
  mfc_dec_destroy (context);

  if (!gst_element_register (plugin, "mfcdec", GST_RANK_PRIMARY,
          GST_TYPE_MFC_DEC))
    return FALSE;

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    mfc,
    "Samsung Exynos MFC plugin",
    plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
