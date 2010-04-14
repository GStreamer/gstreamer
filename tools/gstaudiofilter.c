% ClassName
GstAudioFilter
% TYPE_CLASS_NAME
GST_TYPE_AUDIO_FILTER
% pkg-config
gstreamer-audio-0.10
% includes
#include <gst/audio/gstaudiofilter.h>
% prototypes
static gboolean
gst_replace_setup (GstAudioFilter * filter, GstRingBufferSpec * format);
% declare-class
  GstAudioFilter *audio_filter_class = GST_AUDIO_FILTER (klass);
% set-methods
  audio_filter_class-> = GST_DEBUG_FUNCPTR (gst_replace_);
% methods

static gboolean
gst_replace_setup (GstAudioFilter * filter, GstRingBufferSpec * format)
{

}
% end
