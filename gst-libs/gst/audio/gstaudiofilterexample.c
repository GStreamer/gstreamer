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
 * $Id$
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/audio/audio.h>
#include <gst/audio/gstaudiofilter.h>
#include <string.h>

GST_DEBUG_CATEGORY_STATIC (audio_filter_template_debug);
#define GST_CAT_DEFAULT audio_filter_template_debug

static const GstElementDetails audio_filter_template_details =
GST_ELEMENT_DETAILS ("Audio filter template",
    "Filter/Effect/Audio",
    "Filters audio",
    "David Schleef <ds@schleef.org>");

typedef struct _GstAudioFilterTemplate GstAudioFilterTemplate;
typedef struct _GstAudioFilterTemplateClass GstAudioFilterTemplateClass;

#define GST_TYPE_AUDIO_FILTER_TEMPLATE \
  (gst_audio_filter_template_get_type())
#define GST_AUDIO_FILTER_TEMPLATE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_AUDIO_FILTER_TEMPLATE,GstAudioFilterTemplate))
#define GST_AUDIO_FILTER_TEMPLATE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_AUDIO_FILTER_TEMPLATE,GstAudioFilterTemplateClass))
#define GST_IS_AUDIO_FILTER_TEMPLATE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_AUDIO_FILTER_TEMPLATE))
#define GST_IS_AUDIO_FILTER_TEMPLATE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_AUDIO_FILTER_TEMPLATE))

struct _GstAudioFilterTemplate
{
  GstAudioFilter audiofilter;
};

struct _GstAudioFilterTemplateClass
{
  GstAudioFilterClass parent_class;
};


enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0
      /* FILL ME */
};

GST_BOILERPLATE (GstAudioFilterTemplate, gst_audio_filter_template,
    GstAudioFilter, GST_TYPE_AUDIO_FILTER);

static void gst_audio_filter_template_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_audio_filter_template_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static gboolean gst_audio_filter_template_setup (GstAudioFilter * filter,
    GstRingBufferSpec * spec);
static GstFlowReturn gst_audio_filter_template_filter (GstBaseTransform * bt,
    GstBuffer * outbuf, GstBuffer * inbuf);
static GstFlowReturn
gst_audio_filter_template_filter_inplace (GstBaseTransform * base_transform,
    GstBuffer * buf);

#define ALLOWED_CAPS_STRING \
    GST_AUDIO_INT_STANDARD_PAD_TEMPLATE_CAPS

static void
gst_audio_filter_template_base_init (gpointer g_class)
{
  GstAudioFilterTemplateClass *klass = (GstAudioFilterTemplateClass *) g_class;
  GstAudioFilterClass *audiofilter_class = GST_AUDIO_FILTER_CLASS (g_class);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstCaps *caps;

  gst_element_class_set_details (element_class, &audio_filter_template_details);

  caps = gst_caps_from_string (ALLOWED_CAPS_STRING);
  gst_audio_filter_class_add_pad_templates (audiofilter_class, caps);
  gst_caps_unref (caps);
}

static void
gst_audio_filter_template_class_init (GstAudioFilterTemplateClass * klass)
{
  GObjectClass *gobject_class;
  GstBaseTransformClass *btrans_class;
  GstAudioFilterClass *audio_filter_class;

  gobject_class = (GObjectClass *) klass;
  btrans_class = (GstBaseTransformClass *) klass;
  audio_filter_class = (GstAudioFilterClass *) klass;

#if 0
  g_object_class_install_property (gobject_class, ARG_METHOD,
      g_param_spec_enum ("method", "method", "method",
          GST_TYPE_AUDIOTEMPLATE_METHOD, GST_AUDIOTEMPLATE_METHOD_1,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
#endif

  gobject_class->set_property = gst_audio_filter_template_set_property;
  gobject_class->get_property = gst_audio_filter_template_get_property;

  /* this function will be called whenever the format changes */
  audio_filter_class->setup = gst_audio_filter_template_setup;

  /* here you set up functions to process data (either in place, or from
   * one input buffer to another output buffer); only one is required */
  btrans_class->transform = gst_audio_filter_template_filter;
  btrans_class->transform_ip = gst_audio_filter_template_filter_inplace;
}

static void
gst_audio_filter_template_init (GstAudioFilterTemplate * audio_filter_template,
    GstAudioFilterTemplateClass * g_class)
{
  GST_DEBUG ("init");

  /* do stuff if you need to */
}

static void
gst_audio_filter_template_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstAudioFilterTemplate *filter;

  filter = GST_AUDIO_FILTER_TEMPLATE (object);

  GST_DEBUG ("set  property %u", prop_id);

  GST_OBJECT_LOCK (filter);
  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (filter);
}

static void
gst_audio_filter_template_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstAudioFilterTemplate *filter;

  filter = GST_AUDIO_FILTER_TEMPLATE (object);

  GST_DEBUG ("get  property %u", prop_id);

  GST_OBJECT_LOCK (filter);
  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (filter);
}

static gboolean
gst_audio_filter_template_setup (GstAudioFilter * filter,
    GstRingBufferSpec * spec)
{
  GstAudioFilterTemplate *audio_filter_template;

  audio_filter_template = GST_AUDIO_FILTER_TEMPLATE (filter);

  /* if any setup needs to be done, do it here */

  return TRUE;                  /* it's all good */
}

/* You may choose to implement either a copying filter or an
 * in-place filter (or both).  Implementing only one will give
 * full functionality, however, implementing both will cause
 * audiofilter to use the optimal function in every situation,
 * with a minimum of memory copies. */

static GstFlowReturn
gst_audio_filter_template_filter (GstBaseTransform * base_transform,
    GstBuffer * inbuf, GstBuffer * outbuf)
{
  GstAudioFilterTemplate *audio_filter_template;
  GstAudioFilter *audiofilter;

  audiofilter = GST_AUDIO_FILTER (base_transform);
  audio_filter_template = GST_AUDIO_FILTER_TEMPLATE (base_transform);

  /* do something interesting here.  This simply copies the source
   * to the destination. */

  memcpy (GST_BUFFER_DATA (outbuf), GST_BUFFER_DATA (inbuf),
      GST_BUFFER_SIZE (inbuf));

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_audio_filter_template_filter_inplace (GstBaseTransform * base_transform,
    GstBuffer * buf)
{
  GstAudioFilterTemplate *audio_filter_template;
  GstAudioFilter *audiofilter;

  audiofilter = GST_AUDIO_FILTER (base_transform);
  audio_filter_template = GST_AUDIO_FILTER_TEMPLATE (base_transform);

  /* do something interesting here.  This simply copies the source
   * to the destination. */

  return GST_FLOW_OK;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (audio_filter_template_debug, "audiofilterexample",
      0, "audiofilterexample");

  return gst_element_register (plugin, "audiofilterexample", GST_RANK_NONE,
      GST_TYPE_AUDIO_FILTER_TEMPLATE);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "gstaudio_filter_template",
    "Audio filter template",
    plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN);
