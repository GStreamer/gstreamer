/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wim.taymans@chello.be>
 *
 * gstaggregator.c: Aggregator element, N in 1 out
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

#include "gstaggregator.h"

GST_DEBUG_CATEGORY_STATIC (gst_aggregator_debug);
#define GST_CAT_DEFAULT gst_aggregator_debug

GstElementDetails gst_aggregator_details =
GST_ELEMENT_DETAILS ("Aggregator pipe fitting",
    "Generic",
    "N-to-1 pipe fitting",
    "Wim Taymans <wim.taymans@chello.be>");

/* Aggregator signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_NUM_PADS,
  ARG_SILENT,
  ARG_SCHED,
  ARG_LAST_MESSAGE,
  /* FILL ME */
};

GstStaticPadTemplate aggregator_src_template =
GST_STATIC_PAD_TEMPLATE ("sink%d",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS_ANY);

#define GST_TYPE_AGGREGATOR_SCHED (gst_aggregator_sched_get_type())
static GType
gst_aggregator_sched_get_type (void)
{
  static GType aggregator_sched_type = 0;
  static GEnumValue aggregator_sched[] = {
    {AGGREGATOR_LOOP, "1", "Loop Based"},
    {AGGREGATOR_LOOP_SELECT, "3", "Loop Based Select"},
    {AGGREGATOR_CHAIN, "4", "Chain Based"},
    {0, NULL, NULL},
  };
  if (!aggregator_sched_type) {
    aggregator_sched_type =
	g_enum_register_static ("GstAggregatorSched", aggregator_sched);
  }
  return aggregator_sched_type;
}

#define AGGREGATOR_IS_LOOP_BASED(ag)	((ag)->sched != AGGREGATOR_CHAIN)

static GstPad *gst_aggregator_request_new_pad (GstElement * element,
    GstPadTemplate * temp, const gchar * unused);
static void gst_aggregator_update_functions (GstAggregator * aggregator);

static void gst_aggregator_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_aggregator_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static void gst_aggregator_chain (GstPad * pad, GstData * _data);
static void gst_aggregator_loop (GstElement * element);

#define _do_init(bla) \
  GST_DEBUG_CATEGORY_INIT (gst_aggregator_debug, "aggregator", 0, "aggregator element");

GST_BOILERPLATE_FULL (GstAggregator, gst_aggregator, GstElement,
    GST_TYPE_ELEMENT, _do_init);

static void
gst_aggregator_base_init (gpointer g_class)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&aggregator_src_template));
  gst_element_class_set_details (gstelement_class, &gst_aggregator_details);
}
static void
gst_aggregator_class_init (GstAggregatorClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;


  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_NUM_PADS,
      g_param_spec_int ("num_pads", "Num pads", "The number of source pads",
	  0, G_MAXINT, 0, G_PARAM_READABLE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_SILENT,
      g_param_spec_boolean ("silent", "Silent", "Don't produce messages",
	  FALSE, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_SCHED,
      g_param_spec_enum ("sched", "Scheduling",
	  "The type of scheduling this element should use",
	  GST_TYPE_AGGREGATOR_SCHED, AGGREGATOR_CHAIN, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_LAST_MESSAGE,
      g_param_spec_string ("last_message", "Last message",
	  "The current state of the element", NULL, G_PARAM_READABLE));

  gobject_class->set_property = GST_DEBUG_FUNCPTR (gst_aggregator_set_property);
  gobject_class->get_property = GST_DEBUG_FUNCPTR (gst_aggregator_get_property);

  gstelement_class->request_new_pad =
      GST_DEBUG_FUNCPTR (gst_aggregator_request_new_pad);
}

static void
gst_aggregator_init (GstAggregator * aggregator)
{
  aggregator->srcpad = gst_pad_new ("src", GST_PAD_SRC);
  gst_pad_set_getcaps_function (aggregator->srcpad, gst_pad_proxy_getcaps);
  gst_element_add_pad (GST_ELEMENT (aggregator), aggregator->srcpad);

  aggregator->numsinkpads = 0;
  aggregator->sinkpads = NULL;
  aggregator->silent = FALSE;
  aggregator->sched = AGGREGATOR_LOOP;

  gst_aggregator_update_functions (aggregator);
}

static GstPad *
gst_aggregator_request_new_pad (GstElement * element, GstPadTemplate * templ,
    const gchar * unused)
{
  gchar *name;
  GstPad *sinkpad;
  GstAggregator *aggregator;

  g_return_val_if_fail (GST_IS_AGGREGATOR (element), NULL);

  if (templ->direction != GST_PAD_SINK) {
    g_warning ("gstaggregator: request new pad that is not a sink pad\n");
    return NULL;
  }

  aggregator = GST_AGGREGATOR (element);

  name = g_strdup_printf ("sink%d", aggregator->numsinkpads);

  sinkpad = gst_pad_new_from_template (templ, name);
  g_free (name);

  if (!AGGREGATOR_IS_LOOP_BASED (aggregator)) {
    gst_pad_set_chain_function (sinkpad, gst_aggregator_chain);
  }
  gst_pad_set_getcaps_function (sinkpad, gst_pad_proxy_getcaps);
  gst_element_add_pad (GST_ELEMENT (aggregator), sinkpad);

  aggregator->sinkpads = g_list_prepend (aggregator->sinkpads, sinkpad);
  aggregator->numsinkpads++;

  return sinkpad;
}

static void
gst_aggregator_update_functions (GstAggregator * aggregator)
{
  GList *pads;

  if (AGGREGATOR_IS_LOOP_BASED (aggregator)) {
    gst_element_set_loop_function (GST_ELEMENT (aggregator),
	GST_DEBUG_FUNCPTR (gst_aggregator_loop));
  } else {
    gst_element_set_loop_function (GST_ELEMENT (aggregator), NULL);
  }

  pads = aggregator->sinkpads;
  while (pads) {
    GstPad *pad = GST_PAD (pads->data);

    if (AGGREGATOR_IS_LOOP_BASED (aggregator)) {
      gst_pad_set_get_function (pad, NULL);
    } else {
      gst_element_set_loop_function (GST_ELEMENT (aggregator), NULL);
    }
    pads = g_list_next (pads);
  }
}

static void
gst_aggregator_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstAggregator *aggregator;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_AGGREGATOR (object));

  aggregator = GST_AGGREGATOR (object);

  switch (prop_id) {
    case ARG_SILENT:
      aggregator->silent = g_value_get_boolean (value);
      break;
    case ARG_SCHED:
      aggregator->sched = g_value_get_enum (value);
      gst_aggregator_update_functions (aggregator);
      break;
    default:
      break;
  }
}

static void
gst_aggregator_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstAggregator *aggregator;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_AGGREGATOR (object));

  aggregator = GST_AGGREGATOR (object);

  switch (prop_id) {
    case ARG_NUM_PADS:
      g_value_set_int (value, aggregator->numsinkpads);
      break;
    case ARG_SILENT:
      g_value_set_boolean (value, aggregator->silent);
      break;
    case ARG_SCHED:
      g_value_set_enum (value, aggregator->sched);
      break;
    case ARG_LAST_MESSAGE:
      g_value_set_string (value, aggregator->last_message);
      break;
    default:
      break;
  }
}

static void
gst_aggregator_push (GstAggregator * aggregator, GstPad * pad, GstBuffer * buf,
    guchar * debug)
{
  if (!aggregator->silent) {
    g_free (aggregator->last_message);

    aggregator->last_message =
	g_strdup_printf ("%10.10s ******* (%s:%s)a (%d bytes, %"
	G_GUINT64_FORMAT ")", debug, GST_DEBUG_PAD_NAME (pad),
	GST_BUFFER_SIZE (buf), GST_BUFFER_TIMESTAMP (buf));

    g_object_notify (G_OBJECT (aggregator), "last_message");
  }

  gst_pad_push (aggregator->srcpad, GST_DATA (buf));
}

static void
gst_aggregator_loop (GstElement * element)
{
  GstAggregator *aggregator;
  GstBuffer *buf;
  guchar *debug;

  aggregator = GST_AGGREGATOR (element);

  if (aggregator->sched == AGGREGATOR_LOOP) {
    GList *pads = aggregator->sinkpads;

    /* we'll loop over all pads and try to pull from all
     * active ones */
    while (pads) {
      GstPad *pad = GST_PAD (pads->data);

      pads = g_list_next (pads);

      /* we need to check is the pad is usable. IS_USABLE will check
       * if the pad is linked, if it is enabled (the element is
       * playing and the app didn't gst_pad_set_enabled (pad, FALSE))
       * and that the peer pad is also enabled.
       */
      if (GST_PAD_IS_USABLE (pad)) {
	buf = GST_BUFFER (gst_pad_pull (pad));
	debug = "loop";

	/* then push it forward */
	gst_aggregator_push (aggregator, pad, buf, debug);
      }
    }
  } else {
    if (aggregator->sched == AGGREGATOR_LOOP_SELECT) {
      GstPad *pad;

      debug = "loop_select";

      pad = gst_pad_selectv (aggregator->sinkpads);
      buf = GST_BUFFER (gst_pad_pull (pad));

      gst_aggregator_push (aggregator, pad, buf, debug);
    } else {
      g_assert_not_reached ();
    }
  }
}

/**
 * gst_aggregator_chain:
 * @pad: the pad to follow
 * @buf: the buffer to pass
 *
 * Chain a buffer on a pad.
 */
static void
gst_aggregator_chain (GstPad * pad, GstData * _data)
{
  GstBuffer *buf = GST_BUFFER (_data);
  GstAggregator *aggregator;

  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (buf != NULL);

  aggregator = GST_AGGREGATOR (gst_pad_get_parent (pad));
/*  gst_trace_add_entry (NULL, 0, buf, "aggregator buffer");*/

  gst_aggregator_push (aggregator, pad, buf, "chain");
}
