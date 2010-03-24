/*
 *  gstvaapitypes.h - Basic types
 *
 *  gstreamer-vaapi (C) 2010 Splitted-Desktop Systems
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 */

#ifndef GST_VAAPI_TYPES_H
#define GST_VAAPI_TYPES_H

#include <glib-object.h>

G_BEGIN_DECLS

/**
 * GstVaapiID:
 *
 * An integer large enough to hold a generic VA id or a pointer
 * wherever necessary.
 */
#if defined(GLIB_SIZEOF_VOID_P)
# define GST_VAAPI_TYPE_ID_SIZE GLIB_SIZEOF_VOID_P
#elif G_MAXULONG == 0xffffffff
# define GST_VAAPI_TYPE_ID_SIZE 4
#elif G_MAXULONG == 0xffffffffffffffffull
# define GST_VAAPI_TYPE_ID_SIZE 8
#else
# error "could not determine size of GstVaapiID"
#endif
#if GST_VAAPI_TYPE_ID_SIZE == 4
typedef guint32 GstVaapiID;
#elif GST_VAAPI_TYPE_ID_SIZE == 8
typedef guint64 GstVaapiID;
#else
# error "unsupported value for GST_VAAPI_TYPE_ID_SIZE"
#endif

/**
 * GST_VAAPI_ID_FORMAT:
 *
 * Can be used together with #GST_VAAPI_ID_ARGS to properly output an
 * integer value in a printf()-style text message.
 * <informalexample>
 * <programlisting>
 * printf("id: %" GST_VAAPI_ID_FORMAT "\n", GST_VAAPI_ID_ARGS(id));
 * </programlisting>
 * </informalexample>
 */
#define GST_VAAPI_ID_FORMAT "p"

/**
 * GST_VAAPI_ID_ARGS:
 * @id: a #GstVaapiID
 *
 * Can be used together with #GST_VAAPI_ID_FORMAT to properly output
 * an integer value in a printf()-style text message.
 */
#define GST_VAAPI_ID_ARGS(id) GUINT_TO_POINTER(id)

/**
 * GST_VAAPI_TYPE_ID:
 *
 * A #GValue type that represents a VA identifier.
 *
 * Return value: the #GType of GstVaapiID
 */
#define GST_VAAPI_TYPE_ID gst_vaapi_id_get_type()

/**
 * GST_VAAPI_VALUE_HOLDS_ID:
 * @x: the #GValue to check
 *
 * Checks if the given #GValue contains a #GstVaapiID value.
 */
#define GST_VAAPI_VALUE_HOLDS_ID(x) (G_VALUE_HOLDS((x), GST_VAAPI_TYPE_ID))

GType
gst_vaapi_id_get_type(void);

GstVaapiID
gst_vaapi_value_get_id(const GValue *value);

void
gst_vaapi_value_set_id(GValue *value, GstVaapiID id);

/**
 * GstVaapiPoint:
 * @x: X coordinate
 * @y: Y coordinate
 *
 * A location within a surface.
 */
typedef struct _GstVaapiPoint GstVaapiPoint;
struct _GstVaapiPoint {
    guint32 x;
    guint32 y;
};

/**
 * GstVaapiRectangle:
 * @x: X coordinate
 * @y: Y coordinate
 * @width: region width
 * @height: region height
 *
 * A rectangle region within a surface.
 */
typedef struct _GstVaapiRectangle GstVaapiRectangle;
struct _GstVaapiRectangle {
    guint32 x;
    guint32 y;
    guint32 width;
    guint32 height;
};

G_END_DECLS

#endif /* GST_VAAPI_TYPES_H */
