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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "gstfakesrc.h"
#include <gst/gstmarshal.h>

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

GST_DEBUG_CATEGORY_STATIC (gst_fakesrc_debug);
#define GST_CAT_DEFAULT gst_fakesrc_debug

GstElementDetails gst_fakesrc_details = GST_ELEMENT_DETAILS ("Fake Source",
    "Source",
    "Push empty (no data) buffers around",
    "Erik Walthinsen <omega@cse.ogi.edu>, "
    "Wim Taymans <wim.taymans@chello.be>");


/* FakeSrc signals and args */
enum
{
  /* FILL ME */
  SIGNAL_HANDOFF,
  LAST_SIGNAL
};

#define DEFAULT_OUTPUT		FAKESRC_FIRST_LAST_LOOP
#define DEFAULT_DATA		FAKESRC_DATA_ALLOCATE
#define DEFAULT_SIZETYPE  	FAKESRC_SIZETYPE_NULL
#define DEFAULT_SIZEMIN		0
#define DEFAULT_SIZEMAX		4096
#define DEFAULT_FILLTYPE 	FAKESRC_FILLTYPE_NULL
#define DEFAULT_DATARATE	0
#define DEFAULT_SYNC		FALSE
#define DEFAULT_PATTERN		NULL
#define DEFAULT_EOS		FALSE
#define DEFAULT_SIGNAL_HANDOFFS FALSE
#define DEFAULT_SILENT		FALSE
#define DEFAULT_DUMP		FALSE
#define DEFAULT_PARENTSIZE	4096*10

enum
{
  PROP_0,
  PROP_OUTPUT,
  PROP_DATA,
  PROP_SIZETYPE,
  PROP_SIZEMIN,
  PROP_SIZEMAX,
  PROP_FILLTYPE,
  PROP_DATARATE,
  PROP_SYNC,
  PROP_PATTERN,
  PROP_EOS,
  PROP_SIGNAL_HANDOFFS,
  PROP_SILENT,
  PROP_DUMP,
  PROP_PARENTSIZE,
  PROP_LAST_MESSAGE,
  PROP_HAS_LOOP,
  PROP_HAS_GETRANGE,
  PROP_IS_LIVE
};

/* not implemented
#define GST_TYPE_FAKESRC_OUTPUT (gst_fakesrc_output_get_type())
static GType
gst_fakesrc_output_get_type (void)
{
  static GType fakesrc_output_type = 0;
  static GEnumValue fakesrc_output[] = {
    {FAKESRC_FIRST_LAST_LOOP, "1", "First-Last loop"},
    {FAKESRC_LAST_FIRST_LOOP, "2", "Last-First loop"},
    {FAKESRC_PING_PONG, "3", "Ping-Pong"},
    {FAKESRC_ORDERED_RANDOM, "4", "Ordered Random"},
    {FAKESRC_RANDOM, "5", "Random"},
    {FAKESRC_PATTERN_LOOP, "6", "Patttern loop"},
    {FAKESRC_PING_PONG_PATTERN, "7", "Ping-Pong Pattern"},
    {FAKESRC_GET_ALWAYS_SUCEEDS, "8", "'_get' Always succeeds"},
    {0, NULL, NULL},
  };

  if (!fakesrc_output_type) {
    fakesrc_output_type =
        g_enum_register_static ("GstFakeSrcOutput", fakesrc_output);
  }
  return fakesrc_output_type;
}
*/

#define GST_TYPE_FAKESRC_DATA (gst_fakesrc_data_get_type())
static GType
gst_fakesrc_data_get_type (void)
{
  static GType fakesrc_data_type = 0;
  static GEnumValue fakesrc_data[] = {
    {FAKESRC_DATA_ALLOCATE, "1", "Allocate data"},
    {FAKESRC_DATA_SUBBUFFER, "2", "Subbuffer data"},
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
    {FAKESRC_SIZETYPE_NULL, "1", "Send empty buffers"},
    {FAKESRC_SIZETYPE_FIXED, "2", "Fixed size buffers (sizemax sized)"},
    {FAKESRC_SIZETYPE_RANDOM, "3",
        "Random sized buffers (sizemin <= size <= sizemax)"},
    {0, NULL, NULL},
  };

  if (!fakesrc_sizetype_type) {
    fakesrc_sizetype_type =
        g_enum_register_static ("GstFakeSrcSizeType", fakesrc_sizetype);
  }
  return fakesrc_sizetype_type;
}

#define GST_TYPE_FAKESRC_FILLTYPE (gst_fakesrc_filltype_get_type())
static GType
gst_fakesrc_filltype_get_type (void)
{
  static GType fakesrc_filltype_type = 0;
  static GEnumValue fakesrc_filltype[] = {
    {FAKESRC_FILLTYPE_NOTHING, "1", "Leave data as malloced"},
    {FAKESRC_FILLTYPE_NULL, "2", "Fill buffers with zeros"},
    {FAKESRC_FILLTYPE_RANDOM, "3", "Fill buffers with random crap"},
    {FAKESRC_FILLTYPE_PATTERN, "4", "Fill buffers with pattern 0x00 -> 0xff"},
    {FAKESRC_FILLTYPE_PATTERN_CONT, "5",
        "Fill buffers with pattern 0x00 -> 0xff that spans buffers"},
    {0, NULL, NULL},
  };

  if (!fakesrc_filltype_type) {
    fakesrc_filltype_type =
        g_enum_register_static ("GstFakeSrcFillType", fakesrc_filltype);
  }
  return fakesrc_filltype_type;
}

#define _do_init(bla) \
    GST_DEBUG_CATEGORY_INIT (gst_fakesrc_debug, "fakesrc", 0, "fakesrc element");

GST_BOILERPLATE_FULL (GstFakeSrc, gst_fakesrc, GstBaseSrc, GST_TYPE_BASE_SRC,
    _do_init);

static void gst_fakesrc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_fakesrc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_fakesrc_start (GstBaseSrc * basesrc);
static gboolean gst_fakesrc_stop (GstBaseSrc * basesrc);

static gboolean gst_fakesrc_event_handler (GstBaseSrc * src, GstEvent * event);
static GstFlowReturn gst_fakesrc_create (GstBaseSrc * src, guint64 offset,
    guint length, GstBuffer ** buf);

static guint gst_fakesrc_signals[LAST_SIGNAL] = { 0 };

static void
gst_fakesrc_base_init (gpointer g_class)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&srctemplate));

  gst_element_class_set_details (gstelement_class, &gst_fakesrc_details);
}

static void
gst_fakesrc_class_init (GstFakeSrcClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseSrcClass *gstbase_src_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbase_src_class = (GstBaseSrcClass *) klass;

  gobject_class->set_property = GST_DEBUG_FUNCPTR (gst_fakesrc_set_property);
  gobject_class->get_property = GST_DEBUG_FUNCPTR (gst_fakesrc_get_property);

/*
  FIXME: this is not implemented; would make sense once basesrc and fakesrc
  support multiple pads
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_OUTPUT,
      g_param_spec_enum ("output", "output", "Output method (currently unused)",
          GST_TYPE_FAKESRC_OUTPUT, DEFAULT_OUTPUT, G_PARAM_READWRITE));
*/
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_DATA,
      g_param_spec_enum ("data", "data", "Data allocation method",
          GST_TYPE_FAKESRC_DATA, DEFAULT_DATA, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_SIZETYPE,
      g_param_spec_enum ("sizetype", "sizetype",
          "How to determine buffer sizes", GST_TYPE_FAKESRC_SIZETYPE,
          DEFAULT_SIZETYPE, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_SIZEMIN,
      g_param_spec_int ("sizemin", "sizemin", "Minimum buffer size", 0,
          G_MAXINT, DEFAULT_SIZEMIN, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_SIZEMAX,
      g_param_spec_int ("sizemax", "sizemax", "Maximum buffer size", 0,
          G_MAXINT, DEFAULT_SIZEMAX, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_PARENTSIZE,
      g_param_spec_int ("parentsize", "parentsize",
          "Size of parent buffer for sub-buffered allocation", 0, G_MAXINT,
          DEFAULT_PARENTSIZE, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_FILLTYPE,
      g_param_spec_enum ("filltype", "filltype",
          "How to fill the buffer, if at all", GST_TYPE_FAKESRC_FILLTYPE,
          DEFAULT_FILLTYPE, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_DATARATE,
      g_param_spec_int ("datarate", "Datarate",
          "Timestamps buffers with number of bytes per second (0 = none)", 0,
          G_MAXINT, DEFAULT_DATARATE, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_SYNC,
      g_param_spec_boolean ("sync", "Sync", "Sync to the clock to the datarate",
          DEFAULT_SYNC, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_PATTERN,
      g_param_spec_string ("pattern", "pattern", "pattern", DEFAULT_PATTERN,
          G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_LAST_MESSAGE,
      g_param_spec_string ("last-message", "last-message",
          "The last status message", NULL, G_PARAM_READABLE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_SILENT,
      g_param_spec_boolean ("silent", "Silent",
          "Don't produce last_message events", DEFAULT_SILENT,
          G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_SIGNAL_HANDOFFS,
      g_param_spec_boolean ("signal-handoffs", "Signal handoffs",
          "Send a signal before pushing the buffer", DEFAULT_SIGNAL_HANDOFFS,
          G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_DUMP,
      g_param_spec_boolean ("dump", "Dump", "Dump produced bytes to stdout",
          DEFAULT_DUMP, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_HAS_LOOP,
      g_param_spec_boolean ("has-loop", "Has loop function",
          "True if the element exposes a loop function", TRUE,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_HAS_GETRANGE,
      g_param_spec_boolean ("has-getrange", "Has getrange function",
          "True if the element exposes a getrange function", TRUE,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_IS_LIVE,
      g_param_spec_boolean ("is-live", "Is this a live source",
          "True if the element cannot produce data in PAUSED", FALSE,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

  gst_fakesrc_signals[SIGNAL_HANDOFF] =
      g_signal_new ("handoff", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstFakeSrcClass, handoff), NULL, NULL,
      gst_marshal_VOID__OBJECT_OBJECT, G_TYPE_NONE, 1, G_TYPE_OBJECT);

  /*gstbase_src_class->is_seekable = GST_DEBUG_FUNCPTR (gst_fakesrc_is_seekable); */
  gstbase_src_class->start = GST_DEBUG_FUNCPTR (gst_fakesrc_start);
  gstbase_src_class->stop = GST_DEBUG_FUNCPTR (gst_fakesrc_stop);
  gstbase_src_class->event = GST_DEBUG_FUNCPTR (gst_fakesrc_event_handler);
  gstbase_src_class->create = GST_DEBUG_FUNCPTR (gst_fakesrc_create);
}

static void
gst_fakesrc_init (GstFakeSrc * fakesrc)
{
  fakesrc->output = FAKESRC_FIRST_LAST_LOOP;
  fakesrc->segment_start = -1;
  fakesrc->segment_end = -1;
  fakesrc->buffer_count = 0;
  fakesrc->silent = DEFAULT_SILENT;
  fakesrc->signal_handoffs = DEFAULT_SIGNAL_HANDOFFS;
  fakesrc->dump = DEFAULT_DUMP;
  fakesrc->pattern_byte = 0x00;
  fakesrc->data = FAKESRC_DATA_ALLOCATE;
  fakesrc->sizetype = FAKESRC_SIZETYPE_NULL;
  fakesrc->filltype = FAKESRC_FILLTYPE_NOTHING;
  fakesrc->sizemin = DEFAULT_SIZEMIN;
  fakesrc->sizemax = DEFAULT_SIZEMAX;
  fakesrc->parent = NULL;
  fakesrc->parentsize = DEFAULT_PARENTSIZE;
  fakesrc->last_message = NULL;
  fakesrc->datarate = DEFAULT_DATARATE;
  fakesrc->sync = DEFAULT_SYNC;
}

static gboolean
gst_fakesrc_event_handler (GstBaseSrc * basesrc, GstEvent * event)
{
  GstFakeSrc *src;

  src = GST_FAKESRC (basesrc);

  if (!src->silent) {
    g_free (src->last_message);

    src->last_message =
        g_strdup_printf ("event   ******* E (type: %d) %p",
        GST_EVENT_TYPE (event), event);

    g_object_notify (G_OBJECT (src), "last_message");
  }


  return TRUE;
}

static void
gst_fakesrc_alloc_parent (GstFakeSrc * src)
{
  GstBuffer *buf;

  buf = gst_buffer_new ();
  GST_BUFFER_DATA (buf) = g_malloc (src->parentsize);
  GST_BUFFER_SIZE (buf) = src->parentsize;

  src->parent = buf;
  src->parentoffset = 0;
}

static void
gst_fakesrc_set_property (GObject * object, guint prop_id, const GValue * value,
    GParamSpec * pspec)
{
  GstFakeSrc *src;
  GstBaseSrc *basesrc;

  src = GST_FAKESRC (object);
  basesrc = GST_BASE_SRC (object);

  switch (prop_id) {
    case PROP_OUTPUT:
      g_warning ("not yet implemented");
      break;
    case PROP_DATA:
      src->data = g_value_get_enum (value);

      if (src->data == FAKESRC_DATA_SUBBUFFER) {
        if (!src->parent)
          gst_fakesrc_alloc_parent (src);
      } else {
        if (src->parent) {
          gst_buffer_unref (src->parent);
          src->parent = NULL;
        }
      }
      break;
    case PROP_SIZETYPE:
      src->sizetype = g_value_get_enum (value);
      break;
    case PROP_SIZEMIN:
      src->sizemin = g_value_get_int (value);
      break;
    case PROP_SIZEMAX:
      src->sizemax = g_value_get_int (value);
      break;
    case PROP_PARENTSIZE:
      src->parentsize = g_value_get_int (value);
      break;
    case PROP_FILLTYPE:
      src->filltype = g_value_get_enum (value);
      break;
    case PROP_DATARATE:
      src->datarate = g_value_get_int (value);
      break;
    case PROP_SYNC:
      src->sync = g_value_get_boolean (value);
      break;
    case PROP_PATTERN:
      break;
    case PROP_SILENT:
      src->silent = g_value_get_boolean (value);
      break;
    case PROP_SIGNAL_HANDOFFS:
      src->signal_handoffs = g_value_get_boolean (value);
      break;
    case PROP_DUMP:
      src->dump = g_value_get_boolean (value);
      break;
    case PROP_HAS_LOOP:
      g_return_if_fail (!GST_FLAG_IS_SET (object, GST_BASE_SRC_STARTED));
      src->has_loop = g_value_get_boolean (value);
      break;
    case PROP_HAS_GETRANGE:
      g_return_if_fail (!GST_FLAG_IS_SET (object, GST_BASE_SRC_STARTED));
      src->has_getrange = g_value_get_boolean (value);
      break;
    case PROP_IS_LIVE:
      gst_base_src_set_live (basesrc, g_value_get_boolean (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_fakesrc_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstFakeSrc *src;
  GstBaseSrc *basesrc;

  g_return_if_fail (GST_IS_FAKESRC (object));

  src = GST_FAKESRC (object);
  basesrc = GST_BASE_SRC (object);

  switch (prop_id) {
    case PROP_OUTPUT:
      g_value_set_enum (value, src->output);
      break;
    case PROP_DATA:
      g_value_set_enum (value, src->data);
      break;
    case PROP_SIZETYPE:
      g_value_set_enum (value, src->sizetype);
      break;
    case PROP_SIZEMIN:
      g_value_set_int (value, src->sizemin);
      break;
    case PROP_SIZEMAX:
      g_value_set_int (value, src->sizemax);
      break;
    case PROP_PARENTSIZE:
      g_value_set_int (value, src->parentsize);
      break;
    case PROP_FILLTYPE:
      g_value_set_enum (value, src->filltype);
      break;
    case PROP_DATARATE:
      g_value_set_int (value, src->datarate);
      break;
    case PROP_SYNC:
      g_value_set_boolean (value, src->sync);
      break;
    case PROP_PATTERN:
      g_value_set_string (value, src->pattern);
      break;
    case PROP_SILENT:
      g_value_set_boolean (value, src->silent);
      break;
    case PROP_SIGNAL_HANDOFFS:
      g_value_set_boolean (value, src->signal_handoffs);
      break;
    case PROP_DUMP:
      g_value_set_boolean (value, src->dump);
      break;
    case PROP_LAST_MESSAGE:
      g_value_set_string (value, src->last_message);
      break;
    case PROP_HAS_LOOP:
      g_value_set_boolean (value, src->has_loop);
      break;
    case PROP_HAS_GETRANGE:
      g_value_set_boolean (value, src->has_getrange);
      break;
    case PROP_IS_LIVE:
      g_value_set_boolean (value, gst_base_src_is_live (basesrc));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_fakesrc_prepare_buffer (GstFakeSrc * src, GstBuffer * buf)
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
        *ptr++ = (gint8) ((255.0) * rand () / (RAND_MAX));
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

static GstBuffer *
gst_fakesrc_alloc_buffer (GstFakeSrc * src, guint size)
{
  GstBuffer *buf;

  buf = gst_buffer_new ();
  GST_BUFFER_SIZE (buf) = size;

  if (size != 0) {
    switch (src->filltype) {
      case FAKESRC_FILLTYPE_NOTHING:
        GST_BUFFER_DATA (buf) = g_malloc (size);
        break;
      case FAKESRC_FILLTYPE_NULL:
        GST_BUFFER_DATA (buf) = g_malloc0 (size);
        break;
      case FAKESRC_FILLTYPE_RANDOM:
      case FAKESRC_FILLTYPE_PATTERN:
      case FAKESRC_FILLTYPE_PATTERN_CONT:
      default:
        GST_BUFFER_DATA (buf) = g_malloc (size);
        gst_fakesrc_prepare_buffer (src, buf);
        break;
    }
  }

  return buf;
}

static guint
gst_fakesrc_get_size (GstFakeSrc * src)
{
  guint size;

  switch (src->sizetype) {
    case FAKESRC_SIZETYPE_FIXED:
      size = src->sizemax;
      break;
    case FAKESRC_SIZETYPE_RANDOM:
      size =
          src->sizemin +
          (guint8) (((gfloat) src->sizemax) * rand () / (RAND_MAX +
              (gfloat) src->sizemin));
      break;
    case FAKESRC_SIZETYPE_NULL:
    default:
      size = 0;
      break;
  }

  return size;
}

static GstBuffer *
gst_fakesrc_create_buffer (GstFakeSrc * src)
{
  GstBuffer *buf;
  guint size;
  gboolean dump = src->dump;

  size = gst_fakesrc_get_size (src);
  if (size == 0)
    return gst_buffer_new ();

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
      } else {
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
      buf = gst_buffer_new ();
      break;
  }
  if (dump) {
    gst_util_dump_mem (GST_BUFFER_DATA (buf), GST_BUFFER_SIZE (buf));
  }

  return buf;
}

static GstFlowReturn
gst_fakesrc_create (GstBaseSrc * basesrc, guint64 offset, guint length,
    GstBuffer ** ret)
{
  GstFakeSrc *src;
  GstBuffer *buf;
  GstClockTime time;

  src = GST_FAKESRC (basesrc);

  if (src->buffer_count == src->segment_end) {
    GST_INFO ("buffer_count reaches segment_end %d %d", src->buffer_count,
        src->segment_end);
    return GST_FLOW_UNEXPECTED;
  }

  buf = gst_fakesrc_create_buffer (src);
  GST_BUFFER_OFFSET (buf) = src->buffer_count++;

  time = GST_CLOCK_TIME_NONE;

  if (src->datarate > 0) {
    time = (src->bytes_sent * GST_SECOND) / src->datarate;
    if (src->sync) {
      /* gst_element_wait (GST_ELEMENT (src), time); */
    }

    GST_BUFFER_DURATION (buf) =
        GST_BUFFER_SIZE (buf) * GST_SECOND / src->datarate;
  }
  GST_BUFFER_TIMESTAMP (buf) = time;

  if (!src->silent) {
    g_free (src->last_message);

    src->last_message =
        g_strdup_printf ("get      ******* > (%d bytes, timestamp: %"
        GST_TIME_FORMAT ", duration: %" GST_TIME_FORMAT ", offset: %"
        G_GINT64_FORMAT ", offset_end: %" G_GINT64_FORMAT ", flags: %d) %p",
        GST_BUFFER_SIZE (buf),
        GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)),
        GST_TIME_ARGS (GST_BUFFER_DURATION (buf)), GST_BUFFER_OFFSET (buf),
        GST_BUFFER_OFFSET_END (buf), GST_MINI_OBJECT (buf)->flags, buf);

    g_object_notify (G_OBJECT (src), "last_message");
  }

  if (src->signal_handoffs) {
    GST_LOG_OBJECT (src, "pre handoff emit");
    g_signal_emit (G_OBJECT (src), gst_fakesrc_signals[SIGNAL_HANDOFF], 0, buf);
    GST_LOG_OBJECT (src, "post handoff emit");
  }

  src->bytes_sent += GST_BUFFER_SIZE (buf);

  *ret = buf;
  return GST_FLOW_OK;
}

static gboolean
gst_fakesrc_start (GstBaseSrc * basesrc)
{
  GstFakeSrc *src;

  src = GST_FAKESRC (basesrc);

  src->buffer_count = 0;
  src->pattern_byte = 0x00;
  src->bytes_sent = 0;

  return TRUE;
}

static gboolean
gst_fakesrc_stop (GstBaseSrc * basesrc)
{
  GstFakeSrc *src;

  src = GST_FAKESRC (basesrc);

  if (src->parent) {
    gst_buffer_unref (src->parent);
    src->parent = NULL;
  }
  g_free (src->last_message);
  src->last_message = NULL;

  return TRUE;
}
