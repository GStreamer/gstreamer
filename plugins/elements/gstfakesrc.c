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


#include <stdlib.h>
#include <string.h>

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
  ARG_DATA,
  ARG_SIZETYPE,
  ARG_SIZEMIN,
  ARG_SIZEMAX,
  ARG_FILLTYPE,
  ARG_PATTERN,
  ARG_NUM_BUFFERS,
  ARG_EOS,
  ARG_SILENT,
  ARG_DUMP,
  ARG_PARENTSIZE,
  ARG_LAST_MESSAGE,
};

GST_PAD_TEMPLATE_FACTORY (fakesrc_src_factory,
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

#define GST_TYPE_FAKESRC_DATA (gst_fakesrc_data_get_type())
static GType
gst_fakesrc_data_get_type (void) 
{
  static GType fakesrc_data_type = 0;
  static GEnumValue fakesrc_data[] = {
    { FAKESRC_DATA_ALLOCATE, 		"2", "Allocate data"},
    { FAKESRC_DATA_SUBBUFFER, 		"3", "Subbuffer data"},
    {0, NULL, NULL},
  };
  if (!fakesrc_data_type) {
    fakesrc_data_type = g_enum_register_static ("GstFakeSrcData", fakesrc_data);
  }
  return fakesrc_data_type;
}

#define GST_TYPE_FAKESRC_SIZETYPE (gst_fakesrc_sizetype_get_type())
static GType
gst_fakesrc_sizetype_get_type (void) 
{
  static GType fakesrc_sizetype_type = 0;
  static GEnumValue fakesrc_sizetype[] = {
    { FAKESRC_SIZETYPE_NULL, 		"1", "Send empty buffers"},
    { FAKESRC_SIZETYPE_FIXED, 		"2", "Fixed size buffers (sizemax sized)"},
    { FAKESRC_SIZETYPE_RANDOM, 		"3", "Random sized buffers (sizemin <= size <= sizemax)"},
    {0, NULL, NULL},
  };
  if (!fakesrc_sizetype_type) {
    fakesrc_sizetype_type = g_enum_register_static ("GstFakeSrcSizeType", fakesrc_sizetype);
  }
  return fakesrc_sizetype_type;
}

#define GST_TYPE_FAKESRC_FILLTYPE (gst_fakesrc_filltype_get_type())
static GType
gst_fakesrc_filltype_get_type (void) 
{
  static GType fakesrc_filltype_type = 0;
  static GEnumValue fakesrc_filltype[] = {
    { FAKESRC_FILLTYPE_NOTHING, 	"1", "Leave data as malloced"},
    { FAKESRC_FILLTYPE_NULL, 		"2", "Fill buffers with zeros"},
    { FAKESRC_FILLTYPE_RANDOM, 		"3", "Fill buffers with random crap"},
    { FAKESRC_FILLTYPE_PATTERN, 	"4", "Fill buffers with pattern 0x00 -> 0xff"},
    { FAKESRC_FILLTYPE_PATTERN_CONT, 	"5", "Fill buffers with pattern 0x00 -> 0xff that spans buffers"},
    {0, NULL, NULL},
  };
  if (!fakesrc_filltype_type) {
    fakesrc_filltype_type = g_enum_register_static ("GstFakeSrcFillType", fakesrc_filltype);
  }
  return fakesrc_filltype_type;
}

static void		gst_fakesrc_class_init		(GstFakeSrcClass *klass);
static void		gst_fakesrc_init		(GstFakeSrc *fakesrc);

static GstPad* 		gst_fakesrc_request_new_pad 	(GstElement *element, GstPadTemplate *templ);
static void 		gst_fakesrc_update_functions 	(GstFakeSrc *src);
static void		gst_fakesrc_set_property	(GObject *object, guint prop_id, 
							 const GValue *value, GParamSpec *pspec);
static void		gst_fakesrc_get_property	(GObject *object, guint prop_id, 
							 GValue *value, GParamSpec *pspec);

static GstElementStateReturn gst_fakesrc_change_state 	(GstElement *element);

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
                         FALSE, G_PARAM_READWRITE)); /* CHECKME */
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_OUTPUT,
    g_param_spec_enum("output","output","output",
                      GST_TYPE_FAKESRC_OUTPUT,FAKESRC_FIRST_LAST_LOOP,G_PARAM_READWRITE)); /* CHECKME! */
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_DATA,
    g_param_spec_enum ("data", "data", "data",
                       GST_TYPE_FAKESRC_DATA, FAKESRC_DATA_ALLOCATE, G_PARAM_READWRITE)); 
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_SIZETYPE,
    g_param_spec_enum ("sizetype", "sizetype", "sizetype",
                       GST_TYPE_FAKESRC_SIZETYPE, FAKESRC_SIZETYPE_NULL, G_PARAM_READWRITE)); 
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_SIZEMIN,
    g_param_spec_int ("sizemin","sizemin","sizemin",
                      0, G_MAXINT, 0, G_PARAM_READWRITE)); 
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_SIZEMAX,
    g_param_spec_int ("sizemax","sizemax","sizemax",
                      0, G_MAXINT, 4096, G_PARAM_READWRITE)); 
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_PARENTSIZE,
    g_param_spec_int ("parentsize","parentsize","parentsize",
                      0, G_MAXINT, 4096 * 10, G_PARAM_READWRITE)); 
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_FILLTYPE,
    g_param_spec_enum ("filltype", "filltype", "filltype",
                       GST_TYPE_FAKESRC_FILLTYPE, FAKESRC_FILLTYPE_NULL, G_PARAM_READWRITE)); 
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_PATTERN,
    g_param_spec_string("pattern","pattern","pattern",
                        NULL, G_PARAM_READWRITE)); /* CHECKME */
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_NUM_BUFFERS,
    g_param_spec_int("num_buffers","num_buffers","num_buffers",
                     G_MININT,G_MAXINT,0,G_PARAM_READWRITE)); /* CHECKME */
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_EOS,
    g_param_spec_boolean("eos","eos","eos",
                         TRUE,G_PARAM_READWRITE)); /* CHECKME */
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_LAST_MESSAGE,
    g_param_spec_string ("last_message", "last_message", "last_message",
                         NULL, G_PARAM_READABLE)); 

  gst_element_class_install_std_props (
	  GST_ELEMENT_CLASS (klass),
	  "silent", ARG_SILENT, G_PARAM_READWRITE,
	  "dump",   ARG_DUMP,   G_PARAM_READWRITE,
	  NULL);

  gst_fakesrc_signals[SIGNAL_HANDOFF] =
    g_signal_new ("handoff", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST,
                    G_STRUCT_OFFSET (GstFakeSrcClass, handoff), NULL, NULL,
                    g_cclosure_marshal_VOID__POINTER, G_TYPE_NONE, 1,
                    G_TYPE_POINTER);

  gobject_class->set_property = GST_DEBUG_FUNCPTR (gst_fakesrc_set_property);
  gobject_class->get_property = GST_DEBUG_FUNCPTR (gst_fakesrc_get_property);

  gstelement_class->request_new_pad = 	GST_DEBUG_FUNCPTR (gst_fakesrc_request_new_pad);
  gstelement_class->change_state = 	GST_DEBUG_FUNCPTR (gst_fakesrc_change_state);
}

static void 
gst_fakesrc_init (GstFakeSrc *fakesrc) 
{
  GstPad *pad;

  /* create our first output pad */
  pad = gst_pad_new ("src", GST_PAD_SRC);
  gst_element_add_pad (GST_ELEMENT (fakesrc), pad);

  fakesrc->loop_based = FALSE;
  gst_fakesrc_update_functions (fakesrc);

  fakesrc->output = FAKESRC_FIRST_LAST_LOOP;
  fakesrc->num_buffers = -1;
  fakesrc->rt_num_buffers = -1;
  fakesrc->buffer_count = 0;
  fakesrc->silent = FALSE;
  fakesrc->dump = FALSE;
  fakesrc->pattern_byte = 0x00;
  fakesrc->need_flush = FALSE;
  fakesrc->data = FAKESRC_DATA_ALLOCATE;
  fakesrc->sizetype = FAKESRC_SIZETYPE_NULL;
  fakesrc->filltype = FAKESRC_FILLTYPE_NOTHING;
  fakesrc->sizemin = 0;
  fakesrc->sizemax = 4096;
  fakesrc->parent = NULL;
  fakesrc->parentsize = 4096 * 10;
  fakesrc->last_message = NULL;
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

  name = g_strdup_printf ("src%d", GST_ELEMENT (fakesrc)->numsrcpads);

  srcpad = gst_pad_new_from_template (templ, name);
  gst_element_add_pad (GST_ELEMENT (fakesrc), srcpad);

  g_free (name);

  return srcpad;
}

static gboolean
gst_fakesrc_event_handler (GstPad *pad, GstEvent *event)
{
  GstFakeSrc *src;

  src = GST_FAKESRC (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
      src->buffer_count = GST_EVENT_SEEK_OFFSET (event);

      if (!GST_EVENT_SEEK_FLUSH (event)) {
        gst_event_free (event);
        break;
      }
      /* else we do a flush too */
    case GST_EVENT_FLUSH:
      src->need_flush = TRUE;
      break;
    default:
      break;
  }

  return TRUE;
}

static void
gst_fakesrc_update_functions (GstFakeSrc *src)
{
  GList *pads;

  if (src->loop_based) {
    gst_element_set_loop_function (GST_ELEMENT (src), GST_DEBUG_FUNCPTR (gst_fakesrc_loop));
  }
  else {
    gst_element_set_loop_function (GST_ELEMENT (src), NULL);
  }

  pads = GST_ELEMENT (src)->pads;
  while (pads) {
    GstPad *pad = GST_PAD (pads->data);

    if (src->loop_based) {
      gst_pad_set_get_function (pad, NULL);
    }
    else {
      gst_pad_set_get_function (pad, GST_DEBUG_FUNCPTR (gst_fakesrc_get));
    }

    gst_pad_set_event_function (pad, gst_fakesrc_event_handler);
    pads = g_list_next (pads);
  }
}

static void
gst_fakesrc_alloc_parent (GstFakeSrc *src)
{
  GstBuffer *buf;

  buf = gst_buffer_new ();
  GST_BUFFER_DATA (buf) = g_malloc (src->parentsize);
  GST_BUFFER_SIZE (buf) = src->parentsize;

  src->parent = buf;
  src->parentoffset = 0;
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
      g_warning ("not yet implemented");
      break;
    case ARG_DATA:
      src->data = g_value_get_enum (value);
      switch (src->data) {
	case FAKESRC_DATA_ALLOCATE:
          if (src->parent) {
            gst_buffer_unref (src->parent);
            src->parent = NULL;
	  }
          break;
	case FAKESRC_DATA_SUBBUFFER:
	  if (!src->parent)
	    gst_fakesrc_alloc_parent (src);
	default:
          break;
      }
      break;
    case ARG_SIZETYPE:
      src->sizetype = g_value_get_enum (value);
      break;
    case ARG_SIZEMIN:
      src->sizemin = g_value_get_int (value);
      break;
    case ARG_SIZEMAX:
      src->sizemax = g_value_get_int (value);
      break;
    case ARG_PARENTSIZE:
      src->parentsize = g_value_get_int (value);
      break;
    case ARG_FILLTYPE:
      src->filltype = g_value_get_enum (value);
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
    case ARG_DUMP:
      src->dump = g_value_get_boolean (value);
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
      g_value_set_int (value, GST_ELEMENT (src)->numsrcpads);
      break;
    case ARG_LOOP_BASED:
      g_value_set_boolean (value, src->loop_based);
      break;
    case ARG_OUTPUT:
      g_value_set_enum (value, src->output);
      break;
    case ARG_DATA:
      g_value_set_enum (value, src->data);
      break;
    case ARG_SIZETYPE:
      g_value_set_enum (value, src->sizetype);
      break;
    case ARG_SIZEMIN:
      g_value_set_int (value, src->sizemin);
      break;
    case ARG_SIZEMAX:
      g_value_set_int (value, src->sizemax);
      break;
    case ARG_PARENTSIZE:
      g_value_set_int (value, src->parentsize);
      break;
    case ARG_FILLTYPE:
      g_value_set_enum (value, src->filltype);
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
    case ARG_DUMP:
      g_value_set_boolean (value, src->dump);
      break;
    case ARG_LAST_MESSAGE:
      g_value_set_string (value, src->last_message);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_fakesrc_prepare_buffer (GstFakeSrc *src, GstBuffer *buf)
{
  if (GST_BUFFER_SIZE (buf) == 0) 
    return;

  switch (src->filltype) {
    case FAKESRC_FILLTYPE_NULL:
      memset (GST_BUFFER_DATA (buf), 0, GST_BUFFER_SIZE (buf));
      break;
    case FAKESRC_FILLTYPE_RANDOM:
    {
      gint i;
      guint8 *ptr = GST_BUFFER_DATA (buf);

      for (i = GST_BUFFER_SIZE (buf); i; i--) {
	*ptr++ = (gint8)((255.0)*rand()/(RAND_MAX));
      }
      break;
    }
    case FAKESRC_FILLTYPE_PATTERN:
      src->pattern_byte = 0x00;
    case FAKESRC_FILLTYPE_PATTERN_CONT:
    {
      gint i;
      guint8 *ptr = GST_BUFFER_DATA (buf);

      for (i = GST_BUFFER_SIZE (buf); i; i--) {
	*ptr++ = src->pattern_byte++;
      }
      break;
    }
    case FAKESRC_FILLTYPE_NOTHING:
    default:
      break;
  }
}

static GstBuffer*
gst_fakesrc_alloc_buffer (GstFakeSrc *src, guint size)
{
  GstBuffer *buf;

  buf = gst_buffer_new ();
  GST_BUFFER_SIZE(buf) = size;

  if (size != 0) { 
    switch (src->filltype) {
      case FAKESRC_FILLTYPE_NOTHING:
        GST_BUFFER_DATA(buf) = g_malloc (size);
        break;
      case FAKESRC_FILLTYPE_NULL:
        GST_BUFFER_DATA(buf) = g_malloc0 (size);
        break;
      case FAKESRC_FILLTYPE_RANDOM:
      case FAKESRC_FILLTYPE_PATTERN:
      case FAKESRC_FILLTYPE_PATTERN_CONT:
      default:
        GST_BUFFER_DATA(buf) = g_malloc (size);
        gst_fakesrc_prepare_buffer (src, buf);
        break;
    }
  }

  return buf;
}

static guint
gst_fakesrc_get_size (GstFakeSrc *src)
{
  guint size;

  switch (src->sizetype) {
    case FAKESRC_SIZETYPE_FIXED:
      size = src->sizemax;
      break;
    case FAKESRC_SIZETYPE_RANDOM:
      size = src->sizemin + (guint8)(((gfloat)src->sizemax)*rand()/(RAND_MAX + (gfloat)src->sizemin));
      break;
    case FAKESRC_SIZETYPE_NULL:
    default:
      size = 0; 
      break;
  }

  return size;
}

static GstBuffer *
gst_fakesrc_create_buffer (GstFakeSrc *src)
{
  GstBuffer *buf;
  guint size;
  gboolean dump = src->dump;

  size = gst_fakesrc_get_size (src);
  if (size == 0)
    return gst_buffer_new();

  switch (src->data) {
    case FAKESRC_DATA_ALLOCATE:
      buf = gst_fakesrc_alloc_buffer (src, size);
      break;
    case FAKESRC_DATA_SUBBUFFER:
      /* see if we have a parent to subbuffer */
      if (!src->parent) {
	gst_fakesrc_alloc_parent (src);
	g_assert (src->parent);
      }
      /* see if it's large enough */
      if ((GST_BUFFER_SIZE (src->parent) - src->parentoffset) >= size) {
	 buf = gst_buffer_create_sub (src->parent, src->parentoffset, size);
	 src->parentoffset += size;
      }
      else {
	/* the parent is useless now */
	gst_buffer_unref (src->parent);
	src->parent = NULL;
	/* try again (this will allocate a new parent) */
        return gst_fakesrc_create_buffer (src);
      }
      gst_fakesrc_prepare_buffer (src, buf);
      break;
    default:
      g_warning ("fakesrc: dunno how to allocate buffers !");
      buf = gst_buffer_new();
      break;
  }
  if (dump) {
    gst_util_dump_mem (GST_BUFFER_DATA (buf), GST_BUFFER_SIZE (buf));
  }

  return buf;
}

static GstBuffer *
gst_fakesrc_get(GstPad *pad)
{
  GstFakeSrc *src;
  GstBuffer *buf;

  g_return_val_if_fail (pad != NULL, NULL);

  src = GST_FAKESRC (gst_pad_get_parent (pad));

  g_return_val_if_fail (GST_IS_FAKESRC (src), NULL);

  if (src->need_flush) {
    src->need_flush = FALSE;
    return GST_BUFFER(gst_event_new (GST_EVENT_FLUSH));
  }

  if (src->rt_num_buffers == 0) {
    gst_element_set_eos (GST_ELEMENT (src));
    return GST_BUFFER(gst_event_new (GST_EVENT_EOS));
  }
  else {
    if (src->rt_num_buffers > 0)
      src->rt_num_buffers--;
  }

  if (src->eos) {
    GST_INFO (0, "fakesrc is setting eos on pad");
    return GST_BUFFER(gst_event_new (GST_EVENT_EOS));
  }

  buf = gst_fakesrc_create_buffer (src);
  GST_BUFFER_TIMESTAMP (buf) = src->buffer_count++;

  if (!src->silent) {
    if (src->last_message)
      g_free (src->last_message);

    src->last_message = g_strdup_printf ("get      ******* (%s:%s)> (%d bytes, %llu)",
                      GST_DEBUG_PAD_NAME (pad), GST_BUFFER_SIZE (buf), GST_BUFFER_TIMESTAMP (buf));

    g_object_notify (G_OBJECT (src), "last_message");
  }

  GST_DEBUG_ELEMENT (GST_CAT_DATAFLOW, src, "pre handoff emit\n");
  g_signal_emit (G_OBJECT (src), gst_fakesrc_signals[SIGNAL_HANDOFF], 0,
                   buf, pad);
  GST_DEBUG_ELEMENT (GST_CAT_DATAFLOW, src, "post handoff emit\n");

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
  GList *pads;

  g_return_if_fail(element != NULL);
  g_return_if_fail(GST_IS_FAKESRC(element));

  src = GST_FAKESRC (element);

  pads = gst_element_get_pad_list (element);

  while (pads) {
    GstPad *pad = GST_PAD (pads->data);
    GstBuffer *buf;

    if (src->rt_num_buffers == 0) {
      src->eos = TRUE;
    }
    else {
      if (src->rt_num_buffers > 0)
        src->rt_num_buffers--;
    }

    if (src->eos) {
      gst_pad_push(pad, GST_BUFFER(gst_event_new (GST_EVENT_EOS)));
      return;
    }

    buf = gst_fakesrc_create_buffer (src);
    GST_BUFFER_TIMESTAMP (buf) = src->buffer_count++;

    if (!src->silent) {
      if (src->last_message)
        g_free (src->last_message);

      src->last_message = g_strdup_printf ("fakesrc:  loop    ******* (%s:%s)  > (%d bytes, %llu)",
                      GST_DEBUG_PAD_NAME (pad), GST_BUFFER_SIZE (buf), GST_BUFFER_TIMESTAMP (buf));

      g_object_notify (G_OBJECT (src), "last_message");
    }

    g_signal_emit (G_OBJECT (src), gst_fakesrc_signals[SIGNAL_HANDOFF], 0,
                       buf, pad);
    gst_pad_push (pad, buf);

    pads = g_list_next (pads);
  }
}

static GstElementStateReturn
gst_fakesrc_change_state (GstElement *element)
{
  GstFakeSrc *fakesrc;

  g_return_val_if_fail (GST_IS_FAKESRC (element), GST_STATE_FAILURE);

  fakesrc = GST_FAKESRC (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_PAUSED_TO_READY:
    case GST_STATE_NULL_TO_READY:
      fakesrc->buffer_count = 0;
      fakesrc->pattern_byte = 0x00;
      fakesrc->need_flush = FALSE;
      fakesrc->eos = FALSE;
      fakesrc->rt_num_buffers = fakesrc->num_buffers;
      if (fakesrc->parent) {
        gst_buffer_unref (fakesrc->parent);
        fakesrc->parent = NULL;
      }
      break;
    case GST_STATE_READY_TO_PAUSED:
    case GST_STATE_PAUSED_TO_PLAYING:
    case GST_STATE_PLAYING_TO_PAUSED:
    case GST_STATE_READY_TO_NULL:
      break;
    default:
      g_assert_not_reached ();
      break;
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);

  return GST_STATE_SUCCESS;
}

gboolean
gst_fakesrc_factory_init (GstElementFactory *factory)
{
  gst_element_factory_add_pad_template (factory, GST_PAD_TEMPLATE_GET (fakesrc_src_factory));

  return TRUE;
}

