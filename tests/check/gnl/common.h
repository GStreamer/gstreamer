
#include <gst/check/gstcheck.h>

#define fail_error_message(msg)     \
  G_STMT_START {        \
    GError *error;        \
    gst_message_parse_error(msg, &error, NULL);       \
    fail_unless(FALSE, "Error Message from %s : %s",      \
    GST_OBJECT_NAME (GST_MESSAGE_SRC(msg)), error->message); \
    g_error_free (error);           \
  } G_STMT_END;

#define check_start_stop_duration(object, startval, stopval, durval)  \
  G_STMT_START { guint64 start, stop;         \
    gint64 duration;              \
    GST_DEBUG_OBJECT (object, "Checking for valid start/stop/duration values");         \
    g_object_get (object, "start", &start, "stop", &stop,   \
      "duration", &duration, NULL);       \
    fail_unless_equals_uint64(start, startval);       \
    fail_unless_equals_uint64(stop, stopval);       \
    fail_unless_equals_int64(duration, durval);       \
    GST_DEBUG_OBJECT (object, "start/stop/duration values valid");  \
  } G_STMT_END;

#define check_state_simple(object, expected_state)      \
  G_STMT_START {              \
    GstStateChangeReturn ret;           \
    GstState state, pending;            \
    ret = gst_element_get_state(GST_ELEMENT_CAST(object), &state, &pending, 5 * GST_SECOND); \
    fail_if (ret == GST_STATE_CHANGE_FAILURE);        \
    fail_unless (state == expected_state, "Element state (%s) is not the expected one (%s)", \
     gst_element_state_get_name(state), gst_element_state_get_name(expected_state)); \
  } G_STMT_END;

typedef struct _Segment {
  gdouble rate;
  GstFormat format;
  guint64 start, stop, position;
} Segment;

typedef struct _CollectStructure {
  GstElement  *comp;
  GstElement  *sink;
  guint64 last_time;
  gboolean  gotsegment;
  GList         *seen_segments;
  GList   *expected_segments;
  guint64 expected_base;

  gboolean keep_expected_segments;
} CollectStructure;

void poll_the_bus(GstBus *bus);
void composition_pad_added_cb (GstElement *composition, GstPad *pad, CollectStructure * collect);
GstPadProbeReturn sinkpad_probe (GstPad *sinkpad, GstPadProbeInfo * info, CollectStructure * collect);
GstElement *videotest_gnl_src (const gchar * name, guint64 start, gint64 duration,
			       gint pattern, guint priority);
GstElement * videotest_gnl_src_full (const gchar * name, guint64 start, gint64 duration,
				     guint64 inpoint,
				     gint pattern, guint priority);
GstElement *
videotest_in_bin_gnl_src (const gchar * name, guint64 start, gint64 duration, gint pattern, guint priority);
GstElement *
audiotest_bin_src (const gchar * name, guint64 start,
		   gint64 duration, guint priority, gboolean intaudio);
GstElement *
new_operation (const gchar * name, const gchar * factory, guint64 start, gint64 duration, guint priority);
GList *
copy_segment_list (GList *list);
GstElement *
gst_element_factory_make_or_warn (const gchar * factoryname, const gchar * name);
Segment *
segment_new (gdouble rate, GstFormat format, gint64 start, gint64 stop, gint64 position);

void commit_and_wait (GstElement *comp, gboolean *ret);
