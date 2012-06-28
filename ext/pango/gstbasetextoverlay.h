#ifndef __GST_BASE_TEXT_OVERLAY_H__
#define __GST_BASE_TEXT_OVERLAY_H__

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/video-overlay-composition.h>
#include <pango/pangocairo.h>

G_BEGIN_DECLS

#define GST_TYPE_BASE_TEXT_OVERLAY            (gst_base_text_overlay_get_type())
#define GST_BASE_TEXT_OVERLAY(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),\
                                         GST_TYPE_BASE_TEXT_OVERLAY, GstBaseTextOverlay))
#define GST_BASE_TEXT_OVERLAY_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass),\
                                         GST_TYPE_BASE_TEXT_OVERLAY,GstBaseTextOverlayClass))
#define GST_BASE_TEXT_OVERLAY_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),\
                                         GST_TYPE_BASE_TEXT_OVERLAY, GstBaseTextOverlayClass))
#define GST_IS_BASE_TEXT_OVERLAY(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),\
                                         GST_TYPE_BASE_TEXT_OVERLAY))
#define GST_IS_BASE_TEXT_OVERLAY_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),\
                                         GST_TYPE_BASE_TEXT_OVERLAY))

typedef struct _GstBaseTextOverlay      GstBaseTextOverlay;
typedef struct _GstBaseTextOverlayClass GstBaseTextOverlayClass;

/**
 * GstBaseTextOverlayVAlign:
 * @GST_BASE_TEXT_OVERLAY_VALIGN_BASELINE: draw text on the baseline
 * @GST_BASE_TEXT_OVERLAY_VALIGN_BOTTOM: draw text on the bottom
 * @GST_BASE_TEXT_OVERLAY_VALIGN_TOP: draw text on top
 * @GST_BASE_TEXT_OVERLAY_VALIGN_POS: draw text according to the #GstBaseTextOverlay:ypos property
 * @GST_BASE_TEXT_OVERLAY_VALIGN_CENTER: draw text vertically centered
 *
 * Vertical alignment of the text.
 */
typedef enum {
    GST_BASE_TEXT_OVERLAY_VALIGN_BASELINE,
    GST_BASE_TEXT_OVERLAY_VALIGN_BOTTOM,
    GST_BASE_TEXT_OVERLAY_VALIGN_TOP,
    GST_BASE_TEXT_OVERLAY_VALIGN_POS,
    GST_BASE_TEXT_OVERLAY_VALIGN_CENTER
} GstBaseTextOverlayVAlign;

/**
 * GstBaseTextOverlayHAlign:
 * @GST_BASE_TEXT_OVERLAY_HALIGN_LEFT: align text left
 * @GST_BASE_TEXT_OVERLAY_HALIGN_CENTER: align text center
 * @GST_BASE_TEXT_OVERLAY_HALIGN_RIGHT: align text right
 * @GST_BASE_TEXT_OVERLAY_HALIGN_POS: position text according to the #GstBaseTextOverlay:xpos property
 *
 * Horizontal alignment of the text.
 */
/* FIXME 0.11: remove GST_BASE_TEXT_OVERLAY_HALIGN_UNUSED */
typedef enum {
    GST_BASE_TEXT_OVERLAY_HALIGN_LEFT,
    GST_BASE_TEXT_OVERLAY_HALIGN_CENTER,
    GST_BASE_TEXT_OVERLAY_HALIGN_RIGHT,
    GST_BASE_TEXT_OVERLAY_HALIGN_UNUSED,
    GST_BASE_TEXT_OVERLAY_HALIGN_POS
} GstBaseTextOverlayHAlign;

/**
 * GstBaseTextOverlayWrapMode:
 * @GST_BASE_TEXT_OVERLAY_WRAP_MODE_NONE: no wrapping
 * @GST_BASE_TEXT_OVERLAY_WRAP_MODE_WORD: do word wrapping
 * @GST_BASE_TEXT_OVERLAY_WRAP_MODE_CHAR: do char wrapping
 * @GST_BASE_TEXT_OVERLAY_WRAP_MODE_WORD_CHAR: do word and char wrapping
 *
 * Whether to wrap the text and if so how.
 */
typedef enum {
    GST_BASE_TEXT_OVERLAY_WRAP_MODE_NONE = -1,
    GST_BASE_TEXT_OVERLAY_WRAP_MODE_WORD = PANGO_WRAP_WORD,
    GST_BASE_TEXT_OVERLAY_WRAP_MODE_CHAR = PANGO_WRAP_CHAR,
    GST_BASE_TEXT_OVERLAY_WRAP_MODE_WORD_CHAR = PANGO_WRAP_WORD_CHAR
} GstBaseTextOverlayWrapMode;

/**
 * GstBaseTextOverlayLineAlign:
 * @GST_BASE_TEXT_OVERLAY_LINE_ALIGN_LEFT: lines are left-aligned
 * @GST_BASE_TEXT_OVERLAY_LINE_ALIGN_CENTER: lines are center-aligned
 * @GST_BASE_TEXT_OVERLAY_LINE_ALIGN_RIGHT: lines are right-aligned
 *
 * Alignment of text lines relative to each other
 */
typedef enum {
    GST_BASE_TEXT_OVERLAY_LINE_ALIGN_LEFT = PANGO_ALIGN_LEFT,
    GST_BASE_TEXT_OVERLAY_LINE_ALIGN_CENTER = PANGO_ALIGN_CENTER,
    GST_BASE_TEXT_OVERLAY_LINE_ALIGN_RIGHT = PANGO_ALIGN_RIGHT
} GstBaseTextOverlayLineAlign;

/**
 * GstBaseTextOverlay:
 *
 * Opaque textoverlay object structure
 */
struct _GstBaseTextOverlay {
    GstElement               element;

    GstPad                  *video_sinkpad;
    GstPad                  *text_sinkpad;
    GstPad                  *srcpad;

    GstSegment               segment;
    GstSegment               text_segment;
    GstBuffer               *text_buffer;
    gboolean                text_linked;
    gboolean                video_flushing;
    gboolean                video_eos;
    gboolean                text_flushing;
    gboolean                text_eos;

    GMutex                   lock;
    GCond                    cond;  /* to signal removal of a queued text
                                     * buffer, arrival of a text buffer,
                                     * a text segment update, or a change
                                     * in status (e.g. shutdown, flushing) */

    GstVideoInfo             info;
    GstVideoFormat           format;
    gint                     width;
    gint                     height;

    GstBaseTextOverlayVAlign     valign;
    GstBaseTextOverlayHAlign     halign;
    GstBaseTextOverlayWrapMode   wrap_mode;
    GstBaseTextOverlayLineAlign  line_align;

    gint                     xpad;
    gint                     ypad;
    gint                     deltax;
    gint                     deltay;
    gdouble                  xpos;
    gdouble                  ypos;
    gchar                   *default_text;
    gboolean                 want_shading;
    gboolean                 silent;
    gboolean                 wait_text;
    guint                    color, outline_color;

    PangoLayout             *layout;
    gdouble                  shadow_offset;
    gdouble                  outline_offset;
    GstBuffer               *text_image;
    gint                     image_width;
    gint                     image_height;
    gint                     baseline_y;

    gboolean                 auto_adjust_size;
    gboolean                 need_render;

    gint                     shading_value;  /* for timeoverlay subclass */

    gboolean                 have_pango_markup;
    gboolean                 use_vertical_render;

    gboolean                 attach_compo_to_buffer;

    GstVideoOverlayComposition *composition;
};

struct _GstBaseTextOverlayClass {
    GstElementClass parent_class;

    PangoContext *pango_context;
    GMutex       *pango_lock;

    gchar *     (*get_text) (GstBaseTextOverlay *overlay, GstBuffer *video_frame);
};

GType gst_base_text_overlay_get_type(void) G_GNUC_CONST;

G_END_DECLS

#endif /* __GST_BASE_TEXT_OVERLAY_H */
