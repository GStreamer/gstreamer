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

/*#define DEBUG_ENABLED */
#include <gstaudiofilter.h>

#include <string.h>


/* GstAudiofilter signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_METHOD,
  /* FILL ME */
};

static void gst_audiofilter_base_init (gpointer g_class);
static void gst_audiofilter_class_init (gpointer g_class, gpointer class_data);
static void gst_audiofilter_init (GTypeInstance * instance, gpointer g_class);

static void gst_audiofilter_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_audiofilter_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static void gst_audiofilter_chain (GstPad * pad, GstData * _data);
GstCaps *gst_audiofilter_class_get_capslist (GstAudiofilterClass * klass);

static GstElementClass *parent_class = NULL;

GType
gst_audiofilter_get_type (void)
{
  static GType audiofilter_type = 0;

  if (!audiofilter_type) {
    static const GTypeInfo audiofilter_info = {
      sizeof (GstAudiofilterClass),
      gst_audiofilter_base_init,
      NULL,
      gst_audiofilter_class_init,
      NULL,
      NULL,
      sizeof (GstAudiofilter),
      0,
      gst_audiofilter_init,
    };

    audiofilter_type = g_type_register_static (GST_TYPE_ELEMENT,
        "GstAudiofilter", &audiofilter_info, G_TYPE_FLAG_ABSTRACT);
  }
  return audiofilter_type;
}

static void
gst_audiofilter_base_init (gpointer g_class)
{
  static GstElementDetails audiofilter_details = {
    "Audio filter base class",
    "Filter/Effect/Audio",
    "Filters audio",
    "David Schleef <ds@schleef.org>"
  };
  GstAudiofilterClass *klass = (GstAudiofilterClass *) g_class;
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_set_details (element_class, &audiofilter_details);
}

static void
gst_audiofilter_class_init (gpointer g_class, gpointer class_data)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstAudiofilterClass *klass;

  klass = (GstAudiofilterClass *) g_class;
  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  gobject_class->set_property = gst_audiofilter_set_property;
  gobject_class->get_property = gst_audiofilter_get_property;
}

static GstPadLinkReturn
gst_audiofilter_link (GstPad * pad, const GstCaps * caps)
{
  GstAudiofilter *audiofilter;
  GstPadLinkReturn ret;
  GstPadLinkReturn link_ret;
  GstStructure *structure;
  GstAudiofilterClass *audiofilter_class;

  GST_DEBUG ("gst_audiofilter_link");
  audiofilter = GST_AUDIOFILTER (gst_pad_get_parent (pad));
  audiofilter_class = GST_AUDIOFILTER_CLASS (G_OBJECT_GET_CLASS (audiofilter));


  if (pad == audiofilter->srcpad) {
    link_ret = gst_pad_try_set_caps (audiofilter->sinkpad, caps);
  } else {
    link_ret = gst_pad_try_set_caps (audiofilter->srcpad, caps);
  }

  if (GST_PAD_LINK_FAILED (link_ret)) {
    return link_ret;
  }

  structure = gst_caps_get_structure (caps, 0);

  if (strcmp (gst_structure_get_name (structure), "audio/x-raw-int") == 0) {
    ret = gst_structure_get_int (structure, "depth", &audiofilter->depth);
    ret &= gst_structure_get_int (structure, "width", &audiofilter->width);
    ret &=
        gst_structure_get_int (structure, "channels", &audiofilter->channels);
  } else if (strcmp (gst_structure_get_name (structure), "audio/x-raw-float")
      == 0) {

  } else {
    g_assert_not_reached ();
  }
  ret &= gst_structure_get_int (structure, "rate", &audiofilter->rate);

  audiofilter->bytes_per_sample = (audiofilter->width / 8) *
      audiofilter->channels;

  if (audiofilter_class->setup)
    (audiofilter_class->setup) (audiofilter);

  return GST_PAD_LINK_OK;
}

static void
gst_audiofilter_init (GTypeInstance * instance, gpointer g_class)
{
  GstAudiofilter *audiofilter = GST_AUDIOFILTER (instance);
  GstPadTemplate *pad_template;

  GST_DEBUG ("gst_audiofilter_init");

  pad_template =
      gst_element_class_get_pad_template (GST_ELEMENT_CLASS (g_class), "sink");
  g_return_if_fail (pad_template != NULL);
  audiofilter->sinkpad = gst_pad_new_from_template (pad_template, "sink");
  gst_element_add_pad (GST_ELEMENT (audiofilter), audiofilter->sinkpad);
  gst_pad_set_chain_function (audiofilter->sinkpad, gst_audiofilter_chain);
  gst_pad_set_link_function (audiofilter->sinkpad, gst_audiofilter_link);
  gst_pad_set_getcaps_function (audiofilter->sinkpad, gst_pad_proxy_getcaps);

  pad_template =
      gst_element_class_get_pad_template (GST_ELEMENT_CLASS (g_class), "src");
  g_return_if_fail (pad_template != NULL);
  audiofilter->srcpad = gst_pad_new_from_template (pad_template, "src");
  gst_element_add_pad (GST_ELEMENT (audiofilter), audiofilter->srcpad);
  gst_pad_set_link_function (audiofilter->srcpad, gst_audiofilter_link);
  gst_pad_set_getcaps_function (audiofilter->srcpad, gst_pad_proxy_getcaps);

  audiofilter->inited = FALSE;
}

static void
gst_audiofilter_chain (GstPad * pad, GstData * data)
{
  GstBuffer *inbuf = GST_BUFFER (data);
  GstAudiofilter *audiofilter;
  GstBuffer *outbuf;
  GstAudiofilterClass *audiofilter_class;

  GST_DEBUG ("gst_audiofilter_chain");

  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (inbuf != NULL);

  audiofilter = GST_AUDIOFILTER (gst_pad_get_parent (pad));
  //g_return_if_fail (audiofilter->inited);
  audiofilter_class = GST_AUDIOFILTER_CLASS (G_OBJECT_GET_CLASS (audiofilter));

  GST_DEBUG ("gst_audiofilter_chain: got buffer of %d bytes in '%s'",
      GST_BUFFER_SIZE (inbuf), GST_OBJECT_NAME (audiofilter));

  if (audiofilter->passthru) {
    gst_pad_push (audiofilter->srcpad, data);
    return;
  }

  audiofilter->size = GST_BUFFER_SIZE (inbuf);
  audiofilter->n_samples = audiofilter->size / audiofilter->bytes_per_sample;

  if (gst_data_is_writable (data)) {
    if (audiofilter_class->filter_inplace) {
      (audiofilter_class->filter_inplace) (audiofilter, inbuf);
      outbuf = inbuf;
    } else {
      outbuf = gst_buffer_new_and_alloc (GST_BUFFER_SIZE (inbuf));
      GST_BUFFER_DURATION (outbuf) = GST_BUFFER_DURATION (inbuf);
      GST_BUFFER_TIMESTAMP (outbuf) = GST_BUFFER_TIMESTAMP (inbuf);

      (audiofilter_class->filter) (audiofilter, outbuf, inbuf);
      gst_buffer_unref (inbuf);
    }
  } else {
    outbuf = gst_buffer_new_and_alloc (GST_BUFFER_SIZE (inbuf));
    GST_BUFFER_DURATION (outbuf) = GST_BUFFER_DURATION (inbuf);
    GST_BUFFER_TIMESTAMP (outbuf) = GST_BUFFER_TIMESTAMP (inbuf);
    if (audiofilter_class->filter) {
      (audiofilter_class->filter) (audiofilter, outbuf, inbuf);
    } else {
      memcpy (GST_BUFFER_DATA (outbuf), GST_BUFFER_DATA (inbuf),
          GST_BUFFER_SIZE (inbuf));

      (audiofilter_class->filter_inplace) (audiofilter, outbuf);
    }
    gst_buffer_unref (inbuf);
  }

  gst_pad_push (audiofilter->srcpad, GST_DATA (outbuf));
}

static void
gst_audiofilter_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstAudiofilter *src;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_AUDIOFILTER (object));
  src = GST_AUDIOFILTER (object);

  GST_DEBUG ("gst_audiofilter_set_property");
  switch (prop_id) {
    default:
      break;
  }
}

static void
gst_audiofilter_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstAudiofilter *src;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_AUDIOFILTER (object));
  src = GST_AUDIOFILTER (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

void
gst_audiofilter_class_add_pad_templates (GstAudiofilterClass *
    audiofilter_class, const GstCaps * caps)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (audiofilter_class);

  audiofilter_class->caps = gst_caps_copy (caps);

  gst_element_class_add_pad_template (element_class,
      gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
          gst_caps_copy (caps)));

  gst_element_class_add_pad_template (element_class,
      gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
          gst_caps_copy (caps)));
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "gstaudiofilter",
    "Audio filter parent class",
    plugin_init, VERSION, "LGPL", GST_PACKAGE, GST_ORIGIN)
