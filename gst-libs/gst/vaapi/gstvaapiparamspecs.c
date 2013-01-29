/*
 *  gstvaapiparamspecs.c - GParamSpecs for some of our types
 *
 *  Copyright (C) 2010-2011 Splitted-Desktop Systems
 *  Copyright (C) 2012 Intel Corporation
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
 * SECTION:gstvaapiparamspecs
 * @short_description: GParamSpecs for some of our types
 */

#include "sysdeps.h"
#include "gstvaapiparamspecs.h"
#include "gstvaapivalue.h"

/* --- GstVaapiParamSpecID --- */

static void
gst_vaapi_param_id_init(GParamSpec *pspec)
{
    GST_VAAPI_PARAM_SPEC_ID(pspec)->default_value = GST_VAAPI_ID_NONE;
}

static void
gst_vaapi_param_id_set_default(GParamSpec *pspec, GValue *value)
{
    gst_vaapi_value_set_id(value, GST_VAAPI_PARAM_SPEC_ID(pspec)->default_value);
}

static gboolean
gst_vaapi_param_id_validate(GParamSpec *pspec, GValue *value)
{
    /* Return FALSE if everything is OK, otherwise TRUE */
    return FALSE;
}

static gint
gst_vaapi_param_id_compare(
    GParamSpec   *pspec,
    const GValue *value1,
    const GValue *value2
)
{
    const GstVaapiID v1 = gst_vaapi_value_get_id(value1);
    const GstVaapiID v2 = gst_vaapi_value_get_id(value2);

    return (v1 < v2 ? -1 : (v1 > v2 ? 1 : 0));
}

GType
gst_vaapi_param_spec_id_get_type(void)
{
    static GType type;

    if (G_UNLIKELY(type == 0)) {
        static GParamSpecTypeInfo pspec_info = {
            sizeof(GstVaapiParamSpecID),        /* instance_size     */
            0,                                  /* n_preallocs       */
            gst_vaapi_param_id_init,            /* instance_init     */
            G_TYPE_INVALID,                     /* value_type        */
            NULL,                               /* finalize          */
            gst_vaapi_param_id_set_default,     /* value_set_default */
            gst_vaapi_param_id_validate,        /* value_validate    */
            gst_vaapi_param_id_compare,         /* values_cmp        */
        };
        pspec_info.value_type = GST_VAAPI_TYPE_ID;
        type = g_param_type_register_static("GstVaapiParamSpecID", &pspec_info);
    }
    return type;
}

/**
 * gst_vaapi_param_spec_id:
 * @name: canonical name of the property specified
 * @nick: nick name for the property specified
 * @blurb: description of the property specified
 * @default_value: default value
 * @flags: flags for the property specified
 *
 * This function creates an ID GParamSpec for use by #GstVaapiObject
 * objects. This function is typically used in connection with
 * g_object_class_install_property() in a GObjects's instance_init
 * function.
 *
 * Return value: a newly created parameter specification
 */
GParamSpec *
gst_vaapi_param_spec_id(
    const gchar *name,
    const gchar *nick,
    const gchar *blurb,
    GstVaapiID   default_value,
    GParamFlags  flags
)
{
    GstVaapiParamSpecID *ispec;
    GParamSpec *pspec;
    GValue value = { 0, };

    ispec = g_param_spec_internal(
        GST_VAAPI_TYPE_PARAM_ID,
        name,
        nick,
        blurb,
        flags
    );
    if (!ispec)
        return NULL;

    ispec->default_value = default_value;
    pspec = G_PARAM_SPEC(ispec);

    /* Validate default value */
    g_value_init(&value, GST_VAAPI_TYPE_ID);
    gst_vaapi_value_set_id(&value, default_value);
    if (gst_vaapi_param_id_validate(pspec, &value)) {
        g_param_spec_ref(pspec);
        g_param_spec_sink(pspec);
        g_param_spec_unref(pspec);
        pspec = NULL;
    }
    g_value_unset(&value);

    return pspec;
}
