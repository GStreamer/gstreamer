/* GStreamer
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


#ifndef __GST_EXAMPLE_H__
#define __GST_EXAMPLE_H__

#include <gst/gst.h>

G_BEGIN_DECLS
/* This is the definition of the element's object structure. */
typedef struct _GstExample GstExample;

/* The structure itself is derived from GstElement, as can be seen by the
 * fact that there's a complete instance of the GstElement structure at
 * the beginning of the object.  This allows the element to be cast to
 * an Element or even an Object.
 */
struct _GstExample
{
  GstElement element;

  /* We need to keep track of our pads, so we do so here. */
  GstPad *sinkpad, *srcpad;

  /* We'll use this to decide whether to do anything to the data we get. */
  gboolean active;
};

/* The other half of the object is its class.  The class also derives from
 * the same parent, though it must be the class structure this time.
 * Function pointers for polymophic methods and signals are placed in this
 * structure. */
typedef struct _GstExampleClass GstExampleClass;

struct _GstExampleClass
{
  GstElementClass parent_class;

  /* signals */
  void (*asdf) (GstElement * element, GstExample * example);
};

/* Five standard preprocessing macros are used in the Gtk+ object system.
 * The first uses the object's _get_type function to return the GType
 * of the object.
 */
#define GST_TYPE_EXAMPLE \
  (gst_example_get_type())
/* The second is a checking cast to the correct type.  If the object passed
 * is not the right type, a warning will be generated on stderr.
 */
#define GST_EXAMPLE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_EXAMPLE,GstExample))
/* The third is a checking cast of the class instead of the object. */
#define GST_EXAMPLE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_EXAMPLE,GstExampleClass))
/* The last two simply check to see if the passed pointer is an object or
 * class of the correct type. */
#define GST_IS_EXAMPLE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_EXAMPLE))
#define GST_IS_EXAMPLE_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_EXAMPLE))

/* This is the only prototype needed, because it is used in the above
 * GST_TYPE_EXAMPLE macro.
 */
GType gst_example_get_type (void);

G_END_DECLS
#endif /* __GST_EXAMPLE_H__ */
