% ClassName
GstAudioSrc
% TYPE_CLASS_NAME
GST_TYPE_AUDIO_SRC
% pkg-config
gstreamer-audio-0.10
% includes
#include <gst/audio/gstaudiosrc.h>
% prototypes
static gboolean gst_replace_open (GstAudioSink * sink);
static gboolean
gst_replace_prepare (GstAudioSink * sink, GstRingBufferSpec * spec);
static gboolean gst_replace_unprepare (GstAudioSink * sink);
static gboolean gst_replace_close (GstAudioSink * sink);
static guint
gst_replace_write (GstAudioSink * sink, gpointer data, guint length);
static guint gst_replace_delay (GstAudioSink * sink);
static void gst_replace_reset (GstAudioSink * sink);
% declare-class
  GstAudioSrc *audio_src_class = GST_AUDIO_SRC (klass);
% set-methods
  audio_src_class-> = GST_DEBUG_FUNCPTR (gst_replace_);
% methods

static gboolean
gst_replace_open (GstAudioSink * sink)
{
}

static gboolean
gst_replace_prepare (GstAudioSink * sink, GstRingBufferSpec * spec)
{
}

static gboolean
gst_replace_unprepare (GstAudioSink * sink)
{
}

static gboolean
gst_replace_close (GstAudioSink * sink)
{
}

static guint
gst_replace_write (GstAudioSink * sink, gpointer data, guint length)
{
}

static guint
gst_replace_delay (GstAudioSink * sink)
{
}

static void
gst_replace_reset (GstAudioSink * sink)
{
}
% end
