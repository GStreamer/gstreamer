% ClassName
GstBaseAudioSink
% TYPE_CLASS_NAME
GST_TYPE_BASE_AUDIO_SINK
% pkg-config
gstreamer-audio-0.10
% includes
#include <gst/audio/gstbaseaudiosink.h>
% prototypes
static GstRingBuffer *gst_replace_create_ringbuffer (GstBaseAudioSink * sink);
% declare-class
  GstBaseAudioSink *base_audio_sink_class = GST_BASE_AUDIO_SINK (klass);
% set-methods
  base_audio_sink_class-> = GST_DEBUG_FUNCPTR (gst_replace_);
% methods

static GstRingBuffer *
gst_replace_create_ringbuffer (GstBaseAudioSink * sink)
{

}
% end
