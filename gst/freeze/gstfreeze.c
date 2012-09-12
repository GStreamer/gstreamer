/* gst-freeze -- Source freezer
 * Copyright (C) 2005 Gergely Nagy <gergely.nagy@neteyes.hu>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * version 2.1, as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307  USA
 */

/**
 * SECTION:element-freeze
 *
 * Makes a stream from buffers of data.
 * 
 * <refsect2>
 * <title>Example launch line</title>
 * <para>
 * |[
 * gst-launch-0.10 filesrc location=gnome-home.png blocksize=4135 !  pngdec ! freeze ! ffmpegcolorspace ! xvimagesink
 * ]| Produces a video stream from one picture.
 * </para>
 * </refsect2>
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstfreeze.h"

GST_DEBUG_CATEGORY_STATIC (freeze_debug);
#define GST_CAT_DEFAULT freeze_debug

enum
{
  ARG_0,
  ARG_MAX_BUFFERS,
};

static GstStaticPadTemplate gst_freeze_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate gst_freeze_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static void gst_freeze_dispose (GObject * object);
static void gst_freeze_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_freeze_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static GstFlowReturn gst_freeze_chain (GstPad * pad, GstObject * parent,
    GstBuffer * buffer);
static GstStateChangeReturn gst_freeze_change_state (GstElement * element,
    GstStateChange transition);
static GstFlowReturn gst_freeze_play (GstFreeze * freeze, GstBuffer * buff);
static void gst_freeze_loop (GstFreeze * freeze);
static gboolean gst_freeze_sink_activate (GstPad * sinkpad, GstObject * parent);
static gboolean gst_freeze_sink_activate_mode (GstPad * sinkpad,
    GstObject * parent, GstPadMode mode, gboolean active);
static gboolean gst_freeze_sink_event (GstPad * pad, GstObject * parent,
    GstEvent * event);
static void gst_freeze_clear_buffer (GstFreeze * freeze);
static void gst_freeze_buffer_free (gpointer data, gpointer user_data);


G_DEFINE_TYPE (GstFreeze, gst_freeze, GST_TYPE_ELEMENT);

static void
gst_freeze_class_init (GstFreezeClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_set_details_simple (element_class, "Stream freezer",
      "Generic",
      "Makes a stream from buffers of data",
      "Gergely Nagy <gergely.nagy@neteyes.hu>,"
      " Renato Filho <renato.filho@indt.org.br>");

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_freeze_sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_freeze_src_template));

  element_class->change_state = gst_freeze_change_state;
  object_class->set_property = gst_freeze_set_property;
  object_class->get_property = gst_freeze_get_property;

  g_object_class_install_property (object_class,
      ARG_MAX_BUFFERS,
      g_param_spec_uint ("max-buffers",
          "max-buffers",
          "Maximum number of buffers", 0, G_MAXUINT, 1,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  object_class->dispose = gst_freeze_dispose;

}

static void
gst_freeze_init (GstFreeze * freeze)
{
  freeze->sinkpad =
      gst_pad_new_from_static_template (&gst_freeze_sink_template, "sink");
  gst_pad_set_activate_function (freeze->sinkpad, gst_freeze_sink_activate);
  gst_pad_set_activatemode_function (freeze->sinkpad,
      gst_freeze_sink_activate_mode);
  gst_pad_set_chain_function (freeze->sinkpad, gst_freeze_chain);
  GST_PAD_SET_PROXY_CAPS (freeze->sinkpad);
  GST_PAD_SET_PROXY_ALLOCATION (freeze->sinkpad);
  gst_pad_set_event_function (freeze->sinkpad, gst_freeze_sink_event);
  gst_element_add_pad (GST_ELEMENT (freeze), freeze->sinkpad);

  freeze->srcpad =
      gst_pad_new_from_static_template (&gst_freeze_src_template, "src");
  GST_PAD_SET_PROXY_CAPS (freeze->srcpad);
  GST_PAD_SET_PROXY_ALLOCATION (freeze->srcpad);
  GST_PAD_SET_PROXY_SCHEDULING (freeze->srcpad);
  gst_element_add_pad (GST_ELEMENT (freeze), freeze->srcpad);

  freeze->timestamp_offset = 0;
  freeze->running_time = 0;
  freeze->current = NULL;
  freeze->max_buffers = 1;
  freeze->buffers = g_queue_new ();
}

static void
gst_freeze_dispose (GObject * object)
{
  GstFreeze *freeze = GST_FREEZE (object);

  gst_freeze_clear_buffer (freeze);

  g_queue_free (freeze->buffers);

  G_OBJECT_CLASS (gst_freeze_parent_class)->dispose (object);
}

static void
gst_freeze_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstFreeze *freeze = GST_FREEZE (object);

  switch (prop_id) {
    case ARG_MAX_BUFFERS:
      freeze->max_buffers = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_freeze_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstFreeze *freeze = GST_FREEZE (object);

  switch (prop_id) {
    case ARG_MAX_BUFFERS:
      g_value_set_uint (value, freeze->max_buffers);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstFlowReturn
gst_freeze_chain (GstPad * pad, GstObject * parent, GstBuffer * buffer)
{
  GstFreeze *freeze = GST_FREEZE (parent);

  return gst_freeze_play (freeze, buffer);
}


static GstStateChangeReturn
gst_freeze_change_state (GstElement * element, GstStateChange transition)
{
  GstFreeze *freeze = GST_FREEZE (element);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      freeze->timestamp_offset = freeze->running_time = 0;
      break;
    default:
      break;
  }

  return GST_ELEMENT_CLASS (gst_freeze_parent_class)->change_state (element,
      transition);
}

static GstFlowReturn
gst_freeze_play (GstFreeze * freeze, GstBuffer * buf)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GstBuffer *outbuf;

  if (freeze->current == NULL)
    freeze->timestamp_offset = GST_BUFFER_TIMESTAMP (buf);

  if (g_queue_get_length (freeze->buffers) < freeze->max_buffers ||
      freeze->max_buffers == 0) {
    g_queue_push_tail (freeze->buffers, buf);
    GST_DEBUG_OBJECT (freeze, "accepted buffer %u",
        g_queue_get_length (freeze->buffers) - 1);
  } else {
    gst_buffer_unref (buf);
  }


  if (freeze->current != NULL) {
    GST_DEBUG_OBJECT (freeze, "switching to next buffer");
    freeze->current = g_queue_peek_nth (freeze->buffers,
        g_queue_index (freeze->buffers, (gpointer) freeze->current) + 1);
  }

  if (freeze->current == NULL) {
    if (freeze->max_buffers > 1)
      GST_DEBUG_OBJECT (freeze, "restarting the loop");
    freeze->current = g_queue_peek_head (freeze->buffers);
  }

  outbuf = gst_buffer_copy (freeze->current);

  GST_BUFFER_TIMESTAMP (outbuf) = freeze->timestamp_offset +
      freeze->running_time;
  freeze->running_time += GST_BUFFER_DURATION (freeze->current);

  ret = gst_pad_push (freeze->srcpad, outbuf);

  return ret;
}

static void
gst_freeze_loop (GstFreeze * freeze)
{
  GstBuffer *buf;
  GstFlowReturn ret;

  ret = gst_pad_pull_range (freeze->sinkpad, freeze->offset, 4096, &buf);
  if (ret != GST_FLOW_OK)
    return;

  freeze->offset += gst_buffer_get_size (buf);

  gst_freeze_play (freeze, buf);
}

static gboolean
gst_freeze_sink_activate (GstPad * sinkpad, GstObject * parent)
{
  GstQuery *query;
  GstPadMode mode;

  query = gst_query_new_scheduling ();

  if (!gst_pad_peer_query (sinkpad, query))
    goto no_valid_mode;

  if (gst_query_has_scheduling_mode (query, GST_PAD_MODE_PULL))
    mode = GST_PAD_MODE_PULL;
  else if (gst_query_has_scheduling_mode (query, GST_PAD_MODE_PUSH))
    mode = GST_PAD_MODE_PUSH;
  else
    goto no_valid_mode;

  gst_query_unref (query);

  return gst_pad_activate_mode (sinkpad, mode, TRUE);

no_valid_mode:
  return FALSE;
}

static gboolean
gst_freeze_sink_activate_mode (GstPad * sinkpad, GstObject * parent,
    GstPadMode mode, gboolean active)
{
  GstFreeze *freeze = GST_FREEZE (parent);
  gboolean result;

  if (mode != GST_PAD_MODE_PULL)
    return TRUE;

  if (active) {
    /* if we have a scheduler we can start the task */
    result = gst_pad_start_task (sinkpad,
        (GstTaskFunction) gst_freeze_loop, freeze, NULL);
  } else {
    result = gst_pad_stop_task (sinkpad);
  }

  return result;
}

static void
gst_freeze_buffer_free (gpointer data, gpointer user_data)
{
  gst_buffer_unref (GST_BUFFER (data));
}

static void
gst_freeze_clear_buffer (GstFreeze * freeze)
{
  if (freeze->buffers != NULL) {
    g_queue_foreach (freeze->buffers, gst_freeze_buffer_free, NULL);
  }
  freeze->current = NULL;
  freeze->running_time = 0;
}

static gboolean
gst_freeze_sink_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstFreeze *freeze = GST_FREEZE (parent);
  gboolean ret = TRUE;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
      GST_DEBUG_OBJECT (freeze, "EOS on sink pad %s",
          gst_pad_get_name (GST_PAD (freeze->sinkpad)));
      gst_event_unref (event);
      break;
    case GST_EVENT_STREAM_START:
    case GST_EVENT_FLUSH_STOP:
      gst_freeze_clear_buffer (freeze);
      /* FALL TROUGH */
    default:
      ret = gst_pad_event_default (GST_PAD (freeze->sinkpad), parent, event);
      break;
  }

  return ret;

}

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (freeze_debug, "freeze", 0, "Stream freezer");

  return gst_element_register (plugin, "freeze", GST_RANK_NONE,
      GST_TYPE_FREEZE);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    freeze,
    "Stream freezer",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN);
