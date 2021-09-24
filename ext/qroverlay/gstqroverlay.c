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

#include "gstqroverlayelements.h"
#include "gstqroverlay.h"

enum
{
  PROP_0,
  PROP_DATA,
};

struct _GstQROverlay
{
  GstBaseQROverlay parent;
  gchar *data;

  gboolean data_changed;
};

#define gst_qr_overlay_parent_class parent_class
G_DEFINE_TYPE (GstQROverlay, gst_qr_overlay, GST_TYPE_BASE_QR_OVERLAY);
GST_ELEMENT_REGISTER_DEFINE_WITH_CODE (qroverlay, "qroverlay", GST_RANK_NONE,
    GST_TYPE_QR_OVERLAY, qroverlay_element_init (plugin));

static gchar *
get_qrcode_content (GstBaseQROverlay * base, GstBuffer * buf,
    GstVideoInfo * info, gboolean * reuse_prev)
{
  gchar *content;
  GstQROverlay *self = GST_QR_OVERLAY (base);

  GST_OBJECT_LOCK (self);
  content = g_strdup (self->data);
  *reuse_prev = self->data_changed;
  GST_OBJECT_UNLOCK (self);

  return content;
}

static void
gst_qr_overlay_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstQROverlay *self = GST_QR_OVERLAY (object);

  switch (prop_id) {
    case PROP_DATA:
      GST_OBJECT_LOCK (self);
      self->data = g_value_dup_string (value);
      self->data_changed = TRUE;
      GST_OBJECT_UNLOCK (self);
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
