
#ifndef __GST_CAIRO_TEXT_OVERLAY_H__
#define __GST_CAIRO_TEXT_OVERLAY_H__

#include <gst/gst.h>
#include <gst/base/gstcollectpads.h>

G_BEGIN_DECLS

#define GST_TYPE_CAIRO_TEXT_OVERLAY           (gst_text_overlay_get_type())
#define GST_CAIRO_TEXT_OVERLAY(obj)           (G_TYPE_CHECK_INSTANCE_CAST((obj),\
                                        GST_TYPE_CAIRO_TEXT_OVERLAY, GstCairoTextOverlay))
#define GST_CAIRO_TEXT_OVERLAY_CLASS(klass)   (G_TYPE_CHECK_CLASS_CAST((klass),\
                                        GST_TYPE_CAIRO_TEXT_OVERLAY, GstCairoTextOverlayClass))
#define GST_CAIRO_TEXT_OVERLAY_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj),\
                                        GST_TYPE_CAIRO_TEXT_OVERLAY, GstCairoTextOverlayClass))
#define GST_IS_CAIRO_TEXT_OVERLAY(obj)        (G_TYPE_CHECK_INSTANCE_TYPE((obj),\
                                        GST_TYPE_CAIRO_TEXT_OVERLAY))
#define GST_IS_CAIRO_TEXT_OVERLAY_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),\
                                        GST_TYPE_CAIRO_TEXT_OVERLAY))

typedef struct _GstCairoTextOverlay      GstCairoTextOverlay;
typedef struct _GstCairoTextOverlayClass GstCairoTextOverlayClass;

typedef enum _GstCairoTextOverlayVAlign GstCairoTextOverlayVAlign;
typedef enum _GstCairoTextOverlayHAlign GstCairoTextOverlayHAlign;

enum _GstCairoTextOverlayVAlign {
    GST_CAIRO_TEXT_OVERLAY_VALIGN_BASELINE,
    GST_CAIRO_TEXT_OVERLAY_VALIGN_BOTTOM,
    GST_CAIRO_TEXT_OVERLAY_VALIGN_TOP
};

enum _GstCairoTextOverlayHAlign {
    GST_CAIRO_TEXT_OVERLAY_HALIGN_LEFT,
    GST_CAIRO_TEXT_OVERLAY_HALIGN_CENTER,
    GST_CAIRO_TEXT_OVERLAY_HALIGN_RIGHT
};


struct _GstCairoTextOverlay {
    GstElement            element;

    GstPad               *video_sinkpad;
    GstPad               *text_sinkpad;
    GstPad               *srcpad;

    GstCollectPads       *collect;
    GstCollectData       *video_collect_data;
    GstCollectData       *text_collect_data;
    GstPadEventFunction   collect_event;

    gint                  width;
    gint                  height;
    gint                  fps_n;
    gint                  fps_d;

    GstCairoTextOverlayVAlign  valign;
    GstCairoTextOverlayHAlign  halign;
    gint                  xpad;
    gint                  ypad;
    gint                  deltax;
    gint                  deltay;
    gchar                *default_text;
    gboolean              want_shading;

    guchar               *text_fill_image;
    guchar               *text_outline_image;
    gint                  font_height;
    gint                  text_x0, text_x1; /* start/end x position of text */
    gint                  text_dy;

    gboolean              need_render;

    gchar                *font;
    gint                  slant;
    gint                  weight;
    gdouble               scale;
};

struct _GstCairoTextOverlayClass {
  GstElementClass parent_class;
};

GType gst_text_overlay_get_type (void);

G_END_DECLS

#endif /* __GST_CAIRO_TEXT_OVERLAY_H */
