/*
 *  gstcompat.h - Compatibility glue for GStreamer
 *
 *  Copyright (C) 2013 Intel Corporation
 *    Author: Gwenole Beauchesne <gwenole.beauchesne@intel.com>
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public License
 *  as published by the Free Software Foundation; either version 2.1
 *  of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free
 *  Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA 02110-1301 USA
 */

#ifndef GST_COMPAT_H
#define GST_COMPAT_H

#include <gst/gst.h>

#include <gst/video/gstvideometa.h>

/* GstStructure */
#undef  gst_structure_get_fourcc
#define gst_structure_get_fourcc(structure, fieldname, value) \
    gst_compat_structure_get_fourcc(structure, fieldname, value)

static inline gboolean
gst_compat_structure_get_fourcc(const GstStructure *structure,
    const gchar *fieldname, guint32 *value)
{
    const gchar *s = gst_structure_get_string(structure, fieldname);

    if (!s || strlen(s) != 4)
        return FALSE;

    *value = GST_MAKE_FOURCC(s[0], s[1], s[2], s[3]);
    return TRUE;
}

/* GstTypeFind */
#undef  GstTypeFindPeekFunction
#define GstTypeFindPeekFunction         GstCompatTypeFindPeekFunction
#undef  GstTypeFindSuggestFunction
#define GstTypeFindSuggestFunction      GstCompatTypeFindSuggestFunction

typedef const guint8 *(*GstCompatTypeFindPeekFunction)(gpointer, gint64, guint);
typedef void (*GstCompatTypeFindSuggestFunction)(gpointer, guint, GstCaps *);

/* GstPad */
#define GST_PAD_CHAIN_FUNCTION_ARGS \
    GstPad *pad, GstObject *parent, GstBuffer *buffer
#define GST_PAD_EVENT_FUNCTION_ARGS \
    GstPad *pad, GstObject *parent, GstEvent *event
#define GST_PAD_QUERY_FUNCTION_ARGS \
    GstPad *pad, GstObject *parent, GstQuery *query
#define GST_PAD_QUERY_FUNCTION_CALL(func, pad, parent, query) \
    (func)(pad, parent, query)

/* Misc helpers */
#define GST_MAKE_FORMAT_STRING(FORMAT) \
    "format=(string)" G_STRINGIFY(FORMAT)

#endif /* GST_COMPAT_H */
