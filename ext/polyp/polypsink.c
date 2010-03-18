/*
 * This sink plugin works, but has some room for improvements:
 *
 *  - Export polypaudio's stream clock through gstreamer's API
 *  - Add support for querying latency information
 *  - Add a source for polypaudio
 *
 *  Lennart Poettering, 2004
 */

#include <pthread.h>
#include <string.h>
#include <stdio.h>

#include <polyp/polyplib-error.h>
#include <polyp/mainloop.h>

#include "polypsink.h"

GST_DEBUG_CATEGORY_EXTERN (polyp_debug);
#define GST_CAT_DEFAULT polyp_debug

enum
{
  ARG_0,
  ARG_SERVER,
  ARG_SINK,
};

static GstElementClass *parent_class = NULL;

static void create_stream (GstPolypSink * polypsink);
static void destroy_stream (GstPolypSink * polypsink);

static void create_context (GstPolypSink * polypsink);
static void destroy_context (GstPolypSink * polypsink);

static void
gst_polypsink_base_init (gpointer g_class)
{

  static GstStaticPadTemplate pad_template = GST_STATIC_PAD_TEMPLATE ("sink",
      GST_PAD_SINK,
      GST_PAD_ALWAYS,
      GST_STATIC_CAPS ("audio/x-raw-int, "
          "endianness = (int) { LITTLE_ENDIAN , BIG_ENDIAN }, "
          "signed = (boolean) TRUE, "
          "width = (int) 16, "
          "depth = (int) 16, "
          "rate = (int) [ 1, MAX ], " "channels = (int) [ 1, 16 ]"
#if 0
          ";audio/x-raw-float, "
          "endianness = (int) { LITTLE_ENDIAN, BIG_ENDIAN }, "
          "width = (int) 32, "
          "rate = (int) [ 1, MAX ], "
          "channels = (int) [ 1, 16 ];"
          "audio/x-raw-int, "
          "signed = (boolean) FALSE, "
          "width = (int) 8, "
          "depth = (int) 8, "
          "rate = (int) [ 1, MAX ], " "channels = (int) [ 1, 16 ]"
#endif
      )
      );

  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&pad_template));
  gst_element_class_set_details_simple (element_class, "Polypaudio audio sink",
      "Sink/Audio", "Plays audio to a Polypaudio server", "Lennart Poettering");
}

static void
gst_polypsink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstPolypSink *polypsink;

  g_return_if_fail (GST_IS_POLYPSINK (object));
  polypsink = GST_POLYPSINK (object);

  switch (prop_id) {
    case ARG_SERVER:
      g_free (polypsink->server);
      polypsink->server = g_strdup (g_value_get_string (value));
      break;

    case ARG_SINK:
      g_free (polypsink->sink);
      polypsink->sink = g_strdup (g_value_get_string (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_polypsink_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstPolypSink *polypsink;

  g_return_if_fail (GST_IS_POLYPSINK (object));
  polypsink = GST_POLYPSINK (object);

  switch (prop_id) {
    case ARG_SERVER:
      g_value_set_string (value, polypsink->server);
      break;

    case ARG_SINK:
      g_value_set_string (value, polypsink->sink);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstStateChangeReturn
gst_polypsink_change_state (GstElement * element, GstStateChange transition)
{
  GstPolypSink *polypsink;

  polypsink = GST_POLYPSINK (element);

  switch (transition) {

    case GST_STATE_CHANGE_NULL_TO_READY:
      create_context (polypsink);
      break;

    case GST_STATE_CHANGE_READY_TO_NULL:
      destroy_context (polypsink);
      break;

    case GST_STATE_CHANGE_READY_TO_PAUSED:

      create_stream (polypsink);

      if (polypsink->stream
          && pa_stream_get_state (polypsink->stream) == PA_STREAM_READY)
        pa_operation_unref (pa_stream_cork (polypsink->stream, 1, NULL, NULL));
      break;

    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:

      if (polypsink->stream
          && pa_stream_get_state (polypsink->stream) == PA_STREAM_READY)
        pa_operation_unref (pa_stream_cork (polypsink->stream, 1, NULL, NULL));

      break;

    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:

      create_stream (polypsink);

      if (polypsink->stream
          && pa_stream_get_state (polypsink->stream) == PA_STREAM_READY)
        pa_operation_unref (pa_stream_cork (polypsink->stream, 0, NULL, NULL));

      break;

    case GST_STATE_CHANGE_PAUSED_TO_READY:

      destroy_stream (polypsink);
      break;
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  return GST_STATE_CHANGE_SUCCESS;
}


static void
do_write (GstPolypSink * polypsink, size_t length)
{
  size_t l;

  if (!polypsink->buffer)
    return;

  g_assert (polypsink->buffer_index < GST_BUFFER_SIZE (polypsink->buffer));
  l = GST_BUFFER_SIZE (polypsink->buffer) - polypsink->buffer_index;

  if (l > length)
    l = length;

  pa_stream_write (polypsink->stream,
      GST_BUFFER_DATA (polypsink->buffer) + polypsink->buffer_index, l, NULL,
      0);
  polypsink->buffer_index += l;

  if (polypsink->buffer_index >= GST_BUFFER_SIZE (polypsink->buffer)) {
    gst_buffer_unref (polypsink->buffer);
    polypsink->buffer = NULL;
    polypsink->buffer_index = 0;
  }
}

static void
stream_write_callback (struct pa_stream *s, size_t length, void *userdata)
{
  GstPolypSink *polypsink = userdata;

  g_assert (s && length && polypsink);

  do_write (polypsink, length);
}

static void
stream_state_callback (struct pa_stream *s, void *userdata)
{
  GstPolypSink *polypsink = userdata;

  g_assert (s && polypsink);

  switch (pa_stream_get_state (s)) {
    case PA_STREAM_DISCONNECTED:
    case PA_STREAM_CREATING:
      break;

    case PA_STREAM_READY:
      break;

    case PA_STREAM_FAILED:
      GST_ELEMENT_ERROR (GST_ELEMENT (polypsink), RESOURCE, BUSY,
          ("Stream creation failed: %s",
              pa_strerror (pa_context_errno (pa_stream_get_context (s)))),
          (NULL));

      /* Pass over */
    case PA_STREAM_TERMINATED:
    default:
      polypsink->mainloop_api->quit (polypsink->mainloop_api, 1);
      destroy_context (polypsink);
      break;
  }
}

static void
context_state_callback (struct pa_context *c, void *userdata)
{
  GstPolypSink *polypsink = userdata;

  g_assert (c && polypsink);

  switch (pa_context_get_state (c)) {
    case PA_CONTEXT_UNCONNECTED:
    case PA_CONTEXT_CONNECTING:
    case PA_CONTEXT_AUTHORIZING:
    case PA_CONTEXT_SETTING_NAME:
      break;

    case PA_CONTEXT_READY:{
      GstState state;

      g_assert (!polypsink->stream);

      state = gst_element_get_state (GST_ELEMENT (polypsink));
      if (state == GST_STATE_PAUSED || state == GST_STATE_PLAYING)
        create_stream (polypsink);

      break;
    }

    case PA_CONTEXT_FAILED:
      GST_ELEMENT_ERROR (GST_ELEMENT (polypsink), RESOURCE, BUSY,
          ("Connection failed: %s", pa_strerror (pa_context_errno (c))),
          (NULL));

      /* Pass over */
    case PA_CONTEXT_TERMINATED:
    default:
      polypsink->mainloop_api->quit (polypsink->mainloop_api, 1);
      destroy_context (polypsink);
      break;
  }
}

static void
create_stream (GstPolypSink * polypsink)
{
  char t[256];

  g_assert (polypsink);

  if (polypsink->stream)
    return;

  if (!polypsink->context) {
    create_context (polypsink);
    return;
  }

  if (!polypsink->negotiated)
    return;

  if (pa_context_get_state (polypsink->context) != PA_CONTEXT_READY)
    return;

  pa_sample_spec_snprint (t, sizeof (t), &polypsink->sample_spec);

  polypsink->stream =
      pa_stream_new (polypsink->context, "gstreamer output",
      &polypsink->sample_spec);
  g_assert (polypsink->stream);

  pa_stream_set_state_callback (polypsink->stream, stream_state_callback,
      polypsink);
  pa_stream_set_write_callback (polypsink->stream, stream_write_callback,
      polypsink);
  pa_stream_connect_playback (polypsink->stream, NULL, NULL,
      PA_STREAM_INTERPOLATE_LATENCY, PA_VOLUME_NORM);
}

static void
create_context (GstPolypSink * polypsink)
{
  g_assert (polypsink);

  if (polypsink->context)
    return;

  polypsink->context = pa_context_new (polypsink->mainloop_api, "gstreamer");
  g_assert (polypsink->context);

  pa_context_set_state_callback (polypsink->context, context_state_callback,
      polypsink);
  if (polypsink->server && polypsink->server[0]) {
    pa_context_connect (polypsink->context, polypsink->server, 1, NULL);
  } else {
    pa_context_connect (polypsink->context, NULL, 1, NULL);
  }
}

static void
destroy_stream (GstPolypSink * polypsink)
{
  g_assert (polypsink);

  if (polypsink->stream) {
    struct pa_stream *s = polypsink->stream;

    polypsink->stream = NULL;
    pa_stream_set_state_callback (s, NULL, NULL);
    pa_stream_set_write_callback (s, NULL, NULL);
    pa_stream_unref (s);
  }
}

static void
destroy_context (GstPolypSink * polypsink)
{
  destroy_stream (polypsink);

  if (polypsink->context) {
    struct pa_context *c = polypsink->context;

    polypsink->context = NULL;
    pa_context_set_state_callback (c, NULL, NULL);
    pa_context_unref (c);
  }
}

static void
gst_polypsink_chain (GstPad * pad, GstData * data)
{
  GstPolypSink *polypsink = GST_POLYPSINK (gst_pad_get_parent (pad));

  g_assert (!polypsink->buffer);

  if (GST_IS_EVENT (data)) {
    GstEvent *event = GST_EVENT (data);

    switch (GST_EVENT_TYPE (event)) {
      case GST_EVENT_EOS:
        if (polypsink->stream) {
          struct pa_operation *o;

          pa_operation_unref (pa_stream_cork (polypsink->stream, 0, NULL,
                  NULL));
          o = pa_stream_drain (polypsink->stream, NULL, NULL);

          /* drain now */
          while (pa_operation_get_state (o) == PA_OPERATION_RUNNING) {
            if (pa_mainloop_iterate (polypsink->mainloop, 1, NULL) < 0)
              return;
          }

          pa_operation_unref (o);
        }

        break;
      case GST_EVENT_FLUSH:
        if (polypsink->stream)
          pa_operation_unref (pa_stream_flush (polypsink->stream, NULL, NULL));
        break;

      default:
        break;
    }

    gst_pad_event_default (polypsink->sinkpad, event);
  } else {
    size_t l;

    polypsink->buffer = GST_BUFFER (data);
    polypsink->buffer_index = 0;
    polypsink->counter += GST_BUFFER_SIZE (polypsink->buffer);

    if (polypsink->stream
        && (l = pa_stream_writable_size (polypsink->stream)) > 0)
      do_write (polypsink, l);
  }

  while (polypsink->context && (pa_context_is_pending (polypsink->context)
          || polypsink->buffer)) {
    if (pa_mainloop_iterate (polypsink->mainloop, 1, NULL) < 0)
      return;
  }

}

#if 0
static void
stream_get_latency_callback (struct pa_stream *s,
    const struct pa_latency_info *i, void *userdata)
{
  GstPolypSink *polypsink = (GstPolypSink *) userdata;

  polypsink->latency = i->buffer_usec + i->sink_usec;
}

static GstClockTime
gst_polypsink_get_time (GstClock * clock, gpointer data)
{
  struct pa_operation *o;
  GstPolypSink *polypsink = GST_POLYPSINK (data);
  GstClockTime r, l;

  if (!polypsink->stream
      || pa_stream_get_state (polypsink->stream) != PA_STREAM_READY)
    return 0;

  polypsink->latency = 0;

  o = pa_stream_get_latency (polypsink_ > stream, latency_func, polypsink);
  g_assert (o);

  while (pa_operation_get_state (o) == PA_OPERATION_RUNNING) {
    if (pa_mainloop_iterate (polypsink->mainloop, 1, NULL) < 0)
      return;
  }

  r = ((GstClockTime) polypsink->counter /
      pa_frame_size (&polypsink->sample_spec)) * GST_SECOND /
      polypsink->sample_spec.rate;
  l = polypsink->latency * GST_USECOND;

  return r > l ? r - l : 0;
}

static GstClock *
gst_polypsink_get_clock (GstElement * element)
{
  GstPolypSink *polypsink = GST_POLYPSINK (element);

  return GST_CLOCK (polypsink->provided_clock);
}

static void
gst_polypsink_set_clock (GstElement * element, GstClock * clock)
{
  GstPolypSink *polypsink = GST_POLYPSINK (element);

  polypsink->clock = clock;
}

#endif

static GstPadLinkReturn
gst_polypsink_link (GstPad * pad, const GstCaps * caps)
{
  int depth = 16, endianness = 1234;
  gboolean sign = TRUE;
  GstPolypSink *polypsink;
  GstStructure *structure;
  const char *n;
  char t[256];
  GstState state;
  int n_channels;

  polypsink = GST_POLYPSINK (gst_pad_get_parent (pad));

  structure = gst_caps_get_structure (caps, 0);

  if (!(gst_structure_get_int (structure, "depth", &depth)))
    gst_structure_get_int (structure, "width", &depth);

  gst_structure_get_int (structure, "endianness", &endianness);
  gst_structure_get_boolean (structure, "signed", &sign);

  n = gst_structure_get_name (structure);

  if (depth == 16 && endianness == 1234 && sign
      && !strcmp (n, "audio/x-raw-int"))
    polypsink->sample_spec.format = PA_SAMPLE_S16LE;
  else if (depth == 16 && endianness == 4321 && sign
      && !strcmp (n, "audio/x-raw-int"))
    polypsink->sample_spec.format = PA_SAMPLE_S16BE;
  else if (depth == 8 && !sign && !strcmp (n, "audio/x-raw-int"))
    polypsink->sample_spec.format = PA_SAMPLE_U8;
  else if (depth == 32 && endianness == 1234
      && !strcmp (n, "audio/x-raw-float"))
    polypsink->sample_spec.format = PA_SAMPLE_FLOAT32LE;
  else if (depth == 32 && endianness == 4321
      && !strcmp (n, "audio/x-raw-float"))
    polypsink->sample_spec.format = PA_SAMPLE_FLOAT32LE;
  else {
    GST_DEBUG ("unrecognized format, refusing link");
    return GST_PAD_LINK_REFUSED;
  }

  GST_DEBUG ("using format %d", polypsink->sample_spec.format);

  polypsink->sample_spec.rate = 44100;
  polypsink->sample_spec.channels = 2;

  pa_sample_spec_snprint (t, sizeof (t), &polypsink->sample_spec);

  gst_structure_get_int (structure, "channels", &n_channels);
  polypsink->sample_spec.channels = n_channels;
  gst_structure_get_int (structure, "rate", &polypsink->sample_spec.rate);

  pa_sample_spec_snprint (t, sizeof (t), &polypsink->sample_spec);
  GST_DEBUG ("using format %s", t);

  if (!pa_sample_spec_valid (&polypsink->sample_spec)) {
    GST_DEBUG ("invalid format, refusing link");
    return GST_PAD_LINK_REFUSED;
  }

  polypsink->negotiated = 1;

  destroy_stream (polypsink);

  state = gst_element_get_state (GST_ELEMENT (polypsink));
  if (state == GST_STATE_PAUSED || state == GST_STATE_PLAYING)
    create_stream (polypsink);

  return GST_PAD_LINK_OK;
}

static GstCaps *
gst_polypsink_sink_fixate (GstPad * pad, const GstCaps * caps)
{
  GstCaps *newcaps;
  GstStructure *structure;

  newcaps =
      gst_caps_new_full (gst_structure_copy (gst_caps_get_structure (caps, 0)),
      NULL);
  structure = gst_caps_get_structure (newcaps, 0);

  if (gst_structure_fixate_field_nearest_int (structure, "rate", 44100) ||
      gst_structure_fixate_field_nearest_int (structure, "depth", 16) ||
      gst_structure_fixate_field_nearest_int (structure, "width", 16) ||
      gst_structure_fixate_field_nearest_int (structure, "channels", 2))
    return newcaps;

  gst_caps_free (newcaps);
  return NULL;
}

static void
gst_polypsink_init (GTypeInstance * instance, gpointer g_class)
{
  GstPolypSink *polypsink = GST_POLYPSINK (instance);

  polypsink->sinkpad =
      gst_pad_new_from_template (gst_element_class_get_pad_template
      (GST_ELEMENT_GET_CLASS (instance), "sink"), "sink");

  gst_element_add_pad (GST_ELEMENT (polypsink), polypsink->sinkpad);
  gst_pad_set_chain_function (polypsink->sinkpad, gst_polypsink_chain);
  gst_pad_set_link_function (polypsink->sinkpad, gst_polypsink_link);
  gst_pad_set_fixate_function (polypsink->sinkpad, gst_polypsink_sink_fixate);

/*     GST_OBJECT_FLAG_SET(polypsink, GST_ELEMENT_THREAD_SUGGESTED); */
  GST_OBJECT_FLAG_SET (polypsink, GST_ELEMENT_EVENT_AWARE);

  polypsink->context = NULL;
  polypsink->stream = NULL;

  polypsink->mainloop = pa_mainloop_new ();
  g_assert (polypsink->mainloop);

  polypsink->mainloop_api = pa_mainloop_get_api (polypsink->mainloop);

  polypsink->sample_spec.rate = 0;
  polypsink->sample_spec.channels = 0;
  polypsink->sample_spec.format = 0;

  polypsink->negotiated = 0;

  polypsink->buffer = NULL;
  polypsink->buffer_index = 0;

  polypsink->latency = 0;
  polypsink->counter = 0;
}

static void
gst_polypsink_dispose (GObject * object)
{
  GstPolypSink *polypsink = GST_POLYPSINK (object);

/*     gst_object_unparent(GST_OBJECT(polypsink->provided_clock)); */


  destroy_context (polypsink);

  if (polypsink->buffer)
    gst_buffer_unref (polypsink->buffer);


  g_free (polypsink->server);
  g_free (polypsink->sink);

  pa_mainloop_free (polypsink->mainloop);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_polypsink_class_init (gpointer g_class, gpointer class_data)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (g_class);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (g_class);

  parent_class = g_type_class_peek_parent (g_class);

  g_object_class_install_property (gobject_class, ARG_SERVER,
      g_param_spec_string ("server", "server", "server", NULL,
          G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_SINK,
      g_param_spec_string ("sink", "sink", "sink", NULL, G_PARAM_READWRITE));

  gobject_class->set_property = gst_polypsink_set_property;
  gobject_class->get_property = gst_polypsink_get_property;
  gobject_class->dispose = gst_polypsink_dispose;

  gstelement_class->change_state = gst_polypsink_change_state;
/*     gstelement_class->set_clock = gst_polypsink_set_clock;  */
/*     gstelement_class->get_clock = gst_polypsink_get_clock;  */
}


GType
gst_polypsink_get_type (void)
{
  static GType polypsink_type = 0;

  if (!polypsink_type) {

    static const GTypeInfo polypsink_info = {
      sizeof (GstPolypSinkClass),
      gst_polypsink_base_init,
      NULL,
      gst_polypsink_class_init,
      NULL,
      NULL,
      sizeof (GstPolypSink),
      0,
      gst_polypsink_init,
    };

    polypsink_type =
        g_type_register_static (GST_TYPE_ELEMENT, "GstPolypSink",
        &polypsink_info, 0);
  }

  return polypsink_type;
}
