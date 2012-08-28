/*
 *  gstvaapidisplay.h - VA display abstraction
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

#ifndef GST_VAAPI_DISPLAY_H
#define GST_VAAPI_DISPLAY_H

#include <va/va.h>
#include <gst/gst.h>
#include <gst/vaapi/gstvaapitypes.h>
#include <gst/vaapi/gstvaapiimageformat.h>
#include <gst/vaapi/gstvaapiprofile.h>

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

typedef enum   _GstVaapiDisplayType             GstVaapiDisplayType;
typedef struct _GstVaapiDisplayInfo             GstVaapiDisplayInfo;
typedef struct _GstVaapiDisplay                 GstVaapiDisplay;
typedef struct _GstVaapiDisplayPrivate          GstVaapiDisplayPrivate;
typedef struct _GstVaapiDisplayClass            GstVaapiDisplayClass;

/**
 * GstVaapiDisplayType:
 * @GST_VAAPI_DISPLAY_TYPE_ANY: Automatic detection of the display type.
 * @GST_VAAPI_DISPLAY_TYPE_X11: VA/X11 display.
 * @GST_VAAPI_DISPLAY_TYPE_GLX: VA/GLX display.
 * @GST_VAAPI_DISPLAY_TYPE_WAYLAND: VA/Wayland display.
 * @GST_VAAPI_DISPLAY_TYPE_DRM: VA/DRM display.
 */
enum _GstVaapiDisplayType {
    GST_VAAPI_DISPLAY_TYPE_ANY = 0,
    GST_VAAPI_DISPLAY_TYPE_X11,
    GST_VAAPI_DISPLAY_TYPE_GLX,
    GST_VAAPI_DISPLAY_TYPE_WAYLAND,
    GST_VAAPI_DISPLAY_TYPE_DRM,
};

#define GST_VAAPI_TYPE_DISPLAY_TYPE \
    (gst_vaapi_display_type_get_type())

GType
gst_vaapi_display_type_get_type(void) G_GNUC_CONST;

/**
 * GstVaapiDisplayInfo:
 *
 * Generic class to retrieve VA display info
 */
struct _GstVaapiDisplayInfo {
    GstVaapiDisplay    *display;
    GstVaapiDisplayType display_type;
    gchar              *display_name;
    VADisplay           va_display;
    gpointer            native_display;
};

/**
 * GstVaapiDisplayProperties:
 * @GST_VAAPI_DISPLAY_PROP_RENDER_MODE: rendering mode (#GstVaapiRenderMode).
 * @GST_VAAPI_DISPLAY_PROP_ROTATION: rotation angle (#GstVaapiRotation).
 * @GST_VAAPI_DISPLAY_PROP_HUE: hue (float: [-180 ; 180], default: 0).
 * @GST_VAAPI_DISPLAY_PROP_SATURATION: saturation (float: [0 ; 2], default: 1).
 * @GST_VAAPI_DISPLAY_PROP_BRIGHTNESS: brightness (float: [-1 ; 1], default: 0).
 * @GST_VAAPI_DISPLAY_PROP_CONTRAST: contrast (float: [0 ; 2], default: 1).
 */
#define GST_VAAPI_DISPLAY_PROP_RENDER_MODE      "render-mode"
#define GST_VAAPI_DISPLAY_PROP_ROTATION         "rotation"
#define GST_VAAPI_DISPLAY_PROP_HUE              "hue"
#define GST_VAAPI_DISPLAY_PROP_SATURATION       "saturation"
#define GST_VAAPI_DISPLAY_PROP_BRIGHTNESS       "brightness"
#define GST_VAAPI_DISPLAY_PROP_CONTRAST         "contrast"

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
 * @lock: (optional) virtual function to lock a display
 * @unlock: (optional) virtual function to unlock a display
 * @sync: (optional) virtual function to sync a display
 * @flush: (optional) virtual function to flush pending requests of a display
 * @get_display: virtual function to retrieve the #GstVaapiDisplayInfo
 * @get_size: virtual function to retrieve the display dimensions, in pixels
 * @get_size_mm: virtual function to retrieve the display dimensions, in millimeters
 *
 * Base class for VA displays.
 */
struct _GstVaapiDisplayClass {
    /*< private >*/
    GObjectClass parent_class;

    /*< public >*/
    gboolean   (*open_display)  (GstVaapiDisplay *display);
    void       (*close_display) (GstVaapiDisplay *display);
    void       (*lock)          (GstVaapiDisplay *display);
    void       (*unlock)        (GstVaapiDisplay *display);
    void       (*sync)          (GstVaapiDisplay *display);
    void       (*flush)         (GstVaapiDisplay *display);
    gboolean   (*get_display)   (GstVaapiDisplay *display,
                                 GstVaapiDisplayInfo *info);
    void       (*get_size)      (GstVaapiDisplay *display,
                                 guint *pwidth, guint *pheight);
    void       (*get_size_mm)   (GstVaapiDisplay *display,
                                 guint *pwidth, guint *pheight);
};

GType
gst_vaapi_display_get_type(void) G_GNUC_CONST;

GstVaapiDisplay *
gst_vaapi_display_new_with_display(VADisplay va_display);

void
gst_vaapi_display_lock(GstVaapiDisplay *display);

void
gst_vaapi_display_unlock(GstVaapiDisplay *display);

void
gst_vaapi_display_sync(GstVaapiDisplay *display);

void
gst_vaapi_display_flush(GstVaapiDisplay *display);

GstVaapiDisplayType
gst_vaapi_display_get_display_type(GstVaapiDisplay *display);

VADisplay
gst_vaapi_display_get_display(GstVaapiDisplay *display);

guint
gst_vaapi_display_get_width(GstVaapiDisplay *display);

guint
gst_vaapi_display_get_height(GstVaapiDisplay *display);

void
gst_vaapi_display_get_size(GstVaapiDisplay *display, guint *pwidth, guint *pheight);

void
gst_vaapi_display_get_pixel_aspect_ratio(
    GstVaapiDisplay *display,
    guint           *par_n,
    guint           *par_d
);

GstCaps *
gst_vaapi_display_get_decode_caps(GstVaapiDisplay *display);

gboolean
gst_vaapi_display_has_decoder(
    GstVaapiDisplay    *display,
    GstVaapiProfile     profile,
    GstVaapiEntrypoint  entrypoint
);

GstCaps *
gst_vaapi_display_get_encode_caps(GstVaapiDisplay *display);

gboolean
gst_vaapi_display_has_encoder(
    GstVaapiDisplay    *display,
    GstVaapiProfile     profile,
    GstVaapiEntrypoint  entrypoint
);

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

gboolean
gst_vaapi_display_has_property(GstVaapiDisplay *display, const gchar *name);

gboolean
gst_vaapi_display_get_render_mode(
    GstVaapiDisplay    *display,
    GstVaapiRenderMode *pmode
);

gboolean
gst_vaapi_display_set_render_mode(
    GstVaapiDisplay   *display,
    GstVaapiRenderMode mode
);

GstVaapiRotation
gst_vaapi_display_get_rotation(GstVaapiDisplay *display);

gboolean
gst_vaapi_display_set_rotation(
    GstVaapiDisplay *display,
    GstVaapiRotation rotation
);

G_END_DECLS

#endif /* GST_VAAPI_DISPLAY_H */
