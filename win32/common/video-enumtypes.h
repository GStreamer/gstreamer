


#ifndef __GST_VIDEO_ENUM_TYPES_H__
#define __GST_VIDEO_ENUM_TYPES_H__

#include <glib-object.h>

G_BEGIN_DECLS

/* enumerations from "video.h" */
GType gst_video_format_get_type (void);
#define GST_TYPE_VIDEO_FORMAT (gst_video_format_get_type())
GType gst_video_format_flags_get_type (void);
#define GST_TYPE_VIDEO_FORMAT_FLAGS (gst_video_format_flags_get_type())
GType gst_video_flags_get_type (void);
#define GST_TYPE_VIDEO_FLAGS (gst_video_flags_get_type())
GType gst_video_chroma_site_get_type (void);
#define GST_TYPE_VIDEO_CHROMA_SITE (gst_video_chroma_site_get_type())
GType gst_video_color_range_get_type (void);
#define GST_TYPE_VIDEO_COLOR_RANGE (gst_video_color_range_get_type())
GType gst_video_color_matrix_get_type (void);
#define GST_TYPE_VIDEO_COLOR_MATRIX (gst_video_color_matrix_get_type())
GType gst_video_transfer_function_get_type (void);
#define GST_TYPE_VIDEO_TRANSFER_FUNCTION (gst_video_transfer_function_get_type())
GType gst_video_color_primaries_get_type (void);
#define GST_TYPE_VIDEO_COLOR_PRIMARIES (gst_video_color_primaries_get_type())
GType gst_video_buffer_flags_get_type (void);
#define GST_TYPE_VIDEO_BUFFER_FLAGS (gst_video_buffer_flags_get_type())
G_END_DECLS

#endif /* __GST_VIDEO_ENUM_TYPES_H__ */



