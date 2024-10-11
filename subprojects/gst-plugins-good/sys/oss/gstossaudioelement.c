/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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

#include <glib/gi18n-lib.h>

#include "common.h"
#include "gstossaudioelements.h"
#include "gstossdeviceprovider.h"

GST_DEBUG_CATEGORY (oss_debug);
#define GST_CAT_DEFAULT oss_debug

GST_DEVICE_PROVIDER_REGISTER_DEFINE (ossdeviceprovider, "ossdeviceprovider",
    GST_RANK_SECONDARY, GST_TYPE_OSS_DEVICE_PROVIDER);

void
oss_element_init (GstPlugin * plugin)
{
  static gsize res = FALSE;
  if (g_once_init_enter (&res)) {
    GST_DEBUG_CATEGORY_INIT (oss_debug, "oss", 0, "OSS elements");

#ifdef ENABLE_NLS
    GST_DEBUG ("binding text domain %s to locale dir %s", GETTEXT_PACKAGE,
        LOCALEDIR);
    bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
    bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
#endif /* ENABLE_NLS */
    g_once_init_leave (&res, TRUE);
  }
}
