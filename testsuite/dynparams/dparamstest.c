/* GStreamer
 * Copyright (C) 2001 Steve Baker <stevebaker_org@yahoo.co.uk>
 *
 * dparamstest.c: 
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
#include <unistd.h>
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/control/control.h>

#define GST_TYPE_DPTEST  		(gst_dptest_get_type())
#define GST_DPTEST(obj)  		(G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_DPTEST,GstDpTest))
#define GST_DPTEST_CLASS(klass) 	(G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_DPTEST,GstDpTestClass))
#define GST_IS_DPTEST(obj)  		(G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_DPTEST))
#define GST_IS_DPTEST_CLASS(obj)  	(G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_DPTEST))

typedef struct _GstDpTest GstDpTest;
typedef struct _GstDpTestClass GstDpTestClass;

struct _GstDpTest
{
  GstElement element;

  GstPad *sinkpad;
  GstPad *srcpad;
  GstDParamManager *dpman;

  gfloat float1;
  gfloat float2;
  gboolean bool1;
  gdouble double1;
};

struct _GstDpTestClass
{
  GstElementClass parent_class;
};

GType gst_dptest_get_type (void);

enum
{
  ARG_0,
};


static void gst_dptest_base_init (gpointer g_class);
static void gst_dptest_class_init (GstDpTestClass * klass);
static void gst_dptest_init (GstDpTest * dptest);

static void gst_dptest_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);

static GstElementStateReturn gst_dptest_change_state (GstElement * element);
static void gst_dptest_chain (GstPad * pad, GstData * buf);

static GstElementClass *parent_class = NULL;

GType
gst_dptest_get_type (void)
{
  static GType dptest_type = 0;

  if (!dptest_type) {
    static const GTypeInfo dptest_info = {
      sizeof (GstDpTestClass),
      gst_dptest_base_init,
      NULL,
      (GClassInitFunc) gst_dptest_class_init,
      NULL,
      NULL,
      sizeof (GstDpTest),
      0,
      (GInstanceInitFunc) gst_dptest_init,
    };

    dptest_type =
        g_type_register_static (GST_TYPE_ELEMENT, "GstDpTest", &dptest_info, 0);
  }
  return dptest_type;
}

static void
gst_dptest_base_init (gpointer g_class)
{
  static GstElementDetails dptest_details = GST_ELEMENT_DETAILS ("DParamTest",
      "Filter",
      "Test for the GstDParam code",
      "Steve Baker <stevebaker_org@yahoo.co.uk>");
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details (element_class, &dptest_details);

  g_print ("got here %d\n", __LINE__);
}

static void
gst_dptest_class_init (GstDpTestClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  gobject_class->set_property = GST_DEBUG_FUNCPTR (gst_dptest_set_property);

  gstelement_class->change_state = gst_dptest_change_state;

}

static void
gst_dptest_init (GstDpTest * dptest)
{

  dptest->sinkpad = gst_pad_new ("sink", GST_PAD_SINK);
  gst_element_add_pad (GST_ELEMENT (dptest), dptest->sinkpad);
  gst_pad_set_chain_function (dptest->sinkpad, gst_dptest_chain);

  dptest->srcpad = gst_pad_new ("src", GST_PAD_SRC);
  gst_element_add_pad (GST_ELEMENT (dptest), dptest->srcpad);

  dptest->dpman = gst_dpman_new ("dptest_dpman", GST_ELEMENT (dptest));

  gst_dpman_add_required_dparam_direct (dptest->dpman,
      g_param_spec_float ("float1", "float1", "float1",
          0.0, 1.0, 0.5, G_PARAM_READWRITE), "float", &(dptest->float1)
      );

  dptest->float1 = 0.0;
}

static void
gst_dptest_set_property (GObject * object, guint prop_id, const GValue * value,
    GParamSpec * pspec)
{
  GstDpTest *dptest;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_DPTEST (object));

  dptest = GST_DPTEST (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstElementStateReturn
gst_dptest_change_state (GstElement * element)
{
  GstDpTest *dptest;

  g_return_val_if_fail (GST_IS_DPTEST (element), GST_STATE_FAILURE);
  g_print ("changing state\n");

  dptest = GST_DPTEST (element);
  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);

  return GST_STATE_SUCCESS;
}

static void
gst_dptest_chain (GstPad * pad, GstData * data)
{
  GstDpTest *dptest;
  gint frame_countdown;

  dptest = GST_DPTEST (gst_pad_get_parent (pad));
  g_assert (dptest);

  /* we're using a made up buffer size of 64 and a timestamp of zero */
  frame_countdown = GST_DPMAN_PREPROCESS (dptest->dpman, 64, 0LL);

  while (GST_DPMAN_PROCESS (dptest->dpman, frame_countdown));

  g_print ("dp chain\n");
}

gboolean
gst_dptest_register_elements (GstPlugin * plugin)
{
  return gst_element_register (plugin, "dptest", GST_RANK_NONE,
      GST_TYPE_DPTEST);
}

static GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "dptest_elements",
  "test elements",
  &gst_dptest_register_elements,
  NULL,
  VERSION,
  GST_LICENSE,
  GST_PACKAGE,
  GST_ORIGIN
};

int
main (int argc, char *argv[])
{

  GstElement *src;
  GstElement *sink;
  GstElement *testelement;
  GstElement *pipeline;
  GstDParamManager *dpman;
  GstDParam *dp_float1;
  GValue *dp_float1_value;

  alarm (10);

  gst_init (&argc, &argv);
  gst_control_init (&argc, &argv);

  _gst_plugin_register_static (&plugin_desc);

  pipeline = gst_element_factory_make ("pipeline", "pipeline");
  g_assert (pipeline);

  src = gst_element_factory_make ("fakesrc", "src");
  g_assert (src);

  sink = gst_element_factory_make ("fakesink", "sink");
  g_assert (sink);

  testelement = gst_element_factory_make ("dptest", "testelement");
  g_assert (testelement);

  gst_element_link (src, testelement);
  gst_element_link (testelement, sink);

  gst_bin_add (GST_BIN (pipeline), src);
  gst_bin_add (GST_BIN (pipeline), testelement);
  gst_bin_add (GST_BIN (pipeline), sink);

  g_print ("playing pipeline\n");

  g_object_set (G_OBJECT (src), "num_buffers", 1, NULL);

  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_PLAYING);

  /* test that dparam manager is accessable */
  g_print ("getting dparam manager\n");
  dpman = gst_dpman_get_manager (testelement);
  gst_dpman_set_mode (dpman, "synchronous");

  g_assert (dpman);
  g_assert (GST_IS_DPMAN (dpman));

  g_print ("creating dparam for float1\n");
  dp_float1 = gst_dparam_new (G_TYPE_FLOAT);;
  g_assert (dp_float1);
  g_assert (GST_IS_DPARAM (dp_float1));

  g_print ("attach dparam to float1\n");
  g_assert (gst_dpman_attach_dparam (dpman, "float1", dp_float1));

  dp_float1_value = g_new0 (GValue, 1);
  g_value_init (dp_float1_value, G_TYPE_FLOAT);

  g_value_set_float (dp_float1_value, 0.1);
  g_object_set_property (G_OBJECT (dp_float1), "value_float", dp_float1_value);

  g_print ("iterate once\n");
  gst_bin_iterate (GST_BIN (pipeline));

  g_print ("check that value changed\n");
  g_assert (GST_DPTEST (testelement)->float1 == 0.1F);
  g_assert (!GST_DPARAM_READY_FOR_UPDATE (dp_float1));

  g_print ("nulling pipeline\n");
  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_NULL);

  g_print ("playing pipeline\n");
  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_PLAYING);

  g_print ("iterate twice\n");

  g_object_set (G_OBJECT (src), "num_buffers", 2, NULL);
  gst_bin_iterate (GST_BIN (pipeline));

  return 0;
}
