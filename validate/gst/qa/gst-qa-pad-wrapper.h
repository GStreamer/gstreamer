/* GStreamer
 * Copyright (C) 2013 Thiago Santos <thiago.sousa.santos@collabora.com>
 *
 * gst-qa-pad_wrapper.h - QA PadWrapper class
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

#ifndef __GST_QA_PAD_WRAPPER_H__
#define __GST_QA_PAD_WRAPPER_H__

#include <glib-object.h>
#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_TYPE_QA_PAD_WRAPPER			(gst_qa_pad_wrapper_get_type ())
#define GST_IS_QA_PAD_WRAPPER(obj)		(G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_QA_PAD_WRAPPER))
#define GST_IS_QA_PAD_WRAPPER_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_QA_PAD_WRAPPER))
#define GST_QA_PAD_WRAPPER_GET_CLASS(obj)	(G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_QA_PAD_WRAPPER, GstQaPadWrapperClass))
#define GST_QA_PAD_WRAPPER(obj)			(G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_QA_PAD_WRAPPER, GstQaPadWrapper))
#define GST_QA_PAD_WRAPPER_CLASS(klass)		(G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_QA_PAD_WRAPPER, GstQaPadWrapperClass))
#define GST_QA_PAD_WRAPPER_CAST(obj)            ((GstQaPadWrapper*)(obj))
#define GST_QA_PAD_WRAPPER_CLASS_CAST(klass)    ((GstQaPadWrapperClass*)(klass))

typedef struct _GstQaPadWrapper GstQaPadWrapper;
typedef struct _GstQaPadWrapperClass GstQaPadWrapperClass;

/**
 * GstQaPadWrapper:
 *
 * GStreamer QA PadWrapper class.
 *
 * Class that wraps a #GstPad for QA checks
 */
struct _GstQaPadWrapper {
  GObject 	 object;

  gboolean       setup;
  GstPad        *pad;

  /*< private >*/
};

/**
 * GstQaPadWrapperClass:
 * @parent_class: parent
 *
 * GStreamer QA PadWrapper object class.
 */
struct _GstQaPadWrapperClass {
  GObjectClass	parent_class;
};

/* normal GObject stuff */
GType		gst_qa_pad_wrapper_get_type		(void);

GstQaPadWrapper *   gst_qa_pad_wrapper_new      (GstPad * pad);
gboolean            gst_qa_pad_wrapper_setup    (GstQaPadWrapper * pad_wrapper);

G_END_DECLS

#endif /* __GST_QA_PAD_WRAPPER_H__ */

