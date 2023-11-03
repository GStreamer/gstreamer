/*
 * GStreamer
 * Copyright (C) 2023 Seungha Yang <seungha@centricular.com>
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
 * SECTION:plugin-decklink2
 * @title: DeckLink2
 * @short_description: BlackMagic DeckLink capture and playback plugin
 *
 * A plugin containing elements for capturing and outputting video and audio
 * using BlackMagic DeckLink devices.
 *
 * DeckLink API version 10.11 or newer is supported.
 *
 * Since: 1.30
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include "gstdecklink2combiner.h"
#include "gstdecklink2demux.h"
#include "gstdecklink2deviceprovider.h"
#include "gstdecklink2sink.h"
#include "gstdecklink2sinkbin.h"
#include "gstdecklink2src.h"
#include "gstdecklink2srcbin.h"
#include "gstdecklink2utils.h"

GST_DEBUG_CATEGORY (gst_decklink2_debug);

static void
plugin_deinit (gpointer data)
{
  gst_decklink2_deinit ();
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_decklink2_debug, "decklink2", 0, "decklink2");

  gst_plugin_set_static_features_flag (plugin);
  gst_decklink2_init_once ();

  GST_ELEMENT_REGISTER (decklink2combiner, plugin);
  GST_ELEMENT_REGISTER (decklink2demux, plugin);
  GST_ELEMENT_REGISTER (decklink2sink, plugin);
  GST_ELEMENT_REGISTER (decklink2sinkbin, plugin);
  GST_ELEMENT_REGISTER (decklink2src, plugin);
  GST_ELEMENT_REGISTER (decklink2srcbin, plugin);

  GST_DEVICE_PROVIDER_REGISTER (decklink2deviceprovider, plugin);

  g_object_set_data_full (G_OBJECT (plugin),
      "plugin-decklink2-shutdown", (gpointer) "shutdown-data",
      (GDestroyNotify) plugin_deinit);

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    decklink2,
    "Blackmagic Decklink plugin",
    plugin_init, VERSION, "LGPL", PACKAGE_NAME, GST_PACKAGE_ORIGIN)
