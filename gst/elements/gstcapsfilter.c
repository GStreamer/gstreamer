/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *                    2005 Wim Taymans <wim@fluendo.com>
 *                    2005 David Schleef <ds@schleef.org>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "../gst-i18n-lib.h"
#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>


GstElementDetails gst_capsfilter_details = GST_ELEMENT_DETAILS ("CapsFilter",
    "Generic",
    "Pass data without modification, limiting formats",
    "David Schleef <ds@schleef.org>");


#define GST_TYPE_CAPSFILTER \
  (gst_capsfilter_get_type())
#define GST_CAPSFILTER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_CAPSFILTER,GstCapsFilter))
#define GST_CAPSFILTER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_CAPSFILTER,GstCapsFilterClass))
#define GST_IS_CAPSFILTER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_CAPSFILTER))
#define GST_IS_CAPSFILTER_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_CAPSFILTER))

typedef struct _GstCapsFilter GstCapsFilter;
typedef struct _GstCapsFilterClass GstCapsFilterClass;

struct _GstCapsFilter
{
  GstBaseTransform trans;

  GstCaps *filter_caps;
};

struct _GstCapsFilterClass
{
  GstBaseTransformClass trans_class;
};

GType gst_capsfilter_get_type (void);


enum
{
  PROP_0,
  PROP_FILTER_CAPS
};


static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);


GST_DEBUG_CATEGORY_STATIC (gst_capsfilter_debug);
#define GST_CAT_DEFAULT gst_capsfilter_debug

#define _do_init(bla) \
    GST_DEBUG_CATEGORY_INIT (gst_capsfilter_debug, "capsfilter", 0, \
    "capsfilter element");

GST_BOILERPLATE_FULL (GstCapsFilter, gst_capsfilter, GstBaseTransform,
    GST_TYPE_BASE_TRANSFORM, _do_init);


static void gst_capsfilter_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_capsfilter_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_capsfilter_dispose (GObject * object);
static GstCaps *gst_capsfilter_transform_caps (GstBaseTransform * base,
    GstPad * pad, GstCaps * caps);
static GstFlowReturn gst_capsfilter_transform_ip (GstBaseTransform * base,
    GstBuffer * buf);


static void
gst_capsfilter_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&srctemplate));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sinktemplate));
  gst_element_class_set_details (element_class, &gst_capsfilter_details);
}

static void
gst_capsfilter_class_init (GstCapsFilterClass * klass)
{
  GObjectClass *gobject_class;
  GstBaseTransformClass *trans_class;

  gobject_class = (GObjectClass *) klass;
  gobject_class->set_property = gst_capsfilter_set_property;
  gobject_class->get_property = gst_capsfilter_get_property;
  gobject_class->dispose = gst_capsfilter_dispose;

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_FILTER_CAPS,
      g_param_spec_boxed ("filter_caps", _("Filter caps"),
          _("Restrict the possible allowed formats"),
          GST_TYPE_CAPS, G_PARAM_READWRITE));

  trans_class = (GstBaseTransformClass *) klass;
  trans_class->transform_caps = gst_capsfilter_transform_caps;
  trans_class->transform_ip = gst_capsfilter_transform_ip;
}

static void
gst_capsfilter_init (GstCapsFilter * filter)
{
  gst_base_transform_set_passthrough (GST_BASE_TRANSFORM (filter), TRUE);
  filter->filter_caps = gst_caps_new_any ();
}

static void
gst_capsfilter_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstCapsFilter *capsfilter = GST_CAPSFILTER (object);

  switch (prop_id) {
    case PROP_FILTER_CAPS:{
      GstCaps *new_caps = gst_caps_copy (gst_value_get_caps (value));
      GstCaps *old_caps;

      if (new_caps == NULL) {
        new_caps = gst_caps_new_any ();
      }

      old_caps = capsfilter->filter_caps;
      capsfilter->filter_caps = new_caps;
      gst_caps_unref (old_caps);

      /* FIXME: Need to activate these caps on the pads */
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_capsfilter_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstCapsFilter *capsfilter = GST_CAPSFILTER (object);

  switch (prop_id) {
    case PROP_FILTER_CAPS:
      gst_value_set_caps (value, capsfilter->filter_caps);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_capsfilter_dispose (GObject * object)
{
  GstCapsFilter *filter = GST_CAPSFILTER (object);

  gst_caps_replace (&filter->filter_caps, NULL);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static GstCaps *
gst_capsfilter_transform_caps (GstBaseTransform * base, GstPad * pad,
    GstCaps * caps)
{
  GstCapsFilter *capsfilter = GST_CAPSFILTER (base);
  GstCaps *ret;

  ret = gst_caps_intersect (caps, capsfilter->filter_caps);

  return ret;
}

static GstFlowReturn
gst_capsfilter_transform_ip (GstBaseTransform * base, GstBuffer * buf)
{
  return GST_FLOW_OK;
}
