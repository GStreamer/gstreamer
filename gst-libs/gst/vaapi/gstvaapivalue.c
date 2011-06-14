/*
 *  gstvaapivalue.c - GValue implementations specific to VA-API
 *
 *  gstreamer-vaapi (C) 2010-2011 Splitted-Desktop Systems
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

/**
 * SECTION:gstvaapivalue
 * @short_description: GValue implementations specific to VA-API
 */

#include "config.h"
#include <gobject/gvaluecollector.h>
#include "gstvaapivalue.h"

static GTypeInfo gst_vaapi_type_info = {
    0,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    0,
    0,
    NULL,
    NULL,
};

static GTypeFundamentalInfo gst_vaapi_type_finfo = {
    0
};

#define GST_VAAPI_TYPE_DEFINE(type, name)                               \
GType gst_vaapi_ ## type ## _get_type(void)                             \
{                                                                       \
    static GType gst_vaapi_ ## type ## _type = 0;                       \
                                                                        \
    if (G_UNLIKELY(gst_vaapi_ ## type ## _type == 0)) {                 \
        gst_vaapi_type_info.value_table =                               \
            &gst_vaapi_ ## type ## _value_table;                        \
        gst_vaapi_ ## type ## _type = g_type_register_fundamental(      \
            g_type_fundamental_next(),                                  \
            name,                                                       \
            &gst_vaapi_type_info,                                       \
            &gst_vaapi_type_finfo,                                      \
            0                                                           \
        );                                                              \
    }                                                                   \
    return gst_vaapi_ ## type ## _type;                                 \
}

/* --- GstVaapiID --- */

#if GST_VAAPI_TYPE_ID_SIZE == 4
# define GST_VAAPI_VALUE_ID_(cvalue)    ((cvalue).v_int)
# define GST_VAAPI_VALUE_ID_CFORMAT     "i"
#elif GST_VAAPI_TYPE_ID_SIZE == 8
# define GST_VAAPI_VALUE_ID_(cvalue)    ((cvalue).v_int64)
# define GST_VAAPI_VALUE_ID_CFORMAT     "q"
#else
# error "unsupported GstVaapiID size"
#endif
#define GST_VAAPI_VALUE_ID(value)       GST_VAAPI_VALUE_ID_((value)->data[0])

static void
gst_vaapi_value_id_init(GValue *value)
{
    GST_VAAPI_VALUE_ID(value) = 0;
}

static void
gst_vaapi_value_id_copy(const GValue *src_value, GValue *dst_value)
{
    GST_VAAPI_VALUE_ID(dst_value) = GST_VAAPI_VALUE_ID(src_value);
}

static gchar *
gst_vaapi_value_id_collect(
    GValue      *value,
    guint        n_collect_values,
    GTypeCValue *collect_values,
    guint        collect_flags
)
{
    GST_VAAPI_VALUE_ID(value) = GST_VAAPI_VALUE_ID_(collect_values[0]);

    return NULL;
}

static gchar *
gst_vaapi_value_id_lcopy(
    const GValue *value,
    guint         n_collect_values,
    GTypeCValue  *collect_values,
    guint         collect_flags
)
{
    GstVaapiID *id_p = collect_values[0].v_pointer;

    if (!id_p)
        return g_strdup_printf("value location for `%s' passed as NULL",
                               G_VALUE_TYPE_NAME(value));

    *id_p = GST_VAAPI_VALUE_ID(value);
    return NULL;
}

static const GTypeValueTable gst_vaapi_id_value_table = {
    gst_vaapi_value_id_init,
    NULL,
    gst_vaapi_value_id_copy,
    NULL,
    GST_VAAPI_VALUE_ID_CFORMAT,
    gst_vaapi_value_id_collect,
    "p",
    gst_vaapi_value_id_lcopy
};

GST_VAAPI_TYPE_DEFINE(id, "GstVaapiID");

/**
 * gst_vaapi_value_get_id:
 * @value: a GValue initialized to #GstVaapiID
 *
 * Gets the integer contained in @value.
 *
 * Return value: the integer contained in @value
 */
GstVaapiID
gst_vaapi_value_get_id(const GValue *value)
{
    g_return_val_if_fail(GST_VAAPI_VALUE_HOLDS_ID(value), 0);

    return GST_VAAPI_VALUE_ID(value);
}

/**
 * gst_vaapi_value_set_id:
 * @value: a GValue initialized to #GstVaapiID
 * @id: a #GstVaapiID
 *
 * Sets the integer contained in @id to @value.
 */
void
gst_vaapi_value_set_id(GValue *value, GstVaapiID id)
{
    g_return_if_fail(GST_VAAPI_VALUE_HOLDS_ID(value));

    GST_VAAPI_VALUE_ID(value) = id;
}
