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

//#define GST_DEBUG_ENABLED

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


static void	gst_typefind_class_init	(GstTypeFindClass *klass);
static void	gst_typefind_init	(GstTypeFind *typefind);

static void	gst_typefind_set_arg	(GtkObject *object, GtkArg *arg, guint id);
static void	gst_typefind_get_arg	(GtkObject *object, GtkArg *arg, guint id);

static void	gst_typefind_chain	(GstPad *pad, GstBuffer *buf);

static GstElementClass *parent_class = NULL;
static guint gst_typefind_signals[LAST_SIGNAL] = { 0 };

GtkType
gst_typefind_get_type (void)
{
  static GtkType typefind_type = 0;

  if (!typefind_type) {
    static const GtkTypeInfo typefind_info = {
      "GstTypeFind",
      sizeof(GstTypeFind),
      sizeof(GstTypeFindClass),
      (GtkClassInitFunc)gst_typefind_class_init,
      (GtkObjectInitFunc)gst_typefind_init,
      (GtkArgSetFunc)gst_typefind_set_arg,
      (GtkArgGetFunc)gst_typefind_get_arg,
      (GtkClassInitFunc)NULL,
    };
    typefind_type = gtk_type_unique (GST_TYPE_ELEMENT, &typefind_info);
  }
  return typefind_type;
}

static void
gst_typefind_class_init (GstTypeFindClass *klass)
{
  GtkObjectClass *gtkobject_class;

  gtkobject_class = (GtkObjectClass*)klass;

  parent_class = gtk_type_class (GST_TYPE_ELEMENT);

  gtk_object_add_arg_type("GstTypeFind::caps", GTK_TYPE_POINTER,
                          GTK_ARG_READABLE, ARG_CAPS);

  gst_typefind_signals[HAVE_TYPE] =
      gtk_signal_new ("have_type", GTK_RUN_LAST, gtkobject_class->type,
                      GTK_SIGNAL_OFFSET (GstTypeFindClass, have_type),
                      gtk_marshal_NONE__INT, GTK_TYPE_NONE, 1, GTK_TYPE_POINTER);

  gtk_object_class_add_signals (gtkobject_class, gst_typefind_signals,
                                LAST_SIGNAL);

  gtkobject_class->set_arg = gst_typefind_set_arg;
  gtkobject_class->get_arg = gst_typefind_get_arg;
}

static void
gst_typefind_init (GstTypeFind *typefind)
{
  typefind->sinkpad = gst_pad_new ("sink", GST_PAD_SINK);
  gst_element_add_pad (GST_ELEMENT (typefind), typefind->sinkpad);
  gst_pad_set_chain_function (typefind->sinkpad, gst_typefind_chain);
}

static void
gst_typefind_set_arg (GtkObject *object, GtkArg *arg, guint id)
{
  GstTypeFind *typefind;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_TYPEFIND (object));

  typefind = GST_TYPEFIND (object);

  switch(id) {
    default:
      break;
  }
}

static void
gst_typefind_get_arg (GtkObject *object, GtkArg *arg, guint id)
{
  GstTypeFind *typefind;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_TYPEFIND (object));

  typefind = GST_TYPEFIND (object);

  switch(id) {
    case ARG_CAPS:
      GTK_VALUE_POINTER (*arg) =  typefind->caps;
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
    GSList *funcs;
    type = (GstType *)type_list->data;

    funcs = type->typefindfuncs;

    while (funcs) {
      GstTypeFindFunc typefindfunc = (GstTypeFindFunc)funcs->data;
      GstCaps *caps;

      GST_DEBUG (0,"try type :%d \"%s\"\n", type->id, type->mime);
      if (typefindfunc && (caps = typefindfunc (buf, type))) {
        GST_DEBUG (0,"found type :%d \"%s\" \"%s\"\n", caps->id, type->mime, 
			gst_caps_get_name (caps));
	typefind->caps = caps;
        gtk_signal_emit (GTK_OBJECT (typefind), gst_typefind_signals[HAVE_TYPE],
	                      typefind->caps);
	gst_pad_set_caps (pad, caps);
        goto end;
      }
      funcs = g_slist_next (funcs);
    }

    type_list = g_list_next (type_list);
  }
end:
  gst_buffer_unref (buf);
}
