#pragma once

#include <wpe/webkit.h>
#include <gst/gl/gl.h>
#include "gstwpeview.h"

typedef struct _GstWpeVideoSrc2 GstWpeVideoSrc2;

G_BEGIN_DECLS


#define GST_TYPE_WPE_VIDEO_SRC (gst_wpe_video_src_get_type ())
G_DECLARE_FINAL_TYPE (GstWpeVideoSrc2, gst_wpe_video_src, GST, WPE_VIDEO_SRC, GstGLBaseSrc);

void gst_wpe_video_src_configure_web_view (GstWpeVideoSrc2 * src, WebKitWebView * webview);

G_END_DECLS
