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


#include <stdlib.h>

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "../gst-i18n-lib.h"
#include <gst/gstmarshal.h>
#include <gst/gst.h>


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
  GstElement element;

  GstPad *srcpad;
  GstPad *sinkpad;

  GstCaps *filter_caps;
};

struct _GstCapsFilterClass
{
  GstElementClass element_class;

};

GType gst_capsfilter_get_type (void);



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

GstElementDetails gst_capsfilter_details = GST_ELEMENT_DETAILS ("CapsFilter",
    "Generic",
    "Pass data without modification, limiting formats",
    "David Schleef <ds@schleef.org>");

enum
{
  PROP_0,
  PROP_FILTER_CAPS
};


#define _do_init(bla) \
    GST_DEBUG_CATEGORY_INIT (gst_capsfilter_debug, "capsfilter", 0, "capsfilter element");

GST_BOILERPLATE_FULL (GstCapsFilter, gst_capsfilter, GstElement,
    GST_TYPE_ELEMENT, _do_init);

static void gst_capsfilter_finalize (GObject * object);
static void gst_capsfilter_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_capsfilter_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstCaps *gst_capsfilter_getcaps (GstPad * pad);
static GstFlowReturn gst_capsfilter_chain (GstPad * pad, GstBuffer * buf);

static void
gst_capsfilter_base_init (gpointer g_class)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&srctemplate));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&sinktemplate));
  gst_element_class_set_details (gstelement_class, &gst_capsfilter_details);
}

static void
gst_capsfilter_finalize (GObject * object)
{
  GstCapsFilter *capsfilter;

  capsfilter = GST_CAPSFILTER (object);

  gst_caps_unref (capsfilter->filter_caps);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_capsfilter_class_init (GstCapsFilterClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gstelement_class = GST_ELEMENT_CLASS (klass);

  gobject_class->set_property = GST_DEBUG_FUNCPTR (gst_capsfilter_set_property);
  gobject_class->get_property = GST_DEBUG_FUNCPTR (gst_capsfilter_get_property);
  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_capsfilter_finalize);

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_FILTER_CAPS,
      g_param_spec_boxed ("filter_caps", _("Filter caps"),
          _("Caps to use to filter the possible allowed formats"),
          GST_TYPE_CAPS, G_PARAM_READWRITE));
}

static void
gst_capsfilter_init (GstCapsFilter * capsfilter)
{
  gst_element_create_all_pads (GST_ELEMENT (capsfilter));

  capsfilter->srcpad = gst_element_get_pad (GST_ELEMENT (capsfilter), "src");
  capsfilter->sinkpad = gst_element_get_pad (GST_ELEMENT (capsfilter), "sink");

  gst_pad_set_getcaps_function (capsfilter->srcpad, gst_capsfilter_getcaps);

  gst_pad_set_getcaps_function (capsfilter->sinkpad, gst_capsfilter_getcaps);
  gst_pad_set_chain_function (capsfilter->sinkpad, gst_capsfilter_chain);

  capsfilter->filter_caps = gst_caps_new_any ();
}

static GstCaps *
gst_capsfilter_getcaps (GstPad * pad)
{
  GstPad *otherpad;
  GstCapsFilter *capsfilter = GST_CAPSFILTER (GST_OBJECT_PARENT (pad));
  GstCaps *caps;
  GstCaps *icaps;

  otherpad = (pad == capsfilter->srcpad) ? capsfilter->sinkpad :
      capsfilter->srcpad;

  caps = gst_pad_peer_get_caps (otherpad);
  if (caps == NULL)
    caps = gst_caps_new_any ();

  icaps = gst_caps_intersect (caps, capsfilter->filter_caps);
  gst_caps_unref (caps);

  return icaps;
}

static GstFlowReturn
gst_capsfilter_chain (GstPad * pad, GstBuffer * buf)
{
  GstCapsFilter *capsfilter = GST_CAPSFILTER (GST_PAD_PARENT (pad));

  gst_pad_push (capsfilter->srcpad, buf);

  return GST_FLOW_OK;
}

static void
gst_capsfilter_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstCapsFilter *capsfilter;

  capsfilter = GST_CAPSFILTER (object);

  switch (prop_id) {
    case PROP_FILTER_CAPS:
      capsfilter->filter_caps = gst_caps_copy (gst_value_get_caps (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_capsfilter_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstCapsFilter *capsfilter;

  capsfilter = GST_CAPSFILTER (object);

  switch (prop_id) {
    case PROP_FILTER_CAPS:
      gst_value_set_caps (value, capsfilter->filter_caps);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}
