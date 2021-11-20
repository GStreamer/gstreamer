/*
 *  gstvaapioverlay.h - VA-API vpp overlay
 *
 *  Copyright (C) 2019 Intel Corporation
 *    Author: U. Artie Eoff <ullysses.a.eoff@intel.com>
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public License
 *  as published by the Free Software Foundation; either version 2.1
 *  of the License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this program; if not, write to the Free
 *  Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA 02110-1301 USA
*/

#ifndef GST_VAAPI_OVERLAY_H
#define GST_VAAPI_OVERLAY_H

#include "gstvaapipluginbase.h"
#include <gst/vaapi/gstvaapisurfacepool.h>
#include <gst/vaapi/gstvaapiblend.h>

G_BEGIN_DECLS

#define GST_TYPE_VAAPI_OVERLAY (gst_vaapi_overlay_get_type ())
#define GST_VAAPI_OVERLAY(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_VAAPI_OVERLAY, GstVaapiOverlay))
#define GST_VAAPI_OVERLAY_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_VAAPI_OVERLAY, \
      GstVaapiOverlayClass))
#define GST_IS_VAAPI_OVERLAY(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_VAAPI_OVERLAY))
#define GST_IS_VAAPI_OVERLAY_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_VAAPI_OVERLAY))
#define GST_VAAPI_OVERLAY_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_VAAPI_OVERLAY, \
      GstVaapiOverlayClass))

#define GST_TYPE_VAAPI_OVERLAY_SINK_PAD (gst_vaapi_overlay_sink_pad_get_type())
#define GST_VAAPI_OVERLAY_SINK_PAD(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_VAAPI_OVERLAY_SINK_PAD, \
      GstVaapiOverlaySinkPad))
#define GST_VAAPI_OVERLAY_SINK_PAD_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_VAAPI_OVERLAY_SINK_PAD, \
      GstVaapiOverlaySinkPadClass))
#define GST_IS_VAAPI_OVERLAY_SINK_PAD(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_VAAPI_OVERLAY_SINK_PAD))
#define GST_IS_VAAPI_OVERLAY_SINK_PAD_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_VAAPI_OVERLAY_SINK_PAD))

typedef struct _GstVaapiOverlay GstVaapiOverlay;
typedef struct _GstVaapiOverlayClass GstVaapiOverlayClass;

typedef struct _GstVaapiOverlaySinkPad GstVaapiOverlaySinkPad;
typedef struct _GstVaapiOverlaySinkPadClass GstVaapiOverlaySinkPadClass;

struct _GstVaapiOverlay
{
  GstVaapiPluginBase parent_instance;

  GstVaapiBlend *blend;
  GstVaapiVideoPool *blend_pool;
};

struct _GstVaapiOverlayClass
{
  GstVaapiPluginBaseClass parent_class;
};

struct _GstVaapiOverlaySinkPad
{
  GstVideoAggregatorPad parent_instance;

  gint xpos, ypos;
  gint width, height;
  gdouble alpha;

  GstVaapiPadPrivate *priv;
};

struct _GstVaapiOverlaySinkPadClass
{
  GstVideoAggregatorPadClass parent_class;
};

GType
gst_vaapi_overlay_get_type (void) G_GNUC_CONST;

GType
gst_vaapi_overlay_sink_pad_get_type (void) G_GNUC_CONST;

gboolean
gst_vaapioverlay_register (GstPlugin * plugin, GstVaapiDisplay * display);

G_END_DECLS

#endif
