/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstfakesrc.c: 
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


#include <gstfakesrc.h>


GstElementDetails gst_fakesrc_details = {
  "Fake Source",
  "Source",
  "Push empty (no data) buffers around",
  VERSION,
  "Erik Walthinsen <omega@cse.ogi.edu>\n"
  "Wim Taymans <wim.taymans@chello.be>",
  "(C) 1999",
};


/* FakeSrc signals and args */
enum {
  /* FILL ME */
  SIGNAL_HANDOFF,
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_NUM_SOURCES,
  ARG_LOOP_BASED,
  ARG_OUTPUT,
  ARG_PATTERN,
  ARG_NUM_BUFFERS,
  ARG_EOS,
  ARG_SILENT
};

GST_PADTEMPLATE_FACTORY (fakesrc_src_factory,
  "src%d",
  GST_PAD_SRC,
  GST_PAD_REQUEST,
  NULL                  /* no caps */
);

#define GST_TYPE_FAKESRC_OUTPUT (gst_fakesrc_output_get_type())
static GType
gst_fakesrc_output_get_type (void) 
{
  static GType fakesrc_output_type = 0;
  static GEnumValue fakesrc_output[] = {
    { FAKESRC_FIRST_LAST_LOOP, 		"1", "First-Last loop"},
    { FAKESRC_LAST_FIRST_LOOP, 		"2", "Last-First loop"},
    { FAKESRC_PING_PONG, 		"3", "Ping-Pong"},
    { FAKESRC_ORDERED_RANDOM, 		"4", "Ordered Random"},
    { FAKESRC_RANDOM, 			"5", "Random"},
    { FAKESRC_PATTERN_LOOP, 		"6", "Patttern loop"},
    { FAKESRC_PING_PONG_PATTERN, 	"7", "Ping-Pong Pattern"},
    { FAKESRC_GET_ALWAYS_SUCEEDS, 	"8", "'_get' Always succeeds"},
    {0, NULL, NULL},
  };
  if (!fakesrc_output_type) {
    fakesrc_output_type = g_enum_register_static ("GstFakeSrcOutput", fakesrc_output);
  }
  return fakesrc_output_type;
}

static void		gst_fakesrc_class_init		(GstFakeSrcClass *klass);
static void		gst_fakesrc_init		(GstFakeSrc *fakesrc);

static GstPad* 		gst_fakesrc_request_new_pad 	(GstElement *element, GstPadTemplate *templ);
static void		gst_fakesrc_set_property	(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void		gst_fakesrc_get_property	(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);

static GstBuffer*	gst_fakesrc_get			(GstPad *pad);
static void 		gst_fakesrc_loop		(GstElement *element);

static GstElementClass *parent_class = NULL;
static guint gst_fakesrc_signals[LAST_SIGNAL] = { 0 };

GType
gst_fakesrc_get_type (void) 
{
  static GType fakesrc_type = 0;

  if (!fakesrc_type) {
    static const GTypeInfo fakesrc_info = {
      sizeof(GstFakeSrcClass),
      NULL,
      NULL,
      (GClassInitFunc)gst_fakesrc_class_init,
      NULL,
      NULL,
      sizeof(GstFakeSrc),
      0,
      (GInstanceInitFunc)gst_fakesrc_init,
    };
    fakesrc_type = g_type_register_static (GST_TYPE_ELEMENT, "GstFakeSrc", &fakesrc_info, 0);
  }
  return fakesrc_type;
}

static void
gst_fakesrc_class_init (GstFakeSrcClass *klass) 
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_NUM_SOURCES,
    g_param_spec_int ("num_sources", "num_sources", "num_sources",
                      1, G_MAXINT, 1, G_PARAM_READABLE));
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_LOOP_BASED,
    g_param_spec_boolean("loop_based","loop_based","loop_based",
                         FALSE, G_PARAM_READWRITE)); // CHECKME
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_OUTPUT,
    g_param_spec_enum("output","output","output",
                      GST_TYPE_FAKESRC_OUTPUT,FAKESRC_FIRST_LAST_LOOP,G_PARAM_READWRITE)); // CHECKME!
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_PATTERN,
    g_param_spec_string("pattern","pattern","pattern",
                        NULL, G_PARAM_READWRITE)); // CHECKME
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_NUM_BUFFERS,
    g_param_spec_int("num_buffers","num_buffers","num_buffers",
                     G_MININT,G_MAXINT,0,G_PARAM_READWRITE)); // CHECKME
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_EOS,
    g_param_spec_boolean("eos","eos","eos",
                         TRUE,G_PARAM_READWRITE)); // CHECKME
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_SILENT,
    g_param_spec_boolean("silent","silent","silent",
                         FALSE, G_PARAM_READWRITE)); // CHECKME

  gst_fakesrc_signals[SIGNAL_HANDOFF] =
    g_signal_newc ("handoff", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST,
                    G_STRUCT_OFFSET (GstFakeSrcClass, handoff), NULL, NULL,
                    g_cclosure_marshal_VOID__POINTER, G_TYPE_NONE, 1,
                    G_TYPE_POINTER);

  gobject_class->set_property = GST_DEBUG_FUNCPTR (gst_fakesrc_set_property);
  gobject_class->get_property = GST_DEBUG_FUNCPTR (gst_fakesrc_get_property);

  gstelement_class->request_new_pad = GST_DEBUG_FUNCPTR (gst_fakesrc_request_new_pad);
}

static void 
gst_fakesrc_init (GstFakeSrc *fakesrc) 
{
  GstPad *pad;

  // set the default number of 
  fakesrc->numsrcpads = 1;

  // create our first output pad
  pad = gst_pad_new ("src", GST_PAD_SRC);
  gst_element_add_pad (GST_ELEMENT (fakesrc), pad);
  fakesrc->srcpads = g_slist_append (NULL, pad);

  fakesrc->loop_based = FALSE;

  if (fakesrc->loop_based)
    gst_element_set_loop_function (GST_ELEMENT (fakesrc), GST_DEBUG_FUNCPTR (gst_fakesrc_loop));
  else
    gst_pad_set_get_function (pad, GST_DEBUG_FUNCPTR (gst_fakesrc_get));

  fakesrc->num_buffers = -1;
  fakesrc->buffer_count = 0;
  fakesrc->silent = FALSE;
  // we're ready right away, since we don't have any args...
//  gst_element_set_state(GST_ELEMENT(fakesrc),GST_STATE_READY);
}

static GstPad*
gst_fakesrc_request_new_pad (GstElement *element, GstPadTemplate *templ)
{
  gchar *name;
  GstPad *srcpad;
  GstFakeSrc *fakesrc;

  g_return_val_if_fail (GST_IS_FAKESRC (element), NULL);

  if (templ->direction != GST_PAD_SRC) {
    g_warning ("gstfakesrc: request new pad that is not a SRC pad\n");
    return NULL;
  }

  fakesrc = GST_FAKESRC (element);

  name = g_strdup_printf ("src%d", fakesrc->numsrcpads);

  srcpad = gst_pad_new_from_template (templ, name);
  gst_element_add_pad (GST_ELEMENT (fakesrc), srcpad);

  fakesrc->srcpads = g_slist_prepend (fakesrc->srcpads, srcpad);
  fakesrc->numsrcpads++;

  return srcpad;
}

static void
gst_fakesrc_update_functions (GstFakeSrc *src)
{
  GSList *pads;

  if (src->loop_based) {
    gst_element_set_loop_function (GST_ELEMENT (src), GST_DEBUG_FUNCPTR (gst_fakesrc_loop));
  }
  else {
    gst_element_set_loop_function (GST_ELEMENT (src), NULL);
  }

  pads = src->srcpads;
  while (pads) {
    GstPad *pad = GST_PAD (pads->data);

    if (src->loop_based) {
      gst_pad_set_get_function (pad, NULL);
    }
    else {
      gst_pad_set_get_function (pad, GST_DEBUG_FUNCPTR (gst_fakesrc_get));
    }
    pads = g_slist_next (pads);
  }
}

static void
gst_fakesrc_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  GstFakeSrc *src;

  /* it's not null if we got it, but it might not be ours */
  src = GST_FAKESRC (object);
   
  switch (prop_id) {
    case ARG_LOOP_BASED:
      src->loop_based = g_value_get_boolean (value);
      gst_fakesrc_update_functions (src);
      break;
    case ARG_OUTPUT:
      break;
    case ARG_PATTERN:
      break;
    case ARG_NUM_BUFFERS:
      src->num_buffers = g_value_get_int (value);
      break;
    case ARG_EOS:
      src->eos = g_value_get_boolean (value);
GST_INFO (0, "will EOS on next buffer");
      break;
    case ARG_SILENT:
      src->silent = g_value_get_boolean (value);
      break;
    default:
      break;
  }
}

static void 
gst_fakesrc_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  GstFakeSrc *src;
   
  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_FAKESRC (object));
  
  src = GST_FAKESRC (object);
   
  switch (prop_id) {
    case ARG_NUM_SOURCES:
      g_value_set_int (value, src->numsrcpads);
      break;
    case ARG_LOOP_BASED:
      g_value_set_boolean (value, src->loop_based);
      break;
    case ARG_OUTPUT:
      g_value_set_int (value, src->output);
      break;
    case ARG_PATTERN:
      g_value_set_string (value, src->pattern);
      break;
    case ARG_NUM_BUFFERS:
      g_value_set_int (value, src->num_buffers);
      break;
    case ARG_EOS:
      g_value_set_boolean (value, src->eos);
      break;
    case ARG_SILENT:
      g_value_set_boolean (value, src->silent);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


/**
 * gst_fakesrc_get:
 * @src: the faksesrc to get
 * 
 * generate an empty buffer and return it
 *
 * Returns: a new empty buffer
 */
static GstBuffer *
gst_fakesrc_get(GstPad *pad)
{
  GstFakeSrc *src;
  GstBuffer *buf;

  g_return_val_if_fail (pad != NULL, NULL);

  src = GST_FAKESRC (gst_pad_get_parent (pad));

  g_return_val_if_fail (GST_IS_FAKESRC (src), NULL);

  if (src->num_buffers == 0) {
    gst_pad_set_eos (pad);
    return NULL;
  }
  else {
    if (src->num_buffers > 0)
      src->num_buffers--;
  }

  if (src->eos) {
    GST_INFO (0, "fakesrc is setting eos on pad");
    gst_pad_set_eos (pad);
    return NULL;
  }

  buf = gst_buffer_new();
  GST_BUFFER_TIMESTAMP (buf) = src->buffer_count++;

  if (!src->silent)
    g_print("fakesrc: get      ******* (%s:%s)> (%d bytes, %llu) \n",
               GST_DEBUG_PAD_NAME (pad), GST_BUFFER_SIZE (buf), GST_BUFFER_TIMESTAMP (buf));

  g_signal_emit (G_OBJECT (src), gst_fakesrc_signals[SIGNAL_HANDOFF], 0,
                   buf);

  return buf;
}

/**
 * gst_fakesrc_loop:
 * @element: the faksesrc to loop
 * 
 * generate an empty buffer and push it to the next element.
 */
static void
gst_fakesrc_loop(GstElement *element)
{
  GstFakeSrc *src;

  g_return_if_fail(element != NULL);
  g_return_if_fail(GST_IS_FAKESRC(element));

  src = GST_FAKESRC (element);

  do {
    GSList *pads;

    pads = src->srcpads;

    while (pads) {
      GstPad *pad = GST_PAD (pads->data);
      GstBuffer *buf;

      if (src->num_buffers == 0) {
        gst_pad_set_eos (pad);
        return;
      }
      else {
      if (src->num_buffers > 0)
         src->num_buffers--;
      }

      if (src->eos) {
        GST_INFO (0, "fakesrc is setting eos on pad");
        gst_pad_set_eos (pad);
        return;
      }

      buf = gst_buffer_new();
      GST_BUFFER_TIMESTAMP (buf) = src->buffer_count++;

      if (!src->silent)
        g_print("fakesrc:  loop    ******* (%s:%s)  > (%d bytes, %llu) \n",
               GST_DEBUG_PAD_NAME (pad), GST_BUFFER_SIZE (buf), GST_BUFFER_TIMESTAMP (buf));

      g_signal_emit (G_OBJECT (src), gst_fakesrc_signals[SIGNAL_HANDOFF], 0,
                       buf);
      gst_pad_push (pad, buf);

      pads = g_slist_next (pads);
    }
  } while (!GST_ELEMENT_IS_COTHREAD_STOPPING (element));
}

gboolean
gst_fakesrc_factory_init (GstElementFactory *factory)
{
  gst_elementfactory_add_padtemplate (factory, GST_PADTEMPLATE_GET (fakesrc_src_factory));

  return TRUE;
}

