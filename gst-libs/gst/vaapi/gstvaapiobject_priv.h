/*
 *  gstvaapiobject_priv.h - Base VA object (private definitions)
 *
 *  Copyright (C) 2010-2011 Splitted-Desktop Systems
 *    Author: Gwenole Beauchesne <gwenole.beauchesne@splitted-desktop.com>
 *  Copyright (C) 2012-2013 Intel Corporation
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

#ifndef GST_VAAPI_OBJECT_PRIV_H
#define GST_VAAPI_OBJECT_PRIV_H

#include <gst/vaapi/gstvaapiobject.h>
#include "gstvaapiminiobject.h"
#include "gstvaapidisplay_priv.h"

G_BEGIN_DECLS

#define GST_VAAPI_OBJECT_CLASS(klass) \
  ((GstVaapiObjectClass *) (klass))
#define GST_VAAPI_IS_OBJECT_CLASS(klass) \
  ((klass) != NULL)
#define GST_VAAPI_OBJECT_GET_CLASS(object) \
  GST_VAAPI_OBJECT_CLASS (GST_VAAPI_MINI_OBJECT_GET_CLASS (object))

typedef struct _GstVaapiObjectClass GstVaapiObjectClass;
typedef void (*GstVaapiObjectInitFunc) (GstVaapiObject * object);
typedef void (*GstVaapiObjectFinalizeFunc) (GstVaapiObject * object);

#define GST_VAAPI_OBJECT_DEFINE_CLASS_WITH_CODE(TN, t_n, code)  \
static inline const GstVaapiObjectClass *                       \
G_PASTE(t_n,_class) (void)                                      \
{                                                               \
    static G_PASTE(TN,Class) g_class;                           \
    static gsize g_class_init = FALSE;                          \
                                                                \
    if (g_once_init_enter (&g_class_init)) {                    \
        GstVaapiObjectClass * const klass =                     \
            GST_VAAPI_OBJECT_CLASS (&g_class);                  \
        gst_vaapi_object_class_init (klass, sizeof(TN));        \
        code;                                                   \
        klass->finalize = (GstVaapiObjectFinalizeFunc)          \
            G_PASTE(t_n,_finalize);                             \
        g_once_init_leave (&g_class_init, TRUE);                \
    }                                                           \
    return GST_VAAPI_OBJECT_CLASS (&g_class);                   \
}

#define GST_VAAPI_OBJECT_DEFINE_CLASS(TN, t_n) \
  GST_VAAPI_OBJECT_DEFINE_CLASS_WITH_CODE (TN, t_n, /**/)

/**
 * GST_VAAPI_OBJECT_ID:
 * @object: a #GstVaapiObject
 *
 * Macro that evaluates to the #GstVaapiID contained in @object.
 * This is an internal macro that does not do any run-time type checks.
 */
#undef  GST_VAAPI_OBJECT_ID
#define GST_VAAPI_OBJECT_ID(object) \
  (GST_VAAPI_OBJECT (object)->object_id)

/**
 * GST_VAAPI_OBJECT_DISPLAY:
 * @object: a #GstVaapiObject
 *
 * Macro that evaluates to the #GstVaapiDisplay the @object is bound to.
 * This is an internal macro that does not do any run-time type check.
 */
#undef  GST_VAAPI_OBJECT_DISPLAY
#define GST_VAAPI_OBJECT_DISPLAY(object) \
  (GST_VAAPI_OBJECT (object)->display)

/**
 * GST_VAAPI_OBJECT_DISPLAY_X11:
 * @object: a #GstVaapiObject
 *
 * Macro that evaluates to the #GstVaapiDisplayX11 the @object is bound to.
 * This is an internal macro that does not do any run-time type check
 * and requires #include "gstvaapidisplay_x11_priv.h"
 */
#define GST_VAAPI_OBJECT_DISPLAY_X11(object) \
  GST_VAAPI_DISPLAY_X11_CAST (GST_VAAPI_OBJECT_DISPLAY (object))

/**
 * GST_VAAPI_OBJECT_DISPLAY_GLX:
 * @object: a #GstVaapiObject
 *
 * Macro that evaluates to the #GstVaapiDisplayGLX the @object is bound to.
 * This is an internal macro that does not do any run-time type check
 * and requires #include "gstvaapidisplay_glx_priv.h".
 */
#define GST_VAAPI_OBJECT_DISPLAY_GLX(object) \
  GST_VAAPI_DISPLAY_GLX_CAST (GST_VAAPI_OBJECT_DISPLAY (object))

/**
 * GST_VAAPI_OBJECT_DISPLAY_WAYLAND:
 * @object: a #GstVaapiObject
 *
 * Macro that evaluates to the #GstVaapiDisplayWayland the @object is
 * bound to.  This is an internal macro that does not do any run-time
 * type check and requires #include "gstvaapidisplay_wayland_priv.h"
 */
#define GST_VAAPI_OBJECT_DISPLAY_WAYLAND(object) \
  GST_VAAPI_DISPLAY_WAYLAND_CAST (GST_VAAPI_OBJECT_DISPLAY (object))

/**
 * GST_VAAPI_OBJECT_VADISPLAY:
 * @object: a #GstVaapiObject
 *
 * Macro that evaluates to the #VADisplay of @display.
 * This is an internal macro that does not do any run-time type check
 * and requires #include "gstvaapidisplay_priv.h".
 */
#define GST_VAAPI_OBJECT_VADISPLAY(object) \
  GST_VAAPI_DISPLAY_VADISPLAY (GST_VAAPI_OBJECT_DISPLAY (object))

/**
 * GST_VAAPI_OBJECT_NATIVE_DISPLAY:
 * @object: a #GstVaapiObject
 *
 * Macro that evaluates to the underlying native @display object.
 * This is an internal macro that does not do any run-time type check.
 */
#define GST_VAAPI_OBJECT_NATIVE_DISPLAY(object) \
  GST_VAAPI_DISPLAY_NATIVE (GST_VAAPI_OBJECT_DISPLAY (object))

/**
 * GST_VAAPI_OBJECT_LOCK_DISPLAY:
 * @object: a #GstVaapiObject
 *
 * Macro that locks the #GstVaapiDisplay contained in the @object.
 * This is an internal macro that does not do any run-time type check.
 */
#define GST_VAAPI_OBJECT_LOCK_DISPLAY(object) \
  GST_VAAPI_DISPLAY_LOCK (GST_VAAPI_OBJECT_DISPLAY (object))

/**
 * GST_VAAPI_OBJECT_UNLOCK_DISPLAY:
 * @object: a #GstVaapiObject
 *
 * Macro that unlocks the #GstVaapiDisplay contained in the @object.
 * This is an internal macro that does not do any run-time type check.
 */
#define GST_VAAPI_OBJECT_UNLOCK_DISPLAY(object) \
  GST_VAAPI_DISPLAY_UNLOCK (GST_VAAPI_OBJECT_DISPLAY (object))

/**
 * GstVaapiObject:
 *
 * VA object base.
 */
struct _GstVaapiObject
{
  /*< private >*/
  GstVaapiMiniObject parent_instance;

  GstVaapiDisplay *display;
  GstVaapiID object_id;
};

/**
 * GstVaapiObjectClass:
 *
 * VA object base class.
 */
struct _GstVaapiObjectClass
{
  /*< private >*/
  GstVaapiMiniObjectClass parent_class;

  GstVaapiObjectInitFunc init;
  GstVaapiObjectFinalizeFunc finalize;
};

void
gst_vaapi_object_class_init (GstVaapiObjectClass * klass, guint size);

gpointer
gst_vaapi_object_new (const GstVaapiObjectClass * klass,
    GstVaapiDisplay * display);

/* Inline reference counting for core libgstvaapi library */
#ifdef IN_LIBGSTVAAPI_CORE
static inline gpointer
gst_vaapi_object_ref_internal (gpointer object)
{
  return gst_vaapi_mini_object_ref (object);
}

static inline void
gst_vaapi_object_unref_internal (gpointer object)
{
  gst_vaapi_mini_object_unref (object);
}

static inline void
gst_vaapi_object_replace_internal (gpointer old_object_ptr, gpointer new_object)
{
  gst_vaapi_mini_object_replace ((GstVaapiMiniObject **) old_object_ptr,
      new_object);
}

#undef  gst_vaapi_object_ref
#define gst_vaapi_object_ref(object) \
  gst_vaapi_object_ref_internal ((object))

#undef  gst_vaapi_object_unref
#define gst_vaapi_object_unref(object) \
  gst_vaapi_object_unref_internal ((object))

#undef  gst_vaapi_object_replace
#define gst_vaapi_object_replace(old_object_ptr, new_object) \
  gst_vaapi_object_replace_internal ((old_object_ptr), (new_object))
#endif

G_END_DECLS

#endif /* GST_VAAPI_OBJECT_PRIV_H */
