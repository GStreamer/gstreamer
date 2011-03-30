/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2004 Wim Taymans <wim@fluendo.com>
 *
 * gstcompat.h: backwards compatibility stuff
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
/**
 * SECTION:gstcompat
 * @short_description: Deprecated API entries
 *
 * Please do not use these in new code.
 * These symbols are only available by defining GST_DISABLE_DEPRECATED.
 * This can be done in CFLAGS for compiling old code.
 */

/* API compatibility stuff */
#ifndef __GSTCOMPAT_H__
#define __GSTCOMPAT_H__

G_BEGIN_DECLS

/* added to ease the transition to 0.11 */
#define gst_element_class_set_details_simple  gst_element_class_set_metadata

#define gst_element_factory_get_longname(f)    gst_element_factory_get_metadata(f, GST_ELEMENT_METADATA_LONGNAME)
#define gst_element_factory_get_klass(f)       gst_element_factory_get_metadata(f, GST_ELEMENT_METADATA_KLASS)
#define gst_element_factory_get_description(f) gst_element_factory_get_metadata(f, GST_ELEMENT_METADATA_DESCRIPTION)
#define gst_element_factory_get_author(f)      gst_element_factory_get_metadata(f, GST_ELEMENT_METADATA_AUTHOR)
#define gst_element_factory_get_documentation_uri(f)  gst_element_factory_get_metadata(f, GST_ELEMENT_METADATA_DOC_URI)
#define gst_element_factory_get_icon_name(f)   gst_element_factory_get_metadata(f, GST_ELEMENT_METADATA_ICON_NAME)

#define gst_pad_get_caps_reffed(p)             gst_pad_get_caps(p)
#define gst_pad_peer_get_caps_reffed(p)        gst_pad_peer_get_caps(p)

//#define gst_buffer_create_sub(b,o,s)           gst_buffer_copy_region(b,GST_BUFFER_COPY_ALL,o,s)

#ifndef GST_DISABLE_DEPRECATED

#endif /* not GST_DISABLE_DEPRECATED */

G_END_DECLS

#endif /* __GSTCOMPAT_H__ */
