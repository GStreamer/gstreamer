/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 *               <2001> Steve Baker <stevebaker_org@yahoo.co.uk>
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

#include <string.h>
#include <math.h>

//#define DEBUG_ENABLED
#include "gstladspa.h"
#include "ladspa.h"
#include "search.h"
#include "utils.h"


static GstPadTemplate* 
ladspa_src_factory (void)
{
  return 
    gst_padtemplate_new (
    "src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    gst_caps_new (
    "ladspa_src",
      "audio/raw",
      gst_props_new (
        "format",     GST_PROPS_STRING ("float"),
        "layout",     GST_PROPS_STRING ("gfloat"),
        "intercept",  GST_PROPS_FLOAT(0.0),
        "slope",      GST_PROPS_FLOAT(1.0),
        "channels",   GST_PROPS_INT (1),
        "rate",       GST_PROPS_INT_RANGE (0,G_MAXINT),
      NULL)),
    NULL);
}

static GstPadTemplate* 
ladspa_sink_factory (void) 
{
  return 
    gst_padtemplate_new (
      "sink",
      GST_PAD_SINK,
      GST_PAD_ALWAYS,
      gst_caps_new (
      "float2int_sink",
        "audio/raw",
        gst_props_new (
          "format",     GST_PROPS_STRING ("float"),
          "layout",     GST_PROPS_STRING ("gfloat"),
          "intercept",  GST_PROPS_FLOAT(0.0),
          "slope",      GST_PROPS_FLOAT(1.0),
          "channels",   GST_PROPS_INT (1),
          "rate",       GST_PROPS_INT_RANGE (0,G_MAXINT),
      NULL)),
    NULL);
}

enum {
  ARG_0,
  ARG_LOOP_BASED,
  ARG_SAMPLERATE,
  ARG_BUFFERSIZE,
  ARG_LAST,
};

static GstPadTemplate *srctempl, *sinktempl;

static void			gst_ladspa_class_init		(GstLADSPAClass *klass);
static void			gst_ladspa_init			(GstLADSPA *ladspa);

static GstPadNegotiateReturn gst_ladspa_negotiate_sink_mono (GstPad *pad, GstCaps **caps, gpointer *data);
static GstPadNegotiateReturn gst_ladspa_negotiate_src_mono (GstPad *pad, GstCaps **caps, gpointer *data);
static GstPadNegotiateReturn gst_ladspa_negotiate_src_get_mono (GstPad *pad, GstCaps **caps, gpointer *data);
static void gst_ladspa_force_caps(GstLADSPA *ladspa, GstPad *pad);

static void			gst_ladspa_set_property		(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void			gst_ladspa_get_property		(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);

static gboolean gst_ladspa_instantiate (GstLADSPA *ladspa);
static void gst_ladspa_activate(GstLADSPA *ladspa);
static void gst_ladspa_deactivate(GstLADSPA *ladspa);

static GstElementStateReturn	gst_ladspa_change_state		(GstElement *element);
static void			gst_ladspa_loop			(GstElement *element);
static void      gst_ladspa_chain_inplace_mono (GstPad *pad,GstBuffer *buf);
static GstBuffer * gst_ladspa_get_mono(GstPad *pad);
static GstBuffer * gst_ladspa_get(GstPad *pad);

static GstElementClass *parent_class = NULL;
//static guint gst_ladspa_signals[LAST_SIGNAL] = { 0 };

static GstPlugin *ladspa_plugin;
static GHashTable *ladspa_descriptors;

static void
gst_ladspa_class_init (GstLADSPAClass *klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  LADSPA_Descriptor *desc;
  gint i,current_portnum,sinkcount,srccount,controlcount;
  gint hintdesc;
  gint argtype,argperms;
  GParamSpec *paramspec = NULL;
  gchar *argname, *tempstr;

  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  gobject_class->set_property = gst_ladspa_set_property;
  gobject_class->get_property = gst_ladspa_get_property;

  gstelement_class->change_state = gst_ladspa_change_state;

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_LOOP_BASED,
    g_param_spec_boolean("loop-based","loop-based","loop-based",
                         FALSE,G_PARAM_READWRITE));

  // look up and store the ladspa descriptor
  klass->descriptor = g_hash_table_lookup(ladspa_descriptors,GINT_TO_POINTER(G_TYPE_FROM_CLASS(klass)));
  desc = klass->descriptor;

  klass->numports = desc->PortCount;

  klass->numsinkpads = 0;
  klass->numsrcpads = 0;
  klass->numcontrols = 0;

  // walk through the ports, count the input, output and control ports
  for (i=0;i<desc->PortCount;i++) {
    if (LADSPA_IS_PORT_AUDIO(desc->PortDescriptors[i]) && 
        LADSPA_IS_PORT_INPUT(desc->PortDescriptors[i])){
      klass->numsinkpads++;
    }
      
    if (LADSPA_IS_PORT_AUDIO(desc->PortDescriptors[i]) && 
        LADSPA_IS_PORT_OUTPUT(desc->PortDescriptors[i])){
      klass->numsrcpads++;
    }
      
    if (LADSPA_IS_PORT_CONTROL(desc->PortDescriptors[i]) && 
        LADSPA_IS_PORT_INPUT(desc->PortDescriptors[i])){
      klass->numcontrols++;
    }
  }

  klass->srcpad_portnums = g_new0(gint,klass->numsrcpads);
  klass->sinkpad_portnums = g_new0(gint,klass->numsinkpads);
  klass->control_portnums = g_new0(gint,klass->numcontrols);
  sinkcount = 0;
  srccount = 0;
  controlcount = 0;

  // walk through the ports, note the portnums for srcpads, sinkpads and control params
  for (i=0;i<desc->PortCount;i++) {
    if (LADSPA_IS_PORT_AUDIO(desc->PortDescriptors[i]) && 
        LADSPA_IS_PORT_INPUT(desc->PortDescriptors[i])){
      //g_print("input port %d\n", i);
      klass->sinkpad_portnums[sinkcount++] = i;
    }
      
    if (LADSPA_IS_PORT_AUDIO(desc->PortDescriptors[i]) && 
        LADSPA_IS_PORT_OUTPUT(desc->PortDescriptors[i])){
      //g_print("output port %d\n", i);
      klass->srcpad_portnums[srccount++] = i;
    }
      
    if (LADSPA_IS_PORT_CONTROL(desc->PortDescriptors[i]) && 
        LADSPA_IS_PORT_INPUT(desc->PortDescriptors[i])){
      //g_print("control port %d\n", i);
      klass->control_portnums[controlcount++] = i;
    }
  }

  // no sink pads - we'll use get mode and add params for samplerate and buffersize
  if (klass->numsinkpads == 0 && klass->numsrcpads > 0){
    g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_SAMPLERATE,
      g_param_spec_int("samplerate","samplerate","samplerate",
                      0,G_MAXINT,44100,G_PARAM_READWRITE));
    g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_BUFFERSIZE,
      g_param_spec_int("buffersize","buffersize","buffersize",
                      0,G_MAXINT,64,G_PARAM_READWRITE));

  }
  
  // now build the contorl info from the control ports
  klass->control_info = g_new0(ladspa_control_info,klass->numcontrols);
    
  for (i=0;i<klass->numcontrols;i++) {

    current_portnum = klass->control_portnums[i];
    
    // short name for hint descriptor
    hintdesc = desc->PortRangeHints[current_portnum].HintDescriptor;

    // get the various bits
    if (LADSPA_IS_HINT_TOGGLED(hintdesc))
      klass->control_info[i].toggled = TRUE;
    if (LADSPA_IS_HINT_LOGARITHMIC(hintdesc))
      klass->control_info[i].logarithmic = TRUE;
    if (LADSPA_IS_HINT_INTEGER(hintdesc))
      klass->control_info[i].integer = TRUE;

    // figure out the argument details
    if (klass->control_info[i].toggled) argtype = G_TYPE_BOOLEAN;
    else if (klass->control_info[i].integer) argtype = G_TYPE_INT;
    else argtype = G_TYPE_FLOAT;

    // grab the bounds
    if (LADSPA_IS_HINT_BOUNDED_BELOW(hintdesc)) {
      klass->control_info[i].lower = TRUE;
      klass->control_info[i].lowerbound =
        desc->PortRangeHints[current_portnum].LowerBound;
    } else {
      if (argtype==G_TYPE_INT) klass->control_info[i].lowerbound = (gfloat)G_MININT;
      if (argtype==G_TYPE_FLOAT) klass->control_info[i].lowerbound = G_MINFLOAT;
    }
    
    if (LADSPA_IS_HINT_BOUNDED_ABOVE(hintdesc)) {
      klass->control_info[i].upper = TRUE;
      klass->control_info[i].upperbound =
        desc->PortRangeHints[current_portnum].UpperBound;
      if (LADSPA_IS_HINT_SAMPLE_RATE(hintdesc))
        klass->control_info[i].samplerate = TRUE;
    } else {
      if (argtype==G_TYPE_INT) klass->control_info[i].upperbound = (gfloat)G_MAXINT;
      if (argtype==G_TYPE_FLOAT) klass->control_info[i].upperbound = G_MAXFLOAT;
    }

    if (LADSPA_IS_PORT_INPUT(desc->PortDescriptors[current_portnum])) {
      argperms = G_PARAM_READWRITE;
      klass->control_info[i].writable = TRUE;
    } else {
      argperms = G_PARAM_READABLE;
      klass->control_info[i].writable = FALSE;
    }

    klass->control_info[i].name = g_strdup(desc->PortNames[current_portnum]);
    argname = g_strdup(klass->control_info[i].name);
    /* this is the same thing that param_spec_* will do */
    g_strcanon (argname, G_CSET_A_2_Z G_CSET_a_2_z G_CSET_DIGITS "-", '-');
    /* satisfy glib2 (argname[0] must be [A-Za-z]) */
    if (!((argname[0] >= 'a' && argname[0] <= 'z') || (argname[0] >= 'A' && argname[0] <= 'Z'))) {
      tempstr = argname;
      argname = g_strconcat("param-", argname, NULL);
      g_free (tempstr);
    }
    
    /* check for duplicate property names */
    if (g_object_class_find_property(G_OBJECT_CLASS(klass), argname) != NULL){
      gint numarg=1;
      gchar *numargname = g_strdup_printf("%s_%d",argname,numarg++);
      while (g_object_class_find_property(G_OBJECT_CLASS(klass), numargname) != NULL){
        g_free(numargname);
        numargname = g_strdup_printf("%s_%d",argname,numarg++);
      }
      argname = numargname;
    }
    
    g_print("adding arg %s from %s\n",argname, klass->control_info[i].name);
    
    if (argtype==G_TYPE_BOOLEAN){
      paramspec = g_param_spec_boolean(argname,argname,argname, FALSE, argperms);
    } else if (argtype==G_TYPE_INT){      
      paramspec = g_param_spec_int(argname,argname,argname, 
        (gint)klass->control_info[i].lowerbound, (gint)klass->control_info[i].upperbound, 0, argperms);
    } else {
      paramspec = g_param_spec_float(argname,argname,argname, 
        klass->control_info[i].lowerbound, klass->control_info[i].upperbound, 
        (klass->control_info[i].lowerbound + klass->control_info[i].upperbound) / 2.0f, argperms);
    }
    
    g_object_class_install_property(G_OBJECT_CLASS(klass), i+ARG_LAST, paramspec);
  }
}

static void
gst_ladspa_init (GstLADSPA *ladspa)
{
  GstLADSPAClass *oclass = (GstLADSPAClass*)(G_OBJECT_GET_CLASS(ladspa));
  
  LADSPA_Descriptor *desc;
  gint i,sinkcount,srccount,controlcount;

  desc = oclass->descriptor;
  ladspa->descriptor = oclass->descriptor;
  
  // allocate the various arrays
  ladspa->srcpads = g_new0(GstPad*,oclass->numsrcpads);
  ladspa->sinkpads = g_new0(GstPad*,oclass->numsinkpads);
  ladspa->controls = g_new(gfloat,oclass->numcontrols);

  // walk through the ports and add all the pads
  sinkcount = 0;
  srccount = 0;
  controlcount = 0;
  for (i=0;i<desc->PortCount;i++) {
    if (LADSPA_IS_PORT_AUDIO(desc->PortDescriptors[i]) &&
        LADSPA_IS_PORT_INPUT(desc->PortDescriptors[i])) {
      ladspa->sinkpads[sinkcount] = gst_pad_new_from_template 
      (sinktempl, (gchar *)desc->PortNames[i]);
      gst_element_add_pad(GST_ELEMENT(ladspa),ladspa->sinkpads[sinkcount]);
      sinkcount++;
    }
    if (LADSPA_IS_PORT_AUDIO(desc->PortDescriptors[i]) &&
        LADSPA_IS_PORT_OUTPUT(desc->PortDescriptors[i])) {
      ladspa->srcpads[srccount] = gst_pad_new_from_template 
        (srctempl, (gchar *)desc->PortNames[i]);
      gst_element_add_pad(GST_ELEMENT(ladspa),ladspa->srcpads[srccount]);
      srccount++;
    }
    if (LADSPA_IS_PORT_CONTROL(desc->PortDescriptors[i]) &&
        LADSPA_IS_PORT_INPUT(desc->PortDescriptors[i])) {
          
      // use the lowerbound as the default value if it exists
      if (oclass->control_info[controlcount].lower){
        ladspa->controls[controlcount]=oclass->control_info[controlcount].lowerbound;
      } else {
        ladspa->controls[controlcount] = 0.0;
      }
      controlcount++;
    }
  }

  ladspa->samplerate = 0;
  ladspa->buffersize = 0;
  ladspa->newcaps = FALSE;
  ladspa->activated = FALSE;
  
  // mono chain
  if (sinkcount==1 && srccount==1){
    //g_print("inplace mono chain mode\n");
    gst_pad_set_negotiate_function (ladspa->sinkpads[0], gst_ladspa_negotiate_sink_mono);
    gst_pad_set_chain_function(ladspa->sinkpads[0],gst_ladspa_chain_inplace_mono);
    gst_pad_set_negotiate_function (ladspa->srcpads[0], gst_ladspa_negotiate_src_mono);
  }
  
  // mono get (no sink pads)
  if (sinkcount==0 && srccount == 1){
    //g_print("get mode\n");
    ladspa->newcaps = TRUE;
    ladspa->samplerate = 44100;
    ladspa->buffersize = 64;
    gst_pad_set_get_function(ladspa->srcpads[0],gst_ladspa_get_mono);
    gst_pad_set_negotiate_function (ladspa->srcpads[0], gst_ladspa_negotiate_src_get_mono);
    gst_ladspa_instantiate(ladspa);
  }
  
  // multi srcpad get
  if (sinkcount==0 && srccount > 1){
    //g_print("multi get mode\n");
    ladspa->newcaps = TRUE;
    ladspa->samplerate = 44100;
    ladspa->buffersize = 64;
    gst_pad_set_get_function(ladspa->srcpads[0],gst_ladspa_get);
    gst_pad_set_negotiate_function (ladspa->srcpads[0], gst_ladspa_negotiate_src_get_mono);
    gst_ladspa_instantiate(ladspa);
    ladspa->buffers = g_new0(GstBuffer*,oclass->numsrcpads);
  }
}

static GstPadNegotiateReturn
gst_ladspa_negotiate_src_mono (GstPad *pad, GstCaps **caps, gpointer *data)
{
  GstLADSPA *ladspa = (GstLADSPA*)GST_OBJECT_PARENT (pad);
  
  // have to instantiate ladspa plugin when samplerate changes (groan)
  if (ladspa->samplerate != gst_caps_get_int (*caps, "rate")){
    ladspa->samplerate = gst_caps_get_int (*caps, "rate");
    if (!gst_ladspa_instantiate(ladspa)) return GST_PAD_NEGOTIATE_FAIL;
  }
  return gst_pad_negotiate_proxy (pad, ladspa->sinkpads[0], caps);
}

static GstPadNegotiateReturn
gst_ladspa_negotiate_sink_mono (GstPad *pad, GstCaps **caps, gpointer *data)
{
  GstLADSPA *ladspa = (GstLADSPA*)GST_OBJECT_PARENT (pad);
  
  // have to instantiate ladspa plugin when samplerate changes (groan)
  if (ladspa->samplerate != gst_caps_get_int (*caps, "rate")){
    ladspa->samplerate = gst_caps_get_int (*caps, "rate");
    if (!gst_ladspa_instantiate(ladspa)) return GST_PAD_NEGOTIATE_FAIL;
  }
  return gst_pad_negotiate_proxy (pad, ladspa->srcpads[0], caps);
}

static GstPadNegotiateReturn 
gst_ladspa_negotiate_src_get_mono (GstPad *pad, GstCaps **caps, gpointer *data) 
{
  GstLADSPA *ladspa;
  //g_print("gst_ladspa_negotiate_src_get_mono\n");
  if (*caps) {
    g_return_val_if_fail (pad != NULL, GST_PAD_NEGOTIATE_FAIL);
    ladspa = (GstLADSPA*)GST_OBJECT_PARENT (pad);
    ladspa->samplerate = gst_caps_get_int (*caps, "rate");
    if (!gst_ladspa_instantiate(ladspa)) return GST_PAD_NEGOTIATE_FAIL;
    return GST_PAD_NEGOTIATE_AGREE;
  }
  return GST_PAD_NEGOTIATE_FAIL;
}

static void
gst_ladspa_force_caps(GstLADSPA *ladspa, GstPad *pad) {
  
  // g_print("forcing caps\n");
  gst_pad_set_caps (pad, gst_caps_new (
    "ladspa_src_caps",
    "audio/raw",
    gst_props_new (
      "format",     GST_PROPS_STRING ("float"),
      "layout",     GST_PROPS_STRING ("gfloat"),
      "intercept",  GST_PROPS_FLOAT(0.0),
      "slope",      GST_PROPS_FLOAT(1.0),
      "rate",       GST_PROPS_INT (ladspa->samplerate),
      "channels",   GST_PROPS_INT (1),
      NULL
    )
  ));
  ladspa->newcaps=FALSE;
}

static void
gst_ladspa_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  GstLADSPA *ladspa = (GstLADSPA*)object;
  gint cid = prop_id - ARG_LAST;
  GstLADSPAClass *oclass;
  ladspa_control_info *control_info;
  gfloat val=0.0;
    
  // these are only registered in get mode
  switch (prop_id) {
    case ARG_SAMPLERATE:
      ladspa->samplerate = g_value_get_int (value);
      ladspa->newcaps=TRUE;
      break;
    case ARG_BUFFERSIZE:
      ladspa->buffersize = g_value_get_int (value);
      break;
  }
  
  // is it a ladspa plugin arg?
  if (cid<0) return;

/*
  if (id == ARG_LOOP_BASED) {
    // we can only do this in NULL state
    g_return_if_fail (GST_STATE(object) != GST_STATE_NULL);
    ladspa->loopbased = g_value_get_boolean (value);
    if (ladspa->loopbased) {
      gst_element_set_loop_function (GST_ELEMENT (ladspa), gst_ladspa_loop);
    } else {
      gst_element_set_loop_function (GST_ELEMENT (ladspa), NULL);
    }
  }
*/

  oclass = (GstLADSPAClass*)(G_OBJECT_GET_CLASS (object));

  // verify it exists and is a control (not a port)
  g_return_if_fail(cid < oclass->numcontrols);
  
  control_info = &(oclass->control_info[cid]);
  g_return_if_fail (control_info->name != NULL);

  // check to see if it's writable
  g_return_if_fail (control_info->writable);

  // g_print("set arg %s to %f\n",control_info->name,ladspa->controls[cid]);

  // now see what type it is
  if (control_info->toggled) {
    if (g_value_get_boolean (value))
      ladspa->controls[cid] = 1.0;
    else
      ladspa->controls[cid] = 0.0;
  } else if (control_info->integer) {
    val = (gfloat)g_value_get_int (value);
    ladspa->controls[cid] = val;
  } else {
    val = g_value_get_float (value);
    ladspa->controls[cid] = val;
  }    
}

static void
gst_ladspa_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  GstLADSPA *ladspa = (GstLADSPA*)object;
  gint cid = prop_id - ARG_LAST;
  GstLADSPAClass *oclass = (GstLADSPAClass*)(G_OBJECT_GET_CLASS (object));
  ladspa_control_info *control_info;

  // these are only registered in get mode
  switch (prop_id){
    case ARG_SAMPLERATE:
      g_value_set_int (value, ladspa->samplerate);
      break;
    case ARG_BUFFERSIZE:
      g_value_set_int (value, ladspa->buffersize);
      break;
  }
    
  if (cid<0) return;
  // verify it exists and is a control (not a port)
  if (cid >= oclass->numcontrols) return;
  control_info = &(oclass->control_info[cid]);
  if (control_info->name == NULL) return;

  //g_print("got arg %s as %f\n",control_info->name,ladspa->controls[cid]);

  // now see what type it is
  if (control_info->toggled) {
    if (ladspa->controls[cid] == 1.0)
      g_value_set_boolean (value, TRUE);
    else
      g_value_set_boolean (value, FALSE);
  } else if (control_info->integer) {
    g_value_set_int (value, (gint)ladspa->controls[cid]);
  } else {
    g_value_set_float (value, ladspa->controls[cid]);
  }
}

static gboolean
gst_ladspa_instantiate (GstLADSPA *ladspa)
{
  LADSPA_Descriptor *desc;
  int i;
  GstLADSPAClass *oclass = (GstLADSPAClass*)(G_OBJECT_GET_CLASS (ladspa));
  gboolean was_activated;
  
  desc = ladspa->descriptor;
  
  // check for old handle
  was_activated = ladspa->activated;
  if (ladspa->handle != NULL){
    gst_ladspa_deactivate(ladspa);
    desc->cleanup(ladspa->handle);
  }
        
  // instantiate the plugin  
  ladspa->handle = desc->instantiate(desc,ladspa->samplerate);
  g_return_val_if_fail (ladspa->handle != NULL, FALSE);

  // walk through the ports and add all the arguments
  for (i=0;i<oclass->numcontrols;i++) {
    // connect the argument to the plugin
    //g_print("added control port %d\n", oclass->control_portnums[i]);
    desc->connect_port(ladspa->handle,
                       oclass->control_portnums[i],
                       &(ladspa->controls[i]));
  }

  // reactivate if it was activated before the reinstantiation
  if (was_activated){
    gst_ladspa_activate(ladspa);
  }
  return TRUE;
}

static GstElementStateReturn
gst_ladspa_change_state (GstElement *element)
{
  LADSPA_Descriptor *desc;
  GstLADSPA *ladspa = (GstLADSPA*)element;
//  GstLADSPAClass *oclass = (GstLADSPAClass*)(G_OBJECT(ladspa)->klass);
  desc = ladspa->descriptor;

  //g_print("changing state\n");
  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_NULL_TO_READY:
      gst_ladspa_activate(ladspa);
      break;
    case GST_STATE_READY_TO_NULL:
      gst_ladspa_deactivate(ladspa);
      break;
    default:
      break;
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);

  return GST_STATE_SUCCESS;
}

static void
gst_ladspa_activate(GstLADSPA *ladspa)
{
  LADSPA_Descriptor *desc;
  desc = ladspa->descriptor;
  
  if (ladspa->activated){
    gst_ladspa_deactivate(ladspa);
  }
  
  //g_print("activating\n");

  // activate the plugin (function might be null)
  if (desc->activate != NULL){
    desc->activate(ladspa->handle);
  }
  
  ladspa->activated = TRUE;
}

static void
gst_ladspa_deactivate(GstLADSPA *ladspa)
{
  LADSPA_Descriptor *desc;
  desc = ladspa->descriptor;

  //g_print("deactivating\n");

  // deactivate the plugin (function might be null)  
  if (ladspa->activated && desc->deactivate != NULL){
    desc->deactivate(ladspa->handle);
  }
  ladspa->activated = FALSE;
}

static void
gst_ladspa_loop (GstElement *element)
{
  int i;
  GstLADSPA *ladspa = (GstLADSPA *)element;
  GstLADSPAClass *oclass = (GstLADSPAClass*)(G_OBJECT_GET_CLASS (ladspa));
  LADSPA_Descriptor *desc;

  desc = ladspa->descriptor;
  printf("looping something\n");

  // first get all the necessary data from the input ports
  for (i=0;i<oclass->numsinkpads;i++){  
    ladspa->buffers[i] = gst_pad_pull(ladspa->sinkpads[i]);
    printf("pulling buffer %d\n", i);
  }
    
  for (i=0;i<oclass->numsinkpads;i++) {
//      desc->connect_port(ladspa->handle,i,&(ladspa->controls[i]));
  }

  for (i=0;i<oclass->numsrcpads && i<oclass->numsinkpads;i++){
    printf("pushing buffer %d\n", i);
    gst_pad_push (ladspa->srcpads[i], ladspa->buffers[i]);
    ladspa->buffers[i] = NULL;
  }
}

static void
gst_ladspa_chain_inplace_mono (GstPad *pad,GstBuffer *buf)
{
  LADSPA_Descriptor *desc;
  LADSPA_Data *data;
  unsigned long num_samples;
  
  GstLADSPA *ladspa;
  GstLADSPAClass *oclass;

  g_return_if_fail(pad != NULL);
  g_return_if_fail(GST_IS_PAD(pad));
  g_return_if_fail(buf != NULL);

  ladspa = (GstLADSPA *)gst_pad_get_parent (pad);
  g_return_if_fail(ladspa != NULL);

  // this might happen if caps nego hasn't happened
  g_return_if_fail(ladspa->handle != NULL);

  oclass = (GstLADSPAClass*)(G_OBJECT_GET_CLASS (ladspa));
  data=(LADSPA_Data*)GST_BUFFER_DATA(buf);
  num_samples = GST_BUFFER_SIZE(buf) / sizeof(gfloat);
  
  desc = ladspa->descriptor;

  desc->connect_port(ladspa->handle,oclass->sinkpad_portnums[0],data);
  desc->connect_port(ladspa->handle,oclass->srcpad_portnums[0],data);

  desc->run(ladspa->handle,num_samples);
  
  desc->connect_port(ladspa->handle,oclass->sinkpad_portnums[0],NULL);
  desc->connect_port(ladspa->handle,oclass->srcpad_portnums[0],NULL);

  gst_pad_push (ladspa->srcpads[0], buf);  
}

static GstBuffer *
gst_ladspa_get_mono(GstPad *pad)
{
  LADSPA_Descriptor *desc;
  LADSPA_Data *data;
  
  GstLADSPA *ladspa;
  GstLADSPAClass *oclass;
  GstBuffer *buf;

  g_return_val_if_fail(pad != NULL, NULL);
  g_return_val_if_fail(GST_IS_PAD(pad), NULL);

  ladspa = (GstLADSPA *)gst_pad_get_parent (pad);
  g_return_val_if_fail(ladspa != NULL, NULL);

  // this might happen if caps nego hasn't happened
  g_return_val_if_fail(ladspa->handle != NULL, NULL);

  oclass = (GstLADSPAClass*)(G_OBJECT_GET_CLASS(ladspa));
  
  if (ladspa->newcaps) {
    gst_ladspa_force_caps(ladspa, ladspa->srcpads[0]);
  }
  
  buf = gst_buffer_new();
  g_return_val_if_fail (buf, NULL);
  data = g_new(LADSPA_Data, ladspa->buffersize);
  GST_BUFFER_DATA(buf) = (gpointer) data;
  GST_BUFFER_SIZE(buf) = sizeof(LADSPA_Data) * ladspa->buffersize;
  GST_BUFFER_TIMESTAMP(buf) = ladspa->timestamp;

  desc = ladspa->descriptor;
  desc->connect_port(ladspa->handle,oclass->srcpad_portnums[0],data);
  desc->run(ladspa->handle,(unsigned long)ladspa->buffersize);  
  desc->connect_port(ladspa->handle,oclass->srcpad_portnums[0],NULL);
  
  return buf;
}

static GstBuffer *
gst_ladspa_get(GstPad *pad)
{
  LADSPA_Descriptor *desc;
  LADSPA_Data *data;
  
  GstLADSPA *ladspa;
  GstLADSPAClass *oclass;
  GstBuffer *buf;

  g_return_val_if_fail(pad != NULL, NULL);
  g_return_val_if_fail(GST_IS_PAD(pad), NULL);

  ladspa = (GstLADSPA *)gst_pad_get_parent (pad);
  g_return_val_if_fail(ladspa != NULL, NULL);

  // this might happen if caps nego hasn't happened
  g_return_val_if_fail(ladspa->handle != NULL, NULL);

  oclass = (GstLADSPAClass*)(G_OBJECT_GET_CLASS(ladspa));
  
  if (ladspa->newcaps) {
    gst_ladspa_force_caps(ladspa, ladspa->srcpads[0]);
  }
  
  buf = gst_buffer_new();
  g_return_val_if_fail (buf, NULL);
  data = g_new(LADSPA_Data, ladspa->buffersize);
  GST_BUFFER_DATA(buf) = (gpointer) data;
  GST_BUFFER_SIZE(buf) = sizeof(LADSPA_Data) * ladspa->buffersize;
  GST_BUFFER_TIMESTAMP(buf) = ladspa->timestamp;
  ladspa->timestamp+= ladspa->buffersize * ladspa->samplerate * 10^9;

  desc = ladspa->descriptor;
  desc->connect_port(ladspa->handle,oclass->srcpad_portnums[0],data);
  desc->run(ladspa->handle,(unsigned long)ladspa->buffersize);  
  desc->connect_port(ladspa->handle,oclass->srcpad_portnums[0],NULL);
  
  return buf;
}

static void
ladspa_describe_plugin(const char *pcFullFilename,
                       void *pvPluginHandle,
                       LADSPA_Descriptor_Function pfDescriptorFunction)
{
  const LADSPA_Descriptor *desc;
  int i,j;
  
  GstElementDetails *details;
  GTypeInfo typeinfo = {
      sizeof(GstLADSPAClass),      NULL,
      NULL,
      (GClassInitFunc)gst_ladspa_class_init,
      NULL,
      NULL,
      sizeof(GstLADSPA),
      0,
      (GInstanceInitFunc)gst_ladspa_init,
  };
  GType type;
  GstElementFactory *factory;

  // walk through all the plugins in this pluginlibrary
  i = 0;
  while ((desc = pfDescriptorFunction(i++))) {
    gchar *type_name;

    // construct the type
    type_name = g_strdup_printf("ladspa_%s",desc->Label);
    g_strcanon (type_name, G_CSET_A_2_Z G_CSET_a_2_z G_CSET_DIGITS "-_+", '-');
    // if it's already registered, drop it
    if (g_type_from_name(type_name)) {
      g_free(type_name);
      continue;
    }
    // create the type now
    type = g_type_register_static(GST_TYPE_ELEMENT, type_name , &typeinfo, 0);

    // construct the element details struct
    details = g_new0(GstElementDetails,1);
    details->longname = g_strdup(desc->Name);
    details->klass = "Filter/LADSPA";
    details->description = details->longname;
    details->version = g_strdup_printf("%ld",desc->UniqueID);
    details->author = g_strdup(desc->Maker);
    details->copyright = g_strdup(desc->Copyright);

    // register the plugin with gstreamer
    factory = gst_elementfactory_new(type_name,type,details);
    g_return_if_fail(factory != NULL);
    gst_plugin_add_feature (ladspa_plugin, GST_PLUGIN_FEATURE (factory));

    // add this plugin to the hash
    g_hash_table_insert(ladspa_descriptors,
                        GINT_TO_POINTER(type),
                        (gpointer)desc);
    

    // only add sink padtemplate if there are sinkpads                        
    for (j=0;j<desc->PortCount;j++) {
      if (LADSPA_IS_PORT_AUDIO(desc->PortDescriptors[j]) &&
          LADSPA_IS_PORT_INPUT(desc->PortDescriptors[j])) {
        sinktempl = ladspa_sink_factory();
        gst_elementfactory_add_padtemplate (factory, sinktempl);
        break;
      }
    }
  
    srctempl = ladspa_src_factory();
    gst_elementfactory_add_padtemplate (factory, srctempl);

  }
}

static gboolean
plugin_init (GModule *module, GstPlugin *plugin)
{
  
  ladspa_descriptors = g_hash_table_new(NULL,NULL);
  parent_class = g_type_class_ref(GST_TYPE_ELEMENT);

  ladspa_plugin = plugin;

  LADSPAPluginSearch(ladspa_describe_plugin);

  return TRUE;
}

GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "ladspa",
  plugin_init
};

