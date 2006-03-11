/*
 * (c) 2006 Christophe Fergeau  <teuf@gnome.org>
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

#include <gst/gst.h>

typedef struct _GstXingMuxPriv GstXingMuxPriv;

/* Definition of structure storing data for this element. */
typedef struct _GstXingMux {
  GstElement element;

  GstPad *sinkpad, *srcpad;
  
  GstXingMuxPriv *priv;

} GstXingMux;

/* Standard definition defining a class for this element. */
typedef struct _GstXingMuxClass {
  GstElementClass parent_class;
} GstXingMuxClass;

/* Standard macros for defining types for this element.  */
#define GST_TYPE_XING_MUX \
  (gst_xing_mux_get_type())
#define GST_XING_MUX(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_XING_MUX,GstXingMux))
#define GST_XING_MUX_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_XING_MUX,GstXingMuxClass))
#define GST_IS_XING_MUX(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_XING_MUX))
#define GST_IS_XING_MUX_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_XING_MUX))

/* Standard function returning type information. */
GType gst_my_filter_get_type (void);
