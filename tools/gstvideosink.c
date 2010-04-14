% ClassName
GstVideoSink
% TYPE_CLASS_NAME
GST_TYPE_VIDEO_SINK
% pkg-config
gstreamer-video-0.10
% includes
#include <gst/video/gstvideosink.h>
% prototypes

static GstFlowReturn
gst_replace_show_frame (GstVideoSink * video_sink, GstBuffer * buf);


% declare-class
  GstVideoSinkClass *video_sink_class = GST_VIDEO_SINK_CLASS (klass);
% set-methods
  video_sink_class->show_frame = GST_DEBUG_FUNCPTR (gst_replace_show_frame);
% methods

static GstFlowReturn
gst_replace_show_frame (GstVideoSink * video_sink, GstBuffer * buf)
{

  return GST_FLOW_OK;
}

% end

