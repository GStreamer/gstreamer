/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *                    2005 Wim Taymans <wim@fluendo.com>
 *
 * gstsignalprocessor.c: 
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

#include <gst/audio/audio.h>
#include "gstsignalprocessor.h"


GST_DEBUG_CATEGORY_STATIC (gst_signal_processor_debug);
#define GST_CAT_DEFAULT gst_signal_processor_debug


static GstStaticCaps template_caps =
GST_STATIC_CAPS (GST_AUDIO_FLOAT_STANDARD_PAD_TEMPLATE_CAPS);

#define GST_TYPE_SIGNAL_PROCESSOR_PAD_TEMPLATE \
    (gst_signal_processor_pad_template_get_type ())
#define GST_SIGNAL_PROCESSOR_PAD_TEMPLATE(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_SIGNAL_PROCESSOR_PAD_TEMPLATE,\
    GstSignalProcessorPadTemplate))
typedef struct _GstSignalProcessorPadTemplate GstSignalProcessorPadTemplate;
typedef GstPadTemplateClass GstSignalProcessorPadTemplateClass;

struct _GstSignalProcessorPadTemplate
{
  GstPadTemplate parent;

  guint index;
};

static GType
gst_signal_processor_pad_template_get_type (void)
{
  static GType type = 0;

  if (!type) {
    static const GTypeInfo info = {
      sizeof (GstSignalProcessorPadTemplateClass), NULL, NULL, NULL, NULL,
      NULL, sizeof (GstSignalProcessorPadTemplate), 0, NULL
    };

    type = g_type_register_static (GST_TYPE_PAD_TEMPLATE,
        "GstSignalProcessorPadTemplate", &info, 0);
  }
  return type;
}

void
gst_signal_processor_class_add_pad_template (GstSignalProcessorClass * klass,
    const gchar * name, GstPadDirection direction, guint index)
{
  GstPadTemplate *new;

  g_return_if_fail (GST_IS_SIGNAL_PROCESSOR_CLASS (klass));
  g_return_if_fail (name != NULL);
  g_return_if_fail (direction == GST_PAD_SRC || direction == GST_PAD_SINK);

  new = g_object_new (gst_signal_processor_pad_template_get_type (),
      "name", name, NULL);

  GST_PAD_TEMPLATE_NAME_TEMPLATE (new) = g_strdup (name);
  GST_PAD_TEMPLATE_DIRECTION (new) = direction;
  GST_PAD_TEMPLATE_PRESENCE (new) = GST_PAD_ALWAYS;
  GST_PAD_TEMPLATE_CAPS (new) = gst_caps_copy (gst_static_caps_get
      (&template_caps));
  GST_SIGNAL_PROCESSOR_PAD_TEMPLATE (new)->index = index;

  gst_element_class_add_pad_template (GST_ELEMENT_CLASS (klass), new);
  gst_object_unref (new);
}


#define GST_TYPE_SIGNAL_PROCESSOR_PAD (gst_signal_processor_pad_get_type ())
#define GST_SIGNAL_PROCESSOR_PAD(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_SIGNAL_PROCESSOR_PAD,\
    GstSignalProcessorPad))
typedef struct _GstSignalProcessorPad GstSignalProcessorPad;
typedef GstPadClass GstSignalProcessorPadClass;

struct _GstSignalProcessorPad
{
  GstPad parent;

  GstBuffer *pen;

  guint index;
};

static GType
gst_signal_processor_pad_get_type (void)
{
  static GType type = 0;

  if (!type) {
    static const GTypeInfo info = {
      sizeof (GstSignalProcessorPadClass), NULL, NULL, NULL, NULL,
      NULL, sizeof (GstSignalProcessorPad), 0, NULL
    };

    type = g_type_register_static (GST_TYPE_PAD,
        "GstSignalProcessorPad", &info, 0);
  }
  return type;
}


GST_BOILERPLATE (GstSignalProcessor, gst_signal_processor, GstElement,
    GST_TYPE_ELEMENT);


static void gst_signal_processor_finalize (GObject * object);
static void gst_signal_processor_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_signal_processor_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static gboolean gst_signal_processor_src_activate_pull (GstPad * pad,
    gboolean active);
static gboolean gst_signal_processor_sink_activate_push (GstPad * pad,
    gboolean active);
static GstElementStateReturn gst_signal_processor_change_state (GstElement *
    element);

static gboolean gst_signal_processor_event (GstPad * pad, GstEvent * event);
static GstFlowReturn gst_signal_processor_getrange (GstPad * pad,
    guint64 offset, guint length, GstBuffer ** buffer);
static GstFlowReturn gst_signal_processor_chain (GstPad * pad,
    GstBuffer * buffer);
static gboolean gst_signal_processor_setcaps (GstPad * pad, GstCaps * caps);


static void
gst_signal_processor_base_init (gpointer g_class)
{
  GST_DEBUG_CATEGORY_INIT (gst_signal_processor_debug, "gst-dsp", 0,
      "signalprocessor element");
}

static void
gst_signal_processor_class_init (GstSignalProcessorClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gstelement_class = GST_ELEMENT_CLASS (klass);

  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_signal_processor_finalize);
  gobject_class->set_property =
      GST_DEBUG_FUNCPTR (gst_signal_processor_set_property);
  gobject_class->get_property =
      GST_DEBUG_FUNCPTR (gst_signal_processor_get_property);

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_signal_processor_change_state);
}

static void
gst_signal_processor_add_pad_from_template (GstSignalProcessor * self,
    GstPadTemplate * templ)
{
  GstPad *new;

  new = g_object_new (GST_TYPE_PAD, "name", GST_OBJECT_NAME (templ),
      "direction", templ->direction, "template", templ, NULL);
  GST_SIGNAL_PROCESSOR_PAD (new)->index =
      GST_SIGNAL_PROCESSOR_PAD_TEMPLATE (templ)->index;

  gst_pad_set_setcaps_function (new,
      GST_DEBUG_FUNCPTR (gst_signal_processor_setcaps));

  if (templ->direction == GST_PAD_SINK) {
    gst_pad_set_event_function (new,
        GST_DEBUG_FUNCPTR (gst_signal_processor_event));
    gst_pad_set_chain_function (new,
        GST_DEBUG_FUNCPTR (gst_signal_processor_chain));
    gst_pad_set_activatepush_function (new,
        GST_DEBUG_FUNCPTR (gst_signal_processor_sink_activate_push));
  } else {
    gst_pad_set_getrange_function (new,
        GST_DEBUG_FUNCPTR (gst_signal_processor_getrange));
    gst_pad_set_activatepull_function (new,
        GST_DEBUG_FUNCPTR (gst_signal_processor_src_activate_pull));
  }

  gst_element_add_pad (GST_ELEMENT (self), new);
}

static void
gst_signal_processor_init (GstSignalProcessor * self)
{
  GstSignalProcessorClass *klass;
  GList *templates;

  klass = GST_SIGNAL_PROCESSOR_GET_CLASS (self);

  GST_DEBUG ("gst_signal_processor_init");

  templates =
      gst_element_class_get_pad_template_list (GST_ELEMENT_GET_CLASS (self));

  while (templates) {
    GstPadTemplate *templ = GST_PAD_TEMPLATE (templates->data);

    gst_signal_processor_add_pad_from_template (self, templ);
    templates = templates->next;
  }

  self->audio_in = g_new0 (gfloat *, klass->num_audio_in);
  self->control_in = g_new0 (gfloat, klass->num_control_in);
  self->audio_out = g_new0 (gfloat *, klass->num_audio_out);
  self->control_out = g_new0 (gfloat, klass->num_control_out);
}

static void
gst_signal_processor_finalize (GObject * object)
{
  GstSignalProcessor *self = GST_SIGNAL_PROCESSOR (object);

  g_free (self->audio_in);
  self->audio_in = NULL;
  g_free (self->control_in);
  self->control_in = NULL;
  g_free (self->audio_out);
  self->audio_out = NULL;
  g_free (self->control_out);
  self->control_out = NULL;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_signal_processor_setcaps (GstPad * pad, GstCaps * caps)
{
  GstSignalProcessor *self;
  GstSignalProcessorClass *klass;

  self = GST_SIGNAL_PROCESSOR (GST_PAD_PARENT (pad));
  klass = GST_SIGNAL_PROCESSOR_GET_CLASS (self);

  if (caps != self->caps) {
    GstStructure *s;
    gint sample_rate, buffer_frames;

    s = gst_caps_get_structure (caps, 0);
    if (!gst_structure_get_int (s, "rate", &sample_rate))
      return FALSE;
    if (!gst_structure_get_int (s, "buffer-frames", &buffer_frames))
      return FALSE;

    if (!klass->setup (self, sample_rate))
      return FALSE;

    self->sample_rate = sample_rate;
    self->buffer_frames = buffer_frames;
  }

  /* FIXME: handle was_active, etc */

  return TRUE;
}

static gboolean
gst_signal_processor_event (GstPad * pad, GstEvent * event)
{
  GstSignalProcessor *self;
  GstSignalProcessorClass *bclass;
  gboolean ret = FALSE;
  gboolean unlock;

  self = GST_SIGNAL_PROCESSOR (GST_PAD_PARENT (pad));
  bclass = GST_SIGNAL_PROCESSOR_GET_CLASS (self);

  if (bclass->event)
    bclass->event (self, event);

  unlock = FALSE;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_STOP:
    case GST_EVENT_EOS:
      GST_STREAM_LOCK (pad);
      unlock = TRUE;
      break;
    default:
      break;
  }
  ret = gst_pad_event_default (pad, event);
  if (unlock)
    GST_STREAM_UNLOCK (pad);

  return ret;
}

static void
gst_signal_processor_process (GstSignalProcessor * self)
{
  GstElement *elem;
  GList *l1, *l2;
  GstSignalProcessorClass *klass;

  g_return_if_fail (self->pending_in == 0);
  g_return_if_fail (self->pending_out == 0);

  /* arrange the output buffers */
  for (l1 = elem->sinkpads, l2 = elem->srcpads; l1 || l2;
      l1 = l1->next, l2 = l2->next) {
    GstSignalProcessorPad *srcpad, *sinkpad;

    if (!l2) {
      /* the output buffers have been covered, yay */
      break;
    } else if (!l1) {
      /* need to alloc some output buffers */
      for (; l2; l2 = l2->next) {
        GstFlowReturn ret;

        srcpad = (GstSignalProcessorPad *) l2->data;

        ret = gst_pad_alloc_buffer (GST_PAD (srcpad), -1, self->buffer_frames,
            GST_PAD_CAPS (srcpad), &srcpad->pen);

        if (ret != GST_FLOW_OK) {
          self->state = ret;
          return;
        } else {
          self->audio_out[srcpad->index] =
              (gfloat *) GST_BUFFER_DATA (srcpad->pen);
          self->pending_out++;
        }
      }
      break;
    } else {
      /* copy input to output */
      sinkpad = (GstSignalProcessorPad *) l1->data;
      srcpad = (GstSignalProcessorPad *) l2->data;

      srcpad->pen = sinkpad->pen;
      sinkpad->pen = NULL;
      self->audio_out[srcpad->index] = (gfloat *) GST_BUFFER_DATA (srcpad->pen);
      self->pending_out++;
    }
  }

  klass = GST_SIGNAL_PROCESSOR_GET_CLASS (self);

  klass->process (self, self->buffer_frames);

  /* free unneeded input buffers */

  for (l1 = elem->sinkpads; l1; l1 = l1->next) {
    GstSignalProcessorPad *sinkpad = (GstSignalProcessorPad *) l1->data;

    if (sinkpad->pen) {
      gst_buffer_unref (sinkpad->pen);
      sinkpad->pen = NULL;
    }
  }
}

static void
gst_signal_processor_pen_buffer (GstSignalProcessor * self, GstPad * pad,
    GstBuffer * buffer)
{
  GstSignalProcessorPad *spad = (GstSignalProcessorPad *) pad;

  if (spad->pen) {
    g_critical ("Pad %s:%s already has penned buffer",
        GST_DEBUG_PAD_NAME (pad));
    gst_buffer_unref (buffer);
    return;
  }

  /* keep the reference */
  spad->pen = buffer;
  self->audio_in[spad->index] = (gfloat *) GST_BUFFER_DATA (buffer);

  g_assert (self->pending_in != 0);

  self->pending_in--;

  if (self->pending_in == 0) {
    gst_signal_processor_process (self);
  }
}

static void
gst_signal_processor_flush (GstSignalProcessor * self)
{
  GList *pads;

  pads = GST_ELEMENT (self)->pads;

  for (pads = GST_ELEMENT (self)->pads; pads; pads = pads->next) {
    GstSignalProcessorPad *spad = (GstSignalProcessorPad *) pads->data;

    if (spad->pen) {
      gst_buffer_unref (spad->pen);
      spad->pen = NULL;
    }
  }
}

static void
gst_signal_processor_do_pulls (GstSignalProcessor * self)
{
  GList *sinkpads;

  /* not threadsafe atm */

  sinkpads = GST_ELEMENT (self)->sinkpads;

  for (; sinkpads; sinkpads = sinkpads->next) {
    GstSignalProcessorPad *spad = (GstSignalProcessorPad *) sinkpads->data;
    GstFlowReturn ret = GST_FLOW_OK;
    GstBuffer *buf;

    if (spad->pen) {
      g_warning ("Unexpectedly full buffer pen for pad %s:%s",
          GST_DEBUG_PAD_NAME (spad));
      continue;
    }

    ret = gst_pad_pull_range (GST_PAD (spad), -1, self->buffer_frames, &buf);

    if (ret != GST_FLOW_OK) {
      self->state = ret;
      gst_signal_processor_flush (self);
      return;
    } else if (!buf) {
      g_critical ("Pull failed to make a buffer!");
      self->state = GST_FLOW_ERROR;
      return;
    } else {
      gst_signal_processor_pen_buffer (self, GST_PAD (spad), buf);
    }
  }

  if (self->pending_in != 0) {
    g_critical ("Something wierd happened...");
    self->state = GST_FLOW_ERROR;
  } else {
    gst_signal_processor_process (self);
  }
}

static GstFlowReturn
gst_signal_processor_getrange (GstPad * pad, guint64 offset,
    guint length, GstBuffer ** buffer)
{
  GstSignalProcessor *self;
  GstSignalProcessorPad *spad = (GstSignalProcessorPad *) pad;
  GstFlowReturn ret;

  self = GST_SIGNAL_PROCESSOR (GST_PAD_PARENT (pad));

  GST_STREAM_LOCK (pad);

  if (spad->pen) {
    *buffer = spad->pen;
    spad->pen = NULL;
    g_assert (self->pending_out != 0);
    self->pending_out--;
    ret = GST_FLOW_OK;
  } else {
    gst_signal_processor_do_pulls (self);
    if (!spad->pen) {
      *buffer = NULL;
      ret = self->state;
    } else {
      *buffer = spad->pen;
      spad->pen = NULL;
      self->pending_out--;
      ret = GST_FLOW_OK;
    }
  }

  GST_STREAM_UNLOCK (pad);

  return ret;
}

static void
gst_signal_processor_do_pushes (GstSignalProcessor * self)
{
  GList *srcpads;

  /* not threadsafe atm */

  srcpads = GST_ELEMENT (self)->srcpads;

  for (; srcpads; srcpads = srcpads->next) {
    GstSignalProcessorPad *spad = (GstSignalProcessorPad *) srcpads->data;
    GstFlowReturn ret = GST_FLOW_OK;

    if (!spad->pen) {
      g_warning ("Unexpectedly empty buffer pen for pad %s:%s",
          GST_DEBUG_PAD_NAME (spad));
      continue;
    }

    ret = gst_pad_push (GST_PAD (spad), spad->pen);

    if (ret != GST_FLOW_OK) {
      self->state = ret;
      gst_signal_processor_flush (self);
      return;
    } else {
      spad->pen = NULL;
      g_assert (self->pending_out > 0);
      self->pending_out--;
    }
  }

  if (self->pending_out != 0) {
    g_critical ("Something wierd happened...");
    self->state = GST_FLOW_ERROR;
  }
}

static GstFlowReturn
gst_signal_processor_chain (GstPad * pad, GstBuffer * buffer)
{
  GstSignalProcessor *self;

  self = GST_SIGNAL_PROCESSOR (GST_PAD_PARENT (pad));

  GST_STREAM_LOCK (pad);

  gst_signal_processor_pen_buffer (self, pad, buffer);

  if (self->pending_in == 0) {
    gst_signal_processor_process (self);

    gst_signal_processor_do_pushes (self);
  }

  GST_STREAM_UNLOCK (pad);

  return self->state;
}

static void
gst_signal_processor_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  /* GstSignalProcessor *self = GST_SIGNAL_PROCESSOR (object); */

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_signal_processor_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  /* GstSignalProcessor *self = GST_SIGNAL_PROCESSOR (object); */

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_signal_processor_sink_activate_push (GstPad * pad, gboolean active)
{
  gboolean result = TRUE;
  GstSignalProcessor *self;
  GstSignalProcessorClass *bclass;

  self = GST_SIGNAL_PROCESSOR (GST_OBJECT_PARENT (pad));
  bclass = GST_SIGNAL_PROCESSOR_GET_CLASS (self);

  if (active) {
    if (self->mode == GST_ACTIVATE_NONE) {
      self->mode = GST_ACTIVATE_PUSH;
      result = TRUE;
    } else if (self->mode == GST_ACTIVATE_PUSH) {
      result = TRUE;
    } else {
      g_warning ("foo");
      result = FALSE;
    }
  } else {
    if (self->mode == GST_ACTIVATE_NONE) {
      result = TRUE;
    } else if (self->mode == GST_ACTIVATE_PUSH) {
      self->mode = GST_ACTIVATE_NONE;
      result = TRUE;
    } else {
      g_warning ("foo");
      result = FALSE;
    }
  }

  return result;
}

static gboolean
gst_signal_processor_src_activate_pull (GstPad * pad, gboolean active)
{
  gboolean result = TRUE;
  GstSignalProcessor *self;
  GstSignalProcessorClass *bclass;

  self = GST_SIGNAL_PROCESSOR (GST_OBJECT_PARENT (pad));
  bclass = GST_SIGNAL_PROCESSOR_GET_CLASS (self);

  if (active) {
    if (self->mode == GST_ACTIVATE_NONE) {
      GList *l;

      for (l = GST_ELEMENT (self)->sinkpads; l; l = l->next)
        result &= gst_pad_activate_pull (pad, active);
      if (result)
        self->mode = GST_ACTIVATE_PULL;
    } else if (self->mode == GST_ACTIVATE_PULL) {
      result = TRUE;
    } else {
      g_warning ("foo");
      result = FALSE;
    }
  } else {
    if (self->mode == GST_ACTIVATE_NONE) {
      result = TRUE;
    } else if (self->mode == GST_ACTIVATE_PULL) {
      GList *l;

      for (l = GST_ELEMENT (self)->sinkpads; l; l = l->next)
        result &= gst_pad_activate_pull (pad, active);
      if (result)
        self->mode = GST_ACTIVATE_NONE;
      result = TRUE;
    } else {
      g_warning ("foo");
      result = FALSE;
    }
  }

  return result;
}

static GstElementStateReturn
gst_signal_processor_change_state (GstElement * element)
{
  /* GstSignalProcessor *self;
     GstSignalProcessorClass *klass; */
  GstElementState transition;
  GstElementStateReturn result;

  /* self = GST_SIGNAL_PROCESSOR (element);
     klass = GST_SIGNAL_PROCESSOR_GET_CLASS (self); */

  transition = GST_STATE_TRANSITION (element);

  switch (transition) {
    case GST_STATE_NULL_TO_READY:
      break;
    case GST_STATE_READY_TO_PAUSED:
      break;
    case GST_STATE_PAUSED_TO_PLAYING:
      break;
    default:
      break;
  }

  result = GST_ELEMENT_CLASS (parent_class)->change_state (element);

  switch (transition) {
    case GST_STATE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_PAUSED_TO_READY:
      break;
    case GST_STATE_READY_TO_NULL:
      /* gst_signal_processor_cleanup (self); */
      break;
    default:
      break;
  }

  return result;
}
