/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstshaper.c: 
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "gstshaper.h"

GST_DEBUG_CATEGORY_STATIC (gst_shaper_debug);
#define GST_CAT_DEFAULT gst_shaper_debug

GstElementDetails gst_shaper_details = GST_ELEMENT_DETAILS (
  "Shaper",
  "Generic",
  "Synchronizes streams on different pads",
  "Wim Taymans <wim.taymans@chello.be>"
);


/* Shaper signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_POLICY,
  ARG_SILENT,
  ARG_LAST_MESSAGE,
};

typedef struct
{
  GstPad 	*sinkpad;
  GstPad 	*srcpad;
  GstBuffer	*buffer;
} GstShaperConnection;

GstStaticPadTemplate shaper_src_template = GST_STATIC_PAD_TEMPLATE (
  "src%d",
  GST_PAD_SRC,
  GST_PAD_SOMETIMES,
  GST_STATIC_CAPS_ANY
);

GstStaticPadTemplate shaper_sink_template = GST_STATIC_PAD_TEMPLATE (
  "sink%d",
  GST_PAD_SINK,
  GST_PAD_REQUEST,
  GST_STATIC_CAPS_ANY
);

#define GST_TYPE_SHAPER_POLICY (gst_shaper_policy_get_type())
static GType
gst_shaper_policy_get_type (void)
{
  static GType shaper_policy_type = 0;
  static GEnumValue shaper_policy[] = {
    { SHAPER_POLICY_TIMESTAMPS,         "1", "sync on timestamps"},
    { SHAPER_POLICY_BUFFERSIZE,         "2", "sync on buffer size"},
    {0, NULL, NULL},
  };
  if (!shaper_policy_type) {
    shaper_policy_type = g_enum_register_static ("GstShaperPolicy", shaper_policy);
  }
  return shaper_policy_type;
}

static void	gst_shaper_base_init		(gpointer g_class);
static void 	gst_shaper_class_init		(GstShaperClass *klass);
static void 	gst_shaper_init			(GstShaper *shaper);

static void 	gst_shaper_set_property		(GObject *object, guint prop_id, 
						 const GValue *value, GParamSpec *pspec);
static void 	gst_shaper_get_property		(GObject *object, guint prop_id, 
						 GValue *value, GParamSpec *pspec);

static GstPad* 	gst_shaper_request_new_pad 	(GstElement *element, GstPadTemplate *templ, 
						 const gchar *unused);

static void 	gst_shaper_loop			(GstElement *element);

static GstElementClass *parent_class = NULL;
/* static guint gst_shaper_signals[LAST_SIGNAL] = { 0 }; */

GType
gst_shaper_get_type (void) 
{
  static GType shaper_type = 0;

  if (!shaper_type) {
    static const GTypeInfo shaper_info = {
      sizeof(GstShaperClass),
      gst_shaper_base_init,
      NULL,
      (GClassInitFunc)gst_shaper_class_init,
      NULL,
      NULL,
      sizeof(GstShaper),
      0,
      (GInstanceInitFunc)gst_shaper_init,
    };
    shaper_type = g_type_register_static (GST_TYPE_ELEMENT, "GstShaper", &shaper_info, 0);
  
    GST_DEBUG_CATEGORY_INIT (gst_shaper_debug, "shaper", 0, "shaper element");
  }
  return shaper_type;
}

static void
gst_shaper_base_init (gpointer g_class)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (g_class);
  
  gst_element_class_set_details (gstelement_class, &gst_shaper_details);
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&shaper_src_template));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&shaper_sink_template));
}

static void 
gst_shaper_class_init (GstShaperClass *klass) 
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*) klass;
  gstelement_class = (GstElementClass*) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_POLICY,
    g_param_spec_enum ("policy", "Policy", "Shaper policy",
                       GST_TYPE_SHAPER_POLICY, SHAPER_POLICY_TIMESTAMPS, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_SILENT,
    g_param_spec_boolean ("silent", "silent", "silent",
                          FALSE, G_PARAM_READWRITE)); 
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_LAST_MESSAGE,
    g_param_spec_string ("last-message", "last-message", "last-message",
                         NULL, G_PARAM_READABLE)); 

  gobject_class->set_property = GST_DEBUG_FUNCPTR (gst_shaper_set_property);  
  gobject_class->get_property = GST_DEBUG_FUNCPTR (gst_shaper_get_property);

  gstelement_class->request_new_pad = GST_DEBUG_FUNCPTR (gst_shaper_request_new_pad);
}

static GstCaps*
gst_shaper_getcaps (GstPad *pad)
{
  GstPad *otherpad;
  GstShaperConnection *connection;

  connection = gst_pad_get_element_private (pad);

  otherpad = (pad == connection->srcpad ? connection->sinkpad : connection->srcpad);

  if (GST_PAD_PEER (otherpad)) {
    return gst_pad_get_caps (GST_PAD_PEER (otherpad));
  } else {
    return gst_caps_new_any ();
  }
}

static GList*
gst_shaper_get_internal_link (GstPad *pad)
{
  GList *res = NULL;
  GstShaperConnection *connection;
  GstPad *otherpad;

  connection = gst_pad_get_element_private (pad);

  otherpad = (pad == connection->srcpad ? connection->sinkpad : connection->srcpad);

  res = g_list_prepend (res, otherpad);

  return res;
}

static GstPadLinkReturn
gst_shaper_link (GstPad *pad, const GstCaps *caps)
{
  GstPad *otherpad;
  GstShaperConnection *connection;

  connection = gst_pad_get_element_private (pad);

  otherpad = (pad == connection->srcpad ? connection->sinkpad : connection->srcpad);

  return gst_pad_try_set_caps (otherpad, caps);
}

static GstShaperConnection*
gst_shaper_create_connection (GstShaper *shaper)
{
  GstShaperConnection *connection;
  gchar *padname;

  shaper->nconnections++;

  connection = g_new0 (GstShaperConnection, 1);

  padname = g_strdup_printf ("sink%d", shaper->nconnections);
  connection->sinkpad = gst_pad_new_from_template (
      gst_static_pad_template_get (&shaper_sink_template), padname);
  g_free (padname);
  gst_pad_set_getcaps_function (connection->sinkpad, gst_shaper_getcaps);
  gst_pad_set_internal_link_function (connection->sinkpad, gst_shaper_get_internal_link);
  gst_pad_set_link_function (connection->sinkpad, gst_shaper_link);
  gst_pad_set_element_private (connection->sinkpad, connection);
  gst_element_add_pad (GST_ELEMENT (shaper), connection->sinkpad);

  padname = g_strdup_printf ("src%d", shaper->nconnections);
  connection->srcpad = gst_pad_new_from_template (
      gst_static_pad_template_get (&shaper_src_template), padname);
  g_free (padname);
  gst_pad_set_getcaps_function (connection->srcpad, gst_shaper_getcaps);
  gst_pad_set_internal_link_function (connection->srcpad, gst_shaper_get_internal_link);
  gst_pad_set_link_function (connection->srcpad, gst_shaper_link);
  gst_pad_set_element_private (connection->srcpad, connection);
  gst_element_add_pad (GST_ELEMENT (shaper), connection->srcpad);

  shaper->connections = g_slist_prepend (shaper->connections, connection);

  return connection;
}

static GstPad*
gst_shaper_request_new_pad (GstElement *element, GstPadTemplate *templ, const gchar *unused)
{
  GstShaper *shaper = GST_SHAPER (element);
  GstShaperConnection *connection;

  connection = gst_shaper_create_connection (shaper);

  return connection->sinkpad;
}

static void 
gst_shaper_init (GstShaper *shaper) 
{
  gst_element_set_loop_function (GST_ELEMENT (shaper), gst_shaper_loop);

  shaper->policy = SHAPER_POLICY_TIMESTAMPS;
  shaper->connections = NULL;
  shaper->nconnections = 0;
  shaper->silent = FALSE;
  shaper->last_message = NULL;
}

static void 
gst_shaper_loop (GstElement *element) 
{
  GstShaper *shaper;
  GSList *connections;
  gboolean eos = TRUE;
  GstShaperConnection *min = NULL;

  shaper = GST_SHAPER (element);

  /* first make sure we have a buffer on all pads */
  connections = shaper->connections;
  while (connections) {
    GstShaperConnection *connection = (GstShaperConnection *) connections->data;

    /* try to fill a connection without a buffer on a pad that is
     * active */
    if (connection->buffer == NULL && GST_PAD_IS_USABLE (connection->sinkpad)) {
      GstBuffer *buffer;

      buffer = GST_BUFFER (gst_pad_pull (connection->sinkpad));

      /* events are simply pushed ASAP */
      if (GST_IS_EVENT (buffer)) {
	/* save event type as it will be unreffed after the next push */
	GstEventType type = GST_EVENT_TYPE (buffer);

	gst_pad_push (connection->srcpad, GST_DATA (buffer));

	switch (type) {
          /* on EOS we disable the pad so that we don't pull on
	   * it again and never get more data */
          case GST_EVENT_EOS:
	    gst_pad_set_active (connection->sinkpad, FALSE);
	    break;
	  default:
	    break;
	}
      }
      else {
	/* we store the buffer */
	connection->buffer = buffer;
      }
    }
    /* FIXME policy stuff goes here */
    /* find connection with lowest timestamp */
    if (min == NULL || (connection->buffer != NULL &&
	(GST_BUFFER_TIMESTAMP (connection->buffer) < 
	 GST_BUFFER_TIMESTAMP (min->buffer)))) 
    {
      min = connection;
    }
    connections = g_slist_next (connections);
  }
  /* if we have a connection with a buffer, push it */
  if (min != NULL && min->buffer) {
    gst_pad_push (min->srcpad, GST_DATA (min->buffer));
    min->buffer = NULL;
    /* since we pushed a buffer, it's not EOS */
    eos = FALSE;
  }
  
  if (eos) {
    gst_element_set_eos (element);
  }
}

static void 
gst_shaper_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec) 
{
  GstShaper *shaper;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_SHAPER (object));
  
  shaper = GST_SHAPER (object);

  switch (prop_id) {
    case ARG_POLICY:
      shaper->policy = g_value_get_enum (value);
      break;
    case ARG_SILENT:
      shaper->silent = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void gst_shaper_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec) {
  GstShaper *shaper;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_SHAPER (object));
  
  shaper = GST_SHAPER (object);

  switch (prop_id) {
    case ARG_POLICY:
      g_value_set_enum (value, shaper->policy);
      break;
    case ARG_SILENT:
      g_value_set_boolean (value, shaper->silent);
      break;
    case ARG_LAST_MESSAGE:
      g_value_set_string (value, shaper->last_message);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

