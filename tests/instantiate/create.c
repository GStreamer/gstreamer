/* GStreamer
 * Copyright (C) 1999 Erik Walthinsen <omega@cse.ogi.edu>
 *
 * create.c: test which instantiates all elements and unrefs them
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>

static void
create_all_elements (void)
{
  const GList *elements;
  GstElementFactory *factory;
  GstElement *element;

  /* get list of elements */
  for (elements = gst_registry_pool_feature_list (GST_TYPE_ELEMENT_FACTORY);
       elements != NULL; elements = elements->next) {
    factory = (GstElementFactory *) elements->data;
    if ((element = gst_element_factory_create (factory, "test"))) {
      gst_object_unref (GST_OBJECT (element));
    }
  }
}

gint
main (gint   argc,
      gchar *argv[])
{
  gst_init (&argc, &argv);
  create_all_elements ();
  return 0;
}
