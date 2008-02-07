/*
 * GStreamer DirectShow codecs wrapper
 * Copyright <2006, 2007, 2008> Fluendo <gstreamer@fluendo.com>
 * Copyright <2006, 2007, 2008> Pioneers of the Inevitable <songbird@songbirdnest.com>
 * Copyright <2007,2008> Sebastien Moutte <sebastien@moutte.net>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Alternatively, the contents of this file may be used under the
 * GNU Lesser General Public License Version 2.1 (the "LGPL"), in
 * which case the following provisions apply instead of the ones
 * mentioned above:
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

#include "gstdshowaudiodec.h"
#include "gstdshowvideodec.h"

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
  gst_dshow_register_fakefilters ();

  if (!dshow_adec_register (plugin) || !dshow_vdec_register (plugin))
    return FALSE;

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "dshowdecwrapper",
    "Directshow decoder wrapper plugin",
    plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
