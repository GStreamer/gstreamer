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

/*
 * This file was (probably) generated from
 * $Id$
 * and
 * MAKEFILTERVERSION
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/audio/audio.h>
#include <gst/audio/gstaudiofilter.h>
#include <string.h>

typedef struct _GstAudiofilterTemplate GstAudiofilterTemplate;
typedef struct _GstAudiofilterTemplateClass GstAudiofilterTemplateClass;

#define GST_TYPE_AUDIOFILTER_TEMPLATE \
  (gst_audiofilter_template_get_type())
#define GST_AUDIOFILTER_TEMPLATE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_AUDIOFILTER_TEMPLATE,GstAudiofilterTemplate))
#define GST_AUDIOFILTER_TEMPLATE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_AUDIOFILTER_TEMPLATE,GstAudiofilterTemplateClass))
#define GST_IS_AUDIOFILTER_TEMPLATE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_AUDIOFILTER_TEMPLATE))
#define GST_IS_AUDIOFILTER_TEMPLATE_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_AUDIOFILTER_TEMPLATE))

struct _GstAudiofilterTemplate
{
  GstAudiofilter audiofilter;

};

struct _GstAudiofilterTemplateClass
{
  GstAudiofilterClass parent_class;

};


enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0,
  /* FILL ME */
};

static void gst_audiofilter_template_base_init (gpointer g_class);
static void gst_audiofilter_template_class_init (gpointer g_class,
    gpointer class_data);
static void gst_audiofilter_template_init (GTypeInstance * instance,
    gpointer g_class);

static void gst_audiofilter_template_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_audiofilter_template_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static void gst_audiofilter_template_setup (GstAudiofilter * audiofilter);
static void gst_audiofilter_template_filter (GstAudiofilter * audiofilter,
    GstBuffer * outbuf, GstBuffer * inbuf);
static void gst_audiofilter_template_filter_inplace (GstAudiofilter *
    audiofilter, GstBuffer * buf);

GType
gst_audiofilter_template_get_type (void)
{
  static GType audiofilter_template_type = 0;

  if (!audiofilter_template_type) {
    static const GTypeInfo audiofilter_template_info = {
      sizeof (GstAudiofilterTemplateClass),
      gst_audiofilter_template_base_init,
      NULL,
      gst_audiofilter_template_class_init,
      NULL,
      gst_audiofilter_template_init,
      sizeof (GstAudiofilterTemplate),
      0,
      NULL,
    };

    audiofilter_template_type = g_type_register_static (GST_TYPE_AUDIOFILTER,
        "GstAudiofilterTemplate", &audiofilter_template_info, 0);
  }
  return audiofilter_template_type;
}

static void
gst_audiofilter_template_base_init (gpointer g_class)
{
  static GstElementDetails audiofilter_template_details = {
    "Audio filter template",
    "Filter/Effect/Audio",
    "Filters audio",
    "David Schleef <ds@schleef.org>"
  };
  GstAudiofilterTemplateClass *klass = (GstAudiofilterTemplateClass *) g_class;
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_set_details (element_class, &audiofilter_template_details);

  gst_audiofilter_class_add_pad_templates (GST_AUDIOFILTER_CLASS (g_class),
      gst_caps_from_string (GST_AUDIO_INT_STANDARD_PAD_TEMPLATE_CAPS));
}

static void
gst_audiofilter_template_class_init (gpointer g_class, gpointer class_data)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstAudiofilterTemplateClass *klass;
  GstAudiofilterClass *audiofilter_class;

  klass = (GstAudiofilterTemplateClass *) g_class;
  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  audiofilter_class = (GstAudiofilterClass *) g_class;

#if 0
  g_object_class_install_property (gobject_class, ARG_METHOD,
      g_param_spec_enum ("method", "method", "method",
          GST_TYPE_AUDIOTEMPLATE_METHOD, GST_AUDIOTEMPLATE_METHOD_1,
          G_PARAM_READWRITE));
#endif

  gobject_class->set_property = gst_audiofilter_template_set_property;
  gobject_class->get_property = gst_audiofilter_template_get_property;

  audiofilter_class->setup = gst_audiofilter_template_setup;
  audiofilter_class->filter = gst_audiofilter_template_filter;
  audiofilter_class->filter_inplace = gst_audiofilter_template_filter_inplace;
  audiofilter_class->filter = NULL;
}

static void
gst_audiofilter_template_init (GTypeInstance * instance, gpointer g_class)
{
  //GstAudiofilterTemplate *audiofilter_template = GST_AUDIOFILTER_TEMPLATE (instance);
  //GstAudiofilter *audiofilter = GST_AUDIOFILTER (instance);

  GST_DEBUG ("gst_audiofilter_template_init");

  /* do stuff */

}

static void
gst_audiofilter_template_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstAudiofilterTemplate *src;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_AUDIOFILTER_TEMPLATE (object));
  src = GST_AUDIOFILTER_TEMPLATE (object);

  GST_DEBUG ("gst_audiofilter_template_set_property");
  switch (prop_id) {
    default:
      break;
  }
}

static void
gst_audiofilter_template_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstAudiofilterTemplate *src;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_AUDIOFILTER_TEMPLATE (object));
  src = GST_AUDIOFILTER_TEMPLATE (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  if (!gst_library_load ("gstaudiofilter"))
    return FALSE;

  return gst_element_register (plugin, "audiofiltertemplate", GST_RANK_NONE,
      GST_TYPE_AUDIOFILTER_TEMPLATE);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "gstaudiofilter_template",
    "Audio filter template",
    plugin_init, VERSION, "LGPL", GST_PACKAGE, GST_ORIGIN)

     static void gst_audiofilter_template_setup (GstAudiofilter * audiofilter)
{
  GstAudiofilterTemplate *audiofilter_template;

  g_return_if_fail (GST_IS_AUDIOFILTER_TEMPLATE (audiofilter));
  audiofilter_template = GST_AUDIOFILTER_TEMPLATE (audiofilter);

  /* if any setup needs to be done, do it here */

}

/* You may choose to implement either a copying filter or an
 * in-place filter (or both).  Implementing only one will give
 * full functionality, however, implementing both will cause
 * audiofilter to use the optimal function in every situation,
 * with a minimum of memory copies. */

static void
gst_audiofilter_template_filter (GstAudiofilter * audiofilter,
    GstBuffer * outbuf, GstBuffer * inbuf)
{
  GstAudiofilterTemplate *audiofilter_template;

  g_return_if_fail (GST_IS_AUDIOFILTER_TEMPLATE (audiofilter));
  audiofilter_template = GST_AUDIOFILTER_TEMPLATE (audiofilter);

  /* do something interesting here.  This simply copies the source
   * to the destination. */

  memcpy (GST_BUFFER_DATA (outbuf), GST_BUFFER_DATA (inbuf), audiofilter->size);
}

static void
gst_audiofilter_template_filter_inplace (GstAudiofilter * audiofilter,
    GstBuffer * buf)
{
  GstAudiofilterTemplate *audiofilter_template;

  g_return_if_fail (GST_IS_AUDIOFILTER_TEMPLATE (audiofilter));
  audiofilter_template = GST_AUDIOFILTER_TEMPLATE (audiofilter);

  /* do something interesting here.  This simply copies the source
   * to the destination. */

}
