/*
 *  gstvaapicontext.h - VA context abstraction
 *
 *  Copyright (C) 2010-2011 Splitted-Desktop Systems
 *  Copyright (C) 2011-2012 Intel Corporation
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

#ifndef GST_VAAPI_CONTEXT_H
#define GST_VAAPI_CONTEXT_H

#include <gst/vaapi/gstvaapiobject.h>
#include <gst/vaapi/gstvaapiprofile.h>
#include <gst/vaapi/gstvaapidisplay.h>
#include <gst/vaapi/gstvaapisurface.h>
#include <gst/video/video-overlay-composition.h>

G_BEGIN_DECLS

#define GST_VAAPI_TYPE_CONTEXT \
    (gst_vaapi_context_get_type())

#define GST_VAAPI_CONTEXT(obj)                          \
    (G_TYPE_CHECK_INSTANCE_CAST((obj),                  \
                                GST_VAAPI_TYPE_CONTEXT, \
                                GstVaapiContext))

#define GST_VAAPI_CONTEXT_CLASS(klass)                  \
    (G_TYPE_CHECK_CLASS_CAST((klass),                   \
                             GST_VAAPI_TYPE_CONTEXT,    \
                             GstVaapiContextClass))

#define GST_VAAPI_IS_CONTEXT(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_VAAPI_TYPE_CONTEXT))

#define GST_VAAPI_IS_CONTEXT_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass), GST_VAAPI_TYPE_CONTEXT))

#define GST_VAAPI_CONTEXT_GET_CLASS(obj)                \
    (G_TYPE_INSTANCE_GET_CLASS((obj),                   \
                               GST_VAAPI_TYPE_CONTEXT,  \
                               GstVaapiContextClass))

typedef struct _GstVaapiContext                 GstVaapiContext;
typedef struct _GstVaapiContextInfo             GstVaapiContextInfo;
typedef struct _GstVaapiContextPrivate          GstVaapiContextPrivate;
typedef struct _GstVaapiContextClass            GstVaapiContextClass;

/**
 * GstVaapiContextInfo:
 *
 * Structure holding VA context info like encoded size, decoder
 * profile and entry-point to use, and maximum number of reference
 * frames reported by the bitstream.
 */
struct _GstVaapiContextInfo {
    GstVaapiProfile     profile;
    GstVaapiEntrypoint  entrypoint;
    guint               width;
    guint               height;
    guint               ref_frames;
};

/**
 * GstVaapiContext:
 *
 * A VA context wrapper.
 */
struct _GstVaapiContext {
    /*< private >*/
    GstVaapiObject parent_instance;

    GstVaapiContextPrivate *priv;
};

/**
 * GstVaapiContextClass:
 *
 * A VA context wrapper class.
 */
struct _GstVaapiContextClass {
    /*< private >*/
    GstVaapiObjectClass parent_class;
};

GType
gst_vaapi_context_get_type(void) G_GNUC_CONST;

GstVaapiContext *
gst_vaapi_context_new(
    GstVaapiDisplay    *display,
    GstVaapiProfile     profile,
    GstVaapiEntrypoint  entrypoint,
    guint               width,
    guint               height
);

GstVaapiContext *
gst_vaapi_context_new_full(GstVaapiDisplay *display, GstVaapiContextInfo *cip);

gboolean
gst_vaapi_context_reset(
    GstVaapiContext    *context,
    GstVaapiProfile     profile,
    GstVaapiEntrypoint  entrypoint,
    guint               width,
    guint               height
);

gboolean
gst_vaapi_context_reset_full(GstVaapiContext *context, GstVaapiContextInfo *cip);

GstVaapiID
gst_vaapi_context_get_id(GstVaapiContext *context);

GstVaapiProfile
gst_vaapi_context_get_profile(GstVaapiContext *context);

gboolean
gst_vaapi_context_set_profile(GstVaapiContext *context, GstVaapiProfile profile);

GstVaapiEntrypoint
gst_vaapi_context_get_entrypoint(GstVaapiContext *context);

void
gst_vaapi_context_get_size(
    GstVaapiContext *context,
    guint           *pwidth,
    guint           *pheight
);

GstVaapiSurface *
gst_vaapi_context_get_surface(GstVaapiContext *context);

guint
gst_vaapi_context_get_surface_count(GstVaapiContext *context);

void
gst_vaapi_context_put_surface(GstVaapiContext *context, GstVaapiSurface *surface);

GstVaapiSurface *
gst_vaapi_context_find_surface_by_id(GstVaapiContext *context, GstVaapiID id);

gboolean
gst_vaapi_context_apply_composition(
    GstVaapiContext            *context,
    GstVideoOverlayComposition *composition
);

G_END_DECLS

#endif /* GST_VAAPI_CONTEXT_H */
