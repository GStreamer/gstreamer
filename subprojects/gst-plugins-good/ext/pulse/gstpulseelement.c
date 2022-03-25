/*
 *  GStreamer pulseaudio plugin
 *
 *  Copyright (c) 2004-2008 Lennart Poettering
 *
 *  gst-pulse is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as
 *  published by the Free Software Foundation; either version 2.1 of the
 *  License, or (at your option) any later version.
 *
 *  gst-pulse is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with gst-pulse; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301
 *  USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <glib/gi18n-lib.h>

#include "gstpulseelements.h"
#include "pulsedeviceprovider.h"

GST_DEBUG_CATEGORY (pulse_debug);

GST_DEVICE_PROVIDER_REGISTER_DEFINE (pulsedeviceprovider, "pulsedeviceprovider",
    GST_RANK_PRIMARY, GST_TYPE_PULSE_DEVICE_PROVIDER);

void
pulse_element_init (GstPlugin * plugin)
{
  static gsize res = FALSE;

  if (g_once_init_enter (&res)) {
#ifdef ENABLE_NLS
    GST_DEBUG ("binding text domain %s to locale dir %s", GETTEXT_PACKAGE,
        LOCALEDIR);
    bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
    bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
#endif

    GST_DEBUG_CATEGORY_INIT (pulse_debug, "pulse", 0, "PulseAudio elements");

    g_once_init_leave (&res, TRUE);
  }
}
