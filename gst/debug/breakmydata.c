/* GStreamer
 * Copyright (C) 2004 Benjamin Otte <otte@gnome.org>
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

#include <gst/gst.h>

GST_DEBUG_CATEGORY_STATIC (gst_break_my_data_debug);
#define GST_CAT_DEFAULT gst_break_my_data_debug

/* This plugin modifies the contents of the buffer it is passed randomly 
 * according to the parameters set.
 * It otherwise acts as an identity.
 */

#define GST_TYPE_BREAK_MY_DATA \
  (gst_break_my_data_get_type())
#define GST_BREAK_MY_DATA(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_BREAK_MY_DATA,GstBreakMyData))
#define GST_BREAK_MY_DATA_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_BREAK_MY_DATA,GstBreakMyDataClass))
#define GST_IS_BREAK_MY_DATA(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_BREAK_MY_DATA))
#define GST_IS_BREAK_MY_DATA_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_BREAK_MY_DATA))

enum
{
  ARG_0,
  ARG_SEED,
  ARG_SET_TO,
  ARG_SKIP,
  ARG_PROBABILITY
};

typedef struct _GstBreakMyData GstBreakMyData;
typedef struct _GstBreakMyDataClass GstBreakMyDataClass;

struct _GstBreakMyData
{
  GstElement element;

  GstPad *sinkpad;
  GstPad *srcpad;

  GRand *rand;
  guint skipped;

  guint32 seed;
  gint set;
  guint skip;
  gdouble probability;
};

struct _GstBreakMyDataClass
{
  GstElementClass parent_class;
};

GST_BOILERPLATE (GstBreakMyData, gst_break_my_data, GstElement,
    GST_TYPE_ELEMENT)

     static void gst_break_my_data_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
     static void gst_break_my_data_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

     static void gst_break_my_data_chain (GstPad * pad, GstData * _data);
     static GstElementStateReturn gst_break_my_data_change_state (GstElement *
    element);

     static void gst_break_my_data_base_init (gpointer g_class)
{
  static GstElementDetails details = GST_ELEMENT_DETAILS ("breakmydata",
      "Testing",
      "randomly change data in the stream",
      "Benjamin Otte <otte@gnome>");
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details (gstelement_class, &details);
}

static void
gst_break_my_data_class_init (GstBreakMyDataClass * klass)
{
  GObjectClass *object = G_OBJECT_CLASS (klass);
  GstElementClass *element = GST_ELEMENT_CLASS (klass);

  object->set_property = GST_DEBUG_FUNCPTR (gst_break_my_data_set_property);
  object->get_property = GST_DEBUG_FUNCPTR (gst_break_my_data_get_property);

  g_object_class_install_property (object, ARG_SEED,
      g_param_spec_uint ("seed", "seed",
          "seed for randomness (initialized when goint from READY to PAUSED)",
          0, 0xFFFFFFFF, 0, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
  g_object_class_install_property (object, ARG_SET_TO,
      g_param_spec_int ("set-to", "set-to",
          "set changed bytes to this value (-1 means random value",
          -1, 255, -1, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
  g_object_class_install_property (object, ARG_SKIP,
      g_param_spec_uint ("skip", "skip",
          "amount of bytes skipped at the beginning of stream",
          0, G_MAXUINT, 0, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
  g_object_class_install_property (object, ARG_PROBABILITY,
      g_param_spec_double ("probability", "probability",
          "probability that a buffer is changed", 0.0, 1.0, 0.0,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

  element->change_state = gst_break_my_data_change_state;
}

static void
gst_break_my_data_init (GstBreakMyData * break_my_data)
{
  break_my_data->sinkpad = gst_pad_new ("sink", GST_PAD_SINK);
  gst_element_add_pad (GST_ELEMENT (break_my_data), break_my_data->sinkpad);
  gst_pad_set_chain_function (break_my_data->sinkpad,
      GST_DEBUG_FUNCPTR (gst_break_my_data_chain));
  gst_pad_set_link_function (break_my_data->sinkpad, gst_pad_proxy_pad_link);
  gst_pad_set_getcaps_function (break_my_data->sinkpad, gst_pad_proxy_getcaps);

  break_my_data->srcpad = gst_pad_new ("src", GST_PAD_SRC);
  gst_element_add_pad (GST_ELEMENT (break_my_data), break_my_data->srcpad);
  gst_pad_set_link_function (break_my_data->srcpad, gst_pad_proxy_pad_link);
  gst_pad_set_getcaps_function (break_my_data->srcpad, gst_pad_proxy_getcaps);
}

static void
gst_break_my_data_chain (GstPad * pad, GstData * data)
{
  GstBuffer *copy = NULL, *buf = GST_BUFFER (data);
  GstBreakMyData *bmd = GST_BREAK_MY_DATA (gst_pad_get_parent (pad));
  guint i, size;

  if (bmd->skipped < bmd->skip) {
    i = bmd->skip - bmd->skipped;
  } else {
    i = 0;
  }
  size = GST_BUFFER_SIZE (buf);
  GST_LOG_OBJECT (bmd,
      "got buffer %p (size %u, timestamp %" G_GUINT64_FORMAT ", offset %"
      G_GUINT64_FORMAT "", buf, size, GST_BUFFER_TIMESTAMP (buf),
      GST_BUFFER_OFFSET (buf));
  for (; i < size; i++) {
    if (g_rand_double_range (bmd->rand, 0, 1) < bmd->probability) {
      guint8 new;

      if (!copy)
        copy = gst_buffer_copy_on_write (buf);
      if (bmd->set < 0) {
        new = g_rand_int_range (bmd->rand, 0, 256);
      } else {
        new = bmd->set;
      }
      GST_INFO_OBJECT (bmd, "changing byte %u from 0x%2X to 0x%2X", i,
          (gint) GST_BUFFER_DATA (copy)[i], (gint) new);
      GST_BUFFER_DATA (copy)[i] = new;
    }
  }
  /* don't overflow */
  bmd->skipped += MIN (G_MAXUINT - bmd->skipped, GST_BUFFER_SIZE (buf));
  gst_pad_push (bmd->srcpad, GST_DATA (copy ? copy : buf));
}

static void
gst_break_my_data_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstBreakMyData *bmd = GST_BREAK_MY_DATA (object);

  switch (prop_id) {
    case ARG_SEED:
      bmd->seed = g_value_get_uint (value);
      break;
    case ARG_SET_TO:
      bmd->set = g_value_get_int (value);
      break;
    case ARG_SKIP:
      bmd->skip = g_value_get_uint (value);
      break;
    case ARG_PROBABILITY:
      bmd->probability = g_value_get_double (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_break_my_data_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstBreakMyData *bmd = GST_BREAK_MY_DATA (object);

  switch (prop_id) {
    case ARG_SEED:
      g_value_set_uint (value, bmd->seed);
      break;
    case ARG_SET_TO:
      g_value_set_int (value, bmd->set);
      break;
    case ARG_SKIP:
      g_value_set_uint (value, bmd->skip);
      break;
    case ARG_PROBABILITY:
      g_value_set_double (value, bmd->probability);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstElementStateReturn
gst_break_my_data_change_state (GstElement * element)
{
  GstBreakMyData *bmd = GST_BREAK_MY_DATA (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_READY_TO_PAUSED:
      bmd->rand = g_rand_new_with_seed (bmd->seed);
      bmd->skipped = 0;
      break;
    case GST_STATE_PAUSED_TO_READY:
      g_rand_free (bmd->rand);
      break;
    default:
      break;
  }

  return GST_CALL_PARENT_WITH_DEFAULT (GST_ELEMENT_CLASS, change_state,
      (element), GST_STATE_SUCCESS);
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  if (!gst_element_register (plugin, "breakmydata", GST_RANK_NONE,
          GST_TYPE_BREAK_MY_DATA))
    return FALSE;

  GST_DEBUG_CATEGORY_INIT (gst_break_my_data_debug, "breakmydata", 0,
      "debugging category for breakmydata element");

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "debug",
    "elements for testing and debugging",
    plugin_init, VERSION, "LGPL", GST_PACKAGE, GST_ORIGIN)
