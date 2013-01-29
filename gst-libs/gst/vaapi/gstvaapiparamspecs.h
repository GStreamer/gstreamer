/*
 *  gstvaapiparamspecs.h - GParamSpecs for some of our types
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

#ifndef GST_VAAPI_PARAM_SPECS_H
#define GST_VAAPI_PARAM_SPECS_H

#include <gst/vaapi/gstvaapitypes.h>
#include <glib-object.h>

G_BEGIN_DECLS

/**
 * GstVaapiParamSpecID:
 * @parent_instance: super class
 * @default_value: default value
 *
 * A GParamSpec derived structure that contains the meta data for
 * #GstVaapiID properties.
 */
typedef struct _GstVaapiParamSpecID GstVaapiParamSpecID;
struct _GstVaapiParamSpecID {
    GParamSpec  parent_instance;

    GstVaapiID  default_value;
};

#define GST_VAAPI_TYPE_PARAM_ID \
    (gst_vaapi_param_spec_id_get_type())

#define GST_VAAPI_IS_PARAM_SPEC_ID(pspec)                       \
    (G_TYPE_CHECK_INSTANCE_TYPE((pspec),                        \
                                GST_VAAPI_TYPE_PARAM_ID))

#define GST_VAAPI_PARAM_SPEC_ID(pspec)                          \
    (G_TYPE_CHECK_INSTANCE_CAST((pspec),                        \
                                GST_VAAPI_TYPE_PARAM_ID,        \
                                GstVaapiParamSpecID))

GType
gst_vaapi_param_spec_id_get_type(void) G_GNUC_CONST;

GParamSpec *
gst_vaapi_param_spec_id(
    const gchar *name,
    const gchar *nick,
    const gchar *blurb,
    GstVaapiID   default_value,
    GParamFlags  flags
);

G_END_DECLS

#endif /* GST_VAAPI_PARAM_SPECS_H */
