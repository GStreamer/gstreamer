/*
 *  gstvaapidisplay.h - VA display abstraction
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

#ifndef GST_VAAPI_DISPLAY_H
#define GST_VAAPI_DISPLAY_H

#ifdef GST_VAAPI_USE_OLD_VAAPI_0_29
# include <va.h>
#else
# include <va/va.h>
#endif

#include <gst/gst.h>
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
 * @lock: (optional) virtual function to lock a display
 * @unlock: (optional) virtual function to unlock a display
 * @sync: (optional) virtual function to sync a display
 * @flush: (optional) virtual function to flush pending requests of a display
 * @get_display: virtual function to retrieve the #VADisplay
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
    VADisplay  (*get_display)   (GstVaapiDisplay *display);
    void       (*get_size)      (GstVaapiDisplay *display,
                                 guint *pwidth, guint *pheight);
    void       (*get_size_mm)   (GstVaapiDisplay *display,
                                 guint *pwidth, guint *pheight);
};

GType
gst_vaapi_display_get_type(void);

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

G_END_DECLS

#endif /* GST_VAAPI_DISPLAY_H */
