/* GStreamer
 * Copyright (C) 2023 Thibault Saunier <tsaunier@igalia.com>
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
 * Free Software Foundation, Inc., 51 Franklin Street, Suite 500,
 * Boston, MA 02110-1335, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <math.h>

#include "ges-frame-composition-meta.h"

typedef struct _GESVideoScale GESVideoScale;
typedef struct
{
  GstBinClass parent_class;
} GESVideoScaleClass;

struct _GESVideoScale
{
  GstBin parent;

  GstPad *sink;
  GstElement *capsfilter;

  gint width, height;
};

/* *INDENT-OFF* */
static GstStaticPadTemplate gst_video_scale_sink_template =
    GST_STATIC_PAD_TEMPLATE ("sink",
        GST_PAD_SINK,
        GST_PAD_ALWAYS,
      GST_STATIC_CAPS ("ANY")
    );

static GstStaticPadTemplate gst_video_scale_src_template =
    GST_STATIC_PAD_TEMPLATE ("src",
      GST_PAD_SRC,
      GST_PAD_ALWAYS,
      GST_STATIC_CAPS ("ANY")
    );

GES_DECLARE_TYPE (VideoScale, video_scale, VIDEO_SCALE)
G_DEFINE_TYPE (GESVideoScale, ges_video_scale, GST_TYPE_BIN);
/* *INDENT-ON* */

static void
set_dimension (GESVideoScale * self, gint width, gint height)
{
  GstCaps *caps = gst_caps_new_simple ("video/x-raw",
      "pixel-aspect-ratio", GST_TYPE_FRACTION, 1, 1,
      NULL);

  if (width >= 0)
    gst_caps_set_simple (caps, "width", G_TYPE_INT, width, NULL);
  if (height >= 0)
    gst_caps_set_simple (caps, "height", G_TYPE_INT, height, NULL);

  gst_caps_set_features (caps, 0, gst_caps_features_new_any ());
  g_object_set (self->capsfilter, "caps", caps, NULL);
  gst_caps_unref (caps);

  GST_OBJECT_LOCK (self);
  self->width = width;
  self->height = height;
  GST_OBJECT_UNLOCK (self);
}

static GstFlowReturn
chain (GstPad * pad, GESVideoScale * self, GstBuffer * buffer)
{
  GESFrameCompositionMeta *meta;

  meta =
      (GESFrameCompositionMeta *) gst_buffer_get_meta (buffer,
      ges_frame_composition_meta_api_get_type ());

  if (meta) {
    GST_OBJECT_LOCK (self);
    if (meta->height != self->height || meta->width != self->width) {
      GST_OBJECT_UNLOCK (self);

      set_dimension (self, (gint) round (meta->width),
          (gint) round (meta->height));
    } else {
      GST_OBJECT_UNLOCK (self);
    }

    meta->height = meta->width = -1;
  }

  return gst_proxy_pad_chain_default (pad, GST_OBJECT (self), buffer);
}

static GstStateChangeReturn
change_state (GstElement * element, GstStateChange transition)
{
  GESVideoScale *self = GES_VIDEO_SCALE (element);
  GstStateChangeReturn res =
      ((GstElementClass *) ges_video_scale_parent_class)->change_state (element,
      transition);

  if (transition == GST_STATE_CHANGE_PAUSED_TO_READY) {
    GST_OBJECT_LOCK (self);
    self->width = 0;
    self->height = 0;
    GST_OBJECT_UNLOCK (self);
  }

  return res;
}

static void
ges_video_scale_init (GESVideoScale * self)
{
  GstPad *pad;
  GstElement *scale;
  GstPadTemplate *template =
      gst_static_pad_template_get (&gst_video_scale_sink_template);

  scale = gst_element_factory_make ("videoscale", NULL);
  g_object_set (scale, "add-borders", FALSE, NULL);
  self->capsfilter = gst_element_factory_make ("capsfilter", NULL);

  gst_bin_add_many (GST_BIN (self), scale, self->capsfilter, NULL);
  gst_element_link (scale, self->capsfilter);

  self->sink =
      gst_ghost_pad_new_from_template ("sink", scale->sinkpads->data, template);
  gst_pad_set_chain_function (self->sink, (GstPadChainFunction) chain);
  gst_element_add_pad (GST_ELEMENT (self), self->sink);
  gst_object_unref (template);

  template = gst_static_pad_template_get (&gst_video_scale_src_template);
  pad =
      gst_ghost_pad_new_from_template ("src", self->capsfilter->srcpads->data,
      template);
  gst_element_add_pad (GST_ELEMENT (self), pad);
  gst_object_unref (template);
}

static void
ges_video_scale_class_init (GESVideoScaleClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_set_static_metadata (element_class,
      "VideoScale",
      "Video/Filter",
      "Scaling element usable as a GES effect",
      "Thibault Saunier <tsaunier@igalia.com>");

  gst_element_class_add_static_pad_template (element_class,
      &gst_video_scale_sink_template);
  gst_element_class_add_static_pad_template (element_class,
      &gst_video_scale_src_template);

  element_class->change_state = change_state;
}
