/* Gnome-Streamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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


#include <gnome.h>

#include <gst/gst.h>

#include "gsteditor.h"
#include "gstelementselect.h"

GstEditorElement *gst_editor_create_item(GstEditorBin *bin,
                                         gdouble x,gdouble y) {
  GstElementFactory *factory;
  GstElement *element;
  GstEditorElement *editorelement;
  GtkType itemtype;

  factory = element_select_dialog();
  if (factory) {
//    g_print("got factory \"%s\"\n",factory->name);
    element = gst_elementfactory_create(factory,factory->name);
    if (element) {
      if (GST_IS_BIN(element)) {
//        g_print("factory is a bin\n");
        editorelement = GST_EDITOR_ELEMENT(gst_editor_bin_new(
          GST_EDITOR_BIN(bin),GST_BIN(element),
          "x",x,"y",y,"width",50.0,"height",20.0,NULL));
      } else {
//        g_print("factory is an element\n");
        editorelement = gst_editor_element_new(bin,element,
          "x",x,"y",y,"width",50.0,"height",20.0,NULL);
      }
//      g_print("created element \"%s\" at %.2fx%.2f\n",
//              gst_object_get_name(GST_OBJECT(element)),
//              x,y);
      return editorelement;
    }
  }
  return NULL;
}
