/* GStreamer
 * Copyright (C) 2004 David A. Schleef <ds@schleef.org>
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
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <gst/gst.h>


extern GType gst_diracdec_get_type (void);

//extern GType gst_diracenc_get_type (void);


static gboolean
plugin_init (GstPlugin * plugin)
{
#if 0
  if (!gst_element_register (plugin, "diracenc", GST_RANK_NONE,
          gst_diracenc_get_type ())) {
    return FALSE;
  }
#endif

  if (!gst_element_register (plugin, "diracdec", GST_RANK_PRIMARY,
          gst_diracdec_get_type ())) {
    return FALSE;
  }

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "dirac",
    "Dirac plugin", plugin_init, VERSION, "LGPL", GST_PACKAGE, GST_ORIGIN)
