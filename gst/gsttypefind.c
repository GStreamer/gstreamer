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

#include "gsttypefind.h"

/* #define GST_DEBUG_ENABLED */

GstElementDetails gst_typefind_details = {
  "TypeFind",
  "TypeFind",
  "Finds the media type",
  VERSION,
  "Erik Walthinsen <omega@cse.ogi.edu>"
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
};


static void	gst_typefind_class_init		(GstTypeFindClass *klass);
static void	gst_typefind_init		(GstTypeFind *typefind);

static void	gst_typefind_set_property	(GObject *object, guint prop_id, 
						 const GValue *value, GParamSpec *pspec);
static void	gst_typefind_get_property	(GObject *object, guint prop_id, 
						 GValue *value, GParamSpec *pspec);

static void	gst_typefind_chain		(GstPad *pad, GstBuffer *buf);

static GstElementClass *parent_class = NULL;
static guint gst_typefind_signals[LAST_SIGNAL] = { 0 };

GType
gst_typefind_get_type (void)
{
  static GType typefind_type = 0;

  if (!typefind_type) {
    static const GTypeInfo typefind_info = {
      sizeof(GstTypeFindClass),
      NULL,
      NULL,
      (GClassInitFunc)gst_typefind_class_init,
      NULL,
      NULL,
      sizeof(GstTypeFind),
      0,
      (GInstanceInitFunc)gst_typefind_init,
      NULL
    };
    typefind_type = g_type_register_static (GST_TYPE_ELEMENT, "GstTypeFind", &typefind_info, 0);
  }
  return typefind_type;
}

static void
gst_typefind_class_init (GstTypeFindClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass*)klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_CAPS,
    g_param_spec_pointer("caps", "Caps", "Found capabilities", G_PARAM_READABLE));

  gst_typefind_signals[HAVE_TYPE] =
      g_signal_new ("have_type", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST,
                     G_STRUCT_OFFSET (GstTypeFindClass, have_type), NULL, NULL,
                     g_cclosure_marshal_VOID__POINTER, G_TYPE_NONE, 1,
                     G_TYPE_POINTER);

  gobject_class->set_property = GST_DEBUG_FUNCPTR (gst_typefind_set_property);
  gobject_class->get_property = GST_DEBUG_FUNCPTR (gst_typefind_get_property);
}

static void
gst_typefind_init (GstTypeFind *typefind)
{
  typefind->sinkpad = gst_pad_new ("sink", GST_PAD_SINK);
  gst_element_add_pad (GST_ELEMENT (typefind), typefind->sinkpad);
  gst_pad_set_chain_function (typefind->sinkpad, gst_typefind_chain);
}

static void
gst_typefind_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  GstTypeFind *typefind;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_TYPEFIND (object));

  typefind = GST_TYPEFIND (object);

  switch (prop_id) {
    default:
      break;
  }
}

static void
gst_typefind_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  GstTypeFind *typefind;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_TYPEFIND (object));

  typefind = GST_TYPEFIND (object);

  switch (prop_id) {
    case ARG_CAPS:
      g_value_set_pointer(value, typefind->caps);
      break;
    default:
      break;
  }
}

static void
gst_typefind_chain (GstPad *pad, GstBuffer *buf)
{
  GstTypeFind *typefind;
  GList *type_list;
  GstType *type;

  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (buf != NULL);

  typefind = GST_TYPEFIND (GST_OBJECT_PARENT (pad));
  GST_DEBUG (0,"got buffer of %d bytes in '%s'\n",
        GST_BUFFER_SIZE (buf), GST_OBJECT_NAME (typefind));

  type_list = gst_type_get_list ();

  while (type_list) {
    GSList *factories;
    type = (GstType *)type_list->data;

    factories = type->factories;

    while (factories) {
      GstTypeFactory *factory = GST_TYPEFACTORY (factories->data);
      GstTypeFindFunc typefindfunc = (GstTypeFindFunc)factory->typefindfunc;
      GstCaps *caps;

      GST_DEBUG (0,"try type :%d \"%s\"\n", type->id, type->mime);
      if (typefindfunc && (caps = typefindfunc (buf, factory))) {
        GST_DEBUG (0,"found type :%d \"%s\" \"%s\"\n", caps->id, type->mime, 
			gst_caps_get_name (caps));
	typefind->caps = caps;

	gst_pad_set_caps (pad, caps);

{ /* FIXME: this should all be in an _emit() wrapper eventually */
        int oldstate = GST_STATE(typefind);
	gst_object_ref (GST_OBJECT (typefind));
        g_signal_emit (G_OBJECT (typefind), gst_typefind_signals[HAVE_TYPE], 0,
	                      typefind->caps);
        if (GST_STATE(typefind) != oldstate) {
	  gst_object_unref (GST_OBJECT (typefind));
          GST_DEBUG(0, "state changed during signal, aborting\n");
	  gst_element_yield (typefind);
        }
	gst_object_unref (GST_OBJECT (typefind));
}

        goto end;
      }
      factories = g_slist_next (factories);
    }

    type_list = g_list_next (type_list);
  }
end:
  gst_buffer_unref (buf);
}
