/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *               2000,2001,2002,2003,2004,2005 Wim Taymans <wim@fluendo.com>
 *
 *
 * gsttee.c: Tee element, one in N out
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
#  include "config.h"
#endif

#include "gsttee.h"

#include <string.h>

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

GST_DEBUG_CATEGORY_STATIC (gst_tee_debug);
#define GST_CAT_DEFAULT gst_tee_debug

static GstElementDetails gst_tee_details =
GST_ELEMENT_DETAILS ("Tee pipe fitting",
    "Generic",
    "1-to-N pipe fitting",
    "Erik Walthinsen <omega@cse.ogi.edu>, "
    "Wim \"Tim\" Taymans <wim@fluendo.com>");

enum
{
  PROP_0,
  PROP_NUM_SRC_PADS,
  PROP_HAS_SINK_LOOP,
  PROP_HAS_CHAIN,
  PROP_SILENT,
  PROP_LAST_MESSAGE
      /* FILL ME */
};

GstStaticPadTemplate tee_src_template = GST_STATIC_PAD_TEMPLATE ("src%d",
    GST_PAD_SRC,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS_ANY);

#define _do_init(bla) \
    GST_DEBUG_CATEGORY_INIT (gst_tee_debug, "tee", 0, "tee element");

GST_BOILERPLATE_FULL (GstTee, gst_tee, GstElement, GST_TYPE_ELEMENT, _do_init);

static GstPad *gst_tee_request_new_pad (GstElement * element,
    GstPadTemplate * temp, const gchar * unused);

static void gst_tee_finalize (GObject * object);
static void gst_tee_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_tee_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstFlowReturn gst_tee_chain (GstPad * pad, GstBuffer * buffer);
static void gst_tee_loop (GstPad * pad);
static gboolean gst_tee_sink_activate_push (GstPad * pad, gboolean active);
static gboolean gst_tee_sink_activate_pull (GstPad * pad, gboolean active);


static void
gst_tee_base_init (gpointer g_class)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&sinktemplate));
  gst_element_class_set_details (gstelement_class, &gst_tee_details);
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&tee_src_template));
}

static void
gst_tee_finalize (GObject * object)
{
  GstTee *tee;

  tee = GST_TEE (object);

  g_free (tee->last_message);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_tee_class_init (GstTeeClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_tee_finalize);
  gobject_class->set_property = GST_DEBUG_FUNCPTR (gst_tee_set_property);
  gobject_class->get_property = GST_DEBUG_FUNCPTR (gst_tee_get_property);

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_NUM_SRC_PADS,
      g_param_spec_int ("num-src-pads", "num-src-pads", "num-src-pads",
          0, G_MAXINT, 0, G_PARAM_READABLE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_HAS_SINK_LOOP,
      g_param_spec_boolean ("has-sink-loop", "has-sink-loop", "has-sink-loop",
          FALSE, G_PARAM_CONSTRUCT | G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_HAS_CHAIN,
      g_param_spec_boolean ("has-chain", "has-chain", "has-chain",
          TRUE, G_PARAM_CONSTRUCT | G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_SILENT,
      g_param_spec_boolean ("silent", "silent", "silent",
          TRUE, G_PARAM_CONSTRUCT | G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_LAST_MESSAGE,
      g_param_spec_string ("last_message", "last_message", "last_message",
          NULL, G_PARAM_READABLE));

  gstelement_class->request_new_pad =
      GST_DEBUG_FUNCPTR (gst_tee_request_new_pad);
}

static void
gst_tee_init (GstTee * tee, GstTeeClass * g_class)
{
  tee->sinkpad = gst_pad_new_from_static_template (&sinktemplate, "sink");
  gst_pad_set_setcaps_function (tee->sinkpad,
      GST_DEBUG_FUNCPTR (gst_pad_proxy_setcaps));
  gst_pad_set_getcaps_function (tee->sinkpad,
      GST_DEBUG_FUNCPTR (gst_pad_proxy_getcaps));
  gst_element_add_pad (GST_ELEMENT (tee), tee->sinkpad);

  tee->last_message = NULL;
}

static void
gst_tee_update_pad_functions (GstTee * tee)
{
  gst_pad_set_activatepush_function (tee->sinkpad,
      GST_DEBUG_FUNCPTR (gst_tee_sink_activate_push));
  gst_pad_set_activatepull_function (tee->sinkpad,
      GST_DEBUG_FUNCPTR (gst_tee_sink_activate_pull));

  if (tee->has_chain)
    gst_pad_set_chain_function (tee->sinkpad,
        GST_DEBUG_FUNCPTR (gst_tee_chain));
  else
    gst_pad_set_chain_function (tee->sinkpad, NULL);
}

static GstPad *
gst_tee_request_new_pad (GstElement * element, GstPadTemplate * templ,
    const gchar * unused)
{
  gchar *name;
  GstPad *srcpad;
  GstTee *tee;

  tee = GST_TEE (element);

  GST_OBJECT_LOCK (tee);
  name = g_strdup_printf ("src%d", tee->pad_counter++);
  GST_OBJECT_UNLOCK (tee);

  srcpad = gst_pad_new_from_template (templ, name);
  g_free (name);

  gst_pad_set_setcaps_function (srcpad,
      GST_DEBUG_FUNCPTR (gst_pad_proxy_setcaps));
  gst_pad_set_getcaps_function (srcpad,
      GST_DEBUG_FUNCPTR (gst_pad_proxy_getcaps));
  gst_element_add_pad (GST_ELEMENT (tee), srcpad);

  return srcpad;
}

static void
gst_tee_set_property (GObject * object, guint prop_id, const GValue * value,
    GParamSpec * pspec)
{
  GstTee *tee = GST_TEE (object);

  GST_OBJECT_LOCK (tee);
  switch (prop_id) {
    case PROP_HAS_SINK_LOOP:
      tee->has_sink_loop = g_value_get_boolean (value);
      gst_tee_update_pad_functions (tee);
      break;
    case PROP_HAS_CHAIN:
      tee->has_chain = g_value_get_boolean (value);
      gst_tee_update_pad_functions (tee);
      break;
    case PROP_SILENT:
      tee->silent = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (tee);
}

static void
gst_tee_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstTee *tee = GST_TEE (object);

  GST_OBJECT_LOCK (tee);
  switch (prop_id) {
    case PROP_NUM_SRC_PADS:
      g_value_set_int (value, GST_ELEMENT (tee)->numsrcpads);
      break;
    case PROP_HAS_SINK_LOOP:
      g_value_set_boolean (value, tee->has_sink_loop);
      break;
    case PROP_HAS_CHAIN:
      g_value_set_boolean (value, tee->has_chain);
      break;
    case PROP_SILENT:
      g_value_set_boolean (value, tee->silent);
      break;
    case PROP_LAST_MESSAGE:
      g_value_set_string (value, tee->last_message);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (tee);
}

typedef struct
{
  GstTee *tee;
  GstBuffer *buffer;
} PushData;

static gboolean
gst_tee_do_push (GstPad * pad, GValue * ret, PushData * data)
{
  GstFlowReturn res;

  if (G_UNLIKELY (!data->tee->silent)) {
    GstTee *tee = data->tee;
    GstBuffer *buf = data->buffer;

    g_free (tee->last_message);
    tee->last_message =
        g_strdup_printf ("chain        ******* (%s:%s)t (%d bytes, %"
        G_GUINT64_FORMAT ") %p", GST_DEBUG_PAD_NAME (pad),
        GST_BUFFER_SIZE (buf), GST_BUFFER_TIMESTAMP (buf), buf);
    g_object_notify (G_OBJECT (tee), "last_message");
  }

  res = gst_pad_push (pad, gst_buffer_ref (data->buffer));
  g_value_set_enum (ret, res);

  gst_object_unref (pad);

  return (res == GST_FLOW_OK);
}

static GstFlowReturn
gst_tee_handle_buffer (GstTee * tee, GstBuffer * buffer)
{
  GstIterator *iter;
  PushData data;
  GValue ret = { 0, };
  GstIteratorResult res;

  tee->offset += GST_BUFFER_SIZE (buffer);

  g_value_init (&ret, GST_TYPE_FLOW_RETURN);
  iter = gst_element_iterate_src_pads (GST_ELEMENT (tee));
  data.tee = tee;
  data.buffer = buffer;

  res = gst_iterator_fold (iter, (GstIteratorFoldFunction) gst_tee_do_push,
      &ret, &data);
  gst_iterator_free (iter);

  gst_buffer_unref (buffer);

  /* no need to unset gvalue */
  return g_value_get_enum (&ret);
}

static GstFlowReturn
gst_tee_chain (GstPad * pad, GstBuffer * buffer)
{
  GstFlowReturn res;
  GstTee *tee;

  tee = GST_TEE (GST_PAD_PARENT (pad));

  res = gst_tee_handle_buffer (tee, buffer);

  return res;
}

#define DEFAULT_SIZE 1024

static void
gst_tee_loop (GstPad * pad)
{
  GstBuffer *buffer;
  GstFlowReturn res;
  GstTee *tee;

  tee = GST_TEE (GST_PAD_PARENT (pad));

  res = gst_pad_pull_range (pad, tee->offset, DEFAULT_SIZE, &buffer);
  if (res != GST_FLOW_OK)
    goto pause_task;

  res = gst_tee_handle_buffer (tee, buffer);
  if (res != GST_FLOW_OK)
    goto pause_task;

  return;

pause_task:
  {
    gst_pad_pause_task (pad);
    return;
  }
}

static gboolean
gst_tee_sink_activate_push (GstPad * pad, gboolean active)
{
  GstTee *tee;

  tee = GST_TEE (GST_OBJECT_PARENT (pad));

  tee->sink_mode = active && GST_ACTIVATE_PUSH;

  if (active) {
    g_return_val_if_fail (tee->has_chain, FALSE);
  }

  return TRUE;
}

/* won't be called until we implement an activate function */
static gboolean
gst_tee_sink_activate_pull (GstPad * pad, gboolean active)
{
  GstTee *tee;

  tee = GST_TEE (GST_OBJECT_PARENT (pad));

  tee->sink_mode = active && GST_ACTIVATE_PULL;

  if (active) {
    g_return_val_if_fail (tee->has_sink_loop, FALSE);
    return gst_pad_start_task (pad, (GstTaskFunction) gst_tee_loop, pad);
  } else {
    return gst_pad_stop_task (pad);
  }
}
