/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) <2003> David Schleef <ds@schleef.org>
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
#include <gst/audio/gstaudiofilter.h>

typedef struct _GstAudiofilterExample GstAudiofilterExample;
typedef struct _GstAudiofilterExampleClass GstAudiofilterExampleClass;

#define GST_TYPE_AUDIOFILTER_EXAMPLE \
  (gst_audiofilter_example_get_type())
#define GST_AUDIOFILTER_EXAMPLE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_AUDIOFILTER_EXAMPLE,GstAudiofilterExample))
#define GST_AUDIOFILTER_EXAMPLE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_AUDIOFILTER_EXAMPLE,GstAudiofilterExampleClass))
#define GST_IS_AUDIOFILTER_EXAMPLE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_AUDIOFILTER_EXAMPLE))
#define GST_IS_AUDIOFILTER_EXAMPLE_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_AUDIOFILTER_EXAMPLE))

struct _GstAudiofilterExample {
  GstAudiofilter audiofilter;

};

struct _GstAudiofilterExampleClass {
  GstAudiofilterClass parent_class;

};


/* GstAudiofilterExample signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_METHOD,
  /* FILL ME */
};

static void     gst_audiofilter_example_base_init       (gpointer g_class);
static void	gst_audiofilter_example_class_init	(gpointer g_class, gpointer class_data);

static void	gst_audiofilter_example_set_property		(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void	gst_audiofilter_example_get_property		(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);

GType
gst_audiofilter_example_get_type (void)
{
  static GType audiofilter_example_type = 0;

  if (!audiofilter_example_type) {
    static const GTypeInfo audiofilter_example_info = {
      sizeof(GstAudiofilterExampleClass),
      gst_audiofilter_example_base_init,
      NULL,
      gst_audiofilter_example_class_init,
      NULL,
      NULL,
      sizeof(GstAudiofilterExample),
      0,
      NULL,
    };
    audiofilter_example_type = g_type_register_static(GST_TYPE_AUDIOFILTER,
	"GstAudiofilterExample", &audiofilter_example_info, 0);
  }
  return audiofilter_example_type;
}

static void gst_audiofilter_example_base_init (gpointer g_class)
{
  static GstElementDetails audiofilter_example_details = {
    "Audio filter example",
    "Filter/Effect/Audio",
    "Filters audio",
    "David Schleef <ds@schleef.org>"
  };
  GstAudiofilterExampleClass *klass = (GstAudiofilterExampleClass *) g_class;
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_set_details (element_class, &audiofilter_example_details);
}

static void gst_audiofilter_example_class_init (gpointer g_class, gpointer class_data)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstAudiofilterExampleClass *klass;

  klass = (GstAudiofilterExampleClass *)g_class;
  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  gobject_class->set_property = gst_audiofilter_example_set_property;
  gobject_class->get_property = gst_audiofilter_example_get_property;
}

static void
gst_audiofilter_example_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  GstAudiofilterExample *src;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_AUDIOFILTER_EXAMPLE(object));
  src = GST_AUDIOFILTER_EXAMPLE(object);

  GST_DEBUG("gst_audiofilter_example_set_property");
  switch (prop_id) {
    default:
      break;
  }
}

static void
gst_audiofilter_example_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  GstAudiofilterExample *src;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_AUDIOFILTER_EXAMPLE(object));
  src = GST_AUDIOFILTER_EXAMPLE(object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
plugin_init (GstPlugin *plugin)
{
  return TRUE;
}

GST_PLUGIN_DEFINE (
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "gstaudiofilter_example",
  "Audio filter example",
  plugin_init,
  VERSION,
  "LGPL",
  GST_PACKAGE,
  GST_ORIGIN
)
