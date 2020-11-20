/*
 * GStreamer
 * Copyright (C) 2006 Stefan Kost <ensonic@users.sf.net>
 * Copyright (c) 2020 Anthony Violo <anthony.violo@ubicast.eu>
 * Copyright (c) 2020 Thibault Saunier <tsaunier@igalia.com>
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

/**
 * SECTION:element-qroverlay
 *
 * Element to set random data on a qroverlay.
 *
 * ## Example launch line
 *
 * ``` bash
 * gst-launch -v -m videotestsrc ! qroverlay ! fakesink silent=TRUE
 * ```
 *
 * Since: 1.20
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>
#include <json-glib/json-glib.h>

#include <qrencode.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "gstqroverlay.h"

GST_DEBUG_CATEGORY_STATIC (gst_qr_overlay_debug);
#define GST_CAT_DEFAULT gst_qr_overlay_debug

enum
{
  PROP_0,
  PROP_DATA,
};

struct _GstQROverlay
{
  GstBaseQROverlay parent;
  gchar *data;
};

#define gst_qr_overlay_parent_class parent_class
G_DEFINE_TYPE (GstQROverlay, gst_qr_overlay, GST_TYPE_BASE_QR_OVERLAY);

static gchar *
get_qrcode_content (GstBaseQROverlay * self, GstBuffer * buf,
    GstVideoInfo * info)
{
  return g_strdup (GST_QR_OVERLAY (self)->data);
}

static void
gst_qr_overlay_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstQROverlay *self = GST_QR_OVERLAY (object);

  switch (prop_id) {
    case PROP_DATA:
      self->data = g_value_dup_string (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_qr_overlay_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstQROverlay *self = GST_QR_OVERLAY (object);

  switch (prop_id) {
    case PROP_DATA:
      g_value_set_string (value, self->data);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_qr_overlay_class_init (GstQROverlayClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->set_property = gst_qr_overlay_set_property;
  gobject_class->get_property = gst_qr_overlay_get_property;

  gst_element_class_set_details_simple (gstelement_class,
      "qroverlay",
      "Qrcode overlay containing random data",
      "Overlay Qrcodes over each buffer with data passed in",
      "Thibault Saunier <tsaunier@igalia.com>");

  g_object_class_install_property (gobject_class,
      PROP_DATA, g_param_spec_string ("data",
          "Data",
          "Data to write in the QRCode to be overlaid",
          NULL,
          G_PARAM_READWRITE | GST_PARAM_MUTABLE_PLAYING |
          GST_PARAM_CONTROLLABLE));

  GST_BASE_QR_OVERLAY_CLASS (klass)->get_content =
      GST_DEBUG_FUNCPTR (get_qrcode_content);
}

/* initialize the new element
 * initialize instance structure
 */
static void
gst_qr_overlay_init (GstQROverlay * filter)
{
}

#include "gstdebugqroverlay.h"

static gboolean
qroverlay_init (GstPlugin * qroverlay)
{
  GST_DEBUG_CATEGORY_INIT (gst_qr_overlay_debug, "qroverlay", 0,
      "Qrcode overlay element");

  if (gst_element_register (qroverlay, "debugqroverlay", GST_RANK_NONE,
          GST_TYPE_DEBUG_QR_OVERLAY))
    return gst_element_register (qroverlay, "qroverlay", GST_RANK_NONE,
        GST_TYPE_QR_OVERLAY);

  return FALSE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    qroverlay,
    "libqrencode qroverlay plugin",
    qroverlay_init, VERSION, "LGPL", PACKAGE_NAME, GST_PACKAGE_ORIGIN)
