/*
 * GStreamer
 * Copyright (C) 1999-2001 Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) 2004 David A. Schleef <ds@schleef.org>
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


#define GST_TYPE_SUNAUDIOSINK \
  (gst_gst_sunaudiosink_get_type())
#define GST_SUNAUDIOSINK(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_SUNAUDIOSINK,GstSunAudioSink))
#define GST_SUNAUDIOSINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_SUNAUDIOSINK,GstSunAudioSink))
#define GST_IS_SUNAUDIOSINK(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_SUNAUDIOSINK))
#define GST_IS_SUNAUDIOSINK_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_SUNAUDIOSINK))

typedef struct _GstSunAudioSink GstSunAudioSink;
typedef struct _GstSunAudioSinkClass GstSunAudioSinkClass;

struct _GstSunAudioSink
{
  GstElement element;

  GstPad *sinkpad;
};

struct _GstSunAudioSinkClass
{
  GstElementClass parent_class;
};

GType gst_gst_sunaudiosink_get_type (void);


static GstElementDetails plugin_details = {
  "SunAudioSink",
  "Sink/Audio",
  "This element acts like identity, except that one can control how "
      "sunaudiosink works",
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

static GstStaticPadTemplate gst_sunaudiosink_sink_factory =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static void gst_sunaudiosink_base_init (gpointer g_class);
static void gst_sunaudiosink_class_init (GstSunAudioSinkClass * klass);
static void gst_sunaudiosink_init (GstSunAudioSink * filter);

//static GstCaps *gst_sunaudiosink_getcaps (GstPad * pad);
static GstPadLinkReturn gst_sunaudiosink_pad_link (GstPad * pad,
    const GstCaps * caps);

static void gst_sunaudiosink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_sunaudiosink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static void gst_sunaudiosink_chain (GstPad * pad, GstData * _data);

static GstElementClass *parent_class = NULL;

typedef struct _GstFencedBuffer GstFencedBuffer;
struct _GstFencedBuffer
{
  GstBuffer buffer;
  void *region;
  unsigned int length;
};

GType
gst_gst_sunaudiosink_get_type (void)
{
  static GType plugin_type = 0;

  if (!plugin_type) {
    static const GTypeInfo plugin_info = {
      sizeof (GstSunAudioSinkClass),
      gst_sunaudiosink_base_init,
      NULL,
      (GClassInitFunc) gst_sunaudiosink_class_init,
      NULL,
      NULL,
      sizeof (GstSunAudioSink),
      0,
      (GInstanceInitFunc) gst_sunaudiosink_init,
    };

    plugin_type = g_type_register_static (GST_TYPE_ELEMENT,
        "GstSunAudioSink", &plugin_info, 0);
  }
  return plugin_type;
}

static void
gst_sunaudiosink_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_sunaudiosink_sink_factory));
  gst_element_class_set_details (element_class, &plugin_details);
}

static void
gst_sunaudiosink_class_init (GstSunAudioSinkClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  gobject_class->set_property = gst_sunaudiosink_set_property;
  gobject_class->get_property = gst_sunaudiosink_get_property;
}

static void
gst_sunaudiosink_init (GstSunAudioSink * filter)
{
  filter->sinkpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&gst_sunaudiosink_sink_factory), "sink");
  //gst_pad_set_getcaps_function (filter->sinkpad, gst_sunaudiosink_getcaps);
  gst_pad_set_link_function (filter->sinkpad, gst_sunaudiosink_pad_link);

  gst_element_add_pad (GST_ELEMENT (filter), filter->sinkpad);
  gst_pad_set_chain_function (filter->sinkpad, gst_sunaudiosink_chain);
}

#if 0
static GstCaps *
gst_sunaudiosink_getcaps (GstPad * pad)
{
  GstSunAudioSink *sunaudiosink = GST_SUNAUDIOSINK (gst_pad_get_parent (pad));
  GstPad *otherpad;
  GstCaps *caps;

  caps = gst_pad_get_allowed_caps (otherpad);

  GST_ERROR ("getcaps called on %" GST_PTR_FORMAT ", returning %"
      GST_PTR_FORMAT, pad, caps);

  return caps;
}
#endif

static GstPadLinkReturn
gst_sunaudiosink_pad_link (GstPad * pad, const GstCaps * caps)
{
  //GstSunAudioSink *sunaudiosink = GST_SUNAUDIOSINK (gst_pad_get_parent (pad));
  //GstPad *otherpad;
  GstPadLinkReturn ret;

  ret = GST_PAD_LINK_REFUSED;
  GST_ERROR ("pad_link called on %" GST_PTR_FORMAT " with caps %"
      GST_PTR_FORMAT ", returning %d", pad, caps, ret);

  return ret;
}

static void
gst_sunaudiosink_chain (GstPad * pad, GstData * _data)
{
  //GstSunAudioSink *sunaudiosink = GST_SUNAUDIOSINK (gst_pad_get_parent (pad));

}

#if 0
static gboolean
gst_sunaudiosink_open (GstSunAudioSink * sunaudiosink)
{
  const char *file;

  file = "/dev/audio";


  return FALSE;
}
#endif

static void
gst_sunaudiosink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstSunAudioSink *filter;

  g_return_if_fail (GST_IS_SUNAUDIOSINK (object));
  filter = GST_SUNAUDIOSINK (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_sunaudiosink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstSunAudioSink *filter;

  g_return_if_fail (GST_IS_SUNAUDIOSINK (object));
  filter = GST_SUNAUDIOSINK (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

gboolean
gst_sunaudiosink_plugin_init (GstPlugin * plugin)
{
  if (!gst_element_register (plugin, "sunaudiosink", GST_RANK_NONE,
          GST_TYPE_SUNAUDIOSINK))
    return FALSE;

  return TRUE;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  if (!gst_element_register (plugin, "sunaudiosink", GST_RANK_NONE,
          GST_TYPE_SUNAUDIOSINK))
    return FALSE;

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "sunaudio",
    "elements for SunAudio",
    plugin_init, VERSION, "LGPL", GST_PACKAGE, GST_ORIGIN)
