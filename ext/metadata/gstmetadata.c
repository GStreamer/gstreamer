/*
 * GStreamer
 * Copyright 2007 Edgard Lima <edgard.lima@indt.org.br>
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

#include <gst/gst.h>

#include "gstmetadatademux.h"
#include "gstmetadatamux.h"
#include "metadatatags.h"

GST_DEBUG_CATEGORY_EXTERN (gst_metadata_exif_debug);
GST_DEBUG_CATEGORY_EXTERN (gst_metadata_iptc_debug);
GST_DEBUG_CATEGORY_EXTERN (gst_metadata_xmp_debug);

static gboolean
plugin_init (GstPlugin * plugin)
{

  gboolean ret = TRUE;

  GST_DEBUG_CATEGORY_INIT (gst_metadata_exif_debug, "metadata_exif",
      0, "Metadata exif");
  GST_DEBUG_CATEGORY_INIT (gst_metadata_iptc_debug, "metadata_iptc",
      0, "Metadata iptc");
  GST_DEBUG_CATEGORY_INIT (gst_metadata_xmp_debug, "metadata_xmp", 0,
      "Metadata xmp");

  metadata_tags_register ();

  ret = gst_metadata_demux_plugin_init (plugin);

  ret = ret && gst_metadata_mux_plugin_init (plugin);

  return ret;

}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "metadata",
    "Metadata (EXIF, IPTC and XMP) image (JPEG, TIFF) demuxer and muxer",
    plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
