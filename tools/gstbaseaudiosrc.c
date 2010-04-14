% ClassName
GstBaseAudioSrc
% TYPE_CLASS_NAME
GST_TYPE_BASE_AUDIO_SRC
% pkg-config
gstreamer-audio-0.10
% includes
#include <gst/audio/gstbaseaudiosrc.h>
% prototypes
static GstRingBuffer *gst_replace_create_ringbuffer (GstBaseAudioSrc * src);
% declare-class
  GstBaseAudioSrc *base_audio_src_class = GST_BASE_AUDIO_SRC (klass);
% set-methods
  base_audio_src_class-> = GST_DEBUG_FUNCPTR (gst_replace_);
% methods

static GstRingBuffer *
gst_replace_create_ringbuffer (GstBaseAudioSrc * src)
{

}
% end
