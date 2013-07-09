/* GStreamer
 * Copyright (C) 2013 Thiago Santos <thiago.sousa.santos@collabora.com>
 *
 * gst-qa-element_wrapper.h - QA ElementWrapper class
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
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

#ifndef __GST_QA_ELEMENT_WRAPPER_H__
#define __GST_QA_ELEMENT_WRAPPER_H__

#include <glib-object.h>
#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_TYPE_QA_ELEMENT_WRAPPER			(gst_qa_element_wrapper_get_type ())
#define GST_IS_QA_ELEMENT_WRAPPER(obj)		        (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_QA_ELEMENT_WRAPPER))
#define GST_IS_QA_ELEMENT_WRAPPER_CLASS(klass)	        (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_QA_ELEMENT_WRAPPER))
#define GST_QA_ELEMENT_WRAPPER_GET_CLASS(obj)	        (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_QA_ELEMENT_WRAPPER, GstQaElementWrapperClass))
#define GST_QA_ELEMENT_WRAPPER(obj)			(G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_QA_ELEMENT_WRAPPER, GstQaElementWrapper))
#define GST_QA_ELEMENT_WRAPPER_CLASS(klass)		(G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_QA_ELEMENT_WRAPPER, GstQaElementWrapperClass))
#define GST_QA_ELEMENT_WRAPPER_CAST(obj)                ((GstQaElementWrapper*)(obj))
#define GST_QA_ELEMENT_WRAPPER_CLASS_CAST(klass)        ((GstQaElementWrapperClass*)(klass))

typedef struct _GstQaElementWrapper GstQaElementWrapper;
typedef struct _GstQaElementWrapperClass GstQaElementWrapperClass;

/**
 * GstQaElementWrapper:
 *
 * GStreamer QA ElementWrapper class.
 *
 * Class that wraps a #GstElement for QA checks
 */
struct _GstQaElementWrapper {
  GObject 	 object;

  gboolean       setup;
  GstElement    *element;

  /*< private >*/
};

/**
 * GstQaElementWrapperClass:
 * @parent_class: parent
 *
 * GStreamer QA ElementWrapper object class.
 */
struct _GstQaElementWrapperClass {
  GObjectClass	parent_class;
};

/* normal GObject stuff */
GType		gst_qa_element_wrapper_get_type		(void);

GstQaElementWrapper *   gst_qa_element_wrapper_new      (GstElement * element);
gboolean                gst_qa_element_wrapper_setup    (GstQaElementWrapper * element_wrapper);

G_END_DECLS

#endif /* __GST_QA_ELEMENT_WRAPPER_H__ */

