% ClassName
GstAudioSink
% TYPE_CLASS_NAME
GST_TYPE_AUDIO_SINK
% pkg-config
gstreamer-audio-0.10
% includes
#include <gst/audio/gstaudiosink.h>
% prototypes
static gboolean gst_replace_open (GstAudioSrc * src);
static gboolean
gst_replace_prepare (GstAudioSrc * src, GstRingBufferSpec * spec);
static gboolean gst_replace_unprepare (GstAudioSrc * src);
static gboolean gst_replace_close (GstAudioSrc * src);
static guint gst_replace_read (GstAudioSrc * src, gpointer data, guint length);
static guint gst_replace_delay (GstAudioSrc * src);
static void gst_replace_reset (GstAudioSrc * src);
% declare-class
  GstAudioSink *audio_sink_class = GST_AUDIO_SINK (klass);
% set-methods
  audio_sink_class-> = GST_DEBUG_FUNCPTR (gst_replace_);
% methods

static gboolean
gst_replace_open (GstAudioSrc * src)
{
}

static gboolean
gst_replace_prepare (GstAudioSrc * src, GstRingBufferSpec * spec)
{
}

static gboolean
gst_replace_unprepare (GstAudioSrc * src)
{
}

static gboolean
gst_replace_close (GstAudioSrc * src)
{
}

static guint
gst_replace_read (GstAudioSrc * src, gpointer data, guint length)
{
}

static guint
gst_replace_delay (GstAudioSrc * src)
{
}

static void
gst_replace_reset (GstAudioSrc * src)
{
}
% end
