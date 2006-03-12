/* GStreamer taglib-based ID3 muxer
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

#ifndef GST_TAG_LIB_H
#define GST_TAG_LIB_H

#include <gst/gst.h>

G_BEGIN_DECLS

typedef struct _GstTagLibMuxPriv GstTagLibMuxPriv;

/* Definition of structure storing data for this element. */
typedef struct _GstTagLibMux {
  GstElement element;

  GstPad *sinkpad, *srcpad;
  GstTagList *tags;
  gsize tag_size;
  gboolean render_tag;

} GstTagLibMux;

/* Standard definition defining a class for this element. */
typedef struct _GstTagLibMuxClass {
  GstElementClass parent_class;
} GstTagLibMuxClass;

/* Standard macros for defining types for this element.  */
#define GST_TYPE_TAGLIB_MUX \
  (gst_tag_lib_mux_get_type())
#define GST_TAGLIB_MUX(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_TAGLIB_MUX,GstTagLibMux))
#define GST_TAGLIB_MUX_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_TAGLIB_MUX,GstTagLibMuxClass))
#define GST_IS_TAGLIB_MUX(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_TAGLIB_MUX))
#define GST_IS_TAGLIB_MUX_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_TAGLIB_MUX))

/* Standard function returning type information. */
GType gst_my_filter_get_type (void);

G_END_DECLS

#endif
