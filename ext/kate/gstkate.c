/*
 * GStreamer
 * Copyright 2005 Thomas Vander Stichele <thomas@apestaart.org>
 * Copyright 2005 Ronald S. Bultje <rbultje@ronald.bitfreak.net>
 * Copyright 2008 Vincent Penquerc'h <ogg.k.ogg.k@googlemail.com>
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include <gst/gst.h>

#include "gstkate.h"
#include "gstkatedec.h"
#include "gstkateenc.h"
#include "gstkateparse.h"
#include "gstkatetag.h"

#undef HAVE_TIGER
#ifdef HAVE_TIGER
#include "gstkatetiger.h"
#endif

GST_DEBUG_CATEGORY (gst_katedec_debug);
GST_DEBUG_CATEGORY (gst_kateenc_debug);
GST_DEBUG_CATEGORY (gst_kateparse_debug);
GST_DEBUG_CATEGORY (gst_katetag_debug);
GST_DEBUG_CATEGORY (gst_kateutil_debug);
#ifdef HAVE_TIGER
GST_DEBUG_CATEGORY (gst_katetiger_debug);
#endif

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_katedec_debug, "katedec", 0, "Kate decoder");
  GST_DEBUG_CATEGORY_INIT (gst_kateenc_debug, "kateenc", 0, "Kate encoder");
  GST_DEBUG_CATEGORY_INIT (gst_kateparse_debug, "kateparse", 0, "Kate parser");
  GST_DEBUG_CATEGORY_INIT (gst_katetag_debug, "katetag", 0, "Kate tagger");
  GST_DEBUG_CATEGORY_INIT (gst_kateutil_debug, "kateutil", 0,
      "Kate utility functions");
#ifdef HAVE_TIGER
  GST_DEBUG_CATEGORY_INIT (gst_katetiger_debug, "tiger", 0,
      "Kate Tiger renderer");
#endif

  if (!gst_element_register (plugin, "katedec", GST_RANK_PRIMARY,
          GST_TYPE_KATE_DEC))
    return FALSE;

  if (!gst_element_register (plugin, "kateenc", GST_RANK_NONE,
          GST_TYPE_KATE_ENC))
    return FALSE;

  if (!gst_element_register (plugin, "kateparse", GST_RANK_NONE,
          GST_TYPE_KATE_PARSE))
    return FALSE;

  if (!gst_element_register (plugin, "katetag", GST_RANK_NONE,
          GST_TYPE_KATE_TAG))
    return FALSE;

#ifdef HAVE_TIGER
  if (!gst_element_register (plugin, "tiger", GST_RANK_PRIMARY,
          GST_TYPE_KATE_TIGER))
    return FALSE;
#endif

  return TRUE;
}

/* this is the structure that gstreamer looks for to register plugins
 */
GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    kate,
    "Kate plugin",
    plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
