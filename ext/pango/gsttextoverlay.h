/* -*- Mode: C; c-file-style: "stroustrup" -*- */
#ifndef __GST_TEXTOVERLAY_H__
#define __GST_TEXTOVERLAY_H__

#include <gst/gst.h>
#include <pango/pangoft2.h>

G_BEGIN_DECLS

#define GST_TYPE_TEXTOVERLAY           (gst_textoverlay_get_type())
#define GST_TEXTOVERLAY(obj)           (G_TYPE_CHECK_INSTANCE_CAST((obj),\
                                        GST_TYPE_TEXTOVERLAY, GstTextOverlay))
#define GST_TEXTOVERLAY_CLASS(klass)   (G_TYPE_CHECK_CLASS_CAST((klass),\
                                        GST_TYPE_ULAW, GstTextOverlay))
#define GST_TEXTOVERLAY_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj),\
                                        GST_TYPE_TEXTOVERLAY, GstTextOverlayClass))
#define GST_IS_TEXTOVERLAY(obj)        (G_TYPE_CHECK_INSTANCE_TYPE((obj),\
                                        GST_TYPE_TEXTOVERLAY))
#define GST_IS_TEXTOVERLAY_CLASS(obj)  (G_TYPE_CHECK_CLASS_TYPE((klass),\
                                        GST_TYPE_TEXTOVERLAY))

typedef struct _GstTextOverlay      GstTextOverlay;
typedef struct _GstTextOverlayClass GstTextOverlayClass;

typedef enum _GstTextOverlayVAlign GstTextOverlayVAlign;
typedef enum _GstTextOverlayHAlign GstTextOverlayHAlign;

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


struct _GstTextOverlay {
    GstElement            element;

    GstPad               *video_sinkpad;
    GstPad               *text_sinkpad;
    GstPad               *srcpad;
    gint                  width;
    gint                  height;
    PangoLayout          *layout;
    FT_Bitmap             bitmap;
    gint                  bitmap_buffer_size;
    gint                  baseline_y;
    GstTextOverlayVAlign  valign;
    GstTextOverlayHAlign  halign;
    gint                  x0;
    gint                  y0;
    GstBuffer		 *current_buffer;
    GstBuffer		 *next_buffer;
    gchar		 *default_text;
    gboolean		  need_render;
};

struct _GstTextOverlayClass {
    GstElementClass parent_class;

    PangoContext *pango_context;
};

GType gst_textoverlay_get_type(void) G_GNUC_CONST;

G_END_DECLS

#endif /* __GST_TEXTOVERLAY_H */
