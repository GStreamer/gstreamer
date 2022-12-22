/* Copyright (C) <2021> Thibault Saunier <tsaunier@igalia.com>
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

#include "gstwpeextension.h"

#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>

#include <gst/gst.h>
#include <gmodule.h>
#include <gio/gunixfdlist.h>
#include <wpe/webkit-web-extension.h>

GST_DEBUG_CATEGORY_STATIC (wpe_extension_debug);
#define GST_CAT_DEFAULT wpe_extension_debug

G_MODULE_EXPORT void webkit_web_extension_initialize (WebKitWebExtension *
    extension);

static WebKitWebExtension *global_extension = NULL;

void
webkit_web_extension_initialize (WebKitWebExtension * extension)
{
  g_return_if_fail (!global_extension);

  gst_init (NULL, NULL);

  GST_DEBUG_CATEGORY_INIT (wpe_extension_debug, "wpewebextension", 0,
      "GstWPE WebExtension");

  /* Register our own audio sink to */
  gst_element_register (NULL, "gstwpeaudiosink", GST_RANK_PRIMARY + 500,
      gst_wpe_audio_sink_get_type ());
  gst_object_unref (g_object_new (gst_wpe_bus_msg_forwarder_get_type (), NULL));

  global_extension = extension;
  GST_INFO ("Setting as global extension.");
}

void
gst_wpe_extension_send_message (WebKitUserMessage * msg,
    GCancellable * cancellable, GAsyncReadyCallback cb, gpointer udata)
{
  webkit_web_extension_send_message_to_context (global_extension, msg,
      cancellable, cb, udata);
}
