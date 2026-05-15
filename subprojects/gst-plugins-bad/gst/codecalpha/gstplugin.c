/* GStreamer
 * Copyright (C) <2021> Collabora Ltd.
 *   Author: Nicolas Dufresne <nicolas.dufresne@collabora.com>
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

/**
 * SECTION:plugin-codecalpha
 *
 * This plugin contains a set of utilities that helps handling alpha encoded
 * streams as produced by some WebM streams using VP8/VP9. The elements are
 * meant to be used in decoder wrappers which allows playbin to automatically
 * handle these streams.
 *
 * `codecalphademux` will produce two streams out of a stream of buffers holding
 * the #GstVideoCodecAlphaMeta. The presence of the meta is indicated by the
 * usage of the field `codec-alpha=(boolean)true` in the caps. This is only
 * applicable to VP8 and VP9 for now.
 *
 * Since: 1.20
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gst/gst.h>

#include "gstcodecalphademux.h"
#include "gstalphacombine.h"

static gboolean
plugin_init (GstPlugin * plugin)
{
  gst_plugin_set_static_features_flag (plugin);
  gboolean ret = FALSE;

  ret |= GST_ELEMENT_REGISTER (codec_alpha_demux, plugin);
  ret |= GST_ELEMENT_REGISTER (alpha_combine, plugin);

  return ret;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    codecalpha,
    "CODEC Alpha Utilities",
    plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
