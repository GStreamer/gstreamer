#ifndef __GST_TEXT_OVERLAY_H__
#define __GST_TEXT_OVERLAY_H__

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/video-overlay-composition.h>
#include <gst/controller/gstcontroller.h>
#include <pango/pangocairo.h>

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

/**
 * GstTextOverlayVAlign:
 * @GST_TEXT_OVERLAY_VALIGN_BASELINE: draw text on the baseline
 * @GST_TEXT_OVERLAY_VALIGN_BOTTOM: draw text on the bottom
 * @GST_TEXT_OVERLAY_VALIGN_TOP: draw text on top
 * @GST_TEXT_OVERLAY_VALIGN_POS: draw text according to the #GstTextOverlay:ypos property
 * @GST_TEXT_OVERLAY_VALIGN_CENTER: draw text vertically centered
 *
 * Vertical alignment of the text.
 */
typedef enum {
    GST_TEXT_OVERLAY_VALIGN_BASELINE,
    GST_TEXT_OVERLAY_VALIGN_BOTTOM,
    GST_TEXT_OVERLAY_VALIGN_TOP,
    GST_TEXT_OVERLAY_VALIGN_POS,
    GST_TEXT_OVERLAY_VALIGN_CENTER
} GstTextOverlayVAlign;

/**
 * GstTextOverlayHAlign:
 * @GST_TEXT_OVERLAY_HALIGN_LEFT: align text left
 * @GST_TEXT_OVERLAY_HALIGN_CENTER: align text center
 * @GST_TEXT_OVERLAY_HALIGN_RIGHT: align text right
 * @GST_TEXT_OVERLAY_HALIGN_POS: position text according to the #GstTextOverlay:xpos property
 *
 * Horizontal alignment of the text.
 */
/* FIXME 0.11: remove GST_TEXT_OVERLAY_HALIGN_UNUSED */
typedef enum {
    GST_TEXT_OVERLAY_HALIGN_LEFT,
    GST_TEXT_OVERLAY_HALIGN_CENTER,
    GST_TEXT_OVERLAY_HALIGN_RIGHT,
    GST_TEXT_OVERLAY_HALIGN_UNUSED,
    GST_TEXT_OVERLAY_HALIGN_POS
} GstTextOverlayHAlign;

/**
 * GstTextOverlayWrapMode:
 * @GST_TEXT_OVERLAY_WRAP_MODE_NONE: no wrapping
 * @GST_TEXT_OVERLAY_WRAP_MODE_WORD: do word wrapping
 * @GST_TEXT_OVERLAY_WRAP_MODE_CHAR: do char wrapping
 * @GST_TEXT_OVERLAY_WRAP_MODE_WORD_CHAR: do word and char wrapping
 *
 * Whether to wrap the text and if so how.
 */
typedef enum {
    GST_TEXT_OVERLAY_WRAP_MODE_NONE = -1,
    GST_TEXT_OVERLAY_WRAP_MODE_WORD = PANGO_WRAP_WORD,
    GST_TEXT_OVERLAY_WRAP_MODE_CHAR = PANGO_WRAP_CHAR,
    GST_TEXT_OVERLAY_WRAP_MODE_WORD_CHAR = PANGO_WRAP_WORD_CHAR
} GstTextOverlayWrapMode;

/**
 * GstTextOverlayLineAlign:
 * @GST_TEXT_OVERLAY_LINE_ALIGN_LEFT: lines are left-aligned
 * @GST_TEXT_OVERLAY_LINE_ALIGN_CENTER: lines are center-aligned
 * @GST_TEXT_OVERLAY_LINE_ALIGN_RIGHT: lines are right-aligned
 *
 * Alignment of text lines relative to each other
 */
typedef enum {
    GST_TEXT_OVERLAY_LINE_ALIGN_LEFT = PANGO_ALIGN_LEFT,
    GST_TEXT_OVERLAY_LINE_ALIGN_CENTER = PANGO_ALIGN_CENTER,
    GST_TEXT_OVERLAY_LINE_ALIGN_RIGHT = PANGO_ALIGN_RIGHT
} GstTextOverlayLineAlign;

/**
 * GstTextOverlay:
 *
 * Opaque textoverlay object structure
 */
struct _GstTextOverlay {
    GstElement                  element;

    GstPad                     *video_sinkpad;
    GstPad                     *text_sinkpad;
    GstPad                     *srcpad;

    GstSegment                  segment;
    GstSegment                  text_segment;
    GstBuffer                  *text_buffer;
    gboolean                    text_linked;
    gboolean                    video_flushing;
    gboolean                    video_eos;
    gboolean                    text_flushing;
    gboolean                    text_eos;

    GCond                      *cond;  /* to signal removal of a queued text
                                     * buffer, arrival of a text buffer,
                                     * a text segment update, or a change
                                     * in status (e.g. shutdown, flushing) */

    gint                        width;
    gint                        height;
    gint                        fps_n;
    gint                        fps_d;
    GstVideoFormat              format;

    GstTextOverlayVAlign        valign;
    GstTextOverlayHAlign        halign;
    GstTextOverlayWrapMode      wrap_mode;
    GstTextOverlayLineAlign     line_align;

    gint                        xpad;
    gint                        ypad;
    gint                        deltax;
    gint                        deltay;
    gdouble                     xpos;
    gdouble                     ypos;
    gchar                      *default_text;
    gboolean                    want_shading;
    gboolean                    silent;
    gboolean                    wait_text;
    guint                       color, outline_color;

    PangoLayout                *layout;
    gdouble                     shadow_offset;
    gboolean                    want_shadow;
    gdouble                     outline_offset;
    GstBuffer                  *text_image;
    gint                        image_width;
    gint                        image_height;
    gint                        baseline_y;

    gboolean                    auto_adjust_size;
    gboolean                    need_render;

    gint                        shading_value;  /* for timeoverlay subclass */

    gboolean                    have_pango_markup;
    gboolean                    use_vertical_render;

    gboolean                    attach_compo_to_buffer;

    GstVideoOverlayComposition *composition;
};

struct _GstTextOverlayClass {
    GstElementClass parent_class;

    PangoContext *pango_context;
    GMutex       *pango_lock;

    gchar *     (*get_text) (GstTextOverlay *overlay, GstBuffer *video_frame);
};

GType gst_text_overlay_get_type(void) G_GNUC_CONST;

G_END_DECLS

#endif /* __GST_TEXT_OVERLAY_H */
