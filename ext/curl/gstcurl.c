/* GStreamer
 * Copyright (C) 2011 Axis Communications <dev-gstreamer@axis.com>
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
#include <config.h>
#endif

#include <gst/gst-i18n-plugin.h>

#include "gstcurlbasesink.h"
#include "gstcurltlssink.h"
#include "gstcurlhttpsink.h"
#include "gstcurlfilesink.h"
#include "gstcurlftpsink.h"
#include "gstcurlsmtpsink.h"
#ifdef HAVE_SSH2
#include "gstcurlsftpsink.h"
#endif
#include "gstcurlhttpsrc.h"

static gboolean
plugin_init (GstPlugin * plugin)
{
#ifdef ENABLE_NLS
  GST_DEBUG ("binding text domain %s to locale dir %s", GETTEXT_PACKAGE,
      LOCALEDIR);
  bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
#endif /* ENABLE_NLS */

  if (!gst_element_register (plugin, "curlhttpsink", GST_RANK_NONE,
          GST_TYPE_CURL_HTTP_SINK))
    return FALSE;

  if (!gst_element_register (plugin, "curlfilesink", GST_RANK_NONE,
          GST_TYPE_CURL_FILE_SINK))
    return FALSE;

  if (!gst_element_register (plugin, "curlftpsink", GST_RANK_NONE,
          GST_TYPE_CURL_FTP_SINK))
    return FALSE;

  if (!gst_element_register (plugin, "curlsmtpsink", GST_RANK_NONE,
          GST_TYPE_CURL_SMTP_SINK))
    return FALSE;

#ifdef HAVE_SSH2
  if (!gst_element_register (plugin, "curlsftpsink", GST_RANK_NONE,
          GST_TYPE_CURL_SFTP_SINK))
    return FALSE;
#endif
  if (!gst_element_register (plugin, "curlhttpsrc", GST_RANK_SECONDARY,
          GST_TYPE_CURLHTTPSRC))
    return FALSE;

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    curl,
    "libcurl-based elements",
    plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
