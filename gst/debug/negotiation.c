/*
 * GStreamer
 * Copyright (C) 1999-2001 Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) 2002 David A. Schleef <ds@schleef.org>
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
#include <gst/gst.h>

#include <string.h>
#include <unistd.h>
#include <sys/mman.h>


#define GST_TYPE_NEGOTIATION \
  (gst_gst_negotiation_get_type())
#define GST_NEGOTIATION(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_NEGOTIATION,GstNegotiation))
#define GST_NEGOTIATION_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_NEGOTIATION,GstNegotiation))
#define GST_IS_NEGOTIATION(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_NEGOTIATION))
#define GST_IS_NEGOTIATION_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_NEGOTIATION))

typedef struct _GstNegotiation GstNegotiation;
typedef struct _GstNegotiationClass GstNegotiationClass;

struct _GstNegotiation
{
  GstElement element;

  GstPad *sinkpad, *srcpad;
};

struct _GstNegotiationClass
{
  GstElementClass parent_class;
};

GType gst_gst_negotiation_get_type (void);


static GstElementDetails plugin_details = {
  "Negotiation",
  "Testing",
  "This element acts like identity, except that one can control how "
      "negotiation works",
  "David A. Schleef <ds@schleef.org>",
};

/* Filter signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0
};

static GstStaticPadTemplate gst_negotiation_sink_factory =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate gst_negotiation_src_factory =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static void gst_negotiation_base_init (gpointer g_class);
static void gst_negotiation_class_init (GstNegotiationClass * klass);
static void gst_negotiation_init (GstNegotiation * filter);

static GstCaps *gst_negotiation_getcaps (GstPad * pad);
static GstPadLinkReturn gst_negotiation_pad_link (GstPad * pad,
    const GstCaps * caps);

static void gst_negotiation_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_negotiation_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static void gst_negotiation_chain (GstPad * pad, GstData * _data);

static GstElementClass *parent_class = NULL;

typedef struct _GstFencedBuffer GstFencedBuffer;
struct _GstFencedBuffer
{
  GstBuffer buffer;
  void *region;
  unsigned int length;
};

GType
gst_gst_negotiation_get_type (void)
{
  static GType plugin_type = 0;

  if (!plugin_type) {
    static const GTypeInfo plugin_info = {
      sizeof (GstNegotiationClass),
      gst_negotiation_base_init,
      NULL,
      (GClassInitFunc) gst_negotiation_class_init,
      NULL,
      NULL,
      sizeof (GstNegotiation),
      0,
      (GInstanceInitFunc) gst_negotiation_init,
    };

    plugin_type = g_type_register_static (GST_TYPE_ELEMENT,
        "GstNegotiation", &plugin_info, 0);
  }
  return plugin_type;
}

static void
gst_negotiation_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_negotiation_sink_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_negotiation_src_factory));
  gst_element_class_set_details (element_class, &plugin_details);
}

static void
gst_negotiation_class_init (GstNegotiationClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  gobject_class->set_property = gst_negotiation_set_property;
  gobject_class->get_property = gst_negotiation_get_property;
}

static void
gst_negotiation_init (GstNegotiation * filter)
{
  filter->sinkpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&gst_negotiation_sink_factory), "sink");
  gst_pad_set_getcaps_function (filter->sinkpad, gst_negotiation_getcaps);
  gst_pad_set_link_function (filter->sinkpad, gst_negotiation_pad_link);
  filter->srcpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&gst_negotiation_src_factory), "src");
  gst_pad_set_getcaps_function (filter->srcpad, gst_negotiation_getcaps);
  gst_pad_set_link_function (filter->srcpad, gst_negotiation_pad_link);

  gst_element_add_pad (GST_ELEMENT (filter), filter->sinkpad);
  gst_element_add_pad (GST_ELEMENT (filter), filter->srcpad);
  gst_pad_set_chain_function (filter->sinkpad, gst_negotiation_chain);
}

static GstCaps *
gst_negotiation_getcaps (GstPad * pad)
{
  GstNegotiation *negotiation = GST_NEGOTIATION (gst_pad_get_parent (pad));
  GstPad *otherpad;
  GstCaps *caps;

  otherpad = (pad == negotiation->sinkpad) ? negotiation->srcpad :
      negotiation->sinkpad;

  caps = gst_pad_get_allowed_caps (otherpad);

  GST_ERROR ("getcaps called on %" GST_PTR_FORMAT ", returning %"
      GST_PTR_FORMAT, pad, caps);

  return caps;
}

static GstPadLinkReturn
gst_negotiation_pad_link (GstPad * pad, const GstCaps * caps)
{
  GstNegotiation *negotiation = GST_NEGOTIATION (gst_pad_get_parent (pad));
  GstPad *otherpad;
  GstPadLinkReturn ret;

  otherpad = (pad == negotiation->sinkpad) ? negotiation->srcpad :
      negotiation->sinkpad;

  ret = gst_pad_try_set_caps (otherpad, caps);

  GST_ERROR ("pad_link called on %" GST_PTR_FORMAT " with caps %"
      GST_PTR_FORMAT ", returning %d", pad, caps, ret);

  return ret;
}

static void
gst_negotiation_chain (GstPad * pad, GstData * _data)
{
  GstNegotiation *negotiation = GST_NEGOTIATION (gst_pad_get_parent (pad));

  gst_pad_push (negotiation->srcpad, _data);
}

static void
gst_negotiation_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstNegotiation *filter;

  g_return_if_fail (GST_IS_NEGOTIATION (object));
  filter = GST_NEGOTIATION (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_negotiation_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstNegotiation *filter;

  g_return_if_fail (GST_IS_NEGOTIATION (object));
  filter = GST_NEGOTIATION (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

gboolean
gst_negotiation_plugin_init (GstPlugin * plugin)
{
  if (!gst_element_register (plugin, "negotiation", GST_RANK_NONE,
          GST_TYPE_NEGOTIATION))
    return FALSE;

  return TRUE;
}
