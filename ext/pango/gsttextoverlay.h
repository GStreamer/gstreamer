#ifndef __GST_TEXT_OVERLAY_H__
#define __GST_TEXT_OVERLAY_H__

#include <gst/gst.h>
#include <gst/base/gstcollectpads.h>
#include <pango/pangoft2.h>

G_BEGIN_DECLS

#define GST_TYPE_TEXT_OVERLAY            (gst_text_overlay_get_type())
#define GST_TEXT_OVERLAY(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),\
                                         GST_TYPE_TEXT_OVERLAY, GstTextOverlay))
#define GST_TEXT_OVERLAY_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass),\
                                         GST_TYPE_TEXT_OVERLAY,GstTextOverlayClass))
#define GST_TEXT_OVERLAY_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),\
                                         GST_TYPE_TEXT_OVERLAY, GstTextOverlayClass))
#define GST_IS_TEXT_OVERLAY(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),\
                                         GST_TYPE_TEXT_OVERLAY))
#define GST_IS_TEXT_OVERLAY_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),\
                                         GST_TYPE_TEXT_OVERLAY))

typedef struct _GstTextOverlay      GstTextOverlay;
typedef struct _GstTextOverlayClass GstTextOverlayClass;

typedef enum _GstTextOverlayVAlign   GstTextOverlayVAlign;
typedef enum _GstTextOverlayHAlign   GstTextOverlayHAlign;
typedef enum _GstTextOverlayWrapMode GstTextOverlayWrapMode;

enum _GstTextOverlayVAlign {
    GST_TEXT_OVERLAY_VALIGN_BASELINE,
    GST_TEXT_OVERLAY_VALIGN_BOTTOM,
    GST_TEXT_OVERLAY_VALIGN_TOP
};

enum _GstTextOverlayHAlign {
    GST_TEXT_OVERLAY_HALIGN_LEFT,
    GST_TEXT_OVERLAY_HALIGN_CENTER,
    GST_TEXT_OVERLAY_HALIGN_RIGHT
};

enum _GstTextOverlayWrapMode {
    GST_TEXT_OVERLAY_WRAP_MODE_NONE = -1,
    GST_TEXT_OVERLAY_WRAP_MODE_WORD = PANGO_WRAP_WORD,
    GST_TEXT_OVERLAY_WRAP_MODE_CHAR = PANGO_WRAP_CHAR,
    GST_TEXT_OVERLAY_WRAP_MODE_WORD_CHAR = PANGO_WRAP_WORD_CHAR
};


struct _GstTextOverlay {
    GstElement               element;

    GstPad                  *video_sinkpad;
    GstPad                  *text_sinkpad;
    GstPad                  *srcpad;

    GstCollectPads          *collect;
    GstCollectData          *video_collect_data;
    GstCollectData          *text_collect_data;

    gint                     width;
    gint                     height;
    gint                     fps_n;
    gint                     fps_d;

    GstTextOverlayVAlign     valign;
    GstTextOverlayHAlign     halign;
    GstTextOverlayWrapMode   wrap_mode;

    gint                     xpad;
    gint                     ypad;
    gint                     deltax;
    gint                     deltay;
    gchar                   *default_text;
    gboolean                 want_shading;

    PangoLayout             *layout;
    FT_Bitmap                bitmap;
    gint                     bitmap_buffer_size;
    gint                     baseline_y;

    gboolean                 need_render;

    gint                     shading_value;  /* for timeoverlay subclass */
};

struct _GstTextOverlayClass {
    GstElementClass parent_class;

    PangoContext *pango_context;

    gchar *     (*get_text) (GstTextOverlay *overlay, GstBuffer *video_frame);

};

GType gst_text_overlay_get_type(void) G_GNUC_CONST;

G_END_DECLS

#endif /* __GST_TEXT_OVERLAY_H */
