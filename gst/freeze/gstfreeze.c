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

#include <gst/gst.h>

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
static GstFlowReturn gst_freeze_chain (GstPad * pad, GstBuffer * buffer);
static GstStateChangeReturn gst_freeze_change_state (GstElement * element,
    GstStateChange transition);
static GstFlowReturn gst_freeze_play (GstPad * pad, GstBuffer * buff);
static void gst_freeze_loop (GstPad * pad);
static gboolean gst_freeze_sink_activate (GstPad * sinkpad);
static gboolean gst_freeze_sink_activate_pull (GstPad * sinkpad,
    gboolean active);
static gboolean gst_freeze_sink_event (GstPad * pad, GstEvent * event);
static void gst_freeze_clear_buffer (GstFreeze * freeze);
static void gst_freeze_buffer_free (gpointer data, gpointer user_data);


GST_BOILERPLATE (GstFreeze, gst_freeze, GstElement, GST_TYPE_ELEMENT);

static void
gst_freeze_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_set_details_simple (element_class, "Stream freezer",
      "Generic",
      "Makes a stream from buffers of data",
      "Gergely Nagy <gergely.nagy@neteyes.hu>,"
      " Renato Filho <renato.filho@indt.org.br>");

  gst_element_class_add_static_pad_template (element_class,
      &gst_freeze_sink_template);
  gst_element_class_add_static_pad_template (element_class,
      &gst_freeze_src_template);

}

static void
gst_freeze_class_init (GstFreezeClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

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
gst_freeze_init (GstFreeze * freeze, GstFreezeClass * klass)
{
  freeze->sinkpad =
      gst_pad_new_from_static_template (&gst_freeze_sink_template, "sink");
  gst_element_add_pad (GST_ELEMENT (freeze), freeze->sinkpad);
  gst_pad_set_activate_function (freeze->sinkpad, gst_freeze_sink_activate);
  gst_pad_set_activatepull_function (freeze->sinkpad,
      gst_freeze_sink_activate_pull);
  gst_pad_set_chain_function (freeze->sinkpad, gst_freeze_chain);
  gst_pad_set_getcaps_function (freeze->sinkpad, gst_pad_proxy_getcaps);
  gst_pad_set_event_function (freeze->sinkpad, gst_freeze_sink_event);

  freeze->srcpad =
      gst_pad_new_from_static_template (&gst_freeze_src_template, "src");
  gst_element_add_pad (GST_ELEMENT (freeze), freeze->srcpad);
  gst_pad_set_getcaps_function (freeze->srcpad, gst_pad_proxy_getcaps);

  freeze->timestamp_offset = 0;
  freeze->running_time = 0;
  freeze->current = NULL;
  freeze->max_buffers = 1;
  freeze->on_flush = FALSE;
  freeze->buffers = g_queue_new ();
}

static void
gst_freeze_dispose (GObject * object)
{
  GstFreeze *freeze = GST_FREEZE (object);

  gst_freeze_clear_buffer (freeze);

  g_queue_free (freeze->buffers);

  G_OBJECT_CLASS (parent_class)->dispose (object);
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
gst_freeze_chain (GstPad * pad, GstBuffer * buffer)
{
  return gst_freeze_play (pad, buffer);
}


static GstStateChangeReturn
gst_freeze_change_state (GstElement * element, GstStateChange transition)
{
  GstFreeze *freeze = GST_FREEZE (element);
  GstStateChangeReturn return_val = GST_STATE_CHANGE_SUCCESS;

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_NULL_TO_READY:
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      freeze->timestamp_offset = freeze->running_time = 0;
      break;
    default:
      break;
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return_val =
        GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      freeze->timestamp_offset = freeze->running_time = 0;
      break;
    default:
      break;
  }

  return return_val;
}

static GstFlowReturn
gst_freeze_play (GstPad * pad, GstBuffer * buff)
{
  GstFreeze *freeze;
  GstFlowReturn ret = GST_FLOW_OK;

  freeze = GST_FREEZE (gst_pad_get_parent (pad));

  if (freeze->on_flush) {
    g_object_unref (freeze);
    return GST_FLOW_WRONG_STATE;
  }

  /* If it is working in push mode this function will be called by "_chain"
     and buff will never be NULL. In pull mode this function will be called
     by _loop and buff will be NULL */
  if (!buff) {
    ret =
        gst_pad_pull_range (GST_PAD (freeze->sinkpad), freeze->offset, 4096,
        &buff);
    if (ret != GST_FLOW_OK) {
      gst_object_unref (freeze);
      return ret;
    }

    freeze->offset += GST_BUFFER_SIZE (buff);

  }

  if (g_queue_get_length (freeze->buffers) < freeze->max_buffers ||
      freeze->max_buffers == 0) {
    g_queue_push_tail (freeze->buffers, buff);
    GST_DEBUG_OBJECT (freeze, "accepted buffer %u",
        g_queue_get_length (freeze->buffers) - 1);
  } else {
    gst_buffer_unref (buff);
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

  GST_BUFFER_TIMESTAMP (freeze->current) = freeze->timestamp_offset +
      freeze->running_time;
  freeze->running_time += GST_BUFFER_DURATION (freeze->current);

  gst_buffer_ref (freeze->current);
  ret = gst_pad_push (freeze->srcpad, freeze->current);

  gst_object_unref (freeze);

  return ret;
}

static void
gst_freeze_loop (GstPad * pad)
{
  gst_freeze_play (pad, NULL);
}

static gboolean
gst_freeze_sink_activate (GstPad * sinkpad)
{
  if (gst_pad_check_pull_range (sinkpad)) {
    return gst_pad_activate_pull (sinkpad, TRUE);
  } else {
    return gst_pad_activate_push (sinkpad, TRUE);
  }
}

static gboolean
gst_freeze_sink_activate_pull (GstPad * sinkpad, gboolean active)
{
  gboolean result;

  if (active) {
    /* if we have a scheduler we can start the task */
    result = gst_pad_start_task (sinkpad,
        (GstTaskFunction) gst_freeze_loop, sinkpad);
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
gst_freeze_sink_event (GstPad * pad, GstEvent * event)
{
  gboolean ret = TRUE;
  GstFreeze *freeze = GST_FREEZE (gst_pad_get_parent (pad));


  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
      GST_DEBUG_OBJECT (freeze, "EOS on sink pad %s",
          gst_pad_get_name (GST_PAD (freeze->sinkpad)));
      gst_event_unref (event);
      break;
    case GST_EVENT_NEWSEGMENT:
      /* FALL TROUGH */
    case GST_EVENT_FLUSH_STOP:
      gst_freeze_clear_buffer (freeze);
      /* FALL TROUGH */
    default:
      ret = gst_pad_event_default (GST_PAD (freeze->sinkpad), event);
      break;
  }

  gst_object_unref (freeze);
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
    "freeze",
    "Stream freezer",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN);

/*
 * Local variables:
 * mode: c
 * file-style: k&r
 * c-basic-offset: 2
 * arch-tag: fb0ee62b-cf74-46c0-8e62-93b58bacc0ed
 * End:
 */
