/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *                    2005 Wim Taymans <wim@fluendo.com>
 *
 * gstbasesrc.c: 
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
#include <string.h>

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "gstbasesrc.h"
#include <gst/gstmarshal.h>

#define DEFAULT_BLOCKSIZE	4096

GST_DEBUG_CATEGORY_STATIC (gst_basesrc_debug);
#define GST_CAT_DEFAULT gst_basesrc_debug

/* BaseSrc signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_BLOCKSIZE,
};

static GstElementClass *parent_class = NULL;

static void gst_basesrc_base_init (gpointer g_class);
static void gst_basesrc_class_init (GstBaseSrcClass * klass);
static void gst_basesrc_init (GstBaseSrc * src, gpointer g_class);

GType
gst_basesrc_get_type (void)
{
  static GType basesrc_type = 0;

  if (!basesrc_type) {
    static const GTypeInfo basesrc_info = {
      sizeof (GstBaseSrcClass),
      (GBaseInitFunc) gst_basesrc_base_init,
      NULL,
      (GClassInitFunc) gst_basesrc_class_init,
      NULL,
      NULL,
      sizeof (GstBaseSrc),
      0,
      (GInstanceInitFunc) gst_basesrc_init,
    };

    basesrc_type = g_type_register_static (GST_TYPE_ELEMENT,
        "GstBaseSrc", &basesrc_info, G_TYPE_FLAG_ABSTRACT);
  }
  return basesrc_type;
}

static gboolean gst_basesrc_activate (GstPad * pad, GstActivateMode mode);
static void gst_basesrc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_basesrc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static gboolean gst_basesrc_event_handler (GstPad * pad, GstEvent * event);
static const GstEventMask *gst_basesrc_get_event_mask (GstPad * pad);
static const GstQueryType *gst_basesrc_get_query_types (GstPad * pad);
static gboolean gst_basesrc_query (GstPad * pad, GstQueryType type,
    GstFormat * format, gint64 * value);
static const GstFormat *gst_basesrc_get_formats (GstPad * pad);

static GstElementStateReturn gst_basesrc_change_state (GstElement * element);

static void gst_basesrc_loop (GstPad * pad);
static GstFlowReturn gst_basesrc_get_range (GstPad * pad, guint64 offset,
    guint length, GstBuffer ** buf);

static void
gst_basesrc_base_init (gpointer g_class)
{
  GST_DEBUG_CATEGORY_INIT (gst_basesrc_debug, "basesrc", 0, "basesrc element");
}

static void
gst_basesrc_class_init (GstBaseSrcClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  gobject_class->set_property = GST_DEBUG_FUNCPTR (gst_basesrc_set_property);
  gobject_class->get_property = GST_DEBUG_FUNCPTR (gst_basesrc_get_property);

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_BLOCKSIZE,
      g_param_spec_ulong ("blocksize", "Block size",
          "Size in bytes to read per buffer", 1, G_MAXULONG, DEFAULT_BLOCKSIZE,
          G_PARAM_READWRITE));

  gstelement_class->change_state = GST_DEBUG_FUNCPTR (gst_basesrc_change_state);
}

static void
gst_basesrc_init (GstBaseSrc * basesrc, gpointer g_class)
{
  GstPad *pad;
  GstPadTemplate *pad_template;

  pad_template =
      gst_element_class_get_pad_template (GST_ELEMENT_CLASS (g_class), "src");
  g_return_if_fail (pad_template != NULL);

  pad = gst_pad_new_from_template (pad_template, "src");

  gst_pad_set_activate_function (pad, gst_basesrc_activate);
  gst_pad_set_event_function (pad, gst_basesrc_event_handler);
  gst_pad_set_event_mask_function (pad, gst_basesrc_get_event_mask);
  gst_pad_set_query_function (pad, gst_basesrc_query);
  gst_pad_set_query_type_function (pad, gst_basesrc_get_query_types);
  gst_pad_set_formats_function (pad, gst_basesrc_get_formats);
  gst_pad_set_loop_function (pad, gst_basesrc_loop);
  gst_pad_set_getrange_function (pad, gst_basesrc_get_range);
  gst_element_add_pad (GST_ELEMENT (basesrc), pad);

  basesrc->segment_start = -1;
  basesrc->segment_end = -1;
  basesrc->blocksize = DEFAULT_BLOCKSIZE;
}

static const GstFormat *
gst_basesrc_get_formats (GstPad * pad)
{
  static const GstFormat formats[] = {
    GST_FORMAT_DEFAULT,
    0,
  };

  return formats;
}

static const GstQueryType *
gst_basesrc_get_query_types (GstPad * pad)
{
  static const GstQueryType types[] = {
    GST_QUERY_TOTAL,
    GST_QUERY_POSITION,
    GST_QUERY_START,
    GST_QUERY_SEGMENT_END,
    0,
  };

  return types;
}

static gboolean
gst_basesrc_query (GstPad * pad, GstQueryType type,
    GstFormat * format, gint64 * value)
{
  GstBaseSrc *src = GST_BASESRC (GST_PAD_PARENT (pad));

  switch (type) {
    case GST_QUERY_START:
      *value = src->segment_start;
      break;
    case GST_QUERY_SEGMENT_END:
      *value = src->segment_end;
      break;
    default:
      return FALSE;
  }
  return TRUE;
}

static const GstEventMask *
gst_basesrc_get_event_mask (GstPad * pad)
{
  static const GstEventMask masks[] = {
    {GST_EVENT_SEEK, GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_SEGMENT_LOOP},
    {GST_EVENT_FLUSH, 0},
    {0, 0},
  };

  return masks;
}

static gboolean
gst_basesrc_event_handler (GstPad * pad, GstEvent * event)
{
  GstBaseSrc *src;

  src = GST_BASESRC (GST_PAD_PARENT (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
      src->segment_start = GST_EVENT_SEEK_OFFSET (event);
      src->segment_end = GST_EVENT_SEEK_ENDOFFSET (event);
      src->segment_loop =
          GST_EVENT_SEEK_FLAGS (event) & GST_SEEK_FLAG_SEGMENT_LOOP;
      break;
    case GST_EVENT_FLUSH:
      break;
    default:
      break;
  }
  gst_event_unref (event);

  return TRUE;
}

static void
gst_basesrc_set_property (GObject * object, guint prop_id, const GValue * value,
    GParamSpec * pspec)
{
  GstBaseSrc *src;

  src = GST_BASESRC (object);

  switch (prop_id) {
    case ARG_BLOCKSIZE:
      src->blocksize = g_value_get_int (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_basesrc_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstBaseSrc *src;

  src = GST_BASESRC (object);

  switch (prop_id) {
    case ARG_BLOCKSIZE:
      g_value_set_int (value, src->blocksize);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstFlowReturn
gst_basesrc_get_range_unlocked (GstPad * pad, guint64 offset, guint length,
    GstBuffer ** buf)
{
  GstFlowReturn ret;
  GstBaseSrc *src;
  GstBaseSrcClass *bclass;

  src = GST_BASESRC (GST_OBJECT_PARENT (pad));
  bclass = GST_BASESRC_GET_CLASS (src);

  if (bclass->create)
    ret = bclass->create (src, offset, length, buf);
  else
    ret = GST_FLOW_ERROR;

  return ret;
}

static GstFlowReturn
gst_basesrc_get_range (GstPad * pad, guint64 offset, guint length,
    GstBuffer ** ret)
{
  GstFlowReturn fret;

  GST_STREAM_LOCK (pad);

  fret = gst_basesrc_get_range_unlocked (pad, offset, length, ret);

  GST_STREAM_UNLOCK (pad);

  return fret;
}

static void
gst_basesrc_loop (GstPad * pad)
{
  GstBaseSrc *src;
  GstBuffer *buf = NULL;
  GstFlowReturn ret;

  src = GST_BASESRC (GST_OBJECT_PARENT (pad));

  GST_STREAM_LOCK (pad);

  ret =
      gst_basesrc_get_range_unlocked (pad, src->offset, DEFAULT_BLOCKSIZE,
      &buf);
  if (ret != GST_FLOW_OK)
    goto pause;

  src->offset += GST_BUFFER_SIZE (buf);

  ret = gst_pad_push (pad, buf);
  if (ret != GST_FLOW_OK)
    goto pause;

  GST_STREAM_UNLOCK (pad);
  return;

pause:
  {
    gst_task_pause (GST_RPAD_TASK (pad));
    GST_STREAM_UNLOCK (pad);
    return;
  }
}

static gboolean
gst_basesrc_activate (GstPad * pad, GstActivateMode mode)
{
  gboolean result = FALSE;
  GstBaseSrc *basesrc;

  basesrc = GST_BASESRC (GST_OBJECT_PARENT (pad));

  switch (mode) {
    case GST_ACTIVATE_PUSH:
      /* if we have a scheduler we can start the task */
      if (GST_ELEMENT_SCHEDULER (basesrc)) {
        GST_STREAM_LOCK (pad);
        GST_RPAD_TASK (pad) =
            gst_scheduler_create_task (GST_ELEMENT_SCHEDULER (basesrc),
            (GstTaskFunction) gst_basesrc_loop, pad);

        gst_task_start (GST_RPAD_TASK (pad));
        GST_STREAM_UNLOCK (pad);
        result = TRUE;
      }
      break;
    case GST_ACTIVATE_PULL:
      result = TRUE;
      break;
    case GST_ACTIVATE_NONE:
      /* step 1, unblock clock sync (if any) */

      /* step 2, make sure streaming finishes */
      GST_STREAM_LOCK (pad);
      /* step 3, stop the task */
      if (GST_RPAD_TASK (pad)) {
        gst_task_stop (GST_RPAD_TASK (pad));
        gst_object_unref (GST_OBJECT (GST_RPAD_TASK (pad)));
        GST_RPAD_TASK (pad) = NULL;
      }
      GST_STREAM_UNLOCK (pad);

      result = TRUE;
      break;
  }
  return result;
}

static GstElementStateReturn
gst_basesrc_change_state (GstElement * element)
{
  GstBaseSrc *basesrc;
  GstElementStateReturn result = GST_STATE_FAILURE;
  GstElementState transition;

  basesrc = GST_BASESRC (element);

  transition = GST_STATE_TRANSITION (element);

  switch (transition) {
    case GST_STATE_NULL_TO_READY:
      break;
    case GST_STATE_READY_TO_PAUSED:
    {
      basesrc->offset = 0;
      break;
    }
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
      break;
    default:
      break;
  }

  return result;
}
