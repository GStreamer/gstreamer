#ifndef __GST_TEXT_RENDER_H__
#define __GST_TEXT_RENDER_H__

#include <gst/gst.h>
#include <pango/pangoft2.h>

G_BEGIN_DECLS

#define GST_TYPE_TEXT_RENDER            (gst_text_render_get_type())
#define GST_TEXT_RENDER(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),\
                                        GST_TYPE_TEXT_RENDER, GstTextRender))
#define GST_TEXT_RENDER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass),\
                                        GST_TYPE_TEXT_RENDER, GstTextRenderClass))
#define GST_TEXT_RENDER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),\
                                        GST_TYPE_TEXT_RENDER, GstTextRenderClass))
#define GST_IS_TEXT_RENDER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),\
                                        GST_TYPE_TEXT_RENDER))
#define GST_IS_TEXT_RENDER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),\
                                        GST_TYPE_TEXT_RENDER))

typedef struct _GstTextRender      GstTextRender;
typedef struct _GstTextRenderClass GstTextRenderClass;

/**
 * GstTextRender:
 *
 * Opaque textrender data structure.
 */
struct _GstTextRender {
    GstElement            element;

    GstPad               *sinkpad, *srcpad;
    gint                  width;
    gint                  height;
    PangoLayout          *layout;
    FT_Bitmap             bitmap;
    gint                  bitmap_buffer_size;
    gint                  baseline_y;
};

struct _GstTextRenderClass {
    GstElementClass parent_class;

    PangoContext *pango_context;
};

GType gst_text_render_get_type(void) G_GNUC_CONST;

G_END_DECLS

#endif /* __GST_TEXT_RENDER_H */
