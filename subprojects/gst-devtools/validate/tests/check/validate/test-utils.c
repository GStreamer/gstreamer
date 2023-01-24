/* GstValidate
 * Copyright (C) 2014 Thibault Saunier <thibault.saunier@collabora.com>
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include "test-utils.h"

typedef struct _DestroyedObjectStruct
{
  GObject *object;
  gboolean destroyed;
} DestroyedObjectStruct;

static void
weak_notify (DestroyedObjectStruct * destroyed, GObject ** object)
{
  destroyed->destroyed = TRUE;
}

void
check_destroyed (gpointer object_to_unref, gpointer first_object, ...)
{
  gint i = 0;
  GObject *object;
  GList *objs = NULL, *tmp;
  DestroyedObjectStruct *destroyed = g_new0 (DestroyedObjectStruct, 1);

  destroyed->object = G_OBJECT (object_to_unref);
  g_object_weak_ref (G_OBJECT (object_to_unref), (GWeakNotify) weak_notify,
      destroyed);
  objs = g_list_prepend (objs, destroyed);

  if (first_object) {
    va_list varargs;

    object = G_OBJECT (first_object);

    va_start (varargs, first_object);
    while (object) {
      destroyed = g_new0 (DestroyedObjectStruct, 1);
      destroyed->object = object;
      g_object_weak_ref (object, (GWeakNotify) weak_notify, destroyed);
      objs = g_list_append (objs, destroyed);
      object = va_arg (varargs, GObject *);
    }
    va_end (varargs);
  }
  gst_object_unref (object_to_unref);

  for (tmp = objs; tmp; tmp = tmp->next) {
    fail_unless (((DestroyedObjectStruct *) tmp->data)->destroyed == TRUE,
        "%p is not destroyed (object nb %i)",
        ((DestroyedObjectStruct *) tmp->data)->object, i);
    g_free (tmp->data);
    i++;
  }
  g_list_free (objs);

}

void
clean_bus (GstElement * element)
{
  GstBus *bus;

  bus = gst_element_get_bus (element);
  gst_bus_set_flushing (bus, TRUE);
  gst_object_unref (bus);
}

GstValidatePadMonitor *
get_pad_monitor (GstPad * pad)
{
  return g_object_get_data ((GObject *) pad, "validate-monitor");
}

static GstStaticPadTemplate fake_demuxer_src_template =
GST_STATIC_PAD_TEMPLATE ("src%u",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS ("something")
    );

static GstStaticPadTemplate fake_demuxer_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("something")
    );

static GstFlowReturn
_demuxer_chain (GstPad * pad, GstObject * self, GstBuffer * buffer)
{
  gst_buffer_unref (buffer);

  return FAKE_DEMUXER (self)->return_value;
}

static void
fake_demuxer_init (FakeDemuxer * self, gpointer * g_class)
{
  GstPad *pad;
  GstElement *element = GST_ELEMENT (self);
  GstPadTemplate *pad_template;

  pad_template =
      gst_element_class_get_pad_template (GST_ELEMENT_CLASS (g_class), "src%u");

  pad = gst_pad_new_from_template (pad_template, "src0");
  gst_element_add_pad (element, pad);

  pad = gst_pad_new_from_template (pad_template, "src1");
  gst_element_add_pad (element, pad);

  pad = gst_pad_new_from_template (pad_template, "src2");
  gst_element_add_pad (element, pad);

  pad_template =
      gst_element_class_get_pad_template (GST_ELEMENT_CLASS (g_class), "sink");
  pad = gst_pad_new_from_template (pad_template, "sink");
  gst_element_add_pad (element, pad);

  self->return_value = GST_FLOW_OK;

  gst_pad_set_chain_function (pad, _demuxer_chain);
}

static void
fake_demuxer_class_init (FakeDemuxerClass * self_class)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (self_class);

  gst_element_class_add_static_pad_template (gstelement_class,
      &fake_demuxer_src_template);
  gst_element_class_add_static_pad_template (gstelement_class,
      &fake_demuxer_sink_template);
  gst_element_class_set_static_metadata (gstelement_class, "Fake Demuxer",
      "Demuxer", "Some demuxer", "Thibault Saunier");
}

GType
fake_demuxer_get_type (void)
{
  static gsize type = 0;

  if (g_once_init_enter (&type)) {
    GType _type;
    static const GTypeInfo info = {
      sizeof (FakeDemuxerClass),
      NULL,
      NULL,
      (GClassInitFunc) fake_demuxer_class_init,
      NULL,
      NULL,
      sizeof (FakeDemuxer),
      0,
      (GInstanceInitFunc) fake_demuxer_init,
    };

    _type = g_type_register_static (GST_TYPE_ELEMENT, "FakeDemuxer", &info, 0);
    g_once_init_leave (&type, _type);
  }
  return type;
}

GstElement *
fake_demuxer_new (void)
{
  return GST_ELEMENT (g_object_new (FAKE_DEMUXER_TYPE, NULL));
}

GstElement *
create_and_monitor_element (const gchar * factoryname, const gchar * name,
    GstValidateRunner * runner)
{
  GstElement *element;
  GstValidateMonitor *monitor;

  element = gst_element_factory_make (factoryname, name);
  if (runner) {
    monitor =
        gst_validate_monitor_factory_create (GST_OBJECT (element), runner,
        NULL);
    fail_unless (GST_IS_VALIDATE_ELEMENT_MONITOR (monitor));
  }

  return element;
}

void
free_element_monitor (GstElement * element)
{
  GstValidateMonitor *monitor;
  monitor =
      (GstValidateMonitor *) g_object_get_data (G_OBJECT (element),
      "validate-monitor");

  g_object_unref (G_OBJECT (monitor));
}

/******************************************
 *          Fake decoder                  *
 ******************************************/
static GstStaticPadTemplate fake_decoder_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS ("video/x-raw")
    );

static GstStaticPadTemplate fake_decoder_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw")
    );

static GstFlowReturn
_decoder_chain (GstPad * pad, GstObject * self, GstBuffer * buffer)
{
  gst_buffer_unref (buffer);

  return FAKE_DECODER (self)->return_value;
}

static void
fake_decoder_init (FakeDecoder * self, gpointer * g_class)
{
  GstPad *pad;
  GstElement *element = GST_ELEMENT (self);
  GstPadTemplate *pad_template;

  pad_template =
      gst_element_class_get_pad_template (GST_ELEMENT_CLASS (g_class), "src");
  pad = gst_pad_new_from_template (pad_template, "src");
  gst_element_add_pad (element, pad);

  pad_template =
      gst_element_class_get_pad_template (GST_ELEMENT_CLASS (g_class), "sink");
  pad = gst_pad_new_from_template (pad_template, "sink");
  gst_element_add_pad (element, pad);

  self->return_value = GST_FLOW_OK;

  gst_pad_set_chain_function (pad, _decoder_chain);
}

static void
fake_decoder_class_init (FakeDecoderClass * self_class)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (self_class);

  gst_element_class_add_static_pad_template (gstelement_class,
      &fake_decoder_src_template);
  gst_element_class_add_static_pad_template (gstelement_class,
      &fake_decoder_sink_template);
  gst_element_class_set_static_metadata (gstelement_class, "Fake Decoder",
      "Decoder", "Some decoder", "Thibault Saunier");
}

GType
fake_decoder_get_type (void)
{
  static gsize type = 0;

  if (g_once_init_enter (&type)) {
    GType _type;
    static const GTypeInfo info = {
      sizeof (FakeDecoderClass),
      NULL,
      NULL,
      (GClassInitFunc) fake_decoder_class_init,
      NULL,
      NULL,
      sizeof (FakeDecoder),
      0,
      (GInstanceInitFunc) fake_decoder_init,
    };

    _type = g_type_register_static (GST_TYPE_ELEMENT, "FakeDecoder", &info, 0);
    g_once_init_leave (&type, _type);
  }
  return type;
}

GstElement *
fake_decoder_new (void)
{
  return GST_ELEMENT (g_object_new (FAKE_DECODER_TYPE, NULL));
}

/******************************************
 *          Fake mixer                    *
 ******************************************/
static GstElementClass *fake_mixer_parent_class = NULL;

static GstStaticPadTemplate fake_mixer_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS ("something")
    );

static GstStaticPadTemplate fake_mixer_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink_%u",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS ("something")
    );

static gboolean
_mixer_event (GstPad * pad, GstObject * obj, GstEvent * event)
{
  FakeMixer *self = FAKE_MIXER (obj);

  switch (event->type) {
    case GST_EVENT_STREAM_START:
      if (g_atomic_int_compare_and_exchange (&self->sent_stream_start, FALSE,
              TRUE)) {
        return gst_pad_event_default (pad, obj, event);
      } else {
        gst_event_unref (event);
        return TRUE;
      }
    case GST_EVENT_SEGMENT:
      if (g_atomic_int_compare_and_exchange (&self->sent_segment, FALSE, TRUE)) {
        return gst_pad_event_default (pad, obj, event);
      } else {
        gst_event_unref (event);
        return TRUE;
      }
    default:
      return gst_pad_event_default (pad, obj, event);
  }
}

static GstFlowReturn
_mixer_chain (GstPad * pad, GstObject * self, GstBuffer * buffer)
{
  gst_buffer_unref (buffer);

  return FAKE_MIXER (self)->return_value;
}

static GstPad *
_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * req_name, const GstCaps * caps)
{
  GstPad *pad;
  GstPadTemplate *pad_template;

  pad_template =
      gst_element_class_get_pad_template (GST_ELEMENT_CLASS (G_OBJECT_GET_CLASS
          (element)), "sink_%u");
  pad = gst_pad_new_from_template (pad_template, req_name);

  gst_pad_set_chain_function (pad, _mixer_chain);
  gst_pad_set_event_function (pad, _mixer_event);

  gst_element_add_pad (element, pad);

  return pad;
}

static void
fake_mixer_init (FakeMixer * self, FakeMixerClass * g_class)
{
  GstPad *pad;
  GstElement *element = GST_ELEMENT (self);
  GstPadTemplate *pad_template;

  pad_template =
      gst_element_class_get_pad_template (GST_ELEMENT_CLASS (g_class), "src");
  pad = gst_pad_new_from_template (pad_template, "src");
  gst_element_add_pad (element, pad);

  self->return_value = GST_FLOW_OK;
}

static void
fake_mixer_class_init (FakeMixerClass * self_class)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (self_class);

  fake_mixer_parent_class = g_type_class_peek_parent (self_class);

  gst_element_class_add_static_pad_template (gstelement_class,
      &fake_mixer_src_template);
  gst_element_class_add_static_pad_template (gstelement_class,
      &fake_mixer_sink_template);
  gst_element_class_set_static_metadata (gstelement_class, "Fake mixer",
      "Mixer", "Some mixer", "Thibault Saunier");

  gstelement_class->request_new_pad = GST_DEBUG_FUNCPTR (_request_new_pad);
}

GType
fake_mixer_get_type (void)
{
  static gsize type = 0;

  if (g_once_init_enter (&type)) {
    GType _type;
    static const GTypeInfo info = {
      sizeof (FakeMixerClass),
      NULL,
      NULL,
      (GClassInitFunc) fake_mixer_class_init,
      NULL,
      NULL,
      sizeof (FakeMixer),
      0,
      (GInstanceInitFunc) fake_mixer_init,
    };

    _type = g_type_register_static (GST_TYPE_ELEMENT, "FakeMixer", &info, 0);
    g_once_init_leave (&type, _type);
  }
  return type;
}

GstElement *
fake_mixer_new (void)
{
  return GST_ELEMENT (g_object_new (FAKE_MIXER_TYPE, NULL));
}

/******************************************
 *              Fake Source               *
 *******************************************/
static GstElementClass *fake_src_parent_class = NULL;

static GstStaticPadTemplate fake_src_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS ("something")
    );

static void
fake_src_init (FakeSrc * self, FakeSrcClass * g_class)
{
  GstPad *pad;
  GstElement *element = GST_ELEMENT (self);
  GstPadTemplate *pad_template;

  pad_template =
      gst_element_class_get_pad_template (GST_ELEMENT_CLASS (g_class), "src");
  pad = gst_pad_new_from_template (pad_template, "src");
  gst_element_add_pad (element, pad);

  self->return_value = GST_FLOW_OK;
}

static void
fake_src_class_init (FakeSrcClass * self_class)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (self_class);

  fake_src_parent_class = g_type_class_peek_parent (self_class);

  gst_element_class_add_static_pad_template (gstelement_class,
      &fake_src_src_template);
  gst_element_class_set_static_metadata (gstelement_class, "Fake src", "Source",
      "Some src", "Thibault Saunier");
}

GType
fake_src_get_type (void)
{
  static gsize type = 0;

  if (g_once_init_enter (&type)) {
    GType _type;
    static const GTypeInfo info = {
      sizeof (FakeSrcClass),
      NULL,
      NULL,
      (GClassInitFunc) fake_src_class_init,
      NULL,
      NULL,
      sizeof (FakeSrc),
      0,
      (GInstanceInitFunc) fake_src_init,
    };

    _type = g_type_register_static (GST_TYPE_ELEMENT, "FakeSrc", &info, 0);
    g_once_init_leave (&type, _type);
  }
  return type;
}

GstElement *
fake_src_new (void)
{
  return GST_ELEMENT (g_object_new (FAKE_SRC_TYPE, NULL));
}


void
fake_elements_register (void)
{
  gst_element_register (NULL, "fakemixer", 0, FAKE_MIXER_TYPE);
  gst_element_register (NULL, "fakedecoder", 0, FAKE_DECODER_TYPE);
  gst_element_register (NULL, "fakedemuxer", 0, FAKE_DEMUXER_TYPE);
  gst_element_register (NULL, "fakesrc2", 0, FAKE_SRC_TYPE);
}
