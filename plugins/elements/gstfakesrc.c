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
  "Wim Taymans <wim.taymans@chello.be>"
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
};

#define GST_TYPE_FAKESRC_OUTPUT (gst_fakesrc_output_get_type())
static GtkType
gst_fakesrc_output_get_type(void) {
  static GtkType fakesrc_output_type = 0;
  static GtkEnumValue fakesrc_output[] = {
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
    fakesrc_output_type = gtk_type_register_enum("GstFakeSrcOutput", fakesrc_output);
  }
  return fakesrc_output_type;
}

static void		gst_fakesrc_class_init	(GstFakeSrcClass *klass);
static void		gst_fakesrc_init	(GstFakeSrc *fakesrc);

static void		gst_fakesrc_set_arg	(GtkObject *object, GtkArg *arg, guint id);
static void		gst_fakesrc_get_arg	(GtkObject *object, GtkArg *arg, guint id);

static GstBuffer*	gst_fakesrc_get		(GstPad *pad);
static void 		gst_fakesrc_loop	(GstElement *element);

static GstElementClass *parent_class = NULL;
static guint gst_fakesrc_signals[LAST_SIGNAL] = { 0 };

GtkType
gst_fakesrc_get_type (void) 
{
  static GtkType fakesrc_type = 0;

  if (!fakesrc_type) {
    static const GtkTypeInfo fakesrc_info = {
      "GstFakeSrc",
      sizeof(GstFakeSrc),
      sizeof(GstFakeSrcClass),
      (GtkClassInitFunc)gst_fakesrc_class_init,
      (GtkObjectInitFunc)gst_fakesrc_init,
      (GtkArgSetFunc)NULL,
      (GtkArgGetFunc)NULL,
      (GtkClassInitFunc)NULL,
    };
    fakesrc_type = gtk_type_unique (GST_TYPE_ELEMENT, &fakesrc_info);
  }
  return fakesrc_type;
}

static void
gst_fakesrc_class_init (GstFakeSrcClass *klass) 
{
  GtkObjectClass *gtkobject_class;

  gtkobject_class = (GtkObjectClass*)klass;

  parent_class = gtk_type_class (GST_TYPE_ELEMENT);

  gtk_object_add_arg_type ("GstFakeSrc::num_sources", GTK_TYPE_INT,
                           GTK_ARG_READWRITE, ARG_NUM_SOURCES);
  gtk_object_add_arg_type ("GstFakeSrc::loop_based", GTK_TYPE_BOOL,
                           GTK_ARG_READWRITE, ARG_LOOP_BASED);
  gtk_object_add_arg_type ("GstFakeSrc::output", GST_TYPE_FAKESRC_OUTPUT,
                           GTK_ARG_READWRITE, ARG_OUTPUT);
  gtk_object_add_arg_type ("GstFakeSrc::pattern", GTK_TYPE_STRING,
                           GTK_ARG_READWRITE, ARG_PATTERN);
  gtk_object_add_arg_type ("GstFakeSrc::num_buffers", GTK_TYPE_INT,
                           GTK_ARG_READWRITE, ARG_NUM_BUFFERS);
  gtk_object_add_arg_type ("GstFakeSrc::eos", GTK_TYPE_BOOL,
                           GTK_ARG_READWRITE, ARG_EOS);

  gtkobject_class->set_arg = gst_fakesrc_set_arg;
  gtkobject_class->get_arg = gst_fakesrc_get_arg;

  gst_fakesrc_signals[SIGNAL_HANDOFF] =
    gtk_signal_new ("handoff", GTK_RUN_LAST, gtkobject_class->type,
                    GTK_SIGNAL_OFFSET (GstFakeSrcClass, handoff),
                    gtk_marshal_NONE__NONE, GTK_TYPE_NONE, 0);

  gtk_object_class_add_signals (gtkobject_class, gst_fakesrc_signals,
                                LAST_SIGNAL);
}

static void 
gst_fakesrc_init (GstFakeSrc *fakesrc) 
{
  GstPad *pad;

  // set the default number of 
  fakesrc->numsrcpads = 1;

  // create our first output pad
  pad = gst_pad_new("src",GST_PAD_SRC);
  gst_element_add_pad(GST_ELEMENT(fakesrc),pad);
  fakesrc->srcpads = g_slist_append(NULL,pad);

  fakesrc->loop_based = TRUE;

  if (fakesrc->loop_based)
    gst_element_set_loop_function (GST_ELEMENT (fakesrc), gst_fakesrc_loop);
  else
    gst_pad_set_get_function(pad,gst_fakesrc_get);

  fakesrc->num_buffers = -1;
  // we're ready right away, since we don't have any args...
//  gst_element_set_state(GST_ELEMENT(fakesrc),GST_STATE_READY);
}

static void
gst_fakesrc_update_functions (GstFakeSrc *src)
{
  GSList *pads;

  pads = src->srcpads;
  while (pads) {
    GstPad *pad = GST_PAD (pads->data);

    if (src->loop_based) {
      gst_element_set_loop_function (GST_ELEMENT (src), gst_fakesrc_loop);
      gst_pad_set_get_function (pad, NULL);
    }
    else {
      gst_pad_set_get_function (pad, gst_fakesrc_get);
      gst_element_set_loop_function (GST_ELEMENT (src), NULL);
    }
    pads = g_slist_next (pads);
  }
}

static void
gst_fakesrc_set_arg (GtkObject *object, GtkArg *arg, guint id)
{
  GstFakeSrc *src;
  gint new_numsrcs;
  GstPad *pad;

  /* it's not null if we got it, but it might not be ours */
  src = GST_FAKESRC (object);
   
  switch(id) {
    case ARG_NUM_SOURCES:
      new_numsrcs = GTK_VALUE_INT (*arg);
      if (new_numsrcs > src->numsrcpads) {
        while (src->numsrcpads != new_numsrcs) {
          pad = gst_pad_new(g_strdup_printf("src%d",src->numsrcpads),GST_PAD_SRC);
          gst_element_add_pad(GST_ELEMENT(src),pad);
          src->srcpads = g_slist_append(src->srcpads,pad);
          src->numsrcpads++;
        }
        gst_fakesrc_update_functions (src);
      }
      break;
    case ARG_LOOP_BASED:
      src->loop_based = GTK_VALUE_BOOL (*arg);
      gst_fakesrc_update_functions (src);
      break;
    case ARG_OUTPUT:
      break;
    case ARG_PATTERN:
      break;
    case ARG_NUM_BUFFERS:
      src->num_buffers = GTK_VALUE_INT (*arg);
      break;
    case ARG_EOS:
      src->eos = GTK_VALUE_BOOL (*arg);
GST_INFO (0, "will EOS on next buffer");
      break;
    default:
      break;
  }
}

static void 
gst_fakesrc_get_arg (GtkObject *object, GtkArg *arg, guint id)
{
  GstFakeSrc *src;
   
  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_FAKESRC (object));
  
  src = GST_FAKESRC (object);
   
  switch (id) {
    case ARG_NUM_SOURCES:
      GTK_VALUE_INT (*arg) = src->numsrcpads;
      break;
    case ARG_LOOP_BASED:
      GTK_VALUE_BOOL (*arg) = src->loop_based;
      break;
    case ARG_OUTPUT:
      GTK_VALUE_INT (*arg) = src->output;
      break;
    case ARG_PATTERN:
      GTK_VALUE_STRING (*arg) = src->pattern;
      break;
    case ARG_NUM_BUFFERS:
      GTK_VALUE_INT (*arg) = src->num_buffers;
      break;
    case ARG_EOS:
      GTK_VALUE_BOOL (*arg) = src->eos;
    default:
      arg->type = GTK_TYPE_INVALID;
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

  g_print("fakesrc: ******* (%s:%s)> \n",GST_DEBUG_PAD_NAME(pad));
  buf = gst_buffer_new();

  gtk_signal_emit (GTK_OBJECT (src), gst_fakesrc_signals[SIGNAL_HANDOFF],
                                  src);

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
      g_print("fakesrc: ******* (%s:%s)> \n",GST_DEBUG_PAD_NAME(pad));

      gtk_signal_emit (GTK_OBJECT (src), gst_fakesrc_signals[SIGNAL_HANDOFF],
                                  src);
      gst_pad_push (pad, buf);

      pads = g_slist_next (pads);
    }
  } while (!GST_ELEMENT_IS_COTHREAD_STOPPING (element));
}
