/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gsttypefind.c: 
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


#include "gst_private.h"
#include "gsttype.h"
#include "gstlog.h"
#include "gsttypefind.h"

#define DEFAULT_MAX_BUFFERS	1

GstElementDetails gst_type_find_details = {
  "TypeFind",
  "Generic",
  "LGPL",
  "Finds the media type of a stream",
  VERSION,
  "Erik Walthinsen <omega@cse.ogi.edu>,"
  "Wim Taymans <wim.taymans@chello.be>",
  "(C) 1999",
};


/* TypeFind signals and args */
enum {
  HAVE_TYPE,
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_CAPS,
  ARG_MAX_BUFFERS,
};


static void	gst_type_find_class_init	(GstTypeFindClass *klass);
static void	gst_type_find_init		(GstTypeFind *typefind);

static void	gst_type_find_set_property	(GObject *object, guint prop_id,
						 const GValue *value, 
						 GParamSpec *pspec);
static void	gst_type_find_get_property	(GObject *object, guint prop_id,
						 GValue *value, 
						 GParamSpec *pspec);

static void	gst_type_find_chain		(GstPad *pad, GstBuffer *buf);
static GstElementStateReturn
		gst_type_find_change_state 	(GstElement *element);

static GstElementClass *parent_class = NULL;
static guint gst_type_find_signals[LAST_SIGNAL] = { 0 };

GType
gst_type_find_get_type (void)
{
  static GType typefind_type = 0;

  if (!typefind_type) {
    static const GTypeInfo typefind_info = {
      sizeof(GstTypeFindClass),
      NULL,
      NULL,
      (GClassInitFunc)gst_type_find_class_init,
      NULL,
      NULL,
      sizeof(GstTypeFind),
      0,
      (GInstanceInitFunc)gst_type_find_init,
      NULL
    };
    typefind_type = g_type_register_static (GST_TYPE_ELEMENT, "GstTypeFind", &typefind_info, 0);
  }
  return typefind_type;
}

static void
gst_type_find_class_init (GstTypeFindClass *klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_CAPS,
    g_param_spec_pointer ("caps", "Caps", "Found capabilities", G_PARAM_READABLE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_MAX_BUFFERS,
    g_param_spec_int ("max_buffers", 
	    	      "Max Buffers", 
		      "Maximal amount of buffers before giving en error (0 == unlimited)", 
		      0, G_MAXINT, DEFAULT_MAX_BUFFERS, G_PARAM_READWRITE));

  gst_type_find_signals[HAVE_TYPE] =
      g_signal_new ("have_type", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
                     G_STRUCT_OFFSET (GstTypeFindClass, have_type), NULL, NULL,
                     g_cclosure_marshal_VOID__POINTER, G_TYPE_NONE, 1,
                     G_TYPE_OBJECT);

  gobject_class->set_property = GST_DEBUG_FUNCPTR (gst_type_find_set_property);
  gobject_class->get_property = GST_DEBUG_FUNCPTR (gst_type_find_get_property);

  gstelement_class->change_state = GST_DEBUG_FUNCPTR (gst_type_find_change_state);
}

static void
gst_type_find_init (GstTypeFind *typefind)
{
  typefind->sinkpad = gst_pad_new ("sink", GST_PAD_SINK);
  gst_element_add_pad (GST_ELEMENT (typefind), typefind->sinkpad);
  gst_pad_set_chain_function (typefind->sinkpad, gst_type_find_chain);

  typefind->num_buffer = 0;
  typefind->max_buffers = DEFAULT_MAX_BUFFERS;
  typefind->caps = NULL;
}

static void
gst_type_find_set_property (GObject *object, guint prop_id, 
		            const GValue *value, GParamSpec *pspec)
{
  GstTypeFind *typefind;

  g_return_if_fail (GST_IS_TYPE_FIND (object));

  typefind = GST_TYPE_FIND (object);

  switch (prop_id) {
    case ARG_MAX_BUFFERS:
      typefind->max_buffers = g_value_get_int (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_type_find_get_property (GObject *object, guint prop_id, 
		            GValue *value, GParamSpec *pspec)
{
  GstTypeFind *typefind;

  g_return_if_fail (GST_IS_TYPE_FIND (object));

  typefind = GST_TYPE_FIND (object);

  switch (prop_id) {
    case ARG_CAPS:
      g_value_set_pointer (value, typefind->caps);
      break;
    case ARG_MAX_BUFFERS:
      g_value_set_int (value, typefind->max_buffers);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_type_find_chain (GstPad *pad, GstBuffer *buf)
{
  GstTypeFind *typefind;
  const GList *type_list;
  GstType *type;

  g_return_if_fail (GST_IS_PAD (pad));

  typefind = GST_TYPE_FIND (GST_OBJECT_PARENT (pad));

  GST_DEBUG (0,"got buffer of %d bytes in '%s'",
        GST_BUFFER_SIZE (buf), GST_OBJECT_NAME (typefind));

  type_list = gst_type_get_list ();

  while (type_list) {
    GSList *factories;
    type = (GstType *) type_list->data;

    factories = type->factories;

    while (factories) {
      GstTypeFactory *factory = GST_TYPE_FACTORY (factories->data);
      GstTypeFindFunc typefindfunc = (GstTypeFindFunc)factory->typefindfunc;
      GstCaps *caps;

      GST_DEBUG (GST_CAT_TYPES, "try type (%p) :%d \"%s\" %p", 
		 factory, type->id, type->mime, typefindfunc);
      if (typefindfunc && (caps = typefindfunc (buf, factory))) {
        GST_DEBUG (GST_CAT_TYPES, "found type: %d \"%s\" \"%s\"", 
		   caps->id, type->mime, gst_caps_get_name (caps));
	typefind->caps = caps;

	if (gst_pad_try_set_caps (pad, caps) <= 0) {
          g_warning ("typefind: found type but peer didn't accept it");
	}

	{
	  gst_object_ref (GST_OBJECT (typefind));
          g_signal_emit (G_OBJECT (typefind), gst_type_find_signals[HAVE_TYPE], 0,
	                      typefind->caps);
	  gst_object_unref (GST_OBJECT (typefind));
          goto end;
	}
      }
      factories = g_slist_next (factories);
    }
    type_list = g_list_next (type_list);
  }

  typefind->num_buffer++;

end:
  gst_buffer_unref (buf);

  if (typefind->max_buffers && typefind->num_buffer >= typefind->max_buffers) {
    gst_element_error (GST_ELEMENT (typefind), 
		    "typefind could not determine type after %d buffers", typefind->num_buffer);
  }
}

static GstElementStateReturn
gst_type_find_change_state (GstElement *element)
{
  GstTypeFind *typefind;
  GstElementStateReturn ret;

  typefind = GST_TYPE_FIND (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_NULL_TO_READY:
      break;
    case GST_STATE_READY_TO_PAUSED:
      typefind->num_buffer = 0;
      gst_caps_unref (typefind->caps);
      typefind->caps = NULL;
      break;
    case GST_STATE_PAUSED_TO_PLAYING:
    case GST_STATE_PLAYING_TO_PAUSED:
    case GST_STATE_PAUSED_TO_READY:
    case GST_STATE_READY_TO_NULL:
      break;
  }
  
  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element);
  
  return ret;
}
