
/* Generated data (by glib-mkenums) */

#include <gst/gst.h>

/* enumerations from "/usr/include/gst/gstautoplug.h" */
GType
gst_autoplug_flags_get_type (void)
{
  static GType etype = 0;
  if (etype == 0) {
    static const GEnumValue values[] = {
      { GST_AUTOPLUG_TO_CAPS, "GST_AUTOPLUG_TO_CAPS", "to-caps" },
      { GST_AUTOPLUG_TO_RENDERER, "GST_AUTOPLUG_TO_RENDERER", "to-renderer" },
      { GST_AUTOPLUG_FLAG_LAST, "GST_AUTOPLUG_FLAG_LAST", "flag-last" },
      { 0, NULL, NULL }
    };
    etype = g_enum_register_static ("GstAutoplugFlags", values);
  }
  return etype;
}


/* enumerations from "/usr/include/gst/gstbin.h" */
GType
gst_bin_flags_get_type (void)
{
  static GType etype = 0;
  if (etype == 0) {
    static const GEnumValue values[] = {
      { GST_BIN_FLAG_MANAGER, "GST_BIN_FLAG_MANAGER", "flag-manager" },
      { GST_BIN_SELF_SCHEDULABLE, "GST_BIN_SELF_SCHEDULABLE", "self-schedulable" },
      { GST_BIN_FLAG_PREFER_COTHREADS, "GST_BIN_FLAG_PREFER_COTHREADS", "flag-prefer-cothreads" },
      { GST_BIN_FLAG_FIXED_CLOCK, "GST_BIN_FLAG_FIXED_CLOCK", "flag-fixed-clock" },
      { GST_BIN_FLAG_LAST, "GST_BIN_FLAG_LAST", "flag-last" },
      { 0, NULL, NULL }
    };
    etype = g_enum_register_static ("GstBinFlags", values);
  }
  return etype;
}


/* enumerations from "/usr/include/gst/gstbuffer.h" */
GType
gst_buffer_flags_get_type (void)
{
  static GType etype = 0;
  if (etype == 0) {
    static const GEnumValue values[] = {
      { GST_BUFFER_READONLY, "GST_BUFFER_READONLY", "readonly" },
      { GST_BUFFER_ORIGINAL, "GST_BUFFER_ORIGINAL", "original" },
      { GST_BUFFER_DONTFREE, "GST_BUFFER_DONTFREE", "dontfree" },
      { 0, NULL, NULL }
    };
    etype = g_enum_register_static ("GstBufferFlags", values);
  }
  return etype;
}


/* enumerations from "/usr/include/gst/gstclock.h" */
GType
gst_clock_return_get_type (void)
{
  static GType etype = 0;
  if (etype == 0) {
    static const GEnumValue values[] = {
      { GST_CLOCK_STOPPED, "GST_CLOCK_STOPPED", "stopped" },
      { GST_CLOCK_TIMEOUT, "GST_CLOCK_TIMEOUT", "timeout" },
      { GST_CLOCK_EARLY, "GST_CLOCK_EARLY", "early" },
      { GST_CLOCK_ERROR, "GST_CLOCK_ERROR", "error" },
      { 0, NULL, NULL }
    };
    etype = g_enum_register_static ("GstClockReturn", values);
  }
  return etype;
}


/* enumerations from "/usr/include/gst/gstcpu.h" */
GType
gst_cpu_flags_get_type (void)
{
  static GType etype = 0;
  if (etype == 0) {
    static const GFlagsValue values[] = {
      { GST_CPU_FLAG_MMX, "GST_CPU_FLAG_MMX", "mmx" },
      { GST_CPU_FLAG_SSE, "GST_CPU_FLAG_SSE", "sse" },
      { GST_CPU_FLAG_MMXEXT, "GST_CPU_FLAG_MMXEXT", "mmxext" },
      { GST_CPU_FLAG_3DNOW, "GST_CPU_FLAG_3DNOW", "3dnow" },
      { 0, NULL, NULL }
    };
    etype = g_flags_register_static ("GstCPUFlags", values);
  }
  return etype;
}


/* enumerations from "/usr/include/gst/gstelement.h" */
GType
gst_element_flags_get_type (void)
{
  static GType etype = 0;
  if (etype == 0) {
    static const GEnumValue values[] = {
      { GST_ELEMENT_COMPLEX, "GST_ELEMENT_COMPLEX", "complex" },
      { GST_ELEMENT_DECOUPLED, "GST_ELEMENT_DECOUPLED", "decoupled" },
      { GST_ELEMENT_THREAD_SUGGESTED, "GST_ELEMENT_THREAD_SUGGESTED", "thread-suggested" },
      { GST_ELEMENT_NO_SEEK, "GST_ELEMENT_NO_SEEK", "no-seek" },
      { GST_ELEMENT_INFINITE_LOOP, "GST_ELEMENT_INFINITE_LOOP", "infinite-loop" },
      { GST_ELEMENT_SCHEDULER_PRIVATE1, "GST_ELEMENT_SCHEDULER_PRIVATE1", "scheduler-private1" },
      { GST_ELEMENT_SCHEDULER_PRIVATE2, "GST_ELEMENT_SCHEDULER_PRIVATE2", "scheduler-private2" },
      { GST_ELEMENT_NEW_LOOPFUNC, "GST_ELEMENT_NEW_LOOPFUNC", "new-loopfunc" },
      { GST_ELEMENT_EVENT_AWARE, "GST_ELEMENT_EVENT_AWARE", "event-aware" },
      { GST_ELEMENT_FLAG_LAST, "GST_ELEMENT_FLAG_LAST", "flag-last" },
      { 0, NULL, NULL }
    };
    etype = g_enum_register_static ("GstElementFlags", values);
  }
  return etype;
}


/* enumerations from "/usr/include/gst/gstevent.h" */
GType
gst_event_type_get_type (void)
{
  static GType etype = 0;
  if (etype == 0) {
    static const GEnumValue values[] = {
      { GST_EVENT_UNKNOWN, "GST_EVENT_UNKNOWN", "unknown" },
      { GST_EVENT_EOS, "GST_EVENT_EOS", "eos" },
      { GST_EVENT_FLUSH, "GST_EVENT_FLUSH", "flush" },
      { GST_EVENT_EMPTY, "GST_EVENT_EMPTY", "empty" },
      { GST_EVENT_SEEK, "GST_EVENT_SEEK", "seek" },
      { GST_EVENT_DISCONTINUOUS, "GST_EVENT_DISCONTINUOUS", "discontinuous" },
      { GST_EVENT_NEW_MEDIA, "GST_EVENT_NEW_MEDIA", "new-media" },
      { GST_EVENT_INFO, "GST_EVENT_INFO", "info" },
      { GST_EVENT_ERROR, "GST_EVENT_ERROR", "error" },
      { 0, NULL, NULL }
    };
    etype = g_enum_register_static ("GstEventType", values);
  }
  return etype;
}

GType
gst_seek_type_get_type (void)
{
  static GType etype = 0;
  if (etype == 0) {
    static const GEnumValue values[] = {
      { GST_SEEK_ANY, "GST_SEEK_ANY", "any" },
      { GST_SEEK_TIMEOFFSET_SET, "GST_SEEK_TIMEOFFSET_SET", "timeoffset-set" },
      { GST_SEEK_BYTEOFFSET_SET, "GST_SEEK_BYTEOFFSET_SET", "byteoffset-set" },
      { GST_SEEK_BYTEOFFSET_CUR, "GST_SEEK_BYTEOFFSET_CUR", "byteoffset-cur" },
      { GST_SEEK_BYTEOFFSET_END, "GST_SEEK_BYTEOFFSET_END", "byteoffset-end" },
      { 0, NULL, NULL }
    };
    etype = g_enum_register_static ("GstSeekType", values);
  }
  return etype;
}


/* enumerations from "/usr/include/gst/gstobject.h" */
GType
gst_object_flags_get_type (void)
{
  static GType etype = 0;
  if (etype == 0) {
    static const GEnumValue values[] = {
      { GST_DESTROYED, "GST_DESTROYED", "destroyed" },
      { GST_FLOATING, "GST_FLOATING", "floating" },
      { GST_OBJECT_FLAG_LAST, "GST_OBJECT_FLAG_LAST", "object-flag-last" },
      { 0, NULL, NULL }
    };
    etype = g_enum_register_static ("GstObjectFlags", values);
  }
  return etype;
}


/* enumerations from "/usr/include/gst/gstpad.h" */
GType
gst_region_type_get_type (void)
{
  static GType etype = 0;
  if (etype == 0) {
    static const GEnumValue values[] = {
      { GST_REGION_VOID, "GST_REGION_VOID", "void" },
      { GST_REGION_OFFSET_LEN, "GST_REGION_OFFSET_LEN", "offset-len" },
      { GST_REGION_TIME_LEN, "GST_REGION_TIME_LEN", "time-len" },
      { 0, NULL, NULL }
    };
    etype = g_enum_register_static ("GstRegionType", values);
  }
  return etype;
}

GType
gst_pad_connect_return_get_type (void)
{
  static GType etype = 0;
  if (etype == 0) {
    static const GEnumValue values[] = {
      { GST_PAD_CONNECT_REFUSED, "GST_PAD_CONNECT_REFUSED", "refused" },
      { GST_PAD_CONNECT_DELAYED, "GST_PAD_CONNECT_DELAYED", "delayed" },
      { GST_PAD_CONNECT_OK, "GST_PAD_CONNECT_OK", "ok" },
      { GST_PAD_CONNECT_DONE, "GST_PAD_CONNECT_DONE", "done" },
      { 0, NULL, NULL }
    };
    etype = g_enum_register_static ("GstPadConnectReturn", values);
  }
  return etype;
}

GType
gst_pad_direction_get_type (void)
{
  static GType etype = 0;
  if (etype == 0) {
    static const GEnumValue values[] = {
      { GST_PAD_UNKNOWN, "GST_PAD_UNKNOWN", "unknown" },
      { GST_PAD_SRC, "GST_PAD_SRC", "src" },
      { GST_PAD_SINK, "GST_PAD_SINK", "sink" },
      { 0, NULL, NULL }
    };
    etype = g_enum_register_static ("GstPadDirection", values);
  }
  return etype;
}

GType
gst_pad_flags_get_type (void)
{
  static GType etype = 0;
  if (etype == 0) {
    static const GEnumValue values[] = {
      { GST_PAD_DISABLED, "GST_PAD_DISABLED", "disabled" },
      { GST_PAD_EOS, "GST_PAD_EOS", "eos" },
      { GST_PAD_FLAG_LAST, "GST_PAD_FLAG_LAST", "flag-last" },
      { 0, NULL, NULL }
    };
    etype = g_enum_register_static ("GstPadFlags", values);
  }
  return etype;
}

GType
gst_pad_presence_get_type (void)
{
  static GType etype = 0;
  if (etype == 0) {
    static const GEnumValue values[] = {
      { GST_PAD_ALWAYS, "GST_PAD_ALWAYS", "always" },
      { GST_PAD_SOMETIMES, "GST_PAD_SOMETIMES", "sometimes" },
      { GST_PAD_REQUEST, "GST_PAD_REQUEST", "request" },
      { 0, NULL, NULL }
    };
    etype = g_enum_register_static ("GstPadPresence", values);
  }
  return etype;
}


/* enumerations from "/usr/include/gst/gstparse.h" */
GType
gst_parse_error_get_type (void)
{
  static GType etype = 0;
  if (etype == 0) {
    static const GEnumValue values[] = {
      { GST_PARSE_ERROR_SYNTAX, "GST_PARSE_ERROR_SYNTAX", "syntax" },
      { GST_PARSE_ERROR_NO_SUCH_ELEMENT, "GST_PARSE_ERROR_NO_SUCH_ELEMENT", "no-such-element" },
      { GST_PARSE_ERROR_NO_SUCH_PROPERTY, "GST_PARSE_ERROR_NO_SUCH_PROPERTY", "no-such-property" },
      { GST_PARSE_ERROR_CONNECT, "GST_PARSE_ERROR_CONNECT", "connect" },
      { 0, NULL, NULL }
    };
    etype = g_enum_register_static ("GstParseError", values);
  }
  return etype;
}


/* enumerations from "/usr/include/gst/gstprops.h" */
GType
gst_props_type_get_type (void)
{
  static GType etype = 0;
  if (etype == 0) {
    static const GEnumValue values[] = {
      { GST_PROPS_END_TYPE, "GST_PROPS_END_TYPE", "end-type" },
      { GST_PROPS_INVALID_TYPE, "GST_PROPS_INVALID_TYPE", "invalid-type" },
      { GST_PROPS_INT_TYPE, "GST_PROPS_INT_TYPE", "int-type" },
      { GST_PROPS_FLOAT_TYPE, "GST_PROPS_FLOAT_TYPE", "float-type" },
      { GST_PROPS_FOURCC_TYPE, "GST_PROPS_FOURCC_TYPE", "fourcc-type" },
      { GST_PROPS_BOOL_TYPE, "GST_PROPS_BOOL_TYPE", "bool-type" },
      { GST_PROPS_STRING_TYPE, "GST_PROPS_STRING_TYPE", "string-type" },
      { GST_PROPS_VAR_TYPE, "GST_PROPS_VAR_TYPE", "var-type" },
      { GST_PROPS_LIST_TYPE, "GST_PROPS_LIST_TYPE", "list-type" },
      { GST_PROPS_FLOAT_RANGE_TYPE, "GST_PROPS_FLOAT_RANGE_TYPE", "float-range-type" },
      { GST_PROPS_INT_RANGE_TYPE, "GST_PROPS_INT_RANGE_TYPE", "int-range-type" },
      { GST_PROPS_LAST_TYPE, "GST_PROPS_LAST_TYPE", "last-type" },
      { 0, NULL, NULL }
    };
    etype = g_enum_register_static ("GstPropsType", values);
  }
  return etype;
}


/* enumerations from "/usr/include/gst/gstscheduler.h" */
GType
gst_scheduler_flags_get_type (void)
{
  static GType etype = 0;
  if (etype == 0) {
    static const GEnumValue values[] = {
      { GST_SCHEDULER_FLAG_FIXED_CLOCK, "GST_SCHEDULER_FLAG_FIXED_CLOCK", "fixed-clock" },
      { GST_SCHEDULER_FLAG_LAST, "GST_SCHEDULER_FLAG_LAST", "last" },
      { 0, NULL, NULL }
    };
    etype = g_enum_register_static ("GstSchedulerFlags", values);
  }
  return etype;
}

GType
gst_scheduler_state_get_type (void)
{
  static GType etype = 0;
  if (etype == 0) {
    static const GEnumValue values[] = {
      { GST_SCHEDULER_STATE_NONE, "GST_SCHEDULER_STATE_NONE", "none" },
      { GST_SCHEDULER_STATE_RUNNING, "GST_SCHEDULER_STATE_RUNNING", "running" },
      { GST_SCHEDULER_STATE_STOPPED, "GST_SCHEDULER_STATE_STOPPED", "stopped" },
      { GST_SCHEDULER_STATE_ERROR, "GST_SCHEDULER_STATE_ERROR", "error" },
      { 0, NULL, NULL }
    };
    etype = g_enum_register_static ("GstSchedulerState", values);
  }
  return etype;
}


/* enumerations from "/usr/include/gst/gstthread.h" */
GType
gst_thread_state_get_type (void)
{
  static GType etype = 0;
  if (etype == 0) {
    static const GEnumValue values[] = {
      { GST_THREAD_STATE_STARTED, "GST_THREAD_STATE_STARTED", "state-started" },
      { GST_THREAD_STATE_SPINNING, "GST_THREAD_STATE_SPINNING", "state-spinning" },
      { GST_THREAD_STATE_REAPING, "GST_THREAD_STATE_REAPING", "state-reaping" },
      { GST_THREAD_FLAG_LAST, "GST_THREAD_FLAG_LAST", "flag-last" },
      { 0, NULL, NULL }
    };
    etype = g_enum_register_static ("GstThreadState", values);
  }
  return etype;
}


/* enumerations from "/usr/include/gst/gsttimecache.h" */
GType
gst_time_cache_certainty_get_type (void)
{
  static GType etype = 0;
  if (etype == 0) {
    static const GEnumValue values[] = {
      { GST_TIME_CACHE_UNKNOWN, "GST_TIME_CACHE_UNKNOWN", "unknown" },
      { GST_TIME_CACHE_CERTAIN, "GST_TIME_CACHE_CERTAIN", "certain" },
      { GST_TIME_CACHE_FUZZY_LOCATION, "GST_TIME_CACHE_FUZZY_LOCATION", "fuzzy-location" },
      { GST_TIME_CACHE_FUZZY_TIMESTAMP, "GST_TIME_CACHE_FUZZY_TIMESTAMP", "fuzzy-timestamp" },
      { GST_TIME_CACHE_FUZZY, "GST_TIME_CACHE_FUZZY", "fuzzy" },
      { 0, NULL, NULL }
    };
    etype = g_enum_register_static ("GstTimeCacheCertainty", values);
  }
  return etype;
}


/* enumerations from "/usr/include/gst/gsttypes.h" */
GType
gst_element_state_get_type (void)
{
  static GType etype = 0;
  if (etype == 0) {
    static const GFlagsValue values[] = {
      { GST_STATE_VOID_PENDING, "GST_STATE_VOID_PENDING", "void-pending" },
      { GST_STATE_NULL, "GST_STATE_NULL", "null" },
      { GST_STATE_READY, "GST_STATE_READY", "ready" },
      { GST_STATE_PAUSED, "GST_STATE_PAUSED", "paused" },
      { GST_STATE_PLAYING, "GST_STATE_PLAYING", "playing" },
      { 0, NULL, NULL }
    };
    etype = g_flags_register_static ("GstElementState", values);
  }
  return etype;
}

GType
gst_element_state_return_get_type (void)
{
  static GType etype = 0;
  if (etype == 0) {
    static const GEnumValue values[] = {
      { GST_STATE_FAILURE, "GST_STATE_FAILURE", "failure" },
      { GST_STATE_SUCCESS, "GST_STATE_SUCCESS", "success" },
      { GST_STATE_ASYNC, "GST_STATE_ASYNC", "async" },
      { 0, NULL, NULL }
    };
    etype = g_enum_register_static ("GstElementStateReturn", values);
  }
  return etype;
}


/* Generated data ends here */

