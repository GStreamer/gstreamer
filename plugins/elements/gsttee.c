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

/**
 * SECTION:element-tee
 * @short_description: 1-to-N pipe fitting
 * @see_also: #GstIdentity
 *
 * Split data to multiple pads.
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

static const GstElementDetails gst_tee_details =
GST_ELEMENT_DETAILS ("Tee pipe fitting",
    "Generic",
    "1-to-N pipe fitting",
    "Erik Walthinsen <omega@cse.ogi.edu>, "
    "Wim \"Tim\" Taymans <wim@fluendo.com>");

#define GST_TYPE_TEE_PULL_MODE (gst_tee_pull_mode_get_type())
static GType
gst_tee_pull_mode_get_type (void)
{
  static GType type = 0;
  static const GEnumValue data[] = {
    {GST_TEE_PULL_MODE_NEVER, "Never activate in pull mode", "never"},
    {GST_TEE_PULL_MODE_SINGLE, "Only one src pad can be active in pull mode",
        "single"},
    {0, NULL, NULL},
  };

  if (!type) {
    type = g_enum_register_static ("GstTeePullMode", data);
  }
  return type;
}

#define DEFAULT_PROP_NUM_SRC_PADS	0
#define DEFAULT_PROP_HAS_SINK_LOOP	FALSE
#define DEFAULT_PROP_HAS_CHAIN		TRUE
#define DEFAULT_PROP_SILENT		TRUE
#define DEFAULT_PROP_LAST_MESSAGE	NULL
#define DEFAULT_PULL_MODE		GST_TEE_PULL_MODE_NEVER

enum
{
  PROP_0,
  PROP_NUM_SRC_PADS,
  PROP_HAS_SINK_LOOP,
  PROP_HAS_CHAIN,
  PROP_SILENT,
  PROP_LAST_MESSAGE,
  PROP_PULL_MODE,
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
static void gst_tee_release_pad (GstElement * element, GstPad * pad);

static void gst_tee_finalize (GObject * object);
static void gst_tee_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_tee_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstFlowReturn gst_tee_chain (GstPad * pad, GstBuffer * buffer);
static GstFlowReturn gst_tee_buffer_alloc (GstPad * pad, guint64 offset,
    guint size, GstCaps * caps, GstBuffer ** buf);
static gboolean gst_tee_sink_activate_push (GstPad * pad, gboolean active);
static gboolean gst_tee_src_check_get_range (GstPad * pad);
static gboolean gst_tee_src_activate_pull (GstPad * pad, gboolean active);
static GstFlowReturn gst_tee_src_get_range (GstPad * pad, guint64 offset,
    guint length, GstBuffer ** buf);


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

  gobject_class = G_OBJECT_CLASS (klass);
  gstelement_class = GST_ELEMENT_CLASS (klass);

  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_tee_finalize);
  gobject_class->set_property = GST_DEBUG_FUNCPTR (gst_tee_set_property);
  gobject_class->get_property = GST_DEBUG_FUNCPTR (gst_tee_get_property);

  g_object_class_install_property (gobject_class, PROP_NUM_SRC_PADS,
      g_param_spec_int ("num-src-pads", "Num Src Pads",
          "The number of source pads", 0, G_MAXINT, DEFAULT_PROP_NUM_SRC_PADS,
          G_PARAM_READABLE));
  g_object_class_install_property (gobject_class, PROP_HAS_SINK_LOOP,
      g_param_spec_boolean ("has-sink-loop", "Has Sink Loop",
          "If the element should spawn a thread (unimplemented and deprecated)",
          DEFAULT_PROP_HAS_SINK_LOOP, G_PARAM_CONSTRUCT | G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_HAS_CHAIN,
      g_param_spec_boolean ("has-chain", "Has Chain",
          "If the element can operate in push mode",
          DEFAULT_PROP_HAS_CHAIN, G_PARAM_CONSTRUCT | G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_SILENT,
      g_param_spec_boolean ("silent", "Silent",
          "Don't produce last_message events", DEFAULT_PROP_SILENT,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_LAST_MESSAGE,
      g_param_spec_string ("last_message", "Last Message",
          "The message describing current status", DEFAULT_PROP_LAST_MESSAGE,
          G_PARAM_READABLE));
  g_object_class_install_property (gobject_class, PROP_PULL_MODE,
      g_param_spec_enum ("pull-mode", "Pull mode",
          "Behavior of tee in pull mode", GST_TYPE_TEE_PULL_MODE,
          DEFAULT_PULL_MODE, G_PARAM_CONSTRUCT | G_PARAM_READWRITE));

  gstelement_class->request_new_pad =
      GST_DEBUG_FUNCPTR (gst_tee_request_new_pad);
  gstelement_class->release_pad = GST_DEBUG_FUNCPTR (gst_tee_release_pad);
}

static void
gst_tee_init (GstTee * tee, GstTeeClass * g_class)
{
  tee->sinkpad = gst_pad_new_from_static_template (&sinktemplate, "sink");
  tee->sink_mode = GST_ACTIVATE_NONE;

  gst_pad_set_setcaps_function (tee->sinkpad,
      GST_DEBUG_FUNCPTR (gst_pad_proxy_setcaps));
  gst_pad_set_getcaps_function (tee->sinkpad,
      GST_DEBUG_FUNCPTR (gst_pad_proxy_getcaps));
  gst_pad_set_bufferalloc_function (tee->sinkpad,
      GST_DEBUG_FUNCPTR (gst_tee_buffer_alloc));
  gst_pad_set_activatepush_function (tee->sinkpad,
      GST_DEBUG_FUNCPTR (gst_tee_sink_activate_push));
  gst_pad_set_chain_function (tee->sinkpad, GST_DEBUG_FUNCPTR (gst_tee_chain));
  gst_element_add_pad (GST_ELEMENT (tee), tee->sinkpad);

  tee->last_message = NULL;
}

static GstPad *
gst_tee_request_new_pad (GstElement * element, GstPadTemplate * templ,
    const gchar * unused)
{
  gchar *name;
  GstPad *srcpad;
  GstTee *tee;
  GstActivateMode mode;
  gboolean res;

  tee = GST_TEE (element);

  GST_OBJECT_LOCK (tee);
  name = g_strdup_printf ("src%d", tee->pad_counter++);

  srcpad = gst_pad_new_from_template (templ, name);
  g_free (name);

  if (tee->allocpad == NULL)
    tee->allocpad = srcpad;

  mode = tee->sink_mode;
  GST_OBJECT_UNLOCK (tee);

  switch (mode) {
    case GST_ACTIVATE_PULL:
      /* we already have a src pad in pull mode, and our pull mode can only be
         SINGLE, so fall through to activate this new pad in push mode */
    case GST_ACTIVATE_PUSH:
      res = gst_pad_activate_push (srcpad, TRUE);
      break;
    default:
      res = TRUE;
      break;
  }

  if (!res)
    goto activate_failed;

  gst_pad_set_setcaps_function (srcpad,
      GST_DEBUG_FUNCPTR (gst_pad_proxy_setcaps));
  gst_pad_set_getcaps_function (srcpad,
      GST_DEBUG_FUNCPTR (gst_pad_proxy_getcaps));
  gst_pad_set_activatepull_function (srcpad,
      GST_DEBUG_FUNCPTR (gst_tee_src_activate_pull));
  gst_pad_set_checkgetrange_function (srcpad,
      GST_DEBUG_FUNCPTR (gst_tee_src_check_get_range));
  gst_pad_set_getrange_function (srcpad,
      GST_DEBUG_FUNCPTR (gst_tee_src_get_range));
  gst_element_add_pad (GST_ELEMENT_CAST (tee), srcpad);

  return srcpad;

  /* ERRORS */
activate_failed:
  {
    GST_OBJECT_LOCK (tee);
    GST_DEBUG_OBJECT (tee, "warning failed to activate request pad");
    if (tee->allocpad == srcpad)
      tee->allocpad = NULL;
    gst_object_unref (srcpad);
    GST_OBJECT_LOCK (tee);
    return NULL;
  }
}

static void
gst_tee_release_pad (GstElement * element, GstPad * pad)
{
  GstTee *tee;

  tee = GST_TEE (element);

  GST_OBJECT_LOCK (tee);
  if (tee->allocpad == pad)
    tee->allocpad = NULL;
  GST_OBJECT_UNLOCK (tee);

  gst_pad_set_active (pad, FALSE);

  gst_element_remove_pad (GST_ELEMENT_CAST (tee), pad);
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
      if (tee->has_sink_loop) {
        g_warning ("tee will never implement has-sink-loop==TRUE");
      }
      break;
    case PROP_HAS_CHAIN:
      tee->has_chain = g_value_get_boolean (value);
      break;
    case PROP_SILENT:
      tee->silent = g_value_get_boolean (value);
      break;
    case PROP_PULL_MODE:
      tee->pull_mode = g_value_get_enum (value);
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
    case PROP_PULL_MODE:
      g_value_set_enum (value, tee->pull_mode);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (tee);
}

static GstFlowReturn
gst_tee_buffer_alloc (GstPad * pad, guint64 offset, guint size,
    GstCaps * caps, GstBuffer ** buf)
{
  GstTee *tee;
  GstFlowReturn res;
  GstPad *allocpad;

  tee = GST_TEE (GST_PAD_PARENT (pad));

  GST_OBJECT_LOCK (tee);
  if ((allocpad = tee->allocpad))
    gst_object_ref (allocpad);
  GST_OBJECT_UNLOCK (tee);

  if (allocpad) {
    res = gst_pad_alloc_buffer (allocpad, offset, size, caps, buf);
    gst_object_unref (allocpad);
  } else {
    res = GST_FLOW_OK;
    *buf = NULL;
  }
  return res;
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
  GstTee *tee = data->tee;

  if (G_UNLIKELY (!data->tee->silent)) {
    GstBuffer *buf = data->buffer;

    GST_OBJECT_LOCK (tee);
    g_free (tee->last_message);
    tee->last_message =
        g_strdup_printf ("chain        ******* (%s:%s)t (%d bytes, %"
        G_GUINT64_FORMAT ") %p", GST_DEBUG_PAD_NAME (pad),
        GST_BUFFER_SIZE (buf), GST_BUFFER_TIMESTAMP (buf), buf);
    GST_OBJECT_UNLOCK (tee);
    g_object_notify (G_OBJECT (tee), "last_message");
  }

  /* Push */
  if (pad == data->tee->pull_pad) {
    res = GST_FLOW_OK;
  } else {
    res = gst_pad_push (pad, gst_buffer_ref (data->buffer));
    GST_LOG_OBJECT (tee, "Pushing buffer %p to %" GST_PTR_FORMAT
        " yielded result=%d", data->buffer, pad, res);
  }

  /* If it's fatal or OK, or if ret is currently
   * not-linked, we overwrite the previous value */
  if (GST_FLOW_IS_FATAL (res) || (res == GST_FLOW_OK) ||
      (g_value_get_enum (ret) == GST_FLOW_NOT_LINKED)) {
    GST_LOG_OBJECT (tee, "Replacing ret val %d with %d",
        g_value_get_enum (ret), res);
    g_value_set_enum (ret, res);
  }

  gst_object_unref (pad);

  /* Stop iterating if flow return is fatal */
  return (!GST_FLOW_IS_FATAL (res));
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
  g_value_set_enum (&ret, GST_FLOW_NOT_LINKED);
  iter = gst_element_iterate_src_pads (GST_ELEMENT (tee));
  data.tee = tee;
  data.buffer = buffer;

  GST_LOG_OBJECT (tee, "Starting to push buffer %p", buffer);
  /* FIXME: Not sure how tee would handle RESEND buffer from some of the
   * pads but not from others. */
  res = gst_iterator_fold (iter, (GstIteratorFoldFunction) gst_tee_do_push,
      &ret, &data);
  gst_iterator_free (iter);

  GST_LOG_OBJECT (tee, "Pushing buffer %p yielded result=%d", buffer,
      g_value_get_enum (&ret));

  gst_buffer_unref (buffer);

  /* no need to unset gvalue */
  return g_value_get_enum (&ret);
}

static GstFlowReturn
gst_tee_chain (GstPad * pad, GstBuffer * buffer)
{
  GstFlowReturn res;
  GstTee *tee;

  tee = GST_TEE (gst_pad_get_parent (pad));

  res = gst_tee_handle_buffer (tee, buffer);

  gst_object_unref (tee);

  return res;
}

static gboolean
gst_tee_sink_activate_push (GstPad * pad, gboolean active)
{
  GstTee *tee;

  tee = GST_TEE (GST_OBJECT_PARENT (pad));

  GST_OBJECT_LOCK (tee);
  tee->sink_mode = active && GST_ACTIVATE_PUSH;

  if (active && !tee->has_chain)
    goto no_chain;
  GST_OBJECT_UNLOCK (tee);

  return TRUE;

  /* ERRORS */
no_chain:
  {
    GST_OBJECT_UNLOCK (tee);
    GST_INFO_OBJECT (tee,
        "Tee cannot operate in push mode with has-chain==FALSE");
    return FALSE;
  }
}

static gboolean
gst_tee_src_activate_pull (GstPad * pad, gboolean active)
{
  GstTee *tee;
  gboolean res;
  GstPad *sinkpad;

  tee = GST_TEE (gst_pad_get_parent (pad));

  GST_OBJECT_LOCK (tee);

  if (tee->pull_mode == GST_TEE_PULL_MODE_NEVER)
    goto cannot_pull;

  if (tee->pull_mode == GST_TEE_PULL_MODE_SINGLE && active && tee->pull_pad)
    goto cannot_pull_multiple_srcs;

  sinkpad = gst_object_ref (tee->sinkpad);

  GST_OBJECT_UNLOCK (tee);

  res = gst_pad_activate_pull (sinkpad, active);
  gst_object_unref (sinkpad);

  if (!res)
    goto sink_activate_failed;

  GST_OBJECT_LOCK (tee);
  if (active) {
    if (tee->pull_mode == GST_TEE_PULL_MODE_SINGLE)
      tee->pull_pad = pad;
  } else {
    if (pad == tee->pull_pad)
      tee->pull_pad = NULL;
  }
  tee->sink_mode = active && GST_ACTIVATE_PULL;
  GST_OBJECT_UNLOCK (tee);

  gst_object_unref (tee);

  return res;

  /* ERRORS */
cannot_pull:
  {
    GST_OBJECT_UNLOCK (tee);
    GST_INFO_OBJECT (tee, "Cannot activate in pull mode, pull-mode "
        "set to NEVER");
    gst_object_unref (tee);
    return FALSE;
  }
cannot_pull_multiple_srcs:
  {
    GST_OBJECT_UNLOCK (tee);
    GST_INFO_OBJECT (tee, "Cannot activate multiple src pads in pull mode, "
        "pull-mode set to SINGLE");
    gst_object_unref (tee);
    return FALSE;
  }
sink_activate_failed:
  {
    GST_INFO_OBJECT (tee, "Failed to %sactivate sink pad in pull mode",
        active ? "" : "de");
    gst_object_unref (tee);
    return FALSE;
  }
}

static gboolean
gst_tee_src_check_get_range (GstPad * pad)
{
  GstTee *tee;
  gboolean res;
  GstPad *sinkpad;

  tee = GST_TEE (gst_pad_get_parent (pad));

  GST_OBJECT_LOCK (tee);

  if (tee->pull_mode == GST_TEE_PULL_MODE_NEVER)
    goto cannot_pull;

  if (tee->pull_mode == GST_TEE_PULL_MODE_SINGLE && tee->pull_pad)
    goto cannot_pull_multiple_srcs;

  sinkpad = gst_object_ref (tee->sinkpad);

  GST_OBJECT_UNLOCK (tee);

  res = gst_pad_check_pull_range (sinkpad);
  gst_object_unref (sinkpad);

  gst_object_unref (tee);

  return res;

  /* ERRORS */
cannot_pull:
  {
    GST_OBJECT_UNLOCK (tee);
    GST_INFO_OBJECT (tee, "Cannot activate in pull mode, pull-mode "
        "set to NEVER");
    gst_object_unref (tee);
    return FALSE;
  }
cannot_pull_multiple_srcs:
  {
    GST_OBJECT_UNLOCK (tee);
    GST_INFO_OBJECT (tee, "Cannot activate multiple src pads in pull mode, "
        "pull-mode set to SINGLE");
    gst_object_unref (tee);
    return FALSE;
  }
}

static void
gst_tee_push_eos (GstPad * pad, GstTee * tee)
{
  if (pad != tee->pull_pad)
    gst_pad_push_event (pad, gst_event_new_eos ());
  gst_object_unref (pad);
}

static void
gst_tee_pull_eos (GstTee * tee)
{
  GstIterator *iter;

  iter = gst_element_iterate_src_pads (GST_ELEMENT (tee));
  gst_iterator_foreach (iter, (GFunc) gst_tee_push_eos, tee);
  gst_iterator_free (iter);
}

static GstFlowReturn
gst_tee_src_get_range (GstPad * pad, guint64 offset, guint length,
    GstBuffer ** buf)
{
  GstTee *tee;
  GstFlowReturn ret;

  tee = GST_TEE (gst_pad_get_parent (pad));

  ret = gst_pad_pull_range (tee->sinkpad, offset, length, buf);

  if (ret == GST_FLOW_OK)
    ret = gst_tee_handle_buffer (tee, gst_buffer_ref (*buf));
  else if (ret == GST_FLOW_UNEXPECTED)
    gst_tee_pull_eos (tee);

  gst_object_unref (tee);

  return ret;
}
