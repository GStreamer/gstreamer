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

#include <gst/gst.h>
#include <libs/control/gstcontrol.h>

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


GstElementDetails gst_dptest_details = {
  "DParamsTest",
  "Filter",
  "Test for the GstDParam code",
  VERSION,
  "Steve Baker <stevebaker_org@yahoo.co.uk>",
  "(C) 2001",
};

enum
{
  ARG_0,
};


static void 	gst_dptest_class_init 		(GstDpTestClass * klass);
static void 	gst_dptest_init 		(GstDpTest * dptest);

static void gst_dptest_set_property (GObject * object, guint prop_id, const GValue * value,
				     GParamSpec * pspec);

static GstElementStateReturn 	gst_dptest_change_state 	(GstElement *element);
static void gst_dptest_chain (GstPad *pad, GstBuffer *buf);

static GstElementClass *parent_class = NULL;

GType
gst_dptest_get_type (void)
{
  static GType dptest_type = 0;

  if (!dptest_type) {
    static const GTypeInfo dptest_info = {
      sizeof (GstDpTestClass), NULL,
      NULL,
      (GClassInitFunc) gst_dptest_class_init,
      NULL,
      NULL,
      sizeof (GstDpTest),
      0,
      (GInstanceInitFunc) gst_dptest_init,
    };

    dptest_type = g_type_register_static (GST_TYPE_ELEMENT, "GstDpTest", &dptest_info, 0);
  }
  return dptest_type;
}

static void
gst_dptest_class_init (GstDpTestClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass*)klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  gobject_class->set_property = GST_DEBUG_FUNCPTR (gst_dptest_set_property);

  gstelement_class->change_state = gst_dptest_change_state;

}

static void
gst_dptest_init (GstDpTest * dptest)
{
  GstDParamSpec *spec;

  dptest->sinkpad = gst_pad_new ("sink", GST_PAD_SINK);
  gst_element_add_pad (GST_ELEMENT (dptest), dptest->sinkpad);
  gst_pad_set_chain_function(dptest->sinkpad, gst_dptest_chain);

  dptest->srcpad = gst_pad_new ("src", GST_PAD_SRC);
  gst_element_add_pad (GST_ELEMENT (dptest), dptest->srcpad);

  dptest->dpman = gst_dpman_new ("dptest_dpman", GST_ELEMENT(dptest));

  gst_dpman_add_required_dparam_direct (dptest->dpman, "float1", G_TYPE_FLOAT, &(dptest->float1));
  spec = gst_dpman_get_dparam_spec (dptest->dpman, "float1");
  g_value_set_float(spec->min_val, 0.0);
  g_value_set_float(spec->max_val, 1.0);
  g_value_set_float(spec->default_val, 0.5);
  spec->unit_name = "scalar";
  
  dptest->float1 = 0.0;
}

static void
gst_dptest_set_property (GObject * object, guint prop_id, const GValue * value, GParamSpec * pspec)
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
gst_dptest_change_state (GstElement *element)
{
  GstDpTest *dptest;

  g_return_val_if_fail (GST_IS_DPTEST (element), GST_STATE_FAILURE);
  g_print("changing state\n");

  dptest = GST_DPTEST (element);
  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);
    
  return GST_STATE_SUCCESS;
}

static void 
gst_dptest_chain (GstPad *pad, GstBuffer *buf)
{

  g_print("dp chain\n");
}

int main(int argc,char *argv[]) {

  GstElementFactory *factory;
  GstElement *src;
  GstElement *sink;
  GstElement *dp;
  GstElement *pipeline;
  
  gst_init (&argc, &argv);
  gst_control_init(&argc,&argv);
  
  factory = gst_elementfactory_new ("dptest", GST_TYPE_DPTEST, &gst_dptest_details);
  g_assert (factory != NULL);

  pipeline = gst_elementfactory_make ("pipeline", "pipeline");
  g_assert (pipeline);

  src = gst_elementfactory_make ("fakesrc", "src");
  g_assert (src);

  sink = gst_elementfactory_make ("fakesink", "sink");
  g_assert (sink);

  dp = gst_elementfactory_make ("dptest", "dp");
  g_assert (dp);

  gst_element_connect (src, "src", dp, "sink");
  gst_element_connect (dp, "src", sink, "sink");

  gst_bin_add (GST_BIN (pipeline), src);
  gst_bin_add (GST_BIN (pipeline), dp);
  gst_bin_add (GST_BIN (pipeline), sink);

  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_PLAYING);

  g_object_set (G_OBJECT (src), "num_buffers", 2, NULL);				    
  gst_bin_iterate (GST_BIN (pipeline));

  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_NULL);
  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_PLAYING);

  g_object_set (G_OBJECT (src), "num_buffers", 2, NULL);
  gst_bin_iterate (GST_BIN (pipeline));
    
  return 0;
}
