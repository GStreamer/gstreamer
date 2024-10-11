/* GStreamer SVT JPEG XS plugin
 * Copyright (C) 2024 Tim-Philipp Müller <tim centricular com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

/**
 * SECTION:plugin-svtjpegxs
 *
 * The svtjpegxs plugin provides JPEG XS encoding and decoding using the
 * Scalable Video Technology for JPEG XS library ([SVT-JPEG-XS][svtjpegxs]).
 *
 * [svtjpegxs]: https://github.com/OpenVisualCloud/SVT-JPEG-XS/
 *
 * Since: 1.26
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstsvtjpegxsdec.h"
#include "gstsvtjpegxsenc.h"

#include <gst/gst.h>

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_ELEMENT_REGISTER (svtjpegxsdec, plugin);
  GST_ELEMENT_REGISTER (svtjpegxsenc, plugin);
  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR, GST_VERSION_MINOR, svtjpegxs,
    "Scalable Video Technology for JPEG XS (SVT-JPEG-XS)", plugin_init,
    VERSION, "LGPL", PACKAGE_NAME, GST_PACKAGE_ORIGIN)
