/*
 *  gstvaapiobject.c - Base VA object
 *
 *  Copyright (C) 2010-2011 Splitted-Desktop Systems
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
 * SECTION:gstvaapiobject
 * @short_description: Base VA object
 */

#include "sysdeps.h"
#include "gstvaapiobject.h"
#include "gstvaapi_priv.h"
#include "gstvaapiparamspecs.h"
#include "gstvaapivalue.h"

#define DEBUG 1
#include "gstvaapidebug.h"

G_DEFINE_TYPE(GstVaapiObject, gst_vaapi_object, G_TYPE_OBJECT);

enum {
    PROP_0,

    PROP_DISPLAY,
    PROP_ID
};

enum {
    DESTROY,

    LAST_SIGNAL
};

static guint object_signals[LAST_SIGNAL] = { 0, };

static void
gst_vaapi_object_dispose(GObject *object)
{
    GstVaapiObjectPrivate * const priv = GST_VAAPI_OBJECT(object)->priv;

    if (!priv->is_destroying) {
        priv->is_destroying = TRUE;
        g_signal_emit(object, object_signals[DESTROY], 0);
        priv->is_destroying = FALSE;
    }

    G_OBJECT_CLASS(gst_vaapi_object_parent_class)->dispose(object);
}

static void
gst_vaapi_object_finalize(GObject *object)
{
    GstVaapiObjectPrivate * const priv = GST_VAAPI_OBJECT(object)->priv;

    priv->id = GST_VAAPI_ID_NONE;

    g_clear_object(&priv->display);

    G_OBJECT_CLASS(gst_vaapi_object_parent_class)->finalize(object);
}

static void
gst_vaapi_object_set_property(
    GObject      *gobject,
    guint         prop_id,
    const GValue *value,
    GParamSpec   *pspec
)
{
    GstVaapiObject * const object = GST_VAAPI_OBJECT(gobject);

    switch (prop_id) {
    case PROP_DISPLAY:
        object->priv->display = g_object_ref(g_value_get_object(value));
        break;
    case PROP_ID:
        object->priv->id = gst_vaapi_value_get_id(value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(gobject, prop_id, pspec);
        break;
    }
}

static void
gst_vaapi_object_get_property(
    GObject    *gobject,
    guint       prop_id,
    GValue     *value,
    GParamSpec *pspec
)
{
    GstVaapiObject * const object = GST_VAAPI_OBJECT(gobject);

    switch (prop_id) {
    case PROP_DISPLAY:
        g_value_set_object(value, gst_vaapi_object_get_display(object));
        break;
    case PROP_ID:
        gst_vaapi_value_set_id(value, gst_vaapi_object_get_id(object));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(gobject, prop_id, pspec);
        break;
    }
}

static void
gst_vaapi_object_class_init(GstVaapiObjectClass *klass)
{
    GObjectClass * const object_class = G_OBJECT_CLASS(klass);

    g_type_class_add_private(klass, sizeof(GstVaapiObjectPrivate));

    object_class->dispose      = gst_vaapi_object_dispose;
    object_class->finalize     = gst_vaapi_object_finalize;
    object_class->set_property = gst_vaapi_object_set_property;
    object_class->get_property = gst_vaapi_object_get_property;

    /**
     * GstVaapiObject:display:
     *
     * The #GstVaapiDisplay this object is bound to.
     */
    g_object_class_install_property
        (object_class,
         PROP_DISPLAY,
         g_param_spec_object("display",
                             "Display",
                             "The GstVaapiDisplay this object is bound to",
                             GST_VAAPI_TYPE_DISPLAY,
                             G_PARAM_READWRITE|G_PARAM_CONSTRUCT_ONLY));

    /**
     * GstVaapiObject:id:
     *
     * The #GstVaapiID contained in this object.
     */
    g_object_class_install_property
        (object_class,
         PROP_ID,
         gst_vaapi_param_spec_id("id",
                                 "ID",
                                 "The GstVaapiID contained in this object",
                                 GST_VAAPI_ID_NONE,
                                 G_PARAM_READWRITE|G_PARAM_CONSTRUCT_ONLY));

    /**
     * GstVaapiObject::destroy:
     * @object: the object which received the signal
     *
     * The ::destroy signal is emitted when an object is destroyed,
     * when the user released the last reference to @object.
     */
    object_signals[DESTROY] = g_signal_new(
        "destroy",
        G_TYPE_FROM_CLASS(object_class),
        G_SIGNAL_RUN_CLEANUP | G_SIGNAL_NO_RECURSE | G_SIGNAL_NO_HOOKS,
        G_STRUCT_OFFSET(GstVaapiObjectClass, destroy),
        NULL, NULL,
        g_cclosure_marshal_VOID__VOID,
        G_TYPE_NONE, 0
    );
}

static void
gst_vaapi_object_init(GstVaapiObject *object)
{
    GstVaapiObjectPrivate *priv = GST_VAAPI_OBJECT_GET_PRIVATE(object);

    object->priv        = priv;
    priv->display       = NULL;
    priv->id            = GST_VAAPI_ID_NONE;
    priv->is_destroying = FALSE;
}

/**
 * gst_vaapi_object_get_display:
 * @object: a #GstVaapiObject
 *
 * Returns the #GstVaapiDisplay this @object is bound to.
 *
 * Return value: the parent #GstVaapiDisplay object
 */
GstVaapiDisplay *
gst_vaapi_object_get_display(GstVaapiObject *object)
{
    g_return_val_if_fail(GST_VAAPI_IS_OBJECT(object), NULL);

    return object->priv->display;
}

/**
 * gst_vaapi_object_lock_display:
 * @object: a #GstVaapiObject
 *
 * Locks @object parent display. If display is already locked by
 * another thread, the current thread will block until display is
 * unlocked by the other thread.
 */
void
gst_vaapi_object_lock_display(GstVaapiObject *object)
{
    g_return_if_fail(GST_VAAPI_IS_OBJECT(object));

    GST_VAAPI_OBJECT_LOCK_DISPLAY(object);
}

/**
 * gst_vaapi_object_unlock_display:
 * @object: a #GstVaapiObject
 *
 * Unlocks @object parent display. If another thread is blocked in a
 * gst_vaapi_object_lock_display() call, it will be woken and can lock
 * display itself.
 */
void
gst_vaapi_object_unlock_display(GstVaapiObject *object)
{
    g_return_if_fail(GST_VAAPI_IS_OBJECT(object));

    GST_VAAPI_OBJECT_UNLOCK_DISPLAY(object);
}

/**
 * gst_vaapi_object_get_id:
 * @object: a #GstVaapiObject
 *
 * Returns the #GstVaapiID contained in the @object.
 *
 * Return value: the #GstVaapiID of the @object
 */
GstVaapiID
gst_vaapi_object_get_id(GstVaapiObject *object)
{
    g_return_val_if_fail(GST_VAAPI_IS_OBJECT(object), GST_VAAPI_ID_NONE);

    return object->priv->id;
}
