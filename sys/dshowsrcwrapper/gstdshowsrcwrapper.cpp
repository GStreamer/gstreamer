/* GStreamer
 * Copyright (C)  2007 Sebastien Moutte <sebastien@moutte.net>
 *
 * gstdshowsrcwrapper.c: 
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
#include "config.h"
#endif

#include "gstdshowaudiosrc.h"
#include "gstdshowvideosrc.h"

const GUID CLSID_GstreamerSrcFilter
    =
    { 0x6a780808, 0x9725, 0x4d0b, {0x86, 0x95, 0xa4, 0xdd, 0x8d, 0x21, 0x7,
        0x73} };

const GUID IID_IGstSrcInterface =
    { 0x542c0a24, 0x8bd1, 0x46cb, {0xaa, 0x57, 0x3e, 0x46, 0xd0, 0x6, 0xd2,
        0xf3} };

static gboolean
plugin_init (GstPlugin * plugin)
{
  /* register fake filters */
  HRESULT hr = gst_dshow_register_fakefilters ();
  if (FAILED (hr)) {
    g_warning ("failed to register directshow fakesink filter: 0x%x\n", hr);
    return FALSE;
  }

  if (!gst_element_register (plugin, "dshowaudiosrc",
          GST_RANK_NONE,
          GST_TYPE_DSHOWAUDIOSRC) ||
      !gst_element_register (plugin, "dshowvideosrc",
          GST_RANK_NONE, GST_TYPE_DSHOWVIDEOSRC))
    return FALSE;

  return TRUE;
}

extern "C" {

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "dshowsrcwrapper",
    "DirectShow sources wrapper plugin",
    plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)

}
