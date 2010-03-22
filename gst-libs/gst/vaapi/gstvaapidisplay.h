/*
 *  gstvaapidisplay.h - VA display abstraction
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

#ifndef GST_VAAPI_DISPLAY_H
#define GST_VAAPI_DISPLAY_H

#include <va/va.h>
#include <gst/gst.h>
#include <gst/vaapi/gstvaapiimageformat.h>

G_BEGIN_DECLS

#define GST_VAAPI_TYPE_DISPLAY \
    (gst_vaapi_display_get_type())

#define GST_VAAPI_DISPLAY(obj)                          \
    (G_TYPE_CHECK_INSTANCE_CAST((obj),                  \
                                GST_VAAPI_TYPE_DISPLAY, \
                                GstVaapiDisplay))

#define GST_VAAPI_DISPLAY_CLASS(klass)                  \
    (G_TYPE_CHECK_CLASS_CAST((klass),                   \
                             GST_VAAPI_TYPE_DISPLAY,    \
                             GstVaapiDisplayClass))

#define GST_VAAPI_IS_DISPLAY(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_VAAPI_TYPE_DISPLAY))

#define GST_VAAPI_IS_DISPLAY_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass), GST_VAAPI_TYPE_DISPLAY))

#define GST_VAAPI_DISPLAY_GET_CLASS(obj)                \
    (G_TYPE_INSTANCE_GET_CLASS((obj),                   \
                               GST_VAAPI_TYPE_DISPLAY,  \
                               GstVaapiDisplayClass))

/**
 * GST_VAAPI_DISPLAY_VADISPLAY:
 * @display: a #GstVaapiDisplay
 *
 * Macro that evaluates to the #VADisplay bound to @display
 */
#define GST_VAAPI_DISPLAY_VADISPLAY(display) \
    gst_vaapi_display_get_display(display)

/**
 * GST_VAAPI_DISPLAY_LOCK:
 * @display: a #GstVaapiDisplay
 *
 * Locks @display
 */
#define GST_VAAPI_DISPLAY_LOCK(display) \
    gst_vaapi_display_lock(display)

/**
 * GST_VAAPI_DISPLAY_UNLOCK:
 * @display: a #GstVaapiDisplay
 *
 * Unlocks @display
 */
#define GST_VAAPI_DISPLAY_UNLOCK(display) \
    gst_vaapi_display_unlock(display)

typedef struct _GstVaapiDisplay                 GstVaapiDisplay;
typedef struct _GstVaapiDisplayPrivate          GstVaapiDisplayPrivate;
typedef struct _GstVaapiDisplayClass            GstVaapiDisplayClass;

/**
 * GstVaapiDisplay:
 *
 * Base class for VA displays.
 */
struct _GstVaapiDisplay {
    /*< private >*/
    GObject parent_instance;

    GstVaapiDisplayPrivate *priv;
};

/**
 * GstVaapiDisplayClass:
 * @open_display: virtual function to open a display
 * @close_display: virtual function to close a display
 * @lock_display: virtual function to lock a display
 * @unlock_display: virtual function to unlock a display
 * @get_display: virtual function to retrieve the #VADisplay
 * @get_size: virtual function to retrieve the display dimensions
 *
 * Base class for VA displays.
 */
struct _GstVaapiDisplayClass {
    /*< private >*/
    GObjectClass parent_class;

    /*< public >*/
    gboolean   (*open_display)  (GstVaapiDisplay *display);
    void       (*close_display) (GstVaapiDisplay *display);
    void       (*lock_display)  (GstVaapiDisplay *display);
    void       (*unlock_display)(GstVaapiDisplay *display);
    VADisplay  (*get_display)   (GstVaapiDisplay *display);
    void       (*get_size)      (GstVaapiDisplay *display, guint *pw, guint *ph);
};

GType
gst_vaapi_display_get_type(void);

GstVaapiDisplay *
gst_vaapi_display_new_with_display(VADisplay va_display);

void
gst_vaapi_display_lock(GstVaapiDisplay *display);

void
gst_vaapi_display_unlock(GstVaapiDisplay *display);

VADisplay
gst_vaapi_display_get_display(GstVaapiDisplay *display);

guint
gst_vaapi_display_get_width(GstVaapiDisplay *display);

guint
gst_vaapi_display_get_height(GstVaapiDisplay *display);

void
gst_vaapi_display_get_size(GstVaapiDisplay *display, guint *pwidth, guint *pheight);

gboolean
gst_vaapi_display_has_profile(GstVaapiDisplay *display, VAProfile profile);

GstCaps *
gst_vaapi_display_get_image_caps(GstVaapiDisplay *display);

gboolean
gst_vaapi_display_has_image_format(
    GstVaapiDisplay    *display,
    GstVaapiImageFormat format
);

GstCaps *
gst_vaapi_display_get_subpicture_caps(GstVaapiDisplay *display);

gboolean
gst_vaapi_display_has_subpicture_format(
    GstVaapiDisplay    *display,
    GstVaapiImageFormat format
);

G_END_DECLS

#endif /* GST_VAAPI_DISPLAY_H */
